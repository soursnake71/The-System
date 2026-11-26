// Omega Five sound engine: a line-for-line C++17 port of five/engine.js (24 sample voices through the RX5-style
// 8/12-bit PCM playback path, a 12-channel mixer, a REV7-style reverb and a TC 2290-style delay).
// Header only, no JUCE. Every DSP state is a double, outputs are float; the same operations in the same order as the
// JS, so tools/compare.mjs can check this against engine.js sample by sample (build with -ffp-contract=off).
// process() and trigger() never allocate; setUserSample() may (it is not real-time).
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace five {

constexpr int ROM_SR = 25000;           // native rate the voice ROMs are rendered and stored at
constexpr int DAC_SR = 50000;           // the playback DAC's sample-and-hold rate
constexpr int NV = 24, NCH = 12;
constexpr double PI = 3.141592653589793;
constexpr double TWO_PI = 2 * PI;
static const double LN10 = std::log(10.0);
inline double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
// JS Math.round: nearest, halves toward +infinity
inline double jsround(double x) { const double r = std::floor(x); return (x - r >= .5) ? r + 1 : r; }

static const char* const VOICE_NAMES[NV] = { "BD 1", "SD 1", "RIM 1", "TOM 1A", "TOM 2A", "TOM 3A", "TOM 4A", "HH CLS", "RIDE", "CRASH", "TAMB", "SHAKER",
    "BD 2", "SD 2", "RIM 2", "TOM 1B", "TOM 2B", "TOM 3B", "TOM 4B", "HH OPN", "RIDE CP", "CHINA", "CLAPS", "COWBELL" };
static const char* const CHANNEL_NAMES[NCH] = { "BD", "SD", "RIM", "TOM1", "TOM2", "TOM3", "TOM4", "HIHAT", "RIDE", "CRASH", "TMBRN/CLAPS", "SHKR/CWBEL" };

/* ---------------- random numbers: xorshift32 ---------------- */
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    double operator()() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s / 4294967296.0; }
};

/* ---------------- filters ---------------- */
struct OnePole {
    double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
    OnePole& lp(double fs, double f) { const double k = std::tan(PI * clampd(f, 1, fs * .49) / fs); b0 = k / (1 + k); b1 = b0; a1 = (k - 1) / (k + 1); return *this; }
    OnePole& hp(double fs, double f) { const double k = std::tan(PI * clampd(f, 1, fs * .49) / fs); b0 = 1 / (1 + k); b1 = -b0; a1 = (k - 1) / (k + 1); return *this; }
    double run(double x) { const double y = b0 * x + b1 * x1 - a1 * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = 0; y1 = 0; }
};
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Type { LP, HP, BP };
    Biquad& set(Type type, double fs, double f, double q) {
        const double w = TWO_PI * clampd(f, 1, fs * .49) / fs, cw = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        double c0, c1, c2;
        if (type == LP) { c0 = (1 - cw) / 2; c1 = 1 - cw; c2 = c0; }
        else if (type == HP) { c0 = (1 + cw) / 2; c1 = -(1 + cw); c2 = c0; }
        else { c0 = al; c1 = 0; c2 = -al; }
        b0 = c0 / a0; b1 = c1 / a0; b2 = c2 / a0; a1 = -2 * cw / a0; a2 = (1 - al) / a0;
        return *this;
    }
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = 0; z2 = 0; }
};

// No drum is synthesized (repo rule): every voice is a sample (the factory kit of real RX5 recordings, or the user's own)
// loaded with setUserSample(); a voice without one is silent.

// the factory kit: voice v plays five/kit/FACTORY_MAP[v] (25 kHz 16-bit mono recordings of a real RX5), loaded as 'rec'
static const char* const FACTORY_MAP[NV] = { "BDrum1-RX5.wav", "SDrum1-RX5.wav", "Rim1-RX5.wav", "Tom1-RX5.wav", "Tom2-RX5.wav", "Tom3-RX5.wav", "Tom4-RX5.wav",
    "CHat-RX5.wav", "RideEdge-RX5.wav", "Crash-RX5.wav", "Tamb-RX5.wav", "Shaker-RX5.wav", "BDrum2-RX5.wav", "SDrum2-RX5.wav", "Rim2-RX5.wav",
    "ETom1-RX5.wav", "ETom2-RX5.wav", "ETom3-RX5.wav", "ETom4-RX5.wav", "OHat-RX5.wav", "RideBell-RX5.wav", "China-RX5.wav", "Clap-RX5.wav", "Cowbell-RX5.wav" };

// recording pre-emphasis: a 7-tap linear-phase FIR at 25 kHz that undoes this path's playback droop (applied once to a 'rec' ROM)
static const double REC_EQ[7] = { -.0426, .1175, -.2817, 1.4136, -.2817, .1175, -.0426 };
inline std::vector<float> recEq(const std::vector<float>& x) {
    const long n = (long) x.size(), h = 3; std::vector<float> y((size_t) n);
    for (long i = 0; i < n; i++) {
        double s = 0;
        for (long k = 0; k < 7; k++) { const long j = i + k - h; if (j >= 0 && j < n) s += REC_EQ[k] * x[(size_t) j]; }
        y[(size_t) i] = (float) s;
    }
    return y;
}

/* ---------------- the ROM word ---------------- */
inline void quantizeRom(const std::vector<float>& x, int bits, bool floatMode, std::vector<float>& out) {
    const size_t n = x.size(); const double M = std::pow(2.0, bits - 1);
    out.resize(n);
    for (size_t b = 0; b < n; b += 64) {
        const size_t e = std::min(n, b + 64);
        int ex = 0;
        if (floatMode) { double pk = 0; for (size_t i = b; i < e; i++) { const double a = std::fabs((double) x[i]); if (a > pk) pk = a; } ex = pk >= .5 ? 0 : pk >= .25 ? 1 : pk >= .125 ? 2 : 3; }
        const double step = std::pow(2.0, -ex) / M;
        for (size_t i = b; i < e; i++) out[i] = (float) (clampd(std::floor((double) x[i] / step + .5), -M, M - 1) * step);
    }
}

