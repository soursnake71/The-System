// The FX rack units, ported from the web app (RACK_CODE in index.html). Every unit takes the (mono-summed) input
// like the hardware, gives stereo out, and sleeps once its input has been silent longer than its own tail.
#pragma once
#include <array>
#include <string>
#include "../common/Spec.h"
#include "Common.h"

namespace omega::dsp {

class RackUnit : public Engine {
public:
    using Engine::Engine;
    void prepare(double sr, int) override { fs = sr; build(); cfg(); update(); reset(); quiet = 1e12; asleep = true; ready = true; }
    void changed() override { if (!ready) return; cfg(); update(); }
    void process(const float* inL, const float* inR, const float*, const float*, float* L, float* R, int N, const Transport& t) override {
        if (bpm != t.bpm) { bpm = t.bpm; cfg(); update(); }
        const float* a = inL; const float* b = inR ? inR : inL;
        bool any = false; if (a) for (int i = 0; i < N; ++i) if (a[i] != 0 || b[i] != 0) { any = true; break; }
        if (any) { quiet = 0; if (asleep) { asleep = false; update(); } } else quiet += N;
        const double mix = has("omix") ? v("omix") : 1.0;
        if (quiet > tail() * fs) {
            if (!asleep) { asleep = true; reset(); }
            for (int i = 0; i < N; ++i) { L[i] = a ? (float) (a[i] * (1 - mix)) : 0.f; R[i] = a ? (float) (b[i] * (1 - mix)) : 0.f; }
            return;
        }
        run(a, b, L, R, N);
        if (a && mix < 1) for (int i = 0; i < N; ++i) { L[i] = (float) (a[i] * (1 - mix) + L[i] * mix); R[i] = (float) (b[i] * (1 - mix) + R[i] * mix); }
    }
protected:
    virtual void build() {}            // allocate for this sample rate
    virtual void cfg() {}              // read the parameters
    virtual void update() {}
    virtual double tail() { return 5; }
    virtual void run(const float* a, const float* b, float* L, float* R, int N) = 0;
    double in(const float* a, const float* b, int i) const { return a ? (a[i] + b[i]) * .5 : 0; }
    double syncMs(int idx) const { static const double beats[] = {0, .25, .5, .75, 1, 1.5, 2, .333}; return idx <= 0 ? 0 : beats[idx] * 60000 / bpm; }
    double fs = 48000, quiet = 1e12, bpm = 120; bool asleep = true, ready = false;
};

/* Lexicon 224 / AMS RMX16 */
class Verb224 : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    struct Tap { int d; double gL, gR; };
    void build() override {
        sc = fs / 29761; auto D = [&](double n) { return DLine((int) std::ceil(n * sc * 1.6) + 64); };
        pre.init((int) std::ceil(fs * .26));
        const double ns[4] = {142, 107, 379, 277}; for (int k = 0; k < 4; ++k) ind[k] = D(ns[k]);
        apL = D(672 + 40); dL1 = D(4453); apL2 = D(1800); dL2 = D(3720); apR = D(908 + 40); dR1 = D(4217); apR2 = D(2656); dR2 = D(3163);
        taps.init((int) std::ceil(fs * 1.3));
        inLp = BQ::lp(fs, 8000, .54); inLp2 = BQ::lp(fs, 8000, 1.3); outL = BQ::lp(fs, 8000, .7); outR = BQ::lp(fs, 8000, .7);
    }
    void reset() override { pre.clear(); for (auto& d : ind) d.clear(); for (auto* d : {&apL, &dL1, &apL2, &dL2, &apR, &dR1, &apR2, &dR2}) d->clear(); taps.clear(); fbL = fbR = bw = dmL = dmR = 0; }
    double tail() override { return (nTaps ? 1.4 : 1 + 2 * rt) + v("pre") / 1000; }
    void cfg() override { prog = (int) v("prog"); size = v("size"); treble = v("treble"); }
    void update() override {
        // programs: vplate, pplate, hall, room, chamber, rplate, rhall, ambience, nonlin, reverse
        struct C { double scale, rt, dd1; int lex; };
        const C tab[7] = {{1, .6 + size * 5, .7, 1}, {.8, .4 + size * 3, .75, 1}, {1.35, 1 + size * 7, .6, 1}, {.55, .25 + size * 1.6, .6, 1}, {.8, .5 + size * 3, .65, 1},
                          {.9, .5 + size * 4.5, .7, 0}, {1.3, 1 + size * 6, .62, 0}};
        const bool isCfg = prog < 7;
        const double bwf = (isCfg && !tab[prog].lex) || !isCfg ? 16000 : 8000;
        for (BQ* f : {&inLp, &inLp2, &outL, &outR}) { BQ n = BQ::lp(fs, bwf, .7); n.keepState(*f); *f = n; }
        lex = isCfg ? tab[prog].lex : 0;
        damp = .05 + (1 - treble) * .6;
        if (isCfg) {
            scale = tab[prog].scale; rt = tab[prog].rt;
            const double half = (672 + 4453 + 1800 + 3720) / 29761.0 * scale;
            decay = std::pow(10, -3 * half / rt); dd1 = tab[prog].dd1; dd2 = std::max(.25, std::min(.5, decay + .15)); nTaps = 0;
        } else {
            const double len = (prog == 7 ? .04 + size * .25 : .1 + size * .5) * fs; const int n = 96;
            double seed = 7; auto rnd = [&] { seed = std::fmod(seed * 16807, 2147483647); return seed / 2147483647; };
            double ss = 0;
            for (int i = 0; i < n; ++i) {
                const double x = (i + rnd() * .9) / n; const int d = std::max(1, (int) std::floor(x * len));
                const double env = prog == 8 ? (1 - .1 * x) * std::min(1.0, (1 - x) * len / fs / .02) : prog == 9 ? x * x : std::exp(-x * 5);
                const double gL = env * (rnd() - .5) * 2, gR = env * (rnd() - .5) * 2;
                tapSet[(size_t) i] = {d, gL, gR}; ss += gL * gL;
            }
            const double norm = 1.4 / std::sqrt(ss); for (auto& t : tapSet) { t.gL *= norm; t.gR *= norm; }
            nTaps = n; rt = len / fs; scale = 1;
        }
    }
    double ap(DLine& dl, int n, double g, double x) { const double vv = dl.at(n); const double w = x + g * vv; dl.push(w); return vv - g * w; }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        const double s2 = sc * scale; const int preN = std::max(0, std::min(pre.m() - 2, (int) jround(v("pre") / 1000 * fs)));
        const double ret = v("ret");
        const double n1 = 142 * sc, n2 = 107 * sc, n3 = 379 * sc, n4 = 277 * sc;
        const double dl1 = 4453 * s2, al2 = 1800 * s2, dl2 = 3720 * s2, dr1 = 4217 * s2, ar2 = 2656 * s2, dr2 = 3163 * s2, exc = 16 * sc;
        auto k = [&](double s) { return (int) jround(s * s2); };
        for (int i = 0; i < N; ++i) {
            double x = in(a, b, i);
            x = inLp2.run(inLp.run(x)); if (lex) x = jround(x * 2048) / 2048;
            pre.push(x); x = pre.at(preN);
            double yl = 0, yr = 0;
            if (nTaps) {
                taps.push(ap(ind[1], (int) std::ceil(n2), .75, ap(ind[0], (int) std::ceil(n1), .75, x)));
                for (int j = 0; j < nTaps; ++j) { const double vv = taps.at(tapSet[(size_t) j].d); yl += vv * tapSet[(size_t) j].gL; yr += vv * tapSet[(size_t) j].gR; }
            } else {
                bw += .7 * (x - bw);
                double d = ap(ind[0], (int) jround(n1), .75, bw); d = ap(ind[1], (int) jround(n2), .75, d);
                d = ap(ind[2], (int) jround(n3), .625, d); d = ap(ind[3], (int) jround(n4), .625, d);
                lfo += 2 * PI * 1 / fs; if (lfo > 6.2832) lfo -= 6.2832;
                const double mL = 672 * s2 + exc * std::sin(lfo), mR = 908 * s2 + exc * std::cos(lfo);
                double u = d + fbR, vv = apL.lerp(mL), w = u + dd1 * vv; apL.push(w); u = vv - dd1 * w;
                dL1.push(u); u = dL1.at((int) jround(dl1));
                dmL += (1 - damp) * (u - dmL); u = dmL * decay;
                u = ap(apL2, (int) jround(al2), -dd2, u); dL2.push(u); const double oL = dL2.at((int) jround(dl2)) * decay;
                double r = d + fbL; vv = apR.lerp(mR); w = r + dd1 * vv; apR.push(w); r = vv - dd1 * w;
                dR1.push(r); r = dR1.at((int) jround(dr1));
                dmR += (1 - damp) * (r - dmR); r = dmR * decay;
                r = ap(apR2, (int) jround(ar2), -dd2, r); dR2.push(r); const double oR = dR2.at((int) jround(dr2)) * decay;
                fbL = oL; fbR = oR;
                yl = .6 * (dR1.at(k(266)) + dR1.at(k(2974)) - apR2.at(k(1913)) + dR2.at(k(1996)) - dL1.at(k(1990)) - apL2.at(k(187)) - dL2.at(k(1066)));
                yr = .6 * (dL1.at(k(353)) + dL1.at(k(3627)) - apL2.at(k(1228)) + dL2.at(k(2673)) - dR1.at(k(2111)) - apR2.at(k(335)) - dR2.at(k(121)));
            }
            L[i] = (float) (outL.run(yl) * ret); R[i] = (float) (outR.run(yr) * ret);
        }
    }
    double sc = 1, scale = 1, rt = 1, decay = .5, dd1 = .7, dd2 = .5, damp = .3, bw = 0, dmL = 0, dmR = 0, lfo = 0, fbL = 0, fbR = 0, size = .5, treble = .6;
    int prog = 0, lex = 1, nTaps = 0;
    DLine pre, ind[4], apL, dL1, apL2, dL2, apR, dR1, apR2, dR2, taps;
    std::array<Tap, 96> tapSet {};
    BQ inLp, inLp2, outL, outR;
};

