# Singer sprite pipeline

`build_singers.py` turns the AI mockups in `design/mockups/` into the layered choir
assets in `ui/assets/choir/`:

- `tNN/pXX.webp` - one RGBA cut-out per singer per tier (`XX` = reveal order),
  native resolution, lossy WebP q90 with lossless alpha.
- `foreground.webp` - lossless RGBA crop of the empty-loft base image holding what
  stands in front of the singers (rail and panelling, pulpit and cross cloth,
  candles, lilies). Draw it last.
- `manifest.json` - placement in base-image pixels, rows, reveal order, paint order.

## Running

```sh
python3 -m venv --system-site-packages tools/art/.venv
tools/art/.venv/bin/pip install --index-url https://download.pytorch.org/whl/cpu torch==2.14.1 torchvision==0.29.1
tools/art/.venv/bin/pip install -r tools/art/requirements.txt
tools/art/.venv/bin/python tools/art/build_singers.py            # all tiers
tools/art/.venv/bin/python tools/art/build_singers.py --tiers t45  # one tier
tools/art/.venv/bin/python tools/art/build_singers.py --fresh      # drop cached stages
```

It runs on CPU. A clean run takes about 20-30 minutes, mostly SAM. Detections, SAM
logits and the foreground alpha are cached in `tools/art/.work/`, so a re-run after
editing the post-processing takes about a minute.

### Model weights (downloaded to `tools/art/.cache/` on first run)

| file | size | source |
|---|---|---|
| `yolo11x.pt` | 115 MB | person and book boxes (COCO), github.com/ultralytics/assets v8.3.0 |
| `yolo11x-pose.pt` | 118 MB | face keypoints used as SAM point prompts, same release |
| `sam2.1_l.pt` | 450 MB | SAM 2.1 Large, ultralytics conversion, same release |

`.cache/` and `.work/` are gitignored.

## What it does

1. **Detection.** YOLO11x runs on the church scene upscaled 2x with flip TTA, so
   the small back-row singers are found. YOLO11x-pose gives a face point for each
   box.
2. **Segmentation.** SAM 2.1-L is prompted with each box, a positive face point and
   negative points on the neighbours' faces. It runs on an upscaled crop around each
   person, which gives far finer edges than running it on the full frame. Soft
   logits are kept. Each pixel then goes to the person with the highest positive
   logit.
3. **Held objects.** SAM never puts a held hymnal in the person mask. Book boxes
   from YOLO and dark unassigned pockets next to people are segmented with SAM,
   validated (mostly unassigned, dark, bordered by people) and merged into the
   holder: the adjacent person whose face is closest above the book.
4. **Occlusion.** For every pair of touching masks, the pipeline decides who is in
   front. When the two faces are about a head-height or more apart, the lower face
   is in front (the singers stand on risers). Otherwise the occluder is the mask
   whose outline is locally convex along the shared boundary.
5. **Rows and order.** Face heights are clustered into rows, with row 0 at the
   front. The preferred reveal order goes row by row, centre-outward and alternating
   left and right. A priority topological sort then makes sure an occluder is never
   revealed after the person it covers. `z` is a back-to-front paint order that is
   consistent with the same occlusion edges.
6. **Matting.** Outlines facing the background, or facing people behind, get a
   closed-form alpha matte (pymatting) in a narrow band. The trimap is widened
   where SAM was uncertain, for example on hair. Semi-transparent edge pixels are
   colour-decontaminated with `estimate_foreground_ml`. Fully opaque pixels are the
   tier image's own pixels. Outlines covered by a person in front, or by the base
   foreground, are hard and run a few px underneath it, so no seam shows.
7. **Registration.** A SIFT + RANSAC similarity transform on the church interior
   above the choir (people masked out) gives the horizontal offset. In every tier
   the architecture agrees with the base to within about 2 px. The mockups do
   differ in the height of the loft rail, so the vertical offset is chosen so that
   the flat cut the tier's own rail leaves on front-row bodies sits `RAIL_MARGIN`
   (3) px below the top of the base foreground. Sprites are only translated, never
   resampled.
8. **Foreground.** This is the base image below the fitted rail ledge, plus the
   pulpit top (a measured polygon) and the lilies and end candles (SAM + matting).
9. **Verification.** `tools/art/.work/verify/` holds composites for several k per
   tier (`tNN_kXX.png`), a contact sheet of every k (`tNN_all_k.png`) and the full
   tier next to the original mockup (`tNN_full_vs_original.png`).
   `tools/art/.work/labels_tNN.png` shows the masks, the reveal index and the
   occlusion arrows (front -> back). `held_tNN.png` shows the attached books.

## Using the manifest

```
draw base image
for p in tier.people (already sorted by z, back to front):
    if p.order < visibleCount: draw p.file at (p.x, p.y)
draw foreground.webp at (foreground.x, foreground.y)
```

`cx, cy` is the alpha-weighted centroid, `head` is the face centre and `row` 0 is
the front row (all in base pixels). Tiers are sorted by `count`.
