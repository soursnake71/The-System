// Omega Twelve sound engine: a line-for-line C++ port of twelve/engine.js (plain C++, no JUCE).
// SP-1200 audio path after libmd12 (MIT, github.com/Mudb0y/libmd12): 26,041.67 Hz frame clock (20 MHz / 768),
// 7th-order input anti-alias filter, truncating 12-bit converter, drop/repeat pitch with a 7-bit fraction counter and
// E-mu's semitone table, outputs 1-2 through an SSM2044 4-pole dynamic filter, 3-6 through fixed filters, 7-8 unfiltered.
// twelve/tools/test.cpp renders the same signal here and in engine.js and checks they match.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "SP1200.h"

namespace twelve {

using Buf = std::vector<float>;
using Chans = std::vector<Buf>;
static constexpr double PI = 3.14159265358979323846;

struct ParamInfo { const char* id; const char* name; double min, max, def, step; const char* unit; const char* group; bool log, toggle; std::vector<const char*> choices; };
#include "Params.inc"
struct PresetInfo { const char* name; const char* family; double v[kSound]; };
#include "Presets.inc"

struct Params {
    double v[pid::count];
    Params() { for (int i = 0; i < pid::count; ++i) v[i] = PARAMS[i].def; }
    double operator[](int i) const { return v[i]; }
    double& operator[](int i) { return v[i]; }
};

static const double SP_SR = 20e6 / 768;
static const int SP_TUNE[32] = {50,53,56,59,63,67,71,75,80,84,90,95,101,107,113,120,127,135,143,151,160,170,180,191,202,214,227,241,255,255,255,255};
struct PZ { double re, im; };
struct Proto { std::vector<PZ> poles, zeros; };
inline const Proto& proto(int which) {   // 0 input, 3..6 outputs
    static const Proto input {{{-3.1645569620e4,0},{-2.3809523810e4,4.6628955649e4},{-1.1574074074e4,7.4291800562e4},{-3.0864197531e3,8.2586975770e4},{-1.9405594406e5,0}},
                              {{1.4005991967e1,1.0385249071e5},{2.2080491445e1,1.2148212766e5},{-7.1836348348e1,1.4125368878e5}}};
    static const Proto o3 {{{-4.6549557989e4,0},{-1.6129032258e4,4.8387096774e4},{-4.2413850609e4,3.0520415770e4}}, {}};
    static const Proto o4 {{{-5.5372960884e4,0},{-1.9607843137e4,5.8823529412e4},{-5.1403645657e4,3.7527098089e4}}, {}};
    static const Proto o5 {{{-7.3190092409e4,0},{-2.3255813953e4,6.9767441860e4},{-6.1051842833e4,4.2379116726e4}}, {}};
    static const Proto o6 {{{-6.0667642111e4,4.8266457976e4},{-2.5641025641e4,7.6923076923e4},{-9.5952540933e4,0}}, {}};
    switch (which) { case 3: return o3; case 4: return o4; case 5: return o5; case 6: return o6; default: return input; }
}
struct Machine { const char* name; double rate; int bits, pitch, ftype; double cutoff, res, lo, hi; };
static const Machine MACHINES[18] = {
    {"SP-1200", SP_SR, 12, 0, 0, 1000, .3, 4000, SP_SR}, {"SP-12", 27500, 12, 0, 0, 1300, .3, 4000, 27500}, {"MPC60", 40000, 12, 2, 5, 16000, 0, 4000, 40000},
    {"S950", 31250, 12, 1, 6, 11000, 0, 7500, 48000}, {"S612", 32000, 12, 1, 7, 9000, .1, 4000, 32000}, {"Mirage", 29411, 8, 1, 12, 7000, .25, 10000, 33000},
    {"Clean", 48000, 16, 3, 5, 20000, 0, 4000, 52100}, {"MPC3000", 44100, 16, 2, 8, 18000, 0, 4000, 44100}, {"S1000", 44100, 16, 3, 9, 18000, 0, 22050, 44100},
    {"RZ-1", 20000, 8, 1, 5, 20000, 0, 4000, 20000}, {"SK-1", 9380, 8, 1, 5, 20000, 0, 4000, 9380}, {"Emax", 27778, 12, 1, 10, 12000, .1, 10000, 42000},
    {"EPS", 31250, 13, 2, 11, 16000, 0, 6250, 52100}, {"EPS 16+", 44600, 16, 2, 11, 18000, 0, 11200, 44600}, {"DSS-1", 48000, 12, 1, 13, 12000, .15, 16000, 48000},
    {"S-550", 30000, 12, 2, 14, 10000, 0, 15000, 30000}, {"SU700", 44100, 16, 2, 15, 18000, 0, 11025, 44100}, {"TX16W", 33333, 12, 1, 5, 20000, 0, 16700, 50000}};
static const double FILTER_RANGE[16][2] = {{250, 14000}, {20, 20000}, {20, 20000}, {20, 20000}, {20, 20000}, {20, 20000}, {350, 16000}, {50, 16000},
    {70, 20000}, {20, 20000}, {40, 16000}, {20, 20000}, {50, 14000}, {30, 16000}, {100, 10000}, {20, 20000}};
static const double MATCH_BANDS[10] = {31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
static const double TARGET_CURVE[10] = {-6, -2, 0, 0, -1, -2, -3, -4.5, -7, -12};
static const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline double dbToGain(double d) { return std::pow(10.0, d / 20); }
inline bool isMaster(int id) { return std::string(PARAMS[id].group) == "master"; }

// same generator as engine.js (mulberry32)
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed) {}
    double operator()() {
        s += 0x6D2B79F5u; uint32_t t = s;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return (double) (t ^ (t >> 14)) / 4294967296.0;
    }
};

