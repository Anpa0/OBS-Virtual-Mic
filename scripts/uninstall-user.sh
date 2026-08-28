#!/usr/bin/env bash
set -euo pipefail
for BASE in \
  "$HOME/.var/app/com.obsproject.Studio/config/obs-studio" \
  "${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio"; do
  TARGET="$BASE/plugins/obs-virtual-mic"
  if [[ -e "$TARGET" ]]; then
    rm -rf "$TARGET"
    echo "Removed $TARGET"
  fi
done
