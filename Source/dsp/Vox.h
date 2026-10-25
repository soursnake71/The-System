// Vocoders: VX-350 (clear model and SVC circuit model) and the Talker pedal.
// C++ port of the page's VxDsp, VxEnsemble, VxClear, VxCircuit, TalkerPedal.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>
#include "Val.h"

namespace farp {

struct Bq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };

struct VxDsp {
    static constexpr double PI_ = 3.14159265358979323846;
    static double run(Bq& q, double x) { const double y = q.b0 * x + q.z1; q.z1 = q.b1 * x - q.a1 * y + q.z2; q.z2 = q.b2 * x - q.a2 * y; return y; }
    static void bp(Bq& q, double fs, double f, double Q) { const double w = 2 * PI_ * std::min(f, fs * .45) / fs, al = std::sin(w) / (2 * Q), a0 = 1 + al; q.b0 = al / a0; q.b1 = 0; q.b2 = -al / a0; q.a1 = -2 * std::cos(w) / a0; q.a2 = (1 - al) / a0; }
    static void hp(Bq& q, double fs, double f, double Q) { const double w = 2 * PI_ * std::min(f, fs * .45) / fs, al = std::sin(w) / (2 * Q), c = std::cos(w), a0 = 1 + al; q.b0 = (1 + c) / 2 / a0; q.b1 = -(1 + c) / a0; q.b2 = q.b0; q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0; }
    static void lp(Bq& q, double fs, double f, double Q) { const double w = 2 * PI_ * std::min(f, fs * .45) / fs, al = std::sin(w) / (2 * Q), c = std::cos(w), a0 = 1 + al; q.b0 = (1 - c) / 2 / a0; q.b1 = (1 - c) / a0; q.b2 = q.b0; q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0; }
    static double ms(double ms_, double fs) { return 1 - std::exp(-1 / (ms_ * 0.001 * fs)); }
    static double db(double d) { return std::pow(10.0, d / 20); }
    static double sgn(double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
    static double soft(double x, double k) { const double a = std::abs(x); if (a <= k) return x; const double o = (a - k) / (1 - k); return sgn(x) * (k + (1 - k) * o / (1 + o)); }
    static double harm(double x, double h) { const double d = 1 + 20 * h; return x * (1 - h) + (0.35 * std::tanh(d * x) + 0.5 * std::abs(x)) * h; }
};

struct VxParams {
    double micGainDb = 0, instGainDb = 0; bool guitarMode = false; double harmonics = 0.3; double bands[10] = {.8, .8, .8, .8, .8, .8, .8, .8, .8, .8};
    double consonant = 0.5, balance = 1; bool ensemble = true, hold = false, stereo = true; double volumeDb = 0;
    void assign(const Val& o) {
        if (o.has("micGainDb")) micGainDb = o["micGainDb"].num(); if (o.has("instGainDb")) instGainDb = o["instGainDb"].num();
        if (o.has("guitarMode")) guitarMode = o["guitarMode"].truthy(); if (o.has("harmonics")) harmonics = o["harmonics"].num();
        if (o.has("bands")) for (int b = 0; b < 10; b++) bands[b] = o["bands"][b].num();
        if (o.has("consonant")) consonant = o["consonant"].num(); if (o.has("balance")) balance = o["balance"].num();
        if (o.has("ensemble")) ensemble = o["ensemble"].truthy(); if (o.has("hold")) hold = o["hold"].truthy(); if (o.has("stereo")) stereo = o["stereo"].truthy();
        if (o.has("volumeDb")) volumeDb = o["volumeDb"].num();
    }
};
struct VxSmooth { double mic, inst, vol, ens, harm, cons, bal, band[10]; };

class VxEnsemble {
public:
    double fs; std::vector<float> buf; int64_t mask; int64_t w = 0; Bq pre, pA, pB; double sI, fI, sP = 0, fP = 0.25, L = 0, R = 0;
    explicit VxEnsemble(double fs_) : fs(fs_) {
        int n = 1; while (n < fs * 0.02) n <<= 1; buf.assign(n, 0.f); mask = n - 1;
        VxDsp::lp(pre, fs, 9000, 0.707); VxDsp::lp(pA, fs, 9000, 0.707); VxDsp::lp(pB, fs, 9000, 0.707);
        sI = 1 / (4.5 * fs); fI = 1 / (0.215 * fs);
    }
    double read(double d) const {
        const double pos = (double)w - d; const double flD = std::floor(pos); const int64_t fl = (int64_t)flD; const double t = pos - flD; const int64_t m = mask;
        const double y0 = buf[(fl - 1) & m], y1 = buf[fl & m], y2 = buf[(fl + 1) & m], y3 = buf[(fl + 2) & m];
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2); return ((c3 * t + c2) * t + c1) * t + y1;
    }
    void process(double x) {
        buf[w & mask] = (float)VxDsp::run(pre, x); sP += sI; if (sP >= 1) sP -= 1; fP += fI; if (fP >= 1) fP -= 1;
        const double m = 0.8 * std::sin(2 * VxDsp::PI_ * sP) + 0.2 * (4 * std::abs(fP - 0.5) - 1), mid = 9.5, half = 5.5;
        auto sat = [](double v) { return v / (1 + 0.15 * std::abs(v)); };
        const double a = VxDsp::run(pA, sat(read(256 * (mid + half * m) * 1e-6 * fs))), b = VxDsp::run(pB, sat(read(256 * (mid - half * m) * 1e-6 * fs)));
        w++; L = 0.7 * (x + a); R = 0.7 * (x + b);
    }
};

