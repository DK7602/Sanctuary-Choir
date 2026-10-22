#!/usr/bin/env python3
"""Sanctuary Choir - singer sprite pipeline.

Cuts every singer out of the four AI-generated "tier" mockups, registers them onto
the empty-loft base mockup, extracts the loft front from the base as a foreground
layer, computes per-tier reveal order (front row first, centre-outward, respecting
occlusion) and writes ui/assets/choir/{tNN/pXX.webp, foreground.webp, manifest.json}.

Run:  tools/art/.venv/bin/python tools/art/build_singers.py      (see README.md)

Stages are cached in tools/art/.work/ (detections, SAM logits); pass --fresh to
recompute everything.
"""
from __future__ import annotations

import argparse
import heapq
import json
import shutil
import sys
import time
import urllib.request
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MOCKUPS = ROOT / "design" / "mockups"
OUT_DIR = ROOT / "ui" / "assets" / "choir"
CACHE = HERE / ".cache"
WORK = HERE / ".work"
sys.path.insert(0, str(HERE))

BASE_FILE = "perform-00-empty.png"
TIER_FILES = {
    "t10": "perform-10-singers.png",
    "t20": "perform-20-singers.png",
    "t33": "perform-33-singers.png",
    "t45": "perform-45-singers.png",
}
WEIGHTS = {
    "yolo11x.pt": "https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11x.pt",
    "yolo11x-pose.pt": "https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11x-pose.pt",
    "sam2.1_l.pt": "https://github.com/ultralytics/assets/releases/download/v8.3.0/sam2.1_l.pt",
}

# Church scene inside the UI chrome (x0, y0, x1, y1), identical in all mockups.
SCENE = (140, 85, 1310, 570)
# Symmetry axis of the loft (pulpit / window cross) in base coordinates.
CENTER_X = 725
# The tier's own rail cut on front-row bodies is tucked this many px below the top of
# the base foreground (so no flat cut edge is ever visible above the base rail).
RAIL_MARGIN = 3
# Detection: YOLO runs on the scene upscaled by this factor (back rows are small).
DET_UPSCALE = 2
DET_CONF = 0.25
# Occlusion decisions (see occlusion_graph): minimum |convexity| for a same-row
# edge, and the |convexity| needed to overrule the row order.
SHAPE_MIN = 0.08
SHAPE_OVERRIDE = 0.25
# WebP quality for the sprites (foreground is stored lossless, see build_foreground).
WEBP_QUALITY = 90

# Things in the BASE image that stand in front of the singers and rise above the
# rail line. Boxes are SAM prompts in base pixels (x0, y0, x1, y1).
FG_OBJECTS = {
    "lily_left": (143, 390, 280, 530),
    "lily_right": (1168, 390, 1308, 530),
    "candle_left": (276, 426, 292, 470),
    "candle_right": (1158, 426, 1174, 470),
}
# The pulpit top is a simple carpentry shape (SAM confuses it with the seat backs
# behind it), measured from the base image at 8x zoom: centre box top y=421, wings
# y=428 sloping down to the rail. Polygon in base pixels, closed below the rail.
PULPIT_POLY = [(632, 446), (633, 434), (669, 428.5), (689, 428.5), (689, 421), (756, 421), (756, 428.5),
               (780, 428.5), (816, 435), (818, 446)]
# Columns used to fit the straight rail ledge in the base image.
RAIL_FIT_X = [(262, 628), (822, 1150)]

# Manual corrections (filled in after visual review; empty means "trust the models").
EXTRA_PEOPLE: dict[str, list[tuple[float, float, float, float]]] = {}
DROP_PEOPLE: dict[str, list[int]] = {}


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# --------------------------------------------------------------------------- setup

def ensure_weights():
    CACHE.mkdir(parents=True, exist_ok=True)
    for name, url in WEIGHTS.items():
        p = CACHE / name
        if p.exists() and p.stat().st_size > 1_000_000:
            continue
        log(f"downloading {name} ...")
        tmp = p.with_suffix(".part")
        with urllib.request.urlopen(url, timeout=600) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f, 1 << 20)
        tmp.rename(p)


def imread(name):
    im = cv2.imread(str(MOCKUPS / name), cv2.IMREAD_COLOR)
    if im is None:
        raise FileNotFoundError(MOCKUPS / name)
    return im


def disk(r):
    return cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * r + 1, 2 * r + 1))


def dil(m, r):
    return cv2.dilate(m.astype(np.uint8), disk(r)) > 0 if r > 0 else m.astype(bool)


def ero(m, r):
    return cv2.erode(m.astype(np.uint8), disk(r)) > 0 if r > 0 else m.astype(bool)


# ---------------------------------------------------------------------- registration

def register_architecture(base, tier, people_boxes):
    """Similarity transform tier->base from SIFT matches on the church interior
    above the choir (windows, organ, lanterns, pillars), people masked out."""
    g0 = cv2.cvtColor(base, cv2.COLOR_BGR2GRAY)
    g1 = cv2.cvtColor(tier, cv2.COLOR_BGR2GRAY)
    m0 = np.zeros_like(g0)
    m0[88:320, 150:1300] = 255
    m1 = m0.copy()
    for b in people_boxes:
        x0, y0, x1, y1 = (np.asarray(b[:4]) + [-12, -12, 12, 12]).astype(int)
        m1[max(0, y0):y1, max(0, x0):x1] = 0
        m0[max(0, y0):y1, max(0, x0):x1] = 0  # same region in base: avoid one-sided features
    sift = cv2.SIFT_create(nfeatures=8000)
    k0, d0 = sift.detectAndCompute(g0, m0)
    k1, d1 = sift.detectAndCompute(g1, m1)
    ms = cv2.BFMatcher().knnMatch(d1, d0, k=2)
    good = [m for m, n in ms if m.distance < 0.75 * n.distance]
    src = np.float32([k1[m.queryIdx].pt for m in good])
    dst = np.float32([k0[m.trainIdx].pt for m in good])
    A, inl = cv2.estimateAffinePartial2D(src, dst, method=cv2.RANSAC, ransacReprojThreshold=2.0,
                                         maxIters=20000, confidence=0.999)
    inl = inl.ravel().astype(bool)
    res = np.linalg.norm(src[inl] @ A[:, :2].T + A[:, 2] - dst[inl], axis=1)
    scale = float(np.hypot(A[0, 0], A[1, 0]))
    angle = float(np.degrees(np.arctan2(A[1, 0], A[0, 0])))
    c = np.array([CENTER_X, 380.0])
    shift = c @ A[:, :2].T + A[:, 2] - c  # displacement at the choir centre
    return dict(matches=len(good), inliers=int(inl.sum()), scale=scale, angle_deg=angle,
                shift_at_choir=[float(shift[0]), float(shift[1])], median_residual=float(np.median(res)),
                A=A.tolist())


# ------------------------------------------------------------------------ detection

def _nms(boxes, scores, iou=0.5):
    import torch
    import torchvision

    if len(boxes) == 0:
        return []
    keep = torchvision.ops.nms(torch.tensor(boxes, dtype=torch.float32), torch.tensor(scores, dtype=torch.float32), iou)
    return keep.numpy().tolist()


