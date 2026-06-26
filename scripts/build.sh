#!/usr/bin/env bash
# Configure and build Spindle (macOS / Linux).
#
# Env overrides:
#   BUILD_DIR        build directory (default: <repo>/build)
#   BUILD_TYPE       Release | Debug (default: Release)
#   CMAKE_PREFIX_PATH  Qt 6 prefix (auto-detected via Homebrew on macOS)
#
# Usage: scripts/build.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

prefix="${CMAKE_PREFIX_PATH:-}"
if [ -z "$prefix" ] && command -v brew >/dev/null 2>&1; then
  if brew --prefix qt >/dev/null 2>&1; then
    prefix="$(brew --prefix qt)"
  fi
fi

echo "==> Configuring (type=$BUILD_TYPE, prefix=${prefix:-<system>})"
args=(-S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE")
[ -n "$prefix" ] && args+=(-DCMAKE_PREFIX_PATH="$prefix")

cache="$BUILD_DIR/CMakeCache.txt"
if [ -f "$cache" ]; then
  cache_root="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache" | tail -n 1)"
  if [ -n "$cache_root" ] && [ "$cache_root" != "$ROOT" ]; then
    echo "   (stale CMake cache from $cache_root — resetting $BUILD_DIR)"
    rm -f "$cache"
    rm -rf "$BUILD_DIR/CMakeFiles"
  fi
fi

# Recent macOS SDKs dropped the legacy AGL framework that some Qt releases
# (e.g. 6.8) still reference at link time. Provide a harmless stub so linking
# succeeds; it is never loaded at runtime. (Newer Qt doesn't need this.)
if [ "$(uname)" = "Darwin" ]; then
  # The *SDK* is what the linker searches; a runtime copy under /System is
  # irrelevant if the SDK no longer ships AGL.
  sdk_agl="$(xcrun --show-sdk-path 2>/dev/null)/System/Library/Frameworks/AGL.framework"
  if [ ! -d "$sdk_agl" ]; then
    stub="$BUILD_DIR/.stubfw"
    mkdir -p "$stub/AGL.framework"
    [ -f "$stub/AGL.framework/AGL" ] || printf 'void __spindle_agl_stub(void){}\n' \
      | clang -dynamiclib -x c - -install_name @rpath/AGL.framework/AGL \
        -current_version 1.0 -compatibility_version 1.0 \
        -o "$stub/AGL.framework/AGL"
    args+=(-DCMAKE_EXE_LINKER_FLAGS="-F$stub")
    echo "   (this SDK has no AGL.framework — added a link-time stub)"
  fi
fi

cmake "${args[@]}"

echo "==> Building"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

echo "==> Done. Artifacts in: $BUILD_DIR"
