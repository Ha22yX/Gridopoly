#!/bin/sh
# Publish one immutable avatar directory. Does not restart or configure services.
set -eu
if [ "$(id -u)" -ne 0 ] || [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: sudo install-avatar-components.sh <1|2> [runtime-directory]" >&2
  exit 2
fi
VERSION=$1
case "$VERSION" in 1|2) ;; *) echo "unsupported catalog" >&2; exit 2 ;; esac
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
RUNTIME=${2:-$PROJECT_DIR/Assets/GridCity/Avatars/V$VERSION/runtime}
STAGER="$PROJECT_DIR/Server/RaspberryPi/tools/stage-avatar-components.py"
PARENT=/usr/local/share/gridopoly/avatar
install -d -o root -g root -m 0755 "$PARENT"
# Serialize publishers; a released version can only be installed identically.
exec 9>"$PARENT/.install.lock"
flock -x 9
TARGET="$PARENT/components-v$VERSION"
if [ -e "$TARGET" ] || [ -L "$TARGET" ]; then
  python3 "$STAGER" "$TARGET" --catalog-version "$VERSION" --runtime-root "$RUNTIME" --verify-existing
  exit 0
fi
STAGE=$(mktemp -d "$PARENT/.components-v$VERSION-stage.XXXXXX")
trap 'rm -rf -- "$STAGE"' EXIT HUP INT TERM
python3 "$STAGER" "$STAGE" --catalog-version "$VERSION" --runtime-root "$RUNTIME"
find "$STAGE" -type d -exec chmod 0755 {} +
find "$STAGE" -type f -exec chmod 0644 {} +
chown -R root:root "$STAGE"
mv -T "$STAGE" "$TARGET"
trap - EXIT HUP INT TERM
echo "published immutable avatar catalog $VERSION at $TARGET; existing catalogs retained"
