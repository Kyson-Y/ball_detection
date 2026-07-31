from __future__ import annotations

import sys


APP_DIRECTORY = "/root/ball_detection/current/app"
if APP_DIRECTORY not in sys.path:
    sys.path.insert(0, APP_DIRECTORY)

from maixcam_ai_ball import DEFAULT_CONFIG_PATH, run


if __name__ == "__main__":
    raise SystemExit(run(DEFAULT_CONFIG_PATH))
