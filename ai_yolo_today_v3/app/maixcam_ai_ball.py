from __future__ import annotations

import argparse
import json
import os
import signal
import time

from maix import camera, comm, err, image, nn, pinmap, uart

from ai_ball_detector import YoloBallSelector
from ball_uart_protocol import (
    AlphaBetaTracker,
    FLAG_CALIBRATION_VALID,
    FLAG_DETECTED,
    FLAG_LOW_CONFIDENCE,
    FLAG_TEMPERATURE_WARNING,
    FLAG_VELOCITY_VALID,
    build_state_packet,
)
from preview_encoder import PreviewEncoder
from status_server import StatusServer


PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CONFIG_PATH = os.path.join(PROJECT_ROOT, "config", "ai_ball.json")
stop_requested = False


def request_stop(_signum, _frame) -> None:
    global stop_requested
    stop_requested = True


signal.signal(signal.SIGTERM, request_stop)
signal.signal(signal.SIGINT, request_stop)


def load_config(path: str) -> dict:
    with open(path, "r", encoding="ascii") as handle:
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


def prepare_control_uart(config: dict) -> None:
    if int(config["bus"]) == 0 and not comm.rm_default_comm_listener():
        raise RuntimeError("failed to release Maix default UART0 listener")


def open_control_uart(config: dict):
    bus = int(config["bus"])
    tx_pin = str(config["tx_pin"])
    rx_pin = str(config["rx_pin"])
    err.check_raise(
        pinmap.set_pin_function(tx_pin, f"UART{bus}_TX"),
        f"failed to map {tx_pin} to UART{bus}_TX",
    )
    err.check_raise(
        pinmap.set_pin_function(rx_pin, f"UART{bus}_RX"),
        f"failed to map {rx_pin} to UART{bus}_RX",
    )
    return uart.UART(str(config["device"]), int(config["baud_rate"]))


def close_uart(serial_port) -> None:
    if serial_port is not None:
        try:
            serial_port.close()
        except Exception:
            pass


def smooth_rate(previous: float, timestamp_s: float, previous_time_s):
    if previous_time_s is None or timestamp_s <= previous_time_s:
        return previous, timestamp_s
    instant = min(120.0, 1.0 / (timestamp_s - previous_time_s))
    value = instant if previous <= 0.0 else 0.9 * previous + 0.1 * instant
    return value, timestamp_s


