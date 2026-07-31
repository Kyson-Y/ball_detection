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


def test_uart_and_media_rate_contract_remain_compatible() -> None:
    config = json.loads((ROOT / "config" / "ai_ball.json").read_text())
    assert config["uart"] == {
        "bus": 0,
        "device": "/dev/ttyS0",
        "baud_rate": 115200,
        "tx_pin": "A16",
        "rx_pin": "A17",
        "retry_interval_s": 2.0,
        "output_hz": 60.0,
        "max_valid_age_ms": 50,
        "prediction_horizon_ms": 25,
    }
    assert config["camera"]["fps"] == 60.0
    assert config["status_server"]["port"] == 8080
    assert config["status_server"]["update_fps"] == 15.0
    assert config["status_server"]["preview_fps"] == 0.0
    assert config["media"]["enabled"] is True
    assert config["media"]["rtsp"]["width"] == 160
    assert config["media"]["rtsp"]["height"] == 120
    assert config["media"]["rtsp"]["fps"] == 60
    assert config["media"]["rtsp"]["bitrate"] == 300_000
