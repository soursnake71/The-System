// Omega XR10: the 64 sounds, built by synthesis (no factory samples are included) and stored like the
// hardware's PCM ROM: mono, 44.1 kHz, 16-bit. Each recipe aims at the character of the sound named on the
// XR10's panel list.
#pragma once
#include "xmath.h"

namespace xr {
static const int NSOUNDS = 64;
// the machine's sample rate (12 MHz / 360 = 33.33 kHz, an estimate: see MACHINE_RATE in xr10.h); ROM_HZ is it rounded, for files
static const double ROM_RATE = 12000000.0 / 360.0;
static const int ROM_HZ = 33333;
static const char* const SOUND_NAMES[NSOUNDS] = {
  "KICK1", "KICK2", "KICK3", "KICK4", "KICK5", "KICK6", "KICK7", "KICK8", "KICK9", "KICK10",
  "SNARE1", "SNARE2", "SNARE3", "SNARE4", "SNARE5", "SNARE6", "SNARE7", "SNARE8", "SNARE9", "SNARE10",
  "RIMSHOT1", "RIMSHOT2", "HAT CL", "HAT MD", "HAT OP",
  "TOM1 L", "TOM1 M", "TOM1 H", "TOM2 L", "TOM2 M", "TOM2 H", "TOM3 L", "TOM3 M", "TOM3 H", "TOM4 L", "TOM4 M", "TOM4 H",
  "CRASH1", "CRASH2", "RIDE", "CHOKE", "CLAPS", "FINGER", "CONGA1", "CONGA2", "CONGA3", "CONGA4", "VIBRASLP",
  "GUIRO1", "GUIRO2", "CABASA", "TAMBRINE", "HITS", "TRIANG L", "TRIANG H", "COWBELL", "CLAVES", "WHISTLE",
  "SQUEAK L", "SQUEAK H", "TIMBAL L", "TIMBAL H", "AGOGO L", "AGOGO H"};
enum {
  KICK1 = 0, SNARE1 = 10, RIM1 = 20, RIM2, HATCL, HATMD, HATOP, TOM1L, TOM1M, TOM1H, TOM2L, TOM2M, TOM2H, TOM3L, TOM3M, TOM3H,
  TOM4L, TOM4M, TOM4H, CRASH1, CRASH2, RIDE, CHOKE, CLAPS, FINGER, CONGA1, CONGA2, CONGA3, CONGA4, VIBRA, GUIRO1, GUIRO2,
  CABASA, TAMB, HITS, TRIL, TRIH, COWBELL, CLAVES, WHISTLE, SQKL, SQKH, TIMBL, TIMBH, AGOGOL, AGOGOH
};
// length of each sound in ms (the ROM budget)
inline int soundLengthMs(int id) {
  if (id < 10) return id == 8 ? 1500 : 1000;
  if (id < 20) return id == 19 ? 1500 : (id == 17 ? 700 : 600);
  if (id <= RIM2) return 150;
  if (id == HATCL) return 160;
  if (id == HATMD) return 350;
  if (id == HATOP) return 900;
  if (id <= TOM4H) return 900;
  if (id == CRASH1 || id == CRASH2) return 2600;
  if (id == RIDE) return 2600;
  if (id == CHOKE) return 300;
  if (id == VIBRA) return 1300;
  if (id == TRIL || id == TRIH) return 1800;
  if (id == HITS) return 800;
  if (id == WHISTLE) return 550;
  return 500;
}

struct Render {
  float* o;
  int n;
  double sr = ROM_RATE;
  xm::Rng rng;
  double t(int i) const { return i / sr; }
  static double env(double tt, double decay) { return xm::exp(-tt / decay); }
  static double att(double tt, double a) { return a <= 0 ? 1 : (tt < a ? tt / a : 1); }

