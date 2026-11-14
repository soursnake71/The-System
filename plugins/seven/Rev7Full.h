// Rev7Full: a standalone copy of the Omega REV7 engine (Yamaha REV7 model; tag omega-rev7, plugins/dsp/Rev7.h on
// top of the RackUnit / DLine / BQ / LPN helpers of plugins/dsp/Rack.h and Common.h), for use inside the synth.
//
// Same math in the same order as the original, all in double (delay lines store float, as the original's DLine does),
// the same 31.25 kHz sample-rate conversion, converters and analog filters, and the same RackUnit block logic
// (the unit sleeps and resets once its input has been silent for longer than tail(), counted per process() call).
// Panel controls outside the Param enum are fixed: input level 8 (unity), MONO on, mix 100 % wet, output 1.0,
// no mute / bypass / out-phase. Tempo 120 BPM (the REV7 engine does not use it).
//
// Differences from the plugin that come from JUCE / the host, not from the engine:
//  - parameter values here are plain doubles; in the plugin they pass through JUCE's float parameters (and their
//    step snapping), so e.g. 2.6 arrives as (double) 2.6f. Feed the same doubles to both and they agree bit for bit.
//  - the input meter (Rev7::level()) is not reproduced; it has no effect on the sound.
// Build without -ffast-math and without FMA contraction (-ffp-contract=off) to keep the rounding identical.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace seven::rev7 {

enum Param { PROG, RT, HI, LO, DIFF, IDLY, FDLY, FLEV, LIVE, MODE, SIZE, DLYL, DLYR, DLEV, FB, HDAMP, MFREQ, MDEP, MDLY, AMDEP,
             EQ, EQLF, EQLG, EQMF, EQMG, EQHF, EQHG, CIRC, PCOUNT };

static const char* const PROG_NAMES[30] = {
    "LARGE HALL", "SMALL HALL", "VOCAL PLATE", "PERCUSSION PLATE", "EARLY REF. 1", "EARLY REF. 2", "DELAY L,R", "STEREO ECHO",
    "STEREO FLANGE", "REVERB FLANGE", "CHORUS A", "CHORUS B", "STEREO PHASING", "TREMOLO", "SYMPHONIC", "REVERB ROOM",
    "ECHO ROOM", "STRINGS", "ELECTRIC BASS A", "ELECTRIC BASS B", "KICK", "SNARE", "GATE REVERB", "REVERSE GATE",
    "PIANO", "ORGAN", "BRASS", "GUITAR", "HANDCLAPS", "LIVE REFERENCE"};

struct ParamInfo { const char* key; const char* name; double min, max, def; };

static const ParamInfo PARAMS[PCOUNT] = {
    {"prog", "Program", 1, 30, 1},
    {"rt", "Rev time", .3, 10, 2.6},
    {"hi", "High RT ratio", .1, 1, .3},
    {"lo", "Low RT ratio", .1, 2.4, 1.2},
    {"diff", "Diffusion", 0, 10, 5},
    {"idly", "Initial delay", .1, 100, 30},
    {"fdly", "1st ref delay", .1, 100, 10},
    {"flev", "1st ref level", 0, 1, 0},
    {"live", "Liveness", 0, 10, 5},
    {"mode", "E/R mode", 1, 6, 1},
    {"size", "Room size", .1, 20, 2},
    {"dlyL", "L delay", .1, 900, 100},
    {"dlyR", "R delay", .1, 900, 200},
    {"dlev", "Delay level", 0, 1, 1},
    {"fb", "Feedback gain", 0, .99, .6},
    {"hdamp", "High damp", 0, 10, 5},
    {"mfreq", "Mod freq", .1, 20, 1.1},
    {"mdep", "Mod depth", 0, 1, .5},
    {"mdly", "Mod delay", .1, 100, 1.2},
    {"amdep", "AM depth", 0, 1, .4},
    {"eq", "Panel: EQ on", 0, 1, 0},
    {"eqlf", "EQ: Lo freq", 50, 700, 100},
    {"eqlg", "EQ: Lo level", -15, 15, 0},
    {"eqmf", "EQ: Mid freq", 350, 7000, 1000},
    {"eqmg", "EQ: Mid level", -15, 15, 0},
    {"eqhf", "EQ: Hi freq", 2000, 20000, 8000},
    {"eqhg", "EQ: Hi level", -15, 15, 0},
    {"circ", "Panel: Vintage converters", 0, 1, 1},
};

