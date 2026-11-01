// Omega Synsonics: a circuit model of the Mattel Synsonics Drums (model 5281, 1981).
//
// Everything here follows the service manual schematic (Figure 4) and its "Technical Description" of drum sound
// generator U3. The part numbers in the comments (R33, C9, Q4...) are the schematic's own, so the code can be read
// against it. U3 is a custom chip with no published internals: what it does is taken from the manual's waveform
// charts (test points 26 to 40), and those choices are marked "U3 model" below. Everything outside U3 (the JFET noise
// gates, the impact networks, the emitter followers, the bass discharge, the summing amps, the blend matrix and the
// LM386 output stages) is simulated node by node.
//
// The "bent" mode adds the Circuitbenders mods (circuitbenders.co.uk/newsarchive/synsonics.html): hi-hat decay knob,
// hi-hat open/closed button, snare decay, tom 1 decay, tom 1 click amount, tom 1 level/overdrive, tom 1 boing decay
// switch, tom 2 pitch, tom 2 level/overdrive, tom 2 decay, and a trigger input per sound (MIDI notes here).
//
// No dependencies; one header so the plugin, the Android app and the tests share it unchanged.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace syn {

// ---------------------------------------------------------------------------------------------------------- ids
enum Drum { Tom1 = 0, Snare = 1, Cymbal = 2, Tom2 = 3, Bass = 4, NumDrums = 5 };

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
  NumParams
};

struct ParamInfo { const char* id; const char* name; float def; bool toggle; };

// knob ranges (log knobs: value = lo * (hi/lo)^k); defaults sit where the stock part values are
inline float knobLog(float k, float lo, float hi) { return lo * std::pow(hi / lo, std::clamp(k, 0.f, 1.f)); }
inline float knobFor(float v, float lo, float hi) { return std::log(v / lo) / std::log(hi / lo); }

constexpr float kHatRlo = 220.f, kHatRhi = 68000.f, kHatRstock = 5600.f;          // R32 5.6k
constexpr float kSnCLo = 0.33e-6f, kSnCHi = 22e-6f, kSnCStock = 2.2e-6f;          // C9 2.2uF
constexpr float kT1DLo = 0.012f, kT1DHi = 1.6f, kT1DStock = 0.060f;               // tom 1 envelope (s)
constexpr float kT2PLo = 28.f, kT2PHi = 420.f, kT2PStock = 118.f;                 // tom 2 pitch (Hz)
constexpr float kT2DLo = 0.015f, kT2DHi = 2.0f, kT2DStock = 0.085f;               // tom 2 envelope (s)

inline const ParamInfo* paramInfo() {
  static const ParamInfo p[NumParams] = {
      {"mode", "Circuitbent", 0.f, true},
      {"volume", "Volume", 0.62f, false},
      {"tune", "Tom 1 Tune", 0.5f, false},
      {"hatdecay", "Hat Decay", knobFor(kHatRstock, kHatRlo, kHatRhi), false},
      {"snaredecay", "Snare Decay", knobFor(kSnCStock, kSnCLo, kSnCHi), false},
      {"tom1decay", "Tom 1 Decay", knobFor(kT1DStock, kT1DLo, kT1DHi), false},
      {"tom1click", "Tom 1 Click", 0.25f, false},
      {"tom1drive", "Tom 1 Drive", 0.f, false},
      {"tom1boing", "Tom 1 Boing", 0.f, true},
      {"tom2pitch", "Tom 2 Pitch", knobFor(kT2PStock, kT2PLo, kT2PHi), false},
      {"tom2decay", "Tom 2 Decay", knobFor(kT2DStock, kT2DLo, kT2DHi), false},
      {"tom2drive", "Tom 2 Drive", 0.f, false},
      {"hatclosed", "Hat Closed", 0.f, true},
      {"sync", "Host Sync", 1.f, true},
  };
  return p;
}

// front panel buttons
enum Button {
  bStop, bRecord, bPlayback, bSlower, bFaster, bBass, bAccent,
  bRate0,  // + drum*3 + speed (speed 0 fast, 1 med, 2 slow), drums Tom1, Snare, Cymbal, Tom2
  NumButtons = bRate0 + 12
};
inline int rateButton(int drum, int speed) { return bRate0 + drum * 3 + speed; }

