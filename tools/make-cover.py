#!/usr/bin/env python3
"""ZenDecoder store cover — 1080x540.

Palette and motif come from icon-512: the icon's tile is a vertical
gradient (236,230,218 -> 228,218,202), so the cover background continues
that exact gradient and the icon (scaled to full canvas height) blends
into it with no visible tile edge. Charcoal/red accents are sampled from
the icon's ring and dot.
"""
from PIL import Image, ImageDraw, ImageFont
import numpy as np
import argparse

W, H = 1080, 540
TOP = (236, 230, 218)
BOT = (228, 218, 202)
CHARCOAL = (35, 32, 29)
RED = (200, 66, 44)
MUTED = (107, 100, 90)

# The store assets live outside the repo, next to the sibling checkout.
_assets = "/home/theb/Jolla/store-screenshots/harbour-zendecoder"
ap = argparse.ArgumentParser(description="ZenDecoder store cover")
ap.add_argument("--icon", default=f"{_assets}/icon-512.png")
ap.add_argument("--out", default=f"{_assets}/cover-1080x540")
a = ap.parse_args()
SRC, OUT = a.icon, a.out

F = "/usr/share/fonts/truetype/noto/"
f_title = ImageFont.truetype(F + "NotoSans-Bold.ttf", 70)
f_sub = ImageFont.truetype(F + "NotoSans-Medium.ttf", 33)
f_small = ImageFont.truetype(F + "NotoSans-Regular.ttf", 24)


def fit(font, text, maxw):
    """Shrink until the line fits (defensive; layout is sized to fit)."""
    size = font.size
    while font.getbbox(text)[2] > maxw and size > 18:
        size -= 1
        font = ImageFont.truetype(font.path, size)
    return font


# --- background: the icon's own vertical gradient, continued -------------
img = Image.new("RGB", (1, H))
d = ImageDraw.Draw(img)
for y in range(H):
    t = y / (H - 1)
    d.point((0, y), fill=tuple(round(TOP[i] + (BOT[i] - TOP[i]) * t)
                               for i in range(3)))
img = img.resize((W, H))

# --- icon at full canvas height: same gradient, so no seam --------------
icon = Image.open(SRC).convert("RGBA").resize((H, H), Image.LANCZOS)
arr = np.asarray(icon).astype(np.int16).copy()
# The artwork's rounded tile carries a near-white 1px rim (opaque, so an
# alpha test misses it). Per row, the tile colour is the median of the
# opaque pixels; anything brighter than that by >6 in every channel is
# rim/edge junk — drop it to transparent.
alpha = arr[..., 3].copy()
for yy in range(H):
    m = alpha[yy] == 255
    if m.sum() < 20:
        continue
    med = np.median(arr[yy][m][:, :3], axis=0)
    if ((arr[yy][:, :3] - med).min(axis=1) > 6).any():
        alpha[yy][(arr[yy][:, :3] - med).min(axis=1) > 6] = 0
arr[..., 3] = alpha
# Anti-aliased edge pixels keep a halo in their RGB; repaint them with the
# background colour at their row so compositing yields the gradient exactly.
edge = arr[..., 3] < 255
rows = np.arange(H)[:, None]
bgcol = (np.array(TOP)[None, :] +
         (np.array(BOT) - np.array(TOP))[None, :] *
         (rows / (H - 1))[:, :1])
arr[..., :3] = np.where(edge[..., None], bgcol[:, None, :], arr[..., :3])
icon = Image.fromarray(arr.astype(np.uint8), "RGBA")
img.paste(icon, (-44, 0), icon)

draw = ImageDraw.Draw(img)

# --- text block ---------------------------------------------------------
TX = 546
MAXW = W - 64 - TX
y = 118
draw.text((TX, y), "ZenDecoder", font=f_title, fill=CHARCOAL)
y += 92
draw.rounded_rectangle([TX, y, TX + 104, y + 10], radius=5, fill=RED)
y += 46
draw.text((TX, y), "Barcode & QR reader", font=f_sub, fill=(58, 53, 47))
y += 44
draw.text((TX, y), "for Sailfish OS", font=f_sub, fill=(58, 53, 47))
y += 54
draw.text((TX, y), "On-device \u00b7 No ads \u00b7 No tracking",
          font=f_small, fill=MUTED)

# --- barcode motif, bottom right (one red bar = the icon's dot) ---------
widths = [4, 2, 7, 3, 2, 9, 4, 2, 5, 3, 8, 2, 4, 6, 2, 3, 7, 4, 2, 5,
          3, 2, 8, 4, 3, 6, 2, 4, 5, 2, 7, 3, 4, 2, 6, 3, 5, 2, 8, 4]
bx, by, bh = TX, H - 74, 46
for i, w in enumerate(widths):
    if bx + w > W - 64:
        break
    draw.rectangle([bx, by, bx + w - 1, by + bh],
                   fill=RED if i == 17 else CHARCOAL)
    bx += w + (6 if i % 3 else 9)

img.save(OUT + ".png")
img.save(OUT + ".jpg", quality=95)
print("wrote", OUT + ".png", img.size)
