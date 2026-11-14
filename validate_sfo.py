#!/usr/bin/env python3
"""Validate the generated PSF metadata values without proprietary tools."""
import struct
import re
import sys


def read_sfo(path):
    data = open(path, "rb").read()
    if len(data) < 20 or data[:4] != b"\x00PSF":
        raise ValueError("Missing PSF signature")
    _, key_offset, value_offset, count = struct.unpack_from("<IIII", data, 4)
    values = {}
    for index in range(count):
        entry = 20 + index * 16
        key_rel, value_type, length, max_length, value_rel = struct.unpack_from("<HHIII", data, entry)
        key_start = key_offset + key_rel
        key_end = data.index(b"\0", key_start)
        key = data[key_start:key_end].decode("utf-8")
        start = value_offset + value_rel
        raw = data[start:start + length]
        # PSF types: 0x0004 = UTF-8 special, 0x0204 = UTF-8 text (NUL padded),
        # 0x0404 = 32-bit integer.
        if value_type in (0x0004, 0x0204):
            values[key] = raw.rstrip(b"\0").decode("utf-8")
        elif value_type == 0x0404 and length in (1, 2, 4, 8):
            values[key] = int.from_bytes(raw, "little")
        else:
            values[key] = raw
    return values


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: validate_sfo.py sce_sys/param.sfo")
    values = read_sfo(sys.argv[1])
    expected = {
        "TITLE": "PhoneCast",
        "TITLE_ID": "PHON00001",
        "CONTENT_ID": "IV0000-PHON00001_00-PHONECAST0000001",
        "CATEGORY": "gd",
    }
    for key, value in expected.items():
        if values.get(key) != value:
            raise SystemExit(f"{key} mismatch: expected {value!r}, got {values.get(key)!r}")
    if not re.fullmatch(r"[A-Z]{4}[0-9]{5}", values["TITLE_ID"]):
        raise SystemExit(f"TITLE_ID must be four letters followed by five digits: {values['TITLE_ID']!r}")
    if f"-{values['TITLE_ID']}_" not in values["CONTENT_ID"]:
        raise SystemExit("CONTENT_ID does not contain the matching TITLE_ID")
    print("PSF metadata validated:", ", ".join(f"{key}={values[key]}" for key in expected))


if __name__ == "__main__":
    main()

