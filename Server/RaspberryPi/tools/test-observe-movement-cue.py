"""Local HTTP regressions for read-only movement/Tag evidence collection."""
from __future__ import annotations

import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import unittest

SCRIPT = Path(__file__).with_name('observe-movement-cue.py')
SPEC = importlib.util.spec_from_file_location('observe_movement_cue', SCRIPT)
observer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(observer)

FIXTURE = {
    '/api/sync': {'roomId': 123, 'version': 86, 'phase': 2, 'activePlayer': 1,
                  'tagBindingRevision': 4, 'players': [{'id': 1, 'position': 3, 'tagUid': '8EFA24DF'}],
                  'movementCueGate': {'active': True, 'ready': False, 'player': 1, 'origin': 3, 'target': 7}},
    '/health': {'ok': True, 'roomId': 123, 'version': 86,
                'udp': {'authFailures': 0, 'replayDrops': 0, 'txErrors': 32}},
    '/api/tile-debug/assignments': {'roomId': 123, 'serverRevision': 8,
        'modules': [{'moduleId': 'tile-a', 'tagRevision': 19, 'tagReaderState': 'stable',
                     'tagOverflow': False, 'online': True}],
        'assignments': [{'moduleId': 'tile-a', 'tile_id': 'B2', 'mapIndex': 7, 'revision': 8}]},
    '/api/tile-tags': {'ok': True, 'roomId': 123, 'tagRevision': 21, 'bindingRevision': 4,
        'updatedAtMs': 1788832705821,
        'tags': [
            {'uid': '8EFA24DF', 'currentlySeen': True, 'lastSeenMs': 1788832705821,
             'boundPlayerId': 1, 'sightings': [
                {'moduleId': 'tile-a', 'tileId': 'B2', 'mapIndex': 7,
                 'currentlySeen': True, 'lastSeenMs': 1788832705821},
                {'moduleId': 'tile-b', 'tileId': 'A2', 'mapIndex': 3,
                 'currentlySeen': False, 'lastSeenMs': 1788832705000}]},
            {'uid': '12345678', 'currentlySeen': False, 'lastSeenMs': 1788832704000,
             'boundPlayerId': 0, 'sightings': []}],
        'bindings': [{'playerId': 1, 'uid': '8EFA24DF'}, {'playerId': 2, 'uid': ''}]},
}


