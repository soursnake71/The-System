// Omega Seven: the 6-operator FM engine (Mark I hardware model), sysex codec, arpeggiator, function sequencer and
// MIDI files. A line-for-line port of the web app's seven/engine.js and seven/seq.js; seven/tools/compare.mjs renders
// the same performance with both and compares them sample by sample. No JUCE in here (the test runner is plain C++).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace seven {

constexpr double kPi = 3.14159265358979323846;
constexpr double DX_RATE = 9426500.0 / 192.0;    // 49096.35 Hz
constexpr int N = 64, VOICES = 16, VOICE_SIZE = 156;
constexpr int64_t Q24 = 16777216;
using Voice156 = std::array<uint8_t, VOICE_SIZE>;
inline double jsRound(double x) { return std::floor(x + 0.5); }       // Math.round

/* ---------------- tables ---------------- */
struct Tables {
    int32_t logsin[4096], expt[256], lfosin[4096];
    Tables() {
        for (int i = 0; i < 4096; ++i) logsin[i] = (int32_t) jsRound(-std::log2(std::sin((i + 0.5) / 4096 * kPi / 2)) * 256);
        for (int i = 0; i < 256; ++i) expt[i] = (int32_t) jsRound(8192 * std::pow(2.0, -i / 256.0));
        for (int i = 0; i < 4096; ++i) lfosin[i] = (int32_t) jsRound(std::sin(2 * kPi * i / 4096) * 16777216);
    }
};
inline const Tables& tables() { static const Tables t; return t; }

inline int scaleOutLevel(int l) { static const int lut[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46}; return l >= 20 ? 28 + l : lut[l]; }
inline int scaleCurve(int group, int depth, int curve) {
    static const int ex[33] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
    int s;
    if (curve == 0 || curve == 3) s = (group * depth * 329) >> 12;
    else s = (ex[std::min(group, 32)] * depth * 329) >> 15;
    return curve < 2 ? -s : s;
}
inline int scaleLevel(int note, int bp, int ld, int rd, int lc, int rc) {
    const int off = note - bp - 17;
    if (off >= 0) return scaleCurve((off + 1) / 3, rd, rc);
    return scaleCurve((-(off - 1)) / 3, ld, lc);
}
inline int scaleRate(int note, int sens) { const int x = std::min(31, std::max(0, note / 3 - 7)); return (sens * x) >> 3; }
inline int scaleVelocity(int vel, int sens) {
    static const int vt[64] = {0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
        206, 209, 211, 214, 216, 218, 220, 222, 224, 226, 228, 230, 232, 233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254, 254};
    const int v = vt[std::max(0, std::min(127, vel)) >> 1] - 239;
    return ((sens * v + 7) >> 3) * 16;
}
static const int PMS_TAB[8] = {0, 10, 20, 33, 55, 92, 153, 255};
static const int AMS_TAB[4] = {0, 66, 109, 255};
static const int PITCH_RATE[100] = {1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31,
    33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135,
    141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};
static const int PITCH_LEVEL[100] = {-128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22, -21,
    -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127};
inline int64_t ENV_SRM() { static const int64_t v = (int64_t) std::floor(44100.0 / DX_RATE * (double) Q24); return v; }
inline int64_t LFO_UNIT() { static const int64_t v = (int64_t) jsRound(N * 25190424.0 / DX_RATE); return v; }
inline int64_t PENV_UNIT() { static const int64_t v = (int64_t) jsRound(N * (double) Q24 / (21.3 * DX_RATE)); return v; }
constexpr int64_t NOTE_LOG0 = 50857777;
inline int64_t coarseLog(int c) { return c == 0 ? -Q24 : (int64_t) jsRound(std::log2((double) c) * (double) Q24); }

/* ---------------- algorithms ---------------- */
struct Alg { int mod[6]; int car[6]; int fb, fbs; };
inline const std::array<Alg, 32>& algs() {
    static const std::array<Alg, 32> a = [] {
        struct D { std::vector<int> car; const char* mods; int fb, fbs; };
        const D d[32] = {
            {{1, 3}, "2>1 4>3 5>4 6>5", 6, 6}, {{1, 3}, "2>1 4>3 5>4 6>5", 2, 2}, {{1, 4}, "2>1 3>2 5>4 6>5", 6, 6}, {{1, 4}, "2>1 3>2 5>4 6>5", 6, 4},
            {{1, 3, 5}, "2>1 4>3 6>5", 6, 6}, {{1, 3, 5}, "2>1 4>3 6>5", 6, 5}, {{1, 3}, "2>1 4>3 5>3 6>5", 6, 6}, {{1, 3}, "2>1 4>3 5>3 6>5", 4, 4},
            {{1, 3}, "2>1 4>3 5>3 6>5", 2, 2}, {{1, 4}, "2>1 3>2 5>4 6>4", 3, 3}, {{1, 4}, "2>1 3>2 5>4 6>4", 6, 6}, {{1, 3}, "2>1 4>3 5>3 6>3", 2, 2},
            {{1, 3}, "2>1 4>3 5>3 6>3", 6, 6}, {{1, 3}, "2>1 4>3 5>4 6>4", 6, 6}, {{1, 3}, "2>1 4>3 5>4 6>4", 2, 2}, {{1}, "2>1 3>1 5>1 4>3 6>5", 6, 6},
            {{1}, "2>1 3>1 5>1 4>3 6>5", 2, 2}, {{1}, "2>1 3>1 4>1 5>4 6>5", 3, 3}, {{1, 4, 5}, "2>1 3>2 6>4 6>5", 6, 6}, {{1, 2, 4}, "3>1 3>2 5>4 6>4", 3, 3},
            {{1, 2, 4, 5}, "3>1 3>2 6>4 6>5", 3, 3}, {{1, 3, 4, 5}, "2>1 6>3 6>4 6>5", 6, 6}, {{1, 2, 4, 5}, "3>2 6>4 6>5", 6, 6}, {{1, 2, 3, 4, 5}, "6>3 6>4 6>5", 6, 6},
            {{1, 2, 3, 4, 5}, "6>4 6>5", 6, 6}, {{1, 2, 4}, "3>2 5>4 6>4", 6, 6}, {{1, 2, 4}, "3>2 5>4 6>4", 3, 3}, {{1, 3, 6}, "2>1 4>3 5>4", 5, 5},
            {{1, 2, 3, 5}, "4>3 6>5", 6, 6}, {{1, 2, 3, 6}, "4>3 5>4", 5, 5}, {{1, 2, 3, 4, 5}, "6>5", 6, 6}, {{1, 2, 3, 4, 5, 6}, "", 6, 6}};
        std::array<Alg, 32> r {};
        for (int i = 0; i < 32; ++i) {
            Alg g {};
            for (const char* p = d[i].mods; *p; ) {
                if (*p >= '1' && *p <= '6' && p[1] == '>') { const int s = *p - '0', t = p[2] - '0'; g.mod[t - 1] |= 1 << (s - 1); p += 3; } else ++p;
            }
            for (int c : d[i].car) g.car[c - 1] = 1;
            g.fb = d[i].fb - 1; g.fbs = d[i].fbs - 1;
            r[(size_t) i] = g;
        }
        return r;
    }();
    return a;
}

/* ---------------- voice data ---------------- */
inline int opBase(int op) { return (6 - op) * 21; }
struct VParam { int op; const char* shortName; const char* name; int max; int min; };
inline const std::vector<VParam>& vparams() {
    static const std::vector<VParam> v = [] {
        static const struct { const char* s; const char* n; int m; } OPP[21] = {
            {"R1", "EG RATE 1", 99}, {"R2", "EG RATE 2", 99}, {"R3", "EG RATE 3", 99}, {"R4", "EG RATE 4", 99}, {"L1", "EG LEVEL 1", 99}, {"L2", "EG LEVEL 2", 99},
            {"L3", "EG LEVEL 3", 99}, {"L4", "EG LEVEL 4", 99}, {"BP", "BREAK POINT", 99}, {"LD", "L SCALE DEPTH", 99}, {"RD", "R SCALE DEPTH", 99}, {"LC", "L SCALE CURVE", 3},
            {"RC", "R SCALE CURVE", 3}, {"RS", "RATE SCALING", 7}, {"AMS", "AMP MOD SENS", 3}, {"KVS", "KEY VELOCITY", 7}, {"OL", "OUTPUT LEVEL", 99}, {"OM", "OSC MODE", 1},
            {"FC", "FREQ COARSE", 31}, {"FF", "FREQ FINE", 99}, {"DET", "DETUNE", 14}};
        static const struct { const char* s; const char* n; int m; } GP[19] = {
            {"PR1", "PITCH EG RATE 1", 99}, {"PR2", "PITCH EG RATE 2", 99}, {"PR3", "PITCH EG RATE 3", 99}, {"PR4", "PITCH EG RATE 4", 99}, {"PL1", "PITCH EG LEVEL 1", 99},
            {"PL2", "PITCH EG LEVEL 2", 99}, {"PL3", "PITCH EG LEVEL 3", 99}, {"PL4", "PITCH EG LEVEL 4", 99}, {"ALG", "ALGORITHM", 31}, {"FB", "FEEDBACK", 7}, {"OKS", "OSC KEY SYNC", 1},
            {"LFS", "LFO SPEED", 99}, {"LFD", "LFO DELAY", 99}, {"LPMD", "LFO PM DEPTH", 99}, {"LAMD", "LFO AM DEPTH", 99}, {"LFKS", "LFO KEY SYNC", 1}, {"LFW", "LFO WAVE", 5},
            {"LPMS", "P MOD SENS", 7}, {"TRNSP", "TRANSPOSE", 48}};
        static const char* NM[10] = {"N1", "N2", "N3", "N4", "N5", "N6", "N7", "N8", "N9", "N10"};
        static const char* NN[10] = {"NAME 1", "NAME 2", "NAME 3", "NAME 4", "NAME 5", "NAME 6", "NAME 7", "NAME 8", "NAME 9", "NAME 10"};
        std::vector<VParam> r;
        for (int op = 6; op >= 1; --op) for (auto& p : OPP) r.push_back({op, p.s, p.n, p.m, 0});
        for (auto& p : GP) r.push_back({0, p.s, p.n, p.m, 0});
        for (int i = 0; i < 10; ++i) r.push_back({0, NM[i], NN[i], 127, 32});
        r.push_back({0, "OPON", "OPERATOR ON/OFF", 63, 0});
        return r;
    }();
    return v;
}
namespace P { enum { PR = 126, PL = 130, ALG = 134, FB = 135, OKS = 136, LFS = 137, LFD = 138, LPMD = 139, LAMD = 140, LFKS = 141, LFW = 142, LPMS = 143, TRNSP = 144, NAME = 145, OPON = 155 }; }

