#!/usr/bin/env bash
# Build SuperStrike Studio on Windows: one exe that runs anywhere, plus the same files as a folder.
# Usage: build-windows.bat (double-click; installs what is missing), or in the "MSYS2 UCRT64" shell:
#   tools/windows/build-windows.sh
# Result: dist/SuperStrike-Studio-windows-x64.exe (the one file to share; tools/windows/onefile.c)
#         dist/SuperStrike-Studio/superstrike-studio.exe (the unpacked folder)
# Safe to run again: the build folder is reused, the dist folder is rebuilt from scratch.
set -euo pipefail

echo "Needed packages (install once):"
echo "  pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,gtk4,libadwaita,librsvg,json-glib,gettext-runtime,gettext-tools,ntldd}"
echo

step() { echo; echo "==> $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

[ "${MSYSTEM:-}" = "UCRT64" ] || die "open the 'MSYS2 UCRT64' shell (not MSYS, MINGW64 or CLANG64) and run this again"
for tool in gcc cmake ninja pkg-config msgfmt ntldd cygpath glib-compile-schemas gdk-pixbuf-query-loaders; do
    command -v "$tool" >/dev/null || die "'$tool' is missing - run the pacman line above"
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="$MSYSTEM_PREFIX"   # /ucrt64
BUILD="$ROOT/build/windows"
OUT="$ROOT/dist/SuperStrike-Studio"
ONE="$ROOT/dist/SuperStrike-Studio-windows-x64.exe"

step "Configure (Ninja, MinSizeRel) in $BUILD"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel

step "Build (also compiles the translations)"
cmake --build "$BUILD"

step "Create $OUT"
rm -rf "$OUT" "$ONE"
mkdir -p "$OUT"
cp "$BUILD/superstrike-studio.exe" "$BUILD/sscheck.exe" "$OUT/"
echo "copied superstrike-studio.exe and sscheck.exe (read-only device check, run it from a shell)"

step "Copy assets (svg, css, icons, locale)"
cp -r "$ROOT/assets" "$OUT/assets"
rm -rf "$OUT/assets/windows"   # icon and resource script are only needed at build time

step "Compile GLib schemas"
mkdir -p "$OUT/share/glib-2.0/schemas"
cp "$PREFIX"/share/glib-2.0/schemas/*.xml "$OUT/share/glib-2.0/schemas/"
glib-compile-schemas "$OUT/share/glib-2.0/schemas"

step "Copy icon themes (Adwaita, hicolor index)"
mkdir -p "$OUT/share/icons/hicolor"
cp -r "$PREFIX/share/icons/Adwaita" "$OUT/share/icons/"
cp "$PREFIX/share/icons/hicolor/index.theme" "$OUT/share/icons/hicolor/"

step "Copy GTK / libadwaita / GLib translations and font config"
for mo in "$PREFIX"/share/locale/*/LC_MESSAGES/{gtk40,gtk40-properties,libadwaita,glib20}.mo; do
    [ -f "$mo" ] || continue
    rel="${mo#"$PREFIX"/}"
    mkdir -p "$OUT/$(dirname "$rel")"
    cp "$mo" "$OUT/$rel"
done
if [ -d "$PREFIX/etc/fonts" ]; then
    mkdir -p "$OUT/etc"
    cp -r "$PREFIX/etc/fonts" "$OUT/etc/"
fi

step "Copy gdk-pixbuf loaders and write a relative loaders.cache"
LOADERS="$OUT/lib/gdk-pixbuf-2.0/2.10.0"
mkdir -p "$LOADERS/loaders"
cp "$PREFIX"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll "$LOADERS/loaders/"
gdk-pixbuf-query-loaders "$LOADERS"/loaders/*.dll \
    | sed -E 's#^"[^"]*loaders(\\\\|/)#"lib\\\\gdk-pixbuf-2.0\\\\2.10.0\\\\loaders\\\\#' > "$LOADERS/loaders.cache"
echo "loaders: $(ls "$LOADERS/loaders" | wc -l), cache entries: $(grep -c '^"' "$LOADERS/loaders.cache")"

# Every DLL (recursively) that the given files need, as MSYS paths.
dll_list() {
    ntldd -R "$@" | sed -n 's/^.*=> *\(.*\) (0x.*$/\1/p' | while IFS= read -r p; do cygpath -u "$p"; done
}

step "Copy the DLLs (only the ones from $PREFIX; Windows system DLLs stay out)"
dll_list "$OUT/superstrike-studio.exe" "$OUT/sscheck.exe" "$LOADERS"/loaders/*.dll | sort -u | while IFS= read -r dll; do
    case "$dll" in "$PREFIX"/*) [ -e "$OUT/$(basename "$dll")" ] || cp "$dll" "$OUT/" ;; esac
done
echo "DLLs copied: $(ls "$OUT"/*.dll | wc -l)"
missing="$(ntldd -R "$OUT/superstrike-studio.exe" | grep -i 'not found' || true)"
if [ -n "$missing" ]; then echo "WARNING: DLLs not found:"; echo "$missing"; fi

step "Create $ONE (launcher + packed folder)"
"$BUILD/ss-pack.exe" "$(cygpath -w "$BUILD/ss-onefile.exe")" "$(cygpath -w "$OUT")" "$(cygpath -w "$ONE")"

step "Done"
echo "folder: $OUT ($(du -sh "$OUT" | cut -f1))"
echo "exe:    $ONE ($(du -h "$ONE" | cut -f1))"
echo "run:    $ONE   (or $OUT/superstrike-studio.exe)"
