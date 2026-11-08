// Omega SID 64: a playable synth built on the SID circuit model (SidChip.h).
//
// Pure C++ (no JUCE): the VST3 plugin and the Android app both run this. Everything that changes over time is
// updated on "ticks" at the C64's frame rate (50 Hz PAL by default), the way C64 music players drive the chip:
// pitch slides, vibrato, pulse-width sweeps, the software filter envelope, arpeggios and the attack programs
// (per-frame waveform / pitch tables, like a tracker's wavetable). Gates are written the moment a note arrives.
//
// Polyphony is multi-chip: in POLY each note gets its own SID with all three oscillators stacked; 3-VOICE plays
// a single chip with one note per oscillator, like a C64.
#pragma once
#include "SidChip.h"
#include "SidSampler.h"
#include "../dsp/Rev7.h"
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace sid {

const omega::Spec& rev7Spec();   // Rev7Spec.cpp: the Omega REV7's parameters and presets

// ---------------------------------------------------------------------------------------------------------------
// parameters
enum Kind : uint8_t { CONT, INT, TOGGLE, CHOICE };
struct ParamInfo {
    const char* key; const char* name; const char* section;
    float min, max, def; Kind kind; const char* choices; float skew; const char* unit; bool preset;
};

#define SID_OSC_PARAMS(n) \
    X(o##n##on,  "OSC " #n,      "OSC " #n, 0, 1, (n == 1), TOGGLE, "", 1, "", 1) \
    X(o##n##tri, "TRIANGLE",     "OSC " #n, 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(o##n##saw, "SAWTOOTH",     "OSC " #n, 0, 1, 1, TOGGLE, "", 1, "", 1) \
    X(o##n##pul, "PULSE",        "OSC " #n, 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(o##n##noi, "NOISE",        "OSC " #n, 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(o##n##crs, "COARSE",       "OSC " #n, -24, 24, 0, INT, "", 1, "ST", 1) \
    X(o##n##fin, "FINE",         "OSC " #n, -100, 100, 0, CONT, "", 1, "CT", 1) \
    X(o##n##pw,  "PULSE WIDTH",  "OSC " #n, 0, 100, 50, CONT, "", 1, "%", 1) \
    X(o##n##pwm, "PWM DEPTH",    "OSC " #n, 0, 50, 0, CONT, "", 1, "%", 1) \
    X(o##n##pwr, "PWM RATE",     "OSC " #n, 0.05f, 20, 0.5f, CONT, "", 3, "HZ", 1) \
    X(o##n##ring,"RING MOD",     "OSC " #n, 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(o##n##sync,"HARD SYNC",    "OSC " #n, 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(o##n##flt, "TO FILTER",    "OSC " #n, 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(o##n##a,   "ATTACK",       "OSC " #n, 0, 15, 0, INT, "", 1, "", 1) \
    X(o##n##d,   "DECAY",        "OSC " #n, 0, 15, 8, INT, "", 1, "", 1) \
    X(o##n##s,   "SUSTAIN",      "OSC " #n, 0, 15, 12, INT, "", 1, "", 1) \
    X(o##n##r,   "RELEASE",      "OSC " #n, 0, 15, 4, INT, "", 1, "", 1)

#define SID_PARAMS \
    X(model,  "CHIP",          "CHIP", 0, 1, 0, CHOICE, "6581|8580", 1, "", 1) \
    X(curve,  "6581 CURVE",    "CHIP", -1, 1, 0, CONT, "", 1, "", 1) \
    X(drive,  "FILTER DRIVE",  "CHIP", 0, 1, 0.5f, CONT, "", 1, "", 1) \
    X(clock,  "CLOCK",         "CHIP", 0, 1, 0, CHOICE, "PAL|NTSC", 1, "", 1) \
    X(adsrbug,"ADSR BUG",      "CHIP", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(vol,    "SID VOLUME",    "CHIP", 0, 15, 15, INT, "", 1, "", 1) \
    X(quality,"OVERSAMPLE",    "CHIP", 0, 2, 1, CHOICE, "2X|4X|8X", 1, "", 0) \
    X(spread, "STEREO SPREAD", "CHIP", 0, 1, 0.5f, CONT, "", 1, "", 1) \
    X(gain,   "OUTPUT",        "CHIP", -24, 12, 0, CONT, "", 1, "DB", 0) \
    X(mode,   "PLAY MODE",     "PLAY", 0, 3, 0, CHOICE, "POLY|MONO|LEGATO|3-VOICE", 1, "", 1) \
    X(poly,   "CHIPS (POLY)",  "PLAY", 1, 8, 6, INT, "", 1, "", 1) \
    X(glide,  "GLIDE",         "PLAY", 0, 2000, 0, CONT, "", 3, "MS", 1) \
    X(bend,   "BEND RANGE",    "PLAY", 0, 24, 2, INT, "", 1, "ST", 1) \
    X(velcut, "VEL>CUTOFF",    "PLAY", -1, 1, 0, CONT, "", 1, "", 1) \
    X(vellvl, "VEL>LEVEL",     "PLAY", 0, 1, 0.3f, CONT, "", 1, "", 1) \
    X(hard,   "HARD RESTART",  "PLAY", 0, 1, 1, TOGGLE, "", 1, "", 1) \
    X(tick,   "TICK RATE",     "PLAY", 0, 3, 0, CHOICE, "50HZ|60HZ|100HZ|200HZ", 1, "", 1) \
    X(mw,     "WHEEL>VIBRATO", "PLAY", 0, 100, 40, CONT, "", 1, "CT", 1) \
    SID_OSC_PARAMS(1) SID_OSC_PARAMS(2) SID_OSC_PARAMS(3) \
    X(cut,    "CUTOFF",        "FILTER", 0, 2047, 1024, INT, "", 1, "", 1) \
    X(res,    "RESONANCE",     "FILTER", 0, 15, 0, INT, "", 1, "", 1) \
    X(lp,     "LOW PASS",      "FILTER", 0, 1, 1, TOGGLE, "", 1, "", 1) \
    X(bp,     "BAND PASS",     "FILTER", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(hp,     "HIGH PASS",     "FILTER", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(off3,   "VOICE 3 OFF",   "FILTER", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(fenv,   "ENV AMOUNT",    "FILTER", -2047, 2047, 0, CONT, "", 1, "", 1) \
    X(fa,     "ENV ATTACK",    "FILTER", 0, 3000, 5, CONT, "", 3, "MS", 1) \
    X(fd,     "ENV DECAY",     "FILTER", 0, 3000, 300, CONT, "", 3, "MS", 1) \
    X(fs,     "ENV SUSTAIN",   "FILTER", 0, 1, 0, CONT, "", 1, "", 1) \
    X(fr,     "ENV RELEASE",   "FILTER", 0, 3000, 300, CONT, "", 3, "MS", 1) \
    X(key,    "KEY TRACK",     "FILTER", 0, 1, 0, CONT, "", 1, "", 1) \
    X(flfo,   "LFO>CUTOFF",    "FILTER", 0, 1024, 0, CONT, "", 2, "", 1) \
    X(lrate,  "LFO RATE",      "MOD", 0.05f, 20, 5, CONT, "", 3, "HZ", 1) \
    X(lshape, "LFO SHAPE",     "MOD", 0, 3, 0, CHOICE, "TRIANGLE|SAW|SQUARE|RANDOM", 1, "", 1) \
    X(vib,    "VIBRATO",       "MOD", 0, 100, 0, CONT, "", 2, "CT", 1) \
    X(vdel,   "VIB DELAY",     "MOD", 0, 2000, 0, CONT, "", 2, "MS", 1) \
    X(prog,   "ATTACK PROGRAM","MOD", 0, 17, 0, CHOICE, "NONE|NOISE HIT|PLUCK|SNARE|KICK|TOM|HIHAT|CLAP|ZAP|LASER|OCT BLIP|BELL|DRUM BASS|PITCH DROP|COIN|SAW-PUL LOOP|OCT TRILL|NOISE TAIL", 1, "", 1) \
    X(ptgt,   "PROGRAM ON",    "MOD", 0, 1, 0, CHOICE, "OSC 1|ALL", 1, "", 1) \
    X(arp,    "ARP",           "ARP", 0, 5, 0, CHOICE, "OFF|UP|DOWN|UP-DOWN|RANDOM|AS PLAYED", 1, "", 1) \
    X(chord,  "CHORD",         "ARP", 0, 13, 0, CHOICE, "NONE|OCTAVE|MAJOR|MINOR|MAJ7|MIN7|DOM7|SUS4|POWER|DIM|AUG|OCT+5TH|MAJ9|MIN9", 1, "", 1) \
    X(aspd,   "ARP SPEED",     "ARP", 1, 16, 2, INT, "", 1, "TICKS", 1) \
    X(async,  "ARP TEMPO SYNC","ARP", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(adiv,   "ARP DIVISION",  "ARP", 0, 4, 2, CHOICE, "1/8|1/16|1/16T|1/32|1/64", 1, "", 1) \
    X(aoct,   "ARP OCTAVES",   "ARP", 1, 4, 1, INT, "", 1, "", 1) \
    X(emix,   "ECHO MIX",      "ECHO", 0, 1, 0, CONT, "", 1, "", 1) \
    X(etime,  "ECHO TIME",     "ECHO", 10, 1000, 300, CONT, "", 2, "MS", 1) \
    X(esync,  "ECHO SYNC",     "ECHO", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(ediv,   "ECHO DIVISION", "ECHO", 0, 5, 2, CHOICE, "1/4|1/8.|1/8|1/8T|1/16|1/16.", 1, "", 1) \
    X(efb,    "ECHO FEEDBACK", "ECHO", 0, 0.9f, 0.35f, CONT, "", 1, "", 1) \
    X(etone,  "ECHO TONE",     "ECHO", 0, 1, 0.5f, CONT, "", 1, "", 1) \
    X(m1lvl,  "OSC 1 LEVEL",   "MIX", -48, 6, 0, CONT, "", 1, "DB", 1) \
    X(m2lvl,  "OSC 2 LEVEL",   "MIX", -48, 6, 0, CONT, "", 1, "DB", 1) \
    X(m3lvl,  "OSC 3 LEVEL",   "MIX", -48, 6, 0, CONT, "", 1, "DB", 1) \
    X(mflvl,  "FILTER LEVEL",  "MIX", -48, 6, 0, CONT, "", 1, "DB", 1) \
    X(m1pan,  "OSC 1 PAN",     "MIX", -1, 1, 0, CONT, "", 1, "", 1) \
    X(m2pan,  "OSC 2 PAN",     "MIX", -1, 1, 0, CONT, "", 1, "", 1) \
    X(m3pan,  "OSC 3 PAN",     "MIX", -1, 1, 0, CONT, "", 1, "", 1) \
    X(mfpan,  "FILTER PAN",    "MIX", -1, 1, 0, CONT, "", 1, "", 1) \
    X(m1mute, "OSC 1 MUTE",    "MIX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(m2mute, "OSC 2 MUTE",    "MIX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(m3mute, "OSC 3 MUTE",    "MIX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(mfmute, "FILTER MUTE",   "MIX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(m1solo, "OSC 1 SOLO",    "MIX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(m2solo, "OSC 2 SOLO",    "MIX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(m3solo, "OSC 3 SOLO",    "MIX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(mfsolo, "FILTER SOLO",   "MIX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(emute,  "ECHO MUTE",     "MIX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(dmode,  "SAMPLER",       "SMP", 0, 4, 0, CHOICE, "OFF|$D418 4-BIT|MAHONEY 8-BIT|PWM 8-BIT|DIGIMAX 8-BIT", 1, "", 1) \
    X(drate,  "SAMPLE RATE",   "SMP", 0, 9, 4, CHOICE, "3.9K|4K|5.5K|6.3K|7.8K|8K|10K|11K|16K|22K", 1, "", 1) \
    X(droot,  "ROOT KEY",      "SMP", 24, 96, 60, INT, "", 1, "", 1) \
    X(dstart, "START",         "SMP", 0, 1, 0, CONT, "", 1, "", 1) \
    X(dend,   "END",           "SMP", 0, 1, 1, CONT, "", 1, "", 1) \
    X(dloop,  "LOOP",          "SMP", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(dgate,  "PLAY",          "SMP", 0, 1, 0, CHOICE, "ONE SHOT|GATED", 1, "", 1) \
    X(dtrack, "KEY TRACK",     "SMP", 0, 1, 1, TOGGLE, "", 1, "", 1) \
    X(dlevel, "DIGI LEVEL",    "SMP", 0, 1, 0.8f, CONT, "", 1, "", 1) \
    X(dram,   "64K RAM LIMIT", "SMP", 0, 1, 1, TOGGLE, "", 1, "", 1) \
    X(dboost, "8580 DIGI BOOST","SMP", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(s1e,    "OSC 1 > ECHO",  "FX", 0, 1, 1, CONT, "", 1, "", 1) \
    X(s2e,    "OSC 2 > ECHO",  "FX", 0, 1, 1, CONT, "", 1, "", 1) \
    X(s3e,    "OSC 3 > ECHO",  "FX", 0, 1, 1, CONT, "", 1, "", 1) \
    X(sfe,    "FILTER > ECHO", "FX", 0, 1, 1, CONT, "", 1, "", 1) \
    X(s1r,    "OSC 1 > REV",   "FX", 0, 1, 0.5f, CONT, "", 1, "", 1) \
    X(s2r,    "OSC 2 > REV",   "FX", 0, 1, 0.5f, CONT, "", 1, "", 1) \
    X(s3r,    "OSC 3 > REV",   "FX", 0, 1, 0.5f, CONT, "", 1, "", 1) \
    X(sfr,    "FILTER > REV",  "FX", 0, 1, 0.5f, CONT, "", 1, "", 1) \
    X(erev,   "ECHO > REV",    "FX", 0, 1, 0, CONT, "", 1, "", 1) \
    X(rret,   "REV RETURN",    "FX", 0, 1, 0, CONT, "", 1, "", 1) \
    X(rmute,  "REV MUTE",      "FX", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(r7prog, "REV7 PROGRAM",  "REV", 0, 29, 0, CHOICE, "LARGE HALL|SMALL HALL|VOCAL PLATE|PERC PLATE|EARLY REF 1|EARLY REF 2|DELAY L,R|STEREO ECHO|STEREO FLANGE|REVERB FLANGE|CHORUS A|CHORUS B|STEREO PHASING|TREMOLO|SYMPHONIC|REVERB ROOM|ECHO ROOM|STRINGS|ELEC BASS A|ELEC BASS B|KICK|SNARE|GATE REVERB|REVERSE GATE|PIANO|ORGAN|BRASS|GUITAR|HANDCLAPS|LIVE REFERENCE", 1, "", 1) \
    X(r7rt,   "REV TIME",      "REV", 0.3f, 10, 2.6f, CONT, "", 1.5f, "S", 1) \
    X(r7hi,   "HIGH RT RATIO", "REV", 0.1f, 1, 0.3f, CONT, "", 1, "", 1) \
    X(r7lo,   "LOW RT RATIO",  "REV", 0.1f, 2.4f, 1.2f, CONT, "", 1, "", 1) \
    X(r7diff, "DIFFUSION",     "REV", 0, 10, 5, INT, "", 1, "", 1) \
    X(r7idly, "INITIAL DELAY", "REV", 0.1f, 100, 30, CONT, "", 1.5f, "MS", 1) \
    X(r7fdly, "1ST REF DELAY", "REV", 0.1f, 100, 10, CONT, "", 1.5f, "MS", 1) \
    X(r7flev, "1ST REF LEVEL", "REV", 0, 1, 0, CONT, "", 1, "PCT", 1) \
    X(r7live, "LIVENESS",      "REV", 0, 10, 5, INT, "", 1, "", 1) \
    X(r7mode, "E/R MODE",      "REV", 0, 5, 0, CHOICE, "SMALL HALL|LARGE HALL|RANDOM|REVERSE|PLATE|SPRING", 1, "", 1) \
    X(r7size, "ROOM SIZE",     "REV", 0.1f, 20, 2, CONT, "", 1.5f, "", 1) \
    X(r7dlyL, "L DELAY",       "REV", 0.1f, 900, 100, CONT, "", 2, "MS", 1) \
    X(r7dlyR, "R DELAY",       "REV", 0.1f, 900, 200, CONT, "", 2, "MS", 1) \
    X(r7dlev, "DELAY LEVEL",   "REV", 0, 1, 1, CONT, "", 1, "PCT", 1) \
    X(r7fb,   "FEEDBACK GAIN", "REV", 0, 0.99f, 0.6f, CONT, "", 1, "PCT", 1) \
    X(r7hdmp, "HIGH DAMP",     "REV", 0, 10, 5, INT, "", 1, "", 1) \
    X(r7mfrq, "MOD FREQ",      "REV", 0.1f, 20, 1.1f, CONT, "", 2, "HZ", 1) \
    X(r7mdep, "MOD DEPTH",     "REV", 0, 1, 0.5f, CONT, "", 1, "PCT", 1) \
    X(r7mdly, "MOD DELAY",     "REV", 0.1f, 100, 1.2f, CONT, "", 2, "MS", 1) \
    X(r7am,   "AM DEPTH",      "REV", 0, 1, 0.4f, CONT, "", 1, "PCT", 1) \
    X(r7in,   "INPUT LEVEL",   "REV", 0, 10, 8, CONT, "", 1, "", 1) \
    X(r7eq,   "EQ",            "REV", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(r7elf,  "EQ LO FREQ",    "REV", 50, 700, 100, CONT, "", 2, "HZ", 1) \
    X(r7elg,  "EQ LO LEVEL",   "REV", -15, 15, 0, CONT, "", 1, "DB", 1) \
    X(r7emf,  "EQ MID FREQ",   "REV", 350, 7000, 1000, CONT, "", 2, "HZ", 1) \
    X(r7emg,  "EQ MID LEVEL",  "REV", -15, 15, 0, CONT, "", 1, "DB", 1) \
    X(r7ehf,  "EQ HI FREQ",    "REV", 2000, 20000, 8000, CONT, "", 2, "HZ", 1) \
    X(r7ehg,  "EQ HI LEVEL",   "REV", -15, 15, 0, CONT, "", 1, "DB", 1) \
    X(r7circ, "VINTAGE CONV",  "REV", 0, 1, 1, TOGGLE, "", 1, "", 1) \
    X(r7phase,"OUT PHASE",     "REV", 0, 1, 0, TOGGLE, "", 1, "", 1) \
    X(bpm,    "TEMPO",         "SEQ", 40, 240, 120, CONT, "", 1, "BPM", 0) \
    X(srate,  "STEP RATE",     "SEQ", 0, 3, 1, CHOICE, "1/8|1/16|1/16T|1/32", 1, "", 0) \
    X(swing,  "SWING",         "SEQ", 0, 0.75f, 0, CONT, "", 1, "", 0) \
    X(slen,   "STEPS",         "SEQ", 1, 32, 16, INT, "", 1, "", 0) \
    X(sgate,  "GATE LENGTH",   "SEQ", 0.05f, 1, 0.5f, CONT, "", 1, "", 0) \
    X(strans, "KEYS TRANSPOSE","SEQ", 0, 1, 1, TOGGLE, "", 1, "", 0) \
    X(shost,  "FOLLOW HOST",   "SEQ", 0, 1, 1, TOGGLE, "", 1, "", 0) \
    X(spat,   "PATTERN",       "SEQ", 1, 16, 1, INT, "", 1, "", 0)

namespace P {
#define X(k, ...) k,
enum : int { SID_PARAMS count };
#undef X
}
constexpr int NP = P::count;

inline const ParamInfo* params() {
#define X(k, n, s, mn, mx, d, kd, ch, sk, u, pr) { #k, n, s, (float) (mn), (float) (mx), (float) (d), kd, ch, (float) (sk), u, (bool) (pr) },
    static const ParamInfo t[NP] = { SID_PARAMS };
#undef X
    return t;
}
inline int paramIndex(const std::string& key) {
    for (int i = 0; i < NP; ++i) if (key == params()[i].key) return i;
    return -1;
}
inline std::vector<std::string> choiceList(int id) {
    std::vector<std::string> out; std::string s = params()[id].choices, cur;
    for (char c : s) { if (c == '|') { out.push_back(cur); cur.clear(); } else cur += c; }
    if (!s.empty()) out.push_back(cur);
    return out;
}
// normalised 0..1 <-> value (skew > 1 puts more of the travel at the low end)
inline float toNorm(int id, float v) {
    const ParamInfo& p = params()[id];
    if (p.max <= p.min) return 0;
    float n = (std::clamp(v, p.min, p.max) - p.min) / (p.max - p.min);
    return std::fabs(p.skew - 1) > 1e-6f ? std::pow(n, 1.0f / p.skew) : n;
}
inline float fromNorm(int id, float n) {
    const ParamInfo& p = params()[id];
    n = std::clamp(n, 0.0f, 1.0f);
    if (std::fabs(p.skew - 1) > 1e-6f) n = std::pow(n, p.skew);
    float v = p.min + n * (p.max - p.min);
    if (p.kind != CONT) v = std::round(v);
    return v;
}
inline std::string formatValue(int id, float v) {
    const ParamInfo& p = params()[id];
    char b[48];
    if (p.kind == TOGGLE) return v > 0.5f ? "ON" : "OFF";
    if (p.kind == CHOICE) { auto c = choiceList(id); int i = std::clamp((int) std::lround(v), 0, (int) c.size() - 1); return c[(size_t) i]; }
    if (id == P::cut) { std::snprintf(b, sizeof b, "$%03X", (int) std::lround(v)); return b; }
    if (id == P::droot) { static const char* nm[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" }; const int n = (int) std::lround(v); std::snprintf(b, sizeof b, "%s%d", nm[n % 12], n / 12 - 1); return b; }
    if (id == P::dstart || id == P::dend || id == P::dlevel) { std::snprintf(b, sizeof b, "%.0f %%", v * 100); return b; }
    if (std::string(p.section) == "FX" && p.kind == CONT) { std::snprintf(b, sizeof b, "%.0f %%", v * 100); return b; }
    if (std::string(p.unit) == "PCT") { std::snprintf(b, sizeof b, "%.0f %%", v * 100); return b; }
    if (std::string(p.unit) == "S") { std::snprintf(b, sizeof b, "%.1f S", v); return b; }
    if (std::string(p.unit) == "HZ" && v >= 1000) { std::snprintf(b, sizeof b, "%.1fK HZ", v / 1000); return b; }
    if (std::string(p.unit) == "HZ" && v >= 100) { std::snprintf(b, sizeof b, "%.0f HZ", v); return b; }
    if (p.kind == INT) {
        if (std::string(p.section).rfind("OSC", 0) == 0 && (int) p.max == 15) { std::snprintf(b, sizeof b, "%X", (int) std::lround(v)); return b; }
        std::snprintf(b, sizeof b, "%s%d%s%s", (p.min < 0 && v > 0) ? "+" : "", (int) std::lround(v), *p.unit ? " " : "", p.unit); return b;
    }
    const std::string u = p.unit;
    if (u == "HZ") std::snprintf(b, sizeof b, v < 10 ? "%.2f HZ" : "%.1f HZ", v);
    else if (u == "MS") std::snprintf(b, sizeof b, v < 10 ? "%.1f MS" : "%.0f MS", v);
    else if (u == "DB") std::snprintf(b, sizeof b, "%+.1f DB", v);
    else if (u == "CT" || u == "%" || u == "BPM") std::snprintf(b, sizeof b, "%s%.0f %s", (p.min < 0 && v > 0) ? "+" : "", v, p.unit);
    else if (p.min < 0) std::snprintf(b, sizeof b, "%+.2f", v);
    else if (p.max > 100) std::snprintf(b, sizeof b, "%.0f", v);
    else std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}

// ---------------------------------------------------------------------------------------------------------------
// attack programs: per-tick waveform / pitch tables (wave -1 keeps the patch's own; abs = fixed note number)
struct Frame { int wave; int pitch; bool abs; };
struct Program { std::vector<Frame> f; int end; };   // end: 0 return to the patch, 1 hold the last frame, 2 loop
inline const std::vector<Program>& programs() {
    enum { T = TRI, S = SAW, Pu = PULSE, N = NOISE };
    static const std::vector<Program> p = {
        { {}, 0 },
        { { {N, 36, false} }, 0 },                                                          // NOISE HIT
        { { {S, 12, false}, {-1, 0, false} }, 0 },                                           // PLUCK
        { { {N, 96, true}, {Pu, 52, true}, {N, 90, true}, {N, 86, true}, {N, 83, true}, {N, 80, true} }, 1 },   // SNARE
        { { {Pu, 60, true}, {T, 50, true}, {T, 44, true}, {T, 40, true}, {T, 36, true}, {T, 33, true}, {T, 31, true}, {T, 30, true} }, 1 },  // KICK
        { { {N, 24, false}, {T, 8, false}, {T, 6, false}, {T, 4, false}, {T, 3, false}, {T, 2, false}, {T, 1, false}, {T, 0, false} }, 1 },  // TOM
        { { {N, 118, true} }, 1 },                                                           // HIHAT
        { { {N, 100, true}, {N, 92, true}, {N, 100, true}, {N, 94, true}, {N, 98, true} }, 1 },   // CLAP
        { { {-1, 36, false}, {-1, 30, false}, {-1, 24, false}, {-1, 18, false}, {-1, 12, false}, {-1, 8, false}, {-1, 4, false}, {-1, 2, false} }, 0 },  // ZAP
        { { {-1, -24, false}, {-1, -18, false}, {-1, -12, false}, {-1, -8, false}, {-1, -4, false}, {-1, -2, false} }, 0 },  // LASER
        { { {-1, 12, false} }, 0 },                                                          // OCT BLIP
        { { {N, 36, false}, {T, 12, false} }, 0 },                                           // BELL
        { { {N, 90, true}, {N, 72, true} }, 0 },                                             // DRUM BASS
        { { {-1, 12, false}, {-1, 10, false}, {-1, 8, false}, {-1, 6, false}, {-1, 4, false}, {-1, 3, false}, {-1, 2, false}, {-1, 1, false} }, 0 },  // PITCH DROP
        { { {-1, 0, false}, {-1, 0, false}, {-1, 0, false}, {-1, 0, false}, {-1, 12, false} }, 1 },   // COIN
        { { {S, 0, false}, {Pu, 0, false} }, 2 },                                            // SAW-PUL LOOP
        { { {-1, 0, false}, {-1, 12, false} }, 2 },                                          // OCT TRILL
        { { {-1, 0, false}, {-1, 0, false}, {-1, 0, false}, {N, 0, false} }, 1 },            // NOISE TAIL
    };
    return p;
}
inline const std::vector<std::vector<int>>& chords() {
    static const std::vector<std::vector<int>> c = {
        {0}, {0, 12}, {0, 4, 7}, {0, 3, 7}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 5, 7}, {0, 7, 12},
        {0, 3, 6}, {0, 4, 8}, {0, 12, 19}, {0, 4, 7, 14}, {0, 3, 7, 14} };
    return c;
}

// ---------------------------------------------------------------------------------------------------------------
// sequencer patterns
struct Step { int note = 60; bool on = false, accent = false, slide = false; };
constexpr int kSteps = 32, kPatterns = 16;
inline uint32_t packStep(const Step& s) { return (uint32_t) (s.note & 127) | (s.on ? 128u : 0u) | (s.accent ? 256u : 0u) | (s.slide ? 512u : 0u); }
inline Step unpackStep(uint32_t v) { Step s; s.note = (int) (v & 127); s.on = v & 128; s.accent = v & 256; s.slide = v & 512; return s; }

inline std::string noteName(int n) {
    static const char* nm[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    char b[16]; std::snprintf(b, sizeof b, "%s%d", nm[((n % 12) + 12) % 12], n / 12 - 1); return b;
}
// "C4", "C#4", "Db4", "C-4" -> MIDI note (C4 = 60); -1 if it isn't a note
inline int parseNote(const std::string& t) {
    if (t.empty()) return -1;
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };
    const char c = (char) std::toupper((unsigned char) t[0]);
    if (c < 'A' || c > 'G') return -1;
    int n = base[c - 'A']; size_t i = 1;
    if (i < t.size() && t[i] == '#') { ++n; ++i; } else if (i < t.size() && (t[i] == 'b')) { --n; ++i; } else if (i < t.size() && t[i] == '-') ++i;
    if (i >= t.size()) return -1;
    const int oct = std::atoi(t.c_str() + i);
    return std::clamp((oct + 1) * 12 + n, 0, 127);
}

// ---------------------------------------------------------------------------------------------------------------
struct PresetDef { const char* cat; const char* name; const char* diff; };
struct PatternDef { const char* name; const char* steps; };
}  // namespace sid

#include "SidPresets.h"

namespace sid {

inline int presetCount() { return (int) (sizeof(PRESETS) / sizeof(PRESETS[0])); }
inline int patternPresetCount() { return (int) (sizeof(PATTERNS) / sizeof(PATTERNS[0])); }

// fast tanh for the output stage
inline float ftanh(float x) { if (x > 3) return 1; if (x < -3) return -1; const float x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }

// ---------------------------------------------------------------------------------------------------------------
class Synth {
public:
    static constexpr int kChips = 8;

    Synth() {
        for (int i = 0; i < NP; ++i) values[i].store(params()[i].def);
        for (auto& p : pat) for (auto& s : p) s.store(packStep(Step {}));
        for (int i = 0; i < 16 && i < patternPresetCount(); ++i) loadPatternPreset(i, i);
        for (auto& s : scope) s.store(0);
        for (auto& m : meters) m.store(0);
        source.loadBuiltin(0);
        for (auto& r : retired) r.store(nullptr);
        for (auto& r : regs) r.store(0);
    }

    ~Synth() { delete cur; delete incoming.exchange(nullptr); collectRetired(); }

    // ---- setup (call before rendering, not on the audio thread while it runs) ----
    void prepare(double sampleRate) {
        fs = sampleRate;
        recBuf.assign((size_t) (fs * 20), 0.f); recPos = 0; recOn = false;
        echoL.assign((size_t) (fs * 2.1) + 8, 0.f); echoR.assign(echoL.size(), 0.f); echoW = 0;
        for (int q = 0; q < 3; ++q) designFir(q);
        for (auto& b : firBuf) b.assign(4096, 0.f);
        firPos = 0;
        if (!rev7) initRev7();
        rev7->prepare(fs, 64); r7last.clear();
        rvIn.assign(64, 0.f); rvL.assign(64, 0.f); rvR.assign(64, 0.f); preL.assign(64, 0.f); preR.assign(64, 0.f);
        curOS = -1; configure();
        for (auto& v : voices) v.chip.reset();
        tickCount = 0;
    }

    // ---- parameters (any thread) ----
    void set(int id, float v) { if (id >= 0 && id < NP) { const ParamInfo& p = params()[id]; values[id].store(std::clamp(v, p.min, p.max)); } }
    float get(int id) const { return values[id].load(std::memory_order_relaxed); }
    std::atomic<int> program { 0 };
    std::string presetName = "INIT";

    // apply a preset: everything the presets set goes back to its default, then the preset's changes
    void applyPreset(int i) {
        if (i < 0 || i >= presetCount()) return;
        for (int k = 0; k < NP; ++k) if (params()[k].preset) set(k, params()[k].def);
        applyDiff(PRESETS[i].diff);
        program = i; presetName = PRESETS[i].name;
        const int b = presetBuiltin(i); if (b >= 0) { source.loadBuiltin(b); ++sourceVersion; }
    }
    // the built-in sample a preset asks for (dsmp=NAME), or -1
    static int presetBuiltin(int i) {
        if (i < 0 || i >= presetCount()) return -1;
        const std::string d = PRESETS[i].diff; const size_t k = d.find("dsmp=");
        if (k == std::string::npos) return -1;
        size_t e = d.find(' ', k); std::string nm = d.substr(k + 5, e == std::string::npos ? std::string::npos : e - k - 5);
        for (char& c : nm) if (c == '_') c = ' ';
        for (int b = 0; b < builtin::count; ++b) if (nm == builtin::names()[b]) return b;
        return -1;
    }
    void initPatch() { for (int k = 0; k < NP; ++k) if (params()[k].preset) set(k, params()[k].def); presetName = "INIT"; }
    // a random but playable patch
    void randomize(uint32_t seed) {
        uint32_t lr = seed ? seed : 1;   // its own generator: the audio thread uses rng
        auto rnd01 = [&lr]() { lr ^= lr << 13; lr ^= lr >> 17; lr ^= lr << 5; return (float) (lr & 0xFFFFFF) / 16777216.0f; };
        initPatch();
        auto r = [&](float a, float b) { return a + (b - a) * rnd01(); };
        auto ri = [&](int a, int b) { return (int) std::lround(r((float) a, (float) b)); };
        set(P::model, (float) ri(0, 1));
        const int nOsc = ri(1, 3);
        for (int o = 0; o < 3; ++o) {
            const int b = osc(o);
            set(b + 0, o < nOsc ? 1.f : 0.f);
            int w = ri(1, 7); if (rnd01() < 0.12f) w = 8;
            set(b + 1, w & 1); set(b + 2, w >> 1 & 1); set(b + 3, w >> 2 & 1); set(b + 4, w >> 3 & 1);
            set(b + 5, o == 0 ? 0.f : (float) (std::vector<int> { 0, 0, 12, -12, 7, 5, 24 })[(size_t) ri(0, 6)]);
            set(b + 6, r(-12, 12)); set(b + 7, r(10, 90)); set(b + 8, rnd01() < .5f ? r(0, 40) : 0.f); set(b + 9, r(0.1f, 6));
            set(b + 10, rnd01() < .15f); set(b + 11, rnd01() < .15f); set(b + 12, rnd01() < .6f);
            set(b + 13, (float) ri(0, 8)); set(b + 14, (float) ri(2, 11)); set(b + 15, (float) ri(0, 15)); set(b + 16, (float) ri(1, 10));
        }
        set(P::cut, r(150, 1800)); set(P::res, (float) ri(0, 15));
        const int fm = ri(1, 7); set(P::lp, fm & 1); set(P::bp, fm >> 1 & 1); set(P::hp, fm >> 2 & 1);
        set(P::fenv, rnd01() < .6f ? r(-600, 1400) : 0.f); set(P::fd, r(40, 1500)); set(P::fs, r(0, 1)); set(P::fr, r(50, 1200));
        set(P::vib, rnd01() < .4f ? r(5, 40) : 0.f);
        if (rnd01() < .2f) { set(P::arp, (float) ri(1, 5)); set(P::chord, (float) ri(1, 13)); set(P::aspd, (float) ri(1, 4)); }
        if (rnd01() < .25f) set(P::prog, (float) ri(1, 17));
        if (rnd01() < .3f) { set(P::emix, r(.1f, .35f)); set(P::efb, r(.2f, .6f)); }
        presetName = "RANDOM";
    }

    // the preset language: key=value pairs; w1=SP (waveforms T S P N, "-" = oscillator off), e1=09A4 (ADSR in hex),
    // p1 (pulse width %), c1 / f1 (coarse / fine), w= e= p= f= set all three; choices by number or name
    void applyDiff(const std::string& diff) {
        size_t i = 0;
        while (i < diff.size()) {
            while (i < diff.size() && diff[i] == ' ') ++i;
            const size_t j = diff.find(' ', i);
            const std::string tok = diff.substr(i, j == std::string::npos ? std::string::npos : j - i);
            i = j == std::string::npos ? diff.size() : j + 1;
            const size_t eq = tok.find('='); if (eq == std::string::npos) continue;
            const std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
            setByKey(k, v);
        }
    }

    // ---- notes (audio thread: MIDI) ----
    void noteOn(int note, float vel) {
        if (seqRunning() && get(P::strans) > .5f) {
            keyTranspose = note - 60; transposeKeys.push_back(note); return;
        }
        playNoteOn(note, vel, false);
    }
    void noteOff(int note) {
        if (!transposeKeys.empty()) {
            auto it = std::find(transposeKeys.begin(), transposeKeys.end(), note);
            if (it != transposeKeys.end()) { transposeKeys.erase(it); return; }
        }
        if (sustain) { sustained.push_back(note); return; }
        playNoteOff(note, false);
    }
    void setSustain(bool on) {
        sustain = on;
        if (!on) { auto s = sustained; sustained.clear(); for (int n : s) playNoteOff(n, false); }
    }
    void setBend(double semitonesNorm) { bendNorm = semitonesNorm; }   // -1..1
    void setModWheel(double v) { modWheel = v; }
    void setCutoffCC(double v) { ccCut = v; }   // -1..1 offset from CC 74
    void allNotesOff() {
        held.clear(); sustained.clear(); transposeKeys.clear(); arpOn = false;
        for (auto& v : voices) for (auto& s : v.s) { s.gate = false; s.key = -1; }
        for (auto& v : voices) { v.chip.stopDigi(); writeRegs(v); }
    }
    void panic() { allNotesOff(); for (auto& v : voices) v.chip.reset(); std::fill(echoL.begin(), echoL.end(), 0.f); std::fill(echoR.begin(), echoR.end(), 0.f); if (rev7) rev7->reset(); }

    // ---- notes from another thread (the on-screen keyboard): a single-producer queue ----
    void post(int type, int a, float b) {
        const int w = qW.load(std::memory_order_relaxed);
        evq[(size_t) (w & 255)] = { type, a, b };
        qW.store(w + 1, std::memory_order_release);
    }
    enum { EV_ON, EV_OFF, EV_PANIC, EV_SEQ_PLAY, EV_SEQ_STOP, EV_BEND, EV_WHEEL, EV_ALLOFF };

    // ---- the sampler (UI thread, except recordInput) ----
    SampleSource source;          // what's been sampled; only the UI thread touches it
    int sourceVersion = 0;
    std::atomic<int> digiShown { -1 };     // where the newest digi is playing, for the screen
    std::atomic<bool> recOn { false }; std::atomic<int> recPos { 0 };
    // audio thread: the input to record from
    void recordInput(const float* l, const float* r, int n) {
        if (!recOn.load(std::memory_order_relaxed) || recBuf.empty()) return;
        int p = recPos.load(std::memory_order_relaxed);
        for (int i = 0; i < n && p < (int) recBuf.size(); ++i) recBuf[(size_t) p++] = r ? 0.5f * (l[i] + r[i]) : l[i];
        recPos.store(p, std::memory_order_relaxed);
        if (p >= (int) recBuf.size()) recOn = false;
    }
    void startRecording() { recPos = 0; recOn = true; }
    // stop and take what was recorded as the source
    void stopRecording() {
        recOn = false;
        const int n = recPos.load();
        if (n > (int) (fs * 0.05)) { source.set(std::vector<float>(recBuf.begin(), recBuf.begin() + n), fs, "RECORDING"); ++sourceVersion; }
    }
    void setSource(std::vector<float> mono, double rate, const std::string& name) { source.set(std::move(mono), rate, name); ++sourceVersion; }
    void loadBuiltin(int i) { source.loadBuiltin(i); ++sourceVersion; }
    // convert again when the source, the rate, the method's bit depth or the RAM limit changes (call from the UI)
    void pollSample() {
        collectRetired();
        const int bits = iv(P::dmode) == DIGI_VOL4 ? 4 : 8;
        const int sig = sourceVersion * 1000 + iv(P::drate) * 10 + (bits == 4 ? 1 : 0) + (on(P::dram) ? 2 : 0);
        if (sig == lastSig) return;
        lastSig = sig;
        SampleData* d = source.convert(digiRates()[std::clamp(iv(P::drate), 0, 9)], bits, on(P::dram));
        delete incoming.exchange(d);
    }
    double effectiveRate(double rate) const { const double clk = on(P::clock) ? NTSC_CLOCK : PAL_CLOCK; return clk / std::max(1.0, std::round(clk / rate)); }

    // ---- sequencer ----
    std::atomic<uint32_t> pat[kPatterns][kSteps];
    Step step(int p, int i) const { return unpackStep(pat[p][i].load(std::memory_order_relaxed)); }
    void setStep(int p, int i, const Step& s) { pat[p][i].store(packStep(s), std::memory_order_relaxed); }
    void loadPatternPreset(int preset, int slot) {
        if (preset < 0 || preset >= patternPresetCount() || slot < 0 || slot >= kPatterns) return;
        int n = 0; std::string s = PATTERNS[preset].steps; size_t i = 0;
        for (int k = 0; k < kSteps; ++k) setStep(slot, k, Step {});
        int last = 48;
        while (i < s.size() && n < kSteps) {
            while (i < s.size() && s[i] == ' ') ++i;
            if (i >= s.size()) break;
            size_t j = s.find(' ', i); std::string t = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
            i = j == std::string::npos ? s.size() : j + 1;
            Step st; st.note = last;
            if (t != ".") {
                if (!t.empty() && t[0] == '!') { st.accent = true; t = t.substr(1); }
                if (!t.empty() && t.back() == '~') { st.slide = true; t.pop_back(); }
                const int nn = parseNote(t);
                if (nn >= 0) { st.note = nn; st.on = true; last = nn; }
            }
            setStep(slot, n++, st);
        }
        patLen[slot] = std::max(1, n);
    }
    int patLen[kPatterns] {};   // length of the loaded preset pattern, for the editor
    bool seqRunning() const { return seqPlay.load() || hostRolling; }
    std::atomic<bool> seqPlay { false };
    std::atomic<int> seqStepShown { -1 };
    // host transport: call before render() each block (plugin only)
    void hostTransport(bool playing, double ppq, double bpm) { hostPlaying = playing; hostPpq = ppq; hostBpm = bpm; hostValid = true; }

    // ---- what the screen shows ----
    std::array<std::atomic<float>, 512> scope; std::atomic<int> scopeW { 0 };
    std::array<std::atomic<int>, 25> regs;   // the first chip's registers, $D400-$D418
    std::atomic<int> activeVoices { 0 };
    // mixer meters (peak, linear, falling back over ~300 ms): OSC 1-3, FILTER, ECHO return, REV return, OUT L, OUT R
    enum { M_OSC1, M_OSC2, M_OSC3, M_FILTER, M_ECHO, M_REV, M_OUTL, M_OUTR, M_COUNT };
    std::array<std::atomic<float>, M_COUNT> meters;

    // ---- state ----
    std::string saveState() const {
        std::string s = "omega-sid 1\n";
        for (int i = 0; i < NP; ++i) { char b[64]; std::snprintf(b, sizeof b, "%s=%.6g\n", params()[i].key, get(i)); s += b; }
        s += "name=" + presetName + "\nprogram=" + std::to_string(program.load()) + "\n";
        if (!source.empty()) s += "smpname=" + source.name + "\nsmp=" + source.save() + "\n";
        for (int p = 0; p < kPatterns; ++p) {
            s += "pat" + std::to_string(p) + "=";
            for (int i = 0; i < kSteps; ++i) s += std::to_string(pat[p][i].load()) + (i < kSteps - 1 ? "," : "");
            s += "\n";
        }
        return s;
    }
    void loadState(const std::string& st) {
        std::string smpName = "SAMPLE";
        size_t i = 0;
        while (i < st.size()) {
            size_t j = st.find('\n', i); if (j == std::string::npos) j = st.size();
            const std::string line = st.substr(i, j - i); i = j + 1;
            const size_t eq = line.find('='); if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "name") presetName = v;
            else if (k == "smpname") smpName = v;
            else if (k == "smp") { source.load(v, smpName); ++sourceVersion; }
            else if (k == "program") program = std::atoi(v.c_str());
            else if (k.rfind("pat", 0) == 0 && k.size() > 3 && std::isdigit((unsigned char) k[3])) {
                const int p = std::atoi(k.c_str() + 3); if (p < 0 || p >= kPatterns) continue;
                size_t a = 0; int n = 0;
                while (a <= v.size() && n < kSteps) { size_t b = v.find(',', a); if (b == std::string::npos) b = v.size(); pat[p][n++].store((uint32_t) std::strtoul(v.substr(a, b - a).c_str(), nullptr, 10)); a = b + 1; }
            } else { const int id = paramIndex(k); if (id >= 0) set(id, (float) std::atof(v.c_str())); }
        }
    }

    // ---- audio ----
    void render(float* L, float* R, int n) {
        adoptSample();
        drainQueue();
        configure();
        int pos = 0;
        const double tickHz = tickRate();
        while (pos < n) {
            if (tickCount <= 0) { doTick(); tickCount += fs / tickHz; }
            int len = std::min({ n - pos, 32, std::max(1, (int) std::ceil(tickCount)) });
            sequencer(len);
            synth(L + pos, R + pos, len);
            tickCount -= len; pos += len;
        }
        hostValid = false;
    }

private:
    // ---------------- voices ----------------
    struct Slot {            // one oscillator's note
        int key = -1, note = 60; float vel = 1; bool gate = false; double pitch = 60, target = 60;
        int progPos = 0; double vibTime = 0; uint64_t age = 0;
    };
    struct NVoice {
        Chip chip; Slot s[3]; double fenv = 0; int fstage = 3; float level = 1; float panL = .707f, panR = .707f; int dslot = 0;
        bool asleep = true; uint64_t age = 0; bool anyGate() const { return s[0].gate || s[1].gate || s[2].gate; }
    };
    NVoice voices[kChips];
    double fs = 48000, tickCount = 0;
    int curOS = -1, curModel = -1, curClock = -1;
    uint64_t ageCounter = 0;
    std::atomic<float> values[NP];
    uint32_t rng = 12345;
    SampleData* cur = nullptr; std::atomic<SampleData*> incoming { nullptr };
    std::array<std::atomic<SampleData*>, 8> retired;
    std::vector<float> recBuf; int lastSig = -1;
    void collectRetired() { for (auto& r : retired) delete r.exchange(nullptr); }
    float rnd01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }

    static int osc(int o) { return P::o1on + o * (P::o2on - P::o1on); }
    bool on(int id) const { return get(id) > 0.5f; }
    int iv(int id) const { return (int) std::lround(get(id)); }
    double tickRate() const { static const double r[4] = { 50, 60, 100, 200 }; return r[std::clamp(iv(P::tick), 0, 3)]; }
    int nVoices() const { const int m = iv(P::mode); return m == 0 ? std::clamp(iv(P::poly), 1, kChips) : 1; }

    void setByKey(const std::string& k, const std::string& v) {
        auto all = [&](const std::string& key, const std::string& val) { for (int o = 1; o <= 3; ++o) setByKey(key + std::to_string(o), val); };
        if (k == "w" || k == "e" || k == "p" || k == "f" || k == "c") { all(k, v); return; }
        if (k == "r7p") { applyRev7Preset(std::atoi(v.c_str()) - 1); return; }   // an Omega REV7 preset, by number (1-30 factory)
        if (k.size() == 2 && std::isdigit((unsigned char) k[1])) {
            const int o = k[1] - '1'; if (o < 0 || o > 2) return; const int b = osc(o);
            switch (k[0]) {
                case 'w':
                    if (v == "-") { set(b, 0); return; }
                    set(b, 1); set(b + 1, v.find('T') != std::string::npos); set(b + 2, v.find('S') != std::string::npos);
                    set(b + 3, v.find('P') != std::string::npos); set(b + 4, v.find('N') != std::string::npos); return;
                case 'e':
                    if (v.size() == 4) for (int q = 0; q < 4; ++q) set(b + 13 + q, (float) std::strtol(v.substr((size_t) q, 1).c_str(), nullptr, 16));
                    return;
                case 'p': set(b + 7, (float) std::atof(v.c_str())); return;
                case 'f': set(b + 6, (float) std::atof(v.c_str())); return;
                case 'c': set(b + 5, (float) std::atof(v.c_str())); return;
                default: break;
            }
        }
        int id = paramIndex(k);
        if (id < 0 && paramIndex("o1" + k) >= 0) { for (int o = 1; o <= 3; ++o) setByKey("o" + std::to_string(o) + k, v); return; }
        if (id < 0) return;
        const ParamInfo& p = params()[id];
        if (p.kind == CHOICE && !v.empty() && !(std::isdigit((unsigned char) v[0]) || v[0] == '-')) {
            auto c = choiceList(id);
            for (size_t q = 0; q < c.size(); ++q) if (c[q] == v) { set(id, (float) q); return; }
            return;
        }
        set(id, (float) std::atof(v.c_str()));
    }

    void configure() {
        const int os = std::clamp(iv(P::quality), 0, 2), model = iv(P::model), clk = iv(P::clock);
        if (os != curOS || clk != curClock) {
            curOS = os; curClock = clk;
            const double fsI = fs * osFactor();
            for (auto& v : voices) { v.chip.clock = clk ? NTSC_CLOCK : PAL_CLOCK; v.chip.setRate(fsI); }
            for (int c = 0; c < 5; ++c) extHp[c] = extLp[c] = extIn[c] = 0;
            const double fsIn = fs * osFactor();
            extHpA = std::exp(-2 * kPi * 16.0 / fsIn); extLpA = std::exp(-2 * kPi * 16000.0 / fsIn);
        }
        if (model != curModel) { curModel = model; for (auto& v : voices) v.chip.model = model ? MOS8580 : MOS6581; }
        for (auto& v : voices) { v.chip.curve = get(P::curve); v.chip.drive = get(P::drive); for (auto& x : v.chip.v) x.env.bug = on(P::adsrbug); }
    }
    int osFactor() const { return 2 << std::clamp(curOS, 0, 2); }

    // ---------------- notes ----------------
    std::vector<int> held;          // keys down, in the order played (arp, mono)
    std::vector<int> sustained, transposeKeys;
    bool sustain = false; int keyTranspose = 0;
    double bendNorm = 0, modWheel = 0, ccCut = 0;
    float heldVel = 1;
    // arpeggiator
    bool arpOn = false; int arpIdx = 0, arpDir = 1, arpTicks = 0, arpNote = -1; double arpPhase = 0;

    std::vector<int> expand(int note) const {
        const auto& c = chords()[(size_t) std::clamp(iv(P::chord), 0, (int) chords().size() - 1)];
        std::vector<int> out; for (int x : c) out.push_back(note + x); return out;
    }

    void playNoteOn(int note, float vel, bool fromSeq) {
        (void) fromSeq;
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        held.push_back(note); heldVel = vel;
        if (iv(P::arp) > 0) { if (!arpOn) { arpOn = true; arpIdx = 0; arpDir = 1; arpTicks = 0; arpPhase = 0; arpStep(true, vel); } return; }
        const int mode = iv(P::mode);
        if (mode == 1 || mode == 2) { monoTo(note, vel, mode == 1 || !voices[0].anyGate()); return; }
        for (int n : expand(note)) {
            if (mode == 3) startSlot(note, n, vel);
            else startVoice(note, n, vel);
        }
    }
    void playNoteOff(int note, bool fromSeq) {
        (void) fromSeq;
        held.erase(std::remove(held.begin(), held.end(), note), held.end());
        if (iv(P::arp) > 0 || arpOn) {
            if (held.empty()) { arpOn = false; for (auto& s : voices[0].s) s.gate = false; voices[0].chip.releaseDigi(); writeRegs(voices[0]); arpNote = -1; }
            return;
        }
        const int mode = iv(P::mode);
        if (mode == 1 || mode == 2) {
            if (held.empty()) { for (auto& s : voices[0].s) s.gate = false; voices[0].fstage = 3; voices[0].chip.releaseDigi(); writeRegs(voices[0]); }
            else monoTo(held.back(), heldVel, false);
            return;
        }
        for (auto& v : voices) {
            bool hit = false;
            for (auto& s : v.s) if (s.key == note && s.gate) { s.gate = false; hit = true; }
            if (hit) {
                if (!v.anyGate()) v.fstage = 3;
                if (!v.s[v.dslot].gate) v.chip.releaseDigi();
                writeRegs(v);
            }
        }
    }
    void retrig(NVoice& v, int oscMask) {
        for (int i = 0; i < 3; ++i) if (oscMask >> i & 1) {
            auto& e = v.chip.v[i].env;
            if (on(P::hard)) { e.gate = false; e.level = 0; e.rateCounter = 0; e.expCounter = 0; e.holdZero = true; }
            else e.setGate(false);
        }
    }
    void begin(Slot& s, int key, int note, float vel) {
        s.key = key; s.note = note; s.vel = vel; s.gate = true; s.target = note; s.progPos = 0; s.vibTime = 0; s.age = ++ageCounter;
        if (get(P::glide) < 1 || s.pitch < 0) s.pitch = note;
    }
    void wake(NVoice& v, float vel) {
        if (v.asleep) { v.asleep = false; for (auto& s : v.s) s.pitch = -1; }
        v.fenv = 0; v.fstage = 0; v.level = 1 - get(P::vellvl) * (1 - vel); v.age = ++ageCounter;
    }
    // the sampler: start this chip's digi for a note (slot = the oscillator slot whose pitch it follows)
    // audio thread: take a new conversion (stopping anything still playing the old one)
    void adoptSample() {
        SampleData* in = incoming.exchange(nullptr);
        if (!in) return;
        for (auto& v : voices) if (v.chip.digiActive) v.chip.stopDigi();
        if (cur) for (auto& r : retired) { SampleData* e = nullptr; if (r.compare_exchange_strong(e, cur)) { cur = nullptr; break; } }
        cur = in;   // (if every retired slot were full the old one would be left alone rather than freed here)
    }
    void startDigi(NVoice& v, int slot) {
        adoptSample();
        const int m = iv(P::dmode);
        if (m == DIGI_OFF || !cur || cur->d.empty()) return;
        const int n = (int) cur->d.size();
        int a = (int) (get(P::dstart) * n), b = (int) (get(P::dend) * n);
        if (b <= a) b = std::min(n, a + 1);
        v.dslot = slot;
        v.chip.startDigi(cur->d.data(), a, b, m, on(P::dloop), iv(P::dgate) == 0);
    }
    void startVoice(int key, int note, float vel) {
        const int nv = nVoices();
        int best = -1; uint64_t bestAge = UINT64_MAX; bool bestFree = false;
        for (int i = 0; i < nv; ++i) {
            NVoice& v = voices[i]; const bool free = !v.anyGate();
            if (free && !bestFree) { best = i; bestAge = v.age; bestFree = true; continue; }
            if (free == bestFree && v.age < bestAge) { best = i; bestAge = v.age; }
        }
        NVoice& v = voices[best];
        retrig(v, 7);
        wake(v, vel);
        for (auto& s : v.s) begin(s, key, note, vel);
        startDigi(v, 0);
        updateVoice(v, 0); writeRegs(v);
    }
    void startSlot(int key, int note, float vel) {
        NVoice& v = voices[0];
        int best = -1; uint64_t bestAge = UINT64_MAX; bool bestFree = false;
        for (int i = 0; i < 3; ++i) {
            if (!on(osc(i))) continue;
            const bool free = !v.s[i].gate;
            if (free && !bestFree) { best = i; bestAge = v.s[i].age; bestFree = true; continue; }
            if (free == bestFree && v.s[i].age < bestAge) { best = i; bestAge = v.s[i].age; }
        }
        if (best < 0) return;
        retrig(v, 1 << best);
        if (!v.anyGate()) wake(v, vel); else { v.asleep = false; v.fenv = 0; v.fstage = 0; }
        begin(v.s[best], key, note, vel);
        startDigi(v, best);
        updateVoice(v, 0); writeRegs(v);
    }
    void monoTo(int note, float vel, bool retrigger) {
        NVoice& v = voices[0];
        if (retrigger || !v.anyGate()) {
            retrig(v, 7); wake(v, vel);
            for (auto& s : v.s) { const double keep = s.pitch; begin(s, note, note, vel); if (keep >= 0 && get(P::glide) >= 1) s.pitch = keep; }
            startDigi(v, 0);
        } else {
            for (auto& s : v.s) { s.key = note; s.note = note; s.target = note; if (get(P::glide) < 1) s.pitch = note; }
        }
        updateVoice(v, 0); writeRegs(v);
    }

    // the next arpeggio note (on the tick that's due); the gate stays on, only the pitch moves, as on a C64
    void arpStep(bool first, float vel) {
        if (held.empty()) return;
        std::vector<int> base = held;
        const int m = iv(P::arp);
        if (m != 5) std::sort(base.begin(), base.end());
        std::vector<int> seq;
        for (int o = 0; o < std::clamp(iv(P::aoct), 1, 4); ++o) for (int b : base) for (int n : expand(b)) seq.push_back(n + 12 * o);
        if (m != 5) { std::sort(seq.begin(), seq.end()); seq.erase(std::unique(seq.begin(), seq.end()), seq.end()); }
        const int N = (int) seq.size();
        int idx = 0;
        if (!first) {
            switch (m) {
                case 2: arpIdx = (arpIdx - 1 + N) % N; break;
                case 3: if (N > 1) { if (arpIdx + arpDir >= N || arpIdx + arpDir < 0) arpDir = -arpDir; arpIdx += arpDir; } else arpIdx = 0; break;
                case 4: arpIdx = (int) (rnd01() * N) % N; break;
                default: arpIdx = (arpIdx + 1) % N; break;
            }
        } else arpIdx = m == 2 ? N - 1 : 0;
        idx = std::clamp(arpIdx, 0, N - 1);
        const int note = seq[(size_t) idx];
        NVoice& v = voices[0];
        if (first || !v.anyGate()) {
            retrig(v, 7); wake(v, vel);
            for (auto& s : v.s) begin(s, -2, note, vel);
            startDigi(v, 0);
        } else for (auto& s : v.s) { s.note = note; s.target = note; if (get(P::glide) < 1) s.pitch = note; }
        arpNote = note;
        updateVoice(v, 0); writeRegs(v);
    }

    // ---------------- ticks ----------------
    double lfoPhase = 0, lfoVal = 0, lfoRand = 0, pwmPhase[3] {};
    void doTick() {
        const double dt = 1.0 / tickRate();
        // LFOs
        lfoPhase += get(P::lrate) * dt;
        if (lfoPhase >= 1) { lfoPhase -= std::floor(lfoPhase); lfoRand = rnd01() * 2 - 1; }
        switch (iv(P::lshape)) {
            case 1: lfoVal = 1 - 2 * lfoPhase; break;
            case 2: lfoVal = lfoPhase < .5 ? 1 : -1; break;
            case 3: lfoVal = lfoRand; break;
            default: lfoVal = lfoPhase < .5 ? 4 * lfoPhase - 1 : 3 - 4 * lfoPhase; break;
        }
        for (int o = 0; o < 3; ++o) { pwmPhase[o] += get(osc(o) + 9) * dt; pwmPhase[o] -= std::floor(pwmPhase[o]); }
        // arpeggio
        if (arpOn && iv(P::arp) > 0) {
            if (on(P::async)) {
                static const double div[5] = { 0.5, 0.25, 1.0 / 6, 0.125, 0.0625 };
                arpPhase += bpmNow() / 60.0 * dt / div[std::clamp(iv(P::adiv), 0, 4)];
                if (arpPhase >= 1) { arpPhase -= std::floor(arpPhase); arpStep(false, heldVel); }
            } else if (++arpTicks >= std::max(1, iv(P::aspd))) { arpTicks = 0; arpStep(false, heldVel); }
        }
        int active = 0;
        for (int i = 0; i < kChips; ++i) {
            NVoice& v = voices[i];
            if (v.asleep) continue;
            updateVoice(v, dt); writeRegs(v);
            ++active;
        }
        activeVoices = active;
        // registers for the screen
        const Chip& c = voices[lastShown()].chip;
        for (int i = 0; i < 3; ++i) {
            const Voice& x = c.v[i];
            regs[(size_t) (i * 7 + 0)] = x.freq & 255; regs[(size_t) (i * 7 + 1)] = x.freq >> 8;
            regs[(size_t) (i * 7 + 2)] = x.pw & 255; regs[(size_t) (i * 7 + 3)] = x.pw >> 8 & 15; regs[(size_t) (i * 7 + 4)] = x.ctrl;
            regs[(size_t) (i * 7 + 5)] = x.env.attack << 4 | x.env.decay; regs[(size_t) (i * 7 + 6)] = x.env.sustain << 4 | x.env.release;
        }
        regs[21] = c.fc & 7; regs[22] = c.fc >> 3; regs[23] = c.res << 4 | c.filt; regs[24] = c.mode | c.vol;
    }
    int lastShown() const {
        int best = 0; uint64_t a = 0;
        for (int i = 0; i < kChips; ++i) if (!voices[i].asleep && voices[i].age >= a) { a = voices[i].age; best = i; }
        return best;
    }
    double bpmNow() const { return hostValid && hostBpm > 0 && on(P::shost) ? hostBpm : (double) get(P::bpm); }

    // time-based modulation for one chip; dt = 0 just recomputes (a note arrived between ticks)
    void updateVoice(NVoice& v, double dt) {
        const double tr = tickRate();
        // software filter envelope (linear segments, stepped at the tick rate like a C64 player)
        if (dt > 0) {
            const double a = get(P::fa) / 1000.0, d = get(P::fd) / 1000.0, su = get(P::fs), r = get(P::fr) / 1000.0;
            switch (v.fstage) {
                case 0: v.fenv += a > 0.001 ? dt / a : 1; if (v.fenv >= 1) { v.fenv = 1; v.fstage = 1; } break;
                case 1: v.fenv -= d > 0.001 ? dt / d * (1 - su) : 1; if (v.fenv <= su) { v.fenv = su; v.fstage = 2; } break;
                case 2: v.fenv = su; break;
                default: v.fenv -= r > 0.001 ? dt / r : 1; if (v.fenv < 0) v.fenv = 0; break;
            }
        }
        const auto& progs = programs();
        const Program& pr = progs[(size_t) std::clamp(iv(P::prog), 0, (int) progs.size() - 1)];
        const double glide = get(P::glide) / 1000.0;
        const double gk = glide < 0.001 ? 1.0 : 1.0 - std::exp(-1.0 / (glide * tr / 4.0));
        for (int i = 0; i < 3; ++i) {
            Slot& s = v.s[i];
            if (s.pitch < 0) s.pitch = s.target;
            if (dt > 0) {
                s.pitch += (s.target - s.pitch) * gk; if (std::fabs(s.target - s.pitch) < 0.005) s.pitch = s.target;
                s.vibTime += dt;
            }
        }
        // advance the attack programs (the frame shown at note-on lasts one tick)
        if (dt > 0) for (int i = 0; i < 3; ++i) {
            Slot& s = v.s[i];
            if (pr.f.empty()) continue;
            if (s.progPos < (int) pr.f.size()) ++s.progPos;
            if (s.progPos >= (int) pr.f.size() && pr.end == 2) s.progPos = 0;
        }
    }

    // registers from the parameters and the voice's state
    void writeRegs(NVoice& v) {
        Chip& c = v.chip;
        const auto& progs = programs();
        const Program& pr = progs[(size_t) std::clamp(iv(P::prog), 0, (int) progs.size() - 1)];
        const bool progAll = iv(P::ptgt) == 1;
        const double bend = bendNorm * get(P::bend);
        const double vibDepth = get(P::vib), vdel = get(P::vdel) / 1000.0, mwv = modWheel * get(P::mw);
        const bool chip3 = iv(P::mode) == 3;
        int filt = 0; double keyNote = 0, vel = 0; int nk = 0;
        for (int i = 0; i < 3; ++i) {
            const int b = osc(i); Voice& x = c.v[i]; const Slot& s = v.s[i];
            const bool oscOn = on(b);
            // pitch
            double n = s.pitch + iv(b + 5) + get(b + 6) / 100.0 + bend;
            const double fade = vdel > 0 ? std::min(1.0, s.vibTime / vdel) : 1.0;
            n += (vibDepth * fade + mwv) / 100.0 * lfoVal;
            int wave = (on(b + 1) ? TRI : 0) | (on(b + 2) ? SAW : 0) | (on(b + 3) ? PULSE : 0) | (on(b + 4) ? NOISE : 0);
            if (!pr.f.empty() && (progAll || i == 0) && s.gate) {
                int p = s.progPos;
                if (p >= (int) pr.f.size()) p = pr.end == 1 ? (int) pr.f.size() - 1 : -1;
                if (p >= 0) {
                    const Frame& f = pr.f[(size_t) p];
                    if (f.wave >= 0) wave = f.wave;
                    if (f.abs) n = f.pitch + bend; else n += f.pitch;
                }
            }
            const double hz = 440.0 * std::pow(2.0, (n - 69.0) / 12.0);
            x.freq = (uint16_t) std::clamp(std::lround(hz * 16777216.0 / c.clock), 0L, 65535L);
            double pw = get(b + 7) + get(b + 8) * (pwmPhase[i] < .5 ? 4 * pwmPhase[i] - 1 : 3 - 4 * pwmPhase[i]);
            x.pw = (uint16_t) std::clamp(std::lround(pw / 100.0 * 4095.0), 0L, 4095L);
            const bool gate = oscOn && s.gate;
            x.ctrl = (uint8_t) ((oscOn ? wave : 0) | (on(b + 10) ? RING : 0) | (on(b + 11) ? SYNC : 0) | (gate ? GATE : 0));
            x.env.attack = (uint8_t) std::clamp(iv(b + 13), 0, 15); x.env.decay = (uint8_t) std::clamp(iv(b + 14), 0, 15);
            x.env.sustain = (uint8_t) std::clamp(iv(b + 15), 0, 15); x.env.release = (uint8_t) std::clamp(iv(b + 16), 0, 15);
            x.env.setGate(gate);
            if (oscOn && on(b + 12)) filt |= 1 << i;
            if (oscOn && (s.gate || !chip3)) { keyNote += s.pitch; vel = std::max(vel, (double) s.vel); ++nk; }
        }
        if (nk) keyNote /= nk; else keyNote = 60;
        double cut = get(P::cut) + get(P::fenv) * v.fenv + get(P::flfo) * lfoVal
                   + get(P::key) * (keyNote - 60) * 28.0 + get(P::velcut) * (vel - 0.5) * 1400.0 + ccCut * 1024.0;
        c.fc = (uint16_t) std::clamp(std::lround(cut), 0L, 2047L);
        c.res = (uint8_t) std::clamp(iv(P::res), 0, 15);
        c.filt = (uint8_t) filt;
        c.mode = (uint8_t) ((on(P::lp) ? 0x10 : 0) | (on(P::bp) ? 0x20 : 0) | (on(P::hp) ? 0x40 : 0) | (on(P::off3) ? 0x80 : 0));
        c.vol = (uint8_t) std::clamp(iv(P::vol), 0, 15);
        c.updateFilter();
        applyMixer(c);
        // the digi: the NMI timer period follows the note (rounded to whole cycles, as the timer is), and the
        // registers it owns stay as it last wrote them
        c.baseVol = c.vol; c.baseMode = c.mode;
        c.mixDC = c.model == MOS6581 || on(P::dboost) ? 0.5f : 0.06f;
        c.digiLevel = get(P::dlevel);
        c.mahoney = mahoneyTable(c.model);
        if (c.digiActive && cur) {
            double rate = cur->rate;
            if (on(P::dtrack)) rate *= std::pow(2.0, (v.s[v.dslot].pitch + bendNorm * get(P::bend) - get(P::droot)) / 12.0);
            c.nmiPeriod = std::clamp(std::round(c.clock / rate), 30.0, 4000.0);
            c.pwmMax = (int) std::min(4095.0, 65535.0 * c.nmiPeriod / 4096.0);
            if (c.digiMethod == DIGI_MAHONEY) {
                for (auto& x : c.v) { x.ctrl = TEST | PULSE | GATE; x.env.attack = 0; x.env.decay = 0; x.env.sustain = 15; x.env.release = 0; x.env.setGate(true); }
                c.filt = 3; c.fc = 0; c.res = 0; c.updateFilter();
            } else if (c.digiMethod == DIGI_PWM) {
                Voice& x = c.v[0]; const uint16_t pw = x.pw;
                x.freq = 0xFFFF; x.ctrl = PULSE | GATE; x.env.attack = 0; x.env.decay = 0; x.env.sustain = 15; x.env.release = 0; x.env.setGate(true);
                x.pw = c.digiPwm(pw);
            }
            c.keepDigiRegs();
        }
    }


    // the mixer's gains for one chip. Solo: only soloed channels are heard; soloing the filter keeps the voices
    // routed into it feeding it (their direct paths go quiet), and soloing a filtered voice keeps the filter open.
    void applyMixer(Chip& c) {
        static const int lvl[3] = { P::m1lvl, P::m2lvl, P::m3lvl }, pan[3] = { P::m1pan, P::m2pan, P::m3pan };
        static const int mute[3] = { P::m1mute, P::m2mute, P::m3mute }, solo[3] = { P::m1solo, P::m2solo, P::m3solo };
        const bool soloF = on(P::mfsolo);
        bool anySolo = soloF, soloRouted = false;
        for (int i = 0; i < 3; ++i) if (on(solo[i])) { anySolo = true; if (c.filt >> i & 1) soloRouted = true; }
        auto db = [](float d) { return d <= -47.9f ? 0.f : std::pow(10.f, d / 20.f); };
        auto panGains = [](float p, float& l, float& r) { const float a = (p + 1) * 0.25f * (float) kPi; l = std::cos(a); r = std::sin(a); };
        for (int i = 0; i < 3; ++i) {
            const bool routed = c.filt >> i & 1;
            const bool heard = !on(mute[i]) && (!anySolo || on(solo[i]) || (soloF && routed));
            c.vgain[i] = heard ? db(get(lvl[i])) : 0.f;
            // soloing the filter silences the direct paths of voices that aren't routed into it
            panGains(get(pan[i]), c.vpl[i], c.vpr[i]);
        }
        static const int se[3] = { P::s1e, P::s2e, P::s3e }, sr[3] = { P::s1r, P::s2r, P::s3r };
        for (int i = 0; i < 3; ++i) { c.vsa[i] = get(se[i]); c.vsb[i] = get(sr[i]); }
        c.fsa = get(P::sfe); c.fsb = get(P::sfr);
        const bool fHeard = !on(P::mfmute) && (!anySolo || soloF || soloRouted);
        c.fgain = fHeard ? db(get(P::mflvl)) : 0.f;
        panGains(get(P::mfpan), c.fpl, c.fpr);
    }

    // ---------------- sequencer ----------------
    bool hostPlaying = false, hostValid = false, hostRolling = false; double hostPpq = 0, hostBpm = 120;
    double seqPos = 0; long nextStep = 0; int seqNote = -1; double seqOffAt = -1; bool wasRunning = false; bool seqInternal = false;
    double stepBeats() const { static const double d[4] = { 0.5, 0.25, 1.0 / 6, 0.125 }; return d[std::clamp(iv(P::srate), 0, 3)]; }
    double stepTime(long k) const { const double sb = stepBeats(); return k * sb + ((k & 1) ? get(P::swing) * sb * 0.5 : 0); }
    void seqStop() {
        if (seqNote >= 0) { playNoteOff(seqNote, true); seqNote = -1; }
        seqOffAt = -1; seqStepShown = -1;
    }
    void sequencer(int len) {
        const bool follow = on(P::shost) && hostValid;
        const bool internal = seqPlay.load();
        hostRolling = follow && hostPlaying;
        const bool running = internal || hostRolling;
        if (!running) { if (wasRunning) { seqStop(); keyTranspose = 0; transposeKeys.clear(); } wasRunning = false; return; }
        const double bpm = bpmNow();
        const double dBeats = bpm / 60.0 * len / fs;
        double pos;
        if (hostRolling) {
            pos = hostPpq; hostPpq += dBeats;   // advance within the block
            if (!wasRunning || std::fabs(pos - seqPos) > 0.25) {    // started or jumped: find the step at this position
                if (seqNote >= 0) { playNoteOff(seqNote, true); seqNote = -1; }
                nextStep = (long) std::ceil(pos / stepBeats() - 1e-9);
                if (stepTime(nextStep) - pos > stepBeats() * .99) --nextStep;
                seqOffAt = -1;
            }
        } else {
            if (!wasRunning) { seqPos = 0; nextStep = 0; seqOffAt = -1; }
            pos = seqPos;
        }
        wasRunning = true;
        const double end = pos + dBeats;
        const int patIdx = std::clamp(iv(P::spat) - 1, 0, kPatterns - 1), slen = std::clamp(iv(P::slen), 1, kSteps);
        if (seqOffAt >= 0 && seqOffAt < end) { if (seqNote >= 0) playNoteOff(seqNote, true); seqNote = -1; seqOffAt = -1; }
        while (stepTime(nextStep) < end) {
            const int si = (int) (((nextStep % slen) + slen) % slen);
            const Step st = step(patIdx, si);
            seqStepShown = si;
            if (st.on) {
                const int note = std::clamp(st.note + keyTranspose, 0, 127);
                const Step prev = step(patIdx, ((si - 1) % slen + slen) % slen);
                const bool tie = prev.slide && prev.on && seqNote >= 0;
                const float vel = st.accent ? 1.0f : 0.72f;
                if (tie) {
                    // slide: the new note starts before the old one stops, so MONO / LEGATO glide into it
                    const int old = seqNote; playNoteOn(note, vel, true); if (old != note) playNoteOff(old, true);
                } else {
                    if (seqNote >= 0) playNoteOff(seqNote, true);
                    playNoteOn(note, vel, true);
                }
                seqNote = note;
                seqOffAt = st.slide ? -1 : stepTime(nextStep) + stepBeats() * get(P::sgate);
            }
            ++nextStep;
        }
        if (!hostRolling) seqPos = end; else seqPos = end;
    }

    // ---------------- sound ----------------
    // the chip's buses, decimated: main L / R, echo send L / R, reverb send
    std::vector<float> firCoef[3]; std::vector<float> firBuf[5]; int firPos = 0;
    double extHp[5] {}, extLp[5] {}, extHpA = 0.999, extLpA = 0.5, extIn[5] {};
    // the reverb: the Omega REV7 engine (plugins/dsp/Rev7.h), fed by the reverb sends, wet only
    std::unique_ptr<omega::Engine> rev7; std::vector<double> r7vals, r7last; std::vector<int> r7map;
    std::vector<float> rvIn, rvL, rvR, preL, preR; float pkRev = 0;
    void initRev7() {
        const omega::Spec& sp = rev7Spec();
        rev7 = sp.make(sp);
        r7vals.assign(sp.params.size(), 0.0);
        for (size_t i = 0; i < sp.params.size(); ++i) { const auto& q = sp.params[i]; r7vals[i] = q.choices.empty() ? q.def : q.choices[(size_t) q.defChoice].value; }
        // the SID parameters, in REV7 order of the table below
        r7map.clear();
        for (auto& m : rev7Ids()) r7map.push_back(sp.index(m.second));
        auto set = [&](const char* id, double v) { const int k = sp.index(id); if (k >= 0) r7vals[(size_t) k] = v; };
        set("omix", 1); set("mute", 0); set("bypass", 0); set("mono", 0); set("ret", 1);   // wet only: the return fader mixes it
        r7last.clear();
    }
    void syncRev7() {
        const auto& ids = rev7Ids();
        for (size_t i = 0; i < ids.size(); ++i) {
            if (r7map[i] < 0) continue;
            double v = get(ids[i].first);
            if (ids[i].first == P::r7prog || ids[i].first == P::r7mode) v = std::round(v) + 1;   // menus count from 1
            r7vals[(size_t) r7map[i]] = v;
        }
        if (r7vals != r7last) { rev7->setValues(r7vals); r7last = r7vals; }
    }
public:
    // SID parameter -> REV7 parameter id
    static const std::vector<std::pair<int, const char*>>& rev7Ids() {
        static const std::vector<std::pair<int, const char*>> t = {
            { P::r7prog, "prog" }, { P::r7rt, "rt" }, { P::r7hi, "hi" }, { P::r7lo, "lo" }, { P::r7diff, "diff" }, { P::r7idly, "idly" },
            { P::r7fdly, "fdly" }, { P::r7flev, "flev" }, { P::r7live, "live" }, { P::r7mode, "mode" }, { P::r7size, "size" },
            { P::r7dlyL, "dlyL" }, { P::r7dlyR, "dlyR" }, { P::r7dlev, "dlev" }, { P::r7fb, "fb" }, { P::r7hdmp, "hdamp" },
            { P::r7mfrq, "mfreq" }, { P::r7mdep, "mdep" }, { P::r7mdly, "mdly" }, { P::r7am, "amdep" }, { P::r7in, "in" },
            { P::r7eq, "eq" }, { P::r7elf, "eqlf" }, { P::r7elg, "eqlg" }, { P::r7emf, "eqmf" }, { P::r7emg, "eqmg" },
            { P::r7ehf, "eqhf" }, { P::r7ehg, "eqhg" }, { P::r7circ, "circ" }, { P::r7phase, "phase" } };
        return t;
    }
    // one of the Omega REV7's presets (factory 1-30, artists, library), onto the SID's REV7 parameters
    void applyRev7Preset(int i) {
        const omega::Spec& sp = rev7Spec();
        if (i < 0 || i >= (int) sp.presets.size()) return;
        for (auto& m : rev7Ids()) { const int k = sp.index(m.second); if (k >= 0) { const auto& q = sp.params[(size_t) k]; set(m.first, m.first == P::r7prog || m.first == P::r7mode ? (float) q.defChoice : q.def); } }
        for (auto& kv : sp.presets[(size_t) i].values)
            for (auto& m : rev7Ids()) if (kv.first == m.second) set(m.first, (float) (m.first == P::r7prog || m.first == P::r7mode ? kv.second - 1 : kv.second));
        rev7Preset = i;
    }
    int rev7Preset = -1;
private:
    std::vector<float> echoL, echoR; int echoW = 0; float echoLpL = 0, echoLpR = 0;
    float pkEcho = 0, pkL = 0, pkR = 0;

    void designFir(int q) {
        const int os = 2 << q, taps = 24 * os;
        auto& c = firCoef[q]; c.assign((size_t) taps, 0.f);
        const double fc = 0.46 / os;   // relative to the internal rate: a little under the output Nyquist
        double sum = 0;
        for (int i = 0; i < taps; ++i) {
            const double m = i - (taps - 1) / 2.0;
            const double sinc = std::fabs(m) < 1e-9 ? 2 * fc : std::sin(2 * kPi * fc * m) / (kPi * m);
            const double w = 0.42 - 0.5 * std::cos(2 * kPi * i / (taps - 1)) + 0.08 * std::cos(4 * kPi * i / (taps - 1));   // Blackman
            c[(size_t) i] = (float) (sinc * w); sum += sinc * w;
        }
        for (auto& x : c) x = (float) (x / sum);
    }

    void synth(float* L, float* R, int n) {
        const int os = osFactor(), nv = kChips;
        const auto& coef = firCoef[std::clamp(curOS, 0, 2)];
        const int taps = (int) coef.size();
        const float spread = get(P::spread);
        const bool single = iv(P::mode) != 0;
        for (int i = 0; i < nv; ++i) {
            const float p = single ? 0.f : (nv > 1 ? spread * ((i % 2 ? 1.f : -1.f) * (0.35f + 0.65f * (float) (i / 2) / 3.f)) : 0.f);
            voices[i].panL = std::sqrt(0.5f * (1 - p)); voices[i].panR = std::sqrt(0.5f * (1 + p));
        }
        const float g = std::pow(10.f, get(P::gain) / 20.f);
        // echo setup
        static const double ediv[6] = { 1.0, 0.75, 0.5, 1.0 / 3, 0.25, 0.375 };
        const double eSec = on(P::esync) ? ediv[std::clamp(iv(P::ediv), 0, 5)] * 60.0 / bpmNow() : get(P::etime) / 1000.0;
        const int eLen = (int) std::clamp(eSec * fs, 1.0, (double) echoL.size() - 2);
        const float emix = get(P::emix), efb = get(P::efb), etone = 0.08f + 0.9f * get(P::etone);
        const float rret = on(P::rmute) ? 0.f : get(P::rret), erev = get(P::erev);
        const bool revOn = get(P::rret) > 0.0001f;
        if (revOn) syncRev7();
        for (int k = 0; k < n; ++k) {
            for (int o = 0; o < os; ++o) {
                float ch[5] = { 0, 0, 0, 0, 0 };
                for (int i = 0; i < nv; ++i) {
                    NVoice& v = voices[i];
                    if (v.asleep) continue;
                    float o5[5]; v.chip.tick(o5);
                    const float gl = v.level * v.panL * 1.41421356f, gr = v.level * v.panR * 1.41421356f;
                    ch[0] += o5[0] * gl; ch[1] += o5[1] * gr; ch[2] += o5[2] * gl; ch[3] += o5[3] * gr; ch[4] += o5[4] * v.level;
                }
                // C64 board: 10 uF / 1 k high-pass, 10 k / 1 nF low-pass on the audio output
                for (int c2 = 0; c2 < 5; ++c2) {
                    const double x = ch[c2];
                    extHp[c2] = extHpA * (extHp[c2] + x - extIn[c2]); extIn[c2] = x;
                    extLp[c2] = extHp[c2] + extLpA * (extLp[c2] - extHp[c2]);
                    firBuf[(size_t) c2][(size_t) firPos] = (float) extLp[c2];
                    firBuf[(size_t) c2][(size_t) (firPos + 2048)] = (float) extLp[c2];
                }
                firPos = (firPos + 1) & 2047;
            }
            // decimate: one FIR output per output sample
            float y5[5] = { 0, 0, 0, 0, 0 };
            for (int c2 = 0; c2 < 5; ++c2) {
                const float* bb = firBuf[(size_t) c2].data() + firPos + 2048 - taps; float a = 0;
                for (int t = 0; t < taps; ++t) a += bb[t] * coef[(size_t) t];
                y5[c2] = a;
            }
            float yl = y5[0], yr = y5[1];
            // echo, fed by the echo sends
            int rp = echoW - eLen; if (rp < 0) rp += (int) echoL.size();
            const float dl = echoL[(size_t) rp], dr = echoR[(size_t) rp];
            echoLpL += etone * (dl - echoLpL); echoLpR += etone * (dr - echoLpR);
            echoL[(size_t) echoW] = y5[2] + echoLpR * efb;     // ping-pong
            echoR[(size_t) echoW] = y5[3] + echoLpL * efb;
            if (++echoW >= (int) echoL.size()) echoW = 0;
            const float ret = on(P::emute) ? 0.f : emix;
            yl += ret * echoLpL; yr += ret * echoLpR;
            pkEcho = std::max(pkEcho, std::max(std::fabs(ret * echoLpL), std::fabs(ret * echoLpR)));
            // the REV7's input: the reverb sends (and the echo, if asked); it runs after this loop
            rvIn[(size_t) k] = y5[4] + erev * 0.5f * (echoLpL + echoLpR) * emix;
            preL[(size_t) k] = yl; preR[(size_t) k] = yr;
        }
        if (revOn) {
            omega::Transport tr; tr.bpm = bpmNow();
            rev7->process(rvIn.data(), rvIn.data(), nullptr, nullptr, rvL.data(), rvR.data(), n, tr);
        }
        for (int k = 0; k < n; ++k) {
            float yl = preL[(size_t) k], yr = preR[(size_t) k];
            if (revOn) {
                const float rl = rvL[(size_t) k] * rret, rr = rvR[(size_t) k] * rret;
                yl += rl; yr += rr; pkRev = std::max(pkRev, std::max(std::fabs(rl), std::fabs(rr)));
            }
            yl = ftanh(yl * g * 1.6f) / 1.6f; yr = ftanh(yr * g * 1.6f) / 1.6f;
            L[k] = yl; R[k] = yr;
            pkL = std::max(pkL, std::fabs(yl)); pkR = std::max(pkR, std::fabs(yr));
            const int w = scopeW.load(std::memory_order_relaxed);
            scope[(size_t) (w & 511)].store(0.5f * (yl + yr), std::memory_order_relaxed); scopeW.store(w + 1, std::memory_order_relaxed);
        }
        // meters: the loudest chip for each channel, with a ~300 ms fall
        {
            float pk[M_COUNT] {};
            for (auto& v : voices) {
                for (int i = 0; i < 3; ++i) { pk[i] = std::max(pk[i], v.chip.vpk[i] * v.level); v.chip.vpk[i] = 0; }
                pk[M_FILTER] = std::max(pk[M_FILTER], v.chip.fpk * v.level); v.chip.fpk = 0;
            }
            // voice and filter peaks are inside the chip, before its volume and output scaling
            const float chipScale = (float) (iv(P::vol) / 15.0 * 0.72) * g;
            for (int i = 0; i <= M_FILTER; ++i) pk[i] *= chipScale;
            pk[M_ECHO] = pkEcho * g; pk[M_REV] = pkRev * g; pk[M_OUTL] = pkL; pk[M_OUTR] = pkR;
            pkEcho = pkL = pkR = pkRev = 0;
            const float fall = std::exp(-(float) n / (float) (fs * 0.3));
            for (int i = 0; i < M_COUNT; ++i) meters[(size_t) i].store(std::max(pk[i], meters[(size_t) i].load(std::memory_order_relaxed) * fall), std::memory_order_relaxed);
        }
        {
            int shown = -1; uint64_t a = 0;
            for (auto& v : voices) if (v.chip.digiActive && v.age >= a) { a = v.age; shown = v.chip.digiIdx; }
            digiShown.store(shown, std::memory_order_relaxed);
        }
        // chips that have finished go to sleep
        for (auto& v : voices) if (!v.asleep && !v.anyGate() && v.chip.silent()) { v.asleep = true; v.chip.reset(); }
    }

    // ---------------- the UI queue ----------------
    struct Ev { int type; int a; float b; };
    std::array<Ev, 256> evq {}; std::atomic<int> qW { 0 }; int qR = 0;
    void drainQueue() {
        const int w = qW.load(std::memory_order_acquire);
        while (qR != w) {
            const Ev e = evq[(size_t) (qR & 255)]; ++qR;
            switch (e.type) {
                case EV_ON: noteOn(e.a, e.b); break;
                case EV_OFF: noteOff(e.a); break;
                case EV_PANIC: panic(); break;
                case EV_SEQ_PLAY: seqPlay = true; break;
                case EV_SEQ_STOP: seqPlay = false; break;
                case EV_BEND: bendNorm = e.b; break;
                case EV_WHEEL: modWheel = e.b; break;
                case EV_ALLOFF: allNotesOff(); break;
                default: break;
            }
        }
    }
};

}  // namespace sid
