from __future__ import annotations

import argparse
import json
import os
import signal
import time

import numpy as np
from maix import camera, err, image, pinmap, uart

from ball_detector import BallDetector
from ball_uart_protocol import (
    AlphaBetaTracker,
    FLAG_CALIBRATION_VALID,
    FLAG_DETECTED,
    FLAG_LOW_CONFIDENCE,
    FLAG_PREDICTED,
    FLAG_REFERENCE_MISMATCH,
    FLAG_TEMPERATURE_WARNING,
    FLAG_VELOCITY_VALID,
    build_state_packet,
)
from preview_encoder import PreviewEncoder
from status_server import StatusServer


PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CONFIG_PATH = os.path.join(PROJECT_ROOT, "config", "ball_detection.json")
stop_requested = False


def request_stop(_signum, _frame) -> None:
    global stop_requested
    stop_requested = True


signal.signal(signal.SIGTERM, request_stop)
signal.signal(signal.SIGINT, request_stop)


def load_config(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        config = json.load(handle)
    if config.get("schema_version") != 1:
        raise ValueError("unsupported configuration schema")
    return config


def read_temperature_mc(path: str):
    try:
        with open(path, "r", encoding="ascii") as handle:
            return int(handle.read().strip())
    except (OSError, ValueError):
        return None


def frame_to_gray(frame, width: int, height: int) -> np.ndarray:
    gray = image.image2cv(frame, ensure_bgr=False, copy=False)
    if gray.ndim == 3 and gray.shape[2] == 1:
        gray = gray[:, :, 0]
    if gray.dtype != np.uint8 or gray.shape != (height, width):
        raise RuntimeError(
            f"unexpected grayscale frame dtype={gray.dtype} shape={gray.shape}"
        )
    return gray


def open_control_uart(uart_config: dict):
    tx_pin = str(uart_config["tx_pin"])
    rx_pin = str(uart_config["rx_pin"])
    err.check_raise(
        pinmap.set_pin_function(tx_pin, "UART1_TX"),
        f"failed to map {tx_pin} to UART1_TX",
    )
    err.check_raise(
        pinmap.set_pin_function(rx_pin, "UART1_RX"),
        f"failed to map {rx_pin} to UART1_RX",
    )
    return uart.UART(str(uart_config["device"]), int(uart_config["baud_rate"]))


def close_control_uart(serial_port) -> None:
    if serial_port is None:
        return
    try:
        serial_port.close()
    except Exception:
        pass


def run(config_path: str) -> int:
    config = load_config(config_path)
    camera_config = config["camera"]
    detector_config = config["detector"]
    calibration = config["calibration"]
    tracker_config = config["tracker"]
    uart_config = config["uart"]
    reference_config = config["reference"]
    status_config = config["status_server"]
    thermal = config["thermal"]

    width = int(camera_config["width"])
    height = int(camera_config["height"])
    requested_fps = float(camera_config["fps"])
    reference = np.load(str(reference_config["path"]), allow_pickle=False)
    detector = BallDetector(config, reference)
    origin_x_px = detector.origin_x
    tracker = AlphaBetaTracker(
        alpha=float(tracker_config["alpha"]),
        beta=float(tracker_config["beta"]),
        velocity_valid_measurements=int(
            tracker_config["velocity_valid_measurements"]
        ),
        reset_after_s=float(tracker_config["reset_after_s"]),
    )

    temperature_path = str(thermal["path"])
    warning_mc = int(thermal["warning_mc"])
    stop_mc = int(thermal["stop_mc"])
    temperature_mc = read_temperature_mc(temperature_path)
    if temperature_mc is not None and temperature_mc >= stop_mc:
        raise RuntimeError(
            f"temperature too high to start: {temperature_mc / 1000.0:.3f} C"
        )

    cam = None
    serial_port = None
    status_server = None
    preview_encoder = None
    frames = 0
    uart_packets = 0
    uart_errors = 0
    sequence = 0
    next_uart_open_at = 0.0
    report_every_frames = int(config["logging"]["report_every_frames"])
    latest_result = None
    latest_control = None
    control_hz = 0.0
    uart_hz = 0.0
    last_control_at = None
    last_uart_at = None
    next_preview_at = 0.0
    preview_fps = float(status_config.get("preview_fps", 0.0))
    preview_interval_s = 1.0 / preview_fps if preview_fps > 0.0 else 0.0

    try:
        cam = camera.Camera(
            width=width,
            height=height,
            format=image.Format.FMT_GRAYSCALE,
            fps=requested_fps,
            buff_num=int(camera_config["buffer_count"]),
            open=True,
        )
        if not cam.is_opened():
            raise RuntimeError("camera did not open")
        cam.hmirror(int(camera_config["hmirror"]))
        cam.vflip(int(camera_config["vflip"]))
        cam.skip_frames(int(camera_config["warmup_frames"]))

        alignment_frame = cam.read(block=True, block_ms=1000)
        if alignment_frame is None:
            raise RuntimeError("could not read alignment frame")
        alignment_gray = frame_to_gray(alignment_frame, width, height)
        vertical_shift, alignment_cost = detector.estimate_vertical_shift(
            alignment_gray
        )

        try:
            serial_port = open_control_uart(uart_config)
            print(
                f"uart_opened device={uart_config['device']} "
                f"baud={uart_config['baud_rate']} tx={uart_config['tx_pin']} "
                f"rx={uart_config['rx_pin']}",
                flush=True,
            )
        except Exception as exc:
            uart_errors += 1
            next_uart_open_at = time.monotonic() + float(
                uart_config["retry_interval_s"]
            )
            print(f"uart_open_error count={uart_errors} error={exc!r}", flush=True)

        if bool(status_config["enabled"]):
            try:
                status_server = StatusServer(int(status_config["port"]))
                status_server.start()
                print(f"status_server_started port={status_server.port}", flush=True)
            except Exception as exc:
                status_server = None
                print(f"status_server_error error={exc!r}", flush=True)

        if status_server is not None and preview_fps > 0.0:
            try:
                preview_encoder = PreviewEncoder(
                    status_server=status_server,
                    image_module=image,
                    roi_x=detector.roi_x,
                    roi_w=detector.roi_w,
                    roi_h=detector.roi_h,
                    width=int(status_config["preview_width"]),
                    height=int(status_config["preview_height"]),
                    jpeg_quality=int(status_config["jpeg_quality"]),
                )
                preview_encoder.start()
                print(
                    f"preview_encoder_started fps={preview_fps:.2f} "
                    f"size={status_config['preview_width']}x"
                    f"{status_config['preview_height']} "
                    f"quality={status_config['jpeg_quality']}",
                    flush=True,
                )
            except Exception as exc:
                preview_encoder = None
                print(f"preview_encoder_error error={exc!r}", flush=True)

        report_started = time.monotonic()
        report_frames = 0
        report_uart_packets = 0
        print(
            f"ball_control_started requested_fps={requested_fps:.1f} "
            f"actual_fps={cam.fps()} resolution={width}x{height} "
            f"roi=({detector.roi_x},{detector.roi_y},"
            f"{detector.roi_w},{detector.roi_h}) "
            f"vertical_shift={vertical_shift} alignment_cost={alignment_cost:.3f} "
            f"origin_x_px={origin_x_px:.3f} "
            f"mm_per_pixel={float(calibration['mm_per_pixel']):.8f}",
            flush=True,
        )

        while not stop_requested:
            frame = cam.read(block=True, block_ms=1000)
            if frame is None:
                continue
            capture_monotonic_s = time.monotonic()
            capture_time_ms = int(capture_monotonic_s * 1000.0) & 0xFFFFFFFF
            gray = frame_to_gray(frame, width, height)
            latest_result = detector.detect(gray, vertical_shift, alignment_cost)
            frames += 1
            report_frames += 1

            if frames % int(thermal["check_every_frames"]) == 0:
                temperature_mc = read_temperature_mc(temperature_path)
                if temperature_mc is not None and temperature_mc >= stop_mc:
                    print(
                        f"thermal_stop temperature_c={temperature_mc / 1000.0:.3f}",
                        flush=True,
                    )
                    break

            ball_x_px = None
            ball_offset_px = None
            ball_offset_mm = None
            if latest_result["reference_mismatch"]:
                tracker.reset()
                tracked_state = None
                predicted = False
            elif latest_result["detected"]:
                ball_x_px = detector.roi_x + latest_result["center_x"]
                ball_offset_px = float(latest_result["offset_x"])
                ball_offset_mm = (
                    float(calibration["position_sign"])
                    * ball_offset_px
                    * float(calibration["mm_per_pixel"])
                )
                measured_offset_mm = ball_offset_mm
                tracked_state = tracker.update(measured_offset_mm, capture_monotonic_s)
                predicted = False
            else:
                tracked_state = tracker.predict(capture_monotonic_s)
                predicted = tracked_state is not None

            if tracked_state is None:
                center_offset_mm = 0.0
                velocity_mm_s = 0.0
                velocity_valid = False
            else:
                center_offset_mm, velocity_mm_s, velocity_valid = tracked_state

            flags = 0
            if latest_result["detected"]:
                flags |= FLAG_DETECTED
            if velocity_valid:
                flags |= FLAG_VELOCITY_VALID
            if predicted:
                flags |= FLAG_PREDICTED
            if (
                latest_result["detected"]
                and latest_result["confidence"]
                < float(detector_config["low_confidence_threshold"])
            ):
                flags |= FLAG_LOW_CONFIDENCE
            if latest_result["reference_mismatch"]:
                flags |= FLAG_REFERENCE_MISMATCH
            if bool(calibration["valid"]):
                flags |= FLAG_CALIBRATION_VALID
            if temperature_mc is not None and temperature_mc >= warning_mc:
                flags |= FLAG_TEMPERATURE_WARNING

            packet_created_at = time.monotonic()
            measurement_age_ms = tracker.measurement_age_ms(packet_created_at)
            packet = build_state_packet(
                seq=sequence,
                capture_time_ms=capture_time_ms,
                center_offset_mm=center_offset_mm,
                velocity_mm_s=velocity_mm_s,
                confidence=(
                    latest_result["confidence"] if latest_result["detected"] else 0.0
                ),
                age_ms=measurement_age_ms,
                flags=flags,
            )
            latest_control = {
                "center_offset_mm": center_offset_mm,
                "velocity_mm_s": velocity_mm_s,
                "age_ms": measurement_age_ms,
                "flags": flags,
            }
            sequence = (sequence + 1) & 0xFFFF

            if serial_port is None and packet_created_at >= next_uart_open_at:
                try:
                    serial_port = open_control_uart(uart_config)
                    print(
                        f"uart_reopened device={uart_config['device']} "
                        f"baud={uart_config['baud_rate']}",
                        flush=True,
                    )
                except Exception as exc:
                    uart_errors += 1
                    next_uart_open_at = packet_created_at + float(
                        uart_config["retry_interval_s"]
                    )
                    if uart_errors <= 3:
                        print(
                            f"uart_open_error count={uart_errors} error={exc!r}",
                            flush=True,
                        )

            if serial_port is not None:
                try:
                    written = serial_port.write(packet)
                    if written != len(packet):
                        raise RuntimeError(
                            f"short UART write expected={len(packet)} actual={written}"
                        )
                    uart_packets += 1
                    report_uart_packets += 1
                    uart_sent_at = time.monotonic()
                    if last_uart_at is not None and uart_sent_at > last_uart_at:
                        instant_uart_hz = min(120.0, 1.0 / (uart_sent_at - last_uart_at))
                        uart_hz = (
                            instant_uart_hz
                            if uart_hz <= 0.0
                            else 0.9 * uart_hz + 0.1 * instant_uart_hz
                        )
                    last_uart_at = uart_sent_at
                except Exception as exc:
                    uart_errors += 1
                    if uart_errors <= 3:
                        print(
                            f"uart_write_error count={uart_errors} error={exc!r}",
                            flush=True,
                        )
                    close_control_uart(serial_port)
                    serial_port = None
                    uart_hz = 0.0
                    last_uart_at = None
                    next_uart_open_at = time.monotonic() + float(
                        uart_config["retry_interval_s"]
                    )

            loop_completed_at = time.monotonic()
            if last_control_at is not None and loop_completed_at > last_control_at:
                instant_control_hz = min(
                    120.0, 1.0 / (loop_completed_at - last_control_at)
                )
                control_hz = (
                    instant_control_hz
                    if control_hz <= 0.0
                    else 0.9 * control_hz + 0.1 * instant_control_hz
                )
            last_control_at = loop_completed_at
            if serial_port is None:
                uart_hz = 0.0

            if (
                preview_encoder is not None
                and status_server is not None
                and status_server.preview_requested_recently()
                and loop_completed_at >= next_preview_at
            ):
                preview_encoder.submit(
                    gray.copy(),
                    dict(latest_result),
                    origin_x_px,
                    ball_offset_px,
                    ball_offset_mm,
                )
                next_preview_at = loop_completed_at + preview_interval_s

            if status_server is not None:
                preview_stats = (
                    preview_encoder.snapshot()
                    if preview_encoder is not None
                    else {
                        "preview_encoded": 0,
                        "preview_dropped": 0,
                        "preview_errors": 0,
                        "preview_encode_ms": 0.0,
                    }
                )
                status_server.update(
                    {
                        "control_hz": control_hz,
                        "uart_hz": uart_hz,
                        "detected": bool(latest_result["detected"]),
                        "reference_mismatch": bool(
                            latest_result["reference_mismatch"]
                        ),
                        "ball_x_px": ball_x_px,
                        "offset_px": ball_offset_px,
                        "offset_mm": ball_offset_mm,
                        "origin_x_px": origin_x_px,
                        "position_mm": center_offset_mm,
                        "velocity_mm_s": velocity_mm_s,
                        "velocity_valid": bool(velocity_valid),
                        "confidence": float(latest_result["confidence"]),
                        "temperature_c": (
                            None
                            if temperature_mc is None
                            else temperature_mc / 1000.0
                        ),
                        "flags": flags,
                        "uart_errors": uart_errors,
                        "frames": frames,
                        **preview_stats,
                    }
                )

            if report_frames >= report_every_frames:
                now = time.monotonic()
                elapsed = max(0.001, now - report_started)
                temperature_text = (
                    "NA"
                    if temperature_mc is None
                    else f"{temperature_mc / 1000.0:.3f}"
                )
                print(
                    f"frames={frames} detection_fps={report_frames / elapsed:.2f} "
                    f"uart_fps={report_uart_packets / elapsed:.2f} "
                    f"detected={int(latest_result['detected'])} "
                    f"reference_mismatch={int(latest_result['reference_mismatch'])} "
                    f"x_px={latest_result['center_x']:.3f} "
                    f"position_mm={latest_control['center_offset_mm']:.3f} "
                    f"velocity_mm_s={latest_control['velocity_mm_s']:.3f} "
                    f"age_ms={latest_control['age_ms']} "
                    f"flags=0x{latest_control['flags']:02X} "
                    f"confidence={latest_result['confidence']:.3f} "
                    f"changed_ratio={latest_result['changed_ratio']:.4f} "
                    f"temperature_c={temperature_text} uart_errors={uart_errors}",
                    flush=True,
                )
                report_started = now
                report_frames = 0
                report_uart_packets = 0

        return 0
    finally:
        if preview_encoder is not None:
            preview_encoder.stop()
        if status_server is not None:
            status_server.stop()
        close_control_uart(serial_port)
        if cam is not None:
            try:
                cam.close()
            except Exception:
                pass
        print(
            f"ball_control_stopped frames={frames} uart_packets={uart_packets} "
            f"uart_errors={uart_errors}",
            flush=True,
        )


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=DEFAULT_CONFIG_PATH)
    return parser.parse_args()


if __name__ == "__main__":
    arguments = parse_args()
    raise SystemExit(run(arguments.config))
