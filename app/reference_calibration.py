from __future__ import annotations

import hashlib
import io
import json
import os
import time

import numpy as np


class EmptyReferenceAccumulator:
    def __init__(self, config: dict) -> None:
        camera = config["camera"]
        roi = config["roi"]
        reference = config["reference"]
        self.frame_width = int(camera["width"])
        self.frame_height = int(camera["height"])
        self.roi_x = int(roi["x"])
        self.roi_y = int(roi["y"])
        self.roi_w = int(roi["width"])
        self.roi_h = int(roi["height"])
        self.target_frames = int(reference["capture_frames"])
        if self.target_frames <= 0:
            raise ValueError("reference capture_frames must be positive")
        if (
            self.roi_x < 0
            or self.roi_y < 0
            or self.roi_x + self.roi_w > self.frame_width
            or self.roi_y + self.roi_h > self.frame_height
        ):
            raise ValueError("reference ROI is outside the frame")
        self._accumulator = np.zeros((self.roi_h, self.roi_w), dtype=np.uint32)
        self.captured_frames = 0
        self.started_at = time.monotonic()

    @property
    def complete(self) -> bool:
        return self.captured_frames >= self.target_frames

    @property
    def progress(self) -> float:
        return min(1.0, self.captured_frames / self.target_frames)

    def add(self, gray: np.ndarray) -> None:
        if self.complete:
            raise RuntimeError("reference accumulator is already complete")
        if gray.dtype != np.uint8 or gray.shape != (
            self.frame_height,
            self.frame_width,
        ):
            raise ValueError(
                f"invalid frame dtype={gray.dtype} shape={gray.shape}; expected "
                f"uint8 ({self.frame_height}, {self.frame_width})"
            )
        roi = gray[
            self.roi_y : self.roi_y + self.roi_h,
            self.roi_x : self.roi_x + self.roi_w,
        ]
        self._accumulator += roi
        self.captured_frames += 1

    def reference(self) -> np.ndarray:
        if not self.complete:
            raise RuntimeError("reference capture is incomplete")
        return (self._accumulator // self.captured_frames).astype(np.uint8)


def npy_bytes(array: np.ndarray) -> bytes:
    buffer = io.BytesIO()
    np.save(buffer, array, allow_pickle=False)
    return buffer.getvalue()


def atomic_write_bytes(path: str, payload: bytes) -> None:
    incoming = path + ".incoming"
    with open(incoming, "wb") as handle:
        handle.write(payload)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(incoming, path)


def next_calibration_id(archive_directory: str) -> str:
    base = time.strftime("%Y%m%d_%H%M%S", time.localtime())
    calibration_id = base
    suffix = 1
    while os.path.exists(os.path.join(archive_directory, calibration_id + ".npy")):
        calibration_id = f"{base}_{suffix:02d}"
        suffix += 1
    return calibration_id


def save_reference(
    reference: np.ndarray,
    config: dict,
    elapsed_s: float,
    temperature_start_mc,
    temperature_end_mc,
    source: str,
    calibration_id: str | None = None,
    raw_jpeg: bytes | None = None,
) -> dict:
    roi = config["roi"]
    camera = config["camera"]
    reference_config = config["reference"]
    roi_w = int(roi["width"])
    roi_h = int(roi["height"])
    if reference.dtype != np.uint8 or reference.shape != (roi_h, roi_w):
        raise ValueError(
            f"invalid reference dtype={reference.dtype} shape={reference.shape}"
        )

    reference_path = str(reference_config["path"])
    output_directory = os.path.dirname(reference_path)
    archive_directory = os.path.join(output_directory, "calibrations")
    os.makedirs(output_directory, exist_ok=True)
    os.makedirs(archive_directory, exist_ok=True)
    if calibration_id is None:
        calibration_id = next_calibration_id(archive_directory)

    raw_reference = reference.tobytes(order="C")
    reference_npy = npy_bytes(reference)
    reference_pgm = f"P5\n{roi_w} {roi_h}\n255\n".encode("ascii") + raw_reference
    config_payload = json.dumps(
        config, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    previous_reference = None
    if os.path.isfile(reference_path):
        with open(reference_path, "rb") as handle:
            previous_reference = handle.read()

    metadata = {
        "schema_version": 1,
        "kind": "empty_pipe_reference",
        "source": source,
        "calibration_id": calibration_id,
        "resolution": [int(camera["width"]), int(camera["height"])],
        "roi": [int(roi["x"]), int(roi["y"]), roi_w, roi_h],
        "reference_frames": int(reference_config["capture_frames"]),
        "elapsed_s": float(elapsed_s),
        "temperature_start_c": (
            None if temperature_start_mc is None else temperature_start_mc / 1000.0
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
        "config_sha256": hashlib.sha256(config_payload).hexdigest(),
        "previous_reference_npy_sha256": (
            None
            if previous_reference is None
            else hashlib.sha256(previous_reference).hexdigest()
        ),
    }
    if raw_jpeg is not None:
        metadata["raw_jpeg_sha256"] = hashlib.sha256(raw_jpeg).hexdigest()
    metadata_bytes = (
        json.dumps(metadata, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")

    archive_prefix = os.path.join(archive_directory, calibration_id)
    if previous_reference is not None:
        atomic_write_bytes(archive_prefix + "_previous.npy", previous_reference)
    atomic_write_bytes(archive_prefix + ".npy", reference_npy)
    atomic_write_bytes(archive_prefix + ".pgm", reference_pgm)
    atomic_write_bytes(archive_prefix + ".json", metadata_bytes)
    if raw_jpeg is not None:
        atomic_write_bytes(archive_prefix + "_raw.jpg", raw_jpeg)

    base_path = os.path.splitext(reference_path)[0]
    atomic_write_bytes(base_path + ".pgm", reference_pgm)
    atomic_write_bytes(base_path + ".json", metadata_bytes)
    if raw_jpeg is not None:
        atomic_write_bytes(base_path + "_raw.jpg", raw_jpeg)
    atomic_write_bytes(reference_path, reference_npy)
    return metadata