/* ---------------- parameters ---------------- */
// voice parameter ids (names as in engine.js)
enum VoiceParam { vPitch, vLevel, vAr, vD1r, vD1l, vD2r, vRr, vGate, vBendRate, vBendRange, vReverse, vLoop, vAcc1, vAcc2, vBits, vCount };
static const char* const VOICE_PARAM_NAMES[vCount] = { "pitch", "level", "ar", "d1r", "d1l", "d2r", "rr", "gate", "bendRate", "bendRange", "reverse", "loop", "acc1", "acc2", "bits" };
static const double VOICE_RANGES[vCount][2] = { {-3600, 2400}, {0, 31}, {1, 99}, {0, 99}, {0, 60}, {0, 99}, {1, 99}, {0, 6500}, {0, 60}, {-6000, 6000}, {0, 1}, {0, 1}, {-31, 31}, {-31, 31}, {8, 12} };
struct VoiceParams { double v[vCount]; bool rec = false; double& operator[](int i) { return v[i]; } double operator[](int i) const { return v[i]; } };
inline VoiceParams voiceDefault(int voice) {
    VoiceParams p; const double d[vCount] = { 0, 25, 99, 0, 60, 0, 45, 0, 0, 0, 0, 0, 2, 4, 12 };
    std::memcpy(p.v, d, sizeof d); if (voice == 19) p.v[vRr] = 70; return p;
}
inline int voiceParamIndex(const std::string& n) { for (int i = 0; i < vCount; i++) if (n == VOICE_PARAM_NAMES[i]) return i; return -1; }

enum ChannelParam { cFader, cPan, cSendRev, cSendDly, cMute, cCount };
static const char* const CHANNEL_PARAM_NAMES[cCount] = { "fader", "pan", "sendRev", "sendDly", "mute" };
static const double CHANNEL_RANGES[cCount][2] = { {0, 1}, {-1, 1}, {0, 1}, {0, 1}, {0, 1} };
struct ChannelParams { double fader = 1, pan = 0, sendRev = 0, sendDly = 0; bool mute = false; };
inline ChannelParams channelDefault(int ch) { static const int POS[NCH] = { 8, 9, 9, 12, 10, 6, 4, 13, 3, 11, 5, 8 }; ChannelParams c; c.pan = (8 - POS[ch]) / 7.0; return c; }

enum RevParam { rProgram, rTime, rHiRatio, rDelay, rDiffusion, rDensity, rErLevel, rRevLevel, rHpf, rLpf, rGateTime, rLevel, rCount };
static const char* const REV_PARAM_NAMES[rCount] = { "program", "time", "hiRatio", "delay", "diffusion", "density", "erLevel", "revLevel", "hpf", "lpf", "gateTime", "level" };
static const double REV_RANGES[rCount][2] = { {0, 10}, {.3, 30}, {.1, 1}, {0, 200}, {0, 10}, {0, 100}, {0, 1}, {0, 1}, {20, 1000}, {1000, 16000}, {10, 500}, {0, 1} };
static const double REV_DEFAULTS[rCount] = { 0, 2.6, .6, 30, 8, 80, .45, .8, 20, 12000, 200, 1 };
inline int revParamIndex(const std::string& n) { for (int i = 0; i < rCount; i++) if (n == REV_PARAM_NAMES[i]) return i; return -1; }

enum DlyParam { dOn, dTimeL, dTimeR, dSync, dTempo, dFeedback, dPingpong, dHiCut, dLoCut, dModRate, dModDepth, dDuckThresh, dDuckRelease, dDuckDepth, dSpread, dMix, dCount };
static const char* const DLY_PARAM_NAMES[dCount] = { "on", "timeL", "timeR", "sync", "tempo", "feedback", "pingpong", "hiCut", "loCut", "modRate", "modDepth", "duckThresh", "duckRelease", "duckDepth", "spread", "mix" };
static const double DLY_RANGES[dCount][2] = { {0, 1}, {1, 2000}, {1, 2000}, {0, 5}, {40, 250}, {0, 120}, {0, 1}, {1000, 20000}, {20, 1000}, {.05, 10}, {0, 1}, {-60, 0}, {10, 2000}, {0, 40}, {0, 1}, {0, 1} };
static const double DLY_DEFAULTS[dCount] = { 1, 375, 500, 0, 120, 35, 0, 6000, 120, .6, .1, 0, 300, 12, 1, .6 };
static const char* const DLY_SYNC[6] = { "off", "1/4", "1/8", "1/8.", "1/16", "1/4T" };
static const double SYNC_DIV[6] = { 0, 1, .5, .75, .25, 2.0 / 3 };
inline int dlyParamIndex(const std::string& n) { for (int i = 0; i < dCount; i++) if (n == DLY_PARAM_NAMES[i]) return i; return -1; }

/* =====================================================================================================================
   Playback slot
   ===================================================================================================================== */
constexpr double EG_FLOOR = 1e-4, FADE_STEP = 1 / (.002 * DAC_SR), DAC_HEAD = 1.45, OUT_K = 2;
inline double atkTime(double r) { return 2 * std::pow(2.0, -r / 8); }
inline double t60(double r) { return 12 * std::pow(2.0, -r / 8.5); }
inline double decCoef(double r) { return r <= 0 ? 1 : std::exp(-3 * LN10 / (t60(r) * DAC_SR)); }
inline double relCoef(double r) { return std::exp(-3 * LN10 / (t60(r) * DAC_SR)); }
inline double egLevel(double l) { return l >= 60 ? 1 : l <= 0 ? 0 : std::pow(10.0, -(60 - l) * 1.2 / 20); }
inline double levelGain(double l) { return l <= 0 ? 0 : std::pow(10.0, (l - 31) * 1.5 / 20); }

