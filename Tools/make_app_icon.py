#!/usr/bin/env python3
# Copyright (c) 2026 Red Marmoset Studios Ltd.
# Renders Modules/MarconiUI/Resources/Marconi.ico, the icon embedded in the Windows executable and installer.
#
# It draws the same picture as Modules/MarconiUI/AppIcon.cpp (the runtime window icon), so keep the two in step.
# Needs Pillow: pip install pillow
#
# Usage: python Tools/make_app_icon.py [output.ico]

import os
import sys

from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 48, 64, 128, 256]
SUPERSAMPLE = 8

RED = (178, 34, 52, 255)


def draw_icon(size):
    big = size * SUPERSAMPLE
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    s = big / 32.0

    # Same mirrored geometry and rounded joins as AppIcon.cpp.
    points = [(5, 23), (10.5, 9), (16, 20), (21.5, 9), (27, 23)]
    draw.line([(x * s, y * s) for x, y in points], fill=RED, width=round(4 * s))
    for x, y in points:
        radius = 3 if (x, y) in (points[0], points[-1]) else 2
        draw.ellipse(((x - radius) * s, (y - radius) * s,
                      (x + radius) * s, (y + radius) * s), fill=RED)

    return image.resize((size, size), Image.LANCZOS)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default = os.path.join(root, "Modules", "MarconiUI", "Resources", "Marconi.ico")
    output = sys.argv[1] if len(sys.argv) > 1 else default
    os.makedirs(os.path.dirname(output), exist_ok=True)

    images = [draw_icon(size) for size in SIZES]
    # Pillow writes one entry per size from the largest image; pass the hand-drawn smaller ones so small sizes
    # stay crisp instead of being downscaled from 256.
    images[-1].save(output, format="ICO", sizes=[(n, n) for n in SIZES], append_images=images[:-1])
    print("Wrote " + output)


if __name__ == "__main__":
    main()
