// Omega DR-110: line-for-line C++ port of dr110/engine.js (the circuit model of the DR-110 voices, the mixer and the
// REV7-style reverb). No JUCE in here, so tools can build it on its own. dr110/tools/compare.mjs renders the same
// hits with both and compares them sample by sample; keep the two files in step.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace dr110 {

constexpr int NV = 7;                       // BD SD OH CH CY HC KN
constexpr int NG = 13;                      // global parameters; voice v field f is NG + v * 6 + f
constexpr int NMOD = 15;                    // the voice-mod panel and SUPPLY, after the voices
constexpr int NX = 26;                      // Omega 110: bass extender, master EQ, overdrive, delay
constexpr int NG2 = NG + NV * 6 + NMOD + NX;  // then 13 per pad
constexpr int NP = NG2 + NV * 13;
inline const char* const VOICES[NV] = {"BD", "SD", "OH", "CH", "CY", "HC", "KN"};
inline const char* const VOICE_NAMES[NV] = {"Bass Drum", "Snare Drum", "Open Hihat", "Closed Hihat", "Cymbal", "Hand Clap", "Knuckle"};
inline const char* const RV_PROGS[5] = {"HALL", "ROOM", "VOCAL", "PLATE", "EARLY REF"};
constexpr int GROUP[NV] = {0, 0, 1, 1, 1, 0, 0};
constexpr int OUTS[NV] = {0, 1, 3, 3, 3, 2, 2};   // separate outputs: BD, SD, CP (clap, knuckle), HH/CY
constexpr double VGAIN[NV] = {2.07, 0.2, 0.81, 1.05, 1.15, 0.26, 0.053};
constexpr double METAL[6] = {163.25, 230.5, 263.0, 299.25, 371.5, 378.0};
constexpr double BD_PULSE = 0.006, BD_SWEEP = 2.5, BD_PTAU = 0.004, BD_TAU = 0.024;

namespace pid { enum { tempo, balance, accent, volume, src, rvProg, rvTime, rvHigh, rvDelay, rvDiff, rvEr, rvCut, rvLevel }; }
namespace mod { enum { bdSnap = NG + NV * 6, bdBoost, bdBass, sdSnap, sdBoost, sdMix, hhFilter, hhMix, cyFilter, hcFilter, hcBoost, swTarget, swDecay, swAmount, supply }; }
namespace ox { enum { bdExtend = NG + NV * 6 + NMOD, bdEnv, mLsF, mLsG, mP1F, mP1G, mP1Q, mP2F, mP2G, mP2Q, mHsF, mHsG,
    odDrive, odMode, odMix, odOut, dlTime, dlSync, dlFb, dlHiCut, dlLoCut, dlModRate, dlModDepth, dlDuck, dlPing, dlLevel }; }
namespace qf { enum { lsF, lsG, pkF, pkG, pkQ, hsF, hsG, atk, dcy, sus, rel, drv, dls }; }
inline int vq(int v, int f) { return NG2 + v * 13 + f; }
constexpr double DL_DIVS[14] = {0, 0.125, 1.0 / 6, 0.25, 1.0 / 3, 0.375, 0.5, 2.0 / 3, 0.75, 1, 4.0 / 3, 1.5, 2, 4};
inline const char* const DL_DIV_NAMES[14] = {"OFF", "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/1"};
inline const char* const OD_MODES[3] = {"OVERDRIVE", "HARD CLIP", "TUBE"};
namespace vf { enum { lv, pan, tune, dec, mute, snd }; }
inline int vp(int v, int f) { return NG + v * 6 + f; }

