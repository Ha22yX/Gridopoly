import csv
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import batch_flash as batch
from flash_layout import check_existing_layout, parse_partitions, validate_images
from flash_worker import EspToolApi, process_job
from prepare_release import prepare


def partition_table(app_size=0x640000, nvs_offset=0x9000):
    rows = [(1, 2, nvs_offset, 0x5000, b'nvs'),
            (1, 0, 0xe000, 0x2000, b'otadata'),
            (0, 16, 0x10000, app_size, b'app0'),
            (0, 17, 0x10000 + app_size, app_size, b'app1'),
            (1, 130, 0x10000 + 2 * app_size, 0xff0000 - 0x10000 - 2 * app_size, b'spiffs'),
            (1, 3, 0xff0000, 0x10000, b'coredump')]
    entries = b''.join(struct.pack('<HBBII16sI', 0x50aa, kind, subtype, offset,
                                  size, name, 0) for kind, subtype, offset, size, name in rows)
    data = entries + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(entries).digest()
    return data.ljust(3072, b'\xff')


class FakeEsp:
    GPIO_STRAP_REG = 0x60004038
    RTC_CNTL_OPTION1_REG = 0x60008000
    RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK = 1 << 5
    secure_download_mode = False

    def __init__(self, api, handle=None):
        self.api = api
        self._port = object() if handle is None else handle
        self.closed = False

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.closed = True
        self.api.calls.append(('close', self._port))

    def connect(self, reset, attempts):
        self.api.calls.append(('connect', self._port))
        if self.api.on_connect:
            self.api.on_connect()

    def read_mac(self, kind):
        assert kind == 'BASE_MAC'
        self.api.calls.append(('mac', self._port))
        return tuple(bytes.fromhex(self.api.mac.replace(':', '')))

    def change_baud(self, baud):
        assert baud == 460800
        self.api.calls.append(('baud', self._port))

    def uses_usb_jtag_serial(self):
        return self.api.usb_jtag

    def read_reg(self, address):
        self.api.calls.append(('read_reg', self._port))
        if address == self.GPIO_STRAP_REG + 4:
            return 1 if self.api.gpio0_high else 0
        assert address == self.RTC_CNTL_OPTION1_REG
        return self.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK if self.api.force_download else 0

    def write_reg(self, address, value, mask):
        assert address == self.RTC_CNTL_OPTION1_REG and value == 0
        assert mask == self.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK
        self.api.calls.append(('clear_force_download', self._port))
        if not self.api.fail_clear_force:
            self.api.force_download = False


