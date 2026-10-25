// The studio rack: ten units on the mixer's FX send, two slots in series or parallel.
// C++ port of the page's rack worklets (Verb224, PrimeTime, Effectron, DP4, Microverb, ADAFlanger, UREI1176, PCSim,
// H910, MXR113) with their shared parts; each takes the same settings object the page sends.
#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "Val.h"

namespace farp {
namespace rack {

static constexpr double PI = 3.14159265358979323846;
inline double jr (double x) { return std::floor (x + 0.5); }          // JS Math.round
inline double sgn (double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
inline double tnh (double x) { return x < -3 ? -1 : x > 3 ? 1 : x * (27 + x * x) / (27 + 9 * x * x); }
inline double sat (double x) { return x > 3 ? 1 : x < -3 ? -1 : x * (27 + x * x) / (27 + 9 * x * x); }

// the units' tiny noise sources (the page uses Math.random; any white noise will do)
struct Rng { uint32_t r = 0x12345678; double next() { r = r * 1664525u + 1013904223u; return (double) (r >> 8) / 16777216.0; } };

// settings: a JS object of numbers, strings and booleans
struct Cfg {
    std::map<std::string, Val> v;
    bool isNull (const char* k) const { auto it = v.find (k); return it == v.end() || it->second.isNull(); }
    double nz (const char* k, double d) const { auto it = v.find (k); return (it == v.end() || it->second.isNull()) ? d : it->second.num(); }   // C.k == null ? d : C.k
    double orv (const char* k, double d) const { auto it = v.find (k); return (it == v.end() || ! it->second.truthy()) ? d : it->second.num(); } // C.k || d
    bool truthy (const char* k) const { auto it = v.find (k); return it != v.end() && it->second.truthy(); }
    std::string str (const char* k, const char* d = "") const { auto it = v.find (k); if (it == v.end() || ! it->second.truthy()) return d; return it->second.isStr() ? it->second.str() : std::string(); }
    const Val* get (const char* k) const { auto it = v.find (k); return it == v.end() ? nullptr : &it->second; }
    void assign (const Val& o) { for (auto& kv : o.o) v[kv.first] = kv.second; }
};

struct DLine {
    std::vector<float> b; int64_t m = 0, w = 0;
    explicit DLine (double n = 1) { int64_t mm = 1; while ((double) mm < n + 4) mm <<= 1; b.assign ((size_t) mm, 0.f); m = mm - 1; }
    void push (double x) { w = (w + 1) & m; b[(size_t) w] = (float) x; }
    double at (double d) const { return b[(size_t) (((int64_t) ((double) w - d)) & m)]; }
    double lerp (double d) const { const double i = std::floor (d), f = d - i; const int64_t ii = (int64_t) i; const double a = b[(size_t) ((w - ii) & m)], c = b[(size_t) ((w - ii - 1) & m)]; return a + (c - a) * f; }
    void clear() { std::fill (b.begin(), b.end(), 0.f); }
};

struct Biq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
struct bq {
    static Biq lp (double fs, double f, double q) { const double w = 2 * PI * std::min (f, fs * .45) / fs, al = std::sin (w) / (2 * q), cw = std::cos (w), a0 = 1 + al; return { (1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0, -2 * cw / a0, (1 - al) / a0, 0, 0 }; }
    static Biq hp (double fs, double f, double q) { const double w = 2 * PI * std::min (f, fs * .45) / fs, al = std::sin (w) / (2 * q), cw = std::cos (w), a0 = 1 + al; return { (1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, -2 * cw / a0, (1 - al) / a0, 0, 0 }; }
    static double run (Biq& q, double x) { const double y = q.b0 * x + q.z1; q.z1 = q.b1 * x - q.a1 * y + q.z2; q.z2 = q.b2 * x - q.a2 * y; return y; }
};

// low-pass of n poles (pairs of biquads + an optional single pole)
struct LPN {
    double fs; int n; std::vector<double> qs; double f = -1; std::vector<Biq> b; double a = 0, y = 0;
    LPN (double fs_, double f_, int n_, std::vector<double> qs_ = {}) : fs (fs_), n (n_), qs (std::move (qs_)) { set (f_); }
    void set (double fNew) {
        if (fNew == f) return; f = fNew; const std::vector<double> Q = qs.empty() ? std::vector<double> { .54, .65, 1.3 } : qs;
        b.clear(); for (int k = 0; k < n / 2; k++) b.push_back (bq::lp (fs, f, k < (int) Q.size() && Q[(size_t) k] != 0 ? Q[(size_t) k] : .707));
        a = n % 2 ? 1 - std::exp (-2 * PI * std::min (f, fs * .45) / fs) : 0;
    }
    double run (double x) { for (auto& q : b) x = bq::run (q, x); if (a != 0) { y += a * (x - y); x = y; } return x; }
};

struct Hold { double ph = 1, v = 0; double run (double x, double ratio) { ph += ratio; if (ph >= 1) { ph -= std::floor (ph); v = x; } return v; } };
inline double tailOf (double ms, double fb) { return std::min (20.0, ms / 1000 * (fb > .01 ? std::log (1e-4) / std::log (std::min (.98, std::abs (fb))) : 1) + .3); }

// ---------------------------------------------------------------------------------------------
class Unit {
public:
    double fs; Cfg c; double quiet = 1e12; bool asleep = true; Rng rng;
    Unit (double fs_, const Val& cfg) : fs (fs_) { c.assign (cfg); }
    virtual ~Unit() = default;
    virtual void update() {}
    virtual double tail() { return 5; }
    virtual void reset() {}
    virtual void run (const float* a, const float* b, float* L, float* R, int N) = 0;
    void setCfg (const Val& cfg) { c.assign (cfg); update(); }
    // mono or stereo in, stereo out; sleeps once its input has been silent for longer than its tail
    void process (const float* a, const float* b, float* L, float* R, int N) {
        bool has = false; if (a) for (int i = 0; i < N; i++) if (a[i] != 0 || b[i] != 0) { has = true; break; }
        if (has) { quiet = 0; if (asleep) { asleep = false; update(); } } else quiet += N;
        if (quiet > tail() * fs) {
            if (! asleep) { asleep = true; reset(); }
            std::fill (L, L + N, 0.f); std::fill (R, R + N, 0.f); return;
        }
        run (a, b, L, R, N);
    }
};

// ---- Lexicon 224 / AMS RMX16 ----
class Verb224 : public Unit {
public:
    double sc; DLine pre; struct Ind { DLine d; double n; }; std::vector<Ind> ind;
    DLine apL, dL1, apL2, dL2, apR, dR1, apR2, dR2, taps;
    double bw = 0, dmL = 0, dmR = 0, lfo = 0, fbL = 0, fbR = 0;
    struct Tap { double d, gL, gR; }; std::vector<Tap> tapSet; bool haveTaps = false;
    Biq inLp, inLp2, outL, outR; int lex = 0; double damp = 0, scale = 1, rt = 1, decay = 0, dd1 = 0, dd2 = 0;
    static DLine D (double sc, double n) { return DLine (std::ceil (n * sc * 1.6) + 64); }
    Verb224 (double fs_, const Val& cfg) : Unit (fs_, cfg), sc (fs_ / 29761), pre (std::ceil (fs_ * .26)),
        apL (D (sc, 672 + 40)), dL1 (D (sc, 4453)), apL2 (D (sc, 1800)), dL2 (D (sc, 3720)), apR (D (sc, 908 + 40)), dR1 (D (sc, 4217)), apR2 (D (sc, 2656)), dR2 (D (sc, 3163)), taps (std::ceil (fs_ * 1.3)) {
        for (double n : { 142.0, 107.0, 379.0, 277.0 }) ind.push_back ({ D (sc, n), n });
        inLp = bq::lp (fs, 8000, .54); inLp2 = bq::lp (fs, 8000, 1.3); outL = bq::lp (fs, 8000, .7); outR = bq::lp (fs, 8000, .7);
        update();
    }
    void reset() override { pre.clear(); for (auto& a : ind) a.d.clear(); for (DLine* d : { &apL, &dL1, &apL2, &dL2, &apR, &dR1, &apR2, &dR2 }) d->clear(); taps.clear(); fbL = fbR = bw = dmL = dmR = 0; }
    double tail() override { return (haveTaps ? 1.4 : 1 + 2 * rt) + c.orv ("pre", 0) / 1000; }
    void update() override {
        const std::string P = c.str ("prog", "vplate"); const double size = c.nz ("size", .5);
        struct Pr { double scale, rt, dd1; int lex; }; bool has = true; Pr cfg { 1, 1, .7, 1 };
        if (P == "vplate") cfg = { 1, .6 + size * 5, .7, 1 }; else if (P == "pplate") cfg = { .8, .4 + size * 3, .75, 1 };
        else if (P == "hall") cfg = { 1.35, 1 + size * 7, .6, 1 }; else if (P == "room") cfg = { .55, .25 + size * 1.6, .6, 1 }; else if (P == "chamber") cfg = { .8, .5 + size * 3, .65, 1 };
        else if (P == "rplate") cfg = { .9, .5 + size * 4.5, .7, 0 }; else if (P == "rhall") cfg = { 1.3, 1 + size * 6, .62, 0 }; else has = false;
        const double bwF = (has && ! cfg.lex) || P == "nonlin" || P == "reverse" || P == "ambience" ? 16000 : 8000;
        for (Biq* f : { &inLp, &inLp2, &outL, &outR }) { const double z1 = f->z1, z2 = f->z2; *f = bq::lp (fs, bwF, .7); f->z1 = z1; f->z2 = z2; }
        lex = has ? cfg.lex : 0;
        damp = .05 + (1 - c.nz ("treble", .6)) * .6;
        if (has) {
            scale = cfg.scale; rt = cfg.rt;
            const double half = (672 + 4453 + 1800 + 3720) / 29761.0 * cfg.scale;
            decay = std::pow (10.0, -3 * half / cfg.rt); dd1 = cfg.dd1; dd2 = std::max (.25, std::min (.5, decay + .15));
            haveTaps = false; tapSet.clear();
        } else {
            const double len = (P == "ambience" ? .04 + size * .25 : .1 + size * .5) * fs; const int n = 96; std::vector<Tap> t;
            double seed = 7; auto rnd = [&seed] { seed = std::fmod (seed * 16807, 2147483647.0); return seed / 2147483647; };
            for (int i = 0; i < n; i++) {
                const double x = (i + rnd() * .9) / n, d = std::max (1.0, std::floor (x * len));
                const double env = P == "nonlin" ? (1 - .1 * x) * std::min (1.0, (1 - x) * len / fs / .02) : P == "reverse" ? x * x : std::exp (-x * 5);
                const double gL = env * (rnd() - .5) * 2; const double gR = env * (rnd() - .5) * 2;
                t.push_back ({ d, gL, gR });
            }
            double s = 0; for (auto& q : t) s += q.gL * q.gL; const double norm = 1.4 / std::sqrt (s);
            for (auto& q : t) { q.gL *= norm; q.gR *= norm; } tapSet = t; haveTaps = true; rt = len / fs;
        }
    }
    static double ap (DLine& dl, double n, double g, double x) { const double v = dl.at (n); const double w = x + g * v; dl.push (w); return v - g * w; }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const double scl = sc * (scale != 0 ? scale : 1); const double preN = std::max (0.0, std::min ((double) pre.m - 2, jr (c.orv ("pre", 0) / 1000 * fs)));
        const double ret = c.nz ("ret", .8);
        const double n1 = 142 * sc, n2 = 107 * sc, n3 = 379 * sc, n4 = 277 * sc;
        const double dl1 = 4453 * scl, al2 = 1800 * scl, dl2 = 3720 * scl, dr1 = 4217 * scl, ar2 = 2656 * scl, dr2 = 3163 * scl, exc = 16 * sc;
        auto k = [scl] (double s) { return jr (s * scl); };
        for (int i = 0; i < N; i++) {
            double x = a ? (a[i] + b[i]) * .5 : 0;
            x = bq::run (inLp2, bq::run (inLp, x)); if (lex) x = jr (x * 2048) / 2048;
            pre.push (x); x = pre.at (preN);
            double yl = 0, yr = 0;
            if (haveTaps) {
                taps.push (ap (ind[1].d, n2, .75, ap (ind[0].d, n1, .75, x)));
                for (auto& t : tapSet) { const double v = taps.at (t.d); yl += v * t.gL; yr += v * t.gR; }
            } else {
                bw += .7 * (x - bw);
                double d = ap (ind[0].d, jr (n1), .75, bw); d = ap (ind[1].d, jr (n2), .75, d);
                d = ap (ind[2].d, jr (n3), .625, d); d = ap (ind[3].d, jr (n4), .625, d);
                lfo += 2 * PI * 1 / fs; if (lfo > 6.2832) lfo -= 6.2832;
                const double mL = 672 * scl + exc * std::sin (lfo), mR = 908 * scl + exc * std::cos (lfo);
                double u = d + fbR, v = apL.lerp (mL); double w = u + dd1 * v; apL.push (w); u = v - dd1 * w;
                dL1.push (u); u = dL1.at (jr (dl1));
                dmL += (1 - damp) * (u - dmL); u = dmL * decay;
                u = ap (apL2, jr (al2), -dd2, u); dL2.push (u); const double oL = dL2.at (jr (dl2)) * decay;
                double r = d + fbL; v = apR.lerp (mR); w = r + dd1 * v; apR.push (w); r = v - dd1 * w;
                dR1.push (r); r = dR1.at (jr (dr1));
                dmR += (1 - damp) * (r - dmR); r = dmR * decay;
                r = ap (apR2, jr (ar2), -dd2, r); dR2.push (r); const double oR = dR2.at (jr (dr2)) * decay;
                fbL = oL; fbR = oR;
                yl = .6 * (dR1.at (k (266)) + dR1.at (k (2974)) - apR2.at (k (1913)) + dR2.at (k (1996)) - dL1.at (k (1990)) - apL2.at (k (187)) - dL2.at (k (1066)));
                yr = .6 * (dL1.at (k (353)) + dL1.at (k (3627)) - apL2.at (k (1228)) + dL2.at (k (2673)) - dR1.at (k (2111)) - apR2.at (k (335)) - dR2.at (k (121)));
            }
            L[i] = (float) (bq::run (outL, yl) * ret); R[i] = (float) (bq::run (outR, yr) * ret);
        }
    }
};

// ---- Lexicon PrimeTime 93 ----
class PrimeTime : public Unit {
public:
    DLine dl; LPN aa, rc; Hold h; double lfo = 0, fl = 0, yA = 0, yB = 0;
    PrimeTime (double fs_, const Val& cfg) : Unit (fs_, cfg), dl (std::ceil (fs_ * 2.1)), aa (fs_, 12000, 5), rc (fs_, 12000, 5) { update(); }
    void reset() override { dl.clear(); fl = yA = yB = 0; }
    double tail() override { return tailOf (std::max (c.orv ("dA", 0), c.orv ("dB", 0)), std::min (.85, c.orv ("fbA", 0) + c.orv ("fbB", 0))); }
    void update() override { const double bw = 12000 / c.orv ("mult", 1); aa.set (bw); rc.set (bw); }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const double bw = 12000 / c.orv ("mult", 1), clk = bw * 2.6 / fs, q = 2048;
        const double fa = 1 - std::exp (-2 * PI * c.orv ("hcut", 6000) / fs), inv = c.truthy ("inv") ? -1 : 1, ret = c.nz ("ret", .8), fbA = c.orv ("fbA", 0), fbB = c.orv ("fbB", 0), fbSum = fbA + fbB, fbScale = fbSum > .85 ? .85 / fbSum : 1;
        const double vr = c.orv ("vcoRate", .5), vd = c.orv ("vcoDepth", 0), dA = c.orv ("dA", 0), dB = c.orv ("dB", 0), mixA = c.nz ("mixA", 1), mixB = c.nz ("mixB", 1);
        for (int i = 0; i < N; i++) {
            lfo += vr / fs; if (lfo >= 1) lfo -= 1;
            const double tri = 1 - 2 * std::abs (2 * lfo - 1), k = 1 - .5 * vd * (tri * .5 + .5);
            const double x = (a ? (a[i] + b[i]) * .5 : 0) + fl * inv;
            double w = aa.run (x); w = h.run (w, clk); w = jr (sat (w) * q) / q;
            dl.push (w);
            yA = rc.run (dl.lerp (std::max (1.0, dA / 1000 * fs * k)));
            const double yb = dl.lerp (std::max (1.0, dB / 1000 * fs * k)); yB += (yb - yB) * .5;
            fl += fa * ((yA * fbA + yB * fbB) * fbScale - fl);
            L[i] = (float) (yA * mixA * ret); R[i] = (float) (yB * mixB * ret);
        }
    }
};

// ---- Deltalab Effectron ADM 1024 ----
class Effectron : public Unit {
public:
    DLine dl; double est = 0, step = .001; int hist = 0; double acc = 0, lfo = 0, preX = 0, de = 0; LPN rec; double fbl = 0;
    Effectron (double fs_, const Val& cfg) : Unit (fs_, cfg), dl (std::ceil (fs_ * 4.2)), rec (fs_, 15000, 4) { update(); }
    void reset() override { dl.clear(); est = fbl = de = 0; step = .001; }
    double tail() override { return tailOf (c.orv ("delay", 0), c.orv ("regen", 0)) * 5; }
    static double bitsOf (const std::string& r) {
        if (r == "fhi") return 256; if (r == "fmid") return 1024; if (r == "flo") return 4096; if (r == "dbl") return 16384; if (r == "short") return 65536; if (r == "long") return 262144; return 16384;
    }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const double bits = bitsOf (c.str ("range")), ret = c.nz ("ret", .8);
        const double fbA = 1 - std::exp (-2 * PI * 12000 / fs), grit = c.nz ("grit", .5);
        const double lr = c.orv ("lfoRate", .5), width = c.orv ("width", 0), delay = c.orv ("delay", 30), regen = c.orv ("regen", 0); const std::string m = c.str ("pan", "center");
        for (int i = 0; i < N; i++) {
            lfo += lr / fs; if (lfo >= 1) lfo -= 1;
            const double sweep = std::pow (5.0, -width * (.5 + .5 * std::sin (2 * PI * lfo)));
            const double dMs = std::max (.25, delay * sweep), clock = std::min (1e6, bits / (dMs / 1000));
            double x = (a ? (a[i] + b[i]) * .5 : 0) + fbl;
            const double pe = x + 2.2 * (x - preX); preX = x;
            acc += clock / fs; double sum = 0; int n = 0;
            const double up = 1.35 + grit * .4, down = .9 - grit * .06, smin = 2e-4 * (1 + grit * 6);
            while (acc >= 1) {
                acc -= 1;
                const int bit = pe >= est ? 1 : 0; hist = ((hist << 1) | bit) & 7;
                step = (hist == 7 || hist == 0) ? std::min (.3, step * up) : std::max (smin, step * down);
                est = est * .9998 + (bit ? step : -step); sum += est; n++;
            }
            const double dec = n ? sum / n : est;
            de += (dec - de) * .31; dl.push (de);
            const double y = rec.run (dl.lerp (std::max (1.0, dMs / 1000 * fs)));
            fbl += fbA * (y * std::max (-.9, std::min (.9, regen)) - fbl);
            const double v = y * ret;
            if (m == "haasL") { L[i] = (float) v; R[i] = 0; } else if (m == "haasR") { L[i] = 0; R[i] = (float) v; } else if (m == "wide") { L[i] = (float) v; R[i] = (float) -v; } else { L[i] = (float) (v * .7); R[i] = (float) (v * .7); }
        }
    }
};

// ---- Ensoniq DP/4 ----
inline double dpq (double x) { const double Q = 1.0 / 8388608; return jr (x / Q) * Q; }
class DPUnit {
public:
    double fs; std::string alg = "off"; DLine dl[2]; double ph = 0, lp[2] = { 0, 0 }; float ap[16] = {}; double fb[2] = { 0, 0 };
    struct Comb { DLine d; double n, f; }; struct Ap { DLine d; double n; };
    std::vector<Comb> combs[2]; std::vector<Ap> aps[2]; DLine pre[2]; double pp = 0, sh[2] = { 0, 0 };
    explicit DPUnit (double fs_) : fs (fs_), dl { DLine (std::ceil (fs_ * 2.1)), DLine (std::ceil (fs_ * 2.1)) }, pre { DLine (std::ceil (fs_ * .2)), DLine (std::ceil (fs_ * .2)) } {
        const double s = fs / 44100;
        for (int c = 0; c < 2; c++) {
            for (double n : { 1116.0, 1188.0, 1277.0, 1356.0, 1422.0, 1491.0 }) combs[c].push_back ({ DLine (std::ceil (n * s * 2.2 + 50)), jr (n * s), 0 });
            for (double n : { 556.0, 441.0, 341.0, 225.0 }) aps[c].push_back ({ DLine (std::ceil (n * s + 30)), jr (n * s * (1 + c * .04)) });
        }
    }
    void clear() { for (auto& d : dl) d.clear(); for (auto& cc : combs) for (auto& cb : cc) { cb.d.clear(); cb.f = 0; } for (auto& aa : aps) for (auto& a : aa) a.d.clear(); for (auto& d : pre) d.clear(); fb[0] = fb[1] = 0; lp[0] = lp[1] = 0; std::fill (ap, ap + 16, 0.f); }
    double* run (double l, double r, double x, double y) {
        const std::string& A = alg; double* out = sh;
        if (A == "hall" || A == "plate" || A == "room") {
            const double size = A == "hall" ? 1 + x * 1.2 : A == "plate" ? .7 + x * .7 : .35 + x * .5, damp = A == "plate" ? .15 + y * .35 : .25 + y * .5, fbk = std::min (.97, .7 + .26 * std::min (1.0, size / 2.2));
            const double pd = jr ((A == "hall" ? .03 : A == "room" ? .005 : .012) * fs);
            for (int c = 0; c < 2; c++) {
                DLine& P = pre[c]; P.push (c ? r : l); double inp = P.at (pd) * .3, sum = 0;
                for (auto& cb : combs[c]) { const double n = jr (cb.n * size), o = cb.d.at (n); cb.f = o * (1 - damp) + cb.f * damp; cb.d.push (inp + cb.f * fbk); sum += o; }
                for (auto& a : aps[c]) { const double o = a.d.at (a.n); a.d.push (sum + o * .5); sum = o - sum * .5; }
                out[c] = sum * (A == "plate" ? .9 : .7);
            }
            return out;
        }
        if (A == "chorus" || A == "flange") {
            ph += (.08 + x * (A == "chorus" ? 2.5 : 1.2)) / fs; if (ph > 1) ph -= 1;
            for (int c = 0; c < 2; c++) {
                const double lfo = std::sin (2 * PI * (ph + c * .25)), base = A == "chorus" ? .012 : .0025, dep = A == "chorus" ? .004 : .002 * (.3 + y);
                const double d = (base + dep * (1 + lfo) * .5) * fs, v = dl[c].lerp (d); dl[c].push ((c ? r : l) + (A == "flange" ? v * (y * 1.7 - .85) : 0)); out[c] = A == "chorus" ? v * (.5 + y * .5) : v;
            }
            return out;
        }
        if (A == "phaser") {
            ph += (.05 + x * 3) / fs; if (ph > 1) ph -= 1;
            for (int c = 0; c < 2; c++) {
                const double sw = .5 - .5 * std::cos (2 * PI * (ph + c * .2)), f = 150 * std::pow (25.0, sw), t = std::tan (PI * f / fs), a1 = (t - 1) / (t + 1);
                double v = (c ? r : l) + fb[c] * (y * .9);
                for (int s = 0; s < 6; s++) { const int k = c * 8 + s; const double o = a1 * v + ap[k]; ap[k] = (float) (v - a1 * o); v = o; }
                fb[c] = v; out[c] = ((c ? r : l) + v) * .6;
            }
            return out;
        }
        if (A == "delay" || A == "pingpong") {
            const double d = std::max (1.0, (.02 + x * 1.18) * fs), fbk = y * .9;
            if (A == "delay") { for (int c = 0; c < 2; c++) { const double v = dl[c].at (jr (d * (c ? 1.02 : 1))); lp[c] += (v - lp[c]) * .35; dl[c].push ((c ? r : l) + lp[c] * fbk); out[c] = v; } }
            else { const double vL = dl[0].at (jr (d)), vR = dl[1].at (jr (d)); dl[0].push ((l + r) * .5 + vR * fbk); dl[1].push (vL * fbk); out[0] = vL; out[1] = vR; }
            return out;
        }
        if (A == "pitch") {
            const double semis = jr ((x - .5) * 24), ratio = std::pow (2.0, semis / 12), W = .06 * fs; pp = std::fmod (pp + (1 - ratio) + W, W);
            for (int c = 0; c < 2; c++) {
                dl[c].push (c ? r : l); const double d1 = pp + 2, d2 = std::fmod (pp + W / 2, W) + 2, g1 = std::sin (PI * pp / W), g2 = std::sin (PI * std::fmod (pp + W / 2, W) / W);
                out[c] = (dl[c].lerp (d1) * g1 + dl[c].lerp (d2) * g2) * .8 + (c ? r : l) * y * .5;
            }
            return out;
        }
        if (A == "dist") {
            const double g = 1 + x * 40, tone = .05 + y * .6;
            for (int c = 0; c < 2; c++) { const double v = std::tanh ((c ? r : l) * g) * .6; lp[c] += (v - lp[c]) * tone; out[c] = lp[c]; }
            return out;
        }
        if (A == "rotary") {
            ph += (.7 + x * 6) / fs; if (ph > 1) ph -= 1; const double m = (l + r) * .5, s = std::sin (2 * PI * ph), cs = std::cos (2 * PI * ph);
            dl[0].push (m); const double v = dl[0].lerp ((.002 + .0012 * (1 + s)) * fs); out[0] = v * (1 + .5 * y * cs) * .7; out[1] = v * (1 - .5 * y * cs) * .7; return out;
        }
        if (A == "filter") {
            ph += (.05 + x * 4) / fs; if (ph > 1) ph -= 1;
            for (int c = 0; c < 2; c++) {
                const double f = 120 * std::pow (60.0, .5 - .5 * std::cos (2 * PI * ph)), g = 2 * std::sin (PI * std::min (f, fs / 6) / fs), q = 1.9 - y * 1.75; const int k = c * 8;
                const double hp = (c ? r : l) - ap[k] - q * ap[k + 1]; ap[k + 1] = (float) (ap[k + 1] + g * hp); ap[k] = (float) (ap[k] + g * ap[k + 1]); out[c] = ap[k] * .8;
            }
            return out;
        }
        out[0] = 0; out[1] = 0; return out;
    }
};
class DP4 : public Unit {
public:
    std::vector<std::unique_ptr<DPUnit>> u; double v[2] = { 0, 0 }, w2[2] = { 0, 0 };
    DP4 (double fs_, const Val& cfg) : Unit (fs_, cfg) { for (int k = 0; k < 4; k++) u.push_back (std::make_unique<DPUnit> (fs)); update(); }
    void update() override {
        for (int k = 0; k < 4; k++) { const std::string key = "a" + std::to_string (k + 1); const std::string a = c.str (key.c_str(), "off"); if (u[(size_t) k]->alg != a) { u[(size_t) k]->alg = a; u[(size_t) k]->clear(); } }
    }
    void reset() override { for (auto& x : u) x->clear(); }
    double tail() override { return 8; }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const std::string cfg = c.str ("cfg", "par"); const double ret = c.nz ("ret", .8);
        double P[4][3];
        for (int k = 0; k < 4; k++) { const std::string s = std::to_string (k + 1); P[k][0] = c.nz (("m" + s).c_str(), 1); P[k][1] = c.nz (("x" + s).c_str(), .5); P[k][2] = c.nz (("y" + s).c_str(), .5); }
        auto step = [&] (int k, double* o) { DPUnit& un = *u[(size_t) k]; if (un.alg == "off") return; const double m = P[k][0]; double* r2 = un.run (o[0], o[1], P[k][1], P[k][2]); o[0] = dpq (o[0] * (1 - m) + r2[0] * m); o[1] = dpq (o[1] * (1 - m) + r2[1] * m); };
        for (int i = 0; i < N; i++) {
            const double l = a ? a[i] : 0, r = b ? b[i] : 0; double oL = 0, oR = 0;
            if (cfg == "ser") { v[0] = l; v[1] = r; for (int k = 0; k < 4; k++) step (k, v); oL = v[0]; oR = v[1]; }
            else if (cfg == "2x2") { v[0] = l; v[1] = r; step (0, v); step (1, v); w2[0] = l; w2[1] = r; step (2, w2); step (3, w2); oL = (v[0] + w2[0]) * .5; oR = (v[1] + w2[1]) * .5; }
            else { int n = 0; for (int k = 0; k < 4; k++) { if (u[(size_t) k]->alg == "off") continue; v[0] = l; v[1] = r; step (k, v); oL += v[0]; oR += v[1]; n++; } if (n) { oL /= std::sqrt ((double) n); oR /= std::sqrt ((double) n); } }
            L[i] = (float) (oL * ret); R[i] = (float) (oR * ret);
        }
    }
};

// ---- Alesis Microverb ----
class Microverb : public Unit {
public:
    struct Comb { DLine d; double n, f; }; struct Ap { DLine d; double n; };
    std::vector<Comb> cb[2]; std::vector<Ap> ap[2]; DLine pre[2]; double bl[4] = { 0, 0, 0, 0 }, gate = 0, o[2] = { 0, 0 };
    Microverb (double fs_, const Val& cfg) : Unit (fs_, cfg), pre { DLine (std::ceil (fs_ * 1.2)), DLine (std::ceil (fs_ * 1.2)) } {
        const double sc = fs / 44100;
        for (int c = 0; c < 2; c++) {
            for (double n : { 1557.0, 1617.0, 1491.0, 1422.0, 1277.0, 1356.0 }) cb[c].push_back ({ DLine (std::ceil (n * sc * 2.6) + 10), jr (n * sc * (1 + c * .023)), 0 });
            for (double n : { 225.0, 556.0, 441.0, 341.0 }) ap[c].push_back ({ DLine (std::ceil (n * sc) + 10), jr (n * sc * (1 + c * .03)) });
        }
        update();
    }
    void reset() override { for (auto& cc : cb) for (auto& c2 : cc) { c2.d.clear(); c2.f = 0; } for (auto& aa : ap) for (auto& a : aa) a.d.clear(); for (auto& p : pre) p.clear(); }
    double tail() override { return 6; }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const int P = (int) c.orv ("prog", 0); const double ret = c.nz ("ret", .8);
        static const double T[16][4] = { { .45, .5, 2, 0 }, { .6, .45, 6, 0 }, { .8, .4, 12, 0 }, { 1, .45, 18, 0 }, { 1.25, .4, 25, 0 }, { 1.55, .35, 35, 0 }, { .9, .15, 5, 0 }, { 1.1, .55, 8, 0 },
            { 1.8, .3, 45, 0 }, { 2.2, .25, 60, 0 }, { .7, .3, 0, 1 }, { 1, .25, 0, 1 }, { .8, .3, 0, 2 }, { 1.2, .3, 0, 2 }, { .35, .6, 0, 0 }, { 1.4, .7, 20, 0 } };
        static const double Tdef[4] = { 1, .4, 10, 0 };
        const double* Tp = (P >= 0 && P < 16) ? T[P] : Tdef;
        const double size = Tp[0], damp = Tp[1], pd = Tp[2]; const int kind = (int) Tp[3];
        const double fbk = std::min (.975, .72 + size * .14), pdS = jr (pd / 1000 * fs), bw = 1 - std::exp (-2 * PI * 11000 / fs);
        for (int i = 0; i < N; i++) {
            const double inL = a ? a[i] : 0, inR = b ? b[i] : 0; const double lev = std::abs (inL) + std::abs (inR); gate = std::max (lev > .01 ? 1.0 : 0.0, gate * std::exp (-1 / (fs * .25)));
            for (int c = 0; c < 2; c++) {
                DLine& P2 = pre[c]; const double x = c ? inR : inL; bl[c] += (x - bl[c]) * bw; P2.push (bl[c]);
                double inp;
                if (kind == 2) { const double len = jr (fs * .35 * size); double accv = 0; for (int k = 0; k < 4; k++) { const double d = jr (len * (k + 1) / 4); accv += P2.at (d) * (k + 1) / 4; } inp = accv * .25; }
                else inp = P2.at (pdS) * .3;
                double s = 0; for (auto& cv : cb[c]) { const double o2 = cv.d.at (jr (cv.n * size)); cv.f = o2 * (1 - damp) + cv.f * damp; cv.d.push (inp + cv.f * fbk); s += o2; }
                for (auto& av : ap[c]) { const double o2 = av.d.at (av.n); av.d.push (s + o2 * .5); s = o2 - s * .5; }
                if (kind == 1) s *= gate;
                o[c] = jr (s * .45 * 32767) / 32767;
            }
            L[i] = (float) (o[0] * ret); R[i] = (float) (o[1] * ret);
        }
    }
};

