#!/usr/bin/env python3
"""Builds the room impulse responses for Sanctuary Choir's venues from measured, CC BY rooms.

Usage: python3 tools/rooms/build_rooms.py <cache dir> [<out dir>] [--wav]   (default out dir: rooms/)

--wav also writes each response as a float WAV (<slug>.wav, emphasis already taken out), the
form the command-line renderer reads with --rooms.

Each venue is a measured room heard by a spaced pair of microphones in the hall: two impulse
responses from the same source to two listening positions a few metres apart. Only the files
needed are read from the datasets on Zenodo (ranges of the zip archives, cached in <cache dir>).

Processing, per venue:
  1. Take the two responses (one capsule or the omni/W channel of each position).
  2. Remove the direct sound (the dry choir is mixed separately), keep everything from 2 ms
     after it, and start the response at the earlier of the two arrivals.
  3. Below the measurement's noise floor, continue each 1/6-octave band's own decay with
     noise (the noise floor would otherwise ring on as hiss), and extend the response far
     enough for the Decay knob (up to 250%).
  4. Lengthen or shorten the decay where the venue asks for it, band by band in proportion.
  5. High-pass at 40 Hz, scale to unit energy, and write <slug>.ogg stored with its decay
     flattened (see emphasisDbPerSecond in SOURCES.json; the loader takes it back out).

Writes SOURCES.json (one row per venue: dataset, record, licence, files, SHA-256 of the files
read, processing) and CREDITS.txt.
"""
import datetime
import hashlib
import io
import json
import os
import struct
import subprocess
import sys
import urllib.request
import zipfile

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt, stft, istft

SR = 48000
MAX_DECAY_KNOB = 2.5      # the Decay knob's top (250%)

DATASETS = {
    "cremona": {
        "record": "14901751", "zip": "ChurchIR.zip", "licence": "CC-BY-4.0",
        "title": "ChurchIR: A Dataset of Multichannel Church Impulse Responses for Spatial Audio Application",
        "authors": "Riccardo Giampiccolo, Sofia Parrinelli and Fabio Antonacci (2025)", "doi": "10.5281/zenodo.14901751",
        "place": "the church of Santi Marcellino e Pietro, Cremona",
    },
    "aachen": {
        "record": "23312556", "zip": "impulse_responses.zip", "licence": "CC-BY-4.0",
        "title": "Room acoustic measurement and decay analysis data of the St. Nicholas Chapel, Aachen Cathedral",
        "authors": "Martin Zerwas and Selin Kayku (2026)", "doi": "10.5281/zenodo.23312556",
        "place": "the St. Nicholas Chapel (Nikolauskapelle), Aachen Cathedral",
    },
    "detmold": {
        "record": "4116247", "zip": "DetmoldSRIR_v01.zip", "licence": "CC-BY-4.0",
        "title": "Open Database of Spatial Room Impulse Responses at Detmold University of Music",
        "authors": "Sebastia Vicenc Amengual Gari, Banu Sahin, Dusty Eddy and Malte Kob (2020)", "doi": "10.5281/zenodo.4116247",
        "place": "the Brahmssaal, Detmold University of Music",
    },
    "arvedi": {
        "record": "20098848", "zip": "arvedi_auditorium_dataset.zip", "licence": "CC-BY-4.0",
        "title": "The Sound of the Violin's Home: A Higher-Order Room Impulse Response Dataset of the Arvedi Auditorium",
        "authors": "Federico Miotello, Gioele Greco, Paolo Ostan, Francesca Del Gaudio, Luca Comanducci, Raffaele Malvermi, Mirco Pezzoli and Fabio Antonacci (2026)", "doi": "10.5281/zenodo.20098848",
        "place": "the Giovanni Arvedi Auditorium, Museo del Violino, Cremona",
    },
    "mv5": {
        "record": "10020866", "zip": "MV5.zip", "licence": "CC-BY-3.0",
        "title": "BBC Maida Vale Impulse Response Dataset",
        "authors": "Gavin Kearney, Helena Daffern, Patrick Cairns, Anthony Hunt, Ben Lee, Jacob Cooper, Panos Tsagkarakis, Tomasz Rudzki and Daniel Johnston (2023), with BBC R&D", "doi": "10.5281/zenodo.10020866",
        "place": "BBC Maida Vale Studio 5 (live room)",
    },
}

