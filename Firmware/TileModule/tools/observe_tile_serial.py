#!/usr/bin/env python3
"""Parse saved tile logs, or explicitly capture one serial port, as JSONL."""
from __future__ import annotations

import argparse
from contextlib import nullcontext
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import sys
import time


TIMESTAMP = re.compile(r'^(\d{4}-\d{2}-\d{2}T\S+)\s+(.*)$')
TAG_STATES = {0: 'SCANNING', 1: 'NO_TAG', 2: 'PRESENT', 3: 'UNSTABLE', 4: 'FAULT'}
EPOCH = datetime(1970, 1, 1, tzinfo=timezone.utc)


def timestamp_epoch_ms(value: str) -> int:
    moment = datetime.fromisoformat(value.replace('Z', '+00:00'))
    if moment.utcoffset() is None:
        raise ValueError('timestamp lacks a UTC offset')
    delta = moment.astimezone(timezone.utc) - EPOCH
    return (delta.days * 86400 + delta.seconds) * 1000 + delta.microseconds // 1000


def parse_line(raw_line: str, *, mode: str = 'offline', line_number: int | None = None,
               received_epoch_ms: int | None = None, line_complete: bool = True) -> dict:
    """Never reads the clock. Offline time must come from the saved record."""
    raw_line = raw_line.rstrip('\r\n')
    message = raw_line
    epoch_ms = received_epoch_ms if mode == 'live' else None
    time_source = 'host_receive' if epoch_ms is not None else 'unavailable'
    recorded_time = None
    timestamp_error = None
    from_jsonl = False
    # Accept our JSONL too, preserving its original receive time and raw line.
    if mode == 'offline' and raw_line.startswith('{'):
        try:
            saved = json.loads(raw_line)
        except ValueError:
            saved = None
        if isinstance(saved, dict) and saved.get('source') == 'tileSerial' and saved.get('schemaVersion') == 1:
            if isinstance(saved.get('rawLine'), str):
                from_jsonl = True
                raw_line = message = saved['rawLine']
                saved_epoch = saved.get('epochMs')
                if type(saved_epoch) is int:
                    epoch_ms = saved_epoch
                    time_source = 'recorded_host_receive'
                recorded_time = saved.get('recordedHostTime')
                timestamp_error = saved.get('timestampError')
                line_complete = saved.get('lineComplete', True) is True
    stamped = TIMESTAMP.match(message)
    if stamped:
        recorded_time, message = stamped.groups()
        if mode == 'offline' and not from_jsonl:
            try:
                epoch_ms = timestamp_epoch_ms(recorded_time)
                time_source = 'recorded_host_receive'
            except ValueError as error:
                epoch_ms = None
                time_source = 'unavailable'
                timestamp_error = str(error)

    row = {'schemaVersion': 1, 'source': 'tileSerial', 'mode': mode,
           'epochMs': epoch_ms, 'timeSource': time_source,
           'recordedHostTime': recorded_time, 'device_uptime_ms': None,
           'event': 'log', 'rawLine': raw_line, 'message': message,
           'lineComplete': line_complete}
    if line_number is not None:
        row['lineNumber'] = line_number
    if timestamp_error:
        row['timestampError'] = timestamp_error
    uptime = re.search(r'\bt=(\d+)ms\b', message)
    if uptime:
        row['device_uptime_ms'] = int(uptime[1])
    field = re.search(r'\bfield_off=(\S+)', message)
    if field:
        row['field_off'] = field[1]
    row['fault'] = bool(re.search(r'\bFAULT\b|\bERROR\b|\bsafe=NO\b|\bfield_off=FAIL\b|readback failed', message))

    if message.startswith(('[TAG]', '[TAG-STATUS]')):
        row['event'] = 'tag_inventory' if message.startswith('[TAG]') else 'tag_status'
        state = re.search(r'\bstate=(\w+)', message)
        if state:
            row['tagState'] = TAG_STATES.get(int(state[1]), 'UNKNOWN') if state[1].isdigit() else state[1]
            row['fault'] = row['fault'] or row['tagState'] == 'FAULT'
        count = re.search(r'\bcount=(\d+)(\+?)', message)
        uids = re.search(r'\buids=([^\s]*)', message)
        if count:
            row['tagCount'] = int(count[1])
            row['tagOverflow'] = bool(count[2])
        if uids:
            row['uids'] = [] if uids[1] in ('', '--------') else uids[1].upper().split(',')
        values = row.get('uids', [])
        row['inventoryComplete'] = bool(
            line_complete and count and uids and row.get('tagState') in ('PRESENT', 'NO_TAG')
            and not row.get('tagOverflow') and row['tagCount'] == len(values) <= 6
            and len(set(values)) == len(values)
            and all(re.fullmatch(r'[0-9A-F]{8}', value) for value in values)
            and (row['tagState'] == 'NO_TAG') == (len(values) == 0))
        for name in ('consistent', 'sampling'):
            value = re.search(r'\b' + name + r'=(\S+)', message)
            if value:
                row[name] = value[1]
    elif message.startswith('[MOVE]'):
        row['event'] = 'movement_cue'
        for key, pattern in (('cue', r'cue=(\w+)'), ('playerId', r'player=(\d+)'),
                             ('movementRevision', r'revision=(\d+)')):
            match = re.search(pattern, message)
            if match:
                row[key] = match[1] if key == 'cue' else int(match[1])
    elif message.startswith('[HTRC110]'):
        row['event'] = 'reader_diagnostic'
        txdis = re.search(r'TXDIS=(\S+)', message)
        if txdis:
            row['txdis'] = txdis[1]
        if 'verify=FAIL' in message or 'UNKNOWN/FAIL' in message:
            row['fault'] = True
    elif re.search(r'^rst:|\[BOOT\].*reset_reason|USB_UART_CHIP_RESET', message):
        row['event'] = 'reset'
        reason = re.search(r'reset_reason=(\S+)|rst:(\S+)', message)
        if reason:
            row['resetReason'] = reason[1] or reason[2]
    elif message.startswith('ESP-ROM:'):
        row['event'] = 'boot'
    elif message.startswith('[NET]'):
        row['event'] = 'network'
        for key, pattern in (('moduleId', r'\bmodule=(\S+)'), ('deviceId', r'\bdevice=(\S+)'),
                             ('networkState', r'\bstate=(\S+)'), ('httpStatus', r'\bhttp=(-?\d+)')):
            match = re.search(pattern, message)
            if match:
                row[key] = int(match[1]) if key == 'httpStatus' else match[1]
    elif message.startswith('[TILE]'):
        row['event'] = 'tile_assignment'
        for key, pattern in (('tileId', r'\bid=(\S+)'), ('mapIndex', r'\bmap=(\d+)'),
                             ('assignmentSource', r'\bsource=(\S+)')):
            match = re.search(pattern, message)
            if match:
                row[key] = int(match[1]) if key == 'mapIndex' else match[1]
    elif message.startswith('MONITOR '):
        row['event'] = 'capture_lifecycle'
        row['recordOrigin'] = 'collector'
        if mode == 'live':
            row['timeSource'] = 'collector_event'
    if row['fault'] and row['event'] == 'log':
        row['event'] = 'fault'
    return row


