from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_today_geometry_and_model_contract() -> None:
    config = json.loads((ROOT / "config" / "ai_ball.json").read_text())
    assert config["model"]["input_width"] == 320
    assert config["model"]["input_height"] == 64
    assert config["roi"] == {
        "x": 0,
        "y": 176,
        "width": 640,
        "height": 128,
    }


def test_uart_and_preview_contract_remain_compatible() -> None:
    config = json.loads((ROOT / "config" / "ai_ball.json").read_text())
    assert config["uart"] == {
        "bus": 0,
        "device": "/dev/ttyS0",
        "baud_rate": 115200,
        "tx_pin": "A16",
        "rx_pin": "A17",
        "retry_interval_s": 2.0,
    }
    assert config["status_server"]["port"] == 8080
    assert config["status_server"]["images_enabled"] is False
    assert config["status_server"]["preview_fps"] == 0.0


def test_software_thermal_stop_is_disabled() -> None:
    config = json.loads((ROOT / "config" / "ai_ball.json").read_text())
    assert config["thermal"]["stop_enabled"] is False
    assert config["thermal"]["warning_mc"] == 70000