/* ---------------- filters ---------------- */
struct Bq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
enum class Rbj { lp, hp, peak, ls, hs };
inline Bq rbj(Rbj type, double fs, double f, double q, double gdb = 0) {
    const double w = 2 * PI * clampd(f, 1, fs * .49) / fs, cw = std::cos(w), sw = std::sin(w), al = sw / (2 * q), A = std::pow(10.0, gdb / 40);
    double b0, b1, b2, a0, a1, a2;
    if (type == Rbj::lp) { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
    else if (type == Rbj::hp) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
    else if (type == Rbj::peak) { b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
    else if (type == Rbj::ls) { const double s = 2 * std::sqrt(A) * al; b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s); a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s; }
    else { const double s = 2 * std::sqrt(A) * al; b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s); a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s; }
    Bq r; r.b0 = b0 / a0; r.b1 = b1 / a0; r.b2 = b2 / a0; r.a1 = a1 / a0; r.a2 = a2 / a0; return r;
}
using Chain = std::vector<Bq>;
inline double chainRun(Chain& c, double x) { for (auto& f : c) x = f.run(x); return x; }
// analog poles/zeros (rad/s), optionally scaled, to digital sections (bilinear, pre-warped, unity gain at DC)
inline Chain sDesign(const Proto& pr, double fs, double scale = 1) {
    const double K = 2 * fs;
    auto warp = [&](PZ p) { p.re *= scale; p.im *= scale; const double w = std::hypot(p.re, p.im); if (w > .9 * PI * fs) return p; const double k = K * std::tan(w / K) / w; return PZ{p.re * k, p.im * k}; };
    auto mag = [](const PZ& p) { return std::hypot(p.re, p.im); };
    auto quad = [&](double c1, double c0) { return std::array<double, 3>{K * K + c1 * K + c0, 2 * c0 - 2 * K * K, K * K - c1 * K + c0}; };
    std::vector<PZ> zs, ps;
    for (auto z : pr.zeros) zs.push_back(warp(z));
    for (auto p : pr.poles) ps.push_back(warp(p));
    std::stable_sort(zs.begin(), zs.end(), [&](const PZ& a, const PZ& b) { return mag(a) < mag(b); });
    std::stable_sort(ps.begin(), ps.end(), [&](const PZ& a, const PZ& b) { return mag(a) < mag(b); });
    size_t zi = 0; Chain secs;
    for (auto p : ps) {
        std::array<double, 3> a, b;
        if (p.im > 0) { a = quad(-2 * p.re, p.re * p.re + p.im * p.im); if (zi < zs.size()) { const auto z = zs[zi++]; b = quad(-2 * z.re, z.re * z.re + z.im * z.im); } else b = {1, 2, 1}; }
        else { a = {K - p.re, -p.re - K, 0}; b = {1, 1, 0}; }
        const double dc = (b[0] + b[1] + b[2]) / (a[0] + a[1] + a[2]);
        Bq s; s.b0 = b[0] / dc / a[0]; s.b1 = b[1] / dc / a[0]; s.b2 = b[2] / dc / a[0]; s.a1 = a[1] / a[0]; s.a2 = a[2] / a[0]; secs.push_back(s);
    }
    return secs;
}

/* ---------------- converter ---------------- */
inline double quantize(double v, double bits) {
    const double q = std::pow(2.0, bits - 1);
    return clampd(std::floor(v * q), -q, q - 1) / q;
}
inline Chain inputFilter(double rate, double fs) { return sDesign(proto(0), fs, rate / SP_SR); }
inline double driveGain(const Params& p) { return dbToGain(p[pid::input]) * (1 + p[pid::drive] * 1.5); }
// the input preamp: clean up to its knee, then a soft clip that comes in a little earlier on the negative side
inline double preamp(double x) {
    const double a = std::fabs(x), k = x >= 0 ? .9 : .85; if (a <= k) return x;
    const double h = 1 - k; return (x < 0 ? -1 : 1) * (k + h * std::tanh((a - k) / h));
}
// what the machine does with the knobs: its rate range, the filter's cutoff range, the filter switch
inline Params effective(const Params& p) {
    const Machine& M = MACHINES[(int) clampd(p[pid::machine], 0, 17)]; Params q = p;
    q[pid::rate] = clampd(p[pid::rate], M.lo, M.hi);
    if (p[pid::fOn] < .5) q[pid::ftype] = 5;
    const int ft = (int) clampd(q[pid::ftype], 0, 15); q[pid::cutoff] = clampd(p[pid::cutoff], FILTER_RANGE[ft][0], FILTER_RANGE[ft][1]);
    return q;
}
inline Buf preamped(const Buf& src, const Params& p) {
    const double g = driveGain(p); const bool on = p[pid::pre] > .5; Buf out(src.size());
    for (size_t i = 0; i < src.size(); ++i) { const double x = src[i] * g; out[i] = (float) (on ? preamp(x) : x); }
    return out;
}

inline Buf sampleBase(const Buf& src, double sr, const Params& p, bool unity = false) {
    const double rate = p[pid::rate], a = p[pid::alias], g = unity ? 1 : driveGain(p);
    Chain aa = inputFilter(rate, sr);
    Buf m(src.size());
    for (size_t i = 0; i < src.size(); ++i) { const double x = src[i] * g, f = chainRun(aa, x); m[i] = (float) (f + (x - f) * a); }
    const size_t n = std::max<size_t>(1, (size_t) std::floor(src.size() * rate / sr));
    Buf out(n); const double step = sr / rate;
    auto at = [&](size_t i) { return i < m.size() ? (double) m[i] : 0.0; };
    for (size_t k = 0; k < n; ++k) {
        const double x = k * step; const size_t i = (size_t) std::floor(x); const double f = x - i;
        out[k] = (float) quantize(at(i) * (1 - f) + at(i + 1) * f, p[pid::bits]);
    }
    return out;
}

class Converter {
public:
    explicit Converter(double sampleRate = 48000) : fs(sampleRate) {}
    void setup(const Params& p) {
        // the alias trick on live input: sampled as if sped up, so the converter runs slower by that ratio
        const double r = p[pid::rate] / std::pow(2.0, p[pid::rpm] / 12);
        if (r != rate) { rate = r; aa = inputFilter(rate, fs); }
        inc = std::min(1.0, rate / fs); alias = p[pid::alias]; bits = p[pid::bits]; g = driveGain(p); pre = p[pid::pre] > .5;
    }
    double process(double x) {
        x *= g; if (pre) x = preamp(x);
        const double f = chainRun(aa, x), v = f + (x - f) * alias;
        ph += inc;
        if (ph >= 1) { ph -= 1; const double t = 1 - ph / inc; hold = quantize(prev + (v - prev) * clampd(t, 0, 1), bits); }
        prev = v;
        return hold;
    }
    void reset() { for (auto& s : aa) s.z1 = s.z2 = 0; ph = 1; prev = hold = 0; }
private:
    double fs, rate = 0, inc = 1, alias = 0, bits = 12, g = 1, ph = 1, prev = 0, hold = 0; bool pre = true;
    Chain aa;
};

/* ---------------- pitch ---------------- */
inline double totalCents(const Params& p, double extraSemis = 0) { return (p[pid::semis] + extraSemis) * 100 + p[pid::cents] + 1200 * std::log2(p[pid::a4] / 440); }
inline int spInc(double cents) {
    const double st = cents / 100;
    if (std::fabs(st - std::round(st)) < 1e-6 && st >= -16 && st <= 15) return SP_TUNE[(int) std::round(st) + 16] + 1;
    return (int) clampd(std::round(128 * std::pow(2.0, cents / 1200)), 1, 1024);
}
inline double cubicAt(const Buf& d, long k, double f) {
    auto at = [&](long i) { return i >= 0 && i < (long) d.size() ? (double) d[(size_t) i] : 0.0; };
    const double y0 = at(k - 1), y1 = at(k), y2 = at(k + 1), y3 = at(k + 2);
    const double c1 = .5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - .5 * y3, c3 = .5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}
