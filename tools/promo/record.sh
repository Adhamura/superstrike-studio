#!/usr/bin/env bash
# Records the app with the demo mouse while drive.py clicks through it: OUT.mp4 + OUT.marks.
# Runs itself inside xvfb-run. Usage: record.sh BIN OUT_PREFIX   (env as in capture.sh)
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
if [ -z "${SS_REC_INNER:-}" ]; then
    SS_REC_INNER=1 exec xvfb-run -a -s "-screen 0 1600x1000x24" "$0" "$@"
fi
BIN=$1 OUT=$2
DUMP=$(mktemp)
SS_SHOT_DUMP=$DUMP SS_SHOT_DELAY_MS=600000 SS_SHOT_CLEAN=1 "$BIN" --demo --shot "hits:/dev/null" >/dev/null 2>&1 & APP=$!
trap 'kill $APP 2>/dev/null || true; rm -f "$DUMP"' EXIT
sleep 5
ffmpeg -loglevel error -y -f x11grab -draw_mouse 1 -framerate 30 -video_size 1310x850 -i "$DISPLAY+0,0" \
    -c:v libx264 -preset veryfast -crf 14 -pix_fmt yuv420p "$OUT.mp4" & REC=$!
sleep 0.5
python3 "$HERE/drive.py" "$DUMP" "$OUT.marks"
kill -INT $REC; wait $REC || true
