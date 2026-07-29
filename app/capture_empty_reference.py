from __future__ import annotations

import argparse
import hashlib
import json
import os
import time

import numpy as np
from maix import camera, image


PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CONFIG_PATH = os.path.join(PROJECT_ROOT, "config", "ball_detection.json")


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


def atomic_write_bytes(path: str, payload: bytes) -> None:
    incoming = path + ".incoming"
    with open(incoming, "wb") as handle:
        handle.write(payload)
    os.replace(incoming, path)


def npy_bytes(array: np.ndarray) -> bytes:
    import io

    buffer = io.BytesIO()
    np.save(buffer, array, allow_pickle=False)
    return buffer.getvalue()


def run(config_path: str) -> int:
    config = load_config(config_path)
    camera_config = config["camera"]
    roi = config["roi"]
    reference_config = config["reference"]
    thermal = config["thermal"]

    width = int(camera_config["width"])
    height = int(camera_config["height"])
    roi_x = int(roi["x"])
    roi_y = int(roi["y"])
    roi_w = int(roi["width"])
    roi_h = int(roi["height"])
    capture_frames = int(reference_config["capture_frames"])
    reference_path = str(reference_config["path"])
    output_directory = os.path.dirname(reference_path)
    archive_directory = os.path.join(output_directory, "calibrations")
    os.makedirs(output_directory, exist_ok=True)
    os.makedirs(archive_directory, exist_ok=True)

    temperature_path = str(thermal["path"])
    stop_mc = int(thermal["stop_mc"])
    temperature_start_mc = read_temperature_mc(temperature_path)
    if temperature_start_mc is not None and temperature_start_mc >= stop_mc:
        raise RuntimeError(
            f"temperature too high to capture: {temperature_start_mc / 1000.0:.3f} C"
        )

    cam = None
    started = time.monotonic()
    accumulator = np.zeros((roi_h, roi_w), dtype=np.uint32)
    captured = 0
    raw_jpeg = None
    try:
        cam = camera.Camera(
            width=width,
            height=height,
            format=image.Format.FMT_GRAYSCALE,
            fps=float(reference_config["capture_fps"]),
            buff_num=int(camera_config["buffer_count"]),
            open=True,
        )
        if not cam.is_opened():
            raise RuntimeError("camera did not open")
        cam.hmirror(int(camera_config["hmirror"]))
        cam.vflip(int(camera_config["vflip"]))
        cam.skip_frames(int(camera_config["warmup_frames"]))

        for index in range(capture_frames):
            temperature_mc = read_temperature_mc(temperature_path)
            if temperature_mc is not None and temperature_mc >= stop_mc:
                raise RuntimeError(
                    f"thermal stop during capture: {temperature_mc / 1000.0:.3f} C"
                )

            frame = cam.read(block=True, block_ms=1000)
            if frame is None:
                raise RuntimeError(f"camera timeout at reference frame {index}")
            gray = image.image2cv(frame, ensure_bgr=False, copy=False)
            if gray.ndim == 3 and gray.shape[2] == 1:
                gray = gray[:, :, 0]
            if gray.dtype != np.uint8 or gray.shape != (height, width):
                raise RuntimeError(
                    f"unexpected frame dtype={gray.dtype} shape={gray.shape}"
                )

            accumulator += gray[roi_y : roi_y + roi_h, roi_x : roi_x + roi_w]
            captured += 1
            if index == capture_frames - 1:
                jpeg = frame.to_jpeg(quality=90)
                raw_jpeg = bytes(jpeg.to_bytes(copy=True))

        reference = (accumulator // captured).astype(np.uint8)
        raw_reference = reference.tobytes(order="C")
        reference_npy = npy_bytes(reference)
        reference_pgm = f"P5\n{roi_w} {roi_h}\n255\n".encode("ascii") + raw_reference
        temperature_end_mc = read_temperature_mc(temperature_path)
        calibration_id = time.strftime("%Y%m%d_%H%M%S", time.localtime())
        config_payload = json.dumps(
            config, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
        metadata = {
            "schema_version": 1,
            "kind": "empty_pipe_reference",
            "calibration_id": calibration_id,
            "resolution": [width, height],
            "roi": [roi_x, roi_y, roi_w, roi_h],
            "fps_requested": float(reference_config["capture_fps"]),
            "fps_actual": cam.fps(),
            "reference_frames": captured,
            "elapsed_s": time.monotonic() - started,
            "temperature_start_c": (
                None
                if temperature_start_mc is None
                else temperature_start_mc / 1000.0
            ),
            "temperature_end_c": (
                None if temperature_end_mc is None else temperature_end_mc / 1000.0
            ),
            "gray_min": int(reference.min()),
            "gray_max": int(reference.max()),
            "gray_mean": float(reference.mean()),
            "gray_std": float(reference.std()),
            "reference_raw_sha256": hashlib.sha256(raw_reference).hexdigest(),
            "reference_npy_sha256": hashlib.sha256(reference_npy).hexdigest(),
            "raw_jpeg_sha256": hashlib.sha256(raw_jpeg).hexdigest(),
            "config_sha256": hashlib.sha256(config_payload).hexdigest(),
        }
        metadata_bytes = (
            json.dumps(metadata, indent=2, sort_keys=True) + "\n"
        ).encode("utf-8")

        archive_prefix = os.path.join(archive_directory, calibration_id)
        atomic_write_bytes(archive_prefix + ".npy", reference_npy)
        atomic_write_bytes(archive_prefix + ".pgm", reference_pgm)
        atomic_write_bytes(archive_prefix + "_raw.jpg", raw_jpeg)
        atomic_write_bytes(archive_prefix + ".json", metadata_bytes)

        base_path = os.path.splitext(reference_path)[0]
        atomic_write_bytes(reference_path, reference_npy)
        atomic_write_bytes(base_path + ".pgm", reference_pgm)
        atomic_write_bytes(base_path + "_raw.jpg", raw_jpeg)
        atomic_write_bytes(base_path + ".json", metadata_bytes)
        print(json.dumps(metadata, sort_keys=True), flush=True)
        return 0
    finally:
        if cam is not None:
            try:
                cam.close()
            except Exception:
                pass
        print(f"empty_reference_capture_stopped captured={captured}", flush=True)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=DEFAULT_CONFIG_PATH)
    return parser.parse_args()


if __name__ == "__main__":
    arguments = parse_args()
    raise SystemExit(run(arguments.config))