struct Slot {
    bool active = false; int v = -1; const float* rom = nullptr; int len = 0; double pos = 0, inc = 0, baseInc = 0;
    double pitch = 0, bend = 0, bendT = 0, bendC = 0; bool rev = false, loop = false; double ll = 0;
    int stage = 0; double lvl = 0, atk = 0, L1 = 1, c1 = 1, c2 = 1, cr = 1; double gateN = 0, n = 0;
    double gain = 0, fade = 1; bool rec = false;
    void start(int v_, const std::vector<float>& r, const VoiceParams& p, int vel, int acc, bool rec_, double pitchOff = 0) {
        v = v_; rom = r.data(); len = (int) r.size(); pos = 0; rec = rec_;
        rev = p[vReverse] >= .5; loop = p[vLoop] >= .5; ll = (len - 1) - std::floor(len * .7);
        const double pc = pitchOff != 0 ? clampd(p[vPitch] + pitchOff, -3600, 2400) : p[vPitch];
        baseInc = (double) ROM_SR / DAC_SR * std::pow(2.0, pc / 1200); inc = baseInc;
        pitch = pc; bend = 0; bendT = p[vBendRange];
        bendC = p[vBendRate] > 0 && p[vBendRange] != 0 ? 25 * std::pow(2.0, p[vBendRate] / 6) / DAC_SR : 0;
        stage = p[vAr] >= 99 ? 1 : 0; lvl = stage ? 1 : 0; atk = p[vAr] >= 99 ? 1 : 1 / (atkTime(p[vAr]) * DAC_SR);
        L1 = egLevel(p[vD1l]); c1 = decCoef(p[vD1r]); c2 = decCoef(p[vD2r]); cr = relCoef(p[vRr]);
        gateN = p[vGate] > 0 ? jsround(p[vGate] * DAC_SR / 1000) : 0; n = 0;
        const double lv = clampd(p[vLevel] + (acc == 1 ? p[vAcc1] : acc == 2 ? p[vAcc2] : 0), 0, 31);
        gain = DAC_HEAD * levelGain(lv) * (vel / 127.0);
        fade = 1; active = len >= 2;
    }
    double tick() {
        const int L = len; const float* R = rom; const double q = rev ? (L - 1) - pos : pos;
        const double fi = std::floor(q); const int i = (int) fi; const double f = q - fi, a = R[i], b = i + 1 < L ? R[i + 1] : 0;
        const double s = (a + (b - a) * f) * lvl * gain;
        n++;
        if (gateN > 0 && stage < 3 && n >= gateN) stage = 3;
        if (stage == 0) { lvl += atk; if (lvl >= 1) { lvl = 1; stage = 1; } }
        else if (stage == 1) {
            if (lvl <= L1) stage = 2;
            else if (c1 < 1) { lvl *= c1; if (lvl <= L1) { lvl = L1; stage = 2; } }
        }
        else if (stage == 2) { if (c2 < 1) { lvl *= c2; if (lvl < EG_FLOOR) active = false; } }
        else { lvl *= cr; if (lvl < EG_FLOOR) active = false; }
        if (bendC > 0) {
            if (bendT > bend) { bend += bendC; if (bend >= bendT) { bend = bendT; bendC = 0; } }
            else { bend -= bendC; if (bend <= bendT) { bend = bendT; bendC = 0; } }
            inc = (double) ROM_SR / DAC_SR * std::pow(2.0, clampd(pitch + bend, -3600, 2400) / 1200);
        }
        pos += inc;
        if (pos >= L - 1) {
            if (loop && ll > 0) { while (pos >= L - 1) pos -= ll; }
            else active = false;
        }
        return s;
    }
};

/* =====================================================================================================================
   REV7-style reverb
   ===================================================================================================================== */
struct RevProgram { const char* name; int type; double size, erSpan; int erTaps; double time, hiRatio, delay, diffusion, density, erLevel, revLevel, hpf, lpf, gateTime; };
static const RevProgram REV_PROGRAMS[11] = {
    { "HALL1", 0, 1.35, 80, 12, 2.6, .6, 30, 8, 80, .45, .8, 20, 12000, 200 },
    { "HALL2", 0, 1.8, 110, 14, 4.2, .5, 45, 9, 90, .4, .8, 40, 10000, 200 },
    { "ROOM1", 0, .6, 35, 10, .9, .7, 8, 7, 70, .6, .7, 20, 14000, 200 },
    { "ROOM2", 0, .8, 50, 12, 1.5, .55, 15, 8, 80, .55, .75, 60, 11000, 200 },
    { "VOCAL", 0, 1, 50, 10, 2.2, .45, 60, 6, 60, .35, .8, 100, 9000, 200 },
    { "PLATE1", 0, .9, 0, 0, 2, .8, 10, 10, 100, 0, .85, 20, 16000, 200 },
    { "PLATE2", 0, 1.1, 0, 0, 3.2, .7, 25, 10, 100, 0, .85, 80, 12000, 200 },
    { "ER1", 1, .7, 70, 16, .6, .6, 5, 5, 60, 1, .12, 20, 14000, 200 },
    { "ER2", 1, 1, 140, 10, .8, .6, 20, 3, 40, 1, .1, 20, 12000, 200 },
    { "GATE", 2, 1, 0, 32, 1, .8, 0, 8, 100, 0, 1, 40, 12000, 180 },
    { "REV GATE", 3, 1, 0, 32, 1, .8, 0, 8, 100, 0, 1, 40, 12000, 250 },
};
constexpr int NUM_REV_PROGRAMS = 11;
inline double programValue(const RevProgram& p, int k) {
    switch (k) { case rTime: return p.time; case rHiRatio: return p.hiRatio; case rDelay: return p.delay; case rDiffusion: return p.diffusion; case rDensity: return p.density;
        case rErLevel: return p.erLevel; case rRevLevel: return p.revLevel; case rHpf: return p.hpf; case rLpf: return p.lpf; case rGateTime: return p.gateTime; default: return 0; }
}
static const double AP_REF = 29761; static const int AP_LEN[4] = { 142, 107, 379, 277 };
static const double FDN_MS[8] = { 29.7, 37.1, 41.1, 43.7, 53.0, 59.9, 67.7, 73.1 };
static const double FDN_IN[8] = { 1, -1, 1, -1, 1, -1, 1, -1 }, FDN_L[8] = { 1, 1, -1, 1, -1, -1, 1, -1 }, FDN_R[8] = { 1, -1, 1, 1, -1, 1, -1, -1 };
constexpr int MAX_TAPS = 32; constexpr double REV_SR = 31250;
inline double q16(double x) { return clampd(std::floor(x * 32768 + .5), -32768, 32767) / 32768; }