/* Lexicon PrimeTime Model 93 */
class PrimeTime : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    void build() override { dl.init((int) std::ceil(fs * 2.1)); aa.init(fs, 12000, 5); rc.init(fs, 12000, 5); }
    void reset() override { dl.clear(); fl = yA = yB = 0; }
    double tail() override { return tailOf(std::max(dA, dB), std::max(v("fbA"), v("fbB"))); }
    void cfg() override {
        // tempo-synced times resolved to ms, and the multiplier raised so the time fits (as the app does)
        dA = v("dA"); dB = v("dB"); mult = v("mult");
        const int sA = (int) v("sync"), sB = (int) v("syncB");
        if (sA > 0) dA = syncMs(sA); if (sB > 0) dB = syncMs(sB);
        const double need = std::max(dA, dB);
        if (need > 128 * mult) { mult = 8; for (double m : {1.0, 2.0, 4.0, 8.0}) if (128 * m >= need) { mult = m; break; } }
        dA = std::min(dA, 128 * mult); dB = std::min(dB, 128 * mult);
    }
    void update() override { const double bwf = 12000 / (mult > 0 ? mult : 1); aa.set(bwf); rc.set(bwf); }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        const double bwf = 12000 / (mult > 0 ? mult : 1), clk = bwf * 2.6 / fs, q = 2048;
        const double fa = 1 - std::exp(-2 * PI * v("hcut") / fs), inv = v("inv") >= .5 ? -1 : 1, ret = v("ret");
        const double rate = v("vcoRate") > 0 ? v("vcoRate") : .5, depth = v("vcoDepth"), fbA = v("fbA"), fbB = v("fbB"), mixA = v("mixA"), mixB = v("mixB");
        for (int i = 0; i < N; ++i) {
            lfo += rate / fs; if (lfo >= 1) lfo -= 1;
            const double tri = 1 - 2 * std::fabs(2 * lfo - 1), k = 1 - .5 * depth * (tri * .5 + .5);
            const double x = in(a, b, i) + fl * inv;
            double w = aa.run(x); w = h.run(w, clk); w = jround(sat(w) * q) / q;
            dl.push(w);
            yA = rc.run(dl.lerp(std::max(1.0, dA / 1000 * fs * k)));
            const double y = dl.lerp(std::max(1.0, dB / 1000 * fs * k)); yB += (y - yB) * .5;
            fl += fa * (yA * fbA + yB * fbB - fl);
            L[i] = (float) (yA * mixA * ret); R[i] = (float) (yB * mixB * ret);
        }
    }
    DLine dl; LPN aa, rc; Hold h; double lfo = 0, fl = 0, yA = 0, yB = 0, dA = 0, dB = 0, mult = 2;
};

