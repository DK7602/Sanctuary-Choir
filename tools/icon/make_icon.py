#!/usr/bin/env python3
"""Cuts the app icon out of the icon artwork: the gold frame is the edge, outside it is
transparent. Writes the 1024 px and 256 px PNGs the plug-in build turns into the standalone
app's icon (macOS .icns, Windows .ico), and the Windows installer's .ico.

Usage: make_icon.py <artwork image> [<repo root>]

The frame is found in the artwork. Each side's outer edge is where a line coming in from the
outside first gets brighter than the black around the frame (sampled along the straight
parts; the frame is lit from above, so its bottom is shaded bronze, and that is frame too).
Each corner's radius is fitted to the frame's outer curve: on each row through the corner, the
first sharp step up from the black (the frame's edge is crisp; the glow around it is not).
"""
import os
import sys

import numpy as np
from PIL import Image

THRESHOLD = 40      # mean of R, G, B: the frame (even its shaded bronze) against the black
STEP = 25           # brightness rise over 2 px that marks the frame's crisp outer edge
INSET = 1.0         # px at 1024: keep the cut on the gold
SIZE = 1024


def find_frame(lum):
    h, w = lum.shape

    def first(values):
        idx = np.where(np.asarray(list(values), dtype=float) > THRESHOLD)[0]
        return int(idx[0]) if len(idx) else None

    def side(samples):
        found = [v for v in samples if v is not None]
        return float(np.median(found))

    mid = range(int(h * 0.25), int(h * 0.75), 10)
    left = side(first(lum[y, : w // 5]) for y in mid)
    right = w - side(first(lum[y, ::-1][: w // 5]) for y in mid)
    top = side(first(lum[: h // 5, x]) for x in range(int(w * 0.25), int(w * 0.75), 10))
    # The bottom has an ornament in the middle: sample either side of it.
    xs = list(range(int(w * 0.2), int(w * 0.38), 10)) + list(range(int(w * 0.62), int(w * 0.8), 10))
    bottom = h - side(first(lum[::-1, x][: h // 5]) for x in xs)

    # Each corner, mirrored so it is the top-left one: (x, y) of the frame's outer edge on each
    # row through the curve, then the radius whose circle (touching both sides) fits them best.
    radii = {}
    span = int(min(w, h) * 0.25)
    for name, flip_x, flip_y in (("tl", False, False), ("tr", True, False), ("bl", False, True), ("br", True, True)):
        img = lum[::-1] if flip_y else lum
        img = img[:, ::-1] if flip_x else img
        x0 = (w - right) if flip_x else left
        y0 = (h - bottom) if flip_y else top
        pts = []
        for y in range(int(y0) + 3, int(y0) + span):
            row = img[y, : w // 3]
            rise = row[2:] - row[:-2]
            hits = np.where((rise > STEP) & (row[2:] > THRESHOLD))[0]
            if len(hits):
                x = hits[0] + 1
                if x > x0 + 2:   # on the curve, not the straight side
                    pts.append((x - x0, y - y0))
        pts = np.array(pts, dtype=float)
        best = min(np.arange(100.0, span, 0.5),
                   key=lambda r: np.mean((np.hypot(pts[:, 0] - r, pts[:, 1] - r) - r) ** 2)
                   if len(pts) else 0.0)
        radii[name] = float(best)
    return left, top, right, bottom, radii


def rounded_alpha(size, radii, inset, supersample=4):
    """Alpha of a rounded square filling size x size, corners of the given radii, anti-aliased."""
    n = size * supersample
    c = (np.arange(n) + 0.5) / supersample
    x, y = np.meshgrid(c, c)
    lo, hi = inset, size - inset
    inside = (x >= lo) & (x <= hi) & (y >= lo) & (y <= hi)
    for name, r in radii.items():
        r = max(1.0, r - inset)
        cx = lo + r if name in ("tl", "bl") else hi - r
        cy = lo + r if name in ("tl", "tr") else hi - r
        corner = ((x < cx) if name in ("tl", "bl") else (x > cx)) & ((y < cy) if name in ("tl", "tr") else (y > cy))
        inside &= ~corner | ((x - cx) ** 2 + (y - cy) ** 2 <= r * r)
    return inside.reshape(size, supersample, size, supersample).mean(axis=(1, 3))


def main():
    src = sys.argv[1]
    root = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    art = Image.open(src).convert("RGB")
    lum = np.asarray(art).astype(float).mean(axis=2)
    left, top, right, bottom, radii = find_frame(lum)
    print("frame: left %.0f, top %.0f, right %.0f, bottom %.0f; corner radii %s"
          % (left, top, right, bottom, ", ".join("%s %.0f" % kv for kv in radii.items())))

    # The frame's box, a few per cent off square, becomes the whole square icon.
    crop = art.crop((int(round(left)), int(round(top)), int(round(right)), int(round(bottom))))
    scale = SIZE / ((crop.width + crop.height) / 2.0)
    rgb = crop.resize((SIZE, SIZE), Image.LANCZOS)
    alpha = rounded_alpha(SIZE, {k: v * scale for k, v in radii.items()}, INSET)
    icon = rgb.convert("RGBA")
    icon.putalpha(Image.fromarray(np.clip(alpha * 255.0 + 0.5, 0, 255).astype(np.uint8)))

    res = os.path.join(root, "plugin", "Resources")
    os.makedirs(res, exist_ok=True)
    icon.save(os.path.join(res, "icon-1024.png"), optimize=True)
    icon.resize((256, 256), Image.LANCZOS).save(os.path.join(res, "icon-256.png"), optimize=True)
    icon.save(os.path.join(root, "packaging", "windows", "SanctuaryChoir.ico"),
              sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("wrote plugin/Resources/icon-1024.png, icon-256.png and packaging/windows/SanctuaryChoir.ico")


if __name__ == "__main__":
    main()
