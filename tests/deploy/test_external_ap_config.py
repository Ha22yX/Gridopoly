#!/usr/bin/env python3
"""Offline external-AP regression. Never contacts Pi or invokes host networking."""
from __future__ import annotations

import configparser
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "Server/RaspberryPi/deploy/external-ap/external_ap.py"
spec = importlib.util.spec_from_file_location("external_ap", SCRIPT)
ap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ap)
SSH = "10.0.0.234 52341 10.0.0.124 22"


def address(interface, ip, prefix=24):
    return {"ifname": interface, "addr_info": [{"family": "inet", "local": ip,
                                              "prefixlen": prefix, "scope": "global"}]}


class FakeHost(ap.Host):
    """Stateful nmcli/systemd model, with failures injected at mutation boundaries."""
    def __init__(self, directory):
        self.directory = Path(directory)
        self.commands = []
        self.mutations = []
        self.fail_at = None
        self.carrier = "1"
        self.route_device = "wlan0"
        self.extra_addresses = []
        self.routes = [{"dst": "default", "dev": "wlan0", "gateway": "10.0.0.1"},
                       {"dst": "10.42.0.0/24", "dev": "ap0"}]
        self.nm_state = "30 (disconnected)"
        self.eth_address = False
        self.ap_address = True
        self.cleanup_broken = False
        self.stuck_eth = False
        self.profile = False
        self.autoconnect = False
        self.timer = False
        self.active = {x: True for x in ap.OLD}
        self.active.update({"NetworkManager.service": True, "gridopoly.service": True, ap.UNIT: False})
        self.enabled = {x: True for x in ap.OLD}
        self.enabled[ap.UNIT] = False
        self.requires = ""
        self.health_ok = True
        self.udp_listen = "0.0.0.0:4242 10.42.0.1:53 0.0.0.0:67"
        self.path("/var/lib").mkdir(parents=True)
        for filename in ("/usr/sbin/dnsmasq", "/usr/bin/python3", "/usr/bin/systemd-run"):
            self.path(filename).parent.mkdir(parents=True, exist_ok=True)
            self.path(filename).touch()
        self.write("/etc/gridopoly/ap.env", "GRIDOPOLY_UPLINK_IFACE=wlan0\nGRIDOPOLY_AP_PARENT_IFACE=wlan0\n"
                   "GRIDOPOLY_AP_IFACE=ap0\nGRIDOPOLY_AP_ADDRESS=10.42.0.1/24\nSECRET=private\n")
        self.write("/etc/gridopoly/server.env", "\n".join(f"{k}={v}" for k, v in ap.EXPECTED_SERVER.items()))
        self.write("/etc/gridopoly/dnsmasq.conf", "interface=ap0\n")
        self.original = {p: self.read(p) for p in ("/etc/gridopoly/ap.env", "/etc/gridopoly/server.env",
                                                   "/etc/gridopoly/dnsmasq.conf")}

    def path(self, name):
        return self.directory / name.lstrip("/")

    def read(self, name):
        if name == "/sys/class/net/eth0/carrier":
            return self.carrier
        return super().read(name)

    def addresses(self):
        result = [address("wlan0", "10.0.0.124"), {"ifname": "eth0", "addr_info": []}]
        if self.eth_address:
            result[1] = address("eth0", "10.42.0.1")
        if self.ap_address:
            result.append(address("ap0", "10.42.0.1"))
        return result + self.extra_addresses

    def health(self):
        ap.require(self.health_ok, "Simulated health failure")

    def run(self, *args, check=True):
        self.commands.append(args)
        if args[0] == "ip":
            if "address" in args:
                return json.dumps(self.addresses())
            if "get" in args:
                return json.dumps([{"dev": self.route_device}])
            return json.dumps(self.routes)
        if args[0] == "ss":
            return self.udp_listen
        if args[:3] == ("nmcli", "-g", "GENERAL.STATE"):
            return self.nm_state
        if args[:3] == ("nmcli", "-g", "UUID"):
            return ap.UUID if self.profile else ""
        if args[:2] == ("systemctl", "is-active"):
            return "\n".join("active" if self.active.get(x, False) else "inactive" for x in args[2:])
        if args[:2] == ("systemctl", "is-enabled"):
            return "enabled" if self.enabled[args[2]] else "disabled"
        if args[:2] == ("systemctl", "show"):
            if "LoadState" in args:
                return "loaded" if self.path(ap.UNIT_PATH).exists() else "not-found"
            return self.requires if "Requires" in args else ""
        if args[:2] == ("systemctl", "cat"):
            return "[Unit]\nDescription=Original " + args[2] + "\n"
        if args[0] in ("dnsmasq", "systemd-analyze"):
            return ""
        self.mutations.append(args)
        if self.fail_at == len(self.mutations):
            raise ap.Refused("Injected mutation failure")
        if args[0] == "systemd-run":
            self.timer = True
        elif args[:3] == ("systemctl", "stop", ap.TIMER + ".timer"):
            self.timer = False
        elif args[:2] in (("systemctl", "start"), ("systemctl", "stop")):
            for unit in args[2:]:
                self.active[unit] = args[1] == "start"
                if unit == "gridopoly-ap.service":
                    self.ap_address = self.active[unit] or self.cleanup_broken
        elif args[:2] in (("systemctl", "enable"), ("systemctl", "disable")):
            for unit in args[2:]:
                self.enabled[unit] = args[1] == "enable"
        elif args[:2] == ("systemctl", "daemon-reload"):
            pass
        elif args[:3] == ("nmcli", "connection", "load"):
            self.profile = True
        elif args[:3] == ("nmcli", "connection", "reload"):
            pass
        elif args[:3] == ("nmcli", "connection", "modify"):
            self.autoconnect = args[-1] == "yes"
        elif args[:3] == ("nmcli", "connection", "delete"):
            self.profile = False
        elif "disconnect" in args:
            if self.stuck_eth:
                raise ap.Refused("Simulated eth0 disconnect failure")
            self.eth_address = False
            self.nm_state = "30 (disconnected)"
        elif "up" in args and args[0] == "nmcli":
            self.eth_address = True
            self.nm_state = "100 (connected)"
        else:
            raise AssertionError("Unexpected command: " + repr(args))
        return ""


class ExternalAPTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.host = FakeHost(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def trial(self):
        with contextlib.redirect_stdout(io.StringIO()):
            ap.apply(self.host, SSH, True)

    def assert_original(self):
        self.assertTrue(all(self.host.active[x] and self.host.enabled[x] for x in ap.OLD))
        self.assertFalse(self.host.eth_address)
        self.assertTrue(self.host.ap_address)
        self.assertTrue(self.host.active["gridopoly.service"])
        self.assertTrue(self.host.active["NetworkManager.service"])
        for path, content in self.host.original.items():
            self.assertEqual(self.host.read(path), content)
        for command in self.host.mutations:
            self.assertFalse("wlan0" in command)
            self.assertFalse("gridopoly.service" in command)

    def test_config_contract_and_dhcp_capacity(self):
        generated = ap.configs()
        config = configparser.ConfigParser()
        config.read_string(generated[ap.PROFILE])
        self.assertEqual(config["ipv4"]["address1"], "10.42.0.1/24")
        self.assertEqual(config["ipv4"]["never-default"], "true")
        self.assertNotIn("gateway", config["ipv4"])
        self.assertEqual(config["connection"]["autoconnect"], "false")
        self.assertEqual(config["connection"]["interface-name"], "eth0")
        dhcp = dict(line.split("=", 1) for line in generated[ap.CONF].splitlines()
                    if "=" in line and not line.startswith("#"))
        self.assertEqual(dhcp["interface"], "eth0")
        self.assertEqual(dhcp["dhcp-lease-max"], "181")
        self.assertEqual(dhcp["dhcp-range"], "10.42.0.20,10.42.0.200,255.255.255.0,12h")
        self.assertIn("dhcp-option=3,10.42.0.1\n", generated[ap.CONF])
        self.assertEqual(dhcp["dhcp-leasefile"], "/var/lib/misc/dnsmasq.leases")
        self.assertEqual(dhcp["except-interface"], "lo")
        self.assertNotIn("ap0", generated[ap.CONF])
        self.assertNotIn("Requires=", generated[ap.UNIT_PATH])
        self.assertNotIn("gridopoly-ap", generated[ap.UNIT_PATH])
        self.assertIn("Before=gridopoly.service", generated[ap.UNIT_PATH])

    def test_default_and_dry_run_have_no_writes_or_commands(self):
        before = list(self.host.directory.rglob("*"))
        with patch.object(ap.Host, "run", side_effect=AssertionError("system command")), \
             patch.object(ap.Host, "write", side_effect=AssertionError("system write")), \
             contextlib.redirect_stdout(io.StringIO()):
            ap.main([])
            ap.main(["--dry-run"])
        self.assertEqual(list(self.host.directory.rglob("*")), before)

    def test_generated_directory_is_new_and_contains_review_files_only(self):
        directory = self.host.directory / "review space $(touch injection)"
        with contextlib.redirect_stdout(io.StringIO()):
            ap.main(["--generate", str(directory)])
        self.assertEqual({p.name for p in directory.iterdir()}, {Path(p).name for p in ap.configs()})
        with self.assertRaises(ap.Refused):
            ap.main(["--generate", str(directory)])
        self.assertFalse((self.host.directory / "injection").exists())

    def test_name_and_argument_injection_rejected(self):
        for interface in ("eth0\n[ipv4]\nmethod=shared", "eth0;reboot", "$(reboot)", "--help", "../eth0", "wlan0"):
            with self.subTest(interface=interface), self.assertRaises(ap.Refused):
                ap.configs(interface)
        with self.assertRaises(ValueError):
            ap.preflight(self.host, "--help 1 10.0.0.124 22")
        self.assertEqual(self.host.mutations, [])

    def test_preflight_valid_is_read_only(self):
        ap.preflight(self.host, SSH)
        self.assertEqual(self.host.mutations, [])
        self.assertFalse(self.host.path(ap.STATE).exists())

    def test_management_on_eth0_or_ap_subnet_is_refused(self):
        for ssh, route in ((SSH, "eth0"), (SSH, "ap0"), ("10.42.0.20 1234 10.42.0.1 22", "ap0"), ("", "wlan0")):
            with self.subTest(ssh=ssh, route=route):
                self.host.route_device = route
                with self.assertRaises(ap.Refused):
                    ap.preflight(self.host, ssh)
        self.assertEqual(self.host.mutations, [])

    def test_unplugged_or_busy_eth0_is_refused(self):
        for carrier, nm_state in (("0", "30"), ("1", "100 (connected)"), ("1", "10 (unmanaged)"), ("1", "")):
            with self.subTest(carrier=carrier, nm_state=nm_state), self.assertRaises(ap.Refused):
                self.host.carrier, self.host.nm_state = carrier, nm_state
                ap.preflight(self.host, SSH)
        self.assertEqual(self.host.mutations, [])

    def test_conflicting_address_and_routes_refused(self):
        self.host.extra_addresses = [address("br0", "10.42.0.99")]
        with self.assertRaises(ap.Refused):
            ap.preflight(self.host, SSH)
        self.host.extra_addresses = []
        self.host.routes.append({"dst": "10.42.0.0/16", "dev": "tun0"})
        with self.assertRaises(ap.Refused):
            ap.preflight(self.host, SSH)
        self.assertEqual(self.host.mutations, [])

    def test_existing_files_and_service_dependencies_refused(self):
        self.host.requires = "gridopoly-ap.service"
        with self.assertRaises(ap.Refused):
            ap.preflight(self.host, SSH)
        self.host.requires = ""
        self.host.write(ap.PROFILE, "do not overwrite")
        with self.assertRaises(ap.Refused):
            ap.preflight(self.host, SSH)
        self.assertEqual(self.host.read(ap.PROFILE), "do not overwrite")
        self.assertEqual(self.host.mutations, [])

    def test_apply_needs_physical_isolation_acknowledgement(self):
        with self.assertRaises(ap.Refused):
            ap.apply(self.host, SSH, False)
        self.assertEqual(self.host.commands, [])

    def test_trial_is_nonpersistent_and_timer_precedes_disruption(self):
        self.trial()
        self.assertTrue(self.host.timer)
        self.assertTrue(self.host.profile)
        self.assertFalse(self.host.autoconnect)
        self.assertFalse(self.host.enabled[ap.UNIT])
        self.assertTrue(all(self.host.enabled[x] for x in ap.OLD))
        self.assertFalse(any(self.host.active[x] for x in ap.OLD))
        self.assertEqual(self.host.mutations[0][0], "systemd-run")
        self.assertEqual(ap.load_state(self.host)["phase"], "pending")
        self.assertTrue(self.host.eth_address)
        self.assertFalse(self.host.ap_address)
        self.assertEqual(self.host.read(ap.STATE + "/backup/ap.env"), self.host.original["/etc/gridopoly/ap.env"])

    def test_timer_rollback_restores_original_services_and_retains_backup(self):
        self.trial()
        ap.rollback(self.host, expired=True)
        self.assert_original()
        self.assertFalse(self.host.profile)
        self.assertFalse(self.host.timer)
        self.assertEqual(ap.load_state(self.host)["phase"], "rolled-back")
        self.assertTrue(self.host.path(ap.STATE + "/backup/ap.env").exists())
        for target in ap.configs():
            self.assertFalse(self.host.path(target).exists())
        before = len(self.host.mutations)
        ap.rollback(self.host)
        self.assertEqual(len(self.host.mutations), before)

    def test_successful_commit_and_later_explicit_rollback(self):
        self.trial()
        with contextlib.redirect_stdout(io.StringIO()):
            ap.commit(self.host, SSH, True)
        self.assertTrue(self.host.autoconnect)
        self.assertTrue(self.host.enabled[ap.UNIT])
        self.assertFalse(any(self.host.enabled[x] for x in ap.OLD))
        self.assertFalse(self.host.timer)
        self.assertEqual(ap.load_state(self.host)["phase"], "committed")
        before = len(self.host.mutations)
        ap.rollback(self.host, expired=True)
        self.assertEqual(len(self.host.mutations), before)
        ap.rollback(self.host)
        self.assert_original()

    def test_unverified_or_expired_trial_cannot_commit(self):
        self.trial()
        with self.assertRaises(ap.Refused):
            ap.commit(self.host, SSH, False)
        with patch.object(ap.time, "time", return_value=time.time() + 301), self.assertRaises(ap.Refused):
            ap.commit(self.host, SSH, True)
        self.assertFalse(self.host.autoconnect)
        self.assertTrue(self.host.timer)

    def test_failure_at_each_apply_mutation_restores_access(self):
        self.trial()
        count = len(self.host.mutations)
        for index in range(1, count + 1):
            with self.subTest(index=index), tempfile.TemporaryDirectory() as directory:
                self.host = FakeHost(directory)
                self.host.fail_at = index
                with self.assertRaises(ap.Refused):
                    self.trial()
                self.assert_original()

    def test_failure_during_each_commit_mutation_restores_access(self):
        for offset in range(1, 5):
            with self.subTest(offset=offset), tempfile.TemporaryDirectory() as directory:
                self.host = FakeHost(directory)
                self.trial()
                self.host.fail_at = len(self.host.mutations) + offset
                with self.assertRaises(ap.Refused):
                    ap.commit(self.host, SSH, True)
                self.assert_original()

    def test_failed_ap_cleanup_does_not_activate_eth0(self):
        self.host.cleanup_broken = True
        with self.assertRaises(ap.Refused):
            self.trial()
        self.assert_original()
        self.assertFalse(any("up" in c and c[0] == "nmcli" for c in self.host.commands))

    def test_missing_dhcp_listener_rolls_back(self):
        self.host.udp_listen = "0.0.0.0:4242 10.42.0.1:53"
        with self.assertRaises(ap.Refused):
            self.trial()
        self.assert_original()

    def test_failed_disconnect_refuses_duplicate_ip_and_preserves_management(self):
        self.trial()
        self.host.stuck_eth = True
        with self.assertRaises(ap.Refused):
            ap.rollback(self.host)
        self.assertFalse(self.host.ap_address)
        self.assertTrue(self.host.active["NetworkManager.service"])
        self.assertTrue(self.host.active["gridopoly.service"])
        self.assertEqual(ap.load_state(self.host)["phase"], "rollback-failed")

    def test_shell_wrapper_syntax(self):
        candidates = [Path("C:/Program Files/Git/bin/bash.exe")]
        if os.name != "nt" and shutil.which("sh"):
            candidates.append(Path(shutil.which("sh")))
        shell = next((p for p in candidates if p.is_file()), None)
        if shell is None:
            self.skipTest("No local POSIX shell; wrapper syntax not validated")
        result = subprocess.run([str(shell), "-n", str(SCRIPT.with_name("external-ap"))],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
