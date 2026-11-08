// The effects on the two send buses: a REV7-style digital reverb (send A) and a tempo-synced digital delay (send B).
//
// REV7: what is documented about the Yamaha REV7 (Sound On Sound review, Nov. 1985, and collector notes) is followed
// here. The input is summed to mono and goes through a single converter. Two outputs carry slightly different reverb
// patterns. Reverb time runs 0.3 to 9.9 s (mid band), with separate high- and low-frequency decay times. Initial delay
// and first reflection each run 0 to 99.9 ms. There are 10 diffusion steps, liveness and room size, a reverb
// bandwidth of 20 Hz to 12 kHz, a 3-band EQ on the effect only, and a reverb dynamic range of 78 dB. Its algorithms
// themselves are not published, so the engine is a model built on those specs: early-reflection taps, input diffusion
// and an 8-line feedback delay network with 3-band decay. The 30 programs are this app's own, in the families the
// reviews describe (halls, rooms, vocal, plates, early reflections, gated, reverse, delays, flange/phase); they are not
// the factory list.
#pragma once
#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846  // MSVC leaves it out
#endif
#include <cstdint>
#include <cstring>
#include <vector>

namespace syn {

struct DelayLine {
  std::vector<float> buf;
  int w = 0;
  void init(int n) { buf.assign(size_t(std::max(2, n)), 0.f); w = 0; }
  void clear() { std::fill(buf.begin(), buf.end(), 0.f); }
  void push(float x) { buf[size_t(w)] = x; if (++w >= int(buf.size())) w = 0; }
  float tap(int d) const {  // d samples ago (1 = last written)
    int n = int(buf.size());
    int i = w - std::clamp(d, 1, n - 1);
    if (i < 0) i += n;
    return buf[size_t(i)];
  }
  float tapf(float d) const {
    int n = int(buf.size());
    d = std::clamp(d, 1.f, float(n - 2));
    int di = int(d);
    float fr = d - float(di);
    return tap(di) * (1.f - fr) + tap(di + 1) * fr;
  }
};

struct LP1 {  // one-pole low-pass
  float a = 0, z = 0;
  void set(float fc, float fs) { a = 1.f - std::exp(-2.f * float(M_PI) * std::min(fc, fs * 0.49f) / fs); }
  float run(float x) { z += a * (x - z); return z; }
};

struct Bq {  // RBJ biquad
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  float run(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
  void norm(double B0, double B1, double B2, double A0, double A1, double A2) {
    b0 = float(B0 / A0); b1 = float(B1 / A0); b2 = float(B2 / A0); a1 = float(A1 / A0); a2 = float(A2 / A0);
  }
  void lowpass(double f, double fs, double q = 0.7071) {
    double w = 2 * M_PI * std::min(f, fs * 0.45) / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
    norm((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
  }
  void highpass(double f, double fs, double q = 0.7071) {
    double w = 2 * M_PI * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
    norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
  }
  void peak(double f, double fs, double q, double db) {
    double A = std::pow(10.0, db / 40), w = 2 * M_PI * std::min(f, fs * 0.45) / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
    norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
  }
  void lowshelf(double f, double fs, double db) {
    double A = std::pow(10.0, db / 40), w = 2 * M_PI * f / fs, c = std::cos(w), s = std::sin(w), al = s / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
    norm(A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa), (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
  }
  void highshelf(double f, double fs, double db) {
    double A = std::pow(10.0, db / 40), w = 2 * M_PI * std::min(f, fs * 0.45) / fs, c = std::cos(w), s = std::sin(w), al = s / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
    norm(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa), (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
  }
};

// ------------------------------------------------------------------------------------------------------- REV7
enum RevAlgo { AHall, ARoom, AVocal, APlate, AEarly, AGate, AReverse, ADelay, AEcho, AFlange, APhase, NumAlgos };

// a program: algorithm + the knob positions it loads (all 0..1, see Rev7::Settings for what they mean)
struct RevProgram {
  const char* name; int algo;
  float time, high, low, init, first, diff, live, size;
};
constexpr int kRevPrograms = 30;
inline const RevProgram* revPrograms() {
  // time: RT 0.3..9.9 s (log)   high/low: HF / LF decay ratios   init/first: 0..99.9 ms   diff: 1..10
  static const RevProgram p[kRevPrograms] = {
      {"LARGE HALL", AHall, 0.70f, 0.45f, 0.55f, 0.32f, 0.28f, 0.9f, 0.55f, 0.85f},
      {"HALL", AHall, 0.58f, 0.50f, 0.50f, 0.22f, 0.20f, 0.8f, 0.50f, 0.70f},
      {"SMALL HALL", AHall, 0.45f, 0.55f, 0.45f, 0.12f, 0.14f, 0.8f, 0.45f, 0.50f},
      {"CONCERT HALL", AHall, 0.76f, 0.40f, 0.60f, 0.40f, 0.30f, 1.0f, 0.60f, 0.95f},
      {"ROOM", ARoom, 0.30f, 0.55f, 0.50f, 0.08f, 0.08f, 0.7f, 0.55f, 0.40f},
      {"SMALL ROOM", ARoom, 0.18f, 0.60f, 0.45f, 0.04f, 0.05f, 0.6f, 0.60f, 0.22f},
      {"LIVE ROOM", ARoom, 0.36f, 0.75f, 0.50f, 0.06f, 0.10f, 0.6f, 0.85f, 0.45f},
      {"VOCAL", AVocal, 0.42f, 0.55f, 0.40f, 0.30f, 0.15f, 0.9f, 0.40f, 0.55f},
      {"VOCAL PLATE", APlate, 0.45f, 0.65f, 0.40f, 0.25f, 0.00f, 1.0f, 0.30f, 0.55f},
      {"PLATE", APlate, 0.52f, 0.70f, 0.45f, 0.05f, 0.00f, 1.0f, 0.30f, 0.60f},
      {"BRIGHT PLATE", APlate, 0.48f, 0.90f, 0.35f, 0.03f, 0.00f, 1.0f, 0.30f, 0.50f},
      {"DARK PLATE", APlate, 0.55f, 0.30f, 0.55f, 0.08f, 0.00f, 1.0f, 0.30f, 0.60f},
      {"EARLY REF 1", AEarly, 0.20f, 0.60f, 0.50f, 0.05f, 0.30f, 0.5f, 0.50f, 0.50f},
      {"EARLY REF 2", AEarly, 0.20f, 0.60f, 0.50f, 0.15f, 0.60f, 0.5f, 0.75f, 0.80f},
      {"SLAP ROOM", AEarly, 0.20f, 0.70f, 0.50f, 0.45f, 0.20f, 0.3f, 0.30f, 0.35f},
      {"GATE REVERB", AGate, 0.62f, 0.60f, 0.50f, 0.02f, 0.30f, 1.0f, 0.70f, 0.60f},
      {"TIGHT GATE", AGate, 0.55f, 0.65f, 0.50f, 0.01f, 0.15f, 1.0f, 0.70f, 0.35f},
      {"REVERSE GATE", AReverse, 0.50f, 0.60f, 0.50f, 0.05f, 0.40f, 1.0f, 0.60f, 0.60f},
      {"REVERSE", AReverse, 0.60f, 0.55f, 0.50f, 0.10f, 0.70f, 1.0f, 0.80f, 0.80f},
      {"STUDIO A", ARoom, 0.25f, 0.50f, 0.50f, 0.10f, 0.06f, 0.8f, 0.45f, 0.35f},
      {"STUDIO B", ARoom, 0.40f, 0.45f, 0.55f, 0.15f, 0.12f, 0.9f, 0.50f, 0.55f},
      {"CHAMBER", AHall, 0.50f, 0.55f, 0.55f, 0.10f, 0.10f, 1.0f, 0.65f, 0.45f},
      {"CATHEDRAL", AHall, 0.92f, 0.35f, 0.75f, 0.55f, 0.45f, 1.0f, 0.70f, 1.00f},
      {"STEREO DELAY", ADelay, 0.45f, 0.55f, 0.50f, 0.60f, 0.90f, 0.3f, 0.50f, 0.50f},
      {"ECHO", AEcho, 0.55f, 0.45f, 0.50f, 0.75f, 0.75f, 0.5f, 0.50f, 0.50f},
      {"SLAP ECHO", AEcho, 0.30f, 0.60f, 0.50f, 0.25f, 0.30f, 0.3f, 0.50f, 0.50f},
      {"FLANGE A", AFlange, 0.30f, 0.60f, 0.50f, 0.05f, 0.05f, 0.6f, 0.30f, 0.60f},
      {"FLANGE B", AFlange, 0.45f, 0.60f, 0.50f, 0.08f, 0.10f, 0.8f, 0.15f, 0.90f},
      {"PHASING", APhase, 0.30f, 0.60f, 0.50f, 0.02f, 0.02f, 0.5f, 0.25f, 0.70f},
      {"TUNNEL", AEcho, 0.85f, 0.35f, 0.60f, 0.90f, 0.40f, 0.9f, 0.50f, 0.50f},
  };
  return p;
}

class Rev7 {
 public:
  struct Settings {
    int algo = AHall;
    float time = 0.5f, high = 0.5f, low = 0.5f, init = 0.2f, first = 0.2f, diff = 0.8f, live = 0.5f, size = 0.7f;
    float eqLo = 0.5f, eqMid = 0.5f, eqHi = 0.5f;  // 0.5 = flat, +/-12 dB
  };
  static float rtSeconds(float t) { return 0.3f * std::pow(33.f, std::clamp(t, 0.f, 1.f)); }   // 0.3 .. 9.9 s
  static float msInit(float v) { return 99.9f * std::clamp(v, 0.f, 1.f); }
  static int diffSteps(float v) { return 1 + int(std::round(std::clamp(v, 0.f, 1.f) * 9)); }

  void prepare(double sampleRate) {
    fs = float(sampleRate);
    int maxD = int(fs * 1.2f) + 8;
    pre.init(int(fs * 0.25f) + 8);
    er.init(int(fs * 0.4f) + 8);
    for (int i = 0; i < 8; ++i) fdn[i].init(int(fs * 0.25f) + 8);
    for (auto& a : diffAp) a.init(int(fs * 0.02f) + 8);
    for (auto& d : dly) d.init(maxD);
    for (auto& m : modLine) m.init(int(fs * 0.03f) + 8);
    bwHp.highpass(20, fs); bwLp1.lowpass(12000, fs, 0.541); bwLp2.lowpass(12000, fs, 1.307);
    outLp[0].lowpass(12000, fs); outLp[1].lowpass(12000, fs);
    last = Settings(); last.algo = -1;
    clear();
  }
  void clear() {
    pre.clear(); er.clear();
    for (auto& f : fdn) f.clear();
    for (auto& a : diffAp) a.clear();
    for (auto& d : dly) d.clear();
    for (auto& m : modLine) m.clear();
    for (auto& z : lpState) z = 0;
    for (auto& z : hpState) z = 0;
    gateEnv = 0; gateOpen = 0;
  }

  void process(const float* inL, const float* inR, float* outL, float* outR, int n, const Settings& s, double bpm) {
    configure(s);
    for (int i = 0; i < n; ++i) {
      // one converter: the stereo input summed to mono, band-limited to the 20 Hz - 12 kHz reverb bandwidth
      float x = 0.5f * (inL[i] + inR[i]);
      x = bwLp2.run(bwLp1.run(bwHp.run(x)));
      float l = 0, r = 0;
      switch (s.algo) {
        case ADelay: case AEcho: delays(x, l, r, s, bpm); break;
        case AFlange: case APhase: modulation(x, l, r, s); break;
        default: reverb(x, l, r, s); break;
      }
      l = eq[0][2].run(eq[0][1].run(eq[0][0].run(outLp[0].run(l))));
      r = eq[1][2].run(eq[1][1].run(eq[1][0].run(outLp[1].run(r))));
      // 78 dB of dynamic range: a 13-bit-ish output step, which gives long tails their grain
      const float q = 1.f / 4096.f;
      outL[i] = std::round(l / q) * q;
      outR[i] = std::round(r / q) * q;
    }
  }

 private:
  float fs = 48000;
  Settings last;
  DelayLine pre, er, fdn[8], diffAp[4], dly[2], modLine[2];
  int fdnLen[8] = {}, apLen[4] = {};
  float g[8][3] = {};  // per line: low / mid / high gains
  float lpState[8] = {}, hpState[8] = {};
  float lpA = 0, hpA = 0;
  float apG = 0.6f;
  Bq bwHp, bwLp1, bwLp2, outLp[2], eq[2][3];
  struct Tap { float t, gl, gr; };
  Tap taps[16];
  int nTaps = 0;
  float erLevel = 0.5f, revLevel = 0.5f, preMs = 0, firstMs = 0;
  float lfo = 0, gateEnv = 0, gateOpen = 0;
  float dlyFb = 0.4f;
  LP1 dlyDamp[2];

  void configure(const Settings& s) {
    if (std::memcmp(&s, &last, sizeof s) == 0) return;
    bool sizeChanged = s.size != last.size || s.algo != last.algo;
    last = s;
    preMs = msInit(s.init);
    firstMs = msInit(s.first);
    // size scales every delay in the network; prime-ish ratios keep the echoes from lining up
    static const float base[8] = {0.0297f, 0.0371f, 0.0411f, 0.0437f, 0.0533f, 0.0577f, 0.0631f, 0.0713f};
    float scale = 0.45f + 1.25f * s.size;
    if (s.algo == APlate) scale = 0.35f + 0.6f * s.size;  // plates are dense and short-pathed
    if (s.algo == ARoom) scale *= 0.7f;
    for (int i = 0; i < 8; ++i) fdnLen[i] = std::max(8, int(base[i] * scale * fs));
    static const float apBase[4] = {0.0047f, 0.0036f, 0.0127f, 0.0093f};
    for (int i = 0; i < 4; ++i) apLen[i] = std::max(4, int(apBase[i] * (0.6f + 0.6f * s.size) * fs));
    apG = 0.25f + 0.05f * float(diffSteps(s.diff));  // 10 steps of diffusion
    // three-band decay: low below 300 Hz, high above 3.5 kHz
    float rt = rtSeconds(s.time);
    if (s.algo == AGate || s.algo == AReverse) rt = std::max(rt, 1.2f);
    float rtH = rt * (0.1f + 0.9f * s.high), rtL = rt * (0.4f + 2.0f * s.low);
    lpA = 1.f - std::exp(-2.f * float(M_PI) * 300.f / fs);
    hpA = 1.f - std::exp(-2.f * float(M_PI) * 3500.f / fs);
    for (int i = 0; i < 8; ++i) {
      float d = float(fdnLen[i]) / fs;
      g[i][0] = std::pow(10.f, -3.f * d / rtL);
      g[i][1] = std::pow(10.f, -3.f * d / rt);
      g[i][2] = std::pow(10.f, -3.f * d / rtH);
    }
    // early reflections: liveness sets how slowly they die away, size how far apart they are
    nTaps = 12;
    uint32_t seed = 0x9E3779B9u;
    float spread = (8.f + 60.f * s.size) * 0.001f;
    for (int i = 0; i < nTaps; ++i) {
      seed = seed * 1664525u + 1013904223u;
      float jitter = float((seed >> 8) & 0xFFFF) / 65535.f;
      float t = spread * (float(i) + 0.3f + 0.7f * jitter) / float(nTaps) * 1.6f;
      float decay = std::pow(0.15f + 0.8f * s.live, float(i) / 3.f);
      if (s.algo == AReverse) decay = std::pow(0.15f + 0.8f * s.live, float(nTaps - 1 - i) / 3.f);
      float pan = (i & 1) ? 0.85f : 0.35f;
      taps[i] = {t, decay * pan, decay * (1.f - pan + 0.2f)};
    }
    erLevel = s.algo == AEarly ? 1.f : (s.algo == APlate ? 0.f : (s.algo == AReverse ? 0.9f : 0.35f + 0.4f * s.live));
    revLevel = s.algo == AEarly ? 0.f : (s.algo == AReverse ? 0.25f : 1.f);
    if (s.algo == AVocal) erLevel *= 0.6f;
    // 3-band EQ on the effect: 200 Hz shelf, 1.5 kHz peak, 6 kHz shelf, +/-12 dB
    for (int c = 0; c < 2; ++c) {
      eq[c][0].lowshelf(200, fs, (s.eqLo - 0.5f) * 24);
      eq[c][1].peak(1500, fs, 0.9, (s.eqMid - 0.5f) * 24);
      eq[c][2].highshelf(6000, fs, (s.eqHi - 0.5f) * 24);
    }
    dlyFb = 0.15f + 0.8f * s.diff;
    for (auto& d : dlyDamp) d.set(1500.f + 9000.f * s.high, fs);
    if (sizeChanged) { for (auto& f : fdn) f.clear(); }
  }

  void reverb(float x, float& outL, float& outR, const Settings& s) {
    pre.push(x);
    float xp = pre.tapf(std::max(1.f, preMs * 0.001f * fs));
    // early reflections
    er.push(xp);
    float el = 0, erR = 0;
    for (int i = 0; i < nTaps; ++i) {
      float v = er.tapf(1.f + taps[i].t * fs);
      el += v * taps[i].gl; erR += v * taps[i].gr;
    }
    // the reverb starts "first reflection" ms after the early sound, through the diffusers
    float xr = er.tapf(std::max(1.f, firstMs * 0.001f * fs));
    for (int k = 0; k < 4; ++k) {
      float dlv = diffAp[k].tap(apLen[k]);
      float v = xr + apG * dlv;
      diffAp[k].push(v);
      xr = dlv - apG * v;
    }
    // gate: the tail is cut a fixed time after the input goes quiet
    float gateGain = 1.f;
    if (s.algo == AGate || (s.algo == AReverse && s.time < 0.55f)) {
      float ax = std::fabs(x);
      gateEnv = std::max(ax, gateEnv * 0.9995f);
      float hold = 0.05f + 0.4f * s.time;
      if (gateEnv > 0.01f) gateOpen = hold * fs; else if (gateOpen > 0) gateOpen -= 1;
      gateGain = gateOpen > 0 ? 1.f : 0.f;
    }
    // 8-line feedback delay network, Hadamard mixing, three-band decay per line, slow modulation on two lines
    float y[8];
    lfo += 0.7f / fs; if (lfo >= 1) lfo -= 1;
    float mod = std::sin(2.f * float(M_PI) * lfo) * 0.0006f * fs;
    for (int i = 0; i < 8; ++i) {
      float d = float(fdnLen[i]) + (i == 2 ? mod : (i == 5 ? -mod : 0.f));
      float v = fdn[i].tapf(d);
      lpState[i] += lpA * (v - lpState[i]);
      hpState[i] += hpA * (v - hpState[i]);
      float lo = lpState[i], hi = v - hpState[i], mid = v - lo - hi;
      y[i] = lo * g[i][0] + mid * g[i][1] + hi * g[i][2];
    }
    float h[8];
    for (int i = 0; i < 8; ++i) h[i] = y[i];
    for (int len = 1; len < 8; len <<= 1)
      for (int i = 0; i < 8; i += len << 1)
        for (int j = i; j < i + len; ++j) { float a = h[j], b = h[j + len]; h[j] = a + b; h[j + len] = a - b; }
    const float norm = 0.35355339f;  // 1/sqrt(8)
    for (int i = 0; i < 8; ++i) fdn[i].push(h[i] * norm + xr * ((i & 1) ? 0.5f : -0.5f));
    // two outputs, different taps: slightly different patterns left and right
    float rl = (y[0] - y[2] + y[4] + y[6]) * 0.5f, rr = (y[1] + y[3] - y[5] + y[7]) * 0.5f;
    outL = (el * erLevel + rl * revLevel * gateGain);
    outR = (erR * erLevel + rr * revLevel * gateGain);
  }

  void delays(float x, float& outL, float& outR, const Settings& s, double bpm) {
    // delay programs: TIME sets the left delay (up to 1 s, or a note value when synced), FIRST the right/left ratio
    float t = 0.02f + 0.98f * s.time;
    (void)bpm;
    float dl = t * fs, dr = dl * (0.5f + 0.5f * s.first);
    float l = dly[0].tapf(dl), r = dly[1].tapf(dr);
    float fb = s.algo == AEcho ? dlyFb : dlyFb * 0.5f;
    if (s.algo == AEcho) {  // cross feedback: ping-pong echo
      dly[0].push(x + dlyDamp[0].run(r) * fb);
      dly[1].push(dlyDamp[1].run(l) * fb);
    } else {
      dly[0].push(x + dlyDamp[0].run(l) * fb);
      dly[1].push(x + dlyDamp[1].run(r) * fb);
    }
    outL = l; outR = r;
  }

  void modulation(float x, float& outL, float& outR, const Settings& s) {
    // flange / phase: LIVENESS sets the rate, SIZE the depth, DIFFUSION the feedback
    lfo += (0.05f + 3.f * s.live) / fs; if (lfo >= 1) lfo -= 1;
    float base = (s.algo == APhase ? 0.0004f : 0.0015f) * fs, depth = (s.algo == APhase ? 0.0012f : 0.006f) * fs * s.size;
    float ml = base + depth * 0.5f * (1 + std::sin(2.f * float(M_PI) * lfo));
    float mr = base + depth * 0.5f * (1 + std::cos(2.f * float(M_PI) * lfo));
    float fb = 0.85f * s.diff;
    float l = modLine[0].tapf(ml), r = modLine[1].tapf(mr);
    modLine[0].push(x + l * fb);
    modLine[1].push(x - r * fb);
    outL = 0.6f * (x + l); outR = 0.6f * (x - r);
  }
};

// ---------------------------------------------------------------------------------------------- digital delay
constexpr int kDelayDivs = 9;
inline const char* delayDivName(int i) {
  static const char* n[kDelayDivs] = {"1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2"};
  return n[std::clamp(i, 0, kDelayDivs - 1)];
}
inline double delayDivBeats(int i) {
  static const double b[kDelayDivs] = {0.25, 1.0 / 3, 0.375, 0.5, 2.0 / 3, 0.75, 1.0, 1.5, 2.0};
  return b[std::clamp(i, 0, kDelayDivs - 1)];
}
inline int delayDiv(float v) { return std::clamp(int(v * (kDelayDivs - 1) + 0.5f), 0, kDelayDivs - 1); }

class SyncDelay {
 public:
  void prepare(double sampleRate) {
    fs = float(sampleRate);
    for (auto& d : line) d.init(int(fs * 4.2f) + 8);
    for (auto& t : time) t = fs * 0.25f;
  }
  void clear() { for (auto& d : line) d.clear(); }
  // division index, feedback 0..1, damp 0..1, ping-pong
  void process(const float* inL, const float* inR, float* outL, float* outR, int n, int div, float fb, float damp, bool ping, double bpm) {
    float target = float(std::min(4.0, delayDivBeats(div) * 60.0 / std::max(30.0, bpm))) * fs;
    for (auto& d : lp) d.set(18000.f * std::pow(0.08f, damp), fs);
    float f = std::min(0.95f, fb);
    for (int i = 0; i < n; ++i) {
      for (auto& t : time) t += (target - t) * 0.0005f;  // glide on tempo changes, like a tape-speed change
      float l = line[0].tapf(time[0]), r = line[1].tapf(time[1]);
      float dl = lp[0].run(l), dr = lp[1].run(r);
      if (ping) {
        line[0].push(0.5f * (inL[i] + inR[i]) + dr * f);
        line[1].push(dl * f);
      } else {
        line[0].push(inL[i] + dl * f);
        line[1].push(inR[i] + dr * f);
      }
      outL[i] = l; outR[i] = r;
    }
  }

 private:
  float fs = 48000;
  DelayLine line[2];
  LP1 lp[2];
  float time[2] = {};
};

}  // namespace syn
