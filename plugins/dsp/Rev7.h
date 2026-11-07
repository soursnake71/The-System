// Yamaha REV7 (1985) digital reverberator.
//
// Modeled from the REV7 service manual (archive.org, "yamaha_yamaha_rev7"): the analog path described in its circuit
// description and the 30 factory presets and parameter ranges in its "Memory (ROM) contents" chart.
//
//   input level (8 = unity, 10 = +12 dB) -> 3-band parametric EQ (wet only, before the converter, as on the unit)
//   -> 12 kHz LC low-pass -> pre-emphasis (+8 dB at 10 kHz) -> sample & hold at 31.25 kHz -> 16-bit A/D (one channel)
//   -> DSP at 31.25 kHz -> 16-bit D/A (L/R) -> S/H -> 12 kHz low-pass -> de-emphasis + aperture compensation -> mix.
//
// The DSP side is an original algorithm built to the manual's description of each program type (REV with 3 early
// reflections per channel and separate mid/high/low reverb times; E/R with 18 reflections per channel and the six
// modes; Delay; Echo with high damp in the loop; the modulation types with their +/-4 ms depth). Yamaha's own DSP
// microcode and coefficients live in the unit's EPROM and are not used or included here, so the "invisible"
// per-program values below are this model's own choices, tuned by ear against the manual's descriptions.
#pragma once
#include <atomic>
#include "Rack.h"

namespace omega::dsp {

class Rev7 : public RackUnit {
public:
    using RackUnit::RackUnit;
    static constexpr double FS = 31250;   // the REV7's sampling rate (4 MHz ceramic resonator / 128)

    enum Kind { REV, ER, DLY, ECHO, FLANGE, RFLANGE, CHORUS, PHASE, TREM, SYMPH };
    // the hidden ("invisible") part of each program: what kind it is, the size of its space, its input bandwidth,
    // and for ER programs a small variation of the reflection pattern
    struct Prog { Kind kind; double scale, bw, aux; };
    static const Prog& prog(int n) {
        static const Prog P[31] = {
            {REV, 1, 9000, 0},
            {REV, 1.0, 9000, 0},  {REV, .62, 9500, 0}, {REV, .5, 11000, 1}, {REV, .42, 11500, 1},   // 1-4 halls, plates
            {ER, 1, 12000, 0}, {ER, 1, 12000, 1},                                                     // 5-6 E/R 1, 2
            {DLY, 1, 12000, 0}, {ECHO, 1, 12000, 0},                                                  // 7-8
            {FLANGE, 1, 12000, 0}, {RFLANGE, .8, 10000, 0}, {CHORUS, 1, 12000, 0}, {CHORUS, 1, 12000, 1}, // 9-12
            {PHASE, 1, 12000, 0}, {TREM, 1, 12000, 0}, {SYMPH, 1, 12000, 0},                          // 13-15
            {REV, .55, 10000, 0}, {REV, .6, 8500, 0}, {REV, .9, 10500, 0},                            // 16-18
            {ER, 1, 9000, 2}, {ER, 1, 8000, 3}, {ER, 1, 7000, 4},                                     // 19-21 bass A, B, kick
            {REV, .45, 11500, 1},                                                                     // 22 snare
            {ER, 1, 12000, 5}, {ER, 1, 12000, 6}, {ER, 1, 11000, 7}, {ER, 1, 10000, 8}, {ER, 1, 11000, 9}, {ER, 1, 11500, 10}, // 23-28
            {REV, .4, 12000, 1}, {ER, 1, 12000, 11},                                                  // 29 handclaps, 30 live ref
        };
        return P[n < 1 || n > 30 ? 0 : n];
    }