// MIDI trigger inputs (General MIDI drum notes)
inline int drumForNote(int note, bool& closedHat) {
  closedHat = false;
  switch (note) {
    case 35: case 36: return Bass;
    case 37: case 38: case 39: case 40: return Snare;
    case 42: case 44: closedHat = true; return Cymbal;
    case 46: case 49: case 51: case 52: case 55: case 57: case 59: return Cymbal;
    case 41: case 43: case 45: return Tom2;
    case 47: case 48: case 50: return Tom1;
    default: return -1;
  }
}

// ----------------------------------------------------------------------------------------------- circuit pieces
// one-pole filters in topology-preserving form (stable for any time constant, even below the step)
struct OnePole {
  float g = 0, s = 0;
  void setTau(float tau, float dt) { float wc = 1.f / tau; float w = 2.f / dt * std::tan(wc * dt / 2.f); g = w * dt / 2.f; g = g / (1.f + g); }
  float lp(float x) { float v = (x - s) * g; float y = v + s; s = y + v; return y; }
  float hp(float x) { return x - lp(x); }
};

// MM5837 (U7): 17-stage shift register, feedback from stages 14 and 17, free-running clock.
struct MM5837 {
  uint32_t reg = 0x1ACE1u;
  double phase = 0, inc = 0;
  float out = 0;
  void setRate(double clockHz, double simRate) { inc = clockHz / simRate; }
  float step() {  // gate voltage at TP29: about -0.5 V high, -5.5 V low (waveform 29: 5 V p-p)
    phase += inc;
    while (phase >= 1.0) {
      phase -= 1.0;
      uint32_t fb = ((reg >> 13) ^ (reg >> 16)) & 1u;
      reg = ((reg << 1) | fb) & 0x1FFFFu;
    }
    float target = (reg & 1u) ? -0.5f : -5.5f;
    out += (target - out) * 0.6f;  // output slew of the PMOS stage
    return out;
  }
};

// 2N5638 used as a voltage-controlled resistor (Q3, Q4): drain to the envelope through R33/R35, source to ground,
// gate on the noise. Below pinch-off it is open; above, r_ds = R_on / (1 - Vgs/Vp).
inline float jfetRds(float vgs) {
  const float Vp = -3.0f, Ron = 30.f;
  if (vgs <= Vp) return 1e9f;
  return Ron / std::max(1e-4f, 1.f - vgs / Vp);
}

// smooth op-amp/LM386 style limiter
inline float railClip(float v, float rail) {
  float x = v / rail;
  return rail * x / std::pow(1.f + std::pow(std::fabs(x), 3.f), 1.f / 3.f);
}

// U3's envelope output pins: during the trigger pulse the pin sources current toward ~3 V (waveforms 26, 28, 33, 35:
// "3V MAX"); afterwards the pin floats and the external network discharges the cap. U3 model: 270 ohm source.
constexpr float kEnvPeak = 3.0f, kPinR = 270.f, kPinInternalR = 15000.f;

// Noise voices: cymbal (pin 7, C6 10uF, R33 8.2k, Q3, C7 100pF + R34 120k) and snare (pin 18, C9 2.2uF, R35 12k,
// Q4, R36 390k, impact C10 .01uF + R37 120k).
struct NoiseVoice {
  float V = 0;       // envelope node (TP26 / TP28)
  float C = 10e-6f;  // envelope cap
  float Rd = 8200;   // drain resistor
  float Rpar = 0;    // extra discharge (Q2 + R32 for the closed hat), 0 = none
  int gate = 0;      // samples left in the trigger pulse
  OnePole out, impact;
  // returns the current (A) it pushes into its summing node
  float step(float dt, float vg, bool cymbal) {
    float iSrc = gate > 0 ? (kEnvPeak - V) / kPinR : 0.f;
    if (gate > 0) --gate;
    float rds = jfetRds(vg);
    float iD = V / (Rd + rds);
    float vDrain = V * rds / (Rd + rds);
    float iLeak = V / kPinInternalR + (Rpar > 0 ? V / Rpar : 0.f);
    V += dt / C * (iSrc - iD - iLeak);
    if (V < 0) V = 0;
    if (cymbal) return out.hp(vDrain) / 120e3f;                 // C7 into R34 (13 kHz high-pass)
    return vDrain / 390e3f + impact.hp(V) / 120e3f;              // R36, plus C10/R37 impact
  }
};

