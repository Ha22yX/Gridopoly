#!/usr/bin/env python3
"""Validate the UDP loss probe only inside a fresh, empty network namespace.

Run on Linux as root: python3 test-interrupt-player-udp.py --netns
No host network mutation: the child rejects the parent namespace and any
namespace already containing an interface other than loopback.
"""
import argparse
import json
import os
from pathlib import Path
import select
import signal
import socket
import subprocess
import sys
import time

PROBE = Path(__file__).with_name('interrupt-player-udp.py')
MAC = 'dc:b4:d9:02:d1:dc'


def command(*args):
    return subprocess.check_output(list(args), text=True)


def tables():
    return [x['table']['name'] for x in json.loads(command('nft', '-j', 'list', 'tables'))
            .get('nftables', []) if 'table' in x]


def launch(duration=1):
    process = subprocess.Popen([sys.executable, str(PROBE), '--player-ip', '10.42.0.37',
                                '--expected-mac', MAC, '--duration', str(duration), '--apply'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, bufsize=1)
    # Read one byte at a time to avoid TextIO buffering hiding a second line
    # from select while still bounding a failed start.
    lines = []
    line = bytearray()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if not select.select([process.stdout], [], [], 0.1)[0]:
            continue
        data = os.read(process.stdout.fileno(), 1)
        if not data:
            break
        if data != b'\n':
            line.extend(data)
            continue
        event = json.loads(line)
        line.clear()
        lines.append(event)
        if event['event'] == 'blocked':
            return process, event['table'], lines
    process.kill()
    out, err = process.communicate(timeout=3)
    raise AssertionError(f'probe did not start: {lines} {out} {err}')


def outbound_count(table):
    state = json.loads(command('nft', '-j', 'list', 'table', 'inet', table))
    return sum(expr['counter']['packets'] for item in state.get('nftables', [])
               if item.get('rule', {}).get('chain') == 'outgoing'
               for expr in item['rule'].get('expr', []) if 'counter' in expr)


def send_blocked(sock, payload):
    try:
        sock.sendto(payload, ('10.42.0.37', 4242))
    except PermissionError:
        # nft local-output DROP can return EPERM to the sending socket.
        pass


def run_isolated(parent_namespace):
    if os.stat('/proc/self/ns/net').st_ino == parent_namespace:
        raise RuntimeError('refusing to run in the parent network namespace')
    links = json.loads(command('ip', '-j', 'link', 'show'))
    if {link['ifname'] for link in links} != {'lo'}:
        raise RuntimeError('network namespace is not empty')
    if tables():
        raise RuntimeError('network namespace already contains nft tables')
    command('ip', 'link', 'add', 'ap0', 'type', 'dummy')
    command('ip', 'addr', 'add', '10.42.0.1/24', 'dev', 'ap0')
    command('ip', 'link', 'set', 'ap0', 'up')
    command('ip', 'neigh', 'add', '10.42.0.37', 'lladdr', MAC,
            'dev', 'ap0', 'nud', 'permanent')
    command('ip', 'neigh', 'add', '10.42.0.38', 'lladdr', '02:00:00:00:00:38',
            'dev', 'ap0', 'nud', 'permanent')
    base = [sys.executable, str(PROBE), '--player-ip', '10.42.0.37', '--expected-mac', MAC]
    dry = json.loads(command(*base))
    assert not dry['apply'] and tables() == []
    invalid = subprocess.run(base + ['--duration', '61'], capture_output=True)
    assert invalid.returncode != 0 and tables() == []
    wrong = subprocess.run(base[:-1] + ['00:00:00:00:00:00', '--apply'], capture_output=True)
    assert wrong.returncode != 0 and tables() == []
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('10.42.0.1', 4242))
    process, table, _ = launch()
    send_blocked(sock, b'target')
    sock.sendto(b'other-player', ('10.42.0.38', 4242))
    assert outbound_count(table) == 1
    out, err = process.communicate(timeout=5)
    assert process.returncode == 0, err
    assert any(json.loads(line)['event'] == 'cleaned' for line in out.splitlines())
    assert tables() == []
    print('PASS dry-run, input guard, IP/MAC guard, single-player counter, normal cleanup')

    process, table, _ = launch(2)
    process.send_signal(signal.SIGTERM)
    out, _ = process.communicate(timeout=5)
    assert process.returncode != 0
    assert any(json.loads(line)['event'] == 'cleaned' for line in out.splitlines())
    assert tables() == []
    print('PASS SIGTERM finally cleanup')

    # Inject the exact reviewed race: the kernel commits the nft transaction,
    # then Python receives an exception before subprocess.run returns.
    inject = """import runpy, subprocess, sys
original = subprocess.run
def run(*args, **kwargs):
    result = original(*args, **kwargs)
    if args[0] == ['nft', '-f', '-']:
        raise InterruptedError('injected immediately after nft apply')
    return result
subprocess.run = run
script = sys.argv[1]
sys.argv = sys.argv[1:]
runpy.run_path(script, run_name='__main__')
"""
    raced = subprocess.run([sys.executable, '-c', inject, str(PROBE),
                            '--player-ip', '10.42.0.37', '--expected-mac', MAC,
                            '--duration', '2', '--apply'], capture_output=True, text=True, timeout=5)
    assert raced.returncode != 0 and tables() == []
    assert any(json.loads(line)['event'] == 'cleaned' for line in raced.stdout.splitlines())
    print('PASS immediate post-apply exception cleanup')

    process, table, _ = launch(2)
    send_blocked(sock, b'before-kill')
    assert outbound_count(table) == 1
    process.kill()
    process.communicate(timeout=3)
    time.sleep(2.2)
    state = json.loads(command('nft', '-j', 'list', 'set', 'inet', table, 'player'))
    assert not any(item.get('set', {}).get('elem') for item in state.get('nftables', []))
    sock.sendto(b'after-expiry', ('10.42.0.37', 4242))
    assert outbound_count(table) == 1  # no new drop after the kernel timeout
    command('nft', 'delete', 'table', 'inet', table)
    assert tables() == []
    sock.close()
    print('PASS SIGKILL kernel timeout restores traffic without finally')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--netns', action='store_true')
    parser.add_argument('--inside-parent-netns', type=int)
    args = parser.parse_args()
    if args.inside_parent_netns is not None:
        run_isolated(args.inside_parent_netns)
    elif args.netns:
        parent = os.stat('/proc/self/ns/net').st_ino
        subprocess.run(['unshare', '-n', sys.executable, str(Path(__file__).resolve()),
                        '--inside-parent-netns', str(parent)], check=True)
    else:
        parser.error('use --netns to run only in a new isolated network namespace')


if __name__ == '__main__':
    main()
