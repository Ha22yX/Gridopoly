#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "run with sudo" >&2
  exit 2
fi
if [ "$#" -ne 1 ] || [ ! -x "$1" ]; then
  echo "usage: sudo ./install.sh /path/to/gridopoly_server" >&2
  exit 2
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
TILE_STAGER="$PROJECT_DIR/Server/RaspberryPi/tools/stage-web-tile-assets.py"
AVATAR_LAYER_PACK="$PROJECT_DIR/Assets/GridCity/Avatars/V1/runtime/avatar-v1.layers"
AVATAR_COMPONENT_SOURCE="$PROJECT_DIR/Assets/GridCity/Avatars/V1/runtime/components-v1"
AVATAR_COMPONENT_VERIFIER="$PROJECT_DIR/Server/RaspberryPi/tools/verify-avatar-components.py"
AVATAR_COMPONENT_STAGER="$PROJECT_DIR/Server/RaspberryPi/tools/stage-avatar-components.py"
TILE_PARENT=/usr/local/share/gridopoly
TILE_TARGET="$TILE_PARENT/tiles"
TILE_STAGE="$TILE_PARENT/.tiles-install-$$"
TILE_OLD="$TILE_PARENT/.tiles-old-$$"
AVATAR_COMPONENT_TARGET="$TILE_PARENT/avatar/components-v1"
AVATAR_COMPONENT_STAGE="$TILE_PARENT/avatar/.components-v1-install-$$"
AVATAR_COMPONENT_OLD="$TILE_PARENT/avatar/.components-v1-old-$$"
if [ ! -f "$TILE_STAGER" ]; then
  echo "missing tile asset stager: $TILE_STAGER" >&2
  exit 3
fi
if [ ! -f "$AVATAR_LAYER_PACK" ]; then
  echo "missing avatar runtime layer pack: $AVATAR_LAYER_PACK" >&2
  exit 3
fi
if [ ! -d "$AVATAR_COMPONENT_SOURCE" ] || [ ! -f "$AVATAR_COMPONENT_VERIFIER" ] || \
   [ ! -f "$AVATAR_COMPONENT_STAGER" ]; then
  echo "missing avatar component runtime assets" >&2
  exit 3
fi
python3 "$AVATAR_COMPONENT_VERIFIER"
if ! getent group gridopoly >/dev/null; then groupadd --system gridopoly; fi
if ! id gridopoly >/dev/null 2>&1; then
  useradd --system --gid gridopoly --home-dir /var/lib/gridopoly --shell /usr/sbin/nologin gridopoly
fi
install -d -o gridopoly -g gridopoly -m 0700 /var/lib/gridopoly
install -d -o gridopoly -g gridopoly -m 0750 /var/lib/gridopoly/assets
install -d -o root -g gridopoly -m 0750 /etc/gridopoly
install -m 0755 "$1" /usr/local/bin/gridopoly_server
install -d -o root -g root -m 0755 "$TILE_PARENT"
rm -rf "$TILE_STAGE" "$TILE_OLD"
install -d -o root -g root -m 0755 "$TILE_STAGE"
python3 "$TILE_STAGER" "$TILE_STAGE"
find "$TILE_STAGE" -maxdepth 1 -type f -exec chown root:root {} +
find "$TILE_STAGE" -maxdepth 1 -type f -exec chmod 0644 {} +
PNG_COUNT=$(find "$TILE_STAGE" -maxdepth 1 -type f -name '*.png' | wc -l | tr -d ' ')
RGB565_COUNT=$(find "$TILE_STAGE" -maxdepth 1 -type f -name '*.rgb565' | wc -l | tr -d ' ')
FILE_COUNT=$(find "$TILE_STAGE" -maxdepth 1 -type f | wc -l | tr -d ' ')
if [ "$PNG_COUNT" -ne 36 ] || [ "$RGB565_COUNT" -ne 36 ] || [ "$FILE_COUNT" -ne 72 ]; then
  echo "unexpected staged tile counts: png=$PNG_COUNT rgb565=$RGB565_COUNT files=$FILE_COUNT" >&2
  exit 3
fi
if [ -e "$TILE_TARGET" ]; then mv "$TILE_TARGET" "$TILE_OLD"; fi
if ! mv "$TILE_STAGE" "$TILE_TARGET"; then
  if [ -e "$TILE_OLD" ]; then mv "$TILE_OLD" "$TILE_TARGET"; fi
  exit 3
