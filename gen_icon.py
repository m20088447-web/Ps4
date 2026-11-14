#!/usr/bin/env python3
"""Create a simple opaque 24-bit PhoneCast icon without third-party modules."""
import struct
import os
import zlib

SIZE = 512
os.makedirs("sce_sys", exist_ok=True)


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


rows = []
for y in range(SIZE):
    row = bytearray([0])
    for x in range(SIZE):
        # Deep navy field with a cyan screen outline and a simple cast motif.
        color = (8, 18, 29)
        if 92 <= x < 420 and 92 <= y < 354:
            if x < 108 or x >= 404 or y < 108 or y >= 338:
                color = (0, 217, 245)
            else:
                color = (14, 35, 49)
        if 150 <= x < 362 and 372 <= y < 394:
            color = (0, 217, 245)
        # Three broadcast arcs over the lower-left corner.
        dx, dy = x - 156, y - 315
        radius2 = dx * dx + dy * dy
        if dx >= 0 and dy >= 0 and y < 354:
            if 20_000 <= radius2 <= 22_900:
                color = (0, 217, 245)
            elif 7_000 <= radius2 <= 8_400:
                color = (0, 217, 245)
            elif 500 <= radius2 <= 1_250:
                color = (0, 217, 245)
        row.extend(color)
    rows.append(bytes(row))

image = b"".join(rows)
png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(image, 9))
png += chunk(b"IEND", b"")
with open("sce_sys/icon0.png", "wb") as output:
    output.write(png)

