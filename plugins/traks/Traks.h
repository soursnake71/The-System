// Omega Traks: line-for-line C++ port of traks/engine.js (the Drumtraks circuit model: companded 8-bit ROM, the
// sample clock that TUNING varies, the six channels and their output stages) with the Omega additions. The filters,
// the REV7-style reverb and the TC-style delay are the Omega 110's (plugins/dr110/DR110.h). No JUCE in here.
// traks/tools/compare.mjs renders the same hits with both and compares them sample by sample; keep them in step.
#pragma once
#include "../dr110/DR110.h"

namespace traks {
using dr110::SVF; using dr110::Biquad; using dr110::TCDelay; using dr110::Rev7; using dr110::onePole; using dr110::decayCoef; using dr110::jsRound;
using dr110::PI; using dr110::DL_DIVS; using dr110::DL_DIV_NAMES; using dr110::OD_MODES; using dr110::RV_PROGS;

constexpr int NV = 13;
constexpr int NG = 16, NF = 11;
constexpr int NX = 24;                        // master EQ, overdrive, delay
constexpr int NG2 = NG + NV * NF + NX;        // then 13 per pad
constexpr int NP = NG2 + NV * 13;
inline const char* const VOICES[NV] = {"BS", "SN", "RM", "T1", "T2", "CR", "RD", "CH", "OH", "CP", "TB", "CB", "CA"};
inline const char* const VOICE_NAMES[NV] = {"Bass", "Snare", "Rim", "Tom 1", "Tom 2", "Crash", "Ride", "Closed Hat", "Open Hat", "Claps", "Tamb", "Cowbell", "Cabasa"};
inline const char* const KEY_NAMES[NV] = {"BASS", "SNARE", "RIM", "TOM1", "TOM2", "CRASH", "RIDE", "CLOSED", "OPEN", "CLAPS", "TAMB", "COWBELL", "CABASA"};
inline const char* const DAC_MODES[3] = {"COMPANDED 8-BIT", "LINEAR 8-BIT", "LINEAR 6-BIT"};
constexpr int CHAN[NV] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5, 5};
constexpr double VGAIN[NV] = {1.0, 0.85, 0.6, 0.62, 0.62, 0.55, 0.5, 0.55, 0.5, 0.7, 0.45, 0.5, 0.45};
constexpr double SEND[NV] = {0.03, 0.18, 0.1, 0.12, 0.12, 0.05, 0.03, 0.03, 0.05, 0.2, 0.05, 0.05, 0.03};

namespace pid { enum { tempo, volume, metro, accent, dac, interp, poly, filt, rvProg, rvTime, rvHigh, rvDelay, rvDiff, rvEr, rvCut, rvLevel }; }
namespace vf { enum { lv, pan, tune, dec, mute, snd, rev, cut, res, start, out }; }
inline int vp(int v, int f) { return NG + v * NF + f; }
namespace ox { enum { mLsF = NG + NV * NF, mLsG, mP1F, mP1G, mP1Q, mP2F, mP2G, mP2Q, mHsF, mHsG,
    odDrive, odMode, odMix, odOut, dlTime, dlSync, dlFb, dlHiCut, dlLoCut, dlModRate, dlModDepth, dlDuck, dlPing, dlLevel }; }
namespace qf { enum { lsF, lsG, pkF, pkG, pkQ, hsF, hsG, atk, dcy, sus, rel, drv, dls }; }
inline int vq(int v, int f) { return NG2 + v * 13 + f; }