// ---- A/DA Flanger ----
class ADAFlanger : public Unit {
public:
    DLine dl; LPN aa, rc; Hold hi, ho; double lfo = .25, fb = 0, ce = 1e-3, ee = 1e-3, gate = 0, ge = 0, dc = 0, ldel = 0;
    ADAFlanger (double fs_, const Val& cfg) : Unit (fs_, cfg), dl (std::ceil (fs_ * .03)), aa (fs_, 11000, 4), rc (fs_, 11000, 4) {}
    void reset() override { dl.clear(); fb = 0; ce = ee = 1e-3; gate = ge = 0; }
    double tail() override { return .6 + std::abs (c.orv ("enh", 0)) * 2; }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const double ret = c.nz ("ret", .8), man = c.nz ("manual", .4), rng = c.nz ("range", .6);
        const double rate = .02 * std::pow (400.0, c.nz ("speed", .3)), enh = std::min (.97, c.orv ("enh", 0)) * (c.truthy ("odd") ? -1 : 1);
        const double thr = c.truthy ("thresh") ? std::pow (10.0, (-80 + c.orv ("thresh", 0) * 60) / 20) : 0, mix = c.nz ("mix", .5); const bool st = c.truthy ("stereo");
        const double ca = 1 - std::exp (-1 / (fs * .012)), ga = 1 - std::exp (-1 / (fs * .002)), gr = 1 - std::exp (-1 / (fs * .15));
        for (int i = 0; i < N; i++) {
            const double x0 = a ? (a[i] + b[i]) * .5 : 0;
            const double lv = std::abs (x0); ge += (lv > ge ? ga : gr) * (lv - ge); const double go = thr == 0 || ge > thr ? 1 : 0; gate += (go - gate) * (go != 0 ? .01 : .0004);
            const double x = x0 * gate;
            lfo += rate / fs; if (lfo >= 1) lfo -= 1; const double tri = 1 - 2 * std::abs (2 * lfo - 1);
            const double dMax = .25 * std::pow (40.0, man), dMs = std::max (.25, dMax / std::pow (1 + 39 * rng, tri * .5 + .5)), clk = 512 / (2 * dMs / 1000);
            ldel += (dMs - ldel) * .002;
            double v = aa.run (x + sat (fb));
            ce += ca * (std::abs (v) - ce); v = v / std::sqrt (std::max (ce, 1e-4)) * .1;
            const double r = clk / fs; v = r < 1 ? hi.run (v, r) : v; dl.push (v);
            double y = dl.lerp (std::max (1.0, ldel / 1000 * fs)) * .985 + (rng_.next() - .5) * 2e-4;
            y = r < 1 ? ho.run (y, r) : y; y = rc.run (y);
            ee += ca * (std::abs (y) - ee); y = y * ee * 100;
            dc += (y - dc) * .0005; y -= dc;
            fb = y * enh;
            const double wet = y * mix * 1.4, dry = x * (1 - mix) * 1.4;
            L[i] = (float) ((dry + wet) * ret); R[i] = (float) ((dry + (st ? -wet : wet)) * ret);
        }
    }
    Rng rng_;
};

