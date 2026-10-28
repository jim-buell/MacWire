#!/usr/bin/env python3
"""Wrap a raw 800 KB HFS floppy in Disk Copy 4.2 format and MacBinary."""
import argparse
from pathlib import Path
import struct

from package_disk_docs import macbinary


def diskcopy_checksum(data):
    """Disk Copy's big-endian word sum with a right rotation after each word."""
    if len(data) % 2:
        raise ValueError('Disk Copy checksum requires an even byte count')
    checksum = 0
    for (word,) in struct.iter_unpack('>H', data):
        checksum = (checksum + word) & 0xFFFFFFFF
        checksum = (checksum >> 1) | ((checksum & 1) << 31)
    return checksum


def diskcopy_image(raw, name='MacWire'):
    """DC42 container for the unchanged HFS sectors; no floppy tag data needed."""
    if len(raw) != 819200 or raw[1024:1026] != b'BD':
        raise ValueError('Expected an 800 KB raw HFS disk image')
    label = name.encode('mac_roman')
    if not 1 <= len(label) <= 63:
        raise ValueError('Disk Copy name must be 1–63 MacRoman bytes')
    header = bytearray(84)
    header[0] = len(label)
    header[1:1 + len(label)] = label
    struct.pack_into('>IIII', header, 64, len(raw), 0, diskcopy_checksum(raw), 0)
    # 800 KB GCR Macintosh media, Disk Copy format marker.
    header[80:84] = bytes.fromhex('01 22 01 00')
    return bytes(header) + raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('raw_disk', type=Path)
    parser.add_argument('--output', required=True, type=Path, help='Destination .image file')
    args = parser.parse_args()
    image = diskcopy_image(args.raw_disk.read_bytes())
    binary_path = args.output.with_name(args.output.name + '.bin')
    if args.output.exists() or binary_path.exists():
        parser.error('Output image or MacBinary already exists; choose a new output path')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    binary = macbinary('MacWire.image', image, type_creator=b'dImgddsk')
    with args.output.open('xb') as file:
        file.write(image)
    with binary_path.open('xb') as file:
        file.write(binary)
    print(f'Created {args.output} and {binary_path}')


if __name__ == '__main__':
    main()
