#!/usr/bin/env python3
# Draws the neon artwork for the Orchi Tron box main menu into textures/menu/.
# Usage: python3 orchi/make_menu_images.py
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "textures", "menu")

CYAN = (40, 220, 255)
ORANGE = (255, 150, 30)
GREEN = (60, 255, 140)
RED = (255, 60, 70)
GRID = (30, 60, 140)


def glow(layer, radius):
    """adds a soft glow around everything drawn on layer"""
    halo = layer.filter(ImageFilter.GaussianBlur(radius))
    return ImageChops.add(ImageChops.add(halo, halo), layer)


def backdrop(w, h, step=32):
    """black background with a faint grid"""
    img = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(img)
    faint = tuple(c // 3 for c in GRID)
    for x in range(0, w, step):
        d.line([(x, 0), (x, h)], fill=faint)
    for y in range(0, h, step):
        d.line([(0, y), (w, y)], fill=faint)
    return img


def play(w=512, h=256):
    img = Image.new("RGB", (w, h), (0, 0, 0))
    horizon = int(h * 0.38)
    # sky gradient
    d = ImageDraw.Draw(img)
    for y in range(horizon):
        t = y / horizon
        d.line([(0, y), (w, y)], fill=(0, int(10 + 30 * t), int(25 + 60 * t)))
    # perspective floor grid
    lines = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(lines)
    vx = w / 2
    for i in range(-14, 15):
        d.line([(vx, horizon), (vx + i * w / 9, h)], fill=GRID, width=1)
    for k in range(1, 12):
        y = horizon + (h - horizon) * (k / 11) ** 2
        d.line([(0, y), (w, y)], fill=GRID, width=1)
    d.line([(0, horizon), (w, horizon)], fill=CYAN, width=2)
    img = ImageChops.add(img, glow(lines, 3))
    # two light cycles with trails
    trails = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(trails)
    a = [(40, h - 10), (150, h - 70), (330, h - 70), (395, horizon + 30)]
    b = [(w - 30, h - 20), (w - 120, h - 40), (w - 120, horizon + 45), (265, horizon + 18)]
    d.line(a, fill=ORANGE, width=5, joint="curve")
    d.line(b, fill=CYAN, width=5, joint="curve")
    for (x, y), col, s in ((a[-1], ORANGE, 9), (b[-1], CYAN, 7)):
        d.ellipse([x - s, y - s * .6, x + s, y + s * .6], fill=col)
    img = ImageChops.add(img, glow(trails, 6))
    return img


def icon(draw_fn, color, w=256, h=256):
    img = backdrop(w, h)
    layer = Image.new("RGB", (w, h))
    draw_fn(ImageDraw.Draw(layer), w, h, color)
    return ImageChops.add(img, glow(layer, 7))


def rider(d, w, h, c):
    cx = w / 2
    # helmet
    d.ellipse([cx - 52, 40, cx + 52, 144], outline=c, width=8)
    d.line([(cx - 44, 100), (cx + 44, 100)], fill=c, width=8)
    # shoulders
    d.arc([cx - 100, 150, cx + 100, 330], 200, 340, fill=c, width=8)


def gear(d, w, h, c):
    cx, cy = w / 2, h / 2 - 8
    teeth = 8
    pts = []
    for i in range(teeth * 4):
        ang = 2 * math.pi * i / (teeth * 4)
        r = 82 if (i % 4) in (0, 1) else 62
        pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    d.line(pts + [pts[0]], fill=c, width=7, joint="curve")
    d.ellipse([cx - 26, cy - 26, cx + 26, cy + 26], outline=c, width=7)


def question(d, w, h, c):
    cx, cy = w / 2, h / 2 - 8
    d.ellipse([cx - 88, cy - 88, cx + 88, cy + 88], outline=c, width=7)
    d.arc([cx - 36, cy - 62, cx + 36, cy + 6], 180, 400, fill=c, width=9)
    d.line([(cx + 12, cy + 2), (cx, cy + 14), (cx, cy + 32)], fill=c, width=9)
    d.ellipse([cx - 7, cy + 50, cx + 7, cy + 64], fill=c)


def power(d, w, h, c):
    cx, cy = w / 2, h / 2 - 8
    d.arc([cx - 72, cy - 72, cx + 72, cy + 72], -60, 240, fill=c, width=10)
    d.line([(cx, cy - 92), (cx, cy - 10)], fill=c, width=10)


def main():
    os.makedirs(OUT, exist_ok=True)
    images = {
        "play.png": play(),
        "player.png": icon(rider, CYAN),
        "settings.png": icon(gear, ORANGE),
        "help.png": icon(question, GREEN),
        "exit.png": icon(power, RED),
    }
    for name, img in images.items():
        img.save(os.path.join(OUT, name), optimize=True)
        print("wrote", os.path.normpath(os.path.join(OUT, name)))


if __name__ == "__main__":
    sys.exit(main())