# Each venue: dataset, the two positions (zip member and channel), and how much to stretch
# the decay (1 = as measured).
VENUES = [
    dict(slug="grand-cathedral", venue="Grand Cathedral", dataset="cremona", stretch=1.5,
         left=("OMNI/SC_ML_OMNI_7.wav", 0), right=("OMNI/SC_MR_OMNI_7.wav", 0),
         where="source at the centre of the sanctuary, omnidirectional microphones 21 m down the nave, 5.4 m apart"),
    dict(slug="stone-abbey", venue="Stone Abbey", dataset="aachen", stretch=1.0,
         left=("Nikolauskapelle_Source2_BRIR/S2-Do_R5.wav", 0), right=("Nikolauskapelle_Source2_BRIR/S2-Do_R7.wav", 0),
         where="source in the apse, the W (omnidirectional) channel at receivers R5 and R7 in the main hall, 4.2 m apart"),
    dict(slug="wooden-chapel", venue="Wooden Chapel", dataset="detmold", stretch=1.35,
         left=("SetA_SingleSources/Data/Brahmssaal/OpenArray/C1S1R2.wav", 0),
         right=("SetA_SingleSources/Data/Brahmssaal/OpenArray/C1S1R3.wav", 0),
         where="source on the stage, the front capsule of the open array at audience positions R2 and R3"),
    dict(slug="concert-hall", venue="Concert Hall", dataset="arvedi", stretch=1.5,
         left=("arvedi_auditorium_dataset/rirs/rir-S0-A406.wav", 0), right=("arvedi_auditorium_dataset/rirs/rir-S0-A413.wav", 0),
         where="source S0 at the centre of the stage, capsule 1 of the arrays A406 and A413 in the fourth ring of seats, 4 m apart"),
    dict(slug="gospel-church", venue="Gospel Church", dataset="cremona", stretch=0.45,
         left=("OMNI/SC_ML_OMNI_3.wav", 0), right=("OMNI/SC_MR_OMNI_3.wav", 0),
         where="source at the centre of the sanctuary, omnidirectional microphones 10 m down the nave, 5.4 m apart"),
    dict(slug="studio-room", venue="Studio Room", dataset="mv5", stretch=1.0,
         left=("MV5/AS1/AS1/TOA/MV5_AS1_Eigen_R_PA-01_S_PA-3A_N_3OA.wav", 0),
         right=("MV5/AS1/AS1/TOA/MV5_AS1_Eigen_R_PA-02_S_PA-3A_N_3OA.wav", 0),
         where="source at PA-3A, the W (omnidirectional) channel of the Eigenmike at performer positions PA-01 and PA-02, 2.25 m apart"),
]


# ---------------------------------------------------------------------------------------------
# Reading single files out of the zip archives on Zenodo (HTTP ranges, no full download).
class HttpFile(io.RawIOBase):
    def __init__(self, url):
        self.url, self.pos = url, 0
        with urllib.request.urlopen(urllib.request.Request(url, method="HEAD")) as r:
            self.size = int(r.headers["Content-Length"])

    def seekable(self): return True
    def readable(self): return True
    def tell(self): return self.pos

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.size + off
        return self.pos

    def read(self, n=-1):
        if n < 0: n = self.size - self.pos
        if n == 0 or self.pos >= self.size: return b""
        end = min(self.pos + n, self.size) - 1
        req = urllib.request.Request(self.url, headers={"Range": f"bytes={self.pos}-{end}"})
        for attempt in range(5):
            try:
                with urllib.request.urlopen(req) as r:
                    data = r.read()
                break
            except OSError:
                if attempt == 4: raise
        self.pos += len(data)
        return data

    def readinto(self, b):
        d = self.read(len(b)); b[:len(d)] = d; return len(d)