namespace detail {

constexpr double PI = 3.14159265358979323846;
inline double jround(double x) { return std::floor(x + .5); }   // JavaScript's Math.round

class DLine {
public:
    void init(int n) { int m = 1; while (m < n + 4) m <<= 1; b.assign((size_t) m, 0.f); mask = m - 1; w = 0; }
    void push(double x) { w = (w + 1) & mask; b[(size_t) w] = (float) x; }
    double at(int d) const { return b[(size_t) ((w - d) & mask)]; }
    double lerp(double d) const { const int i = (int) std::floor(d); const double f = d - i; const double a = b[(size_t) ((w - i) & mask)], c = b[(size_t) ((w - i - 1) & mask)]; return a + (c - a) * f; }
    void clear() { std::fill(b.begin(), b.end(), 0.f); }
private:
    std::vector<float> b; int mask = 0, w = 0;
};

struct BQ {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void keepState(const BQ& o) { z1 = o.z1; z2 = o.z2; }
    static BQ lp(double fs, double f, double q) { const double w = 2 * PI * std::min(f, fs * .45) / fs, al = std::sin(w) / (2 * q), cw = std::cos(w), a0 = 1 + al;
        BQ r; r.b0 = (1 - cw) / 2 / a0; r.b1 = (1 - cw) / a0; r.b2 = (1 - cw) / 2 / a0; r.a1 = -2 * cw / a0; r.a2 = (1 - al) / a0; return r; }
};

// n-pole low-pass. As in the original, init() keeps the biquads' state (it does not clear it).
class LPN {
public:
    void init(double fs_, double f, int n_, std::vector<double> qs_ = {}) { fs = fs_; n = n_; qs = qs_.empty() ? std::vector<double> {.54, .65, 1.3} : qs_; cur = -1; set(f); }
    void set(double f) {
        if (f == cur) return;
        cur = f; std::vector<BQ> nb;
        for (int k = 0; k < n / 2; ++k) { BQ q = BQ::lp(fs, f, k < (int) qs.size() ? qs[(size_t) k] : .707); if (k < (int) b.size()) q.keepState(b[(size_t) k]); nb.push_back(q); }
        b = nb; a = n % 2 ? 1 - std::exp(-2 * PI * std::min(f, fs * .45) / fs) : 0;
    }
    double run(double x) { for (auto& q : b) x = q.run(x); if (a != 0) { y += a * (x - y); x = y; } return x; }
private:
    double fs = 48000, cur = -1, a = 0, y = 0; int n = 2; std::vector<double> qs; std::vector<BQ> b;
};

inline double tailOf(double ms, double fb) {
    return std::min(20.0, ms / 1000 * (fb > .01 ? std::log(1e-4) / std::log(std::min(.98, std::fabs(fb))) : 1) + .3);
}

}  // namespace detail

class Rev7Full {
public:
    static constexpr double FS = 31250;   // the REV7's sampling rate (4 MHz ceramic resonator / 128)

    Rev7Full() { for (int k = 0; k < PCOUNT; ++k) p[k] = PARAMS[k].def; }

    // allocate for this rate and reset (RackUnit::prepare)
    void prepare(double hostRate) { fs = hostRate; build(); cfg(); update(); reset(); quiet = 1e12; asleep = true; ready = true; }

    // Rev7::reset (the LFO phases are not reset, and the 12 kHz LC filters keep their state, as in the original)
    void reset() {
        line.clear(); for (auto& d : loop) d.clear(); for (auto& d : mod) d.clear(); for (auto& d : fdn) d.clear(); for (auto& d : inAp) d.clear();
        for (auto& c : erAp) { for (auto& d : c) d.clear(); }
        for (auto& d : outAp) d.clear();
        aaf.init(fs, 12000, 6, {.518, .707, 1.932}); for (auto& f : rcf) f.init(fs, 12000, 6, {.518, .707, 1.932});
        pre.clear(); for (auto& s : de) s.clear(); for (auto& b : eqb) b.z1 = b.z2 = 0; for (auto& b : aperture) b.z1 = b.z2 = 0;
        for (auto& b : bands) { b = {}; }
        for (double& d : damp) d = 0;
        bwz = 0; acc = 0; xPrev = 0; y0[0] = y0[1] = y1[0] = y1[1] = 0;
    }

