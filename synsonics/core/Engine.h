// Omega Synsonics: a circuit model of the Mattel Synsonics Drums (model 5281, 1981), with a pattern sequencer,
// a mixer and two effects.
//
// The voices follow the service manual schematic (Figure 4) and its "Technical Description" of drum sound generator
// U3 (Circuit.h). The part numbers in the comments (R33, C9, Q4...) are the schematic's own. U3 is a custom chip with
// no published internals: what it does is taken from the manual's waveform charts (test points 26 to 40), and those
// choices are marked "U3 model". Everything outside U3 is simulated node by node.
//
// The "bent" mode adds the Circuitbenders mods (circuitbenders.co.uk/newsarchive/synsonics.html).
//
// Beyond the hardware: 16 patterns (the hardware's three memories are patterns 1 to 3) with a song chain and swing,
// a sample pad, and a mixer. The mixer takes each voice where it enters the U4 summing amps, so with its default
// settings (the stock left/right placement and the 20% blend) it sounds exactly like the unit. Two send buses feed a
// REV7-style reverb and a tempo-synced delay (Fx.h).
//
// No dependencies; headers only, so the plugin, the Android app and the tests share it unchanged.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Circuit.h"
#include "Fx.h"

namespace syn {

// ---------------------------------------------------------------------------------------------------------- ids
enum Drum { Tom1 = 0, Snare = 1, Cymbal = 2, Tom2 = 3, Bass = 4, NumDrums = 5, Sample = 5, NumVoices = 6 };
// pattern step bits: one per voice, plus the accent (the cymbal plays as a closed hi-hat on that step)
constexpr int kAccentBit = 5, kSampleBit = 6;
constexpr uint8_t bitFor(int voice) { return voice == Sample ? uint8_t(1 << kSampleBit) : uint8_t(1 << voice); }

// mixer channel parameters: one block per voice
enum ChParam { ChLevel, ChPan, ChSendA, ChSendB, ChMute, ChSolo, NumChParams };

enum Param {
  pMode,        // 0 = stock, 1 = Circuitbenders mods
  pVolume,      // headphone level R49 (dual pot)
  pTune,        // tom tom 1 tuning R30
  pHatDecay,    // bent: replaces R32 (closed hat decay)
  pSnareDecay,  // bent: capacitance across C9
  pTom1Decay,   // bent
  pTom1Click,   // bent: C11/R39 impact path
  pTom1Drive,   // bent: level/overdrive (R38 made variable)
  pTom1Boing,   // bent: switch, long pitch sweep
  pTom2Pitch,   // bent: capacitance across C5
  pTom2Decay,   // bent
  pTom2Drive,   // bent: level/overdrive (R42 made variable)
  pHatClosed,   // bent: open/closed button (latches Q2 on)
  pSync,        // plugin: follow the host's tempo and bar position
  // added with the sequencer, mixer and effects (appended, so saved plugin states keep their meaning)
  pSwing,
  pSmpTune,
  pChan,                                   // first channel parameter: pChan + voice * NumChParams + ChParam
  pRetA = pChan + NumVoices * NumChParams,  // REV7 return
  pRetB,                                    // delay return
  pMaster,
  pRevProg, pRevTime, pRevHigh, pRevLow, pRevInit, pRevFirst, pRevDiff, pRevLive, pRevSize, pRevEqLo, pRevEqMid, pRevEqHi,
  pDlyDiv, pDlyFb, pDlyDamp, pDlyPing,
  NumParams
};
constexpr int chParam(int voice, int k) { return pChan + voice * NumChParams + k; }

struct ParamInfo { const char* id; const char* name; float def; bool toggle; };

// knob ranges (log knobs: value = lo * (hi/lo)^k); defaults sit where the stock part values are
inline float knobLog(float k, float lo, float hi) { return lo * std::pow(hi / lo, std::clamp(k, 0.f, 1.f)); }
inline float knobFor(float v, float lo, float hi) { return std::log(v / lo) / std::log(hi / lo); }

constexpr float kHatRlo = 220.f, kHatRhi = 68000.f, kHatRstock = 5600.f;          // R32 5.6k
constexpr float kSnCLo = 0.33e-6f, kSnCHi = 22e-6f, kSnCStock = 2.2e-6f;          // C9 2.2uF
constexpr float kT1DLo = 0.012f, kT1DHi = 1.6f, kT1DStock = 0.060f;               // tom 1 envelope (s)
constexpr float kT2PLo = 28.f, kT2PHi = 420.f, kT2PStock = 118.f;                 // tom 2 pitch (Hz)
constexpr float kT2DLo = 0.015f, kT2DHi = 2.0f, kT2DStock = 0.085f;               // tom 2 envelope (s)

// The stock blend matrix (R48/R50/R51/R52 into the 10k volume pot) as a pan law: each side keeps gS/gT and sends
// gX/gT to the other. A linear pan of k*(1-p)/2, k*(1+p)/2 gives exactly that at p = (gX-gS)/(gS+gX).
constexpr float kBlendS = 1.f / 1e3f, kBlendX = 1.f / 4.7e3f, kBlendT = kBlendS + kBlendX + 1.f / 10e3f;
constexpr float kPanK = (kBlendS + kBlendX) / kBlendT;
constexpr float kStockPan = (kBlendX - kBlendS) / (kBlendS + kBlendX);  // about -0.65: tom 1 and snare
inline float faderGain(float v) { float x = std::max(0.f, v) / 0.8f; return x * x * x; }  // 0.8 = 0 dB
inline float faderDb(float v) { float g = faderGain(v); return g <= 1e-6f ? -120.f : 20.f * std::log10(g); }

inline const char* voiceName(int v) {
  static const char* n[NumVoices] = {"TOM 1", "SNARE", "CYMBAL", "TOM 2", "BASS", "SAMPLE"};
  return n[std::clamp(v, 0, NumVoices - 1)];
}

inline const ParamInfo* paramInfo() {
  struct Table {
    std::deque<std::string> strings;
    std::array<ParamInfo, NumParams> p{};
    const char* keep(const std::string& s) { strings.push_back(s); return strings.back().c_str(); }
    void set(int i, const std::string& id, const std::string& name, float def, bool toggle = false) { p[size_t(i)] = {keep(id), keep(name), def, toggle}; }
    Table() {
      set(pMode, "mode", "Circuitbent", 0.f, true);
      set(pVolume, "volume", "Volume", 0.62f);
      set(pTune, "tune", "Tom 1 Tune", 0.5f);
      set(pHatDecay, "hatdecay", "Hat Decay", knobFor(kHatRstock, kHatRlo, kHatRhi));
      set(pSnareDecay, "snaredecay", "Snare Decay", knobFor(kSnCStock, kSnCLo, kSnCHi));
      set(pTom1Decay, "tom1decay", "Tom 1 Decay", knobFor(kT1DStock, kT1DLo, kT1DHi));
      set(pTom1Click, "tom1click", "Tom 1 Click", 0.25f);
      set(pTom1Drive, "tom1drive", "Tom 1 Drive", 0.f);
      set(pTom1Boing, "tom1boing", "Tom 1 Boing", 0.f, true);
      set(pTom2Pitch, "tom2pitch", "Tom 2 Pitch", knobFor(kT2PStock, kT2PLo, kT2PHi));
      set(pTom2Decay, "tom2decay", "Tom 2 Decay", knobFor(kT2DStock, kT2DLo, kT2DHi));
      set(pTom2Drive, "tom2drive", "Tom 2 Drive", 0.f);
      set(pHatClosed, "hatclosed", "Hat Closed", 0.f, true);
      set(pSync, "sync", "Host Sync", 1.f, true);
      set(pSwing, "swing", "Swing", 0.f);
      set(pSmpTune, "smptune", "Sample Tune", 0.5f);
      static const char* ids[NumVoices] = {"tom1", "snare", "cym", "tom2", "bass", "smp"};
      static const char* names[NumVoices] = {"Tom 1", "Snare", "Cymbal", "Tom 2", "Bass", "Sample"};
      for (int v = 0; v < NumVoices; ++v) {
        float pan = 0.5f;
        if (v == Tom1 || v == Snare) pan = (kStockPan + 1.f) / 2.f;
        if (v == Cymbal || v == Tom2) pan = (-kStockPan + 1.f) / 2.f;
        std::string id = ids[v], nm = names[v];
        set(chParam(v, ChLevel), id + "_level", nm + " Level", 0.8f);
        set(chParam(v, ChPan), id + "_pan", nm + " Pan", pan);
        set(chParam(v, ChSendA), id + "_reva", nm + " REV7 Send", 0.f);
        set(chParam(v, ChSendB), id + "_dlyb", nm + " Delay Send", 0.f);
        set(chParam(v, ChMute), id + "_mute", nm + " Mute", 0.f, true);
        set(chParam(v, ChSolo), id + "_solo", nm + " Solo", 0.f, true);
      }
      set(pRetA, "rev_return", "REV7 Return", 0.8f);
      set(pRetB, "dly_return", "Delay Return", 0.8f);
      set(pMaster, "master", "Master", 0.8f);
      const RevProgram& h = revPrograms()[1];
      set(pRevProg, "rev_prog", "REV7 Program", 1.f / float(kRevPrograms - 1));
      set(pRevTime, "rev_time", "REV7 Time", h.time);
      set(pRevHigh, "rev_high", "REV7 High", h.high);
      set(pRevLow, "rev_low", "REV7 Low", h.low);
      set(pRevInit, "rev_init", "REV7 Initial Delay", h.init);
      set(pRevFirst, "rev_first", "REV7 First Reflection", h.first);
      set(pRevDiff, "rev_diff", "REV7 Diffusion", h.diff);
      set(pRevLive, "rev_live", "REV7 Liveness", h.live);
      set(pRevSize, "rev_size", "REV7 Room Size", h.size);
      set(pRevEqLo, "rev_eqlo", "REV7 EQ Low", 0.5f);
      set(pRevEqMid, "rev_eqmid", "REV7 EQ Mid", 0.5f);
      set(pRevEqHi, "rev_eqhi", "REV7 EQ High", 0.5f);
      set(pDlyDiv, "dly_time", "Delay Time", 5.f / float(kDelayDivs - 1));
      set(pDlyFb, "dly_fb", "Delay Feedback", 0.35f);
      set(pDlyDamp, "dly_damp", "Delay Damp", 0.4f);
      set(pDlyPing, "dly_ping", "Delay Ping-Pong", 0.f, true);
    }
  };
  static const Table t;
  return t.p.data();
}
inline int revProgramIndex(float v) { return std::clamp(int(std::round(v * (kRevPrograms - 1))), 0, kRevPrograms - 1); }

// front panel buttons
enum Button {
  bStop, bRecord, bPlayback, bSlower, bFaster, bBass, bAccent,
  bRate0,  // + drum*3 + speed (speed 0 fast, 1 med, 2 slow), drums Tom1, Snare, Cymbal, Tom2
  NumButtons = bRate0 + 12
};
inline int rateButton(int drum, int speed) { return bRate0 + drum * 3 + speed; }

// MIDI notes (General MIDI drums; C4 plays the sample pad)
inline int drumForNote(int note, bool& closedHat) {
  closedHat = false;
  switch (note) {
    case 35: case 36: return Bass;
    case 37: case 38: case 39: case 40: return Snare;
    case 42: case 44: closedHat = true; return Cymbal;
    case 46: case 49: case 51: case 52: case 55: case 57: case 59: return Cymbal;
    case 41: case 43: case 45: return Tom2;
    case 47: case 48: case 50: return Tom1;
    case 60: return Sample;
    default: return -1;
  }
}
inline int noteForVoice(int v, bool accent) {
  static const int n[NumVoices] = {48, 38, 46, 43, 36, 60};
  return v == Cymbal && accent ? 42 : n[std::clamp(v, 0, NumVoices - 1)];
}

// ------------------------------------------------------------------------------------------------- the engine
struct HostTime { bool valid = false, playing = false; double bpm = 120, ppq = 0; };

// UI and other threads talk to the audio thread through this queue (single producer, single consumer)
struct Cmd { uint8_t type, a; };
enum CmdType : uint8_t { CHit = 1, CDown, CUp, CPlayPattern, CPlaySong, CStopSeq, CCue, CReset };
template <int N> struct SpscQueue {
  Cmd buf[N];
  std::atomic<uint32_t> w{0}, r{0};
  bool push(Cmd c) { uint32_t wi = w.load(std::memory_order_relaxed); if (wi - r.load(std::memory_order_acquire) >= N) return false; buf[wi % N] = c; w.store(wi + 1, std::memory_order_release); return true; }
  bool pop(Cmd& c) { uint32_t ri = r.load(std::memory_order_relaxed); if (ri == w.load(std::memory_order_acquire)) return false; c = buf[ri % N]; r.store(ri + 1, std::memory_order_release); return true; }
};

enum SeqMode { SeqIdle, SeqRecArm, SeqRecord, SeqPlayArm, SeqPlay };

// What the panel shows. Written by the audio thread, read by the UI.
struct Display {
  std::atomic<float> led[NumVoices];  // 0..1
  std::atomic<float> meter[NumVoices + 4];  // channels, REV7 return, delay return, master L, master R (peak, linear)
  std::atomic<int> seq{SeqIdle}, memory{0}, step{0}, length{0}, songPos{0}, cue{-1};
  std::atomic<int> rate[4];          // -1 off, else speed
  std::atomic<bool> bassOn{false}, overdub{false}, song{false};
  std::atomic<float> bpm{120};
  Display() { for (auto& l : led) l = 0; for (auto& m : meter) m = 0; for (auto& r : rate) r = -1; }
};

// an imported sample (the sample pad)
struct SampleData {
  std::vector<float> l, r;  // r empty for mono
  double rate = 44100;
  std::string name;
  size_t frames() const { return l.size(); }
};

class Engine {
 public:
  static constexpr int kStepsPerBeat = 4;   // 16th notes
  static constexpr int kMemSteps = 340;     // the 2114 RAM: 1024 nibbles, 340 steps per memory
  static constexpr int kNumMem = 3;         // memories on the front panel
  static constexpr int kNumPat = 16;        // patterns in the grid (1-3 are the memories)
  static constexpr int kSongMax = 64;

