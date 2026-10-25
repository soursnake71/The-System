// Prophet-5 (Rev 3) voice model, ported from the web app's worklet (P5_CODE in index.html):
// 2 x CEM3340 VCO per voice, mixer with noise, CEM3320 4-pole OTA ladder (2x oversampled), 2 x CEM3310 ADSR, CA3280 VCA,
// Poly-Mod, Wheel-Mod (LFO / pink noise), 7-bit knob CVs, five voices round robin, unison with glide, vintage drift.
#pragma once
#include <algorithm>
#include <vector>
#include "../common/Spec.h"
#include "Common.h"

namespace omega::dsp {

struct P5Cfg {
    double oAf = .5, oApw = .5, oBf = .5, oBfine = 0, oBpw = .5, mA = .8, mB = 0, mN = 0, cut = .6, res = .1, fenv = .4, fa = 0, fd = .45, fs = .4, fr = .4,
           aa = 0, ad = .4, as = .9, ar = .4, pmEnv = 0, pmB = 0, lfoF = .6, wmMix = 0, glide = 0, vint = .35, vol = .8;
    bool oAsaw = true, oApul = false, sync = false, oBsaw = true, oBtri = false, oBpul = false, oBlo = false, oBkbd = true, fkbd = true, rel = true,
         pmFA = false, pmPW = false, pmF = false, lfoSaw = false, lfoTri = true, lfoSq = false, wmFA = true, wmFB = true, wmPWA = false, wmPWB = false, wmF = false, uni = false;
};

class P5Core {
public:
    P5Cfg C; double wheel = 0, bend = 0;
    void prepare(double sampleRate) {
        fs = sampleRate; double seed = 12345;
        auto r = [&] { seed = std::fmod(seed * 16807, 2147483647); return seed / 2147483647 * 2 - 1; };
        for (auto& v : V) { v = Voice(); v.pa = rnd(); v.pb = rnd(); v.offA = r(); v.offB = r(); v.offF = r(); v.offT = r(); v.offPW = r(); }
        lcg = seed;
    }
    void noteOn(int n) {
        held.erase(std::remove(held.begin(), held.end(), n), held.end()); held.push_back(n);
        if (C.uni) { const int low = *std::min_element(held.begin(), held.end()); for (auto& v : V) { const bool fresh = !v.gate; v.note = low; v.gate = true; if (fresh) trig(v); } return; }
        Voice* v = nullptr;
        for (auto& x : V) if (x.note == n) { v = &x; break; }
        const int NV = 5;
        if (!v) for (int k = 0; k < NV && !v; ++k) { auto& c = V[(rr + k) % NV]; if (!c.gate && c.ast == 0) v = &c; }
        if (!v) { Voice* best = nullptr; for (auto& x : V) if (!x.gate && (!best || x.age < best->age)) best = &x;
                  if (!best) for (auto& x : V) if (!best || x.age < best->age) best = &x; v = best; }
        rr = (int) (v - V + 1) % NV;
        v->note = n; v->gate = true; v->age = frame; v->hasGlide = false; trig(*v);
    }
    void noteOff(int n) {
        held.erase(std::remove(held.begin(), held.end(), n), held.end());
        if (C.uni) { if (!held.empty()) { const int low = *std::min_element(held.begin(), held.end()); for (auto& v : V) v.note = low; } else for (auto& v : V) v.gate = false; return; }
        for (auto& v : V) if (v.note == n && v.gate) v.gate = false;
    }
    void allOff() { held.clear(); for (auto& v : V) v.gate = false; }
    bool active() const { for (auto& v : V) if (v.note >= 0 && (v.gate || v.ast != 0)) return true; return false; }