class VxClear {
public:
    double fs; Bq ana[10][3], syn[10][3]; double env[10] = {}, envSm[10] = {}, deemph[10];
    double peB0, peB1, peA1, peX = 0, peY = 0; Bq c1, c2; double eA, eR, eRH, eS, mA, mR, gA, gR, dA, dR, gtA, gtR, holdDecay; int holdN; double sk;
    double dcR, dcX = 0, dcY = 0; VxEnsemble ens; VxParams p; double micE = 0, gtrE = 0, carE = 0, gate = 0; bool gateOpen = false; double expCv = 0; int hc = 0; bool held = false;
    bool sInit = false; VxSmooth s{}; double L = 0, R = 0;
    explicit VxClear(double fs_) : fs(fs_), ens(fs_) {
        static const double T[10] = {161, 234, 333, 499, 732, 1098, 1614, 2335, 3326, 4988};
        auto shelf = [](double f, double f1, double f2) { return std::sqrt(1 + (f / f1) * (f / f1)) / std::sqrt(1 + (f / f2) * (f / f2)); };
        for (int b = 0; b < 10; b++) { for (int i = 0; i < 3; i++) { VxDsp::bp(ana[b][i], fs, T[b], 2.5); VxDsp::bp(syn[b][i], fs, T[b], 2.5); } deemph[b] = 1 / shelf(T[b], 1000, 8000); }
        { const double K = 2 * fs, w1 = 2 * VxDsp::PI_ * 1000, w2 = 2 * VxDsp::PI_ * std::min(8000.0, fs * .45), a0 = 1 + K / w2; peB0 = (1 + K / w1) / a0; peB1 = (1 - K / w1) / a0; peA1 = (1 - K / w2) / a0; }
        VxDsp::hp(c1, fs, 6000, 1.2); VxDsp::hp(c2, fs, 6000, 0.707);
        eA = VxDsp::ms(1, fs); eR = VxDsp::ms(8, fs); eRH = VxDsp::ms(8000, fs); eS = 1 - std::exp(-2 * VxDsp::PI_ * 120 / fs);
        mA = VxDsp::ms(2, fs); mR = VxDsp::ms(150, fs); gA = VxDsp::ms(1, fs); gR = VxDsp::ms(200, fs); dA = VxDsp::ms(3, fs); dR = VxDsp::ms(80, fs);
        gtA = VxDsp::ms(3, fs); gtR = VxDsp::ms(60, fs); holdDecay = std::exp(-1 / (8 * fs)); holdN = (int)std::lround(0.05 * fs); sk = VxDsp::ms(20, fs);
        dcR = std::exp(-2 * VxDsp::PI_ * 20 / fs);
    }
    void initS() { s.mic = VxDsp::db(p.micGainDb); s.inst = VxDsp::db(p.instGainDb); s.vol = VxDsp::db(p.volumeDb); s.ens = p.ensemble ? 1 : 0; s.harm = p.harmonics; s.cons = p.consonant; s.bal = p.balance; for (int b = 0; b < 10; b++) s.band[b] = p.bands[b]; sInit = true; }
    void process(double voice, double carrier) {
        const VxParams& P = p; const double k = sk; if (!sInit) initS();
        VxSmooth& S = s; S.mic += k * (VxDsp::db(P.micGainDb) - S.mic); S.inst += k * (VxDsp::db(P.instGainDb) - S.inst); S.vol += k * (VxDsp::db(P.volumeDb) - S.vol); S.ens += k * ((P.ensemble ? 1 : 0) - S.ens);
        S.harm += k * (P.harmonics - S.harm); S.cons += k * (P.consonant - S.cons); S.bal += k * (P.balance - S.bal); for (int b = 0; b < 10; b++) S.band[b] += k * (P.bands[b] - S.band[b]);
        if (P.hold) { if (hc < holdN) hc++; else held = true; } else { hc = 0; held = false; }
        const double mic = voice * S.mic, ma = std::abs(mic); micE += (ma > micE ? mA : mR) * (ma - micE);
        if (held) expCv *= holdDecay; else expCv = micE;
        const double cg = std::min(10.0, std::max(0.1, std::sqrt(0.2 / std::max(micE, 1e-5)))), pin = held ? 0 : mic * cg;
        const double ain = peB0 * pin + peB1 * peX - peA1 * peY; peX = pin; peY = ain;
        double c = carrier * S.inst; const double ca = std::abs(c); carE += (ca > carE ? dA : dR) * (ca - carE);
        if (P.guitarMode) { gtrE += (ca > gtrE ? gA : gR) * (ca - gtrE); c *= std::min(20.0, std::max(0.0, 0.3 / std::max(gtrE, 1e-5))); const double h = VxDsp::harm(c, S.harm), y = h - dcX + dcR * dcY; dcX = h; dcY = y; c = y; }
        if (carE > 0.003) gateOpen = true; else if (carE < 0.0015) gateOpen = false; const double gt = gateOpen ? 1 : 0; gate += (gt > gate ? gtA : gtR) * (gt - gate);
        double sum = 0; const double rel = held ? eRH : eR;
        for (int b = 0; b < 10; b++) {
            Bq* A = ana[b]; double a = VxDsp::run(A[2], VxDsp::run(A[1], VxDsp::run(A[0], ain))); a = std::abs(a); env[b] += (a > env[b] ? eA : rel) * (a - env[b]); envSm[b] += eS * (env[b] - envSm[b]);
            const double duty = std::min(1.0, std::max(0.0, (envSm[b] - 0.001) * 3)); Bq* Y = syn[b]; double y = VxDsp::run(Y[1], VxDsp::run(Y[0], c)); y = VxDsp::run(Y[2], y * duty); sum += y * S.band[b] * deemph[b];
        }
        double voc = sum * 35 * std::min(1.0, expCv / 0.2);
        const double hp = VxDsp::run(c2, VxDsp::run(c1, mic)); voc += hp * S.cons * (held ? 0 : std::min(1.0, carE / 0.1)); voc *= gate;
        double l = voc, r = voc; ens.process(S.ens > 1e-4 ? voc : 0); if (S.ens > 1e-4) { l = voc + S.ens * (ens.L - voc); r = voc + S.ens * (ens.R - voc); }
        const double vg = std::sin(S.bal * VxDsp::PI_ / 2), dg = std::cos(S.bal * VxDsp::PI_ / 2); l = (l * vg + mic * dg) * S.vol; r = (r * vg + mic * dg) * S.vol; if (!P.stereo) l = r = 0.5 * (l + r);
        L = VxDsp::soft(l, 0.8); R = VxDsp::soft(r, 0.8);
    }
};