/* Deltalab Effectron ADM 1024 */
class Effectron : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    void build() override { dl.init((int) std::ceil(fs * 4.2)); rec.init(fs, 15000, 4); }
    void reset() override { dl.clear(); est = fb = fbl = de = 0; step = .001; }
    double tail() override { return tailOf(delay, v("regen")) * 5; }
    void cfg() override {
        static const double R[6][2] = {{.25, 1}, {1, 4}, {4, 16}, {16, 64}, {64, 256}, {256, 1024}};
        range = (int) v("range"); delay = clampd(v("delay"), R[range][0], R[range][1]);
    }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        static const double BITS[6] = {256, 1024, 4096, 16384, 65536, 262144};
        const double bits = BITS[range], ret = v("ret"), fbA = 1 - std::exp(-2 * PI * 12000 / fs), grit = v("grit");
        const double lr = v("lfoRate") > 0 ? v("lfoRate") : .5, width = v("width"), regen = v("regen"); const int pan = (int) v("pan");
        for (int i = 0; i < N; ++i) {
            lfo += lr / fs; if (lfo >= 1) lfo -= 1;
            const double sweep = std::pow(5, -width * (.5 + .5 * std::sin(2 * PI * lfo)));
            const double dMs = std::max(.25, (delay > 0 ? delay : 30) * sweep), clock = std::min(1e6, bits / (dMs / 1000));
            const double x = in(a, b, i) + fbl;
            const double pe = x + 2.2 * (x - preX); preX = x;
            acc += clock / fs; double sum = 0; int n = 0;
            const double up = 1.35 + grit * .4, down = .9 - grit * .06, smin = 2e-4 * (1 + grit * 6);
            while (acc >= 1) { acc -= 1;
                const int bit = pe >= est ? 1 : 0; hist = ((hist << 1) | bit) & 7;
                step = (hist == 7 || hist == 0) ? std::min(.3, step * up) : std::max(smin, step * down);
                est = est * .9998 + (bit ? step : -step); sum += est; n++; }
            const double dec = n ? sum / n : est;
            de += (dec - de) * .31; dl.push(de);
            const double y = rec.run(dl.lerp(std::max(1.0, dMs / 1000 * fs)));
            fbl += fbA * (y * regen - fbl);
            const double vv = y * ret;
            if (pan == 1) { L[i] = (float) vv; R[i] = 0; } else if (pan == 2) { L[i] = 0; R[i] = (float) vv; }
            else if (pan == 3) { L[i] = (float) vv; R[i] = (float) -vv; } else { L[i] = (float) (vv * .7); R[i] = (float) (vv * .7); }
        }
    }
    DLine dl; LPN rec; double est = 0, step = .001, acc = 0, lfo = 0, fb = 0, preX = 0, de = 0, fbl = 0, delay = 22; int hist = 0, range = 3;
};

