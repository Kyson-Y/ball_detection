#!/bin/sh
set -eu

AUTOSTART=/maixapp/auto_start.txt
BACKUP=/maixapp/auto_start.txt.before_ball_detection

test "$(id -u)" -eq 0
if test -f "$BACKUP"; then
    cp -p "$BACKUP" "$AUTOSTART.incoming"
else
    printf '\n' > "$AUTOSTART.incoming"
fi
mv "$AUTOSTART.incoming" "$AUTOSTART"
sync
echo autostart_disabled
