#!/usr/bin/env python3
# Copyright (c) 2026 Red Marmoset Studios Ltd.
# Renders the installer's branding bitmaps from the app icon's geometry, the theme's accent red, Nebula Sans and the
# Red Marmoset logo (Assets/RedMarmoset.png, logo-open.png from web-loading-screens):
#
#   Installer/Art/Wizard.bmp   164x314, the left panel of the welcome and finish pages
#   Installer/Art/Header.bmp   150x57, the top-right image on the other pages
#   Installer/Art/DmgBackground.png and DmgBackground@2x.png, the macOS disk image window's background: an arrow
#                              from Marconi to the Applications folder, laid out by Tools/dmg_layout.py
#
# NSIS needs uncompressed 24-bit BMPs at these sizes (it stretches them on high-DPI displays).
# Needs Pillow: pip install pillow
#
# Usage: python Tools/make_installer_art.py

import os

from PIL import Image, ImageDraw, ImageFont

import dmg_layout
from make_app_icon import draw_icon

SUPERSAMPLE = 4

# Appearance.cpp: the light theme's accent and text colours.
RED = (178, 34, 52)
RED_DARK = (112, 18, 32)
TEXT = (27, 35, 47)
MUTED = (96, 104, 116)
WHITE = (255, 255, 255)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "Assets", "Fonts")
STUDIO_LOGO = os.path.join(ROOT, "Assets", "RedMarmoset.png")


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size * SUPERSAMPLE)


def logo(size, colour):
    # The icon's alpha, filled with the requested colour.
    mask = draw_icon(size * SUPERSAMPLE).getchannel("A")
    image = Image.new("RGBA", mask.size, colour + (0,))
    image.putalpha(mask)
    return image


def centred_text(draw, width, y, text, typeface, fill):
    left, _, right, _ = draw.textbbox((0, 0), text, font=typeface)
    draw.text(((width - (right - left)) / 2 - left, y), text, font=typeface, fill=fill)


def wizard():
    width, height = 164 * SUPERSAMPLE, 314 * SUPERSAMPLE
    image = Image.new("RGB", (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / (height - 1)
        draw.line([(0, y), (width, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(RED, RED_DARK)))

    mark = logo(112, WHITE)
    image.paste(mark, ((width - mark.width) // 2, 26 * SUPERSAMPLE), mark)

    centred_text(draw, width, 132 * SUPERSAMPLE, "Marconi", font("NebulaSans-Black.ttf", 30), WHITE)
    subtitle = font("NebulaSans-Semibold.ttf", 10)
    for i, line in enumerate(["Multi-protocol Access", "and Remote Control", "Interface"]):
        centred_text(draw, width, (174 + i * 14) * SUPERSAMPLE, line, subtitle, (240, 200, 205))

    # The studio logo has a white outline, so it reads on the red as it does on the loading screens.
    studio = Image.open(STUDIO_LOGO).convert("RGBA")
    studio = studio.crop(studio.getbbox())
    size = 56 * SUPERSAMPLE
    studio = studio.resize((size, size), Image.LANCZOS)
    image.paste(studio, ((width - size) // 2, 226 * SUPERSAMPLE), studio)

    centred_text(draw, width, 288 * SUPERSAMPLE, "Red Marmoset Studios", font("NebulaSans-Semibold.ttf", 9),
                 (220, 150, 160))
    return image.resize((164, 314), Image.LANCZOS)


def header():
    width, height = 150 * SUPERSAMPLE, 57 * SUPERSAMPLE
    image = Image.new("RGB", (width, height), WHITE)
    draw = ImageDraw.Draw(image)

    mark = logo(40, RED)
    markX = 10 * SUPERSAMPLE
    image.paste(mark, (markX, (height - mark.height) // 2), mark)

    typeface = font("NebulaSans-Black.ttf", 22)
    left, top, right, bottom = draw.textbbox((0, 0), "Marconi", font=typeface)
    textX = markX + mark.width + 4 * SUPERSAMPLE
    draw.text((textX - left, (height - (bottom - top)) / 2 - top), "Marconi", font=typeface, fill=TEXT)
    return image.resize((150, 57), Image.LANCZOS)


def dmg_background(scale):
    # Drawn at twice the requested scale and reduced, for smooth edges. Coordinates are in points.
    s = scale * 2
    size = dmg_layout.WINDOW_SIZE * s
    image = Image.new("RGB", (size, size), WHITE)
    draw = ImageDraw.Draw(image)

    def font_at(name, points):
        return ImageFont.truetype(os.path.join(FONTS, name), points * s)

    # The name at the top, with the mark beside it
    mark = draw_icon(76 * s)
    typeface = font_at("NebulaSans-Black.ttf", 64)
    left, top, right, bottom = draw.textbbox((0, 0), "Marconi", font=typeface)
    gap = 18 * s
    total = mark.width + gap + (right - left)
    x = (size - total) // 2
    centreY = 118 * s
    image.paste(mark, (x, centreY - mark.height // 2), mark)
    draw.text((x + mark.width + gap - left, centreY - (bottom - top) / 2 - top), "Marconi", font=typeface, fill=TEXT)

    # The arrow between the icons: a thick rounded shaft and a solid head, in the accent red
    half = dmg_layout.ICON_SIZE / 2
    start = (dmg_layout.APP_X + half + 22) * s
    end = (dmg_layout.APPLICATIONS_X - half - 22) * s
    y = dmg_layout.ICON_Y * s
    shaft = 7 * s
    headLength, headHalf = 24 * s, 18 * s
    draw.line([(start, y), (end - headLength + shaft, y)], fill=RED, width=round(shaft * 2))
    draw.ellipse([start - shaft, y - shaft, start + shaft, y + shaft], fill=RED)
    draw.polygon([(end - headLength, y - headHalf), (end, y), (end - headLength, y + headHalf)], fill=RED)

    # The instruction, below the icons' labels
    centred_text(draw, size, 430 * s, "Drag Marconi into Applications to install",
                 font_at("NebulaSans-Semibold.ttf", 17), MUTED)
    return image.resize((dmg_layout.WINDOW_SIZE * scale,) * 2, Image.LANCZOS)


def main():
    output = os.path.join(ROOT, "Installer", "Art")
    os.makedirs(output, exist_ok=True)
    for name, image in (("Wizard.bmp", wizard()), ("Header.bmp", header())):
        path = os.path.join(output, name)
        image.save(path, format="BMP")
        print("Wrote " + path)
    for name, scale in (("DmgBackground.png", 1), ("DmgBackground@2x.png", 2)):
        path = os.path.join(output, name)
        image = dmg_background(scale)
        # 72 dpi for the normal one and 144 for the Retina one, so macOS shows both at the window's size in points
        image.save(path, format="PNG", dpi=(72 * scale, 72 * scale))
        print("Wrote " + path)


if __name__ == "__main__":
    main()
