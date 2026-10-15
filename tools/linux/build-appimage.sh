#!/usr/bin/env bash
# Build SuperStrike Studio as one AppImage that runs on any x86_64 Linux (glibc, GTK 4 and
# libadwaita are inside; sharun from pkgforge-dev/Anylinux-AppImages, which wants an Arch build).
# Usage (Arch Linux, as in CI; packages below):  tools/linux/build-appimage.sh
# Result: dist/SuperStrike-Studio-x86_64.AppImage
#   pacman -S --needed base-devel cmake gettext gtk4 libadwaita librsvg json-glib glib-networking \
#     xorg-server-xvfb xorg-xauth patchelf binutils wget
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/build/appimage"
WORK="$ROOT/build/appimage-work"
APPDIR="$WORK/AppDir"
OUT="$ROOT/dist/SuperStrike-Studio-x86_64.AppImage"
# quick-sharun, pinned (it downloads sharun and appimagetool and checks their sha256 itself)
QS_COMMIT=2607cf8e8846f7a3cd0501272e8fd10e32efa640
QS_URL="https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/$QS_COMMIT/useful-tools/quick-sharun.sh"

step() { echo; echo "==> $*"; }

step "Build"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build "$BUILD" -j"$(nproc)" --target superstrike-studio

step "AppDir"
rm -rf "$WORK" "$OUT"
mkdir -p "$WORK" "$ROOT/dist"
cat > "$WORK/io.github.superstrike.Studio.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=SuperStrike Studio
GenericName=Mouse settings
Comment=HITS actuation, rapid trigger, haptics, buttons, DPI and onboard memory for the PRO X SUPERSTRIKE
Exec=superstrike-studio
Icon=io.github.superstrike.Studio
Terminal=false
Categories=Settings;HardwareSettings;
Keywords=logitech;mouse;superstrike;hits;haptics;dpi;
StartupWMClass=io.github.superstrike.Studio
EOF
# the app finds them through $APPDIR (asset_path in src/levelbars.cpp)
mkdir -p "$APPDIR/share/superstrike-studio"
cp -r "$ROOT/assets" "$APPDIR/share/superstrike-studio/assets"
rm -rf "$APPDIR/share/superstrike-studio/assets/windows"

wget -qO "$WORK/quick-sharun.sh" "$QS_URL"
chmod +x "$WORK/quick-sharun.sh"

step "Bundle the libraries (quick-sharun)"
cd "$WORK"
export APPDIR OUTPATH="$WORK/out"
export DESKTOP="$WORK/io.github.superstrike.Studio.desktop" ICON="$ROOT/assets/io.github.superstrike.Studio.svg"
export DEPLOY_GTK=1 DEPLOY_GDK=1 DEPLOY_GLIB_NETWORKING=1   # TLS for the update check
export STRACE_FLAGS="--demo"   # the short test run to find dlopen()ed libraries uses the demo mouse
./quick-sharun.sh "$BUILD/superstrike-studio"

step "Make the AppImage"
# no zsync update info: the app checks GitHub releases itself
env -u GITHUB_REPOSITORY UPINFO="" ./quick-sharun.sh --make-appimage
mv "$WORK"/out/*.AppImage "$OUT"
chmod +x "$OUT"

step "Done"
echo "AppImage: $OUT ($(du -h "$OUT" | cut -f1))"
