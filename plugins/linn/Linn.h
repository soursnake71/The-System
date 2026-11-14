// Omega Linn: line-for-line C++ port of linn/engine.js (the LinnDrum circuit model: the mux / snare / tom clocks, the
// dynamics, the hi-hat EG and VCA, the MXR board's mixer) with the Omega additions. The filters, the REV7-style reverb
// and the TC-style delay are the Omega 110's (plugins/dr110/DR110.h). No JUCE in here.
// linn/tools/compare.mjs renders the same hits with both and compares them sample by sample; keep them in step.
#pragma once
#include "../dr110/DR110.h"

namespace linn {
using dr110::SVF; using dr110::Biquad; using dr110::TCDelay; using dr110::Rev7; using dr110::onePole; using dr110::decayCoef; using dr110::jsRound;
using dr110::PI; using dr110::DL_DIVS; using dr110::DL_DIV_NAMES; using dr110::OD_MODES; using dr110::RV_PROGS;

constexpr int NV = 15;                        // mixer channels with a sound
constexpr int NG = 24, NF = 11;
constexpr int NX = 24;                        // master EQ, overdrive, delay
constexpr int NG2 = NG + NV * NF + NX;        // then 13 per channel
constexpr int NP = NG2 + NV * 13;
constexpr int NG_ROM = 13;
constexpr int NS = 23;
constexpr int S_OPEN = 8;
inline const char* const CHS[NV] = {"BS", "SN", "SS", "HH", "HT", "MT", "LT", "RD", "CR", "CB", "TB", "HC", "LC", "CW", "CP"};
inline const char* const CH_NAMES[NV] = {"Bass", "Snare", "Sidestick", "Hihat", "Hi Tom", "Mid Tom", "Lo Tom", "Ride", "Crash", "Cabasa", "Tamb", "Hi Conga", "Lo Conga", "Cowbell", "Claps"};
inline const char* const CH_KEYS[NV] = {"BASS", "SNARE", "SIDESTICK", "HIHAT", "HI TOM", "MID TOM", "LO TOM", "RIDE", "CRASH", "CABASA", "TAMB", "HI CONGA", "LO CONGA", "COWBELL", "CLAPS"};
inline const char* const GROUPS[NG_ROM] = {"BASS", "BASS ALT", "SNARE", "SIDESTICK", "HIHAT", "TOMS", "CONGAS", "RIDE", "CRASH", "CABASA", "TAMB", "COWBELL", "CLAPS"};
inline const char* const HAT_MODES[3] = {"LINN", "RESTART", "LM-1 LOOP"};
namespace G { enum { BASS, BASS2, SNARE, SIDESTICK, HAT, TOM, CONGA, RIDE, CRASH, CABASA, TAMB, COWBELL, CLAPS }; }
constexpr int ROM_SIZE[NG_ROM] = {4096, 4096, 4096, 4096, 16384, 8192, 8192, 32768, 32768, 4096, 4096, 4096, 4096};
constexpr int CORE[NV] = {0, 1, 1, 2, 3, 3, 3, 4, 5, 6, 7, 3, 3, 8, 9};
constexpr int CH_GROUP[NV] = {0, 2, 3, 4, 5, 5, 5, 7, 8, 9, 10, 6, 6, 11, 12};
struct SoundInfo { const char* name; int ch, dyn; };
inline const SoundInfo SOUNDS[NS] = {
    {"BASS 1", 0, 1}, {"BASS 2", 0, 0}, {"SNARE 1", 1, 2}, {"SNARE 2", 1, 3}, {"SNARE 3", 1, 4}, {"SIDESTICK", 2, 4},
    {"HIHAT 1", 3, 1}, {"HIHAT 2", 3, 0}, {"HIHAT OPEN", 3, 0}, {"HI TOM", 4, 0}, {"MID TOM", 5, 0}, {"LO TOM", 6, 0},
    {"RIDE 1", 7, 1}, {"RIDE 2", 7, 0}, {"CRASH", 8, 0}, {"CABASA 1", 9, 1}, {"CABASA 2", 9, 0}, {"TAMB 1", 10, 1}, {"TAMB 2", 10, 0},
    {"HI CONGA", 11, 0}, {"LO CONGA", 12, 0}, {"COWBELL", 13, 0}, {"CLAPS", 14, 0}};
constexpr double SEND[NV] = {0.03, 0.18, 0.1, 0.03, 0.12, 0.12, 0.12, 0.03, 0.05, 0.03, 0.05, 0.08, 0.08, 0.05, 0.2};
constexpr int OUT_DEFAULT[NV] = {0, 1, 2, 3, 4, 4, 4, 5, 5, 7, 7, 6, 6, 7, 7};
constexpr int WANT[5] = {2, 0, 0, 1, 2};

namespace pid { enum { tempo, volume, mux, hatDecay, tSN, tHT, tMT, tLT, tHC, tLC, hatMode, bassChip, kit, poly, clickLv, clickPan,
    rvProg, rvTime, rvHigh, rvDelay, rvDiff, rvEr, rvCut, rvLevel }; }
namespace vf { enum { lv, pan, tune, dec, mute, snd, rev, cut, res, start, out }; }
inline int vp(int v, int f) { return NG + v * NF + f; }
namespace ox { enum { mLsF = NG + NV * NF, mLsG, mP1F, mP1G, mP1Q, mP2F, mP2G, mP2Q, mHsF, mHsG,
    odDrive, odMode, odMix, odOut, dlTime, dlSync, dlFb, dlHiCut, dlLoCut, dlModRate, dlModDepth, dlDuck, dlPing, dlLevel }; }
namespace qf { enum { lsF, lsG, pkF, pkG, pkQ, hsF, hsG, atk, dcy, sus, rel, drv, dls }; }
inline int vq(int v, int f) { return NG2 + v * 13 + f; }
constexpr int TUNE_PARAM[NV] = {-1, pid::tSN, pid::tSN, -1, pid::tHT, pid::tMT, pid::tLT, -1, -1, -1, -1, pid::tHC, pid::tLC, -1, -1};

struct ParamInfo { std::string id, name; double min, max, def; };
inline std::vector<ParamInfo> params() {
    std::vector<ParamInfo> p = {
        {"tempo", "Tempo", 40, 250, 120}, {"volume", "Master Volume", 0, 100, 90}, {"mux", "Mux Tuning (trimmer)", 0, 100, 25},
        {"hatDecay", "Hihat Decay", 0, 100, 50},
        {"tSN", "Snare Tuning", 0, 100, 25}, {"tHT", "Hi Toms Tuning", 0, 100, 60}, {"tMT", "Mid Toms Tuning", 0, 100, 50},
        {"tLT", "Lo Toms Tuning", 0, 100, 40}, {"tHC", "Hi Congas Tuning", 0, 100, 52}, {"tLC", "Lo Congas Tuning", 0, 100, 40},
        {"hatMode", "Hat Mode", 0, 2, 0}, {"bassChip", "Bass Chip", 0, 1, 0}, {"kit", "Sound Set", 0, 1, 0}, {"poly", "Poly Voices", 0, 1, 0},
        {"clickLv", "Click Volume", 0, 100, 70}, {"clickPan", "Click Pan", 0, 100, 50},
        {"rvProg", "Reverb Program", 0, 4, 1}, {"rvTime", "Reverb Time", 0.3, 10, 1.6}, {"rvHigh", "Reverb High", 0.1, 1, 0.6},
        {"rvDelay", "Reverb Delay", 0, 200, 12}, {"rvDiff", "Reverb Diffusion", 0, 10, 8}, {"rvEr", "Reverb ER Level", 0, 1, 0.35},
        {"rvCut", "Reverb High Cut", 1000, 12000, 9000}, {"rvLevel", "Reverb Return", 0, 1, 0.5}};
    for (int v = 0; v < NV; ++v) {
        const std::string id = CHS[v], n = std::string(CH_NAMES[v]) + " ";
        const std::vector<ParamInfo> f = {{id + "lv", n + "Volume", 0, 100, 100}, {id + "pan", n + "Pan", 0, 100, 50}, {id + "tune", n + "Tune", -12, 12, 0},
            {id + "dec", n + "Decay", 0.02, 3, 3}, {id + "mute", n + "Mute", 0, 1, 0}, {id + "snd", n + "Reverb Send", 0, 1, SEND[v]},
            {id + "rev", n + "Reverse", 0, 1, 0}, {id + "cut", n + "Cutoff", 200, 20000, 20000}, {id + "res", n + "Resonance", 0, 1, 0},
            {id + "start", n + "Start", 0, 0.9, 0}, {id + "out", n + "Output", 0, 8, 0}};
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
        const std::string id = CHS[v], n = std::string(CH_NAMES[v]) + " ";
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

/* ---- the circuit's numbers (linn/engine.js) */
constexpr double VPLUS = 15, VCC = 5;
inline double par(double a, double b) { return a * b / (a + b); }
inline double divr(double r1, double r2) { return r2 / (r1 + r2); }
inline double potLaw(double x) { const double B = 81, A = 1.0 / 80; return A * std::pow(B, x) - A; }
inline double ls627(double lo, double hi, double cv) { return lo + (hi - lo) / VCC * std::max(0.0, std::min(VCC, cv)); }
inline double muxClock(double t) {
    const double bot = t * 10e3 / 100, top = 10e3 - bot, p = par(std::max(bot, 1e-6), 100e3);
    return ls627(250e3, 2330e3, VPLUS * divr(18e3 + top, p)) / 32;
}
inline double stCv(double v) { return std::max(0.0, std::min(VCC, v * (1 + 30e3 / 47e3) * divr(10e3, 100e3))); }
inline double snareClock(double k) {
    const double bot = k * 100e3 / 100, top = 100e3 - bot, p = bot > 0 ? par(bot, 97e3) : 0;
    const double v = VPLUS * p / (top + p);
    return ls627(8e3, 80e3, stCv(v * divr(75e3, 22e3)));
}
inline double tomClock(double k) { const double bot = k * 100e3 / 100, top = 100e3 - bot; return ls627(8e3, 80e3, stCv(VPLUS * bot / (390e3 + top + bot))); }
inline double snareIref(double d2, double d3) {
    const double R0 = 3.3e3, R1 = 380e3, R2 = 22e3, R3 = 5.6e3, a = R0 * R2 * R3, b = R0 * R1 * R3, c = R0 * R1 * R2, d = R1 * R2 * R3;
    return (a * VPLUS + b * d2 * VCC + c * d3 * VCC) / (R0 * (a + b + c + d));
}
inline double dynGain(int i) {
    const double mx = snareIref(1, 1);
    switch (i) { case 1: return divr(10e3, 3.3e3 + 125); case 2: return snareIref(1, 0) / mx; case 3: return snareIref(0, 1) / mx; default: return 1; }
}
constexpr double R_PRE[16] = {0, 0, 0, 5.1e3, 0, 0, 0, 10e3, 5.1e3, 10e3, 10e3, 0, 0, 5.1e3, 2.4e3, 0};
inline void mixGain(int ch, double vol, double pan, double& gl, double& gr) {
    gl = gr = 0; if (vol <= 0) return;
    const double RV = 5e3, RP = 10e3, R1 = 5.6e3, R2 = 5.6e3, R3 = 15e3, R4 = 15e3, RF = 33e3;
    const double rb = std::max(1e-3, RV * potLaw(vol / 100)), rt = RV - rb, pl = pan * RP / 100, pr = RP - pl;
    const double r0 = R_PRE[ch] + rt, rR = R1 + (pr > 0 ? par(pr, R3) : 0), rL = R2 + (pl > 0 ? par(pl, R4) : 0);
    const double rw = 1 / (1 / rb + 1 / rL + 1 / rR), i0 = 1 / (r0 + rw), vp_ = 1 - i0 * r0;
    gl = (vp_ - vp_ / rL * R2) * RF / R4; gr = (vp_ - vp_ / rR * R1) * RF / R3;
}
inline double mixNorm() { double l, r; mixGain(0, 100, 50, l, r); return 1 / l; }
inline double masterGain(double pot) { const double rb = std::max(1e-3, 10e3 * potLaw(pot / 100)), rt = 10e3 - rb; return divr(rt, par(rb, 10e3)); }
constexpr double HAT_HOLD = 1.1 * 510e3 * 0.01e-6;
inline double hatTau(bool open, double decay) { const double r = open ? 1e6 : par(1e6, 10e3 + 125 + 100e3 * decay / 100); return r * 1e-6; }
inline double hatVca(double eg) { return std::max(0.0, std::min(1.93, eg * divr(8.2e3, 10e3))) / 1.93; }
inline double tempoHz(double k) { return 1 / (0.693 * (5.1e3 + 2 * (18e3 + (100 - k) * 1e3)) * 0.1e-6); }
inline double tempoOfKnob(double k) { return tempoHz(k) * (250 / tempoHz(100)); }
inline double knobOfTempo(double b) { double lo = 0, hi = 100; for (int i = 0; i < 40; ++i) { const double m = (lo + hi) / 2; if (tempoOfKnob(m) < b) lo = m; else hi = m; } return (lo + hi) / 2; }

// a ROM layer: one recording of a sound, the sample clock it was recorded at, its sound set and dynamic
struct Layer { double rate = 44100, clk = 24836; int kit = 0, dyn = -1; std::vector<double> data; };
using Rom = std::vector<std::vector<Layer>>;      // layers per group
// the ROM image (linn/rom/linn.rom, version 2)
inline Rom parseRom(const uint8_t* b, size_t size) {
    Rom out((size_t) NG_ROM); if (size < 8 || b[0] != 'O' || b[1] != 'L') return out;
    const int n = b[5] | b[6] << 8; size_t off = 8 + (size_t) n * 16;
    auto u32 = [b](size_t o) { return (uint32_t) b[o] | (uint32_t) b[o + 1] << 8 | (uint32_t) b[o + 2] << 16 | (uint32_t) b[o + 3] << 24; };
    for (int i = 0; i < n; ++i) {
        const size_t h = 8 + (size_t) i * 16; const int g = b[h];
        Layer L; L.kit = b[h + 2]; L.dyn = (int) b[h + 3] - 1; L.rate = u32(h + 4); L.clk = u32(h + 8); const uint32_t len = u32(h + 12);
        if (off + (size_t) len * 2 > size) break;
        L.data.resize(len);
        for (uint32_t j = 0; j < len; ++j) { int x = b[off + j * 2] | b[off + j * 2 + 1] << 8; if (x >= 32768) x -= 65536; L.data[j] = x / 32768.0; }
        off += (size_t) len * 2;
        if (g < NG_ROM) out[(size_t) g].push_back(std::move(L));
    }
    return out;
}

class Engine {
public:
    explicit Engine(double sampleRate) : sr(sampleRate), rv(sampleRate), dl(sampleRate) { p = defaults(); rom.resize(NG_ROM); for (auto& l : lay) l = -1; for (auto& s : stage) s = 4; update(); }
    Params p;
    double songGain = 1;
    double meter[NV] = {};
    void set(int i, double v) { p[(size_t) i] = v; update(); }
    void setAll(const Params& a) { p = a; update(); }
    void setGroup(int g, const std::vector<Layer>& layers) { if (g < 0 || g >= NG_ROM) return; rom[(size_t) g] = layers; for (int v = 0; v < NV; ++v) if (group(v) == g) { act[v] = false; lay[v] = -1; } }
    int group(int v) const { return v == 0 && P(pid::bassChip) >= 0.5 ? G::BASS2 : CH_GROUP[v]; }
    double clock(int v) const { const int k = TUNE_PARAM[v]; if (k < 0) return muxClock(P(pid::mux)); return v <= 2 ? snareClock(P(k)) : tomClock(P(k)); }
    void update() {
        const double norm = mixNorm();
        for (int v = 0; v < NV; ++v) {
            const double g = P(vp(v, vf::mute)) >= 0.5 ? 0 : 1; double ml, mr; mixGain(v, P(vp(v, vf::lv)), 100 - P(vp(v, vf::pan)), ml, mr);
            gL[v] = g * ml * norm; gR[v] = g * mr * norm;
            send[v] = g * P(vp(v, vf::snd));
            const int o = (int) jsRound(P(vp(v, vf::out))); outCh[v] = o >= 1 && o <= 8 ? o - 1 : OUT_DEFAULT[v];
            dk[v] = P(vp(v, vf::dec)) >= 2.999 ? 1 : decayCoef(P(vp(v, vf::dec)) / 3, sr);
            const double cut = P(vp(v, vf::cut)); cutOn[v] = cut < 19999;
            vflt[v].set(cut, 0.707 + P(vp(v, vf::res)) * 9.3, sr);
        }
        double ckl, ckr; mixGain(15, P(pid::clickLv), 100 - P(pid::clickPan), ckl, ckr); cL = ckl * norm; cR = ckr * norm;
        clkA = onePole(1150, sr); clkN = (int) jsRound(1.1e-3 * sr);
        poly = P(pid::poly) >= 0.5; hatMode = (int) jsRound(P(pid::hatMode));
        egK = decayCoef(hatTau(egOpen, P(pid::hatDecay)), sr); egHoldN = (int) jsRound(HAT_HOLD * sr);
        master = masterGain(P(pid::volume)) / masterGain(90) * 0.5;
        lpA = onePole(1 / (2 * PI * 10e3 * 1000e-12), sr);
        dcA = onePole(8, sr);
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
        const double dv = DL_DIVS[std::max(0, std::min(13, (int) jsRound(P(ox::dlSync))))];
        dl.setParams(dv > 0 ? 60 / P(pid::tempo) * dv * 1000 : P(ox::dlTime), P(ox::dlFb), P(ox::dlHiCut), P(ox::dlLoCut), P(ox::dlModRate), P(ox::dlModDepth), P(ox::dlDuck), P(ox::dlPing) >= 0.5);
        dlLevel = P(ox::dlLevel);
    }
    int pick(int g, double f, int dyn) {
        const auto& L = rom[(size_t) g]; const int kit = P(pid::kit) >= 0.5 ? 2 : 1, want = WANT[dyn];
        bool any = false, hasDyn = false, exact = false; int top = -1; const int n = (int) L.size();
        for (int i = 0; i < n; ++i) if (L[(size_t) i].kit == 0 || L[(size_t) i].kit == kit) any = true;
        auto inSet = [&](int i) { return !any || L[(size_t) i].kit == 0 || L[(size_t) i].kit == kit; };
        for (int i = 0; i < n; ++i) if (inSet(i) && L[(size_t) i].dyn >= 0) { hasDyn = true; if (L[(size_t) i].dyn == want) exact = true; if (L[(size_t) i].dyn > top) top = L[(size_t) i].dyn; }
        pickGain = hasDyn && exact ? 1 : dynGain(dyn);
        int best = -1; double bd = 1e9;
        for (int i = 0; i < n; ++i) {
            if (!inSet(i) || (hasDyn && L[(size_t) i].dyn != (exact ? want : top))) continue;
            const double d = std::abs(std::log(f / L[(size_t) i].clk)); if (d < bd) { bd = d; best = i; }
        }
        return best;
    }
    void trigger(int s) {
        if (s < 0 || s >= NS) return;
        const int v = SOUNDS[s].ch, dyn = SOUNDS[s].dyn, g = group(v);
        if (!poly) for (int k = 0; k < NV; ++k) if (k != v && CORE[k] == CORE[v]) act[k] = false;
        if (v == 3) { egOpen = s == S_OPEN; eg = VCC; egHold = egHoldN; egK = decayCoef(hatTau(egOpen, P(pid::hatDecay)), sr); }
        const double f = clock(v); const int li = pick(g, f, dyn);
        if (li < 0) { act[v] = false; return; }
        const Layer& L = rom[(size_t) g][(size_t) li];
        inc[v] = L.rate / sr * (f / L.clk) * std::pow(2.0, P(vp(v, vf::tune)) / 12);
        const double start = std::floor(P(vp(v, vf::start)) * (double) L.data.size());
        const bool keep = v == 3 && lay[v] == li && ((hatMode == 0 && act[v]) || hatMode == 2);
        if (!keep) pos[v] = start;
        lay[v] = li;
        amp[v] = pickGain;
        dec[v] = 1; act[v] = true;
        stage[v] = 0; if (envA[v] < 1) env[v] = 0; else { env[v] = 1; stage[v] = 1; }
    }
    void release(int v) { if (v >= 0 && v < NV) stage[v] = 3; }
    void click(bool first) { clk = clkN; clkX = first ? 2 : 1; }
    void beep() { beepN = (int) jsRound(0.07 * sr); }
    double overdrive(double x) const {
        const double g = 1 + od * 20; double y;
        if (odMode == 1) y = std::max(-1.0, std::min(1.0, x * g)) * 0.8;
        else if (odMode == 2) y = (std::tanh(x * g + 0.3) - std::tanh(0.3)) * 0.85;
        else y = std::tanh(x * g) * 0.85;
        return (x * (1 - odMix) + y * odMix) * odOut;
    }
    double read(int v) {
        const int g = group(v);
        if (lay[v] < 0 || lay[v] >= (int) rom[(size_t) g].size()) { act[v] = false; return 0; }
        const auto& d = rom[(size_t) g][(size_t) lay[v]].data; const long n = (long) d.size();
        double ps = pos[v];
        if (v == 3 && hatMode == 2 && ps >= (double) n) ps -= (double) n * std::floor(ps / (double) n);
        const long i = (long) std::floor(ps);
        if (i >= n) { act[v] = false; return 0; }
        const bool rv_ = P(vp(v, vf::rev)) >= 0.5;
        const double a = d[(size_t) (rv_ ? n - 1 - i : i)], b = i + 1 < n ? d[(size_t) (rv_ ? n - 2 - i : i + 1)] : 0;
        pos[v] = ps + inc[v];
        return a + (b - a) * (ps - (double) i);
    }
    // outs: 16 channels (OUT 1-8 as L/R pairs) or nullptr
    void process(float* L, float* R, int n, float* const* outs) {
        for (int s = 0; s < n; ++s) {
            double l = 0, r = 0, snd = 0, dsnd = 0, grp[16] = {};
            if (egHold > 0) egHold--; else eg *= egK;
            for (int v = 0; v < NV; ++v) {
                if (!act[v] && stage[v] == 4) continue;
                double y = 0;
                if (act[v]) {
                    y = read(v) * amp[v] * dec[v]; dec[v] *= dk[v];
                    if (v == 3) y *= hatVca(eg);
                    tail[v] = 1;
                } else if (tail[v] < 1e-9) { stage[v] = 4; continue; }
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
                if (drv[v] > 0) { const double dd = drv[v]; y = std::tanh(y * (1 + dd * 15)) / (1 + dd); }
                y = eq[v][2].tick(eq[v][1].tick(eq[v][0].tick(y)));
                if (!act[v]) tail[v] = tail[v] * 0.999 + std::abs(y);
                const double ay = std::abs(y); if (ay > meter[v]) meter[v] = ay;
                l += y * gL[v]; r += y * gR[v]; snd += y * send[v]; dsnd += y * dls[v];
                if (outs) { const int o = outCh[v]; grp[o * 2] += y * gL[v]; grp[o * 2 + 1] += y * gR[v]; }
            }
            double rl, rr; rv.tick(snd, rl, rr);
            l += rl * rvLevel; r += rr * rvLevel;
            double dlL, dlR; dl.tick(dsnd, dlL, dlR);
            l += dlL * dlLevel; r += dlR * dlLevel;
            double c = 0;
            if (clk > 0) { clk--; c = clkX; }
            clkHp += clkA * (c - clkHp); const double cy = (c - clkHp) * 0.6;
            l += cy * cL; r += cy * cR;
            if (beepN > 0) { beepN--; beepPh += 2000 / sr; if (beepPh >= 1) beepPh -= 1; const double b = beepPh < 0.5 ? 0.08 : -0.08; l += b; r += b; }
            for (int k = 0; k < 4; ++k) { l = meq[0][k].tick(l); r = meq[1][k].tick(r); }
            if (od > 0) { l = overdrive(l); r = overdrive(r); }
            const double m = master * songGain;
            l *= m; r *= m;
            lpL += lpA * (l - lpL); lpR += lpA * (r - lpR);
            dcL += dcA * (lpL - dcL); dcR += dcA * (lpR - dcR);
            L[s] = (float) std::tanh(lpL - dcL); R[s] = (float) std::tanh(lpR - dcR);
            if (outs) for (int k = 0; k < 16; ++k) outs[k][s] = (float) std::tanh(grp[k] * m);
        }
    }
    const Rom& getRom() const { return rom; }

private:
    double P(int i) const { return p[(size_t) i]; }
    double sr;
    Rom rom;
    bool act[NV] = {}; double pos[NV] = {}, inc[NV] = {}, amp[NV] = {}, dk[NV] = {}, dec[NV] = {}, tail[NV] = {}; int lay[NV];
    bool cutOn[NV] = {};
    SVF vflt[NV];
    Biquad eq[NV][3];
    double env[NV] = {}; int stage[NV]; bool envOn[NV] = {};
    double envA[NV] = {}, envD[NV] = {}, envS[NV] = {}, envR[NV] = {}, drv[NV] = {}, dls[NV] = {};
    double eg = 0, egK = 0; int egHold = 0, egHoldN = 0; bool egOpen = false; double pickGain = 1;
    Biquad meq[2][4];
    Rev7 rv; TCDelay dl;
    int clk = 0, clkN = 0, beepN = 0; double clkX = 0, clkHp = 0, clkA = 0, beepPh = 0, cL = 0, cR = 0;
    double gL[NV] = {}, gR[NV] = {}, send[NV] = {}; int outCh[NV] = {};
    double master = 1, lpA = 0, lpL = 0, lpR = 0, dcA = 0, dcL = 0, dcR = 0, rvLevel = 0, dlLevel = 0;
    bool poly = false; int hatMode = 0;
    double od = 0, odMix = 1, odOut = 1; int odMode = 0;
};

} // namespace linn