    // every parameter from that factory preset (Rev7.cpp makeSpec), the defaults for the rest, then apply
    void loadFactory(int prog) {
        if (prog < 1 || prog > 30) return;
        for (int k = 0; k < PCOUNT; ++k) p[k] = PARAMS[k].def;
        p[PROG] = prog;
        auto rev = [&](double rt, double id, double hi, double lo, double df, double fd = .1, double fl = 0) {
            p[RT] = rt; p[IDLY] = id; p[FDLY] = fd; p[FLEV] = fl; p[HI] = hi; p[LO] = lo; p[DIFF] = df; };
        auto er = [&](double live, double id, double fl, int mode, double size, double df) {
            p[LIVE] = live; p[IDLY] = id; p[FDLY] = .1; p[FLEV] = fl; p[MODE] = (double) mode; p[SIZE] = size; p[DIFF] = df; };
        switch (prog) {
            case 1: rev(2.6, 30, .3, 1.2, 5, 10); break;
            case 2: rev(2.0, 20, .4, 1.0, 5, 10); break;
            case 3: rev(2.4, 45, .3, 1.0, 5, 10); break;
            case 4: rev(2.0, 10, .5, 1.2, 5, 10); break;
            case 5: er(5, 10, 0, 1, 2.0, 5); break;
            case 6: er(5, 10, 0, 1, 2.0, 5); break;
            case 7: p[IDLY] = .1; p[FDLY] = .1; p[DLYL] = 100; p[DLYR] = 200; p[DLEV] = 1; break;
            case 8: p[FB] = .6; p[IDLY] = .1; p[DLYL] = 170; p[DLYR] = 178; p[HDAMP] = 9; break;
            case 9: p[FB] = .35; p[IDLY] = .1; p[MFREQ] = 2.5; p[MDEP] = .5; p[MDLY] = 1.2; break;
            case 10: p[RT] = 2.5; p[IDLY] = .1; p[MFREQ] = 1.1; p[MDEP] = .8; p[MDLY] = 1.2; break;
            case 11: p[IDLY] = .1; p[MFREQ] = .2; p[MDEP] = .5; p[AMDEP] = .4; break;
            case 12: p[IDLY] = .1; p[MFREQ] = .6; p[MDEP] = .5; p[AMDEP] = .1; break;
            case 13: p[IDLY] = .1; p[MFREQ] = 1.1; p[MDEP] = .5; p[MDLY] = 1.0; break;
            case 14: p[IDLY] = .1; p[MFREQ] = 5.0; p[MDEP] = .5; break;
            case 15: p[IDLY] = .1; p[MFREQ] = .6; p[MDEP] = .5; break;
            case 16: rev(1.6, 25, .2, 1.0, 5); break;
            case 17: rev(1.8, 16, .3, 1.0, 5); break;
            case 18: rev(2.2, 13, .3, 1.0, 5); break;
            case 19: er(5, 12, .65, 5, 2.0, 5); break;
            case 20: er(3, 12, .68, 5, 2.0, 5); break;
            case 21: er(1, 12, .87, 5, 2.0, 5); break;
            case 22: rev(1.2, 10, .8, 1.0, 5); break;
            case 23: er(5, 20, 0, 3, 1.6, 5); break;
            case 24: er(5, 25, 0, 4, 2.4, 5); break;
            case 25: er(3, 12, .72, 6, 2.0, 5); break;
            case 26: er(4, 10, .61, 1, 2.0, 7); break;
            case 27: er(4, 12, .61, 3, .9, 5); break;
            case 28: er(5, 5, .69, 6, 1.5, 5); break;
            case 29: rev(.4, .1, .1, 2.0, 5); break;
            case 30: er(5, 20, 0, 2, 2.5, 5); break;
        }
        changed();
    }

