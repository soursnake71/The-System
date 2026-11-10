"""Tempo from the drum hits (only sets the MIDI grid; note times never depend on it)."""
from __future__ import annotations

import numpy as np


def estimate_tempo(hits, duration, lo=70.0, hi=180.0) -> float:
    """Autocorrelation of the hit train (kick, snare and hats weighted by velocity), refined to 0.01 BPM."""
    if len(hits) < 8:
        return 120.0
    res = 0.005
    n = int(duration / res) + 2
    train = np.zeros(n)
    for h in hits:
        w = 1.0 + (0.5 if h.piece in ("kick", "snare") else 0.0)
        i = int(h.time / res)
        if 0 <= i < n:
            train[i] += w * (h.velocity / 127.0)
    # smear a little so near misses still line up
    k = np.exp(-0.5 * (np.arange(-4, 5) / 1.5) ** 2)
    train = np.convolve(train, k, mode="same")
    train -= train.mean()
    f = np.fft.rfft(train, 2 * n)
    ac = np.fft.irfft(np.abs(f) ** 2)[:n]
    bpms = np.arange(lo, hi, 0.01)
    lags = 60.0 / bpms / res
    def score(b):
        s = 0.0
        for mult, w in ((1, 1.0), (2, 0.5), (4, 0.25)):  # beat, half bar, bar
            lag = 60.0 / b / res * mult
            if lag < n - 1:
                s += w * np.interp(lag, np.arange(n), ac)
        return s
    coarse = np.array([score(b) for b in bpms[::50]])
    b0 = bpms[::50][int(np.argmax(coarse))]
    fine = bpms[(bpms > b0 - 1) & (bpms < b0 + 1)]
    best = fine[int(np.argmax([score(b) for b in fine]))]
    return float(round(best, 2))
