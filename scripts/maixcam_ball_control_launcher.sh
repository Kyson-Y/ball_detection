#!/bin/sh
set -eu

PROJECT_ROOT=/root/ball_detection/current
RUNTIME_ROOT=/root/ball_detection/runtime

if pidof launcher_daemon >/dev/null 2>&1; then
    echo "refusing direct start: launcher_daemon owns the Maix app lifecycle" >&2
    echo "use app id ball_detection_control; see docs/CODEX_HANDOFF.md" >&2
    exit 64
fi

mkdir -p "$RUNTIME_ROOT"

exec python3 "$PROJECT_ROOT/app/maixcam_ball_control.py" \
    --config "$PROJECT_ROOT/config/ball_detection.json" \
    > "$RUNTIME_ROOT/ball_control.log" 2>&1