  std::atomic<float> params[NumParams];
  Display display;
  SpscQueue<512> queue;

  Engine() {
    for (int i = 0; i < NumParams; ++i) params[i] = paramInfo()[i].def;
    clearAllPatterns();
    setSampleRate(48000);
  }

  void setSampleRate(double fs) {
    sampleRate = fs;
    os = fs < 60000 ? 2 : 1;
    simRate = fs * os;
    dt = float(1.0 / simRate);
    noise.setRate(110000.0, simRate);
    cym.out.setTau(120e3f * 100e-12f, dt);
    snare.impact.setTau(120e3f * 0.01e-6f, dt);
    bass.hp.setTau(0.1e-6f * 110e3f, dt);
    bass.hp.s = bass.v;  // C15 starts charged to TP36's resting voltage: no thump at power-up or in exports
    for (auto& f : sumLP) f.setTau(20e3f * 470e-12f, dt);  // U4: R53/R47 20k with C24/C16 470pF
    for (auto& f : ampHP) f.setTau(0.1e-6f * 50e3f, float(1.0 / fs));  // C25/C17 into the LM386 input
    for (int c = 0; c < 2; ++c) {
      dec[c][0].lowpass(fs * 0.45, simRate, 0.5412);
      dec[c][1].lowpass(fs * 0.45, simRate, 1.3066);
    }
    rev.prepare(fs);
    dly.prepare(fs);
  }
  double getSampleRate() const { return sampleRate; }

