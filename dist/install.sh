#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_BIN="$ROOT_DIR/build/hypr-quiklight"
TARGET_BIN="$HOME/.local/bin/hypr-quiklight"
TARGET_LAUNCHER="$HOME/.local/bin/hypr-quiklight-launcher"
TARGET_CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/hypr-quiklight"
TARGET_CONFIG="$TARGET_CONFIG_DIR/config.ini"
TARGET_SYSTEMD_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
TARGET_SERVICE="$TARGET_SYSTEMD_DIR/hypr-quiklight.service"

if [ ! -x "$BUILD_BIN" ]; then
  echo "Build binary not found at $BUILD_BIN" >&2
  echo "Build first:" >&2
  echo "  mkdir -p build && cd build && cmake .. && make -j\$(nproc)" >&2
  exit 1
fi

mkdir -p "$HOME/.local/bin"
mkdir -p "$TARGET_CONFIG_DIR"
mkdir -p "$TARGET_SYSTEMD_DIR"

cp "$BUILD_BIN" "$TARGET_BIN"
chmod +x "$TARGET_BIN"
cp "$ROOT_DIR/dist/hypr-quiklight-launcher" "$TARGET_LAUNCHER"
chmod +x "$TARGET_LAUNCHER"
cp "$ROOT_DIR/dist/hypr-quiklight.service" "$TARGET_SERVICE"

if [ ! -f "$TARGET_CONFIG" ]; then
  cp "$ROOT_DIR/dist/config.ini.example" "$TARGET_CONFIG"
  echo "Installed default config to $TARGET_CONFIG"
else
  echo "Config already exists, keeping $TARGET_CONFIG"
fi

echo
echo "Installed:"
echo "  $TARGET_BIN"
echo "  $TARGET_LAUNCHER"
echo "  $TARGET_SERVICE"
echo
printf '%s\n' "Next steps:" \
  "  systemctl --user daemon-reload" \
  "  systemctl --user enable --now hypr-quiklight.service" \
  "  sudo cp \"$ROOT_DIR/dist/99-quiklight.rules\" /etc/udev/rules.d/" \
  "  sudo udevadm control --reload-rules" \
  "  sudo udevadm trigger"