class VxCircuit {
public:
    struct Det { double vc = 0, v1 = 0, v2 = 0; };
    struct CDet { double vh = 0, v1 = 0, v2 = 0; };
    struct Fir { double buf[254] = {}; int pos = 0; };
    int factor; double fs; double sumR[10] = {56e3, 56e3, 56e3, 56e3, 68e3, 82e3, 100e3, 100e3, 100e3, 100e3}; double synBRinNom = 22e3 + 6.8e3 / 2;
    Bq aA[10], aB[10], sA[10], sB[10]; Det det[10];
    double peB0, peA1, peX = 0, peY = 0; Bq hpf[3]; double hp1b0, hp1a1, hp1x = 0, hp1y = 0; double hfB0, hfA1, hfX = 0, hfY = 0, hfNode = 0;
    double T; CDet micD, outD; double micY = 0, outY = 0, carDet = 0, carA; double gate = 0; bool gateOpen = false; double gA, gR, expCv = 0; int hc = 0; bool held = false; int holdN; double holdDecay;
    double gtr = 0, gtrA, gtrR, dcR, dcX = 0, dcY = 0, sk, vLow = 0.6; VxEnsemble ens; VxParams p; bool sInit = false; VxSmooth s{};
    std::vector<int> nz; std::vector<double> hz; Fir fir[4]; double L = 0, R = 0, oL = 0, oR = 0;

    static const double* HB() { static const double h[127] = {-1.4312955212e-05,-1.5896435997e-08,2.3687908834e-05,8.6212468188e-09,-4.2587036693e-05,-4.6936576562e-08,7.0847325091e-05,4.7601995464e-08,-1.1135922590e-04,-9.5612520489e-08,1.6776892255e-04,1.1365945542e-07,-2.4417767030e-04,-1.8454245198e-07,3.4554367973e-04,2.1572517139e-07,-4.7735920416e-04,-3.0959578192e-07,6.4607380070e-04,3.7192803994e-07,-8.5877124131e-04,-4.9053331378e-07,1.1236115821e-03,5.7227949030e-07,-1.4495504906e-03,-7.1838036555e-07,1.8468561249e-03,8.3338939003e-07,-2.3269311867e-03,-1.0053803049e-06,2.9029967183e-03,1.1393516436e-06,-3.5901815192e-03,-1.3310819614e-06,4.4065940785e-03,1.4864892213e-06,-5.3740115800e-03,-1.6932851157e-06,6.5198885995e-03,1.8563843453e-06,-7.8795465192e-03,-2.0662197099e-06,9.5006239393e-03,2.2307748074e-06,-1.1449450651e-02,-2.4364017327e-06,1.3822975909e-02,2.5874421431e-06,-1.6769280557e-02,-2.7688099108e-06,2.0528113717e-02,2.8914165620e-06,-2.5514575810e-02,-3.0444544301e-06,3.2515555907e-02,3.1366892195e-06,-4.3221524510e-02,-3.2498326557e-06,6.2036006122e-02,3.2925685089e-06,-1.0512045301e-01,-3.3523473028e-06,3.1798107152e-01,5.0000334308e-01,3.1798107152e-01,-3.3523473028e-06,-1.0512045301e-01,3.2925685089e-06,6.2036006122e-02,-3.2498326557e-06,-4.3221524510e-02,3.1366892195e-06,3.2515555907e-02,-3.0444544301e-06,-2.5514575810e-02,2.8914165620e-06,2.0528113717e-02,-2.7688099108e-06,-1.6769280557e-02,2.5874421431e-06,1.3822975909e-02,-2.4364017327e-06,-1.1449450651e-02,2.2307748074e-06,9.5006239393e-03,-2.0662197099e-06,-7.8795465192e-03,1.8563843453e-06,6.5198885995e-03,-1.6932851157e-06,-5.3740115800e-03,1.4864892213e-06,4.4065940785e-03,-1.3310819614e-06,-3.5901815192e-03,1.1393516436e-06,2.9029967183e-03,-1.0053803049e-06,-2.3269311867e-03,8.3338939003e-07,1.8468561249e-03,-7.1838036555e-07,-1.4495504906e-03,5.7227949030e-07,1.1236115821e-03,-4.9053331378e-07,-8.5877124131e-04,3.7192803994e-07,6.4607380070e-04,-3.0959578192e-07,-4.7735920416e-04,2.1572517139e-07,3.4554367973e-04,-1.8454245198e-07,-2.4417767030e-04,1.1365945542e-07,1.6776892255e-04,-9.5612520489e-08,-1.1135922590e-04,4.7601995464e-08,7.0847325091e-05,-4.6936576562e-08,-4.2587036693e-05,8.6212468188e-09,2.3687908834e-05,-1.5896435997e-08,-1.4312955212e-05}; return h; }

