#!/usr/bin/env python3
"""Read-only AF_PACKET decoder for Gridopoly UDP frame diagnostics.

Run as root on the Raspberry Pi. The tool never verifies or prints keys/tags;
it only decodes protocol headers and selected public metadata from packets
on the dedicated player AP. Captured frames are observations, not proof of
server authentication or acceptance; correlate ActionResult and authority state.
"""

from __future__ import annotations

import argparse
import collections
import socket
import struct
import time


UDP_PORT = 4242
UDP_ENVELOPE_MAGIC = 0x31555047
FRAME_MAGIC = 0x44495247
ENVELOPE_HEADER_SIZE = 48
FRAME_HEADER_SIZE = 32
TYPE_NAMES = {
    0x01: "Discover",
    0x02: "PairRequest",
    0x03: "PairAccept",
    0x04: "Heartbeat",
    0x10: "StateSnapshot",
    0x11: "GameEvent",
    0x12: "AuthoritySnapshot",
    0x13: "RosterSnapshot",
    0x20: "ActionRequest",
    0x21: "ActionResult",
    0x22: "Ack",
    0x23: "Error",
    0x24: "PlayerDetailRequest",
    0x25: "PlayerDetailResponse",
    0x26: "PlayerCardEvent",
}


def ipv4_text(raw: bytes) -> str:
    return socket.inet_ntoa(raw)


def decode_packet(packet: bytes) -> dict[str, object] | None:
    if len(packet) < 14 or struct.unpack_from("!H", packet, 12)[0] != 0x0800:
        return None
    ip_offset = 14
    ihl = (packet[ip_offset] & 0x0F) * 4
    if ihl < 20 or len(packet) < ip_offset + ihl + 8 or packet[ip_offset + 9] != 17:
        return None
    udp_offset = ip_offset + ihl
    source_port, destination_port, udp_length = struct.unpack_from("!HHH", packet, udp_offset)
    if UDP_PORT not in (source_port, destination_port) or udp_length < 8:
        return None
    payload = packet[udp_offset + 8: udp_offset + udp_length]
    if len(payload) < ENVELOPE_HEADER_SIZE + FRAME_HEADER_SIZE:
        return None
    if struct.unpack_from("<I", payload, 0)[0] != UDP_ENVELOPE_MAGIC:
        return None
    frame = payload[ENVELOPE_HEADER_SIZE:]
    if struct.unpack_from("<I", frame, 0)[0] != FRAME_MAGIC:
        return None
    payload_length = struct.unpack_from("<H", frame, 24)[0]
    if len(frame) != FRAME_HEADER_SIZE + payload_length:
        return None
    return {
        "source": ipv4_text(packet[ip_offset + 12:ip_offset + 16]),
        "destination": ipv4_text(packet[ip_offset + 16:ip_offset + 20]),
        "type": frame[5],
        "flags": struct.unpack_from("<H", frame, 6)[0],
        "sequence": struct.unpack_from("<I", frame, 8)[0],
        "acknowledgement": struct.unpack_from("<I", frame, 12)[0],
        "room": struct.unpack_from("<I", frame, 16)[0],
        "device": struct.unpack_from("<I", frame, 20)[0],
        "payload": frame[FRAME_HEADER_SIZE:],
        "datagram_length": len(payload),
    }


def describe_detail(frame: dict[str, object]) -> str:
    payload = frame["payload"]
    assert isinstance(payload, bytes)
    if frame["type"] == 0x20 and len(payload) == 12 and payload[0] == 1:
        argument, expected_version = struct.unpack_from("<iI", payload, 4)
        return (f"action={payload[1]} player={payload[2]} asset={payload[3]} "
                f"argument={argument} expectedVersion={expected_version}")
    if frame["type"] == 0x21 and len(payload) == 12 and payload[0] == 1:
        state_version, request_sequence = struct.unpack_from("<II", payload, 4)
        return (f"result={payload[1]} player={payload[2]} "
                f"stateVersion={state_version} requestSequence={request_sequence}")
    if frame["type"] == 0x24 and len(payload) == 12:
        request_id, expected_version = struct.unpack_from("<II", payload, 4)
        return (f"requestId={request_id} target={payload[1]} "
                f"expectedVersion={expected_version}")
    if frame["type"] == 0x25 and len(payload) >= 20:
        request_id, state_version, cash = struct.unpack_from("<IIi", payload, 4)
        return (f"requestId={request_id} target={payload[2]} stateVersion={state_version} "
                f"position={payload[3]} cash={cash} assets={payload[16]} "
                f"ledger={payload[17]} totalOwned={payload[18]} flags={payload[1]} "
                f"bytes={len(payload)}")
    return ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--interface", default="ap0")
    parser.add_argument("--duration", type=float, default=90.0)
    parser.add_argument("--actions", action="store_true",
                        help="Also timestamp ActionRequest/ActionResult for movement-cue correlation")
    args = parser.parse_args()

    counts: collections.Counter[int] = collections.Counter()
    detail_frames = 0
    action_frames = 0
    started = time.monotonic()
    # ETH_P_ALL also observes locally transmitted ActionResult/Ack packets.
    # decode_packet keeps only IPv4 Gridopoly UDP traffic.
    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0003))
    sock.bind((args.interface, 0))
    sock.settimeout(1.0)
    try:
        while time.monotonic() - started < args.duration:
            try:
                packet = sock.recv(2048)
            except TimeoutError:
                continue
            frame = decode_packet(packet)
            if frame is None:
                continue
            message_type = int(frame["type"])
            counts[message_type] += 1
            if message_type in (0x24, 0x25):
                detail_frames += 1
            elif args.actions and message_type in (0x20, 0x21):
                action_frames += 1
            else:
                continue
            print(
                f"GRIDOPOLY_UDP_FRAME epochMs={time.time_ns() // 1_000_000} type={TYPE_NAMES.get(message_type, hex(message_type))} "
                f"src={frame['source']} dst={frame['destination']} room={frame['room']} "
                f"seq={frame['sequence']} ack={frame['acknowledgement']} "
                f"flags=0x{int(frame['flags']):04x} {describe_detail(frame)}",
                flush=True,
            )
    finally:
        sock.close()

    summary = " ".join(
        f"{TYPE_NAMES.get(message_type, hex(message_type))}={count}"
        for message_type, count in sorted(counts.items())
    )
    print(f"GRIDOPOLY_UDP_SNIFF_COMPLETE detailFrames={detail_frames} actionFrames={action_frames} {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
