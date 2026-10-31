#!/usr/bin/env python3
# Draws the artwork for the Orchid Client box main menu into textures/menu/.
# Flat, clean style matching the server list: dark slate, light glyphs, one accent color.
# All images are 512x256 with the subject centered; the menu crops, never stretches.
# Usage: python3 orchi/make_menu_images.py
import os
import sys

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "textures", "menu")

W, H = 512, 256
SS = 4  # supersampling factor for smooth edges

BG = (16, 21, 27)
GRID = (28, 35, 43)
GLYPH = (201, 209, 217)
ACCENT = (77, 216, 232)
ORANGE = (240, 160, 48)


def canvas():
    img = Image.new("RGB", (W * SS, H * SS), BG)
    return img, ImageDraw.Draw(img)


def finish(img):
    return img.resize((W, H), Image.LANCZOS)


def s(*v):
    """scales design coordinates to the supersampled canvas"""
    return [x * SS for x in v]


def play():
    img, d = canvas()
    # top-down arena: subtle grid inside a rim
    for x in range(32, W, 32):
        d.line(s(x, 0, x, H), fill=GRID, width=SS)
    for y in range(32, H, 32):
        d.line(s(0, y, W, y), fill=GRID, width=SS)
    d.rectangle(s(16, 16, W - 16, H - 16), outline=(60, 70, 82), width=3 * SS)
    # two flat light trails with right-angle turns, square cycle heads
    a = [(48, 208), (176, 208), (176, 112), (304, 112), (304, 64)]
    b = [(464, 48), (400, 48), (400, 176), (240, 176)]
    d.line([c * SS for p in a for c in p], fill=ORANGE, width=6 * SS, joint="curve")
    d.line([c * SS for p in b for c in p], fill=ACCENT, width=6 * SS, joint="curve")
    for (x, y), col in ((a[-1], ORANGE), (b[-1], ACCENT)):
        d.rectangle(s(x - 9, y - 9, x + 9, y + 9), fill=col)
    return finish(img)


def icon(draw_fn):
    img, d = canvas()
    # icon area: upper part of the image, so the label strip of the box leaves it free
    cx, cy, r = W / 2, 92, 62
    draw_fn(d, cx, cy, r)
    return finish(img)


def player(d, cx, cy, r):
    d.ellipse(s(cx - 26, cy - 52, cx + 26, cy), fill=GLYPH)
    d.chord(s(cx - 52, cy + 8, cx + 52, cy + 112), 180, 360, fill=GLYPH)
    d.rectangle(s(cx - 52, cy + 58, cx + 52, cy + 62), fill=ACCENT)


def settings(d, cx, cy, r):
    import math
    teeth = 8
    pts = []
    for i in range(teeth * 4):
        ang = 2 * math.pi * (i - 0.5) / (teeth * 4)
        rr = 60 if (i % 4) in (0, 1) else 46
        pts.append(((cx + rr * math.cos(ang)) * SS, (cy + rr * math.sin(ang)) * SS))
    d.polygon(pts, fill=GLYPH)
    d.ellipse(s(cx - 20, cy - 20, cx + 20, cy + 20), fill=BG)
    d.ellipse(s(cx - 8, cy - 8, cx + 8, cy + 8), fill=ACCENT)


def help_(d, cx, cy, r):
    d.ellipse(s(cx - 58, cy - 58, cx + 58, cy + 58), outline=GLYPH, width=8 * SS)
    d.arc(s(cx - 24, cy - 38, cx + 24, cy + 6), 180, 400, fill=ACCENT, width=10 * SS)
    d.line(s(cx + 8, cy + 2, cx, cy + 10, cx, cy + 20), fill=ACCENT, width=10 * SS, joint="curve")
    d.ellipse(s(cx - 7, cy + 30, cx + 7, cy + 44), fill=ACCENT)


def exit_(d, cx, cy, r):
    d.arc(s(cx - 52, cy - 52, cx + 52, cy + 52), -55, 235, fill=GLYPH, width=10 * SS)
    d.line(s(cx, cy - 66, cx, cy - 6), fill=ACCENT, width=10 * SS)


def main():
    os.makedirs(OUT, exist_ok=True)
    images = {
        "play.png": play(),
        "player.png": icon(player),
        "settings.png": icon(settings),
        "help.png": icon(help_),
        "exit.png": icon(exit_),
    }
    for name, img in images.items():
        img.save(os.path.join(OUT, name), optimize=True)
        print("wrote", os.path.normpath(os.path.join(OUT, name)))


if __name__ == "__main__":
    sys.exit(main())
