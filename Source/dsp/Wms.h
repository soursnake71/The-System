// Shared DSP helpers, the P-5 model and the E-II sampler (port of the page's WmsDsp, P5Engine, E2Engine).
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>
#include "Val.h"

namespace farp {

static constexpr double PI = 3.14159265358979323846;

struct Noise { int32_t s; explicit Noise(int32_t seed) : s(seed) {} };
struct Adsr { int stage = 0; double value = 0, a = .01, d = .2, s = .7, r = .3, sr = 48000; };
struct Cem { double s[4] = {0, 0, 0, 0}; double dc1 = 0, dc2 = 0; };
struct Ssm { double s[4] = {0, 0, 0, 0}, G[4] = {0, 0, 0, 0}, B[4] = {0, 0, 0, 0}; };
struct Biq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
struct Track { double fc = -1; Biq st[4]; };

struct WmsDsp {
    static double noise(Noise& st) {
        st.s ^= (int32_t)((uint32_t)st.s << 13);
        st.s ^= (int32_t)((uint32_t)st.s >> 17);
        st.s ^= (int32_t)((uint32_t)st.s << 5);
        return (double)st.s / 2147483648.0;
    }
    static double blep(double t, double dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1; }
        if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
        return 0;
    }
    static double hz(double n) { return 440 * std::pow(2.0, (n - 69) / 12); }
    static Adsr adsr(double sr) { Adsr e; e.sr = sr; return e; }
    static void gate(Adsr& e, bool on) { e.stage = on ? 1 : (e.stage == 0 ? 0 : 4); }
    static double next(Adsr& e) {
        switch (e.stage) {
            case 1: e.value += (1.3 - e.value) * (1 - std::exp(-1 / (std::max(.0005, e.a) * e.sr))); if (e.value >= 1) { e.value = 1; e.stage = 2; } break;
            case 2: e.value += (e.s - e.value) * (1 - std::exp(-1 / (std::max(.001, e.d) * e.sr * .3))); if (std::abs(e.value - e.s) < 1e-4) e.stage = 3; break;
            case 3: e.value = e.s; break;
            case 4: e.value += (0 - e.value) * (1 - std::exp(-1 / (std::max(.001, e.r) * e.sr * .3))); if (e.value < 1e-5) { e.value = 0; e.stage = 0; } break;
        }
        return e.value;
    }
    static double cem(Cem& f, double u, double cut, double res, double sr) {
        const double g = std::tan(PI * std::min(std::max(cut, 15.0), .45 * sr) / sr), G = g / (1 + g), beta = 1 / (1 + g), k = 4 * std::min(std::max(res, 0.0), 1.05);
        double* s = f.s;
        const double G2 = G * G, G4 = G2 * G2, S = G2 * G * beta * s[0] + G2 * beta * s[1] + G * beta * s[2] + beta * s[3];
        const double car = f.dc1 + f.dc2, y4 = (G4 * (u + k * car) + S) / (1 + k * G4); double x = u - k * (y4 - car);
        for (int i = 0; i < 4; i++) { const double v = (std::tanh(x) - s[i]) * G, y = v + s[i]; s[i] = y + v; x = y; }
        f.dc1 += (x - f.dc1) * (2 * PI * 1.06 / sr); f.dc2 += ((x - f.dc1) - f.dc2) * (2 * PI * 5.3 / sr); return x;
    }
    static double ssm2045(Ssm& f, double u, double cut, double res, double sr) {
        static const double P[4] = {1, 20 / 3.9, 1, 20 / 1.8};
        double* G = f.G; double* B = f.B;
        for (int i = 0; i < 4; i++) { const double g = std::tan(PI * std::min(std::max(cut * P[i], 15.0), .45 * sr) / sr); G[i] = g / (1 + g); B[i] = 1 / (1 + g); }
        const double k = 9.43 * std::min(std::max(res, 0.0), 1.0) * .98, Gp = G[0] * G[1] * G[2] * G[3]; double* s = f.s;
        const double S = G[1] * G[2] * G[3] * B[0] * s[0] + G[2] * G[3] * B[1] * s[1] + G[3] * B[2] * s[2] + B[3] * s[3], y4 = (Gp * u + S) / (1 + k * Gp);
        double x = std::tanh(u - k * y4); for (int i = 0; i < 4; i++) { const double v = (x - s[i]) * G[i], y = v + s[i]; s[i] = y + v; x = y; } return x;
    }
    static void trackSet(Track& t, double fc, double sr) {
        fc = std::min(std::max(fc, 200.0), .45 * sr); if (std::abs(fc - t.fc) < 1) return; t.fc = fc;
        static const double q[4] = {.5098, .6013, .9, 2.5629}; const double w = 2 * PI * fc / sr, cs = std::cos(w), sn = std::sin(w);
        for (int i = 0; i < 4; i++) { const double al = sn / (2 * q[i]), a0 = 1 + al; Biq& b = t.st[i]; b.b0 = (1 - cs) * .5 / a0; b.b1 = (1 - cs) / a0; b.b2 = b.b0; b.a1 = -2 * cs / a0; b.a2 = (1 - al) / a0; }
    }
    static double track(Track& t, double x) { for (auto& b : t.st) { const double y = b.b0 * x + b.z1; b.z1 = b.b1 * x - b.a1 * y + b.z2; b.z2 = b.b2 * x - b.a2 * y; x = y; } return x; }
};

