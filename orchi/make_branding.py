#!/usr/bin/env python3
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ART = os.path.join(ROOT, "orchi", "art")


def title():
    src = Image.open(os.path.join(ART, "title_source.png")).convert("RGB")
    background = src.getpixel((2, 2))
    canvas = Image.new("RGB", (2048, 1024), background)
    height = 700
    art = src.resize((round(src.width * height / src.height), height), Image.LANCZOS)
    canvas.paste(art, ((canvas.width - art.width) // 2, (canvas.height - art.height) // 2))
    canvas.save(os.path.join(ROOT, "textures", "title.jpg"), quality=93)


def menu_background():
    src = Image.open(os.path.join(ART, "menu_background_source.png")).convert("RGB")
    src.resize((2048, 1024), Image.LANCZOS).save(os.path.join(ROOT, "textures", "menu", "background.jpg"), quality=92)


def icon():
    src = Image.open(os.path.join(ART, "icon_source.png")).convert("RGB")
    w, h = src.size
    pink = lambda p: p[0] > 150 and p[2] > 100

    xs = [x for x in range(w) if pink(src.getpixel((x, h // 2)))]
    ys = [y for y in range(h) if pink(src.getpixel((w // 2, y)))]
    cx, cy = (xs[0] + xs[-1] + 1) / 2, (ys[0] + ys[-1] + 1) / 2
    radius = min(xs[-1] + 1 - xs[0], ys[-1] + 1 - ys[0]) / 2 - .5

    ss = 8
    mask = Image.new("L", (w * ss, h * ss), 0)
    ImageDraw.Draw(mask).ellipse(((cx - radius) * ss, (cy - radius) * ss, (cx + radius) * ss, (cy + radius) * ss), fill=255)
    mask = mask.resize((w, h), Image.LANCZOS)

    edge = src.getpixel((int(cx), int(cy - radius + 3)))
    rgb = Image.composite(src, Image.new("RGB", (w, h), edge), mask)
    rgba = rgb.copy()
    rgba.putalpha(mask)
    box = (round(cx - radius - 1), round(cy - radius - 1), round(cx + radius + 1), round(cy + radius + 1))
    rgba = rgba.crop(box)

    rgba.resize((48, 48), Image.LANCZOS).save(os.path.join(ROOT, "textures", "icon.png"))
    for size in (16, 32, 48, 64, 128):
        out = os.path.join(ROOT, "desktop", "icons", f"{size}x{size}", "armagetronad.png")
        rgba.resize((size, size), Image.LANCZOS).save(out)
    # the macOS app icon; the source is small, so stop at 256 pixels
    rgba.resize((256, 256), Image.LANCZOS).save(os.path.join(ART, "orchid.icns"), sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256)])


def main():
    title()
    icon()
    menu_background()
    print("wrote textures/title.jpg, textures/icon.png, textures/menu/background.jpg and desktop/icons/*/armagetronad.png and orchi/art/orchid.icns")


if __name__ == "__main__":
    sys.exit(main())