    // one parameter, clamped to its range, then applied (RackUnit::changed)
    void set(int k, double x) {
        if (k < 0 || k >= PCOUNT) return;
        p[k] = std::min(PARAMS[k].max, std::max(PARAMS[k].min, x));
        changed();
    }
    double get(int k) const { return k >= 0 && k < PCOUNT ? p[k] : 0.0; }

    // Rev7::process with MONO on, then RackUnit::process with mix 1 (wet only); in may be null (silence)
    void process(const float* in, float* outL, float* outR, int N) {
        const float* a = in; const float* b = in;   // MONO: the left input feeds both sides
        bool any = false; if (a) for (int i = 0; i < N; ++i) if (a[i] != 0 || b[i] != 0) { any = true; break; }
        if (any) { quiet = 0; if (asleep) { asleep = false; update(); } } else quiet += N;
        const double mix = 1.0;
        if (quiet > tail() * fs) {
            if (!asleep) { asleep = true; reset(); }
            for (int i = 0; i < N; ++i) { outL[i] = a ? (float) (a[i] * (1 - mix)) : 0.f; outR[i] = a ? (float) (b[i] * (1 - mix)) : 0.f; }
            return;
        }
        run(a, b, outL, outR, N);
    }

    double tail() const {
        double t = .3 + p[IDLY] / 1000;
        switch (P.kind) {
            case REV: case RFLANGE: t += p[RT] * std::max(1.0, p[LO]) * 1.15; break;
            case ER: t += .07 * p[SIZE] + .3; break;
            case DLY: t += std::max(p[DLYL], p[DLYR]) / 1000; break;
            case ECHO: t += detail::tailOf(std::max(p[DLYL], p[DLYR]), p[FB]); break;
            case FLANGE: t += detail::tailOf(p[MDLY] + 4, p[FB]); break;
            default: t += .2; break;
        }
        return std::min(30.0, t + p[FDLY] / 1000);
    }

private:
    using DLine = detail::DLine; using BQ = detail::BQ; using LPN = detail::LPN;
    static constexpr double PI = detail::PI;
    static double jround(double x) { return detail::jround(x); }

    // fixed panel values: input 8 (unity), output 1.0, out phase normal
    static constexpr double PANEL_IN = 8, PANEL_RET = 1, PANEL_PHASE = 0;

    enum Kind { REV, ER, DLY, ECHO, FLANGE, RFLANGE, CHORUS, PHASE, TREM, SYMPH };
    struct Prog { Kind kind; double scale, bw, aux; };
    static const Prog& prog(int n) {
        static const Prog T[31] = {
            {REV, 1, 9000, 0},
            {REV, 1.0, 9000, 0},  {REV, .62, 9500, 0}, {REV, .5, 11000, 1}, {REV, .42, 11500, 1},
            {ER, 1, 12000, 0}, {ER, 1, 12000, 1},
            {DLY, 1, 12000, 0}, {ECHO, 1, 12000, 0},
            {FLANGE, 1, 12000, 0}, {RFLANGE, .8, 10000, 0}, {CHORUS, 1, 12000, 0}, {CHORUS, 1, 12000, 1},
            {PHASE, 1, 12000, 0}, {TREM, 1, 12000, 0}, {SYMPH, 1, 12000, 0},
            {REV, .55, 10000, 0}, {REV, .6, 8500, 0}, {REV, .9, 10500, 0},
            {ER, 1, 9000, 2}, {ER, 1, 8000, 3}, {ER, 1, 7000, 4},
            {REV, .45, 11500, 1},
            {ER, 1, 12000, 5}, {ER, 1, 12000, 6}, {ER, 1, 11000, 7}, {ER, 1, 10000, 8}, {ER, 1, 11000, 9}, {ER, 1, 11500, 10},
            {REV, .4, 12000, 1}, {ER, 1, 12000, 11},
        };
        return T[n < 1 || n > 30 ? 0 : n];
    }

    void changed() { if (!ready) return; cfg(); update(); }

