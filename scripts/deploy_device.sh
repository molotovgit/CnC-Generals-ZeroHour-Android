#!/usr/bin/env bash
# Install the built APK on a connected device and (optionally) launch it.
# Usage: scripts/deploy_device.sh path/to/app.apk [--launch]
set -euo pipefail

APK="${1:?usage: deploy_device.sh <apk> [--launch]}"
PKG="org.generalsx.zerohour"

adb install -r "$APK"

if [[ "${2:-}" == "--launch" ]]; then
  # SetupActivity is the launcher; it extracts game data on first run, then starts the game.
  adb shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null
  echo "launched $PKG"
fi
