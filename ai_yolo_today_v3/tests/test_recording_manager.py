from __future__ import annotations

import io
import sys
import tempfile
import unittest
from pathlib import Path


APP = Path(__file__).resolve().parents[1] / "app"
sys.path.insert(0, str(APP))

from recording_manager import RecordingManager


class FakeProcess:
    def __init__(self, output_path: str) -> None:
        self.stdin = io.BytesIO()
        self.returncode = None
        Path(output_path).write_bytes(b"fake mp4")

    def poll(self):
        return self.returncode

    def wait(self, timeout=None):
        self.returncode = 0
        return self.returncode

    def terminate(self):
        self.returncode = -15


class FakePopen:
    def __init__(self) -> None:
        self.commands = []

    def __call__(self, command, **_kwargs):
        self.commands.append(command)
        return FakeProcess(command[-1])


class RecordingManagerTests(unittest.TestCase):
    def test_recording_start_stop_and_list(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            popen = FakePopen()
            manager = RecordingManager(
                "rtsp://127.0.0.1:8554/live",
                directory,
                popen_factory=popen,
            )
            started = manager.start()
            self.assertTrue(started["recording"])
            self.assertIn("-c:v", popen.commands[0])
            self.assertIn("copy", popen.commands[0])

            stopped = manager.stop()
            self.assertFalse(stopped["recording"])
            self.assertTrue(stopped["completed_file"].endswith(".mp4"))
            recordings = manager.list_recordings()
            self.assertEqual(len(recordings), 1)
            self.assertEqual(recordings[0]["size_bytes"], len(b"fake mp4"))

    def test_rejects_duplicate_start(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manager = RecordingManager(
                "rtsp://127.0.0.1:8554/live",
                directory,
                popen_factory=FakePopen(),
            )
            manager.start()
            with self.assertRaisesRegex(RuntimeError, "already active"):
                manager.start()
            manager.stop()

    def test_resolve_recording_blocks_path_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manager = RecordingManager("rtsp://local/live", directory)
            valid = root / "record_valid.mp4"
            valid.write_bytes(b"data")
            self.assertEqual(manager.resolve_recording(valid.name), valid)
            self.assertIsNone(manager.resolve_recording("../record_valid.mp4"))
            self.assertIsNone(manager.resolve_recording(".partial.mp4"))


if __name__ == "__main__":
    unittest.main()