struct ParamInfo { std::string id, name; double min, max, def; };
inline std::vector<ParamInfo> params() {
    std::vector<ParamInfo> p = {
        {"tempo", "Tempo", 40, 250, 120}, {"balance", "Balance", -5, 5, 0}, {"accent", "Accent", 0, 10, 5}, {"volume", "Volume", 0, 10, 7},
        {"src", "Source", 0, 1, 0}, {"rvProg", "Reverb Program", 0, 4, 0}, {"rvTime", "Reverb Time", 0.3, 10, 2.6}, {"rvHigh", "Reverb High", 0.1, 1, 0.6},
        {"rvDelay", "Reverb Delay", 0, 200, 22}, {"rvDiff", "Reverb Diffusion", 0, 10, 8}, {"rvEr", "Reverb ER Level", 0, 1, 0.35},
        {"rvCut", "Reverb High Cut", 1000, 12000, 9000}, {"rvLevel", "Reverb Return", 0, 1, 0.5}};
    for (int v = 0; v < NV; ++v) {
        const std::string id = VOICES[v], nm = VOICE_NAMES[v];
        p.push_back({id + "lv", nm + " Level", 0, 1.5, 1});
        p.push_back({id + "pan", nm + " Pan", -1, 1, 0});
        p.push_back({id + "tune", nm + " Tune", -12, 12, 0});
        p.push_back({id + "dec", nm + " Decay", 0.25, 4, 1});
        p.push_back({id + "mute", nm + " Mute", 0, 1, 0});
        p.push_back({id + "snd", nm + " Reverb Send", 0, 1, v == 1 || v == 5 ? 0.25 : 0.1});
    }
    const std::vector<ParamInfo> m = {
        {"bdSnap", "Kick Snap", 0, 1, 0.5}, {"bdBoost", "Kick Decay Boost", 0, 1, 0}, {"bdBass", "Kick Bass Boost", 0, 1, 0},
        {"sdSnap", "Snare Snap", 0, 1, 0.5}, {"sdBoost", "Snare Decay Boost", 0, 1, 0}, {"sdMix", "Snare Mix", 0, 1, 0.5},
        {"hhFilter", "HiHat Filter", 0, 1, 0.5}, {"hhMix", "HiHat Noise Mix", 0, 1, 0}, {"cyFilter", "Cymbal Filter", 0, 1, 0.5},
        {"hcFilter", "Clap Filter", 0, 1, 0.5}, {"hcBoost", "Clap Decay Boost", 0, 1, 0},
        {"swTarget", "Trig Sweep Target", 0, 1, 0}, {"swDecay", "Trig Sweep Decay", 0, 1, 0.3}, {"swAmount", "Trig Sweep Amount", 0, 1, 0},
        {"supply", "Supply", 0, 1, 0}};
    p.insert(p.end(), m.begin(), m.end());
    const std::vector<ParamInfo> x = {
        {"bdExtend", "Bass Extender", 0, 1, 0}, {"bdEnv", "Kick Envelope Shape", 0, 1, 0},
        {"mLsF", "Master Low Freq", 20, 500, 80}, {"mLsG", "Master Low Gain", -15, 15, 0},
        {"mP1F", "Master Mid 1 Freq", 40, 8000, 250}, {"mP1G", "Master Mid 1 Gain", -15, 15, 0}, {"mP1Q", "Master Mid 1 Q", 0.3, 8, 1},
        {"mP2F", "Master Mid 2 Freq", 200, 16000, 2500}, {"mP2G", "Master Mid 2 Gain", -15, 15, 0}, {"mP2Q", "Master Mid 2 Q", 0.3, 8, 1},
        {"mHsF", "Master High Freq", 1000, 18000, 8000}, {"mHsG", "Master High Gain", -15, 15, 0},
        {"odDrive", "Overdrive", 0, 1, 0}, {"odMode", "Overdrive Mode", 0, 2, 0}, {"odMix", "Overdrive Mix", 0, 1, 1}, {"odOut", "Overdrive Output", -12, 12, 0},
        {"dlTime", "Delay Time", 10, 2000, 375}, {"dlSync", "Delay Sync", 0, 13, 0}, {"dlFb", "Delay Feedback", 0, 0.95, 0.35},
        {"dlHiCut", "Delay High Cut", 1000, 20000, 6000}, {"dlLoCut", "Delay Low Cut", 20, 1000, 100},
        {"dlModRate", "Delay Mod Rate", 0.05, 5, 0.6}, {"dlModDepth", "Delay Mod Depth", 0, 1, 0.2}, {"dlDuck", "Delay Ducking", 0, 1, 0.4},
        {"dlPing", "Delay Ping-Pong", 0, 1, 1}, {"dlLevel", "Delay Return", 0, 1, 0.4}};
    p.insert(p.end(), x.begin(), x.end());
    for (int v = 0; v < NV; ++v) {
        const std::string id = VOICES[v], n = std::string(VOICE_NAMES[v]) + " ";
        const std::vector<ParamInfo> q = {
            {id + "lsF", n + "Low Freq", 20, 1000, 100}, {id + "lsG", n + "Low Gain", -15, 15, 0},
            {id + "pkF", n + "Mid Freq", 40, 16000, 1000}, {id + "pkG", n + "Mid Gain", -15, 15, 0}, {id + "pkQ", n + "Mid Q", 0.3, 10, 1},
            {id + "hsF", n + "High Freq", 1000, 18000, 6000}, {id + "hsG", n + "High Gain", -15, 15, 0},
            {id + "atk", n + "Attack", 0, 1, 0}, {id + "dcy", n + "Env Decay", 0.005, 4, 0.3}, {id + "sus", n + "Sustain", 0, 1, 1}, {id + "rel", n + "Release", 0.005, 4, 4},
            {id + "drv", n + "Drive", 0, 1, 0}, {id + "dls", n + "Delay Send", 0, 1, 0}};
        p.insert(p.end(), q.begin(), q.end());
    }
    return p;
}
using Params = std::array<double, NP>;
inline Params defaults() { Params a{}; auto p = params(); for (int i = 0; i < NP; ++i) a[(size_t) i] = p[(size_t) i].def; return a; }

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
inline double blep(double t, double dt) {
    if (t < dt) { const double x = t / dt; return x + x - x * x - 1; }
    if (t > 1 - dt) { const double x = (t - 1) / dt; return x * x + x + x + 1; }
    return 0;
}
struct Square {
    double ph = 0, inc = 0;
    double tick() {
        const double t = ph, dt = inc; double y = t < 0.5 ? 1 : -1;
        y += blep(t, dt); double t2 = t + 0.5; if (t2 >= 1) t2 -= 1; y -= blep(t2, dt);
        ph += dt; if (ph >= 1) ph -= 1; return y;
    }
};
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
struct Sample { std::vector<float> d, acc; double sr = 44100; };

