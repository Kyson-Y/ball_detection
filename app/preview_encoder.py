from __future__ import annotations

import queue
import threading
import time


class PreviewEncoder:
    def __init__(
        self,
        status_server,
        image_module,
        roi_x: int,
        roi_w: int,
        roi_h: int,
        width: int,
        height: int,
        jpeg_quality: int,
    ) -> None:
        self._status_server = status_server
        self._image = image_module
        self._roi_x = int(roi_x)
        self._roi_w = int(roi_w)
        self._roi_h = int(roi_h)
        self._width = int(width)
        self._height = int(height)
        self._jpeg_quality = int(jpeg_quality)
        self._queue = queue.Queue(maxsize=1)
        self._stop_event = threading.Event()
        self._thread = None
        self.encoded = 0
        self.dropped = 0
        self.errors = 0
        self.last_encode_ms = 0.0
        self.last_error = ""

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def submit(
        self,
        gray,
        result: dict,
        origin_x_px: float,
        offset_px,
        offset_mm,
    ) -> None:
        item = (gray, result, origin_x_px, offset_px, offset_mm)
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
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)

    def _run(self) -> None:
        while not self._stop_event.is_set():
            try:
                item = self._queue.get(timeout=0.2)
            except queue.Empty:
                continue

            started = time.monotonic()
            try:
                jpeg = self._encode(*item)
                self._status_server.update_preview(jpeg)
                self.encoded += 1
                self.last_error = ""
            except Exception as exc:
                self.errors += 1
                self.last_error = repr(exc)[:160]
            finally:
                self.last_encode_ms = (time.monotonic() - started) * 1000.0

    def _encode(
        self,
        gray,
        result: dict,
        origin_x_px: float,
        offset_px,
        offset_mm,
    ) -> bytes:
        frame = self._image.cv2image(gray, bgr=False, copy=False)
        black = self._image.Color.from_gray(0)
        gray_line = self._image.Color.from_gray(160)
        white = self._image.Color.from_gray(255)

        roi_y = int(result["roi_y"])
        origin_x = int(round(origin_x_px))
        origin_y = roi_y + self._roi_h // 2

        frame.draw_rect(
            self._roi_x, roi_y, self._roi_w, self._roi_h, black, thickness=4
        )
        frame.draw_rect(
            self._roi_x, roi_y, self._roi_w, self._roi_h, white, thickness=1
        )
        frame.draw_line(
            origin_x, roi_y, origin_x, roi_y + self._roi_h, black, thickness=5
        )
        frame.draw_line(
            origin_x,
            roi_y,
            origin_x,
            roi_y + self._roi_h,
            gray_line,
            thickness=2,
        )
        frame.draw_cross(origin_x, origin_y, black, size=12, thickness=5)
        frame.draw_cross(origin_x, origin_y, white, size=12, thickness=2)

        if bool(result["detected"]):
            ball_x = self._roi_x + int(round(result["center_x"]))
            ball_y = roi_y + int(round(result["center_y"]))
            frame.draw_line(origin_x, ball_y, ball_x, ball_y, black, thickness=5)
            frame.draw_line(origin_x, ball_y, ball_x, ball_y, white, thickness=2)
            frame.draw_cross(ball_x, ball_y, black, size=14, thickness=5)
            frame.draw_cross(ball_x, ball_y, white, size=14, thickness=2)
            frame.draw_circle(ball_x, ball_y, 4, black, thickness=-1)
            frame.draw_circle(ball_x, ball_y, 2, white, thickness=-1)
            label = f"BALL err={offset_px:+.1f}px {offset_mm:+.1f}mm"
        elif bool(result["reference_mismatch"]):
            label = "REFERENCE MISMATCH"
        elif bool(result.get("measurement_rejected", False)):
            label = "MEASUREMENT REJECTED"
        else:
            label = "NO BALL"

        frame.draw_rect(4, 4, 410, 28, black, thickness=-1)
        frame.draw_string(8, 7, label, white, scale=1.0, thickness=1)

        if frame.width() != self._width or frame.height() != self._height:
            frame = frame.resize(self._width, self._height)
        jpeg = frame.to_jpeg(quality=self._jpeg_quality)
        return bytes(jpeg.to_bytes(copy=True))
