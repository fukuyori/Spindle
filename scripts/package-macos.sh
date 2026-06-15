#!/usr/bin/env bash
# Build Spindle, bundle Qt frameworks (incl. WebEngine) and produce a .dmg.
#
# Requires: Qt 6 (with macdeployqt), CMake, a C++17 toolchain.
# Output:   dist/Spindle-<version>-macOS.dmg
#
# Env overrides: BUILD_DIR, BUILD_TYPE, CMAKE_PREFIX_PATH
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
DIST_DIR="$ROOT/dist"
VERSION="$(sed -n 's/.*project(Spindle VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
VERSION="${VERSION:-0.0.0}"

# Resolve Qt prefix → macdeployqt path.
prefix="${CMAKE_PREFIX_PATH:-}"
if [ -z "$prefix" ] && command -v brew >/dev/null 2>&1 && brew --prefix qt >/dev/null 2>&1; then
  prefix="$(brew --prefix qt)"
fi
MACDEPLOYQT="${MACDEPLOYQT:-$prefix/bin/macdeployqt}"
if [ ! -x "$MACDEPLOYQT" ]; then
  if command -v macdeployqt >/dev/null 2>&1; then MACDEPLOYQT="$(command -v macdeployqt)";
  else echo "ERROR: macdeployqt not found (set CMAKE_PREFIX_PATH or MACDEPLOYQT)"; exit 1; fi
fi

"$ROOT/scripts/build.sh"

APP="$BUILD_DIR/spindle.app"
[ -d "$APP" ] || { echo "ERROR: $APP not found"; exit 1; }

IS_BREW_QT=0
case "$prefix" in
  *"$(brew --prefix 2>/dev/null)"*) [ -n "$prefix" ] && IS_BREW_QT=1 ;;
esac

echo "==> Bundling Qt frameworks with macdeployqt"
# Homebrew keeps Qt's transitive deps (brotli, webp, QtSvg, …) in keg dirs that
# macdeployqt can't follow via @rpath. Hand it Homebrew's aggregate lib dir
# (full of symlinks) so those resolve.
deploy_args=(-always-overwrite)
if command -v brew >/dev/null 2>&1; then
  deploy_args+=("-libpath=$(brew --prefix)/lib")
fi
"$MACDEPLOYQT" "$APP" "${deploy_args[@]}" || true

MAIN_FW="$APP/Contents/Frameworks"

# If a stub AGL.framework was needed at link time, bundle it so the app loads.
STUB_AGL="$BUILD_DIR/.stubfw/AGL.framework"
if [ -d "$STUB_AGL" ] && [ ! -e "$MAIN_FW/AGL.framework" ]; then
  mkdir -p "$MAIN_FW/AGL.framework"
  cp "$STUB_AGL/AGL" "$MAIN_FW/AGL.framework/AGL"
fi

# The WebEngine helper (QtWebEngineProcess) resolves QtWebEngineCore's
# dependencies via @executable_path/../Frameworks — i.e. its OWN Frameworks dir,
# NOT the app's. macdeployqt doesn't populate it, so the render process aborts
# at launch (→ blank chapters). Mirror every bundled framework/dylib into the
# helper's Frameworks via relative symlinks so all deps resolve.
HELPER_FW="$MAIN_FW/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app/Contents/Frameworks"
if [ -d "$MAIN_FW/QtWebEngineCore.framework" ]; then
  rm -rf "$HELPER_FW"
  mkdir -p "$HELPER_FW"
  for entry in "$MAIN_FW"/*; do
    name="$(basename "$entry")"
    [ "$name" = "QtWebEngineCore.framework" ] && continue   # avoid self-reference
    ln -sfn "../../../../../../../$name" "$HELPER_FW/$name"
  done
fi

# macdeployqt rewrites load paths, which invalidates existing signatures.
# Re-apply an ad-hoc signature so the bundle launches locally.
echo "==> Ad-hoc codesigning"
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true

mkdir -p "$DIST_DIR"
DMG="$DIST_DIR/Spindle-$VERSION-macOS.dmg"
rm -f "$DMG"

echo "==> Creating $DMG"
if command -v create-dmg >/dev/null 2>&1; then
  create-dmg --volname "Spindle $VERSION" --app-drop-link 480 170 \
    --icon "spindle.app" 160 170 --window-size 640 360 \
    "$DMG" "$APP" >/dev/null
else
  # Fallback: plain drag-install dmg via hdiutil.
  staging="$(mktemp -d)"
  cp -R "$APP" "$staging/"
  ln -s /Applications "$staging/Applications"
  hdiutil create -volname "Spindle $VERSION" -srcfolder "$staging" \
    -ov -format UDZO "$DMG" >/dev/null
  rm -rf "$staging"
fi

echo "==> Done: $DMG"
echo "    (unsigned — to ship widely, codesign + notarize the .app before packaging)"