def detect_people(tier, name):
    """Person boxes (YOLO11x on the upscaled scene + horizontal flip TTA) and face
    keypoints (YOLO11x-pose) used as SAM point prompts."""
    cache = WORK / f"det_{name}.json"
    if cache.exists():
        return json.loads(cache.read_text())
    from ultralytics import YOLO

    x0, y0, x1, y1 = SCENE
    s = DET_UPSCALE
    crop = cv2.resize(tier[y0:y1, x0:x1], None, fx=s, fy=s, interpolation=cv2.INTER_CUBIC)
    imgsz = int(np.ceil(max(crop.shape[:2]) / 32) * 32)
    W = crop.shape[1]
    boxes, scores = [], []
    det = YOLO(str(CACHE / "yolo11x.pt"))
    for flip in (False, True):
        img = crop[:, ::-1].copy() if flip else crop
        r = det.predict(img, imgsz=imgsz, conf=DET_CONF, iou=0.5, classes=[0], verbose=False)[0]
        for b, c in zip(r.boxes.xyxy.cpu().numpy(), r.boxes.conf.cpu().numpy()):
            if flip:
                b = np.array([W - b[2], b[1], W - b[0], b[3]])
            boxes.append((b / s + [x0, y0, x0, y0]).tolist())
            scores.append(float(c))
    keep = _nms(boxes, scores, 0.5)
    boxes = [boxes[k] for k in keep]
    scores = [scores[k] for k in keep]
    # Pose keypoints (COCO order: 0 nose, 1-2 eyes, 3-4 ears, 5-6 shoulders).
    pose = YOLO(str(CACHE / "yolo11x-pose.pt"))
    r = pose.predict(crop, imgsz=imgsz, conf=0.15, iou=0.5, verbose=False)[0]
    pb = r.boxes.xyxy.cpu().numpy() / s + [x0, y0, x0, y0]
    kp = r.keypoints.data.cpu().numpy()
    kp[..., :2] = kp[..., :2] / s + [x0, y0]
    people = []
    for b, c in zip(boxes, scores):
        b = np.array(b)
        bw, bh = b[2] - b[0], b[3] - b[1]
        head = None
        best = 0.0
        for pbi, kpi in zip(pb, kp):
            ix = max(0, min(b[2], pbi[2]) - max(b[0], pbi[0])) * max(0, min(b[3], pbi[3]) - max(b[1], pbi[1]))
            iou = ix / (bw * bh + (pbi[2] - pbi[0]) * (pbi[3] - pbi[1]) - ix + 1e-6)
            face = kpi[:5]
            vis = face[face[:, 2] > 0.5]
            if iou > max(best, 0.5) and len(vis) >= 2:
                hx, hy = vis[:, 0].mean(), vis[:, 1].mean()
                if b[0] <= hx <= b[2] and b[1] <= hy <= b[3]:
                    best, head = iou, [float(hx), float(hy)]
        if head is None:  # fallback: upper-centre of the box
            head = [float((b[0] + b[2]) / 2), float(b[1] + 0.13 * bh)]
        people.append(dict(box=b.tolist(), conf=c, head=head, source="yolo"))
    for b in EXTRA_PEOPLE.get(name, []):
        b = np.array(b, float)
        people.append(dict(box=b.tolist(), conf=1.0, head=[float((b[0] + b[2]) / 2), float(b[1] + 0.13 * (b[3] - b[1]))],
                           source="manual"))
    drop = set(DROP_PEOPLE.get(name, []))
    people = [p for k, p in enumerate(people) if k not in drop]
    people.sort(key=lambda p: p["box"][0])
    cache.write_text(json.dumps(people, indent=1))
    return people


# ---------------------------------------------------------------------------- SAM

def sam_logits(tier, name, people):
    """Per-person SAM 2.1 logits, computed on an upscaled crop around each person
    (much finer edges than running SAM on the whole 1448px frame)."""
    cache = WORK / f"sam_{name}.npz"
    if cache.exists():
        d = np.load(cache)
        return [(d[f"l{i}"].astype(np.float32), tuple(d[f"o{i}"])) for i in range(len(people))]
    from sam_util import SoftSAM

    sam = SoftSAM(str(CACHE / "sam2.1_l.pt"))
    X0, Y0, X1, Y1 = SCENE
    heads = np.array([p["head"] for p in people])
    out = []
    t0 = time.time()
    for i, p in enumerate(people):
        b = np.array(p["box"])
        bw, bh = b[2] - b[0], b[3] - b[1]
        cx0, cx1 = int(max(X0, b[0] - 0.35 * bw)), int(min(X1, b[2] + 0.35 * bw))
        cy0, cy1 = int(max(Y0, b[1] - 0.25 * bh)), int(min(Y1, b[3] + 0.2 * bh))
        sam.set_crop(tier[cy0:cy1, cx0:cx1])
        box = [b[0] - cx0 - 0.03 * bw, b[1] - cy0 - 0.03 * bh, b[2] - cx0 + 0.03 * bw, b[3] - cy0 + 0.01 * bh]
        neg = [(hx - cx0, hy - cy0) for j, (hx, hy) in enumerate(heads)
               if j != i and cx0 < hx < cx1 and cy0 < hy < cy1]
        lg, score = sam.logits(box, [(heads[i][0] - cx0, heads[i][1] - cy0)], neg)
        out.append((lg, (cx0, cy0)))
        log(f"  {name} SAM {i + 1}/{len(people)} score={score:.3f} ({time.time() - t0:.0f}s)")
    np.savez_compressed(cache, **{f"l{i}": l.astype(np.float16) for i, (l, _) in enumerate(out)},
                        **{f"o{i}": np.array(o) for i, (_, o) in enumerate(out)})
    return out


def detect_books(tier, name):
    """COCO 'book' boxes (the singers' hymnals) - SAM never includes a held book in
    the person mask, so they are segmented separately and attached to the holder."""
    cache = WORK / f"books_{name}.json"
    if cache.exists():
        return json.loads(cache.read_text())
    from ultralytics import YOLO

    x0, y0, x1, y1 = SCENE
    s = DET_UPSCALE
    crop = cv2.resize(tier[y0:y1, x0:x1], None, fx=s, fy=s, interpolation=cv2.INTER_CUBIC)
    imgsz = int(np.ceil(max(crop.shape[:2]) / 32) * 32)
    W = crop.shape[1]
    det = YOLO(str(CACHE / "yolo11x.pt"))
    boxes, scores = [], []
    for flip in (False, True):
        img = crop[:, ::-1].copy() if flip else crop
        r = det.predict(img, imgsz=imgsz, conf=0.15, iou=0.5, classes=[73], verbose=False)[0]
        for b, c in zip(r.boxes.xyxy.cpu().numpy(), r.boxes.conf.cpu().numpy()):
            if flip:
                b = np.array([W - b[2], b[1], W - b[0], b[3]])
            boxes.append((b / s + [x0, y0, x0, y0]).tolist())
            scores.append(float(c))
    keep = _nms(boxes, scores, 0.4)
    books = [dict(box=boxes[k], conf=scores[k]) for k in keep]
    cache.write_text(json.dumps(books, indent=1))
    return books


def merge_logits(a, b):
    """Pixel-wise max of two (logits, (ox, oy)) windows -> one window covering both."""
    (la, (ax, ay)), (lb, (bx, by)) = a, b
    x0, y0 = min(ax, bx), min(ay, by)
    x1, y1 = max(ax + la.shape[1], bx + lb.shape[1]), max(ay + la.shape[0], by + lb.shape[0])
    out = np.full((y1 - y0, x1 - x0), -30.0, np.float32)
    out[ay - y0:ay - y0 + la.shape[0], ax - x0:ax - x0 + la.shape[1]] = la
    sub = out[by - y0:by - y0 + lb.shape[0], bx - x0:bx - x0 + lb.shape[1]]
    np.maximum(sub, lb, out=sub)
    return out, (x0, y0)


def held_seeds(tier, name, people, lab):
    """Seeds for held objects: YOLO book boxes + dark unassigned pockets near people."""
    H, W = lab.shape
    V = tier.max(2).astype(np.int32)
    seeds = [dict(kind="book", box=b["box"]) for b in detect_books(tier, name)]
    zone = np.zeros((H, W), bool)
    for p in people:
        b = np.array(p["box"])
        bw, bh = b[2] - b[0], b[3] - b[1]
        zone[int(b[1] + 0.25 * bh):int(b[3] + 2), int(b[0] - 0.05 * bw):int(b[2] + 0.05 * bw)] = True
    dark = ero((V < 60) & (lab == -1) & zone, 1)
    n, cc, st, _ = cv2.connectedComponentsWithStats(dark.astype(np.uint8), 8)
    for k in np.argsort(-st[1:, cv2.CC_STAT_AREA]) + 1:
        if st[k, cv2.CC_STAT_AREA] < 20:
            continue
        x, y, w, h = (int(v) for v in st[k, :4])
        comp = (cc == k).astype(np.uint8)
        dt = cv2.distanceTransform(comp, cv2.DIST_L2, 3)
        py, px = np.unravel_index(np.argmax(dt), dt.shape)
        seeds.append(dict(kind="pocket", box=[x - 3, y - 3, x + w + 3, y + h + 3], point=[int(px), int(py)]))
    return seeds


