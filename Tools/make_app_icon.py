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
DARK = (32, 36, 44, 255)
PAPER = (236, 240, 245, 255)
WHITE = (255, 255, 255, 255)


def draw_icon(size):
    big = size * SUPERSAMPLE
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    s = big / 32.0

    def rect(x, y, w, h, fill, radius=0.0):
        box = [x * s, y * s, (x + w) * s - 1, (y + h) * s - 1]
        if radius > 0:
            draw.rounded_rectangle(box, radius=radius * s, fill=fill)
        else:
            draw.rectangle(box, fill=fill)

    # Bezel and stand.
    rect(1, 3, 30, 21, RED, 3)
    rect(13, 24, 6, 3, RED)
    rect(8, 27, 16, 3, RED, 1.5)
    # Screen.
    rect(3, 5, 26, 17, DARK)
    # Remote window with its title bar.
    rect(7, 8, 18, 11, PAPER, 1.5)
    rect(7, 8, 18, 2.5, RED)
    # Pointer, outlined at least one output pixel wide like AppIcon.cpp.
    pointer = [(17, 12), (17, 21), (19.5, 18.5), (22.5, 20.5), (23.5, 19), (20.5, 17), (23.5, 15.5)]
    points = [(x * s, y * s) for x, y in pointer]
    outline = max(1.0, size / 32.0) * SUPERSAMPLE
    draw.polygon(points, fill=WHITE)
    draw.line(points + [points[0]], fill=DARK, width=int(round(outline)), joint="curve")

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
