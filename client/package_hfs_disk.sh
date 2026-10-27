#!/bin/sh
# Build a fresh 800 KB HFS app floppy from Retro68's MacBinary output.
# Rez's --cc disk can retain an older RSSReader fork across incremental builds.
# The disk image is named with a build timestamp (MacWire-YYYYMMDD-HHMMSS.dsk)
# so successive builds stay unambiguous instead of overwriting a single file.
set -eu

app_bin=$1
disk_base=$2          # base path, e.g. client/build/MacWire.dsk
timestamp=$(date "+%Y%m%d-%H%M%S")
disk_path="${disk_base%.*}-${timestamp}.dsk"
python3 "$(dirname "$0")/stamp_macbinary.py" "$app_bin"
temp_disk=$(mktemp "${disk_path}.tmp.XXXXXX")
roundtrip=$(mktemp "${disk_path}.check.XXXXXX")
docs_dir=$(mktemp -d "${disk_path}.docs.XXXXXX")
trap 'rm -f "$temp_disk" "$roundtrip"; rm -rf "$docs_dir"' EXIT HUP INT TERM

truncate -s 819200 "$temp_disk"
hformat -l 'MacWire' "$temp_disk" >/dev/null
hcopy -m "$app_bin" :MacWire
python3 "$(dirname "$0")/package_disk_docs.py" "$docs_dir" --copy-to-hfs
hcopy -m :MacWire "$roundtrip"

python3 - "$app_bin" "$roundtrip" <<'PY'
import sys
from pathlib import Path

def forks(path):
    image = Path(path).read_bytes()
    if len(image) < 128:
        raise ValueError(f"{path}: truncated MacBinary")
    data_len = int.from_bytes(image[83:87], "big")
    resource_len = int.from_bytes(image[87:91], "big")
    resource_start = 128 + ((data_len + 127) // 128) * 128
    if resource_len == 0 or resource_start + resource_len > len(image):
        raise ValueError(f"{path}: missing or truncated resource fork")
    return (image[128:128 + data_len],
            image[resource_start:resource_start + resource_len])

source, copied = map(forks, sys.argv[1:])
if source != copied:
    raise SystemExit("HFS packaging changed the app's data or resource fork")
for path in sys.argv[1:]:
    image = Path(path).read_bytes()
    if image[2:2 + image[1]] != b"MacWire" or image[65:73] != b"APPLRSSR":
        raise SystemExit("HFS packaging lost the MacWire name or creator")
    if not image[73] & 0x20:
        raise SystemExit("HFS packaging lost the Finder bundle flag")
PY

humount
mv -f "$temp_disk" "$disk_path"
printf '%s\n' "Packaged $disk_path"