    explicit VxCircuit(double fs0) : factor(fs0 < 88000 ? 2 : 1), fs(fs0 * (fs0 < 88000 ? 2 : 1)), ens(fs0 * (fs0 < 88000 ? 2 : 1)) {
        const double F = fs;
        static const double anaC[10] = {0.068e-6, 0.047e-6, 0.033e-6, 0.022e-6, 0.015e-6, 0.01e-6, 0.0068e-6, 0.0047e-6, 0.0033e-6, 0.0022e-6};
        static const double synC[10] = {0.056e-6, 0.039e-6, 0.027e-6, 0.018e-6, 0.012e-6, 0.0082e-6, 0.0056e-6, 0.0039e-6, 0.0027e-6, 0.0018e-6};
        for (int b = 0; b < 10; b++) { setMFB(aA[b], F, 33e3, 1.2e3, 220e3, anaC[b]); setMFB(aB[b], F, 27e3, 1e3, 180e3, anaC[b]); setMFB(sA[b], F, 180e3, 1.2e3, 220e3, synC[b]); setMFB(sB[b], F, synBRinNom, 1e3, 180e3, synC[b]); }
        { const double w = 1 / (10e3 * 0.012e-6), K = w / std::tan(w / (2 * F)), g = -15e3 / 10e3; peB0 = g * K / (K + w); peA1 = (w - K) / (K + w); }
        { const double fq[3][2] = {{4046.64, 1.2706}, {4693.82, 0.62281}, {4452.41, 0.67085}}; for (int i = 0; i < 3; i++) { const double w = 2 * VxDsp::PI_ * fq[i][0]; a2b(hpf[i], F, 1, 0, 0, 1, w / fq[i][1], w * w); } }
        { const double w = 2 * VxDsp::PI_ * 3175.61, K = w / std::tan(w / (2 * F)); hp1b0 = K / (K + w); hp1a1 = (w - K) / (K + w); }
        { const double w = 1 / (10e3 * 0.01e-6), K = w / std::tan(w / (2 * F)); hfB0 = K / (K + w); hfA1 = (w - K) / (K + w); }
        T = 1 / F; carA = VxDsp::ms(15, F); gA = VxDsp::ms(2, F); gR = VxDsp::ms(40, F); holdN = (int)std::lround(0.05 * F); holdDecay = std::exp(-1 / (8.6 * F));
        gtrA = VxDsp::ms(1, F); gtrR = VxDsp::ms(200, F); dcR = std::exp(-2 * VxDsp::PI_ * 20 / F); sk = VxDsp::ms(20, F);
        const double* H = HB(); for (int k = 0; k < 127; k++) if (std::abs(H[k]) > 1e-9) { nz.push_back(k); hz.push_back(H[k]); }
    }
    static void a2b(Bq& q, double fs, double b0, double b1, double b2, double a0, double a1, double a2) {
        const double w0 = std::sqrt(a2 / a0), K = w0 / std::tan(std::min(w0 / (2 * fs), 1.5)), K2 = K * K, A0 = a0 * K2 + a1 * K + a2;
        q.b0 = (b0 * K2 + b1 * K + b2) / A0; q.b1 = 2 * (b2 - b0 * K2) / A0; q.b2 = (b0 * K2 - b1 * K + b2) / A0; q.a1 = 2 * (a2 - a0 * K2) / A0; q.a2 = (a0 * K2 - a1 * K + a2) / A0;
    }
    static void setMFB(Bq& q, double fs, double R1, double R2, double R3, double C) {
        const double wt = 2 * VxDsp::PI_ * 3e6, d3 = C * C * R1 * R2 * R3, d2 = C * C * R1 * R2 * R3 * wt + 2 * C * R1 * R2 + C * R1 * R3 + C * R2 * R3, d1 = 2 * C * R1 * R2 * wt + R1 + R2, d0 = (R1 + R2) * wt;
        double p = -d2 / d3; for (int i = 0; i < 50; i++) { const double f = ((d3 * p + d2) * p + d1) * p + d0, df = (3 * d3 * p + 2 * d2) * p + d1, st = f / df; p -= st; if (std::abs(st) < 1e-9 * std::abs(p)) break; }
        const double e1 = d2 + d3 * p, e0 = d1 + e1 * p, num = -C * R2 * R3 * wt / (-p); a2b(q, fs, 0, num, 0, d3, e1, e0);
    }
    static double rail(double v) { const double r = 13.5, a = std::abs(v); if (a < 0.8 * r) return v; return VxDsp::sgn(v) * (0.8 * r + 0.2 * r * std::tanh((a - 0.8 * r) / (0.2 * r))); }
    double envDet(Det& d, double vo, bool hold) {
        const double detC = 0.022e-6, skR1 = 100e3, skR2 = 100e3, skC1 = 0.033e-6, skC2 = 0.022e-6, dIs = 2.52e-9, nVt = 1.752 * 0.025852, gDis = 1 / (hold ? 390e6 : 470e3);
        double x = d.vc;
        if (vo - d.vc < 0.3) { const double a0 = detC / T; x = (a0 * d.vc + d.v1 / skR1) / (a0 + gDis + 1 / skR1); }
        else for (int it = 0; it < 4; it++) { const double vd = std::min(vo - x, 0.9), ed = std::exp(vd / nVt), id = dIs * (ed - 1), f = detC * (x - d.vc) / T - id + x * gDis + (x - d.v1) / skR1, df = detC / T + dIs * ed / nVt + gDis + 1 / skR1; x -= f / df; }
        d.vc = x; const double a = skC1 / T, b = skC2 / T, g1 = 1 / skR1, g2 = 1 / skR2, m11 = a + g1 + g2, m12 = -a - g2, r1 = a * (d.v1 - d.v2) + g1 * d.vc, m21 = -g2, m22 = b + g2, r2 = b * d.v2, dt = m11 * m22 - m12 * m21;
        d.v1 = (r1 * m22 - m12 * r2) / dt; d.v2 = (m11 * r2 - m21 * r1) / dt; return d.v2;
    }
    double compDet(CDet& d, double vin, bool hold) {
        const double r = std::abs(vin), gC = 0.047e-6 / T, gDis = hold ? 0 : 1 / 4.7e6, gAc = r > d.vh ? 1 / 1e3 : 0; d.vh = (gC * d.vh + gAc * r) / (gC + gAc + gDis);
        if (d.vh - 0.55 > d.v2) d.v2 += (d.vh - 0.55 - d.v2) * std::min(1.0, T / (1e3 * 0.0022e-6));
        const double a = 0.0033e-6 / T, b = 0.0022e-6 / T, g1 = 1e-5, g2 = 1e-5, m11 = a + g1 + g2, m12 = -a - g2, r1 = a * (d.v1 - d.v2) + g1 * d.vh, m21 = -g2, m22 = b + g2, r2 = b * d.v2, dt = m11 * m22 - m12 * m21;
        d.v1 = (r1 * m22 - m12 * r2) / dt; d.v2 = (m11 * r2 - m21 * r1) / dt; return std::max(0.0, d.v2);
    }
    double leveler(double vin, double venv, double& y) {
        const double ZT = 100e3 + 100e3 + 1e8, K = (470 / (270e3 + 470)) / (2 * 0.025852), iabc = std::max(0.0, venv) / 33e3, tg = vin / 98.7e3;
        for (int it = 0; it < 2; it++) { const double th = std::tanh(K * y), f = y / ZT + iabc * th - tg, df = 1 / ZT + iabc * K * (1 - th * th); y -= f / df; y = std::min(20.0, std::max(-20.0, y)); }
        return rail(y);
    }
    void initS() { s.mic = VxDsp::db(p.micGainDb); s.inst = VxDsp::db(p.instGainDb); s.vol = VxDsp::db(p.volumeDb); s.ens = p.ensemble ? 1 : 0; s.harm = p.harmonics; s.cons = p.consonant; s.bal = p.balance; for (int b = 0; b < 10; b++) s.band[b] = p.bands[b]; sInit = true; }
    void step(double voice, double carrier) {
        const VxParams& P = p; const double k = sk; VxSmooth& S = s; const double triPP = 2 * 15 * 2.7 / (5.6 + 2.7) * 0.9;
        S.mic += k * (VxDsp::db(P.micGainDb) - S.mic); S.inst += k * (VxDsp::db(P.instGainDb) - S.inst); S.vol += k * (VxDsp::db(P.volumeDb) - S.vol); S.ens += k * ((P.ensemble ? 1 : 0) - S.ens);
        S.harm += k * (P.harmonics - S.harm); S.cons += k * (P.consonant - S.cons); S.bal += k * (P.balance - S.bal); for (int b = 0; b < 10; b++) S.band[b] += k * (P.bands[b] - S.band[b]);
        if (P.hold) { if (hc < holdN) hc++; else held = true; } else { hc = 0; held = false; }
        const double vmic = rail(voice * 10 * S.mic), vEnv = compDet(micD, vmic, held), vComp = leveler(vmic, vEnv, micY);
        if (!held) expCv = vEnv; else expCv *= holdDecay;
        double vPre = 0; { const double y = peB0 * (vComp - peX) - peA1 * peY; peX = vComp; peY = y; if (!held) vPre = rail(y); }
        double c = carrier * 10 * S.inst;
        if (P.guitarMode) { const double a = std::abs(c); gtr += (a > gtr ? gtrA : gtrR) * (a - gtr); c *= std::min(20.0, std::max(0.0, 3 / std::max(gtr, 1e-4))); const double h = VxDsp::harm(c * 0.1, S.harm) * 10, y = h - dcX + dcR * dcY; dcX = h; dcY = y; c = rail(y); }
        c = rail(c);
        carDet += carA * (std::min(150.0 / 22 * std::max(-c, 0.0), 13.5) - carDet); const double th = 15 * 10e3 / 230e3;
        if (carDet > th * 1.1) gateOpen = true; else if (carDet < th * 0.9) gateOpen = false; gate += (gateOpen ? gA : gR) * ((gateOpen ? 1 : 0) - gate);
        double gSum = 1 / 100e3, iSum = 0;
        for (int b = 0; b < 10; b++) {
            const double a1 = rail(VxDsp::run(aA[b], vPre)), a2 = rail(VxDsp::run(aB[b], a1)), venv = envDet(det[b], a2, held);
            const double duty = std::min(1.0, std::max(0.0, (venv - vLow) / triPP)), s1 = rail(VxDsp::run(sA[b], c)), drive = s1 * duty / (1 + duty) * (synBRinNom / (22e3 + 6.8e3 / (1 + duty))), s2 = rail(VxDsp::run(sB[b], drive));
            const double rBot = 1e3 + S.band[b] * 10e3, rTop = 11e3 - rBot, vw = s2 * rBot / 11e3, rw = rTop * rBot / 11e3, g = 1 / (sumR[b] + rw); gSum += g; iSum += vw * g;
        }
        double h = 0.22702 * vPre; for (auto& q : hpf) h = VxDsp::run(q, h); { const double y = hp1b0 * (h - hp1x) - hp1a1 * hp1y; hp1x = h; hp1y = y; h = y; }
        const double dutyC = held ? 0 : std::min(1.0, std::max(0.0, (carDet - vLow) / triPP));
        { const double hh = hfB0 * (h - hfX) - hfA1 * hfY; hfX = h; hfY = hh; const double G1 = dutyC / (6.8e3 + 33e3), G2 = 1 / 11e3, a = 1 - std::exp(-(G1 + G2) / 0.0022e-6 / fs);
          hfNode += a * ((G1 * hh) / (G1 + G2) - hfNode); const double rBot = 1e3 + S.cons * 10e3, rTop = 11e3 - rBot, g = 1 / (100e3 + rTop * rBot / 11e3); gSum += g; iSum += hfNode * rBot / 11e3 * g; }
        const double vSum = rail(iSum / gSum * (1 + 100e3 / 1.5e3)), vOEnv = compDet(outD, vSum, false), vLev = leveler(vSum, vOEnv, outY);
        const double voc = vLev * std::min(expCv / 10, 1.5) * gate / 10;
        ens.process(S.ens > 1e-4 ? voc : 0); double l = voc + S.ens * (ens.L - voc), r = voc + S.ens * (ens.R - voc);
        const double dry = vmic / 10, vg = std::sin(S.bal * VxDsp::PI_ / 2), dg = std::cos(S.bal * VxDsp::PI_ / 2); l = (l * vg + dry * dg) * S.vol; r = (r * vg + dry * dg) * S.vol; if (!P.stereo) l = r = 0.5 * (l + r);
        oL = VxDsp::soft(l, 0.8); oR = VxDsp::soft(r, 0.8);
    }
    static void firPush(Fir& f, double x) { f.pos = (f.pos + 126) % 127; f.buf[f.pos] = f.buf[f.pos + 127] = x; }
    double firOut(const Fir& f) const { double acc = 0; const int p0 = f.pos; for (size_t i = 0; i < nz.size(); i++) acc += hz[i] * f.buf[p0 + nz[i]]; return acc; }
    void process(double voice, double carrier) {
        if (!sInit) initS();
        if (factor == 1) { step(voice, carrier); L = oL; R = oR; return; }
        Fir& uV = fir[0]; Fir& uC = fir[1]; Fir& dL = fir[2]; Fir& dR = fir[3];
        firPush(uV, 2 * voice); const double v0 = firOut(uV); firPush(uV, 0); const double v1 = firOut(uV);
        firPush(uC, 2 * carrier); const double c0 = firOut(uC); firPush(uC, 0); const double c1 = firOut(uC);
        step(v0, c0); firPush(dL, oL); firPush(dR, oR); step(v1, c1); firPush(dL, oL); firPush(dR, oR);
        L = VxDsp::soft(firOut(dL), 0.9); R = VxDsp::soft(firOut(dR), 0.9);
    }
};

