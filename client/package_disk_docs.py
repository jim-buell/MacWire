#!/usr/bin/env python3
"""Prepare MacRoman/CR documents with native SimpleText styles."""
import argparse
from binascii import crc_hqx
from datetime import datetime
from pathlib import Path
import subprocess
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
TEXT_LIMIT = 30000  # Leave room below TeachText's roughly 32 KB text limit.


def macbinary(name, data, resource=b'', type_creator=b'TEXTttxt'):
    encoded = name.encode('mac_roman')
    if not 1 <= len(encoded) <= 31 or ':' in name:
        raise ValueError('Invalid HFS document name')
    if len(type_creator) != 8:
        raise ValueError('MacBinary type and creator must total eight bytes')
    header = bytearray(128)
    header[1] = len(encoded)
    header[2:2 + len(encoded)] = encoded
    header[65:73] = type_creator
    header[83:87] = len(data).to_bytes(4, 'big')
    header[87:91] = len(resource).to_bytes(4, 'big')
    timestamp = int((datetime.now() - datetime(1904, 1, 1)).total_seconds())
    for offset in (91, 95):
        header[offset:offset + 4] = timestamp.to_bytes(4, 'big')
    header[122:124] = bytes((129, 129))  # MacBinary II.
    header[124:126] = crc_hqx(header[:124], 0).to_bytes(2, 'big')
    return (bytes(header) + data + bytes((-len(data)) % 128) +
            resource + bytes((-len(resource)) % 128))


def styled_text(markdown):
    """Render the Read Me's headings/emphasis/code, leaving prose unwrapped."""
    # Font IDs: Chicago=0, Geneva=3, Monaco=4. Face bits: bold=1, italic=2.
    body = (3, 12, 0)
    data = bytearray()
    runs = []

    def append(text, style):
        if not text:
            return
        if not runs or runs[-1][1] != style:
            runs.append((len(data), style))
        data.extend(text.encode('mac_roman'))

    def inline(text, base):
        face = base[2]
        for token in re.split(r'(\*\*|\*|\[[^]]+\]\([^)]+\))', text):
            if token == '**':
                face ^= 1
            elif token == '*':
                face ^= 2
            else:
                link = re.fullmatch(r'\[([^]]+)\]\(([^)]+)\)', token)
                if link:
                    append(link[1], (base[0], base[1], face | 4))
                    append(' (' + link[2] + ')', (base[0], base[1], face))
                else:
                    append(token, (base[0], base[1], face))

    code = False
    for line in markdown.splitlines():
        if line.startswith('```'):
            code = not code
            continue
        if code:
            append(line.rstrip() + '\r', (4, 9, 0))
            continue
        line = line.strip()
        if not line:
            append('\r', body)
        elif line.startswith('# '):
            inline(line[2:], (0, 18, 0))
            append('\r', body)
        elif line.startswith('## '):
            inline(line[3:], (3, 12, 1))
            append('\r', body)
        else:
            inline(line, body)
            append('\r', body)
    if len(data) > TEXT_LIMIT:
        raise ValueError('Read Me exceeds the classic text-editor limit')
    # Apple's TextEdit StScrpRec / ScrpSTElement on-disk format.
    style_data = bytearray(struct.pack('>H', len(runs)))
    for offset, (font, size, face) in runs:
        style_data.extend(struct.pack('>IhhhBBhHHH', offset, size + 3, size,
                                      font, face, 0, size, 0, 0, 0))
    return bytes(data), resource_fork(b'styl', 128, bytes(style_data))


def resource_fork(kind, resource_id, payload):
    """Write a Resource Manager file with one unnamed resource."""
    contents = struct.pack('>I', len(payload)) + payload
    header = struct.pack('>IIII', 256, 256 + len(contents), len(contents), 50)
    # Map header, type-list offset, name-list offset, one type, one reference.
    resource_map = (header + bytes(8) + struct.pack('>HHH', 28, 50, 0) +
                    kind + struct.pack('>HH', 0, 10) +
                    struct.pack('>hHII', resource_id, 0xFFFF, 0, 0))
    return header + bytes(240) + contents + resource_map


def text_parts(text):
    """Split without dropping any notice text; join parts to recover the whole."""
    data = text.replace('\r\n', '\n').replace('\r', '\n').replace('\n', '\r').encode('mac_roman')
    parts = []
    while len(data) > TEXT_LIMIT:
        end = data.rfind(b'\r', 0, TEXT_LIMIT)
        end = end + 1 if end >= 0 else TEXT_LIMIT
        parts.append(data[:end])
        data = data[end:]
    parts.append(data)
    return parts


def prepare(output):
    sources = [('Read Me', 'client/ReadMe.md'),
               ('Licenses/MacWire MIT', 'LICENSE'),
               ('Licenses/Third Party', 'THIRD_PARTY.md'),
               ('Licenses/Multiversal MIT', 'client/licenses/Multiversal-MIT.txt'),
               ('Licenses/GCC Exception', 'client/licenses/GCC-Runtime-Exception.txt'),
               ('Licenses/GPL 3', 'client/licenses/GPL-3.0.txt'),
               ('Licenses/newlib', 'client/licenses/newlib-4.6.0.txt')]
    documents = []
    for title, source in sources:
        text = (ROOT / source).read_text()
        if title == 'Read Me':
            data, resource = styled_text(text)
            parts = [data]
        else:
            resource = b''
            parts = text_parts(text)
        for number, data in enumerate(parts, 1):
            relative = Path(title + (f' part {number}' if len(parts) > 1 else ''))
            raw = output / relative
            raw.parent.mkdir(parents=True, exist_ok=True)
            raw.write_bytes(data)
            binary = raw.with_name(raw.name + '.bin')
            binary.write_bytes(macbinary(relative.name, data, resource))
            documents.append((relative, binary, data))
    return documents


def copy_to_hfs(documents):
    subprocess.run(['hmkdir', ':Licenses'], check=True)
    for relative, binary, data in documents:
        target = ':' + ':'.join(relative.parts)
        subprocess.run(['hcopy', '-m', str(binary), target], check=True)
        # Reading MacBinary back checks the data, name, and Finder association.
        copied = subprocess.run(['hcopy', '-m', target, '-'], check=True,
                                capture_output=True).stdout
        expected = binary.read_bytes()
        length = int.from_bytes(copied[83:87], 'big')
        resource_length = int.from_bytes(expected[87:91], 'big')
        resource_start = 128 + ((len(data) + 127) // 128) * 128
        if (copied[65:73] != b'TEXTttxt' or
                copied[2:2 + copied[1]].decode('mac_roman') != relative.name or
                copied[128:128 + length] != data or
                int.from_bytes(copied[87:91], 'big') != resource_length or
                copied[resource_start:resource_start + resource_length] !=
                expected[resource_start:resource_start + resource_length]):
            raise ValueError(f'HFS document round trip failed: {target}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--copy-to-hfs', action='store_true')
    args = parser.parse_args()
    documents = prepare(args.output)
    if args.copy_to_hfs:
        copy_to_hfs(documents)
    print(f'Prepared {len(documents)} classic text documents in {args.output}')


if __name__ == '__main__':
    main()
