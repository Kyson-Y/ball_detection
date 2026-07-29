#!/bin/sh
set -eu

AUTOSTART=/maixapp/auto_start.txt
BACKUP=/maixapp/auto_start.txt.before_ball_detection
APP_ID=ball_detection_control
APP_MAIN=/maixapp/apps/$APP_ID/main.py

test "$(id -u)" -eq 0
test -r "$APP_MAIN"
if test ! -e "$BACKUP"; then
    cp -p "$AUTOSTART" "$BACKUP"
fi

printf '%s' "$APP_ID" > "$AUTOSTART.incoming"
test "$(cat "$AUTOSTART.incoming")" = "$APP_ID"
mv "$AUTOSTART.incoming" "$AUTOSTART"
sync
echo "autostart_enabled app_id=$APP_ID"
