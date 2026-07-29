import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RuntimeConfigTests(unittest.TestCase):
    @staticmethod
    def load_config():
        with (ROOT / "config" / "ball_detection.json").open(
            "r", encoding="utf-8"
        ) as handle:
            return json.load(handle)

    def test_control_uart_uses_uart0_a16_a17(self):
        uart = self.load_config()["uart"]

        self.assertEqual(uart["bus"], 0)
        self.assertEqual(uart["device"], "/dev/ttyS0")
        self.assertEqual(uart["tx_pin"], "A16")
        self.assertEqual(uart["rx_pin"], "A17")
        self.assertEqual(uart["baud_rate"], 115200)

    def test_lightweight_false_positive_filters_preserve_frame_rate(self):
        config = self.load_config()
        self.assertEqual(config["alignment"]["update_every_frames"], 0)
        self.assertFalse(config["detector"]["local_brightness_enabled"])
        self.assertGreaterEqual(config["detector"]["max_candidates"], 2)
        self.assertTrue(config["measurement_filter"]["enabled"])
        self.assertGreaterEqual(
            config["measurement_filter"]["acquire_confirm_frames"], 2
        )


if __name__ == "__main__":
    unittest.main()