inline Buf playBase(const Buf& base, const Params& p, double fs, int mode, double cents) {
    const double rate = p[pid::rate];
    if (mode == 0) {
        const long inc = spInc(cents), frames = (long) std::floor((double) base.size() * 128 / inc);
        const size_t n = std::max<size_t>(1, (size_t) std::ceil(frames * fs / rate));
        Buf d(n); const double step = rate / fs; double kp = 0;
        for (size_t i = 0; i < n; ++i, kp += step) { const long idx = ((long) std::floor(kp) * inc) >> 7; d[i] = idx < (long) base.size() ? base[(size_t) idx] : 0.f; }
        return d;
    }
    const double ratio = std::pow(2.0, cents / 1200), step = rate * ratio / fs;
    const size_t n = std::max<size_t>(1, (size_t) std::ceil(base.size() / step));
    Buf d(n); double pos = 0;
    auto at = [&](long i) { return i >= 0 && i < (long) base.size() ? (double) base[(size_t) i] : 0.0; };
    for (size_t i = 0; i < n; ++i, pos += step) {
        const long k = (long) std::floor(pos); const double f = pos - k;
        if (mode == 1) d[i] = (float) at(k);
        else if (mode == 2) d[i] = (float) (at(k) * (1 - f) + at(k + 1) * f);
        else d[i] = (float) cubicAt(base, k, f);
    }
    return d;
}
inline Buf playDry(const Buf& src, double sr, double fs, double cents, size_t n) {
    const double step = sr / fs * std::pow(2.0, cents / 1200);
    Buf d(n); double pos = 0;
    for (size_t i = 0; i < n; ++i, pos += step) { const long k = (long) std::floor(pos); d[i] = k + 2 < (long) src.size() ? (float) cubicAt(src, k, pos - k) : (k >= 0 && k < (long) src.size() ? src[(size_t) k] : 0.f); }
    return d;
}

/* ---------------- output filters ---------------- */
inline Bq onePoleLp(double fs, double f) { const double K = std::tan(PI * clampd(f, 1, fs * .49) / fs); Bq b; b.b0 = b.b1 = K / (1 + K); b.b2 = 0; b.a1 = (K - 1) / (K + 1); b.a2 = 0; return b; }
class OutFilter {
public:
    explicit OutFilter(double sampleRate = 48000) : fs(sampleRate) {}
    void setup(const Params& p) {
        const int t = (int) p[pid::ftype];
        if (t != type || p[pid::cutoff] != cut) {
            type = t; cut = p[pid::cutoff];
            if (t >= 1 && t <= 4) secs = sDesign(proto(t + 2), fs);
            else if (t == 6) secs = {rbj(Rbj::lp, fs, cut, .5176), rbj(Rbj::lp, fs, cut, .7071), rbj(Rbj::lp, fs, cut, 1.9319)};
            else secs.clear();
        }
        // the digital filters: resonance raises the last section's Q
        const double rs = p[pid::res];
        if (t == 8 || t == 14) secs = {rbj(Rbj::lp, fs, p[pid::cutoff], .7071 + rs * 9)};
        else if (t == 9) secs = {onePoleLp(fs, p[pid::cutoff]), rbj(Rbj::lp, fs, p[pid::cutoff], 1 + rs * 8)};
        else if (t == 11 || t == 15) secs = {rbj(Rbj::lp, fs, p[pid::cutoff], .5412), rbj(Rbj::lp, fs, p[pid::cutoff], 1.3066 + rs * (t == 15 ? 10 : 7))};
        drive = t == 10 ? 1 : t == 12 ? .9 : t == 13 ? 1.25 : .7;
        resv = p[pid::res]; env = p[pid::fenv]; retrig = p[pid::retrig] > .5;
        tau = std::exp(-1 / (.075 * fs)); trigLen = .0094 * fs;
    }
    void trigger() { age = 0; }
    void reset() { lad = {0, 0, 0, 0}; for (auto& s : secs) s.z1 = s.z2 = 0; fe = ss = 0; age = 1e9; since = 1e9; }
    double process(double x) {
        const int t = type;
        if (t == 5) return x;
        if (t == 0 || t == 7) {
            if (retrig && detect(x)) age = 0;
            const double a = age++, open = a < trigLen ? 1 : std::pow(tau, a - trigLen);
            double fc;
            if (t == 0) { const double top = cut + (13000 - cut) * env; fc = cut + (top - cut) * open; }
            else fc = cut * std::pow(2.0, env * 3 * open);
            return ladder(x, fc);
        }
        if (t == 10 || t == 12 || t == 13) return ladder(x, cut);   // Emax, Mirage, DSS-1: the ladder with its own drive, no sweep
        return chainRun(secs, x);
    }
private:
    double ladder(double x, double fc) {
        const double fo = fs * 2, g = 1 - std::exp(-2 * PI * std::min(fc, .45 * fo) / fo), r = resv, dr = drive;
        auto& l = lad;
        for (int o = 0; o < 2; ++o) {
            const double u = std::tanh(x * dr - 4 * r * l[3]);
            const double t0 = std::tanh(l[0]), t1 = std::tanh(l[1]), t2 = std::tanh(l[2]);
            l[0] += g * (u - t0); l[1] += g * (t0 - t1); l[2] += g * (t1 - t2); l[3] += g * (t2 - std::tanh(l[3]));
        }
        return l[3] / dr * (1 + 2 * r);
    }
    bool detect(double x) {
        const double a = std::fabs(x);
        fe = a > fe ? a : fe * .995;
        ss += (a - ss) * .0005;
        since++;
        if (fe > .05 && fe > ss * 4 && since > .08 * fs) { since = 0; return true; }
        return false;
    }
    double drive = .7;
    double fs; int type = -1; double cut = 0, resv = 0, env = 0, tau = 0, trigLen = 0, age = 1e9, fe = 0, ss = 0, since = 1e9; bool retrig = false;
    std::array<double, 4> lad {0, 0, 0, 0};
    Chain secs;
};

