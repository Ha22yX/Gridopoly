#!/usr/bin/env python3
"""Independent onboard management AP; the USB game AP is never reconfigured.

The AP and home STA share a PHY/channel. A VRF keeps 10.0.0.1 off the
default routing table, even when the home router uses that same address.
"""
from __future__ import annotations

import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

UPLINK = "wlan0"
IFACE = "gpadmin0"
VRF = "gpadmin"
TABLE = "10430"
ADDRESS = "10.0.0.1/24"
ALIAS = "gridopoly-management-ap-v1"
RUN = Path("/run/gridopoly-admin-ap")
STATE = Path("/var/lib/gridopoly-admin-ap")


def command(*args, check=True):
    p = subprocess.run(args, text=True, capture_output=True, timeout=12)
    if check and p.returncode:
        # Commands never carry the Wi-Fi passphrase.
        raise RuntimeError(f'{args[0]} {args[1:]} failed: {p.stderr.strip()}')
    return p


def log(message):
    print(message, flush=True)


def load_config(path):
    cfg = json.loads(Path(path).read_text())
    allowed = {"ssid", "passphrase", "country", "fallback_channel", "max_clients"}
    if set(cfg) - allowed:
        raise ValueError("Unknown management AP setting")
    ssid, password = cfg.get("ssid", "gridopoly-admin"), cfg.get("passphrase", "")
    if not isinstance(ssid, str) or not 1 <= len(ssid.encode()) <= 32 or any(ord(c) < 32 for c in ssid):
        raise ValueError("SSID must be 1-32 bytes without control characters")
    if ssid == "gridopoly":
        raise ValueError("Management SSID must differ from the module hotspot gridopoly")
    if not isinstance(password, str) or not 8 <= len(password) <= 63 or not all(32 <= ord(c) < 127 for c in password):
        raise ValueError("Passphrase must be 8-63 printable ASCII characters")
    if password == "replace-locally":
        raise ValueError("Replace the example passphrase before installation")
    channel, clients = cfg.get("fallback_channel", 6), cfg.get("max_clients", 6)
    if type(channel) is not int or channel not in range(1, 12):
        raise ValueError("Fallback channel must be a US 2.4 GHz channel (1-11)")
    if type(clients) is not int or not 1 <= clients <= 6:
        raise ValueError("The onboard management AP allows 1-6 clients")
    if cfg.get("country", "US") != "US":
        raise ValueError("This configuration has been validated for US operation only")
    return dict(ssid=ssid, passphrase=password, country="US", fallback_channel=channel, max_clients=clients)


def station_channel(link):
    """Only a real association selects the channel, not a transient scan."""
    if not link.startswith("Connected to "):
        return None
    match = re.search(r"^\s*freq:\s*(\d+)(?:\.\d+)?", link, re.M)
    if not match:
        raise ValueError("Associated uplink has no reported frequency")
    freq = int(match[1])
    if freq < 2412 or freq > 2462 or (freq - 2407) % 5:
        raise ValueError("Home Wi-Fi must use 2.4 GHz channels 1-11 for the concurrent AP")
    return (freq - 2407) // 5


def hostapd_config(cfg, channel):
    # ssid2 prevents interpretation of special characters by the config parser.
    return f'''interface={IFACE}
driver=nl80211
ssid2={cfg['ssid'].encode().hex()}
country_code={cfg['country']}
hw_mode=g
channel={channel}
ieee80211n=1
wmm_enabled=1
auth_algs=1
wpa=2
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
wpa_passphrase={cfg['passphrase']}
max_num_sta={cfg['max_clients']}
ap_isolate=1
disassoc_low_ack=0
ap_max_inactivity=300
ctrl_interface={RUN}/hostapd
'''


def dnsmasq_config():
    # Local administration only: no Internet promise, NAT, or upstream DNS.
    # Run inside the VRF so replies never follow the overlapping home subnet.
    return f'''interface={IFACE}
except-interface=lo
bind-interfaces
listen-address=10.0.0.1
no-resolv
no-hosts
domain-needed
bogus-priv
address=/gridopoly.admin/10.0.0.1
dhcp-range=10.0.0.20,10.0.0.80,255.255.255.0,1h
dhcp-option=option:router,10.0.0.1
dhcp-option=option:dns-server,10.0.0.1
dhcp-authoritative
dhcp-leasefile={STATE}/leases
pid-file={RUN}/dnsmasq.pid
log-facility=-
'''


