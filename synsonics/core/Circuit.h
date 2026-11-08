// The Synsonics circuit, part by part (see Engine.h for the sources and how it is put together).
#pragma once
#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846  // MSVC leaves it out
#endif
#include <cstdint>

namespace syn {

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

}  // namespace syn