// ---- UREI 1176LN ----
class UREI1176 : public Unit {
public:
    double gr = 0, slow = 0, yp = 0, hpL = 0, hpR = 0, dcL = 0, dcR = 0; bool ins = false;
    UREI1176 (double fs_, const Val& cfg) : Unit (fs_, cfg) {}
    void reset() override { gr = slow = yp = 0; }
    double tail() override { return ins ? 1e9 : 2; }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const double inG = std::pow (10.0, (c.nz ("input", .5) * 50 - 20) / 20), outG = std::pow (10.0, (c.nz ("output", .5) * 40 - 30) / 20);
        const Val* rv = c.get ("ratio"); const bool all = rv && rv->isStr() && rv->str() == "all"; const double ratio = all ? 20 : (rv && rv->truthy() ? rv->num() : 4), thr = .126;
        const double atk = (20 + (7 - c.nz ("attack", 4)) / 6 * 780) * 1e-6 * (all ? 1.6 : 1), rel = (.05 + (7 - c.nz ("release", 4)) / 6 * 1.05);
        const double aA = 1 - std::exp (-1 / (fs * atk)), aR = 1 - std::exp (-1 / (fs * rel)), aS = 1 - std::exp (-1 / (fs * rel * 5)), hpA = 1 - std::exp (-2 * PI * 18 / fs);
        const double mix = c.nz ("mix", 1), slope = 1 - 1 / ratio;
        for (int i = 0; i < N; i++) {
            double l = a ? a[i] : 0, r = b ? b[i] : l; const double dl = l, dr = r;
            hpL += hpA * (l - hpL); hpR += hpA * (r - hpR); l -= hpL * .9; r -= hpR * .9;
            l = tnh (l * .5) / .5 * inG; r = tnh (r * .5) / .5 * inG;
            const double lv = std::max ({ std::abs (l), std::abs (r), 1e-6 }), over = 20 * std::log10 (lv / thr);
            const double tgt = over > 0 ? std::min (40.0, over * slope * (all ? 1.25 : 1) + (all ? 2 : 0)) : 0;
            if (tgt > gr) gr += std::min (.9, aA * ratio * .3) * (tgt - gr); else { slow += aS * (gr - slow); gr += aR * (std::max (tgt, slow * .6) - gr); }
            const double g = std::pow (10.0, -gr / 20), d2 = std::min (.25, gr * .006) * (all ? 2.2 : 1);
            double yl = l * g, yr = r * g; yl += d2 * yl * yl; yr += d2 * yr * yr;
            yl = tnh (yl * 1.2 + .1 * yl * yl) / 1.2 * outG * 2.7; yr = tnh (yr * 1.2 + .1 * yr * yr) / 1.2 * outG * 2.7;
            dcL += (yl - dcL) * .0004; dcR += (yr - dcR) * .0004; yl -= dcL; yr -= dcR;
            const double ret = ins ? 1 : c.nz ("ret", .8);
            L[i] = (float) ((yl * mix + dl * (1 - mix)) * ret); R[i] = (float) ((yr * mix + dr * (1 - mix)) * ret);
        }
    }
};

