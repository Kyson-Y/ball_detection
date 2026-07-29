#!/bin/sh
set -eu

PROJECT_ROOT=/root/ball_detection/current
RUNTIME_ROOT=/root/ball_detection/runtime
mkdir -p "$RUNTIME_ROOT"

exec python3 "$PROJECT_ROOT/app/capture_empty_reference.py" \
    --config "$PROJECT_ROOT/config/ball_detection.json" \
    > "$RUNTIME_ROOT/capture_reference.log" 2>&1