  // pitched body: sine (plus a share of a 2nd partial) swept from f0 down to f1
  void body(double gain, double f0, double f1, double pdec, double adec, double partial = 0, double pr = 1.5, double drive = 1) {
    double ph = 0, ph2 = 0;
    for (int i = 0; i < n; i++) {
      double tt = t(i), f = f1 + (f0 - f1) * env(tt, pdec);
      ph += f / sr; ph2 += f * pr / sr;
      ph -= xm::floor(ph); ph2 -= xm::floor(ph2);
      double s = xm::sin(xm::TAU * ph) + partial * xm::sin(xm::TAU * ph2) * env(tt, adec * 0.5);
      s *= env(tt, adec) * att(tt, 0.0008);
      if (drive != 1) s = xm::tanh(s * drive) / xm::tanh(drive);
      o[i] += (float)(gain * s);
    }
  }
  // filtered noise: type 0 LP, 1 HP, 2 BP; second filter optional HP
  void noise(double gain, int type, double f, double q, double adec, double a = 0, double hp = 0, double gateAt = 0) {
    xm::Biquad b, h;
    b.set(type, f, q, sr);
    if (hp > 0) h.set(1, hp, 0.707, sr);
    for (int i = 0; i < n; i++) {
      double tt = t(i), s = b.run(rng.bi());
      if (hp > 0) s = h.run(s);
      double e = env(tt, adec) * att(tt, a);
      if (gateAt > 0 && tt > gateAt) e *= env(tt - gateAt, 0.006);
      o[i] += (float)(gain * s * e);
    }
  }
  // metallic: square waves at inharmonic ratios, band-passed (808/909 style cymbals and hats)
  void metal(double gain, const double* fr, int nf, double scale, double bpf, double hpf, double adec, double a = 0, double bend = 0) {
    double ph[12] = {0};
    xm::Biquad b1, b2, h1;
    b1.set(2, bpf, 0.9, sr); b2.set(2, bpf * 1.3, 0.9, sr); h1.set(1, hpf, 0.707, sr);
    for (int i = 0; i < n; i++) {
      double tt = t(i), s = 0;
      for (int k = 0; k < nf; k++) { ph[k] += fr[k] * scale / sr; ph[k] -= xm::floor(ph[k]); s += ph[k] < 0.5 ? 1 : -1; }
      s /= nf;
      s = h1.run(b1.run(s) + 0.5 * b2.run(s));
      double e = bend > 0 ? (0.6 * env(tt, adec * 0.25) + 0.4 * env(tt, adec)) : env(tt, adec);
      o[i] += (float)(gain * s * e * att(tt, a));
    }
  }
  // decaying sine partials (bells, triangle, agogo)
  void partials(double gain, double f, const double* rat, const double* amp, const double* dec, int np, double a = 0.0005) {
    for (int k = 0; k < np; k++) {
      double ph = rng.uni(), fk = f * rat[k];
      if (fk > sr * 0.45) continue;
      for (int i = 0; i < n; i++) {
        ph += fk / sr; ph -= xm::floor(ph);
        o[i] += (float)(gain * amp[k] * xm::sin(xm::TAU * ph) * env(t(i), dec[k]) * att(t(i), a));
      }
    }
  }
  // a burst of noise through a resonator at time at
  void burst(double gain, double at, double f, double q, double dec) {
    xm::Biquad b;
    b.set(2, f, q, sr);
    int s0 = (int)(at * sr);
    for (int i = s0; i < n; i++) {
      double tt = (i - s0) / sr;
      o[i] += (float)(gain * b.run(rng.bi()) * env(tt, dec));
    }
  }
  void click(double gain, double f, double dec) { burst(gain, 0, f, 0.8, dec); }
  void lowpass(double f) { xm::Biquad b; b.set(0, f, 0.707, sr); for (int i = 0; i < n; i++) o[i] = (float)b.run(o[i]); }
  void highpass(double f) { xm::Biquad b; b.set(1, f, 0.707, sr); for (int i = 0; i < n; i++) o[i] = (float)b.run(o[i]); }
  // a cheap room: a few feedback-free taps of the dry sound, lowpassed
  void room(double mix, double size, double damp) {
    static const double taps[6] = {0.0113, 0.0197, 0.0269, 0.0351, 0.0433, 0.0577};
    for (int i = n - 1; i >= 0; i--) {
      double s = 0;
      for (int k = 0; k < 6; k++) { int d = (int)(taps[k] * size * sr); if (i - d >= 0) s += o[i - d] * (1 - k * 0.12); }
      o[i] += (float)(mix * s / 6);
    }
    (void)damp;
  }
  // 12-bit, 22 kHz (the cheap-sampler flavour of SNARE9)
  void crush(int bits, int hold) {
    double q = (double)(1 << (bits - 1));
    float last = 0;
    for (int i = 0; i < n; i++) { if (i % hold == 0) last = (float)(xm::floor(o[i] * q + 0.5) / q); o[i] = last; }
  }
};

static const double HAT_FR[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};   // the 808's six square oscillators
static const double CYM_FR[8] = {263.0, 400.0, 421.0, 474.0, 587.0, 845.0, 1093.0, 1340.0};

// renders sound id into out (zeroed here); returns its length in samples
inline int renderSound(int id, float* out, int maxLen) {
  Render r;
  r.n = (int)(soundLengthMs(id) * ROM_RATE / 1000);
  if (r.n > maxLen) r.n = maxLen;
  r.o = out;
  r.rng.seed(1000 + id * 7919);
  for (int i = 0; i < r.n; i++) out[i] = 0;
  if (id < 10) {  // kicks: start Hz, end Hz, pitch decay, amp decay, beater click, drive
    static const double K[10][6] = {
      {120, 54, 0.030, 0.34, 0.45, 1.6}, {145, 60, 0.022, 0.24, 0.70, 1.8}, {100, 47, 0.040, 0.50, 0.30, 1.3},
      {165, 66, 0.016, 0.17, 0.80, 2.2}, {115, 52, 0.030, 0.40, 0.40, 1.5}, {150, 48, 0.050, 0.85, 0.12, 1.0},
      {300, 55, 0.012, 0.42, 0.50, 1.4}, {210, 50, 0.025, 0.60, 0.40, 4.0}, {92, 41, 0.060, 1.20, 0.10, 1.1},
      {260, 72, 0.010, 0.12, 0.30, 1.0}};
    const double* k = K[id];
    r.body(0.9, k[0], k[1], k[2], k[3], id < 5 ? 0.25 : 0, 1.6, k[5]);
    r.click(k[4], id < 5 ? 3200 : 4500, 0.004);
    if (id < 5) r.noise(0.12, 0, 900, 0.7, 0.05);    // the skin
    if (id == 4) r.room(0.9, 1.6, 0);
    if (id == 6) r.noise(0.25, 1, 4000, 0.7, 0.012);   // 909 attack noise
  } else if (id < 20) {  // snares: tone1, tone2, body decay, noise level, noise decay, noise BP Hz, noise HP Hz
    static const double S[10][7] = {
      {190, 330, 0.10, 0.75, 0.20, 5000, 1500}, {225, 385, 0.07, 0.70, 0.12, 6000, 2000}, {160, 280, 0.13, 0.80, 0.30, 3800, 900},
      {265, 470, 0.06, 0.75, 0.16, 7000, 2500}, {210, 360, 0.09, 0.85, 0.18, 4500, 1200}, {180, 330, 0.11, 0.45, 0.16, 6500, 2000},
      {200, 350, 0.08, 0.80, 0.22, 7500, 1800}, {195, 340, 0.10, 0.80, 0.70, 4800, 1200}, {205, 345, 0.09, 0.75, 0.20, 5200, 1400},
      {185, 320, 0.11, 0.80, 0.25, 4500, 1000}};
    const double* s = S[id - 10];
    r.body(0.55, s[0] * 1.25, s[0], 0.012, s[2], 0.7, s[1] / s[0], 1.2);
    r.noise(s[3], 2, s[5], 0.6, s[4], 0.0005, s[6], id == 17 ? 0.32 : 0);
    if (id == 14) r.click(0.6, 2500, 0.006);                      // rimshot crack
    if (id == 17) r.room(1.4, 2.2, 0);                             // gated room
    if (id == 18) r.crush(12, 2);
    if (id == 19) { r.room(1.2, 3.5, 0); r.noise(0.25, 2, 3000, 0.5, 0.45, 0.01, 600); }  // big room tail
  } else if (id == RIM1) {
    r.burst(1.0, 0, 1700, 12, 0.018); r.burst(0.6, 0, 480, 6, 0.012);
  } else if (id == RIM2) {
    r.burst(1.0, 0, 950, 8, 0.03); r.burst(0.7, 0, 2100, 10, 0.02); r.click(0.3, 5000, 0.002);
  } else if (id >= HATCL && id <= HATOP) {
    double dec = id == HATCL ? 0.035 : (id == HATMD ? 0.11 : 0.42);
    r.metal(1.0, HAT_FR, 6, 1.0, 10000, 7000, dec, 0.0003);
    r.noise(0.35, 1, 8000, 0.7, dec * 0.8);
  } else if (id <= TOM4H) {
    int set = (id - TOM1L) / 3, pos = (id - TOM1L) % 3;
    static const double base[4][3] = {{88, 118, 158}, {108, 148, 196}, {82, 112, 150}, {74, 100, 134}};
    double f = base[set][pos];
    if (set == 0) { r.body(0.9, f * 1.45, f, 0.05, 0.42, 0.35, 1.52, 1.4); r.noise(0.15, 2, 1500, 0.8, 0.05); r.click(0.25, 3000, 0.004); }
    if (set == 1) { r.body(0.9, f * 1.6, f, 0.03, 0.30, 0.3, 1.47, 1.8); r.click(0.35, 3500, 0.004); }
    if (set == 2) { r.body(0.9, f * 2.6, f, 0.12, 0.55, 0, 1, 1.2); r.noise(0.25, 0, 3000, 0.7, 0.08); }        // Simmons-style sweep
    if (set == 3) { r.body(0.95, f * 1.25, f, 0.04, 0.50, 0, 1, 1.0); }                                          // 808-style
  } else if (id == CRASH1 || id == CRASH2 || id == CHOKE) {
    double sc = id == CRASH2 ? 1.25 : 1.0, dec = id == CRASH2 ? 1.0 : 1.3;
    r.metal(0.9, CYM_FR, 8, sc * 1.6, 7000, 3500, dec, 0.001, 1);
    r.noise(0.5, 1, 6000, 0.7, dec * 0.8, 0.001);
    if (id == CHOKE) for (int i = 0; i < r.n; i++) { double tt = r.t(i); if (tt > 0.12) out[i] *= (float)Render::env(tt - 0.12, 0.02); }
  } else if (id == RIDE) {
    r.metal(0.6, CYM_FR, 8, 2.1, 6000, 4000, 1.6, 0.0005);
    static const double rr[4] = {1, 2.4, 3.9, 5.2}, ra[4] = {0.35, 0.2, 0.12, 0.08}, rd[4] = {1.2, 0.8, 0.5, 0.4};
    r.partials(1, 620, rr, ra, rd, 4);
    r.noise(0.12, 1, 7000, 0.7, 0.9);
  } else if (id == CLAPS) {
    static const double at[4] = {0, 0.010, 0.021, 0.031};
    for (int k = 0; k < 4; k++) r.burst(k == 3 ? 0.9 : 0.8, at[k], 1150, 1.6, k == 3 ? 0.16 : 0.007);
    r.highpass(500);
  } else if (id == FINGER) {
    r.burst(1.0, 0, 2800, 3, 0.022); r.burst(0.5, 0, 1200, 4, 0.01);
  } else if (id >= CONGA1 && id <= CONGA4) {
    static const double cf[4] = {380, 330, 225, 170}, cd[4] = {0.10, 0.22, 0.28, 0.32};
    int k = id - CONGA1;
    r.body(0.9, cf[k] * 1.12, cf[k], 0.02, cd[k], 0.25, 1.6, 1.3);
    if (k == 0) r.burst(0.6, 0, 2200, 1.5, 0.03); else r.click(0.2, 2500, 0.004);
  } else if (id == VIBRA) {
    double tt = 0.0, gap = 0.028; int k = 0;
    while (tt < 1.1 && k < 60) { r.burst(0.7 * Render::env(tt, 0.6), tt, 3100, 18, 0.015); r.burst(0.4 * Render::env(tt, 0.6), tt, 5200, 20, 0.01); tt += gap; gap *= 1.03; k++; }
    r.burst(0.6, 0, 800, 3, 0.05);
  } else if (id == GUIRO1 || id == GUIRO2) {
    int strokes = id == GUIRO1 ? 14 : 6;
    double span = id == GUIRO1 ? 0.34 : 0.12;
    for (int k = 0; k < strokes; k++) r.burst(0.6 + 0.3 * (k % 2), span * k / strokes, 3300, 2.5, 0.009);
    r.highpass(1200);
  } else if (id == CABASA) {
    r.noise(1.0, 1, 6500, 0.7, 0.07, 0.006);
  } else if (id == TAMB) {
    static const double tr[5] = {1, 1.31, 1.62, 1.91, 2.3}, ta[5] = {0.3, 0.25, 0.25, 0.2, 0.15}, td[5] = {0.18, 0.15, 0.14, 0.12, 0.1};
    static const double hits[3] = {0, 0.009, 0.017};
    for (int h = 0; h < 3; h++) r.burst(0.5, hits[h], 7500, 3, 0.09);
    r.partials(0.5, 5400, tr, ta, td, 5);
    r.highpass(3000);
  } else if (id == HITS) {  // orchestra stab: detuned saws on a C major chord, brassy filter sweep
    static const double notes[7] = {130.81, 164.81, 196.0, 261.63, 329.63, 392.0, 523.25};
    double ph[14] = {0};
    xm::Biquad lp;
    for (int i = 0; i < r.n; i++) {
      double tt = r.t(i), s = 0;
      for (int k = 0; k < 14; k++) { double f = notes[k % 7] * (k < 7 ? 1.004 : 0.996); ph[k] += f / r.sr; ph[k] -= xm::floor(ph[k]); s += 2 * ph[k] - 1; }
      if (i % 32 == 0) lp.set(0, 600 + 7000 * Render::env(tt, 0.12), 1.2, r.sr);
      out[i] += (float)(lp.run(s / 7) * Render::env(tt, 0.28) * Render::att(tt, 0.004));
    }
    r.noise(0.3, 2, 2000, 0.6, 0.05);
  } else if (id == TRIL || id == TRIH) {
    static const double rr[4] = {1, 2.76, 5.40, 8.93}, ra[4] = {0.6, 0.5, 0.35, 0.25}, rd[4] = {1.4, 1.0, 0.7, 0.5};
    r.partials(1, id == TRIL ? 1400 : 2150, rr, ra, rd, 4);
  } else if (id == COWBELL) {
    static const double fr[2] = {540, 800};
    double ph[2] = {0, 0};
    xm::Biquad bp;
    bp.set(2, 1100, 1.2, r.sr);
    for (int i = 0; i < r.n; i++) {
      double s = 0;
      for (int k = 0; k < 2; k++) { ph[k] += fr[k] / r.sr; ph[k] -= xm::floor(ph[k]); s += ph[k] < 0.5 ? 1 : -1; }
      double tt = r.t(i);
      out[i] += (float)(bp.run(s) * (0.65 * Render::env(tt, 0.015) + 0.35 * Render::env(tt, 0.22)));
    }
  } else if (id == CLAVES) {
    static const double rr[2] = {1, 2.6}, ra[2] = {1, 0.2}, rd[2] = {0.045, 0.02};
    r.partials(1, 2450, rr, ra, rd, 2);
  } else if (id == WHISTLE) {
    double ph = 0;
    for (int i = 0; i < r.n; i++) {
      double tt = r.t(i), f = 2650 + 170 * xm::sin(xm::TAU * 34 * tt);
      ph += f / r.sr; ph -= xm::floor(ph);
      double e = Render::att(tt, 0.012) * (tt > 0.45 ? Render::env(tt - 0.45, 0.02) : 1);
      out[i] += (float)(0.8 * xm::sin(xm::TAU * ph) * e);
    }
    r.noise(0.08, 2, 2650, 2, 0.4, 0.01, 0, 0.45);
  } else if (id == SQKL || id == SQKH) {
    double f0 = id == SQKL ? 380 : 650, f1 = id == SQKL ? 700 : 1150, ph = 0;
    for (int i = 0; i < r.n; i++) {
      double tt = r.t(i), f = f0 + (f1 - f0) * (1 - Render::env(tt, 0.06));
      ph += f / r.sr; ph -= xm::floor(ph);
      out[i] += (float)(0.9 * xm::sin(xm::TAU * ph) * Render::att(tt, 0.01) * Render::env(tt, 0.13));
    }
  } else if (id == TIMBL || id == TIMBH) {
    double f = id == TIMBL ? 330 : 480;
    r.body(0.8, f * 1.05, f, 0.02, 0.32, 0.45, 2.21, 1.3);
    r.burst(0.45, 0, 4200, 2, 0.02);
  } else if (id == AGOGOL || id == AGOGOH) {
    static const double rr[3] = {1, 2.03, 3.32}, ra[3] = {0.8, 0.4, 0.2}, rd[3] = {0.35, 0.2, 0.12};
    r.partials(1, id == AGOGOL ? 700 : 1010, rr, ra, rd, 3);
  }
  // normalize to just under full scale
  double peak = 0;
  for (int i = 0; i < r.n; i++) { double a = xm::fabs(out[i]); if (a > peak) peak = a; }
  if (peak > 0) { float g = (float)(0.95 / peak); for (int i = 0; i < r.n; i++) out[i] *= g; }
  // trim trailing silence (below -72 dB)
  int len = r.n;
  while (len > 64 && xm::fabs(out[len - 1]) < 0.00025) len--;
  return len;
}
}  // namespace xr
