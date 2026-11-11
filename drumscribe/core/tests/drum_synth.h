// A synthetic drummer with exact ground truth (times, pieces, velocities, note lengths), for tests/test_drums.cpp.
// Every kit piece is synthesized (no samples) and played with human timing: per-note jitter, swing, slow drift,
// flams, ghost notes, fills, open / pedal hats, ride bell, two crashes, side stick. render() gives the six stems
// a perfect separator would give, with -26 dB of each stem leaking into the others.
#pragma once
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>
#include "ds/types.h"

namespace synth {
using ds::Signal;
constexpr int SR = 44100;
constexpr double PI = 3.14159265358979323846;

struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned s) : g(s) {}
    // portable: the standard distributions give different numbers on libstdc++, libc++ and MSVC; mt19937's raw
    // output is the same everywhere, so the tests see the same drummer on every platform
    double u01() { return ((double) g() + 0.5) / 4294967296.0; }
    double uni(double a, double b) { return a + (b - a) * u01(); }
    double norm(double m, double s) { return m + s * std::sqrt(-2 * std::log(u01())) * std::cos(6.283185307179586 * u01()); }
};

struct BQ {
    double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    BQ(bool high, double fc, double q)
    {
        const double w = 2 * PI * fc / SR, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        if (high) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; } else { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    double operator()(double x) { const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
};
inline Signal hp(Signal x, double fc) { BQ f(true, fc, 0.7071); for (auto& v : x) v = (float) f(v); return x; }
inline Signal bp(Signal x, double lo, double hi) { BQ a(true, lo, 0.7071), b(false, std::min(hi, 0.45 * SR), 0.7071); for (auto& v : x) v = (float) b(a(v)); return x; }
inline Signal noise(Rng& r, size_t n) { Signal x(n); for (auto& v : x) v = (float) r.norm(0, 1); return x; }

inline Signal kick(Rng& r, double v)
{
    const size_t n = (size_t) (0.45 * SR);
    Signal click = hp(noise(r, n), 2000), out(n);
    double ph = 0;
    for (size_t i = 0; i < n; ++i) {
        const double t = (double) i / SR, f = 48 + 110 * std::exp(-t / 0.035);
        ph += 2 * PI * f / SR;
        out[i] = (float) ((std::sin(ph) * std::exp(-t / 0.16) + click[i] * std::exp(-t / 0.003) * 0.4) * v);
    }
    return out;
}
inline Signal snare(Rng& r, double v)
{
    const size_t n = (size_t) (0.40 * SR);
    Signal w = bp(noise(r, n), 1500, 9000), out(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = (double) i / SR;
        const double tone = (std::sin(2 * PI * 185 * t) + 0.6 * std::sin(2 * PI * 330 * t)) * std::exp(-t / 0.06);
        out[i] = (float) ((0.7 * tone + 1.6 * w[i] * std::exp(-t / 0.13)) * v);
    }
    return out;
}
inline Signal side_stick(Rng& r, double v)
{
    const size_t n = (size_t) (0.10 * SR);
    Signal z = bp(noise(r, n), 2000, 6000), out(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = (double) i / SR;
        const double tone = (std::sin(2 * PI * 520 * t) + 0.8 * std::sin(2 * PI * 1630 * t) + 0.4 * std::sin(2 * PI * 2750 * t)) * std::exp(-t / 0.018);
        out[i] = (float) ((tone + 0.05 * z[i] * std::exp(-t / 0.01)) * v);
    }
    return out;
}
inline Signal metal(Rng& r, size_t n, double base, double decay, double bright)
{
    const double ratios[6] = {1.0, 1.4471, 1.6170, 1.9265, 2.5028, 2.6637};
    double ph[6];
    for (auto& p : ph) p = r.uni(0, 6.28);
    Signal x(n), nz = noise(r, n);
    for (size_t i = 0; i < n; ++i) {
        const double t = (double) i / SR;
        double sq = 0;
        for (int k = 0; k < 6; ++k) sq += std::sin(2 * PI * base * ratios[k] * t + ph[k]) >= 0 ? 1 : -1;
        x[i] = (float) (sq * 0.3 + nz[i]);
    }
    x = hp(x, bright);
    for (size_t i = 0; i < n; ++i) x[i] *= (float) std::exp(-(double) i / SR / decay);
    return x;
}
inline Signal scale(Signal x, double g) { for (auto& v : x) v = (float) (v * g); return x; }
inline Signal hh(Rng& r, double v, const std::string& kind)
{
    if (kind == "open") return scale(metal(r, (size_t) (0.9 * SR), 410, 0.35, 6000), v * 0.8);
    if (kind == "pedal") return scale(bp(metal(r, (size_t) (0.08 * SR), 410, 0.012, 300), 300, 2500), v * 0.7);
    return scale(metal(r, (size_t) (0.12 * SR), 410, 0.018, 7000), v * 0.8);
}
inline Signal tom(Rng& r, double v, double f0)
{
    const size_t n = (size_t) (0.6 * SR);
    Signal att = bp(noise(r, n), 800, 5000), out(n);
    double ph = 0;
    for (size_t i = 0; i < n; ++i) {
        const double t = (double) i / SR;
        ph += 2 * PI * f0 * (1 + 0.12 * std::exp(-t / 0.05)) / SR;
        out[i] = (float) ((std::sin(ph) * std::exp(-t / 0.22) + att[i] * std::exp(-t / 0.006) * 0.4) * v);
    }
    return out;
}
inline Signal crash(Rng& r, double v, bool bright) { return scale(metal(r, (size_t) (2.2 * SR), bright ? 520 : 300, 0.8, bright ? 5500 : 3000), v * 0.7); }
inline Signal ride(Rng& r, double v, bool bell)
{
    const size_t n = (size_t) (1.4 * SR);
    const double parts[4][2] = {{470, 1.0}, {1130, 0.7}, {2380, 0.5}, {3510, 0.35}};
    double ph[4];
    for (auto& p : ph) p = r.uni(0, 6.28);
    Signal wash = hp(noise(r, n), 4000), out(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = (double) i / SR;
        double ping = 0;
        for (int k = 0; k < 4; ++k) ping += parts[k][1] * std::sin(2 * PI * parts[k][0] * t + ph[k]);
        ping *= std::exp(-t / (bell ? 0.5 : 0.35));
        const double w = wash[i] * std::exp(-t / 0.4);
        out[i] = (float) ((bell ? 1.6 * ping + 0.05 * w : 0.25 * ping + 0.6 * w) * v * 0.6);
    }
    return out;
}

inline std::string stem_of(const std::string& p)
{
    if (p == "kick") return "kick";
    if (p == "snare" || p == "side_stick") return "snare";
    if (p.rfind("hh_", 0) == 0) return "hh";
    if (p.rfind("tom", 0) == 0 || p.rfind("floor", 0) == 0) return "toms";
    if (p.rfind("crash", 0) == 0) return "crash";
    return "ride";
}

struct Ev { double t; std::string piece; double v; double end = 0; };

inline std::vector<Ev> pattern(Rng& r, int bars = 24, double bpm = 96, double swing = 0.03, double jitter = 0.006)
{
    const double beat = 60.0 / bpm;
    std::vector<Ev> ev;
    double drift = 0;
    for (int bar = 0; bar < bars; ++bar) {
        drift += r.norm(0, 0.003);
        const double t0 = 0.5 + bar * 4 * beat + drift;
        const int section = (bar / 8) % 3;
        const bool fill = bar % 4 == 3;
        auto at = [&](int s) { return t0 + s * beat / 4 + (s % 2 ? swing * beat : 0.0) + r.norm(0, jitter); };
        if (bar % 2 == 0) ev.push_back({at(0), bar % 4 == 0 ? "crash1" : "crash2", r.uni(0.8, 1.0)});
        for (int s = 0; s < 16; ++s) {
            if (fill && s >= 8) continue;
            if (section == 0 && s % 2 == 0) ev.push_back({at(s), "hh_closed", s % 4 ? r.uni(0.35, 0.6) : r.uni(0.6, 0.8)});
            if (section == 1 && s % 2 == 0) ev.push_back({at(s), (s == 4 || s == 12) ? "ride_bell" : "ride", r.uni(0.5, 0.8)});
            if (section == 2 && s % 4 == 2) ev.push_back({at(s), "hh_open", r.uni(0.6, 0.8)});
            if (section == 2 && s % 4 == 0) ev.push_back({at(s), "hh_closed", r.uni(0.5, 0.7)});
            if (section == 1 && s % 4 == 0) ev.push_back({at(s) + r.norm(0, 0.003), "hh_pedal", r.uni(0.4, 0.6)});
        }
        const int k1[3] = {0, 6, 10}, k2[3] = {0, 7, 8};
        for (int i = 0; i < 3; ++i) {
            const int s = bar % 2 ? k1[i] : k2[i];
            if (!(fill && s >= 8)) ev.push_back({at(s), "kick", r.uni(0.75, 1.0)});
        }
        for (int s : {4, 12}) {
            if (fill && s >= 8) continue;
            ev.push_back({at(s), section == 2 ? "side_stick" : "snare", r.uni(0.8, 1.0)});
        }
        if (section == 0 && !fill) for (int s : {7, 14}) ev.push_back({at(s), "snare", r.uni(0.15, 0.25)});
        if (bar % 8 == 4) ev.push_back({at(12) - 0.022, "snare", 0.3});
        if (fill) {
            const char* toms[8] = {"tom_himid", "tom_himid", "tom_low", "tom_low", "floor_high", "floor_high", "snare", "kick"};
            for (int i = 0; i < 8; ++i) ev.push_back({at(8 + i), toms[i], r.uni(0.7, 1.0)});
        }
    }
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    return ev;
}

struct Render {
    std::map<std::string, Signal> stems;
    std::vector<Ev> truth;  // times rounded to the sample; end = where the sound fell 30 dB (or was choked / restruck)
    double duration;
};

inline Render render(unsigned seed = 1, double bleed_db = -26)
{
    Rng r(seed);
    auto ev = pattern(r);
    Render out;
    out.duration = ev.back().t + 3.0;
    const size_t n = (size_t) (out.duration * SR);
    for (auto s : {"kick", "snare", "toms", "hh", "ride", "crash"}) out.stems[s] = Signal(n, 0.f);
    for (size_t k = 0; k < ev.size(); ++k) {
        auto& e = ev[k];
        const std::string& p = e.piece;
        Signal s;
        if (p == "kick") s = kick(r, e.v);
        else if (p == "snare") s = snare(r, e.v);
        else if (p == "side_stick") s = side_stick(r, e.v);
        else if (p.rfind("hh_", 0) == 0) s = hh(r, e.v, p.substr(3));
        else if (p.rfind("tom", 0) == 0 || p.rfind("floor", 0) == 0) s = tom(r, e.v, p == "tom_himid" ? 196.0 : p == "tom_low" ? 131.0 : 87.0);
        else if (p.rfind("crash", 0) == 0) s = crash(r, e.v, p == "crash2");
        else s = ride(r, e.v, p == "ride_bell");
        // the next stroke of the same piece (any hi-hat stroke for a hi-hat) ends / chokes this one
        double next = 1e9;
        for (size_t j = k + 1; j < ev.size(); ++j)
            if (ev[j].t > e.t + 1e-4 && (ev[j].piece == p || (stem_of(p) == "hh" && stem_of(ev[j].piece) == "hh"))) { next = ev[j].t; break; }
        if (stem_of(p) == "hh" && next < 1e8) {
            const size_t m = (size_t) ((next - e.t) * SR), fade = (size_t) (0.004 * SR);
            for (size_t i = m; i < s.size(); ++i) s[i] *= i < m + fade ? (float) (1.0 - (double) (i - m) / fade) : 0.f;
        }
        // true length: 30 dB under the peak (3 ms RMS frames), or the next stroke
        double peak = -300, end = (double) s.size() / SR;
        const size_t fr = (size_t) (0.003 * SR);
        for (size_t a = 0; a + fr <= s.size(); a += fr) {
            double q = 0;
            for (size_t i = a; i < a + fr; ++i) q += (double) s[i] * s[i];
            const double l = 10 * std::log10(q / fr + 1e-20);
            peak = std::max(peak, l);
            if (a > 0.01 * SR && l < peak - 30) { end = (double) a / SR; break; }
        }
        const size_t a0 = (size_t) std::llround(e.t * SR);
        e.t = (double) a0 / SR;
        e.end = std::min(e.t + end, next);
        Signal& dst = out.stems[stem_of(p)];
        for (size_t i = 0; i < s.size() && a0 + i < n; ++i) dst[a0 + i] += s[i];
    }
    if (bleed_db > -200) {
        const auto clean = out.stems;
        const double g = std::pow(10.0, bleed_db / 20);
        for (auto& [k, x] : out.stems)
            for (auto& [o, y] : clean) {
                if (o == k) continue;
                Signal b = bp(y, 100, 12000);
                for (size_t i = 0; i < n; ++i) x[i] += (float) (g * b[i]);
            }
    }
    out.truth = ev;
    return out;
}
}  // namespace synth
