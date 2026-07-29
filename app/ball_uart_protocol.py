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