inline void setName(Voice156& v, const std::string& s) { for (int i = 0; i < 10; ++i) { const int c = i < (int) s.size() ? (unsigned char) s[(size_t) i] : 32; v[(size_t) (145 + i)] = (uint8_t) (c >= 32 && c < 128 ? c : 32); } }
inline std::string voiceName(const Voice156& v) { std::string s; for (int i = 0; i < 10; ++i) { const int c = v[(size_t) (145 + i)]; s += (char) (c >= 32 && c < 127 ? c : ' '); } return s; }
inline Voice156 initVoice() {
    Voice156 v {};
    for (int op = 1; op <= 6; ++op) {
        const uint8_t d[21] = {99, 99, 99, 99, 99, 99, 99, 0, 39, 0, 0, 0, 0, 0, 0, 0, (uint8_t) (op == 1 ? 99 : 0), 0, 1, 0, 7};
        std::memcpy(&v[(size_t) opBase(op)], d, 21);
    }
    const uint8_t g[19] = {99, 99, 99, 99, 50, 50, 50, 50, 0, 0, 1, 35, 0, 0, 0, 1, 0, 3, 24};
    std::memcpy(&v[126], g, 19);
    setName(v, "INIT VOICE");
    v[155] = 63;
    return v;
}
inline Voice156& clampVoice(Voice156& v) {
    const auto& ps = vparams();
    for (int i = 0; i < 155; ++i) { const auto& p = ps[(size_t) i]; if (v[(size_t) i] > p.max || v[(size_t) i] < p.min) v[(size_t) i] = (uint8_t) (i >= 145 ? 32 : std::min(p.max, std::max(p.min, (int) v[(size_t) i]))); }
    v[155] &= 63;
    return v;
}

/* ---------------- sysex ---------------- */
inline int checksum(const uint8_t* d, int a, int b) { int s = 0; for (int i = a; i < b; ++i) s += d[i]; return (128 - (s & 127)) & 127; }
inline void packVoice(const Voice156& v, uint8_t* out) {
    for (int op = 6; op >= 1; --op) {
        const int b = opBase(op), p = (6 - op) * 17;
        for (int i = 0; i < 11; ++i) out[p + i] = v[(size_t) (b + i)];
        out[p + 11] = (uint8_t) ((v[(size_t) (b + 11)] & 3) | ((v[(size_t) (b + 12)] & 3) << 2));
        out[p + 12] = (uint8_t) ((v[(size_t) (b + 13)] & 7) | ((v[(size_t) (b + 20)] & 15) << 3));
        out[p + 13] = (uint8_t) ((v[(size_t) (b + 14)] & 3) | ((v[(size_t) (b + 15)] & 7) << 2));
        out[p + 14] = v[(size_t) (b + 16)];
        out[p + 15] = (uint8_t) ((v[(size_t) (b + 17)] & 1) | ((v[(size_t) (b + 18)] & 31) << 1));
        out[p + 16] = v[(size_t) (b + 19)];
    }
    for (int i = 0; i < 8; ++i) out[102 + i] = v[(size_t) (126 + i)];
    out[110] = v[134] & 31;
    out[111] = (uint8_t) ((v[135] & 7) | ((v[136] & 1) << 3));
    for (int i = 0; i < 4; ++i) out[112 + i] = v[(size_t) (137 + i)];
    out[116] = (uint8_t) ((v[141] & 1) | ((v[142] & 7) << 1) | ((v[143] & 7) << 4));
    out[117] = v[144];
    for (int i = 0; i < 10; ++i) out[118 + i] = v[(size_t) (145 + i)] & 127;
}
inline Voice156 unpackVoice(const uint8_t* d) {
    Voice156 v {};
    for (int op = 6; op >= 1; --op) {
        const int b = opBase(op); const uint8_t* p = d + (6 - op) * 17;
        for (int i = 0; i < 11; ++i) v[(size_t) (b + i)] = p[i] & 127;
        v[(size_t) (b + 11)] = p[11] & 3; v[(size_t) (b + 12)] = (p[11] >> 2) & 3;
        v[(size_t) (b + 13)] = p[12] & 7; v[(size_t) (b + 20)] = (p[12] >> 3) & 15;
        v[(size_t) (b + 14)] = p[13] & 3; v[(size_t) (b + 15)] = (p[13] >> 2) & 7;
        v[(size_t) (b + 16)] = p[14] & 127;
        v[(size_t) (b + 17)] = p[15] & 1; v[(size_t) (b + 18)] = (p[15] >> 1) & 31;
        v[(size_t) (b + 19)] = p[16] & 127;
    }
    for (int i = 0; i < 8; ++i) v[(size_t) (126 + i)] = d[102 + i] & 127;
    v[134] = d[110] & 31; v[135] = d[111] & 7; v[136] = (d[111] >> 3) & 1;
    for (int i = 0; i < 4; ++i) v[(size_t) (137 + i)] = d[112 + i] & 127;
    v[141] = d[116] & 1; v[142] = (d[116] >> 1) & 7; v[143] = (d[116] >> 4) & 7;
    v[144] = d[117] & 127;
    for (int i = 0; i < 10; ++i) v[(size_t) (145 + i)] = d[118 + i] & 127;
    v[155] = 63;
    return clampVoice(v);
}
using Bank = std::array<Voice156, 32>;
inline std::vector<uint8_t> bankToSysex(const Bank& bank, int ch) {
    std::vector<uint8_t> d(4104);
    const uint8_t h[6] = {0xF0, 0x43, (uint8_t) (ch & 15), 0x09, 0x20, 0x00}; std::memcpy(d.data(), h, 6);
    for (int i = 0; i < 32; ++i) packVoice(bank[(size_t) i], d.data() + 6 + i * 128);
    d[4102] = (uint8_t) checksum(d.data(), 6, 4102); d[4103] = 0xF7;
    return d;
}
inline std::vector<uint8_t> voiceToSysex(const Voice156& v, int ch) {
    std::vector<uint8_t> d(163);
    const uint8_t h[6] = {0xF0, 0x43, (uint8_t) (ch & 15), 0x00, 0x01, 0x1B}; std::memcpy(d.data(), h, 6);
    for (int i = 0; i < 155; ++i) d[(size_t) (6 + i)] = v[(size_t) i];
    d[161] = (uint8_t) checksum(d.data(), 6, 161); d[162] = 0xF7;
    return d;
}
inline std::vector<uint8_t> paramChange(int ch, int group, int param, int value) {
    return {0xF0, 0x43, (uint8_t) (0x10 | (ch & 15)), (uint8_t) ((group << 2) | ((param >> 7) & 3)), (uint8_t) (param & 127), (uint8_t) (value & 127), 0xF7};
}
struct SysexMsg { enum Kind { none, bank, voice, param, fparam, reqBank, reqVoice } kind = none; Bank bankData {}; Voice156 voiceData {}; int n = 0, v = 0; bool bad = false; };
inline SysexMsg parseSysexMessage(const uint8_t* m, size_t len) {
    SysexMsg r;
    if (len < 6 || m[0] != 0xF0 || m[1] != 0x43) return r;
    const int sub = m[2] >> 4;
    if (sub == 0) {
        const int fmt = m[3], cnt = (m[4] << 7) | m[5];
        if (fmt == 9 && len >= 4104) {
            r.bad = checksum(m, 6, 4102) != (m[4102] & 127);
            for (int k = 0; k < 32; ++k) r.bankData[(size_t) k] = unpackVoice(m + 6 + k * 128);
            r.kind = SysexMsg::bank; return r;
        }
        if (fmt == 0 && cnt == 155 && len >= 163) {
            r.bad = checksum(m, 6, 161) != (m[161] & 127);
            Voice156 v {}; for (int k = 0; k < 155; ++k) v[(size_t) k] = m[6 + k] & 127; v[155] = 63;
            r.voiceData = clampVoice(v); r.kind = SysexMsg::voice; return r;
        }
        return r;
    }
    if (sub == 1 && len >= 7) {
        const int g = (m[3] >> 2) & 31, n = ((m[3] & 3) << 7) | m[4], val = m[5] & 127;
        if (g == 0 && n <= 155) { r.kind = SysexMsg::param; r.n = n; r.v = val; }
        else if (g == 2) { r.kind = SysexMsg::fparam; r.n = n; r.v = val; }
        return r;
    }
    if (sub == 2) { r.kind = m[3] == 9 ? SysexMsg::reqBank : SysexMsg::reqVoice; return r; }
    return r;
}
// a whole file: banks and voices found (a bare 4096-byte bank or 155-byte voice too)
struct SysexFile { bool hasBank = false; Bank bank {}; std::vector<Voice156> voices; std::vector<std::pair<int, int>> params; int bad = 0; };
inline SysexFile parseSysexFile(const uint8_t* d, size_t len) {
    SysexFile r;
    if (len == 4096 && d[0] != 0xF0) { r.hasBank = true; for (int i = 0; i < 32; ++i) r.bank[(size_t) i] = unpackVoice(d + i * 128); return r; }
    if ((len == 155 || len == 156) && d[0] != 0xF0) { Voice156 v {}; std::memcpy(v.data(), d, 155); v[155] = 63; r.voices.push_back(clampVoice(v)); return r; }
    size_t i = 0;
    while (i < len) {
        if (d[i] != 0xF0) { ++i; continue; }
        size_t e = i + 1; while (e < len && d[e] != 0xF7) ++e;
        const size_t ml = std::min(e + 1, len) - i;
        const SysexMsg m = parseSysexMessage(d + i, ml);
        i = e + 1;
        if (m.bad) r.bad++;
        if (m.kind == SysexMsg::bank) { r.hasBank = true; r.bank = m.bankData; }
        else if (m.kind == SysexMsg::voice) r.voices.push_back(m.voiceData);
        else if (m.kind == SysexMsg::param) r.params.push_back({m.n, m.v});
    }
    return r;
}