/* Ensoniq DP/4 */
class DPUnit {
public:
    enum Alg { off, hall, plate, room, chorus, flange, phaser, delay, pingpong, pitch, dist, rotary, filter };
    void init(double f) {
        fs = f; for (auto& d : dl) d.init((int) std::ceil(fs * 2.1));
        const int cm[6] = {1116, 1188, 1277, 1356, 1422, 1491}; const double s = fs / 44100;
        for (int c = 0; c < 2; ++c) {
            for (int k = 0; k < 6; ++k) { combs[c][k].d.init((int) std::ceil(cm[k] * s * 2.2 + 50)); combs[c][k].n = (int) jround(cm[k] * s); combs[c][k].f = 0; }
            const int an[4] = {556, 441, 341, 225};
            for (int k = 0; k < 4; ++k) { aps[c][k].d.init((int) std::ceil(an[k] * s + 30)); aps[c][k].n = (int) jround(an[k] * s * (1 + c * .04)); }
            pre[c].init((int) std::ceil(fs * .2));
        }
    }
    void clear() { for (auto& d : dl) d.clear(); for (auto& cc : combs) for (auto& c : cc) { c.d.clear(); c.f = 0; } for (auto& aa : aps) for (auto& a : aa) a.d.clear(); for (auto& p : pre) p.clear(); fb[0] = fb[1] = lp[0] = lp[1] = 0; std::fill(std::begin(ap), std::end(ap), 0.0); }
    void run(double l, double r, double x, double y, double* out) {
        const int A = alg;
        if (A == hall || A == plate || A == room) {
            const double size = A == hall ? 1 + x * 1.2 : A == plate ? .7 + x * .7 : .35 + x * .5, damp = A == plate ? .15 + y * .35 : .25 + y * .5, fbk = std::min(.97, .7 + .26 * std::min(1.0, size / 2.2));
            const int pd = (int) jround((A == hall ? .03 : A == room ? .005 : .012) * fs);
            for (int c = 0; c < 2; ++c) { pre[c].push(c ? r : l); const double inp = pre[c].at(pd) * .3; double sum = 0;
                for (auto& cb : combs[c]) { const int n = (int) jround(cb.n * size); const double o = cb.d.at(n); cb.f = o * (1 - damp) + cb.f * damp; cb.d.push(inp + cb.f * fbk); sum += o; }
                for (auto& a : aps[c]) { const double o = a.d.at(a.n); a.d.push(sum + o * .5); sum = o - sum * .5; }
                out[c] = sum * (A == plate ? .9 : .7); }
            return;
        }
        if (A == chorus || A == flange) {
            ph += (.08 + x * (A == chorus ? 2.5 : 1.2)) / fs; if (ph > 1) ph -= 1;
            for (int c = 0; c < 2; ++c) { const double lfo = std::sin(2 * PI * (ph + c * .25)), base = A == chorus ? .012 : .0025, dep = A == chorus ? .004 : .002 * (.3 + y);
                const double d = (base + dep * (1 + lfo) * .5) * fs, vv = dl[c].lerp(d); dl[c].push((c ? r : l) + (A == flange ? vv * (y * 1.7 - .85) : 0)); out[c] = A == chorus ? vv * (.5 + y * .5) : vv; }
            return;
        }
        if (A == phaser) {
            ph += (.05 + x * 3) / fs; if (ph > 1) ph -= 1;
            for (int c = 0; c < 2; ++c) { const double sw = .5 - .5 * std::cos(2 * PI * (ph + c * .2)), f = 150 * std::pow(25, sw), t = std::tan(PI * f / fs), a1 = (t - 1) / (t + 1);
                double vv = (c ? r : l) + fb[c] * (y * .9); for (int s = 0; s < 6; ++s) { const int k = c * 8 + s; const double o = a1 * vv + ap[k]; ap[k] = vv - a1 * o; vv = o; } fb[c] = vv; out[c] = ((c ? r : l) + vv) * .6; }
            return;
        }
        if (A == delay || A == pingpong) {
            const double d = std::max(1.0, (.02 + x * 1.18) * fs), fbk = y * .9;
            if (A == delay) { for (int c = 0; c < 2; ++c) { const double vv = dl[c].at((int) jround(d * (c ? 1.02 : 1))); lp[c] += (vv - lp[c]) * .35; dl[c].push((c ? r : l) + lp[c] * fbk); out[c] = vv; } }
            else { const double vL = dl[0].at((int) jround(d)), vR = dl[1].at((int) jround(d)); dl[0].push((l + r) * .5 + vR * fbk); dl[1].push(vL * fbk); out[0] = vL; out[1] = vR; }
            return;
        }
        if (A == pitch) {
            const double semis = jround((x - .5) * 24), ratio = std::pow(2, semis / 12), W = .06 * fs; pp = std::fmod(pp + (1 - ratio) + W, W);
            for (int c = 0; c < 2; ++c) { dl[c].push(c ? r : l); const double d1 = pp + 2, d2 = std::fmod(pp + W / 2, W) + 2, g1 = std::sin(PI * pp / W), g2 = std::sin(PI * std::fmod(pp + W / 2, W) / W);
                out[c] = (dl[c].lerp(d1) * g1 + dl[c].lerp(d2) * g2) * .8 + (c ? r : l) * y * .5; }
            return;
        }
        if (A == dist) {
            const double g = 1 + x * 40, tone = .05 + y * .6;
            for (int c = 0; c < 2; ++c) { const double vv = std::tanh((c ? r : l) * g) * .6; lp[c] += (vv - lp[c]) * tone; out[c] = lp[c]; }
            return;
        }
        if (A == rotary) {
            ph += (.7 + x * 6) / fs; if (ph > 1) ph -= 1; const double m = (l + r) * .5, s = std::sin(2 * PI * ph), cs = std::cos(2 * PI * ph);
            dl[0].push(m); const double vv = dl[0].lerp((.002 + .0012 * (1 + s)) * fs); out[0] = vv * (1 + .5 * y * cs) * .7; out[1] = vv * (1 - .5 * y * cs) * .7; return;
        }
        if (A == filter) {
            ph += (.05 + x * 4) / fs; if (ph > 1) ph -= 1;
            for (int c = 0; c < 2; ++c) { const double f = 120 * std::pow(60, .5 - .5 * std::cos(2 * PI * ph)), g = 2 * std::sin(PI * std::min(f, fs / 6) / fs), q = 1.9 - y * 1.75; const int k = c * 8;
                const double hp = (c ? r : l) - ap[k] - q * ap[k + 1]; ap[k + 1] += g * hp; ap[k] += g * ap[k + 1]; out[c] = ap[k] * .8; }
            return;
        }
        out[0] = 0; out[1] = 0;
    }
    int alg = off;
private:
    struct Comb { DLine d; int n = 0; double f = 0; };
    struct AP { DLine d; int n = 0; };
    double fs = 48000, ph = 0, pp = 0, lp[2] {}, fb[2] {}, ap[16] {};
    DLine dl[2], pre[2]; Comb combs[2][6]; AP aps[2][4];
};

