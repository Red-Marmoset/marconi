#!/usr/bin/env python3
# Copyright (c) 2026 Red Marmoset Studios Ltd.
# Renders the app icons:
#
#   Modules/MarconiUI/Resources/Marconi.ico    embedded in the Windows executable and installer
#   Assets/AppIcon.png                         1024x1024, the icon Project.json's package.iconImage names: the Una
#                                              engine's packaging makes the macOS app's icon and the disk image's
#                                              background from it (the mark on a white plate)
#   Modules/MarconiUI/Resources/Marconi.icns   the same picture as a macOS icon, for PackageServer.sh until the
#                                              server's packaging goes through the engine too
#   Installer/Linux/icons/hicolor/<size>x<size>/apps/marconi.png
#                                              the Linux package's icons (Package.sh), one PNG per size in the layout
#                                              /usr/share/icons expects
#   Apps/iPad/Assets.xcassets/AppIcon.appiconset/Icon-1024.png
#                                              the iPad and iPhone app's icon: the mark on a white square with no
#                                              transparency, as iOS wants (it rounds the corners itself)
#   Modules/MarconiServerUI/Resources/MarconiServer.ico
#                                              Marconi Server's icon: the same mark in white on a blue circle, as in
#                                              the tray (only with --server, so the others aren't redrawn)
#
# It draws the same picture as Modules/MarconiUI/AppIcon.cpp (the runtime window icon), so keep the two in step.
# Needs Pillow: pip install pillow
#
# Usage: python Tools/make_app_icon.py [--server]

import os
import sys

from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 48, 64, 96, 128, 256]
MAC_SIZES = [16, 32, 64, 128, 256, 512, 1024]
LINUX_SIZES = [16, 24, 32, 48, 64, 128, 256]
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


SERVER_BLUE = (0x1E, 0x66, 0xD0, 255)
WHITE = (255, 255, 255, 255)


def draw_server_icon(size):
    # Matches the tray icon drawn in Modules/MarconiServerUI/ServerApp.cpp: a blue circle with the main icon's mark
    # in white, scaled about its centre to sit inside the circle.
    big = size * SUPERSAMPLE
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.ellipse((0, 0, big - 1, big - 1), fill=SERVER_BLUE)
    s = big / 32.0
    fit = 0.86

    def at(v):
        return (16.0 + (v - 16.0) * fit) * s

    points = [(5, 23), (10.5, 9), (16, 20), (21.5, 9), (27, 23)]
    draw.line([(at(x), at(y)) for x, y in points], fill=WHITE, width=round(4 * s * fit))
    for x, y in points:
        radius = (3 if (x, y) in (points[0], points[-1]) else 2) * s * fit
        draw.ellipse((at(x) - radius, at(y) - radius, at(x) + radius, at(y) + radius), fill=WHITE)
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


def draw_ios_icon(size):
    image = Image.new("RGBA", (size, size), (255, 255, 255, 255))
    mark = draw_icon(round(size * 0.78))
    offset = (size - mark.width) // 2
    image.alpha_composite(mark, (offset, offset + round(size * 0.02)))
    return image.convert("RGB")


def make_server_icon(root):
    resources = os.path.join(root, "Modules", "MarconiServerUI", "Resources")
    os.makedirs(resources, exist_ok=True)
    ico = os.path.join(resources, "MarconiServer.ico")
    images = [draw_server_icon(size) for size in SIZES]
    images[-1].save(ico, format="ICO", sizes=[(n, n) for n in SIZES], append_images=images[:-1])
    print("Wrote " + ico)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if "--server" in sys.argv[1:]:
        make_server_icon(root)
        return
    resources = os.path.join(root, "Modules", "MarconiUI", "Resources")
    os.makedirs(resources, exist_ok=True)

    ico = os.path.join(resources, "Marconi.ico")
    images = [draw_icon(size) for size in SIZES]
    # Pillow writes one entry per size from the largest image; pass the hand-drawn smaller ones so small sizes
    # stay crisp instead of being downscaled from 256.
    images[-1].save(ico, format="ICO", sizes=[(n, n) for n in SIZES], append_images=images[:-1])
    print("Wrote " + ico)

    app_icon = os.path.join(root, "Assets", "AppIcon.png")
    draw_mac_icon(1024).save(app_icon, format="PNG", optimize=True)
    print("Wrote " + app_icon)

    icns = os.path.join(resources, "Marconi.icns")
    images = [draw_mac_icon(size) for size in MAC_SIZES]
    images[-1].save(icns, format="ICNS", append_images=images[:-1])
    print("Wrote " + icns)

    icon_set = os.path.join(root, "Apps", "iPad", "Assets.xcassets", "AppIcon.appiconset")
    os.makedirs(icon_set, exist_ok=True)
    draw_ios_icon(1024).save(os.path.join(icon_set, "Icon-1024.png"), format="PNG", optimize=True)

    for size in LINUX_SIZES:
        folder = os.path.join(root, "Installer", "Linux", "icons", "hicolor", "%dx%d" % (size, size), "apps")
        os.makedirs(folder, exist_ok=True)
        png = os.path.join(folder, "marconi.png")
        draw_icon(size).save(png, format="PNG", optimize=True)
        print("Wrote " + png)


if __name__ == "__main__":
    main()