  // ---- input (thread safe, from any thread)
  void hit(int voice) { queue.push({CHit, uint8_t(voice)}); }
  void buttonDown(int b) { queue.push({CDown, uint8_t(b)}); }
  void buttonUp(int b) { queue.push({CUp, uint8_t(b)}); }
  void playPattern(int p) { queue.push({CPlayPattern, uint8_t(p)}); }
  void playSong() { queue.push({CPlaySong, 0}); }
  void stopSequencer() { queue.push({CStopSeq, 0}); }
  void cuePattern(int p) { queue.push({CCue, uint8_t(p)}); }  // switch at the end of the playing pattern

  // ---- MIDI on the audio thread, between process() calls
  void midiNote(int note, bool on) {
    bool closed = false;
    int d = drumForNote(note, closed);
    if (d < 0) return;
    if (d == Cymbal) midiClosedHat = on ? closed : midiClosedHat;
    if (on) triggerLive(d, kPadPulse);
  }

  // ---- patterns and song: read and edited from the UI thread, played by the audio thread (atomics, relaxed)
  uint8_t step(int pat, int s) const { return (pat >= 0 && pat < kNumPat && s >= 0 && s < kMemSteps) ? pats[pat].steps[s].load(std::memory_order_relaxed) : 0; }
  void setStepBits(int pat, int s, uint8_t bits) { if (pat >= 0 && pat < kNumPat && s >= 0 && s < kMemSteps) pats[pat].steps[s].store(bits, std::memory_order_relaxed); }
  void setStepBit(int pat, int s, int bit, bool on) {
    if (pat < 0 || pat >= kNumPat || s < 0 || s >= kMemSteps) return;
    if (on) pats[pat].steps[s].fetch_or(uint8_t(1 << bit), std::memory_order_relaxed);
    else pats[pat].steps[s].fetch_and(uint8_t(~(1 << bit)), std::memory_order_relaxed);
  }
  int patternLength(int pat) const { return (pat >= 0 && pat < kNumPat) ? pats[pat].length.load(std::memory_order_relaxed) : 0; }
  void setPatternLength(int pat, int len) { if (pat >= 0 && pat < kNumPat) pats[pat].length.store(std::clamp(len, 0, kMemSteps), std::memory_order_relaxed); }
  void clearPattern(int pat) { if (pat < 0 || pat >= kNumPat) return; for (auto& s : pats[pat].steps) s.store(0, std::memory_order_relaxed); pats[pat].length = 0; }
  void clearAllPatterns() { for (int p = 0; p < kNumPat; ++p) clearPattern(p); songLen = 0; }
  int songLength() const { return songLen.load(std::memory_order_relaxed); }
  int songEntry(int i) const { return (i >= 0 && i < kSongMax) ? song[i].load(std::memory_order_relaxed) : 0; }
  void setSongEntry(int i, int pat) {
    if (i < 0 || i >= kSongMax) return;
    song[i].store(uint8_t(std::clamp(pat, 0, kNumPat - 1)), std::memory_order_relaxed);
    if (i >= songLen) songLen.store(i + 1, std::memory_order_relaxed);
  }
  void setSongLength(int n) { songLen.store(std::clamp(n, 0, kSongMax), std::memory_order_relaxed); }
  bool songMode() const { return songOn.load(std::memory_order_relaxed); }
  void setSongMode(bool on) { songOn.store(on, std::memory_order_relaxed); }
  float tempo() const { return bpmShared.load(std::memory_order_relaxed); }
  void setTempo(float b) { bpmRequest.store(std::clamp(b, kMinBpm, kMaxBpm), std::memory_order_relaxed); }