    // ---- analog section, at the host rate ----
    struct Shelf {
        double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
        void set(double fs, double fz, double fp) {
            const double k = 2 * fs, wz = 2 * PI * fz, wp = 2 * PI * fp;
            const double n0 = 1 + k / wz, n1 = 1 - k / wz, d0 = 1 + k / wp, d1 = 1 - k / wp;
            b0 = n0 / d0; b1 = n1 / d0; a1 = d1 / d0;
        }
        double run(double x) { const double y = b0 * x + b1 * x1 - a1 * y1; x1 = x; y1 = y; return y; }
        void clear() { x1 = y1 = 0; }
    };
    static BQ peak(double fs, double f, double q, double db) {
        const double A = std::pow(10, db / 40), w = 2 * PI * std::min(f, fs * .45) / fs, al = std::sin(w) / (2 * q), cw = std::cos(w), a0 = 1 + al / A;
        BQ r; r.b0 = (1 + al * A) / a0; r.b1 = -2 * cw / a0; r.b2 = (1 - al * A) / a0; r.a1 = -2 * cw / a0; r.a2 = (1 - al / A) / a0; return r;
    }

    void build() {
        line.init((int) (FS * 2.6));
        for (int c = 0; c < 2; ++c) { loop[c].init((int) (FS * .5)); mod[c].init((int) (FS * .06)); erAp[c][0].init(160); erAp[c][1].init(160); outAp[c].init(400); }
        for (int k = 0; k < 4; ++k) inAp[k].init(400);
        for (int k = 0; k < 8; ++k) fdn[k].init(4096);
        for (LPN* f : {&aaf, &rcf[0], &rcf[1]}) f->init(fs, 12000, 6, {.518, .707, 1.932});
        pre.set(fs, 3200, 12000); de[0].set(fs, 12000, 3200); de[1].set(fs, 12000, 3200);
        aperture[0] = aperture[1] = peak(fs, 11000, .7, 2.2);
        buildPatterns();
    }
    void cfg() {
        num = (int) p[PROG]; P = prog(num);
        rt = p[RT]; hiR = p[HI]; loR = p[LO]; diff = p[DIFF]; live = p[LIVE]; mode = std::max(1, std::min(6, (int) p[MODE])); size = p[SIZE];
        circuit = p[CIRC] >= .5;
    }
    void update() {
        eqOn = p[EQ] >= .5;
        const int f[3] = {EQLF, EQMF, EQHF}; const int g[3] = {EQLG, EQMG, EQHG};
        for (int k = 0; k < 3; ++k) { BQ n = peak(fs, p[f[k]], k == 1 ? 1.0 : .8, p[g[k]]); n.keepState(eqb[k]); eqb[k] = n; }
        const double inPos = PANEL_IN; inGain = inPos <= 0 ? 0 : std::pow(10, (inPos - 8) * 6 / 20);
        bwA = 1 - std::exp(-2 * PI * P.bw / FS);
        static const int base[8] = {1061, 1277, 1489, 1667, 1847, 2011, 2203, 2399};
        for (int k = 0; k < 8; ++k) {
            len[k] = std::max(40, (int) jround(base[k] * P.scale));
            auto gf = [&](double t) { return std::pow(10, -3 * len[k] / (std::max(.05, t) * FS)); };
            gM[k] = gf(rt); gH[k] = gf(rt * hiR); gL[k] = std::min(.9995, gf(rt * loR));
        }
        lo500 = 1 - std::exp(-2 * PI * 500 / FS); lo4k = 1 - std::exp(-2 * PI * 4000 / FS);
        const double plate = P.aux > .5 && P.kind == REV ? .08 : 0;
        apG = diff <= 0 ? .1 : .34 + .041 * diff + plate;
        if (P.kind == REV) setRevER(); else if (P.kind == ER) setER();
        const double hd = p[HDAMP]; dampA = 1 - std::exp(-2 * PI * (12000 / (1 + .35 * hd)) / FS);
    }

