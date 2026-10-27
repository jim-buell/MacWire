#!/usr/bin/env python3
"""Set MacBinary dates to local wall time and repair its header checksum.

Retro68 currently computes the 1904 epoch with tm_isdst=0. During daylight
saving time this makes the app inside an HFS disk appear one hour older than
the disk itself, even though both were produced in the same build.
"""

from binascii import crc_hqx
from datetime import datetime
from pathlib import Path
import sys


def main(path: Path) -> None:
    image = bytearray(path.read_bytes())
    if len(image) < 128 or image[0] != 0:
        raise ValueError(f"{path}: invalid MacBinary header")
    old_crc = int.from_bytes(image[124:126], "big")
    if crc_hqx(image[:124], 0) != old_crc:
        raise ValueError(f"{path}: invalid MacBinary header checksum")

    # Classic Mac file dates are seconds since 1904 in local wall time.
    mac_time = int((datetime.now() - datetime(1904, 1, 1)).total_seconds())
    for offset in (91, 95):
        image[offset : offset + 4] = mac_time.to_bytes(4, "big")
    # Finder must register the BNDL/FREF icon family on a fresh disk.
    # High Finder-flags byte: hasBundle (0x2000), clear hasBeenInited (0x0100).
    image[73] = (image[73] | 0x20) & ~0x01
    image[124:126] = crc_hqx(image[:124], 0).to_bytes(2, "big")
    path.write_bytes(image)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: stamp_macbinary.py APP.bin")
    main(Path(sys.argv[1]))