class TalkerPedal {
public:
    struct Svf { double a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0; };
    struct Out3 { double lp, bp, hp; };
    struct Band { Svf an1, an2, sy1, sy2; double envM = 0, envC = 0, g = 0, gStep = 0, freq = 0, lf = 0; };
    static const double* mode(int m) {
        static const double M[6][17] = {
            {20, 100, 7500, 1.0, 1.00, 1.5, 18, 0.8, 2.5, 0.7, 0.5, 0.0, 0.0, 0.0, 0.0, 0, 1.60}, {12, 150, 5000, 1.0, 1.00, 3.0, 35, 0.5, 2.0, 0.6, 0.1, 1.0, 0.0, 0.0, 0.0, 0, 2.80},
            {16, 90, 6000, 1.3, 0.72, 1.0, 12, 0.8, 8.0, 0.8, 0.2, 0.0, 0.7, 0.0, 0.0, 0, 0.60}, {14, 120, 5500, 1.8, 1.00, 4.0, 40, 0.6, 3.0, 0.7, 0.15, 0.0, 0.0, 0.0, 0.5, 0, 2.80},
            {18, 100, 7000, 1.2, 1.55, 1.0, 15, 0.8, 3.0, 0.7, 0.3, 0.0, 0.0, 0.6, 0.0, 0, 2.40}, {14, 120, 5500, 1.8, 1.00, 4.0, 40, 0.5, 3.0, 0.6, 0.0, 0.0, 0.0, 0.0, 0.4, 1, 1.00}};
        return M[std::max(0, std::min(5, m))];
    }
    double fs; double cAtk, cRel, gateC, envCC, fastAtk, fastRel, slowC, fadeStep, wahSmooth, glide; Svf sib1, sib2, wah; double gInc, gInc2, rInc, gPh = 0, gPh2 = 0, rPh = 0;
    int curMode = 0, pending = 0; double fade = 1, carE = 0, gate = 0, fastE = 0, slowE = 0; int refr = 0, cd = 0; std::vector<Band> bands; double envAtk = 0, envRel = 0, wahF = 700;
    int vi = 0; double mouth = 0, form[3], formT[3];
    struct Knob { double mic = .5, inst = .5, out = .7; } knob, sk; double smooth;
    struct Cap { double a, z; } micCap, instCap, outCap; Bq adcM[4], adcI[4], dac[4]; int32_t rng1 = 0x1234567, rng2 = (int32_t)0x89abcdefu; double L = 0;

