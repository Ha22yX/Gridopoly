#!/usr/bin/env python3
"""Offline USB AP preparation tests: no Pi, network changes, or service restarts."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
DEPLOY = ROOT / "Server/RaspberryPi/deploy"
spec = importlib.util.spec_from_file_location("usb_ap", DEPLOY / "usb-ap/usb_ap.py")
ap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ap)
MAC = "00:11:22:33:44:66"
SSH = "10.0.0.234 55555 10.0.0.124 22"
RADIO = "Supported interface modes:\n\t * managed\n\t * AP\nBand 1:\n\t * 2437 MHz [6] (20.0 dBm)\n"


def device(name="wlx001122334466", mac=MAC, phy="phy1", driver="mt76x2u", usb="0e8d:7612"):
    return dict(name=name, mac=mac, permanent_mac=mac, phy=phy, driver=driver, usb_id=usb, master=False)


def address(name, ip, prefix=24):
    return dict(ifname=name, addr_info=[dict(family="inet", local=ip, prefixlen=prefix, scope="global")])


class FakeHost(ap.Host):
    def __init__(self):
        self.inventory = [device("wlan0", "00:11:22:33:44:88", "phy0", "brcmfmac", None), device()]
        self.commands = []
        self.addresses = [address("wlan0", "10.0.0.124"), address("ap0", "10.42.0.1")]
        self.routes = [{"dst": "default", "dev": "wlan0"}, {"dst": "10.42.0.0/24", "dev": "ap0"}]
        self.nm = "30 (disconnected)"
        self.firmware_ok = True
        self.dropins = ""
        self.files = {}
        for path, name in ((ap.ENV, "ap.env.example"), (ap.DNS, "dnsmasq.conf"),
                           ("/usr/local/libexec/gridopoly-ap-cleanup", "gridopoly-ap-cleanup"),
                           ("/usr/local/libexec/gridopoly-ap-watchdog", "gridopoly-ap-watchdog")):
            self.files[path] = (DEPLOY / name).read_bytes()
        self.files[ap.SCRIPT] = (ROOT / "tests/deploy/fixtures/gridopoly-ap-deployed-10-0.sh").read_bytes()
        for unit in ap.UNITS:
            self.files["/etc/systemd/system/" + unit] = (DEPLOY / unit).read_bytes()
        self.files["/etc/gridopoly/server.env"] = "\n".join(f"{k}={v}" for k, v in ap.EXPECTED_SERVER.items()).encode()
        self.files["/run/gridopoly/hostapd.conf"] = b"interface=ap0\nssid=gridopoly\nap_max_inactivity=10\ndisassoc_low_ack=0\n"

    def devices(self):
        return self.inventory

    def read(self, path):
        return self.files[path]

    def capture(self, path):
        return dict(data=self.files[path], mode=0o640, uid=0, gid=0) if path in self.files else None

    def firmware_present(self, name):
        return self.firmware_ok

    def run(self, *args, **kwargs):
        self.commands.append(args)
        if args == ("iw", "phy", "phy1", "info"):
            return RADIO
        if args == ("iw", "reg", "get"):
            return "global\ncountry US: DFS-FCC\n"
        if args == ("nmcli", "-g", "GENERAL.STATE", "device", "show", self.inventory[-1]["name"]):
            return self.nm
        if args == ("ip", "-j", "-4", "route", "get", "10.0.0.234"):
            return '[{"dev":"wlan0"}]'
        if args == ("ip", "-j", "address", "show"):
            return json.dumps(self.addresses)
        if args == ("ip", "-j", "-4", "route", "show", "table", "all"):
            return json.dumps(self.routes)
        if args == ("modinfo", "-F", "firmware", "mt76x2u"):
            return "mediatek/mt7662u.bin\nmediatek/mt7662u_rom_patch.bin\n"
        if args[0] == "systemctl" and args[-1] in ap.UNITS:
            if args[1] == "is-active":
                return "active"
            if args[1] == "is-enabled":
                return "enabled"
            if args[1:4] == ("show", "--value", "--property=DropInPaths"):
                return self.dropins
            if args[1:4] == ("show", "--value", "--property=FragmentPath"):
                return "/etc/systemd/system/" + args[-1]
        raise AssertionError("Unapproved command: " + repr(args))


class USBPreparationTests(unittest.TestCase):
    def setUp(self):
        self.host = FakeHost()

    def prepare(self):
        return ap.preflight(self.host, MAC, "US", 6, ssh=SSH)

    def test_default_is_help_without_host_access(self):
        with patch.object(ap, "Host", side_effect=AssertionError("Host touched")), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(ap.main([]), 0)

    def test_interface_comes_from_identity_not_wlan1(self):
        self.assertEqual(ap.select_device(self.host.inventory, MAC)["name"], "wlx001122334466")
        with self.assertRaises(ap.Refused):
            ap.select_device(self.host.inventory, MAC, "wlan1")

    def test_missing_or_ambiguous_hardware_refused(self):
        for inventory in ([], self.host.inventory + [device("wlan7")]):
            with self.subTest(inventory=inventory), self.assertRaises(ap.Refused):
                ap.select_device(inventory, MAC)

    def test_protected_wrong_or_shared_radio_refused(self):
        for key, value in (("name", "wlan0"), ("driver", "brcmfmac"), ("usb_id", "1234:5678"),
                           ("phy", "phy0"), ("master", True), ("mac", "02:11:22:33:44:66")):
            inventory = copy.deepcopy(self.host.inventory)
            inventory[-1][key] = value
            with self.subTest(key=key), self.assertRaises(ap.Refused):
                ap.select_device(inventory, MAC)
        self.host.inventory.append(device("p2p0", "00:11:22:33:44:77"))
        with self.assertRaises(ap.Refused):
            ap.select_device(self.host.inventory, MAC)

    def test_address_and_route_conflicts(self):
        for extra in (address("eth0", "10.42.0.1"), address("wlan0", "10.42.1.5", 16),
                      address("wlx001122334466", "192.168.2.1")):
            self.host.addresses.append(extra)
            with self.subTest(extra=extra), self.assertRaises(ap.Refused):
                self.prepare()
            self.host.addresses.pop()
        self.host.routes.append({"dst": "10.42.0.0/24", "dev": "eth0"})
        with self.assertRaises(ap.Refused):
            self.prepare()

    def test_missing_old_address_and_management_refused(self):
        self.host.addresses.pop()
        with self.assertRaises(ap.Refused):
            self.prepare()
        with self.assertRaises(ap.Refused):
            ap.preflight(FakeHost(), MAC, "US", 6, ssh="")

    def test_channel_country_and_ap_capability(self):
        for info, reg, country in ((RADIO.replace(" * AP", " * monitor"), "country US:", "US"),
                                   (RADIO + "", "country CN:", "US"),
                                   (RADIO.replace("20.0 dBm", "no IR"), "country US:", "US"),
                                   (RADIO, "country US:\nphy#1\ncountry CN:", "US")):
            with self.subTest(info=info, reg=reg), self.assertRaises(ap.Refused):
                ap.validate_radio(info, reg, country, 6)

    def test_missing_firmware_and_busy_device_refused(self):
        self.host.firmware_ok = False
        with self.assertRaises(ap.Refused):
            self.prepare()
        self.host.firmware_ok = True
        self.host.nm = "100 (connected)"
        with self.assertRaises(ap.Refused):
            self.prepare()

    def test_dropin_unknown_script_existing_destination_refused(self):
        self.host.dropins = "/etc/systemd/system/service.d/override.conf"
        with self.assertRaises(ap.Refused):
            self.prepare()
        self.host.dropins = ""
        self.host.files[ap.SCRIPT] += b"\n# unreviewed\n"
        with self.assertRaises(ap.Refused):
            self.prepare()
        self.host = FakeHost()
        self.host.files[ap.LINK] = b"already present"
        with self.assertRaises(ap.Refused):
            self.prepare()

    def test_generated_contract_and_preserved_credentials(self):
        original_files = copy.deepcopy(self.host.files)
        originals, output, _ = self.prepare()
        env = ap.parse_env(output[ap.ENV])
        self.assertEqual(env["GRIDOPOLY_AP_PASSPHRASE"], "replace-locally")
        self.assertEqual(env["GRIDOPOLY_AP_MAX_INACTIVITY_SECONDS"], "10")
        self.assertEqual(env["GRIDOPOLY_AP_DISASSOC_LOW_ACK"], "0")
        self.assertEqual(env["GRIDOPOLY_AP_IFACE"], env["GRIDOPOLY_AP_PARENT_IFACE"])
        self.assertIn(b"interface=gpusb0", output[ap.DNS].splitlines())
        self.assertIn(b"dhcp-option=3,10.42.0.1", output[ap.DNS])
        self.assertIn(b"country_code=US", output[ap.USB_SCRIPT])
        self.assertIn(b"PermanentMACAddress=" + MAC.encode(), output[ap.LINK])
        self.assertEqual(self.host.files, original_files)
        self.assertEqual(originals[ap.SCRIPT]["data"], original_files[ap.SCRIPT])

    def test_exact_deployed_fixture_and_crlf_source_boundary(self):
        source = self.host.files[ap.SCRIPT]
        self.assertEqual(ap.sha(source), "0538095d81181708fd67f7bfb090205b0c0fd64915a1674e17b8359c09592930")
        self.assertNotIn(b"\r", source)
        self.prepare()
        self.host.files[ap.SCRIPT] = source.replace(b"\n", b"\r\n")
        with self.assertRaises(ap.Refused):
            self.prepare()

    def test_full_unit_removes_both_online_waits_keeps_game_command(self):
        _, output, _ = self.prepare()
        for path in (ap.AP, ap.GAME):
            self.assertNotIn(b"network-online.target", output[path])
            self.assertNotIn(b"NetworkManager-wait-online.service", output[path])
        self.assertIn(b"ExecStart=/usr/local/bin/gridopoly_server", output[ap.GAME])
        self.assertIn(b"gridopoly-dnsmasq.service", output[ap.GAME])
        self.assertIn(b"BindsTo=sys-subsystem-net-devices-gpusb0.device", output[ap.AP])
        self.assertIn(b"ExecStopPost=" + ap.GUARD.encode() + b" cleanup", output[ap.AP])

    def test_duplicate_dhcp_gateway_refused(self):
        self.host.files[ap.DNS] += b"\ndhcp-option=3,0.0.0.0\n"
        with self.assertRaises(ap.Refused):
            self.prepare()

    def test_roundtrip_original_bytes_modes_and_original_absence(self):
        originals, output, evidence = self.prepare()
        # Preserve byte-level originals, including CRLF and unknown comments.
        originals[ap.ENV]["data"] += b"# private backup\r\n"
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "new"
            manifest = ap.write_bundle(target, originals, output, evidence)
            restore = ap.verify_bundle(target)
            for path in (ap.ENV, ap.DNS, ap.AP, ap.GAME):
                self.assertEqual(restore[path], originals[path]["data"])
                self.assertEqual(manifest["files"][path]["original"]["mode"], 0o640)
            for path in (ap.USB_SCRIPT, ap.GUARD, ap.LINK, ap.NM):
                self.assertIsNone(restore[path])
            with self.assertRaises(ap.Refused):
                ap.write_bundle(target, originals, output, evidence)

    def test_corrupt_backup_or_candidate_is_rejected(self):
        originals, output, evidence = self.prepare()
        for tree in ("original", "candidate"):
            with self.subTest(tree=tree), tempfile.TemporaryDirectory() as temp:
                target = Path(temp) / "new"
                ap.write_bundle(target, originals, output, evidence)
                (target / tree / ap.ENV.lstrip("/")).write_bytes(b"corrupted")
                with self.assertRaises(ap.Refused):
                    ap.verify_bundle(target)

    def test_no_output_directory_without_hardware(self):
        self.host.inventory = []
        with tempfile.TemporaryDirectory() as temp, patch.object(ap, "Host", return_value=self.host):
            directory = Path(temp) / "not-created"
            with contextlib.redirect_stderr(io.StringIO()):
                result = ap.main(["--prepare", str(directory), "--mac", MAC, "--country", "US"])
            self.assertEqual(result, 2)
            self.assertFalse(directory.exists())


class GuardTests(unittest.TestCase):
    def setUp(self):
        self.calls = []
        self.permanent = MAC
        self.present = True
        self.addresses = [address("gpusb0", "10.42.0.1"), address("wlan0", "10.0.0.124")]
        owner = self

        class FakePath:
            def __init__(self, *args):
                pass

            def __truediv__(self, key):
                return self

            def exists(self):
                return owner.present

            def read_text(self):
                return MAC

            def resolve(self):
                return types.SimpleNamespace(name="mt76x2u")

        self.module = {"__name__": "guard_test"}
        exec(compile((DEPLOY / "usb-ap/guard.py.in").read_text().replace("@MAC@", MAC), "guard.py.in", "exec"), self.module)
        self.module["Path"] = FakePath
        self.module["run"] = self.run_command

    def run_command(self, *args):
        self.calls.append(args)
        if args == ("/usr/sbin/ethtool", "-P", "gpusb0"):
            return "Permanent address: " + self.permanent
        if args == ("/usr/sbin/ip", "-j", "address", "show"):
            return json.dumps(self.addresses)
        if args == ("/usr/sbin/ip", "address", "del", "10.42.0.1/24", "dev", "gpusb0"):
            return ""
        raise AssertionError(args)

    def test_cleanup_only_removes_matched_usb_game_address(self):
        self.module["main"]("cleanup")
        self.assertEqual(self.calls[-1], ("/usr/sbin/ip", "address", "del", "10.42.0.1/24", "dev", "gpusb0"))

    def test_wrong_identity_refuses_cleanup(self):
        self.permanent = "00:00:00:00:00:02"
        with self.assertRaises(RuntimeError):
            self.module["main"]("cleanup")
        self.assertFalse(any("del" in call for call in self.calls))

    def test_unplugged_cleanup_is_noop(self):
        self.present = False
        self.module["main"]("cleanup")
        self.assertEqual(self.calls, [])

    def test_start_conflict_refuses_without_mutation(self):
        self.addresses.append(address("ap0", "10.42.0.1"))
        with self.assertRaises(RuntimeError):
            self.module["main"]("check")
        self.assertFalse(any("del" in call for call in self.calls))


if __name__ == "__main__":
    unittest.main()
