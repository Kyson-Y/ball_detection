from __future__ import annotations

import numpy as np


class BallDetector:
    def __init__(self, config: dict, reference: np.ndarray) -> None:
        camera = config["camera"]
        roi = config["roi"]
        alignment = config["alignment"]
        detector = config["detector"]

        self.frame_width = int(camera["width"])
        self.frame_height = int(camera["height"])
        self.roi_x = int(roi["x"])
        self.roi_y = int(roi["y"])
        self.roi_w = int(roi["width"])
        self.roi_h = int(roi["height"])
        self.origin_x = self.roi_x + self.roi_w / 2.0
        self.analysis_y0 = int(roi["analysis_y0"])
        self.analysis_y1 = int(roi["analysis_y1"])

        self.alignment_enabled = bool(alignment["enabled"])
        self.shift_min = int(alignment["shift_min"])
        self.shift_max = int(alignment["shift_max"])
        self.align_y0 = int(alignment["profile_y0"])
        self.align_y1 = int(alignment["profile_y1"])
        self.align_x0 = int(alignment["profile_x0"])
        self.align_x1 = int(alignment["profile_x1"])
        self.align_x_step = int(alignment["profile_x_step"])
        self.align_diff_cap = int(alignment["difference_cap"])

        self.pixel_diff_threshold = int(detector["pixel_diff_threshold"])
        self.ball_window_px = int(detector["ball_window_px"])
        self.search_margin_px = int(detector["search_margin_px"])
        self.min_peak_response = int(detector["min_peak_response"])
        self.peak_mad_multiplier = float(detector["peak_mad_multiplier"])
        self.max_changed_ratio = float(detector["max_changed_ratio"])
        self.local_brightness_enabled = bool(
            detector.get("local_brightness_enabled", False)
        )
        self.local_brightness_sample_step = int(
            detector.get("local_brightness_sample_step", 4)
        )
        self.local_brightness_window_px = int(
            detector.get("local_brightness_window_px", 31)
        )
        self.local_brightness_max_offset = int(
            detector.get("local_brightness_max_offset", 12)
        )
        self.max_candidates = int(detector.get("max_candidates", 5))
        self.candidate_suppression_px = int(
            detector.get("candidate_suppression_px", self.ball_window_px)
        )
        self.min_candidate_changed_pixels = int(
            detector.get("min_candidate_changed_pixels", self.min_peak_response)
        )
        self.min_candidate_width_px = int(detector.get("min_candidate_width_px", 1))
        self.min_candidate_height_px = int(
            detector.get("min_candidate_height_px", 1)
        )

        self._validate_geometry()
        if reference.dtype != np.uint8 or reference.shape != (self.roi_h, self.roi_w):
            raise ValueError(
                f"invalid reference dtype={reference.dtype} shape={reference.shape}; "
                f"expected uint8 ({self.roi_h}, {self.roi_w})"
            )
        self.reference_i16 = reference.astype(np.int16)
        self.reference_profile = np.median(
            self.reference_i16[
                self.align_y0 : self.align_y1,
                self.align_x0 : self.align_x1 : self.align_x_step,
            ],
            axis=1,
        ).astype(np.int16)
        self.window_kernel = np.ones(self.ball_window_px, dtype=np.int32)

    def _validate_geometry(self) -> None:
        if self.roi_w <= 0 or self.roi_h <= 0:
            raise ValueError("ROI dimensions must be positive")
        if self.roi_x < 0 or self.roi_x + self.roi_w > self.frame_width:
            raise ValueError("ROI x range is outside the frame")
        if self.roi_y + self.shift_min < 0:
            raise ValueError("aligned ROI can move above the frame")
        if self.roi_y + self.shift_max + self.roi_h > self.frame_height:
            raise ValueError("aligned ROI can move below the frame")
        if not 0 <= self.analysis_y0 < self.analysis_y1 <= self.roi_h:
            raise ValueError("invalid analysis y range")
        if not 0 <= self.align_y0 < self.align_y1 <= self.roi_h:
            raise ValueError("invalid alignment profile y range")
        if not 0 <= self.align_x0 < self.align_x1 <= self.roi_w:
            raise ValueError("invalid alignment profile x range")
        if self.align_x_step <= 0:
            raise ValueError("alignment x step must be positive")
        if self.search_margin_px * 2 >= self.roi_w:
            raise ValueError("search margins leave no valid x range")
        if self.local_brightness_sample_step <= 0:
            raise ValueError("local brightness sample step must be positive")
        if not 1 <= self.local_brightness_window_px <= self.roi_w:
            raise ValueError("invalid local brightness window")
        if self.local_brightness_window_px % 2 == 0:
            raise ValueError("local brightness window must be odd")
        if self.local_brightness_max_offset < 0:
            raise ValueError("local brightness maximum offset cannot be negative")
        if self.max_candidates <= 0:
            raise ValueError("max candidates must be positive")
        if self.candidate_suppression_px <= 0:
            raise ValueError("candidate suppression must be positive")
        if self.min_candidate_changed_pixels < 0:
            raise ValueError("candidate changed pixel minimum cannot be negative")
        if self.min_candidate_width_px <= 0 or self.min_candidate_height_px <= 0:
            raise ValueError("candidate dimensions must be positive")

    def validate_frame(self, gray: np.ndarray) -> None:
        if gray.dtype != np.uint8 or gray.shape != (self.frame_height, self.frame_width):
            raise ValueError(
                f"invalid frame dtype={gray.dtype} shape={gray.shape}; expected "
                f"uint8 ({self.frame_height}, {self.frame_width})"
            )

    def estimate_vertical_shift(self, gray: np.ndarray):
        self.validate_frame(gray)
        if not self.alignment_enabled:
            return 0, 0.0

        search_y0 = self.roi_y + self.shift_min + self.align_y0
        search_y1 = self.roi_y + self.shift_max + self.align_y1
        current_profile = np.median(
            gray[
                search_y0:search_y1,
                self.roi_x
                + self.align_x0 : self.roi_x
                + self.align_x1 : self.align_x_step,
            ],
            axis=1,
        ).astype(np.int16)

        best_shift = 0
        best_cost = float("inf")
        for shift in range(self.shift_min, self.shift_max + 1):
            start = shift - self.shift_min
            candidate = current_profile[start : start + self.reference_profile.size]
            delta = candidate - self.reference_profile
            delta -= int(np.median(delta))
            np.abs(delta, out=delta)
            cost = float(np.mean(np.minimum(delta, self.align_diff_cap)))
            if cost < best_cost:
                best_shift = shift
                best_cost = cost
        return best_shift, best_cost

    def _compensate_brightness(self, delta: np.ndarray):
        brightness_offset = int(np.median(delta[::4, ::4]))
        delta -= brightness_offset
        local_offset_max = 0
        if not self.local_brightness_enabled:
            return brightness_offset, local_offset_max

        column_offsets = np.median(
            delta[:: self.local_brightness_sample_step], axis=0
        ).astype(np.float32)
        if self.local_brightness_window_px > 1:
            half_window = self.local_brightness_window_px // 2
            padded = np.pad(column_offsets, (half_window, half_window), mode="edge")
            kernel = np.full(
                self.local_brightness_window_px,
                1.0 / self.local_brightness_window_px,
                dtype=np.float32,
            )
            column_offsets = np.convolve(padded, kernel, mode="valid")
        np.clip(
            column_offsets,
            -self.local_brightness_max_offset,
            self.local_brightness_max_offset,
            out=column_offsets,
        )
        local_offset_max = int(round(float(np.max(np.abs(column_offsets)))))
        delta -= np.rint(column_offsets).astype(np.int16)[None, :]
        return brightness_offset, local_offset_max

    def _measure_candidate(
        self,
        *,
        peak_x: int,
        peak_response: int,
        threshold: int,
        baseline: float,
        score: np.ndarray,
        changed: np.ndarray,
    ) -> dict:
        centroid_radius = self.ball_window_px
        x0 = max(self.search_margin_px, peak_x - centroid_radius)
        x1 = min(
            self.roi_w - self.search_margin_px,
            peak_x + centroid_radius + 1,
        )
        score_baseline = float(
            np.median(score[self.search_margin_px : self.roi_w - self.search_margin_px])
        )
        weights = np.maximum(score[x0:x1].astype(np.float32) - score_baseline, 0.0)
        weight_sum = float(np.sum(weights))
        center_x = float(peak_x)
        if weight_sum > 0.0:
            xs = np.arange(x0, x1, dtype=np.float32)
            center_x = float(np.sum(xs * weights) / weight_sum)

        local_x0 = max(0, int(round(center_x)) - self.ball_window_px // 2)
        local_x1 = min(self.roi_w, local_x0 + self.ball_window_px)
        local_x0 = max(0, local_x1 - self.ball_window_px)
        local_changed = changed[:, local_x0:local_x1]
        row_score = np.count_nonzero(local_changed, axis=1).astype(np.float32)
        row_weight_sum = float(np.sum(row_score))
        center_y = float((self.analysis_y0 + self.analysis_y1 - 1) / 2.0)
        if row_weight_sum > 0.0:
            ys = np.arange(self.analysis_y0, self.analysis_y1, dtype=np.float32)
            center_y = float(np.sum(ys * row_score) / row_weight_sum)

        changed_pixels = int(np.count_nonzero(local_changed))
        candidate_width = int(
            np.count_nonzero(np.count_nonzero(local_changed, axis=0))
        )
        candidate_height = int(
            np.count_nonzero(np.count_nonzero(local_changed, axis=1))
        )
        strength = (peak_response - threshold) / max(1.0, float(peak_response))
        prominence = (peak_response - baseline) / max(1.0, float(peak_response))
        density = peak_response / max(
            1.0, float(self.ball_window_px * (self.analysis_y1 - self.analysis_y0))
        )
        confidence = float(
            np.clip(0.55 * strength + 0.25 * density + 0.20 * prominence, 0.0, 1.0)
        )
        return {
            "center_x": center_x,
            "center_y": center_y,
            "offset_x": center_x - self.roi_w / 2.0,
            "peak": int(peak_response),
            "confidence": confidence,
            "changed_pixels": changed_pixels,
            "candidate_width": candidate_width,
            "candidate_height": candidate_height,
        }

    def _find_candidates(
        self,
        *,
        response: np.ndarray,
        threshold: int,
        baseline: float,
        score: np.ndarray,
        changed: np.ndarray,
    ) -> list[dict]:
        work = response.copy()
        work[: self.search_margin_px] = 0
        work[self.roi_w - self.search_margin_px :] = 0
        candidates = []
        for _ in range(self.max_candidates):
            peak_x = int(np.argmax(work))
            peak_response = int(work[peak_x])
            if peak_response < threshold:
                break
            candidate = self._measure_candidate(
                peak_x=peak_x,
                peak_response=peak_response,
                threshold=threshold,
                baseline=baseline,
                score=score,
                changed=changed,
            )
            if (
                candidate["changed_pixels"] >= self.min_candidate_changed_pixels
                and candidate["candidate_width"] >= self.min_candidate_width_px
                and candidate["candidate_height"] >= self.min_candidate_height_px
            ):
                candidates.append(candidate)
            suppress_x0 = max(0, peak_x - self.candidate_suppression_px)
            suppress_x1 = min(
                self.roi_w, peak_x + self.candidate_suppression_px + 1
            )
            work[suppress_x0:suppress_x1] = 0
        return candidates

    def detect(self, gray: np.ndarray, vertical_shift: int, alignment_cost: float) -> dict:
        self.validate_frame(gray)
        roi_y = self.roi_y + int(vertical_shift)
        roi = gray[roi_y : roi_y + self.roi_h, self.roi_x : self.roi_x + self.roi_w]

        delta = roi.astype(np.int16)
        delta -= self.reference_i16
        brightness_offset, local_brightness_offset_max = self._compensate_brightness(
            delta
        )
        np.abs(delta, out=delta)

        analysis_diff = delta[self.analysis_y0 : self.analysis_y1]
        changed = analysis_diff >= self.pixel_diff_threshold
        changed_ratio = float(np.mean(changed))
        score = np.count_nonzero(changed, axis=0).astype(np.int32)
        response = np.convolve(score, self.window_kernel, mode="same")

        valid = response[self.search_margin_px : self.roi_w - self.search_margin_px]
        baseline = float(np.median(valid))
        mad = float(np.median(np.abs(valid - baseline)))
        threshold = max(
            self.min_peak_response,
            int(baseline + self.peak_mad_multiplier * max(1.0, mad)),
        )

        reference_mismatch = changed_ratio > self.max_changed_ratio
        candidates = self._find_candidates(
            response=response,
            threshold=threshold,
            baseline=baseline,
            score=score,
            changed=changed,
        )
        peak_local = int(np.argmax(valid))
        peak_x = peak_local + self.search_margin_px
        peak_response = int(response[peak_x])
        selected = candidates[0] if candidates else None
        raw_detected = selected is not None
        detected = raw_detected and not reference_mismatch

        center_x = float(peak_x) if selected is None else float(selected["center_x"])
        center_y = float((self.analysis_y0 + self.analysis_y1 - 1) / 2.0)
        confidence = 0.0
        if selected is not None:
            center_y = float(selected["center_y"])
            confidence = float(selected["confidence"]) if detected else 0.0

        return {
            "detected": detected,
            "raw_detected": raw_detected,
            "reference_mismatch": reference_mismatch,
            "center_x": center_x,
            "center_y": center_y,
            "offset_x": center_x - self.roi_w / 2.0 if detected else None,
            "peak": peak_response,
            "threshold": threshold,
            "confidence": confidence,
            "brightness_offset": brightness_offset,
            "local_brightness_offset_max": local_brightness_offset_max,
            "changed_ratio": changed_ratio,
            "vertical_shift": int(vertical_shift),
            "alignment_cost": float(alignment_cost),
            "roi_y": roi_y,
            "candidates": candidates,
            "candidate_count": len(candidates),
            "raw_center_x": None if selected is None else float(selected["center_x"]),
            "measurement_rejected": False,
            "rejection_reason": "",
        }