  // ---- the sample pad (UI thread). The audio thread drops the old one at its next block; it is freed later.
  void setSample(std::shared_ptr<SampleData> s) {
    std::lock_guard<std::mutex> lk(smpMutex);
    collectSamples();
    if (smpOwner) retired.push_back({smpOwner, blocksDone.load()});
    smpOwner = std::move(s);
    smpLive.store(smpOwner.get(), std::memory_order_release);
  }
  std::shared_ptr<SampleData> sample() const { std::lock_guard<std::mutex> lk(smpMutex); return smpOwner; }

  // ---- render. out: two channel pointers, n frames
  void process(float* const* out, int n, const HostTime& ht = {}) {
    Cmd c;
    while (queue.pop(c)) handle(c);
    float req = bpmRequest.exchange(-1.f, std::memory_order_relaxed);
    if (req > 0) bpm = req;
    updateParams();
    SampleData* live = smpLive.load(std::memory_order_acquire);
    if (live != smpCur) { smpCur = live; smpActive = false; }
    int done = 0;
    while (done < n) {
      int m = std::min(kChunk, n - done);
      renderChunk(out[0] + done, out[1] + done, m, ht, done);
      done += m;
    }
    blocksDone.fetch_add(1, std::memory_order_release);
    publish(n);
  }

  // ---- state: tempo, bass, rates, patterns, song, parameters (the sample is saved by the host)
  std::vector<uint8_t> saveState() const {
    std::vector<uint8_t> s;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(uint8_t(v >> (8 * i))); };
    auto putf = [&](float f) { uint32_t u; std::memcpy(&u, &f, 4); put32(u); };
    put32(0x53594E32);  // "SYN2"
    putf(bpmShared.load());
    // (read through the published copies, so this is safe from any thread)
    put32(display.bassOn.load()); put32(uint32_t(display.memory.load()));
    put32(kNumPat); put32(kMemSteps);
    for (auto& p : pats) { put32(uint32_t(p.length.load())); for (auto& st : p.steps) s.push_back(st.load()); }
    put32(kSongMax); put32(uint32_t(songLen.load())); for (auto& e : song) s.push_back(e.load());
    put32(songOn.load());
    for (int i = 0; i < 4; ++i) put32(uint32_t(display.rate[i].load() + 1));
    put32(NumParams);
    for (int i = 0; i < NumParams; ++i) putf(params[i]);
    return s;
  }
  bool loadState(const uint8_t* d, size_t n) {
    size_t p = 0;
    auto get32 = [&](uint32_t& v) { if (p + 4 > n) return false; v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(d[p++]) << (8 * i); return true; };
    auto getf = [&](float& f) { uint32_t u; if (!get32(u)) return false; std::memcpy(&f, &u, 4); return std::isfinite(f); };
    uint32_t v, magic;
    if (!get32(magic) || (magic != 0x53594E31 && magic != 0x53594E32)) return false;
    float b;
    if (!getf(b)) return false;
    float newBpm = std::clamp(b, kMinBpm, kMaxBpm);
    if (!get32(v)) return false;
    bool newBass = v != 0;
    if (!get32(v)) return false;
    int newMem = int(v % kNumPat);
    clearAllPatterns();
    if (magic == 0x53594E31) {  // version 1: three memories, then rates and parameters
      for (int m = 0; m < 3; ++m) {
        if (!get32(v) || p + 340 > n) return false;
        pats[m].length = int(std::min<uint32_t>(v, kMemSteps));
        for (int i = 0; i < 340; ++i) pats[m].steps[i] = uint8_t(d[p++] & 0x0F);
      }
    } else {
      uint32_t np, ns;
      if (!get32(np) || !get32(ns)) return false;
      for (uint32_t m = 0; m < np; ++m) {
        if (!get32(v) || p + ns > n) return false;
        if (m < uint32_t(kNumPat)) {
          pats[m].length = int(std::min<uint32_t>(v, kMemSteps));
          for (uint32_t i = 0; i < std::min<uint32_t>(ns, kMemSteps); ++i) pats[m].steps[i] = d[p + i];
        }
        p += ns;
      }
      uint32_t sm, sl;
      if (!get32(sm) || !get32(sl) || p + sm > n) return false;
      for (uint32_t i = 0; i < sm; ++i) if (i < uint32_t(kSongMax)) song[i] = uint8_t(d[p + i] % kNumPat);
      p += sm;
      songLen = int(std::min<uint32_t>(sl, kSongMax));
      if (!get32(v)) return false;
      songOn = v != 0;
    }
    for (int i = 0; i < 4; ++i) { if (!get32(v)) return false; rate[i] = int(v) - 1; if (rate[i] < -1 || rate[i] > 2) rate[i] = -1; }
    uint32_t count = NumParams;
    if (magic == 0x53594E32 && !get32(count)) return false;
    for (uint32_t i = 0; i < count; ++i) { float f; if (!getf(f)) break; if (i < uint32_t(NumParams)) params[i] = std::clamp(f, 0.f, 1.f); }
    bpm = newBpm; bpmShared = newBpm; bassOn = newBass; curMem = newMem;
    seq = SeqIdle;
    publish(0);
    return true;
  }