/* ---------------- function settings ---------------- */
struct FuncDef { const char* key; const char* name; int max, def, num; };
static const FuncDef FUNC_DEF[28] = {
    {"mono", "POLY/MONO", 1, 0, 64}, {"pbRange", "P BEND RANGE", 12, 2, 65}, {"pbStep", "P BEND STEP", 12, 0, 66}, {"portaMode", "PORTA MODE", 1, 0, 67},
    {"gliss", "PORTA GLISS", 1, 0, 68}, {"portaTime", "PORTA TIME", 99, 0, 69}, {"mwRange", "MOD WHEEL RANGE", 99, 50, 70}, {"mwAssign", "MOD WHEEL ASSIGN", 7, 1, 71},
    {"fcRange", "FOOT CTRL RANGE", 99, 0, 72}, {"fcAssign", "FOOT CTRL ASSIGN", 7, 0, 73}, {"bcRange", "BREATH CTRL RANGE", 99, 0, 74}, {"bcAssign", "BREATH ASSIGN", 7, 0, 75},
    {"atRange", "AFTERTOUCH RANGE", 99, 0, 76}, {"atAssign", "AFTERTOUCH ASSIGN", 7, 0, 77},
    {"tune", "MASTER TUNE", 127, 64, -1}, {"rxCh", "MIDI RECEIVE CH", 16, 0, -1}, {"txCh", "MIDI TRANSMIT CH", 15, 0, -1}, {"sysInfo", "SYS INFO", 1, 1, -1},
    {"dac", "CONVERTER", 1, 0, -1}, {"volume", "VOLUME", 99, 80, -1},
    {"chorus", "CHORUS DEPTH", 99, 0, -1}, {"chorusRate", "CHORUS RATE", 99, 30, -1}, {"delayMix", "DELAY MIX", 99, 0, -1}, {"delayTime", "DELAY TIME", 99, 60, -1},
    {"delayFb", "DELAY FEEDBACK", 99, 35, -1}, {"revMix", "REVERB MIX", 99, 0, -1}, {"revSize", "REVERB SIZE", 99, 60, -1}, {"hiss", "DAC NOISE", 99, 50, -1}};
enum F { mono, pbRange, pbStep, portaMode, gliss, portaTime, mwRange, mwAssign, fcRange, fcAssign, bcRange, bcAssign, atRange, atAssign, tune, rxCh, txCh, sysInfo, dac, volume,
    chorus, chorusRate, delayMix, delayTime, delayFb, revMix, revSize, hiss, FCOUNT };
inline int funcByKey(const std::string& k) { for (int i = 0; i < FCOUNT; ++i) if (k == FUNC_DEF[i].key) return i; return -1; }
inline int funcByNum(int n) { for (int i = 0; i < FCOUNT; ++i) if (FUNC_DEF[i].num == n) return i; return -1; }
struct Func { int v[FCOUNT]; Func() { for (int i = 0; i < FCOUNT; ++i) v[i] = FUNC_DEF[i].def; } int& operator[](int i) { return v[i]; } int operator[](int i) const { return v[i]; } };

/* ---------------- envelopes and LFO ---------------- */
struct Env {
    int rates[4] {}, levels[4] {}; int64_t outlevel = 0; int rs = 0; int64_t level = 0, target = 0, inc = 0; int ix = 0; bool rising = false, down = true;
    void init(const int* r, const int* l, int64_t ol, int rsc) { for (int i = 0; i < 4; ++i) { rates[i] = r[i]; levels[i] = l[i]; } outlevel = ol; rs = rsc; level = 0; down = true; advance(0); }
    void update(const int* r, const int* l, int64_t ol, int rsc) { for (int i = 0; i < 4; ++i) { rates[i] = r[i]; levels[i] = l[i]; } outlevel = ol; rs = rsc; if (down && ix < 4) retarget(); }
    void retarget() {
        int64_t a = scaleOutLevel(levels[ix]) >> 1;
        a = (a << 6) + outlevel - 4256;
        if (a < 16) a = 16;
        target = a * 65536;
        rising = target > level;
        int q = (rates[ix] * 41) >> 6;
        q += rs; if (q > 63) q = 63;
        const int64_t in = (int64_t) (4 + (q & 3)) << (8 + (q >> 2));
        inc = (in * ENV_SRM()) >> 24;
    }
    void advance(int i) { ix = i; if (i < 4) retarget(); }
    void keyDown(bool d) { if (down != d) { down = d; advance(d ? 0 : 3); } }
    int64_t tick() {
        if (ix < 3 || (ix < 4 && !down)) {
            if (rising) {
                const int64_t jump = (int64_t) 1716 * 65536;
                if (level < jump) level = jump;
                level += ((17 * Q24 - level) >> 24) * inc;
                if (level >= target) { level = target; advance(ix + 1); }
            } else {
                level -= inc;
                if (level <= target) { level = target; advance(ix + 1); }
            }
        }
        return level;
    }
};
struct PitchEnv {
    int rates[4] {}, levels[4] {}; int64_t level = 0, target = 0, inc = 0; int ix = 0; bool rising = false, down = true;
    void set(const int* r, const int* l) { for (int i = 0; i < 4; ++i) { rates[i] = r[i]; levels[i] = l[i]; } level = (int64_t) PITCH_LEVEL[l[3]] * 524288; down = true; advance(0); }
    void update(const int* r, const int* l) { for (int i = 0; i < 4; ++i) { rates[i] = r[i]; levels[i] = l[i]; } if (ix < 4) advance(ix); }
    void advance(int i) { ix = i; if (i < 4) { target = (int64_t) PITCH_LEVEL[levels[i]] * 524288; rising = target > level; inc = PITCH_RATE[rates[i]] * PENV_UNIT(); } }
    void keyDown(bool d) { if (down != d) { down = d; advance(d ? 0 : 3); } }
    int64_t tick() {
        if (ix < 3 || (ix < 4 && !down)) {
            if (rising) { level += inc; if (level >= target) { level = target; advance(ix + 1); } }
            else { level -= inc; if (level <= target) { level = target; advance(ix + 1); } }
        }
        return level;
    }
};
struct Lfo {
    uint32_t phase = 0, delta = 0; uint64_t delayState = 0, delayInc = 0, delayInc2 = 0; int wave = 0; bool sync = false; int rand = 0; int64_t val = 0, delayVal = 0;
    void reset(const Voice156& v) {
        const int rate = v[P::LFS];
        int sr = rate == 0 ? 1 : (165 * rate) >> 6;
        sr *= sr < 160 ? 11 : (11 + ((sr - 160) >> 4));
        delta = (uint32_t) (LFO_UNIT() * sr);
        int a = 99 - v[P::LFD];
        if (a == 99) { delayInc = 0xFFFFFFFFu; delayInc2 = 0xFFFFFFFFu; }
        else {
            a = (16 + (a & 15)) << (1 + (a >> 4));
            delayInc = (uint64_t) (LFO_UNIT() * a);
            a &= 0xff80; a = std::max(0x80, a);
            delayInc2 = (uint64_t) (LFO_UNIT() * a);
        }
        wave = v[P::LFW]; sync = v[P::LFKS] != 0;
    }
    void keyDown() { if (sync) phase = 0x7FFFFFFFu; delayState = 0; }
    void tick() {
        phase += delta;
        const uint32_t ph = phase;
        uint32_t x;
        switch (wave) {
            case 0: x = ph >> 7; if (ph >> 31) x = ~x; x &= 0xFFFFFFu; break;
            case 1: x = (~ph ^ 0x80000000u) >> 8; break;
            case 2: x = (ph ^ 0x80000000u) >> 8; break;
            case 3: x = ((~ph) >> 7) & 0x1000000u; break;
            case 4: x = (uint32_t) (8388608 + (tables().lfosin[ph >> 20] >> 1)); break;
            default: if (ph < delta) rand = (rand * 179 + 17) & 0xff; x = (uint32_t) (rand << 16);
        }
        val = x;
        const uint64_t in = delayState < 0x80000000u ? delayInc : delayInc2;
        const uint64_t d = delayState + in;
        if (d > 0xFFFFFFFFu) delayVal = Q24;
        else { delayState = d; delayVal = d < 0x80000000u ? 0 : (int64_t) ((d >> 7) & 0xFFFFFF); }
    }
};

/* ---------------- a voice ---------------- */
inline int64_t oscLogFreq(int note, int mode, int coarse, int fine, int det) {
    int64_t lf;
    if (mode == 0) {
        lf = NOTE_LOG0 + (int64_t) note * Q24 / 12;
        const double dr = 0.0209 * std::exp(-0.396 * ((double) lf / (double) Q24)) / 7;
        lf += (int64_t) (dr * (double) lf * (det - 7));
        lf += coarseLog(coarse & 31);
        if (fine) lf += (int64_t) std::floor(0.5 + (double) Q24 * std::log2(1 + 0.01 * fine));
    } else {
        lf = (int64_t) 4458616 * ((coarse & 3) * 100 + fine) / 8;
        lf += det > 7 ? 13457 * (det - 7) : 0;
    }
    return lf;
}
inline uint32_t phaseInc(double logfreq) { return (uint32_t) (int64_t) jsRound(std::pow(2.0, logfreq / (double) Q24) / DX_RATE * 4294967296.0); }

struct Voice {
    Env env[6]; PitchEnv penv;
    uint32_t phase[6] {}, inc[6] {}; int64_t base[6] {}; int fixed[6] {};
    int att0[6] {}, att1[6] {}, out[6] {}, ams[6] {};
    int fb0 = 0, fb1 = 0;
    int note = -1, key = -1, vel = 0; bool active = false, held = false, sustained = false; double age = 0;
    double pitch = 0, target = 0;
    static void rl(const Voice156& v, int b, int* r, int* l) { for (int i = 0; i < 4; ++i) { r[i] = v[(size_t) (b + i)]; l[i] = v[(size_t) (b + 4 + i)]; } }
    void setup(const Voice156& v, int n, int ve, int porta) {
        note = n; vel = ve;
        for (int op = 1; op <= 6; ++op) {
            const int b = opBase(op), k = op - 1; int r[4], l[4]; rl(v, b, r, l);
            env[k].init(r, l, opOutLevel(v, b), scaleRate(n, v[(size_t) (b + 13)]));
            att0[k] = 4095; att1[k] = 4095;
        }
        statics(v, n);
        int r[4], l[4]; rl(v, 126, r, l); penv.set(r, l);
        if (v[P::OKS]) for (int k = 0; k < 6; ++k) phase[k] = 0;
        fb0 = fb1 = 0;
        target = (double) ((int64_t) n * Q24 / 12);
        if (porta < 0) pitch = target;
        active = true; held = true; sustained = false;
    }
    int64_t opOutLevel(const Voice156& v, int b) const {
        int64_t ol = scaleOutLevel(v[(size_t) (b + 16)]);
        ol += scaleLevel(note, v[(size_t) (b + 8)], v[(size_t) (b + 9)], v[(size_t) (b + 10)], v[(size_t) (b + 11)], v[(size_t) (b + 12)]);
        ol = std::min<int64_t>(127, ol) << 5;
        ol += scaleVelocity(vel, v[(size_t) (b + 15)]);
        return std::max<int64_t>(0, ol);
    }
    void statics(const Voice156& v, int n) {
        for (int op = 1; op <= 6; ++op) {
            const int b = opBase(op), k = op - 1;
            fixed[k] = v[(size_t) (b + 17)];
            base[k] = fixed[k] ? oscLogFreq(n, 1, v[(size_t) (b + 18)], v[(size_t) (b + 19)], v[(size_t) (b + 20)])
                               : oscLogFreq(n, 0, v[(size_t) (b + 18)], v[(size_t) (b + 19)], v[(size_t) (b + 20)]) - NOTE_LOG0 - (int64_t) n * Q24 / 12;
            ams[k] = AMS_TAB[v[(size_t) (b + 14)]];
        }
    }
    void update(const Voice156& v) {
        for (int op = 1; op <= 6; ++op) { const int b = opBase(op); int r[4], l[4]; rl(v, b, r, l); env[op - 1].update(r, l, opOutLevel(v, b), scaleRate(note, v[(size_t) (b + 13)])); }
        statics(v, note);
        int r[4], l[4]; rl(v, 126, r, l); penv.update(r, l);
    }
    void keyUp() { held = false; for (auto& e : env) e.keyDown(false); penv.keyDown(false); }
    void retrigger(int n) { note = n; target = (double) ((int64_t) n * Q24 / 12); held = true; sustained = false; }
};

