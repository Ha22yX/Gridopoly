#!/bin/sh
set -eu

# Additive install: no game AP, server, home profile or game data is replaced.
# For remote maintenance, first arrange a bounded rollback over an independent
# connection. See README.md; service installation can affect the onboard PHY.
test "$(id -u)" = 0 || { echo 'Run as root' >&2; exit 1; }
test "$#" = 1 || { echo 'Usage: install.sh /path/to/private-admin-ap.json' >&2; exit 2; }
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
for program in python3 ip iw nmcli hostapd hostapd_cli dnsmasq systemctl; do
    command -v "$program" >/dev/null || { echo "Missing: $program" >&2; exit 1; }
done
python3 "$HERE/admin_ap.py" --config "$1" --check
test -e /sys/class/net/wlan0/phy80211
install -d -m 0755 /etc/gridopoly /usr/local/libexec /etc/NetworkManager/conf.d
install -m 0600 "$1" /etc/gridopoly/admin-ap.json
install -m 0755 "$HERE/admin_ap.py" /usr/local/libexec/gridopoly-admin-ap
install -m 0644 "$HERE/gridopoly-admin-ap.service" /etc/systemd/system/gridopoly-admin-ap.service
install -m 0644 "$HERE/91-gridopoly-admin.conf" /etc/NetworkManager/conf.d/91-gridopoly-admin.conf
nmcli general reload conf
systemctl daemon-reload
systemctl enable gridopoly-admin-ap.service
systemctl restart gridopoly-admin-ap.service