    // ---- the reflection patterns ----
    struct Pat { double t[18], g[18]; };
    Pat pat[6][2] {};
    void buildPatterns() {
        for (int m = 0; m < 6; ++m) for (int c = 0; c < 2; ++c) {
            uint32_t s = 0x5eed + (uint32_t) (m * 977 + c * 131);
            auto rnd = [&] { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0; };
            Pat& pt = pat[m][c];
            for (int k = 0; k < 18; ++k) {
                const double x = (k + .25 + .5 * rnd()) / 18; double t = 0;
                switch (m) {
                    case 0: t = 1.5 + 28 * std::pow(x, 1.3); break;
                    case 1: t = 3 + 58 * std::pow(x, 1.15); break;
                    case 2: t = 1 + 44 * rnd(); break;
                    case 3: t = 2 + 48 * x; break;
                    case 4: t = .7 + 17 * x; break;
                    case 5: t = 4.1 * (k / 3 + 1) * (1 + .035 * c) + (k % 3) * .37 + .15 * rnd(); break;
                }
                // the original writes (rnd() < .5 ? -1 : 1) * (.75 + .25 * rnd()); the sign draw comes first
                const double sign = rnd() < .5 ? -1 : 1; const double mag = .75 + .25 * rnd();
                pt.t[k] = t; pt.g[k] = sign * mag;
            }
            for (int i = 0; i < 18; ++i) for (int j = i + 1; j < 18; ++j) if (pt.t[j] < pt.t[i]) { std::swap(pt.t[i], pt.t[j]); std::swap(pt.g[i], pt.g[j]); }
        }
    }
    int nTap = 0; int tapD[2][18] {}; double tapG[2][18] {};
    void setER() {
        const int m = mode - 1; const double a = (10 - live) * .55 + .3;
        const double var = 1 + .06 * std::sin(P.aux * 1.7);
        for (int c = 0; c < 2; ++c) {
            double ss = 0;
            for (int k = 0; k < 18; ++k) {
                const double x = k / 17.0; double env;
                if (m == 3) env = std::pow(x + 1.0 / 18, .5 + (10 - live) * .2);
                else if (m == 2) env = std::exp(-x * a * .35);
                else env = std::exp(-x * a);
                tapD[c][k] = std::max(1, (int) jround(pat[m][c].t[k] * var * size * FS / 1000));
                tapG[c][k] = pat[m][c].g[k] * env; ss += tapG[c][k] * tapG[c][k];
            }
            const double nrm = .75 / std::sqrt(std::max(1e-9, ss)); for (double& gk : tapG[c]) gk *= nrm;
        }
        nTap = 18;
    }
    void setRevER() {
        static const double tL[3] = {7.1, 13.9, 22.7}, tR[3] = {9.3, 17.1, 26.3}, gg[3] = {.42, -.33, .26};
        for (int k = 0; k < 3; ++k) {
            tapD[0][k] = (int) jround(tL[k] * P.scale * FS / 1000); tapD[1][k] = (int) jround(tR[k] * P.scale * FS / 1000);
            tapG[0][k] = gg[k]; tapG[1][k] = -gg[k] * .9;
        }
        nTap = 3;
    }