// Tom voices. U3 holds a VCO (C4 .1uF for tom 1, C5 .68uF for tom 2) and a VCA keyed by the envelope pin (5 / 14).
// U3 model: triangle VCO; VCA opens above ~1 V of envelope (the burst at TP32/TP34 dies well before the envelope at
// TP33/TP35 does); a pitch sweep that rises during the strike and falls back ("sweep-swoop", Richard C. on tom 1).
// The envelope pin also drives an emitter follower (Q7 / Q8) whose output is differentiated by C11/R39 (C13/R43):
// that is the "impact" click. The tone leaves through R38 (R42) 220k.
struct TomVoice {
  float env = 0, sweep = 0, phase = 0;
  float ve = 0, vc = 0;   // Q7/Q8 emitter (C12 / C14, 1uF) and the charge on C11 / C13
  int gate = 0;
  float f0 = 250, sweepDepth = 0.6f, tauEnv = 0.06f, tauSweep = 0.07f;
  float rOut = 220e3f, clickGain = 1.f;
  float divider = 120.f / 130.f;  // R73/(R72+R73), R44/(R86+R44)
  float vcaOut = 0;
  float stepTone(float dt) {
    if (gate > 0) {
      --gate;
      env += (kEnvPeak - env) * (1.f - std::exp(-dt / 0.0008f));
      sweep += (1.f - sweep) * (1.f - std::exp(-dt / 0.003f));
    } else {
      env *= std::exp(-dt / tauEnv);
      sweep *= std::exp(-dt / tauSweep);
    }
    float f = f0 * (1.f + sweepDepth * sweep);
    phase += f * dt;
    phase -= std::floor(phase);
    float tri = phase < 0.5f ? 4.f * phase - 1.f : 3.f - 4.f * phase;
    tri = 1.5f * tri - 0.5f * tri * tri * tri;  // the chip's rounded triangle
    float vca = std::clamp((env - 1.0f) / 2.0f, 0.f, 1.f);
    vcaOut = 2.0f * tri * vca;  // 2 V peak at the pin (waveform 32 / 34)
    return vcaOut / rOut;
  }
  float stepClick(float dt) {
    // emitter follower: charges C12 fast through the transistor, discharges only through C11 + R39
    float vb = env * divider;
    float target = vb - 0.65f;
    if (target > ve) ve += (target - ve) * std::min(1.f, dt / (50.f * 1e-6f));
    float i = (ve - vc) / 220e3f;  // current through R39 into the virtual ground
    vc += dt / 0.01e-6f * i;
    ve -= dt / 1e-6f * i;
    return i * clickGain;
  }
};

// Bass: U1 pulls TP36 low through R26 1k and D24 for a short pulse; R27 120k brings it back up to +5 V with C30 .1uF;
// C15 .1uF couples the step into R45 and R46 (220k each, to both summing amps). That is the "click" bass drum.
struct BassVoice {
  float v = 5.0f;
  int gate = 0;
  OnePole hp;  // C15 into the two 220k resistors in parallel
  float step(float dt) {
    if (gate > 0) {
      --gate;
      // U1's port can't sink all of it: R26 against R27 and the port's own resistance leave TP36 near 2.7 V
      // (scaled to chart 37, where the bass is about as loud as the snare at U4)
      float vlow = 2.7f;
      if (v > vlow) v += (vlow - v) * (1.f - std::exp(-dt / (1000.f * 0.1e-6f)));
    } else {
      v += (5.0f - v) * (1.f - std::exp(-dt / (120e3f * 0.1e-6f)));
    }
    return hp.hp(v) / 220e3f;  // current into each channel
  }
};

