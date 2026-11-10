"""A synthetic drummer with exact ground truth, for measuring the engine.

Every kit piece is synthesized (no samples), played with human timing (off the grid by a few ms, with swing, flams,
ghost notes, fills, open/pedal hats, ride bell, two crashes), so the true hit times and velocities are known to the
sample. `render()` returns the per-stem signals (as a perfect separator would give them) and the ground truth.
"""
from __future__ import annotations

import numpy as np
from scipy.signal import butter, sosfilt

SR = 44100


def _noise(rng, n):
    return rng.standard_normal(n).astype(np.float32)


def _bp(x, lo, hi):
    sos = butter(2, [lo, min(hi, 0.45 * SR)], btype="band", fs=SR, output="sos")
    return sosfilt(sos, x).astype(np.float32)


def _hp(x, lo):
    sos = butter(2, lo, btype="high", fs=SR, output="sos")
    return sosfilt(sos, x).astype(np.float32)


def _t(n):
    return np.arange(n, dtype=np.float32) / SR


def kick(rng, v):
    n = int(0.45 * SR)
    t = _t(n)
    f = 48 + 110 * np.exp(-t / 0.035)
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 0.16)
    click = _hp(_noise(rng, n), 2000) * np.exp(-t / 0.003) * 0.4
    return (body + click) * v


def snare(rng, v):
    n = int(0.40 * SR)
    t = _t(n)
    tone = (np.sin(2 * np.pi * 185 * t) + 0.6 * np.sin(2 * np.pi * 330 * t)) * np.exp(-t / 0.06)
    wires = _bp(_noise(rng, n), 1500, 9000) * np.exp(-t / 0.13) * 1.6
    return (0.7 * tone + wires) * v


def side_stick(rng, v):
    n = int(0.10 * SR)
    t = _t(n)
    tone = (np.sin(2 * np.pi * 520 * t) + 0.8 * np.sin(2 * np.pi * 1630 * t) + 0.4 * np.sin(2 * np.pi * 2750 * t)) * np.exp(-t / 0.018)
    return (tone + 0.05 * _bp(_noise(rng, n), 2000, 6000) * np.exp(-t / 0.01)) * v


def _metal(rng, n, base, decay, bright):
    t = _t(n)
    ratios = [1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637]
    sq = sum(np.sign(np.sin(2 * np.pi * base * r * t + rng.uniform(0, 6.28))) for r in ratios)
    x = _hp(sq.astype(np.float32) * 0.3 + _noise(rng, n), bright)
    return x * np.exp(-t / decay)


def hh(rng, v, kind):
    if kind == "open":
        return _metal(rng, int(0.9 * SR), 410, 0.35, 6000) * v * 0.8
    if kind == "pedal":
        n = int(0.08 * SR)
        t = _t(n)
        return _bp(_metal(rng, n, 410, 0.012, 300), 300, 2500) * v * 0.7 + 0 * t
    return _metal(rng, int(0.12 * SR), 410, 0.018, 7000) * v * 0.8


def tom(rng, v, f0):
    n = int(0.6 * SR)
    t = _t(n)
    f = f0 * (1 + 0.12 * np.exp(-t / 0.05))
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 0.22)
    att = _bp(_noise(rng, n), 800, 5000) * np.exp(-t / 0.006) * 0.4
    return (body + att) * v


def crash(rng, v, bright):
    return _metal(rng, int(2.2 * SR), 300 if not bright else 520, 0.8, 3000 if not bright else 5500) * v * 0.7


def ride(rng, v, bell=False):
    n = int(1.4 * SR)
    t = _t(n)
    parts = [(470, 1.0), (1130, 0.7), (2380, 0.5), (3510, 0.35)]
    ping = sum(a * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)) for f, a in parts) * np.exp(-t / (0.5 if bell else 0.35))
    wash = _hp(_noise(rng, n), 4000) * np.exp(-t / 0.4)
    if bell:
        return (1.6 * ping + 0.05 * wash).astype(np.float32) * v * 0.6
    return (0.25 * ping + 0.6 * wash).astype(np.float32) * v * 0.6


