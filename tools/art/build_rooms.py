#!/usr/bin/env python3
"""Sanctuary Choir - a scene for every room (venue).

Each room has two mockups in design/mockups/rooms/: the room with its own choir, and the
same room empty. This script registers both onto the plug-in's frame, cuts every singer
out of the choir picture (the same YOLO + SAM steps as build_singers.py), lines them up
with the empty room by its architecture, and writes ui/assets/rooms/<room>/:

- base.webp: the empty room, alpha = the frame's opening (drawn under the singers);
- pXX.webp:  one cut-out per singer, XX = reveal order (front row first, centre out);
- fg.webp:   what stands just below the singers (rail, stands, candles), drawn last so
             the singers can move a little without showing a seam;
- plus ui/assets/rooms/manifest.json with placement, rows and order.

Run:  tools/art/.venv/bin/python tools/art/build_rooms.py [--rooms gospel ...] [--stage detect|all]
(see README.md; the models are the ones build_singers.py downloads).
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build_singers as bs  # noqa: E402  (detection, SAM, rows, occlusion, matting)

ROOT = HERE.parents[1]
ROOMS = ROOT / "design" / "mockups" / "rooms"
SKIN = ROOT / "ui" / "assets" / "skin" / "skin.jpg"
OUT = ROOT / "ui" / "assets" / "rooms"
WORK = HERE / ".work" / "rooms"

# Room key -> mockup file. "abbey-monks" is the Stone Abbey sung by monks (Gregorian style).
ROOM_FILES = {
    "cathedral": "grand-cathedral.png",
    "abbey": "stone-abbey.png",
    "abbey-monks": "stone-abbey-monks.png",
    "chapel": "wooden-chapel.png",
    "hall": "concert-hall.png",
    "gospel": "gospel-church.png",
    "studio": "studio-room.png",
}
# UI drawn into a mockup's scene that is not part of the room (painted out with the singers).
OVERLAYS = {"hall": [(190, 104, 505, 182)]}   # the Concert Hall mockup's venue picker
# Rooms whose singers stand behind microphones and music stands, and what SAM misses there:
# - headgear: headphones (SAM keeps the head and drops the ear cups and headband);
# - hidden: points (choir picture) on a body seen again below a music stand's desk;
# - props: stands, desks and mics in front of the singers are drawn over them (from the
#   empty room), so the singers sway behind them instead of carrying holes that slide over
#   the room.
ROOM_FIXES = {
    "studio": dict(headgear=True, hidden=[(505, 505), (515, 535)], props=True),
}
PROPS_SAME = 16   # max colour difference (choir picture vs empty room) of a prop pixel


def register_to_skin(img, skin):
    """Similarity transform mockup -> skin, from SIFT matches on the frame around the
    scene (the side windows and the top bar), which every mockup shares."""
    g0 = cv2.cvtColor(skin, cv2.COLOR_BGR2GRAY)
    g1 = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    m = np.zeros_like(g0)
    m[0:76, :] = 255
    m[76:585, 0:150] = 255
    m[76:585, 1300:] = 255
    sift = cv2.SIFT_create(nfeatures=6000)
    k0, d0 = sift.detectAndCompute(g0, m)
    k1, d1 = sift.detectAndCompute(g1, m)
    ms = cv2.BFMatcher().knnMatch(d1, d0, k=2)
    good = [a for a, b in ms if a.distance < 0.72 * b.distance]
    src = np.float32([k1[a.queryIdx].pt for a in good])
    dst = np.float32([k0[a.trainIdx].pt for a in good])
    A, inl = cv2.estimateAffinePartial2D(src, dst, method=cv2.RANSAC, ransacReprojThreshold=2.0,
                                         maxIters=20000, confidence=0.999)
    inl = inl.ravel().astype(bool)
    res = np.linalg.norm(src[inl] @ A[:, :2].T + A[:, 2] - dst[inl], axis=1)
    H, W = skin.shape[:2]
    warped = cv2.warpAffine(img, A, (W, H), flags=cv2.INTER_LANCZOS4, borderMode=cv2.BORDER_REPLICATE)
    info = dict(matches=len(good), inliers=int(inl.sum()), scale=float(np.hypot(A[0, 0], A[1, 0])),
                angle_deg=float(np.degrees(np.arctan2(A[1, 0], A[0, 0]))), tx=float(A[0, 2]), ty=float(A[1, 2]),
                median_residual=float(np.median(res)))
    return warped, info


def load_rooms(keys):
    skin = cv2.imread(str(SKIN), cv2.IMREAD_COLOR)
    out = {}
    for k in keys:
        cache = WORK / f"{k}_registered.png"
        if cache.exists():
            out[k] = cv2.imread(str(cache), cv2.IMREAD_COLOR)
            continue
        img = cv2.imread(str(ROOMS / ROOM_FILES[k]), cv2.IMREAD_COLOR)
        warped, info = register_to_skin(img, skin)
        bs.log(f"{k}: registered {info}")
        cv2.imwrite(str(cache), warped)
        (WORK / f"{k}_registration.json").write_text(json.dumps(info, indent=1))
        out[k] = warped
    return skin, out


def detect_stage(keys):
    """The slow part (YOLO + SAM), cached in .work/rooms by the build_singers helpers."""
    _, rooms = load_rooms(keys)
    for k in keys:
        img = rooms[k]
        people = bs.detect_people(img, f"room_{k}")
        bs.log(f"{k}: {len(people)} people")
        bs.sam_logits(img, f"room_{k}", people)
        bs.detect_books(img, f"room_{k}")
        bs.pose_keypoints(img, f"room_{k}", people)


EMPTY_FILES = {
    "cathedral": "grand-cathedral-empty.png",
    "abbey": "stone-abbey-empty.png",
    "abbey-monks": "stone-abbey-empty.png",
    "chapel": "wooden-chapel-empty.png",
    "hall": "concert-hall-empty.png",
    "gospel": "gospel-church-empty.png",
    "studio": "studio-room-empty.png",
}
# The part of the frame the rooms are drawn into (base images are cropped to it).
CROP = (128, 72, 1322, 584)
# How far below a singer's cut-off edge the scenery in front (rail, pews, stands) is
# redrawn over the singers, so they can move a little without showing a seam.
FG_BAND = 7
OPENING_THRESHOLD = 24.0
OPENING_GROW = 5   # px: the threshold sits a little inside the blurred edge


def load_empty(keys, skin):
    out = {}
    for k in keys:
        f = EMPTY_FILES[k]
        cache = WORK / f"{Path(f).stem}_registered.png"
        if not cache.exists():
            img = cv2.imread(str(ROOMS / f), cv2.IMREAD_COLOR)
            warped, info = register_to_skin(img, skin)
            bs.log(f"{f}: registered {info}")
            cv2.imwrite(str(cache), warped)
        out[k] = cv2.imread(str(cache), cv2.IMREAD_COLOR)
    return out


def opening_mask(skin, images):
    """Alpha of the frame's opening, from where every room differs from the skin (the
    frame is the same art in every mockup, the room inside it is not)."""
    stack = np.stack([cv2.GaussianBlur(im, (7, 7), 0).astype(np.float32) for im in [skin] + list(images)])
    med = np.median(stack, axis=0)
    d = np.median(np.abs(stack - med), axis=0).max(axis=2)   # the frame is the same everywhere, the rooms are not
    d = cv2.GaussianBlur(d, (0, 0), 6)
    H, W = d.shape
    m = (d > OPENING_THRESHOLD).astype(np.uint8)
    box = np.zeros_like(m)
    x0, y0, x1, y1 = CROP
    box[y0:y1, x0:x1] = 1
    m &= box
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, bs.disk(3))
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, bs.disk(9))
    n, cc, st, _ = cv2.connectedComponentsWithStats(m, 4)
    main = 1 + int(np.argmax(st[1:, cv2.CC_STAT_AREA]))
    m = (cc == main).astype(np.uint8)
    # fill holes (scenery that happens to match the skin)
    inv = (1 - m).astype(np.uint8)
    n, cc, st, _ = cv2.connectedComponentsWithStats(inv, 4)
    for k in range(1, n):
        x, y, w, h, area = st[k]
        if x > 0 and y > 0 and x + w < W and y + h < H:
            m[cc == k] = 1
    m = cv2.dilate(m, bs.disk(OPENING_GROW))
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, bs.disk(15))   # smooth outline: no frame fragments
    a = cv2.GaussianBlur(m.astype(np.float32), (0, 0), 0.8)
    return np.clip(a, 0, 1)


def warp_sprite(rgba, org, A, shape):
    """Moves a cut-out from the choir picture onto the empty room (similarity A), with
    premultiplied alpha so the soft edges keep their colour."""
    h, w = rgba.shape[:2]
    corners = np.array([[org[0], org[1]], [org[0] + w, org[1]], [org[0], org[1] + h], [org[0] + w, org[1] + h]], np.float64)
    t = corners @ np.array(A)[:, :2].T + np.array(A)[:, 2]
    x0, y0 = int(np.floor(t[:, 0].min())) - 1, int(np.floor(t[:, 1].min())) - 1
    x1, y1 = int(np.ceil(t[:, 0].max())) + 1, int(np.ceil(t[:, 1].max())) + 1
    M = np.array(A, np.float64).copy()
    M[:, 2] = M[:, :2] @ np.array(org, np.float64) + M[:, 2] - [x0, y0]
    a = rgba[..., 3:4].astype(np.float32) / 255
    pm = np.dstack([rgba[..., :3].astype(np.float32) * a, a[..., 0] * 255])
    out = cv2.warpAffine(pm, M, (x1 - x0, y1 - y0), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
    al = out[..., 3:4] / 255
    rgb = np.where(al > 1e-4, out[..., :3] / np.maximum(al, 1e-4), 0)
    res = np.dstack([np.clip(rgb, 0, 255), np.clip(out[..., 3], 0, 255)]).astype(np.uint8)
    return bs.tight(res, (x0, y0))


def rescue_sam(tier, name, people, logits):
    """SAM sometimes keeps only the head of a big figure standing in front of the choir (the
    director, the conductor): the heads of the singers behind, given as negative points, fall
    inside its box. Such masks (far smaller than their box) are redone with a point on the
    body as well and only the heads outside the box as negatives; light robes and stoles SAM
    is unsure of inside the outline are filled in."""
    cache = WORK / f"samfix2_{name}.npz"
    fixed = dict(np.load(cache)) if cache.exists() else {}
    heads = np.array([p["head"] for p in people])
    sam = None
    changed = False
    for i, p in enumerate(people):
        b = np.array(p["box"], float)
        bw, bh = b[2] - b[0], b[3] - b[1]
        lg, (cx0, cy0) = logits[i]
        if (lg > 0).sum() > 0.12 * bw * bh:
            continue
        if f"l{i}" not in fixed:
            if sam is None:
                from sam_util import SoftSAM
                sam = SoftSAM(str(bs.CACHE / "sam2.1_l.pt"))
            X0, Y0, X1, Y1 = CROP
            cx0, cx1 = int(max(X0, b[0] - 0.1 * bw)), int(min(X1, b[2] + 0.1 * bw))
            cy0, cy1 = int(max(Y0, b[1] - 0.15 * bh)), int(min(Y1, b[3] + 0.1 * bh))
            sam.set_crop(tier[cy0:cy1, cx0:cx1])
            box = [b[0] - cx0, b[1] - cy0, b[2] - cx0, b[3] - cy0]
            hx, hy = heads[i]
            cx = (b[0] + b[2]) / 2 - cx0
            pos = [(hx - cx0, hy - cy0)] + [(cx + dx * bw, hy - cy0 + dy * bh)
                                             for dx, dy in ((0, 0.3), (0, 0.55), (-0.08, 0.45), (0.08, 0.45), (0, 0.8))]
            neg = [(x - cx0, y - cy0) for j, (x, y) in enumerate(heads)
                   if j != i and cx0 < x < cx1 and cy0 < y < cy1 and not (b[0] < x < b[2] and b[1] < y < b[3])]
            new, score = sam.logits(box, pos, neg)
            # Fill what the outline encloses (closing it along the bottom, where the figure
            # is cut off by the frame or the rail).
            m = (new > 0).astype(np.uint8)
            m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9)))
            m[-1, :] |= m[-2, :] | m[-3, :]
            filled = m.copy()
            ff = np.zeros((m.shape[0] + 2, m.shape[1] + 2), np.uint8)
            cv2.floodFill(filled, ff, (0, 0), 2)
            inside = (filled == 0) | (m > 0)
            new = np.where(inside & (new < 1.0), 1.0, new).astype(np.float32)
            fixed[f"l{i}"] = new.astype(np.float16)
            fixed[f"o{i}"] = np.array((cx0, cy0))
            changed = True
            bs.log(f"  {name}: person {i} redone (mask {(lg > 0).sum() / (bw * bh):.2f} of its box -> "
                   f"{(new > 0).sum() / (bw * bh):.2f}, score {score:.3f})")
        logits[i] = (fixed[f"l{i}"].astype(np.float32), tuple(int(v) for v in fixed[f"o{i}"]))
    if changed:
        np.savez_compressed(cache, **fixed)
    return logits


def _region_logits(mask):
    """A hard (logits, origin) window for a boolean mask."""
    ys, xs = np.nonzero(mask)
    x0, y0, x1, y1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
    return np.where(mask[y0:y1, x0:x1], 8.0, -8.0).astype(np.float32), (int(x0), int(y0))


def empty_difference(tier, base, A):
    """Per pixel, how different the choir picture is from the empty room (max over the
    colour channels, lightly blurred), in choir-picture coordinates."""
    H, W = tier.shape[:2]
    base_t = cv2.warpAffine(base, cv2.invertAffineTransform(A), (W, H), flags=cv2.INTER_LINEAR,
                            borderMode=cv2.BORDER_REPLICATE)
    return np.abs(cv2.GaussianBlur(tier, (3, 3), 0).astype(np.int16)
                  - cv2.GaussianBlur(base_t, (3, 3), 0).astype(np.int16)).max(axis=2)


def worn_and_hidden(tier, name, people, logits, lab, fix, diff):
    """What SAM leaves out of a singer in a studio: headphones (each head is segmented again
    with a box around it and points on the ears, and what it gains around the head is
    added) and a body seen again below a music stand (segmented from the room's hint
    points; only pixels that differ from the empty room, so the desk itself stays out)."""
    cache = WORK / f"worn_{name}.npz"
    done = dict(np.load(cache)) if cache.exists() else {}
    H, W = lab.shape
    X0, Y0, X1, Y1 = CROP
    kps = bs.pose_keypoints(tier, name, people)
    heads = np.array([p["head"] for p in people])
    sam = None
    changed = False
    regions = []

    def get_sam():
        nonlocal sam
        if sam is None:
            from sam_util import SoftSAM
            sam = SoftSAM(str(bs.CACHE / "sam2.1_l.pt"))
        return sam

    def others(i, c0, r0, c1, r1):
        return [(x - c0, y - r0) for j, (x, y) in enumerate(heads) if j != i and c0 < x < c1 and r0 < y < r1]

    for i, p in enumerate(people):
        kp = kps[i]
        if fix.get("headgear") and kp is not None:
            key = f"h{i}"
            if key not in done:
                ears = np.array(kp)[[3, 4], :2]
                hw = max(abs(ears[0, 0] - ears[1, 0]), 50.0)
                cx, ey = ears[:, 0].mean(), ears[:, 1].mean()
                hb = [cx - 0.95 * hw, ey - 1.25 * hw, cx + 0.95 * hw, ey + 0.55 * hw]
                c0, c1 = int(max(X0, hb[0] - 0.4 * hw)), int(min(X1, hb[2] + 0.4 * hw))
                r0, r1 = int(max(Y0, hb[1] - 0.3 * hw)), int(min(Y1, hb[3] + 0.4 * hw))
                s = get_sam()
                s.set_crop(tier[r0:r1, c0:c1])
                pos = [(heads[i][0] - c0, heads[i][1] - r0)] + [(x - c0, y - r0) for x, y in ears]
                lg, score = s.logits([hb[0] - c0, hb[1] - r0, hb[2] - c0, hb[3] - r0], pos, others(i, c0, r0, c1, r1))
                m = np.zeros((H, W), bool)
                inbox = np.zeros(lg.shape, bool)
                inbox[max(0, int(hb[1] - r0)):int(hb[3] - r0), max(0, int(hb[0] - c0)):int(hb[2] - c0)] = True
                m[r0:r1, c0:c1] = (lg > 0) & inbox
                done[key] = np.packbits(m)
                changed = True
                bs.log(f"  {name}: person {i} head again (score {score:.3f})")
            m = np.unpackbits(done[key])[: H * W].reshape(H, W).astype(bool)
            gain = m & ((lab < 0) | (lab == i))
            if gain.any():
                regions.append((i, gain))
        hints = [h for h in fix.get("hidden", [])
                 if min(range(len(people)), key=lambda j: abs(np.mean([kps[j][q][0] for q in (11, 12)]) - h[0])
                        if kps[j] is not None else 1e9) == i]
        if hints:
            key = f"b{i}"
            if key not in done:
                hy = float(np.mean([kp[q][1] for q in (11, 12)]))
                xs = [h[0] for h in hints]
                c0, c1 = int(max(X0, min(xs) - 120)), int(min(X1, max(xs) + 120))
                r0, r1 = int(hy - 80), Y1
                s = get_sam()
                s.set_crop(tier[r0:r1, c0:c1])
                box = [max(0, min(xs) - c0 - 70), hy - 30 - r0, min(c1 - c0 - 1, max(xs) - c0 + 70), r1 - r0 - 1]
                lg, score = s.logits(box, [(x - c0, y - r0) for x, y in hints], others(i, c0, r0, c1, r1))
                m = np.zeros((H, W), bool)
                m[r0:r1, c0:c1] = lg > 0
                done[key] = np.packbits(m)
                changed = True
                bs.log(f"  {name}: person {i} body below the stand (score {score:.3f})")
            m = np.unpackbits(done[key])[: H * W].reshape(H, W).astype(bool)
            m &= (lab < 0) & (diff >= PROPS_SAME)
            n, cc, _, _ = cv2.connectedComponentsWithStats(m.astype(np.uint8), 8)
            keep = [cc[int(y), int(x)] for x, y in hints if cc[int(y), int(x)] > 0]
            gain = np.isin(cc, keep)
            if gain.any():
                regions.append((i, gain))
    if changed:
        np.savez_compressed(cache, **done)
    for i, gain in regions:
        logits[i] = bs.merge_logits(logits[i], _region_logits(gain))
        bs.log(f"  {name}: person {i} gains {int(gain.sum())} px")
    return logits


def props_in_front(lab, N, diff):
    """Stands, desks and microphones in front of the singers (choir-picture coordinates):
    pixels the choir picture shares with the empty room with a singer on both sides of them
    (above and below, or left and right), in shapes that carry on beyond the singer the
    way a stand or a desk does. Pockets of background a singer encloses (between an arm and
    the body) do not, and are left alone."""
    same = cv2.morphologyEx((diff < PROPS_SAME).astype(np.uint8), cv2.MORPH_CLOSE, bs.disk(1)) > 0
    person = lab >= 0
    env = np.zeros_like(person)
    for i in range(N):
        m = lab == i
        if not m.any():
            continue
        cols = np.maximum.accumulate(m, axis=0) & np.maximum.accumulate(m[::-1], axis=0)[::-1]
        rows = np.maximum.accumulate(m, axis=1) & np.maximum.accumulate(m[:, ::-1], axis=1)[:, ::-1]
        env |= cols | rows
    props = same & ~person & env
    beyond = same & ~person & ~env
    n, cc, st, _ = cv2.connectedComponentsWithStats(props.astype(np.uint8), 8)
    keep = np.zeros(n, bool)
    for q in range(1, n):
        if st[q, cv2.CC_STAT_AREA] < 120:   # specks along a hairline are not stands
            continue
        x, y, w, h = st[q, :4]
        x0, y0, x1, y1 = max(0, x - 3), max(0, y - 3), x + w + 3, y + h + 3
        comp = cc[y0:y1, x0:x1] == q
        keep[q] = (bs.dil(comp, 2) & beyond[y0:y1, x0:x1]).sum() >= 3
    return keep[cc] & props


def build(keys):
    skin = cv2.imread(str(SKIN), cv2.IMREAD_COLOR)
    _, rooms = load_rooms(keys)
    empties = load_empty(keys, skin)
    allimgs = list(rooms.values()) + list(empties.values())
    opening = opening_mask(skin, allimgs)
    cv2.imwrite(str(WORK / "opening.png"), (opening * 255).astype(np.uint8))
    H, W = skin.shape[:2]
    X0, Y0, X1, Y1 = CROP
    manifest_path = OUT / "manifest.json"
    old = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    rooms_out = old.get("rooms", {})
    for k in keys:
        name = f"room_{k}"
        tier, base = rooms[k], empties[k].copy()
        for (ox0, oy0, ox1, oy1) in OVERLAYS.get(k, []):
            mask = np.zeros(base.shape[:2], np.uint8)
            mask[oy0:oy1, ox0:ox1] = 255
            base = cv2.inpaint(base, mask, 7, cv2.INPAINT_TELEA)
        people = bs.detect_people(tier, name)
        N = len(people)
        reg = bs.register_architecture(base, tier, [p["box"] for p in people])
        A = np.array(reg["A"])
        bs.log(f"== {k}: {N} people; choir -> empty room scale={reg['scale']:.4f} angle={reg['angle_deg']:.3f} "
               f"shift={reg['shift_at_choir'][0]:+.1f},{reg['shift_at_choir'][1]:+.1f} inliers={reg['inliers']}")
        logits = rescue_sam(tier, name, people, bs.sam_logits(tier, name, people))
        lab = bs.ownership((H, W), logits)
        fix = ROOM_FIXES.get(k, {})
        diff = empty_difference(tier, base, A) if fix else None
        if fix.get("headgear") or fix.get("hidden"):
            logits = worn_and_hidden(tier, name, people, logits, lab, fix, diff)
            lab = bs.ownership((H, W), logits)
        for stage in ("held", "limbs", "enclosed"):
            if stage == "held":
                objs, recs = bs.held_objects(tier, name, people, lab)
            elif stage == "limbs":
                objs, recs = bs.refine_limbs(tier, name, people, lab, logits)
            else:
                objs, recs = bs.enclosed_gaps(tier, name, people, lab, bs.assign_rows(people, lab, N))
            for (l2, org, owner) in objs:
                logits[owner] = bs.merge_logits(logits[owner], (l2, org))
            bs.log(f"  {stage}: attached {len(objs)} region(s)")
            lab = bs.ownership((H, W), logits)
        rows = bs.assign_rows(people, lab, N)
        rel = bs.occlusion_graph(lab, people, rows, N)
        front_edges = [((i, j) if r["score"] > 0 else (j, i)) for (i, j), r in rel.items() if r["score"] != 0]
        order, _ = bs.reveal_order(people, rows, front_edges, N)
        rank = {i: n for n, i in enumerate(order)}
        cv2.imwrite(str(WORK / f"labels_{k}.png"), bs.label_overlay(tier, lab, people, order, rows, front_edges))

        # Scenery just below each singer's cut-off edge (the rail, pews, stands in front of
        # them), in the empty room's coordinates: redrawn over the singers.
        person = (lab >= 0).astype(np.uint8)
        person_b = cv2.warpAffine(person, A, (W, H), flags=cv2.INTER_NEAREST) > 0
        tier_b = cv2.warpAffine(tier, A, (W, H), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
        below = np.zeros_like(person_b)
        for dy in range(1, FG_BAND + 1):
            below[dy:] |= person_b[:-dy]
        same = np.abs(tier_b.astype(np.int16) - base.astype(np.int16)).max(axis=2) < 40
        fg = below & ~person_b & same & (opening > 0.5)
        if fix.get("props"):
            props = props_in_front(lab, N, diff)
            fg |= (cv2.warpAffine(props.astype(np.uint8), A, (W, H), flags=cv2.INTER_NEAREST) > 0) & (opening > 0.5)
            bs.log(f"  props in front of the singers: {int(props.sum())} px")
        fg_a = cv2.GaussianBlur(fg.astype(np.float32), (0, 0), 0.7)
        fg_a = np.where(fg, 1.0, fg_a) * opening
        # hidden (hard-edged) zone for matting, in choir-picture coordinates
        Ainv = cv2.invertAffineTransform(A)
        hidden = cv2.warpAffine(fg.astype(np.uint8), Ainv, (W, H), flags=cv2.INTER_NEAREST) > 0

        out_dir = OUT / k
        if out_dir.exists():
            shutil.rmtree(out_dir)
        out_dir.mkdir(parents=True)
        fronts = {i: set() for i in range(N)}
        for (i, j), r in rel.items():
            if r["score"] > 0:
                fronts[j].add(i)
            elif r["score"] < 0:
                fronts[i].add(j)
            else:
                a_, b_ = (i, j) if rank[i] < rank[j] else (j, i)
                fronts[b_].add(a_)
        z = order[::-1]
        people_out = []
        sprites = {}
        for i in range(N):
            rgba, org = bs.matte_person(tier, lab, i, fronts[i], logits[i], hidden)
            rgba, org = bs.tight(rgba, org)
            sprites[i] = warp_sprite(rgba, org, A, (H, W))
        for n_, i in enumerate(order):
            rgba, (x, y) = sprites[i]
            fn = f"{k}/p{n_:02d}.webp"
            bs.save_webp(OUT / fn, rgba)
            a = rgba[..., 3].astype(np.float64)
            yy, xx = np.mgrid[:a.shape[0], :a.shape[1]]
            head = np.array(people[i]["head"]) @ A[:, :2].T + A[:, 2]
            people_out.append(dict(file=fn, x=int(x), y=int(y), w=int(rgba.shape[1]), h=int(rgba.shape[0]),
                                   row=int(rows[i]), order=n_, z=z.index(i),
                                   cx=round(float((a * xx).sum() / a.sum()) + x, 1), cy=round(float((a * yy).sum() / a.sum()) + y, 1),
                                   head=[round(float(head[0]), 1), round(float(head[1]), 1)]))
        # The empty room, cut to the frame's opening.
        base_rgba = np.dstack([base[..., ::-1], np.round(opening * 255).astype(np.uint8)])[Y0:Y1, X0:X1]
        Image = bs.Image
        Image.fromarray(base_rgba, "RGBA").save(out_dir / "base.webp", "WEBP", quality=88, alpha_quality=100, method=6)
        fg_rgba = np.dstack([base[..., ::-1], np.round(fg_a * 255).astype(np.uint8)])
        ys, xs = np.nonzero(fg_a > 0.004)
        fx0, fy0, fx1, fy1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
        bs.save_webp(out_dir / "fg.webp", fg_rgba[fy0:fy1, fx0:fx1], lossless=True)
        rooms_out[k] = dict(base=dict(file=f"{k}/base.webp", x=X0, y=Y0, w=X1 - X0, h=Y1 - Y0),
                            fg=dict(file=f"{k}/fg.webp", x=int(fx0), y=int(fy0), w=int(fx1 - fx0), h=int(fy1 - fy0)),
                            count=N, rows=int(rows.max()) + 1, people=people_out)
        # Check: the full room next to the original mockup.
        sprites_z = [(sprites[i][0], *sprites[i][1]) for i in z]
        comp = bs.composite(base[..., ::-1].copy(), sprites_z, fg_rgba[fy0:fy1, fx0:fx1], (int(fx0), int(fy0)))
        tier_b_rgb = tier_b[..., ::-1]
        cv2.imwrite(str(WORK / f"verify_{k}.jpg"), np.hstack([comp, tier_b_rgb])[Y0:Y1, :, ::-1])
        half = [i for i in order[: max(1, N // 3)]]
        comp3 = bs.composite(base[..., ::-1].copy(), [(sprites[i][0], *sprites[i][1]) for i in z if i in half],
                             fg_rgba[fy0:fy1, fx0:fx1], (int(fx0), int(fy0)))
        cv2.imwrite(str(WORK / f"verify_{k}_third.jpg"), comp3[Y0:Y1, :, ::-1])
        bs.log(f"  wrote {N} singers, rows {np.bincount(rows).tolist()}")
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = dict(_notes="One room per venue: draw base, then the first k people (reveal order) from the highest "
                           "order down, then fg. x,y,w,h in plug-in (1448x1086) pixels; row 0 = front.",
                    rooms=rooms_out)
    manifest_path.write_text(json.dumps(manifest, indent=1))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rooms", nargs="*", default=list(ROOM_FILES))
    ap.add_argument("--stage", choices=["detect", "all"], default="all")
    args = ap.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    bs.WORK = WORK
    bs.ensure_weights()
    detect_stage(args.rooms)
    if args.stage == "detect":
        return
    build(args.rooms)


if __name__ == "__main__":
    main()
