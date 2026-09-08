#!/usr/bin/env python3
"""Read-only, timestamped movement-gate observations; never submits game actions."""
from __future__ import annotations

import argparse
import json
import time
import urllib.error
import urllib.request


def get_json(base: str, path: str) -> dict:
    with urllib.request.urlopen(base.rstrip('/') + path, timeout=2) as response:
        return json.load(response)


def summarize(sync: dict, health: dict, assignments: dict) -> dict:
    udp = health.get('udp', {})
    return {
        'roomId': sync.get('roomId'),
        'version': sync.get('version'),
        'phase': sync.get('phase'),
        'activePlayer': sync.get('activePlayer'),
        'gate': sync.get('movementCueGate'),
        'players': [{'id': p.get('id'), 'position': p.get('position')}
                    for p in sync.get('players', [])],
        'udp': {key: udp.get(key) for key in
                ('authFailures', 'replayDrops', 'txErrors')},
        'modules': [{key: module.get(key) for key in
                     ('moduleId', 'online', 'tagReaderState', 'tagRevision', 'tagOverflow')}
                    for module in assignments.get('modules', [])],
        'assignments': [{key: item.get(key) for key in
                         ('moduleId', 'tile_id', 'mapIndex', 'revision')}
                        for item in assignments.get('assignments', [])],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-url', default='http://127.0.0.1')
    parser.add_argument('--duration', type=float, default=60)
    parser.add_argument('--interval', type=float, default=0.5)
    args = parser.parse_args()
    if args.duration <= 0 or args.interval < 0.25:
        parser.error('duration must be positive and interval at least 0.25 seconds')
    end = time.monotonic() + args.duration
    previous = None
    samples = 0
    errors = 0
    while time.monotonic() < end:
        started = time.monotonic()
        try:
            sync = get_json(args.base_url, '/api/sync')
            health = get_json(args.base_url, '/health')
            assignments = get_json(args.base_url, '/api/tile-debug/assignments')
            state = summarize(sync, health, assignments)
            # The GETs are sequential observations, not an atomic snapshot.
            state['healthVersion'] = health.get('version')
            samples += 1
        except (urllib.error.URLError, TimeoutError, OSError, ValueError) as error:
            errors += 1
            state = {'error': type(error).__name__}
        if state != previous:
            print(json.dumps({'epochMs': time.time_ns() // 1_000_000,
                              'sampleSpanMs': round((time.monotonic() - started) * 1000),
                              **state}, separators=(',', ':')), flush=True)
            previous = state
        remaining = min(args.interval - (time.monotonic() - started), end - time.monotonic())
        if remaining > 0:
            time.sleep(remaining)
    print(json.dumps({'complete': True, 'samples': samples, 'errors': errors}), flush=True)
    return 0 if samples > 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
