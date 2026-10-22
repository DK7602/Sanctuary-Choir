"""Thin wrapper around the ultralytics SAM 2.1 predictor that returns soft logits.

ultralytics' public API thresholds masks to booleans; for alpha matting we want the
raw decoder logits, so we prepare the 1024x1024 input ourselves and call the
predictor's decoder directly.
"""
from __future__ import annotations

import cv2
import numpy as np
import torch

SAM_RES = 1024


class SoftSAM:
    def __init__(self, weights: str):
        from ultralytics.models.sam import SAM2Predictor

        self.pred = SAM2Predictor(
            overrides=dict(model=weights, imgsz=SAM_RES, conf=0.0, save=False, verbose=False, device="cpu")
        )
        self.pred.setup_model(model=None, verbose=False)
        self._key = None

    def set_crop(self, crop_bgr: np.ndarray):
        """Pad the crop to a square, resize to 1024 and encode it. Returns the scale factor."""
        h, w = crop_bgr.shape[:2]
        s = SAM_RES / max(h, w)
        img = cv2.resize(crop_bgr, (round(w * s), round(h * s)), interpolation=cv2.INTER_CUBIC if s > 1 else cv2.INTER_AREA)
        pad = np.zeros((SAM_RES, SAM_RES, 3), np.uint8)
        pad[: img.shape[0], : img.shape[1]] = img
        self.pred.set_image(pad)
        self.scale = s
        self.crop_hw = (h, w)
        return s

    @torch.inference_mode()
    def logits(self, box, pos_pts=(), neg_pts=(), multimask=False):
        """box = (x0,y0,x1,y1) in crop pixels; points in crop pixels.

        Returns (logits[h,w] float32 in crop pixel space, score)."""
        s = self.scale
        pts = [[box[0] * s, box[1] * s], [box[2] * s, box[3] * s]]
        lbl = [2, 3]
        for p in pos_pts:
            pts.append([p[0] * s, p[1] * s]); lbl.append(1)
        for p in neg_pts:
            pts.append([p[0] * s, p[1] * s]); lbl.append(0)
        P = torch.tensor([pts], dtype=torch.float32)
        L = torch.tensor([lbl], dtype=torch.int32)
        masks, scores = self.pred._inference_features(self.pred.features, P, L, None, multimask)
        if multimask:
            k = int(torch.argmax(scores))
            masks, scores = masks[k : k + 1], scores[k : k + 1]
        lo = masks[0].float().cpu().numpy()  # 256x256 logits covering the 1024 square
        up = cv2.resize(lo, (SAM_RES, SAM_RES), interpolation=cv2.INTER_LINEAR)
        h, w = self.crop_hw
        hh, ww = round(h * s), round(w * s)
        up = up[:hh, :ww]
        out = cv2.resize(up, (w, h), interpolation=cv2.INTER_AREA if s > 1 else cv2.INTER_LINEAR)
        return out.astype(np.float32), float(scores[0])
