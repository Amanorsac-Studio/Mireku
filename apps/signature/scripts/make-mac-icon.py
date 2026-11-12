# Makes the macOS app icon from the client's square artwork.
#
# macOS draws every app icon as the same rounded square ("squircle"): 824 x 824
# units, centred in a 1024 canvas, the rest transparent. An icon of any other
# shape is shrunk by macOS 26 and set on a pale plate, which is how the Windows
# rounded set looked on a Mac: a small logo with white around it. This script
# cuts the artwork to exactly that shape, so macOS shows it at full size.
#
#   python apps/signature/scripts/make-mac-icon.py
#
# Reads  resources/brand/MIREKU-1024.png (square, opaque, the approved artwork)
# Writes resources/brand/mac/MIREKU-1024.png and MIREKU-256.png
import math
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
BRAND = os.path.join(HERE, "..", "resources", "brand")

CANVAS = 1024      # the icon canvas
BODY = 824         # Apple's icon grid: the body of the icon
SUPER = 4          # supersampling for a smooth edge
EXPONENT = 5.0     # superellipse exponent that matches Apple's corner


def squircle_mask(size, body):
    big = size * SUPER
    half = body * SUPER / 2.0
    centre = big / 2.0
    points = []
    steps = 2000
    for i in range(steps):
        t = 2.0 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        x = math.copysign(abs(c) ** (2.0 / EXPONENT), c) * half
        y = math.copysign(abs(s) ** (2.0 / EXPONENT), s) * half
        points.append((centre + x, centre + y))
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).polygon(points, fill=255)
    return mask.resize((size, size), Image.LANCZOS)


def main():
    source = Image.open(os.path.join(BRAND, "MIREKU-1024.png")).convert("RGB")
    art = source.resize((BODY, BODY), Image.LANCZOS)
    icon = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    offset = (CANVAS - BODY) // 2
    icon.paste(art, (offset, offset))
    icon.putalpha(squircle_mask(CANVAS, BODY))

    out = os.path.join(BRAND, "mac")
    os.makedirs(out, exist_ok=True)
    icon.save(os.path.join(out, "MIREKU-1024.png"))
    icon.resize((256, 256), Image.LANCZOS).save(os.path.join(out, "MIREKU-256.png"))
    print("wrote", os.path.normpath(out))


if __name__ == "__main__":
    sys.exit(main())