  // ---- offline: start the sequencer at once (no waiting for the beat), for rendering to a file
  void startForRender(bool wholeSong, int pattern) {
    for (auto& r : rate) r = -1;
    tickCount = -1; tickPhase = 1.0;  // the first 16th fires on the first sample
    overdub = false;
    if (wholeSong && songLen > 0) { songOn = true; songPos = 0; curMem = song[0]; }
    else { songOn = false; curMem = pattern; }
    seq = SeqPlay; playStep = -1; startPending = false;
  }
  void stopForRender() { seq = SeqIdle; for (auto& r : rate) r = -1; }

  // test hook: start the voice directly (no queue), with the pad pulse or the sequencer's longer pulse
  void strikeNow(int voice, bool fromSequencer = false) { triggerVoice(voice, fromSequencer ? kSeqPulse : kPadPulse); }

  static constexpr float kMinBpm = 40.f, kMaxBpm = 260.f;

 private:
  // waveform 20-23: the drum line is pulsed 3.2 ms from the pads and 12 ms from memory playback
  static constexpr float kPadPulse = 0.0032f, kSeqPulse = 0.012f, kBassPulse = 0.004f;
  static constexpr int kChunk = 256;
  // U4 volts to output units for the sends (what the output stage does at the default volume)
  static constexpr float kSendScale = 1.5f;

  struct Pattern {
    std::atomic<uint8_t> steps[kMemSteps];
    std::atomic<int> length{0};
  };

  double sampleRate = 48000, simRate = 96000;
  int os = 2;
  float dt = 1e-5f;

  // circuit
  MM5837 noise;
  NoiseVoice cym, snare;
  TomVoice tom1, tom2;
  BassVoice bass;
  OnePole sumLP[2], ampHP[2];
  Biquad dec[2][2];
  bool accentHeld = false, midiClosedHat = false, stepAccent = false, bentClosed = false;
  float rAcc = kHatRstock;

  // mixer (per block)
  float chL[NumVoices] = {}, chR[NumVoices] = {}, chA[NumVoices] = {}, chB[NumVoices] = {}, chG[NumVoices] = {};
  float peak[NumVoices + 4] = {};
  float meterHold[NumVoices + 4] = {};
  float sendAL[kChunk], sendAR[kChunk], sendBL[kChunk], sendBR[kChunk], fxAL[kChunk], fxAR[kChunk], fxBL[kChunk], fxBR[kChunk];
  float dryL[kChunk], dryR[kChunk];
  Rev7 rev;
  SyncDelay dly;

  // sample pad
  mutable std::mutex smpMutex;
  std::shared_ptr<SampleData> smpOwner;
  struct Retired { std::shared_ptr<SampleData> s; uint64_t stamp; };
  std::vector<Retired> retired;
  std::atomic<SampleData*> smpLive{nullptr};
  std::atomic<uint64_t> blocksDone{0};
  SampleData* smpCur = nullptr;
  double smpPos = 0;
  bool smpActive = false;
  void collectSamples() {
    uint64_t now = blocksDone.load();
    retired.erase(std::remove_if(retired.begin(), retired.end(), [&](const Retired& r) { return now >= r.stamp + 2; }), retired.end());
  }

  // U1 (the Sanyo LM6402 microcontroller): clock, rate buttons, memories; plus the grid's patterns and song
  float bpm = 120;
  std::atomic<float> bpmShared{120}, bpmRequest{-1};
  double tickPhase = 0;
  long tickCount = 0;
  bool hostRunning = false;
  int rate[4] = {-1, -1, -1, -1};
  bool bassOn = false;
  bool slowerHeld = false, fasterHeld = false;
  int seq = SeqIdle, curMem = 0, recStep = 0, playStep = 0, songPos = 0, cue = -1;
  bool overdub = false, startPending = false;
  Pattern pats[kNumPat];
  std::atomic<uint8_t> song[kSongMax] = {};
  std::atomic<int> songLen{0};
  std::atomic<bool> songOn{false};
  float ledT[NumVoices] = {};

  bool bent() const { return params[pMode] > 0.5f; }

  static float volumeTaper(float v) { return v <= 0 ? 0.f : std::pow(v, 2.5f); }

