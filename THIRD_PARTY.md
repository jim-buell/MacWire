# MacWire licensing and third-party notices

MacWire and MacWire Helper project source and original project assets are licensed
under the [MIT License](LICENSE), copyright 2026 Jim Buell. Third-party components
retain their own licenses; the project license does not relicense them.

## Python dependencies

The source distribution installs dependencies separately through
`backend/requirements.txt`; it does not bundle Python, site-packages, or wheels.
Notices for the pinned dependencies are retained under `backend/licenses/`.
The Pillow, feedparser and feedparser-sgmllib notices match the installed
distributions listed below; the pypng notice retains its module copyright and
MIT license text.

| Component | Pinned version | License | Notice |
| --- | --- | --- | --- |
| feedparser | 6.0.14 | BSD-2-Clause | [feedparser](backend/licenses/feedparser-BSD-2-Clause.txt) |
| feedparser-sgmllib | 2.1.0 | PSF-2.0 | [parser dependency](backend/licenses/feedparser-sgmllib-PSF-2.0.txt) |
| Pillow | 13.0.0 | MIT-CMU | [Pillow](backend/licenses/Pillow-MIT-CMU.txt) |
| pypng, used by tests | 0.20220715.0 | MIT | [pypng](backend/licenses/pypng-MIT.txt) |

If a future Helper application bundles Python or dependency binaries, retain their
complete notices, including codec libraries supplied with Pillow wheels. These
notice files cover the dependencies above and do not replace that future review.

## Classic application build and runtime

The reviewed hardware build uses Retro68, the open-source Multiversal Interfaces,
newlib 4.6.0, and GCC runtime support. Its interface headers include `Multiverse.h`;
the installed `libInterface.a` members correspond to Multiversal's interface/glue
implementation. MacWire's MacTCP transport calls the installed Device Manager ABI
directly and does not link an Apple MacTCP SDK object library.

Retained notices under `client/licenses/` are:

- [Multiversal MIT notice](client/licenses/Multiversal-MIT.txt), copyright 2019
  Wolfgang Thaller, copied from the local Multiversal source checkout.
- [GPLv3 text](client/licenses/GPL-3.0.txt) and
  [GCC Runtime Library Exception 3.1](client/licenses/GCC-Runtime-Exception.txt),
  covering applicable GCC/libretro runtime components. Retro68 libretro's source
  explicitly grants this exception; it permits eligible compiled applications to
  use a different license. MacWire project source remains MIT.
- [newlib notices](client/licenses/newlib-4.6.0.txt), copied from `COPYING.NEWLIB`
  in the official `newlib-4.6.0.20260123` release archive. newlib contains code from
  multiple authors under the terms listed there; preserve the applicable notices
  with the classic binary.

See [Retro68's licensing and interface documentation](https://github.com/autc04/Retro68#license),
[Multiversal](https://github.com/autc04/multiversal), and the
[official newlib releases](https://sourceware.org/ftp/newlib/).
The compiler, SDK/toolchain archives, and HFS utilities are build tools and are not
included in the release. Keep these notices with any separately packaged classic
binary; users should receive the root LICENSE and THIRD_PARTY.md as well.

## Replaced image routines

The grayscale, Atkinson dithering, and PackBits routines formerly adapted from
`macpaint_file` have been replaced by project implementations in
`backend/image_algorithms.py`. Algorithm references, compatibility choices, and
validation are recorded in [image algorithm notes](backend/IMAGE_ALGORITHMS.md).
No `macpaint_file` import or former implementation remains in the current image
pipeline. The distribution contains no old snapshots or review photographs.

## Material outside the distribution

The curated release does not include Apple ROMs, operating-system files, MacTCP,
Apple's Universal Interfaces or MPW, installed fonts, or emulator/system disk
images. The HFS deliverable is an application-only disk, not a bootable System disk.
The classic application uses the OS, fonts, and drivers already installed on the
user's machine; their licenses are separate.

Public RSS URLs are configuration, not a grant of rights in publisher content.
Articles and photos remain their publishers' material and are not covered by
MacWire's MIT license. Personal/subscriber feed URLs, cached articles/images, and
local review photographs are excluded from the source package.