/* ---------------- output stage pieces ---------------- */
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Biquad lowpass(double fc, double fs, double q) {
        const double w = 2 * kPi * fc / fs, cw = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        Biquad f; f.b0 = (1 - cw) / 2 / a0; f.b1 = (1 - cw) / a0; f.b2 = (1 - cw) / 2 / a0; f.a1 = -2 * cw / a0; f.a2 = (1 - al) / a0; return f;
    }
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
struct Resampler {
    double step = 1, pos = 0; int64_t need = 0, count = 0; int W = 1, RES = 64; std::vector<double> tab, buf; int size = 1024;
    void init(double fin, double fout) {
        const double fc = std::min(20000.0, 0.45 * fout) / fin;
        const int Z = 8; W = (int) std::ceil(Z / (2 * fc)); RES = 64;
        tab.assign((size_t) (W * RES + 2), 0.0);
        for (int i = 0; i <= W * RES + 1; ++i) {
            const double x = (double) i / RES;
            if (x >= W) { tab[(size_t) i] = 0; continue; }
            const double s = x == 0 ? 1 : std::sin(kPi * 2 * fc * x) / (kPi * 2 * fc * x);
            const double wv = 0.42 + 0.5 * std::cos(kPi * x / W) + 0.08 * std::cos(2 * kPi * x / W);
            tab[(size_t) i] = 2 * fc * s * wv;
        }
        buf.assign((size_t) size, 0.0); step = fin / fout; pos = 0; count = 0; need = W + 1;
    }
    void push(double x) { buf[(size_t) (count & (size - 1))] = x; count++; }
    double next() {
        const double p = pos; const int64_t ip = (int64_t) std::floor(p);
        double acc = 0;
        for (int64_t k = ip - W + 1; k <= ip + W; ++k) {
            if (k < 0) continue;
            const double x = std::abs(p - (double) k) * RES; const int64_t xi = (int64_t) std::floor(x); const double xf = x - (double) xi;
            if (xi >= (int64_t) W * RES) continue;
            acc += buf[(size_t) (k & (size - 1))] * (tab[(size_t) xi] + (tab[(size_t) xi + 1] - tab[(size_t) xi]) * xf);
        }
        pos += step;
        need = (int64_t) std::floor(pos) + W + 1;
        return acc;
    }
};

/* ---------------- the synth ---------------- */
struct Ctl { int wheel = 0, foot = 0, breath = 0, at = 0, bend = 0; bool sustain = false, portaSw = true; int volume = 127, expression = 127; };
struct MidiResult { enum Kind { none, program, func, param, voice, bankDump, reqBank, reqVoice } kind = none; int a = 0, b = 0; bool bad = false; Voice156 v {}; Bank bank {}; };

/* ---------------- effects: stereo chorus, ping-pong delay, reverb (Freeverb topology) ---------------- */
class Fx {
public:
    explicit Fx(double rate = 48000) { init(rate); }
    void init(double rate) {
        fs = rate;
        chL.assign((size_t) chSize, 0.0); chW = 0; chPh = 0;
        dSize = 1; while (dSize < fs * 1.1) dSize *= 2;
        dL.assign((size_t) dSize, 0.0); dR.assign((size_t) dSize, 0.0); dW = 0; lpL = lpR = 0;
        static const int COMBS[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, APS[4] = {556, 441, 341, 225};
        const double k = fs / 44100;
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < 8; ++i) { auto& cb = combs[c][i]; cb.buf.assign((size_t) std::max(1, (int) std::floor((COMBS[i] + c * 23) * k)), 0.0); cb.i = 0; cb.st = 0; }
            for (int i = 0; i < 4; ++i) { auto& ap = aps[c][i]; ap.buf.assign((size_t) std::max(1, (int) std::floor((APS[i] + c * 23) * k)), 0.0); ap.i = 0; }
        }
    }
    void process(float* L, float* R, int n, const Func& f) {
        const double cd = f[F::chorus] / 99.0 * 3, cr = 0.1 * std::pow(2.0, f[F::chorusRate] / 99.0 * 6) / fs;
        const double dm = f[F::delayMix] / 99.0; const int64_t dt = std::min<int64_t>(dSize - 2, (int64_t) std::floor(20 * std::pow(2.0, f[F::delayTime] / 99.0 * 5.6) * fs / 1000));
        const double dfb = f[F::delayFb] / 99.0 * 0.9;
        const double rm = f[F::revMix] / 99.0 * 3, room = 0.7 + f[F::revSize] / 99.0 * 0.28, damp = 0.45 - f[F::revSize] / 99.0 * 0.25;
        for (int j = 0; j < n; ++j) {
            const double x = L[j];
            double l = x, r = x;
            if (cd > 0) {
                const int64_t cm = chSize - 1;
                chL[(size_t) (chW & cm)] = x;
                auto tri = [](double p) { const double q = p - std::floor(p); return q < 0.5 ? q * 4 - 1 : 3 - q * 4; };
                auto tapd = [&](double d) { const double pos = (double) chW - d * fs / 1000; const int64_t ip = (int64_t) std::floor(pos); const double fr = pos - (double) ip;
                    return chL[(size_t) (ip & cm)] * (1 - fr) + chL[(size_t) ((ip + 1) & cm)] * fr; };
                const double a = tapd(7 + cd * tri(chPh)), b = tapd(7 + cd * tri(chPh + 0.5));
                l = 0.7 * (x + a); r = 0.7 * (x + b);
                chW++; chPh += cr; if (chPh >= 1) chPh -= 1;
            }
            if (dm > 0) {
                const int64_t m = dSize - 1; const double rl = dL[(size_t) ((dW - dt) & m)], rr = dR[(size_t) ((dW - dt) & m)];
                lpL += (rl - lpL) * 0.35; lpR += (rr - lpR) * 0.35;
                dL[(size_t) (dW & m)] = (l + r) * 0.5 + dfb * lpR;
                dR[(size_t) (dW & m)] = dfb * lpL;
                dW++;
                l += dm * rl; r += dm * rr;
            }
            if (rm > 0) {
                const double inp = (l + r) * 0.015;
                double oL = 0, oR = 0;
                for (int c = 0; c < 2; ++c) {
                    double o = 0;
                    for (auto& cb : combs[c]) {
                        const double y = cb.buf[cb.i];
                        cb.st = y * (1 - damp) + cb.st * damp;
                        cb.buf[cb.i] = inp + cb.st * room;
                        if (++cb.i >= cb.buf.size()) cb.i = 0;
                        o += y;
                    }
                    for (auto& ap : aps[c]) {
                        const double b = ap.buf[ap.i];
                        ap.buf[ap.i] = o + b * 0.5;
                        if (++ap.i >= ap.buf.size()) ap.i = 0;
                        o = b - o;
                    }
                    if (c == 0) oL = o; else oR = o;
                }
                l += rm * oL; r += rm * oR;
            }
            L[j] = (float) l; R[j] = (float) r;
        }
    }
private:
    struct Comb { std::vector<double> buf; size_t i = 0; double st = 0; };
    struct Ap { std::vector<double> buf; size_t i = 0; };
    double fs = 48000;
    static constexpr int64_t chSize = 8192;
    std::vector<double> chL; int64_t chW = 0; double chPh = 0;
    int64_t dSize = 1; std::vector<double> dL, dR; int64_t dW = 0; double lpL = 0, lpR = 0;
    Comb combs[2][8]; Ap aps[2][4];
};

/* ---------------- the dice: a random voice that plays (randomVoice in engine.js, same numbers) ---------------- */
inline Voice156 randomVoice(uint32_t seed) {
    uint32_t st = seed ? seed : 1;
    auto rnd = [&]() { st ^= st << 13; st ^= st >> 17; st ^= st << 5; return st / 4294967296.0; };
    auto ri = [&](int a, int b) { return a + (int) std::floor(rnd() * (b - a + 1)); };
    Voice156 v = initVoice();
    const int alg = ri(0, 31); const Alg& A = algs()[(size_t) alg];
    static const int COARSE[19] = {0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 6, 7, 8, 9, 11, 14};
    const bool perc = rnd() < 0.4;
    for (int op = 1; op <= 6; ++op) {
        const int b = opBase(op), car = A.car[op - 1];
        auto V = [&](int i, int x) { v[(size_t) (b + i)] = (uint8_t) x; };
        V(0, car ? (perc ? ri(85, 99) : ri(45, 95)) : ri(40, 99));
        V(1, ri(20, 85)); V(2, ri(10, 75)); V(3, ri(30, 80));
        V(4, car ? 99 : ri(75, 99)); V(5, perc ? ri(40, 85) : ri(75, 99)); V(6, perc ? ri(0, 30) : ri(60, 95)); V(7, 0);
        V(8, ri(30, 60)); V(9, ri(0, 30)); V(10, ri(0, 40)); V(11, ri(0, 3)); V(12, ri(0, 3));
        V(13, ri(0, 4)); V(14, rnd() < 0.15 ? ri(1, 2) : 0); V(15, ri(0, 4));
        V(16, car ? ri(88, 99) : ri(40, 86));
        const bool fixed = !car && rnd() < 0.06;
        V(17, fixed ? 1 : 0);
        V(18, fixed ? ri(1, 3) : COARSE[ri(0, 18)]);
        V(19, rnd() < 0.8 ? 0 : ri(1, 99));
        V(20, ri(4, 10));
    }
    v[P::ALG] = (uint8_t) alg; v[P::FB] = (uint8_t) ri(0, 7); v[P::OKS] = 1;
    v[P::LFS] = (uint8_t) ri(15, 55); v[P::LFD] = (uint8_t) ri(0, 60); v[P::LPMD] = (uint8_t) ri(0, 8);
    v[P::LAMD] = (uint8_t) (rnd() < 0.2 ? ri(5, 30) : 0); v[P::LFKS] = 0;
    static const int W[6] = {0, 4, 4, 0, 1, 2}; v[P::LFW] = (uint8_t) W[ri(0, 5)]; v[P::LPMS] = (uint8_t) ri(1, 4);
    for (int i = 0; i < 4; ++i) { v[(size_t) (P::PR + i)] = 99; v[(size_t) (P::PL + i)] = 50; }
    if (rnd() < 0.15) { v[P::PR] = (uint8_t) ri(70, 99); v[P::PL + 3] = (uint8_t) ri(44, 56); v[P::PL] = v[P::PL + 3]; }
    v[P::TRNSP] = 24;
    char nm[16]; std::snprintf(nm, sizeof nm, "RANDOM %03u", seed % 1000); setName(v, nm);
    return v;
}