_zips = {}


def fetch(cache, dataset, member):
    """The member's bytes, from the cache or read out of the dataset's zip on Zenodo."""
    ds = DATASETS[dataset]
    path = os.path.join(cache, dataset, member.replace("/", "__"))
    if not os.path.exists(path):
        if dataset not in _zips:
            url = f"https://zenodo.org/records/{ds['record']}/files/{ds['zip']}"
            _zips[dataset] = zipfile.ZipFile(io.BufferedReader(HttpFile(url), buffer_size=1 << 16))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path + ".part", "wb") as f:
            f.write(_zips[dataset].read(member))
        os.replace(path + ".part", path)
    with open(path, "rb") as f:
        return f.read()


def channel(data, ch):
    x, sr = sf.read(io.BytesIO(data), always_2d=True, dtype="float64")
    if sr != SR:
        raise ValueError(f"expected {SR} Hz, got {sr}")
    return x[:, ch]


# ---------------------------------------------------------------------------------------------
def direct_peak(x):
    """Index of the direct sound: the first peak within 3 ms of where the response first
    comes within 20 dB of its maximum."""
    a = np.abs(x)
    first = int(np.argmax(a > a.max() * 0.1))
    return first + int(np.argmax(a[first:first + int(0.003 * SR)]))


def remove_direct(x, peak, start):
    """Silence the direct sound, fading in from 2 to 3 ms after it; start the response at start."""
    y = x[start:].copy()
    p = peak - start
    a, b = p + int(0.002 * SR), p + int(0.003 * SR)
    y[:a] = 0.0
    ramp = 0.5 - 0.5 * np.cos(np.pi * np.arange(b - a) / (b - a))
    y[a:b] *= ramp
    return y


def band_edges(freqs):
    """1/6-octave bands from 40 Hz up, as lists of bin indices."""
    edges = 40.0 * 2.0 ** (np.arange(0, 60) / 6.0)
    edges = edges[edges < SR / 2]
    bands = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        idx = np.where((freqs >= lo) & (freqs < hi))[0]
        if len(idx):
            bands.append(idx)
    low = np.where(freqs < edges[0])[0]
    if len(low):
        bands[0] = np.concatenate([low, bands[0]])
    return bands


