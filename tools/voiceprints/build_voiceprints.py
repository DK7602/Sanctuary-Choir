#!/usr/bin/env python3
"""Sanctuary Choir - voice prints of the VocalSet singers.

In Words mode every choir member is the lead's own voice, re-pitched. To make them sound like
different people, each member takes on how one real singer differs from the average singer of
their sex:

- formant scale: the length of their vocal tract (their formants sit this much higher or lower);
- tone: what remains of their spectral envelope after that, as six EQ bands;
- vibrato: their own rate and width;
- centre: where they sang (in VocalSet everyone of a sex sang the same exercises, so it hardly
  differs: the engine gives the shortest vocal tracts to the sopranos and tenors instead).

The envelope of each singer and vowel is measured from the harmonics of their straight-tone
long tones, scales and arpeggios. Many pitches sample it at many frequencies, so it is the
vocal tract's response and not one note's harmonics. The vibrato comes from their vibrato
scales.

usage: build_voiceprints.py <VocalSet FULL folder> [core/src/VoicePrints.inc]

Data: VocalSet (Wilkins, Seetharaman, Wahl, Pardo 2018), CC BY 4.0,
https://zenodo.org/records/1442513
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve

VOWELS = "aeiou"
BIN_LO, BIN_HI, BINS_PER_OCT = 100.0, 10000.0, 6
EDGES = 2.0 ** np.arange(np.log2(BIN_LO), np.log2(BIN_HI) + 1e-9, 1.0 / BINS_PER_OCT)
CENTRES = np.sqrt(EDGES[:-1] * EDGES[1:])
# The EQ each singer is given (the engine has the same bands): low shelf, four peaks, high shelf.
EQ_BANDS = [("lowshelf", 180.0, 0.7), ("peak", 420.0, 1.2), ("peak", 850.0, 1.2), ("peak", 1700.0, 1.2),
            ("peak", 2900.0, 1.8), ("highshelf", 5000.0, 0.7)]
EQ_LIMIT_DB = [4.0, 5.0, 5.0, 5.0, 5.0, 4.0]   # the shelves partly measure the microphone, so less
FIT_LO, FIT_HI = 250.0, 4500.0   # the formant region, where the vocal tract is measured


# ----------------------------------------------------------------------------------------------
def yin(x: np.ndarray, sr: float, fmin=70.0, fmax=1100.0, frame=2048, hop=441):
    """f0 (Hz, 0 = unvoiced) and the YIN aperiodicity per frame."""
    W = frame // 2
    tmax = min(W - 1, int(sr / fmin))
    tmin = max(2, int(sr / fmax))
    n = (len(x) - (W + tmax)) // hop
    f0 = np.zeros(max(n, 0))
    ap = np.ones(max(n, 0))
    centres = np.arange(max(n, 0)) * hop + W // 2
    for k in range(max(n, 0)):
        s = x[k * hop:k * hop + W + tmax].astype(np.float64)
        a = s[:W]
        if np.dot(a, a) < 1e-7 * W:
            continue
        # difference function via cross-correlation and running energies
        c = fftconvolve(s, a[::-1], mode="valid")[: tmax + 1]
        e = np.cumsum(np.concatenate([[0.0], s * s]))
        r0 = e[W] - e[0]
        rt = e[np.arange(tmax + 1) + W] - e[np.arange(tmax + 1)]
        d = r0 + rt - 2.0 * c
        d[0] = 0.0
        cm = d[1:] * np.arange(1, tmax + 1) / np.maximum(np.cumsum(d[1:]), 1e-12)
        cm = np.concatenate([[1.0], cm])
        lo = tmin
        cand = np.nonzero(cm[lo:tmax] < 0.15)[0]
        if len(cand):
            t = cand[0] + lo
            while t + 1 < tmax and cm[t + 1] < cm[t]:
                t += 1
        else:
            t = int(np.argmin(cm[lo:tmax])) + lo
        if cm[t] > 0.3:
            ap[k] = cm[t]
            continue
        # parabolic refinement
        if 1 <= t < tmax:
            y0, y1, y2 = cm[t - 1], cm[t], cm[t + 1]
            den = y0 - 2 * y1 + y2
            dt = 0.5 * (y0 - y2) / den if abs(den) > 1e-12 else 0.0
        else:
            dt = 0.0
        f0[k] = sr / (t + dt)
        ap[k] = cm[t]
    return f0, ap, centres


def steady(f0: np.ndarray, cents=30.0, reach=5):
    """Frames whose pitch stays within `cents` of their neighbours' median (no glides)."""
    ok = f0 > 0
    m = np.where(ok, 1200 * np.log2(np.maximum(f0, 1.0)), np.nan)
    out = np.zeros_like(ok)
    for k in range(len(f0)):
        if not ok[k]:
            continue
        w = m[max(0, k - reach):k + reach + 1]
        if np.sum(~np.isnan(w)) >= reach and abs(m[k] - np.nanmedian(w)) < cents:
            out[k] = True
    return out