    // the front-panel switches: MUTE, BYPASS, MONO/STEREO (mono: the left input feeds both sides)
    void process(const float* inL, const float* inR, const float* scL, const float* scR, float* L, float* R, int N, const Transport& t) override {
        if (v("mute") >= .5) { for (int i = 0; i < N; ++i) L[i] = R[i] = 0.f; return; }
        if (v("bypass") >= .5) { for (int i = 0; i < N; ++i) { L[i] = inL ? inL[i] : 0.f; R[i] = inR ? inR[i] : L[i]; } return; }
        if (v("mono") >= .5 && inL) inR = inL;
        RackUnit::process(inL, inR, scL, scR, L, R, N, t);
    }
    float level() const override { return meter.load(); }

protected:
    std::atomic<float> meter { 0.f };
    // ---- analog section, at the host rate --------------------------------------------------------------------
    struct Shelf {   // first-order shelf by the bilinear transform: zero at fz, pole at fp
        double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
        void set(double fs, double fz, double fp) {
            const double k = 2 * fs, wz = 2 * PI * fz, wp = 2 * PI * fp;   // H(s) = (1 + s/wz) / (1 + s/wp)
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

    void build() override {
        line.init((int) (FS * 2.6));
        for (int c = 0; c < 2; ++c) { loop[c].init((int) (FS * .5)); mod[c].init((int) (FS * .06)); erAp[c][0].init(160); erAp[c][1].init(160); outAp[c].init(400); }
        for (int k = 0; k < 4; ++k) inAp[k].init(400);
        for (int k = 0; k < 8; ++k) fdn[k].init(4096);
        // LC filters: 12 kHz, sixth order (Butterworth pole Qs)
        for (LPN* f : {&aaf, &rcf[0], &rcf[1]}) f->init(fs, 12000, 6, {.518, .707, 1.932});
        pre.set(fs, 3200, 12000); de[0].set(fs, 12000, 3200); de[1].set(fs, 12000, 3200);
        aperture[0] = aperture[1] = peak(fs, 11000, .7, 2.2);   // the S/H's sinc droop (-2.2 dB at 12 kHz) put back
        buildPatterns();
    }
    void reset() override {
        line.clear(); for (auto& d : loop) d.clear(); for (auto& d : mod) d.clear(); for (auto& d : fdn) d.clear(); for (auto& d : inAp) d.clear();
        for (auto& c : erAp) { for (auto& d : c) d.clear(); }
        for (auto& d : outAp) d.clear();
        aaf.init(fs, 12000, 6, {.518, .707, 1.932}); for (auto& f : rcf) f.init(fs, 12000, 6, {.518, .707, 1.932});
        pre.clear(); for (auto& s : de) s.clear(); for (auto& b : eqb) b.z1 = b.z2 = 0; for (auto& b : aperture) b.z1 = b.z2 = 0;
        for (auto& b : bands) { b = {}; }
        for (double& d : damp) d = 0;
        bwz = 0; acc = 0; xPrev = 0; y0[0] = y0[1] = y1[0] = y1[1] = 0;
    }
    double tail() override {
        double t = .3 + v("idly") / 1000;
        switch (P.kind) {
            case REV: case RFLANGE: t += v("rt") * std::max(1.0, v("lo")) * 1.15; break;
            case ER: t += .07 * v("size") + .3; break;
            case DLY: t += std::max(v("dlyL"), v("dlyR")) / 1000; break;
            case ECHO: t += tailOf(std::max(v("dlyL"), v("dlyR")), v("fb")); break;
            case FLANGE: t += tailOf(v("mdly") + 4, v("fb")); break;
            default: t += .2; break;
        }
        return std::min(30.0, t + v("fdly") / 1000);
    }
    void cfg() override {
        num = (int) v("prog"); P = prog(num);
        rt = v("rt"); hiR = v("hi"); loR = v("lo"); diff = v("diff"); live = v("live"); mode = std::max(1, std::min(6, (int) v("mode"))); size = v("size");
        circuit = v("circ") >= .5;
    }
    void update() override {
        // EQ: low / mid / high, each a peaking band (Q fixed), as on the front panel
        eqOn = v("eq") >= .5;
        const char* f[3] = {"eqlf", "eqmf", "eqhf"}; const char* g[3] = {"eqlg", "eqmg", "eqhg"};
        for (int k = 0; k < 3; ++k) { BQ n = peak(fs, v(f[k]), k == 1 ? 1.0 : .8, v(g[k])); n.keepState(eqb[k]); eqb[k] = n; }
        const double inPos = v("in"); inGain = inPos <= 0 ? 0 : std::pow(10, (inPos - 8) * 6 / 20);
        bwA = 1 - std::exp(-2 * PI * P.bw / FS);
        // REV tank: 8 delay lines (scaled by the program's room) with mid / high / low decay
        static const int base[8] = {1061, 1277, 1489, 1667, 1847, 2011, 2203, 2399};
        for (int k = 0; k < 8; ++k) {
            len[k] = std::max(40, (int) jround(base[k] * P.scale));
            auto g = [&](double t) { return std::pow(10, -3 * len[k] / (std::max(.05, t) * FS)); };
            gM[k] = g(rt); gH[k] = g(rt * hiR); gL[k] = std::min(.9995, g(rt * loR));
        }
        lo500 = 1 - std::exp(-2 * PI * 500 / FS); lo4k = 1 - std::exp(-2 * PI * 4000 / FS);
        const double plate = P.aux > .5 && P.kind == REV ? .08 : 0;   // the plates are denser
        apG = diff <= 0 ? .1 : .34 + .041 * diff + plate;
        // early reflections
        if (P.kind == REV) setRevER(); else if (P.kind == ER) setER();
        // echo high damp: a low-pass in the loop
        const double hd = v("hdamp"); dampA = 1 - std::exp(-2 * PI * (12000 / (1 + .35 * hd)) / FS);
    }

    // ---- the reflection patterns -----------------------------------------------------------------------------
    // 18 reflections per channel for each of the six E/R modes, generated once (times in ms at room size x1)
    struct Pat { double t[18], g[18]; };
    Pat pat[6][2] {};
    void buildPatterns() {
        for (int m = 0; m < 6; ++m) for (int c = 0; c < 2; ++c) {
            uint32_t s = 0x5eed + (uint32_t) (m * 977 + c * 131);
            auto rnd = [&] { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0; };
            Pat& p = pat[m][c];
            for (int k = 0; k < 18; ++k) {
                const double x = (k + .25 + .5 * rnd()) / 18; double t = 0;
                switch (m) {
                    case 0: t = 1.5 + 28 * std::pow(x, 1.3); break;              // small hall
                    case 1: t = 3 + 58 * std::pow(x, 1.15); break;               // large hall
                    case 2: t = 1 + 44 * rnd(); break;                           // random
                    case 3: t = 2 + 48 * x; break;                               // reverse
                    case 4: t = .7 + 17 * x; break;                              // plate
                    case 5: t = 4.1 * (k / 3 + 1) * (1 + .035 * c) + (k % 3) * .37 + .15 * rnd(); break;   // spring
                }
                p.t[k] = t; p.g[k] = (rnd() < .5 ? -1 : 1) * (.75 + .25 * rnd());
            }
            // sort by time so the envelope follows arrival order
            for (int i = 0; i < 18; ++i) for (int j = i + 1; j < 18; ++j) if (p.t[j] < p.t[i]) { std::swap(p.t[i], p.t[j]); std::swap(p.g[i], p.g[j]); }
        }
    }
    int nTap = 0; int tapD[2][18] {}; double tapG[2][18] {};
    void setER() {
        const int m = mode - 1; const double a = (10 - live) * .55 + .3;   // liveness: how fast the reflections fade
        const double var = 1 + .06 * std::sin(P.aux * 1.7);              // each E/R program's own slight variation
        for (int c = 0; c < 2; ++c) {
            double ss = 0;
            for (int k = 0; k < 18; ++k) {
                const double x = k / 17.0; double env;
                if (m == 3) env = std::pow(x + 1.0 / 18, .5 + (10 - live) * .2);   // reverse: rising
                else if (m == 2) env = std::exp(-x * a * .35);                        // random: nearly flat, then cut
                else env = std::exp(-x * a);
                tapD[c][k] = std::max(1, (int) jround(pat[m][c].t[k] * var * size * FS / 1000));
                tapG[c][k] = pat[m][c].g[k] * env; ss += tapG[c][k] * tapG[c][k];
            }
            const double nrm = .75 / std::sqrt(std::max(1e-9, ss)); for (double& gk : tapG[c]) gk *= nrm;
        }
        nTap = 18;
    }
    void setRevER() {   // 3 per channel ahead of the tank
        static const double tL[3] = {7.1, 13.9, 22.7}, tR[3] = {9.3, 17.1, 26.3}, gg[3] = {.42, -.33, .26};
        for (int k = 0; k < 3; ++k) {
            tapD[0][k] = (int) jround(tL[k] * P.scale * FS / 1000); tapD[1][k] = (int) jround(tR[k] * P.scale * FS / 1000);
            tapG[0][k] = gg[k]; tapG[1][k] = -gg[k] * .9;
        }
        nTap = 3;
    }

    // ---- the DSP, one sample at 31.25 kHz --------------------------------------------------------------------
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
        double sum = 0; for (double q : r) sum += q; sum *= .25;   // Householder: I - 2/N
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
        const int id = std::max(1, (int) jround(v("idly") / 1000 * FS));
        const double xi = line.at(id);
        oL = oR = 0;
        const double mf = v("mfreq"), md = v("mdep") * .004 * FS;   // full depth: +/-4 ms
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
                const double lev = v("dlev");
                oL = line.at(id + std::max(1, (int) jround(v("dlyL") / 1000 * FS))) * lev;
                oR = line.at(id + std::max(1, (int) jround(v("dlyR") / 1000 * FS))) * lev;
                break;
            }
            case ECHO: {
                const double fb = v("fb"); const char* dk[2] = {"dlyL", "dlyR"}; double* o[2] = {&oL, &oR};
                for (int c = 0; c < 2; ++c) {
                    const int n = std::max(1, (int) jround(std::min(450.0, v(dk[c])) / 1000 * FS));
                    const double y = loop[c].at(n); damp[c] += dampA * (y - damp[c]);
                    loop[c].push(xi + fb * damp[c]); *o[c] = y;
                }
                break;
            }
            case FLANGE: case RFLANGE: {
                double sL = xi, sR = xi;
                if (P.kind == RFLANGE) { reverb(xi, sL, sR); }
                const double ph = lfoStep(lfo, mf), fb = P.kind == RFLANGE ? .3 : v("fb");
                const double base = v("mdly") / 1000 * FS;
                const double dL = std::max(1.0, base + md * std::sin(ph)), dR = std::max(1.0, base - md * std::sin(ph));
                const double yL = mod[0].lerp(dL), yR = mod[1].lerp(dR);
                mod[0].push(sL + fb * yL); mod[1].push(sR + fb * yR);
                oL = .7 * (sL + yL); oR = .7 * (sR + yR); break;
            }
            case CHORUS: {
                const double ph = lfoStep(lfo, mf), am = v("amdep"), base = (P.aux > .5 ? 9.0 : 12.5) / 1000 * FS;
                mod[0].push(xi);
                const double yL = mod[0].lerp(base + md * std::sin(ph)), yR = mod[0].lerp(base + md * std::sin(ph + PI));
                const double gL2 = 1 - am * .5 * (1 + std::sin(ph + PI / 2)), gR2 = 1 - am * .5 * (1 + std::sin(ph - PI / 2));
                oL = .7 * (xi + yL * gL2); oR = .7 * (xi + yR * gR2); break;
            }
            case PHASE: {
                const double ph = lfoStep(lfo, mf), base = std::min(8.0, v("mdly")) / 1000 * FS;
                mod[0].push(xi);
                const double fixed = mod[0].lerp(std::max(.5, base)), centre = mod[0].lerp(std::max(.5, base + md * std::sin(ph)));
                oL = .5 * (fixed + centre) + .35 * xi; oR = .5 * (centre - fixed) + .35 * xi; break;
            }
            case TREM: {   // the manual calls it tremolo; it is a delay-modulation vibrato
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
        // the 1st reflection: mono, in the centre, its delay counted from the direct sound
        const double fl = v("flev");
        if (fl > 0) { const double r = line.at(std::max(1, (int) jround(v("fdly") / 1000 * FS))) * fl; oL += r; oR += r; }
    }

