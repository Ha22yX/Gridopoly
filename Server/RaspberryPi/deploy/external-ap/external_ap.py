#!/usr/bin/env python3
"""Optional, deliberately narrow Pi external-AP deployment. No action by default.

Only the audited wlan0-management / ap0 -> eth0 layout is supported. Commands
use argv, never a shell. Trial changes have an independent systemd rollback
timer; existing config files and the game server are never rewritten/restarted.
"""
from __future__ import annotations

import argparse
import contextlib
import ipaddress
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.request

UUID = "84c34b55-8205-4b61-bfea-744c611d6940"
NAME = "gridopoly-external-eth0"
STATE = "/var/lib/gridopoly-external-ap"
PROFILE = "/etc/NetworkManager/system-connections/gridopoly-external-eth0.nmconnection"
CONF = "/etc/gridopoly/external-ap-dnsmasq.conf"
UNIT = "gridopoly-external-dnsmasq.service"
UNIT_PATH = "/etc/systemd/system/" + UNIT
TIMER = "gridopoly-external-rollback"
OLD = ("gridopoly-ap-watchdog.service", "gridopoly-dnsmasq.service", "gridopoly-ap.service")
NETWORK = ipaddress.ip_network("10.42.0.0/24")
EXPECTED_SERVER = {"GRIDOPOLY_HTTP_PORT": "80", "GRIDOPOLY_SERVICE_IP": "10.42.0.1",
                   "GRIDOPOLY_UDP_BIND": "0.0.0.0", "GRIDOPOLY_UDP_BROADCAST": "10.42.0.255"}