    double c(double ms) const { return 1 - std::exp(-1 / (fs * ms * 0.001)); }
    explicit TalkerPedal(double fs_) : fs(fs_) {
        cAtk = c(5); cRel = c(250); gateC = c(5); envCC = c(30); fastAtk = c(1); fastRel = c(20); slowC = c(40); fadeStep = 1 / (0.008 * fs);
        wahSmooth = 1 - std::exp(-16 / (0.03 * fs)); glide = 1 - std::exp(-16 / (0.12 * fs)); svfSet(sib1, 4500, 0.707, fs); svfSet(sib2, 4500, 0.707, fs);
        gInc = 2 * VxDsp::PI_ * 31 / fs; gInc2 = 2 * VxDsp::PI_ * 7.3 / fs; rInc = 2 * VxDsp::PI_ * 95 / fs;
        apply(0); smooth = 1 - std::exp(-1 / (0.03 * fs));
        auto cc = [this](double R, double C) { return Cap{std::exp(-1 / (R * C * fs)), 0}; }; micCap = cc(20e3, 0.82e-6); instCap = cc(500e3, 33e-9); outCap = cc(10e3, 10e-6);
        const double corner = std::atan(std::tan(VxDsp::PI_ * 14.5e3 / fs) / 0.9464) * fs / VxDsp::PI_;
        for (int k = 0; k < 4; k++) { const double Q = 1 / (2 * std::cos((2 * k + 1) * VxDsp::PI_ / 16)); VxDsp::lp(adcM[k], fs, corner, Q); VxDsp::lp(adcI[k], fs, corner, Q); VxDsp::lp(dac[k], fs, corner, Q); }
    }
    static void svfSet(Svf& s, double f, double q, double fs) { const double g = std::tan(VxDsp::PI_ * std::min(std::max(f, 10.0), 0.45 * fs) / fs), k = 1 / q, b1 = 1 / (1 + g * (g + k)); s.k = k; s.a1 = b1; s.a2 = g * b1; s.a3 = g * g * b1; }
    static Out3 svfTick(Svf& s, double v0) { const double v3 = v0 - s.ic2, v1 = s.a1 * s.ic1 + s.a2 * v3, v2 = s.ic2 + s.a2 * s.ic1 + s.a3 * v3; s.ic1 = 2 * v1 - s.ic1; s.ic2 = 2 * v2 - s.ic2; return {v2, s.k * v1, v0 - s.k * v1 - v2}; }
    void apply(int m) {
        curMode = m; const double* S = mode(m); const int n = (int)S[0]; const double r = std::pow(S[2] / S[1], 1.0 / (n - 1)), q = S[3] / (std::sqrt(r) - 1 / std::sqrt(r)); bands.clear();
        for (int k = 0; k < n; k++) { const double f = S[1] * std::pow(r, k); Band b; b.freq = f; b.lf = std::log2(f); svfSet(b.an1, f, q, fs); svfSet(b.an2, f, q, fs); svfSet(b.sy1, f * S[4], q, fs); svfSet(b.sy2, f * S[4], q, fs); bands.push_back(b); }
        envAtk = c(S[5]); envRel = c(S[6]); wah = Svf(); wahF = 700; svfSet(wah, 700, 6, fs);
        vi = 0; mouth = 0; form[0] = std::log2(250.0); form[1] = std::log2(700.0); form[2] = std::log2(2200.0); formT[0] = std::log2(730.0); formT[1] = std::log2(1090.0); formT[2] = std::log2(2440.0); cd = 0;
    }
    void setMode(double m) { pending = std::max(0, std::min(5, (int)m)); }
    double engine(double mic, double inst) {
        if (pending != curMode) { fade -= fadeStep; if (fade <= 0) { fade = 0; apply(pending); } } else if (fade < 1) fade = std::min(1.0, fade + fadeStep);
        const double* S = mode(curMode); const double a = std::abs(inst); carE += (a > carE ? cAtk : cRel) * (a - carE);
        const double agc = std::min(16.0, 0.3 / (carE + 1e-6)); double car = inst * std::pow(agc, S[9]); car = std::tanh(S[8] * car) * 0.5;
        const double gt = std::min(1.0, std::max(0.0, (carE - 0.0005) / 0.0015)); gate += gateC * (gt - gate);
        if (S[15] != 0) {
            fastE += (a > fastE ? fastAtk : fastRel) * (a - fastE); slowE += slowC * (a - slowE);
            if (refr > 0) refr--;
            else if (fastE > 1.8 * slowE + 0.002) {
                static const double V[6][3] = {{730, 1090, 2440}, {570, 840, 2410}, {530, 1840, 2480}, {730, 1090, 2440}, {270, 2290, 3010}, {640, 1190, 2390}};
                vi = (vi + 1) % 6; form[0] = std::log2(250.0); form[1] = std::log2(700.0); form[2] = std::log2(2200.0); for (int j = 0; j < 3; j++) formT[j] = std::log2(V[vi][j]); mouth = 0.3; refr = (int)std::lround(0.12 * fs);
            }
        }
        if (--cd <= 0) { cd = 16; control(S); }
        double sum = 0; const int n = (int)bands.size();
        for (int k = 0; k < n; k++) {
            Band& b = bands[k];
            if (S[15] == 0) { const double m = std::abs(svfTick(b.an2, svfTick(b.an1, mic).bp).bp); b.envM += (m > b.envM ? envAtk : envRel) * (m - b.envM); }
            const double cc = svfTick(b.sy2, svfTick(b.sy1, car).bp).bp; b.envC += envCC * (std::abs(cc) - b.envC); b.g += b.gStep; sum += cc * b.g;
        }
        double y = sum * 9 * std::sqrt((double)n);
        if (S[11] > 0) y = y * 0.6 + svfTick(wah, car).bp * S[11] * 0.8;
        if (S[12] > 0) { gPh = std::fmod(gPh + gInc, 2 * VxDsp::PI_); gPh2 = std::fmod(gPh2 + gInc2, 2 * VxDsp::PI_); const double d = S[12] * (0.6 + 0.4 * std::sin(gPh2)); y *= 1 - d * 0.5 * (1 + std::sin(gPh)); y *= 1.5; }
        if (S[13] > 0) { rPh = std::fmod(rPh + rInc, 2 * VxDsp::PI_); y = y * (1 - S[13]) + y * std::sin(rPh) * S[13] * 1.4; }
        if (S[10] > 0) y += svfTick(sib2, svfTick(sib1, mic).hp).hp * S[10] * 1.5;
        y *= gate * fade * S[16]; const double ay = std::abs(y); return ay <= 0.8 ? y : VxDsp::sgn(y) * (0.8 + 0.2 * std::tanh((ay - 0.8) / 0.2));
    }
    void control(const double* S) {
        const int n = (int)bands.size(); std::vector<double> e(n);
        if (S[15] != 0) {
            for (int j = 0; j < 3; j++) form[j] += glide * (formT[j] - form[j]); mouth += glide * (1 - mouth);
            static const double am[3] = {1, 0.7, 0.4};
            for (int k = 0; k < n; k++) { double g = 0; const double f = bands[k].freq; for (int j = 0; j < 3; j++) { const double F = std::pow(2.0, form[j]), d = (f - F) / (0.2 * F); g += am[j] / (1 + d * d); } e[k] = 0.14 * mouth * g; }
        } else for (int k = 0; k < n; k++) e[k] = bands[k].envM;
        if (S[14] > 0) { std::vector<double> sm = e; for (int k = 0; k < n; k++) { const double l = e[std::max(0, k - 1)], r = e[std::min(n - 1, k + 1)]; sm[k] = (1 - S[14]) * e[k] + S[14] * 0.5 * (l + r); } e = sm; }
        double meanC = 0; for (auto& b : bands) meanC += b.envC; meanC /= n;
        for (int k = 0; k < n; k++) { Band& b = bands[k]; const double w = std::min(8.0, std::max(0.25, std::pow((meanC + 1e-6) / (b.envC + 1e-6), S[7]))); b.gStep = (e[k] * w - b.g) / 16; }
        if (S[11] > 0) { double num = 0, den = 0; for (auto& b : bands) { num += b.envM * b.lf; den += b.envM; } if (den > 1e-4) { const double t = std::min(2400.0, std::max(300.0, std::pow(2.0, num / den) * 0.7)); wahF += wahSmooth * (t - wahF); svfSet(wah, wahF, 6, fs); } }
    }
    static double xs(int32_t& r) { r ^= (int32_t)((uint32_t)r << 13); r ^= (int32_t)((uint32_t)r >> 17); r ^= (int32_t)((uint32_t)r << 5); return (double)(uint32_t)r / 4294967295.0 * 2 - 1; }
    void process(double voice, double inst) {
        const double s = smooth; sk.mic += s * (knob.mic - sk.mic); sk.inst += s * (knob.inst - sk.inst); sk.out += s * (knob.out - sk.out);
        auto pre = [](Cap& cap, double v, double knobv, double lo, double hi) { cap.z = v + cap.a * (cap.z - v); const double x = v - cap.z, gMin = 2.83 / (std::pow(10.0, lo / 20) * std::sqrt(2.0)), g = gMin * std::pow(10.0, (lo - hi) * std::min(1.0, std::max(0.0, knobv)) / 20), y = g * x, a = std::abs(y); return a <= 5.85 ? y : VxDsp::sgn(y) * (5.85 + 0.65 * std::tanh((a - 5.85) / 0.65)); };
        auto adc = [](Bq* f, double v, int32_t& seed) { double x = std::min(1.0, std::max(-1.0, v / 2.83)); x += xs(seed) * 3e-5; for (int k = 0; k < 4; k++) x = VxDsp::run(f[k], x); return x; };
        const double m = adc(adcM, pre(micCap, voice * 0.5, sk.mic, 15, -34), rng1), i = adc(adcI, pre(instCap, inst, sk.inst, 15, -14), rng2);
        double y = std::min(1.0, std::max(-1.0, engine(m, i))); for (int k = 0; k < 4; k++) y = VxDsp::run(dac[k], y);
        const double taper = (std::pow(10.0, 2 * std::min(1.0, std::max(0.0, sk.out))) - 1) / 99, bufG = std::pow(10.0, 11.3 / 20) * std::sqrt(2.0) / 2.83; double v = y * 2.83 * taper * bufG; const double av = std::abs(v);
        if (av > 5.85) v = VxDsp::sgn(v) * (5.85 + 0.65 * std::tanh((av - 5.85) / 0.65));
        outCap.z = v + outCap.a * (outCap.z - v); L = v - outCap.z;
    }
};

} // namespace farp
