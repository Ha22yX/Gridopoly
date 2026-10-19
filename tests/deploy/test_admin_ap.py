"""Management AP configuration and routing isolation regression checks.

Run on Linux; root privileges and actual hardware are not needed.
"""
from pathlib import Path
import importlib.util
import json
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('admin_ap', ROOT / 'Server/RaspberryPi/deploy/admin-ap/admin_ap.py')
ap = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ap)


class AdminApTests(unittest.TestCase):
    def config(self, **changes):
        data = dict(ssid='gridopoly-admin', passphrase='test-only-1234', country='US', fallback_channel=6, max_clients=6)
        data.update(changes)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'config.json';path.write_text(json.dumps(data))
            return ap.load_config(path)

    def test_validation_rejects_config_injection_and_unknown_keys(self):
        for change in [dict(ssid='gridopoly'), dict(ssid='bad\ninterface=wlan0'), dict(passphrase='bad\nwpa=0'),
                       dict(passphrase='replace-locally'), dict(passphrase='short'),
                       dict(fallback_channel=12), dict(fallback_channel=True), dict(max_clients=7),
                       dict(country='CN'), dict(iface='gpusb0')]:
            with self.subTest(change=change), self.assertRaises(ValueError):self.config(**change)

    def test_separate_ssid_and_security(self):
        cfg=self.config(ssid='my # admin')
        text=ap.hostapd_config(cfg,2)
        self.assertIn('ssid2='+b'my # admin'.hex(),text)
        self.assertIn('channel=2\n',text)
        self.assertIn('wpa=2\n',text)
        self.assertIn('interface=gpadmin0\n',text)
        self.assertNotIn('interface=wlan0\n',text)
        self.assertNotIn('gpusb0',text)

    def test_only_associated_station_selects_channel(self):
        self.assertIsNone(ap.station_channel('Not connected.\n'))
        self.assertIsNone(ap.station_channel('scan results\nfreq: 2417\n'))
        self.assertEqual(ap.station_channel('Connected to 00:11:22:33:44:55\n\tfreq: 2417.0\n'),2)
        for freq in [5180,2484,2418]:
            with self.assertRaises(ValueError):ap.station_channel(f'Connected to AP\n freq: {freq}\n')

    def test_home_absent_keeps_ap_and_cold_boot_uses_last_channel(self):
        s=ap.Supervisor(self.config())
        with tempfile.TemporaryDirectory() as tmp, patch.object(ap,'STATE',Path(tmp)), patch.object(ap,'command',return_value=subprocess.CompletedProcess([],0,'Not connected.\n','')):
            self.assertEqual(s.preferred_channel(),6)
            (Path(tmp)/'last-channel').write_text('2')
            self.assertEqual(s.preferred_channel(),2)
            s.channel=11
            self.assertEqual(s.preferred_channel(),11)

    def test_home_channel_change_takes_precedence(self):
        s=ap.Supervisor(self.config());s.channel=6
        with patch.object(ap,'command',return_value=subprocess.CompletedProcess([],0,'Connected to AP\nfreq: 2412\n','')):
            self.assertEqual(s.preferred_channel(),1)

    def test_address_is_only_assigned_after_vrf_attachment(self):
        calls=[]
        def run(*args,**kwargs):
            calls.append(args)
            return subprocess.CompletedProcess(args,0,'0\n' if args[0]=='sysctl' else '[]','')
        owned={'ifalias':ap.ALIAS,'linkinfo':{'info_data':{'table':int(ap.TABLE)}}}
        with tempfile.TemporaryDirectory() as tmp, patch.object(ap,'RUN',Path(tmp)), patch.object(ap,'command',side_effect=run), patch.object(ap,'link_info',return_value=owned), patch.object(Path,'exists',return_value=True), patch.object(Path,'resolve',return_value=Path('/sys/phy0')), patch.object(ap,'keep_radio_awake'):
            ap.setup_network()
        attach=('ip','link','set',ap.IFACE,'master',ap.VRF)
        address=('ip','address','replace',ap.ADDRESS,'dev',ap.IFACE)
        self.assertLess(calls.index(attach),calls.index(address))
        self.assertFalse(any('gpusb0' in c for c in calls))
        self.assertFalse(any('wlan0' in c for c in calls))
        self.assertIn(('ip','route','replace','table',ap.TABLE,'unreachable','default','metric','4278198272'),calls)

    def test_foreign_interface_is_not_adopted_or_deleted(self):
        with patch.object(ap,'link_info',return_value={'ifalias':'someone-else'}), patch.object(ap,'command') as cmd:
            with self.assertRaises(RuntimeError):ap.cleanup()
            cmd.assert_not_called()

    def test_cleanup_only_our_interfaces_and_restores_sysctl(self):
        calls=[]
        def run(*args,**kwargs):
            calls.append(args);return subprocess.CompletedProcess(args,0,'1\n','')
        with tempfile.TemporaryDirectory() as tmp, patch.object(ap,'RUN',Path(tmp)), patch.object(ap,'assert_owned',return_value=True), patch.object(ap,'command',side_effect=run):
            (Path(tmp)/'previous-tcp-accept').write_text('0')
            ap.cleanup()
            self.assertFalse((Path(tmp)/'previous-tcp-accept').exists())
        self.assertEqual(calls[0],('iw','dev','gpadmin0','del'))
        self.assertEqual(calls[1],('ip','route','del','table','10430','unreachable','default','metric','4278198272'))
        self.assertEqual(calls[2],('ip','link','del','dev','gpadmin'))
        self.assertIn(('sysctl','-w','net.ipv4.tcp_l3mdev_accept=0'),calls)

    def test_private_config_and_local_dhcp(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'secret';ap.write_private(p,'secret')
            self.assertEqual(p.stat().st_mode & 0o777,0o600)
        text=ap.dnsmasq_config()
        self.assertIn('except-interface=lo',text)
        self.assertIn('bind-interfaces',text)
        self.assertIn('no-resolv',text)
        self.assertIn('10.0.0.20,10.0.0.80',text)

    def test_radio_stays_awake_across_nm_reconnect_and_restores_on_stop(self):
        state={'power':'on'};calls=[]
        def run(*args,**kwargs):
            calls.append(args)
            if args[3]=='set':state['power']=args[-1]
            return subprocess.CompletedProcess(args,0,'Power save: '+state['power']+'\n','')
        with tempfile.TemporaryDirectory() as tmp, patch.object(ap,'RUN',Path(tmp)), patch.object(ap,'command',side_effect=run), patch.object(ap,'assert_owned',return_value=None):
            ap.keep_radio_awake()
            self.assertEqual(state['power'],'off')
            self.assertEqual((Path(tmp)/'previous-power-save').read_text(),'on')
            ap.keep_radio_awake()
            self.assertEqual(len([c for c in calls if c[3]=='set']),1)
            state['power']='on'  # A later station association reapplies NM settings.
            ap.keep_radio_awake()
            self.assertEqual(state['power'],'off')
            ap.cleanup()
            self.assertEqual(state['power'],'on')
            self.assertFalse((Path(tmp)/'previous-power-save').exists())


if __name__=='__main__':unittest.main()
