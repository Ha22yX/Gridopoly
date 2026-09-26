"""The existing ESP32-S3 16MB layouts only; configuration migration is separate."""
import hashlib
import struct

FLASH_BYTES = 16 * 1024 * 1024
SECTOR = 4096
ALLOWED_OFFSETS = {0, 0x8000, 0xE000, 0x10000}
ENTRY = struct.Struct("<HBBII16sI")


def parse_partitions(data: bytes) -> list[tuple]:
    if not data or len(data) > SECTOR or len(data) % ENTRY.size:
        raise ValueError("invalid partition table length")
    partitions = []
    names = set()
    for cursor in range(0, len(data), ENTRY.size):
        record = data[cursor:cursor + ENTRY.size]
        if record[:2] == b"\xeb\xeb":
            if (record[:16] != b"\xeb\xeb" + b"\xff" * 14 or
                    record[16:] != hashlib.md5(data[:cursor]).digest() or
                    any(byte != 255 for byte in data[cursor + ENTRY.size:])):
                raise ValueError("partition table MD5 or padding is invalid")
            if not partitions:
                raise ValueError("empty partition table")
            return sorted(partitions)
        magic, kind, subtype, offset, size, label, flags = ENTRY.unpack(record)
        name = label.split(b"\0", 1)[0]
        if (magic != 0x50AA or kind not in (0, 1) or flags != 0 or
                not name or name in names or offset < 0x9000 or
                offset % SECTOR or size == 0 or size % SECTOR or
                offset + size > FLASH_BYTES or (kind == 0 and offset % 0x10000)):
            raise ValueError("unsupported or invalid partition entry")
        if any(offset < p[3] + p[4] and p[3] < offset + size for p in partitions):
            raise ValueError("overlapping partitions")
        names.add(name)
        partitions.append((kind, subtype, name, offset, size, flags))
    raise ValueError("partition table requires a valid MD5 record")


def validate_images(images: list[tuple[int, bytes]]) -> list[tuple]:
    offsets = [offset for offset, _ in images]
    if len(offsets) != len(set(offsets)):
        raise ValueError("overlapping or duplicate flash segments")
    if set(offsets) != ALLOWED_OFFSETS:
        raise ValueError("only bootloader/table/otadata/application segments are allowed")
    blobs = dict(images)
    partitions = parse_partitions(blobs[0x8000])
    apps = [p for p in partitions if p[0:2] == (0, 0x10) and p[3] == 0x10000]
    ota = [p for p in partitions if p[0:2] == (1, 0) and p[3:5] == (0xE000, 0x2000)]
    if len(apps) != 1 or len(ota) != 1:
        raise ValueError("expected app0 at 0x10000 and otadata at 0xe000")
    limits = {0: 0x8000, 0x8000: SECTOR, 0xE000: ota[0][4], 0x10000: apps[0][4]}
    protected = [p for p in partitions if p[0] == 1 and p[1] != 0]
    for offset, data in images:
        erased = (len(data) + SECTOR - 1) // SECTOR * SECTOR
        if not data or erased > limits[offset]:
            raise ValueError(f"segment {offset:#x} exceeds its partition boundary")
        if any(offset < p[3] + p[4] and p[3] < offset + erased for p in protected):
            raise ValueError("flash segment would overwrite a protected data partition")
    return partitions


def check_existing_layout(existing: bytes, images: list[tuple[int, bytes]]) -> None:
    new = validate_images(images)
    if len(existing) != SECTOR:
        raise ValueError("incomplete existing partition-table read")
    if existing == b"\xff" * SECTOR:
        return  # A blank board is the only layout-free case.
    old = parse_partitions(existing)
    if old != new:
        raise ValueError("existing partition layout is incompatible; use a separate migration")
    # Exact layout matching also preserves every old NVS/filesystem range.