/* ---------------- the Mark I converter, analog side included (Mk1Dac in engine.js: see the notes there) ---------------- */
struct Mk1Params { double bitErr[12]; double rangeErr[4]; double dacNoise, floorNoise; };
static const Mk1Params MK1 = {{0.0012, -0.0008, 0.0006, -0.0004, 0.0003, -0.0002, 0.00015, -0.0001, 0, 0, 0, 0}, {0, -0.0072, 0.0058, -0.0091}, 0.30, 1.05};
inline const std::array<double, 8>& volSteps() {
    static const std::array<double, 8> v = [] { const double d[8] = {0, -36, -28, -21, -15, -10, -5, 0}; std::array<double, 8> r {}; for (int i = 1; i < 8; ++i) r[(size_t) i] = std::pow(10.0, d[i] / 20); return r; }();
    return v;
}
class Mk1Dac {
public:
    Mk1Dac() {
        for (int c = 0; c < 4096; ++c) {
            double v = 0, id = 0; const int u = c ^ 2048;
            for (int b = 11; b >= 0; --b) if ((u >> b) & 1) { const double w = std::pow(2.0, b); v += w * (1 + MK1.bitErr[11 - b]); id += w; }
            ladder[c] = v - 2048; ideal[c] = id - 2048;
        }
    }
    double noise() {
        double s = 0;
        for (int i = 0; i < 4; ++i) { uint32_t x = seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; seed = x; s += x / 4294967296.0; }
        return (s - 2) * 1.7320508075688772;
    }
    double voice(int v, double amount) {
        if (v > 16383) v = 16383; else if (v < -16384) v = -16384;
        int e = 0;
        while (e < 3 && (v >= (2048 << e) || v < -(2048 << e))) e++;
        const int c = (v >> e) & 4095;
        const double code = ideal[c] + (ladder[c] - ideal[c]) * amount + noise() * MK1.dacNoise * amount;
        return code * std::pow(2.0, e) * (1 + MK1.rangeErr[e] * amount);
    }
    double out(double sum, double vol, int idle, double amount) {
        const double idleNoise = noise() * MK1.dacNoise * amount * std::sqrt((double) idle);
        return (sum + idleNoise) * vol + noise() * MK1.floorNoise * amount;
    }
private:
    double ladder[4096], ideal[4096]; uint32_t seed = 0x2545F491u;
};

class Synth {
public:
    double fs = 48000;
    Voice156 voice = initVoice();
    Func func;
    std::array<Voice, VOICES> voices;
    Lfo lfo; Ctl ctl;
    std::vector<int> stack;
    double lastPitch = -1; double clock = 0;
    int blockPos = 0;
    int pitchMod = 0, ampMod = 0, egMod = 127;
    int zoh = 4; double fin = DX_RATE * 4;
    Biquad bq[2]; Resampler rs; Fx fx; Mk1Dac dac;
    double peak = 0;

    explicit Synth(double rate = 48000) {
        fs = rate; lfo.reset(voice); recalcCtl();
        bq[0] = Biquad::lowpass(16000, fin, 0.5412); bq[1] = Biquad::lowpass(16000, fin, 1.3066);
        rs.init(fin, fs); fx.init(fs);
    }
    void setRate(double rate) { fs = rate; rs.init(fin, fs); fx.init(fs); }

    void loadVoice(const Voice156& v) { voice = v; clampVoice(voice); voiceChanged(); }
    void setParam(int n, int val) {
        if (n < 0 || n > 155) return;
        const auto& p = vparams()[(size_t) n]; val = std::max(p.min, std::min(p.max, val));
        if (voice[(size_t) n] == val) return;
        voice[(size_t) n] = (uint8_t) val; voiceChanged();
    }
    void voiceChanged() { lfo.reset(voice); for (auto& vc : voices) if (vc.active) vc.update(voice); }
    void setFunc(int k, int val) {
        if (k < 0 || k >= FCOUNT) return;
        func[k] = std::max(0, std::min(FUNC_DEF[k].max, val));
        if (k == F::mono) allNotesOff(true);
        recalcCtl();
    }
    void recalcCtl() {
        int pm = 0, am = 0, eg = 0; bool anyEg = false;
        auto add = [&](int val, int range, int assign) {
            const int t = val * range / 100;
            if (assign & 1) pm = std::max(pm, t);
            if (assign & 2) am = std::max(am, t);
            if (assign & 4) { eg = std::max(eg, t); anyEg = true; }
        };
        add(ctl.wheel, func[F::mwRange], func[F::mwAssign]); add(ctl.foot, func[F::fcRange], func[F::fcAssign]);
        add(ctl.breath, func[F::bcRange], func[F::bcAssign]); add(ctl.at, func[F::atRange], func[F::atAssign]);
        pitchMod = pm; ampMod = am; egMod = anyEg ? eg : 127;
    }
    void noteOn(int note, int vel) {
        if (vel == 0) { noteOff(note); return; }
        note &= 127;
        const int tr = voice[P::TRNSP] - 24;
        const int n = std::max(0, std::min(127, note + tr));
        const bool portaOn = func[F::portaTime] > 0 && ctl.portaSw;
        lfo.keyDown();
        clock++;
        if (func[F::mono]) {
            stack.erase(std::remove(stack.begin(), stack.end(), note), stack.end()); stack.push_back(note);
            Voice& vc = voices[0];
            const bool legato = vc.active && vc.held;
            if (legato) { vc.retrigger(n); vc.key = note; if (!portaOn) vc.pitch = vc.target; vc.update(voice); }
            else {
                const double from = vc.active || lastPitch >= 0 ? (vc.active ? vc.pitch : lastPitch) : -1;
                const bool glide = portaOn && func[F::portaMode] == 1 && from >= 0;
                vc.setup(voice, n, vel, glide ? 0 : -1);
                if (glide) vc.pitch = from;
                vc.key = note; vc.age = clock;
            }
            lastPitch = vc.target;
            return;
        }
        int best = -1; double bestScore = 1e300;
        for (int i = 0; i < VOICES; ++i) {
            const Voice& vc = voices[(size_t) i];
            const double score = !vc.active ? -1e9 + i : (vc.held || vc.sustained ? vc.age : vc.age - 1e8);
            if (score < bestScore) { bestScore = score; best = i; }
        }
        Voice& vc = voices[(size_t) best];
        const bool glide = portaOn && lastPitch >= 0;
        vc.setup(voice, n, vel, glide ? 0 : -1);
        if (glide) vc.pitch = lastPitch;
        vc.key = note; vc.age = clock;
        lastPitch = vc.target;
    }
    void noteOff(int note) {
        note &= 127;
        if (func[F::mono]) {
            const bool wasTop = !stack.empty() && stack.back() == note;
            stack.erase(std::remove(stack.begin(), stack.end(), note), stack.end());
            Voice& vc = voices[0];
            if (!wasTop || !vc.active || vc.key != note) return;
            if (!stack.empty()) {
                const int k = stack.back(), n = std::max(0, std::min(127, k + voice[P::TRNSP] - 24));
                vc.retrigger(n); vc.key = k; if (!(func[F::portaTime] > 0 && ctl.portaSw)) vc.pitch = vc.target; vc.update(voice);
                lastPitch = vc.target;
                return;
            }
            if (ctl.sustain) { vc.sustained = true; vc.held = false; return; }
            vc.keyUp(); return;
        }
        for (auto& vc : voices) if (vc.active && vc.held && vc.key == note) { if (ctl.sustain) { vc.held = false; vc.sustained = true; } else vc.keyUp(); }
    }
    void sustain(bool on) { ctl.sustain = on; if (!on) for (auto& vc : voices) if (vc.active && vc.sustained) { vc.sustained = false; vc.keyUp(); } }
    void allNotesOff(bool hard) { stack.clear(); for (auto& vc : voices) if (vc.active) { vc.sustained = false; vc.keyUp(); if (hard) vc.active = false; } }
    void setBend(int v) { ctl.bend = std::max(-8192, std::min(8191, v)); }
    void setCtl(int& c, int v) { c = v; recalcCtl(); }

    MidiResult midi(const uint8_t* m, size_t len) {
        MidiResult r;
        if (len == 0) return r;
        const int st = m[0];
        if (st == 0xF0) return sysex(m, len);
        const int ch = st & 15, ty = st & 0xF0, rx = func[F::rxCh];
        if (ty < 0x80 || st >= 0xF0) return r;
        if (rx > 0 && ch != rx - 1) return r;
        const int a = len > 1 ? m[1] : 0, b = len > 2 ? m[2] : 0;
        switch (ty) {
            case 0x90: if (b) noteOn(a, b); else noteOff(a); break;
            case 0x80: noteOff(a); break;
            case 0xE0: setBend(((b << 7) | a) - 8192); break;
            case 0xD0: setCtl(ctl.at, a); break;
            case 0xA0: setCtl(ctl.at, b); break;
            case 0xC0: r.kind = MidiResult::program; r.a = a; return r;
            case 0xB0:
                switch (a) {
                    case 1: setCtl(ctl.wheel, b); break;
                    case 2: setCtl(ctl.breath, b); break;
                    case 4: setCtl(ctl.foot, b); break;
                    case 5: setFunc(F::portaTime, (int) jsRound(b * 99 / 127.0)); r.kind = MidiResult::func; r.a = F::portaTime; return r;
                    case 7: ctl.volume = b; break;
                    case 11: ctl.expression = b; break;
                    case 64: sustain(b >= 64); break;
                    case 65: ctl.portaSw = b >= 64; break;
                    case 120: allNotesOff(true); break;
                    case 121: ctl.wheel = ctl.foot = ctl.breath = ctl.at = 0; ctl.bend = 0; ctl.expression = 127; sustain(false); recalcCtl(); break;
                    case 123: allNotesOff(false); break;
                    case 126: setFunc(F::mono, 1); r.kind = MidiResult::func; r.a = F::mono; return r;
                    case 127: setFunc(F::mono, 0); r.kind = MidiResult::func; r.a = F::mono; return r;
                }
                break;
        }
        return r;
    }
    MidiResult sysex(const uint8_t* m, size_t len) {
        MidiResult r;
        if (!func[F::sysInfo]) return r;
        const SysexMsg s = parseSysexMessage(m, len);
        switch (s.kind) {
            case SysexMsg::param: setParam(s.n, s.v); r.kind = MidiResult::param; r.a = s.n; r.b = s.v; break;
            case SysexMsg::fparam: { const int k = funcByNum(s.n); if (k >= 0) { setFunc(k, s.v); r.kind = MidiResult::func; r.a = k; } break; }
            case SysexMsg::voice: loadVoice(s.voiceData); r.kind = MidiResult::voice; r.v = s.voiceData; r.bad = s.bad; break;
            case SysexMsg::bank: r.kind = MidiResult::bankDump; r.bank = s.bankData; r.bad = s.bad; break;
            case SysexMsg::reqBank: r.kind = MidiResult::reqBank; break;
            case SysexMsg::reqVoice: r.kind = MidiResult::reqVoice; break;
            default: break;
        }
        return r;
    }