class Engine {
public:
    explicit Engine(double sampleRate) : sr(sampleRate), rv(sampleRate), dl(sampleRate) {
        p = defaults();
        for (int m = 0; m < 3; ++m) for (int i = 0; i < 6; ++i) osc[m][i].ph = std::fmod(i * 0.137 + m * 0.311, 1.0);
        update();
    }
    Params p;
    double noise() { uint32_t x = seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; seed = x; return x / 4294967296.0 * 2 - 1; }
    void set(int i, double v) { p[(size_t) i] = v; update(); }
    void setAll(const Params& a) { p = a; update(); }
    void setSample(int v, const Sample* s) { samples[v] = s; }

    void update() {
        for (int v = 0; v < NV; ++v) { tn[v] = std::pow(2.0, p[(size_t) vp(v, vf::tune)] / 12); dk[v] = p[(size_t) vp(v, vf::dec)]; }
        const double b = p[pid::balance] / 5;
        for (int v = 0; v < NV; ++v) {
            const double bal = GROUP[v] == 0 ? (b > 0 ? 1 - b : 1) : (b < 0 ? 1 + b : 1);
            const bool muted = p[(size_t) vp(v, vf::mute)] >= 0.5;
            const double gg = muted ? 0 : p[(size_t) vp(v, vf::lv)] * bal;
            const double a = (p[(size_t) vp(v, vf::pan)] + 1) * PI / 4;
            gL[v] = gg * std::cos(a) * std::sqrt(2.0); gR[v] = gg * std::sin(a) * std::sqrt(2.0);
            send[v] = muted ? 0 : p[(size_t) vp(v, vf::snd)] * p[(size_t) vp(v, vf::lv)] * bal;
        }
        accGain = 1 + p[pid::accent] / 10;
        head = p[mod::supply] >= 0.5 ? 1.08 : 1.2;
        auto on_ = [&](int i) { return p[(size_t) i] >= 0.5; };
        bdPulseK = 1.5 - p[mod::bdSnap]; bdClick = std::max(0.0, p[mod::bdSnap] - 0.5) * 2;
        bdDk = dk[0] * (on_(mod::bdBoost) ? 2.5 : 1); bdBassK = on_(mod::bdBass) ? 1.6 : 1;
        sdMixT = std::min(1.0, 2 * (1 - p[mod::sdMix])); sdMixN = std::min(1.0, 2 * p[mod::sdMix]);
        sdDk = dk[1] * (on_(mod::sdBoost) ? 2.5 : 1); sdQk = on_(mod::sdBoost) ? 1.6 : 1;
        hhF = std::pow(2.0, (p[mod::hhFilter] - 0.5) * 2); hhMix = p[mod::hhMix];
        cyF = std::pow(2.0, (p[mod::cyFilter] - 0.5) * 2);
        hcQ = 1.2 * std::pow(4.0, p[mod::hcFilter] - 0.5); hcDk = dk[5] * (on_(mod::hcBoost) ? 2.5 : 1);
        swBd = !on_(mod::swTarget); swD = decayCoef(0.02 + p[mod::swDecay] * 0.5, sr); swAmt = p[mod::swAmount];
        const double vol = p[pid::volume] / 10; master = vol * vol * 2;
        sdTone.set(240 * tn[1], PI * 240 * tn[1] * 0.009 * dk[1] * sdQk, sr);
        sdHpA = onePole(1200 * tn[1] * (0.25 + 1.5 * p[mod::sdSnap]), sr); sdLpA = onePole(11000, sr);
        for (int m = 0; m < 3; ++m) {
            const double tm = tn[2 + m];
            for (int i = 0; i < 6; ++i) osc[m][i].inc = METAL[i] * tm / sr;
            const double fc = 7800 * std::sqrt(tm) * (m == 2 ? cyF : hhF);
            mHi[m].set(fc, 1.2, sr); mHi2[m].set(fc, 1.2, sr);
        }
        mLo.set(3342 * std::sqrt(tn[4]) * cyF, 3, sr);
        mHpA = onePole(5000, sr);
        chTau = 0.03 * dk[3]; chD = decayCoef(chTau, sr); ohD = decayCoef(0.36 * dk[2], sr); ohGate = 0.62 * dk[2];
        ohRel = decayCoef(0.05, sr); chokeD = decayCoef(0.003, sr);
        cyD = decayCoef(0.03 * dk[4], sr); cyLoD = decayCoef(0.35 * dk[4], sr);
        hcBp.set(1000 * tn[5], hcQ, sr); hcHpA = onePole(2500 * tn[5], sr);
        hcBurstD = decayCoef(0.005, sr); hcTailD = decayCoef(0.06 * hcDk, sr); hcLongD = decayCoef(0.12 * hcDk, sr);
        kn1.set(820 * tn[6], PI * 820 * tn[6] * 0.018 * dk[6], sr);
        kn2.set(1650 * tn[6], PI * 1650 * tn[6] * 0.011 * dk[6], sr);
        knClick.set(3000 * tn[6], 0.9, sr); knClickD = decayCoef(0.0015, sr);
        sdNoiseD = decayCoef(0.025 * sdDk, sr);
        bdPitchD = decayCoef(BD_PTAU * (p[ox::bdEnv] >= 0.5 ? 2.5 : 1), sr);
        const double tn0 = p[(size_t) vp(0, vf::tune)] / 12, ext = p[ox::bdExtend];
        bdExt = (1 + 17 * ext * ext) * (1 - 0.5 * tn0 * tn0); bdExtIn = std::sqrt(bdExt);
        bdQ = PI * 50 * BD_TAU * bdDk * (bdBassK > 1 ? 1.15 : 1) * bdExt;
        bdSweepK = p[ox::bdEnv] >= 0.5 ? 0.6 : 1;
        bdOut.set(700, 0.707, sr);
        dcA = onePole(18, sr);
        rv.setParams((int) jsRound(p[pid::rvProg]), p[pid::rvTime], p[pid::rvHigh], p[pid::rvDelay], p[pid::rvDiff], p[pid::rvEr], p[pid::rvCut]);
        rvLevel = p[pid::rvLevel];
        for (int v = 0; v < NV; ++v) {
            auto P_ = [&](int f) { return p[(size_t) vq(v, f)]; };
            eq[v][0].set(0, P_(qf::lsF), P_(qf::lsG), 0.707, sr); eq[v][1].set(1, P_(qf::pkF), P_(qf::pkG), P_(qf::pkQ), sr); eq[v][2].set(2, P_(qf::hsF), P_(qf::hsG), 0.707, sr);
            const double a = P_(qf::atk), d = P_(qf::dcy), su = P_(qf::sus), r = P_(qf::rel);
            envOn[v] = !(a <= 0 && su >= 0.999 && r >= 3.999);
            envA[v] = a > 0 ? 1 / (a * sr) : 1; envD[v] = decayCoef(d / 3, sr); envS[v] = su; envR[v] = r >= 3.999 ? 1 : decayCoef(r / 3, sr);
            drv[v] = P_(qf::drv);
            dls[v] = p[(size_t) vp(v, vf::mute)] >= 0.5 ? 0 : P_(qf::dls) * p[(size_t) vp(v, vf::lv)] * (GROUP[v] == 0 ? (b > 0 ? 1 - b : 1) : (b < 0 ? 1 + b : 1));
        }
        const int mf[4][4] = {{0, ox::mLsF, ox::mLsG, -1}, {1, ox::mP1F, ox::mP1G, ox::mP1Q}, {1, ox::mP2F, ox::mP2G, ox::mP2Q}, {2, ox::mHsF, ox::mHsG, -1}};
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) meq[c][k].set(mf[k][0], p[(size_t) mf[k][1]], p[(size_t) mf[k][2]], mf[k][3] < 0 ? 0.707 : p[(size_t) mf[k][3]], sr);
        od = p[ox::odDrive]; odMode = (int) jsRound(p[ox::odMode]); odMix = p[ox::odMix]; odOut = std::pow(10.0, p[ox::odOut] / 20);
        const double div = DL_DIVS[std::max(0, std::min(13, (int) jsRound(p[ox::dlSync])))];
        dl.setParams(div > 0 ? 60 / p[pid::tempo] * div * 1000 : p[ox::dlTime], p[ox::dlFb], p[ox::dlHiCut], p[ox::dlLoCut], p[ox::dlModRate], p[ox::dlModDepth], p[ox::dlDuck], p[ox::dlPing] >= 0.5);
        dlLevel = p[ox::dlLevel];
    }

    void trigger(int v, bool accent) {
        if (v < 0 || v >= NV) return;
        on[v] = 1; acc[v] = accent ? 1 : 0; t[v] = 0;
        stage[v] = 0; if (envA[v] < 1) env[v] = 0; else { env[v] = 1; stage[v] = 1; }
        if (accent) sw = 1;
        const Sample* s = p[pid::src] >= 0.5 ? samples[v] : nullptr;
        if (s && !s->d.empty()) { sbuf[v] = accent && !s->acc.empty() ? &s->acc : &s->d; sp[v] = 0; srate[v] = s->sr / sr * tn[v]; }
        else sp[v] = -1;
        switch (v) {
            case 0: bdPitch = 1; bdPulse = std::max(1, (int) jsRound(BD_PULSE * bdPulseK * sr)); bdPulseN = bdPulse; break;
            case 1: sdNoiseE = 1; sdPulse = std::max(1, (int) jsRound(0.0005 * sr)); break;
            case 2: mE[0] = 1; ohChoke = 1; break;
            case 3: mE[1] = 1; ohChoke = 0; if (!swBd && swAmt > 0) chD = decayCoef(chTau * (1 + swAmt * sw * 4), sr); break;
            case 4: mE[2] = 1; cyLoE = 1; break;
            case 5: hcBurst = 1; hcTail = 0; hcLong = 0; break;
            case 6: knPulse = std::max(1, (int) jsRound(0.0003 * sr)); knClickE = 1; break;
        }
    }
    void sweep() { sw = 1; }
    void release(int v) { if (v >= 0 && v < NV) stage[v] = 3; }
    double overdrive(double x) const {
        const double g = 1 + od * 20; double y;
        if (odMode == 1) y = std::max(-1.0, std::min(1.0, x * g)) * 0.8;
        else if (odMode == 2) y = (std::tanh(x * g + 0.3) - std::tanh(0.3)) * 0.85;
        else y = std::tanh(x * g) * 0.85;
        return (x * (1 - odMix) + y * odMix) * odOut;
    }
    double amp(int v, double x) const { const double h = head; if (!acc[v]) return h * std::tanh(x / h); return h * std::tanh(x * accGain * 1.15 / h) / 1.15; }
    double playSample(int v) {
        const std::vector<float>* b = sbuf[v]; const double pos = sp[v]; if (!b) return 0;
        const int i = (int) std::floor(pos); if (i + 1 >= (int) b->size()) { sp[v] = -1; return 0; }
        const double fr = pos - i, y = (*b)[(size_t) i] + ((*b)[(size_t) i + 1] - (*b)[(size_t) i]) * fr; sp[v] = pos + srate[v];
        return acc[v] ? y * (b == &samples[v]->acc ? 1 : accGain) : y * 0.85;
    }
    // renders n samples (overwrites L and R); outs (optional): 8 channels, the separate outputs BD, SD, CP, HH/CY as
    // L/R pairs, dry. Level per voice goes to meter[] (peak since last read)
    template <typename T> void process(T* L, T* R, int n, T* const* outs = nullptr) {
        const bool src = p[pid::src] >= 0.5;
        double ins[NV];
        for (int s = 0; s < n; ++s) {
            double bd;
            const double swp = swBd ? 1 + swAmt * sw * 2 : 1; sw *= swD;
            {
                const double f = 50 * tn[0] * swp * (1 + BD_SWEEP * bdSweepK * bdPitch);
                bdS.set(f, bdQ * (1 + BD_SWEEP * bdSweepK * bdPitch), sr);
                const double x = bdPulse > 0 ? std::sin(PI * bdPulse / bdPulseN) : 0; if (bdPulse > 0) bdPulse--;
                bdPitch *= bdPitchD;
                bd = bdS.tick(x * 0.9 * bdExtIn) * 2.2;
                bd = bdOut.tick(bd) * bdBassK + x * bdClick * 0.35;
            }
            double sd;
            {
                const double x = sdPulse > 0 ? 1 : 0; if (sdPulse > 0) sdPulse--;
                const double tone = sdTone.tick(x * 1.4) * 1.6;
                const double w = noise();
                sdHp += sdHpA * (w - sdHp); sdLp += sdLpA * ((w - sdHp) - sdLp);
                sd = tone * sdMixT + sdLp * sdNoiseE * 1.1 * sdMixN; sdNoiseE *= sdNoiseD;
            }
            double metal[3];
            for (int m = 0; m < 3; ++m) {
                double sum = 0;
                for (int i = 0; i < 6; ++i) sum += osc[m][i].tick();
                sum *= 1.0 / 6;
                if (m < 2 && hhMix > 0) sum = sum * (1 - hhMix) + noise() * hhMix;
                const double hi = mHi2[m].tick(mHi[m].tick(sum));
                mHp[m] += mHpA * (hi - mHp[m]);
                const double h = hi - mHp[m];
                if (m == 2) { const double lo = mLo.tick(sum); metal[m] = h * (mE[2] * 0.75 + cyLoE * 0.25) * 3.2 + lo * cyLoE * 0.08; }
                else metal[m] = h * mE[m] * 3.2;
            }
            mE[1] *= chD;
            mE[0] *= (double) t[2] < ohGate * sr ? ohD : ohRel;
            if (ohChoke < 1) mE[0] *= chokeD;
            mE[2] *= cyD; cyLoE *= cyLoD;
            double hc;
            {
                const double w = noise(); const double bp = hcBp.tick(w); hcHp += hcHpA * (w - hcHp);
                const long long tt = t[5];
                if (on[5]) {
                    const long long b11 = (long long) jsRound(0.011 * sr);
                    if (tt > 0 && tt <= 3 * b11 && tt % b11 == 0) { hcBurst = 1; if (tt == 3 * b11) { hcTail = 1; hcLong = 1; } }
                }
                const double e = hcBurst * 1.5 + hcTail * 1.1 + hcLong * 0.1;
                hc = (bp * 1.6 + (w - hcHp) * 0.35) * e;
                hcBurst *= hcBurstD; hcTail *= hcTailD; hcLong *= hcLongD;
            }
            double kn;
            {
                const double x = knPulse > 0 ? 1 : 0; if (knPulse > 0) knPulse--;
                const double w = noise();
                kn = kn1.tick(x * 2.5) * 1.3 + kn2.tick(x * 2.5) * 0.7 + knClick.tick(w) * knClickE * 0.6;
                knClickE *= knClickD;
            }
            ins[0] = bd * VGAIN[0]; ins[1] = sd * VGAIN[1]; ins[2] = metal[0] * VGAIN[2]; ins[3] = metal[1] * VGAIN[3]; ins[4] = metal[2] * VGAIN[4]; ins[5] = hc * VGAIN[5]; ins[6] = kn * VGAIN[6];
            double l = 0, r = 0, snd = 0, dsnd = 0, g[8] = {};
            for (int v = 0; v < NV; ++v) {
                double y = src && sp[v] >= 0 ? playSample(v) : (src && samples[v] && !samples[v]->d.empty() ? 0 : ins[v]);
                if (!on[v]) y = 0;
                if (envOn[v]) {
                    double e = env[v];
                    switch (stage[v]) {
                        case 0: e += envA[v]; if (e >= 1) { e = 1; stage[v] = 1; } break;
                        case 1: e = envS[v] + (e - envS[v]) * envD[v]; break;
                        case 3: e *= envR[v]; break;
                        default: break;
                    }
                    env[v] = e; y *= e;
                }
                y = amp(v, y);
                if (drv[v] > 0) { const double d = drv[v]; y = std::tanh(y * (1 + d * 15)) / (1 + d); }
                y = eq[v][2].tick(eq[v][1].tick(eq[v][0].tick(y)));
                meter[v] = std::max(meter[v], std::abs(y));
                l += y * gL[v]; r += y * gR[v]; snd += y * send[v]; dsnd += y * dls[v];
                g[OUTS[v] * 2] += y * gL[v]; g[OUTS[v] * 2 + 1] += y * gR[v];
                t[v]++;
            }
            double rl, rr; rv.tick(snd, rl, rr);
            l += rl * rvLevel; r += rr * rvLevel;
            double dlL, dlR; dl.tick(dsnd, dlL, dlR);
            l += dlL * dlLevel; r += dlR * dlLevel;
            for (int k = 0; k < 4; ++k) { l = meq[0][k].tick(l); r = meq[1][k].tick(r); }
            if (od > 0) { l = overdrive(l); r = overdrive(r); }
            l *= master; r *= master;
            dcL += dcA * (l - dcL); dcR += dcA * (r - dcR);
            L[s] = (T) std::tanh(l - dcL); R[s] = (T) std::tanh(r - dcR);
            if (outs) for (int k = 0; k < 8; ++k) if (outs[k]) outs[k][s] = (T) std::tanh(g[k] * master);
        }
    }
    double meter[NV] = {};