    // ---- the DSP, one sample at 31.25 kHz ----
    double ap(DLine& d, int n, double g, double x) { const double vv = d.at(n); const double w = x + g * vv; d.push(w); return vv - g * w; }
    struct Bands { double l = 0, h = 0; };
    Bands bands[8]; double damp[2] {}, bwz = 0, lfo = 0, lfo2 = 0, lfo3 = 0, tank = 0;
    void reverb(double x, double& oL, double& oR) {
        bwz += bwA * (x - bwz); double d = bwz;
        static const int an[4] = {89, 67, 233, 173};
        for (int k = 0; k < 4; ++k) d = ap(inAp[k], (int) jround(an[k] * (.6 + .4 * P.scale)), apG, d);
        tank += 2 * PI * .73 / FS; if (tank > 2 * PI) tank -= 2 * PI;
        double r[8];
        for (int k = 0; k < 8; ++k) r[k] = (k == 0 || k == 5) ? fdn[k].lerp(len[k] - 1 + 5 * std::sin(tank + k)) : fdn[k].at(len[k]);
        double sum = 0; for (double q : r) sum += q; sum *= .25;
        for (int k = 0; k < 8; ++k) {
            double y = r[k] - sum + (k & 1 ? -d : d) * .5;
            Bands& b = bands[k]; b.l += lo500 * (y - b.l); b.h += lo4k * (y - b.h);
            const double lo = b.l, mid = b.h - b.l, hi = y - b.h;
            fdn[k].push(gL[k] * lo + gM[k] * mid + gH[k] * hi);
        }
        oL = .42 * (r[0] - r[2] + r[4] + r[6] * .8);
        oR = .42 * (r[1] + r[3] - r[5] + r[7] * .8);
        const int oa = (int) jround(311 * (.6 + .4 * P.scale)), ob = (int) jround(257 * (.6 + .4 * P.scale));
        oL = ap(outAp[0], oa, apG * .7, oL); oR = ap(outAp[1], ob, apG * .7, oR);
    }
    double lfoStep(double& ph, double hz) { ph += 2 * PI * hz / FS; if (ph > 2 * PI) ph -= 2 * PI; return ph; }
    void tick(double x, double& oL, double& oR) {
        line.push(x);
        const int id = std::max(1, (int) jround(p[IDLY] / 1000 * FS));
        const double xi = line.at(id);
        oL = oR = 0;
        const double mf = p[MFREQ], md = p[MDEP] * .004 * FS;
        switch (P.kind) {
            case REV: {
                for (int k = 0; k < nTap; ++k) { oL += line.at(id + tapD[0][k]) * tapG[0][k]; oR += line.at(id + tapD[1][k]) * tapG[1][k]; }
                double l, r; reverb(line.at(id + (int) jround(.012 * P.scale * FS)), l, r); oL += l; oR += r; break;
            }
            case ER: {
                for (int k = 0; k < nTap; ++k) { oL += line.at(id + tapD[0][k]) * tapG[0][k]; oR += line.at(id + tapD[1][k]) * tapG[1][k]; }
                const double g = diff * .068;
                oL = ap(erAp[0][1], 79, g, ap(erAp[0][0], 113, g, oL)); oR = ap(erAp[1][1], 67, g, ap(erAp[1][0], 97, g, oR));
                break;
            }
            case DLY: {
                const double lev = p[DLEV];
                oL = line.at(id + std::max(1, (int) jround(p[DLYL] / 1000 * FS))) * lev;
                oR = line.at(id + std::max(1, (int) jround(p[DLYR] / 1000 * FS))) * lev;
                break;
            }
            case ECHO: {
                const double fb = p[FB]; const int dk[2] = {DLYL, DLYR}; double* o[2] = {&oL, &oR};
                for (int c = 0; c < 2; ++c) {
                    const int n = std::max(1, (int) jround(std::min(450.0, p[dk[c]]) / 1000 * FS));
                    const double y = loop[c].at(n); damp[c] += dampA * (y - damp[c]);
                    loop[c].push(xi + fb * damp[c]); *o[c] = y;
                }
                break;
            }
            case FLANGE: case RFLANGE: {
                double sL = xi, sR = xi;
                if (P.kind == RFLANGE) { reverb(xi, sL, sR); }
                const double ph = lfoStep(lfo, mf), fb = P.kind == RFLANGE ? .3 : p[FB];
                const double base = p[MDLY] / 1000 * FS;
                const double dL = std::max(1.0, base + md * std::sin(ph)), dR = std::max(1.0, base - md * std::sin(ph));
                const double yL = mod[0].lerp(dL), yR = mod[1].lerp(dR);
                mod[0].push(sL + fb * yL); mod[1].push(sR + fb * yR);
                oL = .7 * (sL + yL); oR = .7 * (sR + yR); break;
            }
            case CHORUS: {
                const double ph = lfoStep(lfo, mf), am = p[AMDEP], base = (P.aux > .5 ? 9.0 : 12.5) / 1000 * FS;
                mod[0].push(xi);
                const double yL = mod[0].lerp(base + md * std::sin(ph)), yR = mod[0].lerp(base + md * std::sin(ph + PI));
                const double gL2 = 1 - am * .5 * (1 + std::sin(ph + PI / 2)), gR2 = 1 - am * .5 * (1 + std::sin(ph - PI / 2));
                oL = .7 * (xi + yL * gL2); oR = .7 * (xi + yR * gR2); break;
            }
            case PHASE: {
                const double ph = lfoStep(lfo, mf), base = std::min(8.0, p[MDLY]) / 1000 * FS;
                mod[0].push(xi);
                const double fixed = mod[0].lerp(std::max(.5, base)), centre = mod[0].lerp(std::max(.5, base + md * std::sin(ph)));
                oL = .5 * (fixed + centre) + .35 * xi; oR = .5 * (centre - fixed) + .35 * xi; break;
            }
            case TREM: {
                const double ph = lfoStep(lfo, mf); mod[0].push(xi);
                oL = oR = mod[0].lerp(.0055 * FS + md * std::sin(ph)); break;
            }
            case SYMPH: {
                const double p1 = lfoStep(lfo, mf), p2 = lfoStep(lfo2, mf * 1.37), p3 = lfoStep(lfo3, mf * .71);
                mod[0].push(xi);
                const double a = mod[0].lerp(.007 * FS + md * std::sin(p1)), b = mod[0].lerp(.011 * FS + md * std::sin(p2 + 2)), c = mod[0].lerp(.015 * FS + md * std::sin(p3 + 4));
                oL = .55 * (xi + a - .6 * b + .7 * c); oR = .55 * (xi - .6 * a + b + .7 * c); break;
            }
        }
        const double fl = p[FLEV];
        if (fl > 0) { const double r = line.at(std::max(1, (int) jround(p[FDLY] / 1000 * FS))) * fl; oL += r; oR += r; }
    }

