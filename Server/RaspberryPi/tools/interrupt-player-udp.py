#!/usr/bin/env python3
"""Bounded single-player UDP loss probe; print-only unless --apply is explicit.

Run on the Pi in a coordinated console observation window. The kernel expires
only the target address after --duration, even if this process is killed.
This tests UDP/session recovery, not Wi-Fi disassociation or SDK reconnect.
"""
import argparse
import ipaddress
import json
import os
import re
import signal
import subprocess
import time
import uuid


def ruleset(table, interface, address, port, duration):
    return f"""table inet {table} {{
  set player {{ type ipv4_addr; flags timeout; timeout {duration}s;
    elements = {{ {address} timeout {duration}s }}
  }}
  chain incoming {{ type filter hook input priority -10; policy accept;
    iifname "{interface}" ip saddr @player udp dport {port} counter drop
  }}
  chain outgoing {{ type filter hook output priority -10; policy accept;
    oifname "{interface}" ip daddr @player udp sport {port} counter drop
  }}
}}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--player-ip', required=True)
    parser.add_argument('--expected-mac', required=True)
    parser.add_argument('--interface', default='ap0')
    parser.add_argument('--port', type=int, default=4242)
    parser.add_argument('--duration', type=int, default=40)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    try:
        address = ipaddress.IPv4Address(args.player_ip)
    except ipaddress.AddressValueError as error:
        parser.error(str(error))
    network = ipaddress.IPv4Network('10.42.0.0/24')
    if address not in network or (int(address) & 255) in (0, 1, 255):
        parser.error('player IP must be a host on 10.42.0.0/24 other than the Pi')
    if not re.fullmatch(r'[a-zA-Z0-9_.-]{1,15}', args.interface):
        parser.error('invalid interface name')
    if not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', args.expected_mac):
        parser.error('invalid expected MAC')
    if not 1 <= args.duration <= 60 or args.port != 4242:
        parser.error('duration must be 1..60 seconds and port must be 4242')
    table = f'gridopoly_udp_probe_{os.getpid()}_{uuid.uuid4().hex[:8]}'
    nft_input = ruleset(table, args.interface, address, args.port, args.duration)

    def emit(event, **fields):
        print(json.dumps(dict(epochMs=time.time_ns() // 1000000,
                              event=event, table=table, **fields)), flush=True)

    emit('prepared', apply=args.apply, playerIp=str(address),
         expectedMac=args.expected_mac.lower(), durationSeconds=args.duration,
         nftRules=nft_input, scope='single-player UDP only; Wi-Fi remains associated')
    if not args.apply:
        return 0
    neighbors = json.loads(subprocess.check_output(
        ['ip', '-j', 'neigh', 'show', 'dev', args.interface], text=True))
    matching = [n for n in neighbors if n.get('dst') == str(address)
                and n.get('lladdr', '').lower() == args.expected_mac.lower()
                and not any(state in str(n.get('state', '')).upper()
                            for state in ('FAILED', 'INCOMPLETE'))]
    if len(matching) != 1:
        raise RuntimeError('current IP/MAC neighbor mapping is not verified; no rules applied')
    def table_exists():
        tables = json.loads(subprocess.check_output(
            ['nft', '-j', 'list', 'tables'], text=True))
        return any(item.get('table', {}).get('name') == table
                   and item.get('table', {}).get('family') == 'inet'
                   for item in tables.get('nftables', []))

    if table_exists():
        raise RuntimeError('unique probe table already exists; refusing to alter it')
    # Validate the full transaction before changing the running ruleset.
    subprocess.run(['nft', '-c', '-f', '-'], input=nft_input, text=True, check=True)

    def stop(signum, _frame):
        raise InterruptedError(f'received signal {signum}')

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    attempted = False
    try:
        # A successful atomic nft transaction is the only mutation before the
        # bounded wait. No service/configuration/room/assignment is changed.
        attempted = True
        subprocess.run(['nft', '-f', '-'], input=nft_input, text=True, check=True)
        emit('blocked', kernelTimeoutSeconds=args.duration)
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            time.sleep(max(0.0, min(0.5, deadline - time.monotonic())))
        status = subprocess.run(['nft', 'list', 'table', 'inet', table],
                                text=True, capture_output=True, check=True)
        emit('expired', nftState=status.stdout)
    finally:
        if attempted:
            # Probe the exact unique table even if apply succeeded just before
            # a signal/exception reached Python. A post-apply flag has a race.
            if table_exists():
                subprocess.run(['nft', 'delete', 'table', 'inet', table], check=True)
            if table_exists():
                raise RuntimeError('probe table remains after cleanup')
            emit('cleaned', tableAbsent=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
