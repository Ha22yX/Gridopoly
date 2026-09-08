from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('tile_serial', Path(__file__).with_name('observe_tile_serial.py'))
observer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(observer)

PRESENT = ('2026-09-07T21:58:25.821454-04:00 [TAG] t=381632ms state=PRESENT '
           'type=HITAG_S256 count=1 uids=8EFA24DF consistent=3 sampling=0x2D field_off=PASS')


class ParserTests(unittest.TestCase):
    def test_recorded_event_time_is_preserved_separately_from_uptime(self):
        with patch.object(observer.time, 'time_ns', side_effect=AssertionError('offline clock read')):
            row = observer.parse_line(PRESENT)
        self.assertEqual(row['epochMs'], 1788832705821)
        self.assertEqual(row['device_uptime_ms'], 381632)
        self.assertEqual(row['recordedHostTime'], '2026-09-07T21:58:25.821454-04:00')
        self.assertEqual(row['rawLine'], PRESENT)
        self.assertEqual(row['uids'], ['8EFA24DF'])
        self.assertTrue(row['inventoryComplete'])
        self.assertEqual(row['field_off'], 'PASS')

    def test_unknown_or_naive_timestamp_is_never_replaced_by_now(self):
        unstamped = observer.parse_line('[TAG] t=5ms state=NO_TAG count=0 uids=--------')
        self.assertIsNone(unstamped['epochMs'])
        self.assertEqual(unstamped['timeSource'], 'unavailable')
        self.assertEqual(unstamped['uids'], [])
        self.assertTrue(unstamped['inventoryComplete'])
        for stamp in ('2026-09-08T01:58:25', '2026-99-08T01:58:25Z'):
            row = observer.parse_line(stamp + ' [TAG-STATUS] state=2 count=1 uids=8EFA24DF')
            self.assertIsNone(row['epochMs'])
            self.assertIn('timestampError', row)
            self.assertEqual(row['uids'], ['8EFA24DF'])

    def test_multiple_tags_overflow_incomplete_and_fault(self):
        row = observer.parse_line('[TAG-STATUS] state=2 count=2 uids=8EFA24DF,8EFA259D')
        self.assertTrue(row['inventoryComplete'])
        self.assertEqual(row['uids'], ['8EFA24DF', '8EFA259D'])
        for body in ('state=2 count=2+ uids=8EFA24DF,8EFA259D',
                     'state=2 count=2 uids=8EFA24DF',
                     'state=2 count=2 uids=8EFA24DF,8EFA24DF',
                     'state=2 count=1 uids=broken',
                     'state=0 count=0 uids=--------',
                     'state=2 count=0 uids=--------'):
            self.assertFalse(observer.parse_line('[TAG-STATUS] ' + body)['inventoryComplete'])
        self.assertTrue(observer.parse_line('[TAG-STATUS] state=4 count=0 uids=--------')['fault'])
        self.assertTrue(observer.parse_line('[HTRC110] TXDIS=1 verify=FAIL')['fault'])

    def test_cue_reset_and_field_failure(self):
        row = observer.parse_line('[MOVE] cue=destination player=1 revision=123456789012')
        self.assertEqual((row['event'], row['cue'], row['playerId'], row['movementRevision']),
                         ('movement_cue', 'destination', 1, 123456789012))
        for message in ('rst:0x15 (USB_UART_CHIP_RESET),boot:0x2a (SPI_FAST_FLASH_BOOT)',
                        '[BOOT] reset_reason=0'):
            self.assertEqual(observer.parse_line(message)['event'], 'reset')
        self.assertTrue(observer.parse_line('[TAG-INV] safe=NO field_off=FAIL')['fault'])

    def test_jsonl_replay_preserves_original_receive_time(self):
        saved = observer.parse_line(PRESENT, mode='live', received_epoch_ms=1788839999999)
        replay = observer.parse_line(json.dumps(saved))
        self.assertEqual(replay['epochMs'], saved['epochMs'])
        self.assertEqual(replay['device_uptime_ms'], saved['device_uptime_ms'])
        self.assertEqual(replay['rawLine'], PRESENT)
        self.assertEqual(replay['uids'], saved['uids'])
        unknown = observer.parse_line('[TAG] t=2ms state=NO_TAG count=0 uids=--------')
        self.assertIsNone(observer.parse_line(json.dumps(unknown))['epochMs'])

    def test_default_never_starts_capture(self):
        with patch.object(observer, 'capture_live', side_effect=AssertionError('port opened')):
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                observer.main([])
            self.assertEqual(caught.exception.code, 2)

    def test_offline_cli_and_exclusive_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / 'source.log', Path(directory) / 'result.jsonl'
            source.write_text(PRESENT + '\n', encoding='utf-8')
            with patch.object(observer, 'capture_live', side_effect=AssertionError('port opened')):
                self.assertEqual(observer.main(['--input', str(source), '--output', str(output)]), 0)
            first = output.read_bytes()
            record = json.loads(first)
            self.assertEqual(record['epochMs'], 1788832705821)
            self.assertEqual(record['lineNumber'], 1)
            with redirect_stderr(io.StringIO()):
                self.assertEqual(observer.main(['--input', str(source), '--output', str(output)]), 1)
            self.assertEqual(output.read_bytes(), first)