// biquad for the 2x oversampling decimator
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void lowpass(double fc, double fs, double q) {
    double w = 2 * M_PI * fc / fs, c = std::cos(w), s = std::sin(w), al = s / (2 * q), a0 = 1 + al;
    b0 = float((1 - c) / 2 / a0); b1 = float((1 - c) / a0); b2 = b0;
    a1 = float(-2 * c / a0); a2 = float((1 - al) / a0);
  }
  float run(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// ------------------------------------------------------------------------------------------------- the engine
struct HostTime { bool valid = false, playing = false; double bpm = 120, ppq = 0; };

// UI and other threads talk to the audio thread through this queue (single producer, single consumer)
struct Cmd { uint8_t type, a; };  // type: 1 pad hit (a = drum), 2 button down, 3 button up
template <int N> struct SpscQueue {
  Cmd buf[N];
  std::atomic<uint32_t> w{0}, r{0};
  bool push(Cmd c) { uint32_t wi = w.load(std::memory_order_relaxed); if (wi - r.load(std::memory_order_acquire) >= N) return false; buf[wi % N] = c; w.store(wi + 1, std::memory_order_release); return true; }
  bool pop(Cmd& c) { uint32_t ri = r.load(std::memory_order_relaxed); if (ri == w.load(std::memory_order_acquire)) return false; c = buf[ri % N]; r.store(ri + 1, std::memory_order_release); return true; }
};

enum SeqMode { SeqIdle, SeqRecArm, SeqRecord, SeqPlayArm, SeqPlay };

// What the panel shows. Written by the audio thread, read by the UI.
struct Display {
  std::atomic<float> led[NumDrums];  // 0..1
  std::atomic<int> seq{SeqIdle}, memory{0}, step{0}, length{0};
  std::atomic<int> rate[4];          // -1 off, else speed
  std::atomic<bool> bassOn{false}, overdub{false};
  std::atomic<float> bpm{120};
  Display() { for (auto& l : led) l = 0; for (auto& r : rate) r = -1; }
};

class Engine {
 public:
  static constexpr int kStepsPerBeat = 4;   // 16th notes
  static constexpr int kMemSteps = 340;     // the 2114 RAM: 1024 nibbles shared by three memories
  static constexpr int kNumMem = 3;

  std::atomic<float> params[NumParams];
  Display display;
  SpscQueue<256> queue;

  Engine() {
    for (int i = 0; i < NumParams; ++i) params[i] = paramInfo()[i].def;
    for (auto& m : mem) m.fill();
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
    for (auto& f : sumLP) f.setTau(20e3f * 470e-12f, dt);  // U4: R53/R47 20k with C24/C16 470pF
    for (auto& f : ampHP) f.setTau(0.1e-6f * 50e3f, float(1.0 / fs));  // C25/C17 into the LM386 input
    for (int c = 0; c < 2; ++c) {
      dec[c][0].lowpass(fs * 0.45, simRate, 0.5412);
      dec[c][1].lowpass(fs * 0.45, simRate, 1.3066);
    }
    samplesPerTick = 0;
  }

  // ---- input (thread safe, from any thread)
  void hit(int drum) { queue.push({1, uint8_t(drum)}); }
  void buttonDown(int b) { queue.push({2, uint8_t(b)}); }
  void buttonUp(int b) { queue.push({3, uint8_t(b)}); }

  // ---- input on the audio thread (MIDI): sample-accurate trigger at `offset` within the next process() block
  void midiNote(int note, bool on) {
    bool closed = false;
    int d = drumForNote(note, closed);
    if (d < 0) return;
    if (d == Cymbal) midiClosedHat = on ? closed : midiClosedHat;
    if (on) triggerLive(d, kPadPulse);
  }

  // ---- render. out: two channel pointers, n frames
  void process(float* const* out, int n, const HostTime& ht = {}) {
    Cmd c;
    while (queue.pop(c)) handle(c);
    updateParams();
    for (int i = 0; i < n; ++i) {
      clock(ht, i, n);
      float l = 0, r = 0;
      for (int k = 0; k < os; ++k) {
        float ol, orr;
        tickCircuit(ol, orr);
        l = dec[0][1].run(dec[0][0].run(ol));
        r = dec[1][1].run(dec[1][0].run(orr));
      }
      // after the decimator, at the output rate: C25/C17 coupling, LM386 (gain 20, 9 V supply), line-out pad
      float vol = volumeTaper(params[pVolume].load());
      l = ampHP[0].hp(l * vol);
      r = ampHP[1].hp(r * vol);
      l = railClip(l * 20.f, 3.7f) * 0.5f;  // R57/R55 1k:1k
      r = railClip(r * 20.f, 3.7f) * 0.5f;
      out[0][i] = l * 0.5f;                 // 2 V line level = full scale
      out[1][i] = r * 0.5f;
    }
    publish(n);
  }

  // ---- state (memories, tempo, bass, rates): parameters are saved by the host
  std::vector<uint8_t> saveState() const {
    std::vector<uint8_t> s;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(uint8_t(v >> (8 * i))); };
    put32(0x53594E31);  // "SYN1"
    float b = bpm;
    uint32_t bb; std::memcpy(&bb, &b, 4); put32(bb);
    put32(bassOn); put32(curMem);
    for (auto& m : mem) { put32(uint32_t(m.length)); for (int i = 0; i < kMemSteps; ++i) s.push_back(m.steps[i]); }
    for (int i = 0; i < 4; ++i) put32(uint32_t(rate[i] + 1));
    for (int i = 0; i < NumParams; ++i) { float v = params[i]; uint32_t u; std::memcpy(&u, &v, 4); put32(u); }
    return s;
  }
  bool loadState(const uint8_t* d, size_t n) {
    size_t p = 0;
    auto get32 = [&](uint32_t& v) { if (p + 4 > n) return false; v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(d[p++]) << (8 * i); return true; };
    uint32_t v;
    if (!get32(v) || v != 0x53594E31) return false;
    if (!get32(v)) return false;
    float b; std::memcpy(&b, &v, 4); bpm = std::clamp(b, kMinBpm, kMaxBpm);
    if (!get32(v)) return false;
    bassOn = v != 0;
    if (!get32(v)) return false;
    curMem = int(v % kNumMem);
    for (auto& m : mem) {
      if (!get32(v)) return false;
      m.length = int(std::min<uint32_t>(v, kMemSteps));
      if (p + kMemSteps > n) return false;
      std::memcpy(m.steps, d + p, kMemSteps); p += kMemSteps;
    }
    for (int i = 0; i < 4; ++i) { if (!get32(v)) return false; rate[i] = int(v) - 1; if (rate[i] < -1 || rate[i] > 2) rate[i] = -1; }
    for (int i = 0; i < NumParams; ++i) { if (!get32(v)) break; float f; std::memcpy(&f, &v, 4); if (std::isfinite(f)) params[i] = std::clamp(f, 0.f, 1.f); }
    seq = SeqIdle;
    publish(0);
    return true;
  }

  // test hook: start the voice directly (no queue), with the pad pulse or the sequencer's longer pulse
  void strikeNow(int drum, bool fromSequencer = false) { triggerVoice(drum, fromSequencer ? kSeqPulse : kPadPulse); }

 private:
  static constexpr float kMinBpm = 40.f, kMaxBpm = 260.f;
  // waveform 20-23: the drum line is pulsed 3.2 ms from the pads and 12 ms from memory playback
  static constexpr float kPadPulse = 0.0032f, kSeqPulse = 0.012f, kBassPulse = 0.004f;

  struct Memory {
    uint8_t steps[kMemSteps];
    int length = 0;
    void fill() { std::memset(steps, 0, sizeof steps); length = 0; }
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
  bool accentHeld = false, midiClosedHat = false;

  // U1 (the Sanyo LM6402 microcontroller): clock, rate buttons, memories
  float bpm = 120;
  double tickPhase = 0, samplesPerTick = 0;
  long tickCount = 0;
  bool hostRunning = false;
  int rate[4] = {-1, -1, -1, -1};
  bool bassOn = false;
  bool slowerHeld = false, fasterHeld = false;
  int seq = SeqIdle, curMem = 0, recStep = 0, playStep = 0;
  bool overdub = false, startPending = false;
  Memory mem[kNumMem];
  float ledT[NumDrums] = {0, 0, 0, 0, 0};

  bool bent() const { return params[pMode] > 0.5f; }

  static float volumeTaper(float v) { return v <= 0 ? 0.f : std::pow(v, 2.5f); }

  void updateParams() {
    bool b = bent();
    // cymbal / closed hat: Q2 (accent) puts R32 across C6
    cym.C = 10e-6f; cym.Rd = 8200;
    bool closed = accentHeld || midiClosedHat || (b && params[pHatClosed] > 0.5f);
    float rAcc = b ? knobLog(params[pHatDecay], kHatRlo, kHatRhi) : kHatRstock;
    cym.Rpar = closed ? rAcc : 0.f;
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
  }

  void tickCircuit(float& outL, float& outR) {
    float vg = noise.step();
    float iCym = cym.step(dt, vg, true);
    float iSn = snare.step(dt, vg, false);
    float iT1 = tom1.stepTone(dt) + tom1.stepClick(dt);
    float iT2 = tom2.stepTone(dt) + tom2.stepClick(dt);
    float iB = bass.step(dt);
    // U4: inverting summers, 20k feedback with 470pF, rails about +/-7.5 V on the +/-9 V converter
    float vL = -20e3f * (iSn + iT1 + iB);
    float vR = -20e3f * (iCym + iT2 + iB);
    vL = railClip(sumLP[0].lp(vL), 7.5f);
    vR = railClip(sumLP[1].lp(vR), 7.5f);
    // blend matrix R48/R50/R51/R52 into the volume pot (10k): about 20% of each side into the other
    const float gS = 1.f / 1e3f, gX = 1.f / 4.7e3f, gT = gS + gX + 1.f / 10e3f;
    outL = (vL * gS + vR * gX) / gT;
    outR = (vR * gS + vL * gX) / gT;
  }

  void triggerVoice(int d, float pulse) {
    if (d < 0 || d >= NumDrums) return;
    int g = std::max(1, int(pulse * simRate));
    switch (d) {
      case Tom1: tom1.gate = g; break;
      case Tom2: tom2.gate = g; break;
      case Snare: snare.gate = g; break;
      case Cymbal: cym.gate = g; break;
      case Bass: bass.gate = std::max(1, int(kBassPulse * simRate)); break;
    }
    ledT[d] = 1.f;
  }

  // a strike from the pads, the MIDI trigger inputs, or the rate buttons: sounds now, and is recorded
  void triggerLive(int d, float pulse) {
    triggerVoice(d, pulse);
    if (d == Bass) return;
    if (seq == SeqRecord || (seq == SeqPlay && overdub)) {
      // quantize to the nearest 16th
      int s = seq == SeqRecord ? recStep : playStep;
      bool late = tickPhase > 0.5;
      Memory& m = mem[curMem];
      if (seq == SeqRecord) {
        int at = late ? s + 1 : s;
        if (at >= 0 && at < kMemSteps) m.steps[at] |= uint8_t(1 << d);
      } else if (m.length > 0) {
        int at = ((late ? s + 1 : s) % m.length + m.length) % m.length;
        m.steps[at] |= uint8_t(1 << d);
      }
    }
  }

  void handle(const Cmd& c) {
    if (c.type == 1) { if (c.a < NumDrums) triggerLive(c.a, kPadPulse); return; }
    int b = c.a;
    bool down = c.type == 2;
    if (b == bAccent) { accentHeld = down; return; }
    if (b == bSlower) { slowerHeld = down; if (down) nudge(-1); return; }
    if (b == bFaster) { fasterHeld = down; if (down) nudge(+1); return; }
    if (!down) return;
    if (b == bBass) { bassOn = !bassOn; return; }
    if (b == bStop) {
      if (seq == SeqRecord) finishRecording();
      seq = SeqIdle; overdub = false;
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
        if (seq == SeqRecArm) { curMem = speed; mem[curMem].fill(); recStep = 0; startPending = true; seq = SeqRecord; }
        else startPlay(speed);
        return;
      }
      rate[drum] = rate[drum] == speed ? -1 : speed;
    }
  }

  void startPlay(int m) {
    curMem = m;
    overdub = false;
    if (mem[m].length <= 0) { seq = SeqIdle; return; }
    seq = SeqPlay; playStep = 0; startPending = true;
  }

  void finishRecording() {
    Memory& m = mem[curMem];
    int len = std::max(recStep, kStepsPerBeat);
    len = (len + kStepsPerBeat - 1) / kStepsPerBeat * kStepsPerBeat;  // whole beats
    m.length = std::min(len, kMemSteps);
  }

  void nudge(int dir) { bpm = std::clamp(bpm + float(dir), kMinBpm, kMaxBpm); }

  // runs once per output sample: the tempo clock, and everything U1 does on each 16th note
  void clock(const HostTime& ht, int i, int n) {
    if (slowerHeld || fasterHeld) {
      float rateBpmPerSec = 18.f;
      bpm = std::clamp(bpm + (fasterHeld ? 1 : -1) * rateBpmPerSec / float(sampleRate), kMinBpm, kMaxBpm);
    }
    bool synced = params[pSync] > 0.5f && ht.valid && ht.playing && ht.bpm > 1;
    double ticksPerSample;
    if (synced) {
      bpm = float(std::clamp(ht.bpm, double(kMinBpm), 400.0));
      ticksPerSample = ht.bpm / 60.0 * kStepsPerBeat / sampleRate;
      // follow the host's position: the 16th grid lines up with the host's beats
      double pos = ht.ppq * kStepsPerBeat + ticksPerSample * i;
      double frac = pos - std::floor(pos);
      if (!hostRunning) { hostRunning = true; tickCount = long(std::floor(pos)); tickPhase = frac; if (frac < ticksPerSample) { onTick(); } return; }
      long want = long(std::floor(pos));
      if (want != tickCount) { tickCount = want; tickPhase = frac; onTick(); return; }
      tickPhase = frac;
      (void)n;
      return;
    }
    hostRunning = false;
    ticksPerSample = bpm / 60.0 * kStepsPerBeat / sampleRate;
    tickPhase += ticksPerSample;
    if (tickPhase >= 1.0) { tickPhase -= 1.0; ++tickCount; onTick(); }
  }

  void onTick() {
    bool beat = (tickCount % kStepsPerBeat) == 0;
    if (startPending) {
      if (!beat) goto rates;  // memories start on the next beat
      startPending = false;
      if (seq == SeqPlay) playStep = 0;
      if (seq == SeqRecord) recStep = 0;
    } else {
      if (seq == SeqRecord) {
        ++recStep;
        if (recStep >= kMemSteps) { finishRecording(); startPlay(curMem); }
      } else if (seq == SeqPlay && mem[curMem].length > 0) {
        playStep = (playStep + 1) % mem[curMem].length;
      }
    }
    if (seq == SeqPlay && !startPending) {
      uint8_t bits = mem[curMem].steps[playStep % kMemSteps];
      for (int d = 0; d < 4; ++d) if (bits & (1 << d)) triggerVoice(d, kSeqPulse);
    }
  rates:
    if (beat) {
      ledT[Bass] = std::max(ledT[Bass], 0.6f);  // the bass LED blinks the tempo
      if (bassOn) triggerVoice(Bass, kBassPulse);
    }
    static const int every[3] = {1, 2, 4};  // fast, medium, slow: 16ths, 8ths, quarters
    for (int d = 0; d < 4; ++d) {
      if (rate[d] < 0) continue;
      if (d == Cymbal && (seq == SeqRecArm || seq == SeqPlayArm)) continue;
      if (tickCount % every[rate[d]] == 0) triggerLive(d, kSeqPulse);
    }
  }

  void publish(int n) {
    float decay = std::exp(-float(n) / float(sampleRate) / 0.06f);
    for (int d = 0; d < NumDrums; ++d) { display.led[d] = ledT[d]; ledT[d] *= decay; }
    display.seq = seq;
    display.memory = curMem;
    display.step = seq == SeqRecord ? recStep : playStep;
    display.length = mem[curMem].length;
    for (int d = 0; d < 4; ++d) display.rate[d] = rate[d];
    display.bassOn = bassOn;
    display.overdub = overdub;
    display.bpm = bpm;
  }
};

}  // namespace syn