private:
    double sr;
    uint32_t seed = 0x2545F491u;
    const Sample* samples[NV] = {};
    int on[NV] = {}, acc[NV] = {};
    long long t[NV] = {};
    double sp[NV] = {-1, -1, -1, -1, -1, -1, -1}, srate[NV] = {};
    const std::vector<float>* sbuf[NV] = {};
    double tn[NV] = {}, dk[NV] = {}, gL[NV] = {}, gR[NV] = {}, send[NV] = {};
    double accGain = 1.5, master = 1, head = 1.2, sw = 0;
    double bdPulseK = 1, bdClick = 0, bdDk = 1, bdBassK = 1, sdMixT = 1, sdMixN = 1, sdDk = 1, sdQk = 1, hhF = 1, hhMix = 0, cyF = 1, hcQ = 1.2, hcDk = 1, swD = 0, swAmt = 0, chTau = 0.03;
    bool swBd = true;
    SVF bdS, bdOut; double bdPitch = 0, bdQ = 1, bdPitchD = 0; int bdPulse = 0, bdPulseN = 1;
    SVF sdTone; double sdNoiseE = 0, sdHp = 0, sdLp = 0, sdHpA = 0, sdLpA = 0, sdNoiseD = 0; int sdPulse = 0;
    Square osc[3][6]; SVF mHi[3], mHi2[3], mLo; double mHp[3] = {}, mE[3] = {}, cyLoE = 0, ohChoke = 1, mHpA = 0;
    double chD = 0, ohD = 0, ohGate = 0, ohRel = 0, chokeD = 0, cyD = 0, cyLoD = 0;
    SVF hcBp; double hcHp = 0, hcBurst = 0, hcTail = 0, hcLong = 0, hcHpA = 0, hcBurstD = 0, hcTailD = 0, hcLongD = 0;
    SVF kn1, kn2, knClick; int knPulse = 0; double knClickE = 0, knClickD = 0;
    double dcL = 0, dcR = 0, dcA = 0;
    Rev7 rv; double rvLevel = 0.5;
    TCDelay dl; double dlLevel = 0.4;
    Biquad eq[NV][3], meq[2][4];
    double env[NV] = {}, envA[NV] = {}, envD[NV] = {}, envS[NV] = {}, envR[NV] = {}, drv[NV] = {}, dls[NV] = {};
    int stage[NV] = {}; bool envOn[NV] = {};
    double od = 0, odMix = 1, odOut = 1; int odMode = 0;
    double bdExt = 1, bdExtIn = 1, bdSweepK = 1;
};

} // namespace dr110
