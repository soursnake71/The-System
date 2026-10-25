// Master/insert processors from the web app: SSL-style bus compressor, resonance tamer + clipper, tempo gate stepper.
#pragma once
#include <string>
#include "../common/Spec.h"
#include "Common.h"

namespace omega::dsp {

/* Mastering bus compressor in the style of the SSL G-series quad bus compressor */
class SSLBus : public Engine {
public:
    using Engine::Engine;
    void prepare(double sr, int) override { fs = sr; gr = slow = 0; hx[0] = hx[1] = hy[0] = hy[1] = 0; }
    void process(const float* a, const float* b, const float*, const float*, float* L, float* R, int N, const Transport&) override {
        static const double ATK[6] = {.1, .3, 1, 3, 10, 30}, REL[5] = {.1, .3, .6, 1.2, 0};
        if (!a) { std::fill(L, L + N, 0.f); std::fill(R, R + N, 0.f); return; }
        if (!b) b = a;
        const int atk = (int) v("atk"), rel = (int) v("rel");
        const double aA = 1 - std::exp(-1 / (ATK[atk] / 1000 * fs)); const bool aut = REL[rel] == 0;
        const double hp = v("hp"), hpA = hp > 0 ? std::exp(-2 * PI * hp / fs) : 0, slope = 1 - 1 / v("ratio"), mk = std::pow(10, v("makeup") / 20), mix = v("mix"), thr = v("thr");
        const double sA = 1 - std::exp(-1 / (2 * fs));
        for (int i = 0; i < N; ++i) {
            double l = a[i], r = b[i];
            if (hpA != 0) { const double yl = hpA * (hy[0] + l - hx[0]); hx[0] = l; hy[0] = yl; const double yr = hpA * (hy[1] + r - hx[1]); hx[1] = r; hy[1] = yr; l = yl; r = yr; }
            const double pk = std::max(std::fabs(l), std::fabs(r)), db = 20 * std::log10(pk + 1e-9);
            const double over = db - thr, want = over <= -2 ? 0 : over >= 2 ? over * slope : slope * (over + 2) * (over + 2) / 8;
            if (want > gr) gr += (want - gr) * aA;
            else { slow += (gr - slow) * sA; const double rt = aut ? .1 + std::min(1.1, slow * .25) : REL[rel]; gr += (want - gr) * (1 - std::exp(-1 / (rt * fs))); }
            const double g = std::pow(10, -gr / 20) * mk;
            L[i] = (float) (a[i] * (1 - mix + mix * g)); R[i] = (float) (b[i] * (1 - mix + mix * g));
        }
    }
private:
    double fs = 48000, gr = 0, slow = 0, hx[2] {}, hy[2] {};
};

/* Resonance tamer + clipper */
class Soother : public Engine {
public:
    using Engine::Engine;
    void prepare(double sr, int) override {
        fs = sr;
        for (int k = 0; k < 18; ++k) { F[k] = 200 * std::pow(12000.0 / 200, k / 17.0); det[k] = bp(F[k], 2.2); ds[k][0] = ds[k][1] = 0; env[k] = 0; gain[k] = 0; eq[k] = Q5 {}; for (auto& s : es[k]) s[0] = s[1] = 0; }
        n = 0; prev[0] = prev[1] = 0;
    }
    void process(const float* a, const float* b, const float*, const float*, float* L, float* R, int N, const Transport&) override {
        if (!a) { std::fill(L, L + N, 0.f); std::fill(R, R + N, 0.f); return; }
        if (!b) b = a;
        const bool on = v("on") >= .5, clipOn = v("clip") >= .5;
        if (!on && !clipOn) { for (int i = 0; i < N; ++i) { L[i] = a[i]; R[i] = b[i]; } return; }
        const double speed = v("speed"), sharp = v("sharp"), depth = v("depth");
        const double rel = std::exp(-1 / (fs * (.02 + (1 - speed) * .25))), Q = 1.5 + sharp * 5;
        const double g = std::pow(10, v("drive") / 20), ceil = std::pow(10, v("ceil") / 20), h = 1 + v("hard") * 8;
        auto clip = [&](double x) { const double u = x * g / ceil; return (u < 0 ? -1 : 1) * std::pow(std::tanh(std::pow(std::fabs(u), h)), 1 / h) * ceil; };
        for (int i = 0; i < N; ++i) {
            double l = a[i], r = b[i];
            if (on) {
                const double m = (l + r) * .5;
                for (int k = 0; k < 18; ++k) { auto& d = det[k]; auto& s = ds[k]; const double y = d.b0 * m + s[0]; s[0] = -d.a1 * y + s[1]; s[1] = d.b2 * m - d.a2 * y; const double e = std::fabs(y); env[k] = e > env[k] ? e : env[k] * rel; }
                if ((n++ & 31) == 0) {
                    for (int k = 0; k < 18; ++k) {
                        const double nb = (env[std::max(0, k - 2)] + env[std::max(0, k - 1)] + env[std::min(17, k + 1)] + env[std::min(17, k + 2)]) / 4 + 1e-6;
                        const double over = 20 * std::log10((env[k] + 1e-6) / nb) - (6 - depth * 4); const double target = over > 0 ? -std::min(12.0, over * (.4 + depth)) : 0;
                        gain[k] += (target - gain[k]) * .2; eq[k] = peak(F[k], Q, gain[k]);
                    }
                }
                for (int k = 0; k < 18; ++k) { if (gain[k] > -.05) continue; const auto& q = eq[k]; auto& sL = es[k][0]; auto& sR = es[k][1];
                    double y = q.b0 * l + sL[0]; sL[0] = q.b1 * l - q.a1 * y + sL[1]; sL[1] = q.b2 * l - q.a2 * y; l = y;
                    y = q.b0 * r + sR[0]; sR[0] = q.b1 * r - q.a1 * y + sR[1]; sR[1] = q.b2 * r - q.a2 * y; r = y; }
            }
            if (clipOn) { const double ml = (l + prev[0]) * .5, mr = (r + prev[1]) * .5; prev[0] = l; prev[1] = r; l = (clip(l) + clip(ml)) * .5; r = (clip(r) + clip(mr)) * .5; }
            L[i] = (float) l; R[i] = (float) r;
        }
    }
private:
    struct Q5 { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    Q5 bp(double f, double Q) const { const double w = 2 * PI * f / fs, al = std::sin(w) / (2 * Q), a0 = 1 + al; Q5 q; q.b0 = al / a0; q.b2 = -al / a0; q.a1 = -2 * std::cos(w) / a0; q.a2 = (1 - al) / a0; return q; }
    Q5 peak(double f, double Q, double dB) const { const double A = std::pow(10, dB / 40), w = 2 * PI * f / fs, al = std::sin(w) / (2 * Q), a0 = 1 + al / A;
        Q5 q; q.b0 = (1 + al * A) / a0; q.b1 = -2 * std::cos(w) / a0; q.b2 = (1 - al * A) / a0; q.a1 = -2 * std::cos(w) / a0; q.a2 = (1 - al / A) / a0; return q; }
    double fs = 48000, F[18] {}, ds[18][2] {}, env[18] {}, gain[18] {}, es[18][2][2] {}, prev[2] {}; Q5 det[18], eq[18]; unsigned n = 0;
};

/* Gate stepper: a tempo-locked 16-step volume gate, locked to the host's beat */
class GateStep : public Engine {
public:
    using Engine::Engine;
    void prepare(double sr, int) override { fs = sr; g = 1; }
    void process(const float* a, const float* b, const float*, const float*, float* L, float* R, int N, const Transport& t) override {
        if (!a) { std::fill(L, L + N, 0.f); std::fill(R, R + N, 0.f); return; }
        if (!b) b = a;
        double st[16]; for (int k = 0; k < 16; ++k) st[k] = v(("s" + std::to_string(k + 1)).c_str());
        const double spb = v("rate") / 4, bound = 1 + v("swing") * .66, duty = v("duty");
        const double aA = 1 - std::exp(-1 / (fs * std::max(.0005, v("atk") / 1000))), aR = 1 - std::exp(-1 / (fs * std::max(.001, v("rel") / 1000))), dep = v("depth"), mix = v("mix");
        // the host's beat position while it plays; our own clock while it is stopped
        double beat = t.playing ? t.ppq : freeBeat; const double dBeat = t.bpm / 60 / fs;
        for (int i = 0; i < N; ++i) {
            const double pos = beat * spb, pair = std::floor(pos / 2), ph = pos - pair * 2;
            double k, f; if (ph < bound) { k = pair * 2; f = ph / bound; } else { k = pair * 2 + 1; f = (ph - bound) / (2 - bound); }
            const int idx = ((((long long) k) % 16) + 16) % 16;
            const double vv = st[idx], open = f < duty ? vv : 0, tgt = 1 - dep * (1 - open);
            g += (tgt > g ? aA : aR) * (tgt - g);
            const double gg = 1 - mix + mix * g; L[i] = (float) (a[i] * gg); R[i] = (float) (b[i] * gg);
            beat += dBeat;
        }
        freeBeat = beat;
    }
private:
    double fs = 48000, g = 1, freeBeat = 0;
};

}  // namespace omega::dsp