class DP4 : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    void build() override { for (auto& u : U) u.init(fs); }
    void cfg() override {
        for (int k = 0; k < 4; ++k) { const std::string s = std::to_string(k + 1); const int a = (int) v(("a" + s).c_str());
            if (U[k].alg != a) { U[k].alg = a; U[k].clear(); }
            P[k][0] = v(("m" + s).c_str()); P[k][1] = v(("x" + s).c_str()); P[k][2] = v(("y" + s).c_str()); }
        conf = (int) v("cfg");
    }
    void reset() override { for (auto& u : U) u.clear(); }
    double tail() override { return 8; }
    static double dpq(double x) { const double Q = 1.0 / 8388608; return jround(x / Q) * Q; }
    void step(int k, double* o) { auto& u = U[k]; if (u.alg == DPUnit::off) return; double r2[2]; u.run(o[0], o[1], P[k][1], P[k][2], r2); const double m = P[k][0]; o[0] = dpq(o[0] * (1 - m) + r2[0] * m); o[1] = dpq(o[1] * (1 - m) + r2[1] * m); }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        const double ret = v("ret"); double vv[2], w[2];
        for (int i = 0; i < N; ++i) {
            const double l = a ? a[i] : 0, r = b ? b[i] : 0; double oL = 0, oR = 0;
            if (conf == 1) { vv[0] = l; vv[1] = r; for (int k = 0; k < 4; ++k) step(k, vv); oL = vv[0]; oR = vv[1]; }
            else if (conf == 2) { vv[0] = l; vv[1] = r; step(0, vv); step(1, vv); w[0] = l; w[1] = r; step(2, w); step(3, w); oL = (vv[0] + w[0]) * .5; oR = (vv[1] + w[1]) * .5; }
            else { int n = 0; for (int k = 0; k < 4; ++k) { if (U[k].alg == DPUnit::off) continue; vv[0] = l; vv[1] = r; step(k, vv); oL += vv[0]; oR += vv[1]; n++; } if (n) { oL /= std::sqrt((double) n); oR /= std::sqrt((double) n); } }
            L[i] = (float) (oL * ret); R[i] = (float) (oR * ret);
        }
    }
    DPUnit U[4]; double P[4][3] {}; int conf = 0;
};

/* Alesis Microverb */
class Microverb : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    void build() override {
        const double sc = fs / 44100; const int cn[6] = {1557, 1617, 1491, 1422, 1277, 1356}, an[4] = {225, 556, 441, 341};
        for (int c = 0; c < 2; ++c) {
            for (int k = 0; k < 6; ++k) { cb[c][k].d.init((int) std::ceil(cn[k] * sc * 2.6) + 10); cb[c][k].n = (int) jround(cn[k] * sc * (1 + c * .023)); cb[c][k].f = 0; }
            for (int k = 0; k < 4; ++k) { ap[c][k].d.init((int) std::ceil(an[k] * sc * 1.04) + 10); ap[c][k].n = (int) jround(an[k] * sc * (1 + c * .03)); }
            pre[c].init((int) std::ceil(fs * 1.2));
        }
    }
    void reset() override { for (auto& cc : cb) for (auto& c : cc) { c.d.clear(); c.f = 0; } for (auto& aa : ap) for (auto& a : aa) a.d.clear(); for (auto& p : pre) p.clear(); }
    double tail() override { return 6; }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        static const double T[16][4] = {{.45, .5, 2, 0}, {.6, .45, 6, 0}, {.8, .4, 12, 0}, {1, .45, 18, 0}, {1.25, .4, 25, 0}, {1.55, .35, 35, 0}, {.9, .15, 5, 0}, {1.1, .55, 8, 0},
            {1.8, .3, 45, 0}, {2.2, .25, 60, 0}, {.7, .3, 0, 1}, {1, .25, 0, 1}, {.8, .3, 0, 2}, {1.2, .3, 0, 2}, {.35, .6, 0, 0}, {1.4, .7, 20, 0}};
        const int P = std::max(0, std::min(15, (int) v("prog"))); const double ret = v("ret");
        const double size = T[P][0], damp = T[P][1], pd = T[P][2]; const int kind = (int) T[P][3];
        const double fbk = std::min(.975, .72 + size * .14), bw = 1 - std::exp(-2 * PI * 11000 / fs); const int pdS = (int) jround(pd / 1000 * fs);
        const double gdec = std::exp(-1 / (fs * .25));
        for (int i = 0; i < N; ++i) {
            const double inL = a ? a[i] : 0, inR = b ? b[i] : 0; const double lev = std::fabs(inL) + std::fabs(inR); gate = std::max(lev > .01 ? 1.0 : 0.0, gate * gdec);
            double out[2];
            for (int c = 0; c < 2; ++c) {
                const double x = c ? inR : inL; bl[c] += (x - bl[c]) * bw; pre[c].push(bl[c]);
                double inp;
                if (kind == 2) { const double len = jround(fs * .35 * size); double acc = 0; for (int k = 0; k < 4; ++k) { const int d = (int) jround(len * (k + 1) / 4); acc += pre[c].at(d) * (k + 1) / 4; } inp = acc * .25; }
                else inp = pre[c].at(pdS) * .3;
                double s = 0; for (auto& q : cb[c]) { const double o = q.d.at((int) jround(q.n * size)); q.f = o * (1 - damp) + q.f * damp; q.d.push(inp + q.f * fbk); s += o; }
                for (auto& q : ap[c]) { const double o = q.d.at(q.n); q.d.push(s + o * .5); s = o - s * .5; }
                if (kind == 1) s *= gate;
                out[c] = jround(s * .45 * 32767) / 32767;
            }
            L[i] = (float) (out[0] * ret); R[i] = (float) (out[1] * ret);
        }
    }
    struct Comb { DLine d; int n = 0; double f = 0; };
    struct AP { DLine d; int n = 0; };
    Comb cb[2][6]; AP ap[2][4]; DLine pre[2]; double bl[4] {}, gate = 0;
};

