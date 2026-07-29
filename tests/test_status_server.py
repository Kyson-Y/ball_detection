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
        self.assertEqual(payload["flags"], 0x23)

    def test_index_has_control_rate(self):
        with urllib.request.urlopen(
            f"http://127.0.0.1:{self.server.port}/", timeout=1.0
        ) as response:
            page = response.read()
        self.assertIn(b"CONTROL RATE", page)


if __name__ == "__main__":
    unittest.main()