class FakePort:
    def __init__(self, **kwargs):
        assert kwargs['port'] is None
        self.dtr = self.rts = None
        self.closed = False
        self.writes = []
        self.chunks = [b'[TAG] t=42ms state=PRESENT count=1 uids=8EFA',
                       b'24DF field_off=PASS\r\n[MOVE] cue=none player=0 revision=5\n',
                       b'partial tail']

    def open(self):
        assert self.dtr is False and self.rts is False

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, count):
        return self.chunks.pop(0) if self.chunks else b''

    def write(self, data):
        self.writes.append(data)

    def close(self):
        self.closed = True


class LiveBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.port = None
        self.clock = 0

    def factory(self, **kwargs):
        self.port = FakePort(**kwargs)
        return self.port

    def tick(self):
        self.clock += 0.1
        return self.clock

    def capture(self, **kwargs):
        return observer.capture_live('COM_TEST_ONLY', 1, serial_factory=self.factory,
                                     monotonic=self.tick, wall_time_ns=lambda: 1234567890000000,
                                     **kwargs)

    def test_fragmented_lines_passive_capture_and_cleanup(self):
        records = list(self.capture())
        tag = next(row for row in records if row['event'] == 'tag_inventory')
        self.assertEqual(tag['uids'], ['8EFA24DF'])
        self.assertEqual(tag['device_uptime_ms'], 42)
        self.assertEqual(tag['epochMs'], 1234567890)
        self.assertTrue(tag['inventoryComplete'])
        self.assertFalse(next(row for row in records if row['rawLine'] == 'partial tail')['lineComplete'])
        self.assertTrue(self.port.closed)
        self.assertEqual(self.port.writes, [])

    def test_only_explicit_status_is_written(self):
        list(self.capture(status_interval=0.2))
        self.assertTrue(self.port.writes)
        self.assertEqual(set(self.port.writes), {b'STATUS\n'})
        self.assertTrue(self.port.closed)

    def test_early_generator_close_releases_port(self):
        stream = self.capture()
        next(stream)
        stream.close()
        self.assertTrue(self.port.closed)

    def test_read_error_releases_port(self):
        stream = self.capture()
        first = next(stream)
        self.assertEqual(first['timeSource'], 'collector_event')
        with patch.object(self.port, 'read', side_effect=OSError('device disconnected')):
            with self.assertRaises(OSError):
                next(stream)
        self.assertTrue(self.port.closed)


if __name__ == '__main__':
    unittest.main()
