#!/usr/bin/env bash
# Makes every picture in docs/screenshots/ without a mouse: the app runs with the demo mouse
# (--demo, src/hid_sim.cpp) on a virtual X server and renders each page itself (--shot).
#   tools/screenshots/capture.sh [path/to/superstrike-studio]   (default: build/release/...)
# Needs: xvfb-run, the Inter font (Adwaita Sans is based on it), and for the language shots the
# ja_JP.UTF-8 and ar_SA.UTF-8 locales (sudo localedef -i ja_JP -f UTF-8 ja_JP.UTF-8, same for ar_SA).
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)
BIN=$(readlink -f "${1:-$ROOT/build/release/superstrike-studio}")
OUT=$ROOT/docs/screenshots
[ -x "$BIN" ] || { echo "Build first: $BIN not found (./install.sh or cmake --build)"; exit 1; }
mkdir -p "$OUT"

WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/config/gtk-4.0"
printf '[Settings]\ngtk-font-name=Inter 11\n' > "$WORK/config/gtk-4.0/settings.ini"
# the same UI font on every machine: Adwaita Sans / Cantarell / sans-serif -> Inter
cat > "$WORK/fonts.conf" <<'XML'
<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
  <match target="pattern"><test name="family"><string>Adwaita Sans</string></test><edit name="family" mode="prepend" binding="strong"><string>Inter</string></edit></match>
  <match target="pattern"><test name="family"><string>Cantarell</string></test><edit name="family" mode="prepend" binding="strong"><string>Inter</string></edit></match>
  <match target="pattern"><test name="family"><string>sans-serif</string></test><edit name="family" mode="prepend" binding="strong"><string>Inter</string></edit></match>
</fontconfig>
XML
export HOME=$WORK XDG_CONFIG_HOME=$WORK/config FONTCONFIG_FILE=$WORK/fonts.conf
export GDK_BACKEND=x11 GSK_RENDERER=cairo NO_AT_BRIDGE=1

# shot NAME PAGE [VAR=value ...]: one PNG of PAGE, with the given SS_SHOT_* settings
shot() {
    local name=$1 page=$2; shift 2
    env LANG=C.UTF-8 LANGUAGE=en SS_SHOT_CLEAN=1 "$@" \
        xvfb-run -a -s "-screen 0 1600x1000x24" "$BIN" --demo --shot "$page:$OUT/$name.png" >/dev/null 2>&1
    echo "  $name.png"
}

echo "Writing to $OUT"
shot hits          hits     SS_SHOT_CLEAN=
shot hits-rapid    hits     SS_SHOT_PROFILE=2 SS_SHOT_SELECT=rt
shot hits-presets  hits     SS_SHOT_SELECT=presets
shot buttons       buttons  SS_SHOT_PROFILE=4 SS_SHOT_SELECT=side
shot sensor        sensor   SS_SHOT_PROFILE=3
shot profiles      profiles SS_SHOT_CLEAN=
shot device        device
shot lang-ja       profiles LANG=ja_JP.UTF-8 LANGUAGE=ja
shot lang-ar       sensor   LANG=ar_SA.UTF-8 LANGUAGE=ar
