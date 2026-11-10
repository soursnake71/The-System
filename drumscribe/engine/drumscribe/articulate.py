"""Turns stem hits into kit pieces and velocities.

- hi-hat: open (slow decay), closed, pedal (foot chick: quiet, short, dull)
- toms: grouped by pitch (1-6 drums), highest to lowest
- ride: bow or bell (bell is tonal and cuts through: lower spectral flatness)
- crash: two cymbals when the hits clearly fall in two brightness groups
- snare: snare or side stick (cross stick: tonal click, no snare wires)

The splits that can be wrong in a way you'd hear (bell, second crash, side stick) only happen when the hits form two
clearly separate groups; otherwise everything stays on the main note.
"""
from __future__ import annotations

import numpy as np

from .detect import Hit
from .kit import TOM_LAYOUT

ANALYSIS_LEN = 0.120


def _spectrum(x, sr, h: Hit, max_len=ANALYSIS_LEN):
    a = int(h.time * sr)
    b = min(len(x), a + int(min(max_len, max(0.01, h.end - h.time - 0.002)) * sr))
    seg = x[a:b]
    if len(seg) < 64:
        seg = np.pad(seg, (0, 64 - len(seg)))
    n = 1 << int(np.ceil(np.log2(max(len(seg), 4096))))
    win = np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg * win, n))
    f = np.fft.rfftfreq(n, 1 / sr)
    return f, mag