def harmonics(x, sr, centre, f0, nfft=8192, frame=2048, top=BIN_HI):
    """(frequencies, levels dB) of the harmonics of one frame."""
    a = max(0, centre - frame // 2)
    seg = x[a:a + frame]
    if len(seg) < frame:
        return None
    spec = np.abs(np.fft.rfft(seg * np.hanning(frame), nfft))
    fs, ls = [], []
    k = 1
    while k * f0 < min(top, 0.45 * sr):
        b = k * f0 * nfft / sr
        half = max(2, int(0.25 * f0 * nfft / sr))
        lo, hi = int(b) - half, int(b) + half + 1
        if lo < 1 or hi >= len(spec):
            break
        fs.append(k * f0)
        ls.append(20 * np.log10(spec[lo:hi].max() + 1e-12))
        k += 1
    return np.array(fs), np.array(ls)


# ----------------------------------------------------------------------------------------------
def envelopes(singer_dir: Path):
    """Per vowel: mean level (dB, relative to the frame's level) per 1/6-octave bin, and the
    singer's median sung pitch (MIDI)."""
    sums = {v: np.zeros(len(CENTRES)) for v in VOWELS}
    counts = {v: np.zeros(len(CENTRES)) for v in VOWELS}
    pitches = []
    for group in ("long_tones/straight", "scales/straight", "arpeggios/straight"):
        d = singer_dir / group
        if not d.is_dir():
            continue
        for f in sorted(d.glob("*.wav")):
            v = f.stem[-1]
            if v not in VOWELS or f.stem[-2] != "_":
                continue
            x, sr = sf.read(f, dtype="float32")
            if x.ndim > 1:
                x = x.mean(axis=1)
            f0, ap, centres = yin(x, sr)
            st = steady(f0)
            for k in np.nonzero(st)[0][::2]:   # every other steady frame (20 ms apart)
                h = harmonics(x, sr, centres[k], f0[k])
                if h is None or len(h[0]) < 3:
                    continue
                fs, ls = h
                band = (fs >= 150) & (fs <= 4000)
                if band.sum() < 2:
                    continue
                level = 10 * np.log10(np.sum(10 ** (ls[band] / 10)))
                idx = np.searchsorted(EDGES, fs) - 1
                good = (idx >= 0) & (idx < len(CENTRES))
                np.add.at(sums[v], idx[good], ls[good] - level)
                np.add.at(counts[v], idx[good], 1)
                pitches.append(69 + 12 * np.log2(f0[k] / 440.0))
    env = {}
    for v in VOWELS:
        e = np.where(counts[v] >= 12, sums[v] / np.maximum(counts[v], 1), np.nan)
        env[v] = e
    return env, float(np.median(pitches)) if pitches else float("nan")


def vibrato(singer_dir: Path):
    """Median vibrato rate (Hz) and width (cents, half peak-to-peak) of the vibrato scales."""
    d = singer_dir / "scales" / "vibrato"
    rates, widths = [], []
    for f in sorted(d.glob("*.wav")) if d.is_dir() else []:
        x, sr = sf.read(f, dtype="float32")
        if x.ndim > 1:
            x = x.mean(axis=1)
        hop = 220
        f0, ap, _ = yin(x, sr, frame=2048, hop=hop)
        fr = sr / hop
        cents = np.where(f0 > 0, 1200 * np.log2(np.maximum(f0, 1.0) / 440.0), np.nan)
        # notes: voiced runs whose pitch stays within a semitone and a half of their median
        k = 0
        n = len(cents)
        while k < n:
            if np.isnan(cents[k]):
                k += 1
                continue
            j = k
            while j < n and not np.isnan(cents[j]) and abs(cents[j] - np.nanmedian(cents[k:j + 1])) < 150:
                j += 1
            seg = cents[k:j]
            if len(seg) / fr >= 0.6:
                trend = np.convolve(seg, np.ones(int(0.25 * fr)) / int(0.25 * fr), mode="same")
                core = slice(int(0.12 * fr), len(seg) - int(0.12 * fr))
                dev = (seg - trend)[core]
                if len(dev) >= int(0.4 * fr):
                    spec = np.abs(np.fft.rfft((dev - dev.mean()) * np.hanning(len(dev)), 4096))
                    freqs = np.fft.rfftfreq(4096, 1 / fr)
                    band = (freqs >= 3.5) & (freqs <= 8.5)
                    if band.any():
                        rate = freqs[band][np.argmax(spec[band])]
                        width = np.sqrt(2) * np.std(dev)
                        if 10 < width < 200:
                            rates.append(rate)
                            widths.append(width)
            k = j + 1
    if not rates:
        return float("nan"), float("nan")
    return float(np.median(rates)), float(np.median(widths))


# ----------------------------------------------------------------------------------------------
def interp_log(env: np.ndarray, freqs: np.ndarray):
    """The envelope (on CENTRES) at other frequencies, interpolated in log frequency."""
    x = np.log2(CENTRES)
    ok = ~np.isnan(env)
    if ok.sum() < 3:
        return np.full_like(freqs, np.nan, dtype=float)
    out = np.interp(np.log2(freqs), x[ok], env[ok], left=np.nan, right=np.nan)
    return out


def fit_scale(env: dict, mean: dict):
    """Formant scale: how much higher (>1) or lower this singer's formants sit than the mean's."""
    band = (CENTRES >= FIT_LO) & (CENTRES <= FIT_HI)
    best, best_err = 1.0, np.inf
    for a in np.arange(0.86, 1.1601, 0.0025):
        errs = []
        for v in VOWELS:
            m = interp_log(mean[v], CENTRES / a)
            dlt = env[v] - m
            dlt = dlt[band]
            dlt = dlt[~np.isnan(dlt)]
            if len(dlt) > 5:
                errs.append(np.mean((dlt - dlt.mean()) ** 2))
        if errs and np.mean(errs) < best_err:
            best, best_err = a, float(np.mean(errs))
    return best


def biquad_db(kind, f0, q, gain_db, freqs, sr=48000.0):
    """dB response of an RBJ biquad (the engine's Biquad uses the same formulas)."""
    A = 10 ** (gain_db / 40)
    w0 = 2 * np.pi * f0 / sr
    alpha = np.sin(w0) / (2 * q)
    cw = np.cos(w0)
    if kind == "peak":
        b = [1 + alpha * A, -2 * cw, 1 - alpha * A]
        a = [1 + alpha / A, -2 * cw, 1 - alpha / A]
    else:
        sq = 2 * np.sqrt(A) * alpha
        if kind == "lowshelf":
            b = [A * ((A + 1) - (A - 1) * cw + sq), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sq)]
            a = [(A + 1) + (A - 1) * cw + sq, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sq]
        else:
            b = [A * ((A + 1) + (A - 1) * cw + sq), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sq)]
            a = [(A + 1) - (A - 1) * cw + sq, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sq]
    z = np.exp(-1j * 2 * np.pi * freqs / sr)
    h = (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)
    return 20 * np.log10(np.abs(h))