/* A/DA Flanger (1977) */
class ADAFlanger : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    void build() override { dl.init((int) std::ceil(fs * .03)); aa.init(fs, 11000, 4); rc.init(fs, 11000, 4); }
    void reset() override { dl.clear(); fb = 0; ce = ee = 1e-3; gate = ge = 0; }
    double tail() override { return .6 + std::fabs(v("enh")) * 2; }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        const double ret = v("ret"), man = v("manual"), rng = v("range");
        const double rate = .02 * std::pow(400, v("speed")), enh = std::min(.97, v("enh")) * (v("odd") >= .5 ? -1 : 1);
        const double thr = v("thresh") > 0 ? std::pow(10, (-80 + v("thresh") * 60) / 20) : 0, mix = v("mix"); const bool st = v("stereo") >= .5;
        const double ca = 1 - std::exp(-1 / (fs * .012)), ga = 1 - std::exp(-1 / (fs * .002)), gr = 1 - std::exp(-1 / (fs * .15));
        for (int i = 0; i < N; ++i) {
            const double x0 = in(a, b, i);
            const double lv = std::fabs(x0); ge += (lv > ge ? ga : gr) * (lv - ge); const double go = thr == 0 || ge > thr ? 1 : 0; gate += (go - gate) * (go != 0 ? .01 : .0004);
            const double x = x0 * gate;
            lfo += rate / fs; if (lfo >= 1) lfo -= 1; const double tri = 1 - 2 * std::fabs(2 * lfo - 1);
            const double dMax = .25 * std::pow(40, man), dMs = std::max(.25, dMax / std::pow(1 + 39 * rng, tri * .5 + .5)), clk = 512 / (2 * dMs / 1000);
            ldel += (dMs - ldel) * .002;
            double vv = aa.run(x + sat(fb));
            ce += ca * (std::fabs(vv) - ce); vv = vv / std::sqrt(std::max(ce, 1e-4)) * .1;
            const double r = clk / fs; vv = r < 1 ? hi.run(vv, r) : vv; dl.push(vv);
            double y = dl.lerp(std::max(1.0, ldel / 1000 * fs)) * .985 + (rnd() - .5) * 2e-4;
            y = r < 1 ? ho.run(y, r) : y; y = rc.run(y);
            ee += ca * (std::fabs(y) - ee); y = y * ee * 100;
            dc += (y - dc) * .0005; y -= dc;
            fb = y * enh;
            const double wet = y * mix * 1.4, dry = x * (1 - mix) * 1.4;
            L[i] = (float) ((dry + wet) * ret); R[i] = (float) ((dry + (st ? -wet : wet)) * ret);
        }
    }
    DLine dl; LPN aa, rc; Hold hi, ho; Rand rnd; double lfo = .25, fb = 0, ce = 1e-3, ee = 1e-3, gate = 0, ge = 0, dc = 0, ldel = 0;
};

