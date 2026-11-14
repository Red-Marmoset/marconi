#!/usr/bin/env python3
# Copyright (c) 2026 Red Marmoset Studios Ltd.
# Renders the installer's branding bitmaps from the app icon's geometry, the theme's accent red, Nebula Sans and the
# Red Marmoset logo (Assets/RedMarmoset.png, logo-open.png from web-loading-screens):
#
#   Installer/Art/Wizard.bmp   164x314, the left panel of the welcome and finish pages
#   Installer/Art/Header.bmp   150x57, the top-right image on the other pages
#
# NSIS needs uncompressed 24-bit BMPs at these sizes (it stretches them on high-DPI displays).
# Needs Pillow: pip install pillow
#
# Usage: python Tools/make_installer_art.py

import os

from PIL import Image, ImageDraw, ImageFont

from make_app_icon import draw_icon

SUPERSAMPLE = 4

# Appearance.cpp: the light theme's accent and text colours.
RED = (178, 34, 52)
RED_DARK = (112, 18, 32)
TEXT = (27, 35, 47)
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


def main():
    output = os.path.join(ROOT, "Installer", "Art")
    os.makedirs(output, exist_ok=True)
    for name, image in (("Wizard.bmp", wizard()), ("Header.bmp", header())):
        path = os.path.join(output, name)
        image.save(path, format="BMP")
        print("Wrote " + path)


if __name__ == "__main__":
    main()
