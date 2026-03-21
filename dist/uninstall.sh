#!/bin/sh
set -eu

TARGET_BIN="$HOME/.local/bin/hypr-quiklight"
TARGET_LAUNCHER="$HOME/.local/bin/hypr-quiklight-launcher"
TARGET_SERVICE="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/hypr-quiklight.service"

systemctl --user disable --now hypr-quiklight.service >/dev/null 2>&1 || true
rm -f "$TARGET_BIN" "$TARGET_LAUNCHER" "$TARGET_SERVICE"
systemctl --user daemon-reload || true

echo "Removed installed binary, launcher, and user service."
echo "Config file was kept in ${XDG_CONFIG_HOME:-$HOME/.config}/hypr-quiklight/config.ini"