// Reads a field of a JS-style parameter object into a double, if present.
inline void setNum(double& dst, const Val& p, const char* k) { const Val& v = p[k]; if (!v.isNull() || p.has(k)) dst = v.num(); }

class P5Engine {
public:
    struct P { double aSaw = 1, aPulse = 0, bSaw = 1, bTri = 0, bPulse = 0, sync = 0, bLow = 0, bKbd = 1, aPw = .5, bPw = .5, bSemi = 7, bFine = 6, mixA = .8, mixB = .6, mixNoise = 0,
        cutoff = 2000, res = .2, envAmt = .5, kbdTrack = .5, fA = .005, fD = .4, fS = .3, fR = .4, aA = .005, aD = .5, aS = .8, aR = .4, pmEnv = 0, pmOscB = 0, pmFreqA = 1, pmPwA = 0, pmFilter = 0,
        lfoRate = 5, lfoFreq = 0, lfoFilter = 0, glide = 0, slop = .3, level = .5, bend = 0, wheel = 0; } p;
    struct V { int note = -1; double cur = 60, vel = 1, phA = 0, phB = 0, off = 0, drift = 0; int age = 0; double cutS = 60; Adsr fE, aE; Cem f; };
    double sr, cvSmooth, lfoPh = 0; int counter = 0; int lastNote = -1; Noise nz{5}, slop{0x55};
    V v[5];
    explicit P5Engine(double sr_) : sr(sr_) {
        cvSmooth = 1 - std::exp(-1 / (.000212 * sr));
        for (auto& x : v) { x.off = WmsDsp::noise(slop) * .08; x.fE = WmsDsp::adsr(sr); x.aE = WmsDsp::adsr(sr); }
    }
    void setParams(const Val& o) {
#define F(k) setNum(p.k, o, #k);
        F(aSaw)F(aPulse)F(bSaw)F(bTri)F(bPulse)F(sync)F(bLow)F(bKbd)F(aPw)F(bPw)F(bSemi)F(bFine)F(mixA)F(mixB)F(mixNoise)F(cutoff)F(res)F(envAmt)F(kbdTrack)
        F(fA)F(fD)F(fS)F(fR)F(aA)F(aD)F(aS)F(aR)F(pmEnv)F(pmOscB)F(pmFreqA)F(pmPwA)F(pmFilter)F(lfoRate)F(lfoFreq)F(lfoFilter)F(glide)F(slop)F(level)F(bend)F(wheel)
#undef F
    }
    void noteOn(int n, double vel) {
        V* t = nullptr; for (auto& x : v) if (x.note == n && x.aE.stage) t = &x;
        if (!t) for (auto& x : v) if (!x.aE.stage) { t = &x; break; }
        if (!t) { t = &v[0]; for (auto& x : v) if (x.age < t->age) t = &x; }
        if (!t->aE.stage) t->cur = lastNote >= 0 ? lastNote : n;
        t->note = n; t->vel = vel; t->age = ++counter; WmsDsp::gate(t->fE, true); WmsDsp::gate(t->aE, true); lastNote = n;
    }
    void noteOff(int n) { for (auto& x : v) if (x.note == n) { WmsDsp::gate(x.fE, false); WmsDsp::gate(x.aE, false); x.note = -1; } }
    void allOff() { for (auto& x : v) { WmsDsp::gate(x.fE, false); WmsDsp::gate(x.aE, false); x.note = -1; } }
    bool active() const { for (auto& x : v) if (x.aE.stage) return true; return false; }
    double tick() {
        lfoPh += p.lfoRate / sr; lfoPh -= std::floor(lfoPh); const double lfo = 1 - 4 * std::abs(lfoPh - .5);
        double sum = 0; const double gc = p.glide <= 0 ? 1 : 1 - std::exp(-1 / (p.glide * sr)); const double PM = 150.0 / 200;
        for (auto& x : v) {
            if (!x.aE.stage) continue;
            x.fE.a = p.fA; x.fE.d = p.fD; x.fE.s = p.fS; x.fE.r = p.fR; x.aE.a = p.aA; x.aE.d = p.aD; x.aE.s = p.aS; x.aE.r = p.aR;
            const double fe = WmsDsp::next(x.fE), ae = WmsDsp::next(x.aE);
            const double tgt = x.note >= 0 ? x.note : std::round(x.cur); if (x.note >= 0) x.cur += gc * (tgt - x.cur);
            x.drift += .0005 * (WmsDsp::noise(slop) * .15 - x.drift);
            const double det = p.slop * (x.off + x.drift) + (p.lfoFreq + .5 * p.wheel) * lfo * .5 + p.bend;
            const double bNote = (p.bKbd != 0 ? x.cur : 48) + p.bSemi + p.bFine * .01 + det, bHz = p.bLow != 0 ? WmsDsp::hz(bNote) * .0055243 : WmsDsp::hz(bNote), bDt = std::min(.45, bHz / sr);
            x.phB += bDt; bool wrapB = false; if (x.phB >= 1) { x.phB -= 1; wrapB = true; }
            double bO = 0; if (p.bSaw != 0) bO += 2 * x.phB - 1 - WmsDsp::blep(x.phB, bDt); if (p.bTri != 0) bO += 1 - 4 * std::abs(x.phB - .5);
            if (p.bPulse != 0) { double pl = x.phB < p.bPw ? 1 : -1; pl += WmsDsp::blep(x.phB, bDt); double t2 = x.phB - p.bPw; if (t2 < 0) t2 += 1; pl -= WmsDsp::blep(t2, bDt); bO += PM * pl; }
            const double pm = p.pmEnv * fe + p.pmOscB * bO, aNote = x.cur + det + (p.pmFreqA != 0 ? pm * 24 : 0), aDt = std::min(.45, WmsDsp::hz(aNote) / sr);
            x.phA += aDt; if (x.phA >= 1) x.phA -= 1; if (p.sync != 0 && wrapB) x.phA = x.phB * aDt / std::max(1e-6, bDt);
            const double pwA = std::min(.95, std::max(.05, p.aPw + (p.pmPwA != 0 ? pm * .4 : 0))); double aO = 0; if (p.aSaw != 0) aO += 2 * x.phA - 1 - WmsDsp::blep(x.phA, aDt);
            if (p.aPulse != 0) { double pl = x.phA < pwA ? 1 : -1; pl += WmsDsp::blep(x.phA, aDt); double t2 = x.phA - pwA; if (t2 < 0) t2 += 1; pl -= WmsDsp::blep(t2, aDt); aO += PM * pl; }
            const double mix = p.mixA * aO + p.mixB * (p.bLow != 0 ? 0 : bO) + p.mixNoise * WmsDsp::noise(nz);
            const double cn = 12 * std::log2(p.cutoff / 261.63) + p.kbdTrack * (x.cur - 60) + p.envAmt * 84 * fe + p.lfoFilter * lfo * 24 + (p.pmFilter != 0 ? pm * 48 : 0);
            x.cutS += (cn - x.cutS) * cvSmooth;
            const double y = WmsDsp::cem(x.f, mix * .5, 261.63 * std::pow(2.0, x.cutS / 12), p.res, sr);
            sum += y * 1.39 * ae * (.4 + .6 * x.vel);
        }
        return sum * p.level * .5;
    }
};

