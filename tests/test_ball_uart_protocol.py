import struct
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "app"))

from ball_uart_protocol import (  # noqa: E402
    AlphaBetaTracker,
    BallMeasurementGate,
    build_state_packet,
    crc16_ccitt_false,
)


class PacketTests(unittest.TestCase):
    def test_crc_known_vector(self):
        self.assertEqual(crc16_ccitt_false(b"123456789"), 0x29B1)

    def test_reference_packet(self):
        packet = build_state_packet(
            seq=1000,
            capture_time_ms=123456,
            center_offset_mm=23.4,
            velocity_mm_s=-126.0,
            confidence=230.0 / 255.0,
            age_ms=3,
            flags=0x23,
        )
        self.assertEqual(
            packet.hex(" ").upper(),
            "AA 55 01 01 16 23 E8 03 40 E2 01 00 EA 00 82 FF E6 00 03 00 21 9A",
        )

    def test_values_are_clamped_and_little_endian(self):
        packet = build_state_packet(
            seq=0x12345,
            capture_time_ms=0x123456789,
            center_offset_mm=-9999.0,
            velocity_mm_s=99999.0,
            confidence=2.0,
            age_ms=99999,
            flags=0x1FF,
        )
        fields = struct.unpack("<BBBBBBHIhhBBHH", packet)
        self.assertEqual(fields[0:2], (0xAA, 0x55))
        self.assertEqual(fields[6], 0x2345)
        self.assertEqual(fields[7], 0x23456789)
        self.assertEqual(fields[8], -32768)
        self.assertEqual(fields[9], 32767)
        self.assertEqual(fields[10], 255)
        self.assertEqual(fields[12], 0xFFFF)


class AlphaBetaTrackerTests(unittest.TestCase):
    def test_velocity_becomes_valid_after_three_measurements(self):
        tracker = AlphaBetaTracker(alpha=0.7, beta=0.2)
        self.assertEqual(tracker.update(0.0, 0.0), (0.0, 0.0, False))
        _position, velocity, valid = tracker.update(10.0, 0.1)
        self.assertAlmostEqual(velocity, 20.0)
        self.assertFalse(valid)
        _position, _velocity, valid = tracker.update(20.0, 0.2)
        self.assertTrue(valid)

    def test_prediction_expires_and_resets(self):
        tracker = AlphaBetaTracker(reset_after_s=0.2)
        tracker.update(10.0, 1.0)
        self.assertIsNotNone(tracker.predict(1.1))
        self.assertEqual(tracker.measurement_age_ms(1.1), 100)
        self.assertIsNone(tracker.predict(1.201))
        self.assertFalse(tracker.initialized)
        self.assertEqual(tracker.measurement_age_ms(1.3), 0xFFFF)


class BallMeasurementGateTests(unittest.TestCase):
    @staticmethod
    def candidate(center_x, peak=500, confidence=0.8):
        return {"center_x": center_x, "peak": peak, "confidence": confidence}

    def make_gate(self):
        return BallMeasurementGate(
            max_speed_px_s=2500.0,
            jump_margin_px=20.0,
            prediction_gate_px=70.0,
            acquire_confirm_frames=3,
            acquire_match_radius_px=18.0,
            acquire_min_confidence=0.15,
            lock_timeout_s=0.35,
            velocity_alpha=0.5,
        )

    def test_acquisition_requires_three_consistent_measurements(self):
        gate = self.make_gate()
        candidate = self.candidate(100.0)
        self.assertIsNone(gate.select([candidate], 0.00)[0])
        self.assertIsNone(gate.select([candidate], 0.02)[0])
        selected, reason, _expected = gate.select([candidate], 0.04)
        self.assertEqual(reason, "accepted")
        self.assertEqual(selected["center_x"], 100.0)
        self.assertTrue(gate.locked)

    def test_far_stronger_peak_is_rejected_for_nearby_candidate(self):
        gate = self.make_gate()
        for timestamp in (0.00, 0.02, 0.04):
            selected, _reason, _expected = gate.select(
                [self.candidate(100.0)], timestamp
            )
        self.assertIsNotNone(selected)

        selected, reason, expected = gate.select(
            [self.candidate(610.0, peak=900), self.candidate(112.0, peak=350)],
            0.06,
        )
        self.assertEqual(reason, "accepted")
        self.assertAlmostEqual(expected, 100.0)
        self.assertEqual(selected["center_x"], 112.0)

    def test_impossible_jump_is_not_accepted(self):
        gate = self.make_gate()
        for timestamp in (0.00, 0.02, 0.04):
            gate.select([self.candidate(100.0)], timestamp)

        selected, reason, _expected = gate.select(
            [self.candidate(610.0, peak=900)], 0.06
        )
        self.assertIsNone(selected)
        self.assertEqual(reason, "jump_rejected")
        self.assertEqual(gate.rejected_jumps, 1)
        self.assertTrue(gate.locked)

    def test_low_confidence_candidate_cannot_acquire_lock(self):
        gate = self.make_gate()
        selected, reason, _expected = gate.select(
            [self.candidate(100.0, confidence=0.1)], 0.0
        )
        self.assertIsNone(selected)
        self.assertEqual(reason, "low_confidence")
        self.assertFalse(gate.locked)


if __name__ == "__main__":
    unittest.main()
