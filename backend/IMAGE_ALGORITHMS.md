# MacWire image algorithms

MacWire Helper uses Pillow for image decoding, EXIF orientation, white-background
alpha compositing, and resizing. `image_algorithms.py` provides MacWire's grayscale,
Atkinson dithering, and PackBits implementations. `image_processor.py` retains the
existing public names so client wire checks and the rest of the server keep working.

## Algorithm references and compatibility choices

The replacement code was written for this project from the mathematical and format
descriptions below. It replaces the previously adapted `macpaint_file` routines;
it is not a clean-room claim, and no former implementation is kept in the release.
The project implementations are covered by the root MIT license.

- **PackBits:** [TIFF 6.0 section 9, page 42](https://www.itu.int/itudoc/itu-t/com16/tiff-fx/docs/tiff6.pdf).
  Repeat and literal packets cover at most 128 bytes. Each scanline is encoded
  independently. Different compressed bytes can represent identical pixels.
- **Atkinson:** the six-neighbor diffusion pattern and one-eighth error shares
  described in [Atkinson Dithering](https://beyondloom.com/blog/dither.html).
  MacWire discards error beyond image edges instead of wrapping it into the next
  row. It uses a strict greater-than-128 white threshold and floating-point error
  accumulation for compatibility with its previous output. The new implementation
  keeps only three error rows, rather than an image-sized error array.
- **Grayscale:** Rec.709 luminance weights 0.2126, 0.7152, 0.0722, also documented by
  [W3C](https://www.w3.org/WAI/WCAG21/Understanding/relative-luminance.html), combined
  with MacWire's existing gamma-2.2 and cube-root tone curve. Specifically, each
  channel is raised to 2.2; weighted luminance Y becomes
  `round(255 * max(0, (116 * Y**(1/3) - 16) / 100))`.
  This is a compatibility curve, not the complete piecewise sRGB or CIELAB
  conversion. Channel powers are precomputed for the 256 possible byte values.

The output is MSB-first, with a set bit representing black and unused trailing
bits clear. Image dimensions, wire format, limits, and production cache behavior
are unchanged. The classic application does not need rebuilding for this change.