struct Zone {
    std::shared_ptr<std::vector<float>> data;
    int lo = 0, hi = 127; double root = -1;
    // RomPcm extras
    double end = 0, ls = 0, rate = 0; int loop = 0; double vlo = -1, vhi = -1;
    size_t len() const { return data ? data->size() : 0; }
};

class E2Engine {
public:
    struct P { double cutoff = 8000, res = .1, envAmt = .3, a = .002, d = .5, s = 1, r = .3, tune = 0, level = .7, bend = 0, root = 60, loop = 0; } p;
    struct V { int note = -1; double play = 60, vel = 1, pos = 0; int age = 0; double vca = 0, cut = 8000; Adsr env; Ssm f; Track rc; };
    double sr, vcaS, cutS; int counter = 0; std::vector<Zone> zones; V v[8];
    explicit E2Engine(double sr_) : sr(sr_) { vcaS = 1 - std::exp(-1 / (.0047 * sr)); cutS = 1 - std::exp(-1 / (.0033 * sr)); for (auto& x : v) x.env = WmsDsp::adsr(sr); }
    void setParams(const Val& o) {
#define F(k) setNum(p.k, o, #k);
        F(cutoff)F(res)F(envAmt)F(a)F(d)F(s)F(r)F(tune)F(level)F(bend)F(root)F(loop)
#undef F
    }
    void setZones(std::vector<Zone> z) { zones = std::move(z); for (auto& x : v) { x.env.stage = 0; x.env.value = 0; } }
    const Zone* zoneFor(double n) const {
        for (auto& z : zones) if (n >= z.lo && n <= z.hi && z.len()) return &z;
        return zones.size() && zones[0].len() ? &zones[0] : nullptr;
    }
    void noteOn(int n, double vel) {
        V* t = nullptr; for (auto& x : v) if (!x.env.stage) { t = &x; break; }
        if (!t) { t = &v[0]; for (auto& x : v) if (x.age < t->age) t = &x; }
        t->note = n; t->play = n; t->vel = vel; t->pos = 0; t->age = ++counter; for (auto& s : t->f.s) s = 0; for (auto& b : t->rc.st) { b.z1 = b.z2 = 0; } t->vca = 0; t->env.value = 0; WmsDsp::gate(t->env, true);
    }
    void noteOff(int n) { for (auto& x : v) if (x.note == n) { WmsDsp::gate(x.env, false); x.note = -1; } }
    void allOff() { for (auto& x : v) { WmsDsp::gate(x.env, false); x.note = -1; } }
    bool active() const { for (auto& x : v) if (x.env.stage) return true; return false; }
    double tick() {
        const double R = 27778; double sum = 0;
        for (auto& x : v) {
            if (!x.env.stage) continue; const Zone* z = zoneFor(x.play); if (!z) { x.env.stage = 0; x.env.value = 0; continue; }
            x.env.a = p.a; x.env.d = p.d; x.env.s = p.s; x.env.r = p.r; const double e = WmsDsp::next(x.env); const double len = (double)z->len();
            if (x.pos >= len) { if (p.loop != 0) x.pos = std::fmod(x.pos, len); else { x.env.stage = 0; x.env.value = 0; continue; } }
            const double xs = (*z->data)[(size_t)x.pos], root = z->root >= 0 ? z->root : p.root, ratio = std::pow(2.0, ((x.play - root) + p.tune / 100 + p.bend) / 12);
            x.pos += R / sr * ratio;
            x.vca += (e - x.vca) * vcaS;
            const double ct = p.cutoff * std::pow(2.0, p.envAmt * 6 * e - 1 + x.vel); x.cut += (ct - x.cut) * cutS;
            const double y = WmsDsp::ssm2045(x.f, xs * x.vca * (.3 + .7 * x.vel), x.cut, p.res, sr);
            WmsDsp::trackSet(x.rc, .45 * R * ratio, sr); sum += WmsDsp::track(x.rc, y);
        }
        return sum * p.level * 2.1;
    }
};

} // namespace farp