def _decay_time(x, sr, h: Hit, drop_db=15.0):
    """Seconds from the peak until the level has fallen drop_db; None if the next hit comes first."""
    a = int(h.time * sr)
    stop = min(len(x), a + int(min(0.6, max(0.01, h.end - h.time - 0.003)) * sr))
    n = int(0.003 * sr)
    if stop - a < 3 * n:
        return None, (stop - a) / sr
    frames = np.lib.stride_tricks.sliding_window_view(x[a:stop], n)[:: n // 2]
    lv = 20 * np.log10(np.sqrt((frames ** 2).mean(axis=1)) + 1e-9)
    pk = int(np.argmax(lv[: max(2, min(len(lv), int(0.03 * sr / (n // 2))))]))
    below = np.flatnonzero(lv[pk:] < lv[pk] - drop_db)
    span = (stop - a) / sr
    if len(below) == 0:
        return None, span
    return below[0] * (n // 2) / sr, span


def _centroid(f, mag, lo=0, hi=None):
    m = (f >= lo) & (f <= (hi or f[-1]))
    w = mag[m] ** 2
    return float((f[m] * w).sum() / max(w.sum(), 1e-12))


def _flatness(f, mag, lo, hi):
    m = (f >= lo) & (f <= hi)
    p = mag[m] ** 2 + 1e-12
    return float(np.exp(np.mean(np.log(p))) / np.mean(p))


def _pitch(f, mag, lo=45.0, hi=450.0):
    m = (f >= lo) & (f <= hi)
    if not m.any():
        return 0.0
    idx = np.flatnonzero(m)
    i = idx[int(np.argmax(mag[m]))]
    if 0 < i < len(mag) - 1:  # parabolic interpolation
        a, b, c = np.log(mag[i - 1] + 1e-12), np.log(mag[i] + 1e-12), np.log(mag[i + 1] + 1e-12)
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        return float((i + d) * (f[1] - f[0]))
    return float(f[i])


def two_groups(values, min_share=0.04, min_sep=2.5):
    """Robust 1-D split in two; returns (threshold, low_median, high_median) if the values clearly form two groups,
    else None. Medians and MADs (not means and SDs), so a stray hit can't make or break a split. min_sep is the gap
    between the group medians in pooled robust standard deviations."""
    v = np.asarray(values, float)
    if len(v) < 8:
        return None
    s = np.sort(v)
    n = len(s)
    kmin = int(max(2, np.ceil(min_share * n)))
    best = None
    for k in range(kmin, n - kmin + 1):
        a, b = s[:k], s[k:]
        cost = np.abs(a - np.median(a)).sum() + np.abs(b - np.median(b)).sum()
        if best is None or cost < best[0]:
            best = (cost, k)
    if best is None:
        return None
    k = best[1]
    a, b = s[:k], s[k:]
    ma, mb = np.median(a), np.median(b)
    mad = 1.4826 * np.sqrt((np.median(np.abs(a - ma)) ** 2 * len(a) + np.median(np.abs(b - mb)) ** 2 * len(b)) / n)
    mad = max(mad, 1e-3 * (abs(ma) + abs(mb)) + 1e-9)
    if (mb - ma) / mad < min_sep:
        return None
    return (s[k - 1] + s[k]) / 2, ma, mb


def _hh(hits, x, sr):
    for h in hits:
        f, mag = _spectrum(x, sr, h, 0.04)
        d, span = _decay_time(x, sr, h)
        h.feat.update(decay=d if d is not None else -span, centroid=_centroid(f, mag, 200))
    cents = sorted(h.feat["centroid"] for h in hits)
    bright = np.median(cents[len(cents) // 2:]) if cents else 0  # the typical stick hit (upper half)
    for h in hits:
        d = h.feat["decay"]
        # still ringing 15 dB down after 90 ms (or never got there before a hit at least 70 ms later): open
        if d > 0.09 or (d < 0 and -d >= 0.07):
            h.piece = "hh_open"
        elif h.feat["centroid"] < 0.4 * bright:  # dull "chick": the pedal
            h.piece = "hh_pedal"
        else:
            h.piece = "hh_closed"


def _toms(hits, x, sr):
    for h in hits:
        f, mag = _spectrum(x, sr, h, 0.15)
        h.feat["pitch"] = _pitch(f, mag)
    hs = [h for h in hits if h.feat["pitch"] > 0]
    if not hs:
        for h in hits:
            h.piece = "tom_himid"
        return
    semis = 12 * np.log2(np.array([h.feat["pitch"] for h in hs]) / 55.0)
    order = np.argsort(semis)
    # groups: split where sorted pitches jump by more than 1.8 semitones
    groups, cur = [], [order[0]]
    for a, b in zip(order[:-1], order[1:]):
        if semis[b] - semis[a] > 1.8:
            groups.append(cur)
            cur = []
        cur.append(b)
    groups.append(cur)
    # drop tiny outlier groups into their nearest neighbour; at most 6 drums
    def centre(g):
        return float(np.median(semis[g]))
    changed = True
    while changed and len(groups) > 1:
        changed = False
        small = [i for i, g in enumerate(groups) if len(g) < max(3, 0.03 * len(hs))]
        if len(groups) > 6 and not small:
            small = [int(np.argmin([len(g) for g in groups]))]
        if small:
            i = small[0]
            g = groups.pop(i)
            cands = [j for j in (i - 1, i) if 0 <= j < len(groups)]
            j = min(cands, key=lambda j: abs(centre(groups[j]) - centre(g)))
            groups[j] = groups[j] + g
            changed = True
    groups.sort(key=centre, reverse=True)  # highest first
    n = len(groups)
    if n == 1:
        p = 55.0 * 2 ** (centre(groups[0]) / 12)
        names = ["tom_himid" if p >= 140 else "tom_low" if p >= 100 else "floor_high"]
    else:
        names = TOM_LAYOUT[n]
    for name, g in zip(names, groups):
        for i in g:
            hs[i].piece = name
    for h in hits:
        if not h.piece:
            h.piece = "tom_himid"


def _ride(hits, x, sr):
    for h in hits:
        f, mag = _spectrum(x, sr, h, 0.08)
        h.feat["flat"] = np.log(_flatness(f, mag, 800, 12000))
    for h in hits:
        h.piece = "ride"
    g = two_groups([h.feat["flat"] for h in hits], min_share=0.03, min_sep=3.0)
    if g:
        th = g[0]
        for h in hits:
            if h.feat["flat"] < th:
                h.piece = "ride_bell"


def _crash(hits, x, sr):
    for h in hits:
        f, mag = _spectrum(x, sr, h, 0.10)
        h.feat["centroid"] = _centroid(f, mag, 300)
    for h in hits:
        h.piece = "crash1"
    g = two_groups([np.log(h.feat["centroid"]) for h in hits], min_share=0.05, min_sep=3.0)
    if g:
        for h in hits:
            if np.log(h.feat["centroid"]) > g[0]:
                h.piece = "crash2"


def _snare(hits, x, sr):
    for h in hits:
        f, mag = _spectrum(x, sr, h, 0.05)
        h.feat["flat"] = np.log(_flatness(f, mag, 200, 10000))
    for h in hits:
        h.piece = "snare"
    g = two_groups([h.feat["flat"] for h in hits], min_share=0.03, min_sep=3.5)
    if g:
        th, lo_mean, hi_mean = g
        # the cross-stick group has to be clearly tonal (a woody click), not just a less noisy snare hit
        if hi_mean - lo_mean > 2.0:
            for h in hits:
                if h.feat["flat"] < th:
                    h.piece = "side_stick"


def articulate(hits: dict[str, list[Hit]], stems: dict[str, np.ndarray], sr: int):
    for name, hl in hits.items():
        if not hl:
            continue
        x = stems[name]
        if name == "kick":
            for h in hl:
                h.piece = "kick"
        elif name == "snare":
            _snare(hl, x, sr)
        elif name == "hh":
            _hh(hl, x, sr)
        elif name == "toms":
            _toms(hl, x, sr)
        elif name == "ride":
            _ride(hl, x, sr)
        elif name == "crash":
            _crash(hl, x, sr)
        set_velocities(hl)
    return hits


def set_velocities(hl: list[Hit], range_db=36.0):
    """Velocity from the hit's level against the loudest hits of the same stem (log scale: 36 dB -> 1..127)."""
    if not hl:
        return
    ref = float(np.percentile([h.level_db for h in hl], 97))
    for h in hl:
        v = 1 + 126 * (h.level_db - (ref - range_db)) / range_db
        h.velocity = int(np.clip(round(v), 1, 127))
