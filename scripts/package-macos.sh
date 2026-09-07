#!/usr/bin/env bash
# Build Spindle on macOS, bundle Qt, sign, notarize, staple, and package a .dmg.
#
# Requires: Qt 6 (with macdeployqt), CMake, Xcode command-line tools.
#
# Env overrides:
#   BUILD_DIR, BUILD_TYPE, CMAKE_PREFIX_PATH, MACDEPLOYQT
#   MACOS_SIGN_IDENTITY        Developer ID Application identity
#                              (auto-detected when unset).
#   MACOS_DMG_SIGN_IDENTITY    Developer ID Application identity for the .dmg
#                              (defaults to MACOS_SIGN_IDENTITY).
#   NOTARYTOOL_PROFILE         xcrun notarytool keychain profile
#                              (auto-detects notarytool/spindle/notary when unset).
#   APPLE_ID                   Apple ID for notarytool fallback auth.
#   APPLE_TEAM_ID              Team ID for notarytool fallback auth.
#   APPLE_APP_SPECIFIC_PASSWORD  App-specific password for fallback auth.
#   SKIP_NOTARIZE=1            Sign but skip notarization.
#
# Output: dist/Spindle-<version>-macos-<arch>.dmg
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
DIST_DIR="$ROOT/dist"
VERSION="$(sed -n 's/.*project(Spindle VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
VERSION="${VERSION:-0.0.0}"

SIGN_IDENTITY="${MACOS_SIGN_IDENTITY:-}"
DMG_SIGN_IDENTITY="${MACOS_DMG_SIGN_IDENTITY:-}"
NOTARYTOOL_PROFILE="${NOTARYTOOL_PROFILE:-}"
APPLE_ID="${APPLE_ID:-}"
APPLE_TEAM_ID="${APPLE_TEAM_ID:-}"
APPLE_APP_SPECIFIC_PASSWORD="${APPLE_APP_SPECIFIC_PASSWORD:-}"
SKIP_NOTARIZE="${SKIP_NOTARIZE:-0}"
ENTITLEMENTS="$BUILD_DIR/macos-entitlements.plist"
NOTARY_ARGS=()