class Rev7 {
public:
    double sr; int L; std::vector<double> inBuf, dfBuf; int w = 0;
    std::vector<double> apBuf[4]; int apN[4] = {0, 0, 0, 0}, apI[4] = {0, 0, 0, 0}; double apG = 0;
    int DL; std::vector<double> lines[8]; int dw = 0;
    double len[8] = {}, fb[8] = {}, fp[8] = {}, z[8] = {};
    int tapT[MAX_TAPS] = {}; double tapL[MAX_TAPS] = {}, tapR[MAX_TAPS] = {}; int nT = 0;
    Biquad aa, hpf, lpf, rcL, rcR;
    double shInc, shPh = 0, sh = 0;
    double lcw, lsw, lc = 1, ls = 0, depth;
    double oL = 0, oR = 0;
    int type = 0, pd = 0;
    double p[rCount];
    explicit Rev7(double sr_) : sr(sr_) {
        L = (int) std::ceil(.75 * sr) + 4; inBuf.assign((size_t) L, 0); dfBuf.assign((size_t) L, 0);
        for (int i = 0; i < 4; i++) apBuf[i].assign((size_t) (std::ceil(AP_LEN[i] * sr / AP_REF) + 2), 0);
        DL = (int) std::ceil(.21 * sr) + 8; for (auto& b : lines) b.assign((size_t) DL, 0);
        aa.set(Biquad::LP, sr, std::min(13500.0, .45 * sr), .707);
        rcL.set(Biquad::LP, sr, std::min(13500.0, .45 * sr), .707); rcR.set(Biquad::LP, sr, std::min(13500.0, .45 * sr), .707);
        shInc = REV_SR / sr;
        const double wv = TWO_PI * .55 / sr; lcw = std::cos(wv); lsw = std::sin(wv); depth = .00035 * sr;
        std::memcpy(p, REV_DEFAULTS, sizeof p); setProgram(0);
    }
    void setProgram(double val) {
        int i = (int) clampd((double) (int) jsround(val), 0, NUM_REV_PROGRAMS - 1); p[rProgram] = i;
        for (int k = rTime; k <= rGateTime; k++) p[k] = programValue(REV_PROGRAMS[i], k);
        rebuild();
    }
    void setProgram(const std::string& name) {
        int i = -1; for (int k = 0; k < NUM_REV_PROGRAMS; k++) { std::string u = name; for (auto& c : u) c = (char) std::toupper((unsigned char) c); if (u == REV_PROGRAMS[k].name) i = k; }
        setProgram((double) (i < 0 ? 0 : i));   // JS: findIndex -1 -> clamp(-1|0) -> 0
    }
    void set(int k, double val) {
        if (k == rProgram) { setProgram(val); return; }
        if (k < 0 || k >= rCount || !std::isfinite(val)) return;
        p[k] = clampd(val, REV_RANGES[k][0], REV_RANGES[k][1]);
        rebuild();
    }
    void rebuild() {
        const RevProgram& pr = REV_PROGRAMS[(int) p[rProgram]];
        type = pr.type;
        hpf.set(Biquad::HP, sr, p[rHpf], .707); lpf.set(Biquad::LP, sr, p[rLpf], .707);
        pd = (int) jsround(p[rDelay] * sr / 1000);
        apG = .07 * p[rDiffusion];
        for (int i = 0; i < 4; i++) { apN[i] = (int) std::max(1.0, jsround(AP_LEN[i] * sr / AP_REF)); if (apI[i] >= apN[i]) apI[i] = 0; }
        const double scale = pr.size * (1.25 - .5 * p[rDensity] / 100);
        for (int i = 0; i < 8; i++) {
            const double n = jsround(FDN_MS[i] * scale * sr / 1000), g = std::pow(10.0, -3 * n / (p[rTime] * sr)), gh = std::pow(10.0, -3 * n / (p[rTime] * p[rHiRatio] * sr));
            len[i] = n; fp[i] = (g - gh) / (g + gh); fb[i] = g * (1 - fp[i]);
        }
        Rng R((uint32_t) (101 + (int) p[rProgram])); const int N = pr.type >= 2 ? MAX_TAPS : pr.erTaps;
        double e = 0;
        for (int k = 0; k < N; k++) {
            double t, g;
            if (pr.type < 2) { t = pr.erSpan * std::pow((double) (k + 1) / N, 1.3) * (.85 + .3 * R()); g = 1 - .7 * k / N; }
            else { t = p[rGateTime] * (k + .8 * R()) / N; g = pr.type == 2 ? (k < .85 * N ? 1 : (N - k) / (.15 * N)) : .08 + .92 * std::pow((double) k / N, 1.5); }
            const double s = R() < .5 ? -1 : 1; const double a = R();
            tapT[k] = (int) std::max(1.0, jsround(t * sr / 1000));
            tapL[k] = s * g * (.3 + .7 * a); tapR[k] = s * g * (1 - .7 * a);
            e += g * g;
        }
        const double nrm = e > 0 ? 1 / std::sqrt(e) : 0;
        for (int k = 0; k < N; k++) { tapL[k] *= nrm; tapR[k] *= nrm; }
        nT = N;
    }
    void reset() {
        std::fill(inBuf.begin(), inBuf.end(), 0.0); std::fill(dfBuf.begin(), dfBuf.end(), 0.0);
        for (auto& b : apBuf) std::fill(b.begin(), b.end(), 0.0);
        for (auto& b : lines) std::fill(b.begin(), b.end(), 0.0);
        for (double& x : z) x = 0;
        aa.reset(); hpf.reset(); lpf.reset(); rcL.reset(); rcR.reset(); sh = 0; oL = oR = 0;
    }
    void process(double x) {
        const int w0 = w;
        const double a = aa.run(x);
        shPh += shInc; if (shPh >= 1) { shPh -= std::floor(shPh); sh = q16(a); }
        inBuf[(size_t) w0] = lpf.run(hpf.run(sh));
        int r = w0 - pd; if (r < 0) r += L;
        double d = inBuf[(size_t) r];
        const double g = apG;
        for (int i = 0; i < 4; i++) {
            auto& b = apBuf[i]; const int j = apI[i]; const double v = b[(size_t) j], s = d - g * v;
            b[(size_t) j] = s; d = v + g * s;
            apI[i] = j + 1 >= apN[i] ? 0 : j + 1;
        }
        dfBuf[(size_t) w0] = d;
        double eL = 0, eR = 0;
        const bool gate = type >= 2; const std::vector<double>& src = gate ? dfBuf : inBuf; const int base = gate ? 0 : pd;
        for (int k = 0; k < nT; k++) { int j = w0 - base - tapT[k]; if (j < 0) j += L; const double s = src[(size_t) j]; eL += s * tapL[k]; eR += s * tapR[k]; }
        double fL = 0, fR = 0;
        if (!gate) {
            const double c = lc, sn = ls; const int dw0 = dw;
            lc = c * lcw - sn * lsw; ls = sn * lcw + c * lsw;
            double sum = 0;
            for (int i = 0; i < 8; i++) {
                const double m = ((i & 1) ? sn : c) * ((i & 2) ? -depth : depth);
                double pos = dw0 - (len[i] + m); if (pos < 0) pos += DL;
                const double fi0 = std::floor(pos); const int i0 = (int) fi0; const double f = pos - fi0; const int i1 = i0 + 1 >= DL ? 0 : i0 + 1; const auto& B = lines[i];
                const double y = B[(size_t) i0] + (B[(size_t) i1] - B[(size_t) i0]) * f;
                const double zz = fb[i] * y + fp[i] * z[i]; z[i] = zz;
                sum += zz; fL += zz * FDN_L[i]; fR += zz * FDN_R[i];
            }
            const double h = sum * .25;
            for (int i = 0; i < 8; i++) lines[i][(size_t) dw0] = z[i] - h + d * FDN_IN[i] * .5;
            dw = dw0 + 1 >= DL ? 0 : dw0 + 1;
        }
        w = w0 + 1 >= L ? 0 : w0 + 1;
        const double eg = gate ? p[rRevLevel] : p[rErLevel], rg = p[rRevLevel] * .35;
        oL = rcL.run(q16(eL * eg + fL * rg));
        oR = rcR.run(q16(eR * eg + fR * rg));
    }
};