class ObserverTests(unittest.TestCase):
    def setUp(self):
        self.responses = copy.deepcopy(FIXTURE)
        self.statuses = {}
        self.paths = []
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                owner.paths.append(self.path)
                value = owner.responses.get(self.path, {})
                body = value if isinstance(value, bytes) else json.dumps(value).encode()
                self.send_response(owner.statuses.get(self.path, 200))
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_args):
                pass

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.worker = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.worker.start()
        self.base = f'http://127.0.0.1:{self.server.server_port}'

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.worker.join()

    def test_complete_uids_revisions_and_per_request_times_survive(self):
        record = observer.collect_sample(self.base)
        self.assertTrue(record['sampleOk'])
        self.assertFalse(record['snapshotAtomic'])
        self.assertEqual(record['tagSnapshot'], FIXTURE['/api/tile-tags'])
        self.assertEqual(record['gate']['target'], 7)
        self.assertEqual(record['tagBindingRevision'], 4)
        self.assertEqual(record['modules'][0]['tagRevision'], 19)
        self.assertEqual(record['assignments'][0]['mapIndex'], 7)
        self.assertEqual(self.paths, list(observer.PATHS))
        previous = record['sampleStartedEpochMs']
        for timing in record['requests'].values():
            self.assertLessEqual(previous, timing['startedEpochMs'])
            self.assertLessEqual(timing['startedEpochMs'], timing['receivedEpochMs'])
            self.assertGreaterEqual(timing['durationMs'], 0)
            previous = timing['receivedEpochMs']
        self.assertLessEqual(previous, record['epochMs'])
        self.assertGreaterEqual(record['sampleSpanMs'], 0)

    def test_room_changes_between_gets_are_not_hidden_by_merging(self):
        self.responses['/api/tile-tags']['roomId'] = 124
        record = observer.collect_sample(self.base)
        self.assertEqual(record['roomId'], 123)
        self.assertEqual(record['tagSnapshot']['roomId'], 124)
        self.assertFalse(record['snapshotAtomic'])

    def test_successful_empty_set_is_distinct_from_tag_endpoint_failure(self):
        self.responses['/api/tile-tags']['tags'] = []
        empty = observer.collect_sample(self.base)
        self.assertTrue(empty['sampleOk'])
        self.assertEqual(empty['tagSnapshot']['tags'], [])
        self.statuses['/api/tile-tags'] = 503
        failed = observer.collect_sample(self.base)
        self.assertFalse(failed['sampleOk'])
        self.assertIsNone(failed['tagSnapshot'])
        self.assertEqual(failed['gate']['target'], 7)
        self.assertEqual(failed['modules'][0]['tagRevision'], 19)
        self.assertEqual(failed['requests']['/api/tile-tags']['httpStatus'], 503)

    def test_malformed_json_and_invalid_shapes_are_recorded_without_crashing(self):
        for body in (b'{broken', [], {'ok': True}, {'tags': [None], 'bindings': []},
                     {'tags': [{'uid': '8EFA24DF', 'sightings': [None]}], 'bindings': []}):
            with self.subTest(body=body):
                self.responses['/api/tile-tags'] = body
                record = observer.collect_sample(self.base)
                self.assertFalse(record['sampleOk'])
                self.assertIsNone(record['tagSnapshot'])
                self.assertIn(record['requests']['/api/tile-tags']['error'],
                              ('ValueError', 'JSONDecodeError'))
                self.assertEqual(record['version'], 86)

    def test_missing_revision_and_timeout_are_explicit_failures(self):
        from unittest.mock import patch
        self.responses['/api/tile-tags'].pop('tagRevision')
        record = observer.collect_sample(self.base)
        self.assertFalse(record['sampleOk'])
        self.assertIsNone(record['tagSnapshot'])
        with patch.object(observer, 'get_json', side_effect=TimeoutError):
            timed_out = observer.collect_sample(self.base, 0.1)
        self.assertTrue(all(r['error'] == 'TimeoutError' for r in timed_out['requests'].values()))
        self.assertFalse(timed_out['sampleOk'])

    def test_connection_refusal_is_timestamped_for_each_endpoint(self):
        with socket.socket() as unused:
            unused.bind(('127.0.0.1', 0))  # Reserve a port without listening.
            record = observer.collect_sample(f'http://127.0.0.1:{unused.getsockname()[1]}', 0.1)
        self.assertFalse(record['sampleOk'])
        self.assertIsNone(record['gate'])
        self.assertIsNone(record['tagSnapshot'])
        for timing in record['requests'].values():
            self.assertFalse(timing['ok'])
            self.assertIn('error', timing)
            self.assertIn('receivedEpochMs', timing)

    def test_cli_retains_unchanged_samples_and_never_overwrites_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'capture.jsonl'
            command = [sys.executable, str(SCRIPT), '--base-url', self.base,
                       '--duration', '0.3', '--interval', '0.25', '--output', str(output)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            saved = output.read_bytes()
            records = [json.loads(line) for line in saved.splitlines()]
            self.assertGreaterEqual(len(records), 3)
            self.assertEqual(records[0]['tagSnapshot'], records[1]['tagSnapshot'])
            self.assertEqual(records[-1]['totalSamples'], len(records) - 1)
            self.assertEqual(records[-1]['requestErrors'], 0)
            self.assertEqual(result.stdout.splitlines(), saved.decode().splitlines())
            refusal = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(refusal.returncode, 0)
            self.assertEqual(output.read_bytes(), saved)


if __name__ == '__main__':
    unittest.main()