# Prefer an official Qt installation for packaging. Homebrew Qt can leave
# QtWebEngine dependencies unresolved because its libraries are split across
# multiple kegs.
QT_PREFIX="${CMAKE_PREFIX_PATH:-}"
BREW_ROOT=""
if [ -z "$QT_PREFIX" ]; then
  official_qt="$(
    for candidate in "${HOME}"/Qt/*/macos; do
      [ -d "$candidate" ] && printf '%s\n' "$candidate"
    done | sort -V | tail -n 1
  )"
  if [ -n "$official_qt" ]; then
    QT_PREFIX="$official_qt"
    echo "==> Using official Qt: $QT_PREFIX"
  elif command -v brew >/dev/null 2>&1 && brew --prefix qt >/dev/null 2>&1; then
    QT_PREFIX="$(brew --prefix qt)"
  fi
else
  echo "==> Using requested Qt: $QT_PREFIX"
fi

if command -v brew >/dev/null 2>&1; then
  BREW_ROOT="$(brew --prefix 2>/dev/null || true)"
fi
if [ -n "$BREW_ROOT" ]; then
  case "$QT_PREFIX" in
    "$BREW_ROOT"/*)
      cat >&2 <<'WARN'
WARNING: Packaging with Homebrew Qt may leave QtWebEngine dependencies unresolved.
Install an official Qt build or set CMAKE_PREFIX_PATH to one when possible.
WARN
      ;;
  esac
fi

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "ERROR: $1 not found" >&2; exit 1; }
}

resolve_notary_credentials() {
  if [ "$SKIP_NOTARIZE" = "1" ]; then
    return
  fi

  detect_sign_identity
  if [ -z "$SIGN_IDENTITY" ]; then
    echo "ERROR: notarization requires a Developer ID Application signing identity." >&2
    echo "Set MACOS_SIGN_IDENTITY or install the certificate in your keychain." >&2
    exit 1
  fi

  require_cmd xcrun
  if [ -z "$NOTARYTOOL_PROFILE" ] \
      && [ -z "$APPLE_ID" ] \
      && [ -z "$APPLE_TEAM_ID" ] \
      && [ -z "$APPLE_APP_SPECIFIC_PASSWORD" ]; then
    # notarytool profiles cannot be enumerated with `security`, so probe the
    # conventional names. A missing profile is rejected immediately.
    local candidate probe_out
    for candidate in notarytool spindle notary; do
      probe_out="$(xcrun notarytool history --keychain-profile "$candidate" 2>&1 || true)"
      if ! printf '%s\n' "$probe_out" | grep -q 'No Keychain password item found'; then
        NOTARYTOOL_PROFILE="$candidate"
        echo "==> Auto-detected notary profile: $NOTARYTOOL_PROFILE"
        break
      fi
    done
  fi

  if [ -n "$NOTARYTOOL_PROFILE" ]; then
    NOTARY_ARGS=(--keychain-profile "$NOTARYTOOL_PROFILE")
  elif [ -n "$APPLE_ID" ] \
      && [ -n "$APPLE_TEAM_ID" ] \
      && [ -n "$APPLE_APP_SPECIFIC_PASSWORD" ]; then
    NOTARY_ARGS=(
      --apple-id "$APPLE_ID"
      --team-id "$APPLE_TEAM_ID"
      --password "$APPLE_APP_SPECIFIC_PASSWORD"
    )
  else
    cat >&2 <<'ERR'
ERROR: notarization credentials are not configured.
Store a notarytool keychain profile with a conventional name, set
NOTARYTOOL_PROFILE, or set APPLE_ID, APPLE_TEAM_ID, and
APPLE_APP_SPECIFIC_PASSWORD. Use SKIP_NOTARIZE=1 for a local package.
ERR
    exit 1
  fi
}

detect_sign_identity() {
  if [ -n "$SIGN_IDENTITY" ]; then
    return
  fi
  if ! command -v security >/dev/null 2>&1; then
    return
  fi
  SIGN_IDENTITY="$(
    security find-identity -v -p codesigning 2>/dev/null \
      | sed -n 's/.*"\(Developer ID Application: .*([^)]*)\)".*/\1/p' \
      | head -n 1
  )"
  if [ -n "$SIGN_IDENTITY" ]; then
    echo "==> Auto-detected signing identity: $SIGN_IDENTITY"
  fi
}

write_entitlements() {
  mkdir -p "$BUILD_DIR"
  cat >"$ENTITLEMENTS" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>com.apple.security.cs.allow-jit</key>
  <true/>
  <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
  <true/>
  <key>com.apple.security.cs.disable-library-validation</key>
  <true/>
</dict>
</plist>
PLIST
}

codesign_app() {
  local app="$1"
  detect_sign_identity
  if [ -z "$SIGN_IDENTITY" ]; then
    echo "==> Ad-hoc code signing"
    codesign --force --deep --sign - "$app"
    return
  fi

  write_entitlements
  echo "==> Developer ID code signing: $SIGN_IDENTITY"

  if [ -d "$app/Contents/PlugIns" ]; then
    while IFS= read -r item; do
      codesign --force --timestamp --options runtime --sign "$SIGN_IDENTITY" "$item"
    done < <(find "$app/Contents/PlugIns" -type f -perm -111 -print)
  fi

  if [ -d "$app/Contents/Frameworks" ]; then
    while IFS= read -r nested_app; do
      codesign --force --timestamp --options runtime \
        --entitlements "$ENTITLEMENTS" \
        --sign "$SIGN_IDENTITY" "$nested_app"
    done < <(find "$app/Contents/Frameworks" -name '*.app' -type d -print)

    while IFS= read -r dylib; do
      codesign --force --timestamp --options runtime --sign "$SIGN_IDENTITY" "$dylib"
    done < <(find "$app/Contents/Frameworks" -type f -name '*.dylib' -print)

    while IFS= read -r framework; do
      codesign --force --timestamp --options runtime --sign "$SIGN_IDENTITY" "$framework"
    done < <(find "$app/Contents/Frameworks" -name '*.framework' -type d -print \
      | awk '{ print length, $0 }' | sort -rn | cut -d' ' -f2-)
  fi

  codesign --force --timestamp --options runtime \
    --entitlements "$ENTITLEMENTS" \
    --sign "$SIGN_IDENTITY" "$app"
  codesign --verify --deep --strict --verbose=2 "$app"
  spctl --assess --type execute --verbose=4 "$app" || true
}

