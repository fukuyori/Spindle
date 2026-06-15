#!/usr/bin/env bash
# Build Spindle on Linux and package it as:
#   - a self-contained AppImage (bundles Qt incl. WebEngine), and
#   - a .deb / .tar.gz via CPack (depends on system Qt 6).
#
# Requires: Qt 6 dev packages, CMake, C++17 toolchain.
# AppImage additionally uses linuxdeploy + linuxdeploy-plugin-qt; if absent they
# are downloaded to scripts/.cache (set NO_DOWNLOAD=1 to disable, SKIP_APPIMAGE=1
# to skip the AppImage entirely).
#
# Output: dist/
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DIST_DIR="$ROOT/dist"
CACHE="$ROOT/scripts/.cache"
VERSION="$(sed -n 's/.*project(Spindle VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
VERSION="${VERSION:-0.0.0}"

"$ROOT/scripts/build.sh"
mkdir -p "$DIST_DIR"

# ---- CPack: .deb (+ .tar.gz) ---------------------------------------------
echo "==> CPack (DEB/TGZ)"
( cd "$BUILD_DIR" && cpack -G "DEB;TGZ" ) || echo "   (CPack DEB skipped — dpkg tools missing?)"
find "$BUILD_DIR" -maxdepth 1 \( -name 'Spindle-*.deb' -o -name 'Spindle-*.tar.gz' \) \
  -exec mv -f {} "$DIST_DIR/" \; 2>/dev/null || true

# ---- AppImage (self-contained) -------------------------------------------
if [ "${SKIP_APPIMAGE:-0}" = "1" ]; then
  echo "==> Skipping AppImage (SKIP_APPIMAGE=1)"; echo "==> Done. See $DIST_DIR"; exit 0
fi

fetch() { # url dest
  [ -f "$2" ] && return 0
  [ "${NO_DOWNLOAD:-0}" = "1" ] && return 1
  mkdir -p "$(dirname "$2")"
  echo "   downloading $(basename "$2")"
  if command -v curl >/dev/null 2>&1; then curl -fsSL "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then wget -q "$1" -O "$2"
  else return 1; fi
  chmod +x "$2"
}

LD="$(command -v linuxdeploy || echo "$CACHE/linuxdeploy-x86_64.AppImage")"
LDQT="$(command -v linuxdeploy-plugin-qt || echo "$CACHE/linuxdeploy-plugin-qt-x86_64.AppImage")"
if [ ! -x "$LD" ]; then
  fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$LD" \
    || { echo "==> linuxdeploy unavailable — AppImage skipped (deb/tgz still produced)"; exit 0; }
fi
if [ ! -x "$LDQT" ]; then
  fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" "$LDQT" \
    || { echo "==> linuxdeploy-plugin-qt unavailable — AppImage skipped"; exit 0; }
fi

APPDIR="$BUILD_DIR/AppDir"
rm -rf "$APPDIR"
echo "==> Staging install tree into AppDir"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr >/dev/null

# Help the qt plugin find this kit's tools.
prefix="${CMAKE_PREFIX_PATH:-}"
[ -n "$prefix" ] && export PATH="$prefix/bin:$PATH"
export QML_SOURCES_PATHS="$ROOT"

# An EPUB reader needs no geolocation. Qt WebEngine drags in QtPositioning, whose
# position plugins (notably NMEA) link Qt SerialPort, which we don't ship — that
# breaks linuxdeploy. Drop the position plugins from the kit before deploying;
# libQt6Positioning itself is still bundled as a WebEngine dependency.
qt_plugins="$(qmake -query QT_INSTALL_PLUGINS 2>/dev/null || qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
[ -z "$qt_plugins" ] && [ -n "$prefix" ] && qt_plugins="$prefix/plugins"
if [ -n "$qt_plugins" ] && [ -d "$qt_plugins/position" ]; then
  echo "==> Removing position plugins (no geolocation needed): $qt_plugins/position"
  rm -rf "$qt_plugins/position"
fi

echo "==> Building AppImage"
( cd "$DIST_DIR" && OUTPUT="Spindle-$VERSION-x86_64.AppImage" \
    "$LD" --appdir "$APPDIR" --plugin qt \
      --desktop-file "$APPDIR/usr/share/applications/spindle.desktop" \
      --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/spindle.svg" \
      --output appimage )

echo "==> Done. Packages in: $DIST_DIR"
