from __future__ import annotations

import queue
import threading
import time


class PreviewEncoder:
    def __init__(self, status_server, image_module, config: dict) -> None:
        self._status_server = status_server
        self._image = image_module
        self._config = config
        self._queue = queue.Queue(maxsize=1)
        self._stop = threading.Event()
        self._thread = None
        self.encoded = 0
        self.dropped = 0
        self.errors = 0
        self.last_encode_ms = 0.0

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def submit(self, frame, result: dict) -> None:
        item = (frame, dict(result))
        try:
            self._queue.put_nowait(item)
            return
        except queue.Full:
            self.dropped += 1
        try:
            self._queue.get_nowait()
        except queue.Empty:
            pass
        try:
            self._queue.put_nowait(item)
        except queue.Full:
            self.dropped += 1

    def snapshot(self) -> dict:
        return {
            "preview_encoded": self.encoded,
            "preview_dropped": self.dropped,
            "preview_errors": self.errors,
            "preview_encode_ms": self.last_encode_ms,
        }

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                frame, result = self._queue.get(timeout=0.2)
            except queue.Empty:
                continue
            started = time.monotonic()
            try:
                self._encode(frame, result)
                self.encoded += 1
            except Exception as exc:
                self.errors += 1
                if self.errors <= 3:
                    print(f"preview_encode_error error={exc!r}", flush=True)
            self.last_encode_ms = (time.monotonic() - started) * 1000.0

    def _encode(self, frame, result: dict) -> None:
        roi = self._config["roi"]
        status = self._config["status_server"]
        calibration = self._config["calibration"]
        roi_x = int(roi["x"])
        roi_y = int(roi["y"])
        roi_w = int(roi["width"])
        roi_h = int(roi["height"])
        origin_x = int(round(float(calibration["center_x_px"])))
        frame.draw_rect(
            roi_x, roi_y, roi_w, roi_h, color=self._image.COLOR_GREEN, thickness=2
        )
        frame.draw_line(
            origin_x,
            roi_y,
            origin_x,
            roi_y + roi_h,
            color=self._image.COLOR_GREEN,
            thickness=2,
        )
        if result.get("candidate") is not None:
            box = result["candidate"]
            frame.draw_rect(
                int(round(box["preview_x"])),
                int(round(box["preview_y"])),
                int(round(box["preview_w"])),
                int(round(box["preview_h"])),
                color=(
                    self._image.COLOR_GREEN
                    if result.get("control_valid")
                    else self._image.COLOR_RED
                ),
                thickness=3,
            )
        label = (
            f"BALL {result['position_mm']:+.1f}mm "
            f"conf={result['confidence']:.2f}"
            if result.get("control_valid")
            else f"INVALID {result.get('reason', 'no_candidate')}"
        )
        frame.draw_string(8, 8, label, color=self._image.COLOR_RED, scale=1.0)
        frame = frame.resize(
            int(status["preview_width"]), int(status["preview_height"])
        )
        jpeg = frame.to_jpeg(quality=int(status["jpeg_quality"]))
        self._status_server.update_preview(bytes(jpeg.to_bytes(copy=True)))