    // adds the synth into out (mono), n samples
    void render(float* out, int N) {
        if (!active()) { std::fill(out, out + N, 0.f); frame += N; return; }
        const double fos = fs * 2, LN2 = std::log(2.0);
        const double stA = jround(q7(C.oAf) * 48) - 24, stB = jround(q7(C.oBf) * 48) - 24 + q7(C.oBfine);
        const double pwA = .01 + .98 * q7(C.oApw), pwB = .01 + .98 * q7(C.oBpw);
        const double cutSt = jround(q7(C.cut) * 127), res = q7(C.res), k = res <= .7 ? 3.9 * res / .7 : 3.9 + (res - .7) * 2.2;
        const double fa = envT(C.fa), fd = envT(C.fd), fsus = q7(C.fs), fr = C.rel ? envT(C.fr) : .004;
        const double aa = envT(C.aa), ad = envT(C.ad), asus = q7(C.as), ar = C.rel ? envT(C.ar) : .004;
        const double lfoHz = .04 * std::pow(500, q7(C.lfoF)), loHz = .4 * std::pow(75, q7(C.oBf));
        const double glideTau = C.uni && C.glide > 0 ? .0005 * std::pow(3000, q7(C.glide)) : 0, gl = glideTau ? 1 - std::exp(-1 / (glideTau * fs)) : 1;
        const double vint = C.vint, mA = q7(C.mA) * .55, mB = q7(C.mB) * .55, mN = q7(C.mN) * .44, pmE = q7(C.pmEnv), pmBq = q7(C.pmB), fenv = q7(C.fenv) * 9, wmMix = q7(C.wmMix);
        const bool audioPM = pmBq > 0 && (C.pmFA || C.pmF);
        const double gk = -2 * PI * 1.073 / fos, lgB = std::log2(261.63), lgLo = std::log2(loHz);
        for (auto& v : V) {
            const double sp = 1 + .06 * vint * v.offT;
            v.kFa = std::exp(-1.466 / (fa * sp * fs)); v.kFd = std::exp(-3 / (fd * sp * fs)); v.kFr = std::exp(-3 / (fr * sp * fs));
            v.kAa = std::exp(-1.466 / (aa * sp * fs)); v.kAd = std::exp(-3 / (ad * sp * fs)); v.kAr = std::exp(-3 / (ar * sp * fs));
        }
        for (int i = 0; i < N; ++i, ++frame) {
            lfoP = std::fmod(lfoP + lfoHz / fs, 1.0);
            double lfo = 0; int nsh = 0;
            if (C.lfoSaw) { lfo += 2 * lfoP - 1; nsh++; } if (C.lfoTri) { lfo += 1 - 4 * std::fabs(lfoP - .5); nsh++; } if (C.lfoSq) { lfo += lfoP < .5 ? 1 : -1; nsh++; }
            if (nsh > 1) lfo /= nsh;
            const double w = rnd() * 2 - 1; double* pk = pink;
            pk[0] = .99886 * pk[0] + w * .0555179; pk[1] = .99332 * pk[1] + w * .0750759; pk[2] = .969 * pk[2] + w * .153852; pk[3] = .8665 * pk[3] + w * .3104856;
            pk[4] = .55 * pk[4] + w * .5329522; pk[5] = -.7616 * pk[5] - w * .016898; const double pinkN = (pk[0] + pk[1] + pk[2] + pk[3] + pk[4] + pk[5] + pk[6] + w * .5362) * .11; pk[6] = w * .115926;
            const double wm = ((1 - wmMix) * lfo + wmMix * pinkN * 3) * wheel; const bool ctl = (i & 7) == 0;
            double o = 0;
            for (auto& v : V) {
                if (v.note < 0 || (!v.gate && v.ast == 0)) continue;
                const double target = v.note + bend;
                v.glide = !v.hasGlide || !glideTau ? target : v.glide + (target - v.glide) * gl; v.hasGlide = true;
                v.driftA += (r1() * .02 - v.driftA * .0005) / fs * 40; v.driftB += (r1() * .02 - v.driftB * .0005) / fs * 40;
                const double fe = envF(v, fsus), ae = envA(v, asus);
                if (ctl || !v.ok) {
                    v.ok = true;
                    const double kb = (v.glide - 60) / 12;
                    const double eB = (C.oBlo ? lgLo : lgB + stB / 12) + (C.oBkbd ? kb : 0) + (vint * (v.offB * 3 + v.driftB * 4)) / 1200 + (C.wmFB ? wm * .5 : 0);
                    v.dB = std::min(.45, std::exp(eB * LN2) / fs);
                    v.pbW = std::max(.01, std::min(.99, pwB + (C.wmPWB ? wm * .45 : 0) + vint * .01 * v.offPW));
                    v.eA = lgB + kb + stA / 12 + (vint * (v.offA * 3 + v.driftA * 4)) / 1200 + (C.wmFA ? wm * .5 : 0);
                    v.paW0 = pwA + (C.wmPWA ? wm * .45 : 0) + vint * .01 * v.offPW;
                    v.fO = cutSt / 12 + (C.fkbd ? kb : 0) + (C.wmF ? wm * 3 : 0) + vint * .05 * v.offF;
                    if (!audioPM) {
                        const double pm = pmE * fe;
                        v.dA = std::min(.45, std::exp((v.eA + (C.pmFA ? pm * 4 : 0)) * LN2) / fs);
                        v.g = 1 - std::exp(gk * std::min(.42 * fos, 16.35 * std::exp((v.fO + fenv * fe + (C.pmF ? pm * 5 : 0)) * LN2)));
                    }
                }
                const double dB = v.dB, pbW = v.pbW;
                double pB = v.pb + dB; bool wrapped = false; if (pB >= 1) { pB -= 1; wrapped = true; } v.pb = pB;
                double oscB = 0;
                if (C.oBsaw) oscB += 2 * pB - 1 - blep(pB, dB);
                if (C.oBtri) oscB += 1 - 4 * std::fabs(pB - .5);
                if (C.oBpul) oscB += (pB < pbW ? 1 : -1) + blep(pB, dB) - blep(std::fmod(pB - pbW + 1, 1.0), dB);
                const double pm = pmE * fe + pmBq * oscB;
                double dA = v.dA, g = v.g;
                if (audioPM) {
                    dA = std::min(.45, std::exp((v.eA + (C.pmFA ? pm * 4 : 0)) * LN2) / fs);
                    g = 1 - std::exp(gk * std::min(.42 * fos, 16.35 * std::exp((v.fO + fenv * fe + (C.pmF ? pm * 5 : 0)) * LN2)));
                }
                const double paW = std::max(.01, std::min(.99, v.paW0 + (C.pmPW ? pm * .5 : 0)));
                double pA = v.pa + dA, jump = 0, tj = 0;
                if (C.sync && wrapped && !C.oBlo) {
                    const double frac = pB / dB, before = std::fmod(pA - frac * dA, 1.0), after = frac * dA;
                    if (C.oAsaw) jump += (2 * after - 1) - (2 * before - 1);
                    if (C.oApul) jump += (after < paW ? 1 : -1) - (before < paW ? 1 : -1);
                    pA = after; tj = frac;
                } else if (pA >= 1) pA -= 1;
                v.pa = pA;
                double oscA = 0;
                if (C.oAsaw) oscA += 2 * pA - 1 - (jump != 0 ? 0 : blep(pA, dA));
                if (C.oApul) oscA += (pA < paW ? 1 : -1) + (jump != 0 ? 0 : blep(pA, dA)) - blep(std::fmod(pA - paW + 1, 1.0), dA);
                if (jump != 0) { const double d = std::min(1.0, tj); oscA -= jump * (1 - d) * (1 - d) * .5; }
                const double x = mA * oscA + mB * oscB + mN * w + w * 2e-5;
                double* l = v.lad; double y = 0;
                for (int s = 0; s < 2; ++s) {
                    const double u = tanhA(x - k * l[3]);
                    const double t0 = tanhA(l[0]), t1 = tanhA(l[1]), t2 = tanhA(l[2]);
                    l[0] += g * (u - t0); l[1] += g * (t0 - t1); l[2] += g * (t1 - t2); l[3] += g * (t2 - tanhA(l[3]));
                    y += l[3];
                }
                y *= .5 * (1 + k * .45);
                o += tanhA(y * ae * 1.4);
            }
            out[i] = (float) (o * .32 * C.vol);
        }
    }

private:
    struct Voice {
        int note = -1; bool gate = false, ok = false, hasGlide = false; long long age = 0;
        double pa = 0, pb = 0, glide = 0, fe = 0, ae = 0, lad[4] {}, offA = 0, offB = 0, offF = 0, offT = 0, offPW = 0, driftA = 0, driftB = 0;
        int fst = 0, ast = 0;
        double kFa = 0, kFd = 0, kFr = 0, kAa = 0, kAd = 0, kAr = 0, dA = 0, dB = 0, g = 0, pbW = .5, eA = 0, paW0 = .5, fO = 0;
    };
    static double q7(double x) { return jround(std::max(0.0, std::min(1.0, x)) * 127) / 127; }
    static double envT(double k) { return .001 * std::pow(30000, std::pow(q7(k), .728)); }
    static double blep(double t, double dt) { if (t < dt) { const double x = t / dt; return x + x - x * x - 1; } if (t > 1 - dt) { const double x = (t - 1) / dt; return x * x + x + x + 1; } return 0; }
    static double tanhA(double x) { if (x > 3) return 1; if (x < -3) return -1; const double x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }
    void trig(Voice& v) { v.fst = 1; v.ast = 1; v.ok = false; }
    static double envStep(double& e, int& st, bool gate, double s, double ka, double kd, double kr) {
        if (!gate && st && st < 4) st = 4;
        if (st == 1) { e = 1.3 - (1.3 - e) * ka; if (e >= 1) { e = 1; st = 2; } }
        else if (st == 2 || st == 3) { e = s + (e - s) * kd; st = 3; }
        else if (st == 4) { e *= kr; if (e < 1e-5) { e = 0; st = 0; } }
        return e;
    }
    double envF(Voice& v, double s) { return envStep(v.fe, v.fst, v.gate, s, v.kFa, v.kFd, v.kFr); }
    double envA(Voice& v, double s) { return envStep(v.ae, v.ast, v.gate, s, v.kAa, v.kAd, v.kAr); }
    double r1() { lcg = std::fmod(lcg * 16807, 2147483647); return lcg / 2147483647 * 2 - 1; }
    Voice V[5]; std::vector<int> held; int rr = 0; long long frame = 0; double fs = 48000, lfoP = 0, pink[7] {}, lcg = 12345; Rand rnd;
};

// reads the plugin parameters into a P5 config
template <class Get> void p5Read(P5Cfg& C, Get v) {
    C.oAf = v("oAf"); C.oApw = v("oApw"); C.oBf = v("oBf"); C.oBfine = v("oBfine"); C.oBpw = v("oBpw"); C.mA = v("mA"); C.mB = v("mB"); C.mN = v("mN");
    C.cut = v("cut"); C.res = v("res"); C.fenv = v("fenv"); C.fa = v("fa"); C.fd = v("fd"); C.fs = v("fs"); C.fr = v("fr"); C.aa = v("aa"); C.ad = v("ad"); C.as = v("as"); C.ar = v("ar");
    C.pmEnv = v("pmEnv"); C.pmB = v("pmB"); C.lfoF = v("lfoF"); C.wmMix = v("wmMix"); C.glide = v("glide"); C.vint = v("vint"); C.vol = v("vol");
    auto b = [&](const char* k) { return v(k) >= .5; };
    C.oAsaw = b("oAsaw"); C.oApul = b("oApul"); C.sync = b("sync"); C.oBsaw = b("oBsaw"); C.oBtri = b("oBtri"); C.oBpul = b("oBpul"); C.oBlo = b("oBlo"); C.oBkbd = b("oBkbd");
    C.fkbd = b("fkbd"); C.rel = b("rel"); C.pmFA = b("pmFA"); C.pmPW = b("pmPW"); C.pmF = b("pmF"); C.lfoSaw = b("lfoSaw"); C.lfoTri = b("lfoTri"); C.lfoSq = b("lfoSq");
    C.wmFA = b("wmFA"); C.wmFB = b("wmFB"); C.wmPWA = b("wmPWA"); C.wmPWB = b("wmPWB"); C.wmF = b("wmF"); C.uni = b("uni");
}

class Prophet5 : public Engine {
public:
    using Engine::Engine;
    void prepare(double sr, int maxBlock) override { core.prepare(sr); buf.assign((size_t) maxBlock + 16, 0.f); changed(); }
    void changed() override { p5Read(core.C, [this](const char* k) { return v(k); }); core.wheel = std::max(v("wheel"), wheelCC); }
    void noteOn(int n, float) override { core.noteOn(n); }
    void noteOff(int n) override { core.noteOff(n); }
    void allNotesOff() override { core.allOff(); }
    void pitchBend(double s) override { core.bend = s; }
    void modWheel(double w) override { wheelCC = w; core.wheel = std::max(v("wheel"), w); }
    void process(const float*, const float*, const float*, const float*, float* L, float* R, int n, const Transport&) override {
        if ((int) buf.size() < n) buf.resize((size_t) n);
        core.render(buf.data(), n);
        for (int i = 0; i < n; ++i) L[i] = R[i] = buf[(size_t) i];
    }
private:
    P5Core core; std::vector<float> buf; double wheelCC = 0;
};

}  // namespace omega::dsp