def fit_eq(target: np.ndarray, lowest: float):
    """Gains of the six bands whose cascade best follows the target curve (dB on CENTRES), from
    `lowest` Hz up (below the sung pitches there are no harmonics to measure)."""
    ok = ~np.isnan(target) & (CENTRES >= lowest) & (CENTRES <= 9000)
    f = CENTRES[ok]
    t = target[ok]
    g = np.zeros(len(EQ_BANDS))
    for _ in range(40):
        resp = sum(biquad_db(k, fc, q, gk, f) for (k, fc, q), gk in zip(EQ_BANDS, g))
        err = t - resp
        # each band moves by the error around it
        for i, (k, fc, q) in enumerate(EQ_BANDS):
            unit = biquad_db(k, fc, q, 1.0, f)
            w = np.abs(unit) / max(1e-6, np.abs(unit).sum())
            g[i] += 0.6 * float(np.sum(w * err))
        g = np.clip(g, -np.array(EQ_LIMIT_DB), np.array(EQ_LIMIT_DB))
    resp = sum(biquad_db(k, fc, q, gk, f) for (k, fc, q), gk in zip(EQ_BANDS, g))
    return g, float(np.sqrt(np.mean((t - resp) ** 2)))


def smooth(curve: np.ndarray, width=2):
    out = curve.copy()
    for i in range(len(curve)):
        w = curve[max(0, i - width):i + width + 1]
        w = w[~np.isnan(w)]
        out[i] = w.mean() if len(w) else np.nan
    return out