// ---- the gated drum room ----
class PCSim : public Unit {
public:
    DLine er; DLine fd[4]; double lp[4] = { 0, 0, 0, 0 }, ce = 0, ke = 0, hold = 0, gg = 0, hp[2] = { 0, 0 };
    PCSim (double fs_, const Val& cfg) : Unit (fs_, cfg), er (std::ceil (fs_ * .25)), fd { DLine (std::ceil (fs_ * .25)), DLine (std::ceil (fs_ * .25)), DLine (std::ceil (fs_ * .25)), DLine (std::ceil (fs_ * .25)) } {}
    void reset() override { er.clear(); for (auto& d : fd) d.clear(); std::fill (lp, lp + 4, 0.0); ce = ke = gg = hold = 0; }
    double tail() override { return 1 + c.orv ("decay", 2); }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const double size = .35 + c.nz ("size", .7) * .9, rt = c.orv ("decay", 2), crush = c.nz ("crush", .8);
        static const double ER[10] = { 7.1, 11.3, 13.9, 17.2, 19.7, 23.1, 29.3, 31.7, 37.1, 41.9 }, EG[10] = { .9, .8, .75, .7, .62, .55, .5, .44, .4, .35 };
        double FL[4], FG[4]; const double fl0[4] = { 29.7, 37.1, 41.1, 43.7 };
        for (int k = 0; k < 4; k++) { FL[k] = jr (fl0[k] * size * fs / 1000); FG[k] = std::pow (10.0, -3 * FL[k] / fs / rt); }
        const double dampA = .25 + c.nz ("tone", .6) * .7, hpA = 1 - std::exp (-2 * PI * 110 / fs);
        const double cA = 1 - std::exp (-1 / (fs * .0002)), cR = 1 - std::exp (-1 / (fs * (.04 + (1 - crush) * .25))), cthr = std::pow (10.0, (-18 - crush * 24) / 20), cmk = std::pow (10.0, crush * 26 / 20);
        const double thr = std::pow (10.0, c.nz ("thresh", -30) / 20), holdN = c.nz ("hold", 280) / 1000 * fs, rel = 1 - std::exp (-1 / (fs * std::max (.001, c.nz ("snap", 8) / 1000)));
        const double kA = 1 - std::exp (-1 / (fs * .0005)), kR = 1 - std::exp (-1 / (fs * .02)), range = std::pow (10.0, -c.nz ("range", 60) / 20), ret = c.nz ("ret", .8), dir = c.orv ("direct", 0);
        for (int i = 0; i < N; i++) {
            const double x = a ? (a[i] + b[i]) * .5 : 0;
            er.push (x); double el = 0, eR = 0;
            for (int k = 0; k < 10; k++) { const double v = er.at (jr (ER[k] * size * fs / 1000)) * EG[k]; if (k & 1) eR += v; else el += v; }
            double o[4]; for (int k = 0; k < 4; k++) o[k] = fd[k].at (FL[k]);
            for (int k = 0; k < 4; k++) lp[k] += dampA * (o[k] - lp[k]);
            const double* p = lp; const double h[4] = { p[0] + p[1] + p[2] + p[3], p[0] - p[1] + p[2] - p[3], p[0] + p[1] - p[2] - p[3], p[0] - p[1] - p[2] + p[3] };
            const double inj = (el + eR) * .25 + x * .3;
            for (int k = 0; k < 4; k++) fd[k].push (inj + h[k] * .5 * FG[k]);
            double wl = el * .5 + o[0] + o[2] * .6, wr = eR * .5 + o[1] + o[3] * .6;
            hp[0] += hpA * (wl - hp[0]); wl -= hp[0]; hp[1] += hpA * (wr - hp[1]); wr -= hp[1];
            const double lv = std::max (std::abs (wl), std::abs (wr)); ce += (lv > ce ? cA : cR) * (lv - ce);
            const double cg = ce > cthr ? std::pow (cthr / ce, .95) : 1; wl = tnh (wl * cg * cmk); wr = tnh (wr * cg * cmk);
            const double kl = std::abs (x); ke += (kl > ke ? kA : kR) * (kl - ke);
            if (ke > thr) { hold = holdN; gg += (1 - gg) * .5; } else if (hold > 0) hold--; else gg += (range - gg) * rel;
            L[i] = (float) ((wl * gg * .5 + x * dir) * ret); R[i] = (float) ((wr * gg * .5 + x * dir) * ret);
        }
    }
};