/* =====================================================================================================================
   TC 2290-style delay
   ===================================================================================================================== */
class TcDelay {
public:
    double sr; int L; std::vector<double> bL, bR; int w = 0;
    OnePole lpL, lpR, hpL, hpR;
    double lc = 1, ls = 0, env = 0, dg = 1, oL = 0, oR = 0;
    double envC, atkC, smC;
    double p[dCount];
    double depth = 0, tL = 0, tR = 0, lcw = 1, lsw = 0, fbk = 0, thr = 1, duckG = 1, relC = 0, dL = 0, dR = 0;
    explicit TcDelay(double sr_) : sr(sr_) {
        L = (int) std::ceil(2.5 * sr) + 4; bL.assign((size_t) L, 0); bR.assign((size_t) L, 0);
        envC = std::exp(-1 / (.01 * sr)); atkC = 1 - std::exp(-1 / (.002 * sr)); smC = 1 - std::exp(-1 / (.06 * sr));
        std::memcpy(p, DLY_DEFAULTS, sizeof p); rebuild(); dL = tL; dR = tR;
    }
    void set(int k, double val) {
        if (k < 0 || k >= dCount || !std::isfinite(val)) return;
        const double was = p[dOn];
        p[k] = clampd(val, DLY_RANGES[k][0], DLY_RANGES[k][1]);
        if (k == dSync) p[dSync] = jsround(p[dSync]);
        if (k == dOn && p[dOn] >= .5 && was < .5) reset();
        rebuild();
    }
    void setSync(const std::string& s) { int i = 0; for (int k = 0; k < 6; k++) if (s == DLY_SYNC[k]) i = k; set(dSync, i); }
    void rebuild() {
        const double sync = SYNC_DIV[(int) p[dSync]];
        const double msL = sync != 0 ? 60000 / p[dTempo] * sync : p[dTimeL], msR = sync != 0 ? 60000 / p[dTempo] * sync : p[dTimeR];
        depth = p[dModDepth] * 5 * sr / 1000;
        const double mx = L - 4 - depth;
        tL = clampd(msL * sr / 1000, 1, mx); tR = clampd(msR * sr / 1000, 1, mx);
        const double wv = TWO_PI * p[dModRate] / sr; lcw = std::cos(wv); lsw = std::sin(wv);
        lpL.lp(sr, p[dHiCut]); lpR.lp(sr, p[dHiCut]); hpL.hp(sr, p[dLoCut]); hpR.hp(sr, p[dLoCut]);
        fbk = p[dFeedback] / 100; thr = std::pow(10.0, p[dDuckThresh] / 20); duckG = std::pow(10.0, -p[dDuckDepth] / 20);
        relC = 1 - std::exp(-1 / (p[dDuckRelease] / 1000 * sr));
    }
    void reset() { std::fill(bL.begin(), bL.end(), 0.0); std::fill(bR.begin(), bR.end(), 0.0); lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset(); env = 0; dg = 1; dL = tL; dR = tR; }
    double read(const std::vector<double>& B, double d) const {
        double pos = w - d; if (pos < 0) pos += L;
        const double fi0 = std::floor(pos); const int i0 = (int) fi0; const double f = pos - fi0; const int i1 = i0 + 1 >= L ? 0 : i0 + 1;
        return B[(size_t) i0] + (B[(size_t) i1] - B[(size_t) i0]) * f;
    }
    void process(double xL, double xR) {
        if (p[dOn] < .5) { oL = 0; oR = 0; return; }
        const double k = std::max(std::fabs(xL), std::fabs(xR));
        env = k > env ? k : env * envC;
        const double tg = p[dDuckThresh] < 0 && env > thr ? duckG : 1;
        dg += (tg - dg) * (tg < dg ? atkC : relC);
        dL += (tL - dL) * smC; dR += (tR - dR) * smC;
        const double c = lc, s = ls; lc = c * lcw - s * lsw; ls = s * lcw + c * lsw;
        const double yL = read(bL, dL + depth * .5 * (1 + s)), yR = read(bR, dR + depth * .5 * (1 + c));
        const double fL = hpL.run(lpL.run(yL)) * fbk, fR = hpR.run(lpR.run(yR)) * fbk;
        double wL, wR;
        if (p[dPingpong] >= .5) { wL = .5 * (xL + xR) + fR; wR = fL; } else { wL = xL + fL; wR = xR + fR; }
        bL[(size_t) w] = std::tanh(wL); bR[(size_t) w] = std::tanh(wR);
        w = w + 1 >= L ? 0 : w + 1;
        const double g = p[dMix] * dg, m = (yL + yR) * .5, sd = (yL - yR) * .5 * p[dSpread];
        oL = (m + sd) * g; oR = (m - sd) * g;
    }
};

