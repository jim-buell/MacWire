"""MacWire's small image algorithms, implemented from algorithm/format descriptions.

References and numeric compatibility choices are recorded in IMAGE_ALGORITHMS.md.
This module contains no dependency on the former macpaint_file implementation.
"""


def packbits_scanline(data: bytes) -> bytes:
    """Encode one scanline using TIFF 6.0 section 9 packets (1–128 bytes).

    Runs of at least three equal bytes use repeat packets; other bytes form
    literal packets. A packet never consumes more than 128 source bytes.
    """
    encoded = bytearray()
    cursor = 0
    size = len(data)
    while cursor < size:
        repeat_end = cursor + 1
        while repeat_end < min(size, cursor + 128) and data[repeat_end] == data[cursor]:
            repeat_end += 1
        if repeat_end - cursor >= 3:
            encoded.extend((257 - (repeat_end - cursor), data[cursor]))
            cursor = repeat_end
            continue
        start = cursor
        cursor = repeat_end
        while cursor < min(size, start + 128):
            if cursor + 2 < size and data[cursor] == data[cursor + 1] == data[cursor + 2]:
                break
            cursor += 1
        encoded.append(cursor - start - 1)
        encoded.extend(data[start:cursor])
    return bytes(encoded)


def atkinson_rows(rows):
    """Dither 8-bit rows using Atkinson's six-neighbor error diffusion.

    One eighth of the quantization error goes to (1,0), (2,0), (-1,1),
    (0,1), (1,1), (0,2). Discard off-image contributions; never wrap rows.
    Keep three error rows rather than an image-sized error allocation.
    Compatibility uses a strict >128 white threshold and unrounded errors.
    """
    if not rows:
        return []
    width = len(rows[0])
    if any(len(row) != width for row in rows):
        raise ValueError('Grayscale rows must have equal widths')
    errors = [[0.0] * (width + 4) for _ in range(3)]
    output = []
    for y, samples in enumerate(rows):
        active, below, following = (errors[(y + n) % 3] for n in range(3))
        line = bytearray(width)
        for x, sample in enumerate(samples):
            column = x + 2
            value = sample + active[column]
            level = 255 if value > 128 else 0
            line[x] = level
            share = (value - level) / 8
            active[column + 1] += share
            active[column + 2] += share
            below[column - 1] += share
            below[column] += share
            below[column + 1] += share
            following[column] += share
        output.append(line)
        # Padding absorbs discarded boundary contributions and is never read.
        active[:] = [0.0] * (width + 4)
    return output


# Compatibility tone curve: power-law 2.2 input, Rec.709 luminance weights,
# then the cube-root lightness branch with black clipping. This deliberately
# preserves MacWire's existing tone mapping, rather than claiming sRGB/Lab
# compliance (both standards include additional piecewise branches).
_LINEAR_CHANNEL = tuple(pow(channel / 255, 2.2) for channel in range(256))


def grayscale_rows(rows, alpha=False):
    """Convert flattened RGB rows to 8-bit perceptual grayscale.

    RGBA rows are accepted for API compatibility; the pipeline composites alpha
    onto white with Pillow before this stage. This routine ignores the fourth
    channel and never performs a second alpha composition.
    """
    stride = 4 if alpha else 3
    converted = []
    for samples in rows:
        if len(samples) % stride:
            raise ValueError('An RGB/RGBA row must contain whole pixels')
        luminance = bytearray(len(samples) // stride)
        for pixel in range(len(luminance)):
            offset = pixel * stride
            red, green, blue = (_LINEAR_CHANNEL[samples[offset + channel]] for channel in range(3))
            brightness = 0.2126 * red + 0.7152 * green + 0.0722 * blue
            lightness = max(0.0, (116 * pow(brightness, 1 / 3) - 16) / 100)
            luminance[pixel] = round(lightness * 255)
        converted.append(bytes(luminance))
    return converted