  void updateParams() {
    bool b = bent();
    cym.C = 10e-6f; cym.Rd = 8200;
    bentClosed = b && params[pHatClosed] > 0.5f;
    rAcc = b ? knobLog(params[pHatDecay], kHatRlo, kHatRhi) : kHatRstock;
    snare.C = b ? knobLog(params[pSnareDecay], kSnCLo, kSnCHi) : kSnCStock;
    snare.Rd = 12000;
    // tom 1: tune pot R30 (with R29 1.2k and R28 10k to -9 V) sets the VCO current
    float t = params[pTune];
    tom1.f0 = 140.f * std::pow(3.2f, t);
    tom1.sweepDepth = 0.6f;
    tom1.tauEnv = b ? knobLog(params[pTom1Decay], kT1DLo, kT1DHi) : kT1DStock;
    tom1.tauSweep = (b && params[pTom1Boing] > 0.5f) ? 0.45f : 0.07f;
    tom1.clickGain = b ? params[pTom1Click] * 4.f : 1.f;
    tom1.rOut = b ? 220e3f / (1.f + 60.f * params[pTom1Drive] * params[pTom1Drive]) : 220e3f;
    tom1.divider = 120.f / 130.f;
    tom2.f0 = b ? knobLog(params[pTom2Pitch], kT2PLo, kT2PHi) : kT2PStock;
    tom2.sweepDepth = 0.22f;
    tom2.tauSweep = 0.05f;
    tom2.tauEnv = b ? knobLog(params[pTom2Decay], kT2DLo, kT2DHi) : kT2DStock;
    tom2.clickGain = 1.f;
    tom2.rOut = b ? 220e3f / (1.f + 60.f * params[pTom2Drive] * params[pTom2Drive]) : 220e3f;
    tom2.divider = 130.f / 140.f;
    // mixer: level, mute/solo, pan (the stock blend law), post-fader sends
    bool anySolo = false;
    for (int v = 0; v < NumVoices; ++v) anySolo |= params[chParam(v, ChSolo)] > 0.5f;
    for (int v = 0; v < NumVoices; ++v) {
      bool audible = params[chParam(v, ChMute)] < 0.5f && (!anySolo || params[chParam(v, ChSolo)] > 0.5f);
      float g = audible ? faderGain(params[chParam(v, ChLevel)]) : 0.f;
      float p = params[chParam(v, ChPan)] * 2.f - 1.f;
      if (v == Sample) { chL[v] = g * std::min(1.f, 1.f - p); chR[v] = g * std::min(1.f, 1.f + p); }
      else { chL[v] = g * kPanK * (1.f - p) * 0.5f; chR[v] = g * kPanK * (1.f + p) * 0.5f; }
      chG[v] = g;
      chA[v] = faderGain(params[chParam(v, ChSendA)]);
      chB[v] = faderGain(params[chParam(v, ChSendB)]);
    }
  }

  // one oversampled step of the circuit: each voice's voltage where it enters U4 (bass counted for both summers)
  void tickVoices(float* v) {
    float vg = noise.step();
    cym.Rpar = (accentHeld || midiClosedHat || stepAccent || bentClosed) ? rAcc : 0.f;
    v[Cymbal] = -20e3f * cym.step(dt, vg, true);
    v[Snare] = -20e3f * snare.step(dt, vg, false);
    v[Tom1] = -20e3f * (tom1.stepTone(dt) + tom1.stepClick(dt));
    v[Tom2] = -20e3f * (tom2.stepTone(dt) + tom2.stepClick(dt));
    v[Bass] = -20e3f * 2.f * bass.step(dt);
  }

  void renderChunk(float* outL, float* outR, int n, const HostTime& ht, int offset) {
    float vol = volumeTaper(params[pVolume].load());
    float ratio = smpCur ? float(smpCur->rate / sampleRate * std::pow(2.0, (params[pSmpTune] - 0.5) * 24.0 / 12.0)) : 1.f;
    for (int i = 0; i < n; ++i) {
      clock(ht, offset + i);
      float l = 0, r = 0, aL = 0, aR = 0, bL = 0, bR = 0;
      for (int k = 0; k < os; ++k) {
        float v[NumDrums];
        tickVoices(v);
        float busL = 0, busR = 0;
        for (int c = 0; c < NumDrums; ++c) {
          float yl = v[c] * chL[c], yr = v[c] * chR[c];
          busL += yl; busR += yr;
          aL += yl * chA[c]; aR += yr * chA[c];
          bL += yl * chB[c]; bR += yr * chB[c];
          float pk = std::fabs(v[c]) * chG[c] * kSendScale;
          if (pk > peak[c]) peak[c] = pk;
        }
        // U4A/B with their 470 pF, clipping at the +/-9 V converter's rails
        busL = railClip(sumLP[0].lp(busL), 7.5f);
        busR = railClip(sumLP[1].lp(busR), 7.5f);
        l = dec[0][1].run(dec[0][0].run(busL));
        r = dec[1][1].run(dec[1][0].run(busR));
      }
      float inv = kSendScale / float(os);
      aL *= inv; aR *= inv; bL *= inv; bR *= inv;
      // volume pot, C25/C17 coupling, LM386 (gain 20, 9 V supply), line-out pad
      l = ampHP[0].hp(l * vol);
      r = ampHP[1].hp(r * vol);
      l = railClip(l * 20.f, 3.7f) * 0.25f;
      r = railClip(r * 20.f, 3.7f) * 0.25f;
      // the sample pad joins at line level
      if (smpActive && smpCur) {
        size_t nf = smpCur->frames();
        size_t i0 = size_t(smpPos);
        if (i0 + 1 >= nf) smpActive = false;
        else {
          float fr = float(smpPos - double(i0));
          float sl = smpCur->l[i0] + (smpCur->l[i0 + 1] - smpCur->l[i0]) * fr;
          float sr = smpCur->r.empty() ? sl : smpCur->r[i0] + (smpCur->r[i0 + 1] - smpCur->r[i0]) * fr;
          smpPos += ratio;
          float yl = sl * chL[Sample], yr = sr * chR[Sample];
          l += yl; r += yr;
          aL += yl * chA[Sample]; aR += yr * chA[Sample];
          bL += yl * chB[Sample]; bR += yr * chB[Sample];
          float pk = std::max(std::fabs(sl), std::fabs(sr)) * chG[Sample];
          if (pk > peak[Sample]) peak[Sample] = pk;
        }
      }
      dryL[i] = l; dryR[i] = r;
      sendAL[i] = aL; sendAR[i] = aR; sendBL[i] = bL; sendBR[i] = bR;
    }
    // effects
    const RevProgram& prog = revPrograms()[revProgramIndex(params[pRevProg])];
    Rev7::Settings rs;
    rs.algo = prog.algo;
    rs.time = params[pRevTime]; rs.high = params[pRevHigh]; rs.low = params[pRevLow]; rs.init = params[pRevInit];
    rs.first = params[pRevFirst]; rs.diff = params[pRevDiff]; rs.live = params[pRevLive]; rs.size = params[pRevSize];
    rs.eqLo = params[pRevEqLo]; rs.eqMid = params[pRevEqMid]; rs.eqHi = params[pRevEqHi];
    rev.process(sendAL, sendAR, fxAL, fxAR, n, rs, bpm);
    dly.process(sendBL, sendBR, fxBL, fxBR, n, delayDiv(params[pDlyDiv]), params[pDlyFb], params[pDlyDamp], params[pDlyPing] > 0.5f, bpm);
    float ra = faderGain(params[pRetA]), rb = faderGain(params[pRetB]), master = faderGain(params[pMaster]);
    for (int i = 0; i < n; ++i) {
      float al = fxAL[i] * ra, ar = fxAR[i] * ra, bl = fxBL[i] * rb, br = fxBR[i] * rb;
      peak[NumVoices] = std::max(peak[NumVoices], std::max(std::fabs(al), std::fabs(ar)));
      peak[NumVoices + 1] = std::max(peak[NumVoices + 1], std::max(std::fabs(bl), std::fabs(br)));
      float l = (dryL[i] + al + bl) * master, r = (dryR[i] + ar + br) * master;
      peak[NumVoices + 2] = std::max(peak[NumVoices + 2], std::fabs(l));
      peak[NumVoices + 3] = std::max(peak[NumVoices + 3], std::fabs(r));
      outL[i] = l; outR[i] = r;
    }
  }

