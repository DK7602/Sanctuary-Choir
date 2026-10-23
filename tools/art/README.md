# Singer sprite pipeline

`build_singers.py` turns the AI mockups in `design/mockups/` into the layered choir
assets in `ui/assets/choir/`:

- `tNN/pXX.webp`: one RGBA cut-out per singer per tier. `XX` is the reveal order.
  Sprites are at native resolution and saved as lossy WebP q90 with lossless alpha.
- `foreground.webp`: a lossless RGBA crop of the empty-loft base image. It holds
  everything that stands in front of the singers: the rail and panelling, the
  pulpit with its cross cloth, the end candles and the lilies. Draw it last.
- `manifest.json`: placement in base-image pixels, plus rows, reveal order and
  paint order.

## Running

```sh
python3 -m venv --system-site-packages tools/art/.venv
tools/art/.venv/bin/pip install --index-url https://download.pytorch.org/whl/cpu torch==2.14.1 torchvision==0.29.1
tools/art/.venv/bin/pip install -r tools/art/requirements.txt
tools/art/.venv/bin/python tools/art/build_singers.py            # all tiers
tools/art/.venv/bin/python tools/art/build_singers.py --tiers t45  # one tier
tools/art/.venv/bin/python tools/art/build_singers.py --fresh      # recompute every cached stage
```

Everything runs on the CPU. A clean run takes about 30 minutes on 4 cores, mostly
spent in SAM. The model outputs are cached in `tools/art/.work/`: detections, pose,
SAM logits, held-object and limb segments, and the foreground alpha. A re-run
after you change the post-processing therefore takes about 2 minutes.

### Model weights

These are downloaded to `tools/art/.cache/` on the first run:

| file | size | used for |
|---|---|---|
| `yolo11x.pt` | 115 MB | person boxes and hymnal ("book") boxes (COCO) |
| `yolo11x-pose.pt` | 118 MB | face, elbow and wrist keypoints |
| `sam2.1_l.pt` | 450 MB | SAM 2.1 Large (ultralytics conversion) |

All three come from https://github.com/ultralytics/assets/releases/tag/v8.3.0.
`.cache/` and `.work/` are gitignored.

## What it does

1. **Detection.** YOLO11x runs on the church scene upscaled 2x, with horizontal
   flip TTA, so the small back-row singers are found. YOLO11x-pose gives each
   person a face point and arm keypoints.
2. **Segmentation.** SAM 2.1-L is prompted with each person's box, a positive
   point on their face and negative points on neighbouring faces. It runs on an
   upscaled crop around the person. The soft logits are kept, and each pixel goes
   to the person with the highest positive logit.
3. **Recovering what SAM leaves out.** Each result is merged into its owner's
   logits.
   - *Hymnals.* SAM never includes a held book in the person mask. The script
     takes YOLO book boxes and dark unassigned pockets as seeds and segments them
     with SAM on a few shared 320px tiles. A segment is accepted if it is
     near-neutral black. It goes to the holder of an already-accepted book it
     touches. Otherwise it goes to the adjacent person whose face is above the
     book and most nearly over it, provided that person does not clearly occlude
     it. A pocket that sits between two bodies is treated as shadow and is not
     attached.
   - *Raised hands.* SAM also misses raised hands. If a confident
     shoulder-elbow(-wrist) chain lies on the person or on unassigned pixels,
     but the elbow or wrist itself is unassigned, SAM is re-run with those
     keypoints as extra positive points. Only newly gained pixels that connect
     to the body are kept.
   - *Enclosed gaps.* Unassigned regions completely surrounded by singers are
     handled by type. A region containing a neighbour's wrist is that
     neighbour's hand. Tiny regions are mask slop and go to a neighbour. Larger
     regions are glimpses of the stalls and stay transparent.
4. **Rows.** Face heights are clustered with an optimal 1-D k-means (dynamic
   programming). The pipeline uses the largest k whose row centres are at least
   a quarter body-height apart. Someone whose bottom edge is mostly covered by
   front-row singers is moved one row back. Row 0 is the front row.
5. **Occlusion.** The pipeline decides who is in front for every pair of
   touching masks:
   - Different rows: the front row wins, unless the silhouettes clearly say
     otherwise.
   - Same row, faces a head-height apart: the lower face is in front.
   - Otherwise only a clear silhouette cue counts. Along the shared boundary the
     occluder's outline is locally convex (head, shoulder, book edge) and the
     occluded outline is locally concave.
   - Weak cases are "side by side" and create no constraint.
   `tools/art/.work/occlusion_tNN.json` lists every pair with its reason.
6. **Reveal order.** The preferred order goes row by row from the front,
   centre-outward, alternating left and right. A priority topological sort then
   enforces "occluder no later than occludee". An occluder inherits the earliest
   preferred slot of anyone it covers, so constraints pull occluders forward
   rather than holding back the people they cover. The paint order is the
   reverse of the reveal order, so a newly revealed singer always goes *behind*
   the singers already shown.
