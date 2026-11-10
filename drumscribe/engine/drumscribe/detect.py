"""Hit detection on the separated drum stems, with sample-accurate onset times.

Two passes per stem:
  1. find candidate hits with a log-compressed spectral flux and an adaptive threshold (coarse, ~6 ms frames);
  2. for each candidate, find the attack start on the waveform itself: the last point before the peak where the
     sample-level envelope is still below a fraction of its rise. That is where a drum sample's first sample would
     have to go to sound the same, so it is the time written to MIDI (no quantizing, sub-millisecond).

Then hits that are only bleed from another stem are dropped: a hit in stem X is bleed when, at the same moment,
another stem Y is much louder AND X's waveform is just a filtered copy of Y's (high correlation). A quiet ghost
note under a loud crash is kept, because it doesn't look like the crash.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from scipy.ndimage import maximum_filter1d, uniform_filter1d
from scipy.signal import butter, sosfilt, sosfiltfilt

N_FFT = 1024
HOP = 256

# per stem: minimum time between two hits (s), flux threshold (relative), how far under the stem's loudest hits a
# hit may be (dB), how much the level must jump at a hit (dB), band for the onset function; for toms the stick's
# attack also has to be there (tick_floor_db), so two toms ringing and beating together don't count as hits.
# tick: high-pass (Hz) for finding the attack of cymbals and toms: the stick's "tick" is bright, while the ring underneath
# (a ride bell, an open hat, the previous tom) is mostly lower, so a new hit stands out above it there.
STEM_PARAMS = {
    "kick": dict(min_ioi=0.030, delta=0.06, floor_db=40.0, min_rise=5.0, band=(25.0, 6000.0), tick=None),
    "snare": dict(min_ioi=0.016, delta=0.05, floor_db=42.0, min_rise=4.0, band=(80.0, 14000.0), tick=None),
    "toms": dict(min_ioi=0.025, delta=0.06, floor_db=36.0, min_rise=4.0, tick_floor_db=24.0, band=(40.0, 8000.0), tick=1500.0),
    "hh": dict(min_ioi=0.025, delta=0.05, floor_db=40.0, min_rise=1.5, band=(300.0, 18000.0), tick=3000.0),
    "ride": dict(min_ioi=0.035, delta=0.05, floor_db=36.0, min_rise=1.5, band=(200.0, 18000.0), tick=4000.0),
    "crash": dict(min_ioi=0.060, delta=0.06, floor_db=26.0, min_rise=1.5, band=(150.0, 18000.0), tick=3000.0),
}

ONSET_FRACTION = 0.15  # attack start = envelope crosses 15% of its rise (tuned in tests/test_accuracy.py)


@dataclass
class Hit:
    stem: str
    time: float  # seconds, attack start
    level_db: float  # short-term peak level (dBFS)
    rise_db: float  # how much the level jumps at the hit
    end: float = 0.0  # where the sound has decayed (s)
    feat: dict = field(default_factory=dict)
    piece: str = ""
    velocity: int = 0


def _db(x):
    return 20.0 * np.log10(np.maximum(x, 1e-9))


def _mag_frames(x: np.ndarray):
    """|STFT| in blocks (keeps memory small on long songs). Frame n is centred on sample n*HOP."""
    pad = np.pad(x, (N_FFT // 2, N_FFT // 2))
    n = 1 + (len(pad) - N_FFT) // HOP
    win = np.hanning(N_FFT).astype(np.float32)
    out = np.empty((n, N_FFT // 2 + 1), np.float32)
    blk = 4096
    for s in range(0, n, blk):
        e = min(n, s + blk)
        idx = np.arange(s, e)[:, None] * HOP + np.arange(N_FFT)[None, :]
        out[s:e] = np.abs(np.fft.rfft(pad[idx] * win, axis=1))
    return out


def onset_strength(x: np.ndarray) -> np.ndarray:
    mag = _mag_frames(x)
    ref = max(float(np.percentile(mag.max(axis=1), 99.5)), 1e-6)
    lm = np.log1p(1000.0 * mag / ref)
    flux = np.maximum(lm[1:] - lm[:-1], 0.0).sum(axis=1)
    flux = np.concatenate([[0.0], flux])
    top = float(np.percentile(flux, 99.5)) or 1.0
    return flux / top


def pick_peaks(odf: np.ndarray, sr: int, min_ioi: float, delta: float):
    fps = sr / HOP
    w_max = max(1, int(round(min_ioi * fps)))
    w_avg = int(round(0.10 * fps))
    local_max = maximum_filter1d(odf, size=2 * w_max + 1, mode="constant")
    local_avg = uniform_filter1d(odf, size=2 * w_avg + 1, mode="nearest")
    cand = np.flatnonzero((odf >= local_max) & (odf >= local_avg + delta) & (odf > delta))
    peaks, last = [], -10**9
    for c in cand:
        if c - last >= w_max:
            peaks.append(c)
            last = c
        elif odf[c] > odf[peaks[-1]]:
            peaks[-1] = c
            last = c
    return np.asarray(peaks, int)


def _envelope(x: np.ndarray, sr: int) -> np.ndarray:
    """|x| through a 0.6 ms trailing max: follows the attack sample by sample, without the zero crossings."""
    w = max(2, int(sr * 0.0006))
    return maximum_filter1d(np.abs(x), size=w, origin=(w - 1) // 2, mode="constant")


def refine(env: np.ndarray, sr: int, t_coarse: float, lo_t: float, hi_t: float):
    """Sample-accurate attack start near t_coarse. Returns (time, peak_index, peak/base power) or None.

    Works in power so a hit on top of something still ringing is measured from that ring, not from silence: the
    attack starts where the power has risen ONSET_FRACTION^2 of the way from the ring's level to the hit's peak.
    """
    lo = max(0, int(lo_t * sr))
    hi = min(len(env) - 1, int(hi_t * sr))
    c = int(t_coarse * sr)
    s0 = max(lo, c - int(0.012 * sr))
    s1 = min(hi, c + int(0.025 * sr))
    if s1 - s0 < 8:
        return None
    seg = env[s0:s1]
    m = seg.max()
    if m <= 0:
        return None
    # the first big maximum (within 3 dB of the window's top), so a later louder sound can't steal the hit
    p = int(np.flatnonzero(seg >= m * 0.7)[0])
    while p + 1 < len(seg) and seg[p + 1] >= seg[p]:
        p += 1
    p += s0
    fl0 = max(lo, s0 - int(0.010 * sr))
    pw = env[fl0:p + 1].astype(np.float64) ** 2
    sm = uniform_filter1d(pw, size=max(1, int(0.002 * sr)), mode="nearest")
    base = float(sm[: max(1, len(sm) - int(0.0005 * sr))].min())
    peak = float(pw[-1])
    th = base + ONSET_FRACTION ** 2 * (peak - base)
    i = len(pw) - 1
    while i > 0 and pw[i - 1] >= th:
        i -= 1
    if i > 0 and pw[i] != pw[i - 1]:
        frac = (th - pw[i - 1]) / (pw[i] - pw[i - 1])
        t = (fl0 + i - 1 + float(np.clip(frac, 0, 1))) / sr
    else:
        t = (fl0 + i) / sr
    return t, p, peak / max(base, 1e-20)


def _rms_db(x, a, b):
    a, b = max(0, a), min(len(x), b)
    if b - a < 4:
        return -180.0
    return float(_db(np.sqrt(np.mean(x[a:b] ** 2))))


def _bandpass(x, sr, band):
    lo, hi = band
    hi = min(hi, 0.45 * sr)
    sos = butter(4, [lo, hi], btype="band", fs=sr, output="sos")
    return sosfiltfilt(sos, x).astype(np.float32)


def detect_stem(name: str, x: np.ndarray, sr: int) -> list[Hit]:
    p = STEM_PARAMS[name]
    xb = _bandpass(x, sr, p["band"])
    odf = onset_strength(xb)
    peaks = pick_peaks(odf, sr, p["min_ioi"], p["delta"])
    # attack finding runs on the raw stem (or a causal high-pass of it): a zero-phase filter would smear the
    # attack backwards in time
    env = _envelope(x, sr)
    env_t = None
    if p["tick"]:
        xt = sosfilt(butter(2, p["tick"], btype="high", fs=sr, output="sos"), x).astype(np.float32)
        env_t = _envelope(xt, sr)
    coarse = peaks * HOP / sr
    hits: list[Hit] = []
    prev_t = -1.0
    for k, tc in enumerate(coarse):
        nxt = coarse[k + 1] if k + 1 < len(coarse) else len(x) / sr
        lo_t = max(prev_t + 0.004, tc - 0.020) if prev_t >= 0 else tc - 0.020
        hi_t = nxt - 0.004
        r = refine(env, sr, tc, lo_t, hi_t)
        sig = x
        if env_t is not None:  # cymbals, toms: use the band (full or bright) where the attack stands out more
            rt = refine(env_t, sr, tc, lo_t, hi_t)
            if rt is not None and (r is None or rt[2] > r[2]):
                r, sig = rt, xt
        if r is None:
            continue
        t, pk, _ = r
        if prev_t >= 0 and t - prev_t < p["min_ioi"] * 0.5:
            continue
        a = int(t * sr)
        n5 = int(0.005 * sr)
        # level: loudest 5 ms RMS in the first 30 ms (or until the next hit)
        end = min(int((nxt - 0.001) * sr), a + int(0.030 * sr))
        # rise: in the band the attack was found in; level (for the floor and velocity): the full stem
        win = range(a, max(a + 1, end - n5 + 1), max(1, n5 // 2))
        lev = max((_rms_db(x, s, s + n5) for s in win), default=-180.0)
        rs = xt if env_t is not None else x  # rise is judged on the stick band when there is one (no beating toms)
        lev_sig = lev if rs is x else max((_rms_db(rs, s, s + n5) for s in win), default=-180.0)
        pre = _rms_db(rs, max(int((prev_t + 0.002) * sr) if prev_t >= 0 else 0, a - int(0.025 * sr)), a - int(0.001 * sr))
        hits.append(Hit(stem=name, time=t, level_db=lev, rise_db=lev_sig - pre, end=nxt, feat={"tick_db": lev_sig}))
        prev_t = t
    if not hits:
        return hits
    ref = float(np.percentile([h.level_db for h in hits], 98))
    tref = float(np.percentile([h.feat["tick_db"] for h in hits], 98))
    keep = [h for h in hits if h.level_db >= ref - p["floor_db"] and h.rise_db >= p["min_rise"] and h.level_db > -75.0
            and h.feat["tick_db"] >= tref - p.get("tick_floor_db", 60.0)]
    for h in keep:
        h.feat["ref_db"] = ref
    return keep


_EDGES = 40.0 * 2 ** (np.arange(0, 26) / 3.0)  # 1/3-octave bands, 40 Hz - 13 kHz
_NFFT_P = 4096


def _band_power(seg: np.ndarray, sr: int) -> np.ndarray:
    if len(seg) < 32:
        return np.zeros(len(_EDGES) - 1)
    w = np.hanning(len(seg))
    p = np.abs(np.fft.rfft(seg * w, _NFFT_P)) ** 2 / max(1, len(seg))
    f = np.fft.rfftfreq(_NFFT_P, 1 / sr)
    idx = np.searchsorted(f, _EDGES)
    c = np.concatenate([[0.0], np.cumsum(p)])
    return np.array([c[max(idx[i] + 1, idx[i + 1])] - c[idx[i]] for i in range(len(_EDGES) - 1)])


def added(x: np.ndarray, sr: int, t: float, length: float = 0.02):
    """What the hit at t ADDS: per 1/3-octave band, the power in the 20 ms after t minus the 20 ms before (same
    length, so the same spectral leakage), so a cymbal or tom still ringing underneath doesn't count.

    Returns (shape: dB per band, peak = 0, floored at -40; added level dB; level after t dB)."""
    a = int(t * sr)
    n = int(length * sr)
    after = _band_power(x[a:a + n], sr)
    before = _band_power(x[a - n:a], sr) if a >= n else np.zeros_like(after)
    add = np.maximum(after - before, 0.0)
    d = 10 * np.log10(np.maximum(add, after * 1e-3) + 1e-24)
    tot_add = 10 * np.log10(add.sum() + 1e-24)
    tot_after = 10 * np.log10(after.sum() + 1e-24)
    return np.maximum(d - d.max(), -40.0), float(tot_add), float(tot_after)


def _unit(m):
    m = m - m.mean(axis=-1, keepdims=True)
    return m / (np.linalg.norm(m, axis=-1, keepdims=True) + 1e-12)


def _near(sorted_times, t, tol):
    i = np.searchsorted(sorted_times, t)
    return any(0 <= j < len(sorted_times) and abs(sorted_times[j] - t) <= tol for j in (i - 1, i))


def _db_sub(a_db, b_db):
    """Level left after taking b out of a (powers), dB."""
    return float(10 * np.log10(max(10 ** (a_db / 10) - 10 ** (b_db / 10), 1e-20)))


ALWAYS_DB = {"snare": 32.0}  # ghost notes can sit far under a crash; every other stem: 24 dB


def remove_bleed(hits: dict[str, list[Hit]], stems: dict[str, np.ndarray], sr: int, louder_db=10.0):
    """Drop hits that are another stem's sound leaking through the separation.

    A hit in stem X is bleed when, at the same moment, another stem Y has an attack at least `louder_db` louder than
    what the hit adds to X, AND the sound the hit adds sounds more like Y (Y's own attack right there, or Y's usual
    sounds) than like any of X's usual sounds. X's and Y's "usual sounds" are the spectra of their loud hits, so a
    stem with several sounds (closed hats and pedal hats) keeps all of them. Quiet hits that sound like their own
    stem (ghost notes, a pedal hat under a ride, hats on every backbeat) stay. A hit 24 dB (snare: 32 dB) under a
    simultaneous attack elsewhere is dropped whatever it sounds like (ALWAYS_DB).
    """
    rng = np.random.default_rng(0)
    nb = len(_EDGES) - 1
    prof, add_db, ex = {}, {}, {}
    tl = {}  # per stem: sorted hit times and their levels
    for name, hl in hits.items():
        tl[name] = (np.array([h.time for h in hl]), np.array([h.level_db for h in hl]))
    for name, hl in hits.items():
        x = stems[name]
        rows = [added(x, sr, h.time) for h in hl]
        P = np.array([r[0] for r in rows]) if rows else np.zeros((0, nb))
        prof[name] = _unit(P) if len(P) else P
        add_db[name] = [r[1] for r in rows]
        if hl:
            ref = float(np.max([h.level_db for h in hl]))
            strong = [i for i, h in enumerate(hl) if h.level_db >= ref - 15.0]

            def masked(h):  # something much louder in another stem at the same moment
                for other, (ts, ls) in tl.items():
                    if other == name or not len(ts):
                        continue
                    j0, j1 = np.searchsorted(ts, [h.time - 0.010, h.time + 0.010])
                    if j1 > j0 and ls[j0:j1].max() >= h.level_db + 6.0:
                        return True
                return False
            idx = [i for i in strong if not masked(hl[i])]
            if len(idx) < 3:
                idx = strong
            if len(idx) > 300:
                idx = list(rng.choice(idx, 300, replace=False))
            ex[name] = (np.asarray(idx, int), prof[name][idx])

    def own_sim(name, i):
        idx, E = ex[name]
        s = E @ prof[name][i]
        s = s[idx != i]
        return float(np.mean(np.sort(s)[-3:])) if len(s) else 0.0

    def other_sim(other, v):
        if other not in ex:
            return -1.0
        s = ex[other][1] @ v
        return float(np.mean(np.sort(s)[-3:])) if len(s) else -1.0

    cache = {}

    def other_at(other, t):
        k = (other, t)
        if k not in cache:
            cache[k] = added(stems[other], sr, t)
        return cache[k]

    out = {}
    for name, hl in hits.items():
        kept = []
        for i, h in enumerate(hl):
            ax = add_db[name][i]
            v = prof[name][i]
            sx = None
            bleed = False
            for other in stems:
                if other == name:
                    continue
                py, ay, ly = other_at(other, h.time)
                # Y attacks here: most of its power right after t is new, or it has a hit within 6 ms
                if ay < ly - 3.0 and not _near(tl[other][0] if other in tl else np.array([]), h.time, 0.006):
                    continue
                d = ax - ly
                if d > -louder_db:
                    continue
                if d < -ALWAYS_DB.get(name, 24.0):
                    bleed = True
                    break
                if sx is None:
                    sx = own_sim(name, i)
                vy = _unit(py[None])[0]
                if max(float(vy @ v), other_sim(other, v)) > sx:
                    bleed = True
                    break
            if not bleed:
                kept.append(h)
        out[name] = kept
    return out


def detect_all(stems: dict[str, np.ndarray], sr: int, progress=None) -> dict[str, list[Hit]]:
    hits = {}
    for i, (name, x) in enumerate(stems.items()):
        hits[name] = detect_stem(name, x.astype(np.float32), sr)
        if progress:
            progress((i + 1) / len(stems))
    return remove_bleed(hits, stems, sr)