/* ---------------- lo-fi ---------------- */
class Lofi {
public:
    explicit Lofi(double sampleRate = 48000, uint32_t seed = 12345) : fs(sampleRate), r(seed) {
        const size_t N = (size_t) std::ceil(fs * .05); dl[0].assign(N, 0.f); dl[1].assign(N, 0.f);
    }
    void setup(const Params& pp) {
        p = pp;
        for (int c = 0; c < 2; ++c) {
            ageHp[c] = rbj(Rbj::hp, fs, 20 + p[pid::age] * 140, .707); ageLp[c] = rbj(Rbj::lp, fs, 19000 - p[pid::age] * 13500, .707);
            tapeLp[c] = rbj(Rbj::lp, fs, 20000 - p[pid::tape] * 9000, .6); bump[c] = rbj(Rbj::peak, fs, 90, 1, p[pid::tape] * 3);
            hissF[c] = rbj(Rbj::hp, fs, 1800, .5);
        }
        on = p[pid::crackle] + p[pid::hiss] + p[pid::wow] + p[pid::flutter] + p[pid::tape] + p[pid::age] + p[pid::hum] > 0;
    }
    void process(double& l, double& rr) {
        if (!on) return;
        if (p[pid::wow] > 0 || p[pid::flutter] > 0) {
            ph[0] += .55 / fs; ph[1] += 7.1 / fs; fn += (noise() - fn) * .001;
            const double wowD = p[pid::wow] * .0032 * fs, flD = p[pid::flutter] * .00012 * fs;
            const double d = 2 + wowD * (1 + std::sin(2 * PI * ph[0])) + flD * (1 + std::sin(2 * PI * ph[1]) * .8 + fn * 4);
            const long N = (long) dl[0].size();
            dl[0][(size_t) w] = (float) l; dl[1][(size_t) w] = (float) rr;
            const double rp = w - d; const long i = (long) std::floor(rp); const double f = rp - i;
            const long i0 = ((i % N) + N) % N, i1 = (i0 + 1) % N;
            l = dl[0][(size_t) i0] * (1 - f) + dl[0][(size_t) i1] * f; rr = dl[1][(size_t) i0] * (1 - f) + dl[1][(size_t) i1] * f;
            w = (w + 1) % N;
        }
        if (p[pid::tape] > 0) {
            const double k = 1 + p[pid::tape] * 4, n = std::tanh(k);
            l = std::tanh(bump[0].run(l) * k) / n; rr = std::tanh(bump[1].run(rr) * k) / n;
            l = tapeLp[0].run(l); rr = tapeLp[1].run(rr);
        }
        if (p[pid::age] > 0) {
            l = ageLp[0].run(ageHp[0].run(l)); rr = ageLp[1].run(ageHp[1].run(rr));
            const double m = (l + rr) * .5, s = p[pid::age] * .6; l += (m - l) * s; rr += (m - rr) * s;
        }
        if (p[pid::crackle] > 0) {
            const double cr = p[pid::crackle];
            surf += (noise() - surf) * .05;
            double c = surf * .004 * cr;
            if (r() < cr * cr * 40 / fs) { const double a = r(); click += (r() < .5 ? -1 : 1) * a * a * a * .9; }
            if (r() < cr * .4 / fs) click += (r() < .5 ? -1 : 1) * .6;
            clickLp += (click - clickLp) * .45; c += clickLp * cr; click *= .6;
            l += c; rr += c * .92;
        }
        if (p[pid::hiss] > 0) { const double a = p[pid::hiss] * p[pid::hiss] * .035; l += hissF[0].run(noise()) * a; rr += hissF[1].run(noise()) * a; }
        if (p[pid::hum] > 0) {
            const double hz = p[pid::humHz] > .5 ? 60 : 50; ph[2] += hz / fs; if (ph[2] > 1) ph[2] -= 1;
            const double x = 2 * PI * ph[2], h = (std::sin(x) + .5 * std::sin(2 * x) + .25 * std::sin(3 * x)) * p[pid::hum] * p[pid::hum] * .02;
            l += h; rr += h;
        }
    }
private:
    double noise() { return r() * 2 - 1; }
    double fs; Rng r; Params p; bool on = false;
    Buf dl[2]; long w = 0;
    double ph[3] {0, 0, 0}, fn = 0, click = 0, clickLp = 0, surf = 0;
    Bq ageHp[2], ageLp[2], tapeLp[2], bump[2], hissF[2];
};

/* ---------------- master ---------------- */
class Master {
public:
    explicit Master(double sampleRate = 48000) : fs(sampleRate), L(std::max(1, (int) std::lround(sampleRate * .002))) { reset(); }
    int latency() const { return L; }
    void reset() {
        dl[0].assign((size_t) L, 0.f); dl[1].assign((size_t) L, 0.f); w = 0;
        mq.clear(); n = 0; box.assign((size_t) L, 1.0); bsum = L; bw = 0; rg = 1; env = 0;
        for (auto& e : eq) for (auto& f : e) f.z1 = f.z2 = 0;
    }
    void setup(const Params& p, const double* match) {
        auto mk = [&] {
            Chain s {rbj(Rbj::ls, fs, p[pid::lsF], .707, p[pid::lsG]), rbj(Rbj::peak, fs, p[pid::p1F], p[pid::p1Q], p[pid::p1G]), rbj(Rbj::peak, fs, p[pid::p2F], p[pid::p2Q], p[pid::p2G]),
                     rbj(Rbj::peak, fs, p[pid::p3F], p[pid::p3Q], p[pid::p3G]), rbj(Rbj::hs, fs, p[pid::hsF], .707, p[pid::hsG])};
            if (match && p[pid::match] > 0) for (int i = 0; i < 10; ++i) if (MATCH_BANDS[i] < fs * .45 && std::fabs(match[i]) > .01) s.push_back(rbj(Rbj::peak, fs, MATCH_BANDS[i], 1.41, match[i] * p[pid::match]));
            return s;
        };
        for (int c = 0; c < 2; ++c) {
            Chain s = mk();
            for (size_t i = 0; i < s.size() && i < eq[c].size(); ++i) { s[i].z1 = eq[c][i].z1; s[i].z2 = eq[c][i].z2; }
            eq[c] = s;
        }
        on = p[pid::mOn] > .5; thr = p[pid::cThr]; rat = p[pid::cRat];
        atk = std::exp(-1 / (p[pid::cAtk] * .001 * fs)); rel = std::exp(-1 / (p[pid::cRel] * .001 * fs));
        post = dbToGain(p[pid::cMake] + p[pid::mGain]); ceil = dbToGain(p[pid::lim]); lrel = 1 - std::exp(-1 / (.08 * fs));
    }
    bool enabled() const { return on; }
    void process(double& l, double& r) {
        if (!on) return;
        for (size_t k = 0; k < eq[0].size(); ++k) { l = eq[0][k].run(l); r = eq[1][k].run(r); }
        if (thr < 0 && rat > 1) {
            const double a = std::max(std::fabs(l), std::fabs(r));
            env = a > env ? atk * env + (1 - atk) * a : rel * env + (1 - rel) * a;
            const double x = 20 * std::log10(env + 1e-9) - thr, s = 1 - 1 / rat;
            const double red = x <= -3 ? 0 : x >= 3 ? x * s : s * (x + 3) * (x + 3) / 12;
            const double g = dbToGain(-red); l *= g; r *= g;
        }
        l *= post; r *= post;
        limit(l, r);
    }
private:
    void limit(double& l, double& r) {
        const double need = std::min(1.0, ceil / (std::max(std::fabs(l), std::fabs(r)) + 1e-12));
        const long i = n++;
        while (!mq.empty() && mq.back().first >= need) mq.pop_back();
        mq.emplace_back(need, i);
        while (mq.front().second <= i - L) mq.pop_front();
        const double m = mq.front().first;
        rg = std::min(m, rg + (1 - rg) * lrel);
        bsum += rg - box[(size_t) bw]; box[(size_t) bw] = rg; bw = (bw + 1) % L;
        const double g = bsum / L, ol = dl[0][(size_t) w], orr = dl[1][(size_t) w];
        dl[0][(size_t) w] = (float) l; dl[1][(size_t) w] = (float) r; w = (w + 1) % L;
        l = clampd(ol * g, -ceil, ceil); r = clampd(orr * g, -ceil, ceil);
    }
    double fs; int L;
    Chain eq[2];
    bool on = true; double thr = 0, rat = 2, atk = 0, rel = 0, post = 1, ceil = 1, lrel = 0, env = 0;
    Buf dl[2]; int w = 0;
    std::deque<std::pair<double, long>> mq; long n = 0;
    std::vector<double> box; double bsum = 0, rg = 1; int bw = 0;
};

