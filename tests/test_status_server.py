import json
import sys
import unittest
import urllib.request
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "app"))

from status_server import StatusServer  # noqa: E402


class StatusServerTests(unittest.TestCase):
    def setUp(self):
        self.server = StatusServer(0, host="127.0.0.1")
        self.server.start()

    def tearDown(self):
        self.server.stop()

    def test_status_json_returns_latest_snapshot(self):
        self.server.update(
            {
                "control_hz": 29.95,
                "uart_hz": 29.94,
                "detected": True,
                "reference_mismatch": False,
                "ball_x_px": 352.0,
                "offset_px": 32.0,
                "offset_mm": 12.5,
                "origin_x_px": 320.0,
                "position_mm": 12.5,
                "velocity_mm_s": -30.0,
                "velocity_valid": True,
                "confidence": 0.8,
                "temperature_c": 61.2,
                "flags": 0x23,
                "uart_errors": 0,
                "frames": 100,
            }
        )
        with urllib.request.urlopen(
            f"http://127.0.0.1:{self.server.port}/status.json", timeout=1.0
        ) as response:
            payload = json.load(response)
        self.assertEqual(payload["control_hz"], 29.95)
        self.assertEqual(payload["offset_px"], 32.0)
        self.assertEqual(payload["origin_x_px"], 320.0)
        self.assertEqual(payload["flags"], 0x23)

    def test_index_has_control_rate(self):
        with urllib.request.urlopen(
            f"http://127.0.0.1:{self.server.port}/", timeout=1.0
        ) as response:
            page = response.read()
        self.assertIn(b"CONTROL RATE", page)
        self.assertIn(b"OFFSET PX", page)
        self.assertIn(b"/frame.jpg", page)
        self.assertTrue(self.server.preview_requested_recently())

    def test_frame_endpoint_returns_latest_jpeg(self):
        jpeg = b"\xff\xd8preview\xff\xd9"
        self.server.update_preview(jpeg)
        with urllib.request.urlopen(
            f"http://127.0.0.1:{self.server.port}/frame.jpg", timeout=1.0
        ) as response:
            payload = response.read()
            content_type = response.headers.get_content_type()
        self.assertEqual(payload, jpeg)
        self.assertEqual(content_type, "image/jpeg")
        self.assertTrue(self.server.preview_requested_recently())


if __name__ == "__main__":
    unittest.main()
