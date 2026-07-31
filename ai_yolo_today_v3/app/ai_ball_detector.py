from __future__ import annotations

import math


class YoloBallSelector:
    def __init__(self, config: dict, input_width: int, input_height: int) -> None:
        detector = config["detector"]
        gate = config["continuity_gate"]
        self.input_width = int(input_width)
        self.input_height = int(input_height)
        self.class_id = int(detector["class_id"])
        self.min_box_px = float(detector["min_box_px"])
        self.max_box_px = float(detector["max_box_px"])
        self.min_aspect = float(detector["min_aspect"])
        self.max_aspect = float(detector["max_aspect"])
        self.max_speed_px_s = float(gate["max_speed_model_px_s"])
        self.jump_margin_px = float(gate["jump_margin_model_px"])
        self.prediction_gate_px = float(gate["prediction_gate_model_px"])
        self.acquire_frames = int(gate["acquire_confirm_frames"])
        self.acquire_radius_px = float(gate["acquire_radius_model_px"])
        self.lock_timeout_s = float(gate["lock_timeout_s"])
        self.velocity_alpha = float(gate["velocity_alpha"])
        self.ambiguity_margin = float(gate["ambiguity_score_margin"])
        self.ambiguity_separation_px = float(
            gate["ambiguity_separation_model_px"]
        )
        self.reset()

    def reset(self) -> None:
        self.locked = False
        self.x = 0.0
        self.velocity_px_s = 0.0
        self.last_time_s = None
        self.pending_x = None
        self.pending_time_s = None
        self.pending_count = 0
        self.rejected_jumps = 0
        self.ambiguous_frames = 0
        self.last_reason = "reset"

    def candidates(self, objects) -> list[dict]:
        result = []
        for obj in objects:
            if int(obj.class_id) != self.class_id:
                continue
            width = float(obj.w)
            height = float(obj.h)
            if not (
                self.min_box_px <= width <= self.max_box_px
                and self.min_box_px <= height <= self.max_box_px
            ):
                continue
            aspect = width / max(height, 1.0)
            if not self.min_aspect <= aspect <= self.max_aspect:
                continue
            center_x = float(obj.x) + width * 0.5
            center_y = float(obj.y) + height * 0.5
            if not (
                0.0 <= center_x < self.input_width
                and 0.0 <= center_y < self.input_height
            ):
                continue
            result.append(
                {
                    "x": float(obj.x),
                    "y": float(obj.y),
                    "w": width,
                    "h": height,
                    "center_x": center_x,
                    "center_y": center_y,
                    "confidence": float(obj.score),
                }
            )
        return result

    def _ambiguous(self, ranked: list[tuple[float, dict]]) -> bool:
        if len(ranked) < 2:
            return False
        first_score, first = ranked[0]
        second_score, second = ranked[1]
        return (
            first_score - second_score < self.ambiguity_margin
            and abs(first["center_x"] - second["center_x"])
            > self.ambiguity_separation_px
        )

    def _accept(self, candidate: dict, timestamp_s: float) -> dict:
        center_x = float(candidate["center_x"])
        if self.locked and self.last_time_s is not None:
            dt = timestamp_s - self.last_time_s
            if dt > 0.0:
                measured_velocity = (center_x - self.x) / dt
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
        self.x = center_x
        self.last_time_s = timestamp_s
        self.pending_x = None
        self.pending_time_s = None
        self.pending_count = 0
        self.last_reason = "accepted"
        return candidate

    def _acquire(self, candidates: list[dict], timestamp_s: float):
        ranked = sorted(
            ((candidate["confidence"], candidate) for candidate in candidates),
            key=lambda item: item[0],
            reverse=True,
        )
        if not ranked:
            self.pending_count = 0
            self.pending_x = None
            self.last_reason = "no_candidate"
            return None, self.last_reason, None
        if self._ambiguous(ranked):
            self.pending_count = 0
            self.pending_x = None
            self.ambiguous_frames += 1
            self.last_reason = "ambiguous"
            return None, self.last_reason, None

        candidate = ranked[0][1]
        center_x = candidate["center_x"]
        if self.pending_x is None or self.pending_time_s is None:
            self.pending_count = 1
        else:
            dt = max(0.0, timestamp_s - self.pending_time_s)
            allowed = self.acquire_radius_px + self.max_speed_px_s * dt
            self.pending_count = (
                self.pending_count + 1
                if abs(center_x - self.pending_x) <= allowed
                else 1
            )
        self.pending_x = center_x
        self.pending_time_s = timestamp_s
        if self.pending_count >= self.acquire_frames:
            return self._accept(candidate, timestamp_s), "accepted", center_x
        self.last_reason = "acquiring"
        return None, self.last_reason, center_x

    def select(self, candidates: list[dict], timestamp_s: float):
        timestamp_s = float(timestamp_s)
        if not math.isfinite(timestamp_s):
            raise ValueError("timestamp must be finite")
        if not self.locked or self.last_time_s is None:
            return self._acquire(candidates, timestamp_s)

        age_s = timestamp_s - self.last_time_s
        if age_s < 0.0 or age_s > self.lock_timeout_s:
            self.locked = False
            self.velocity_px_s = 0.0
            self.last_time_s = None
            return self._acquire(candidates, timestamp_s)

        expected_x = self.x + self.velocity_px_s * age_s
        max_from_last = self.jump_margin_px + self.max_speed_px_s * age_s
        eligible = []
        for candidate in candidates:
            center_x = candidate["center_x"]
            if abs(center_x - self.x) > max_from_last:
                continue
            if abs(center_x - expected_x) > self.prediction_gate_px:
                continue
            distance_score = 1.0 - min(
                1.0, abs(center_x - expected_x) / self.prediction_gate_px
            )
            score = 0.75 * distance_score + 0.25 * candidate["confidence"]
            eligible.append((score, candidate))
        eligible.sort(key=lambda item: item[0], reverse=True)

        if not eligible:
            if candidates:
                self.rejected_jumps += 1
                self.last_reason = "jump_rejected"
            else:
                self.last_reason = "no_candidate"
            return None, self.last_reason, expected_x
        if self._ambiguous(eligible):
            self.ambiguous_frames += 1
            self.last_reason = "ambiguous"
            return None, self.last_reason, expected_x
        return self._accept(eligible[0][1], timestamp_s), "accepted", expected_x