  void triggerVoice(int d, float pulse) {
    if (d < 0 || d >= NumVoices) return;
    int g = std::max(1, int(pulse * simRate));
    switch (d) {
      case Tom1: tom1.gate = g; break;
      case Tom2: tom2.gate = g; break;
      case Snare: snare.gate = g; break;
      case Cymbal: cym.gate = g; break;
      case Bass: bass.gate = std::max(1, int(kBassPulse * simRate)); break;
      case Sample: if (smpCur && smpCur->frames() > 1) { smpPos = 0; smpActive = true; } break;
    }
    ledT[d] = 1.f;
  }

  // a strike from the pads, the MIDI trigger inputs, or the rate buttons: sounds now, and is recorded
  void triggerLive(int d, float pulse) {
    triggerVoice(d, pulse);
    if (seq == SeqRecord || (seq == SeqPlay && overdub)) {
      uint8_t bits = bitFor(d);
      if (d == Cymbal && (accentHeld || midiClosedHat)) bits |= uint8_t(1 << kAccentBit);
      // quantize to the nearest 16th
      int s = seq == SeqRecord ? recStep : playStep;
      bool late = tickPhase > 0.5;
      Pattern& m = pats[curMem];
      if (seq == SeqRecord) {
        int at = late ? s + 1 : s;
        if (at >= 0 && at < kMemSteps) m.steps[at].fetch_or(bits, std::memory_order_relaxed);
      } else if (m.length > 0) {
        int len = m.length;
        int at = ((late ? s + 1 : s) % len + len) % len;
        m.steps[at].fetch_or(bits, std::memory_order_relaxed);
      }
    }
  }

  void handle(const Cmd& c) {
    switch (c.type) {
      case CHit: if (c.a < NumVoices) triggerLive(c.a, kPadPulse); return;
      case CPlayPattern: songOn = false; startPlay(c.a % kNumPat); return;
      case CPlaySong:
        if (songLen <= 0) return;
        songOn = true; songPos = 0; startPlay(song[0]); return;
      case CStopSeq:
        if (seq == SeqRecord) finishRecording();
        seq = SeqIdle; overdub = false; cue = -1; return;
      case CCue:
        if (seq == SeqPlay) cue = c.a % kNumPat;
        else curMem = c.a % kNumPat;
        return;
      default: break;
    }
    int b = c.a;
    bool down = c.type == CDown;
    if (b == bAccent) { accentHeld = down; return; }
    if (b == bSlower) { slowerHeld = down; if (down) nudge(-1); return; }
    if (b == bFaster) { fasterHeld = down; if (down) nudge(+1); return; }
    if (!down) return;
    if (b == bBass) { bassOn = !bassOn; return; }
    if (b == bStop) {
      if (seq == SeqRecord) finishRecording();
      seq = SeqIdle; overdub = false; cue = -1;
      for (auto& r : rate) r = -1;
      return;
    }
    if (b == bRecord) {
      if (seq == SeqPlay) { overdub = !overdub; return; }
      if (seq == SeqRecord) return;
      seq = SeqRecArm; return;
    }
    if (b == bPlayback) {
      if (seq == SeqRecord) { finishRecording(); startPlay(curMem); return; }
      seq = SeqPlayArm; return;
    }
    if (b >= bRate0 && b < bRate0 + 12) {
      int drum = (b - bRate0) / 3, speed = (b - bRate0) % 3;
      if (drum == Cymbal && (seq == SeqRecArm || seq == SeqPlayArm)) {  // CYMBAL / MEMORY: pick memory 1-3
        songOn = false;
        if (seq == SeqRecArm) { curMem = speed; clearPattern(curMem); recStep = 0; startPending = true; seq = SeqRecord; }
        else startPlay(speed);
        return;
      }
      rate[drum] = rate[drum] == speed ? -1 : speed;
    }
  }