/* =====================================================================================================================
   Engine
   ===================================================================================================================== */
constexpr int QMAX = 256;
class Engine {
public:
    double sr; bool dacFloat; int K; double dacInc, dacPh = 0; std::vector<uint8_t> tk;
    std::vector<float> src[NV], roms[NV]; bool hasUser[NV] = {}; uint8_t recData[NV] = {};
    std::vector<float> user[NV];
    Slot cur[NCH], old[NCH]; OnePole lp1[NCH]; Biquad lp2[NCH]; OnePole hp[NCH];
    double held[NCH] = {}; uint8_t on[NCH] = {}; double peak[NCH] = {};
    double gC[NCH] = {}, pL[NCH] = {}, pR[NCH] = {}, sRev[NCH] = {}, sDly[NCH] = {};
    double pkDec;
    int qT[QMAX], qType[QMAX], qV[QMAX], qVel[QMAX], qAcc[QMAX], qCh[QMAX]; double qPitch[QMAX]; int qN = 0;
    Rev7 revFx; TcDelay dlyFx;
    double ckN = 0, ckPh = 0, ckW = 0, ckEnv = 0, ckDec, ckA = 1;
    double mL = 0, mR = 0;
    VoiceParams voices[NV]; ChannelParams channels[NCH]; double volume = 1, click = .5;
    double volG = 1, clkG = 0;
    uint8_t direct[NCH] = {};   // 1: the channel goes only to its individual output (not in engine.js; sends still apply)

    explicit Engine(double sr_, bool floatDac = false) : sr(sr_ > 0 ? sr_ : 48000), dacFloat(floatDac), revFx(sr), dlyFx(sr) {
        K = (int) std::max(1.0, jsround(192000 / sr)); dacInc = DAC_SR / (sr * K); tk.assign((size_t) K, 0);
                const double fo = sr * K, fc = std::min(12000.0, .45 * fo);
        for (int ch = 0; ch < NCH; ch++) { lp1[ch].lp(fo, fc); lp2[ch].set(Biquad::LP, fo, fc, 1); hp[ch].hp(sr, 8); }
        pkDec = std::exp(-1 / (.15 * sr));
        ckDec = std::exp(-1 / (.008 * sr));
        for (int v = 0; v < NV; v++) voices[v] = voiceDefault(v);
        for (int ch = 0; ch < NCH; ch++) channels[ch] = channelDefault(ch);
        for (int v = 0; v < NV; v++) requant(v);
        for (int ch = 0; ch < NCH; ch++) mixCh(ch);
        mixMaster();
    }
    void mixCh(int ch) {
        const ChannelParams& c = channels[ch]; const double a = (c.pan + 1) * PI / 4;
        gC[ch] = c.mute ? 0 : c.fader * c.fader; pL[ch] = std::cos(a); pR[ch] = std::sin(a);
        sRev[ch] = c.sendRev; sDly[ch] = c.sendDly;
    }
    void mixMaster() { volG = volume * volume; clkG = click * click * .5; }

