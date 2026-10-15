#!/usr/bin/env python3
"""Read-only AWUS036ACM preflight and private review bundle. No apply command.

Uses the installed Gridopoly AP service/script as its source, not the unrelated
eth0 deployment engine. Does not rename devices, change addresses, restart
services, load drivers, or install files. Requires Python 3.9+ on the target Pi.
"""
from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

IFACE = "gpusb0"
NETWORK = ipaddress.ip_network("10.42.0.0/24")
ENV = "/etc/gridopoly/ap.env"
DNS = "/etc/gridopoly/dnsmasq.conf"
AP = "/etc/systemd/system/gridopoly-ap.service"
GAME = "/etc/systemd/system/gridopoly.service"
SCRIPT = "/usr/local/libexec/gridopoly-ap"
USB_SCRIPT = "/usr/local/libexec/gridopoly-usb-ap"
GUARD = "/usr/local/libexec/gridopoly-usb-ap-guard"
LINK = "/etc/systemd/network/10-gridopoly-usb.link"
NM = "/etc/NetworkManager/conf.d/90-gridopoly-usb.conf"
UDEV = "/etc/udev/rules.d/90-gridopoly-usb.rules"
UNITS = ("gridopoly-ap.service", "gridopoly-dnsmasq.service",
         "gridopoly-ap-watchdog.service", "gridopoly.service")
EXPECTED_SERVER = {"GRIDOPOLY_HTTP_PORT": "80", "GRIDOPOLY_SERVICE_IP": "10.42.0.1",
                   "GRIDOPOLY_UDP_BIND": "0.0.0.0", "GRIDOPOLY_UDP_BROADCAST": "10.42.0.255"}
# The two script variants reviewed for this preparation. Unknown scripts need
# review, rather than silently copying a changed networking implementation.
AP_SOURCES = {"0538095d81181708fd67f7bfb090205b0c0fd64915a1674e17b8359c09592930",
              "180d17d168b4935d1db03a8937c6f5475bbaa7d09bc5d9f85d721c36fab81842"}
VALUES = {"GRIDOPOLY_UPLINK_IFACE": "wlan0", "GRIDOPOLY_AP_PARENT_IFACE": IFACE,
          "GRIDOPOLY_AP_IFACE": IFACE, "GRIDOPOLY_AP_ADDRESS": "10.42.0.1/24",
          "GRIDOPOLY_AP_MAX_INACTIVITY_SECONDS": "10", "GRIDOPOLY_AP_DISASSOC_LOW_ACK": "0",
          "GRIDOPOLY_STALE_STATION_GRACE_SECONDS": "20",
          "GRIDOPOLY_STALE_STATION_RX_GRACE_SECONDS": "45",
          "GRIDOPOLY_STALE_STATION_POLL_SECONDS": "2"}