STEM_OF = {"kick": "kick", "snare": "snare", "side_stick": "snare", "hh_closed": "hh", "hh_open": "hh", "hh_pedal": "hh",
           "tom_himid": "toms", "tom_low": "toms", "floor_high": "toms", "crash1": "crash", "crash2": "crash",
           "ride": "ride", "ride_bell": "ride"}


def pattern(rng, bars=24, bpm=96.0, swing=0.03, jitter=0.006):
    """List of (time_s, piece, velocity 0..1). Human timing: per-note jitter, 16th swing, slow tempo drift."""
    beat = 60.0 / bpm
    ev = []
    drift = 0.0
    for bar in range(bars):
        drift += rng.normal(0, 0.003)
        t0 = 0.5 + bar * 4 * beat + drift
        section = (bar // 8) % 3  # 0: hats groove, 1: ride groove, 2: open hats + side stick
        fill = bar % 4 == 3
        def at(step16):
            sw = swing * beat if step16 % 2 == 1 else 0.0
            return t0 + step16 * beat / 4 + sw + rng.normal(0, jitter)
        if bar % 2 == 0:
            ev.append((at(0), "crash1" if bar % 4 == 0 else "crash2", rng.uniform(0.8, 1.0)))
        for s in range(16):
            if fill and s >= 8:
                continue
            if section == 0 and s % 2 == 0:
                ev.append((at(s), "hh_closed", rng.uniform(0.35, 0.6) if s % 4 else rng.uniform(0.6, 0.8)))
            if section == 1 and s % 2 == 0:
                ev.append((at(s), "ride_bell" if s == 4 or s == 12 else "ride", rng.uniform(0.5, 0.8)))
            if section == 2 and s % 4 == 2:
                ev.append((at(s), "hh_open", rng.uniform(0.6, 0.8)))
            if section == 2 and s % 4 == 0:
                ev.append((at(s), "hh_closed", rng.uniform(0.5, 0.7)))
            if section == 1 and s % 4 == 0:
                ev.append((at(s) + rng.normal(0, 0.003), "hh_pedal", rng.uniform(0.4, 0.6)))
        for s in (0, 6, 10) if bar % 2 else (0, 7, 8):
            if not (fill and s >= 8):
                ev.append((at(s), "kick", rng.uniform(0.75, 1.0)))
        for s in (4, 12):
            if fill and s >= 8:
                continue
            ev.append((at(s), "side_stick" if section == 2 else "snare", rng.uniform(0.8, 1.0)))
        if section == 0 and not fill:  # ghost notes
            for s in (7, 14):
                ev.append((at(s), "snare", rng.uniform(0.15, 0.25)))
        if bar % 8 == 4:  # flam
            ev.append((at(12) - 0.022, "snare", 0.3))
        if fill:
            toms = ["tom_himid", "tom_himid", "tom_low", "tom_low", "floor_high", "floor_high", "snare", "kick"]
            for i, s in enumerate(range(8, 16)):
                ev.append((at(s), toms[i], rng.uniform(0.7, 1.0)))
    ev.sort()
    return ev


def render(seed=1, bars=24, bpm=96.0, bleed_db=-26.0):
    """Returns (stems dict of mono arrays, mix mono, truth list of (time, piece, vel), duration)."""
    rng = np.random.default_rng(seed)
    ev = pattern(rng, bars, bpm)
    dur = ev[-1][0] + 3.0
    n = int(dur * SR)
    stems = {k: np.zeros(n, np.float32) for k in ("kick", "snare", "toms", "hh", "ride", "crash")}
    last_hh = None
    hh_times = [t for t, p, _ in ev if p.startswith("hh_")]
    for t, p, v in ev:
        if p == "kick":
            s = kick(rng, v)
        elif p == "snare":
            s = snare(rng, v)
        elif p == "side_stick":
            s = side_stick(rng, v)
        elif p.startswith("hh_"):
            s = hh(rng, v, p[3:])
            later = [u for u in hh_times if u > t + 1e-4]
            if later:  # the next hi-hat hit chokes this one
                m = int((later[0] - t) * SR)
                if m < len(s):
                    fade = int(0.004 * SR)
                    s = s.copy()
                    s[m:m + fade] *= np.linspace(1, 0, len(s[m:m + fade]))
                    s[m + fade:] = 0
        elif p.startswith("tom") or p.startswith("floor"):
            s = tom(rng, v, {"tom_himid": 196.0, "tom_low": 131.0, "floor_high": 87.0}[p])
        elif p.startswith("crash"):
            s = crash(rng, v, p == "crash2")
        else:
            s = ride(rng, v, p == "ride_bell")
        a = int(round(t * SR))
        b = min(n, a + len(s))
        stems[STEM_OF[p]][a:b] += s[: b - a]
    clean = {k: v.copy() for k, v in stems.items()}
    if bleed_db is not None:
        g = 10 ** (bleed_db / 20)
        for k in stems:
            for o in clean:
                if o != k:
                    stems[k] += g * _bp(clean[o], 100, 12000)
    mix = sum(clean.values())
    truth = [(round(t * SR) / SR, p, v) for t, p, v in ev]
    return stems, mix, truth, dur


def evaluate(truth, hits, tol=0.025):
    """Per piece: precision, recall, F, timing errors (ms) and velocity correlation. hits: [(time, piece, vel)]."""
    pieces = sorted({p for _, p, _ in truth} | {p for _, p, _ in hits})
    report = {}
    all_err = []
    tp_all = fp_all = fn_all = 0
    for p in pieces:
        T = sorted((t, v) for t, q, v in truth if q == p)
        H = sorted((t, v) for t, q, v in hits if q == p)
        used = [False] * len(H)
        errs, vel_pairs = [], []
        for t, v in T:
            best, bi = tol, -1
            for i, (h, hv) in enumerate(H):
                if not used[i] and abs(h - t) <= best:
                    best, bi = abs(h - t), i
            if bi >= 0:
                used[bi] = True
                errs.append((H[bi][0] - t) * 1000)
                vel_pairs.append((v, H[bi][1]))
        tp = len(errs)
        fp, fn = len(H) - tp, len(T) - tp
        tp_all += tp
        fp_all += fp
        fn_all += fn
        all_err += errs
        vc = float(np.corrcoef(*zip(*vel_pairs))[0, 1]) if len(vel_pairs) > 3 else float("nan")
        report[p] = dict(n=len(T), tp=tp, fp=fp, fn=fn,
                         f=2 * tp / max(1, 2 * tp + fp + fn),
                         mae_ms=float(np.mean(np.abs(errs))) if errs else float("nan"),
                         bias_ms=float(np.mean(errs)) if errs else float("nan"),
                         max_ms=float(np.max(np.abs(errs))) if errs else float("nan"), vel_r=vc)
    total = dict(n=tp_all + fn_all, tp=tp_all, fp=fp_all, fn=fn_all, f=2 * tp_all / max(1, 2 * tp_all + fp_all + fn_all),
                 mae_ms=float(np.mean(np.abs(all_err))) if all_err else float("nan"),
                 p95_ms=float(np.percentile(np.abs(all_err), 95)) if all_err else float("nan"))
    return report, total


def print_report(report, total):
    print(f"{'piece':12s} {'n':>4s} {'tp':>4s} {'fp':>4s} {'fn':>4s} {'F':>6s} {'mae ms':>7s} {'bias':>6s} {'max':>6s} {'vel r':>6s}")
    for p, r in report.items():
        print(f"{p:12s} {r['n']:4d} {r['tp']:4d} {r['fp']:4d} {r['fn']:4d} {r['f']:6.3f} {r['mae_ms']:7.2f} {r['bias_ms']:6.2f} {r['max_ms']:6.2f} {r['vel_r']:6.2f}")
    print(f"TOTAL n={total['n']} F={total['f']:.4f} mae={total['mae_ms']:.2f} ms p95={total['p95_ms']:.2f} ms fp={total['fp']} fn={total['fn']}")