struct ParamInfo { std::string id, name; double min, max, def; };
inline std::vector<ParamInfo> params() {
    std::vector<ParamInfo> p = {
        {"tempo", "Tempo", 40, 250, 120}, {"volume", "Master Volume", 0, 10, 7}, {"metro", "Metronome Volume", 0, 10, 5},
        {"accent", "Accent Level", 0, 12, 6}, {"dac", "DAC Mode", 0, 2, 0}, {"interp", "Interpolation", 0, 1, 0},
        {"poly", "Poly Voices", 0, 1, 0}, {"filt", "Output Filter Bypass", 0, 1, 0},
        {"rvProg", "Reverb Program", 0, 4, 1}, {"rvTime", "Reverb Time", 0.3, 10, 1.6}, {"rvHigh", "Reverb High", 0.1, 1, 0.6},
        {"rvDelay", "Reverb Delay", 0, 200, 12}, {"rvDiff", "Reverb Diffusion", 0, 10, 8}, {"rvEr", "Reverb ER Level", 0, 1, 0.35},
        {"rvCut", "Reverb High Cut", 1000, 12000, 9000}, {"rvLevel", "Reverb Return", 0, 1, 0.5}};
    for (int v = 0; v < NV; ++v) {
        const std::string id = VOICES[v], n = std::string(VOICE_NAMES[v]) + " ";
        const std::vector<ParamInfo> f = {{id + "lv", n + "Volume", 0, 1, 0.8}, {id + "pan", n + "Pan", -1, 1, 0}, {id + "tune", n + "Tuning", -12, 12, 0},
            {id + "dec", n + "Decay", 0.02, 3, 3}, {id + "mute", n + "Mute", 0, 1, 0}, {id + "snd", n + "Reverb Send", 0, 1, SEND[v]},
            {id + "rev", n + "Reverse", 0, 1, 0}, {id + "cut", n + "Cutoff", 200, 20000, 20000}, {id + "res", n + "Resonance", 0, 1, 0},
            {id + "start", n + "Start", 0, 0.9, 0}, {id + "out", n + "Output", 0, 6, 0}};
        p.insert(p.end(), f.begin(), f.end());
    }
    const std::vector<ParamInfo> x = {
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
        const std::vector<ParamInfo> f = {{id + "lsF", n + "Low Freq", 20, 1000, 100}, {id + "lsG", n + "Low Gain", -15, 15, 0},
            {id + "pkF", n + "Mid Freq", 40, 16000, 1000}, {id + "pkG", n + "Mid Gain", -15, 15, 0}, {id + "pkQ", n + "Mid Q", 0.3, 10, 1},
            {id + "hsF", n + "High Freq", 1000, 18000, 6000}, {id + "hsG", n + "High Gain", -15, 15, 0},
            {id + "atk", n + "Attack", 0, 1, 0}, {id + "dcy", n + "Env Decay", 0.005, 4, 0.3}, {id + "sus", n + "Sustain", 0, 1, 1}, {id + "rel", n + "Release", 0.005, 4, 4},
            {id + "drv", n + "Drive", 0, 1, 0}, {id + "dls", n + "Delay Send", 0, 1, 0}};
        p.insert(p.end(), f.begin(), f.end());
    }
    return p;
}
using Params = std::array<double, NP>;
inline Params defaults() { Params a{}; auto p = params(); for (int i = 0; i < NP; ++i) a[(size_t) i] = p[(size_t) i].def; return a; }

// the companding DAC: code -> level
struct MuTable { double t[256]; MuTable() { for (int c = 0; c < 256; ++c) { const int u = ~c & 0xFF; int x = ((u & 0x0F) << 3) + 0x84; x <<= (u & 0x70) >> 4; t[c] = ((u & 0x80) ? 0x84 - x : x - 0x84) / 32768.0; } } };
inline const MuTable& mu() { static const MuTable m; return m; }
inline uint8_t muEncode(double x) {
    int pcm = (int) std::max(-32767.0, std::min(32767.0, std::floor(x * 32767 + 0.5)));
    const int sign = pcm < 0 ? 0x80 : 0; if (pcm < 0) pcm = -pcm; if (pcm > 32635) pcm = 32635; pcm += 0x84;
    int ex = 7; for (int m = 0x4000; (pcm & m) == 0 && ex > 0; m >>= 1) ex--;
    return (uint8_t) ((~(sign | (ex << 4) | ((pcm >> (ex + 3)) & 0x0F))) & 0xFF);
}
// a sound in ROM: its codes and the sample clock they were stored at
struct Rom { std::vector<uint8_t> codes{0}; double rate = 25000; };
// the ROM image (traks/rom/traks.rom)
inline std::vector<Rom> parseRom(const uint8_t* b, size_t size) {
    std::vector<Rom> out; if (size < 8 || b[0] != 'O' || b[1] != 'T') return out;
    const int n = b[5]; size_t off = 8 + (size_t) n * 8;
    for (int i = 0; i < n; ++i) {
        const uint8_t* h = b + 8 + i * 8;
        const uint32_t rate = h[0] | h[1] << 8 | h[2] << 16 | (uint32_t) h[3] << 24, len = h[4] | h[5] << 8 | h[6] << 16 | (uint32_t) h[7] << 24;
        Rom r; r.rate = rate; if (off + len <= size) r.codes.assign(b + off, b + off + len); out.push_back(std::move(r)); off += len;
    }
    return out;
}