/* UREI 1176LN, used as an insert (its own Mix knob is the dry/wet) */
class UREI1176 : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    void reset() override { gr = slow = 0; }
    double tail() override { return 1e9; }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        const double inG = std::pow(10, (v("input") * 50 - 20) / 20), outG = std::pow(10, (v("output") * 40 - 30) / 20);
        const int ri = (int) v("ratio"); const bool all = ri == 4; static const double RT[4] = {4, 8, 12, 20};
        const double ratio = all ? 20 : RT[std::max(0, std::min(3, ri))], thr = .126;
        const double atk = (20 + (7 - v("attack")) / 6 * 780) * 1e-6 * (all ? 1.6 : 1), rel = (.05 + (7 - v("release")) / 6 * 1.05);
        const double aA = 1 - std::exp(-1 / (fs * atk)), aR = 1 - std::exp(-1 / (fs * rel)), aS = 1 - std::exp(-1 / (fs * rel * 5)), hpA = 1 - std::exp(-2 * PI * 18 / fs);
        const double mix = v("mix"), slope = 1 - 1 / ratio, ret = has("ret") ? v("ret") / .8 : 1;
        for (int i = 0; i < N; ++i) {
            double l = a ? a[i] : 0, r = b ? b[i] : l; const double dl = l, dr = r;
            hpL += hpA * (l - hpL); hpR += hpA * (r - hpR); l -= hpL * .9; r -= hpR * .9;
            l = tnh(l * .5) / .5 * inG; r = tnh(r * .5) / .5 * inG;
            const double lv = std::max({std::fabs(l), std::fabs(r), 1e-6}), over = 20 * std::log10(lv / thr);
            const double tgt = over > 0 ? std::min(40.0, over * slope * (all ? 1.25 : 1) + (all ? 2 : 0)) : 0;
            if (tgt > gr) gr += std::min(.9, aA * ratio * .3) * (tgt - gr); else { slow += aS * (gr - slow); gr += aR * (std::max(tgt, slow * .6) - gr); }
            const double g = std::pow(10, -gr / 20), d2 = std::min(.25, gr * .006) * (all ? 2.2 : 1);
            double yl = l * g, yr = r * g; yl += d2 * yl * yl; yr += d2 * yr * yr;
            yl = tnh(yl * 1.2 + .1 * yl * yl) / 1.2 * outG * 2.7; yr = tnh(yr * 1.2 + .1 * yr * yr) / 1.2 * outG * 2.7;
            dcL += (yl - dcL) * .0004; dcR += (yr - dcR) * .0004; yl -= dcL; yr -= dcR;
            L[i] = (float) ((yl * mix + dl * (1 - mix)) * ret); R[i] = (float) ((yr * mix + dr * (1 - mix)) * ret);
        }
    }
    double gr = 0, slow = 0, hpL = 0, hpR = 0, dcL = 0, dcR = 0;
};

/* Phil Collins Simulator: gated, crushed stone room */
class PCSim : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    void build() override { er.init((int) std::ceil(fs * .25)); for (auto& d : fd) d.init((int) std::ceil(fs * .25)); }
    void reset() override { er.clear(); for (auto& d : fd) d.clear(); for (auto& x : lp) x = 0; ce = ke = gg = hold = 0; }
    double tail() override { return 1 + v("decay"); }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        const double size = .35 + v("size") * .9, rt = v("decay") > 0 ? v("decay") : 2, crush = v("crush");
        static const double ER[10] = {7.1, 11.3, 13.9, 17.2, 19.7, 23.1, 29.3, 31.7, 37.1, 41.9}, EG[10] = {.9, .8, .75, .7, .62, .55, .5, .44, .4, .35};
        int FL[4]; double FG[4]; const double fl[4] = {29.7, 37.1, 41.1, 43.7};
        for (int k = 0; k < 4; ++k) { FL[k] = (int) jround(fl[k] * size * fs / 1000); FG[k] = std::pow(10, -3 * FL[k] / fs / rt); }
        int ERn[10]; for (int k = 0; k < 10; ++k) ERn[k] = (int) jround(ER[k] * size * fs / 1000);
        const double dampA = .25 + v("tone") * .7, hpA = 1 - std::exp(-2 * PI * 110 / fs);
        const double cA = 1 - std::exp(-1 / (fs * .0002)), cR = 1 - std::exp(-1 / (fs * (.04 + (1 - crush) * .25))), cthr = std::pow(10, (-18 - crush * 24) / 20), cmk = std::pow(10, crush * 26 / 20);
        const double thr = std::pow(10, v("thresh") / 20), holdN = v("hold") / 1000 * fs, rel = 1 - std::exp(-1 / (fs * std::max(.001, v("snap") / 1000)));
        const double kA = 1 - std::exp(-1 / (fs * .0005)), kR = 1 - std::exp(-1 / (fs * .02)), range = std::pow(10, -v("range") / 20), ret = v("ret"), dir = v("direct");
        for (int i = 0; i < N; ++i) {
            const double x = in(a, b, i);
            er.push(x); double el = 0, eR = 0;
            for (int k = 0; k < 10; ++k) { const double vv = er.at(ERn[k]) * EG[k]; if (k & 1) eR += vv; else el += vv; }
            double o[4]; for (int k = 0; k < 4; ++k) o[k] = fd[k].at(FL[k]);
            for (int k = 0; k < 4; ++k) lp[k] += dampA * (o[k] - lp[k]);
            const double* p = lp; const double h[4] = {p[0] + p[1] + p[2] + p[3], p[0] - p[1] + p[2] - p[3], p[0] + p[1] - p[2] - p[3], p[0] - p[1] - p[2] + p[3]};
            const double inj = (el + eR) * .25 + x * .3;
            for (int k = 0; k < 4; ++k) fd[k].push(inj + h[k] * .5 * FG[k]);
            double wl = el * .5 + o[0] + o[2] * .6, wr = eR * .5 + o[1] + o[3] * .6;
            hp[0] += hpA * (wl - hp[0]); wl -= hp[0]; hp[1] += hpA * (wr - hp[1]); wr -= hp[1];
            const double lv = std::max(std::fabs(wl), std::fabs(wr)); ce += (lv > ce ? cA : cR) * (lv - ce);
            const double cg = ce > cthr ? std::pow(cthr / ce, .95) : 1; wl = tnh(wl * cg * cmk); wr = tnh(wr * cg * cmk);
            const double kl = std::fabs(x); ke += (kl > ke ? kA : kR) * (kl - ke);
            if (ke > thr) { hold = holdN; gg += (1 - gg) * .5; } else if (hold > 0) hold--; else gg += (range - gg) * rel;
            L[i] = (float) ((wl * gg * .5 + x * dir) * ret); R[i] = (float) ((wr * gg * .5 + x * dir) * ret);
        }
    }
    DLine er, fd[4]; double lp[4] {}, ce = 0, ke = 0, hold = 0, gg = 0, hp[2] {};
};

