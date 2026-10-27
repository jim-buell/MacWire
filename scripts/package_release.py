#!/usr/bin/env python3
"""Create a source release without local configuration or cached content."""
import argparse
import hashlib
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def release_files():
    """Yield only reviewed manifest entries; never discover release files by suffix."""
    manifest = ROOT / "scripts/release_manifest.txt"
    if any(path.is_symlink() for path in (manifest, manifest.parent)):
        raise ValueError("Release manifest must not be a symlink")
    seen = set()
    for line in manifest.read_text().splitlines():
        name = line.strip()
        if not name or name.startswith("#"):
            continue
        relative = Path(name)
        if (relative.is_absolute() or ".." in relative.parts
                or relative.as_posix() != name or name in seen):
            raise ValueError(f"Invalid or duplicate release manifest entry: {name}")
        seen.add(name)
        path = ROOT / relative
        if any(parent.is_symlink() for parent in (path, *path.parents)
               if parent != ROOT and ROOT in parent.parents):
            continue
        if not path.is_file():
            raise ValueError(f"Missing release manifest file: {name}")
        yield path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--client-bin", type=Path, help="Optional current hardware MacBinary")
    parser.add_argument("--client-disk", type=Path, help="Optional matching 800 KB HFS application disk")
    parser.add_argument("--classic-only", action="store_true",
                        help="Package both classic downloads with their notices, without source")
    args = parser.parse_args()
    if args.classic_only and not (args.client_bin and args.client_disk):
        parser.error("--classic-only requires --client-bin and --client-disk")
    if args.client_disk:
        if not args.client_disk.is_file() or args.client_disk.suffix.lower() != ".dsk":
            parser.error("--client-disk must be an existing .dsk file")
        disk = args.client_disk.read_bytes()
        if len(disk) != 819200 or disk[1024:1026] != b"BD":
            parser.error("Client disk must be an 800 KB HFS image")
    if args.client_bin:
        if not args.client_bin.is_file() or args.client_bin.suffix.lower() != ".bin":
            parser.error("--client-bin must be an existing .bin file")
        data = args.client_bin.read_bytes()
        if len(data) < 128 or data[65:69] != b"APPL":
            parser.error("Client binary must be a MacBinary application")
        resource_length = int.from_bytes(data[87:91], "big")
        resource_start = 128 + ((int.from_bytes(data[83:87], "big") + 127) // 128) * 128
        if not resource_length or resource_start + resource_length > len(data):
            parser.error("Client binary has a missing or truncated resource fork")
    files = list(release_files())
    if args.classic_only:
        files = [path for path in files if path.relative_to(ROOT).as_posix() in
                 {"LICENSE", "THIRD_PARTY.md", "SECURITY.md", "backend/IMAGE_ALGORITHMS.md"}
                 or path.relative_to(ROOT).as_posix().startswith(("client/licenses/", "backend/licenses/"))]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, "MacWire/" + path.relative_to(ROOT).as_posix())
        if args.client_bin:
            archive.write(args.client_bin, "MacWire/downloads/MacWire.bin")
        if args.client_disk:
            archive.write(args.client_disk, "MacWire/downloads/MacWire.dsk")
        if args.classic_only:
            archive.writestr("MacWire/README.txt", (
                "MacWire classic application - release candidate\n\n"
                "Use the matching MacWire Helper source from the same release.\n"
                "Transfer MacWire.bin in binary mode and decode MacBinary with both\n"
                "forks preserved, or mount MacWire.dsk as an HFS application disk.\n"
                "The disk is not bootable and includes no system software or MacTCP.\n"
                "Keep LICENSE, THIRD_PARTY.md and all license files with these downloads.\n"
                "Notices accompany the disk in this ZIP; they are not inside the disk.\n"
                "The packager checks structure, not binary/disk equivalence or hardware.\n"
            ))
    checksum = hashlib.sha256(args.output.read_bytes()).hexdigest()
    args.output.with_suffix(args.output.suffix + ".sha256").write_text(
        f"{checksum}  {args.output.name}\n")
    print(f"Created {args.output}")


if __name__ == "__main__":
    main()
