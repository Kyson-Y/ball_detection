from __future__ import annotations

import http.client
import io
import sys
import tempfile
import threading
import unittest
from pathlib import Path


APP = Path(__file__).resolve().parents[1] / "app"
sys.path.insert(0, str(APP))

from media_http_server import MediaHttpServer
from recording_manager import RecordingManager


class FakeProcess:
    def __init__(self, output_path: str) -> None:
        self.stdin = io.BytesIO()
        self.returncode = None
        Path(output_path).write_bytes(b"0123456789")

    def poll(self):
        return self.returncode

    def wait(self, timeout=None):
        self.returncode = 0
        return self.returncode

    def terminate(self):
        self.returncode = -15


class FakePopen:
    def __call__(self, command, **_kwargs):
        return FakeProcess(command[-1])


class MediaHttpServerTests(unittest.TestCase):
    def request(self, port, method, path):
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
        connection.request(method, path)
        response = connection.getresponse()
        body = response.read()
        headers = dict(response.getheaders())
        connection.close()
        return response.status, headers, body

    def test_control_list_range_and_download(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manager = RecordingManager(
                "rtsp://127.0.0.1:8554/live",
                directory,
                popen_factory=FakePopen(),
            )
            server = MediaHttpServer(manager, "rtsp://127.0.0.1:8554/live", 0)
            server.start()
            try:
                status, _, _ = self.request(
                    server.port, "POST", "/api/record/start"
                )
                self.assertEqual(status, 200)
                status, _, body = self.request(
                    server.port, "POST", "/api/record/stop"
                )
                self.assertEqual(status, 200)
                self.assertIn(b"completed_file", body)

                status, _, body = self.request(
                    server.port, "GET", "/api/recordings"
                )
                self.assertEqual(status, 200)
                self.assertIn(b"record_", body)
                name = manager.list_recordings()[0]["name"]

                status, headers, body = self.request(
                    server.port, "GET", f"/recordings/{name}"
                )
                self.assertEqual(status, 200)
                self.assertEqual(body, b"0123456789")
                self.assertEqual(headers["Content-Type"], "video/mp4")

                connection = http.client.HTTPConnection(
                    "127.0.0.1", server.port, timeout=2
                )
                connection.request(
                    "GET", f"/recordings/{name}?download=1", headers={"Range": "bytes=2-5"}
                )
                response = connection.getresponse()
                self.assertEqual(response.status, 206)
                self.assertEqual(response.getheader("Content-Range"), "bytes 2-5/10")
                self.assertEqual(response.getheader("Content-Disposition").startswith("attachment"), True)
                self.assertEqual(response.read(), b"2345")
                connection.close()
            finally:
                server.stop()


if __name__ == "__main__":
    unittest.main()