/* ---------------- timestretch (WSOLA) ---------------- */
inline Chans cyclicStretch(const Chans& chs, double factor, double sr, double grainMs) {
    const long len = (long) chs[0].size(), G = std::max(16L, std::lround(grainMs / 1000 * sr)), X = std::min(std::lround(.002 * sr), G >> 1);
    const long outLen = std::max(1L, std::lround(len * factor));
    Chans r;
    for (auto& c : chs) {
        Buf o((size_t) outLen, 0.f);
        for (long op = 0, k = 0; op < outLen; op += G, ++k) {
            const long ip = (long) std::floor(op / factor);
            for (long i = 0; i < G + X && op + i < outLen; ++i) {
                const double v = ip + i < len ? c[(size_t) (ip + i)] : 0.0, w = i < X ? (double) i / X : i >= G ? 1 - (double) (i - G) / X : 1;
                o[(size_t) (op + i)] += (float) (v * (k == 0 && i < X ? 1 : w));
            }
        }
        r.push_back(std::move(o));
    }
    return r;
}
inline Buf resampleTo(const Buf& c, size_t n) {
    Buf o(n); const double step = (double) (c.size() - 1) / (double) std::max<size_t>(1, n - 1);
    for (size_t i = 0; i < n; ++i) { const double x = i * step; const size_t k = (size_t) std::floor(x); const double f = x - k; o[i] = (float) (c[k] * (1 - f) + (k + 1 < c.size() ? c[k + 1] : 0.f) * f); }
    return o;
}
// mode 0 smooth (WSOLA), 1 cyclic Classic (longer), 2 cyclic Warp (the cycles, played back at the original length)
inline Chans stretch(const Chans& chs, double factor, double sr, double grainMs, int mode = 0) {
    if (std::fabs(factor - 1) < 1e-4) return chs;
    if (mode == 2) { Chans c = cyclicStretch(chs, factor, sr, grainMs); for (auto& x : c) x = resampleTo(x, chs[0].size()); return c; }
    if (mode) return cyclicStretch(chs, factor, sr, grainMs);
    const long len = (long) chs[0].size(), N = std::max(64L, 2 * std::lround(grainMs / 1000 * sr / 2)), Hs = N / 2, tol = std::lround(N / 4.0);
    const double Ha = Hs / factor;
    const long outLen = std::max(1L, std::lround(len * factor));
    Buf mono((size_t) len, 0.f);
    for (auto& c : chs) for (long i = 0; i < len; ++i) mono[(size_t) i] += c[(size_t) i] / chs.size();
    std::vector<double> win((size_t) N); for (long i = 0; i < N; ++i) win[(size_t) i] = .5 - .5 * std::cos(2 * PI * i / N);
    std::vector<std::vector<double>> out(chs.size(), std::vector<double>((size_t) (outLen + N), 0.0));
    std::vector<double> norm((size_t) (outLen + N), 0.0);
    auto at = [&](long i) { return i >= 0 && i < len ? (double) mono[(size_t) i] : 0.0; };
    long prev = 0;
    for (long m = 0, op = 0; op < outLen; ++m, op += Hs) {
        long pos = std::lround(m * Ha);
        if (m > 0) {
            const long nat = prev + Hs; long best = 0; double bv = -1e300;
            for (long d = -tol; d <= tol; d += 2) { double s = 0; for (long i = 0; i < N; i += 4) s += at(nat + i) * at(pos + d + i); if (s > bv) { bv = s; best = d; } }
            const long c0 = best; bv = -1e300;
            for (long d = c0 - 2; d <= c0 + 2; ++d) { double s = 0; for (long i = 0; i < N; i += 2) s += at(nat + i) * at(pos + d + i); if (s > bv) { bv = s; best = d; } }
            pos += best;
        }
        pos = std::max(0L, std::min(pos, std::max(0L, len - 1)));
        for (size_t c = 0; c < chs.size(); ++c) for (long i = 0; i < N && pos + i < len; ++i) out[c][(size_t) (op + i)] += chs[c][(size_t) (pos + i)] * win[(size_t) i];
        for (long i = 0; i < N; ++i) norm[(size_t) (op + i)] += win[(size_t) i];
        prev = pos;
    }
    Chans r(chs.size(), Buf((size_t) outLen));
    for (size_t c = 0; c < chs.size(); ++c) for (long i = 0; i < outLen; ++i) r[c][(size_t) i] = (float) (out[c][(size_t) i] / std::max(norm[(size_t) i], .5));
    return r;
}
inline Chans prepareSource(const Chans& chs, const Params& p, double sr) {
    Chans src;
    if (p[pid::mono] > .5 && chs.size() > 1) { Buf m(chs[0].size()); for (size_t i = 0; i < m.size(); ++i) m[i] = (chs[0][i] + chs[1][i]) * .5f; src.push_back(m); }
    else for (size_t c = 0; c < std::min<size_t>(2, chs.size()); ++c) src.push_back(chs[c]);
    if (p[pid::stretch] != 1) src = stretch(src, p[pid::stretch], sr, p[pid::grain], (int) p[pid::smode]);
    return src;
}