def read_offline(path: Path):
    with path.open(encoding='utf-8-sig', errors='replace') as source:
        for line_number, line in enumerate(source, 1):
            yield parse_line(line, line_number=line_number,
                             line_complete=line.endswith(('\n', '\r')))


def capture_live(port_name: str, duration: float, status_interval: float = 0, *,
                 serial_factory=None, monotonic=time.monotonic, wall_time_ns=time.time_ns):
    if serial_factory is None:
        import serial  # Optional; never imported in offline mode.
        serial_factory = serial.Serial
    port = serial_factory(port=None, baudrate=115200, timeout=0.2, write_timeout=2)
    port.dtr = False
    port.rts = False
    port.port = port_name
    buffer = bytearray()
    started = monotonic()
    next_status = started
    received_ms = None
    opened = False
    try:
        port.open()
        opened = True
        yield parse_line(f'MONITOR OPEN {port_name} DTR=false RTS=false', mode='live',
                         received_epoch_ms=wall_time_ns() // 1_000_000)
        while monotonic() - started < duration:
            if status_interval and monotonic() >= next_status:
                port.write(b'STATUS\n')
                next_status = monotonic() + status_interval
            chunk = port.read(min(max(port.in_waiting, 1), 4096))
            if not chunk:
                continue
            received_ms = wall_time_ns() // 1_000_000
            buffer.extend(chunk)
            while b'\n' in buffer:
                raw, _, rest = buffer.partition(b'\n')
                buffer = bytearray(rest)
                yield parse_line(raw.decode('utf-8', errors='replace'), mode='live',
                                 received_epoch_ms=received_ms)
            if len(buffer) >= 65536:
                yield parse_line(buffer.decode('utf-8', errors='replace'), mode='live',
                                 received_epoch_ms=received_ms, line_complete=False)
                buffer.clear()
    finally:
        # Close even on interrupted capture or output errors; never toggle pins.
        port.close()
    if buffer:
        yield parse_line(buffer.decode('utf-8', errors='replace'), mode='live',
                         received_epoch_ms=received_ms, line_complete=False)
    if opened:
        yield parse_line(f'MONITOR CLOSED {port_name}', mode='live',
                         received_epoch_ms=wall_time_ns() // 1_000_000)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--input', type=Path, help='saved timestamped log or this tool\'s JSONL')
    source.add_argument('--port', help='explicit live port; opens it once, without reset commands')
    parser.add_argument('--output', type=Path, help='new JSONL file; existing files are refused')
    parser.add_argument('--duration', type=float, default=60, help='live duration in seconds')
    parser.add_argument('--status-interval', type=float, default=0,
                        help='live STATUS request period; default 0 is passive')
    args = parser.parse_args(argv)
    if not math.isfinite(args.duration) or args.duration <= 0:
        parser.error('duration must be finite and positive')
    if not math.isfinite(args.status_interval) or args.status_interval < 0:
        parser.error('status-interval must be finite and nonnegative')
    stream = None
    try:
        output = args.output.open('x', encoding='utf-8') if args.output else nullcontext(sys.stdout)
        with output as sink:
            stream = read_offline(args.input) if args.input else capture_live(args.port, args.duration, args.status_interval)
            try:
                for row in stream:
                    print(json.dumps(row, ensure_ascii=False, separators=(',', ':')), file=sink, flush=True)
            finally:
                stream.close()
    except KeyboardInterrupt:
        return 130
    except (OSError, ImportError, ValueError) as error:
        print(f'Capture failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