// ---- Eventide H910 ----
class H910 : public Unit {
public:
    DLine dl[2]; double p[2] = { 0, 0 }, fb[2] = { 0, 0 }, env = 1e-3, envO[2] = { 1e-3, 1e-3 }; LPN lp0, lp1;
    H910 (double fs_, const Val& cfg) : Unit (fs_, cfg), dl { DLine (std::ceil (fs_ * .2)), DLine (std::ceil (fs_ * .2)) }, lp0 (fs_, 9000, 4), lp1 (fs_, 9000, 4) { update(); }
    void reset() override { dl[0].clear(); dl[1].clear(); fb[0] = fb[1] = 0; p[0] = p[1] = 0; }
    double tail() override { return tailOf (std::max (c.orv ("dL", 0), c.orv ("dR", 0)) + 30, c.orv ("fb", 0)); }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const double W = .03 * fs, cents = c.nz ("cents", 9); const bool smooth = c.str ("splice") == "smooth";
        const double ratio[2] = { std::pow (2.0, cents / 1200), std::pow (2.0, -cents / 1200) }, del[2] = { c.orv ("dL", 0) / 1000 * fs, c.orv ("dR", 0) / 1000 * fs };
        const double sp = c.nz ("spread", 1), ret = c.nz ("ret", .8), q = 1024, fbv = c.orv ("fb", 0);
        const double atk = 1 - std::exp (-1 / (.004 * fs)), rel = 1 - std::exp (-1 / (.08 * fs));
        double out[2] = { 0, 0 }; LPN* lp[2] = { &lp0, &lp1 };
        for (int i = 0; i < N; i++) {
            const double x0 = a ? (a[i] + b[i]) * .5 : 0;
            for (int c2 = 0; c2 < 2; c2++) {
                const double x = x0 + fb[c2] * std::min (.85, fbv) * .9;
                const double ax = std::abs (x); env += (ax > env ? atk : rel) * (ax - env);
                const double g = 1 / std::sqrt (std::max (env, 1e-4)) * .1;
                dl[c2].push (jr (sat (x * g) * q) / q);
                p[c2] += 1 - ratio[c2]; if (p[c2] < 0) p[c2] += W; else if (p[c2] >= W) p[c2] -= W;
                double y;
                if (smooth) { const double p2 = std::fmod (p[c2] + W / 2, W), w1 = 1 - std::abs (2 * p[c2] / W - 1), w2 = 1 - w1; y = dl[c2].lerp (del[c2] + p[c2] + 2) * w1 + dl[c2].lerp (del[c2] + p2 + 2) * w2; }
                else y = dl[c2].lerp (del[c2] + p[c2] + 2);
                y = lp[c2]->run (y);
                const double ay = std::abs (y); envO[c2] += (ay > envO[c2] ? atk * .5 : rel * .5) * (ay - envO[c2]);
                y *= std::min (600.0, envO[c2] * 100);
                fb[c2] = sat (y * .8) / .8; out[c2] = y;
            }
            const double l = out[0] * (.5 + .5 * sp) + out[1] * (.5 - .5 * sp), r = out[1] * (.5 + .5 * sp) + out[0] * (.5 - .5 * sp);
            const double wg = c.nz ("wings", .7) * ret; L[i] = (float) (l * wg); R[i] = (float) (r * wg);
        }
    }
};