7. **Matting.** Each person's alpha is built from three kinds of edge:
   - Outlines that face the background, or a person drawn underneath, get a
     closed-form alpha matte (pymatting) in a 2-3 px band. The band is wider
     where SAM was unsure, for example around hair.
   - Edge colours are decontaminated with `estimate_foreground_ml`.
   - Outlines that lie under a person drawn on top, or under the base
     foreground, are hard and run a few px underneath, so no seam can show.
   The interior is always fully opaque. Opaque pixels are the tier image's own
   pixels; the models only supply alpha.
8. **Registration.** A SIFT + RANSAC similarity transform is fitted on the church
   interior above the choir, with people masked out. All four tiers come out at
   scale 0.996-0.999 and rotation under 0.07 degrees, which gives the horizontal
   offset. The mockups disagree on the height of the loft rail (by up to 40 px),
   so the vertical offset is chosen so that the flat cut left by the tier's own
   rail sits about 3 px (`RAIL_MARGIN`) under the base rail. It is set from the
   75th percentile over the front row's cut columns. Sprites are only
   translated, never resampled.
9. **Foreground.** The foreground consists of:
   - the base image below the fitted rail ledge (anti-aliased);
   - the pulpit top, as a measured polygon (SAM confuses it with the seats behind);
   - the lilies and end candles (SAM + matting).
10. **Verification.** `tools/art/.work/verify/` holds:
    - `tNN_kXX.png`: composites for several values of k;
    - `tNN_all_k.png`: a contact sheet of every k;
    - `tNN_full_vs_original.png`: the full tier next to the original mockup.

    In `tools/art/.work/`, `labels_tNN.png` shows masks, reveal indices and
    occlusion arrows (front -> back), and `held_tNN.png` shows the attached books
    (cyan), limbs (yellow) and gaps (orange).

Manual hooks: `EXTRA_PEOPLE` / `DROP_PEOPLE` in the script add or remove person
boxes per tier. They are empty at the moment; the models found everyone.

## Using the manifest

```
draw base image (perform-00-empty.png)
for p in reversed(tier.people):          # people[] is in reveal order
    if p.order < visibleCount: draw p.file at (p.x, p.y)
draw foreground.webp at (foreground.x, foreground.y)
```

`z` is the explicit paint index (`count - 1 - order`). `cx, cy` is the
alpha-weighted centroid, `head` is the face centre and `row` 0 is the front row,
all in base pixels. `offset` is the translation from tier pixels to base pixels.
Tiers are sorted by `count`.

# Rooms

`build_rooms.py` gives every venue its own scene and choir, from the mockups in
`design/mockups/rooms/`: each room once with its choir and once empty (the Stone Abbey
also with monks, shown when the Style is Gregorian). It writes `ui/assets/rooms/`:

- `<room>/base.webp`: the empty room, cut to the frame's opening (its alpha). Drawn first.
- `<room>/pXX.webp`: one cut-out per singer, `XX` the reveal order.
- `<room>/fg.webp`: the scenery just below the singers (rail, stands, candles), lossless.
  Drawn last, so singers moving with the music never show a seam.
- `manifest.json`: `{ rooms: { <room>: { base, fg, count, rows, people[] } } }`, with the
  same per-person fields as the loft manifest.

```sh
tools/art/.venv/bin/python tools/art/build_rooms.py --stage detect   # YOLO, pose and SAM (cached)
tools/art/.venv/bin/python tools/art/build_rooms.py                  # every room
tools/art/.venv/bin/python tools/art/build_rooms.py --rooms gospel   # one room
```

The steps:

1. **Registration.** Each mockup is aligned with the plug-in's skin by SIFT on the frame
   around the scene (top bar and stained-glass columns), a similarity transform.
2. **The opening.** Where the rooms differ from one another (and from the skin) is the
   scene; the frame is the same in all. The median absolute deviation across all the
   pictures, blurred and thresholded, gives the opening. The largest component is kept,
   holes are filled and the edge is softened.
3. **Singers.** Detection, SAM, held books, raised hands, rows, occlusion, reveal order
   and matting are the loft pipeline's. A big figure in front of the choir (the gospel
   director, the cathedral conductor) whose SAM mask comes back far smaller than its
   box is redone with points down the body and its enclosed holes filled.
   Rooms in `ROOM_FIXES` (the studio) get more: each head is segmented again with a box
   around it and points on the ears, so headphones belong to their singer; hint points
   bring back a body seen again below a music stand's desk.
4. **Choir onto the empty room.** The two pictures of a room are not drawn identically
   (the abbey's choir picture is 4% larger). SIFT on the architecture outside the
   singers gives the transform, and every sprite is warped with it.
5. **Foreground.** Pixels of the empty room within a few pixels below a singer's edge,
   where the choir picture shows the same scenery, are drawn over the singers. In rooms
   with `props` (the studio), so are the stands, desks and microphones in front of the
   singers: pixels the two pictures share, with a singer on both sides, in shapes that
   carry on beyond the singer. The singers then sway behind fixed stands instead of
   carrying holes that slide over the room.
6. **Verification.** `.work/rooms/verify_<room>.jpg` puts the rebuilt room beside the
   mockup; `verify_<room>_third.jpg` shows a third of the choir; `labels_<room>.png`
   shows the masks and the occlusion.

In the UI the venue picks the room: the scene crossfades to it, and Choir Size reveals the
same share of that room's choir (5 singers in the studio, 62 in the cathedral).