  void startPlay(int m) {
    curMem = m;
    overdub = false;
    cue = -1;
    if (pats[m].length <= 0 && !songOn) { seq = SeqIdle; return; }
    seq = SeqPlay; playStep = 0; startPending = true;
  }

  void finishRecording() {
    Pattern& m = pats[curMem];
    int len = std::max(recStep, kStepsPerBeat);
    len = (len + kStepsPerBeat - 1) / kStepsPerBeat * kStepsPerBeat;  // whole beats
    m.length = std::min(len, kMemSteps);
  }

  void nudge(int dir) { bpm = std::clamp(bpm + float(dir), kMinBpm, kMaxBpm); }

  float swingAmount() const { return params[pSwing] * 0.4f; }  // up to 40%: odd 16ths late, past triplet feel

  // runs once per output sample: the tempo clock, and everything U1 does on each 16th note
  void clock(const HostTime& ht, int i) {
    if (slowerHeld || fasterHeld) {
      float rateBpmPerSec = 18.f;
      bpm = std::clamp(bpm + (fasterHeld ? 1 : -1) * rateBpmPerSec / float(sampleRate), kMinBpm, kMaxBpm);
    }
    float sw = swingAmount();
    bool synced = params[pSync] > 0.5f && ht.valid && ht.playing && ht.bpm > 1;
    if (synced) {
      bpm = float(std::clamp(ht.bpm, double(kMinBpm), 400.0));
      double ticksPerSample = ht.bpm / 60.0 * kStepsPerBeat / sampleRate;
      double pos = ht.ppq * kStepsPerBeat + ticksPerSample * i;
      // swing: within each pair of 16ths the second one starts late
      double pair = std::floor(pos / 2.0) * 2.0, inPair = pos - pair;
      double sp = inPair < 1.0 + sw ? inPair / (1.0 + sw) : 1.0 + (inPair - 1.0 - sw) / (1.0 - sw);
      double spos = pair + sp;
      long want = long(std::floor(spos));
      double frac = spos - std::floor(spos);
      if (!hostRunning) { hostRunning = true; tickCount = want; tickPhase = frac; if (frac < ticksPerSample * 1.5) onTick(); return; }
      tickPhase = frac;
      if (want != tickCount) { tickCount = want; onTick(); }
      return;
    }
    hostRunning = false;
    double ticksPerSample = bpm / 60.0 * kStepsPerBeat / sampleRate;
    // even 16th to odd one is longer by the swing, odd to even shorter
    bool evenNow = ((tickCount % 2) + 2) % 2 == 0;
    ticksPerSample /= evenNow ? (1.0 + sw) : (1.0 - sw);
    tickPhase += ticksPerSample;
    if (tickPhase >= 1.0) { tickPhase -= 1.0; ++tickCount; onTick(); }
  }

  void onTick() {
    bool beat = (((tickCount % kStepsPerBeat) + kStepsPerBeat) % kStepsPerBeat) == 0;
    stepAccent = false;
    bool playNow = false;
    if (startPending) {
      if (beat) {
        startPending = false;
        if (seq == SeqPlay) { playStep = 0; playNow = true; }
        if (seq == SeqRecord) recStep = 0;
      }
    } else if (seq == SeqRecord) {
      ++recStep;
      if (recStep >= kMemSteps) { finishRecording(); startPlay(curMem); }
    } else if (seq == SeqPlay) {
      int len = pats[curMem].length;
      ++playStep;
      if (playStep >= len) {
        // end of the pattern: next song entry, or the cued pattern, or loop
        playStep = 0;
        if (songOn && songLen > 0) {
          int tries = 0;
          do { songPos = (songPos + 1) % songLen; curMem = song[songPos]; } while (pats[curMem].length <= 0 && ++tries < kSongMax);
        } else if (cue >= 0) { curMem = cue; cue = -1; }
      }
      playNow = pats[curMem].length > 0;
    }
    if (seq == SeqPlay && playNow) {
      uint8_t bits = pats[curMem].steps[playStep % kMemSteps].load(std::memory_order_relaxed);
      stepAccent = (bits >> kAccentBit) & 1;
      for (int d = 0; d < NumDrums; ++d) if (bits & (1 << d)) triggerVoice(d, kSeqPulse);
      if (bits & (1 << kSampleBit)) triggerVoice(Sample, kSeqPulse);
    }
    if (beat) {
      ledT[Bass] = std::max(ledT[Bass], 0.6f);  // the bass LED blinks the tempo
      if (bassOn) triggerVoice(Bass, kBassPulse);
    }
    static const int every[3] = {1, 2, 4};  // fast, medium, slow: 16ths, 8ths, quarters
    for (int d = 0; d < 4; ++d) {
      if (rate[d] < 0) continue;
      if (d == Cymbal && (seq == SeqRecArm || seq == SeqPlayArm)) continue;
      if (((tickCount % every[rate[d]]) + every[rate[d]]) % every[rate[d]] == 0) triggerLive(d, kSeqPulse);
    }
  }

  void publish(int n) {
    float decay = std::exp(-float(n) / float(sampleRate) / 0.06f);
    float mdecay = std::exp(-float(n) / float(sampleRate) / 0.25f);
    for (int d = 0; d < NumVoices; ++d) { display.led[d] = ledT[d]; ledT[d] *= decay; }
    for (int i = 0; i < NumVoices + 4; ++i) {
      meterHold[i] = std::max(peak[i], meterHold[i] * mdecay);
      display.meter[i] = meterHold[i];
      peak[i] = 0;
    }
    display.seq = seq;
    display.memory = curMem;
    display.step = seq == SeqRecord ? recStep : playStep;
    display.length = pats[curMem].length.load();
    for (int d = 0; d < 4; ++d) display.rate[d] = rate[d];
    display.bassOn = bassOn;
    display.overdub = overdub;
    display.song = songOn.load();
    display.songPos = songPos;
    display.cue = cue;
    display.bpm = bpm;
    bpmShared = bpm;
  }
};

}  // namespace syn