    // ---- host-rate wrapper: the converters ----
    static double q16(double x) { x = x > 1 ? 1 : x < -1 ? -1 : x; return jround(x * 32767) / 32767; }
    static double in(const float* a, const float* b, int i) { return a ? (a[i] + b[i]) * .5 : 0; }
    void run(const float* a, const float* b, float* L, float* R, int N) {
        const double ratio = FS / fs, ret = PANEL_RET * (PANEL_PHASE >= .5 ? -1 : 1), adcScale = .7;
        for (int i = 0; i < N; ++i) {
            double x = in(a, b, i) * inGain;
            if (eqOn) for (auto& q : eqb) x = q.run(x);
            if (circuit) x = pre.run(aaf.run(x));
            const double before = acc; acc += ratio;
            for (double k = std::floor(before) + 1; k <= acc; k += 1) {
                const double f = (k - before) / ratio;
                double s = (xPrev + (x - xPrev) * f) * adcScale;
                if (circuit) s = q16(s);
                double l, r; tick(s, l, r);
                if (circuit) { l = q16(l); r = q16(r); }
                y0[0] = y1[0]; y0[1] = y1[1]; y1[0] = l / adcScale; y1[1] = r / adcScale;
            }
            if (acc >= 1 << 20) acc -= 1 << 20;
            xPrev = x;
            const double fr = acc - std::floor(acc);
            double oL = y0[0] + (y1[0] - y0[0]) * fr, oR = y0[1] + (y1[1] - y0[1]) * fr;
            if (circuit) { oL = aperture[0].run(de[0].run(rcf[0].run(oL))); oR = aperture[1].run(de[1].run(rcf[1].run(oR))); }
            L[i] = (float) (oL * ret); R[i] = (float) (oR * ret);
        }
    }

    double p[PCOUNT];
    double fs = 48000, quiet = 1e12; bool asleep = true, ready = false;
    Prog P = prog(1); int num = 1, mode = 1;
    double rt = 2.6, hiR = .3, loR = 1.2, diff = 5, live = 5, size = 2;
    bool circuit = true, eqOn = false;
    double inGain = 1, bwA = .8, apG = .55, lo500 = .1, lo4k = .5, dampA = .5;
    int len[8] {}; double gM[8] {}, gH[8] {}, gL[8] {};
    DLine line, loop[2], mod[2], fdn[8], inAp[4], erAp[2][2], outAp[2];
    LPN aaf, rcf[2]; Shelf pre, de[2]; BQ eqb[3], aperture[2];
    double acc = 0, xPrev = 0, y0[2] {}, y1[2] {};
};

}  // namespace seven::rev7
