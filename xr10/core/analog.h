// Omega XR10: the analog output stage, modelled from the Akai XR10 service manual, main schematic L400802M (sheet 2-2)
// and block diagram L400851M. Per channel (R: IC10 pin 1, L: IC10 pin 20, EFFECT: IC11 pin 20):
//
//   LC7881 16-bit DAC (non-oversampling: a zero-order-hold staircase)
//   -> M5223L voltage follower (IC13 / IC12)
//   -> R57 4.7k -> C5 680p to ground -> BL-30VF LC low-pass (middle tap C4 2.7n) -> C12 2.7n to ground   [F1-F3, fc = 12 kHz]
//   -> C69 10u -> R3 47k into the M5216L / M5223L inverting amp, feedback R4 330k || C11 47p
//        main outputs: the VOLUME pot (VR2, on the VR PCB through P104) is a rheostat across R4;
//        effect output: the EFFECT VR (VR1 100k, A taper) in series with R10 330k || C15 47p
//   -> C21 10u -> R21 1.2k -> C25 4.7n || R22 47k -> output jack
//
// The op amps run from a single +12 V supply around a mid-rail analog ground, so they clip about 4.5 V either side.
// Not in the manual, and so estimated: the inductances inside the BL-30VF (two equal inductors, solved so the module
// meets its printed fc = 12 kHz with the capacitors around it), and the VR2 value and taper (1 M, audio taper).
#pragma once
#include "xmath.h"