/* ---------------- live timestretch (cyclic, S950 / S1000 style) ---------------- */
// Warp: each grain is played faster by the factor so the audio keeps its timing and only the cycles are heard.
// Classic: the input slowed by the factor, cycle by cycle, restarting on the bar so it stays on the grid; it reads
// from a ring of the last ~87 s. Grains get 2 ms fades at their edges, which is part of the stuttery sound.
class LiveStretch {
public:
    void prepare(double sampleRate) { fs = sampleRate; for (auto& b : ring) b.assign(N, 0.f); w = 0; t = 0; chunk = 0; segStart = 0; }
    // bar: samples per bar from the host tempo; sinceBar: samples since the last bar line (or -1 without a transport)
    void process(float& l, float& r, double factor, int mode, double grainMs, double bar, double sinceBar) {
        ring[0][(size_t) (w & M)] = l; ring[1][(size_t) (w & M)] = r; w++;
        if (factor <= 1.0001 || mode == 0) { t = 0; return; }
        const double G = std::max(16.0, grainMs / 1000 * fs), X = std::min(.002 * fs, G / 2);
        double pos, k;
        if (mode == 2) {
            const double C = std::max(1.0, G / factor);
            if (chunk >= C) { chunk = 0; chunkStart = (double) w; }
            pos = chunkStart - G + chunk * factor; k = chunk * factor; chunk += 1;
            const double e = std::min(1.0, std::min(k, G - k) / X);
            l = (float) (read(0, pos) * e); r = (float) (read(1, pos) * e);
            return;
        }
        const double seg = bar > 0 ? bar : 2 * fs;
        if (t >= factor * seg || t == 0) { segStart = (double) w - (sinceBar >= 0 ? std::fmod(sinceBar, seg) : 0); t = 0; }
        const double g = std::floor(t / G), inG = t - g * G;
        pos = segStart - G + g * G / factor + inG; t += 1;
        const double e = std::min(1.0, std::min(inG, G - inG) / X);
        l = (float) (read(0, pos) * e); r = (float) (read(1, pos) * e);
    }
private:
    double read(int c, double pos) const {
        pos = std::max(pos, (double) w - (double) N + 4);
        const long i = (long) std::floor(pos); const double f = pos - i;
        return ring[(size_t) c][(size_t) (i & M)] * (1 - f) + ring[(size_t) c][(size_t) ((i + 1) & M)] * f;
    }
    static constexpr long N = 1L << 22, M = N - 1;
    std::array<std::vector<float>, 2> ring;
    double fs = 48000, t = 0, chunk = 1e18, chunkStart = 0, segStart = 0; long w = 0;
};

/* ---------------- offline render ---------------- */
// `src` is already mono-mixed and stretched (prepareSource); returns two channels at fs, before the master
inline Chans renderPre(const Chans& src, double sr, const Params& p0, double fs, double extraSemis = 0) {
    const Params p = effective(p0);
    const Machine& mach = MACHINES[(int) clampd(p[pid::machine], 0, 17)];
    const double cents = totalCents(p, extraSemis), inGain = dbToGain(p[pid::input]), outGain = dbToGain(p[pid::output] - (p[pid::link] > .5 ? p[pid::input] : 0)), mix = p[pid::mix];
    Chans res;
    for (auto& s : src) {
        if (mach.pitch == 0) {
            // the SP-1200 / SP-12 through the circuit-level engine (SP1200.h); 6-pole and 4-pole menu entries run
            // output 7-8 into those filters
            const int ft = (int) p[pid::ftype], out = ft == 0 ? 1 : ft <= 4 ? ft + 2 : 7;
            sp::SampleOpts so; so.clock = p[pid::rate]; so.level = 1; so.bits = p[pid::bits]; so.measured = p[pid::calib] > .5;
            so.preGain = driveGain(p); so.preOn = p[pid::pre] > .5;
            const auto codes = sp::codesFor(s, sr, so, p[pid::rpm]);
            sp::PlayOpts po; po.clock = p[pid::rate]; po.sp12 = (int) p[pid::machine] == 1; po.cents = cents - p[pid::rpm] * 100; po.output = out;
            po.rest = p[pid::cutoff]; po.res = p[pid::res]; po.depth = p[pid::fenv]; po.retrigger = p[pid::retrig] > .5; po.measured = p[pid::calib] > .5;
            Buf wet = sp::playChannel(codes, po, fs);
            if (ft >= 6) { OutFilter f(fs); f.setup(p); f.trigger(); for (auto& v : wet) v = (float) f.process(v); }
            for (auto& v : wet) v = (float) (v / (1 + p[pid::drive] * 1.5));
            if (mix < 1) { const Buf dry = playDry(s, sr, fs, cents, wet.size()); for (size_t i = 0; i < wet.size(); ++i) wet[i] = (float) (wet[i] * mix + dry[i] * inGain * (1 - mix)); }
            res.push_back(std::move(wet));
            continue;
        }
        // the alias trick on any machine: sampled sped up by rpm semitones, played back down again
        const Buf base = sampleBase(preamped(s, p), sr * std::pow(2.0, p[pid::rpm] / 12), p, true);
        Buf wet = playBase(base, p, fs, mach.pitch, cents - p[pid::rpm] * 100);
        Buf dry; if (mix < 1) dry = playDry(s, sr, fs, cents, wet.size());
        OutFilter f(fs); f.setup(p); f.trigger();
        double dc = 0;
        for (size_t i = 0; i < wet.size(); ++i) {
            double y = wet[i]; dc += (y - dc) * (6 / fs); y = f.process(y - dc);
            y /= 1 + p[pid::drive] * 1.5;
            wet[i] = (float) (mix < 1 ? y * mix + dry[i] * inGain * (1 - mix) : y);
        }
        res.push_back(std::move(wet));
    }
    if (res.size() == 1) res.push_back(res[0]);
    Lofi lo(fs, 777); lo.setup(p);
    for (size_t i = 0; i < res[0].size(); ++i) { double l = res[0][i], r = res[1][i]; lo.process(l, r); res[0][i] = (float) (l * outGain); res[1][i] = (float) (r * outGain); }
    return res;
}
inline Chans renderMaster(const Chans& pre, const Params& p, const double* match, double fs) {
    Master m(fs); m.setup(p, match);
    const size_t n = pre[0].size(); const long tail = m.latency();
    Chans o(2, Buf(n));
    if (!m.enabled()) return pre;
    for (long i = 0; i < (long) n + tail; ++i) {
        double l = i < (long) n ? pre[0][(size_t) i] : 0.0, r = i < (long) n ? pre[1][(size_t) i] : 0.0;
        m.process(l, r);
        const long j = i - tail;
        if (j >= 0 && j < (long) n) { o[0][(size_t) j] = (float) l; o[1][(size_t) j] = (float) r; }
    }
    return o;
}

