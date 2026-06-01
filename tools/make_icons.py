#!/usr/bin/env python3
"""Render the PWA icons (web/icon-192.png, web/icon-512.png) from a candy
gradient rounded square and a big "24" set in the bundled Titan One font.

Requires Pillow. If it is missing, exit 2 so callers can fall back to a
system SVG rasteriser.
"""
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Pillow not available", file=sys.stderr)
    sys.exit(2)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "assets", "font.ttf")


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def make(size):
    top, bot = (0x8B, 0x7B, 0xFF), (0xE8, 0x6B, 0xD0)
    grad = Image.new("RGBA", (size, size))
    gd = ImageDraw.Draw(grad)
    for y in range(size):
        gd.line([(0, y), (size, y)], fill=lerp(top, bot, y / size) + (255,))

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [int(size * 0.07), int(size * 0.07), int(size * 0.93), int(size * 0.93)],
        radius=int(size * 0.21), fill=255)

    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)

    draw = ImageDraw.Draw(out)
    font = ImageFont.truetype(FONT, int(size * 0.5))
    box = draw.textbbox((0, 0), "24", font=font)
    tw, th = box[2] - box[0], box[3] - box[1]
    draw.text(((size - tw) / 2 - box[0], (size - th) / 2 - box[1]), "24",
              font=font, fill=(255, 255, 255, 255))
    return out


for s in (192, 512):
    path = os.path.join(ROOT, "web", f"icon-{s}.png")
    make(s).save(path)
    print("wrote", path)
