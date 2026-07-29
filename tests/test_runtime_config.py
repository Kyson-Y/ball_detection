import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RuntimeConfigTests(unittest.TestCase):
    def test_control_uart_uses_uart0_a16_a17(self):
        with (ROOT / "config" / "ball_detection.json").open(
            "r", encoding="utf-8"
        ) as handle:
            uart = json.load(handle)["uart"]

        self.assertEqual(uart["bus"], 0)
        self.assertEqual(uart["device"], "/dev/ttyS0")
        self.assertEqual(uart["tx_pin"], "A16")
        self.assertEqual(uart["rx_pin"], "A17")
        self.assertEqual(uart["baud_rate"], 115200)


if __name__ == "__main__":
    unittest.main()
