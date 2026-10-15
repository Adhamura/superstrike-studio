#!/usr/bin/env bash
# Build SuperStrike Studio on macOS as one app: "SuperStrike Studio.app" (GTK 4, libadwaita and
# every other library inside), delivered in a disk image.
# Usage (Homebrew packages below, once):  tools/macos/build-macos.sh
#   brew install cmake pkgconf gettext gtk4 libadwaita librsvg json-glib adwaita-icon-theme dylibbundler
# Result: dist/SuperStrike Studio.app and dist/SuperStrike-Studio-macos-<arch>.dmg (arm64 or x86_64)
# The app is signed ad hoc (no paid Apple certificate), see the README for the first start.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BREW="$(brew --prefix)"
ARCH="$(uname -m)"
BUILD="$ROOT/build/macos"
APP="$ROOT/dist/SuperStrike Studio.app"
DMG="$ROOT/dist/SuperStrike-Studio-macos-$ARCH.dmg"
C="$APP/Contents"
R="$C/Resources"

step() { echo; echo "==> $*"; }
for tool in cmake pkg-config dylibbundler glib-compile-schemas gdk-pixbuf-query-loaders rsvg-convert; do
    command -v "$tool" >/dev/null || { echo "ERROR: '$tool' is missing - run the brew line at the top of this script"; exit 1; }
done

step "Build"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"
VERSION="$(sed -n 's/^project(superstrike_studio VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"

step "App bundle"
rm -rf "$APP" "$DMG"
mkdir -p "$C/MacOS" "$C/Frameworks" "$R"
cp "$BUILD/superstrike-studio" "$C/MacOS/superstrike-studio"
cp -r "$ROOT/assets" "$R/assets"
rm -rf "$R/assets/windows"

# minimum macOS = what the Homebrew libraries were built for
MINOS="$(otool -l "$BREW/lib/libgtk-4.1.dylib" | awk '/LC_BUILD_VERSION/ { b = 1 } b && $1 == "minos" { print $2; exit }')"
cat > "$C/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>SuperStrike Studio</string>
  <key>CFBundleDisplayName</key><string>SuperStrike Studio</string>
  <key>CFBundleIdentifier</key><string>io.github.superstrike.Studio</string>
  <key>CFBundleExecutable</key><string>superstrike-studio</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>${MINOS:-13.0}</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>SuperStrike Studio contributors. Not affiliated with Logitech.</string>
</dict>
</plist>
EOF

step "Icon"
ICONSET="$BUILD/AppIcon.iconset"
rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
    rsvg-convert -w $s -h $s "$ROOT/assets/io.github.superstrike.Studio.svg" -o "$ICONSET/icon_${s}x${s}.png"
    rsvg-convert -w $((s * 2)) -h $((s * 2)) "$ROOT/assets/io.github.superstrike.Studio.svg" -o "$ICONSET/icon_${s}x${s}@2x.png"
done
iconutil -c icns "$ICONSET" -o "$R/AppIcon.icns"

step "GLib schemas, icon themes, fonts config"
mkdir -p "$R/share/glib-2.0/schemas" "$R/share/icons"
cp "$BREW"/share/glib-2.0/schemas/*.xml "$R/share/glib-2.0/schemas/"
glib-compile-schemas "$R/share/glib-2.0/schemas"
cp -R "$BREW/share/icons/Adwaita" "$R/share/icons/"
mkdir -p "$R/share/icons/hicolor"
cp "$BREW/share/icons/hicolor/index.theme" "$R/share/icons/hicolor/" 2>/dev/null || true
if [ -d "$BREW/etc/fonts" ]; then
    mkdir -p "$R/etc"
    cp -RL "$BREW/etc/fonts" "$R/etc/"
fi

step "GTK / libadwaita translations"
for mo in "$BREW"/share/locale/*/LC_MESSAGES/{gtk40,gtk40-properties,libadwaita,glib20}.mo; do
    [ -f "$mo" ] || continue
    rel="${mo#"$BREW"/}"
    mkdir -p "$R/$(dirname "$rel")"
    cp "$mo" "$R/$rel"
done

step "gdk-pixbuf loaders (cache with a placeholder; the app fills in its path at start)"
L="$R/lib/gdk-pixbuf-2.0/2.10.0"
mkdir -p "$L/loaders"
cp "$BREW"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so "$L/loaders/"
chmod u+w "$L"/loaders/*.so
for so in "$L"/loaders/*.so; do install_name_tool -id "@rpath/${so##*/}" "$so" 2>/dev/null; done  # own name
SRC_LOADERS="$BREW/lib/gdk-pixbuf-2.0/2.10.0/loaders"
gdk-pixbuf-query-loaders "$SRC_LOADERS"/*.so | sed "s#\"$SRC_LOADERS#\"@LOADERS@#" > "$L/loaders.cache.in"
grep -c '^"@LOADERS@' "$L/loaders.cache.in" | xargs echo "loaders in the cache:"

step "Bundle the libraries (dylibbundler)"
args=(-od -b -cd -d "$C/Frameworks" -p "@executable_path/../Frameworks/" -x "$C/MacOS/superstrike-studio")
for so in "$L"/loaders/*.so; do args+=(-x "$so"); done
dylibbundler "${args[@]}" -s "$BREW/lib" >/dev/null

step "Check: nothing may point into Homebrew any more"
bad=0
while IFS= read -r f; do
    if otool -L "$f" | tail -n +2 | grep -E "$BREW|/usr/local/(opt|Cellar|lib)" ; then
        echo "  ^ in $f"; bad=1
    fi
done < <(find "$C/MacOS" "$C/Frameworks" "$L/loaders" -type f)
[ "$bad" = 0 ] || { echo "ERROR: the app still needs Homebrew files"; exit 1; }

step "Sign (ad hoc)"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

step "Disk image"
STAGE="$BUILD/dmg"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "SuperStrike Studio" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null

step "Done"
echo "app:  $APP ($(du -sh "$APP" | cut -f1), macOS ${MINOS:-?}+, $ARCH)"
echo "dmg:  $DMG ($(du -h "$DMG" | cut -f1))"