def run(config_path: str) -> int:
    config = load_config(config_path)
    camera_config = config["camera"]
    roi = config["roi"]
    detector_config = config["detector"]
    calibration = config["calibration"]
    tracker_config = config["tracker"]
    uart_config = config["uart"]
    status_config = config["status_server"]
    thermal = config["thermal"]

    model = nn.YOLO11(
        model=str(config["model"]["path"]),
        dual_buff=bool(config["model"]["dual_buffer"]),
    )
    input_width = int(model.input_width())
    input_height = int(model.input_height())
    if (input_width, input_height) != (
        int(config["model"]["input_width"]),
        int(config["model"]["input_height"]),
    ):
        raise RuntimeError(
            f"unexpected model input {input_width}x{input_height}"
        )

    selector = YoloBallSelector(config, input_width, input_height)
    tracker = AlphaBetaTracker(
        alpha=float(tracker_config["alpha"]),
        beta=float(tracker_config["beta"]),
        velocity_valid_measurements=int(
            tracker_config["velocity_valid_measurements"]
        ),
        reset_after_s=float(tracker_config["reset_after_s"]),
    )

    width = int(camera_config["width"])
    height = int(camera_config["height"])
    roi_x = int(roi["x"])
    roi_y = int(roi["y"])
    roi_w = int(roi["width"])
    roi_h = int(roi["height"])
    scale_x = roi_w / input_width
    scale_y = roi_h / input_height
    origin_x = float(calibration["center_x_px"])
    mm_per_pixel = float(calibration["mm_per_pixel"])
    position_sign = float(calibration["position_sign"])

    cam = None
    serial_port = None
    status_server = None
    preview_encoder = None
    uart_prepared = False
    next_uart_open_at = 0.0
    next_preview_at = 0.0
    preview_fps = float(status_config["preview_fps"])
    preview_interval_s = 1.0 / preview_fps if preview_fps > 0.0 else 0.0
    frames = 0
    uart_packets = 0
    uart_errors = 0
    sequence = 0
    detect_hz = 0.0
    uart_hz = 0.0
    last_detect_at = None
    last_uart_at = None
    temperature_mc = None
    inference_ms = 0.0
    preprocess_ms = 0.0
    latest_result = {
        "control_valid": False,
        "candidate": None,
        "position_mm": 0.0,
        "confidence": 0.0,
        "reason": "starting",
    }

    try:
        temperature_mc = read_temperature_mc(str(thermal["path"]))
        if temperature_mc is not None and temperature_mc >= int(thermal["stop_mc"]):
            raise RuntimeError("temperature is too high to start")

        cam = camera.Camera(
            width=width,
            height=height,
            format=model.input_format(),
            fps=float(camera_config["fps"]),
            buff_num=int(camera_config["buffer_count"]),
            open=True,
        )
        if not cam.is_opened():
            raise RuntimeError("camera did not open")
        cam.hmirror(int(camera_config["hmirror"]))
        cam.vflip(int(camera_config["vflip"]))
        cam.skip_frames(int(camera_config["warmup_frames"]))

        try:
            prepare_control_uart(uart_config)
            uart_prepared = True
            serial_port = open_control_uart(uart_config)
        except Exception as exc:
            uart_errors += 1
            next_uart_open_at = time.monotonic() + float(
                uart_config["retry_interval_s"]
            )
            print(f"uart_open_error error={exc!r}", flush=True)

        if bool(status_config["enabled"]):
            status_server = StatusServer(
                int(status_config["port"]), calibration_target_frames=0
            )
            status_server.start()
            if preview_fps > 0.0:
                preview_encoder = PreviewEncoder(status_server, image, config)
                preview_encoder.start()

        print(
            f"ai_ball_started camera={width}x{height}@{cam.fps()} "
            f"model={input_width}x{input_height} dual_buff="
            f"{int(config['model']['dual_buffer'])} roi="
            f"({roi_x},{roi_y},{roi_w},{roi_h}) uart=UART{uart_config['bus']} "
            f"tx={uart_config['tx_pin']} rx={uart_config['rx_pin']} "
            f"baud={uart_config['baud_rate']}",
            flush=True,
        )

        report_started = time.monotonic()
        report_frames = 0
        while not stop_requested:
            frame = cam.read(block=True, block_ms=1000)
            if frame is None:
                continue
            capture_s = time.monotonic()
            capture_ms = int(capture_s * 1000.0) & 0xFFFFFFFF

            preprocess_started = time.monotonic()
            model_image = frame.crop(roi_x, roi_y, roi_w, roi_h).resize(
                input_width, input_height
            )
            preprocess_ms = (time.monotonic() - preprocess_started) * 1000.0
            inference_started = time.monotonic()
            objects = model.detect(
                model_image,
                conf_th=float(detector_config["confidence_threshold"]),
                iou_th=float(detector_config["iou_threshold"]),
            )
            inference_ms = (time.monotonic() - inference_started) * 1000.0

            candidates = selector.candidates(objects)
            selected, reason, expected_x = selector.select(candidates, capture_s)
            preview_candidate = selected
            if preview_candidate is None and candidates:
                preview_candidate = max(
                    candidates, key=lambda candidate: candidate["confidence"]
                )

            control_valid = selected is not None
            confidence = 0.0 if selected is None else selected["confidence"]
            ball_x_px = None
            offset_px = None
            measured_mm = 0.0
            velocity_mm_s = 0.0
            velocity_valid = False
            if selected is not None:
                ball_x_px = roi_x + selected["center_x"] * scale_x
                offset_px = ball_x_px - origin_x
                measured_mm = position_sign * offset_px * mm_per_pixel
                measured_mm, velocity_mm_s, velocity_valid = tracker.update(
                    measured_mm, capture_s
                )

            flags = 0
            if control_valid:
                flags |= FLAG_DETECTED
            if velocity_valid:
                flags |= FLAG_VELOCITY_VALID
            if control_valid and confidence < float(
                detector_config["low_confidence_threshold"]
            ):
                flags |= FLAG_LOW_CONFIDENCE
            if bool(calibration["valid"]):
                flags |= FLAG_CALIBRATION_VALID
            if temperature_mc is not None and temperature_mc >= int(
                thermal["warning_mc"]
            ):
                flags |= FLAG_TEMPERATURE_WARNING

            packet_time = time.monotonic()
            packet = build_state_packet(
                seq=sequence,
                capture_time_ms=capture_ms,
                center_offset_mm=measured_mm if control_valid else 0.0,
                velocity_mm_s=velocity_mm_s if control_valid else 0.0,
                confidence=confidence if control_valid else 0.0,
                age_ms=(
                    tracker.measurement_age_ms(packet_time)
                    if control_valid
                    else 0xFFFF
                ),
                flags=flags,
            )
            sequence = (sequence + 1) & 0xFFFF

            if serial_port is None and packet_time >= next_uart_open_at:
                try:
                    if not uart_prepared:
                        prepare_control_uart(uart_config)
                        uart_prepared = True
                    serial_port = open_control_uart(uart_config)
                except Exception as exc:
                    uart_errors += 1
                    next_uart_open_at = packet_time + float(
                        uart_config["retry_interval_s"]
                    )
                    if uart_errors <= 3:
                        print(f"uart_reopen_error error={exc!r}", flush=True)

            if serial_port is not None:
                try:
                    written = serial_port.write(packet)
                    if written != len(packet):
                        raise RuntimeError(f"short UART write: {written}")
                    uart_packets += 1
                    uart_hz, last_uart_at = smooth_rate(
                        uart_hz, time.monotonic(), last_uart_at
                    )
                except Exception as exc:
                    uart_errors += 1
                    if uart_errors <= 3:
                        print(f"uart_write_error error={exc!r}", flush=True)
                    close_uart(serial_port)
                    serial_port = None
                    uart_hz = 0.0
                    last_uart_at = None
                    next_uart_open_at = time.monotonic() + float(
                        uart_config["retry_interval_s"]
                    )

            completed_s = time.monotonic()
            detect_hz, last_detect_at = smooth_rate(
                detect_hz, completed_s, last_detect_at
            )
            frames += 1
            report_frames += 1

            if frames % int(thermal["check_every_frames"]) == 0:
                temperature_mc = read_temperature_mc(str(thermal["path"]))
                if temperature_mc is not None and temperature_mc >= int(
                    thermal["stop_mc"]
                ):
                    print("thermal_stop", flush=True)
                    break

            mapped_candidate = None
            if preview_candidate is not None:
                mapped_candidate = dict(preview_candidate)
                mapped_candidate.update(
                    {
                        "preview_x": roi_x + preview_candidate["x"] * scale_x,
                        "preview_y": roi_y + preview_candidate["y"] * scale_y,
                        "preview_w": preview_candidate["w"] * scale_x,
                        "preview_h": preview_candidate["h"] * scale_y,
                    }
                )
            latest_result = {
                "control_valid": control_valid,
                "candidate": mapped_candidate,
                "position_mm": measured_mm if control_valid else 0.0,
                "confidence": confidence,
                "reason": reason,
            }

            if (
                preview_encoder is not None
                and status_server.preview_requested_recently()
                and completed_s >= next_preview_at
            ):
                preview_encoder.submit(frame.copy(), latest_result)
                next_preview_at = completed_s + preview_interval_s

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
                        "mode": "yolo11_today_v3",
                        "control_hz": detect_hz,
                        "detect_rate": detect_hz,
                        "control_valid": control_valid,
                        "uart_hz": uart_hz if serial_port is not None else 0.0,
                        "data_rate": uart_hz if serial_port is not None else 0.0,
                        "detected": control_valid,
                        "raw_detected": bool(candidates),
                        "reference_mismatch": False,
                        "measurement_rejected": bool(candidates) and not control_valid,
                        "rejection_reason": reason,
                        "candidate_count": len(candidates),
                        "raw_center_x": (
                            None
                            if preview_candidate is None
                            else preview_candidate["center_x"] * scale_x + roi_x
                        ),
                        "expected_center_x": (
                            None
                            if expected_x is None
                            else expected_x * scale_x + roi_x
                        ),
                        "filter_locked": selector.locked,
                        "rejected_jumps": selector.rejected_jumps,
                        "ambiguous_frames": selector.ambiguous_frames,
                        "ball_x_px": ball_x_px,
                        "offset_px": offset_px,
                        "offset_mm": measured_mm if control_valid else None,
                        "origin_x_px": origin_x,
                        "position_mm": measured_mm if control_valid else 0.0,
                        "velocity_mm_s": velocity_mm_s if control_valid else 0.0,
                        "velocity_valid": velocity_valid and control_valid,
                        "confidence": confidence,
                        "inference_ms": inference_ms,
                        "preprocess_ms": preprocess_ms,
                        "changed_ratio": 0.0,
                        "brightness_offset": 0,
                        "local_brightness_offset_max": 0,
                        "vertical_shift": 0,
                        "alignment_cost": 0.0,
                        "alignment_valid": True,
                        "alignment_slope": 0.0,
                        "alignment_end_to_end_px": 0.0,
                        "alignment_segments": 0,
                        "alignment_updates": 0,
                        "alignment_update_ms": 0.0,
                        "alignment_search_mode": "model",
                        "alignment_tracking_failures": 0,
                        "alignment_reacquires": 0,
                        "horizontal_shift": 0,
                        "marker_valid": True,
                        "marker_correlation": 1.0,
                        "marker_reference_x": 0.0,
                        "marker_updates": 0,
                        "marker_update_ms": 0.0,
                        "marker_tracking_failures": 0,
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

            if report_frames >= int(config["logging"]["report_every_frames"]):
                now = time.monotonic()
                elapsed = max(0.001, now - report_started)
                print(
                    f"frames={frames} detect_fps={report_frames / elapsed:.2f} "
                    f"detect_hz={detect_hz:.2f} uart_hz={uart_hz:.2f} "
                    f"inference_ms={inference_ms:.2f} "
                    f"preprocess_ms={preprocess_ms:.2f} "
                    f"valid={int(control_valid)} reason={reason} "
                    f"position_mm={latest_result['position_mm']:.2f} "
                    f"confidence={confidence:.3f} candidates={len(candidates)}",
                    flush=True,
                )
                report_started = now
                report_frames = 0
        return 0
    finally:
        if preview_encoder is not None:
            preview_encoder.stop()
        if status_server is not None:
            status_server.stop()
        close_uart(serial_port)
        if cam is not None:
            try:
                cam.close()
            except Exception:
                pass
        print(
            f"ai_ball_stopped frames={frames} uart_packets={uart_packets} "
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
