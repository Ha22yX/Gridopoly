#!/usr/bin/env python3
"""Read-only UTC-timestamped gate/Tag observations. Never submits game actions."""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import json
import math
from pathlib import Path
import time
import urllib.error
import urllib.request

PATHS = ('/api/sync', '/health', '/api/tile-debug/assignments', '/api/tile-tags')


def epoch_ms() -> int:
    return time.time_ns() // 1_000_000


def get_json(base: str, path: str, timeout: float = 2) -> dict:
    with urllib.request.urlopen(base.rstrip('/') + path, timeout=timeout) as response:
        data = json.load(response)
    if not isinstance(data, dict):
        raise ValueError('expected a JSON object')
    if data.get('ok') is False:
        raise ValueError('endpoint returned ok=false')
    required_numbers = {
        '/api/sync': ('roomId', 'version', 'tagBindingRevision'),
        '/health': ('roomId', 'version'),
        '/api/tile-debug/assignments': ('roomId', 'serverRevision'),
        '/api/tile-tags': ('roomId', 'tagRevision', 'bindingRevision', 'updatedAtMs'),
    }.get(path, ())
    for field in required_numbers:
        if type(data.get(field)) is not int:
            raise ValueError('missing or invalid number: ' + field)
    list_fields = {'/api/sync': ('players',),
                   '/api/tile-debug/assignments': ('modules', 'assignments'),
                   '/api/tile-tags': ('tags', 'bindings')}.get(path, ())
    for field in list_fields:
        values = data.get(field)
        if not isinstance(values, list) or any(not isinstance(v, dict) for v in values):
            raise ValueError('missing or invalid list: ' + field)
    if path == '/health' and not isinstance(data.get('udp'), dict):
        raise ValueError('missing or invalid UDP diagnostics')
    if path == '/api/tile-tags':
        for tag in data['tags']:
            if not isinstance(tag.get('uid'), str) or not isinstance(tag.get('sightings'), list):
                raise ValueError('invalid Tag observation')
            if any(not isinstance(s, dict) for s in tag['sightings']):
                raise ValueError('invalid Tag sighting')
    return data


def summarize(sync: dict | None, health: dict | None,
              assignments: dict | None, tags: dict | None) -> dict:
    # Keep failed endpoints as null, distinct from a successful empty UID set.
    sync_data, health_data, assignment_data = sync or {}, health or {}, assignments or {}
    udp = health_data.get('udp', {})
    return {
        'roomId': sync_data.get('roomId'),
        'version': sync_data.get('version'),
        'phase': sync_data.get('phase'),
        'activePlayer': sync_data.get('activePlayer'),
        'gate': sync_data.get('movementCueGate'),
        'tagBindingRevision': sync_data.get('tagBindingRevision'),
        'players': ([{key: p.get(key) for key in ('id', 'position', 'tagUid')}
                     for p in sync_data.get('players', [])] if sync is not None else None),
        'healthRoomId': health_data.get('roomId'),
        'healthVersion': health_data.get('version'),
        'udp': ({key: udp.get(key) for key in ('authFailures', 'replayDrops', 'txErrors')}
                if health is not None else None),
        'assignmentRoomId': assignment_data.get('roomId'),
        'assignmentRevision': assignment_data.get('serverRevision'),
        'modules': ([{key: module.get(key) for key in
                     ('moduleId', 'deviceId', 'online', 'lastSeenMs', 'tagReaderState',
                      'tagRevision', 'tagOverflow')}
                    for module in assignment_data.get('modules', [])]
                    if assignments is not None else None),
        'assignments': ([{key: item.get(key) for key in
                         ('moduleId', 'deviceId', 'tile_id', 'mapIndex', 'revision')}
                        for item in assignment_data.get('assignments', [])]
                        if assignments is not None else None),
        # Preserve every UID, historical/current sightings, room, both revisions,
        # bindings and server lastSeenMs/updatedAtMs exactly as returned.
        'tagSnapshot': tags,
    }


def collect_sample(base: str, timeout: float = 2) -> dict:
    started, started_epoch = time.monotonic(), epoch_ms()
    observations, requests = {}, {}
    for path in PATHS:
        request_started = time.monotonic()
        timing = {'startedEpochMs': epoch_ms()}
        try:
            observations[path] = get_json(base, path, timeout)
            timing['ok'] = True
        except (urllib.error.URLError, TimeoutError, OSError, ValueError) as error:
            observations[path] = None
            timing.update(ok=False, error=type(error).__name__)
            if isinstance(error, urllib.error.HTTPError):
                timing['httpStatus'] = error.code
        timing['receivedEpochMs'] = epoch_ms()
        timing['durationMs'] = round((time.monotonic() - request_started) * 1000, 3)
        requests[path] = timing
    return {
        'schema': 2,
        'sampleStartedEpochMs': started_epoch,
        'epochMs': epoch_ms(),
        'sampleSpanMs': round((time.monotonic() - started) * 1000, 3),
        'snapshotAtomic': False,
        'clockSources': {'epochMs': 'collector_utc_unix_ms',
                         'sampleSpanMs': 'collector_monotonic_elapsed_ms',
                         'tagSnapshot.lastSeenMs/updatedAtMs': 'server_utc_unix_ms'},
        'sampleOk': all(r['ok'] for r in requests.values()),
        'requests': requests,
        **summarize(*(observations[path] for path in PATHS)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-url', default='http://127.0.0.1')
    parser.add_argument('--duration', type=float, default=60)
    parser.add_argument('--interval', type=float, default=0.5)
    parser.add_argument('--timeout', type=float, default=2, help='Per-GET timeout in seconds')
    parser.add_argument('--output', type=Path,
                        help='Also flush every record to a NEW JSONL file; never overwrite evidence')
    args = parser.parse_args()
    if (not all(math.isfinite(v) for v in (args.duration, args.interval, args.timeout)) or
            args.duration <= 0 or args.interval < 0.25 or args.timeout <= 0):
        parser.error('duration/timeout must be finite positive; interval must be finite and >=0.25s')
    with ExitStack() as stack:
        try:
            output = stack.enter_context(args.output.open('x', encoding='utf-8')) if args.output else None
        except OSError as error:
            parser.error(f'cannot create output file: {error.strerror}')

        def emit(record: dict) -> None:
            line = json.dumps(record, separators=(',', ':'))
            if output:
                output.write(line + '\n')
                output.flush()
            print(line, flush=True)

        end = time.monotonic() + args.duration
        samples = errors = total = 0
        interrupted = False
        try:
            while time.monotonic() < end:
                started = time.monotonic()
                record = collect_sample(args.base_url, args.timeout)
                total += 1
                samples += int(record['sampleOk'])
                errors += sum(not request['ok'] for request in record['requests'].values())
                emit(record)  # Retain every sample, including unchanged and partial failures.
                remaining = min(args.interval - (time.monotonic() - started), end - time.monotonic())
                if remaining > 0:
                    time.sleep(remaining)
        except KeyboardInterrupt:
            interrupted = True
        emit({'complete': not interrupted, 'interrupted': interrupted, 'epochMs': epoch_ms(),
              'samples': samples, 'totalSamples': total, 'requestErrors': errors})
    return 0 if samples > 0 and not interrupted else 1


if __name__ == '__main__':
    raise SystemExit(main())