/* Eventide H910 Harmonizer */
class H910 : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    void build() override { for (int c = 0; c < 2; ++c) { dl[c].init((int) std::ceil(fs * .2)); lp[c].init(fs, 9000, 4); } }
    void reset() override { for (auto& d : dl) d.clear(); fb[0] = fb[1] = 0; p[0] = p[1] = 0; }
    double tail() override { return tailOf(std::max(v("dL"), v("dR")) + 30, v("fb")); }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        const double W = .03 * fs, cents = v("cents"); const bool smooth = v("splice") >= .5;
        const double ratio[2] = {std::pow(2, cents / 1200), std::pow(2, -cents / 1200)}, del[2] = {v("dL") / 1000 * fs, v("dR") / 1000 * fs};
        const double sp = v("spread"), ret = v("ret"), q = 1024, fbk = v("fb"), wings = v("wings");
        const double atk = 1 - std::exp(-1 / (.004 * fs)), rel = 1 - std::exp(-1 / (.08 * fs));
        double out[2] = {0, 0};
        for (int i = 0; i < N; ++i) {
            const double x0 = in(a, b, i);
            for (int c = 0; c < 2; ++c) {
                const double x = x0 + fb[c] * fbk;
                const double ax = std::fabs(x); env += (ax > env ? atk : rel) * (ax - env);
                const double g = 1 / std::sqrt(std::max(env, 1e-4)) * .1;
                dl[c].push(jround(sat(x * g) * q) / q);
                p[c] += 1 - ratio[c]; if (p[c] < 0) p[c] += W; else if (p[c] >= W) p[c] -= W;
                double y;
                if (smooth) { const double p2 = std::fmod(p[c] + W / 2, W), w1 = 1 - std::fabs(2 * p[c] / W - 1), w2 = 1 - w1;
                    y = dl[c].lerp(del[c] + p[c] + 2) * w1 + dl[c].lerp(del[c] + p2 + 2) * w2; }
                else y = dl[c].lerp(del[c] + p[c] + 2);
                y = lp[c].run(y);
                const double ay = std::fabs(y); envO[c] += (ay > envO[c] ? atk * .5 : rel * .5) * (ay - envO[c]);
                y *= std::min(600.0, envO[c] * 141);
                fb[c] = y; out[c] = y;
            }
            const double l = out[0] * (.5 + .5 * sp) + out[1] * (.5 - .5 * sp), r = out[1] * (.5 + .5 * sp) + out[0] * (.5 - .5 * sp);
            const double wg = wings * ret; L[i] = (float) (l * wg); R[i] = (float) (r * wg);
        }
    }
    DLine dl[2]; LPN lp[2]; double p[2] {}, fb[2] {}, env = 1e-3, envO[2] {1e-3, 1e-3};
};

/* MXR Model 113 */
class MXR113 : public RackUnit {
public:
    using RackUnit::RackUnit;
protected:
    void build() override { dl.init((int) std::ceil(fs * 1.35)); aa.init(fs, 20000, 6, {.52, .71, 1.93}); rc.init(fs, 20000, 4, {.54, 1.31}); }
    void reset() override { dl.clear(); fb = de = pe = 0; }
    double tail() override { return tailOf(delay, v("regen")); }
    void cfg() override { bwv = v("bw"); delay = v("delay"); const int s = (int) v("sync"); if (s > 0) delay = syncMs(s); delay = std::min(delay, 160 * 20000 / bwv); }
    void update() override { aa.set(std::min(bwv, fs * .45)); rc.set(std::min(bwv, fs * .45)); }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        const double ret = v("ret"), q = 512, regen = v("regen"), lr = v("lfoRate") > 0 ? v("lfoRate") : .3, ld = v("lfoDepth"), inv = v("inv") >= .5 ? -1 : 1;
        const double atk = 1 - std::exp(-1 / (.005 * fs)), rel = 1 - std::exp(-1 / (.05 * fs)), clk = std::min(1.0, bwv * 2.5 / fs);
        for (int i = 0; i < N; ++i) {
            lfo += lr / fs; if (lfo >= 1) lfo -= 1;
            const double k = std::pow(4, -ld * (.5 + .5 * std::sin(2 * PI * lfo)));
            double x = in(a, b, i) + fb;
            const double p = x + 1.5 * (x - pe); pe = x; x = p;
            const double ax = std::fabs(x); env += (ax > env ? atk : rel) * (ax - env);
            x = x / std::sqrt(std::max(env, 1e-4)) * .12;
            x = std::max(-.9, std::min(.9, x));
            x = aa.run(x); x = h.run(x, clk / k); x = jround(x * q) / q;
            dl.push(x);
            double y = rc.run(dl.lerp(std::max(1.0, (delay > 0 ? delay : 100) / 1000 * fs * k)));
            const double ay = std::fabs(y); envO += (ay > envO ? atk : rel) * (ay - envO);
            y *= std::min(300.0, envO * 70);
            de += (y - de) * .4; y = de;
            fb = y * regen;
            const double vv = y * ret * inv; L[i] = (float) vv; R[i] = (float) vv;
        }
    }
    DLine dl; LPN aa, rc; Hold h; double env = 1e-3, envO = 1e-3, pe = 0, de = 0, fb = 0, lfo = 0, bwv = 10000, delay = 180;
};

}  // namespace omega::dsp