    void control() {
        const Voice156& v = voice;
        lfo.tick();
        const int64_t lv = lfo.val, ld = lfo.delayVal;
        const int64_t pmdDepth = (v[P::LPMD] * 165) >> 6, amdDepth = (v[P::LAMD] * 165) >> 6;
        const int64_t sens = PMS_TAB[v[P::LPMS]] * (lv - 8388608);
        const int64_t pm1 = std::llabs(pmdDepth * (ld >> 12) * sens / 134217728);
        const int64_t pm2 = std::llabs((int64_t) pitchMod * sens / 16384);
        const int64_t lfoPitch = std::max(pm1, pm2) * (sens < 0 ? -1 : 1);
        const int64_t am1 = (amdDepth * ld / 256) * lv / Q24;
        const int64_t am2 = (int64_t) ampMod * lv / 128;
        int64_t amd = std::max(am1, am2);
        amd = std::max(Q24 - (int64_t) (egMod + 1) * 131072, amd);
        double bendSemi = ctl.bend / 8192.0 * func[F::pbRange];
        if (func[F::pbStep] > 0) bendSemi = jsRound(bendSemi / func[F::pbStep]) * func[F::pbStep];
        const double global = jsRound(bendSemi * (double) Q24 / 12) + jsRound((func[F::tune] - 64) * 75.0 / 64 / 1200 * (double) Q24);
        const double porta = func[F::portaTime] > 0 && ctl.portaSw ? std::max(1.0, jsRound(1000 * std::pow(2.0, -func[F::portaTime] / 10.0) / 12 * (double) Q24 * N / DX_RATE)) : 0;
        const Alg& alg = algs()[v[P::ALG]]; const int on = v[P::OPON];
        for (auto& vc : voices) {
            if (!vc.active) continue;
            if (porta != 0 && vc.pitch != vc.target) {
                if (vc.pitch < vc.target) vc.pitch = std::min(vc.target, vc.pitch + porta); else vc.pitch = std::max(vc.target, vc.pitch - porta);
            } else vc.pitch = vc.target;
            double np = vc.pitch;
            if (func[F::gliss] && porta != 0) { const double stp = (double) Q24 / 12; np = jsRound(np / stp) * stp; }
            const int64_t penv = vc.penv.tick();
            const double ratioPitch = (double) NOTE_LOG0 + np + (double) penv + (double) lfoPitch + global;
            bool silent = !vc.held && !vc.sustained;
            for (int k = 0; k < 6; ++k) {
                const int64_t lev = vc.env[k].tick();
                int64_t att = 4096 - (lev >> 16);
                if (vc.ams[k]) att += (amd * vc.ams[k] / 256) >> 12;
                att = std::max<int64_t>(0, std::min<int64_t>(4095, att));
                if (!((on >> (6 - (k + 1))) & 1)) att = 4095;
                vc.att0[k] = vc.att1[k]; vc.att1[k] = (int) att;
                vc.inc[k] = phaseInc(vc.fixed[k] ? (double) (vc.base[k] + penv) : (double) vc.base[k] + ratioPitch);
                if (alg.car[k] && (att < 3900 || vc.att0[k] < 3900)) silent = false;
            }
            if (silent) vc.active = false;
        }
    }
    double dxSample() {
        if (blockPos == 0) control();
        const int i = blockPos; blockPos = (blockPos + 1) & (N - 1);
        const Alg& alg = algs()[voice[P::ALG]]; const int fbAmt = voice[P::FB];
        const auto& T = tables();
        const bool mk1 = func[F::dac] == 0; const double amt = func[F::hiss] / 50.0;
        int acc = 0, live = 0; double an = 0;
        for (auto& vc : voices) {
            if (!vc.active) continue;
            live++;
            int sum = 0;
            for (int k = 5; k >= 0; --k) {
                const int a = vc.att0[k] + (((vc.att1[k] - vc.att0[k]) * i) >> 6);
                uint32_t ph = vc.phase[k];
                const int mm = alg.mod[k];
                if (mm) { int m = 0; for (int s = 0; s < 6; ++s) if ((mm >> s) & 1) m += vc.out[s]; ph += (uint32_t) ((int64_t) m * 2097152); }
                if (k == alg.fb && fbAmt) ph += (uint32_t) ((int64_t) (vc.fb0 + vc.fb1) * ((int64_t) 1 << (12 + fbAmt)));
                const int idx = (int) ((ph >> 18) & 16383);
                int y;
                if (a >= 4095) y = 0;
                else {
                    const int q = idx & 4095, t = ((idx & 4096) ? T.logsin[4095 - q] : T.logsin[q]) + a;
                    y = T.expt[t & 255] >> (t >> 8);
                    if (idx & 8192) y = -y;
                }
                vc.out[k] = y;
                if (k == alg.fbs) { vc.fb0 = vc.fb1; vc.fb1 = y; }
                if (alg.car[k]) sum += y;
                vc.phase[k] += vc.inc[k];
            }
            if (mk1) an += dac.voice(sum, amt); else acc += sum;
        }
        if (mk1) return dac.out(an, volSteps()[(size_t) jsRound(func[F::volume] / 99.0 * 7)], VOICES - live, amt);
        if (acc > 32767) acc = 32767; else if (acc < -32768) acc = -32768;
        return acc;
    }
    void render(float* out, int n) {
        const double vol = func[F::dac] == 0 ? 1.0 : func[F::volume] / 99.0, gain = 2.0 / 32768 * vol * vol * (ctl.volume / 127.0) * (ctl.expression / 127.0);
        for (int j = 0; j < n; ++j) {
            while (rs.count < rs.need) {
                const double x = dxSample();
                for (int s = 0; s < zoh; ++s) rs.push(bq[1].run(bq[0].run(x)));
            }
            const double y = rs.next() * gain;
            out[j] = (float) y;
            const double ay = y < 0 ? -y : y; if (ay > peak) peak = ay;
        }
    }
    void renderStereo(float* L, float* R, int n) { render(L, n); fx.process(L, R, n, func); }
    int activeVoices() const { int c = 0; for (auto& v : voices) if (v.active) c++; return c; }
};

/* ---------------- display helpers ---------------- */
inline std::string noteName(int n) { static const char* NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; return std::string(NN[((n % 12) + 12) % 12]) + std::to_string((int) std::floor(n / 12.0) - 2); }
inline std::string bpName(int bp) { static const char* NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; const int n = bp + 9; return std::string(NN[n % 12]) + std::to_string(n / 12 - 1); }
static const char* const CURVES[4] = {"-LIN", "-EXP", "+EXP", "+LIN"};
static const char* const WAVES[6] = {"TRIANGL", "SAW DWN", "SAW UP", "SQUARE", "SINE", "S/HOLD"};
inline std::string fixedDigits(double x, int d) { char b[32]; std::snprintf(b, sizeof b, "%.*f", d, x); return b; }
inline std::string freqText(const Voice156& v, int op) {
    const int b = opBase(op), c = v[(size_t) (b + 18)], f = v[(size_t) (b + 19)];
    if (v[(size_t) (b + 17)]) { const double hz = std::pow(10.0, (c & 3) + f / 100.0); return (hz < 10 ? fixedDigits(hz, 3) : hz < 100 ? fixedDigits(hz, 2) : hz < 1000 ? fixedDigits(hz, 1) : fixedDigits(hz, 0)) + "Hz"; }
    return fixedDigits((c == 0 ? 0.5 : c) * (1 + f / 100.0), 2);
}

