#!/usr/bin/env bash
# Cross-compile the Generals engine (-> libmain.so) for Android arm64-v8a.
#
# Prereqs (see ../BUILDING.md):
#   ANDROID_NDK_HOME  -> your NDK 27 install
#   the cross-compiled deps (SDL3, FFmpeg, OpenAL) and the DXVK .so files available to CMake
#
# Usage: scripts/build_engine.sh [Debug|Release]
set -euo pipefail

BUILD_TYPE="${1:-Release}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$ROOT/engine"
OUT="$ROOT/build/android-arm64"
: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to your NDK path}"

cmake -S "$ENGINE" -B "$OUT" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DSAGE_USE_OPENAL=ON

cmake --build "$OUT" --target z_generals -j"$(nproc)"

echo "built: $OUT (copy libmain.so + the dependency .so files into android/app/src/main/jniLibs/arm64-v8a/)"