fi
rm -rf "$TILE_OLD"
install -d -o root -g root -m 0755 "$TILE_PARENT/avatar"
install -m 0644 "$AVATAR_LAYER_PACK" "$TILE_PARENT/avatar/avatar-v1.layers"
rm -rf "$AVATAR_COMPONENT_STAGE" "$AVATAR_COMPONENT_OLD"
install -d -o root -g root -m 0755 "$AVATAR_COMPONENT_STAGE"
python3 "$AVATAR_COMPONENT_STAGER" "$AVATAR_COMPONENT_STAGE"
find "$AVATAR_COMPONENT_STAGE" -type d -exec chown root:root {} +
find "$AVATAR_COMPONENT_STAGE" -type d -exec chmod 0755 {} +
find "$AVATAR_COMPONENT_STAGE" -type f -exec chown root:root {} +
find "$AVATAR_COMPONENT_STAGE" -type f -exec chmod 0644 {} +
AVATAR_COMPONENT_COUNT=$(find "$AVATAR_COMPONENT_STAGE" -type f -name '*.gavc' | wc -l | tr -d ' ')
AVATAR_COMPONENT_FILE_COUNT=$(find "$AVATAR_COMPONENT_STAGE" -type f | wc -l | tr -d ' ')
if [ "$AVATAR_COMPONENT_COUNT" -ne 30 ] || [ "$AVATAR_COMPONENT_FILE_COUNT" -ne 31 ]; then
  echo "unexpected avatar component counts: gavc=$AVATAR_COMPONENT_COUNT files=$AVATAR_COMPONENT_FILE_COUNT" >&2
  exit 3
fi
if [ -e "$AVATAR_COMPONENT_TARGET" ]; then
  mv "$AVATAR_COMPONENT_TARGET" "$AVATAR_COMPONENT_OLD"
fi
if ! mv "$AVATAR_COMPONENT_STAGE" "$AVATAR_COMPONENT_TARGET"; then
  if [ -e "$AVATAR_COMPONENT_OLD" ]; then
    mv "$AVATAR_COMPONENT_OLD" "$AVATAR_COMPONENT_TARGET"
  fi
  exit 3
fi
rm -rf "$AVATAR_COMPONENT_OLD"
install -m 0755 "$SCRIPT_DIR/gridopoly-ap" /usr/local/libexec/gridopoly-ap
install -m 0755 "$SCRIPT_DIR/gridopoly-ap-cleanup" /usr/local/libexec/gridopoly-ap-cleanup
install -m 0755 "$SCRIPT_DIR/gridopoly-ap-watchdog" /usr/local/libexec/gridopoly-ap-watchdog
install -m 0644 "$SCRIPT_DIR/gridopoly.service" /etc/systemd/system/gridopoly.service
install -m 0644 "$SCRIPT_DIR/gridopoly-ap.service" /etc/systemd/system/gridopoly-ap.service
install -m 0644 "$SCRIPT_DIR/gridopoly-ap-watchdog.service" /etc/systemd/system/gridopoly-ap-watchdog.service
install -m 0644 "$SCRIPT_DIR/gridopoly-dnsmasq.service" /etc/systemd/system/gridopoly-dnsmasq.service
install -m 0644 "$SCRIPT_DIR/dnsmasq.conf" /etc/gridopoly/dnsmasq.conf
install -m 0644 "$SCRIPT_DIR/NetworkManager-gridopoly.conf" \
  /etc/NetworkManager/conf.d/90-gridopoly-ap.conf
# The distribution services would compete for port 53 and the wireless interface.
# Gridopoly uses its own tightly scoped units and runtime configuration instead.
systemctl disable --now dnsmasq.service hostapd.service >/dev/null 2>&1 || true
if [ ! -e /etc/gridopoly/server.env ]; then
  install -o root -g gridopoly -m 0640 "$SCRIPT_DIR/server.env.example" /etc/gridopoly/server.env
fi
if [ ! -e /etc/gridopoly/ap.env ]; then
  install -o root -g root -m 0600 "$SCRIPT_DIR/ap.env.example" /etc/gridopoly/ap.env
fi
systemctl daemon-reload
nmcli general reload >/dev/null 2>&1 || true
echo "installed; configure /etc/gridopoly/server.env and /etc/gridopoly/ap.env before enabling services"
