// Omega DMX engine: the drum machine of dmx/app.js in plain C++ (no JUCE), shared by the plugin and the tests.
// Same kits (Kits.inc, generated from dmx/kits.js), same 16-step patterns with accents, swing, fader curve,
// open-hat choke, song mode and the same panel keys (keypad, SWING / SONG / COPY / TEMPO / RECORD / ERASE / CLEAR / FACTORY).
// Timing follows the host's beat position when it plays, or its own clock (PLAY on the panel, the standalone app).
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "Kits.inc"
#include "Circuit.h"

namespace dmx {

inline float fgain(float v) { return v * v * 1.25f; }       // fader position to gain, as in the app
constexpr float kVel[3] = {0.f, .72f, 1.f};                  // step value: off, hit, accent
constexpr int kSwings[6] = {50, 54, 58, 62, 66, 71};
constexpr int kVol = kGroups - 1, kMet = kGroups - 2;       // the last two faders: the metronome (down = no click) and the master volume

struct Sample { std::vector<float> d; double sr = 44100; };

// 8 / 16 / 24-bit PCM or 32-bit float WAV, mixed to mono
inline bool readWav(const void* data, size_t n, Sample& out) {
    auto* b = static_cast<const uint8_t*>(data);
    auto u16 = [&](size_t o) { return (uint32_t) b[o] | ((uint32_t) b[o + 1] << 8); };
    auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
    if (n < 12 || std::memcmp(b, "RIFF", 4) || std::memcmp(b + 8, "WAVE", 4)) return false;
    int tag = 0, ch = 0, bits = 0; uint32_t sr = 0; const uint8_t* pcm = nullptr; size_t len = 0;
    for (size_t i = 12; i + 8 <= n;) {
        uint32_t sz = u32(i + 4);
        if (!std::memcmp(b + i, "fmt ", 4) && i + 24 <= n) { tag = (int) u16(i + 8); ch = (int) u16(i + 10); sr = u32(i + 12); bits = (int) u16(i + 22);
            if (tag == 0xFFFE && sz >= 40) tag = (int) u16(i + 32); }
        else if (!std::memcmp(b + i, "data", 4)) { pcm = b + i + 8; len = std::min<size_t>(sz, n - i - 8); }
        i += 8 + sz + (sz & 1);
    }
    if (!pcm || ch < 1 || !sr || !(tag == 1 || tag == 3)) return false;
    const int bps = bits / 8; const size_t frames = len / (size_t) (bps * ch);
    out.sr = sr; out.d.assign(frames, 0.f);
    for (size_t f = 0; f < frames; ++f) {
        float s = 0;
        for (int c = 0; c < ch; ++c) {
            const uint8_t* p = pcm + (f * (size_t) ch + (size_t) c) * (size_t) bps;
            float v = 0;
            if (tag == 3 && bits == 32) { float x; std::memcpy(&x, p, 4); v = x; }
            else if (bits == 16) v = (float) (int16_t) (p[0] | (p[1] << 8)) / 32768.f;
            else if (bits == 24) v = (float) ((int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) >> 8) / 8388608.f;
            else if (bits == 8) v = ((float) p[0] - 128.f) / 128.f;
            s += v;
        }
        out.d[f] = s / (float) ch;
    }
    return true;
}

struct Pattern {
    std::string name;
    uint8_t rows[kMaxVoices][kSteps] {};
    bool used() const { for (auto& r : rows) for (auto v : r) if (v) return true; return false; }
};

// a tiny spin lock: the panel edits patterns on the message thread while the audio thread plays them
struct Spin {
    std::atomic_flag f = ATOMIC_FLAG_INIT;
    void lock() { while (f.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { f.clear(std::memory_order_release); }
};
struct Locked { Spin& s; explicit Locked(Spin& x) : s(x) { s.lock(); } ~Locked() { s.unlock(); } };

class Engine {
public:
    struct Host { bool playing = false; double ppq = 0, bpm = 120; };
    struct Hit { int offset; int voice; float vel; };

    std::array<std::vector<Sample>, kKits> samples;          // filled by the plugin from its embedded kits (or the test from disk)
    // ---- saved state ----
    int kit = 0;
    std::array<std::array<Pattern, kPatterns>, kKits> pats;
    std::array<int, kKits> cur {};
    std::array<std::vector<int>, kKits> song;
    std::array<bool, kKits> useSong {};
    std::array<std::array<std::string, kMaxVoices>, kKits> names;
    float fader[kGroups] = {.8f, .8f, .8f, .8f, .8f, .8f, .8f, 0.f, .85f};
    double tempo = 112; int swing = 50;
    std::array<int, kKits> circuitState;                      // -1: the default (on, except the DMX kit, which is recorded from a DMX), 0 off, 1 on
    std::array<std::array<int, kMaxVoices>, kKits> tune;      // each voice's pitch trimmer, 0..100 (50 = centre)
    // ---- panel ----
    int sel = 0; bool acc = false, rec = false; std::string mode, typed; int clearArm = 0;
    std::string toast;                                        // a message for the panel to show (message thread)
    std::atomic<bool> internalPlay { false };
    std::atomic<int> nowStep { -1 }, nowPattern { 0 }, flashVoice { -1 };
    std::atomic<bool> running { false };
    std::vector<Hit> pending;                                 // notes to play this block (MIDI, keys), sample offsets
    Spin lock;

    Engine() {
        for (int k = 0; k < kKits; ++k) for (int p = 0; p < kPatterns; ++p) factory(k, p);
        circuitState.fill(-1); for (auto& t : tune) t.fill(50);
        for (int k = 0; k < kKits; ++k) {   // each voice's card and trigger mode
            const auto& K = kKitDefs[k]; const char* f[kMaxVoices]; const char* nm[kMaxVoices]; int g[kMaxVoices];
            for (int v = 0; v < K.voices; ++v) { f[v] = K.v[v].file; nm[v] = K.v[v].name; g[v] = K.v[v].group; }
            for (int v = 0; v < K.voices; ++v) place[(size_t) k][(size_t) v] = circuit::cardFor(f, nm, g, K.voices, v);
        }
        makeClicks(48000);
    }
    bool circuitOn(int k = -1) const { if (k < 0) k = kit; return circuitState[(size_t) k] >= 0 ? circuitState[(size_t) k] == 1 : k != 0; }
    // each voice's ROM for its card (built the first time the kit plays through the circuit)
    void buildRoms(int only = -1) {
        for (int k = 0; k < kKits; ++k) if (only < 0 || k == only) for (int v = 0; v < kKitDefs[k].voices && v < (int) samples[(size_t) k].size(); ++v) {
            const auto& s = samples[(size_t) k][(size_t) v]; if (s.d.empty()) continue;
            roms[(size_t) k][(size_t) v] = circuit::makeRom(std::vector<double>(s.d.begin(), s.d.end()), s.sr, circuit::CARDS[place[(size_t) k][(size_t) v].card]);
        }
    }
    // the card's output for a voice (or a whole kit) at the current rate; rendered outside the lock, swapped in under it
    void renderVoices(int k, int only = -1) {
        if (!circuitOn(k)) return;
        if (!romsBuilt[(size_t) k]) { buildRoms(k); romsBuilt[(size_t) k] = true; }
        for (int v = 0; v < kKitDefs[k].voices; ++v) {
            if (only >= 0 && v != only) continue;
            const auto& R = roms[(size_t) k][(size_t) v]; if (R.rom.empty()) continue;
            const auto& pl = place[(size_t) k][(size_t) v];
            Sample out; out.sr = sr; out.d = circuit::render(R, circuit::CARDS[pl.card], pl.mode, tune[(size_t) k][(size_t) v], sr);
            Locked l(lock); for (auto& x : vs) if (x.s == &circ[(size_t) k][(size_t) v]) x.on = false;
            circ[(size_t) k][(size_t) v] = std::move(out); rendered[(size_t) k] = true;
        }
    }
    void refresh() { rendered.fill(false); if (sr > 0) renderVoices(kit); }
    void setCircuit(bool on) { circuitState[(size_t) kit] = on ? 1 : 0; if (on) renderVoices(kit); toast = on ? "Circuit: the DMX voice cards" : "Circuit off: the sounds as recorded"; }
    void setTune(int vi, int v) { tune[(size_t) kit][(size_t) vi] = std::clamp(v, 0, 100); renderVoices(kit, vi); hit(vi, 1.f); }

    int voices(int k = -1) const { return kKitDefs[k < 0 ? kit : k].voices; }
    std::string voiceName(int vi, int k = -1) const { if (k < 0) k = kit; return names[(size_t) k][(size_t) vi].empty() ? kKitDefs[k].v[vi].name : names[(size_t) k][(size_t) vi]; }
    Pattern& pattern(int p) { return pats[(size_t) kit][(size_t) p]; }
    int current() const { return cur[(size_t) kit]; }
    std::vector<int>& songList() { return song[(size_t) kit]; }

    void factory(int k, int p) {
        Pattern& P = pats[(size_t) k][(size_t) p]; P = Pattern(); P.name = kPatternNames[p];
        for (int v = 0; v < kKitDefs[k].voices; ++v) for (int s = 0; s < kSteps; ++s) P.rows[v][s] = (uint8_t) (kFactory[k][p][v][s] - '0');
    }
    static std::string rowString(const Pattern& P, int v) { std::string s(kSteps, '0'); for (int i = 0; i < kSteps; ++i) s[(size_t) i] = (char) ('0' + P.rows[v][i]); return s; }
    static void setRow(Pattern& P, int v, const std::string& s) { for (int i = 0; i < kSteps && i < (int) s.size(); ++i) P.rows[v][i] = (uint8_t) std::clamp(s[(size_t) i] - '0', 0, 2); }

    void prepare(double sampleRate) {
        sr = sampleRate; for (auto& v : vs) v.on = false; rendered.fill(false);
        for (int g = 0; g < kGroups; ++g) gNow[g] = gainOf(fader[g]);
        vNow = gainOf(fader[kVol]); makeClicks(sr); busL.assign(kGroups, {}); busR.assign(kGroups, {}); renderVoices(kit);
    }
    // faders: the clean engine's curve, or (circuit) the DMX's audio-taper pots; the same level at .8
    float gainOf(float v) const { return circuitOn() ? (float) (circuit::potLaw(v) * circuit::mixK()) : fgain(v); }

    // ---------------- audio ----------------
    void process(float* L, float* R, int n, const Host& host) {
        Locked l(lock);
        for (auto& b : busL) b.assign((size_t) n, 0.f);
        for (auto& b : busR) b.assign((size_t) n, 0.f);
        const bool hostRun = host.playing, run = hostRun || internalPlay.load();
        const double bpm = hostRun ? (host.bpm > 0 ? host.bpm : 120) : tempo;
        if (hostRun) tempo = std::round(std::clamp(bpm, 40.0, 250.0));
        if (!run) { if (wasRunning) stopped(); }
        else {
            if (!wasRunning) { started = true; if (!hostRun) intPpq = 0; }
            const double ppq0 = hostRun ? host.ppq : intPpq, perSample = bpm / 60.0 / sr, ppq1 = ppq0 + perSample * n;
            const long k0 = (long) std::floor(ppq0 * 4) - 1, k1 = (long) std::floor(ppq1 * 4) + 1;
            for (long k = k0; k <= k1; ++k) {
                if (k < 0) continue;
                const double t = k * .25 + swingOff(k);
                if (t < ppq0 || t >= ppq1) continue;
                stepAt(k, (int) ((t - ppq0) / perSample));
            }
            for (auto& h : pending) recordAt(h, ppq0 + h.offset * perSample);
            if (!hostRun) intPpq = ppq1;
            lastPpq = ppq1;
        }
        wasRunning = run; running = run;
        for (auto& h : pending) start(h.voice, h.vel, h.offset);
        pending.clear();
        // voices into their fader groups, then the groups with their faders (smoothed over the block) into the output
        for (auto& v : vs) if (v.on) renderVoice(v, n);
        std::vector<float>& outR = scratch; outR.assign((size_t) n, 0.f);
        for (int i = 0; i < n; ++i) L[i] = 0;
        for (int g = 0; g < kGroups; ++g) {
            const float to = gainOf(fader[g]), from = gNow[g]; gNow[g] = to;
            if (g == kVol || g == kMet) continue;
            const float* bl = busL[(size_t) g].data(); const float* br = busR[(size_t) g].data();
            for (int i = 0; i < n; ++i) { const float k = from + (to - from) * (float) i / (float) n; L[i] += bl[i] * k; outR[(size_t) i] += br[i] * k; }
        }
        const float vFrom = vNow, vTo = gainOf(fader[kVol]); vNow = vTo;
        const float* cl = busL[(size_t) kVol].data(); const float* cr = busR[(size_t) kVol].data();
        for (int i = 0; i < n; ++i) { const float g = vFrom + (vTo - vFrom) * (float) i / (float) n; L[i] = L[i] * g + cl[i]; const float r = outR[(size_t) i] * g + cr[i]; if (R) R[i] = r; else L[i] = (L[i] + r) * .5f; }
    }

    // a key or a MIDI note: sounds now (offset in this block) and records when RECORD is lit
    void hit(int vi, float vel, int offset = 0) { if (vi < 0 || vi >= voices()) return; Locked l(lock); pending.push_back({offset, vi, vel}); flashVoice = vi; }
    void note(int n, int velocity, int offset) { const int vi = kNoteVoice[kit][n & 127]; if (vi < 0) return; sel = vi; hit(vi, velocity >= 100 || acc ? 1.f : std::max(.25f, (float) velocity / 127.f * .9f), offset); }

    // ---------------- the panel (message thread) ----------------
    void keypad(char k) {
        Locked l(lock);
        auto& ps = pats[(size_t) kit]; const int n = k == '0' ? 9 : k - '1';
        if (mode == "tempo") {
            if (k == '<' || k == '>') { tempo = std::clamp(tempo + (k == '>' ? 1 : -1), 40.0, 250.0); typed.clear(); }
            else { typed = (typed + k); if (typed.size() > 3) typed = typed.substr(typed.size() - 3);
                if (typed.size() == 3 || (typed.size() == 2 && std::stoi(typed) >= 40 && typed[0] > '2')) { tempo = std::clamp((double) std::stoi(typed), 40.0, 250.0); typed.clear(); } }
            return;
        }
        if (mode == "tune") {   // the voice card's pitch trimmer
            if (k == '<' || k == '>') { typed.clear(); pendingTune = std::clamp(tune[(size_t) kit][(size_t) sel] + (k == '>' ? 5 : -5), 0, 100); }
            else { typed += k; if (typed.size() == 2) { pendingTune = std::stoi(typed); typed.clear(); } }
            return;
        }
        if (mode == "song") { if (k != '<' && k != '>') songList().push_back(n); return; }
        if (mode == "copy") { if (k == '<' || k == '>') return; ps[(size_t) n] = ps[(size_t) current()]; mode.clear();
            toast = "Pattern " + std::to_string(current() + 1) + " copied to " + std::to_string(n + 1); return; }
        if (mode == "swing") mode.clear();
        const int to = k == '<' ? (current() + 9) % 10 : k == '>' ? (current() + 1) % 10 : n;
        useSong[(size_t) kit] = false;
        if (running) queued = to; else cur[(size_t) kit] = to;
    }
    void fn(const std::string& f) {
        Locked l(lock);
        auto& ps = pats[(size_t) kit];
        if (f != "clear") clearArm = 0;
        if (f == "tune") { mode = mode == "tune" ? "" : "tune"; typed.clear(); return; }
        if (f == "swing") { if (mode == "swing") { int i = 0; while (i < 6 && kSwings[i] != swing) ++i; swing = kSwings[(i + 1) % 6]; } mode = "swing"; }
        else if (f == "tempo") { mode = mode == "tempo" ? "" : "tempo"; typed.clear(); }
        else if (f == "copy") mode = mode == "copy" ? "" : "copy";
        else if (f == "song") { if (mode == "song") { mode.clear(); useSong[(size_t) kit] = !songList().empty(); songPos = -1; } else mode = "song"; }
        else if (f == "rec") { rec = !rec; if (rec && !running) toast = "RECORD: press PLAY and play the keys"; }
        else if (f == "erase") {
            if (mode == "song") { if (!songList().empty()) songList().pop_back(); }
            else { auto& P = ps[(size_t) (running ? playing : current())]; std::memset(P.rows[sel], 0, kSteps); toast = "Erased " + voiceName(sel); }
        } else if (f == "clear") {
            if (!clearArm) { clearArm = 1; toast = "Press CLEAR again to clear pattern " + std::to_string(current() + 1); }
            else { clearArm = 0; auto& P = ps[(size_t) current()]; std::memset(P.rows, 0, sizeof P.rows); P.name = "Pattern " + std::to_string(current() + 1);
                toast = "Pattern " + std::to_string(current() + 1) + " cleared"; }
        } else if (f == "factory") { factory(kit, current()); toast = "Factory pattern: " + ps[(size_t) current()].name; }
    }
    void toggleStep(int vi, int s, int value) { Locked l(lock); pattern(running ? playing : current()).rows[vi][s] = (uint8_t) value; }
    int stepValue(int vi, int s) const { return pats[(size_t) kit][(size_t) (running ? playing : current())].rows[vi][s]; }
    void setKit(int k) { { Locked l(lock); kit = std::clamp(k, 0, kKits - 1); sel = 0; queued = -1; mode.clear(); for (auto& v : vs) v.on = false; } if (!rendered[(size_t) kit]) renderVoices(kit); }

    std::string led() const {
        auto pad = [](int v, int w) { std::string s = std::to_string(v); while ((int) s.size() < w) s = " " + s; return s; };
        auto pad0 = [](int v) { return (v < 10 ? "0" : "") + std::to_string(v); };
        const auto& sg = song[(size_t) kit];
        if (mode == "tempo") { std::string t = typed.empty() ? std::to_string((int) tempo) : typed; while (t.size() < 4) t = " " + t; return "t" + t; }
        if (mode == "swing") return "SU " + pad(swing, 3);
        if (mode == "tune") { std::string t = typed.empty() ? std::to_string(tune[(size_t) kit][(size_t) sel]) : typed + std::string(2 - typed.size(), '_'); while (t.size() < 3) t = " " + t; return "tu " + t; }
        if (mode == "copy") return "CP" + pad0(current() + 1) + " -";
        if (mode == "song") return "S" + pad0((int) sg.size()) + " " + (sg.empty() ? std::string("--") : pad0(sg.back() + 1));
        if (useSong[(size_t) kit] && !sg.empty() && running) return "S" + pad0(std::max(0, songPos) + 1) + " " + pad0(playing + 1);
        return pad0(current() + 1) + " " + pad((int) tempo, 3);
    }
    std::string msg() const {
        const auto& sg = song[(size_t) kit]; std::string list; for (int p : sg) list += (list.empty() ? "" : "-") + std::to_string(p + 1);
        if (mode == "tempo") return "TEMPO: TYPE IT ON THE KEYPAD, < > NUDGES";
        if (mode == "swing") return "SWING " + std::to_string(swing) + "%: PRESS SWING TO STEP";
        if (mode == "tune") return "TUNE " + voiceName(sel) + ": 00-99 ON THE KEYPAD, < > STEPS 5, A VOICE KEY PICKS";
        if (mode == "copy") return "COPY PATTERN " + std::to_string(current() + 1) + " TO... PRESS A NUMBER";
        if (mode == "song") return "SONG: NUMBERS ADD PATTERNS, ERASE REMOVES (" + list + ")";
        std::string q = queued >= 0 ? " > NEXT " + std::to_string(queued + 1) : "";
        return std::string(kKitDefs[kit].name) + " - " + (useSong[(size_t) kit] && !sg.empty() ? "SONG " + list : "PATTERN " + std::to_string(current() + 1) + " - " + pats[(size_t) kit][(size_t) current()].name) + q;
    }
    int playingPattern() const { return running ? playing : current(); }
    // the panel applies a typed tune outside the engine's lock (rendering takes a moment)
    int takePendingTune() { const int t = pendingTune; pendingTune = -1; return t; }

private:
    struct Voice { const Sample* s = nullptr; double pos = 0, inc = 1; float gain = 0, panL = 1, panR = 1; int group = 0; bool open = false, on = false; int startAt = 0, chokeAt = -1; float fade = 1; };
    std::array<Voice, 48> vs {};
    std::vector<std::vector<float>> busL, busR; std::vector<float> scratch;
    std::array<std::array<circuit::Placement, kMaxVoices>, kKits> place {};
    std::array<std::array<circuit::Rom, kMaxVoices>, kKits> roms;
    std::array<std::array<Sample, kMaxVoices>, kKits> circ;
    std::array<bool, kKits> rendered {}, romsBuilt {};
    int pendingTune = -1;
    Sample clickHi;
    double sr = 48000, intPpq = 0, lastPpq = 0;
    float gNow[kGroups] {}, vNow = 1;
    bool wasRunning = false, started = false;
    int playing = 0, queued = -1, songPos = 0; long lastBar = -1;

    double swingOff(long k) const { return (k & 1) ? (swing / 50.0 - 1.0) * .25 : 0.0; }   // in beats: odd sixteenths late
    void stopped() { nowStep = -1; lastBar = -1; if (queued >= 0) { cur[(size_t) kit] = queued; queued = -1; } }
    int patternForBar(long bar) {
        const auto& sg = song[(size_t) kit];
        if (useSong[(size_t) kit] && !sg.empty()) { songPos = (int) (bar % (long) sg.size()); return sg[(size_t) songPos]; }
        if (bar != lastBar && queued >= 0) { cur[(size_t) kit] = queued; queued = -1; }
        return cur[(size_t) kit];
    }
    void stepAt(long k, int offset) {
        const long bar = k / kSteps; const int s = (int) (k % kSteps);
        if (bar != lastBar || started) { playing = patternForBar(bar); lastBar = bar; started = false; }
        const Pattern& P = pats[(size_t) kit][(size_t) playing];
        for (int v = 0; v < voices(); ++v) if (P.rows[v][s]) start(v, kVel[P.rows[v][s]], offset);
        if (fader[kMet] > .02f && s % 4 == 0) startClick(s == 0, offset);
        nowStep = s; nowPattern = playing;
    }
    void recordAt(const Hit& h, double ppq) {
        if (!rec) return;
        const long k = (long) std::llround(ppq * 4), bar = k / kSteps;
        const int p = bar == lastBar ? playing : patternForBarPeek(bar);
        pats[(size_t) kit][(size_t) p].rows[h.voice][k % kSteps] = acc ? 2 : 1;
    }
    int patternForBarPeek(long bar) const { const auto& sg = song[(size_t) kit]; if (useSong[(size_t) kit] && !sg.empty()) return sg[(size_t) (bar % (long) sg.size())]; return queued >= 0 ? queued : cur[(size_t) kit]; }
    Voice* freeVoice() { for (auto& v : vs) if (!v.on) return &v; Voice* o = &vs[0]; for (auto& v : vs) if (v.pos > o->pos) o = &v; return o; }
    void start(int vi, float vel, int offset) {
        const auto& def = kKitDefs[kit].v[vi]; const auto& smp = samples[(size_t) kit]; const auto& pl = place[(size_t) kit][(size_t) vi]; const auto& card = circuit::CARDS[pl.card];
        const bool circ1 = circuitOn();
        const Sample* src = circ1 ? &circ[(size_t) kit][(size_t) vi] : (vi < (int) smp.size() ? &smp[(size_t) vi] : nullptr);
        if (!src || src->d.empty()) return;
        if (def.group == 2 && !def.open) for (auto& v : vs) if (v.on && v.open && v.chokeAt < 0) v.chokeAt = offset;   // a closed hat cuts the open one
        Voice* v = freeVoice(); *v = Voice(); v->s = src; v->gain = vel; v->group = def.group; v->open = def.open; v->on = true; v->startAt = offset;
        if (circ1) { const auto p = circuit::pan(card); v->panL = (float) p.first; v->panR = (float) p.second; v->inc = src->sr / sr; }   // the card's place in the DMX's stereo mix
        else v->inc = src->sr / sr * circuit::rate(card, pl.mode, tune[(size_t) kit][(size_t) vi]) / circuit::rate(card, pl.mode, 50);  // clean: the trimmer moves the pitch
    }
    void startClick(bool down, int offset) { Voice* v = freeVoice(); *v = Voice(); v->s = &clickHi; v->inc = 1; v->gain = (float) circuit::metV(down) * gainOf(fader[kMet]); v->group = kVol; v->on = true; v->startAt = offset; }
    void renderVoice(Voice& v, int n) {
        float* outL = busL[(size_t) v.group].data(); float* outR = busR[(size_t) v.group].data(); const auto& d = v.s->d; const double len = (double) d.size() - 1;
        const float fadeStep = 1.f / (float) std::max(1.0, sr * .012);   // the choke: about 12 ms down (the app's 4 ms time constant)
        for (int i = std::max(0, v.startAt); i < n; ++i) {
            if (v.pos >= len) { v.on = false; break; }
            if (v.chokeAt >= 0 && i >= v.chokeAt) { v.fade -= fadeStep; if (v.fade <= 0) { v.on = false; break; } }
            const size_t a = (size_t) v.pos; const float f = (float) (v.pos - (double) a);
            const float x = (d[a] + (d[a + 1] - d[a]) * f) * v.gain * v.fade; outL[i] += x * v.panL; outR[i] += x * v.panR;
            v.pos += v.inc;
        }
        v.startAt = 0; if (v.chokeAt >= 0) v.chokeAt = 0;
    }
    // the metronome: the DMX's 1 ms pulse (level set by its fader and loud / quiet)
    void makeClicks(double rate) { clickHi.sr = rate; clickHi.d.assign((size_t) std::max(2.0, std::round(rate * .001)) + 1, 1.f); clickHi.d.back() = 0; }
};

}  // namespace dmx
