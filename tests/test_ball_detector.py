import json
import sys
import unittest
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "app"))

from ball_detector import BallDetector  # noqa: E402


class BallDetectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with (ROOT / "config" / "ball_detection.json").open(
            "r", encoding="utf-8"
        ) as handle:
            cls.config = json.load(handle)
        cls.config["alignment"]["enabled"] = False

    def setUp(self):
        self.reference = np.full((100, 640), 100, dtype=np.uint8)
        self.detector = BallDetector(self.config, self.reference)
        self.frame = np.full((480, 640), 100, dtype=np.uint8)

    def test_empty_reference_has_no_detection(self):
        result = self.detector.detect(self.frame, 0, 0.0)
        self.assertFalse(result["detected"])
        self.assertFalse(result["reference_mismatch"])

    def test_local_ball_change_returns_centroid(self):
        expected_x = 347.5
        roi_y = self.config["roi"]["y"]
        self.frame[roi_y + 10 : roi_y + 45, 338:358] = 155
        result = self.detector.detect(self.frame, 0, 0.0)
        self.assertTrue(result["detected"])
        self.assertFalse(result["reference_mismatch"])
        self.assertAlmostEqual(result["center_x"], expected_x, delta=1.0)
        self.assertGreater(result["confidence"], 0.0)

    def test_global_scene_change_is_rejected(self):
        roi_y = self.config["roi"]["y"]
        self.frame[roi_y : roi_y + 100, :320] = 180
        result = self.detector.detect(self.frame, 0, 0.0)
        self.assertFalse(result["detected"])
        self.assertTrue(result["reference_mismatch"])
        self.assertGreater(result["changed_ratio"], 0.9)

    def test_uniform_brightness_change_is_compensated(self):
        self.frame.fill(112)
        result = self.detector.detect(self.frame, 0, 0.0)
        self.assertFalse(result["detected"])
        self.assertFalse(result["reference_mismatch"])
        self.assertEqual(result["brightness_offset"], 12)


if __name__ == "__main__":
    unittest.main()
