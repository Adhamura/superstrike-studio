#!/bin/bash
# Build SUPERSTRIKE Studio (C++) and install it for the current user: app menu entry, icon,
# desktop launcher. Optional: sudo ./install.sh --udev  (hidraw access rule for the seat user).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ "$1" = "--udev" ]; then
    echo 'KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="046d", TAG+="uaccess"' \
        > /etc/udev/rules.d/70-superstrike.rules
    udevadm control --reload-rules && udevadm trigger --subsystem-match=hidraw
    echo "udev rule installed"; exit 0
fi

cmake -S "$HERE" -B "$HERE/build/release" -DCMAKE_BUILD_TYPE=MinSizeRel >/dev/null
cmake --build "$HERE/build/release" -j"$(nproc)" --target superstrike-studio

BIN="$HOME/.local/bin"; APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons/hicolor/scalable/apps"
mkdir -p "$BIN" "$APPS" "$ICONS"
ln -sf "$HERE/build/release/superstrike-studio" "$BIN/superstrike-studio"
cp "$HERE/assets/io.github.superstrike.Studio.svg" "$ICONS/"

DESK="[Desktop Entry]
Type=Application
Name=SUPERSTRIKE Studio
GenericName=Mouse settings
Comment=HITS actuation, rapid trigger, haptics, buttons, DPI and onboard memory for the PRO X SUPERSTRIKE
Exec=$HERE/build/release/superstrike-studio
Icon=io.github.superstrike.Studio
Terminal=false
Categories=Settings;HardwareSettings;
Keywords=logitech;mouse;superstrike;hits;haptics;dpi;
StartupWMClass=io.github.superstrike.Studio"
echo "$DESK" > "$APPS/io.github.superstrike.Studio.desktop"
for f in "$HOME/Desktop/SUPERSTRIKE Studio.desktop" "$HERE/../SUPERSTRIKE Studio.desktop"; do
    echo "$DESK" > "$f"; chmod +x "$f"; gio set "$f" metadata::trusted true 2>/dev/null || true
done
update-desktop-database "$APPS" 2>/dev/null || true
gtk-update-icon-cache -q "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
echo "Installed: SUPERSTRIKE Studio (app menu, desktop, superstrike-studio)"
