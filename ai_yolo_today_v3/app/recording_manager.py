from __future__ import annotations

import os
import subprocess
import threading
import time
from datetime import datetime
from pathlib import Path


class RecordingManager:
    """Remux one RTSP H.264 stream into finalized MP4 files."""

    def __init__(
        self,
        source_url: str,
        output_directory: str,
        *,
        ffmpeg_path: str = "ffmpeg",
        stop_timeout_s: float = 12.0,
        popen_factory=None,
    ) -> None:
        self.source_url = str(source_url)
        self.output_directory = Path(output_directory)
        self.ffmpeg_path = str(ffmpeg_path)
        self.stop_timeout_s = float(stop_timeout_s)
        self._popen = popen_factory or subprocess.Popen
        self._lock = threading.RLock()
        self._process = None
        self._partial_path = None
        self._final_path = None
        self._started_at = None
        self._last_error = None
        self.output_directory.mkdir(parents=True, exist_ok=True)

    def _refresh_locked(self) -> None:
        if self._process is None:
            return
        code = self._process.poll()
        if code is None:
            return
        self._last_error = f"ffmpeg exited unexpectedly with code {code}"
        self._process = None
        self._partial_path = None
        self._final_path = None
        self._started_at = None

    def _command(self, partial_path: Path) -> list[str]:
        return [
            self.ffmpeg_path,
            "-hide_banner",
            "-loglevel",
            "error",
            "-rtsp_transport",
            "tcp",
            "-i",
            self.source_url,
            "-map",
            "0:v:0",
            "-an",
            "-c:v",
            "copy",
            "-movflags",
            "+faststart",
            "-y",
            str(partial_path),
        ]

    def start(self) -> dict:
        with self._lock:
            self._refresh_locked()
            if self._process is not None:
                raise RuntimeError("recording is already active")

            stamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3]
            final_path = self.output_directory / f"record_{stamp}.mp4"
            partial_path = self.output_directory / f".record_{stamp}.partial.mp4"
            process = self._popen(
                self._command(partial_path),
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            self._process = process
            self._partial_path = partial_path
            self._final_path = final_path
            self._started_at = time.time()
            self._last_error = None
            return self.status()

    def stop(self) -> dict:
        with self._lock:
            self._refresh_locked()
            process = self._process
            partial_path = self._partial_path
            final_path = self._final_path
            if process is None:
                raise RuntimeError("recording is not active")

            try:
                if process.stdin is not None:
                    process.stdin.write(b"q\n")
                    process.stdin.flush()
                code = process.wait(timeout=self.stop_timeout_s)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    code = process.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    code = -1
            finally:
                if process.stdin is not None:
                    try:
                        process.stdin.close()
                    except OSError:
                        pass

            self._process = None
            self._partial_path = None
            self._final_path = None
            self._started_at = None

            if code != 0:
                self._last_error = f"ffmpeg stopped with code {code}"
                raise RuntimeError(self._last_error)
            if partial_path is None or not partial_path.is_file():
                self._last_error = "ffmpeg did not create an MP4 file"
                raise RuntimeError(self._last_error)
            if partial_path.stat().st_size <= 0:
                self._last_error = "recorded MP4 file is empty"
                raise RuntimeError(self._last_error)

            os.replace(partial_path, final_path)
            self._last_error = None
            result = self.status()
            result["completed_file"] = final_path.name
            return result

    def shutdown(self) -> None:
        with self._lock:
            active = self._process is not None
        if active:
            try:
                self.stop()
            except Exception as exc:
                with self._lock:
                    self._last_error = str(exc)

    def status(self) -> dict:
        with self._lock:
            self._refresh_locked()
            active = self._process is not None
            elapsed_s = (
                0.0
                if not active or self._started_at is None
                else max(0.0, time.time() - self._started_at)
            )
            return {
                "recording": active,
                "active_file": (
                    None if self._final_path is None else self._final_path.name
                ),
                "elapsed_s": elapsed_s,
                "last_error": self._last_error,
            }

    def list_recordings(self) -> list[dict]:
        entries = []
        for path in self.output_directory.glob("*.mp4"):
            if path.name.startswith(".") or not path.is_file():
                continue
            stat = path.stat()
            entries.append(
                {
                    "name": path.name,
                    "size_bytes": stat.st_size,
                    "modified_at": stat.st_mtime,
                    "url": f"/recordings/{path.name}",
                }
            )
        entries.sort(key=lambda item: item["modified_at"], reverse=True)
        return entries

    def resolve_recording(self, name: str):
        if name != os.path.basename(name) or not name.endswith(".mp4"):
            return None
        if name.startswith("."):
            return None
        path = self.output_directory / name
        return path if path.is_file() and not path.is_symlink() else None
