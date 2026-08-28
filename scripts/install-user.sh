#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
SO="$BUILD_DIR/obs-virtual-mic.so"

if [[ ! -f "$SO" ]]; then
  echo "Plugin not built: $SO" >&2
  echo "Run: cmake -S '$ROOT' -B '$BUILD_DIR' -G Ninja && cmake --build '$BUILD_DIR' -j\$(nproc)" >&2
  exit 1
fi

FLATPAK_BASE="$HOME/.var/app/com.obsproject.Studio/config/obs-studio"
NATIVE_BASE="${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio"

if [[ -d "$HOME/.var/app/com.obsproject.Studio" ]]; then
  BASE="$FLATPAK_BASE"
  echo "Detected Flatpak OBS Studio."
else
  BASE="$NATIVE_BASE"
  echo "Detected native OBS Studio config path."
fi

DEST="$BASE/plugins/obs-virtual-mic/bin/64bit"
mkdir -p "$DEST"
install -m 0755 "$SO" "$DEST/obs-virtual-mic.so"
echo "Installed to $DEST/obs-virtual-mic.so"
echo "Restart OBS Studio."