def write_private(path, value):
    temp = path.with_suffix(path.suffix + ".tmp")
    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as stream:
        stream.write(value)
    os.replace(temp, path)


def link_info(name):
    p = command("ip", "-d", "-j", "link", "show", "dev", name, check=False)
    return json.loads(p.stdout)[0] if p.returncode == 0 else None


def assert_owned(name):
    info = link_info(name)
    if info is not None and info.get("ifalias") != ALIAS:
        raise RuntimeError(f"Refusing to modify existing unowned interface {name}")
    return info


def keep_radio_awake():
    # The virtual AP shares the station's radio. On this brcmfmac device,
    # station power saving caused AP traffic to stall when home was absent.
    # NetworkManager can re-enable it on a station reconnect, so check while
    # supervising without rewriting the user's saved home profile.
    current = command("iw", "dev", UPLINK, "get", "power_save").stdout.strip()
    if current not in ("Power save: on", "Power save: off"):
        raise RuntimeError("Cannot determine onboard radio power-save state")
    saved = RUN / "previous-power-save"
    if not saved.exists():
        write_private(saved, current.rsplit(" ", 1)[1])
    if current == "Power save: on":
        command("iw", "dev", UPLINK, "set", "power_save", "off")


def cleanup():
    # Child processes are stopped by the supervisor/systemd before this runs.
    for name in (IFACE, VRF):
        if assert_owned(name):
            if name == IFACE:
                command("iw", "dev", name, "del")
            else:
                # This route has no output device, so deleting the VRF does
                # not remove it. Remove only our exact route while ownership
                # is still established, otherwise the next start sees a
                # foreign/nonempty table and correctly refuses to use it.
                command("ip", "route", "del", "table", TABLE, "unreachable", "default",
                        "metric", "4278198272", check=False)
                command("ip", "link", "del", "dev", name)
    saved = RUN / "previous-tcp-accept"
    if saved.exists():
        previous = saved.read_text().strip()
        current = command("sysctl", "-n", "net.ipv4.tcp_l3mdev_accept").stdout.strip()
        if previous in ("0", "1") and current == "1":
            command("sysctl", "-w", "net.ipv4.tcp_l3mdev_accept=" + previous)
        saved.unlink()
    saved = RUN / "previous-power-save"
    if saved.exists():
        previous = saved.read_text().strip()
        if previous in ("on", "off"):
            command("iw", "dev", UPLINK, "set", "power_save", previous, check=False)
        saved.unlink()


def setup_network():
    if not Path(f"/sys/class/net/{UPLINK}/phy80211").exists():
        raise RuntimeError("Waiting for onboard wlan0")
    for name in (IFACE, VRF):
        assert_owned(name)
    if not link_info(VRF):
        routes = command("ip", "-j", "route", "show", "table", TABLE, check=False)
        if routes.returncode == 0 and json.loads(routes.stdout):
            raise RuntimeError("Management routing table is already in use")
        command("ip", "link", "add", VRF, "type", "vrf", "table", TABLE)
        command("ip", "link", "set", VRF, "alias", ALIAS)
    if not link_info(IFACE):
        command("iw", "dev", UPLINK, "interface", "add", IFACE, "type", "__ap")
        command("ip", "link", "set", IFACE, "alias", ALIAS)
    info = link_info(VRF)
    if info.get("linkinfo", {}).get("info_data", {}).get("table") != int(TABLE):
        raise RuntimeError("Unexpected management VRF table")
    if Path(f"/sys/class/net/{IFACE}/phy80211").resolve() != Path(f"/sys/class/net/{UPLINK}/phy80211").resolve():
        raise RuntimeError("Management AP is not on the onboard radio")
    command("nmcli", "device", "set", IFACE, "managed", "no")
    command("ip", "link", "set", VRF, "up")
    # Enslave BEFORE assigning the overlapping address. It must never appear
    # in the default namespace's local/main route tables, even briefly.
    command("ip", "link", "set", IFACE, "master", VRF)
    command("ip", "address", "replace", ADDRESS, "dev", IFACE)
    command("ip", "link", "set", IFACE, "up")
    command("ip", "route", "replace", "table", TABLE, "unreachable", "default", "metric", "4278198272")
    command("sysctl", "-w", f"net.ipv4.conf.{IFACE}.rp_filter=0")
    command("sysctl", "-w", f"net.ipv4.conf.{IFACE}.arp_ignore=1")
    command("sysctl", "-w", f"net.ipv6.conf.{IFACE}.disable_ipv6=1")
    saved = RUN / "previous-tcp-accept"
    if not saved.exists():
        write_private(saved, command("sysctl", "-n", "net.ipv4.tcp_l3mdev_accept").stdout.strip())
    # Existing wildcard HTTP and SSH listeners can serve VRF clients without
    # restarting the game. UDP game traffic remains in the default domain.
    command("sysctl", "-w", "net.ipv4.tcp_l3mdev_accept=1")
    keep_radio_awake()


