"""Convert the supplied pixel art to classic Mac icon and mask resources.

Run with backend/.venv/bin/python client/generate_icons.py from the project root.
The 32x32 source drives the ICON and ICN# resources; the separate 16x16 source
drives the ics# resource so the small icon is the intended final artwork rather
than a downscale of the large one.
"""

from pathlib import Path

from PIL import Image


def pack(rows):
    width = len(rows[0])
    return b"".join(sum(int(bit) << (width - 1 - x) for x, bit in enumerate(row))
                    .to_bytes(width // 8, "big") for row in rows)


def analyze(image):
    width, height = image.size
    pixels = [[image.getpixel((x, y)) for x in range(width)] for y in range(height)]
    mask = [[pixel[3] >= 128 for pixel in row] for row in pixels]
    black = [[opaque and sum(pixel[:3]) < 384 for pixel, opaque in zip(row, mask_row)]
             for row, mask_row in zip(pixels, mask)]
    return black, mask


def main():
    root = Path(__file__).resolve().parent
    large = Image.open(root / "assets/MacWire.png").convert("RGBA")
    if large.size != (32, 32):
        raise ValueError("The main icon must be exactly 32x32 pixels")
    small_image = Image.open(root / "assets/MacWire16.png").convert("RGBA")
    if small_image.size != (16, 16):
        raise ValueError("The small icon must be exactly 16x16 pixels")
    black, mask = analyze(large)
    small_black, small_mask = analyze(small_image)
    resources = {
        "ICON": pack(black),
        "ICN#": pack(black) + pack(mask),
        "ics#": pack(small_black) + pack(small_mask),
    }
    text = "/* Generated from assets/MacWire.png and assets/MacWire16.png by generate_icons.py. */\n"
    for kind, data in resources.items():
        text += f"data '{kind}' (128) {{\n"
        for offset in range(0, len(data), 16):
            text += f'    $"{data[offset:offset + 16].hex().upper()}"\n'
        text += "};\n"
    (root / "TickerTapeIcons.r").write_text(text)


if __name__ == "__main__":
    main()
