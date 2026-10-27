# Build MacWire from source

To run MacWire Helper, follow the [README](README.md). Building the classic
application additionally requires Retro68 with the m68k-apple-macos toolchain,
CMake, Make, Python 3, hfsutils (`hformat`, `hcopy`, `humount`), and `truncate`
on your PATH. These tools are installed separately.

## Classic application

From the project root, replace the example toolchain path with your installation:

```sh
cmake -S client -B build/classic \
  -DCMAKE_TOOLCHAIN_FILE=/absolute/path/to/retro68/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DRSS_DEMO_TRANSPORT=OFF \
  -DTICKERTAPE_UI_SMOKE=OFF \
  -DTICKERTAPE_RENDER_ONLY=OFF \
  -DTICKERTAPE_PACKET_HEADER=
cmake --build build/classic -j4
```

The build produces `build/classic/MacWire.bin` and a timestamped 800 KB HFS disk
image. Packaging checks that the application data and resource forks survive an
HFS round trip. HFS utilities share mount state in `~/.hcwd`; run packaging
sequentially. A successful build does not verify native drawing or MacTCP behavior
on real hardware. See [classic Mac setup](client/SETUP.md) for transfer instructions.

## Assets and tests

The source includes original PNG icon/cursor assets and their generated resources.
After changing assets, regenerate the corresponding resources with Python and
Pillow from the environment created in the README:

```sh
.venv/bin/python client/generate_icons.py
.venv/bin/python client/tools/generate_wait_cursor.py
```

Run the included automated suites with a host C compiler available:

```sh
.venv/bin/python -m unittest discover -s backend -p 'test_*.py'
.venv/bin/python -m unittest discover -s client/tests -p 'test_*.py'
```

Tests use synthetic data and local HTTP listeners. No live feeds or downloaded
publisher articles are required.

## Source archive

```sh
python3 scripts/package_release.py --output dist/public/MacWire-source.zip
```

The packager includes only files listed in `scripts/release_manifest.txt` and
refuses to overwrite an existing archive. Review additions to that manifest before
packaging. Local configuration, caches, emulator settings, internal review tools,
and planning documents are excluded. Use `--client-bin /absolute/path/to/MacWire.bin`
to include a matching MacBinary application; this validates its basic structure,
not its hardware behavior.

## Classic download bundle

Package the matching MacBinary and application-only disk together with the root
license and third-party notices. Keep the ZIP's accompanying notices with the
downloads; the notices are outside the HFS image. The packager leaves both supplied
files unchanged and checks their structure, not their equivalence.

```sh
python3 scripts/package_release.py --classic-only \
  --client-bin dist/public/MacWire-1.0-article-loading-fix.bin \
  --client-disk dist/public/MacWire-1.0-article-loading-fix.dsk \
  --output dist/public/MacWire-1.0-classic-candidate.zip
```

Every generated ZIP gets a sibling `.zip.sha256`. Verify it from the output
directory with `shasum -a 256 -c <archive>.zip.sha256`. Source archives may also
include `--client-disk` when distributing both downloads with source. Generated
downloads remain outside Git. Before publishing, test the matching client and
Helper together, verify the checksums and notices, and audit the pinned
dependencies against current advisory data.