/* ---------------- analysis ---------------- */
inline void fft(std::vector<double>& re, std::vector<double>& im) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double a = -2 * PI / len, wr = std::cos(a), wi = std::sin(a);
        for (size_t i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (size_t k = 0; k < len / 2; ++k) {
                const size_t p = i + k, q = p + len / 2; const double tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
                re[q] = re[p] - tr; im[q] = im[p] - ti; re[p] += tr; im[p] += ti;
                const double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}
inline Buf monoOf(const Chans& chs) { if (chs.size() == 1) return chs[0]; Buf m(chs[0].size(), 0.f); for (auto& c : chs) for (size_t i = 0; i < m.size(); ++i) m[i] += c[i] / chs.size(); return m; }
inline Buf decimate(const Buf& x, double sr, double& outSr) {
    const int k = std::max(1, (int) std::floor(sr / 11025)); const size_t n = x.size() / (size_t) k; Buf d(n);
    for (size_t i = 0; i < n; ++i) { double s = 0; for (int j = 0; j < k; ++j) s += x[i * (size_t) k + (size_t) j]; d[i] = (float) (s / k); }
    outSr = sr / k; return d;
}
inline std::array<double, 10> bandSpectrum(const Chans& chs, double sr) {
    const Buf x = monoOf(chs); const long N = 4096, hop = 2048;
    const long frames = std::max(1L, ((long) x.size() - N) / hop + 1), take = std::min(frames, 400L);
    std::vector<double> pow((size_t) N / 2, 0.0), re((size_t) N), im((size_t) N);
    for (long t = 0; t < take; ++t) {
        const long st = (long) std::floor((double) t * frames / take) * hop;
        for (long i = 0; i < N; ++i) { re[(size_t) i] = (st + i < (long) x.size() ? x[(size_t) (st + i)] : 0.0) * (.5 - .5 * std::cos(2 * PI * i / N)); im[(size_t) i] = 0; }
        fft(re, im);
        for (long i = 0; i < N / 2; ++i) pow[(size_t) i] += re[(size_t) i] * re[(size_t) i] + im[(size_t) i] * im[(size_t) i];
    }
    std::array<double, 10> r {};
    for (int b = 0; b < 10; ++b) {
        const double f = MATCH_BANDS[b];
        const long lo = std::max(1L, (long) std::floor(f / std::sqrt(2.0) * N / sr)), hi = std::min(N / 2 - 1, (long) std::ceil(f * std::sqrt(2.0) * N / sr));
        double s = 1e-20; for (long i = lo; i <= hi; ++i) s += pow[(size_t) i];
        r[(size_t) b] = hi >= lo ? 10 * std::log10(s / take) : -200;
    }
    return r;
}
inline std::array<double, 10> matchGains(const std::array<double, 10>& ref, const std::array<double, 10>& cur) {
    std::array<double, 10> d {}, c {}, o {};
    for (int i = 0; i < 10; ++i) d[(size_t) i] = (ref[(size_t) i] < -150 || cur[(size_t) i] < -150) ? 0 : ref[(size_t) i] - cur[(size_t) i];
    const double mid = (d[3] + d[4] + d[5] + d[6] + d[7]) / 5;
    for (int i = 0; i < 10; ++i) c[(size_t) i] = clampd(d[(size_t) i] - mid, -12, 12);
    for (int i = 0; i < 10; ++i) { const double a = i ? c[(size_t) i - 1] : c[(size_t) i], b = i < 9 ? c[(size_t) i + 1] : c[(size_t) i]; o[(size_t) i] = clampd(.25 * a + .5 * c[(size_t) i] + .25 * b, -12, 12); }
    return o;
}
inline double loudness(const Chans& chs, double sr) {
    const long blk = std::lround(.4 * sr), hop = std::lround(.1 * sr);
    Chans w;
    for (size_t c = 0; c < std::min<size_t>(2, chs.size()); ++c) {
        Bq a = rbj(Rbj::hs, sr, 1681, .7071, 4), b = rbj(Rbj::hp, sr, 38, .5); Buf o(chs[c].size());
        for (size_t i = 0; i < o.size(); ++i) o[i] = (float) b.run(a.run(chs[c][i]));
        w.push_back(std::move(o));
    }
    std::vector<double> ms;
    for (long s = 0; s + blk <= (long) w[0].size(); s += hop) { double e = 0; for (auto& c : w) for (long i = s; i < s + blk; ++i) e += (double) c[(size_t) i] * c[(size_t) i]; ms.push_back(e / blk); }
    if (ms.empty()) { double e = 0; for (auto& c : w) for (float v : c) e += (double) v * v; ms.push_back(e / std::max<size_t>(1, w[0].size())); }
    auto lufs = [](double m) { return -.691 + 10 * std::log10(m + 1e-20); };
    std::vector<double> g1; for (double m : ms) if (lufs(m) > -70) g1.push_back(m);
    if (g1.empty()) return -70;
    double s1 = 0; for (double m : g1) s1 += m;
    const double rel = lufs(s1 / g1.size()) - 10; double s2 = 0; int n2 = 0;
    for (double m : g1) if (lufs(m) > rel) { s2 += m; n2++; }
    return lufs(s2 / std::max(1, n2));
}
inline double peakDb(const Chans& chs) { double p = 1e-9; for (auto& c : chs) for (float v : c) p = std::max(p, (double) std::fabs(v)); return 20 * std::log10(p); }

struct AutoMasterResult { Params params; std::array<double, 10> match; };
inline AutoMasterResult autoMaster(const Chans& pre, double fs, const std::array<double, 10>* refBands, double target = -14) {
    const auto cur = bandSpectrum(pre, fs);
    std::array<double, 10> ref {};
    if (refBands) ref = *refBands; else for (int i = 0; i < 10; ++i) ref[(size_t) i] = TARGET_CURVE[i] + cur[4];
    AutoMasterResult r; r.match = matchGains(ref, cur);
    Params& p = r.params;
    p[pid::lsF] = 90; p[pid::lsG] = 0; p[pid::p1F] = 250; p[pid::p1G] = 0; p[pid::p1Q] = 1; p[pid::p2F] = 1200; p[pid::p2G] = 0; p[pid::p2Q] = 1;
    p[pid::p3F] = 5000; p[pid::p3G] = 0; p[pid::p3Q] = 1; p[pid::hsF] = 10000; p[pid::hsG] = 0; p[pid::match] = refBands ? .8 : .6;
    p[pid::cRat] = 2; p[pid::cAtk] = 20; p[pid::cRel] = 200; p[pid::cMake] = 0; p[pid::lim] = -1; p[pid::mOn] = 1; p[pid::mGain] = 0;
    double rms = 0; long n = 0; for (auto& c : pre) for (size_t i = 0; i < c.size(); i += 4) { rms += (double) c[i] * c[i]; n++; }
    const double rmsDb = 10 * std::log10(rms / std::max(1L, n) + 1e-12);
    p[pid::cThr] = clampd(std::round((rmsDb + 4) * 10) / 10, -40, 0);
    Chans seg;
    for (auto& c : pre) { const size_t len = std::min(c.size(), (size_t) std::lround(60 * fs)), st = (c.size() - len) / 2; seg.emplace_back(c.begin() + (long) st, c.begin() + (long) (st + len)); }
    for (int it = 0; it < 3; ++it) {
        const auto out = renderMaster(seg, p, r.match.data(), fs); const double lu = loudness(out, fs);
        if (!std::isfinite(lu) || lu <= -69) break;
        p[pid::mGain] = clampd(std::round((p[pid::mGain] + (target - lu)) * 10) / 10, -12, 24);
    }
    return r;
}
inline double detectTempo(const Chans& chs, double sr) {   // 0 when too short
    double r; const Buf d = decimate(monoOf(chs), sr, r); const long N = 1024, hop = 256; const double fr = r / hop;
    const long frames = ((long) d.size() - N) / hop;
    if (frames < 32) return 0;
    std::vector<double> re((size_t) N), im((size_t) N), prev((size_t) N / 2, 0.0), on((size_t) frames);
    for (long t = 0; t < frames; ++t) {
        for (long i = 0; i < N; ++i) { re[(size_t) i] = d[(size_t) (t * hop + i)] * (.5 - .5 * std::cos(2 * PI * i / N)); im[(size_t) i] = 0; }
        fft(re, im); double s = 0;
        for (long i = 1; i < N / 2; ++i) { const double m = std::log1p(100 * std::hypot(re[(size_t) i], im[(size_t) i])); if (m > prev[(size_t) i]) s += m - prev[(size_t) i]; prev[(size_t) i] = m; }
        on[(size_t) t] = s;
    }
    double mean = 0; for (double v : on) mean += v / frames;
    for (auto& v : on) v = std::max(0.0, v - mean);
    const long lag0 = (long) std::floor(60 * fr / 200), lag1 = (long) std::ceil(60 * fr / 55);
    std::vector<double> ac((size_t) lag1 + 2, 0.0);
    for (long L = lag0; L <= lag1 + 1; ++L) { double s = 0; for (long t = 0; t + L < frames; ++t) s += on[(size_t) t] * on[(size_t) (t + L)]; ac[(size_t) L] = s / std::max(1L, frames - L); }
    long best = lag0; double bv = -1;
    for (long L = lag0 + 1; L <= lag1; ++L) {
        const double bpm = 60 * fr / L, w = std::exp(-.5 * std::pow(std::log2(bpm / 110) / 1.1, 2)), v = ac[(size_t) L] * w;
        if (v > bv && ac[(size_t) L] >= ac[(size_t) L - 1] && ac[(size_t) L] >= ac[(size_t) L + 1]) { bv = v; best = L; }
    }
    const double a = ac[(size_t) best - 1], b = ac[(size_t) best], c = ac[(size_t) best + 1], den = a - 2 * b + c, off = den != 0 ? clampd(.5 * (a - c) / den, -.5, .5) : 0;
    double bpm = 60 * fr / (best + off);
    while (bpm < 70) bpm *= 2;
    while (bpm > 180) bpm /= 2;
    const double secs = chs[0].size() / sr;
    for (int beats : {4, 8, 16, 32, 2, 64}) { const double lb = 60 * beats / secs; if (lb >= 70 && lb <= 180 && std::fabs(lb / bpm - 1) < .04) { bpm = lb; break; } }
    return std::round(bpm * 10) / 10;
}
struct Key { int root = -1; bool minor = false; double confidence = 0; std::string name() const { return root < 0 ? std::string("n/a") : std::string(NOTE_NAMES[root]) + (minor ? " minor" : " major"); } };
inline Key detectKey(const Chans& chs, double sr) {
    static const double MAJ[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88}, MIN[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    double r; const Buf d = decimate(monoOf(chs), sr, r); const long N = 4096, hop = 2048, frames = ((long) d.size() - N) / hop + 1;
    Key k; if (frames < 1) return k;
    std::vector<double> re((size_t) N), im((size_t) N); double chroma[12] {}; const long take = std::min(frames, 600L);
    const long lo = (long) std::ceil(55.0 * N / r), hi = std::min(N / 2 - 1, (long) std::floor(2000.0 * N / r));
    std::vector<int> pc((size_t) N / 2, 0);
    for (long i = lo; i <= hi; ++i) pc[(size_t) i] = ((((int) std::lround(12 * std::log2(i * r / N / 440))) % 12) + 12 + 9) % 12;
    for (long t = 0; t < take; ++t) {
        const long st = (long) std::floor((double) t * frames / take) * hop;
        for (long i = 0; i < N; ++i) { re[(size_t) i] = (st + i < (long) d.size() ? d[(size_t) (st + i)] : 0.0) * (.5 - .5 * std::cos(2 * PI * i / N)); im[(size_t) i] = 0; }
        fft(re, im);
        for (long i = lo; i <= hi; ++i) chroma[pc[(size_t) i]] += std::hypot(re[(size_t) i], im[(size_t) i]);
    }
    auto corr = [](const double* a, const double* b) { double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) { ma += a[i] / 12; mb += b[i] / 12; } double n = 0, da = 0, db = 0; for (int i = 0; i < 12; ++i) { n += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) * (a[i] - ma); db += (b[i] - mb) * (b[i] - mb); } return n / std::sqrt(da * db + 1e-20); };
    double best = -2;
    for (int root = 0; root < 12; ++root) for (int q = 0; q < 2; ++q) {
        double rot[12]; for (int i = 0; i < 12; ++i) rot[i] = (q ? MIN : MAJ)[(i - root + 12) % 12];
        const double c = corr(chroma, rot);
        if (c > best) { best = c; k.root = root; k.minor = q == 1; }
    }
    k.confidence = std::round(best * 100) / 100;
    return k;
}