def denoise_extend(y, length, stretch, rng):
    """Continue each band's decay below the noise floor, out to `length` samples, and scale
    every band's decay time by `stretch`. Returns the response and its bands' decay times."""
    n, hop = 2048, 512
    f, t, X = stft(y, fs=SR, window="hann", nperseg=n, noverlap=n - hop, boundary="zeros", padded=True)
    P = np.abs(X) ** 2
    bands = band_edges(f)
    nf = X.shape[1]
    frames_out = max(nf, int(np.ceil(length / hop)) + 4)
    tail = slice(int(nf * 0.72), int(nf * 0.92))   # noise only by here (checked by the fits below)
    out = np.zeros((X.shape[0], frames_out), dtype=complex)
    out[:, :nf] = X
    frame_s = hop / SR
    t60s = []
    for idx in bands:
        e = P[idx].sum(axis=0) + 1e-30
        e_db = 10 * np.log10(np.convolve(e, np.ones(5) / 5, "same"))
        noise_db = 10 * np.log10(np.median(e[tail]))
        top = int(np.argmax(e_db[: max(8, nf // 3)]))
        hi = e_db[top] - 5.0
        # Fit the decay from 5 dB under the band's peak to 10 dB above its noise floor.
        start = top + int(np.argmax(e_db[top:] < hi))
        stop_c = np.where(e_db[start:] < noise_db + 10.0)[0]
        stop = start + (int(stop_c[0]) if len(stop_c) else nf - start - 1)
        if stop - start < 6:
            stop = min(nf - 1, start + 6)
        tt = np.arange(start, stop)
        k, a0 = np.polyfit(tt, e_db[start:stop], 1)   # dB per frame
        k = min(k, -0.05)
        t60 = -60.0 / (k / frame_s)
        t60s.append(t60)
        # Hand over from the measurement to the model where the model is 15 dB above the noise.
        cut = int(np.clip(np.ceil((noise_db + 15.0 - a0) / k), start + 2, nf - 8))
        fade = 12
        model_db = a0 + k * np.arange(frames_out)
        # The model's power spread over the band's bins as the measurement had it just before.
        shape = P[idx, max(0, cut - 12):cut].mean(axis=1) + 1e-30
        shape /= shape.sum()
        for j in range(cut - fade, frames_out):
            m = np.sqrt(shape * 10 ** (model_db[j] / 10)) * np.exp(2j * np.pi * rng.random(len(idx)))
            if j < cut:
                c = (j - (cut - fade) + 0.5) / fade
                out[idx, j] = out[idx, j] * np.cos(0.5 * np.pi * c) + m * np.sin(0.5 * np.pi * c)
            else:
                out[idx, j] = m
        # Stretch this band's decay in proportion (1/stretch of its rate).
        if abs(stretch - 1.0) > 1e-6:
            beta_db = (-k) * (1.0 - 1.0 / stretch)   # dB per frame given back
            out[idx, :] *= 10 ** ((beta_db * np.arange(frames_out)) / 20)[None, :]
    _, z = istft(out, fs=SR, window="hann", nperseg=n, noverlap=n - hop, boundary=True)
    return z[:length], np.array(t60s), bands, f


def t30(x, lo_hz=500.0, hi_hz=2000.0):
    sos = butter(4, [lo_hz, hi_hz], "bandpass", fs=SR, output="sos")
    e = sosfilt(sos, x) ** 2
    edc = np.cumsum(e[::-1])[::-1] + 1e-30
    L = 10 * np.log10(edc / edc[0])
    i5, i35 = np.argmax(L <= -5), np.argmax(L <= -35)
    return (i35 - i5) / SR * 2.0


def build(cache, out, keep_wav=False):
    os.makedirs(out, exist_ok=True)
    rows = []
    for v in VENUES:
        ds = DATASETS[v["dataset"]]
        raw = {side: fetch(cache, v["dataset"], v[side][0]) for side in ("left", "right")}
        xl, xr = channel(raw["left"], v["left"][1]), channel(raw["right"], v["right"][1])
        pl, pr = direct_peak(xl), direct_peak(xr)
        start = max(0, min(pl, pr) - int(0.0005 * SR))
        yl, yr = remove_direct(xl, pl, start), remove_direct(xr, pr, start)
        n = min(len(yl), len(yr))
        yl, yr = yl[:n], yr[:n]

        # Long enough for the Decay knob at the top: 2.5 times the venue's decay, to -66 dB.
        rng = np.random.default_rng(int(hashlib.sha256(v["slug"].encode()).hexdigest()[:8], 16))
        t_meas = t30(yl + yr)
        length = int(min(16.0, max(1.2, MAX_DECAY_KNOB * 1.1 * t_meas * v["stretch"])) * SR)
        zl, t60l, bands, freqs = denoise_extend(yl, length, v["stretch"], rng)
        zr, _, _, _ = denoise_extend(yr, length, v["stretch"], rng)

        hp = butter(2, 40.0, "highpass", fs=SR, output="sos")
        z = np.stack([sosfilt(hp, zl), sosfilt(hp, zr)], axis=1)
        fade = int(0.05 * SR)
        z[-fade:] *= (0.5 + 0.5 * np.cos(np.pi * np.arange(fade) / fade))[:, None]
        z /= np.sqrt((z ** 2).sum() / 2.0)
        rt = t30(z[:, 0] + z[:, 1])

        # Stored with its decay flattened (rising emphasisDbPerSecond dB a second), so the
        # encoder's noise falls with the decay once the loader takes the emphasis back out: the
        # quiet end of the tail, which the Decay knob turns up, stays clean.
        emphasis = round(0.8 * 60.0 / rt, 3)
        t = np.arange(len(z)) / SR
        stored = z * (10 ** (emphasis * t / 20))[:, None]
        stored *= 0.89 / np.abs(stored).max()
        wav = os.path.join(out, v["slug"] + ".stored.wav")
        ogg = os.path.join(out, v["slug"] + ".ogg")
        sf.write(wav, stored.astype(np.float32), SR, subtype="FLOAT")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", wav, "-c:a", "libvorbis", "-q:a", "8", ogg], check=True)
        if keep_wav:
            sf.write(os.path.join(out, v["slug"] + ".wav"), z.astype(np.float32), SR, subtype="FLOAT")
        bands_t60 = {int(round(freqs[idx].mean())): round(float(t), 2) for idx, t in zip(bands, t60l)}
        print(f"{v['venue']:16s} measured T30 {t_meas:4.2f} s -> {rt:4.2f} s, {len(z) / SR:5.1f} s long, "
              f"{os.path.getsize(ogg) / 1024:6.0f} KB ogg")
        rows.append({
            "venue": v["venue"], "file": v["slug"] + ".ogg", "emphasisDbPerSecond": emphasis,
            "dataset": ds["title"], "place": ds["place"], "authors": ds["authors"],
            "record": f"https://zenodo.org/records/{ds['record']}", "doi": ds["doi"], "archive": ds["zip"], "licence": ds["licence"],
            "members": [v["left"][0], v["right"][0]],
            "channels": [v["left"][1], v["right"][1]],
            "sha256": [hashlib.sha256(raw["left"]).hexdigest(), hashlib.sha256(raw["right"]).hexdigest()],
            "positions": v["where"],
            "processing": (f"direct sound removed; noise floor replaced by each 1/6-octave band's own decay; "
                           f"decay time x{v['stretch']}; high-pass 40 Hz; unit energy; extended to {len(z) / SR:.1f} s; "
                           f"stored rising {emphasis} dB/s (taken out on loading); Ogg Vorbis q8"),
            "t30_measured_s": round(float(t_meas), 2), "t30_s": round(float(rt), 2),
            "retrieved": datetime.date.today().isoformat(),
        })
        os.remove(wav)
    with open(os.path.join(out, "SOURCES.json"), "w") as f:
        json.dump(rows, f, indent=2)
        f.write("\n")
    write_credits(out, rows)


def write_credits(out, rows):
    licence_urls = {"CC-BY-4.0": "https://creativecommons.org/licenses/by/4.0/",
                    "CC-BY-3.0": "https://creativecommons.org/licenses/by/3.0/"}
    lines = ["Sanctuary Choir rooms - measured impulse responses",
             "=" * 50, "",
             "Each venue's reverb is the measured response of a real room. The measurements",
             "come from these datasets, used under their Creative Commons Attribution licences.",
             "The licensors do not endorse Sanctuary Choir. The material is provided as is,",
             "without warranties of any kind.", ""]
    for r in rows:
        lines += [f"{r['venue']}  ({r['file']})",
                  f"  {r['place']}",
                  f"  From: {r['dataset']}, by {r['authors']}",
                  f"  {r['record']}   DOI {r['doi']}",
                  f"  Licence: {r['licence'].replace('-', ' ', 2)}  {licence_urls[r['licence']]}",
                  f"  Used: {r['positions']}.",
                  f"  Changes: {r['processing']}.", ""]
    with open(os.path.join(out, "CREDITS.txt"), "w") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    here = os.path.dirname(os.path.abspath(__file__))
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    build(os.path.abspath(args[0]), os.path.abspath(args[1]) if len(args) > 1 else os.path.join(here, "..", "..", "rooms"),
          keep_wav="--wav" in sys.argv)
