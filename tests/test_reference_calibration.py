import tempfile
import unittest
from pathlib import Path

import numpy as np


import sys


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "app"))

from reference_calibration import (  # noqa: E402
    EmptyReferenceAccumulator,
    save_reference,
)


class ReferenceCalibrationTests(unittest.TestCase):
    def make_config(self, reference_path: Path, capture_frames: int = 2) -> dict:
        return {
            "camera": {"width": 8, "height": 6},
            "roi": {"x": 2, "y": 1, "width": 4, "height": 3},
            "reference": {
                "path": str(reference_path),
                "capture_frames": capture_frames,
            },
        }

    def test_accumulator_averages_only_the_fixed_roi(self):
        config = self.make_config(Path("unused.npy"))
        accumulator = EmptyReferenceAccumulator(config)
        first = np.zeros((6, 8), dtype=np.uint8)
        second = np.zeros((6, 8), dtype=np.uint8)
        first[1:4, 2:6] = 10
        second[1:4, 2:6] = 21

        accumulator.add(first)
        self.assertFalse(accumulator.complete)
        accumulator.add(second)

        self.assertTrue(accumulator.complete)
        self.assertEqual(accumulator.captured_frames, 2)
        np.testing.assert_array_equal(
            accumulator.reference(), np.full((3, 4), 15, dtype=np.uint8)
        )

    def test_repeat_saves_archive_and_back_up_replaced_reference(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            runtime = Path(temporary_directory) / "runtime"
            runtime.mkdir()
            reference_path = runtime / "empty_pipe_reference.npy"
            config = self.make_config(reference_path)
            original = np.full((3, 4), 1, dtype=np.uint8)
            first = np.full((3, 4), 10, dtype=np.uint8)
            second = np.full((3, 4), 20, dtype=np.uint8)
            np.save(reference_path, original, allow_pickle=False)

            first_metadata = save_reference(
                first,
                config,
                elapsed_s=0.5,
                temperature_start_mc=50000,
                temperature_end_mc=51000,
                source="test",
                calibration_id="first",
                raw_jpeg=b"jpeg-one",
            )
            second_metadata = save_reference(
                second,
                config,
                elapsed_s=0.6,
                temperature_start_mc=51000,
                temperature_end_mc=52000,
                source="test",
                calibration_id="second",
            )

            archive = runtime / "calibrations"
            np.testing.assert_array_equal(np.load(reference_path), second)
            np.testing.assert_array_equal(np.load(archive / "first.npy"), first)
            np.testing.assert_array_equal(
                np.load(archive / "first_previous.npy"), original
            )
            np.testing.assert_array_equal(np.load(archive / "second.npy"), second)
            np.testing.assert_array_equal(
                np.load(archive / "second_previous.npy"), first
            )
            self.assertEqual(first_metadata["calibration_id"], "first")
            self.assertEqual(second_metadata["calibration_id"], "second")
            self.assertTrue((archive / "first.json").is_file())
            self.assertTrue((archive / "first.pgm").is_file())
            self.assertTrue((archive / "first_raw.jpg").is_file())
            self.assertEqual(list(runtime.rglob("*.incoming")), [])


if __name__ == "__main__":
    unittest.main()
