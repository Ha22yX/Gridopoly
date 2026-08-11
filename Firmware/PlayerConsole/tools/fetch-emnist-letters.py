#!/usr/bin/env python3
"""Fetch only the EMNIST Letters members from NIST's aggregate ZIP."""

from __future__ import annotations

import argparse
import binascii
import struct
import urllib.request
import zlib
from dataclasses import dataclass
from pathlib import Path


URL = "https://biometrics.nist.gov/cs_links/EMNIST/gzip.zip"
MEMBERS = (
    "gzip/emnist-letters-test-images-idx3-ubyte.gz",
    "gzip/emnist-letters-test-labels-idx1-ubyte.gz",
    "gzip/emnist-letters-train-images-idx3-ubyte.gz",
    "gzip/emnist-letters-train-labels-idx1-ubyte.gz",
)


@dataclass(frozen=True)
class ZipMember:
    name: str
    method: int
    crc32: int
    compressed_size: int
    uncompressed_size: int
    local_offset: int


def fetch_range(start: int, end: int) -> bytes:
    request = urllib.request.Request(
        URL,
        headers={"Range": f"bytes={start}-{end}", "User-Agent": "Gridopoly/1"},
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read()
    expected = end - start + 1
    if len(data) != expected:
        raise RuntimeError(f"range {start}-{end}: got {len(data)}, expected {expected}")
    return data


def remote_size() -> int:
    request = urllib.request.Request(URL, method="HEAD", headers={"User-Agent": "Gridopoly/1"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return int(response.headers["Content-Length"])


def read_directory() -> dict[str, ZipMember]:
    size = remote_size()
    tail_size = min(size, 65557)
    tail = fetch_range(size - tail_size, size - 1)
    eocd = tail.rfind(b"PK\x05\x06")
    if eocd < 0:
        raise RuntimeError("ZIP end-of-central-directory not found")
    _, _, _, _, count, directory_size, directory_offset, _ = struct.unpack_from(
        "<4s4H2LH", tail, eocd
    )
    directory = fetch_range(directory_offset, directory_offset + directory_size - 1)
    result: dict[str, ZipMember] = {}
    cursor = 0
    for _ in range(count):
        fields = struct.unpack_from("<4s6H3L5H2L", directory, cursor)
        if fields[0] != b"PK\x01\x02":
            raise RuntimeError("invalid ZIP central directory")
        name_len, extra_len, comment_len = fields[10:13]
        name = directory[cursor + 46 : cursor + 46 + name_len].decode("utf-8")
        result[name] = ZipMember(
            name=name,
            method=fields[4],
            crc32=fields[7],
            compressed_size=fields[8],
            uncompressed_size=fields[9],
            local_offset=fields[-1],
        )
        cursor += 46 + name_len + extra_len + comment_len
    return result


def fetch_member(member: ZipMember) -> bytes:
    header = fetch_range(member.local_offset, member.local_offset + 29)
    fields = struct.unpack("<4s5H3L2H", header)
    if fields[0] != b"PK\x03\x04":
        raise RuntimeError(f"invalid local header for {member.name}")
    name_len, extra_len = fields[-2:]
    payload_start = member.local_offset + 30 + name_len + extra_len
    payload = fetch_range(payload_start, payload_start + member.compressed_size - 1)
    if member.method == 0:
        decoded = payload
    elif member.method == 8:
        decoded = zlib.decompress(payload, -15)
    else:
        raise RuntimeError(f"unsupported ZIP method {member.method}")
    if len(decoded) != member.uncompressed_size:
        raise RuntimeError(f"bad size for {member.name}")
    if binascii.crc32(decoded) & 0xFFFFFFFF != member.crc32:
        raise RuntimeError(f"bad CRC for {member.name}")
    return decoded


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    directory = read_directory()
    for name in MEMBERS:
        destination = args.output / Path(name).name
        if destination.is_file():
            print(f"exists {destination}")
            continue
        destination.write_bytes(fetch_member(directory[name]))
        print(f"fetched {destination} ({destination.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