class Engine {
public:
    explicit Engine(double sampleRate) : sr(sampleRate), rv(sampleRate), dl(sampleRate) { p = defaults(); rom.resize(NV); clkD = decayCoef(0.008, sr); update(); }
    Params p;
    double songGain = 1;
    double meter[NV] = {};
    void set(int i, double v) { p[(size_t) i] = v; update(); }
    void setAll(const Params& a) { p = a; update(); }
    void setRom(int v, const Rom& r) { if (v >= 0 && v < NV && r.codes.size() > 1) { rom[(size_t) v] = r; act[v] = false; } }
    const Rom& getRom(int v) const { return rom[(size_t) v]; }
    void update() {
        for (int v = 0; v < NV; ++v) {
            const double g = P(vp(v, vf::mute)) >= 0.5 ? 0 : 1;
            const double a = (P(vp(v, vf::pan)) + 1) * PI / 4;
            gL[v] = g * std::cos(a) * std::sqrt(2.0); gR[v] = g * std::sin(a) * std::sqrt(2.0);
            send[v] = g * P(vp(v, vf::snd));
            const int o = (int) jsRound(P(vp(v, vf::out))); outCh[v] = o >= 1 && o <= 6 ? o - 1 : CHAN[v];
            dk[v] = P(vp(v, vf::dec)) >= 2.999 ? 1 : decayCoef(P(vp(v, vf::dec)) / 3, sr);
            oflt[v].set(9500, 0.707, sr);
            const double cut = P(vp(v, vf::cut)); cutOn[v] = cut < 19999;
            vflt[v].set(cut, 0.707 + P(vp(v, vf::res)) * 9.3, sr);
        }
        accGain = std::pow(10.0, P(pid::accent) / 20);
        dacMode = (int) jsRound(P(pid::dac)); interp = P(pid::interp) >= 0.5; poly = P(pid::poly) >= 0.5; filtOn = P(pid::filt) < 0.5;
        const double vol = P(pid::volume) / 10; master = vol * vol * 1.2;
        metro = P(pid::metro) / 10 * 0.35;
        dcA = onePole(12, sr);
        rv.setParams((int) jsRound(P(pid::rvProg)), P(pid::rvTime), P(pid::rvHigh), P(pid::rvDelay), P(pid::rvDiff), P(pid::rvEr), P(pid::rvCut));
        rvLevel = P(pid::rvLevel);
        for (int v = 0; v < NV; ++v) {
            eq[v][0].set(0, P(vq(v, qf::lsF)), P(vq(v, qf::lsG)), 0.707, sr); eq[v][1].set(1, P(vq(v, qf::pkF)), P(vq(v, qf::pkG)), P(vq(v, qf::pkQ)), sr);
            eq[v][2].set(2, P(vq(v, qf::hsF)), P(vq(v, qf::hsG)), 0.707, sr);
            const double a = P(vq(v, qf::atk)), d = P(vq(v, qf::dcy)), su = P(vq(v, qf::sus)), r = P(vq(v, qf::rel));
            envOn[v] = !(a <= 0 && su >= 0.999 && r >= 3.999);
            envA[v] = a > 0 ? 1 / (a * sr) : 1; envD[v] = decayCoef(d / 3, sr); envS[v] = su; envR[v] = r >= 3.999 ? 1 : decayCoef(r / 3, sr);
            drv[v] = P(vq(v, qf::drv));
            dls[v] = P(vp(v, vf::mute)) >= 0.5 ? 0 : P(vq(v, qf::dls));
        }
        const int mf[4][4] = {{0, ox::mLsF, ox::mLsG, -1}, {1, ox::mP1F, ox::mP1G, ox::mP1Q}, {1, ox::mP2F, ox::mP2G, ox::mP2Q}, {2, ox::mHsF, ox::mHsG, -1}};
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) meq[c][k].set(mf[k][0], P(mf[k][1]), P(mf[k][2]), mf[k][3] < 0 ? 0.707 : P(mf[k][3]), sr);
        od = P(ox::odDrive); odMode = (int) jsRound(P(ox::odMode)); odMix = P(ox::odMix); odOut = std::pow(10.0, P(ox::odOut) / 20);
        const double div = DL_DIVS[std::max(0, std::min(13, (int) jsRound(P(ox::dlSync))))];
        dl.setParams(div > 0 ? 60 / P(pid::tempo) * div * 1000 : P(ox::dlTime), P(ox::dlFb), P(ox::dlHiCut), P(ox::dlLoCut), P(ox::dlModRate), P(ox::dlModDepth), P(ox::dlDuck), P(ox::dlPing) >= 0.5);
        dlLevel = P(ox::dlLevel);
    }
    static double lvGain(double lv) { const double x = lv / 0.8; return x * x; }
    void trigger(int v, bool accent, double vel = 1, double lv = -1, double tune = -99) {
        if (v < 0 || v >= NV) return;
        const Rom& r = rom[(size_t) v];
        if (!poly) for (int k = 0; k < NV; ++k) if (CHAN[k] == CHAN[v]) act[k] = false;
        const double t = tune < -50 ? P(vp(v, vf::tune)) : tune, l = lv < 0 ? P(vp(v, vf::lv)) : lv;
        inc[v] = r.rate * std::pow(2.0, t / 12) / sr;
        pos[v] = std::floor(P(vp(v, vf::start)) * (double) r.codes.size());
        amp[v] = lvGain(l) * vel * (accent ? accGain : 1) * VGAIN[v];
        dec[v] = 1; act[v] = true;
        stage[v] = 0; if (envA[v] < 1) env[v] = 0; else { env[v] = 1; stage[v] = 1; }
    }
    void release(int v) { if (v >= 0 && v < NV) stage[v] = 3; }
    void click(bool first) { clk = 1; clkF = first ? 2600 : 2000; clkPh = 0; }
    double overdrive(double x) const {
        const double g = 1 + od * 20; double y;
        if (odMode == 1) y = std::max(-1.0, std::min(1.0, x * g)) * 0.8;
        else if (odMode == 2) y = (std::tanh(x * g + 0.3) - std::tanh(0.3)) * 0.85;
        else y = std::tanh(x * g) * 0.85;
        return (x * (1 - odMix) + y * odMix) * odOut;
    }
    double read(int v) {
        const auto& c = rom[(size_t) v].codes; const long n = (long) c.size(); const double ps = pos[v]; const long i = (long) std::floor(ps);
        if (i >= n) { act[v] = false; return 0; }
        const bool rev = P(vp(v, vf::rev)) >= 0.5;
        const double* M = mu().t;
        double y = M[c[(size_t) (rev ? n - 1 - i : i)]];
        if (interp && i + 1 < n) { const double y2 = M[c[(size_t) (rev ? n - 2 - i : i + 1)]]; y += (y2 - y) * (ps - (double) i); }
        if (dacMode == 1) y = std::floor(y * 127 + 0.5) / 127;
        else if (dacMode == 2) y = std::floor(y * 31 + 0.5) / 31;
        pos[v] = ps + inc[v];
        return y;
    }
    // outs: 12 channels (CH1..CH6 as L/R pairs) or nullptr
    void process(float* L, float* R, int n, float* const* outs) {
        for (int s = 0; s < n; ++s) {
            double l = 0, r = 0, snd = 0, dsnd = 0, g[12] = {};
            for (int v = 0; v < NV; ++v) {
                if (!act[v] && stage[v] == 4) continue;
                double y = 0;
                if (act[v]) { y = read(v) * amp[v] * dec[v]; dec[v] *= dk[v]; }
                else if (std::abs(oflt[v].ic1) + std::abs(oflt[v].ic2) + std::abs(vflt[v].ic1) + std::abs(vflt[v].ic2) < 1e-9) { stage[v] = 4; continue; }
                if (filtOn) { oflt[v].tick(y); y = oflt[v].lp; }
                if (cutOn[v]) { vflt[v].tick(y); y = vflt[v].lp; }
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
                if (drv[v] > 0) { const double d = drv[v]; y = std::tanh(y * (1 + d * 15)) / (1 + d); }
                y = eq[v][2].tick(eq[v][1].tick(eq[v][0].tick(y)));
                const double ay = std::abs(y); if (ay > meter[v]) meter[v] = ay;
                l += y * gL[v]; r += y * gR[v]; snd += y * send[v]; dsnd += y * dls[v];
                const int o = outCh[v]; g[o * 2] += y * gL[v]; g[o * 2 + 1] += y * gR[v];
            }
            double rl, rr; rv.tick(snd, rl, rr);
            l += rl * rvLevel; r += rr * rvLevel;
            double dlL, dlR; dl.tick(dsnd, dlL, dlR);
            l += dlL * dlLevel; r += dlR * dlLevel;
            for (int k = 0; k < 4; ++k) { l = meq[0][k].tick(l); r = meq[1][k].tick(r); }
            if (od > 0) { l = overdrive(l); r = overdrive(r); }
            const double m = master * songGain;
            l *= m; r *= m;
            if (clk > 1e-4) { clkPh += clkF / sr; if (clkPh >= 1) clkPh -= 1; const double c = (clkPh < 0.5 ? 1 : -1) * clk * metro; l += c; r += c; clk *= clkD; }
            dcL += dcA * (l - dcL); dcR += dcA * (r - dcR);
            L[s] = (float) std::tanh(l - dcL); R[s] = (float) std::tanh(r - dcR);
            if (outs) for (int k = 0; k < 12; ++k) outs[k][s] = (float) std::tanh(g[k] * m);
        }
    }

private:
    double P(int i) const { return p[(size_t) i]; }
    double sr;
    std::vector<Rom> rom;
    bool act[NV] = {}; double pos[NV] = {}, inc[NV] = {}, amp[NV] = {}, dk[NV] = {}, dec[NV] = {};
    bool cutOn[NV] = {};
    SVF oflt[NV], vflt[NV];
    Biquad eq[NV][3];
    double env[NV] = {}; int stage[NV] = {}; bool envOn[NV] = {};
    double envA[NV] = {}, envD[NV] = {}, envS[NV] = {}, envR[NV] = {}, drv[NV] = {}, dls[NV] = {};
    Biquad meq[2][4];
    Rev7 rv; TCDelay dl;
    double clk = 0, clkPh = 0, clkF = 2000, clkD = 0;
    double gL[NV] = {}, gR[NV] = {}, send[NV] = {}; int outCh[NV] = {};
    double accGain = 1, master = 1, metro = 0, dcA = 0, dcL = 0, dcR = 0, rvLevel = 0, dlLevel = 0;
    int dacMode = 0; bool interp = false, poly = false, filtOn = true;
    double od = 0, odMix = 1, odOut = 1; int odMode = 0;
};

} // namespace traks