class Supervisor:
    def __init__(self, cfg):
        self.cfg, self.stopping = cfg, False
        self.ap = self.dns = None
        self.channel = None
        self.last_start = 0.0

    def stop_child(self, child):
        if child is not None and child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill(); child.wait(timeout=5)

    def stop(self, *_):
        self.stopping = True

    def preferred_channel(self):
        active = station_channel(command("iw", "dev", UPLINK, "link").stdout)
        if active is not None:
            return active
        # Do not bounce the AP on short home-Wi-Fi outages. At cold boot away
        # from home, the last channel is usable without an upstream network.
        if self.channel:
            return self.channel
        try:
            saved = int((STATE / "last-channel").read_text())
            if saved in range(1, 12):
                return saved
        except (OSError, ValueError):
            pass
        return self.cfg["fallback_channel"]

    def start_ap(self, channel):
        self.stop_child(self.ap)
        self.ap = None
        write_private(RUN / "hostapd.conf", hostapd_config(self.cfg, channel))
        self.ap = subprocess.Popen(["hostapd", str(RUN / "hostapd.conf")])
        self.channel, self.last_start = channel, time.monotonic()
        write_private(STATE / "last-channel", str(channel))
        log(f"Management AP {self.cfg['ssid']} starting on channel {channel}; http://10.0.0.1")

    def run(self):
        signal.signal(signal.SIGTERM, self.stop)
        signal.signal(signal.SIGINT, self.stop)
        try:
            setup_network()
            write_private(RUN / "dnsmasq.conf", dnsmasq_config())
            while not self.stopping:
                if not link_info(IFACE) or not link_info(VRF):
                    raise RuntimeError("Management interface disappeared; rebuilding after restart")
                keep_radio_awake()
                channel = self.preferred_channel()
                alive = self.ap is not None and self.ap.poll() is None
                if alive and time.monotonic() - self.last_start > 12:
                    status = command("hostapd_cli", "-p", str(RUN / "hostapd"), "-i", IFACE, "status", check=False)
                    alive = status.returncode == 0 and "state=ENABLED" in status.stdout
                if channel != self.channel or not alive:
                    if time.monotonic() - self.last_start >= 5:
                        self.start_ap(channel)
                if self.dns is None or self.dns.poll() is not None:
                    self.dns = subprocess.Popen(["ip", "vrf", "exec", VRF, "dnsmasq", "--keep-in-foreground", "--conf-file=" + str(RUN / "dnsmasq.conf")])
                status = {"ssid": self.cfg["ssid"], "address": "10.0.0.1", "channel": self.channel,
                          "ap_running": self.ap is not None and self.ap.poll() is None,
                          "dhcp_running": self.dns is not None and self.dns.poll() is None}
                write_private(RUN / "status.json", json.dumps(status))
                time.sleep(2)
        finally:
            self.stop_child(self.ap)
            self.stop_child(self.dns)
            cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="/etc/gridopoly/admin-ap.json")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--cleanup", action="store_true")
    args = parser.parse_args()
    if args.check:
        cfg = load_config(args.config)
        print(json.dumps({k: v for k, v in cfg.items() if k != "passphrase"}))
        return
    if os.geteuid() != 0:
        raise RuntimeError("Management AP requires root")
    os.umask(0o077)
    RUN.mkdir(mode=0o700, exist_ok=True)
    STATE.mkdir(mode=0o750, exist_ok=True)
    with (RUN / "lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if args.cleanup:
            cleanup()
        else:
            Supervisor(load_config(args.config)).run()


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        log(f"Management AP stopped: {type(exc).__name__}: {exc}")
        sys.exit(1)
