#!/usr/bin/env bash
# Build Spindle on macOS and package the app bundle as a .dmg.
#
# Requires: Qt 6 (with macdeployqt), CMake, Xcode command-line tools.
# Point CMAKE_PREFIX_PATH at the Qt kit; otherwise build.sh auto-detects a
# Homebrew Qt.
#
# Output: dist/Spindle-<version>-macos.dmg
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DIST_DIR="$ROOT/dist"
VERSION="$(sed -n 's/.*project(Spindle VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
VERSION="${VERSION:-0.0.0}"

"$ROOT/scripts/build.sh"

APP="$BUILD_DIR/spindle.app"
if [ ! -d "$APP" ]; then
  echo "ERROR: $APP not found — the bundle build failed?" >&2
  exit 1
fi

# macdeployqt from the same kit that built the app: PATH first, then
# CMAKE_PREFIX_PATH, then Homebrew.
MACDEPLOYQT="$(command -v macdeployqt || true)"
if [ -z "$MACDEPLOYQT" ] && [ -n "${CMAKE_PREFIX_PATH:-}" ] \
    && [ -x "$CMAKE_PREFIX_PATH/bin/macdeployqt" ]; then
  MACDEPLOYQT="$CMAKE_PREFIX_PATH/bin/macdeployqt"
fi
if [ -z "$MACDEPLOYQT" ] && command -v brew >/dev/null 2>&1 \
    && brew --prefix qt >/dev/null 2>&1; then
  candidate="$(brew --prefix qt)/bin/macdeployqt"
  [ -x "$candidate" ] && MACDEPLOYQT="$candidate"
fi
if [ -z "$MACDEPLOYQT" ]; then
  echo "ERROR: macdeployqt not found. Add the Qt kit's bin to PATH or set CMAKE_PREFIX_PATH." >&2
  exit 1
fi

echo "==> Deploying Qt runtime into the bundle (incl. WebEngine helpers)"
"$MACDEPLOYQT" "$APP" -verbose=1

# macdeployqt's install-name edits invalidate the code signatures, and Apple
# silicon refuses to launch unsigned binaries at all — re-sign everything
# ad-hoc. Replace "-" with a Developer ID identity for notarized releases.
echo "==> Ad-hoc code signing"
codesign --force --deep --sign - "$APP"

echo "==> Building DMG"
mkdir -p "$DIST_DIR"
STAGE="$BUILD_DIR/dmg-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/Spindle.app"
ln -s /Applications "$STAGE/Applications"
DMG="$DIST_DIR/Spindle-$VERSION-macos.dmg"
rm -f "$DMG"
hdiutil create -volname "Spindle" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"

echo "==> Done. Package: $DMG"
