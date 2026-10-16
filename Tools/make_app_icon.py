#!/usr/bin/env python3
# Copyright (c) 2026 Red Marmoset Studios Ltd.
# Renders the app icons:
#
#   Modules/MarconiUI/Resources/Marconi.ico    embedded in the Windows executable and installer
#   Modules/MarconiUI/Resources/Marconi.icns   the macOS app bundle's icon (Package.sh), the same mark on a white
#                                              rounded-square plate as macOS icons expect
#
# It draws the same picture as Modules/MarconiUI/AppIcon.cpp (the runtime window icon), so keep the two in step.
# Needs Pillow: pip install pillow
#
# Usage: python Tools/make_app_icon.py

import os

from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 48, 64, 96, 128, 256]
MAC_SIZES = [16, 32, 64, 128, 256, 512, 1024]
SUPERSAMPLE = 8
MAC_SUPERSAMPLE = 4

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


def draw_mac_icon(size):
    # Apple's icon grid: an 824/1024 plate with a ~185/1024 corner radius, leaving room for the system's shadow.
    big = size * MAC_SUPERSAMPLE
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    inset = big * 100 / 1024
    draw.rounded_rectangle((inset, inset, big - inset, big - inset), radius=big * 185 / 1024,
                           fill=(255, 255, 255, 255))
    image = image.resize((size, size), Image.LANCZOS)

    mark = draw_icon(round(size * 0.8))
    offset = (size - mark.width) // 2
    image.alpha_composite(mark, (offset, offset + round(size * 0.02)))
    return image


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    resources = os.path.join(root, "Modules", "MarconiUI", "Resources")
    os.makedirs(resources, exist_ok=True)

    ico = os.path.join(resources, "Marconi.ico")
    images = [draw_icon(size) for size in SIZES]
    # Pillow writes one entry per size from the largest image; pass the hand-drawn smaller ones so small sizes
    # stay crisp instead of being downscaled from 256.
    images[-1].save(ico, format="ICO", sizes=[(n, n) for n in SIZES], append_images=images[:-1])
    print("Wrote " + ico)

    icns = os.path.join(resources, "Marconi.icns")
    images = [draw_mac_icon(size) for size in MAC_SIZES]
    images[-1].save(icns, format="ICNS", append_images=images[:-1])
    print("Wrote " + icns)


if __name__ == "__main__":
    main()
