#!/usr/bin/env python3
"""Builds the static UI skin for Sanctuary Choir from the design mockup.

The mockup (design/mockups/perform-00-empty.png, 1448x1086) is used as the plugin's
background. Everything that changes at run time (preset name, knob values and pointers,
dropdown texts, section meters, waveforms, range labels) is painted out with OpenCV
inpainting so the web UI can draw live versions on top.

Outputs (ui/assets/skin/):
  skin.jpg      background with the dynamic regions removed
  cap.png       clean knob cap (pointer removed), drawn under each live pointer
  tab_on.png    selected tab button without text
  tab_off.png   unselected tab button without text
  debug_mask.png (tools/art/.work) shows what was painted out

Usage: python3 tools/art/build_skin.py
"""
import os
import sys

import cv2
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "design", "mockups", "perform-00-empty.png")
OUT = os.path.join(ROOT, "ui", "assets", "skin")
WORK = os.path.join(ROOT, "tools", "art", ".work")

# Layout of the mockup (pixels in the 1448x1086 design space). Keep in sync with ui/js/layout.js.
KNOB_CENTRES = [(137, 650), (328, 650), (520, 650), (925, 650), (1119, 650), (1312, 650)]
PRESET_TEXT = (188, 31, 374, 60)
DROPDOWN_TEXT = [(128, 812, 272, 843), (409, 812, 528, 843), (665, 812, 768, 843),
                 (906, 812, 1046, 843), (1185, 812, 1362, 843)]
LEVEL_BARS = [(301, 908, 453, 925), (556, 908, 700, 925), (805, 908, 948, 925), (1052, 908, 1197, 925)]
WAVEFORMS = [(238, 927, 455, 959), (486, 927, 702, 959), (736, 927, 960, 959), (983, 927, 1199, 959)]
RANGE_LABELS = [(312, 1011, 402, 1033), (562, 1011, 652, 1033), (804, 1011, 894, 1033), (1050, 1011, 1140, 1033)]
TABS = {"on": (1021, 27, 1118, 68), "off": (1125, 27, 1207, 68)}


def rect_mask(shape, rects, pad=0):
    m = np.zeros(shape[:2], np.uint8)
    for (x0, y0, x1, y1) in rects:
        m[max(0, y0 - pad):y1 + pad, max(0, x0 - pad):x1 + pad] = 255
    return m


def text_mask(img, rect, thresh=70, grow=2):
    """Mask only the bright text pixels inside a rectangle (keeps borders/background intact)."""
    x0, y0, x1, y1 = rect
    roi = cv2.cvtColor(img[y0:y1, x0:x1], cv2.COLOR_BGR2GRAY)
    _, m = cv2.threshold(roi, thresh, 255, cv2.THRESH_BINARY)
    m = cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=grow)
    full = np.zeros(img.shape[:2], np.uint8)
    full[y0:y1, x0:x1] = m
    return full


def main():
    img = cv2.imread(SRC, cv2.IMREAD_COLOR)
    if img is None:
        sys.exit("cannot read " + SRC)
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)

    mask = np.zeros(img.shape[:2], np.uint8)

    # Text drawn live by the UI.
    mask |= text_mask(img, PRESET_TEXT, 60)
    for r in DROPDOWN_TEXT:
        mask |= text_mask(img, r, 60)
    for (cx, cy) in KNOB_CENTRES:
        mask |= text_mask(img, (cx - 50, cy + 78, cx + 50, cy + 100), 60)
    for r in RANGE_LABELS:
        mask |= text_mask(img, r, 60)

    # Meters and waveforms are fully redrawn.
    mask |= rect_mask(img.shape, LEVEL_BARS)
    mask |= rect_mask(img.shape, WAVEFORMS)

    # Gold needle above each knob cap (the live pointer replaces it).
    for (cx, cy) in KNOB_CENTRES:
        cv2.rectangle(mask, (cx - 5, cy - 52), (cx + 5, cy - 33), 255, -1)

    cv2.imwrite(os.path.join(WORK, "debug_mask.png"), mask)
    clean = cv2.inpaint(img, mask, 4, cv2.INPAINT_TELEA)
    # A second, gentler pass on the large meter areas smooths the fill.
    big = rect_mask(img.shape, LEVEL_BARS + WAVEFORMS)
    clean = cv2.inpaint(clean, big, 9, cv2.INPAINT_NS)
    cv2.imwrite(os.path.join(OUT, "skin.jpg"), clean, [cv2.IMWRITE_JPEG_QUALITY, 92])

    # Clean knob cap from the SPACE knob: remove its pointer line and needle.
    cx, cy = KNOB_CENTRES[2]
    r = 44
    cap = img[cy - r:cy + r, cx - r:cx + r].copy()
    cmask = np.zeros(cap.shape[:2], np.uint8)
    cv2.rectangle(cmask, (r - 4, r - 30), (r + 4, r - 9), 255, -1)    # white pointer line
    cv2.rectangle(cmask, (r - 5, 0), (r + 5, r - 32), 255, -1)         # needle
    cap = cv2.inpaint(cap, cmask, 5, cv2.INPAINT_TELEA)
    # Round alpha: cap plus the dark collar around it, soft edge.
    yy, xx = np.mgrid[0:2 * r, 0:2 * r]
    dist = np.sqrt((xx - r + 0.5) ** 2 + (yy - r + 0.5) ** 2)
    alpha = np.clip((40.5 - dist) / 2.0, 0, 1)
    rgba = cv2.cvtColor(cap, cv2.COLOR_BGR2BGRA)
    rgba[:, :, 3] = (alpha * 255).astype(np.uint8)
    cv2.imwrite(os.path.join(OUT, "cap.png"), rgba)

    # Tab backgrounds without their text.
    for name, (x0, y0, x1, y1) in TABS.items():
        tab = img[y0:y1, x0:x1].copy()
        tmask = text_mask(tab, (8, 6, tab.shape[1] - 8, tab.shape[0] - 6), 40 if name == "on" else 70)
        if name == "on":
            # Dark text on gold: mask the dark pixels instead.
            gray = cv2.cvtColor(tab, cv2.COLOR_BGR2GRAY)
            inner = np.zeros_like(gray)
            inner[8:-8, 10:-10] = 255
            _, dark = cv2.threshold(gray, 110, 255, cv2.THRESH_BINARY_INV)
            tmask = cv2.dilate(dark & inner, np.ones((3, 3), np.uint8), iterations=2)
        tab = cv2.inpaint(tab, tmask, 4, cv2.INPAINT_TELEA)
        cv2.imwrite(os.path.join(OUT, "tab_%s.png" % name), tab)

    print("wrote", OUT)


if __name__ == "__main__":
    main()
