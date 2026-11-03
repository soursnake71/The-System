// Omega CMI sound library.
//
// The CMI's fame rests on its disk library (the orchestra stab, the breathy choir, breaking glass, the pizzicato
// strings, the drum sounds of Page R...). Those recordings belong to their owners, so none is included here: every
// sound below is built from scratch at start-up by physical and spectral models of the same kind of source (strings,
// voices, bars, membranes, glass), recorded the way the CMI recorded: one channel, 8 bits, at the sample rates the
// CMI used, at most 16 KB, loops on whole pages. Then they go through the channel card model like any CMI sound.
// The "waveform" sounds are Page 4 style: 32 harmonics per segment, 128 segments of one cycle each.
#pragma once
#include "Engine.h"
#include <cctype>
#include <functional>
#include <memory>
#include <mutex>

namespace cmi {
namespace gen {

using Vec = std::vector<float>;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double uni() { return next() * (1.0 / 4294967296.0); }
    double bip() { return uni() * 2 - 1; }
};

struct BQ {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    static BQ make(double b0, double b1, double b2, double a0, double a1, double a2) { BQ q; q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0; return q; }
    static double w(double fs, double f) { return 2 * PI * std::min(f, fs * 0.47) / fs; }
    static BQ lp(double fs, double f, double q = 0.707) { const double w0 = w(fs, f), al = std::sin(w0) / (2 * q), c = std::cos(w0); return make((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    static BQ hp(double fs, double f, double q = 0.707) { const double w0 = w(fs, f), al = std::sin(w0) / (2 * q), c = std::cos(w0); return make((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    static BQ bp(double fs, double f, double q) { const double w0 = w(fs, f), al = std::sin(w0) / (2 * q), c = std::cos(w0); return make(al, 0, -al, 1 + al, -2 * c, 1 - al); }
    static BQ peak(double fs, double f, double q, double db) { const double A = std::pow(10, db / 40), w0 = w(fs, f), al = std::sin(w0) / (2 * q), c = std::cos(w0); return make(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
};

inline double blep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}
struct Saw { double ph = 0; double run(double f, double fs) { const double dt = std::min(f / fs, 0.45); ph += dt; if (ph >= 1) ph -= 1; return 2 * ph - 1 - blep(ph, dt); } };
struct Pulse { double ph = 0; double run(double f, double fs, double pw) { const double dt = std::min(f / fs, 0.45); ph += dt; if (ph >= 1) ph -= 1;
    double v = ph < pw ? 1 : -1; v += blep(ph, dt); double t2 = ph - pw; if (t2 < 0) t2 += 1; v -= blep(t2, dt); return v; } };

inline Vec zeros(double fs, double sec) { return Vec((size_t) std::max(1.0, std::round(fs * sec)), 0.f); }
inline double cents(double c) { return std::pow(2.0, c / 1200.0); }
inline double mtof(double m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }
inline void add(Vec& a, const Vec& b, double g = 1, size_t off = 0) { for (size_t i = 0; i + off < a.size() && i < b.size(); ++i) a[i + off] += (float) (b[i] * g); }
inline void filt(Vec& x, BQ q) { for (auto& v : x) v = (float) q.run(v); }

// a sum of decaying sinusoids (bars, bells, membranes, glass)
struct Mode_ { double ratio, amp, decay, phase = 0; };
inline Vec modal(double fs, double sec, double f0, const std::vector<Mode_>& m, double pitchDrop = 0, double dropTime = 0.05) {
    Vec y = zeros(fs, sec);
    for (const auto& md : m) {
        double ph = md.phase; const double fmax = fs * 0.45;
        for (size_t i = 0; i < y.size(); ++i) {
            const double t = i / fs;
            const double f = f0 * md.ratio * (1 + pitchDrop * std::exp(-t / dropTime));
            if (f >= fmax) break;
            ph += 2 * PI * f / fs;
            y[i] += (float) (md.amp * std::exp(-t / md.decay) * std::sin(ph));
        }
    }
    return y;
}

// plucked string (Karplus-Strong with tuned loss), pitch can move: f(t) in Hz
inline Vec pluck(double fs, double sec, const std::function<double(double)>& f, double t60, double bright, double pickPos, uint32_t seed, double excite = 1.0,
                 double jawari = 0) {
    Vec y = zeros(fs, sec);
    const double fmin = 25;
    const int cap = (int) (fs / fmin) + 8;
    std::vector<double> d((size_t) cap, 0.0);
    Rng r(seed);
    // excitation: filtered noise, combed at the pluck position
    const double f0 = f(0);
    const int N0 = std::max(2, (int) (fs / f0));
    std::vector<double> ex((size_t) N0);
    double lp = 0;
    for (int i = 0; i < N0; ++i) { lp += (r.bip() - lp) * (0.15 + 0.85 * bright); ex[(size_t) i] = lp * excite; }
    const int pp = std::max(1, (int) (pickPos * N0));
    for (int i = N0 - 1; i >= pp; --i) ex[(size_t) i] -= ex[(size_t) (i - pp)];
    for (int i = 0; i < N0; ++i) d[(size_t) i] = ex[(size_t) i];
    int w = N0 % cap; double last = 0;
    for (size_t i = 0; i < y.size(); ++i) {
        const double t = i / fs, fr = std::max(fmin, f(t));
        double delay = fs / fr - 0.5;
        // the loss filter: one-pole average, per-period gain for the t60
        const double g = std::pow(10.0, -3.0 / (fr * t60));
        if (jawari > 0) delay *= 1.0 - jawari * std::min(1.0, std::fabs(last) * 4);
        const double rp = w - delay;
        double ri = std::floor(rp); const double fr2 = rp - ri;
        const int i0 = ((int) ri % cap + cap) % cap, i1 = (i0 + 1) % cap;
        const double v = d[(size_t) i0] * (1 - fr2) + d[(size_t) i1] * fr2;
        const double s = 0.5 * (1 - bright) + 0.02;
        const double nv = g * ((1 - s) * v + s * last);
        last = nv;
        d[(size_t) w] = nv;
        w = (w + 1) % cap;
        y[i] = (float) nv;
    }
    return y;
}

// a few singers or players: band-limited sawtooth sources with their own detune, vibrato and jitter
inline Vec ensemble(double fs, double sec, double f0, int n, double detuneCents, double vibDepthSemis, double vibRate, uint32_t seed,
                    double attack = 0.02, double scoopCents = 0, double scoopTime = 0.05, double tilt = 0) {
    Vec y = zeros(fs, sec); Rng r(seed);
    for (int k = 0; k < n; ++k) {
        Saw o; o.ph = r.uni();
        const double det = cents(detuneCents * (n == 1 ? 0 : r.bip()));
        const double vr = vibRate * (0.85 + 0.3 * r.uni()), vp = r.uni();
        const double start = r.uni() * 0.03 * (n > 1);
        double jit = 0, tl = 0;
        for (size_t i = 0; i < y.size(); ++i) {
            const double t = i / fs;
            jit += (r.bip() * 0.002 - jit) * 0.01;
            const double vib = vibDepthSemis * std::sin(2 * PI * (vr * t + vp)) * std::min(1.0, t / 0.35);
            const double sc = scoopCents * std::exp(-t / scoopTime);
            const double f = f0 * det * std::pow(2.0, (vib + sc / 100.0) / 12.0) * (1 + jit);
            double v = o.run(f, fs);
            if (tilt > 0) { tl += (v - tl) * tilt; v = tl; }
            const double a = t < start ? 0 : std::min(1.0, (t - start) / std::max(attack, 1e-3));
            y[i] += (float) (v * a / std::sqrt((double) n));
        }
    }
    return y;
}

struct Formant { double f, q, gainDb; };
inline Vec formants(const Vec& x, double fs, const std::vector<Formant>& fm) {
    Vec y(x.size(), 0.f);
    for (const auto& f : fm) {
        BQ q = BQ::bp(fs, f.f, f.q); const double g = std::pow(10.0, f.gainDb / 20.0);
        for (size_t i = 0; i < x.size(); ++i) y[i] += (float) (q.run(x[i]) * g);
    }
    return y;
}

inline Vec noise(double fs, double sec, uint32_t seed) { Vec y = zeros(fs, sec); Rng r(seed); for (auto& v : y) v = (float) r.bip(); return y; }

inline void envelope(Vec& x, double fs, const std::function<double(double)>& e) { for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (x[i] * e(i / fs)); }

// small Schroeder room, mixed in
inline void room(Vec& x, double fs, double mix, double rt) {
    const double cd[4] = { 0.0297, 0.0371, 0.0411, 0.0437 }, ad[2] = { 0.005, 0.0017 };
    std::vector<std::vector<double>> cb(4), ab(2); std::vector<size_t> cp(4, 0), ap(2, 0); std::vector<double> g(4), lpS(4, 0);
    for (int i = 0; i < 4; ++i) { cb[(size_t) i].assign((size_t) (cd[i] * fs) + 1, 0); g[(size_t) i] = std::pow(10.0, -3 * cd[i] / rt); }
    for (int i = 0; i < 2; ++i) ab[(size_t) i].assign((size_t) (ad[i] * fs) + 1, 0);
    for (auto& v : x) {
        double s = 0;
        for (int i = 0; i < 4; ++i) {
            auto& b = cb[(size_t) i]; size_t& p = cp[(size_t) i];
            const double o = b[p]; lpS[(size_t) i] += (o - lpS[(size_t) i]) * 0.6;
            b[p] = v + lpS[(size_t) i] * g[(size_t) i]; p = (p + 1) % b.size(); s += o;
        }
        s *= 0.25;
        for (int i = 0; i < 2; ++i) {
            auto& b = ab[(size_t) i]; size_t& p = ap[(size_t) i];
            const double o = b[p]; const double in = s + o * 0.5; b[p] = in; p = (p + 1) % b.size(); s = o - in * 0.5;
        }
        v = (float) (v + s * mix);
    }
}

inline void drive(Vec& x, double g) { for (auto& v : x) v = (float) std::tanh(v * g); }
inline double peakOf(const Vec& x) { double p = 0; for (float v : x) p = std::max(p, (double) std::fabs(v)); return p; }

// ---- into CMI memory ----
// crossfade: smooth the loop seam (the synthesised voices); off for loops a file brings with it, which are kept exactly
inline Sound sampled(const char* name, const char* title, const char* cat, double fs, int root, Vec x, double loopFrom, double loopTo, Controls c,
                     bool crossfade = true, bool normalize = true) {
    Sound s; s.name = name; s.title = title; s.category = cat; s.mode = Mode::Sample; s.rate = fs; s.root = root; s.ctl = c;
    if (x.size() > (size_t) RAM_SIZE) x.resize((size_t) RAM_SIZE);
    // remove DC
    double m = 0; for (float v : x) m += v; m /= std::max<size_t>(1, x.size());
    for (auto& v : x) v -= (float) m;
    const size_t pagesN = (x.size() + PAGE - 1) / PAGE;
    x.resize(pagesN * PAGE, 0.f);
    if (loopFrom >= 0) {
        int ls = (int) std::lround(loopFrom * fs / PAGE), le = (int) std::floor(loopTo * fs / PAGE) - 1;
        le = std::min(le, (int) pagesN - 1); ls = clampi(ls, crossfade ? 1 : 0, std::max(crossfade ? 1 : 0, le));
        if (le > ls || (!crossfade && le >= ls)) {
            s.loopStart = ls; s.loopEnd = le;
            const int span = (le - ls + 1) * PAGE;
            const int C = crossfade ? std::min({ 2048, ls * PAGE, span / 2 }) : 0;
            const int endI = (le + 1) * PAGE, preI = ls * PAGE;
            for (int i = 0; i < C; ++i) {
                const double w = (double) (i + 1) / C;
                const size_t a = (size_t) (endI - C + i), b = (size_t) (preI - C + i);
                x[a] = (float) (x[a] * std::cos(w * PI / 2) + x[b] * std::sin(w * PI / 2));
            }
        }
    } else {
        // one-shot: fade the last page so the sound ends at silence
        const size_t f = std::min<size_t>(x.size(), 256);
        for (size_t i = 0; i < f; ++i) x[x.size() - 1 - i] *= (float) (i / (double) f);
    }
    const double p = normalize ? std::max(1e-9, peakOf(x)) : 1.0;
    s.ram.resize(x.size());
    for (size_t i = 0; i < x.size(); ++i) s.ram[i] = (uint8_t) clampi((int) std::lround(x[i] / p * 126.0) + 128, 1, 255);
    return s;
}

// Page 4 style: harmonics(page, amps[32], phases[32]) for each of 128 segments
inline Sound waveform(const char* name, const char* title, const char* cat, const std::function<void(int, double*, double*)>& h,
                      int loopS, int loopE, Controls c) {
    Sound s; s.name = name; s.title = title; s.category = cat; s.mode = Mode::Waveform; s.rate = 128.0 * 440.0; s.root = 69; s.ctl = c;
    s.loopStart = loopS; s.loopEnd = loopE;
    std::vector<double> buf((size_t) RAM_SIZE, 0.0);
    for (int p = 0; p < PAGES; ++p) {
        double a[32] {}, ph[32] {};
        h(p, a, ph);
        for (int i = 0; i < PAGE; ++i) {
            double v = 0;
            for (int k = 0; k < 32; ++k) if (a[k] != 0) v += a[k] * std::sin(2 * PI * (k + 1) * i / PAGE + ph[k]);
            buf[(size_t) (p * PAGE + i)] = v;
        }
    }
    double pk = 1e-9; for (double v : buf) pk = std::max(pk, std::fabs(v));
    s.ram.resize((size_t) RAM_SIZE);
    for (size_t i = 0; i < buf.size(); ++i) s.ram[i] = (uint8_t) clampi((int) std::lround(buf[i] / pk * 126.0) + 128, 1, 255);
    return s;
}

inline Controls ctl(std::initializer_list<std::pair<const char*, float>> kv) {
    Controls c;
    for (const auto& p : kv) {
        const std::string k = p.first; const float v = p.second;
        if (k == "level") c.level = v; else if (k == "filter") c.filter = v; else if (k == "attack") c.attack = v;
        else if (k == "damp") c.damp = v; else if (k == "sustain") c.sustain = v; else if (k == "vib") c.vibDepth = v;
        else if (k == "vibRate") c.vibRate = v; else if (k == "vibDelay") c.vibDelay = v; else if (k == "porta") c.porta = v;
        else if (k == "velVol") c.velVol = v; else if (k == "velFilt") c.velFilt = v; else if (k == "loop") c.loop = v;
        else if (k == "seg") c.segTime = v; else if (k == "transpose") c.transpose = v;
    }
    return c;
}

// ======================================================================================================
// The sounds
// ======================================================================================================

// the 1980s orchestra stab: brass and strings on one chord over octaves, timpani, cymbal, a hall
inline Sound orchHit() {
    const double fs = 22050, sec = 0.743;
    Vec y = zeros(fs, sec);
    const int brass[] = { 48, 55, 60, 64, 67 }, str[] = { 60, 64, 67, 72, 76, 79, 84 };
    uint32_t seed = 11;
    for (int m : brass) {
        Vec b = ensemble(fs, sec, mtof(m), 3, 7, 0, 5, seed++, 0.006, -25, 0.03);
        BQ lp = BQ::lp(fs, 1, 0.9);
        for (size_t i = 0; i < b.size(); ++i) { const double t = i / fs; if (i % 32 == 0) lp = [&] { BQ q = BQ::lp(fs, 900 + 6000 * std::exp(-t / 0.09), 1.1); q.z1 = lp.z1; q.z2 = lp.z2; return q; }(); b[i] = (float) lp.run(b[i]); }
        add(y, b, 0.55);
    }
    for (int m : str) {
        Vec s = ensemble(fs, sec, mtof(m), 4, 12, 0.08, 6, seed++, 0.004);
        filt(s, BQ::lp(fs, 6500, 0.7)); filt(s, BQ::peak(fs, 2600, 1.2, 5));
        add(y, s, 0.42);
    }
    add(y, modal(fs, sec, mtof(36), { { 1, 1, 0.5 }, { 1.5, 0.5, 0.35 }, { 1.98, 0.3, 0.25 }, { 2.44, 0.2, 0.2 } }, 0.04, 0.05), 1.4);
    Vec cym = noise(fs, sec, 99); filt(cym, BQ::hp(fs, 4500, 0.7)); envelope(cym, fs, [](double t) { return 0.35 * std::exp(-t / 0.12); }); add(y, cym);
    Vec snap = noise(fs, sec, 98); filt(snap, BQ::bp(fs, 1800, 0.8)); envelope(snap, fs, [](double t) { return 0.8 * std::exp(-t / 0.018); }); add(y, snap);
    envelope(y, fs, [](double t) { return std::min(1.0, t / 0.003) * (0.75 * std::exp(-t / 0.13) + 0.25 * std::exp(-t / 0.45)); });
    drive(y, 1.6);
    room(y, fs, 0.32, 1.6);
    envelope(y, fs, [sec](double t) { return t > sec - 0.08 ? (sec - t) / 0.08 : 1.0; });
    return sampled("ORCHHIT", "Orchestra stab: the 1980s hit (in the style of the library's ORCH5)", "Famous", fs, 60, y, -1, 0,
                   ctl({ { "damp", 9 }, { "velVol", 0.4f }, { "velFilt", 0.2f } }));
}

inline Vec vowelChoir(double fs, double sec, double f0, int n, const std::vector<Formant>& fm, uint32_t seed, double attack, double breath) {
    Vec src = ensemble(fs, sec, f0, n, 14, 0.22, 5.3, seed, attack, -15, 0.12, 0.35);
    Vec nz = noise(fs, sec, seed + 7); envelope(nz, fs, [attack](double t) { return std::min(1.0, t / attack); });
    add(src, nz, breath);
    Vec y = formants(src, fs, fm);
    filt(y, BQ::hp(fs, 90, 0.7));
    room(y, fs, 0.22, 1.4);
    return y;
}

inline Sound choirAah() {
    const double fs = 16000, sec = 1.024;
    Vec y = vowelChoir(fs, sec, mtof(60), 7, { { 800, 9, 0 }, { 1150, 9, -5 }, { 2900, 14, -18 }, { 3500, 16, -24 }, { 300, 3, -12 } }, 21, 0.22, 0.12);
    return sampled("CHOIRAAH", "Breathy 'aah' choir (in the style of the library's ARR1)", "Famous", fs, 60, y, 0.42, 1.02,
                   ctl({ { "attack", 9 }, { "damp", 14 }, { "velVol", 0.3f } }));
}

inline Sound maleChoir() {
    const double fs = 14000, sec = 1.17;
    Vec y = vowelChoir(fs, sec, mtof(48), 8, { { 600, 8, 0 }, { 1000, 9, -6 }, { 2400, 12, -22 }, { 3000, 14, -26 }, { 250, 3, -8 } }, 33, 0.25, 0.08);
    return sampled("MALECHR", "Low male choir 'ooh-aah' (in the style of the library's SARARR)", "Famous", fs, 48, y, 0.5, 1.16,
                   ctl({ { "attack", 10 }, { "damp", 15 }, { "velVol", 0.3f } }));
}

// breaking glass: an impact, then shards ringing at their own high modes, thinning out
inline Sound glass() {
    const double fs = 22050, sec = 0.743;
    Vec y = zeros(fs, sec); Rng r(77);
    Vec crack = noise(fs, 0.03, 5); filt(crack, BQ::hp(fs, 1500, 0.7)); envelope(crack, fs, [](double t) { return std::exp(-t / 0.006); }); add(y, crack, 1.3);
    add(y, modal(fs, 0.12, 110, { { 1, 0.8, 0.03 }, { 2.3, 0.3, 0.02 } }), 1.0);
    double t = 0.004;
    while (t < sec - 0.03) {
        const double rate = 420 * std::exp(-t / 0.1) + 18;
        t += -std::log(1 - r.uni() * 0.999) / rate;
        const double amp = (0.25 + 0.75 * r.uni()) * std::exp(-t / 0.28);
        std::vector<Mode_> m;
        const int nm = 2 + (int) (r.uni() * 3);
        for (int k = 0; k < nm; ++k) m.push_back({ 1 + r.uni() * 2.7, 0.3 + 0.7 * r.uni(), 0.012 + 0.09 * r.uni(), r.uni() * 6.28 });
        Vec sh = modal(fs, 0.16, 2400 + r.uni() * 3000, m);
        add(y, sh, amp * 0.5, (size_t) (t * fs));
    }
    filt(y, BQ::hp(fs, 300, 0.7));
    room(y, fs, 0.15, 0.8);
    return sampled("GLASSBRK", "Breaking glass (the effect early CMI records made famous)", "Famous", fs, 60, y, -1, 0, ctl({ { "damp", 6 }, { "velVol", 0.4f } }));
}

inline Sound pizz() {
    const double fs = 22050, sec = 0.743, f0 = mtof(55);
    Vec y = zeros(fs, sec); Rng r(3);
    for (int k = 0; k < 4; ++k) {
        const double det = cents(r.bip() * 7);
        Vec p = pluck(fs, sec, [&](double) { return f0 * det; }, 0.55, 0.45, 0.18, 40 + k);
        add(y, p, 0.5, (size_t) (r.uni() * 0.012 * fs));
    }
    filt(y, BQ::peak(fs, 280, 1.5, 6)); filt(y, BQ::peak(fs, 1000, 1.2, 3)); filt(y, BQ::lp(fs, 5000, 0.7));
    room(y, fs, 0.25, 1.2);
    return sampled("PIZZ", "Pizzicato string section", "Strings", fs, 55, y, -1, 0, ctl({ { "damp", 8 } }));
}

inline Sound strings() {
    const double fs = 16000, sec = 1.024;
    Vec y = ensemble(fs, sec, mtof(60), 7, 10, 0.12, 5.6, 51, 0.18, 0, 0.05, 0.0);
    filt(y, BQ::lp(fs, 4200, 0.7)); filt(y, BQ::peak(fs, 450, 1.0, 4)); filt(y, BQ::peak(fs, 2500, 1.4, 3)); filt(y, BQ::hp(fs, 120, 0.7));
    room(y, fs, 0.2, 1.3);
    return sampled("STRINGS", "String section, sustained", "Strings", fs, 60, y, 0.36, 1.0, ctl({ { "attack", 8 }, { "damp", 14 } }));
}

inline Sound cello() {
    const double fs = 16000, sec = 1.024, f0 = mtof(48);
    Vec y = ensemble(fs, sec, f0, 1, 0, 0.16, 5.4, 61, 0.1, -20, 0.06, 0);
    Vec bow = noise(fs, sec, 62); filt(bow, BQ::bp(fs, 2000, 0.8)); add(y, bow, 0.05);
    filt(y, BQ::peak(fs, 220, 2, 6)); filt(y, BQ::peak(fs, 600, 2, 4)); filt(y, BQ::peak(fs, 1300, 2, 3)); filt(y, BQ::lp(fs, 3600, 0.7));
    room(y, fs, 0.15, 1.0);
    return sampled("CELLO", "Solo cello, bowed", "Strings", fs, 48, y, 0.4, 1.0, ctl({ { "attack", 6 }, { "damp", 12 } }));
}

inline Vec brassish(double fs, double sec, double f0, int n, double det, uint32_t seed, double peakHz, double susHz, double vib) {
    Vec y = ensemble(fs, sec, f0, n, det, vib, 5.2, seed, 0.03, -30, 0.04);
    BQ q = BQ::lp(fs, 400);
    for (size_t i = 0; i < y.size(); ++i) {
        const double t = i / fs;
        if (i % 16 == 0) { const double fc = t < 0.06 ? 400 + (peakHz - 400) * (t / 0.06) : susHz + (peakHz - susHz) * std::exp(-(t - 0.06) / 0.12); BQ n2 = BQ::lp(fs, fc, 1.0); n2.z1 = q.z1; n2.z2 = q.z2; q = n2; }
        y[i] = (float) q.run(y[i]);
    }
    return y;
}

inline Sound brass() {
    const double fs = 16000, sec = 1.024;
    Vec y = brassish(fs, sec, mtof(60), 4, 6, 71, 4800, 2400, 0.05);
    filt(y, BQ::peak(fs, 1200, 1.2, 4));
    room(y, fs, 0.2, 1.2);
    return sampled("BRASS", "Brass section", "Brass", fs, 60, y, 0.42, 1.0, ctl({ { "attack", 2 }, { "damp", 10 }, { "velFilt", 0.5f } }));
}

inline Sound trumpet() {
    const double fs = 20000, sec = 0.819;
    Vec y = brassish(fs, sec, mtof(67), 1, 0, 81, 6000, 3800, 0.15);
    filt(y, BQ::peak(fs, 1300, 1.5, 6)); filt(y, BQ::peak(fs, 2600, 2, 3)); filt(y, BQ::hp(fs, 200, 0.7));
    room(y, fs, 0.12, 0.9);
    return sampled("TRUMPET", "Solo trumpet", "Brass", fs, 67, y, 0.38, 0.81, ctl({ { "attack", 1 }, { "damp", 9 }, { "vib", 0.1f }, { "vibDelay", 0.4f } }));
}

inline Sound sax() {
    const double fs = 18000, sec = 0.91;
    Vec y = ensemble(fs, sec, mtof(58), 1, 0, 0.16, 5, 91, 0.04, -40, 0.05);
    Vec br = noise(fs, sec, 92); filt(br, BQ::bp(fs, 2500, 0.8)); add(y, br, 0.08);
    y = formants(y, fs, { { 500, 3, 6 }, { 1500, 4, 3 }, { 3000, 5, -6 }, { 200, 1, -6 } });
    drive(y, 1.5); filt(y, BQ::lp(fs, 5000, 0.7));
    room(y, fs, 0.12, 1.0);
    return sampled("SAX", "Tenor saxophone", "Wind", fs, 58, y, 0.36, 0.9, ctl({ { "attack", 2 }, { "damp", 9 } }));
}

inline Vec breathyPipe(double fs, double sec, double f0, std::initializer_list<double> harm, double breathAmt, double scoop, uint32_t seed) {
    Vec y = zeros(fs, sec); Rng r(seed);
    double ph = 0;
    for (size_t i = 0; i < y.size(); ++i) {
        const double t = i / fs;
        const double vib = 0.12 * std::sin(2 * PI * 5.0 * t) * clampd((t - 0.35) / 0.3, 0, 1);
        const double f = f0 * std::pow(2.0, (vib + scoop * std::exp(-t / 0.09)) / 12.0);
        ph += 2 * PI * f / fs;
        double v = 0; int k = 1;
        for (double a : harm) { if (f * k < fs * 0.45) v += a * std::sin(ph * k); ++k; }
        y[i] = (float) (v * std::min(1.0, t / 0.06));
    }
    Vec nz = noise(fs, sec, seed + 1);
    Vec tone = nz; filt(tone, BQ::bp(fs, f0, 9)); filt(tone, BQ::bp(fs, f0, 6));
    Vec air = nz; filt(air, BQ::hp(fs, 1800, 0.7));
    envelope(tone, fs, [](double t) { return std::min(1.0, t / 0.04); });
    envelope(air, fs, [](double t) { return 0.4 + 1.2 * std::exp(-t / 0.05); });
    add(y, tone, breathAmt * 6); add(y, air, breathAmt * 0.5);
    return y;
}

inline Sound shakuhachi() {
    const double fs = 16000, sec = 1.024;
    Vec y = breathyPipe(fs, sec, mtof(67), { 1, 0.25, 0.12, 0.04 }, 0.22, -0.9, 101);
    room(y, fs, 0.25, 1.4);
    return sampled("SHAKUHCH", "Bamboo flute with a breathy, bent attack", "Wind", fs, 67, y, 0.42, 1.0, ctl({ { "attack", 3 }, { "damp", 11 }, { "vib", 0.08f }, { "vibDelay", 0.5f } }));
}

inline Sound flute() {
    const double fs = 18000, sec = 0.91;
    Vec y = breathyPipe(fs, sec, mtof(72), { 1, 0.3, 0.1, 0.05 }, 0.1, -0.2, 111);
    room(y, fs, 0.2, 1.2);
    return sampled("FLUTE", "Concert flute", "Wind", fs, 72, y, 0.36, 0.9, ctl({ { "attack", 3 }, { "damp", 10 }, { "vib", 0.1f } }));
}

inline Sound panpipe() {
    const double fs = 18000, sec = 0.91;
    Vec y = breathyPipe(fs, sec, mtof(72), { 1, 0, 0.12, 0, 0.04 }, 0.35, -0.3, 121);
    room(y, fs, 0.3, 1.5);
    return sampled("PANPIPE", "Pan pipes", "Wind", fs, 72, y, 0.34, 0.9, ctl({ { "attack", 2 }, { "damp", 12 } }));
}

inline Sound marimba() {
    const double fs = 22050, sec = 0.743;
    Vec y = modal(fs, sec, mtof(60), { { 1, 1, 0.45 }, { 3.99, 0.35, 0.1 }, { 10.65, 0.12, 0.025 } });
    Vec m = noise(fs, 0.01, 9); filt(m, BQ::lp(fs, 3000)); envelope(m, fs, [](double t) { return std::exp(-t / 0.002); }); add(y, m, 0.3);
    room(y, fs, 0.15, 1.0);
    return sampled("MARIMBA", "Marimba", "Tuned perc", fs, 60, y, -1, 0, ctl({ { "damp", 8 }, { "sustain", 1 } }));
}

inline Sound kalimba() {
    const double fs = 22050, sec = 0.743;
    Vec y = modal(fs, sec, mtof(72), { { 1, 1, 0.55 }, { 5.95, 0.35, 0.06 }, { 13.4, 0.15, 0.02 }, { 2.01, 0.04, 0.3 } });
    filt(y, BQ::peak(fs, 400, 1, 3));
    room(y, fs, 0.2, 1.1);
    return sampled("KALIMBA", "Kalimba (thumb piano)", "Tuned perc", fs, 72, y, -1, 0, ctl({ { "damp", 9 } }));
}

inline Sound tubularBell() {
    const double fs = 16000, sec = 1.024;
    Vec y = modal(fs, sec, mtof(60), { { 1, 1, 1.8 }, { 2.76, 0.65, 1.3 }, { 5.40, 0.45, 0.8 }, { 8.93, 0.3, 0.5 }, { 13.34, 0.18, 0.35 }, { 0.5, 0.12, 2.0 } });
    room(y, fs, 0.2, 1.8);
    return sampled("TUBEBELL", "Tubular bells", "Tuned perc", fs, 60, y, 0.62, 1.02, ctl({ { "damp", 22 }, { "sustain", 0 } }));
}

inline Sound glock() {
    const double fs = 24000, sec = 0.683;
    Vec y = modal(fs, sec, mtof(84), { { 1, 1, 0.9 }, { 2.71, 0.35, 0.3 }, { 5.33, 0.15, 0.1 } });
    Vec m = noise(fs, 0.005, 13); filt(m, BQ::hp(fs, 3000)); add(y, m, 0.15);
    return sampled("GLOCK", "Glockenspiel", "Tuned perc", fs, 84, y, 0.45, 0.68, ctl({ { "damp", 15 }, { "sustain", 0 } }));
}

inline Sound steelDrum() {
    const double fs = 22050, sec = 0.743;
    Vec y = modal(fs, sec, mtof(67), { { 1, 1, 0.45 }, { 2, 0.55, 0.35 }, { 3, 0.3, 0.25 }, { 1.5, 0.08, 0.2 }, { 4, 0.12, 0.12 } }, 0.02, 0.04);
    Vec m = noise(fs, 0.01, 17); filt(m, BQ::bp(fs, 900, 1)); add(y, m, 0.2);
    room(y, fs, 0.15, 1.0);
    return sampled("STEELDRM", "Steel drum", "Tuned perc", fs, 67, y, -1, 0, ctl({ { "damp", 8 } }));
}

inline Sound koto() {
    const double fs = 22050, sec = 0.743, f0 = mtof(62);
    Vec y = pluck(fs, sec, [f0](double t) { return f0 * std::pow(2.0, clampd((t - 0.22) / 0.18, 0, 1) * 1.0 / 12.0); }, 1.0, 0.8, 0.1, 131);
    filt(y, BQ::peak(fs, 3000, 1.5, 4)); filt(y, BQ::hp(fs, 120));
    room(y, fs, 0.2, 1.2);
    return sampled("KOTO", "Koto, with the bend after the pluck", "Plucked", fs, 62, y, -1, 0, ctl({ { "damp", 10 } }));
}

inline Sound sitar() {
    const double fs = 22050, sec = 0.743, f0 = mtof(55);
    Vec y = pluck(fs, sec, [f0](double) { return f0; }, 1.6, 0.85, 0.06, 141, 1, 0.012);
    Vec d1 = pluck(fs, sec, [f0](double) { return f0 * 1.5; }, 2.5, 0.6, 0.2, 142, 0.1);
    Vec d2 = pluck(fs, sec, [f0](double) { return f0 * 2; }, 2.5, 0.6, 0.2, 143, 0.1);
    add(y, d1, 0.4); add(y, d2, 0.4);
    filt(y, BQ::peak(fs, 2500, 2, 6)); filt(y, BQ::hp(fs, 150));
    room(y, fs, 0.2, 1.3);
    return sampled("SITAR", "Sitar, with the buzzing bridge and drones", "Plucked", fs, 55, y, 0.5, 0.74, ctl({ { "damp", 17 }, { "sustain", 0 } }));
}

inline Sound harpsichord() {
    const double fs = 22050, sec = 0.743, f0 = mtof(60);
    Vec a = pluck(fs, sec, [f0](double) { return f0; }, 2.2, 0.9, 0.07, 151);
    Vec b = pluck(fs, sec, [f0](double) { return f0 * cents(4); }, 2.0, 0.9, 0.09, 152);
    Vec c = pluck(fs, sec, [f0](double) { return f0 * 2; }, 1.6, 0.9, 0.12, 153);
    add(a, b); add(a, c, 0.5);
    Vec q = noise(fs, 0.006, 154); filt(q, BQ::hp(fs, 2000)); add(a, q, 0.3);
    filt(a, BQ::peak(fs, 3500, 1.2, 3));
    room(a, fs, 0.15, 1.0);
    return sampled("HARPSI", "Harpsichord, two 8' and a 4'", "Keys", fs, 60, a, 0.5, 0.74, ctl({ { "damp", 16 }, { "sustain", 0 } }));
}

inline Sound clav() {
    const double fs = 22050, sec = 0.743, f0 = mtof(60);
    Vec y = pluck(fs, sec, [f0](double) { return f0; }, 1.5, 0.92, 0.05, 161);
    filt(y, BQ::hp(fs, 250, 0.7)); filt(y, BQ::peak(fs, 1300, 2, 8));
    drive(y, 2.0);
    return sampled("CLAV", "Clavinet", "Keys", fs, 60, y, 0.42, 0.74, ctl({ { "damp", 4 }, { "velFilt", 0.5f } }));
}

inline Sound piano() {
    const double fs = 16000, sec = 1.024, f0 = mtof(60), B = 0.00035;
    Vec y = zeros(fs, sec);
    for (int n = 1; n <= 40; ++n) {
        const double fn = n * f0 * std::sqrt(1 + B * n * n);
        if (fn > fs * 0.45) break;
        const double amp = std::pow(n, -0.85) * std::exp(-n / 16.0);
        const double t1 = 2.5 / (1 + 0.18 * n), t2 = 9.0 / (1 + 0.1 * n);
        for (int s = -1; s <= 1; ++s) {
            const double f = fn * cents(s * 0.9);
            double ph = s * 1.3;
            for (size_t i = 0; i < y.size(); ++i) {
                const double t = i / fs; ph += 2 * PI * f / fs;
                y[i] += (float) (amp / 3 * (0.7 * std::exp(-t / t1 * 3) + 0.3 * std::exp(-t / t2)) * std::sin(ph));
            }
        }
    }
    Vec th = noise(fs, 0.04, 171); filt(th, BQ::lp(fs, 600)); envelope(th, fs, [](double t) { return std::exp(-t / 0.01); }); add(y, th, 0.25);
    room(y, fs, 0.18, 1.3);
    return sampled("PIANO", "Grand piano (the grainy 8-bit kind)", "Keys", fs, 60, y, 0.7, 1.02, ctl({ { "damp", 12 } }));
}

inline Sound ePiano() {
    const double fs = 20000, sec = 0.819, f0 = mtof(60);
    Vec y = zeros(fs, sec);
    double pc = 0, pm = 0, pt = 0;
    for (size_t i = 0; i < y.size(); ++i) {
        const double t = i / fs;
        pc += 2 * PI * f0 / fs; pm += 2 * PI * f0 / fs; pt += 2 * PI * f0 * 14 / fs;
        const double idx = 1.4 * std::exp(-t / 0.25) + 0.2;
        y[i] = (float) (std::exp(-t / 1.6) * std::sin(pc + idx * std::sin(pm)) + 0.25 * std::exp(-t / 0.04) * std::sin(pt));
    }
    room(y, fs, 0.1, 0.9);
    return sampled("TINEPNO", "Electric tine piano", "Keys", fs, 60, y, 0.55, 0.81, ctl({ { "damp", 13 }, { "velFilt", 0.5f } }));
}

inline Sound slapBass() {
    const double fs = 22050, sec = 0.743, f0 = mtof(40);
    Vec y = pluck(fs, sec, [f0](double) { return f0; }, 1.4, 0.75, 0.08, 181, 1.3);
    Vec pop = noise(fs, 0.02, 182); filt(pop, BQ::bp(fs, 2500, 1)); envelope(pop, fs, [](double t) { return std::exp(-t / 0.004); }); add(y, pop, 0.6);
    filt(y, BQ::peak(fs, 80, 1, 4)); filt(y, BQ::peak(fs, 2000, 1, 5));
    return sampled("SLAPBASS", "Slap bass", "Bass", fs, 40, y, 0.42, 0.74, ctl({ { "damp", 8 } }));
}

inline Sound fretless() {
    const double fs = 16000, sec = 1.024, f0 = mtof(40);
    Vec y = pluck(fs, sec, [f0](double t) { return f0 * std::pow(2.0, -std::exp(-t / 0.03) / 12.0); }, 2.4, 0.5, 0.25, 191);
    BQ q = BQ::bp(fs, 600, 2);
    Vec w = y;
    for (size_t i = 0; i < w.size(); ++i) { if (i % 32 == 0) { const double t = i / (double) fs; BQ n = BQ::bp(fs, 500 + 700 * std::min(1.0, t / 0.4), 2.5); n.z1 = q.z1; n.z2 = q.z2; q = n; } w[i] = (float) q.run(w[i]); }
    add(y, w, 0.8);
    return sampled("FRETLESS", "Fretless bass with the 'mwah'", "Bass", fs, 40, y, 0.5, 1.02, ctl({ { "damp", 10 }, { "porta", 0.06f } }));
}

inline Sound synBass() {
    const double fs = 16000, sec = 1.024, f0 = mtof(36);
    Vec y = zeros(fs, sec); Saw a, b; Pulse s;
    BQ q1 = BQ::lp(fs, 3000, 1.6), q2 = q1;
    for (size_t i = 0; i < y.size(); ++i) {
        const double t = i / fs;
        if (i % 16 == 0) { const double fc = 280 + 2800 * std::exp(-t / 0.12); BQ n1 = BQ::lp(fs, fc, 1.8), n2 = BQ::lp(fs, fc, 0.6); n1.z1 = q1.z1; n1.z2 = q1.z2; n2.z1 = q2.z1; n2.z2 = q2.z2; q1 = n1; q2 = n2; }
        double v = a.run(f0, fs) + b.run(f0 * cents(8), fs) + 0.6 * s.run(f0 / 2, fs, 0.5);
        y[i] = (float) q2.run(q1.run(v * 0.4));
    }
    return sampled("SYNBASS", "Sampled synth bass", "Bass", fs, 36, y, 0.5, 1.02, ctl({ { "damp", 6 }, { "velFilt", 0.6f } }));
}

inline Sound bassGuitar() {
    const double fs = 22050, sec = 0.743, f0 = mtof(40);
    Vec y = pluck(fs, sec, [f0](double) { return f0; }, 1.8, 0.6, 0.15, 201);
    filt(y, BQ::peak(fs, 900, 1, 3)); filt(y, BQ::lp(fs, 4000));
    return sampled("BASSGT", "Picked bass guitar", "Bass", fs, 40, y, 0.45, 0.74, ctl({ { "damp", 6 } }));
}

inline Sound guitarLead() {
    const double fs = 20000, sec = 0.819, f0 = mtof(64);
    Vec y = zeros(fs, sec); Saw a; Pulse p;
    for (size_t i = 0; i < y.size(); ++i) {
        const double t = i / fs;
        const double vib = 0.3 * std::sin(2 * PI * 5.5 * t) * clampd((t - 0.3) / 0.2, 0, 1);
        const double f = f0 * std::pow(2.0, (vib - 0.5 * std::exp(-t / 0.03)) / 12.0);
        y[i] = (float) (0.6 * a.run(f, fs) + 0.4 * p.run(f, fs, 0.3));
    }
    filt(y, BQ::hp(fs, 120)); drive(y, 7.0);
    filt(y, BQ::lp(fs, 4200, 0.8)); filt(y, BQ::peak(fs, 1800, 1.2, 5));
    room(y, fs, 0.2, 1.2);
    return sampled("GTRLEAD", "Overdriven lead guitar (the TV-theme kind)", "Plucked", fs, 64, y, 0.36, 0.81, ctl({ { "damp", 9 }, { "vib", 0.15f }, { "vibDelay", 0.4f }, { "porta", 0.05f } }));
}

inline Sound nylon() {
    const double fs = 22050, sec = 0.743, f0 = mtof(52);
    Vec y = pluck(fs, sec, [f0](double) { return f0; }, 2.2, 0.45, 0.2, 211);
    filt(y, BQ::peak(fs, 110, 2, 5)); filt(y, BQ::peak(fs, 220, 2, 3));
    room(y, fs, 0.15, 1.0);
    return sampled("NYLONGTR", "Nylon-string guitar", "Plucked", fs, 52, y, -1, 0, ctl({ { "damp", 10 } }));
}

inline Sound timpani() {
    const double fs = 16000, sec = 1.024;
    Vec y = modal(fs, sec, mtof(43), { { 1, 1, 1.2 }, { 1.5, 0.6, 0.9 }, { 1.98, 0.4, 0.7 }, { 2.44, 0.25, 0.5 }, { 2.9, 0.15, 0.4 } }, 0.012, 0.15);
    Vec th = noise(fs, 0.05, 221); filt(th, BQ::lp(fs, 300)); envelope(th, fs, [](double t) { return std::exp(-t / 0.015); }); add(y, th, 0.8);
    room(y, fs, 0.25, 1.8);
    return sampled("TIMPANI", "Timpani", "Orchestral", fs, 43, y, -1, 0, ctl({ { "damp", 14 } }));
}

inline Sound gong() {
    const double fs = 12000, sec = 1.365;
    Vec y = zeros(fs, sec); Rng r(231);
    for (int k = 0; k < 40; ++k) {
        const double f = 90 * std::pow(40.0, r.uni()), hi = std::log(f / 90) / std::log(40.0);
        const double bloom = 0.1 + hi * 0.5;
        double ph = r.uni() * 6.28;
        for (size_t i = 0; i < y.size(); ++i) {
            const double t = i / fs; ph += 2 * PI * f / fs;
            const double e = (1 - std::exp(-t / bloom)) * std::exp(-t / (2.5 - hi * 1.5)) + (1 - hi) * std::exp(-t / 0.08);
            y[i] += (float) (e * std::sin(ph) / (1 + hi * 2));
        }
    }
    add(y, modal(fs, 0.3, 70, { { 1, 2, 0.1 } }));
    return sampled("GONG", "Tam-tam gong, with the bloom", "Orchestral", fs, 48, y, 0.95, 1.36, ctl({ { "damp", 25 }, { "sustain", 0 } }));
}

inline Sound hey() {
    const double fs = 16000, sec = 0.6;
    Vec src = zeros(fs, sec); Rng r(241);
    for (int k = 0; k < 6; ++k) {
        Saw o; const double f0 = 190 + r.uni() * 80, st = r.uni() * 0.02;
        for (size_t i = 0; i < src.size(); ++i) {
            const double t = i / fs; if (t < st) continue;
            const double f = f0 * std::pow(2.0, (-3 * clampd((t - 0.15) / 0.2, 0, 1)) / 12.0);
            src[i] += (float) (o.run(f, fs) * 0.3);
        }
    }
    Vec h = noise(fs, sec, 242); envelope(h, fs, [](double t) { return 2.0 * std::exp(-t / 0.035); }); add(src, h);
    Vec y = formants(src, fs, { { 530, 7, 0 }, { 1850, 9, -3 }, { 2500, 10, -10 }, { 3500, 10, -18 } });
    envelope(y, fs, [](double t) { return std::min(1.0, t / 0.01) * (t < 0.22 ? 1 : std::exp(-(t - 0.22) / 0.06)); });
    room(y, fs, 0.3, 1.0);
    return sampled("VOXHEY", "Crowd shout 'hey!'", "Voices", fs, 60, y, -1, 0, ctl({ { "damp", 6 } }));
}

inline Sound breath() {
    const double fs = 16000, sec = 1.024;
    Vec n = noise(fs, sec, 251);
    Vec y = formants(n, fs, { { 700, 4, 0 }, { 1200, 5, -3 }, { 2600, 6, -6 } });
    envelope(y, fs, [](double t) { return std::sin(PI * std::min(1.0, t / 1.0)) * (0.7 + 0.3 * std::sin(2 * PI * 2 * t)); });
    return sampled("BREATH", "Human breath", "Voices", fs, 60, y, -1, 0, ctl({ { "damp", 10 } }));
}

// ---- drums ----
inline Sound kick() {
    const double fs = 24000, sec = 0.5;
    Vec y = zeros(fs, sec); double ph = 0;
    for (size_t i = 0; i < y.size(); ++i) { const double t = i / fs; ph += 2 * PI * (48 + 110 * std::exp(-t / 0.035)) / fs; y[i] = (float) (std::sin(ph) * std::exp(-t / 0.22)); }
    Vec c = noise(fs, 0.004, 301); filt(c, BQ::hp(fs, 2000)); add(y, c, 0.5);
    drive(y, 1.8);
    return sampled("KICK1", "Kick drum", "Drums", fs, 60, y, -1, 0, ctl({ { "damp", 6 }, { "velVol", 0.5f } }));
}
inline Sound snare() {
    const double fs = 24000, sec = 0.45;
    Vec y = modal(fs, sec, 185, { { 1, 0.8, 0.07 }, { 1.78, 0.5, 0.05 } }, 0.15, 0.01);
    Vec n = noise(fs, sec, 311); filt(n, BQ::hp(fs, 1500)); filt(n, BQ::peak(fs, 5000, 1, 4));
    envelope(n, fs, [](double t) { return 0.8 * std::exp(-t / 0.13); }); add(y, n);
    room(y, fs, 0.28, 0.9);
    return sampled("SNARE1", "Snare drum (a drum-machine snare, sampled)", "Drums", fs, 60, y, -1, 0, ctl({ { "damp", 6 }, { "velVol", 0.5f } }));
}
inline Sound crossStick() {
    const double fs = 24000, sec = 0.15;
    Vec y = modal(fs, sec, 500, { { 1, 0.6, 0.02 }, { 3.4, 0.8, 0.012 }, { 5.2, 0.4, 0.008 } });
    Vec n = noise(fs, 0.004, 321); add(y, n, 0.6);
    return sampled("CROSSTIX", "Cross-stick rim click", "Drums", fs, 60, y, -1, 0, ctl({ { "damp", 4 }, { "velVol", 0.5f } }));
}
inline Vec metal(double fs, double sec, double dec, uint32_t seed) {
    const double fr[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    Vec y = zeros(fs, sec); Pulse p[6];
    for (size_t i = 0; i < y.size(); ++i) { double v = 0; for (int k = 0; k < 6; ++k) v += p[k].run(fr[k] * 1.4, fs, 0.5); y[i] = (float) (v / 6); }
    Vec n = noise(fs, sec, seed); add(y, n, 0.4);
    filt(y, BQ::hp(fs, 7000, 0.7)); filt(y, BQ::bp(fs, 9500, 0.9));
    envelope(y, fs, [dec](double t) { return std::exp(-t / dec); });
    return y;
}
inline Sound hhPedal() { const double fs = 30000; Vec y = metal(fs, 0.12, 0.018, 331); return sampled("HHPEDAL", "Pedal hi-hat", "Drums", fs, 60, y, -1, 0, ctl({ { "damp", 3 }, { "velVol", 0.6f } })); }
inline Sound hiHat() { const double fs = 30000; Vec y = metal(fs, 0.2, 0.04, 341); return sampled("HIHAT2", "Closed hi-hat", "Drums", fs, 60, y, -1, 0, ctl({ { "damp", 3 }, { "velVol", 0.6f } })); }
inline Sound hhOpen() { const double fs = 30000; Vec y = metal(fs, 0.546, 0.22, 351); return sampled("HHOPEN1", "Open hi-hat", "Drums", fs, 60, y, -1, 0, ctl({ { "damp", 5 }, { "velVol", 0.6f } })); }
inline Sound clap() {
    const double fs = 24000, sec = 0.4;
    Vec n = noise(fs, sec, 361); filt(n, BQ::bp(fs, 1200, 1.2));
    envelope(n, fs, [](double t) {
        double e = 0; for (int k = 0; k < 4; ++k) { const double s = k * 0.011; if (t >= s) e = std::max(e, std::exp(-(t - s) / (k == 3 ? 0.1 : 0.006))); }
        return t > 0.17 ? e * std::exp(-(t - 0.17) / 0.008) : e;   // the gate
    });
    room(n, fs, 0.4, 0.6);
    envelope(n, fs, [](double t) { return t > 0.2 ? std::exp(-(t - 0.2) / 0.006) : 1.0; });
    return sampled("CLAPGATE", "Handclap through a gated room", "Drums", fs, 60, n, -1, 0, ctl({ { "damp", 4 }, { "velVol", 0.5f } }));
}
inline Sound tom(bool hi) {
    const double fs = 22050, sec = 0.6;
    const double f = hi ? 190 : 110;
    Vec y = zeros(fs, sec); double ph = 0;
    for (size_t i = 0; i < y.size(); ++i) { const double t = i / fs; ph += 2 * PI * f * (1 + 0.5 * std::exp(-t / 0.05)) / fs; y[i] = (float) (std::sin(ph) * std::exp(-t / 0.25)); }
    Vec n = noise(fs, 0.03, hi ? 371 : 372); filt(n, BQ::lp(fs, 3000)); envelope(n, fs, [](double t) { return std::exp(-t / 0.01); }); add(y, n, 0.4);
    room(y, fs, 0.25, 0.9);
    return sampled(hi ? "TOMHI" : "TOMLO", hi ? "High tom" : "Low tom", "Drums", fs, 60, y, -1, 0, ctl({ { "damp", 6 } }));
}
inline Sound conga() {
    const double fs = 22050, sec = 0.4;
    Vec y = modal(fs, sec, 330, { { 1, 1, 0.12 }, { 1.52, 0.4, 0.06 }, { 2.3, 0.2, 0.03 } }, 0.06, 0.01);
    Vec n = noise(fs, 0.01, 381); filt(n, BQ::hp(fs, 1500)); add(y, n, 0.3);
    return sampled("CONGA", "Conga", "Drums", fs, 60, y, -1, 0, ctl({ { "damp", 5 } }));
}
inline Sound cowbell() {
    const double fs = 22050, sec = 0.35;
    Vec y = zeros(fs, sec); Pulse a, b;
    for (size_t i = 0; i < y.size(); ++i) { const double t = i / fs; y[i] = (float) ((a.run(540, fs, 0.5) + b.run(800, fs, 0.5)) * (0.7 * std::exp(-t / 0.02) + 0.3 * std::exp(-t / 0.12))); }
    filt(y, BQ::bp(fs, 900, 1.2)); filt(y, BQ::bp(fs, 900, 0.8));
    return sampled("COWBELL", "Cowbell", "Drums", fs, 60, y, -1, 0, ctl({ { "damp", 4 } }));
}

// ---- Page 4 waveform sounds (harmonics drawn per segment) ----
inline Sound wSaw() {
    return waveform("SAWTOOTH", "Page 4 sawtooth", "Waveform", [](int, double* a, double*) { for (int k = 0; k < 32; ++k) a[k] = 1.0 / (k + 1); }, 0, 127,
                    ctl({ { "damp", 8 }, { "filter", 190 } }));
}
inline Sound wSquare() {
    return waveform("SQUARE", "Page 4 square", "Waveform", [](int, double* a, double*) { for (int k = 0; k < 32; k += 2) a[k] = 1.0 / (k + 1); }, 0, 127,
                    ctl({ { "damp", 8 }, { "filter", 190 } }));
}
inline Sound wOrgan() {
    // drawbars 88 8800 000 on a 16' fundamental: harmonics 1, 2, 3, 4 (+ a key click in the first segments)
    return waveform("ORGAN", "Page 4 drawbar organ", "Waveform", [](int p, double* a, double* ph) {
        a[0] = 1; a[1] = 1; a[2] = 0.7; a[3] = 0.7; a[5] = 0.25; a[7] = 0.2;
        if (p < 2) for (int k = 8; k < 32; ++k) { a[k] = 0.15; ph[k] = k * 1.7; }
    }, 4, 127, ctl({ { "damp", 4 }, { "vib", 0.08f }, { "vibRate", 6.5f }, { "vibDelay", 0 }, { "seg", 0.01f } }));
}
inline Sound wVox() {
    // a voice morphing a -> o -> i: formant envelopes sampled at the harmonics of a 220 Hz voice
    auto spec = [](double f, double f1, double f2, double f3) {
        auto pk = [](double x, double c, double bw) { const double d = (x - c) / bw; return 1.0 / (1 + d * d); };
        return pk(f, f1, 90) + 0.6 * pk(f, f2, 110) + 0.25 * pk(f, f3, 140) + 0.02;
    };
    return waveform("VOXMORPH", "Page 4 voice, a-o-i", "Waveform", [spec](int p, double* a, double*) {
        const double u = p / 127.0;
        double f1, f2, f3;
        if (u < 0.5) { const double w = u / 0.5; f1 = 750 + (450 - 750) * w; f2 = 1200 + (800 - 1200) * w; f3 = 2600; }
        else { const double w = (u - 0.5) / 0.5; f1 = 450 + (300 - 450) * w; f2 = 800 + (2300 - 800) * w; f3 = 2600 + (3000 - 2600) * w; }
        for (int k = 0; k < 32; ++k) a[k] = spec(220.0 * (k + 1), f1, f2, f3) / std::sqrt(k + 1.0);
    }, 64, 127, ctl({ { "attack", 6 }, { "damp", 12 }, { "vib", 0.12f }, { "seg", 0.025f } }));
}
inline Sound wStrings() {
    return waveform("SYNSTR", "Page 4 evolving synth strings (sweeping pulse)", "Waveform", [](int p, double* a, double*) {
        const double pw = 0.5 - 0.42 * std::sin(PI * p / 127.0);
        for (int k = 0; k < 32; ++k) { const int n = k + 1; a[k] = std::fabs(std::sin(PI * n * pw)) / n * std::exp(-n / 24.0); }
    }, 0, 127, ctl({ { "attack", 9 }, { "damp", 14 }, { "seg", 0.03f }, { "vib", 0.06f } }));
}
inline Sound wBell() {
    return waveform("SYNBELL", "Page 4 bell: the high harmonics die first", "Waveform", [](int p, double* a, double*) {
        const double t = p / 127.0;
        const int h[] = { 1, 2, 3, 5, 7, 9, 11, 14, 17, 21 };
        int i = 0;
        for (int n : h) { a[n - 1] = std::exp(-t * (1 + n * 0.9)) / std::sqrt((double) n) * (i % 2 ? 0.8 : 1.0); ++i; }
    }, 120, 127, ctl({ { "damp", 19 }, { "seg", 0.012f }, { "sustain", 0 } }));
}

}  // namespace gen

// ---------------------------------------------------------------------------------------------------------------
// the library and its presets
// ---------------------------------------------------------------------------------------------------------------
class Library {
public:
    static Library& get() { static Library l; return l; }

    Bank bank;
    int size() const { return bank.count.load(); }
    const Sound* sound(int i) const { return bank.at(i); }
    int find(const std::string& name) const { for (int i = 0; i < size(); ++i) if (sound(i) && sound(i)->name == name) return i; return -1; }

    // user slots (loaded samples): replace the sound in a slot, keeping the old one alive
    int userSlot(int k) const { return builtIn + k; }
    static constexpr int USER_SLOTS = 200;
    void setUser(int k, Sound s) {
        if (k < 0 || k >= USER_SLOTS) return;
        auto p = std::make_unique<Sound>(std::move(s));
        bank.slot[(size_t) (builtIn + k)].store(p.get());
        std::lock_guard<std::mutex> g(lock); owned.push_back(std::move(p));
    }

    // Page R: the factory song (pattern 1 is the one on the famous screen)
    static void factorySeq(SeqData& d, const Library& lib) {
        const char* tracks[TRACKS] = { "KICK1", "SNARE1", "CROSSTIX", "ORCHHIT", "HIHAT2", "HHOPEN1", "CLAPGATE", "BASSGT" };
        for (int t = 0; t < TRACKS; ++t) { d.trk[t].sound = std::max(0, lib.find(tracks[t])); d.trk[t].level = 0.7f; d.trk[t].audible = true; d.trk[t].pan = 0.5f; }
        d.trk[4].pan = 0.62f; d.trk[5].pan = 0.62f; d.trk[2].pan = 0.4f;
        for (auto& p : d.pat) for (auto& tr : p.st) for (auto& s : tr) s = {};
        auto on = [&](int p, int t, std::initializer_list<int> steps, int vel = 110, int note = 0) { for (int s : steps) d.pat[p].st[t][s] = { (uint8_t) vel, (int8_t) note }; };
        // pattern 1
        on(0, 0, { 0, 4, 8, 12 }); on(0, 1, { 4, 12 }); on(0, 2, { 15 }, 90);
        for (int s = 0; s < 16; ++s) on(0, 4, { s }, s % 2 ? 70 : 100);
        on(0, 5, { 2, 10 }, 90); on(0, 6, { 12, 13, 14, 15 }, 100);
        // pattern 2: + bass line
        d.pat[1] = d.pat[0];
        const int bl[16] = { 0, -99, 0, 12, -99, 0, -99, 10, 0, -99, 0, 12, -99, 7, -99, 10 };
        for (int s = 0; s < 16; ++s) if (bl[s] > -99) d.pat[1].st[7][s] = { 110, (int8_t) (bl[s] - 3) };
        // pattern 3: orchestra stabs over the bass, no claps
        d.pat[2] = d.pat[1];
        for (int s = 12; s < 16; ++s) d.pat[2].st[6][s] = {};
        on(2, 3, { 0 }, 120, 0); on(2, 3, { 3 }, 110, 0); on(2, 3, { 6 }, 120, 3); on(2, 3, { 10 }, 110, -2);
        // pattern 4: breakdown fill
        on(3, 0, { 0, 3, 6, 10 }); on(3, 1, { 4, 12, 13, 14, 15 }, 100); on(3, 3, { 0 }, 127, 5); on(3, 3, { 8 }, 120, 3);
        for (int s = 0; s < 16; s += 2) on(3, 4, { s }, 90);
        on(3, 6, { 12, 14 });
        const int song[] = { 0, 0, 1, 1, 2, 2, 1, 3 };
        d.songLen = 8; for (int i = 0; i < 8; ++i) d.song[i] = song[i];
        d.swing = 0;
    }

    int builtIn = 0;
    std::vector<const Sound*> blank;   // the empty user voices

    // copies the library into another bank (each plugin instance or app has its own, for its own user samples)
    void fill(Bank& b) const { const int n = size(); for (int i = 0; i < n; ++i) b.slot[(size_t) i].store(bank.at(i)); b.count = n; }

private:
    Library() {
        using namespace gen;
        std::vector<std::function<Sound()>> makers = {
            orchHit, choirAah, maleChoir, glass, pizz, strings, brass, trumpet, shakuhachi, panpipe, marimba, kalimba,
            tubularBell, harpsichord, piano, ePiano, clav, koto, sitar, steelDrum, glock, flute, sax, cello,
            slapBass, fretless, synBass, bassGuitar, guitarLead, nylon, timpani, gong, hey, breath,
            wSaw, wSquare, wOrgan, wVox, wStrings, wBell,
            kick, snare, crossStick, hhPedal, hiHat, hhOpen, clap, [] { return tom(true); }, [] { return tom(false); }, conga, cowbell,
        };
        int i = 0;
        for (auto& m : makers) { auto p = std::make_unique<Sound>(m()); bank.slot[(size_t) i++].store(p.get()); owned.push_back(std::move(p)); }
        builtIn = i;
        // empty user slots: a soft sine until something is loaded
        const Sound empty = gen::waveform("USER", "Empty: load or import samples (Page 8)", "User", [](int, double* a, double*) { a[0] = 1; }, 0, 127, Controls());
        for (int k = 0; k < USER_SLOTS; ++k) {
            Sound s = empty; s.name = "USER" + std::to_string(k + 1);
            auto p = std::make_unique<Sound>(std::move(s)); blank.push_back(p.get()); bank.slot[(size_t) i++].store(p.get()); owned.push_back(std::move(p));
        }
        bank.count = i;
    }
    std::vector<std::unique_ptr<Sound>> owned;
    std::mutex lock;
};

// One instance's sounds: the library plus its own user samples
class Kit {
public:
    Bank bank;
    Kit() { Library::get().fill(bank); }
    int userSlot(int k) const { return Library::get().builtIn + k; }
    void setUser(int k, Sound s) {
        if (k < 0 || k >= Library::USER_SLOTS) return;
        auto p = std::make_unique<Sound>(std::move(s));
        bank.slot[(size_t) userSlot(k)].store(p.get());
        std::lock_guard<std::mutex> g(lock); owned.push_back(std::move(p));
    }
    const Sound* user(int k) const { return bank.at(userSlot(k)); }
    void clearUser(int k) { if (k >= 0 && k < Library::USER_SLOTS) bank.slot[(size_t) userSlot(k)].store(Library::get().blank[(size_t) k]); }
    int firstFree(int from = 0) const { for (int k = std::max(0, from); k < Library::USER_SLOTS; ++k) if (!userLoaded(k)) return k; return -1; }
    int loadedCount() const { int n = 0; for (int k = 0; k < Library::USER_SLOTS; ++k) n += userLoaded(k); return n; }
    bool userLoaded(int k) const { return k >= 0 && k < Library::USER_SLOTS && user(k) != Library::get().blank[(size_t) k]; }
private:
    std::vector<std::unique_ptr<Sound>> owned; std::mutex lock;
};

// Turns any audio (mono floats at srcRate) into a CMI sound, like Page 8 sampling.
//  - A sound that already fits a channel card (at most 32 kHz and 16 KB, as the CMI library's own sounds are) is
//    kept sample for sample at its own rate.
//  - Anything else is resampled to the highest CMI rate that fits 16 KB (never above 32 kHz), anti-aliased first.
//  - fileLoopStart / fileLoopEnd (samples at srcRate, end inclusive): a loop the file brings (WAV "smpl" chunk, AIFF
//    markers); it's kept exactly, snapped to whole pages as the card needs. Without one, loop = loop the last 45%.
inline Sound sampleToCmi(const std::vector<float>& in, double srcRate, const std::string& name, int root, bool loop,
                         long fileLoopStart = -1, long fileLoopEnd = -1) {
    using namespace gen;
    const bool fileLoop = fileLoopStart >= 0 && fileLoopEnd > fileLoopStart && fileLoopStart < (long) in.size();
    // what has to fit: with a loop, everything up to its end
    const double need = (fileLoop ? std::min<double>((double) in.size(), fileLoopEnd + 1.0) : (double) in.size()) / srcRate;
    double rate;
    const bool native = srcRate <= 32000.5 && srcRate >= 4000 && need * srcRate <= RAM_SIZE;
    if (native) rate = srcRate;
    else rate = clampd(std::min({ 32000.0, srcRate, (double) RAM_SIZE / std::max(need, 1e-3) }), 4000.0, 32000.0);
    const double dur = in.size() / srcRate;
    const size_t n = (size_t) std::min<double>(RAM_SIZE, std::floor(dur * rate + 1e-6));
    Vec y(n);
    if (native) { for (size_t i = 0; i < n; ++i) y[i] = in[i]; }
    else {
        std::vector<float> f = in;
        if (rate < srcRate) { BQ a = BQ::lp(srcRate, rate * 0.45, 0.54), b = BQ::lp(srcRate, rate * 0.45, 1.3); for (auto& v : f) v = (float) b.run(a.run(v)); }
        for (size_t i = 0; i < n; ++i) {
            const double p = i * srcRate / rate; const size_t k = (size_t) p; const double fr = p - k;
            const float a = k < f.size() ? f[k] : 0.f, b = k + 1 < f.size() ? f[k + 1] : a;
            y[i] = (float) (a + (b - a) * fr);
        }
    }
    std::string nm = name; for (auto& c : nm) c = (char) std::toupper((unsigned char) c);
    nm.erase(std::remove_if(nm.begin(), nm.end(), [](char c) { return !std::isalnum((unsigned char) c); }), nm.end());
    if (nm.size() > 8) nm.resize(8);
    if (nm.empty()) nm = "SAMPLE";
    const double secs = n / rate;
    const std::string title = "Sampled: " + name;
    if (fileLoop) {
        const double ls = fileLoopStart / srcRate, le = (fileLoopEnd + 1.0) / srcRate;
        // page-aligned already (sounds from a CMI): kept exactly; otherwise the seam is smoothed
        const double lsP = ls * rate / PAGE, leP = le * rate / PAGE;
        const bool aligned = std::fabs(lsP - std::round(lsP)) < 0.02 && std::fabs(leP - std::round(leP)) < 0.02;
        return gen::sampled(nm.c_str(), title.c_str(), "User", rate, root, y, ls, std::min(le, secs), Controls(), !aligned, true);
    }
    return gen::sampled(nm.c_str(), title.c_str(), "User", rate, root, y, loop ? secs * 0.55 : -1, secs, Controls());
}

// A WAV file in memory: PCM 8/16/24/32-bit or float, any channels (mixed to mono), its "smpl" chunk (root key and
// first loop). Used where there's no audio library (the Android app).
struct WavData { std::vector<float> mono; double rate = 0; int root = -1; long loopStart = -1, loopEnd = -1; std::string err; };
inline WavData parseWav(const uint8_t* d, size_t n, size_t maxFrames = 32000 * 12) {
    WavData w;
    auto u16 = [&](size_t o) { return o + 2 <= n ? (uint32_t) (d[o] | d[o + 1] << 8) : 0u; };
    auto u32 = [&](size_t o) { return o + 4 <= n ? (uint32_t) (d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t) d[o + 3] << 24) : 0u; };
    if (n < 12 || std::memcmp(d, "RIFF", 4) || std::memcmp(d + 8, "WAVE", 4)) { w.err = "NOT A WAV FILE"; return w; }
    int fmt = 0, ch = 0, bits = 0; size_t dataOff = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= n;) {
        const uint32_t len = u32(o + 4); const size_t body = o + 8;
        if (!std::memcmp(d + o, "fmt ", 4)) {
            fmt = (int) u16(body); ch = (int) u16(body + 2); w.rate = u32(body + 4); bits = (int) u16(body + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = (int) u16(body + 24);   // extensible: the sub-format's first word
        } else if (!std::memcmp(d + o, "data", 4)) { dataOff = body; dataLen = std::min<size_t>(len, n - body); }
        else if (!std::memcmp(d + o, "smpl", 4) && len >= 36) {
            const uint32_t unity = u32(body + 12), loops = u32(body + 28);
            if (unity > 0 && unity < 128) w.root = (int) unity;
            if (loops > 0 && len >= 36 + 24) { w.loopStart = (long) u32(body + 36 + 8); w.loopEnd = (long) u32(body + 36 + 12); }
        }
        o = body + len + (len & 1);
    }
    if (!dataOff || ch <= 0 || w.rate <= 0) { w.err = "NO AUDIO IN WAV"; return w; }
    if (!(fmt == 1 || fmt == 3) || !(bits == 8 || bits == 16 || bits == 24 || bits == 32) || (fmt == 3 && bits != 32)) { w.err = "UNSUPPORTED WAV FORMAT"; return w; }
    const size_t bps = (size_t) bits / 8, frames = std::min(maxFrames, dataLen / (bps * (size_t) ch));
    w.mono.resize(frames);
    for (size_t f = 0; f < frames; ++f) {
        double s = 0;
        for (int c = 0; c < ch; ++c) {
            const uint8_t* p = d + dataOff + (f * (size_t) ch + (size_t) c) * bps;
            double v;
            if (fmt == 3) { float x; std::memcpy(&x, p, 4); v = x; }
            else if (bits == 8) v = (p[0] - 128) / 128.0;
            else if (bits == 16) v = (int16_t) (p[0] | p[1] << 8) / 32768.0;
            else if (bits == 24) { int32_t x = p[0] | p[1] << 8 | p[2] << 16; if (x & 0x800000) x |= ~0xFFFFFF; v = x / 8388608.0; }
            else { int32_t x; std::memcpy(&x, p, 4); v = x / 2147483648.0; }
            s += v;
        }
        w.mono[f] = (float) (s / ch);
    }
    if (w.loopEnd >= (long) frames) w.loopEnd = (long) frames - 1;
    if (w.loopStart >= w.loopEnd) w.loopStart = w.loopEnd = -1;
    return w;
}

}  // namespace cmi