namespace xr {
namespace part {
static const double R57 = 4.7e3, C5 = 680e-12, C4 = 2.7e-9, C12 = 2.7e-9, R3 = 47e3, C69 = 10e-6, R4 = 330e3, C11 = 47e-12;
static const double C21 = 10e-6, R21 = 1.2e3, C25 = 4.7e-9, R22 = 47e3, R9 = 47e3, R10 = 330e3, C15 = 47e-12, VR1 = 100e3;
static const double LPF_FC = 12000;     // printed on the block diagram for F1-F3
static const double VR2 = 1e6;          // estimated
static const double RAIL = 4.5;         // op-amp swing around analog ground on a 12 V supply (estimated from the supply)
// the manual's output check: 8.2 dBm at the outputs with VOLUME at max (2.0 V rms, 2.81 V peak) for the test tone,
// taken here as a full-scale sine
static const double OUT_FS_PEAK = 2.81;
}

struct Cx {
  double r, i;
  Cx operator+(Cx o) const { return {r + o.r, i + o.i}; }
  Cx operator*(Cx o) const { return {r * o.r - i * o.i, r * o.i + i * o.r}; }
  Cx operator/(Cx o) const { double d = o.r * o.r + o.i * o.i; return {(r * o.r + i * o.i) / d, (i * o.r - r * o.i) / d}; }
  double mag() const { return xm::sqrt(r * r + i * i); }
};
inline Cx par(Cx a, Cx b) { return (a * b) / (a + b); }

// R57 -> C5 | L | C4 | L | C12 -> load (R3 to the amp's virtual ground): a 5th-order ladder, simulated in state space
// (trapezoidal rule) so the real component values set its response.
struct LCLowpass {
  double L = 0.05;
  double M[5][5], N[5], x[5], uPrev = 0;
  static Cx response(double f, double L) {
    double w = xm::TAU * f;
    Cx s = {0, w};
    auto zc = [&](double c) { return Cx{1, 0} / (s * Cx{c, 0}); };
    Cx zl = s * Cx{L, 0}, rl = {part::R3, 0};
    Cx z3 = f > 0 ? par(zc(part::C12), rl) : rl;
    Cx z23 = zl + z3;
    Cx z2 = f > 0 ? par(zc(part::C4), z23) : z23;
    Cx z12 = zl + z2;
    Cx z1 = f > 0 ? par(zc(part::C5), z12) : z12;
    Cx v1 = z1 / (Cx{part::R57, 0} + z1);
    return v1 * (z2 / z12) * (z3 / z23);
  }
  // the inductance that puts the -3 dB point at the printed 12 kHz
  static double solveL() {
    double lo = xm::log(1e-4), hi = xm::log(10.0);
    double dc = response(0, 1).mag();
    for (int k = 0; k < 80; k++) {
      double mid = 0.5 * (lo + hi);
      double g = response(part::LPF_FC, xm::exp(mid)).mag() / dc;
      if (g > 0.70710678) lo = mid; else hi = mid;   // more inductance, lower cutoff
    }
    return xm::exp(0.5 * (lo + hi));
  }
  void design(double fs, double inductance) {
    L = inductance;
    // states: V(C5), I(L1), V(C4), I(L2), V(C12)
    double A[5][5] = {{-1 / (part::R57 * part::C5), -1 / part::C5, 0, 0, 0},
                      {1 / L, 0, -1 / L, 0, 0},
                      {0, 1 / part::C4, 0, -1 / part::C4, 0},
                      {0, 0, 1 / L, 0, -1 / L},
                      {0, 0, 0, 1 / part::C12, -1 / (part::R3 * part::C12)}};
    double B[5] = {1 / (part::R57 * part::C5), 0, 0, 0, 0};
    double h = 1 / fs, P[5][5], Q[5][10];
    for (int i = 0; i < 5; i++) for (int j = 0; j < 5; j++) { P[i][j] = (i == j) + h / 2 * A[i][j]; Q[i][j] = (i == j) - h / 2 * A[i][j]; Q[i][5 + j] = i == j; }
    // invert (I - hA/2) by Gauss-Jordan
    for (int c = 0; c < 5; c++) {
      int pv = c; for (int r = c + 1; r < 5; r++) if (xm::fabs(Q[r][c]) > xm::fabs(Q[pv][c])) pv = r;
      for (int j = 0; j < 10; j++) { double t = Q[c][j]; Q[c][j] = Q[pv][j]; Q[pv][j] = t; }
      double d = Q[c][c]; for (int j = 0; j < 10; j++) Q[c][j] /= d;
      for (int r = 0; r < 5; r++) if (r != c) { double f = Q[r][c]; for (int j = 0; j < 10; j++) Q[r][j] -= f * Q[c][j]; }
    }
    for (int i = 0; i < 5; i++) {
      N[i] = 0;
      for (int j = 0; j < 5; j++) { M[i][j] = 0; for (int k = 0; k < 5; k++) M[i][j] += Q[i][5 + k] * P[k][j]; N[i] += Q[i][5 + j] * B[j] * h / 2; }
    }
    reset();
  }
  void reset() { for (double& v : x) v = 0; uPrev = 0; }
  double run(double u) {
    double nx[5];
    for (int i = 0; i < 5; i++) { double a = N[i] * (u + uPrev); for (int j = 0; j < 5; j++) a += M[i][j] * x[j]; nx[i] = a; }
    for (int i = 0; i < 5; i++) x[i] = nx[i];
    uPrev = u;
    return x[4];
  }
};

// a first-order section H(s) = (b0 + b1 s) / (a0 + a1 s), bilinear transformed
struct First {
  double B0 = 1, B1 = 0, A1 = 0, x1 = 0, y1 = 0;
  void set(double b0, double b1, double a0, double a1, double fs) {
    double K = 2 * fs, A0 = a0 + a1 * K;
    B0 = (b0 + b1 * K) / A0; B1 = (b0 - b1 * K) / A0; A1 = (a0 - a1 * K) / A0;
  }
  double run(double x) { double y = B0 * x + B1 * x1 - A1 * y1; x1 = x; y1 = y; return y; }
  void reset() { x1 = y1 = 0; }
};

// the op amp running out of swing near the rails
inline double rail(double v) {
  const double r = part::RAIL, k = 0.85 * r;
  double a = v < 0 ? -v : v;
  if (a <= k) return v;
  double y = k + (r - k) * xm::tanh((a - k) / (r - k));
  return v < 0 ? -y : y;
}

// one output (DAC volts in, jack volts out)
struct OutputChannel {
  LCLowpass lpf;
  First couple, amp, outHp, outLp;
  bool effect = false;
  double fs = 0, gainSet = -1;
  void design(double rate, double inductance, bool isEffect) {
    fs = rate; effect = isEffect;
    lpf.design(fs, inductance);
    couple.set(0, part::C69 * part::R3, 1, part::C69 * part::R3, fs);                      // C69 into R3 (virtual ground)
    outHp.set(0, part::C21 * (part::R21 + part::R22), 1, part::C21 * (part::R21 + part::R22), fs);
    double rp = part::R21 * part::R22 / (part::R21 + part::R22);
    outLp.set(part::R22 / (part::R21 + part::R22), 0, 1, rp * part::C25, fs);              // R21 into C25 || R22
    gainSet = -1;
  }
  // knob 0..1: VOLUME (VR2 across R4) or EFFECT VR (VR1 in series with R10 || C15)
  void setKnob(double k) {
    if (k == gainSet) return;
    gainSet = k;
    double taper = k <= 0 ? 0 : (xm::pow(10.0, 2 * k) - 1) / 99.0;   // audio-taper resistance, 0..1 of the track
    if (!effect) {
      double rv = part::VR2 * taper, rf = rv <= 0 ? 0 : part::R4 * rv / (part::R4 + rv);
      // H = -(Rf / R3) / (1 + s Rf C11)
      amp.set(-rf / part::R3, 0, 1, rf * part::C11, fs);
    } else {
      double rv = part::VR1 * taper;
      // H = -((R10 + Rv) + s Rv R10 C15) / (R9 (1 + s R10 C15))
      amp.set(-(part::R10 + rv) / part::R9, -rv * part::R10 * part::C15 / part::R9, 1, part::R10 * part::C15, fs);
    }
  }
  double run(double vdac) {
    double v = lpf.run(vdac);
    v = couple.run(v);
    v = rail(amp.run(v));
    v = outHp.run(v);
    return outLp.run(v);
  }
  void reset() { lpf.reset(); couple.reset(); amp.reset(); outHp.reset(); outLp.reset(); }
};

// gain from DAC volts to jack volts at full volume, at 1 kHz (for the calibration)
inline double stageGain(double inductance) {
  double rf = part::R4 * part::VR2 / (part::R4 + part::VR2);
  double lp = LCLowpass::response(1000, inductance).mag();
  return lp * (rf / part::R3) * (part::R22 / (part::R21 + part::R22));
}

// the whole output stage as a frequency response, DAC to jack, VOLUME at max: the DAC's zero-order hold at the machine
// rate fsm, the LC low-pass, the C69 coupling, the inverting amp (Rf = R4 || VR2, C11) and the output network.
// (Used by tools/make-rom.cpp to take the circuit back out of recordings made from the XR10's outputs.)
inline Cx stageResponse(double f, double fsm, double inductance) {
  const double w = xm::TAU * f;
  Cx s = {0, w};
  Cx zoh = {1, 0};
  if (f > 0) { double x = xm::PI * f / fsm, sinc = xm::sin(x) / x; zoh = Cx{sinc * xm::cos(-x), sinc * xm::sin(-x)}; }
  Cx lc = LCLowpass::response(f, inductance);
  Cx one = {1, 0};
  Cx couple = f > 0 ? (s * Cx{part::C69 * part::R3, 0}) / (one + s * Cx{part::C69 * part::R3, 0}) : Cx{0, 0};
  double rf = part::R4 * part::VR2 / (part::R4 + part::VR2);
  Cx amp = Cx{-rf / part::R3, 0} / (one + s * Cx{rf * part::C11, 0});
  double rt = part::R21 + part::R22, rp = part::R21 * part::R22 / rt;
  Cx outHp = f > 0 ? (s * Cx{part::C21 * rt, 0}) / (one + s * Cx{part::C21 * rt, 0}) : Cx{0, 0};
  Cx outLp = Cx{part::R22 / rt, 0} / (one + s * Cx{rp * part::C25, 0});
  return zoh * lc * couple * amp * outHp * outLp;
}

// windowed-sinc decimator: the analog model runs at OS x the host rate
struct Decimator {
  static const int TAPS = 96;
  double h[TAPS], buf[TAPS * 2];
  int os = 1, pos = 0;
  void design(int factor) {
    os = factor < 1 ? 1 : factor;
    double fc = 0.45 / os, sum = 0;   // cutoff as a fraction of the analog rate
    for (int i = 0; i < TAPS; i++) {
      double m = i - (TAPS - 1) / 2.0, x = xm::TAU * fc * m;
      double sinc = m == 0 ? xm::TAU * fc : xm::sin(x) / m;
      double w = 0.42 - 0.5 * xm::cos(xm::TAU * i / (TAPS - 1)) + 0.08 * xm::cos(2 * xm::TAU * i / (TAPS - 1));   // Blackman
      h[i] = sinc * w; sum += h[i];
    }
    for (double& v : h) v /= sum;
    reset();
  }
  void reset() { for (double& v : buf) v = 0; pos = 0; }
  void push(double x) { buf[pos] = x; buf[pos + TAPS] = x; if (++pos >= TAPS) pos = 0; }
  double out() const { double a = 0; const double* b = buf + pos; for (int i = 0; i < TAPS; i++) a += h[i] * b[i]; return a; }
};
}  // namespace xr