# ----------------------------------------------------------------------------------------------
def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[2] / "core" / "src" / "VoicePrints.inc"
    singers = sorted([d for d in src.iterdir() if d.is_dir() and d.name != "male9"],   # male9: a countertenor
                     key=lambda d: (not d.name.startswith("female"), int("".join(c for c in d.name if c.isdigit()))))
    data = {}
    for d in singers:
        env, centre = envelopes(d)
        rate, width = vibrato(d)
        data[d.name] = dict(env=env, centre=centre, rate=rate, width=width, female=d.name.startswith("female"))
        print(f"{d.name}: centre {centre:.1f}, vibrato {rate:.2f} Hz {width:.0f} cents", flush=True)

    rows = []
    for female in (True, False):
        group = [k for k, v in data.items() if v["female"] == female]
        mean = {v: np.nanmean([data[k]["env"][v] for k in group], axis=0) for v in VOWELS}
        rates = [data[k]["rate"] for k in group if not np.isnan(data[k]["rate"])]
        widths = [data[k]["width"] for k in group if not np.isnan(data[k]["width"])]
        for k in group:
            s = data[k]
            scale = fit_scale(s["env"], mean)
            # what remains once the formants are moved: the singer's own colour
            resid = np.nanmean([s["env"][v] - interp_log(mean[v], CENTRES / scale) for v in VOWELS], axis=0)
            resid = smooth(resid)
            band = (CENTRES >= FIT_LO) & (CENTRES <= FIT_HI) & ~np.isnan(resid)
            resid = resid - np.mean(resid[band])
            gains, rms = fit_eq(resid, 300.0 if female else 170.0)
            rate = s["rate"] if not np.isnan(s["rate"]) else float(np.median(rates))
            width = s["width"] if not np.isnan(s["width"]) else float(np.median(widths))
            tag = ("f" if female else "m") + "".join(c for c in k if c.isdigit())
            rows.append((tag, female, s["centre"], scale, rate, width, gains, rms))
            print(f"  {tag}: formants x{scale:.3f}, EQ {' '.join(f'{g:+.1f}' for g in gains)} dB (fit {rms:.1f} dB), "
                  f"vibrato {rate:.2f} Hz / {width:.0f} c")

    lines = [
        "// Generated by tools/voiceprints/build_voiceprints.py from VocalSet (Wilkins, Seetharaman, Wahl,",
        "// Pardo 2018; CC BY 4.0, https://zenodo.org/records/1442513). Do not edit by hand.",
        "// { singer, female, centre (MIDI), formant scale, vibrato Hz, vibrato cents,",
        "//   EQ dB: low shelf 180 Hz, peaks 420 / 850 / 1700 / 2900 Hz, high shelf 5 kHz }",
    ]
    for tag, female, centre, scale, rate, width, gains, _ in rows:
        g = ", ".join(f"{x:.2f}f" for x in gains)
        lines.append(f'{{ "{tag}", {"true" if female else "false"}, {centre:.2f}f, {scale:.4f}f, {rate:.2f}f, {width:.1f}f, {{ {g} }} }},')
    out.write_text("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(rows)} singers)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