/* ======================= sequencer (seq.js) ======================= */
constexpr int PPQ = 96, MAX_STEPS = 64, LANES = 4, PATTERNS = 16, STEP_NOTES = 8, TIE = 101;
static const int RATE_TICKS[6] = {96, 48, 32, 24, 16, 12};
static const char* const RATE_NAMES[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char* const ARP_MODES[8] = {"UP", "DOWN", "UP/DOWN", "DOWN/UP", "RANDOM", "AS PLAYED", "CHORD", "UP+DOWN"};
struct Step { bool on = true; std::vector<std::pair<int, int>> notes; int gate = 75; };
struct Lane { int param = -1; bool smooth = false; std::array<int, MAX_STEPS> vals; Lane() { vals.fill(-1); } };
struct Pattern { int len = 16, rate = 3, swing = 50; std::array<Step, MAX_STEPS> steps; std::array<Lane, LANES> lanes; };
struct Arp { bool on = false; int mode = 0, oct = 1, rate = 3, gate = 50; bool latch = false; int swing = 50; };
struct SeqState { double tempo = 120; bool ext = false; int pat = 0; std::vector<int> chain; bool keyTrans = true; int transBase = 60; Arp arp; std::array<Pattern, PATTERNS> pats; };

struct SeqHost { virtual ~SeqHost() = default; virtual void seqNoteOn(int n, int v) = 0; virtual void seqNoteOff(int n) = 0; virtual void seqParam(int id, int v) = 0; };

class Sequencer {
public:
    SeqHost* h = nullptr;
    SeqState s;
    bool playing = false, rec = false;
    int64_t tick = 0, abs = 0; int step = -1, chainIx = 0;
    std::vector<std::pair<int, int64_t>> sounding, arpSounding;
    std::vector<std::pair<int, int>> arpHeld, arpLatched; int64_t arpIx = 0, arpTick = 0; bool arpRun = false;
    int trans = 0; uint32_t rand = 12345;
    std::vector<std::pair<int, std::pair<int, int64_t>>> recHeld;   // note -> (step, abs)
    int stepCursor = 0, stepHeld = 0;
    int smoothLast[LANES] = {-1, -1, -1, -1};

    Pattern& pat() { return s.pats[(size_t) s.pat]; }
    int stepTicks(const Pattern& p) const { return RATE_TICKS[p.rate]; }
    void start() { flush(); playing = true; tick = 0; step = -1; chainIx = 0; trans = 0; if (!s.chain.empty()) s.pat = s.chain[0]; }
    void stop() { playing = false; flush(); recHeld.clear(); }
    void flush() { for (auto& x : sounding) h->seqNoteOff(x.first); sounding.clear(); }
    void flushArp() { for (auto& x : arpSounding) h->seqNoteOff(x.first); arpSounding.clear(); }
    void setArp(bool on) { s.arp.on = on; if (!on) { arpRun = false; arpHeld.clear(); arpLatched.clear(); flushArp(); } }
    bool patHasNotes() { auto& p = pat(); for (int i = 0; i < p.len; ++i) if (!p.steps[(size_t) i].notes.empty()) return true; return false; }

    bool keyOn(int n, int v) {
        if (s.arp.on) {
            if (s.arp.latch && arpHeld.empty()) arpLatched.clear();
            arpHeld.push_back({n, v});
            if (s.arp.latch) arpLatched.push_back({n, v});
            if (!arpRun) { arpRun = true; arpIx = 0; arpTick = 0; }
            if (rec) recordOn(n, v);
            return true;
        }
        if (rec) { recordOn(n, v); return false; }
        if (playing && s.keyTrans && patHasNotes()) { trans = n - s.transBase; return true; }
        return false;
    }
    bool keyOff(int n) {
        if (s.arp.on) {
            arpHeld.erase(std::remove_if(arpHeld.begin(), arpHeld.end(), [n](auto& x) { return x.first == n; }), arpHeld.end());
            if (arpHeld.empty() && !s.arp.latch) { arpRun = false; flushArp(); }
            if (rec) recordOff(n);
            return true;
        }
        if (rec) { recordOff(n); return false; }
        return playing && s.keyTrans && patHasNotes();
    }
    void recordOn(int n, int v) {
        auto& p = pat();
        if (playing) {
            const int st = stepTicks(p), si = (int) (jsRound((double) tick / st)) % p.len;
            auto& stp = p.steps[(size_t) si];
            bool has = false; for (auto& x : stp.notes) if (x.first == n) has = true;
            if (!has && (int) stp.notes.size() < STEP_NOTES) stp.notes.push_back({n, v});
            stp.on = true;
            for (auto& r : recHeld) if (r.first == n) { r.second = {si, abs}; return; }
            recHeld.push_back({n, {si, abs}});
        } else {
            auto& stp = p.steps[(size_t) stepCursor];
            if (stepHeld == 0) stp.notes.clear();
            if ((int) stp.notes.size() < STEP_NOTES) stp.notes.push_back({n, v});
            stp.on = true; stp.gate = 75; stepHeld++;
        }
    }
    void recordOff(int n) {
        auto& p = pat();
        if (playing) {
            for (size_t i = 0; i < recHeld.size(); ++i) if (recHeld[i].first == n) {
                const auto r = recHeld[i].second; recHeld.erase(recHeld.begin() + (long) i);
                p.steps[(size_t) r.first].gate = std::max(5, std::min(TIE, (int) jsRound((double) (abs - r.second) / stepTicks(p) * 100)));
                return;
            }
        } else {
            stepHeld = std::max(0, stepHeld - 1);
            if (stepHeld == 0) stepCursor = (stepCursor + 1) % p.len;
        }
    }
    void stepRest() { auto& p = pat(); p.steps[(size_t) stepCursor].notes.clear(); stepCursor = (stepCursor + 1) % p.len; }
    bool recordParam(int id, int v) {
        if (!rec || !playing) return false;
        auto& p = pat(); Lane* lane = nullptr;
        for (auto& l : p.lanes) if (l.param == id) { lane = &l; break; }
        if (!lane) { for (auto& l : p.lanes) if (l.param < 0) { lane = &l; break; } if (!lane) return false; lane->param = id; lane->vals.fill(-1); }
        lane->vals[(size_t) (std::max(0, step) % p.len)] = v;
        return true;
    }
    void doTick() {
        if (!sounding.empty()) { std::vector<std::pair<int, int64_t>> keep; for (auto& x : sounding) { if (x.second <= abs) h->seqNoteOff(x.first); else keep.push_back(x); } sounding.swap(keep); }
        if (!arpSounding.empty()) { std::vector<std::pair<int, int64_t>> keep; for (auto& x : arpSounding) { if (x.second <= abs) h->seqNoteOff(x.first); else keep.push_back(x); } arpSounding.swap(keep); }
        if (playing) {
            Pattern* p = &pat(); int st = stepTicks(*p);
            if (tick >= (int64_t) p->len * st) {
                tick = 0;
                if (s.chain.size() > 1) { chainIx = (chainIx + 1) % (int) s.chain.size(); s.pat = s.chain[(size_t) chainIx]; p = &pat(); st = stepTicks(*p); }
            }
            const int si = (int) (tick / st), pos = (int) (tick - (int64_t) si * st), sw = (int) jsRound((p->swing - 50) / 50.0 * st);
            if (pos == ((si & 1) ? sw : 0)) { step = si; playStep(*p, si); }
            else if (pos > 0) motionSmooth(*p, st, pos);
            tick++;
        }
        if (s.arp.on && arpRun) arpStep();
        abs++;
    }
    void playStep(Pattern& p, int si) {
        auto& stp = p.steps[(size_t) si]; const int st = stepTicks(p);
        for (int i = 0; i < LANES; ++i) { auto& l = p.lanes[(size_t) i]; if (l.param >= 0 && l.vals[(size_t) si] >= 0) { h->seqParam(l.param, l.vals[(size_t) si]); smoothLast[i] = l.vals[(size_t) si]; } }
        if (!stp.on || stp.notes.empty()) return;
        int64_t len = std::max(1, (int) jsRound(st * stp.gate / 100.0));
        if (stp.gate >= TIE) { int k = 1; while (k < p.len && p.steps[(size_t) ((si + k) % p.len)].gate >= TIE) k++; len = (int64_t) st * k + 1; }
        auto& prev = p.steps[(size_t) ((si + p.len - 1) % p.len)]; const bool tied = prev.on && prev.gate >= TIE;
        for (auto& nv : stp.notes) {
            const int nn = std::max(0, std::min(127, nv.first + trans));
            int was = -1; for (size_t i = 0; i < sounding.size(); ++i) if (sounding[i].first == nn) { was = (int) i; break; }
            bool inPrev = false; for (auto& x : prev.notes) if (x.first == nv.first) inPrev = true;
            if (was >= 0 && tied && inPrev) { sounding[(size_t) was].second = std::max(sounding[(size_t) was].second, abs + len); continue; }
            if (was >= 0) { h->seqNoteOff(nn); sounding.erase(sounding.begin() + was); }
            h->seqNoteOn(nn, nv.second);
            sounding.push_back({nn, abs + len});
        }
    }
    void motionSmooth(Pattern& p, int st, int pos) {
        if (step < 0) return;
        for (int i = 0; i < LANES; ++i) {
            auto& l = p.lanes[(size_t) i];
            if (!l.smooth || l.param < 0) continue;
            const int a = l.vals[(size_t) step], b = l.vals[(size_t) ((step + 1) % p.len)];
            if (a < 0 || b < 0 || a == b) continue;
            const int v = (int) jsRound(a + (b - a) * (double) pos / st);
            if (v != smoothLast[i]) { smoothLast[i] = v; h->seqParam(l.param, v); }
        }
    }
    std::vector<std::pair<int, int>> arpNotes() {
        const auto& a = s.arp;
        auto base = a.latch ? arpLatched : arpHeld;
        if (a.mode != 5) std::stable_sort(base.begin(), base.end(), [](auto& x, auto& y) { return x.first < y.first; });
        std::vector<std::pair<int, int>> up;
        for (int o = 0; o < a.oct; ++o) for (auto& nv : base) if (nv.first + 12 * o < 128) up.push_back({nv.first + 12 * o, nv.second});
        auto rev = [](std::vector<std::pair<int, int>> v) { std::reverse(v.begin(), v.end()); return v; };
        switch (a.mode) {
            case 1: return rev(up);
            case 2: { if (up.size() <= 2) return up; auto r = up; auto mid = std::vector<std::pair<int, int>>(up.begin() + 1, up.end() - 1); auto m2 = rev(mid); r.insert(r.end(), m2.begin(), m2.end()); return r; }
            case 3: { auto d = rev(up); if (d.size() <= 2) return d; auto r = d; auto mid = std::vector<std::pair<int, int>>(d.begin() + 1, d.end() - 1); auto m2 = rev(mid); r.insert(r.end(), m2.begin(), m2.end()); return r; }
            case 7: { auto r = up; auto m2 = rev(up); r.insert(r.end(), m2.begin(), m2.end()); return r; }
            default: return up;
        }
    }
    void arpStep() {
        const auto& a = s.arp; const int st = RATE_TICKS[a.rate];
        const int64_t si = arpTick / st, pos = arpTick - si * st; const int sw = (int) jsRound((a.swing - 50) / 50.0 * st);
        if (pos == ((si & 1) ? sw : 0)) {
            const auto list = arpNotes();
            if (!list.empty()) {
                const int64_t len = std::max(1, (int) jsRound(st * a.gate / 100.0));
                std::vector<std::pair<int, int>> pick;
                if (a.mode == 6) pick = list;
                else if (a.mode == 4) { uint32_t r = rand; r ^= r << 13; r ^= r >> 17; r ^= r << 5; rand = r; pick = {list[rand % list.size()]}; }
                else pick = {list[(size_t) (arpIx % (int64_t) list.size())]};
                arpIx++;
                for (auto& nv : pick) {
                    for (size_t i = 0; i < arpSounding.size(); ++i) if (arpSounding[i].first == nv.first) { h->seqNoteOff(nv.first); arpSounding.erase(arpSounding.begin() + (long) i); break; }
                    h->seqNoteOn(nv.first, nv.second);
                    arpSounding.push_back({nv.first, abs + len});
                }
            }
        }
        arpTick++;
    }
};

/* ---------------- standard MIDI files ---------------- */
struct MidiEvent { int64_t tick = 0; int track = 0, seq = 0; int tempo = 0; std::vector<uint8_t> data; };
struct MidiFile { int format = 0, ppq = 96; std::vector<MidiEvent> events; bool ok = false; std::string error; };
inline MidiFile parseMidiFile(const uint8_t* d, size_t n) {
    MidiFile mf;
    auto u32 = [&](size_t o) { return o + 4 <= n ? (uint32_t) ((d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]) : 0u; };
    if (n < 14 || std::memcmp(d, "MThd", 4) != 0) { mf.error = "not a MIDI file"; return mf; }
    const uint32_t hl = u32(4);
    mf.format = (d[8] << 8) | d[9]; const int ntr = (d[10] << 8) | d[11], div = (d[12] << 8) | d[13];
    if (div & 0x8000) { mf.error = "SMPTE-timed MIDI files are not supported"; return mf; }
    mf.ppq = div ? div : 96;
    size_t o = 8 + hl;
    for (int t = 0; t < ntr && o + 8 <= n; ++t) {
        const uint32_t len = u32(o + 4);
        if (std::memcmp(d + o, "MTrk", 4) != 0) { o += 8 + len; --t; if (o >= n) break; continue; }
        const size_t end = std::min(n, o + 8 + len);
        size_t i = o + 8; int64_t tick = 0; int run = 0, seq = 0;
        auto var = [&]() { uint32_t v = 0; uint8_t b; do { b = d[i++]; v = (v << 7) | (b & 127); } while ((b & 128) && i < end); return v; };
        while (i < end) {
            tick += var();
            if (i >= end) break;
            int st = d[i];
            if (st < 0x80) st = run; else { ++i; if (st < 0xF0) run = st; }
            if (st == 0xFF) {
                const int ty = d[i++]; const uint32_t l = var();
                if (ty == 0x51 && l == 3 && i + 3 <= end) { MidiEvent e; e.tick = tick; e.track = t; e.seq = seq++; e.tempo = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2]; mf.events.push_back(e); }
                i += l;
                if (ty == 0x2F) break;
                continue;
            }
            if (st == 0xF0 || st == 0xF7) {
                const uint32_t l = var(); MidiEvent e; e.tick = tick; e.track = t; e.seq = seq++;
                if (st == 0xF0) e.data.push_back(0xF0);
                for (uint32_t k = 0; k < l && i + k < end; ++k) e.data.push_back(d[i + k]);
                i += l; mf.events.push_back(e); continue;
            }
            const int ty = st & 0xF0, cnt = (ty == 0xC0 || ty == 0xD0) ? 1 : 2;
            MidiEvent e; e.tick = tick; e.track = t; e.seq = seq++; e.data.push_back((uint8_t) st);
            for (int k = 0; k < cnt && i < end; ++k) e.data.push_back(d[i++] & 127);
            mf.events.push_back(e);
        }
        o = end;
    }
    std::stable_sort(mf.events.begin(), mf.events.end(), [](const MidiEvent& a, const MidiEvent& b) { return a.tick != b.tick ? a.tick < b.tick : a.track != b.track ? a.track < b.track : a.seq < b.seq; });
    mf.ok = true;
    return mf;
}
struct TimedMsg { double t; std::vector<uint8_t> data; };
inline std::vector<TimedMsg> timeMidiFile(const MidiFile& mf) {
    double tempo = 500000, sec = 0; int64_t last = 0; std::vector<TimedMsg> out;
    for (auto& e : mf.events) {
        sec += (double) (e.tick - last) * tempo / 1e6 / mf.ppq; last = e.tick;
        if (e.tempo) { tempo = e.tempo; continue; }
        out.push_back({sec, e.data});
    }
    return out;
}
inline void writeVar(std::vector<uint8_t>& a, uint32_t v) { uint8_t s[5]; int n = 0; s[n++] = v & 127; while ((v >>= 7) > 0) s[n++] = (uint8_t) ((v & 127) | 128); while (n) a.push_back(s[--n]); }
struct TickMsg { int64_t tick; std::vector<uint8_t> data; };
inline std::vector<uint8_t> writeMidiFile(std::vector<TickMsg> ev, int ppq, double bpm, const std::string& name) {
    std::stable_sort(ev.begin(), ev.end(), [](const TickMsg& a, const TickMsg& b) { return a.tick < b.tick; });
    std::vector<uint8_t> tr;
    if (!name.empty()) { tr.insert(tr.end(), {0, 0xFF, 0x03}); writeVar(tr, (uint32_t) name.size()); tr.insert(tr.end(), name.begin(), name.end()); }
    const int us = (int) jsRound(60e6 / (bpm > 0 ? bpm : 120));
    tr.insert(tr.end(), {0, 0xFF, 0x51, 3, (uint8_t) ((us >> 16) & 255), (uint8_t) ((us >> 8) & 255), (uint8_t) (us & 255)});
    int64_t last = 0;
    for (auto& e : ev) {
        writeVar(tr, (uint32_t) std::max<int64_t>(0, e.tick - last)); last = std::max(last, e.tick);
        if (!e.data.empty() && e.data[0] == 0xF0) { tr.push_back(0xF0); writeVar(tr, (uint32_t) e.data.size() - 1); tr.insert(tr.end(), e.data.begin() + 1, e.data.end()); }
        else tr.insert(tr.end(), e.data.begin(), e.data.end());
    }
    tr.insert(tr.end(), {0, 0xFF, 0x2F, 0});
    std::vector<uint8_t> out = {0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, (uint8_t) ((ppq >> 8) & 255), (uint8_t) (ppq & 255), 0x4D, 0x54, 0x72, 0x6B};
    const uint32_t L = (uint32_t) tr.size();
    out.insert(out.end(), {(uint8_t) (L >> 24), (uint8_t) (L >> 16), (uint8_t) (L >> 8), (uint8_t) L});
    out.insert(out.end(), tr.begin(), tr.end());
    return out;
}
inline Pattern emptyPattern() { return Pattern(); }
inline Pattern patternFromMidi(const MidiFile& mf, int rate, double& tempo, int& count) {
    Pattern p; p.rate = rate;
    const double stepTk = mf.ppq * RATE_TICKS[p.rate] / (double) PPQ;
    tempo = 120; count = 0; int maxStep = 0;
    std::vector<std::pair<int, std::pair<int, int64_t>>> on;
    for (auto& e : mf.events) {
        if (e.tempo) { if (e.tick == 0 || tempo == 120) tempo = jsRound(60e6 / e.tempo * 10) / 10; continue; }
        const auto& m = e.data; if (m.empty()) continue;
        const int s = (int) jsRound((double) e.tick / stepTk);
        if (m[0] == 0xF0) {
            const auto r = parseSysexMessage(m.data(), m.size());
            if (r.kind == SysexMsg::param && s < MAX_STEPS) {
                Lane* lane = nullptr;
                for (auto& l : p.lanes) if (l.param == r.n) { lane = &l; break; }
                if (!lane) for (auto& l : p.lanes) if (l.param < 0) { lane = &l; break; }
                if (lane) { lane->param = r.n; lane->vals[(size_t) s] = r.v; maxStep = std::max(maxStep, s); }
            }
            continue;
        }
        const int ty = m[0] & 0xF0;
        if (ty == 0x90 && m.size() > 2 && m[2] > 0) {
            if (s >= MAX_STEPS) continue;
            auto& st = p.steps[(size_t) s];
            bool has = false; for (auto& x : st.notes) if (x.first == m[1]) has = true;
            if ((int) st.notes.size() < STEP_NOTES && !has) { st.notes.push_back({m[1], m[2]}); count++; }
            bool found = false; for (auto& x : on) if (x.first == m[1]) { x.second = {s, e.tick}; found = true; }
            if (!found) on.push_back({m[1], {s, e.tick}});
            maxStep = std::max(maxStep, s);
        } else if (ty == 0x80 || (ty == 0x90 && m.size() > 2 && m[2] == 0)) {
            for (size_t i = 0; i < on.size(); ++i) if (on[i].first == m[1]) {
                const auto r = on[i].second; on.erase(on.begin() + (long) i);
                p.steps[(size_t) r.first].gate = std::max(5, std::min(TIE, (int) jsRound((double) (e.tick - r.second) / stepTk * 100)));
                break;
            }
        }
    }
    p.len = std::min(MAX_STEPS, std::max(16, (int) std::ceil((maxStep + 1) / 16.0) * 16));
    return p;
}
inline std::vector<uint8_t> patternToMidi(const Pattern& p, const Voice156* voice, double tempo, int ch, int loops) {
    std::vector<TickMsg> ev; const int st = RATE_TICKS[p.rate], c = ch & 15;
    if (voice) ev.push_back({0, voiceToSysex(*voice, c)});
    for (int L = 0; L < std::max(1, loops); ++L)
        for (int s = 0; s < p.len; ++s) {
            const int sw = (s % 2) ? (int) jsRound((p.swing - 50) / 50.0 * st) : 0;
            const int64_t t = (int64_t) (L * p.len + s) * st + sw;
            for (auto& l : p.lanes) if (l.param >= 0 && l.param <= 155 && l.vals[(size_t) s] >= 0) ev.push_back({t, paramChange(c, 0, l.param, l.vals[(size_t) s])});
            auto& stp = p.steps[(size_t) s];
            if (!stp.on) continue;
            const int len = std::max(1, (int) jsRound(st * std::min(stp.gate, TIE) / 100.0));
            for (auto& nv : stp.notes) { ev.push_back({t, {(uint8_t) (0x90 | c), (uint8_t) nv.first, (uint8_t) nv.second}}); ev.push_back({t + len, {(uint8_t) (0x80 | c), (uint8_t) nv.first, 0}}); }
        }
    std::stable_sort(ev.begin(), ev.end(), [](const TickMsg& a, const TickMsg& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return ((a.data[0] & 0xF0) == 0x80 ? -1 : 0) < ((b.data[0] & 0xF0) == 0x80 ? -1 : 0);
    });
    return writeMidiFile(ev, 96, tempo, "Omega Seven pattern");
}
struct RecEvent { double sec; std::vector<uint8_t> data; };
inline std::vector<uint8_t> recordingToMidi(const std::vector<RecEvent>& evs, const Voice156* voice, int ch) {
    std::vector<TickMsg> ev; const int ppq = 480;
    if (voice) ev.push_back({0, voiceToSysex(*voice, ch)});
    for (auto& e : evs) ev.push_back({(int64_t) jsRound(e.sec * 2 * ppq), e.data});
    return writeMidiFile(ev, ppq, 120, "Omega Seven performance");
}
class FilePlayer {
public:
    std::vector<TimedMsg> evs; size_t ix = 0; double t = 0; bool playing = false, loop = false; std::string name;
    void load(const MidiFile& mf, const std::string& nm) { evs = timeMidiFile(mf); ix = 0; t = 0; name = nm; }
    void start() { ix = 0; t = 0; playing = !evs.empty(); }
    void stop() { playing = false; }
    template <class F> void run(double dt, F&& send) {
        if (!playing) return;
        t += dt;
        while (ix < evs.size() && evs[ix].t <= t) { send(evs[ix].data); ix++; }
        if (ix >= evs.size()) { if (loop) { ix = 0; t = 0; } else playing = false; }
    }
};

}  // namespace seven
