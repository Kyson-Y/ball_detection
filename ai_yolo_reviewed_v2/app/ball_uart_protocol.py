from __future__ import annotations

import math
import struct


PACKET_HEADER = b"\xAA\x55"
PACKET_VERSION = 0x01
PACKET_TYPE_STATE = 0x01
PACKET_SIZE = 22

FLAG_DETECTED = 1 << 0
FLAG_VELOCITY_VALID = 1 << 1
FLAG_PREDICTED = 1 << 2
FLAG_LOW_CONFIDENCE = 1 << 3
FLAG_REFERENCE_MISMATCH = 1 << 4
FLAG_CALIBRATION_VALID = 1 << 5
FLAG_TEMPERATURE_WARNING = 1 << 6


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _round_half_away_from_zero(value: float) -> int:
    if not math.isfinite(value):
        raise ValueError("packet values must be finite")
    return int(value + 0.5) if value >= 0.0 else int(value - 0.5)


def _clamp(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def build_state_packet(
    *,
    seq: int,
    capture_time_ms: int,
    center_offset_mm: float,
    velocity_mm_s: float,
    confidence: float,
    age_ms: int,
    flags: int,
) -> bytes:
    center_offset_decimm = _clamp(
        _round_half_away_from_zero(center_offset_mm * 10.0), -32768, 32767
    )
    velocity_integer = _clamp(
        _round_half_away_from_zero(velocity_mm_s), -32768, 32767
    )
    confidence_u8 = _clamp(
        _round_half_away_from_zero(confidence * 255.0), 0, 255
    )
    age_u16 = _clamp(int(age_ms), 0, 0xFFFF)

    crc_payload = struct.pack(
        "<BBBBHIhhBBH",
        PACKET_VERSION,
        PACKET_TYPE_STATE,
        PACKET_SIZE,
        flags & 0xFF,
        seq & 0xFFFF,
        capture_time_ms & 0xFFFFFFFF,
        center_offset_decimm,
        velocity_integer,
        confidence_u8,
        0,
        age_u16,
    )
    crc = crc16_ccitt_false(crc_payload)
    packet = PACKET_HEADER + crc_payload + struct.pack("<H", crc)
    if len(packet) != PACKET_SIZE:
        raise RuntimeError(f"unexpected packet size: {len(packet)}")
    return packet


class AlphaBetaTracker:
    def __init__(
        self,
        *,
        alpha: float = 0.7,
        beta: float = 0.2,
        velocity_valid_measurements: int = 3,
        reset_after_s: float = 0.2,
    ) -> None:
        if not 0.0 < alpha <= 1.0:
            raise ValueError("alpha must be in (0, 1]")
        if not 0.0 <= beta <= 1.0:
            raise ValueError("beta must be in [0, 1]")
        if velocity_valid_measurements < 2:
            raise ValueError("velocity_valid_measurements must be at least 2")
        if reset_after_s <= 0.0:
            raise ValueError("reset_after_s must be positive")
        self.alpha = alpha
        self.beta = beta
        self.velocity_valid_measurements = velocity_valid_measurements
        self.reset_after_s = reset_after_s
        self.reset()

    def reset(self) -> None:
        self.position_mm = 0.0
        self.velocity_mm_s = 0.0
        self.last_state_time_s = None
        self.last_measurement_time_s = None
        self.measurement_count = 0

    @property
    def initialized(self) -> bool:
        return self.last_state_time_s is not None

    @property
    def velocity_valid(self) -> bool:
        return self.measurement_count >= self.velocity_valid_measurements

    def update(self, measurement_mm: float, timestamp_s: float):
        measurement_mm = float(measurement_mm)
        timestamp_s = float(timestamp_s)
        if not math.isfinite(measurement_mm) or not math.isfinite(timestamp_s):
            raise ValueError("tracker inputs must be finite")

        if not self.initialized:
            self.position_mm = measurement_mm
            self.velocity_mm_s = 0.0
            self.last_state_time_s = timestamp_s
            self.last_measurement_time_s = timestamp_s
            self.measurement_count = 1
            return self.state()

        dt = timestamp_s - self.last_state_time_s
        measurement_age_s = timestamp_s - self.last_measurement_time_s
        if dt <= 0.0 or measurement_age_s > self.reset_after_s:
            self.reset()
            return self.update(measurement_mm, timestamp_s)

        predicted_position = self.position_mm + self.velocity_mm_s * dt
        residual = measurement_mm - predicted_position
        self.position_mm = predicted_position + self.alpha * residual
        self.velocity_mm_s += (self.beta / dt) * residual
        self.last_state_time_s = timestamp_s
        self.last_measurement_time_s = timestamp_s
        self.measurement_count += 1
        return self.state()

    def predict(self, timestamp_s: float):
        timestamp_s = float(timestamp_s)
        if not math.isfinite(timestamp_s):
            raise ValueError("tracker timestamp must be finite")
        if not self.initialized:
            return None

        measurement_age_s = timestamp_s - self.last_measurement_time_s
        dt = timestamp_s - self.last_state_time_s
        if dt < 0.0 or measurement_age_s > self.reset_after_s:
            self.reset()
            return None
        if dt > 0.0:
            self.position_mm += self.velocity_mm_s * dt
            self.last_state_time_s = timestamp_s
        return self.state()

    def measurement_age_ms(self, timestamp_s: float) -> int:
        if self.last_measurement_time_s is None:
            return 0xFFFF
        age_s = max(0.0, float(timestamp_s) - self.last_measurement_time_s)
        return _clamp(_round_half_away_from_zero(age_s * 1000.0), 0, 0xFFFF)

    def state(self):
        return self.position_mm, self.velocity_mm_s, self.velocity_valid


class BallMeasurementGate:
    def __init__(
        self,
        *,
        max_speed_px_s: float,
        jump_margin_px: float,
        prediction_gate_px: float,
        acquire_confirm_frames: int,
        acquire_match_radius_px: float,
        acquire_min_confidence: float,
        lock_timeout_s: float,
        velocity_alpha: float = 0.5,
    ) -> None:
        if max_speed_px_s <= 0.0:
            raise ValueError("maximum speed must be positive")
        if jump_margin_px < 0.0 or prediction_gate_px <= 0.0:
            raise ValueError("invalid measurement gate distances")
        if acquire_confirm_frames <= 0:
            raise ValueError("acquisition confirmation must be positive")
        if acquire_match_radius_px < 0.0:
            raise ValueError("acquisition radius cannot be negative")
        if not 0.0 <= acquire_min_confidence <= 1.0:
            raise ValueError("acquisition confidence must be in [0, 1]")
        if lock_timeout_s <= 0.0:
            raise ValueError("lock timeout must be positive")
        if not 0.0 < velocity_alpha <= 1.0:
            raise ValueError("velocity alpha must be in (0, 1]")

        self.max_speed_px_s = float(max_speed_px_s)
        self.jump_margin_px = float(jump_margin_px)
        self.prediction_gate_px = float(prediction_gate_px)
        self.acquire_confirm_frames = int(acquire_confirm_frames)
        self.acquire_match_radius_px = float(acquire_match_radius_px)
        self.acquire_min_confidence = float(acquire_min_confidence)
        self.lock_timeout_s = float(lock_timeout_s)
        self.velocity_alpha = float(velocity_alpha)
        self.reset()

    def reset(self) -> None:
        self.locked = False
        self.position_px = 0.0
        self.velocity_px_s = 0.0
        self.last_measurement_time_s = None
        self.pending_x_px = None
        self.pending_time_s = None
        self.pending_count = 0
        self.rejected_jumps = 0
        self.last_reason = "reset"

    def _clear_pending(self) -> None:
        self.pending_x_px = None
        self.pending_time_s = None
        self.pending_count = 0

    def _accept(self, candidate: dict, timestamp_s: float) -> dict:
        center_x = float(candidate["center_x"])
        if self.locked and self.last_measurement_time_s is not None:
            dt = timestamp_s - self.last_measurement_time_s
            if dt > 0.0:
                measured_velocity = (center_x - self.position_px) / dt
                measured_velocity = max(
                    -self.max_speed_px_s,
                    min(self.max_speed_px_s, measured_velocity),
                )
                self.velocity_px_s = (
                    (1.0 - self.velocity_alpha) * self.velocity_px_s
                    + self.velocity_alpha * measured_velocity
                )
        else:
            self.velocity_px_s = 0.0
        self.locked = True
        self.position_px = center_x
        self.last_measurement_time_s = timestamp_s
        self._clear_pending()
        self.last_reason = "accepted"
        return candidate

    def _acquire(self, candidates: list[dict], timestamp_s: float):
        eligible = [
            candidate
            for candidate in candidates
            if float(candidate.get("confidence", 0.0)) >= self.acquire_min_confidence
        ]
        if not eligible:
            self._clear_pending()
            self.last_reason = "low_confidence" if candidates else "no_candidate"
            return None, self.last_reason, None

        candidate = max(eligible, key=lambda item: int(item.get("peak", 0)))
        center_x = float(candidate["center_x"])
        if self.pending_x_px is None or self.pending_time_s is None:
            self.pending_x_px = center_x
            self.pending_time_s = timestamp_s
            self.pending_count = 1
        else:
            dt = timestamp_s - self.pending_time_s
            allowed = self.acquire_match_radius_px + self.max_speed_px_s * max(0.0, dt)
            if dt < 0.0 or abs(center_x - self.pending_x_px) > allowed:
                self.pending_x_px = center_x
                self.pending_count = 1
            else:
                self.pending_x_px = center_x
                self.pending_count += 1
            self.pending_time_s = timestamp_s

        if self.pending_count >= self.acquire_confirm_frames:
            accepted = self._accept(candidate, timestamp_s)
            return accepted, "accepted", center_x
        self.last_reason = "acquiring"
        return None, self.last_reason, center_x

    def select(self, candidates: list[dict], timestamp_s: float):
        timestamp_s = float(timestamp_s)
        if not math.isfinite(timestamp_s):
            raise ValueError("measurement timestamp must be finite")

        if not self.locked or self.last_measurement_time_s is None:
            return self._acquire(candidates, timestamp_s)

        age_s = timestamp_s - self.last_measurement_time_s
        if age_s < 0.0:
            self.reset()
            return self._acquire(candidates, timestamp_s)
        if age_s > self.lock_timeout_s:
            self.locked = False
            self.velocity_px_s = 0.0
            self.last_measurement_time_s = None
            self._clear_pending()
            return self._acquire(candidates, timestamp_s)

        expected_x = self.position_px + self.velocity_px_s * age_s
        max_from_last = self.jump_margin_px + self.max_speed_px_s * age_s
        eligible = []
        for candidate in candidates:
            center_x = float(candidate["center_x"])
            if abs(center_x - self.position_px) > max_from_last:
                continue
            if abs(center_x - expected_x) > self.prediction_gate_px:
                continue
            eligible.append(candidate)

        if not eligible:
            self.last_reason = "jump_rejected" if candidates else "no_candidate"
            if candidates:
                self.rejected_jumps += 1
            return None, self.last_reason, expected_x

        strongest_peak = max(1, max(int(item.get("peak", 0)) for item in eligible))

        def tracked_score(candidate: dict) -> float:
            peak_score = int(candidate.get("peak", 0)) / strongest_peak
            distance_score = 1.0 - min(
                1.0,
                abs(float(candidate["center_x"]) - expected_x)
                / self.prediction_gate_px,
            )
            return 0.65 * peak_score + 0.35 * distance_score

        selected = max(eligible, key=tracked_score)
        accepted = self._accept(selected, timestamp_s)
        return accepted, "accepted", expected_x
