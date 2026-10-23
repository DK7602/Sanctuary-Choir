#!/usr/bin/env python3
"""Sanctuary Choir - the Stage page's art: the four sections on their risers and the lead
singer seen from the front and from behind.

From design/mockups/stage/stage-parts.webp (one picture: sopranos pink, tenors blue, altos
gold, basses green, then the lead from the front and from behind). Writes
ui/assets/stage/{sop,alt,ten,bas,lead-front,lead-back}.webp and manifest.json.

1. Pieces: each section and each view of the lead, cut out by the picture's alpha. Each
   lead touches the risers above him: SAM (box prompt) tells him from them, and the riser
   behind his hands is inpainted.
2. Proportions: the front rows were drawn too short. Their faces match the rows behind, but
   from shoulders to hips they are 7-15% shorter (measured against eyes to shoulders, by YOLO
   pose). Each front singer's body, shoulders to ankles, is made as much taller as that
   takes (at most MAX_STRETCH) and the head lifted to match, as one layer (SAM outlines)
   warped column by column so neighbours keep overlapping as they did. What the taller
   figures no longer cover is inpainted.
3. Quality: 4x Real-ESRGAN (x4plus), then GFPGAN v1.4 redraws the faces (RetinaFace finds
   them, the face parser blends them back), at GFPGAN_WEIGHT so each face keeps its look.
4. Sizes: scaled to OUT_WIDTH (sections) / OUT_HEIGHT (leads), WebP with alpha.

Run:  tools/art/.venv/bin/python tools/art/build_stage_art.py [--stage pieces|fix|all]
Needs the YOLO pose and SAM 2.1 weights build_singers.py downloads, plus the Real-ESRGAN,
GFPGAN and facexlib weights in tools/art/.cache/restore (see README.md).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / "design" / "mockups" / "stage" / "stage-parts.webp"
OUT = ROOT / "ui" / "assets" / "stage"
WORK = HERE / ".work" / "stage"
CACHE = HERE / ".cache"
RESTORE = CACHE / "restore"

MAX_STRETCH = 1.16     # front rows' bodies, shoulders to ankles, at most
GFPGAN_WEIGHT = 0.6    # 0 keeps the drawn face, 1 the restored one
OUT_WIDTH = 1200       # sections
OUT_HEIGHT = 480       # leads
OPAQUE = 248           # the picture's alpha tops out at 253 (lossy WebP): this and above is solid

# Pieces in reading order of the picture (top left, top right, bottom left, bottom right).
SECTIONS = ["sop", "ten", "alt", "bas"]
# Each lead: the section piece he touches, a box around him and a point on his chest (in
# the picture's pixels), for SAM.
LEADS = {"lead-front": ("alt", (515, 655, 800, 1010), (632, 830)),
         "lead-back": ("bas", (780, 655, 1045, 1010), (888, 830))}


def log(*a):
    print(*a, flush=True)


def load_source():
    rgba = np.asarray(Image.open(SRC).convert("RGBA")).astype(np.float32)
    a = np.clip(rgba[..., 3] * 255.0 / OPAQUE, 0, 255)
    rgba[..., 3] = np.where(a < 6, 0, a)
    return rgba


def components(alpha, min_area=5000):
    n, cc, st, _ = cv2.connectedComponentsWithStats((alpha > 8).astype(np.uint8), 8)
    comps = [(k, tuple(int(v) for v in st[k][:4])) for k in range(1, n) if st[k][4] >= min_area]
    comps.sort(key=lambda c: (c[1][1] // 300, c[1][0]))
    return cc, comps


_models = {}


def _model(kind):
    if kind not in _models:
        from ultralytics import SAM, YOLO
        _models[kind] = SAM(str(CACHE / "sam2.1_l.pt")) if kind == "sam" else YOLO(str(CACHE / "yolo11x-pose.pt"))
    return _models[kind]


def sam_mask(rgb, box, point=None):
    """SAM 2.1 mask for a box (x0, y0, x1, y1), and a point inside, in rgb (uint8, HxWx3)."""
    kw = dict(points=[list(point)], labels=[1]) if point is not None else {}
    r = _model("sam").predict(rgb[..., ::-1].copy(), bboxes=[list(box)], verbose=False, **kw)[0]
    return r.masks.data[0].cpu().numpy().astype(bool)


def singers(rgb, scale=3):
    """YOLO pose on rgb (3x up, the figures are small): every singer whose eyes, nose and
    shoulders are clear, as dicts of x, eye, sh (shoulder line), hip, ank (None if hidden),
    box."""
    up = cv2.resize(rgb, None, fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)
    size = int(np.ceil(max(up.shape[:2]) / 32) * 32)
    r = _model("pose").predict(up[..., ::-1].copy(), imgsz=size, conf=0.2, iou=0.5, verbose=False)[0]
    out = []
    for b, k in zip(r.boxes.xyxy.cpu().numpy() / scale, r.keypoints.data.cpu().numpy()):
        k[:, :2] /= scale
        sure = k[:, 2] > 0.5
        if not sure[[0, 1, 2, 5, 6, 11, 12]].all():
            continue
        out.append(dict(x=float(k[5:7, 0].mean()), eye=float(k[1:3, 1].mean()), sh=float(k[5:7, 1].mean()),
                        hip=float(k[11:13, 1].mean()), box=[float(v) for v in b],
                        ank=float(k[15:17, 1].mean()) if sure[15:17].all() else None))
    return out


def flatten(rgba, grey=60):
    a = rgba[..., 3:4] / 255.0
    return np.clip(rgba[..., :3] * a + grey * (1 - a), 0, 255).astype(np.uint8)


def inpaint_rgba(rgba, hole, radius=4):
    """Fills hole (bool) in colour and alpha from around it."""
    out = rgba.copy()
    m = hole.astype(np.uint8) * 255
    rgb = np.clip(rgba[..., :3], 0, 255).astype(np.uint8)
    out[..., :3] = cv2.inpaint(rgb, m, radius, cv2.INPAINT_TELEA)
    a = np.clip(rgba[..., 3], 0, 255).astype(np.uint8)
    out[..., 3] = cv2.inpaint(a, m, radius, cv2.INPAINT_TELEA)
    return out


def tight(rgba, pad=4):
    ys, xs = np.nonzero(rgba[..., 3] > 4)
    y0, y1, x0, x1 = max(0, ys.min() - pad), ys.max() + 1 + pad, max(0, xs.min() - pad), xs.max() + 1 + pad
    return rgba[y0:y1, x0:x1]


def cut_pieces(src):
    """{name: rgba} for the four sections and the two leads."""
    cc, comps = components(src[..., 3])
    assert len(comps) == 4, f"expected 4 pieces, found {len(comps)}"
    pieces = {}
    for name, (k, (x, y, w, h)) in zip(SECTIONS, comps):
        own = cc[y:y + h, x:x + w] == k
        rgba = src[y:y + h, x:x + w].copy()
        rgba[..., 3] *= own
        leads = [(n, box, pt) for n, (sec, box, pt) in LEADS.items() if sec == name]
        if leads:
            # The lead stands below these risers and reaches up over their lower edge.
            section = rgba.copy()
            for lead_name, box, pt in leads:
                bx = (box[0] - x, box[1] - y, box[2] - x, box[3] - y)
                solid = rgba[..., 3] > 8
                lead = sam_mask(flatten(rgba), bx, (pt[0] - x, pt[1] - y)) & solid & _inside(solid.shape, bx)
                # Whatever the lead's outline leaves hanging below the risers (the head of his
                # microphone, specks) is his too.
                _, lab, st, _ = cv2.connectedComponentsWithStats((solid & ~lead).astype(np.uint8), 8)
                main = 1 + int(np.argmax(st[1:, 4]))
                lead |= solid & (lab != main) & (lab > 0) & _inside(solid.shape, bx)
                lr = rgba.copy()
                lr[..., 3] *= lead
                pieces[lead_name] = tight(lr)
                # The section: the lead taken away and the riser behind his head and hands
                # filled in, down to its front edge carried across where he stood.
                section[..., 3] *= ~lead
                hole = (cv2.dilate(lead.astype(np.uint8), np.ones((9, 9), np.uint8)) > 0) & solid
                edge = _front_edge(section[..., 3], hole.any(0))
                rows = np.arange(h)[:, None]
                hole &= rows <= edge[None, :]
                section = inpaint_rgba(section, hole)
                section[..., 3] = np.where(hole, 255, section[..., 3])
                # Below that edge, across his box, nothing of the section is left (the faint
                # outline of the lead's edge, a finger), nor anywhere apart from the risers.
                cols = np.zeros(w, bool)
                cols[max(0, bx[0]):bx[2]] = True
                cut = np.clip(edge[None, :] + 1.5 - rows, 0, 1)    # a soft 1 px edge
                section[..., 3] = np.where(cols[None, :], section[..., 3] * cut, section[..., 3])
                _, lab, st, _ = cv2.connectedComponentsWithStats((section[..., 3] > 2).astype(np.uint8), 8)
                section[..., 3] *= lab == 1 + int(np.argmax(st[1:, 4]))
                log(f"  {lead_name}: {int(lead.sum())} px, riser filled in behind him: {int(hole.sum())} px")
            pieces[name] = tight(section)
        else:
            pieces[name] = tight(rgba)
    return pieces


def _inside(shape, box):
    m = np.zeros(shape, bool)
    m[max(0, box[1]):box[3], max(0, box[0]):box[2]] = True
    return m


def _front_edge(alpha, gap, reach=60):
    """Row of the risers' front (lowest solid) edge in every column. Across the columns in
    gap, where the lead stood in front of it, the edge is a parabola through the columns
    either side."""
    solid = alpha > 128
    h, w = solid.shape
    edge = np.where(solid.any(0), h - 1 - np.argmax(solid[::-1], 0), -1).astype(np.float32)
    idx = np.nonzero(gap)[0]
    if len(idx):
        lo, hi = idx.min(), idx.max()
        near = np.r_[max(0, lo - reach):lo, hi + 1:min(w, hi + 1 + reach)]
        near = near[edge[near] >= 0]
        fit = np.polyfit(near, edge[near], 2)
        edge[lo:hi + 1] = np.polyval(fit, np.arange(lo, hi + 1))
    return edge


def fix_front_row(rgba, name):
    """The section with its front row's bodies stretched to the rows' behind proportions."""
    rgb = flatten(rgba)
    people = singers(rgb)
    # The front row: feet showing, and nobody's feet lower just in front of them.
    front = [p for p in people if p["ank"] is not None
             and not any(q["ank"] is not None and abs(q["x"] - p["x"]) < 60 and q["ank"] > p["ank"] + 15 for q in people)]
    back = [p for p in people if p not in front]
    torso = lambda p: (p["hip"] - p["sh"]) / (p["sh"] - p["eye"])   # shoulders to hips, in eyes to shoulders
    target = float(np.median([torso(p) for p in back]))
    front.sort(key=lambda p: p["x"])
    for p in front:
        p["s"] = float(np.clip(target / torso(p), 1.0, MAX_STRETCH))
    log(f"  {name}: {len(front)} in the front row, stretched " + " ".join(f"{p['s']:.2f}" for p in front)
        + f" (torso {np.median([torso(p) for p in front]):.2f} against {target:.2f} behind)")

    # Their outlines (SAM, a box and a point on the chest each).
    h, w = rgba.shape[:2]
    layer = np.zeros((h, w), bool)
    for p in front:
        x0, y0, x1, y1 = p["box"]
        chest = (p["x"], p["sh"] + 0.4 * (p["hip"] - p["sh"]))
        layer |= sam_mask(rgb, (x0, y0, x1, y1), chest)
    layer &= rgba[..., 3] > 8

    # Column by column: the feet stay, shoulders to ankles grow s times, above moves up.
    xs = np.array([p["x"] for p in front])
    S = np.interp(np.arange(w), xs, [p["sh"] for p in front]).astype(np.float32)[None, :]
    F = np.interp(np.arange(w), xs, [p["ank"] for p in front]).astype(np.float32)[None, :]
    k = np.interp(np.arange(w), xs, [p["s"] for p in front]).astype(np.float32)[None, :]
    yo = np.arange(h, dtype=np.float32)[:, None]
    top = F - k * (F - S)
    map_y = np.where(yo >= F, yo, np.where(yo >= top, F - (F - yo) / k, yo + (k - 1) * (F - S)))
    map_x = np.broadcast_to(np.arange(w, dtype=np.float32)[None, :], (h, w)).copy()
    pre = rgba.copy()
    pre[..., 3] *= layer
    pre[..., :3] *= pre[..., 3:4] / 255.0
    moved = cv2.remap(pre, map_x, map_y.astype(np.float32), cv2.INTER_CUBIC, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
    moved = np.clip(moved, 0, 255)

    # Under them: the section with the front row taken out and filled in from around it (it
    # shows only where the taller figures no longer reach: beside a lifted hand, say).
    hole = layer & (moved[..., 3] < 250)
    base = inpaint_rgba(rgba, cv2.dilate(layer.astype(np.uint8), np.ones((3, 3), np.uint8)) > 0, radius=6)
    log(f"     uncovered and filled in: {int(hole.sum())} px")
    a = moved[..., 3:4] / 255.0
    out = base.copy()
    out[..., :3] = moved[..., :3] + base[..., :3] * (1 - a)
    out[..., 3] = moved[..., 3] + base[..., 3] * (1 - a[..., 0])
    return out, dict(front=front, layer=layer)


def bleed(rgba):
    """Colour under the transparent pixels taken from the nearest solid one, so upscaling
    gives no dark fringe at the outline."""
    solid = rgba[..., 3] >= 250
    _, lab = cv2.distanceTransformWithLabels((~solid).astype(np.uint8), cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL)
    ys, xs = np.nonzero(solid)
    src = np.zeros((lab.max() + 1, 2), np.int64)
    src[lab[ys, xs]] = np.c_[ys, xs]
    near = src[lab]
    out = rgba.copy()
    out[..., :3] = np.where(solid[..., None], rgba[..., :3], rgba[near[..., 0], near[..., 1], :3])
    return out


def upscale(rgba):
    """4x Real-ESRGAN (x4plus), colour and alpha."""
    from basicsr.archs.rrdbnet_arch import RRDBNet
    from realesrgan import RealESRGANer
    if "esrgan" not in _models:
        net = RRDBNet(num_in_ch=3, num_out_ch=3, num_feat=64, num_block=23, num_grow_ch=32, scale=4)
        _models["esrgan"] = RealESRGANer(scale=4, model_path=str(RESTORE / "RealESRGAN_x4plus.pth"), model=net,
                                         tile=256, tile_pad=12, pre_pad=0, half=False)
    bgra = np.clip(bleed(rgba), 0, 255).astype(np.uint8)[..., [2, 1, 0, 3]]
    out, _ = _models["esrgan"].enhance(bgra, outscale=4)
    return out[..., [2, 1, 0, 3]].astype(np.float32)


def restore_faces(rgba):
    """GFPGAN v1.4 on every face it finds, blended in at GFPGAN_WEIGHT."""
    from gfpgan import GFPGANer
    if "gfpgan" not in _models:
        # facexlib looks for its face detector and parser in ./gfpgan/weights.
        weights = WORK / "gfpgan" / "weights"
        weights.mkdir(parents=True, exist_ok=True)
        for f in ("detection_Resnet50_Final.pth", "parsing_parsenet.pth"):
            if not (weights / f).exists():
                (weights / f).symlink_to(RESTORE / f)
        here = os.getcwd()
        os.chdir(WORK)
        try:
            _models["gfpgan"] = GFPGANer(model_path=str(RESTORE / "GFPGANv1.4.pth"), upscale=1, arch="clean",
                                         channel_multiplier=2, bg_upsampler=None)
        finally:
            os.chdir(here)
    flat = flatten(rgba, grey=110)
    _, faces, restored = _models["gfpgan"].enhance(flat[..., ::-1].copy(), has_aligned=False, only_center_face=False,
                                                   paste_back=True)
    out = rgba.copy()
    out[..., :3] += GFPGAN_WEIGHT * (restored[..., ::-1].astype(np.float32) - flat)
    return out, len(faces)


def shrink(rgba, size):
    """Scales premultiplied (no halo), to size (w, h)."""
    pre = rgba.copy()
    pre[..., :3] *= pre[..., 3:4] / 255.0
    small = cv2.resize(pre, size, interpolation=cv2.INTER_AREA)
    a = np.maximum(small[..., 3:4], 1e-3)
    small[..., :3] = np.where(small[..., 3:4] > 0.5, small[..., :3] * 255.0 / a, 0)
    return np.clip(small, 0, 255)


def save_png(path, rgba):
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.clip(rgba, 0, 255).astype(np.uint8), "RGBA").save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", default="all", choices=["pieces", "fix", "all"])
    args = ap.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    src = load_source()
    log("cutting the pieces")
    pieces = cut_pieces(src)
    for name, rgba in pieces.items():
        save_png(WORK / f"1-{name}.png", rgba)
        log(f"  {name}: {rgba.shape[1]}x{rgba.shape[0]}")
    if args.stage == "pieces":
        return
    log("fixing the front rows")
    for name in SECTIONS:
        pieces[name], _ = fix_front_row(pieces[name], name)
        save_png(WORK / f"2-{name}.png", pieces[name])
    if args.stage == "fix":
        return
    log("upscaling and restoring the faces")
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for name, rgba in pieces.items():
        big = upscale(rgba)
        big, n = restore_faces(big)
        save_png(WORK / f"3-{name}.png", big)
        h, w = big.shape[:2]
        size = (OUT_WIDTH, round(h * OUT_WIDTH / w)) if name in SECTIONS else (round(w * OUT_HEIGHT / h), OUT_HEIGHT)
        final = shrink(big, size)
        Image.fromarray(final.astype(np.uint8), "RGBA").save(OUT / f"{name}.webp", quality=90, method=6)
        manifest[name] = dict(file=f"{name}.webp", w=size[0], h=size[1])
        log(f"  {name}: {n} faces, {w}x{h} -> {size[0]}x{size[1]}")
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")


if __name__ == "__main__":
    main()