    // ---- host-rate wrapper: the converters ----------------------------------------------------------------------
    static double q16(double x) { x = x > 1 ? 1 : x < -1 ? -1 : x; return jround(x * 32767) / 32767; }
    void run(const float* a, const float* b, float* L, float* R, int N) override {
        const double ratio = FS / fs, ret = v("ret") * (v("phase") >= .5 ? -1 : 1), adcScale = .7;   // host full scale sits 3 dB under the A/D's clip
        double pk = 0;
        for (int i = 0; i < N; ++i) {
            double x = in(a, b, i) * inGain;
            if (eqOn) for (auto& q : eqb) x = q.run(x);
            if (circuit) x = pre.run(aaf.run(x));
            pk = std::max(pk, std::fabs(x * adcScale));   // the meter reads after the pre-emphasis, as on the unit
            const double before = acc; acc += ratio;
            // a 31.25 kHz tick happens at each integer crossing of acc
            for (double k = std::floor(before) + 1; k <= acc; k += 1) {
                const double f = (k - before) / ratio;   // where in this host sample it falls
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
        meter.store((float) std::max(pk, meter.load() * std::exp(-N / (fs * .3))));
    }

    Prog P = prog(1); int num = 1, mode = 1;
    double rt = 2.6, hiR = .3, loR = 1.2, diff = 5, live = 5, size = 2;
    bool circuit = true, eqOn = false;
    double inGain = 1, bwA = .8, apG = .55, lo500 = .1, lo4k = .5, dampA = .5;
    int len[8] {}; double gM[8] {}, gH[8] {}, gL[8] {};
    DLine line, loop[2], mod[2], fdn[8], inAp[4], erAp[2][2], outAp[2];
    LPN aaf, rcf[2]; Shelf pre, de[2]; BQ eqb[3], aperture[2];
    double acc = 0, xPrev = 0, y0[2] {}, y1[2] {};
};

}  // namespace omega::dsp
