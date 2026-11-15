// Omega DMX effects and filters: the Omega 110's (plugins/dr110/DR110.h on the Omega 110 / Omega Traks branches),
// copied unchanged so the three machines sound alike: the Simper SVF, the RBJ biquad, the TC 2290-style dynamic delay
// and the REV7-style reverb. dmx/fx.js is the same code in JavaScript (from traks/engine.js).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace dmx::fx {

constexpr double PI = 3.14159265358979323846;
struct SVF {
    double ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1, bp = 0, lp = 0, hp = 0;
    void set(double f, double q, double sr) { const double g = std::tan(PI * std::min(f, sr * 0.45) / sr); k = 1 / q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    double tick(double x) {
        const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; bp = v1; lp = v2; hp = x - k * v1 - v2; return v1;
    }
};
inline double onePole(double f, double sr) { return 1 - std::exp(-2 * PI * f / sr); }
inline double decayCoef(double tau, double sr) { return std::exp(-1 / (tau * sr)); }
inline double jsRound(double x) { return std::floor(x + 0.5); }

// RBJ biquad, transposed direct form II: 0 low shelf, 1 peak, 2 high shelf; off at ~0 dB (see engine.js)
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; bool on = false;
    void set(int type, double f, double gainDb, double q, double sr) {
        on = std::abs(gainDb) >= 0.01;
        if (!on) { z1 = 0; z2 = 0; return; }
        const double A = std::pow(10.0, gainDb / 40), w = 2 * PI * std::min(f, sr * 0.45) / sr, cw = std::cos(w), sw = std::sin(w);
        double B0, B1, B2, A0, A1, A2;
        if (type == 1) { const double al = sw / (2 * q); B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; }
        else {
            const double al = sw / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
            if (type == 0) { B0 = A * ((A + 1) - (A - 1) * cw + sa); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sa);
                A0 = (A + 1) + (A - 1) * cw + sa; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sa; }
            else { B0 = A * ((A + 1) + (A - 1) * cw + sa); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sa);
                A0 = (A + 1) - (A - 1) * cw + sa; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - sa; }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    double tick(double x) { if (!on) return x; const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// TC 2290-style dynamic delay (see engine.js)
class TCDelay {
public:
    explicit TCDelay(double sampleRate) : sr(sampleRate) {
        n = (int) std::ceil(sr * 3) + 8; bL.assign((size_t) n, 0.0); bR.assign((size_t) n, 0.0);
        glide = onePole(3, sr); envA = onePole(1 / (2 * PI * 0.005), sr); envR = onePole(1 / (2 * PI * 0.25), sr);
        setParams(375, 0.35, 6000, 100, 0.6, 0.2, 0.4, true);
    }
    void setParams(double ms, double fbk, double hiCut, double loCut, double rate, double dep, double dk, bool pp) {
        target = std::max(4.0, std::min(n - 8 - sr * 0.004, ms * sr / 1000)); if (cur < 0) cur = target;
        fb = fbk; lpA = onePole(hiCut, sr); hpA = onePole(loCut, sr); inc = rate / sr; depth = dep * 0.003 * sr; duck = dk; ping = pp;
    }
    double read(const std::vector<double>& b, double d) const {
        double pos = w - d; while (pos < 0) pos += n;
        const int i = (int) std::floor(pos); const double fr = pos - i; const int j = i + 1 >= n ? 0 : i + 1;
        return b[(size_t) i] + (b[(size_t) j] - b[(size_t) i]) * fr;
    }
    void tick(double x, double& outL, double& outR) {
        const double ax = std::abs(x); env += (ax > env ? envA : envR) * (ax - env);
        cur += (target - cur) * glide;
        ph += inc; if (ph >= 1) ph -= 1;
        const double m = std::sin(2 * PI * ph) * depth;
        const double yl = read(bL, cur + depth + m), yr = read(bR, cur + depth - m);
        lpL += lpA * (yl - lpL); hpL += hpA * (lpL - hpL);
        lpR += lpA * (yr - lpR); hpR += hpA * (lpR - hpR);
        const double fl = (lpL - hpL) * fb, frr = (lpR - hpR) * fb;
        if (ping) { bL[(size_t) w] = x + frr; bR[(size_t) w] = fl; } else { bL[(size_t) w] = x + fl; bR[(size_t) w] = x + frr; }
        w = w + 1 >= n ? 0 : w + 1;
        const double g = 1 / (1 + duck * env * 12);
        outL = yl * g; outR = yr * g;
    }
private:
    double sr; int n = 0, w = 0; std::vector<double> bL, bR;
    double cur = -1, target = 0, glide = 0, ph = 0, env = 0, envA = 0, envR = 0, lpL = 0, lpR = 0, hpL = 0, hpR = 0;
    double fb = 0, lpA = 0, hpA = 0, inc = 0, depth = 0, duck = 0; bool ping = true;
};

// ---- REV7-style reverb (see engine.js)
constexpr double RV_LINES[8] = {31.7, 37.1, 41.3, 43.9, 53.3, 59.9, 67.7, 73.1};
constexpr double RV_SIZE[5] = {1.0, 0.42, 0.7, 0.55, 0.3};
constexpr double RV_TAPS[5][8][2] = {
    {{7.1, .84}, {11.3, -.71}, {17.9, .62}, {23.3, -.55}, {31.7, .47}, {41.9, -.41}, {53.1, .33}, {67.3, -.27}},
    {{3.1, .9}, {5.3, -.8}, {7.9, .7}, {11.1, -.62}, {13.9, .55}, {17.3, -.47}, {21.1, .4}, {26.9, -.33}},
    {{5.3, .8}, {9.1, -.7}, {13.7, .62}, {19.1, -.55}, {25.3, .47}, {31.1, -.41}, {39.7, .35}, {47.3, -.3}},
    {{1.3, .7}, {2.9, -.66}, {4.1, .6}, {5.9, -.55}, {7.3, .5}, {9.7, -.45}, {11.9, .41}, {14.3, -.37}},
    {{9.7, .95}, {16.3, -.88}, {24.1, .8}, {33.7, -.72}, {44.9, .64}, {57.1, -.56}, {71.3, .48}, {89.9, -.4}},
};
constexpr double RV_AP[4] = {4.771, 3.595, 12.73, 9.307};

class Rev7 {
public:
    explicit Rev7(double sampleRate) : sr(sampleRate) {
        pre.assign((size_t) std::ceil(sr * 0.4) + 2, 0.0);
        const size_t maxLen = (size_t) std::ceil(sr * 0.08) + 2;
        for (int i = 0; i < 8; ++i) { lines[i].assign(maxLen, 0.0); lw[i] = 0; len[i] = 1; g[i] = 0; damp[i] = 0; lz[i] = 0; }
        for (int i = 0; i < 4; ++i) { const int n = std::max(1, (int) std::floor(RV_AP[i] * sr / 1000)); ap[i].assign((size_t) n, 0.0); apw[i] = 0; apLen[i] = n; }
        bwA = onePole(12000, sr);
    }
    void setParams(int prog, double rt, double high, double delayMs, double diff, double erLevel, double cut) {
        prog = std::max(0, std::min(4, prog));
        preD = std::max(1, (int) std::floor(delayMs * sr / 1000));
        const double size = RV_SIZE[prog];
        for (int i = 0; i < 8; ++i) {
            const int n = std::max(2, (int) std::floor(RV_LINES[i] * size * sr / 1000));
            if (n != len[i]) { len[i] = n; std::fill(lines[i].begin(), lines[i].end(), 0.0); lw[i] = 0; lz[i] = 0; }
            const double sec = n / sr, gg = std::pow(10.0, -3 * sec / rt);
            g[i] = prog == 4 ? gg * 0.35 : gg;
            const double b = std::log(10.0) / 4 * std::log10(gg) * (1 - 1 / (high * high));
            damp[i] = std::max(0.0, std::min(0.95, b));
        }
        apG = prog == 3 ? 0.75 * diff / 10 : 0.68 * diff / 10;
        for (int k = 0; k < 8; ++k) {
            const double ms = RV_TAPS[prog][k][0], gn = RV_TAPS[prog][k][1];
            tapL[k] = (int) std::floor(ms * sr / 1000); tapLg[k] = gn;
            tapR[k] = (int) std::floor(ms * 1.137 * sr / 1000); tapRg[k] = -gn * 0.95;
        }
        er = prog == 4 ? 1 : erLevel; late = prog == 4 ? 0.25 : 1;
        cutA = onePole(cut, sr);
    }
    double readPre(int d) const { int i = preW - d; while (i < 0) i += (int) pre.size(); return pre[(size_t) i]; }
    void tick(double x, double& outL, double& outR) {
        pre[(size_t) preW] = x;
        bw += bwA * (readPre(preD) - bw);
        double d = bw;
        for (int i = 0; i < 4; ++i) {
            auto& buf = ap[i]; const int w = apw[i]; const double z = buf[(size_t) w], v = d + apG * z;
            buf[(size_t) w] = v; d = z - apG * v; apw[i] = w + 1 >= apLen[i] ? 0 : w + 1;
        }
        double eL = 0, eR = 0;
        for (int k = 0; k < 8; ++k) { eL += readPre(preD + tapL[k]) * tapLg[k]; eR += readPre(preD + tapR[k]) * tapRg[k]; }
        preW = preW + 1 >= (int) pre.size() ? 0 : preW + 1;
        double o[8];
        for (int i = 0; i < 8; ++i) {
            const double y = lines[i][(size_t) lw[i]];
            lz[i] = y * (1 - damp[i]) + lz[i] * damp[i];
            o[i] = lz[i] * g[i];
        }
        for (int h = 1; h < 8; h *= 2) for (int i = 0; i < 8; i += h * 2) for (int j = i; j < i + h; ++j) { const double a = o[j], b = o[j + h]; o[j] = a + b; o[j + h] = a - b; }
        const double sc = 1 / std::sqrt(8.0);
        double lL = 0, lR = 0;
        for (int i = 0; i < 8; ++i) {
            const double y = lz[i];
            if (i & 1) lR += y * ((i & 2) ? -1 : 1); else lL += y * ((i & 2) ? -1 : 1);
            lines[i][(size_t) lw[i]] = o[i] * sc + d * ((i & 4) ? -0.5 : 0.5);
            lw[i] = lw[i] + 1 >= len[i] ? 0 : lw[i] + 1;
        }
        const double l = eL * er * 0.5 + lL * late * 0.5, r = eR * er * 0.5 + lR * late * 0.5;
        cutL += cutA * (l - cutL); cutR += cutA * (r - cutR);
        outL = cutL; outR = cutR;
    }
private:
    double sr;
    std::vector<double> pre; int preW = 0, preD = 1;
    std::vector<double> lines[8]; int lw[8], len[8]; double g[8], damp[8], lz[8];
    std::vector<double> ap[4]; int apw[4], apLen[4];
    double bwA = 0, bw = 0, cutA = 0, cutL = 0, cutR = 0, apG = 0.6, er = 0.35, late = 1;
    int tapL[8] = {}, tapR[8] = {}; double tapLg[8] = {}, tapRg[8] = {};
};

// a recording to play instead of the circuit (Source: recordings)

}  // namespace dmx::fx