/* ---------------- presets and random ---------------- */
static constexpr int kPresets = (int) (sizeof(PRESETS) / sizeof(PRESETS[0]));
inline void applyPreset(Params& p, int i) { for (int k = 0; k < kSound; ++k) p[SOUND_IDS[k]] = PRESETS[i].v[k]; }
inline void setMachine(Params& p, int m) {
    m = (int) clampd(m, 0, 17); const Machine& M = MACHINES[m]; p[pid::machine] = m; p[pid::rate] = M.rate; p[pid::bits] = M.bits; p[pid::ftype] = M.ftype; p[pid::cutoff] = M.cutoff; p[pid::res] = M.res; }
// random settings: a preset family's sound, then nudged (the app's randomPatch draws from the same families)
inline std::string randomPatch(Params& p, uint32_t seed) {
    Rng r(seed);
    const int i = (int) (r() * kPresets) % kPresets;
    applyPreset(p, i);
    auto lerp = [&](double a, double b) { return a + (b - a) * r(); };
    p[pid::semis] = r() < .35 ? std::round(lerp(-7, 5)) : 0;
    for (int id : {pid::crackle, pid::hiss, pid::wow, pid::flutter, pid::tape, pid::age}) if (r() < .3) p[id] = clampd(p[id] + lerp(0, .35), 0, 1);
    p[pid::drive] = clampd(p[pid::drive] + lerp(-.1, .2), 0, 1);
    p[pid::cutoff] = clampd(p[pid::cutoff] * std::pow(2.0, lerp(-.5, .5)), 100, 20000);
    return std::string("Random · ") + PRESETS[i].family;
}

}  // namespace twelve