class FakeApi:
    version = '5.3.1'

    def __init__(self, mac='02:12:34:56:78:90', old=None):
        self.mac = mac
        self.old = b'\xff' * 4096 if old is None else old
        self.calls = []
        self.on_connect = None
        self.change_handle = False
        self.fail_write = False
        self.fail_verify = False
        self.fail_reset = False
        self.capacity = '16MB'
        self.written = None
        self.usb_jtag = True
        self.gpio0_high = True
        self.force_download = True
        self.fail_clear_force = False
        self.reset_modes = []

    def loader(self, port, baud):
        self.rom = FakeEsp(self)
        self.calls.append(('open', self.rom._port))
        return self.rom

    def run_stub(self, rom):
        self.stub = FakeEsp(self, None if self.change_handle else rom._port)
        return self.stub

    def attach_flash(self, esp):
        self.calls.append(('attach', esp._port))

    def detect_flash_size(self, esp):
        return self.capacity

    def read_flash(self, esp, offset, size, **kwargs):
        assert (offset, size) == (0x8000, 4096)
        self.calls.append(('read', esp._port))
        return self.old

    def write_flash(self, esp, images, **kwargs):
        self.calls.append(('write', esp._port))
        assert esp.WRITE_FLASH_ATTEMPTS == 1  # esptool must never reconnect mid-write.
        assert kwargs['erase_all'] is False and kwargs['force'] is False
        assert all(isinstance(data, bytes) for _, data in images)
        if self.fail_write:
            raise OSError('device unplugged; COM may now belong to another board')
        self.written = images

    def verify_flash(self, esp, images, **kwargs):
        self.calls.append(('verify', esp._port))
        assert images is self.written
        if self.fail_verify:
            raise RuntimeError('verification mismatch')

    def reset_chip(self, esp, mode):
        self.calls.append(('reset', esp._port))
        self.reset_modes.append(mode)
        if self.fail_reset:
            raise RuntimeError('reset failed after verification')


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.candidates = []
        for name in sorted(batch.PROFILES):
            source = self.root / f'{name}-sources.json'
            source.write_text(json.dumps({'source.cpp': 'a' * 64}), encoding='utf-8')
            segments = []
            for offset in (0, 0x8000, 0xe000, 0x10000):
                path = self.root / f'{name}-{offset}.bin'
                path.write_bytes(partition_table() if offset == 0x8000 else name.encode() * 12)
                segments.append({'offset': offset, 'path': str(path), 'sha256': batch.sha256(path)})
            profile = {'schemaVersion': 1, 'profile': name, 'chip': 'esp32s3', 'normalFirmware': True,
                       'flashSize': '16MB', 'flashMode': 'dio', 'flashFrequency': '80m',
                       'gitRevision': 'a' * 40, 'sourceDigest': batch.sha256(source),
                       'sourceManifest': str(source), 'segments': segments}
            path = self.root / f'{name}.json'
            path.write_text(json.dumps(profile), encoding='utf-8')
            self.candidates.append(path)
        self.manifest = prepare(self.candidates, self.root / 'release')
        self.release = batch.load_release(self.manifest)
        self.devices = [{'label': 'corner-01', 'port': 'COM6', 'mac': '02:12:34:56:78:90',
                         'profile': 'tile-corner'},
                        {'label': 'long-01', 'port': 'COM8', 'mac': '02:12:34:56:78:91',
                         'profile': 'tile-long'}]

    def inventory(self, devices):
        path = self.root / 'inventory.csv'
        with path.open('w', newline='', encoding='utf-8') as handle:
            writer = csv.DictWriter(handle, fieldnames=['label', 'port', 'mac', 'profile'])
            writer.writeheader()
            writer.writerows(devices)
        return path

    def changed_manifest(self, change):
        data = json.loads(self.manifest.read_text(encoding='utf-8'))
        change(data)
        self.manifest.write_text(json.dumps(data), encoding='utf-8')

    def job(self, operation='flash'):
        device = self.devices[0]
        return {'operation': operation, 'device': device,
                'profile': self.release['profiles'][device['profile']]}

    def test_portable_package_and_blank_inventory(self):
        self.assertEqual(set(self.release['profiles']), batch.PROFILES)
        template = self.manifest.with_name('inventory-template.csv')
        self.assertEqual(len(template.read_text().splitlines()), 37)
        with self.assertRaises(ValueError):
            batch.load_inventory(template)
        for name in ('flash_worker.py', 'flash_layout.py', 'batch_flash.py'):
            self.assertTrue(self.manifest.with_name(name).is_file())
        for profile in self.release['profiles'].values():
            self.assertFalse(Path(profile['sourceManifest']).is_absolute())
            self.assertEqual(batch.sha256(self.manifest.parent / profile['sourceManifest']),
                             profile['sourceDigest'])

    def test_partial_batch_must_be_explicit(self):
        inventory = self.inventory(self.devices)
        with self.assertRaises(ValueError):
            batch.load_inventory(inventory)
        self.assertEqual(len(batch.load_inventory(inventory, partial=True)), 2)

    def test_diagnostic_candidate_and_release_rejected(self):
        candidate = self.root / 'player.json'
        original = candidate.read_bytes()
        release_original = self.manifest.read_bytes()
        for marker in ({'diagnosticOnly': True}, {'displayDiagnostic': 'pclk12'},
                       {'diagnosticOnly': 'false'}, {'displayDiagnostic': None}):
            with self.subTest(marker=marker):
                value = json.loads(original)
                value.update(marker)
                candidate.write_text(json.dumps(value), encoding='utf-8')
                destination = self.root / 'diagnostic-release'
                with self.assertRaisesRegex(ValueError, 'diagnostic firmware'):
                    prepare(self.candidates, destination)
                self.assertFalse(destination.exists())
                self.changed_manifest(lambda d: d['profiles']['player'].update(marker))
                with self.assertRaisesRegex(ValueError, 'diagnostic firmware'):
                    batch.load_release(self.manifest)
                self.manifest.write_bytes(release_original)
        candidate.write_bytes(original)

    def test_diagnostic_source_rejected_when_outer_marker_is_removed(self):
        candidate = self.root / 'player.json'
        value = json.loads(candidate.read_text(encoding='utf-8'))
        source = Path(value['sourceManifest'])
        source.write_text(json.dumps({'displayDiagnostic': 'pclk12'}), encoding='utf-8')
        value['sourceDigest'] = batch.sha256(source)
        candidate.write_text(json.dumps(value), encoding='utf-8')
        destination = self.root / 'diagnostic-source-release'
        with self.assertRaisesRegex(ValueError, 'diagnostic firmware'):
            prepare(self.candidates, destination)
        self.assertFalse(destination.exists())
        profile = self.release['profiles']['player']
        packaged_source = self.manifest.parent / profile['sourceManifest']
        packaged_source.write_bytes(source.read_bytes())
        self.changed_manifest(lambda d: d['profiles']['player'].update(
            sourceDigest=batch.sha256(packaged_source)))
        with self.assertRaisesRegex(ValueError, 'diagnostic firmware'):
            batch.load_release(self.manifest)

    def test_explicit_non_diagnostic_candidate_remains_portable(self):
        candidate = self.root / 'player.json'
        value = json.loads(candidate.read_text(encoding='utf-8'))
        value.update(diagnosticOnly=False, displayDiagnostic='none')
        candidate.write_text(json.dumps(value), encoding='utf-8')
        manifest = prepare(self.candidates, self.root / 'ordinary-release')
        self.assertFalse(batch.load_release(manifest)['profiles']['player']['diagnosticOnly'])

    def complete_inventory(self, board_size=16, player_count=4):
        devices = []
        for profile, count in batch.inventory_counts(board_size, player_count).items():
            for number in range(count):
                index = len(devices) + 1
                devices.append({'label': f'{profile}-{number + 1}', 'port': f'COM{index + 20}',
                                'mac': f'02:00:00:00:00:{index:02x}', 'profile': profile})
        return devices

    def test_16_tile_release_drives_portable_full_inventory_validation(self):
        manifest = prepare(self.candidates, self.root / 'release16', board_size=16)
        release = batch.load_release(manifest)
        self.assertEqual((release['boardSize'], release['playerCount']), (16, 4))
        with manifest.with_name('inventory-template.csv').open(newline='') as handle:
            counts = batch.Counter(row['profile'] for row in csv.DictReader(handle))
        self.assertEqual(counts, {'tile-long': 12, 'tile-corner': 4, 'player': 4})
        inventory = self.inventory(self.complete_inventory())
        result = subprocess.run([sys.executable, '-B', str(manifest.with_name('batch_flash.py')),
                                 str(manifest), str(inventory)], cwd=self.root,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('DRY RUN', result.stdout)
        self.assertEqual(result.stdout.count('PLAN '), 20)
        inventory = self.inventory(self.complete_inventory(board_size=32))
        result = subprocess.run([sys.executable, '-B', str(manifest.with_name('batch_flash.py')),
                                 str(manifest), str(inventory), '--partial'], cwd=self.root,
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('exceeds the release set', result.stderr)
        self.assertNotIn('PLAN ', result.stdout)

    def test_one_screen_inventory_and_partial_limits(self):
        manifest = prepare(self.candidates, self.root / 'release16-one', board_size=16, player_count=1)
        self.assertEqual(batch.load_release(manifest)['playerCount'], 1)
        devices = self.complete_inventory(player_count=1)
        self.assertEqual(len(batch.load_inventory(self.inventory(devices),
                                                 board_size=16, player_count=1)), 17)
        with self.assertRaisesRegex(ValueError, 'full inventory'):
            batch.load_inventory(self.inventory(devices[:-1]), board_size=16, player_count=1)
        self.assertEqual(len(batch.load_inventory(self.inventory(devices[:-1]), partial=True,
                                                 board_size=16, player_count=1)), 16)
        with self.assertRaisesRegex(ValueError, 'exceeds'):
            batch.load_inventory(self.inventory(self.complete_inventory(player_count=2)),
                                 partial=True, board_size=16, player_count=1)

    def test_legacy_release_keeps_32_tile_inventory(self):
        self.changed_manifest(lambda d: (d.pop('boardSize'), d.pop('playerCount')))
        release = batch.load_release(self.manifest)
        self.assertEqual(batch.inventory_counts(release.get('boardSize', 32),
                                                release.get('playerCount', 4)), batch.FULL_SET)
        self.assertEqual(len(batch.load_inventory(self.inventory(self.complete_inventory(32)))), 36)
        with self.assertRaisesRegex(ValueError, 'full inventory'):
            batch.load_inventory(self.inventory(self.complete_inventory(16)))

    def test_invalid_board_or_screen_count_rejected_before_packaging(self):
        for field, values in [('boardSize', [True, None, '16', 15, 24, 40]),
                              ('playerCount', [True, None, '1', 0, 5])]:
            for value in values:
                with self.subTest(field=field, value=value):
                    destination = self.root / 'invalid-layout'
                    kwargs = {'board_size': value} if field == 'boardSize' else {'player_count': value}
                    with self.assertRaisesRegex(ValueError, field):
                        prepare(self.candidates, destination, **kwargs)
                    self.assertFalse(destination.exists())
                    original = self.manifest.read_bytes()
                    self.changed_manifest(lambda d: d.update({field: value}))
                    with self.assertRaisesRegex(ValueError, field):
                        batch.load_release(self.manifest)
                    self.manifest.write_bytes(original)

    def test_portable_scripts_run_outside_repository_without_serial(self):
        inventory = self.inventory(self.devices)
        result = subprocess.run([sys.executable, '-B', str(self.manifest.with_name('batch_flash.py')),
                                 str(self.manifest), str(inventory), '--partial'],
                                cwd=self.root, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('DRY RUN', result.stdout)
        job = self.job(); job['operation'] = 'invalid-no-serial'
        result = subprocess.run([sys.executable, '-B', str(self.manifest.with_name('flash_worker.py'))],
                                input=json.dumps(job), cwd=self.root, capture_output=True,
                                text=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn('invalid worker operation', result.stderr)
        self.assertNotIn(batch.RESULT_PREFIX, result.stdout)

    def test_duplicate_mac_or_port_rejected(self):
        for field in ('mac', 'port', 'label'):
            with self.subTest(field=field):
                devices = [dict(d) for d in self.devices]
                devices[1][field] = devices[0][field]
                with self.assertRaises(ValueError):
                    batch.load_inventory(self.inventory(devices), partial=True)

    def test_changed_binary_rejected(self):
        Path(self.job()['profile']['segments'][0]['resolvedPath']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            batch.load_release(self.manifest)

    def test_diagnostic_firmware_rejected_before_packaging_or_serial(self):
        for marker in (False, None, 'true'):
            with self.subTest(marker=marker):
                self.changed_manifest(lambda d: d['profiles']['player'].update(normalFirmware=marker))
                with self.assertRaisesRegex(ValueError, 'normal firmware'):
                    batch.load_release(self.manifest)
                value = json.loads(self.candidates[0].read_text())
                value['normalFirmware'] = marker
                self.candidates[0].write_text(json.dumps(value))
                destination = self.root / 'diagnostic-rejected'
                with self.assertRaisesRegex(ValueError, 'normal firmware'):
                    prepare(self.candidates, destination)
                self.assertFalse(destination.exists())

    def test_path_escape_rejected(self):
        self.changed_manifest(lambda d: d['profiles']['player']['segments'][0].update(path='../outside.bin'))
        with self.assertRaisesRegex(ValueError, 'escapes'):
            batch.load_release(self.manifest)

    def test_source_manifest_escape_or_hash_rejected(self):
        self.changed_manifest(lambda d: d['profiles']['player'].update(sourceManifest='../outside.json'))
        with self.assertRaisesRegex(ValueError, 'escapes'):
            batch.load_release(self.manifest)
        value = json.loads(self.candidates[0].read_text())
        Path(value['sourceManifest']).write_text('{}')
        with self.assertRaisesRegex(ValueError, 'source manifest SHA'):
            prepare(self.candidates, self.root / 'bad-source')

    def test_flash_overlap_rejected(self):
        self.changed_manifest(lambda d: d['profiles']['player']['segments'][1].update(offset=0))
        with self.assertRaisesRegex(ValueError, 'overlapping'):
            batch.load_release(self.manifest)

    def test_prepare_rejects_nvs_segment(self):
        path = self.candidates[0]
        value = json.loads(path.read_text())
        data = self.root / 'nvs.bin'
        data.write_bytes(b'\xff' * 4096)
        value['segments'].append({'offset': 0x9000, 'path': str(data), 'sha256': batch.sha256(data)})
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'only bootloader'):
            prepare(self.candidates, self.root / 'bad-nvs')
        self.assertFalse((self.root / 'bad-nvs').exists())

    def test_load_rejects_nvs_segment(self):
        path = self.manifest.parent / 'nvs.bin'
        path.write_bytes(b'\xff' * 4096)
        self.changed_manifest(lambda d: d['profiles']['player']['segments'].append(
            {'offset': 0x9000, 'path': 'nvs.bin', 'sha256': batch.sha256(path)}))
        with self.assertRaisesRegex(ValueError, 'only bootloader'):
            batch.load_release(self.manifest)

    def test_partition_md5_and_app_boundary(self):
        images = batch.load_profile_images(self.job()['profile'])
        corrupt = bytearray(dict(images)[0x8000]); corrupt[12] ^= 1
        for replacement, message in [(bytes(corrupt), 'MD5'), (b'\xff' * 3072, 'partition')]:
            altered = [(offset, replacement if offset == 0x8000 else data) for offset, data in images]
            with self.assertRaisesRegex(ValueError, message):
                validate_images(altered)
        altered = [(offset, b'A' * (0x640000 + 1) if offset == 0x10000 else data)
                   for offset, data in images]
        with self.assertRaisesRegex(ValueError, 'boundary'):
            validate_images(altered)

    def test_bootloader_and_otadata_boundaries(self):
        images = batch.load_profile_images(self.job()['profile'])
        for target, size in ((0, 0x8001), (0xe000, 0x2001)):
            with self.subTest(offset=target), self.assertRaisesRegex(ValueError, 'boundary'):
                validate_images([(offset, b'A' * size if offset == target else data)
                                 for offset, data in images])

    def test_existing_layout_preserves_nvs_and_filesystems(self):
        job = self.job()
        for old in (b'\xff' * 4096, partition_table().ljust(4096, b'\xff')):
            result = process_job(job, FakeApi(old=old))
            self.assertEqual(result['status'], 'verified')
        api = FakeApi(old=partition_table(app_size=0x300000).ljust(4096, b'\xff'))
        with self.assertRaisesRegex(ValueError, 'separate migration'):
            process_job(job, api)
        self.assertNotIn('write', [name for name, _ in api.calls])
        self.assertTrue(api.rom.closed)

    def test_corrupt_existing_layout_rejected_before_write(self):
        api = FakeApi(old=b'\0' * 4096)
        with self.assertRaises(ValueError):
            process_job(self.job(), api)
        self.assertNotIn('write', [name for name, _ in api.calls])

    def test_one_serial_handle_for_mac_write_verify(self):
        api = FakeApi()
        result = process_job(self.job(), api)
        self.assertEqual(result['status'], 'verified')
        self.assertEqual(sum(name == 'open' for name, _ in api.calls), 1)
        self.assertEqual(len({id(handle) for _, handle in api.calls}), 1)
        steps = [name for name, _ in api.calls]
        self.assertLess(steps.index('write'), steps.index('verify'))
        self.assertLess(steps.index('verify'), steps.index('reset'))
        self.assertEqual(steps[-1], 'close')

    def test_stub_cannot_replace_serial_handle(self):
        api = FakeApi(); api.change_handle = True
        with self.assertRaisesRegex(RuntimeError, 'changed the serial handle'):
            process_job(self.job(), api)
        self.assertNotIn('write', [name for name, _ in api.calls])
        self.assertTrue(api.rom.closed)

    def test_wrong_mac_or_capacity_blocks_write(self):
        for api in (FakeApi(mac='02:aa:bb:cc:dd:ee'), FakeApi()):
            if api.mac == self.devices[0]['mac']:
                api.capacity = '8MB'
            with self.assertRaises(ValueError):
                process_job(self.job(), api)
            self.assertNotIn('write', [name for name, _ in api.calls])

    def test_changed_binary_rejected_before_open(self):
        job = self.job()
        Path(job['profile']['segments'][0]['resolvedPath']).write_bytes(b'changed')
        api = FakeApi()
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            process_job(job, api)
        self.assertFalse(api.calls)

    def test_write_and_verify_use_frozen_bytes_even_if_file_changes(self):
        job = self.job()
        segment = next(s for s in job['profile']['segments'] if s['offset'] == 0x10000)
        path = Path(segment['resolvedPath'])
        expected = path.read_bytes()
        api = FakeApi(); api.on_connect = lambda: path.write_bytes(b'changed-after-hash')
        self.assertEqual(process_job(job, api)['status'], 'verified')
        self.assertEqual(dict(api.written)[0x10000], expected)

    def test_write_verify_and_reset_failures_never_return_success(self):
        for flag in ('fail_write', 'fail_verify', 'fail_reset'):
            with self.subTest(flag=flag):
                api = FakeApi(); setattr(api, flag, True)
                with self.assertRaises((OSError, RuntimeError)):
                    process_job(self.job(), api)
                self.assertTrue(api.rom.closed)
                self.assertEqual(sum(name == 'open' for name, _ in api.calls), 1)

    def test_esptool_version_checked_before_open(self):
        api = FakeApi(); api.version = '5.2.0'
        with self.assertRaisesRegex(ValueError, '5.3.1'):
            process_job(self.job(), api)
        self.assertFalse(api.calls)

    def test_swapped_device_after_preflight_is_not_written_or_verified(self):
        calls = []
        def runner(python, job):
            api = FakeApi(mac=job['device']['mac'] if job['operation'] == 'preflight'
                          else '02:aa:bb:cc:dd:ee')
            calls.append(api)
            return process_job(job, api)
        log = self.root / 'log.jsonl'
        with self.assertRaisesRegex(ValueError, 'expected'):
            batch.flash_batch(self.release, self.devices, sys.executable, log, runner)
        self.assertFalse(any(name == 'write' for api in calls for name, _ in api.calls))
        self.assertNotIn('verified', [json.loads(line)['status'] for line in log.read_text().splitlines()])

    def test_wrong_mac_preflight_writes_nothing(self):
        operations = []
        def runner(python, job):
            operations.append(job['operation'])
            return process_job(job, FakeApi())
        with self.assertRaisesRegex(ValueError, 'expected'):
            batch.flash_batch(self.release, self.devices, sys.executable, self.root / 'log.jsonl', runner)
        self.assertNotIn('flash', operations)

    def test_write_failure_stops_next_device(self):
        operations = []
        def runner(python, job):
            operations.append((job['operation'], job['device']['mac']))
            api = FakeApi(mac=job['device']['mac'])
            api.fail_write = job['operation'] == 'flash'
            return process_job(job, api)
        with self.assertRaises(OSError):
            batch.flash_batch(self.release, self.devices, sys.executable, self.root / 'log.jsonl', runner)
        self.assertEqual(sum(op == 'flash' for op, _ in operations), 1)

    def test_each_device_gets_own_profile_and_verification(self):
        def runner(python, job):
            self.assertEqual(job['device']['profile'], job['profile']['profile'])
            return process_job(job, FakeApi(mac=job['device']['mac']))
        log = self.root / 'log.jsonl'
        batch.flash_batch(self.release, self.devices, sys.executable, log, runner)
        self.assertEqual(sum(json.loads(line)['status'] == 'verified'
                             for line in log.read_text().splitlines()), 2)

    def test_worker_bad_identity_result_rejected(self):
        def runner(python, job):
            return {'status': 'preflight_ok', 'mac': '02:aa:bb:cc:dd:ee',
                    'profile': job['device']['profile'], 'esptoolVersion': '5.3.1'}
        with self.assertRaisesRegex(ValueError, 'result identity'):
            batch.flash_batch(self.release, self.devices, sys.executable, self.root / 'log.jsonl', runner)

    def test_worker_timeout_stops_batch_and_logs_failure(self):
        operations = []
        def runner(python, job):
            operations.append(job['operation'])
            if job['operation'] == 'flash':
                raise subprocess.TimeoutExpired('worker', 240)
            return process_job(job, FakeApi(mac=job['device']['mac']))
        log = self.root / 'log.jsonl'
        with self.assertRaises(subprocess.TimeoutExpired):
            batch.flash_batch(self.release, self.devices, sys.executable, log, runner)
        self.assertEqual(operations.count('flash'), 1)
        self.assertEqual(json.loads(log.read_text().splitlines()[-1])['status'], 'failed')

    def test_worker_exit_zero_without_result_is_not_success(self):
        completed = subprocess.CompletedProcess([], 0, 'unrelated output\n', '')
        with patch.object(batch.subprocess, 'run', return_value=completed) as run:
            with self.assertRaisesRegex(ValueError, 'explicit result'):
                batch.run_worker(sys.executable, self.job())
            self.assertEqual(run.call_args.kwargs['timeout'], 240)

    def test_real_tile_partition_if_build_is_available(self):
        path = Path('C:/Users/kicof/AppData/Local/GridopolyTileTools/build-v033/tile_esp32s3_long/partitions.bin')
        if not path.is_file():
            self.skipTest('local tile build is not available')
        data = path.read_bytes()
        self.assertEqual(data, partition_table())
        self.assertEqual(next(p for p in parse_partitions(data) if p[2] == b'nvs')[3:5],
                         (0x9000, 0x5000))

    def test_installed_esptool_api_signatures_without_serial(self):
        try:
            api = EspToolApi()
        except ImportError:
            self.skipTest('esptool==5.3.1 is not installed in this test interpreter')
        import inspect
        self.assertEqual(api.version, '5.3.1')
        inspect.signature(api.loader).bind('unused-port', 115200)
        inspect.signature(api.loader.connect).bind(None, 'default-reset', 3)
        inspect.signature(api.write_flash).bind(None, [], flash_mode='dio', flash_freq='80m',
                                               flash_size='16MB', erase_all=False, force=False,
                                               no_progress=True)
        inspect.signature(api.verify_flash).bind(None, [], flash_mode='dio', flash_freq='80m',
                                                flash_size='16MB')
        inspect.signature(api.read_flash).bind(None, 0x8000, 4096, no_progress=True)
        inspect.signature(api.reset_chip).bind(None, 'hard-reset')

    def test_explicit_watchdog_reset_keeps_identity_handle_and_clears_force_download(self):
        api = FakeApi()
        job = self.job(); job['after'] = 'watchdog-reset'
        result = process_job(job, api)
        self.assertEqual(result['resetMode'], 'watchdog-reset')
        self.assertEqual(api.reset_modes, ['watchdog-reset'])
        self.assertFalse(api.force_download)
        names = [name for name, _ in api.calls]
        self.assertLess(names.index('verify'), names.index('clear_force_download'))
        self.assertLess(names.index('clear_force_download'), names.index('reset'))
        self.assertEqual(len({id(handle) for _, handle in api.calls}), 1)

    def test_watchdog_rejects_held_boot_or_non_usb_before_flash(self):
        for field in ('gpio0_high', 'usb_jtag'):
            with self.subTest(field=field):
                api = FakeApi(); setattr(api, field, False)
                job = self.job(); job['after'] = 'watchdog-reset'
                with self.assertRaises(ValueError):
                    process_job(job, api)
                self.assertFalse(any(name in ('write', 'reset') for name, _ in api.calls))
                self.assertTrue(api.rom.closed)

    def test_failed_force_download_cleanup_does_not_reset_or_report_verified(self):
        api = FakeApi(); api.fail_clear_force = True
        job = self.job(); job['after'] = 'watchdog-reset'
        with self.assertRaisesRegex(RuntimeError, 'did not clear'):
            process_job(job, api)
        self.assertEqual(api.reset_modes, [])
        self.assertTrue(api.rom.closed)

    def test_watchdog_batch_records_mode_and_propagates_to_preflight_and_write(self):
        operations = []
        def runner(python, job):
            operations.append((job['operation'], job['after']))
            return process_job(job, FakeApi(mac=job['device']['mac']))
        log = self.root / 'watchdog.jsonl'
        batch.flash_batch(self.release, self.devices, sys.executable, log, runner,
                          after='watchdog-reset')
        self.assertEqual(operations, [('preflight', 'watchdog-reset')] * 2 +
                         [('flash', 'watchdog-reset')] * 2)
        events = [json.loads(line) for line in log.read_text().splitlines()]
        self.assertEqual([event['resetMode'] for event in events],
                         ['hard-reset'] * 2 + ['watchdog-reset'] * 2)
        self.assertTrue(all(event['requestedAfter'] == 'watchdog-reset' for event in events))

    def test_watchdog_option_does_not_attempt_to_boot_a_blank_preflight_board(self):
        api = FakeApi(old=b'\xff' * 4096)
        job = self.job(); job['operation'] = 'preflight'; job['after'] = 'watchdog-reset'
        result = process_job(job, api)
        self.assertEqual(result['status'], 'preflight_ok')
        self.assertEqual(result['resetMode'], 'hard-reset')
        self.assertEqual(api.reset_modes, ['hard-reset'])
        self.assertFalse(any(name in ('write', 'clear_force_download') for name, _ in api.calls))

    def test_invalid_reset_mode_rejected_without_serial_or_runner(self):
        api = FakeApi(); job = self.job(); job['after'] = 'no-reset'
        with self.assertRaises(ValueError):
            process_job(job, api)
        self.assertFalse(api.calls)
        with self.assertRaises(ValueError):
            batch.flash_batch(self.release, self.devices, sys.executable,
                              self.root / 'invalid.jsonl', after='no-reset')

    def test_worker_must_confirm_requested_reset_mode(self):
        def runner(python, job):
            result = process_job(job, FakeApi(mac=job['device']['mac']))
            result['resetMode'] = 'hard-reset'
            return result
        log = self.root / 'mismatch.jsonl'
        with self.assertRaisesRegex(ValueError, 'identity/status'):
            batch.flash_batch(self.release, self.devices, sys.executable, log, runner,
                              after='watchdog-reset')
        self.assertNotIn('verified', [json.loads(line)['status'] for line in log.read_text().splitlines()])


if __name__ == '__main__':
    unittest.main()
