from __future__ import annotations

import sys
from pathlib import Path
from types import SimpleNamespace


APP = Path(__file__).resolve().parents[1] / "app"
sys.path.insert(0, str(APP))

from ai_ball_detector import YoloBallSelector


CONFIG = {
    "detector": {
        "class_id": 0,
        "min_box_px": 4.0,
        "max_box_px": 48.0,
        "min_aspect": 0.45,
        "max_aspect": 2.2,
    },
    "continuity_gate": {
        "max_speed_model_px_s": 100.0,
        "jump_margin_model_px": 4.0,
        "prediction_gate_model_px": 20.0,
        "acquire_confirm_frames": 2,
        "acquire_radius_model_px": 5.0,
        "lock_timeout_s": 0.25,
        "velocity_alpha": 0.5,
        "ambiguity_score_margin": 0.08,
        "ambiguity_separation_model_px": 28.0,
    },
}


def obj(x: float, score: float = 0.9):
    return SimpleNamespace(class_id=0, x=x, y=40, w=10, h=10, score=score)


def test_requires_two_frames_to_acquire() -> None:
    selector = YoloBallSelector(CONFIG, 320, 64)
    candidates = selector.candidates([obj(20)])
    selected, reason, _ = selector.select(candidates, 0.00)
    assert selected is None
    assert reason == "acquiring"
    selected, reason, _ = selector.select(candidates, 0.03)
    assert selected is not None
    assert reason == "accepted"


def test_rejects_impossible_jump_and_keeps_lock() -> None:
    selector = YoloBallSelector(CONFIG, 320, 64)
    first = selector.candidates([obj(20)])
    selector.select(first, 0.00)
    selector.select(first, 0.03)
    jump = selector.candidates([obj(250)])
    selected, reason, expected = selector.select(jump, 0.06)
    assert selected is None
    assert reason == "jump_rejected"
    assert expected is not None
    assert selector.locked


def test_rejects_conflicting_distant_candidates() -> None:
    selector = YoloBallSelector(CONFIG, 320, 64)
    candidates = selector.candidates([obj(20, 0.90), obj(200, 0.87)])
    selected, reason, _ = selector.select(candidates, 0.00)
    assert selected is None
    assert reason == "ambiguous"


def test_filters_implausible_box_shape() -> None:
    selector = YoloBallSelector(CONFIG, 320, 64)
    wide = SimpleNamespace(class_id=0, x=20, y=40, w=40, h=5, score=0.9)
    assert selector.candidates([wide]) == []
