#!/usr/bin/env bash
# Makes the promo video and GIF in docs/promo/ without a mouse:
#  1. record.sh runs the app with the demo mouse on a virtual X server while drive.py moves the
#     pointer and clicks through HITS, Save to mouse, Buttons, Sensor and Profiles;
#  2. compose.py adds the title, "Close the app. Play." and outro cards (card.html, Chromium) and
#     the captions, and writes superstrike-studio.mp4 (1920x1080) and superstrike-studio.gif.
# Usage: tools/promo/make_promo.sh [path/to/superstrike-studio]
# Needs: xvfb-run, ffmpeg (libx264), Chromium ($CHROME), python3 with python-xlib, libXcursor,
# the Adwaita cursor theme and the Inter font.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BIN=$(readlink -f "${1:-$ROOT/build/release/superstrike-studio}")
[ -x "$BIN" ] || { echo "Build first: $BIN not found"; exit 1; }
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/config/gtk-4.0"
printf '[Settings]\ngtk-font-name=Inter 11\n' > "$WORK/config/gtk-4.0/settings.ini"
sed -n '/<?xml/,/<\/fontconfig>/p' "$ROOT/tools/screenshots/capture.sh" > "$WORK/fonts.conf"  # Inter, as the screenshots
HOME=$WORK XDG_CONFIG_HOME=$WORK/config FONTCONFIG_FILE=$WORK/fonts.conf GDK_BACKEND=x11 GSK_RENDERER=cairo \
    NO_AT_BRIDGE=1 XCURSOR_THEME=Adwaita XCURSOR_SIZE=24 LANG=C.UTF-8 LANGUAGE=en \
    "$HERE/record.sh" "$BIN" "$WORK/rec"
python3 "$HERE/compose.py" "$WORK/rec" "$ROOT/docs/promo"