class Refused(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise Refused(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def mac_address(value):
    value = value.lower()
    require(re.fullmatch(r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", value), "Invalid permanent MAC")
    require(value != "00:00:00:00:00:00" and not int(value[:2], 16) & 1,
            "A nonzero unicast permanent MAC is required")
    return value


def parse_env(data):
    result = {}
    for line in data.decode().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        require(re.fullmatch(r"[A-Z0-9_]+=.*", line), "Unsupported environment syntax")
        key, value = line.split("=", 1)
        require(key not in result, "Duplicate environment key")
        result[key] = value.strip().strip('"').strip("'")
    return result


def select_device(devices, mac, explicit=None):
    mac = mac_address(mac)
    found = [d for d in devices if d.get("permanent_mac") == mac]
    require(len(found) == 1, "USB device missing or permanent MAC is ambiguous; no bundle generated")
    device = found[0]
    require(not explicit or device["name"] == explicit, "Explicit interface does not match permanent MAC")
    require(device["name"] not in ("wlan0", "ap0", "lo", "eth0"), "Protected management/old AP interface")
    require(device["driver"] == "mt76x2u" and device["usb_id"] == "0e8d:7612",
            "Expected MT7612U USB ID 0e8d:7612 and mt76x2u driver; review other hardware separately")
    require(device["mac"] == mac, "Current MAC differs from permanent MAC; resolve randomization first")
    require(not device["master"], "USB device is enslaved to another network")
    management = [d for d in devices if d["name"] == "wlan0"]
    require(len(management) == 1 and management[0]["phy"], "wlan0 management radio missing")
    require(device["phy"] and device["phy"] != management[0]["phy"], "USB needs an independent PHY")
    require(sum(d["phy"] == device["phy"] for d in devices) == 1,
            "USB PHY has another interface; dedicated AP must not also run STA")
    require(not any(d["name"] == IFACE and d is not device for d in devices), "Stable interface name occupied")
    return device


def validate_network(addresses, routes, interface, ssh, route_to_client):
    old = []
    for device in addresses:
        for entry in device.get("addr_info", []):
            if device["ifname"] == interface:
                require(entry.get("scope") != "global", "USB interface already has a global address")
            if entry["family"] != "inet":
                continue
            net = ipaddress.ip_interface(f'{entry["local"]}/{entry["prefixlen"]}').network
            if net.overlaps(NETWORK):
                require(device["ifname"] == "ap0" and entry["local"] == "10.42.0.1"
                        and entry["prefixlen"] == 24, "Game subnet/address conflict")
                old.append(device["ifname"])
    require(old == ["ap0"], "Expected the sole game address on original ap0")
    for route in routes:
        require(route.get("dev") != interface, "USB interface already has a route")
        if route.get("dst", "default") != "default":
            network = ipaddress.ip_network(route["dst"], strict=False)
            require(not network.overlaps(NETWORK) or route.get("dev") == "ap0", "Game subnet route conflict")
    fields = ssh.split()
    require(len(fields) == 4 and route_to_client == "wlan0", "Preparation requires independent wlan0 SSH")
    client, local = (ipaddress.ip_address(fields[i]) for i in (0, 2))
    require(client.version == local.version == 4 and client not in NETWORK and local not in NETWORK,
            "SSH must not use the game network")
    require(any(d["ifname"] == "wlan0" and any(a.get("local") == str(local) for a in d.get("addr_info", []))
                for d in addresses), "SSH server address is not on wlan0")


def validate_radio(info, regulatory, country, channel, review_derived_regdom=False):
    require(re.fullmatch(r"[A-Z]{2}", country), "Supply the actual operating country as two uppercase letters")
    require(channel in (1, 6, 11), "This 2.4 GHz preparation supports channels 1, 6, or 11")
    require(re.search(r"^\s*\* AP\s*$", info, re.M), "Driver does not advertise AP mode")
    frequencies = [line.lower() for line in info.splitlines() if re.search(rf"\[{channel}\]", line)]
    require(len(frequencies) == 1 and re.search(rf"\b{2407 + channel * 5}(?:\.0+)? mhz\b", frequencies[0])
            and not any(x in frequencies[0] for x in ("disabled", "no ir", "no-ir", "radar")),
            "Selected 2.4 GHz channel is not currently usable for AP")
    reported = set(re.findall(r"\bcountry ([A-Z0-9]{2}):", regulatory))
    exact = country in reported and reported <= {country, "00"}
    # Linux reg.c: 98 is an intersection, 99 is driver-built/unknown alpha2.
    # They do not confirm a location. Only a deliberately reviewed preparation
    # may use the explicit operator-supplied country with these special codes.
    derived = (review_derived_regdom and bool(reported & {"98", "99"})
               and reported <= {country, "00", "98", "99"})
    require(exact or derived,
            "Requested country absent from current regulatory state; review separately, no automatic change")
    return {"reportedCodes": sorted(reported), "reviewDerivedRegdom": review_derived_regdom,
            "derivedDomainAccepted": bool(derived and not exact), "raw": regulatory}


class Host:
    """Only read operations. subprocess is private to this bounded collector."""
    def read(self, path):
        return Path(path).read_bytes()

    def capture(self, path):
        file = Path(path)
        require(not file.is_symlink(), "Refusing symlink source/destination: " + path)
        if not file.exists():
            return None
        require(file.is_file(), "Expected regular file: " + path)
        st = file.stat()
        return {"data": file.read_bytes(), "mode": stat.S_IMODE(st.st_mode), "uid": st.st_uid, "gid": st.st_gid}

    def run(self, *args, optional=False):
        # All callers below use literal read-only operations, argv, and a timeout.
        result = subprocess.run(args, capture_output=True, text=True, timeout=15, check=False)
        require(optional or result.returncode == 0, "Read-only command failed: " + args[0])
        return result.stdout if result.returncode == 0 else ""

    def devices(self):
        result = []
        for path in sorted(Path("/sys/class/net").iterdir()):
            phy = path / "phy80211"
            permanent = self.run("ethtool", "-P", path.name, optional=True) if phy.exists() else ""
            match = re.search(r"Permanent address: ([0-9a-fA-F:]{17})", permanent)
            hardware = (path / "device").resolve()
            usb = next((p for p in (hardware, *hardware.parents) if (p / "idVendor").exists()), None)
            result.append({"name": path.name, "phy": phy.resolve().name if phy.exists() else None,
                           "driver": (hardware / "driver").resolve().name,
                           "usb_id": ((usb / "idVendor").read_text().strip() + ":" +
                                      (usb / "idProduct").read_text().strip()) if usb else None,
                           "mac": (path / "address").read_text().strip().lower(),
                           "permanent_mac": match[1].lower() if match else None,
                           "master": (path / "master").exists()})
        return result

    def firmware_present(self, name):
        return any(Path("/lib/firmware", name + ext).is_file() for ext in ("", ".xz", ".zst"))


def remove_online_wait(data):
    # Dependencies cannot be cleared in drop-ins. Rewrite the complete copied
    # unit, retaining unrelated directives. Refuse continuations for this small parser.
    require(not re.search(r"\\\s*$", data, re.M), "Unit continuations require manual review")
    output = []
    for line in data.splitlines():
        if line.startswith(("After=", "Wants=")):
            key, value = line.split("=", 1)
            values = [x for x in value.split() if x not in ("network-online.target", "NetworkManager-wait-online.service")]
            if values:
                output.append(key + "=" + " ".join(values))
        else:
            output.append(line)
    result = "\n".join(output) + "\n"
    require("network-online.target" not in result and "NetworkManager-wait-online.service" not in result,
            "Unexpected online dependency requires review")
    return result


def replace_once(text, old, new):
    require(text.count(old) == 1, "Installed config layout changed; review required")
    return text.replace(old, new, 1)


def candidates(originals, mac, country, channel, guard_template=None):
    env = originals[ENV]["data"]
    current = parse_env(env)
    require(current.get("GRIDOPOLY_AP_SSID") == "gridopoly", "Unexpected SSID")
    require(8 <= len(current.get("GRIDOPOLY_AP_PASSPHRASE", "")) <= 63, "Missing/invalid WPA passphrase")
    require(all(current.get(k) == v for k, v in {"GRIDOPOLY_UPLINK_IFACE": "wlan0",
            "GRIDOPOLY_AP_PARENT_IFACE": "wlan0", "GRIDOPOLY_AP_IFACE": "ap0",
            "GRIDOPOLY_AP_ADDRESS": "10.42.0.1/24"}.items()), "Unexpected original AP topology")
    values = dict(VALUES, GRIDOPOLY_AP_CHANNEL=str(channel))
    # Keep unknown lines and credentials intact, avoiding shell/environment re-quoting.
    lines = [line for line in env.decode().splitlines() if line.split("=", 1)[0] not in values]
    new_env = "\n".join(lines + [f"{k}={v}" for k, v in values.items()]) + "\n"
    script = originals[SCRIPT]["data"]
    require(sha(script) in AP_SOURCES, "Installed AP script is not a reviewed 10/0-compatible source")
    new_script = replace_once(script.decode(), "country_code=CN", "country_code=" + country)
    dns = originals[DNS]["data"].decode()
    for required in ("listen-address=10.42.0.1", "dhcp-option=3,10.42.0.1", "dhcp-option=6,10.42.0.1",
                     "dhcp-range=10.42.0.20,10.42.0.200,255.255.255.0,12h", "bind-dynamic", "no-resolv", "no-hosts"):
        require(required in dns.splitlines(), "Unexpected DHCP configuration")
    require([x for x in dns.splitlines() if x.startswith("interface=")] == ["interface=ap0"],
            "DHCP must bind only the original AP")
    for prefix, expected in (("listen-address=", ["listen-address=10.42.0.1"]),
                             ("dhcp-range=", ["dhcp-range=10.42.0.20,10.42.0.200,255.255.255.0,12h"]),
                             ("dhcp-option=", ["dhcp-option=3,10.42.0.1", "dhcp-option=6,10.42.0.1"])):
        require(sorted(x for x in dns.splitlines() if x.startswith(prefix)) == sorted(expected),
                "Unexpected or duplicate DHCP binding/options")
    dns = replace_once(dns, "interface=ap0", "interface=" + IFACE)
    ap_unit = remove_online_wait(originals[AP]["data"].decode())
    ap_unit = replace_once(ap_unit, "ExecStart=" + SCRIPT, "ExecStart=" + USB_SCRIPT)
    ap_unit = replace_once(ap_unit, "ExecStopPost=/usr/local/libexec/gridopoly-ap-cleanup",
                           "ExecStopPost=" + GUARD + " cleanup")
    ap_unit = replace_once(ap_unit, "[Service]", "[Service]\nExecStartPre=" + GUARD + " check")
    # Wait only for this device, never for management Wi-Fi / Internet.
    ap_unit = replace_once(ap_unit, "[Unit]", "[Unit]\nAfter=sys-subsystem-net-devices-gpusb0.device\n"
                           "BindsTo=sys-subsystem-net-devices-gpusb0.device\nJobTimeoutSec=30")
    game_unit = remove_online_wait(originals[GAME]["data"].decode())
    guard = guard_template if guard_template is not None else Path(__file__).with_name("guard.py.in").read_text(encoding="utf-8")
    guard = guard.replace("@MAC@", mac)
    return {ENV: new_env.encode(), DNS: dns.encode(), AP: ap_unit.encode(), GAME: game_unit.encode(),
            USB_SCRIPT: new_script.encode(), GUARD: guard.encode(),
            LINK: f"[Match]\nPermanentMACAddress={mac}\nDriver=mt76x2u\n\n[Link]\nNamePolicy=\nName={IFACE}\nMACAddressPolicy=none\n".encode(),
            NM: f"[device-gridopoly-usb]\nmatch-device=mac:{mac}\nmanaged=0\n".encode(),
            UDEV: ('# Retain activation properties through the move/change events caused by .link renaming.\n'
                   f'ACTION!="remove", SUBSYSTEM=="net", DRIVERS=="mt76x2u", ATTR{{address}}=="{mac}", '
                   'TAG+="systemd", ENV{SYSTEMD_WANTS}+="gridopoly-ap.service gridopoly-dnsmasq.service gridopoly-ap-watchdog.service"\n').encode()}


def preflight(host, mac, country, channel, explicit=None, ssh="", review_derived_regdom=False):
    device = select_device(host.devices(), mac, explicit)
    radio = validate_radio(host.run("iw", "phy", device["phy"], "info"), host.run("iw", "reg", "get"),
                           country, channel, review_derived_regdom)
    require(host.run("nmcli", "-g", "GENERAL.STATE", "device", "show", device["name"]).split()[:1] == ["30"],
            "USB must be NetworkManager-managed and disconnected for initial preparation")
    fields = ssh.split()
    require(len(fields) == 4, "Preserve SSH_CONNECTION for independent management check")
    # Validate before passing the address as argv, including refusal of options.
    client = str(ipaddress.ip_address(fields[0]))
    route = json.loads(host.run("ip", "-j", "-4", "route", "get", client))
    validate_network(json.loads(host.run("ip", "-j", "address", "show")),
                     json.loads(host.run("ip", "-j", "-4", "route", "show", "table", "all")),
                     device["name"], ssh, route[0].get("dev") if route else None)
    firmware = host.run("modinfo", "-F", "firmware", "mt76x2u").splitlines()
    require(firmware, "Installed mt76x2u module has no firmware metadata")
    for name in firmware:
        require(not name.startswith("/") and ".." not in Path(name).parts, "Unexpected firmware path")
        require(host.firmware_present(name),
                "Driver firmware missing: " + name)
    for unit in UNITS:
        require(host.run("systemctl", "is-active", unit).strip() == "active", "Original service not active: " + unit)
        require(host.run("systemctl", "is-enabled", unit).strip() == "enabled", "Original service not enabled: " + unit)
        require(not host.run("systemctl", "show", "--value", "--property=DropInPaths", unit).strip(),
                "Service drop-ins require manual review: " + unit)
        require(host.run("systemctl", "show", "--value", "--property=FragmentPath", unit).strip()
                == "/etc/systemd/system/" + unit, "Unexpected unit source: " + unit)
    runtime = dict(line.split("=", 1) for line in host.read("/run/gridopoly/hostapd.conf").decode().splitlines()
                   if "=" in line)
    require(all(runtime.get(k) == v for k, v in {"interface": "ap0", "ssid": "gridopoly",
                "ap_max_inactivity": "10", "disassoc_low_ack": "0"}.items()), "Runtime AP policy/topology changed")
    server = parse_env(host.read("/etc/gridopoly/server.env"))
    require(all(server.get(k) == v for k, v in EXPECTED_SERVER.items()), "Server network contract changed")
    source_paths = (ENV, DNS, AP, GAME, SCRIPT, "/etc/gridopoly/server.env",
                    "/usr/local/libexec/gridopoly-ap-cleanup", "/usr/local/libexec/gridopoly-ap-watchdog",
                    "/etc/systemd/system/gridopoly-dnsmasq.service", "/etc/systemd/system/gridopoly-ap-watchdog.service")
    originals = {p: host.capture(p) for p in source_paths}
    require(all(originals.values()), "A required original file is missing")
    for path in (USB_SCRIPT, GUARD, LINK, NM, UDEV):
        require(host.capture(path) is None, "USB candidate destination already exists; review before reuse")
        originals[path] = None
    generated = candidates(originals, mac_address(mac), country, channel)
    return originals, generated, {"device": device, "country": country, "channel": channel,
                                  "firmware": firmware, "regulatory": radio,
                                  "scope": "review-only; not applied or capacity tested"}


def write_bundle(directory, originals, generated, evidence):
    """Create a NEW private directory. No writes outside it and no installer."""
    directory = Path(directory)
    require(not directory.exists(), "Output directory exists; use a new private directory")
    directory.mkdir(mode=0o700)
    os.chmod(directory, 0o700)
    manifest = {"schema": 1, "evidence": evidence, "files": {}}
    for path in sorted(originals):
        original = originals[path]
        item = {"original": None, "candidate": None}
        if original:
            item["original"] = {k: v for k, v in original.items() if k != "data"}
            item["original"]["sha256"] = sha(original["data"])
        if path in generated:
            item["candidate"] = {"sha256": sha(generated[path]),
                                 "mode": 0o700 if path in (USB_SCRIPT, GUARD) else
                                         0o600 if path == ENV else 0o644}
        manifest["files"][path] = item
        for tree, data in (("original", original["data"] if original else None),
                           ("candidate", generated.get(path))):
            if data is not None:
                output = directory / tree / path.lstrip("/")
                output.parent.mkdir(parents=True, exist_ok=True)
                with output.open("xb") as stream:
                    os.chmod(output, 0o600)
                    stream.write(data)
    with (directory / "manifest.json").open("x", encoding="utf-8") as stream:
        os.chmod(stream.name, 0o600)
        json.dump(manifest, stream, indent=2)
    verify_bundle(directory)
    return manifest


def verify_bundle(directory):
    """Validate original bytes and candidate hashes; returns a review-only restore map.

    Hashes detect accidental drift, not hostile replacement of the whole bundle.
    None means 'originally absent': rollback must remove that new destination.
    """
    directory = Path(directory)
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    require(manifest.get("schema") == 1, "Unknown bundle schema")
    restore = {}
    allowed = {ENV, DNS, AP, GAME, SCRIPT, USB_SCRIPT, GUARD, LINK, NM, UDEV, "/etc/gridopoly/server.env",
               "/usr/local/libexec/gridopoly-ap-cleanup", "/usr/local/libexec/gridopoly-ap-watchdog",
               "/etc/systemd/system/gridopoly-dnsmasq.service", "/etc/systemd/system/gridopoly-ap-watchdog.service"}
    require(set(manifest["files"]) == allowed, "Unexpected bundle destination set")
    for path, item in manifest["files"].items():
        for tree in ("original", "candidate"):
            file = directory / tree / path.lstrip("/")
            if item[tree] is not None:
                require(file.is_file() and not file.is_symlink() and sha(file.read_bytes()) == item[tree]["sha256"],
                        "Bundle integrity failure: " + tree + path)
        if item["candidate"] is not None:
            restore[path] = (directory / "original" / path.lstrip("/")).read_bytes() if item["original"] else None
    return restore


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--inspect", action="store_true", help="read wireless identities only")
    action.add_argument("--check", action="store_true", help="read-only target preflight")
    action.add_argument("--prepare", metavar="NEW_PRIVATE_DIRECTORY", help="preflight then write review bundle only")
    action.add_argument("--verify-bundle", metavar="DIRECTORY", help="verify offline bundle; never restore/install")
    parser.add_argument("--mac", help="actual USB permanent MAC, required for check/prepare")
    parser.add_argument("--interface", help="optional current name, must match MAC; no wlan1 assumption")
    parser.add_argument("--country", help="actual operating country, uppercase ISO alpha-2; no default")
    parser.add_argument("--channel", type=int, choices=(1, 6, 11), default=6)
    parser.add_argument("--review-derived-regdom", action="store_true",
                        help="explicitly review Linux 98/99 domains with a confirmed operating country; no regulatory changes")
    args = parser.parse_args(argv)
    try:
        if args.verify_bundle:
            verify_bundle(args.verify_bundle)
            print("Bundle verified; no files installed or restored.")
        elif args.inspect:
            print(json.dumps(Host().devices(), indent=2))
        elif args.check or args.prepare:
            require(args.mac and args.country, "--mac and --country are required")
            originals, generated, evidence = preflight(Host(), args.mac, args.country, args.channel,
                                                       args.interface, os.environ.get("SSH_CONNECTION", ""),
                                                       args.review_derived_regdom)
            if args.prepare:
                write_bundle(args.prepare, originals, generated, evidence)
            print("Preflight passed. " + ("Private review bundle written; " if args.prepare else "") +
                  "no live configuration changed; no client-capacity claim.")
        else:
            parser.print_help()
        return 0
    except (Refused, OSError, ValueError, subprocess.SubprocessError) as error:
        # Never echo command output or file contents: they may contain credentials.
        print("Refused: " + (str(error) if isinstance(error, Refused) else type(error).__name__), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