class Refused(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise Refused(message)


def configs(interface="eth0"):
    # Not a generic network manager: reject option, keyfile and shell injection.
    require(interface == "eth0", "Only the audited interface eth0 is supported")
    return {
        PROFILE: f"""[connection]
id={NAME}
uuid={UUID}
type=ethernet
interface-name=eth0
autoconnect=false
autoconnect-priority=999

[ethernet]

[ipv4]
method=manual
address1=10.42.0.1/24
never-default=true
ignore-auto-dns=true

[ipv6]
method=disabled
""",
        CONF: """# Isolated AP bridge only: no NAT or upstream DNS is configured here.
interface=eth0
except-interface=lo
bind-dynamic
listen-address=10.42.0.1
dhcp-range=10.42.0.20,10.42.0.200,255.255.255.0,12h
dhcp-lease-max=181
dhcp-leasefile=/var/lib/misc/dnsmasq.leases
# Preserve the nonzero gateway required by existing Tile firmware readiness.
dhcp-option=3,10.42.0.1
dhcp-option=6,10.42.0.1
domain=gridopoly.local
address=/gridopoly.local/10.42.0.1
no-resolv
no-hosts
""",
        UNIT_PATH: """[Unit]
Description=Gridopoly external AP DHCP and local DNS
Wants=network-online.target
After=network-online.target NetworkManager.service
Before=gridopoly.service

[Service]
Type=simple
ExecStart=/usr/sbin/dnsmasq --no-daemon --conf-file=/etc/gridopoly/external-ap-dnsmasq.conf
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
""",
    }


def parse_env(text):
    result = {}
    for line in text.splitlines():
        if line.strip() and not line.lstrip().startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            require(key.strip() not in result, "Duplicate environment key")
            result[key.strip()] = value.strip().strip('"').strip("'")
    return result


def validate_network(addresses, routes, ssh, route_to_client, carrier, nm_state):
    require(carrier.strip() == "1", "eth0 has no carrier; attach the isolated AP first")
    require(nm_state.split()[:1] == ["30"], "eth0 must be NM-managed and disconnected, with no active profile")
    fields = ssh.split()
    require(len(fields) == 4, "Preserve SSH_CONNECTION; apply requires an independent wlan0 SSH session")
    client, local = ipaddress.ip_address(fields[0]), ipaddress.ip_address(fields[2])
    require(client.version == local.version == 4, "This installer supports IPv4 management SSH only")
    require(client not in NETWORK and local not in NETWORK, "SSH must not use the AP subnet")
    require(len(route_to_client) == 1 and route_to_client[0].get("dev") == "wlan0",
            "Management SSH return route must use wlan0, never eth0/ap0")
    wlan_ips = {a["local"] for x in addresses if x["ifname"] == "wlan0" for a in x.get("addr_info", [])}
    require(str(local) in wlan_ips, "SSH local address must belong to wlan0")
    ap_ips = []
    for link in addresses:
        interface = link["ifname"]
        if interface == "eth0":
            require(not link.get("master"), "eth0 is enslaved to another interface")
            require(not any(a.get("scope") == "global" for a in link.get("addr_info", [])),
                    "eth0 already has a global address")
        for addr in link.get("addr_info", []):
            if addr["family"] != "inet":
                continue
            net = ipaddress.ip_network(f"{addr['local']}/{addr['prefixlen']}", strict=False)
            if interface == "ap0":
                ap_ips.append(f"{addr['local']}/{addr['prefixlen']}")
            elif net.overlaps(NETWORK):
                raise Refused("Another interface overlaps 10.42.0.0/24: " + interface)
    require(ap_ips == ["10.42.0.1/24"], "Expected the original ap0 address 10.42.0.1/24")
    for route in routes:
        dst = route.get("dst", "default")
        if dst != "default" and route.get("dev") != "ap0":
            if ipaddress.ip_network(dst, strict=False).overlaps(NETWORK):
                raise Refused("Another route overlaps the AP subnet")
        require(route.get("dev") != "eth0", "eth0 already carries an IPv4 route")


class Host:
    def path(self, name):
        return Path(name)

    def run(self, *args, check=True):
        env = dict(os.environ, PATH="/usr/sbin:/usr/bin:/sbin:/bin", LC_ALL="C")
        result = subprocess.run(args, text=True, capture_output=True, timeout=40, env=env)
        if check and result.returncode:
            raise Refused(f"Command failed ({result.returncode}): {args!r}: {result.stderr.strip()}")
        return result.stdout.strip()

    def read(self, name):
        return self.path(name).read_text(encoding="utf-8")

    def write(self, name, content, mode=0o600):
        path = self.path(name)
        require(not path.is_symlink(), "Refusing symlink: " + name)
        path.parent.mkdir(parents=True, exist_ok=True)
        # Replace only installer-owned files. System destination existence was
        # checked before any mutation. State writes are atomic for the timer.
        temp = path.with_name(path.name + ".new")
        with temp.open("x", encoding="utf-8", newline="\n") as stream:
            os.chmod(temp, mode)
            stream.write(content)
        os.replace(temp, path)

    def health(self):
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with opener.open("http://10.42.0.1/health", timeout=4) as response:
            require(response.status == 200 and json.load(response).get("ok") is True,
                    "Game server health check failed")


def preflight(host, ssh):
    addresses = json.loads(host.run("ip", "-j", "address", "show"))
    fields = ssh.split()
    require(len(fields) == 4, "Use sudo --preserve-env=SSH_CONNECTION from wlan0 SSH")
    client = str(ipaddress.ip_address(fields[0]))
    validate_network(addresses, json.loads(host.run("ip", "-j", "-4", "route", "show", "table", "all")),
                     ssh, json.loads(host.run("ip", "-j", "route", "get", client)),
                     host.read("/sys/class/net/eth0/carrier"),
                     host.run("nmcli", "-g", "GENERAL.STATE", "device", "show", "eth0"))
    require(UUID not in host.run("nmcli", "-g", "UUID", "connection", "show").splitlines(),
            "External profile UUID already exists")
    for target in (*configs(), STATE):
        require(not host.path(target).exists() and not host.path(target).is_symlink(), "Already exists: " + target)
    for unit in OLD:
        require(host.run("systemctl", "is-active", unit) == "active", "Original AP services must be active")
        require(host.run("systemctl", "is-enabled", unit) == "enabled", "Original AP services must be enabled")
    require(host.run("systemctl", "is-active", "NetworkManager.service", "gridopoly.service").splitlines()
            == ["active", "active"], "NetworkManager and game server must be active")
    require(host.run("systemctl", "show", UNIT, "-p", "LoadState", "--value", check=False) == "not-found",
            "External DHCP unit already installed elsewhere")
    for prop in ("Requires", "Wants", "BindsTo", "PartOf"):
        dependencies = host.run("systemctl", "show", "gridopoly.service", "-p", prop, "--value").split()
        require(not set(OLD).intersection(dependencies), "Game service depends on original AP: " + prop)
    ap = parse_env(host.read("/etc/gridopoly/ap.env"))
    require(all(ap.get(k) == v for k, v in {
        "GRIDOPOLY_UPLINK_IFACE": "wlan0", "GRIDOPOLY_AP_PARENT_IFACE": "wlan0",
        "GRIDOPOLY_AP_IFACE": "ap0", "GRIDOPOLY_AP_ADDRESS": "10.42.0.1/24"}.items()),
        "Original AP configuration differs from audited baseline")
    server = parse_env(host.read("/etc/gridopoly/server.env"))
    require(all(server.get(k) == v for k, v in EXPECTED_SERVER.items()), "Server network values differ from baseline")
    for binary in ("/usr/sbin/dnsmasq", "/usr/bin/python3", "/usr/bin/systemd-run"):
        require(host.path(binary).is_file(), "Required binary missing: " + binary)
    host.health()


def save_state(host, state):
    host.write(STATE + "/state.json", json.dumps(state, indent=2) + "\n")


def load_state(host):
    return json.loads(host.read(STATE + "/state.json"))


def verify_trial(host):
    require(host.read("/sys/class/net/eth0/carrier").strip() == "1", "eth0 lost carrier")
    addresses = json.loads(host.run("ip", "-j", "-4", "address", "show"))
    owners = [(x["ifname"], a["prefixlen"]) for x in addresses for a in x.get("addr_info", [])
              if a["local"] == "10.42.0.1"]
    require(owners == [("eth0", 24)], "10.42.0.1 must exist exclusively on eth0/24")
    for unit in OLD:
        require(host.run("systemctl", "is-active", unit, check=False) != "active", "Old AP service restarted")
    require(host.run("systemctl", "is-active", UNIT) == "active", "External DHCP service not active")
    require(host.run("systemctl", "is-active", "gridopoly.service") == "active", "Game server not active")
    udp = host.run("ss", "-H", "-l", "-u", "-n")
    require(any(field in ("0.0.0.0:4242", "*:4242") for field in udp.split()), "UDP/4242 not listening")
    require("10.42.0.1:53" in udp.split(), "Local DNS/53 not listening on service address")
    require(any(field in ("0.0.0.0:67", "*:67", "10.42.0.1:67") for field in udp.split()),
            "DHCP/67 not listening")
    host.health()


def rollback(host, expired=False):
    state = load_state(host)
    if state["phase"] == "rolled-back" or (expired and state["phase"] == "committed"):
        return
    # Stop the timer, never this rollback's own transient service.
    host.run("systemctl", "stop", TIMER + ".timer", check=False)
    errors = []
    def attempt(*args):
        try:
            host.run(*args)
        except Exception as exc:
            errors.append(str(exc))
    if host.run("systemctl", "show", UNIT, "-p", "LoadState", "--value", check=False) != "not-found":
        attempt("systemctl", "stop", UNIT)
        attempt("systemctl", "disable", UNIT)
    # Remove the address before restarting ap0; no duplicate service address.
    if host.run("nmcli", "-g", "GENERAL.STATE", "device", "show", "eth0").split()[:1] != ["30"]:
        attempt("nmcli", "--wait", "15", "device", "disconnect", "eth0")
    present = host.run("nmcli", "-g", "UUID", "connection", "show", check=False).splitlines()
    if UUID in present:
        attempt("nmcli", "connection", "modify", "uuid", UUID, "connection.autoconnect", "no")
        attempt("nmcli", "connection", "delete", "uuid", UUID)
    for target in configs():
        path = host.path(target)
        if path.exists() and not path.is_symlink():
            path.unlink()
    attempt("systemctl", "daemon-reload")
    attempt("nmcli", "connection", "reload")
    # Original files were never overwritten; private backup also retained.
    addresses = json.loads(host.run("ip", "-j", "-4", "address", "show"))
    conflict = any(a["local"] == "10.42.0.1" for x in addresses if x["ifname"] != "ap0"
                   for a in x.get("addr_info", []))
    if conflict:
        errors.append("10.42.0.1 still exists off ap0; refused to create duplicate address. wlan0 is untouched.")
    else:
        attempt("systemctl", "enable", *reversed(OLD))
        for unit in reversed(OLD):
            attempt("systemctl", "start", unit)
    state["phase"] = "rollback-failed" if errors else "rolled-back"
    state["errors"] = errors
    save_state(host, state)
    require(not errors, "Rollback needs operator review: " + "; ".join(errors))


def apply(host, ssh, ap_ready):
    require(ap_ready, "Explicit --isolated-ap-ready required: isolated cable, AP bridge, DHCP off")
    preflight(host, ssh)
    root = host.path(STATE)
    root.mkdir(mode=0o700)
    # Backup is root-only: server/ap env may contain credentials, never print it.
    for original in ("/etc/gridopoly/ap.env", "/etc/gridopoly/server.env", "/etc/gridopoly/dnsmasq.conf"):
        host.write(STATE + "/backup/" + Path(original).name, host.read(original))
    for unit in OLD:
        host.write(STATE + "/backup/" + unit, host.run("systemctl", "cat", unit))
    host.write(STATE + "/external_ap.py", Path(__file__).read_text(encoding="utf-8"))
    state = {"phase": "pending", "deadline": time.time() + 300, "management_ssh": ssh,
             "baseline": "three original AP services active/enabled; eth0 disconnected"}
    save_state(host, state)
    armed = False
    try:
        # Arm before touching any interface or service. Transient timer survives
        # SSH/process exit, but not a power loss; trial is deliberately non-bootable.
        host.run("systemd-run", "--unit=" + TIMER, "--on-active=300s", "--timer-property=AccuracySec=1s",
                 "/usr/bin/python3", STATE + "/external_ap.py", "--rollback-expired")
        armed = True
        for target, content in configs().items():
            require(not host.path(target).exists() and not host.path(target).is_symlink(),
                    "Destination appeared after preflight: " + target)
            host.write(target, content, 0o600 if target == PROFILE else 0o644)
        host.run("dnsmasq", "--test", "--conf-file=" + CONF)
        host.run("systemd-analyze", "verify", UNIT_PATH)
        host.run("systemctl", "daemon-reload")
        host.run("nmcli", "connection", "load", PROFILE)
        host.run("systemctl", "stop", *OLD)
        # The original AP cleanup should delete ap0. If it does not, fail safe.
        addresses = json.loads(host.run("ip", "-j", "-4", "address", "show"))
        require(not any(a["local"] == "10.42.0.1" for x in addresses for a in x.get("addr_info", [])),
                "Original AP cleanup left 10.42.0.1 configured")
        host.run("nmcli", "--wait", "20", "connection", "up", "uuid", UUID, "ifname", "eth0")
        host.run("systemctl", "start", UNIT)
        verify_trial(host)
    except BaseException:
        if armed:
            rollback(host)
        raise
    print("TRIAL ACTIVE: 300-second rollback timer. Check a real AP client; then --commit --ap-client-verified.")


def commit(host, ssh, client_verified):
    state = load_state(host)
    require(state["phase"] == "pending" and time.time() < state["deadline"], "No unexpired trial to commit")
    require(client_verified, "Verify a real AP client's DHCP, HTTP and device UDP before --ap-client-verified")
    fields = ssh.split()
    require(len(fields) == 4, "Preserve SSH_CONNECTION for commit")
    client = str(ipaddress.ip_address(fields[0]))
    route = json.loads(host.run("ip", "-j", "route", "get", client))
    require(len(route) == 1 and route[0].get("dev") == "wlan0", "Commit must retain independent wlan0 management")
    verify_trial(host)
    try:
        # Disable the old boot path first: power loss mid-commit must not leave
        # two independently autostarting interfaces with 10.42.0.1. wlan0 stays
        # untouched; an interrupted commit can be rolled back over that link.
        host.run("systemctl", "disable", *OLD)
        host.run("systemctl", "enable", UNIT)
        host.run("nmcli", "connection", "modify", "uuid", UUID, "connection.autoconnect", "yes")
        state["phase"] = "committed"
        save_state(host, state)
        host.run("systemctl", "stop", TIMER + ".timer")
    except BaseException:
        rollback(host)
        raise
    print("COMMITTED. Original files retained; --rollback remains available. Reboot/36-client acceptance still required.")


@contextlib.contextmanager
def operation_lock():
    import fcntl  # Linux only; generator and tests are portable.
    with open("/run/lock/gridopoly-external-ap.lock", "a", encoding="utf-8") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        yield


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true", help="default: print configs, no commands/writes")
    mode.add_argument("--generate", metavar="DIRECTORY", help="create a NEW review directory only")
    mode.add_argument("--check", action="store_true", help="read-only target preflight; root to read private config")
    mode.add_argument("--apply", action="store_true", help="start a 300-second trial; requires explicit readiness")
    mode.add_argument("--commit", action="store_true", help="persist a verified trial")
    mode.add_argument("--rollback", action="store_true", help="restore original AP services")
    mode.add_argument("--rollback-expired", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--interface", default="eth0")
    parser.add_argument("--isolated-ap-ready", action="store_true")
    parser.add_argument("--ap-client-verified", action="store_true")
    args = parser.parse_args(argv)
    rendered = configs(args.interface)
    if args.generate:
        directory = Path(args.generate)
        require(not directory.exists(), "Review directory must not already exist")
        directory.mkdir(parents=True)
        for target, content in rendered.items():
            (directory / Path(target).name).write_text(content, encoding="utf-8", newline="\n")
        print("Generated review files only: " + str(directory.resolve()))
        return
    if not any((args.check, args.apply, args.commit, args.rollback, args.rollback_expired)):
        for target, content in rendered.items():
            print("# Proposed file: " + target + "\n" + content)
        print("DRY RUN: no files written, no commands executed, no AP settings changed.")
        return
    require(sys.platform == "linux" and os.geteuid() == 0, "Target actions require Linux root")
    host = Host()
    ssh = os.environ.get("SSH_CONNECTION", "")
    if args.check:
        preflight(host, ssh)
        print("PREFLIGHT PASS (read-only). AP isolation/DHCP-off must still be verified by operator.")
        return
    with operation_lock():
        if args.apply:
            apply(host, ssh, args.isolated_ap_ready)
        elif args.commit:
            commit(host, ssh, args.ap_client_verified)
        else:
            rollback(host, expired=args.rollback_expired)


if __name__ == "__main__":
    try:
        main()
    except (Refused, OSError, ValueError, subprocess.TimeoutExpired) as exc:
        print("REFUSED: " + str(exc), file=sys.stderr)
        sys.exit(1)