    /* ---------------- control ---------------- */
    void push(int type, int v, int vel, int acc, double time, double pitchOff = 0, int ch = -1) {
        if (qN >= QMAX) return;
        const int k = qN++;
        qPitch[k] = pitchOff; qCh[k] = ch; qType[k] = type; qV[k] = v; qVel[k] = vel; qAcc[k] = acc; qT[k] = (int) std::max(0.0, std::floor(time));
    }
    // pitchOff (cents, not in engine.js): Tunable Note mode and key-assign set-ups; ch: the output channel 0..11
    // (key assign OUTPUT CH), default the voice's own, v % 12
    void trigger(int v, double vel = 127, int accent = 0, double time = 0, double pitchOff = 0, int ch = -1) {
        if (v < 0 || v >= NV || !(vel > 0)) return;
        push(0, v, (int) clampd(jsround(vel), 1, 127), (int) clampd(accent, 0, 2), time, pitchOff, ch >= 0 && ch < NCH ? ch : -1);
    }
    void release(int v, double time = 0) { if (v >= 0 && v < NV) push(1, v, 0, 0, time); }
    void clickTick(bool accented, double time = 0) { push(2, 0, 0, accented ? 1 : 0, time); }
    void fire(int k) {
        const int type = qType[k], v = qV[k];
        if (type == 0) {
            const int ch = qCh[k] >= 0 ? qCh[k] : v % NCH; Slot& a = cur[ch]; Slot& b = old[ch];
            if (a.active) { b = a; b.fade = 1; }
            a.start(v, roms[v], voices[v], qVel[k], qAcc[k], isRec(v), qPitch[k]);
            on[ch] = 1;
        } else if (type == 1) {
            for (int ch = 0; ch < NCH; ch++) { Slot& a = cur[ch]; if (a.active && a.v == v) a.stage = 3; }
        } else {
            const double f = qAcc[k] ? 2000 : 1000;
            ckN = jsround(.03 * sr); ckPh = 0; ckW = TWO_PI * f / sr; ckEnv = 1; ckA = qAcc[k] ? 1 : .6;
        }
    }
    bool isRec(int v) const { return voices[v].rec && recData[v] == 1; }
    void requant(int v) { if (isRec(v)) roms[v] = src[v]; else quantizeRom(src[v], (int) voices[v][vBits], dacFloat, roms[v]); }
    void setPath(int v, bool rec) { if (v < 0 || v >= NV) return; if (rec != voices[v].rec) { voices[v].rec = rec; cut(v); requant(v); } }
    void cut(int v) { for (int ch = 0; ch < NCH; ch++) { if (cur[ch].v == v) cur[ch].active = false; if (old[ch].v == v) old[ch].active = false; } }
    void setVoiceParam(int v, int k, double value) {
        if (k < 0 || k >= vCount || v < 0 || v >= NV || !std::isfinite(value)) return;
        double x = clampd(value, VOICE_RANGES[k][0], VOICE_RANGES[k][1]);
        if (k == vGate && x > 0) x = std::max(100.0, x);
        if (k == vBits) { x = x < 10 ? 8 : 12; if (x != voices[v][vBits]) { voices[v][vBits] = x; cut(v); requant(v); } return; }
        voices[v][k] = x;
    }
    void setChannelParam(int ch, int k, double value) {
        if (ch < 0 || ch >= NCH || k < 0 || k >= cCount) return;
        ChannelParams& c = channels[ch];
        if (k == cMute) c.mute = value != 0;
        else if (std::isfinite(value)) { const double x = clampd(value, CHANNEL_RANGES[k][0], CHANNEL_RANGES[k][1]); if (k == cFader) c.fader = x; else if (k == cPan) c.pan = x; else if (k == cSendRev) c.sendRev = x; else c.sendDly = x; }
        mixCh(ch);
    }
    void setDac(bool f) { dacFloat = f; for (int v = 0; v < NV; v++) { cut(v); requant(v); } }
    void setTempo(double bpm) { dlyFx.set(dTempo, bpm); }
    void setRev(int k, double v) { revFx.set(k, v); }
    void setDly(int k, double v) { dlyFx.set(k, v); }
    void setVolume(double v) { if (std::isfinite(v)) volume = clampd(v, 0, 1); mixMaster(); }
    void setClick(double v) { if (std::isfinite(v)) click = clampd(v, 0, 1); mixMaster(); }
    // user sample: linear resample to ROM_SR, quantized like the factory ROM; empty restores the synthesized voice (not real-time)
    void setUserSample(int v, const float* data, size_t length, double ssr, bool rec = false) {
        if (v < 0 || v >= NV) return;
        cut(v);
        rec = rec && data && length >= 2; recData[v] = rec ? 1 : 0; voices[v].rec = rec;
        if (!data || length < 2) { hasUser[v] = false; user[v].clear(); src[v].clear(); }
        else {
            const size_t n = (size_t) std::max(2.0, std::floor((length - 1) * (double) ROM_SR / ssr) + 1); std::vector<float> out(n); const double step = ssr / ROM_SR;
            for (size_t k = 0; k < n; k++) {
                const double x = k * step, fi = std::floor(x); const size_t i = (size_t) fi; const double f = x - fi, a = data[i], b = i + 1 < length ? data[i + 1] : a;
                out[k] = (float) clampd(a + (b - a) * f, -1, 1);
            }
            user[v] = out; hasUser[v] = true; src[v] = rec ? recEq(out) : out;
        }
        roms[v].assign(src[v].size(), 0.f); requant(v);
    }
    // the same as setUserSample, in two steps for the plugin: prepare() resamples (allocates, any thread, no engine
    // state touched), install() swaps the buffers in (no allocation; the old ones come back in `p` to be freed later)
    struct Prepared { std::vector<float> user, src, rom; bool rec = false, ok = false; };
    static Prepared prepare(const float* data, size_t length, double ssr, bool rec, int bits, bool floatDac) {
        Prepared p; p.rec = rec && data && length >= 2; p.ok = data && length >= 2;
        if (p.ok) {
            const size_t n = (size_t) std::max(2.0, std::floor((length - 1) * (double) ROM_SR / ssr) + 1); p.user.resize(n); const double step = ssr / ROM_SR;
            for (size_t k = 0; k < n; k++) {
                const double x = k * step, fi = std::floor(x); const size_t i = (size_t) fi; const double f = x - fi, a = data[i], b = i + 1 < length ? data[i + 1] : a;
                p.user[k] = (float) clampd(a + (b - a) * f, -1, 1);
            }
            p.src = p.rec ? recEq(p.user) : p.user;
        }
        if (p.rec) p.rom = p.src; else quantizeRom(p.src, bits, floatDac, p.rom);
        return p;
    }
    void install(int v, Prepared& p) {
        if (v < 0 || v >= NV) return;
        cut(v);
        recData[v] = p.rec ? 1 : 0; voices[v].rec = p.rec; hasUser[v] = p.ok;
        std::swap(user[v], p.user); std::swap(src[v], p.src); std::swap(roms[v], p.rom);
        // prepared with other bits / DAC settings than the voice has now: requantize in place (same length, no allocation)
        if (!isRec(v) && roms[v].size() == src[v].size()) quantizeRom(src[v], (int) voices[v][vBits], dacFloat, roms[v]);
    }
    double voiceActive(int ch) const { return std::min(1.0, peak[ch] * 2.5); }