def held_segments(tier, name, people, lab):
    """SAM masks for every held-object seed. The singers' zone is covered by a few
    overlapping 320px tiles (each encoded once at 1024px, ~3x upscale); each seed is
    decoded in the tile where it sits most centrally. Cached."""
    cache = WORK / f"heldseg_{name}.npz"
    if cache.exists():
        d = np.load(cache, allow_pickle=True)
        return list(d["segs"])
    from sam_util import SoftSAM

    seeds = held_seeds(tier, name, people, lab)
    X0, Y0, X1, Y1 = SCENE
    T = 320
    zy0 = int(min(p["box"][1] for p in people))
    zy1 = int(max(p["box"][3] for p in people))
    ys = list(range(max(Y0, zy0 - 20), max(Y0 + 1, zy1 + 20 - T + 1), T // 2)) or [max(Y0, zy0 - 20)]
    if ys[-1] + T < zy1 + 20:
        ys.append(min(Y1 - T, zy1 + 20 - T))
    tiles = [(x, y) for y in ys for x in range(X0, X1 - T + 1, T // 3)]
    if tiles[-1][0] + T < X1:
        tiles += [(X1 - T, y) for y in ys]

    def margin(box, t):
        return min(box[0] - t[0], box[1] - t[1], t[0] + T - box[2], t[1] + T - box[3])

    groups = {}
    for k, sd in enumerate(seeds):
        best = max(range(len(tiles)), key=lambda i: margin(sd["box"], tiles[i]))
        groups.setdefault(best, []).append(k)
    sam = SoftSAM(str(CACHE / "sam2.1_l.pt"))
    segs = [None] * len(seeds)
    for ti, ks in groups.items():
        tx, ty = tiles[ti]
        sam.set_crop(tier[ty:ty + T, tx:tx + T])
        for k in ks:
            sd = seeds[k]
            b = np.array(sd["box"], float)
            bb = [b[0] - tx, b[1] - ty, b[2] - tx, b[3] - ty]
            pos = [(sd["point"][0] - tx, sd["point"][1] - ty)] if sd["kind"] == "pocket" else []
            lg, score = sam.logits(bb, pos)
            segs[k] = dict(seed=sd, logits=lg.astype(np.float16), origin=(tx, ty), score=score)
        log(f"  {name} held-object tile {ti}: {len(ks)} seeds")
    np.savez_compressed(cache, segs=np.array(segs, dtype=object))
    return segs


def held_objects(tier, name, people, lab):
    """Attach hymnals (and similar dark held objects) that SAM left out of the person
    masks. A segment must be mostly unassigned and either book-like (near-neutral
    black) or a deep shadow enclosed by people. A book goes to the holder of an
    already-accepted book it touches (books first, then pockets by size), else to the
    adjacent person whose face is above the book's top edge and most nearly above it
    horizontally, and who does not clearly occlude it. A shadow goes to its rear-most
    neighbour."""
    H, W = lab.shape
    V = tier.max(2).astype(np.int32)
    C = V - tier.min(2).astype(np.int32)  # chroma: hymnals are near-neutral black
    rows = assign_rows(people, lab, len(people))
    segs = held_segments(tier, name, people, lab)
    areas = np.bincount(lab[lab >= 0].ravel(), minlength=len(people))
    med_area = float(np.median(areas))
    heights = np.array([p["box"][3] - p["box"][1] for p in people])
    head_size = 0.18 * float(np.median(heights))
    r = int(np.clip(0.07 * np.median(heights), 5, 14))
    obj_lab = np.full((H, W), -1, np.int32)  # accepted object -> owner
    objs, debug = [], []
    for sg in segs:
        sd, (cx0, cy0) = sg["seed"], sg["origin"]
        lg = sg["logits"].astype(np.float32)
        h, w = lg.shape
        b = sd["box"]
        rec = dict(kind=sd["kind"], box=[float(v) for v in b], ok=False, owner=-1, score=sg["score"])
        debug.append(rec)
        if sd["kind"] == "pocket" and obj_lab[sd["point"][1], sd["point"][0]] >= 0:
            rec["why"] = "inside accepted"
            continue
        O = np.zeros((H, W), bool)
        O[cy0:cy0 + h, cx0:cx0 + w] = lg > 0
        area = int(O.sum())
        rec["area"] = area
        if area < 25:
            rec["why"] = "tiny"
            continue
        new = O & (lab == -1) & (obj_lab < 0)
        un = float((lab[O] == -1).mean())
        if new.sum() < 20 or un < 0.6 or area > 0.6 * med_area:
            rec["why"] = "not an unassigned object"
            continue
        Vn, Cn = V[new], C[new]
        neutral = float(((Vn < 80) & (Cn < 40)).mean())
        book_like = (neutral >= 0.75 and float(np.median(Cn)) <= 28) or (
            sd["kind"] == "book" and neutral >= 0.4 and float(np.median(Vn)) < 90)  # YOLO said book (e.g. candle-lit)
        shadow_like = float(np.median(Vn)) < 25
        ring = dil(O, 2) & ~O
        rl = lab[ring]
        ol = obj_lab[ring]
        adj = float(((rl >= 0) | (ol >= 0)).mean())
        rec.update(unassigned=round(un, 2), neutral=round(neutral, 2), people_border=round(adj, 2))
        if not (book_like or shadow_like) or adj < (0.3 if book_like else 0.5):
            rec["why"] = "not book-like"
            continue
        oy, ox = np.nonzero(O)
        ocx, ocy = ox.mean(), oy.mean()
        otop = oy.min()
        owner = -1
        adjacent = [int(j) for j in np.unique(rl[rl >= 0]) if (rl == j).sum() >= 5]
        if book_like and (ol >= 0).sum() >= 8:
            # continuation of an accepted book (e.g. its lower cover below the hands)
            vals, cnt = np.unique(ol[ol >= 0], return_counts=True)
            owner = int(vals[np.argmax(cnt)])
            rec["via"] = "touches book"
        elif book_like:
            y0w, x0w = max(0, oy.min() - 40), max(0, ox.min() - 40)
            y1w, x1w = oy.max() + 41, ox.max() + 41
            cands = []
            for j in adjacent:
                hx, hy = people[j]["head"]
                if hy >= otop - 0.1 * head_size:  # a holder's face is above the top of the book
                    continue
                cv_ = convexity(O[y0w:y1w, x0w:x1w], lab[y0w:y1w, x0w:x1w] == j, r)
                if cv_ is not None and cv_[0] < -0.04:  # person j clearly covers it
                    continue
                # held in front of the torso: roughly under the holder's face
                cands.append((1.5 * abs(ocx - hx) + 0.3 * (ocy - hy), j))
            if cands:
                owner = min(cands)[1]
                rec["via"] = "face above"
        elif adjacent:
            # deep shadow between bodies: give it to the rear-most neighbour, which is
            # only revealed once the people in front of it are shown
            owner = max(adjacent, key=lambda j: (rows[j], -people[j]["head"][1]))
            rec["via"] = "shadow, rear-most"
        if owner < 0:
            rec["why"] = "no holder"
            continue
        pb = people[owner]["box"]
        if ocy > pb[3] + 0.1 * heights[owner] or abs(ocx - (pb[0] + pb[2]) / 2) > 0.9 * (pb[2] - pb[0]):
            rec["why"] = "far from holder"
            continue
        rec.update(ok=True, owner=owner)
        obj_lab[new] = owner
        l2 = np.where(new[cy0:cy0 + h, cx0:cx0 + w], lg, np.minimum(lg, -1.0)).astype(np.float32)
        objs.append((l2, (cx0, cy0), owner))

    return objs, debug


def enclosed_gaps(tier, name, people, lab, rows):
    """Unassigned regions completely surrounded by singers (hairline SAM gaps
    ignored): a missed body part, or a glimpse of the stalls between bodies. Left
    alone they would become holes once everyone is revealed. Owner: the neighbour
    whose wrist keypoint lies inside, else the rear-most neighbour."""
    H, W = lab.shape
    objs, debug = [], []
    kps = pose_keypoints(tier, name, people)
    zone = np.zeros((H, W), bool)
    for p in people:
        b = np.array(p["box"])
        zone[int(b[1]):int(b[3]) + 1, int(b[0]):int(b[2]) + 1] = True
    taken = lab >= 0
    un = ~taken & zone
    core = ero(un, 1)
    n, cc, st, _ = cv2.connectedComponentsWithStats(core.astype(np.uint8), 8)
    touch_out = set(np.unique(cc[dil(~zone, 2) & (cc > 0)]).tolist())
    for k in range(1, n):
        if k in touch_out or st[k, cv2.CC_STAT_AREA] < 12:
            continue
        R = dil(cc == k, 1) & un
        ring = dil(R, 2) & ~R
        rl = lab[ring]
        if (rl >= 0).mean() < 0.85:
            continue
        adjacent = [int(j) for j in np.unique(rl[rl >= 0]) if (rl == j).sum() >= 3]
        if not adjacent:
            continue
        owner, via = -1, ""
        Rd = dil(R, 3)
        for j in adjacent:  # a wrist of a neighbour inside the region names the owner
            kp = kps[j]
            if kp is None:
                continue
            for idx in (9, 10):
                x, y, cf = kp[idx]
                if cf >= 0.5 and 0 <= int(y) < H and 0 <= int(x) < W and Rd[int(y), int(x)]:
                    owner, via = j, "wrist inside"
        if owner < 0:
            owner, via = max(adjacent, key=lambda j: (rows[j], -people[j]["head"][1])), "enclosed, rear-most"
        ys, xs = np.nonzero(R)
        x0, y0 = xs.min(), ys.min()
        l2 = np.where(R[y0:ys.max() + 1, x0:xs.max() + 1], 8.0, -8.0).astype(np.float32)
        objs.append((l2, (int(x0), int(y0)), owner))
        debug.append(dict(kind="enclosed", box=[float(x0), float(y0), float(xs.max()), float(ys.max())], ok=True,
                          owner=owner, area=int(R.sum()), via=via, score=1.0))
    return objs, debug


def refine_limbs(tier, name, people, lab, logits):
    """SAM often drops raised forearms / hands. For each person whose confident
    elbow or wrist keypoint (YOLO-pose) lands on unassigned pixels, SAM is re-run
    with those keypoints as extra positive points. Only newly gained, unassigned
    pixels connected to the person's current mask are kept (capped at 40% of it)."""
    cache = WORK / f"limbs_{name}.npz"
    kps = pose_keypoints(tier, name, people)
    H, W = lab.shape
    X0, Y0, X1, Y1 = SCENE

    def chain_ok(i, a, b):
        """The limb segment a->b lies on person i or on unassigned pixels (an arm
        that crosses somebody else's body belongs to a mismatched skeleton)."""
        n = int(max(abs(b[0] - a[0]), abs(b[1] - a[1]))) + 1
        xs = np.clip(np.round(np.linspace(a[0], b[0], n)).astype(int), 0, W - 1)
        ys = np.clip(np.round(np.linspace(a[1], b[1], n)).astype(int), 0, H - 1)
        lv = lab[ys, xs]
        return ((lv == i) | (lv == -1)).mean() >= 0.75

    todo = []
    for i, p in enumerate(people):
        kp = kps[i]
        if kp is None:
            continue
        b = np.array(p["box"])
        bw, bh = b[2] - b[0], b[3] - b[1]
        pts = []
        for sh, el, wr in ((5, 7, 9), (6, 8, 10)):
            if min(kp[sh][2], kp[el][2]) < 0.6 or not chain_ok(i, kp[sh][:2], kp[el][:2]):
                continue
            cand = [kp[el][:2]]
            if kp[wr][2] >= 0.6 and chain_ok(i, kp[el][:2], kp[wr][:2]):
                hand = kp[wr][:2] + 0.35 * (kp[wr][:2] - kp[el][:2])  # palm / fingers
                cand += [kp[wr][:2], hand]
            for x, y in cand:
                xi, yi = int(round(x)), int(round(y))
                if (0 <= xi < W and 0 <= yi < H and lab[yi, xi] == -1
                        and b[0] - 0.3 * bw <= x <= b[2] + 0.3 * bw and b[1] - 0.3 * bh <= y <= b[3]):
                    pts.append((round(float(x), 1), round(float(y), 1)))
        if pts:
            todo.append((i, pts))
    prev = {}
    if cache.exists():
        for r in np.load(cache, allow_pickle=True)["raw"]:
            prev[(r["i"], tuple(map(tuple, r["pts"])))] = r
    need = [(i, pts) for i, pts in todo if (i, tuple(pts)) not in prev]
    if need:
        from sam_util import SoftSAM

        sam = SoftSAM(str(CACHE / "sam2.1_l.pt"))
        heads = np.array([p["head"] for p in people])
        for i, pts in need:
            b = np.array(people[i]["box"])
            allx = [b[0], b[2]] + [x for x, _ in pts]
            ally = [b[1], b[3]] + [y for _, y in pts]
            bx0, bx1, by0, by1 = min(allx), max(allx), min(ally), max(ally)
            bw, bh = bx1 - bx0, by1 - by0
            cx0, cx1 = int(max(X0, bx0 - 0.35 * bw)), int(min(X1, bx1 + 0.35 * bw))
            cy0, cy1 = int(max(Y0, by0 - 0.25 * bh)), int(min(Y1, by1 + 0.2 * bh))
            sam.set_crop(tier[cy0:cy1, cx0:cx1])
            box = [bx0 - cx0 - 4, by0 - cy0 - 4, bx1 - cx0 + 4, by1 - cy0 + 2]
            pos = [(heads[i][0] - cx0, heads[i][1] - cy0)] + [(x - cx0, y - cy0) for x, y in pts]
            neg = [(hx - cx0, hy - cy0) for j, (hx, hy) in enumerate(heads)
                   if j != i and cx0 < hx < cx1 and cy0 < hy < cy1]
            lg, score = sam.logits(box, pos, neg)
            prev[(i, tuple(pts))] = dict(i=i, pts=[list(q) for q in pts], logits=lg.astype(np.float16),
                                         origin=(cx0, cy0), score=score)
            log(f"  {name} limb refine person {i}: {len(pts)} point(s) score={score:.2f}")
        np.savez_compressed(cache, raw=np.array(list(prev.values()), dtype=object))
    raw = [prev[(i, tuple(pts))] for i, pts in todo]
    objs, debug = [], []
    for r in raw:
        i, (cx0, cy0) = r["i"], r["origin"]
        lg = r["logits"].astype(np.float32)
        h, w = lg.shape
        M = lab == i
        newp = np.zeros((H, W), bool)
        newp[cy0:cy0 + h, cx0:cx0 + w] = lg > 0
        newp &= lab == -1
        n, cc, st, _ = cv2.connectedComponentsWithStats(newp.astype(np.uint8), 8)
        near = dil(M, 2)
        keep = np.zeros((H, W), bool)
        for k in range(1, n):
            comp = cc == k
            if st[k, cv2.CC_STAT_AREA] >= 6 and (comp & near).any():
                keep |= comp
        added = int(keep.sum())
        ok = 0 < added <= 0.4 * M.sum()
        debug.append(dict(kind="limb", owner=i, ok=ok, area=added, box=[float(v) for v in people[i]["box"]],
                          score=r["score"]))
        if ok:
            l2 = np.where(keep[cy0:cy0 + h, cx0:cx0 + w], lg, -30.0).astype(np.float32)
            objs.append((l2, (cx0, cy0), i))
    return objs, debug


def pose_keypoints(tier, name, people):
    """COCO keypoints (17x3, tier pixels) per person from YOLO11x-pose, matched
    by box IoU; None where no pose matched."""
    cache = WORK / f"pose_{name}.json"
    if cache.exists():
        return [None if k is None else np.array(k) for k in json.loads(cache.read_text())]
    from ultralytics import YOLO

    x0, y0, x1, y1 = SCENE
    s = DET_UPSCALE
    crop = cv2.resize(tier[y0:y1, x0:x1], None, fx=s, fy=s, interpolation=cv2.INTER_CUBIC)
    imgsz = int(np.ceil(max(crop.shape[:2]) / 32) * 32)
    r = YOLO(str(CACHE / "yolo11x-pose.pt")).predict(crop, imgsz=imgsz, conf=0.15, iou=0.5, verbose=False)[0]
    pb = r.boxes.xyxy.cpu().numpy() / s + [x0, y0, x0, y0]
    kp = r.keypoints.data.cpu().numpy()
    kp[..., :2] = kp[..., :2] / s + [x0, y0]
    out = []
    for p in people:
        b = np.array(p["box"])
        best, bk = 0.5, None
        for q, k in zip(pb, kp):
            ix = max(0, min(b[2], q[2]) - max(b[0], q[0])) * max(0, min(b[3], q[3]) - max(b[1], q[1]))
            iou = ix / ((b[2] - b[0]) * (b[3] - b[1]) + (q[2] - q[0]) * (q[3] - q[1]) - ix + 1e-6)
            if iou > best:
                best, bk = iou, k
        out.append(None if bk is None else bk)
    cache.write_text(json.dumps([None if k is None else k.tolist() for k in out]))
    return out


def ownership(shape, logits):
    """Hard label map: each pixel belongs to the person with the highest positive logit."""
    H, W = shape
    N = len(logits)
    best = np.full((H, W), 0.0, np.float32)
    lab = np.full((H, W), -1, np.int32)
    for i, (lg, (ox, oy)) in enumerate(logits):
        h, w = lg.shape
        sub = best[oy:oy + h, ox:ox + w]
        m = lg > sub
        sub[m] = lg[m]
        lab[oy:oy + h, ox:ox + w][m] = i
    # Clean up: drop small islands, fill tiny holes.
    for i in range(N):
        m = (lab == i).astype(np.uint8)
        n, cc, st, _ = cv2.connectedComponentsWithStats(m, 8)
        if n <= 1:
            continue
        # Parts of a body split off by an occluder (e.g. hands below a front-row head)
        # are legitimate; only specks are dropped.
        main_area = st[1:, cv2.CC_STAT_AREA].max()
        for k in range(1, n):
            if st[k, cv2.CC_STAT_AREA] < max(60, 0.01 * main_area):
                lab[cc == k] = -1
    for i in range(N):
        m = lab == i
        inv = (~m).astype(np.uint8)
        n, cc, st, _ = cv2.connectedComponentsWithStats(inv, 4)
        for k in range(1, n):
            if st[k, cv2.CC_STAT_AREA] < 40:
                hole = cc == k
                if (lab[hole] == -1).all():
                    lab[hole] = i
    return lab


# ----------------------------------------------------------------------- occlusion

def convexity(mi, mj, r):
    """Along the shared boundary of masks i and j: coverage of a radius-r disk by j
    minus coverage by i. > 0 means i is the locally convex one, i.e. i is in front."""
    k = disk(r).astype(np.float32)
    k /= k.sum()
    bi = dil(mj, 3) & mi
    bj = dil(mi, 3) & mj
    B = bi | bj
    if B.sum() < 12:
        return None
    ci = cv2.filter2D(mi.astype(np.float32), -1, k, borderType=cv2.BORDER_CONSTANT)
    cj = cv2.filter2D(mj.astype(np.float32), -1, k, borderType=cv2.BORDER_CONSTANT)
    return float(cj[B].mean() - ci[B].mean()), int(B.sum())


def occlusion_graph(lab, people, rows, N):
    """Who covers whom, for every pair of touching masks. rel[(i, j)]["score"] > 0
    means i is in front of j, < 0 means j is in front, 0 means side by side (no
    constraint).

    Shape cue: along the shared boundary the occluder's silhouette is the natural,
    locally convex contour (head, shoulder, book edge) and the occluded mask is
    locally concave there (`convexity`).
    Depth cue: singers stand on risers, so someone in a row further forward is in
    front of someone in a row further back, and within a row a face a full
    head-height lower means standing further forward. This is used unless the shape
    cue strongly contradicts it. Otherwise only a clear shape cue creates an edge:
    neighbours whose outlines merely touch have |conv| close to 0, and a spurious
    edge there would only scramble the reveal order."""
    heights = np.array([p["box"][3] - p["box"][1] for p in people])
    r = int(np.clip(0.07 * np.median(heights), 5, 14))
    head_size = 0.18 * float(np.median(heights))
    rel = {}
    masks = [lab == i for i in range(N)]
    bbs = []
    for m in masks:
        ys, xs = np.nonzero(m)
        bbs.append((xs.min(), ys.min(), xs.max(), ys.max()) if len(xs) else (0, 0, 0, 0))
    for i in range(N):
        for j in range(i + 1, N):
            a, b = bbs[i], bbs[j]
            if a[0] > b[2] + 3 or b[0] > a[2] + 3 or a[1] > b[3] + 3 or b[1] > a[3] + 3:
                continue
            x0, y0 = max(0, min(a[0], b[0]) - r - 3), max(0, min(a[1], b[1]) - r - 3)
            x1, y1 = max(a[2], b[2]) + r + 4, max(a[3], b[3]) + r + 4
            # boundary = pixels within 3px of the other mask (SAM often leaves a hairline gap)
            cv_ = convexity(masks[i][y0:y1, x0:x1], masks[j][y0:y1, x0:x1], r)
            if cv_ is None:
                continue
            conv, nb = cv_
            dy = (people[i]["head"][1] - people[j]["head"][1]) / head_size
            if rows[i] != rows[j] and abs(conv) < SHAPE_OVERRIDE:
                score, why = (1.0 if rows[i] < rows[j] else -1.0), "row"
            elif abs(dy) >= 1.0 and abs(conv) < SHAPE_OVERRIDE:
                # same nominal row but faces a head-height apart: the lower one stands forward
                score, why = float(np.sign(dy)), "head height"
            elif abs(conv) >= SHAPE_MIN:
                score, why = conv, "shape"
            else:
                score, why = 0.0, "side by side"
            rel[(i, j)] = dict(score=float(score), conv=round(conv, 4), rows=[int(rows[i]), int(rows[j])],
                               contact=nb, why=why)
    return rel


def assign_rows(people, lab, N):
    """Cluster face heights into riser rows (row 0 = front = lowest faces) with an
    optimal 1-D k-means (dynamic programming). The largest k is used whose row
    centres are at least a quarter body-height apart and whose rows have >= 2 people."""
    hy = np.array([p["head"][1] for p in people], float)
    hmed = float(np.median([p["box"][3] - p["box"][1] for p in people]))
    o = np.argsort(hy)
    v = hy[o]
    cs, cs2 = np.concatenate([[0], np.cumsum(v)]), np.concatenate([[0], np.cumsum(v * v)])

    def sse(i, j):  # v[i:j]
        n = j - i
        return cs2[j] - cs2[i] - (cs[j] - cs[i]) ** 2 / n

    for k in range(min(5, N // 2), 1, -1):
        D = np.full((k + 1, N + 1), np.inf)
        arg = np.zeros((k + 1, N + 1), int)
        D[0, 0] = 0
        for c in range(1, k + 1):
            for j in range(c, N + 1):
                for i in range(c - 1, j):
                    val = D[c - 1, i] + sse(i, j)
                    if val < D[c, j]:
                        D[c, j], arg[c, j] = val, i
        bounds, j = [], N
        for c in range(k, 0, -1):
            i = arg[c, j]
            bounds.append((i, j))
            j = i
        bounds = bounds[::-1]  # top (back) row first
        centres = [v[i:j].mean() for i, j in bounds]
        sizes = [j - i for i, j in bounds]
        if min(np.diff(centres)) >= 0.25 * hmed and min(sizes) >= 2:
            rows = np.zeros(N, int)
            for r, (i, j) in enumerate(bounds):
                rows[o[i:j]] = k - 1 - r
            return _demote_covered(rows, lab, N)
    return np.zeros(N, int)


def _demote_covered(rows, lab, N):
    """A front-row singer stands on the tier's rail: below their bottom edge is
    scenery. Someone whose bottom edge is mostly covered by front-row singers is in
    the row behind, even if their face happens to sit low."""
    if lab is None:
        return rows
    H = lab.shape[0]
    rows = rows.copy()
    for i in np.nonzero(rows == 0)[0]:
        m = lab == i
        cols = np.nonzero(m.any(0))[0]
        if not len(cols):
            continue
        below = []
        for x in cols:
            yb = np.nonzero(m[:, x])[0].max()
            nxt = lab[yb + 1:min(H, yb + 9), x]
            nxt = nxt[(nxt != i) & (nxt >= 0)]  # skip SAM's hairline gaps
            below.append(int(nxt[0]) if len(nxt) else -1)
        below = np.array(below)
        if np.isin(below, np.nonzero(rows == 0)[0]).mean() > 0.6:
            rows[i] = 1
    return rows


def reveal_order(people, rows, front_edges, N):
    """Preferred order: rows front->back, centre-outward alternating L/R. Then a
    priority topological sort enforces 'occluder revealed no later than occludee'."""
    cx = np.array([(p["box"][0] + p["box"][2]) / 2 for p in people])
    pref = []
    for r in sorted(set(rows)):
        idx = [i for i in range(N) if rows[i] == r]
        left = sorted([i for i in idx if cx[i] < CENTER_X], key=lambda i: CENTER_X - cx[i])
        right = sorted([i for i in idx if cx[i] >= CENTER_X], key=lambda i: cx[i] - CENTER_X)
        seq = []
        side = 0 if (left and (not right or CENTER_X - cx[left[0]] <= cx[right[0]] - CENTER_X)) else 1
        while left or right:
            src = (left, right)[side] or (right, left)[side]
            seq.append(src.pop(0))
            side ^= 1
        pref += seq
    rank0 = {i: k for k, i in enumerate(pref)}
    preds = {i: set() for i in range(N)}
    succs = {i: set() for i in range(N)}
    for (a, b) in front_edges:  # a in front of b -> a before b
        preds[b].add(a)
        succs[a].add(b)
    # An occluder inherits the earliest preferred slot of anyone it (transitively)
    # blocks, so constraints pull occluders forward instead of delaying occludees.
    rank = {}
    for i in range(N):
        seen, stack, best = {i}, [i], rank0[i]
        while stack:
            for j in succs[stack.pop()]:
                if j not in seen:
                    seen.add(j)
                    stack.append(j)
                    best = min(best, rank0[j])
        rank[i] = (best, rank0[i])
    order, done = [], set()
    heap = [(rank[i], i) for i in range(N) if not preds[i]]
    heapq.heapify(heap)
    while len(order) < N:
        if not heap:  # cycle: release the best-ranked blocked node
            i = min((i for i in range(N) if i not in done), key=lambda i: rank[i])
            log(f"  warning: occlusion cycle around person {i}; releasing it")
            heap = [(rank[i], i)]
        _, i = heapq.heappop(heap)
        if i in done:
            continue
        order.append(i)
        done.add(i)
        for j in range(N):
            if j not in done and i in preds[j]:
                preds[j].discard(i)
                if not preds[j]:
                    heapq.heappush(heap, (rank[j], j))
    return order, pref


# ----------------------------------------------------------------------- foreground

def fit_rail(base):
    """Top edge of the loft-front rail ledge in the base image: per-column strongest
    dark->bright transition, robust quadratic fit across the straight ledge."""
    L = cv2.GaussianBlur(base.astype(np.float32).mean(2), (1, 3), 0)
    gy = L[1:] - L[:-1]
    xs, ys = [], []
    for a, b in RAIL_FIT_X:
        for x in range(a, b, 2):
            col = gy[426:450, x:x + 2].mean(1)
            xs.append(x + 1)
            ys.append(426 + int(np.argmax(col)) + 0.5)  # boundary between row y and y+1
    xs, ys = np.array(xs, float), np.array(ys, float)
    keep = np.ones(len(xs), bool)
    for _ in range(5):
        c = np.polyfit(xs[keep], ys[keep], 2)
        res = ys - np.polyval(c, xs)
        keep = np.abs(res) < max(1.0, 2.5 * np.std(res[keep]))
    return c


def soft_object_alpha(base, sam, box, matting=True):
    """Alpha for one base-image object: SAM logits (box prompt) refined by
    closed-form matting in a narrow band."""
    from pymatting import estimate_alpha_cf

    x0, y0, x1, y1 = box
    pad = 24
    cx0, cy0 = max(0, x0 - pad), max(0, y0 - pad)
    cx1, cy1 = min(base.shape[1], x1 + pad), min(base.shape[0], y1 + pad)
    crop = base[cy0:cy1, cx0:cx1]
    sam.set_crop(crop)
    lg, _ = sam.logits([x0 - cx0, y0 - cy0, x1 - cx0, y1 - cy0])
    m = lg > 0
    a = m.astype(np.float64)
    if matting:
        tri = np.full(m.shape, 0.5)
        tri[ero(m, 2)] = 1.0
        tri[~dil(m, 2)] = 0.0
        rgb = crop[..., ::-1].astype(np.float64) / 255
        a = estimate_alpha_cf(rgb, tri)
        a = np.clip(a, 0, 1)
    full = np.zeros(base.shape[:2], np.float32)
    full[cy0:cy1, cx0:cx1] = a
    # restrict to the prompt box (plus a little) so nothing leaks
    lim = np.zeros_like(full)
    lim[max(0, y0 - 4):y1 + 4, max(0, x0 - 4):x1 + 4] = 1
    return full * lim


def polygon_alpha(shape, poly, ss=8):
    """Anti-aliased polygon coverage (supersampled)."""
    H, W = shape
    pts = np.array([[(x + 0.5) * ss - 0.5, (y + 0.5) * ss - 0.5] for x, y in poly])
    x0, y0 = int(np.floor(pts[:, 0].min() / ss)) - 1, int(np.floor(pts[:, 1].min() / ss)) - 1
    x1, y1 = int(np.ceil(pts[:, 0].max() / ss)) + 2, int(np.ceil(pts[:, 1].max() / ss)) + 2
    big = np.zeros(((y1 - y0) * ss, (x1 - x0) * ss), np.uint8)
    cv2.fillPoly(big, [np.round(pts - [x0 * ss, y0 * ss]).astype(np.int32)], 255)
    small = cv2.resize(big.astype(np.float32) / 255, (x1 - x0, y1 - y0), interpolation=cv2.INTER_AREA)
    out = np.zeros(shape, np.float32)
    out[y0:y1, x0:x1] = small
    return out


def build_foreground(base):
    """Full-frame alpha of everything in the base that stands in front of singers."""
    cache = WORK / "fg_alpha.npy"
    if cache.exists():
        return np.load(cache), json.loads((WORK / "fg_info.json").read_text())
    from sam_util import SoftSAM

    H, W = base.shape[:2]
    X0, Y0, X1, Y1 = SCENE
    c = fit_rail(base)
    xs = np.arange(W, dtype=float)
    top = np.polyval(c, xs)
    yy = np.arange(H, dtype=float)[:, None]
    a_rail = np.clip(yy - top[None, :] + 0.5, 0, 1).astype(np.float32)  # sub-pixel AA edge
    a_rail[:, :X0 + 4] = 0
    a_rail[:, X1 - 4:] = 0
    a_rail[Y1:] = 0
    sam = SoftSAM(str(CACHE / "sam2.1_l.pt"))
    alpha = np.maximum(a_rail, polygon_alpha((H, W), [(x - 0.5, y - 0.5) for x, y in PULPIT_POLY]))
    parts = {}
    for name, box in FG_OBJECTS.items():
        a = soft_object_alpha(base, sam, box, matting=True)
        parts[name] = a
        alpha = np.maximum(alpha, a)
    np.save(cache, alpha)
    info = dict(rail_poly=c.tolist(), rail_top_at=[float(np.polyval(c, x)) for x in (300, 725, 1150)])
    (WORK / "fg_info.json").write_text(json.dumps(info, indent=1))
    return alpha, info


def fg_opaque_top(fg_alpha):
    """For each column: first row from which the foreground stays (almost) opaque
    for at least 10 rows - i.e. the line below which anything is hidden."""
    H, W = fg_alpha.shape
    op = fg_alpha > 0.995
    run = np.zeros_like(op, dtype=np.int32)
    for y in range(H - 1, -1, -1):
        run[y] = np.where(op[y], (run[y + 1] + 1) if y + 1 < H else 1, 0)
    top = np.full(W, H, int)
    for x in range(W):
        ys = np.nonzero(run[:, x] >= 10)[0]
        if len(ys):
            top[x] = ys[0]
    return top


# --------------------------------------------------------------------- placement

def compute_shift(lab, rows, N, dx, fg_top):
    """Vertical offset that tucks the tier's own rail cut (bottom of front-row
    bodies) RAIL_MARGIN px below the base foreground's opaque top."""
    need = []
    H, W = lab.shape
    for i in range(N):
        if rows[i] != 0:  # the front row's bottoms are cut by the tier's own rail
            continue
        m = lab == i
        cols = np.nonzero(m.any(0))[0]
        yb = np.array([np.nonzero(m[:, x])[0].max() for x in cols])
        below_free = np.array([yb[k] + 7 < H and (lab[yb[k] + 1:yb[k] + 8, x] == -1).all() for k, x in enumerate(cols)])
        # flat runs (|step| <= 1 for >= 10 columns) whose underside is scenery = the
        # straight cut made by the tier's own rail / pulpit, not a natural outline
        flat = np.zeros(len(cols), bool)
        k = 0
        while k < len(cols):
            e = k
            while e + 1 < len(cols) and cols[e + 1] == cols[e] + 1 and abs(int(yb[e + 1]) - int(yb[e])) <= 1:
                e += 1
            if e - k + 1 >= 10:
                flat[k:e + 1] = True
            k = e + 1
        for k, x in enumerate(cols):
            if flat[k] and below_free[k] and 0 <= x + dx < W:
                need.append((int(yb[k]), fg_top[x + dx] + RAIL_MARGIN - int(yb[k])))
    need = np.array(need)
    # Only cuts near the tier's dominant rail line count (its pulpit top sits <= ~21px
    # higher); flat-ish natural outlines such as raised hands are ignored.
    y_cut = np.median(need[:, 0])
    need = need[need[:, 0] >= y_cut - 24, 1]
    return int(np.ceil(np.percentile(need, 75))), need


# ------------------------------------------------------------------------- matting

def matte_person(tier, lab, i, front_of_i, logits_i, hidden):
    """RGBA cut-out (in tier coords) for person i.

    - boundary against background / people behind i: closed-form matting band
    - boundary against people in front of i: alpha extended *under* them (hard),
      they are always revealed no later than i and painted on top
    - boundary inside the base foreground (rail cut): hard, extended downwards
    Colour of semi-transparent pixels is decontaminated (ML foreground estimate);
    fully opaque pixels are the tier image's own pixels."""
    from pymatting import estimate_alpha_cf, estimate_foreground_ml

    H, W = lab.shape
    m = lab == i
    ys, xs = np.nonzero(m)
    pad = 12
    x0, y0 = max(0, xs.min() - pad), max(0, ys.min() - pad)
    x1, y1 = min(W, xs.max() + pad + 1), min(H, ys.max() + pad + 1)
    M = m[y0:y1, x0:x1]
    L = lab[y0:y1, x0:x1]
    HID = hidden[y0:y1, x0:x1]
    FRONT = np.isin(L, list(front_of_i)) if front_of_i else np.zeros_like(M)
    img = tier[y0:y1, x0:x1]
    lg = np.full(M.shape, -20.0, np.float32)
    (l, (ox, oy)) = logits_i
    # paste person logits into the crop window
    sx0, sy0 = max(x0, ox), max(y0, oy)
    sx1, sy1 = min(x1, ox + l.shape[1]), min(y1, oy + l.shape[0])
    lg[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0] = l[sy0 - oy:sy1 - oy, sx0 - ox:sx1 - ox]

    # Extensions (alpha = 1): under front occluders and into hidden scenery.
    # (unassigned hairline gaps next to an occluder are filled too, otherwise the
    # base would peek through the occluder's soft edge)
    gap = (L == -1) & ~M
    ext = (dil(M, 5) & (FRONT | (gap & dil(FRONT, 3)))) | (dil(M, 3) & HID & gap)
    core = M | ext
    # Unknown band: around the outline facing background / people behind i, plus
    # SAM-uncertain pixels (|logit| small) near the outline (hair, fuzzy robes).
    edge_zone = dil(M, 3) & ~ero(M, 2)
    uncertain = (np.abs(lg) < 2.5) & dil(M, 4)
    unknown = (edge_zone | uncertain) & ~dil(FRONT | (HID & (L == -1)), 2) & ~ext
    # never let the band eat into other people's interiors in front of i
    tri = np.zeros(M.shape, np.float64)
    tri[core] = 1.0
    tri[unknown] = 0.5
    tri[ext] = 1.0
    if (tri == 0.5).any():
        rgb = img[..., ::-1].astype(np.float64) / 255
        a = estimate_alpha_cf(rgb, tri)
        a = np.clip(a, 0, 1)
        a[tri == 1] = 1
        a[tri == 0] = 0
        fg = estimate_foreground_ml(rgb, a)
        fg = np.clip(fg, 0, 1)
    else:
        a = tri
        fg = img[..., ::-1].astype(np.float64) / 255
    # Tidy: tiny alphas -> 0; drop speckles disconnected from the body.
    a[a < 0.02] = 0
    n, cc, st, _ = cv2.connectedComponentsWithStats((a > 0).astype(np.uint8), 8)
    if n > 2:
        main = 1 + int(np.argmax(st[1:, cv2.CC_STAT_AREA]))
        for k in range(1, n):
            if k != main and st[k, cv2.CC_STAT_AREA] < 30:
                a[cc == k] = 0
    orig = img[..., ::-1].astype(np.float64) / 255
    use_orig = a >= 0.999
    rgb_out = np.where(use_orig[..., None], orig, fg)
    rgba = np.dstack([np.round(rgb_out * 255), np.round(a * 255)]).astype(np.uint8)
    return rgba, (x0, y0)


def tight(rgba, origin):
    a = rgba[..., 3]
    ys, xs = np.nonzero(a)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    return rgba[y0:y1, x0:x1], (origin[0] + x0, origin[1] + y0)


def bleed_rgb(rgba, iters=8):
    """Extend edge colours into transparent pixels so lossy WebP (YUV 4:2:0) does
    not bleed dark/odd colours into the soft edges."""
    rgb = rgba[..., :3].astype(np.float32)
    a = (rgba[..., 3] > 0).astype(np.float32)
    acc_rgb, acc_a = rgb * a[..., None], a.copy()
    out = rgb.copy()
    filled = a > 0
    k = np.ones((3, 3), np.float32)
    for _ in range(iters):
        s = cv2.filter2D(acc_rgb, -1, k, borderType=cv2.BORDER_CONSTANT)
        n = cv2.filter2D(acc_a, -1, k, borderType=cv2.BORDER_CONSTANT)
        new = (~filled) & (n > 0)
        out[new] = s[new] / n[new][:, None]
        acc_rgb[new] = out[new]
        acc_a[new] = 1
        filled |= new
    mean = (rgb * a[..., None]).sum((0, 1)) / max(1.0, a.sum())
    out[~filled] = mean
    return np.dstack([np.clip(np.round(out), 0, 255).astype(np.uint8), rgba[..., 3]])


def save_webp(path, rgba, lossless=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    im = Image.fromarray(bleed_rgb(rgba) if not lossless else rgba, "RGBA")
    if lossless:
        im.save(path, "WEBP", lossless=True, quality=100, method=6, exact=True)
    else:
        im.save(path, "WEBP", quality=WEBP_QUALITY, alpha_quality=100, method=6, exact=True)


# ---------------------------------------------------------------------- composites

def composite(base_rgb, sprites, fg_rgba, fg_xy):
    """base + sprites (list of (rgba, x, y) in paint order) + foreground."""
    out = base_rgb.astype(np.float32)
    for rgba, x, y in list(sprites) + [(fg_rgba, *fg_xy)]:
        h, w = rgba.shape[:2]
        a = rgba[..., 3:4].astype(np.float32) / 255
        out[y:y + h, x:x + w] = rgba[..., :3] * a + out[y:y + h, x:x + w] * (1 - a)
    return np.clip(out + 0.5, 0, 255).astype(np.uint8)


def label_overlay(tier, lab, people, order, rows, front_edges):
    N = len(people)
    rng = np.random.RandomState(7)
    cols = rng.randint(70, 255, (N, 3))
    g = cv2.cvtColor(cv2.cvtColor(tier, cv2.COLOR_BGR2GRAY), cv2.COLOR_GRAY2BGR) // 2
    out = g.copy()
    for i in range(N):
        m = lab == i
        out[m] = (tier[m] * 0.55 + cols[i] * 0.45).astype(np.uint8)
        cnts, _ = cv2.findContours(m.astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
        cv2.drawContours(out, cnts, -1, (255, 255, 255), 1)
    pos = {i: (int(people[i]["head"][0]), int(people[i]["head"][1])) for i in range(N)}
    for (a, b) in front_edges:
        cv2.arrowedLine(out, pos[a], pos[b], (0, 0, 255), 1, tipLength=0.15)
    for i in range(N):
        k = order.index(i)
        cv2.putText(out, f"{k}", (pos[i][0] - 6, pos[i][1] + 4), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (0, 0, 0), 3)
        cv2.putText(out, f"{k}", (pos[i][0] - 6, pos[i][1] + 4), cv2.FONT_HERSHEY_SIMPLEX, 0.42,
                    (0, 255, 255) if rows[i] == 0 else (255, 255, 0) if rows[i] == 1 else (255, 0, 255), 1)
    X0, Y0, X1, Y1 = SCENE
    return out[Y0:Y1, X0:X1]


# ---------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fresh", action="store_true", help="ignore cached detections / SAM logits")
    ap.add_argument("--tiers", nargs="*", default=list(TIER_FILES), help="subset of tiers to (re)build")
    args = ap.parse_args()

    WORK.mkdir(parents=True, exist_ok=True)
    if args.fresh:
        for p in WORK.glob("*"):
            if p.is_file() and p.suffix in (".json", ".npz", ".npy"):
                p.unlink()
    ensure_weights()

    base = imread(BASE_FILE)
    H, W = base.shape[:2]
    base_rgb = base[..., ::-1].copy()

    log("foreground ...")
    fg_alpha, fg_info = build_foreground(base)
    fg_top = fg_opaque_top(fg_alpha)

    manifest_path = OUT_DIR / "manifest.json"
    old = json.loads(manifest_path.read_text()) if manifest_path.exists() else None
    tiers_out = {t["name"]: t for t in (old or {}).get("tiers", [])}
    report = {}
    all_sprite_boxes = []

    for name in args.tiers:
        log(f"== {name}")
        tier = imread(TIER_FILES[name])
        people = detect_people(tier, name)
        N = len(people)
        log(f"  {N} people detected")
        reg = register_architecture(base, tier, [p["box"] for p in people])
        dx = int(round(reg["shift_at_choir"][0]))
        log(f"  architecture: scale={reg['scale']:.4f} angle={reg['angle_deg']:.3f} "
            f"shift={reg['shift_at_choir'][0]:+.2f},{reg['shift_at_choir'][1]:+.2f} "
            f"inliers={reg['inliers']} res={reg['median_residual']:.2f}")
        logits = sam_logits(tier, name, people)
        lab = ownership((H, W), logits)
        # Recover what SAM leaves out of the person masks, in three passes; each
        # result is merged into its owner's logits and ownership is recomputed.
        dbg = tier.copy()
        colours = {"book": (255, 255, 0), "pocket": (255, 255, 0), "limb": (0, 255, 255), "enclosed": (255, 128, 0)}
        for stage in ("held", "limbs", "enclosed"):
            if stage == "held":
                objs, recs = held_objects(tier, name, people, lab)
            elif stage == "limbs":
                objs, recs = refine_limbs(tier, name, people, lab, logits)
            else:
                objs, recs = enclosed_gaps(tier, name, people, lab, assign_rows(people, lab, N))
            for rec in recs:
                if rec["kind"] in ("book", "pocket"):
                    x0_, y0_, x1_, y1_ = map(int, rec["box"])
                    cv2.rectangle(dbg, (x0_, y0_), (x1_, y1_), (0, 255, 0) if rec["ok"] else (0, 0, 255), 1)
            kinds = [r["kind"] for r in recs if r["ok"]]
            for (l2, org, owner), kind in zip(objs, kinds):
                logits[owner] = merge_logits(logits[owner], (l2, org))
                cnts, _ = cv2.findContours(((l2 > 0) * 255).astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
                cv2.drawContours(dbg, [c + np.array(org) for c in cnts], -1, colours[kind], 1)
                ys_, xs_ = np.nonzero(l2 > 0)
                cv2.line(dbg, (int(xs_.mean() + org[0]), int(ys_.mean() + org[1])),
                         tuple(int(v) for v in people[owner]["head"]), (255, 0, 255), 1)
            log(f"  {stage}: attached {len(objs)} region(s) (candidates {len(recs)})")
            lab = ownership((H, W), logits)
        cv2.imwrite(str(WORK / f"held_{name}.png"), dbg[SCENE[1]:SCENE[3], SCENE[0]:SCENE[2]])
        rows = assign_rows(people, lab, N)
        rel = occlusion_graph(lab, people, rows, N)
        front_edges = [((i, j) if r["score"] > 0 else (j, i)) for (i, j), r in rel.items() if r["score"] != 0]
        order, pref = reveal_order(people, rows, front_edges, N)
        # Paint order = reverse reveal order. It satisfies every occlusion edge (the
        # reveal order does), and for side-by-side neighbours it puts the one revealed
        # first on top, so the later one can tuck a hard edge underneath it.
        z = order[::-1]
        rank = {i: k for k, i in enumerate(order)}
        violated = [(a, b) for (a, b) in front_edges if rank[a] > rank[b]]
        if violated:
            log(f"  warning: occlusion edges violated by reveal order: {violated}")
        dy, need = compute_shift(lab, rows, N, dx, fg_top)
        log(f"  rows={int(rows.max()) + 1} (sizes {np.bincount(rows).tolist()}), shift dx={dx} dy={dy} "
            f"(need p50={np.percentile(need, 50):.0f} p97={np.percentile(need, 97):.0f} max={need.max():.0f})")
        cv2.imwrite(str(WORK / f"labels_{name}.png"), label_overlay(tier, lab, people, order, rows, front_edges))

        # hidden zone in tier coords: pixels that land in opaque base foreground
        hidden = np.zeros((H, W), bool)
        M = np.float32([[1, 0, -dx], [0, 1, -dy]])
        hidden = cv2.warpAffine((fg_alpha > 0.995).astype(np.uint8), M, (W, H), flags=cv2.INTER_NEAREST) > 0

        out_dir = OUT_DIR / name
        if out_dir.exists():
            shutil.rmtree(out_dir)
        sprites = {}
        # Who is drawn over whom where two sprites touch: true occluders, plus the
        # earlier-revealed partner of a side-by-side pair.
        fronts = {i: set() for i in range(N)}
        for (i, j), r in rel.items():
            if r["score"] > 0:
                fronts[j].add(i)
            elif r["score"] < 0:
                fronts[i].add(j)
            else:
                a, b = (i, j) if rank[i] < rank[j] else (j, i)
                fronts[b].add(a)
        for i in range(N):
            rgba, org = matte_person(tier, lab, i, fronts[i], logits[i], hidden)
            rgba, org = tight(rgba, org)
            sprites[i] = (rgba, org[0] + dx, org[1] + dy)
        people_out = []
        for k, i in enumerate(order):
            rgba, x, y = sprites[i]
            fn = f"{name}/p{k:02d}.webp"
            save_webp(OUT_DIR / fn, rgba)
            a = rgba[..., 3].astype(np.float64)
            yy, xx = np.mgrid[:a.shape[0], :a.shape[1]]
            cx = float((a * xx).sum() / a.sum()) + x
            cy = float((a * yy).sum() / a.sum()) + y
            people_out.append(dict(file=fn, x=int(x), y=int(y), w=int(rgba.shape[1]), h=int(rgba.shape[0]),
                                   row=int(rows[i]), order=k, z=z.index(i), cx=round(cx, 1), cy=round(cy, 1),
                                   head=[round(people[i]["head"][0] + dx, 1), round(people[i]["head"][1] + dy, 1)]))
            all_sprite_boxes.append((x, y, x + rgba.shape[1], y + rgba.shape[0]))
        people_out.sort(key=lambda p: p["z"])
        tiers_out[name] = dict(name=name, count=N, rows=int(rows.max()) + 1, offset=[dx, dy], people=people_out)
        report[name] = dict(count=N, rows=int(rows.max()) + 1, row_sizes=np.bincount(rows).tolist(), dx=dx, dy=dy,
                            registration=reg, occlusion_edges=len(front_edges),
                            order_changed_by_occlusion=[int(i) for i in order] != [int(i) for i in pref])
        np.savez_compressed(WORK / f"layout_{name}.npz", lab=lab, rows=rows, order=np.array(order), z=np.array(z),
                            dx=dx, dy=dy)
        (WORK / f"occlusion_{name}.json").write_text(json.dumps(
            {f"{i}-{j}": r for (i, j), r in rel.items()}, indent=1))

    # Foreground crop: union of where sprites can be, plus margin.
    prev = json.loads(manifest_path.read_text()).get("_sprite_extent") if manifest_path.exists() else None
    if prev:
        all_sprite_boxes.append(tuple(prev))
    sx0 = min(b[0] for b in all_sprite_boxes)
    sx1 = max(b[2] for b in all_sprite_boxes)
    sy1 = max(b[3] for b in all_sprite_boxes)
    sy0 = min(b[1] for b in all_sprite_boxes)
    ys = np.nonzero(fg_alpha[:, sx0:sx1].max(1) > 0)[0]
    fy0 = int(max(sy0, ys.min()))
    fy1 = int(min(H, sy1 + 8))
    fx0, fx1 = int(max(0, sx0 - 4)), int(min(W, sx1 + 4))
    fg_rgba = np.dstack([base_rgb[fy0:fy1, fx0:fx1], np.round(fg_alpha[fy0:fy1, fx0:fx1] * 255).astype(np.uint8)])
    # trim fully transparent border
    a = fg_rgba[..., 3]
    ys, xs = np.nonzero(a)
    fg_rgba = fg_rgba[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    fx0, fy0 = fx0 + int(xs.min()), fy0 + int(ys.min())
    save_webp(OUT_DIR / "foreground.webp", fg_rgba, lossless=True)

    tiers_sorted = sorted(tiers_out.values(), key=lambda t: t["count"])
    manifest = dict(
        canvas=dict(w=W, h=H),
        foreground=dict(file="foreground.webp", x=fx0, y=fy0, w=int(fg_rgba.shape[1]), h=int(fg_rgba.shape[0])),
        tiers=tiers_sorted,
        _notes=("people[] are listed in paint order (back to front, also given as z); reveal people with "
                "order < k. Draw base, then visible sprites in z order, then foreground. x,y are top-left in "
                "base-image pixels; cx,cy = alpha-weighted centroid; head = face centre."),
        _sprite_extent=[int(sx0), int(sy0), int(sx1), int(sy1)],
    )
    manifest_path.write_text(json.dumps(manifest, indent=1))
    (WORK / "report.json").write_text(json.dumps(report, indent=1))
    log("wrote", manifest_path)

    verify(base_rgb, manifest)


def verify(base_rgb, manifest):
    """Composites for visual review in .work/verify/."""
    vdir = WORK / "verify"
    vdir.mkdir(parents=True, exist_ok=True)
    fgm = manifest["foreground"]
    fg = np.array(Image.open(OUT_DIR / fgm["file"]).convert("RGBA"))
    X0, Y0, X1, Y1 = SCENE
    for t in manifest["tiers"]:
        name = t["name"]
        ppl = t["people"]  # paint order
        sp = {p["order"]: (np.array(Image.open(OUT_DIR / p["file"]).convert("RGBA")), p["x"], p["y"], p["z"])
              for p in ppl}
        n = len(ppl)
        ks = sorted(set([1, 2, 3, max(1, n // 4), max(1, n // 2), max(1, (3 * n) // 4), n]))
        tiles = []
        for k in ks:
            vis = sorted([sp[o] for o in range(k)], key=lambda s: s[3])
            c = composite(base_rgb, [(s[0], s[1], s[2]) for s in vis], fg, (fgm["x"], fgm["y"]))[Y0:Y1, X0:X1]
            c = c.copy()
            cv2.putText(c, f"{name} k={k}/{n}", (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 0), 4)
            cv2.putText(c, f"{name} k={k}/{n}", (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
            Image.fromarray(c).save(vdir / f"{name}_k{k:02d}.png")
            tiles.append(c)
        full = tiles[-1]
        orig = imread(TIER_FILES[name])[..., ::-1][Y0:Y1, X0:X1].copy()
        cv2.putText(orig, f"original {name}", (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 0), 4)
        cv2.putText(orig, f"original {name}", (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
        Image.fromarray(np.hstack([full, orig])).save(vdir / f"{name}_full_vs_original.png")
        # contact sheet of every k (small)
        sheet = []
        for k in range(1, n + 1):
            vis = sorted([sp[o] for o in range(k)], key=lambda s: s[3])
            c = composite(base_rgb, [(s[0], s[1], s[2]) for s in vis], fg, (fgm["x"], fgm["y"]))[230:470, 200:1250]
            c = cv2.resize(c, None, fx=0.5, fy=0.5, interpolation=cv2.INTER_AREA)
            cv2.putText(c, f"k={k}", (4, 14), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 0), 1)
            sheet.append(c)
        while len(sheet) % 3:
            sheet.append(np.zeros_like(sheet[0]))
        rows = [np.hstack(sheet[r:r + 3]) for r in range(0, len(sheet), 3)]
        Image.fromarray(np.vstack(rows)).save(vdir / f"{name}_all_k.png")
    log("verification images in", vdir)


if __name__ == "__main__":
    main()
