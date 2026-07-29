import copy
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
        self.assertAlmostEqual(result["offset_x"], expected_x - 320.0, delta=1.0)
        self.assertEqual(self.detector.origin_x, 320.0)
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

    def test_local_brightness_gradient_is_compensated(self):
        config = copy.deepcopy(self.config)
        config["detector"]["local_brightness_enabled"] = True
        detector = BallDetector(config, self.reference)
        roi_y = self.config["roi"]["y"]
        gradient = np.linspace(-24, 24, 640, dtype=np.int16)
        roi = np.full((100, 640), 100, dtype=np.int16) + gradient[None, :]
        self.frame[roi_y : roi_y + 100] = roi.astype(np.uint8)
        result = detector.detect(self.frame, 0, 0.0)
        self.assertFalse(result["detected"])
        self.assertFalse(result["reference_mismatch"])
        self.assertGreater(result["local_brightness_offset_max"], 0)

    def test_multiple_separated_changes_return_multiple_candidates(self):
        roi_y = self.config["roi"]["y"]
        self.frame[roi_y + 10 : roi_y + 42, 90:112] = 155
        self.frame[roi_y + 12 : roi_y + 44, 500:522] = 155
        result = self.detector.detect(self.frame, 0, 0.0)
        self.assertTrue(result["raw_detected"])
        self.assertGreaterEqual(result["candidate_count"], 2)
        centers = [candidate["center_x"] for candidate in result["candidates"]]
        self.assertTrue(any(abs(center - 100.5) < 3.0 for center in centers))
        self.assertTrue(any(abs(center - 510.5) < 3.0 for center in centers))


if __name__ == "__main__":
    unittest.main()