codesign_dmg() {
  local dmg="$1"
  detect_sign_identity
  if [ -z "$DMG_SIGN_IDENTITY" ]; then
    DMG_SIGN_IDENTITY="$SIGN_IDENTITY"
  fi
  if [ -n "$DMG_SIGN_IDENTITY" ]; then
    echo "==> Signing DMG: $DMG_SIGN_IDENTITY"
    codesign --force --timestamp --sign "$DMG_SIGN_IDENTITY" "$dmg"
    codesign --verify --verbose=2 "$dmg"
  fi
}

notarize_dmg() {
  local dmg="$1"
  if [ "$SKIP_NOTARIZE" = "1" ]; then
    echo "==> Skipping notarization (SKIP_NOTARIZE=1)"
    return
  fi

  detect_sign_identity
  if [ -z "$SIGN_IDENTITY" ]; then
    echo "ERROR: notarization requires a Developer ID Application signing identity." >&2
    echo "Set MACOS_SIGN_IDENTITY or install the certificate in your keychain." >&2
    exit 1
  fi

  echo "==> Notarizing DMG"
  local submit_out
  submit_out="$(mktemp)"
  if ! xcrun notarytool submit "$dmg" "${NOTARY_ARGS[@]}" \
      --wait --output-format json >"$submit_out"; then
    cat "$submit_out" >&2
    rm -f "$submit_out"
    echo "ERROR: failed to submit the DMG for notarization." >&2
    exit 1
  fi

  cat "$submit_out"
  local status submission_id
  status="$(plutil -extract status raw -o - "$submit_out" 2>/dev/null || true)"
  submission_id="$(plutil -extract id raw -o - "$submit_out" 2>/dev/null || true)"
  rm -f "$submit_out"
  if [ "$status" != "Accepted" ]; then
    echo "ERROR: notarization failed with status: ${status:-unknown}" >&2
    if [ -n "$submission_id" ]; then
      echo "==> Fetching notarization log for $submission_id" >&2
      xcrun notarytool log "$submission_id" "${NOTARY_ARGS[@]}" >&2 || true
    fi
    exit 1
  fi

  xcrun stapler staple "$dmg"
  xcrun stapler validate "$dmg"
  spctl --assess --type open --context context:primary-signature --verbose=4 "$dmg"
}

# Fail before the potentially long Qt build when credentials are unavailable.
resolve_notary_credentials