// ---- MXR 113 ----
class MXR113 : public Unit {
public:
    DLine dl; LPN aa, rc; Hold h; double env = 1e-3, envO = 1e-3, pe = 0, de = 0, fb = 0, lfo = 0, ceil_ = 0;
    MXR113 (double fs_, const Val& cfg) : Unit (fs_, cfg), dl (std::ceil (fs_ * 1.35)), aa (fs_, 20000, 6, { .52, .71, 1.93 }), rc (fs_, 20000, 4, { .54, 1.31 }) { update(); }
    void reset() override { dl.clear(); fb = de = pe = 0; }
    double tail() override { return tailOf (c.orv ("delay", 0), c.orv ("regen", 0)); }
    void update() override { const double bw = c.orv ("bw", 10000); aa.set (std::min (bw, fs * .45)); rc.set (std::min (bw, fs * .45)); }
    void run (const float* a, const float* b, float* L, float* R, int N) override {
        const double bw = c.orv ("bw", 10000), ret = c.nz ("ret", .8), q = 512;
        const double atk = 1 - std::exp (-1 / (.005 * fs)), rel = 1 - std::exp (-1 / (.05 * fs)), clk = std::min (1.0, bw * 2.5 / fs);
        const double regen = c.orv ("regen", 0), delay = c.orv ("delay", 100);
        const double decay = std::pow (std::max (.05, std::min (.9, regen)), 1 / std::max (1.0, delay / 1000 * fs));
        const double lr = c.orv ("lfoRate", .3), ld = c.orv ("lfoDepth", 0), inv = c.truthy ("inv") ? -1 : 1;
        for (int i = 0; i < N; i++) {
            lfo += lr / fs; if (lfo >= 1) lfo -= 1;
            const double k = std::pow (4.0, -ld * (.5 + .5 * std::sin (2 * PI * lfo)));
            const double xin = a ? (a[i] + b[i]) * .5 : 0;
            ceil_ = std::max (ceil_ * decay, std::abs (xin)); double x = xin + fb;
            const double p = x + 1.5 * (x - pe); pe = x; x = p;
            const double ax = std::abs (x); env += (ax > env ? atk : rel) * (ax - env);
            x = x / std::sqrt (std::max (env, 1e-4)) * .12;
            x = std::max (-.9, std::min (.9, x));
            x = aa.run (x); x = h.run (x, clk / k); x = jr (x * q) / q;
            dl.push (x);
            double y = rc.run (dl.lerp (std::max (1.0, delay / 1000 * fs * k)));
            const double ay = std::abs (y); envO += (ay > envO ? atk : rel) * (ay - envO);
            y *= std::min (300.0, envO * 58);
            de += (y - de) * .4; y = de;
            double f = sat (y * .8) / .8 * std::min (.9, regen) * .92; const double af = std::abs (f), lim = ceil_ * 1.3 + 1e-6; if (af > lim) f *= lim / af; fb = f;
            const double v = y * ret * inv; L[i] = (float) v; R[i] = (float) v;
        }
    }
};