    /* ---------------- audio ---------------- */
    double channel(int ch) {
        Slot& a = cur[ch]; Slot& b = old[ch]; OnePole& f1 = lp1[ch]; Biquad& f2 = lp2[ch];
        double h = held[ch], y = 0;
        for (int k = 0; k < K; k++) {
            if (tk[(size_t) k]) {
                h = 0;
                if (a.active) h += a.tick();
                if (b.active) { h += b.tick() * b.fade; b.fade -= FADE_STEP; if (b.fade <= 0) b.active = false; }
            }
            y = f2.run(f1.run(h));
        }
        held[ch] = h;
        const double u = hp[ch].run(y) / OUT_K, au = std::fabs(u);
        const double o = a.rec ? OUT_K * u : OUT_K * u / std::pow(1 + au * au * std::sqrt(au), .4);
        if (!a.active && !b.active && h == 0) {
            OnePole& hh = hp[ch];
            if (std::fabs(f1.x1) + std::fabs(f1.y1) + std::fabs(f2.z1) + std::fabs(f2.z2) + std::fabs(hh.x1) + std::fabs(hh.y1) < 1e-9) {
                f1.reset(); f2.reset(); hh.reset(); on[ch] = 0;
            }
        }
        return o;
    }
    // aux (optional): NCH pointers to mono buffers for the individual outputs (post fader), or nullptr
    void process(float* outL, float* outR, int n, float* const* aux = nullptr) {
        Rev7& rv = revFx; TcDelay& dl = dlyFx;
        for (int i = 0; i < n; i++) {
            if (qN) for (int k = 0; k < qN; k++) if (qT[k] == i) fire(k);
            for (int k = 0; k < K; k++) { dacPh += dacInc; if (dacPh >= 1) { dacPh -= 1; tk[(size_t) k] = 1; } else tk[(size_t) k] = 0; }
            double dLs = 0, dRs = 0, sRv = 0, sDL = 0, sDR = 0;
            for (int ch = 0; ch < NCH; ch++) {
                double o = 0;
                if (on[ch]) {
                    o = channel(ch) * gC[ch];
                    const double l = o * pL[ch], r = o * pR[ch];
                    if (!direct[ch]) { dLs += l; dRs += r; }
                    sRv += o * sRev[ch]; sDL += l * sDly[ch]; sDR += r * sDly[ch];
                }
                const double ao = std::fabs(o); peak[ch] = ao > peak[ch] ? ao : peak[ch] * pkDec;
                if (aux && aux[ch]) aux[ch][i] = (float) o;
            }
            rv.process(sRv); dl.process(sDL, sDR);
            double ck = 0;
            if (ckN > 0) { ck = std::sin(ckPh) * ckEnv * ckA * clkG; ckPh += ckW; ckEnv *= ckDec; ckN--; }
            const double Lo = (dLs + rv.oL * rv.p[rLevel] + dl.oL) * volG + ck, Ro = (dRs + rv.oR * rv.p[rLevel] + dl.oR) * volG + ck;
            outL[i] = (float) Lo; outR[i] = (float) Ro;
            const double aL = std::fabs(Lo), aR = std::fabs(Ro); if (aL > mL) mL = aL; if (aR > mR) mR = aR;
        }
        if (qN) {
            int m = 0;
            for (int k = 0; k < qN; k++) if (qT[k] >= n) {
                qT[m] = qT[k] - n; qPitch[m] = qPitch[k]; qCh[m] = qCh[k]; qType[m] = qType[k]; qV[m] = qV[k]; qVel[m] = qVel[k]; qAcc[m] = qAcc[k]; m++;
            }
            qN = m;
        }
    }
};

}  // namespace five