# CMake retains Qt6_DIR in its cache. Reset only generated configuration and the
# old app bundle when switching Qt installations, so frameworks are not mixed.
cache="$BUILD_DIR/CMakeCache.txt"
if [ -n "$QT_PREFIX" ] && [ -f "$cache" ]; then
  cached_qt_dir="$(sed -n 's/^Qt6_DIR:PATH=//p' "$cache" | tail -n 1)"
  case "$cached_qt_dir" in
    ""|"$QT_PREFIX"/*) ;;
    *)
      echo "==> Qt changed from $cached_qt_dir; resetting generated build state"
      rm -f "$cache"
      rm -rf "$BUILD_DIR/CMakeFiles" "$BUILD_DIR/spindle.app"
      ;;
  esac
fi

if [ -n "$QT_PREFIX" ]; then
  BUILD_DIR="$BUILD_DIR" BUILD_TYPE="$BUILD_TYPE" CMAKE_PREFIX_PATH="$QT_PREFIX" \
    "$ROOT/scripts/build.sh"
else
  BUILD_DIR="$BUILD_DIR" BUILD_TYPE="$BUILD_TYPE" "$ROOT/scripts/build.sh"
fi

APP="$BUILD_DIR/spindle.app"
if [ ! -d "$APP" ]; then
  echo "ERROR: $APP not found; the bundle build failed." >&2
  exit 1
fi

MACDEPLOYQT="${MACDEPLOYQT:-}"
if [ -z "$MACDEPLOYQT" ] && [ -n "$QT_PREFIX" ] \
    && [ -x "$QT_PREFIX/bin/macdeployqt" ]; then
  MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
fi
if [ -z "$MACDEPLOYQT" ]; then
  MACDEPLOYQT="$(command -v macdeployqt || true)"
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

echo "==> Deploying Qt runtime into the bundle"
deploy_args=(-verbose=1 -always-overwrite)
if [ -n "$BREW_ROOT" ]; then
  case "$QT_PREFIX" in
    "$BREW_ROOT"/*) deploy_args+=("-libpath=$BREW_ROOT/lib") ;;
  esac
fi
set +e
deploy_out="$("$MACDEPLOYQT" "$APP" "${deploy_args[@]}" 2>&1)"
deploy_status=$?
set -e
filtered_deploy_out="$(
  printf '%s\n' "$deploy_out" \
    | awk '
      /Cannot resolve rpath .@rpath\/QtSerialPort\.framework/ { skip=1; next }
      /Cannot resolve rpath .@rpath\/AGL\.framework/ { skip=1; next }
      /codesign verification error:/ { next }
      /In subcomponent: .*AGL\.framework/ { next }
      skip { skip=0; next }
      { print }
    '
)"
printf '%s\n' "$filtered_deploy_out"
if [ "$deploy_status" -ne 0 ] \
    && printf '%s\n' "$filtered_deploy_out" | grep -q '^ERROR:'; then
  echo "ERROR: macdeployqt failed." >&2
  exit "$deploy_status"
fi
if printf '%s\n' "$deploy_out" | grep -q 'QtSerialPort\.framework'; then
  echo "   (ignored QtSerialPort references from unused QtPositioning plugins)"
fi
if printf '%s\n' "$deploy_out" | grep -q 'AGL\.framework'; then
  echo "   (ignored temporary AGL.framework reference)"
fi

# Spindle does not use geolocation. These plugins can pull QtSerialPort into
# macdeployqt's dependency walk, so remove them before final signing.
rm -rf "$APP/Contents/PlugIns/position"

# New macOS SDK compatibility shim from build.sh. It is link/deploy-time only.
rm -rf "$APP/Contents/Frameworks/AGL.framework"

# QtWebEngineProcess resolves dependencies through its own Contents/Frameworks.
HELPER_FW="$APP/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app/Contents/Frameworks"
MAIN_FW="$APP/Contents/Frameworks"
if [ -d "$MAIN_FW/QtWebEngineCore.framework" ]; then
  rm -rf "$HELPER_FW"
  mkdir -p "$HELPER_FW"
  for entry in "$MAIN_FW"/*; do
    name="$(basename "$entry")"
    [ "$name" = "QtWebEngineCore.framework" ] && continue
    ln -sfn "../../../../../../../$name" "$HELPER_FW/$name"
  done
fi

ARCH="$(uname -m)"
APP_BIN="$APP/Contents/MacOS/spindle"
if command -v lipo >/dev/null 2>&1 && [ -f "$APP_BIN" ]; then
  app_archs="$(lipo -archs "$APP_BIN" 2>/dev/null || true)"
  case "$app_archs" in
    "") ;;
    *" "*) ARCH="universal" ;;
    *) ARCH="$app_archs" ;;
  esac
fi

echo "==> Staging DMG contents"
mkdir -p "$DIST_DIR"
STAGE="$BUILD_DIR/dmg-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
ditto --rsrc --extattr --acl "$APP" "$STAGE/Spindle.app"
[ -f "$ROOT/LICENSE" ] && ditto "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
[ -f "$ROOT/third_party/miniz/LICENSE" ] \
  && ditto "$ROOT/third_party/miniz/LICENSE" "$STAGE/LICENSE.miniz.txt"
ln -s /Applications "$STAGE/Applications"

# Sign the exact app copy that will be sealed inside the DMG.
codesign_app "$STAGE/Spindle.app"

echo "==> Building DMG"
DMG="$DIST_DIR/Spindle-$VERSION-macos-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -volname "Spindle $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"

codesign_dmg "$DMG"
notarize_dmg "$DMG"

echo "==> Done. Package: $DMG"
if [ "$SKIP_NOTARIZE" = "1" ]; then
  echo "    (signed; notarization skipped)"
else
  echo "    (signed and notarized)"
fi