inline std::unique_ptr<Unit> make (const std::string& u, double fs, const Val& cfg) {
    if (u == "lx") return std::make_unique<Verb224> (fs, cfg);
    if (u == "pt") return std::make_unique<PrimeTime> (fs, cfg);
    if (u == "ef") return std::make_unique<Effectron> (fs, cfg);
    if (u == "dp") return std::make_unique<DP4> (fs, cfg);
    if (u == "mv") return std::make_unique<Microverb> (fs, cfg);
    if (u == "ad") return std::make_unique<ADAFlanger> (fs, cfg);
    if (u == "u7") return std::make_unique<UREI1176> (fs, cfg);
    if (u == "pc") return std::make_unique<PCSim> (fs, cfg);
    if (u == "h9") return std::make_unique<H910> (fs, cfg);
    if (u == "mx") return std::make_unique<MXR113> (fs, cfg);
    return nullptr;
}

// two slots on the FX send, series (A into B) or parallel
class Rack {
public:
    double fs; std::string route = "ser"; std::string kind[2]; std::unique_ptr<Unit> slot[2];
    std::vector<float> aL, aR, bL, bR;
    explicit Rack (double fs_) : fs (fs_) {}
    bool empty() const { return ! slot[0] && ! slot[1]; }
    void set (const Val& m) {
        route = m["route"].isStr() ? m["route"].str() : std::string ("ser");
        for (int i = 0; i < 2; i++) {
            const Val& s = m["slots"][(size_t) i];
            const std::string u = s.isObj() ? s["u"].str() : std::string();
            if (u.empty()) { slot[i].reset(); kind[i].clear(); continue; }
            if (! slot[i] || kind[i] != u) { slot[i] = make (u, fs, s["cfg"]); kind[i] = slot[i] ? u : std::string(); }
            else slot[i]->setCfg (s["cfg"]);
        }
    }
    // send: the mono FX send; out: the rack's stereo return (added to whatever is in L/R)
    void process (const float* send, float* L, float* R, int n) {
        if ((int) aL.size() < n) { aL.resize ((size_t) n); aR.resize ((size_t) n); bL.resize ((size_t) n); bR.resize ((size_t) n); }
        Unit* A = slot[0].get(); Unit* B = slot[1].get();
        if (route == "par" || ! A || ! B) {
            if (A) { A->process (send, send, aL.data(), aR.data(), n); for (int i = 0; i < n; i++) { L[i] += aL[(size_t) i]; R[i] += aR[(size_t) i]; } }
            if (B) { B->process (send, send, bL.data(), bR.data(), n); for (int i = 0; i < n; i++) { L[i] += bL[(size_t) i]; R[i] += bR[(size_t) i]; } }
        } else {
            A->process (send, send, aL.data(), aR.data(), n);
            B->process (aL.data(), aR.data(), bL.data(), bR.data(), n);
            for (int i = 0; i < n; i++) { L[i] += bL[(size_t) i]; R[i] += bR[(size_t) i]; }
        }
    }
};

} // namespace rack
} // namespace farp
