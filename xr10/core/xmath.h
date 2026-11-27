// Omega XR10: self-contained math so the same engine builds natively (VST3) and as freestanding WebAssembly
// (Android / web) with no C library, and renders the same samples on both.
#pragma once
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef short s16;
typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;

#if defined(_MSC_VER) && !defined(__clang__)
#include <cmath>
#include <cstring>
#define XM_FLOOR(x) std::floor(x)
#define XM_SQRT(x) std::sqrt(x)
template <class T, class F> inline T xm_bitcast(const F& f) { T t; std::memcpy(&t, &f, sizeof t); return t; }
#define XM_BITCAST(T, v) xm_bitcast<T>(v)
#else
#define XM_FLOOR(x) __builtin_floor(x)
#define XM_SQRT(x) __builtin_sqrt(x)
#define XM_BITCAST(T, v) __builtin_bit_cast(T, v)
#endif

namespace xm {
static const double PI = 3.14159265358979323846;
static const double TAU = 6.28318530717958647692;
inline double floor(double x) { return XM_FLOOR(x); }
inline double sqrt(double x) { return XM_SQRT(x); }
inline double fabs(double x) { return x < 0 ? -x : x; }
inline double clamp(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline int clampi(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
inline double sin(double x) {
  x -= TAU * floor(x / TAU + 0.5);              // [-pi, pi]
  if (x > PI / 2) x = PI - x; else if (x < -PI / 2) x = -PI - x;
  double x2 = x * x;
  return x * (1 + x2 * (-1.0 / 6 + x2 * (1.0 / 120 + x2 * (-1.0 / 5040 + x2 * (1.0 / 362880 + x2 * (-1.0 / 39916800 + x2 / 6227020800.0))))));
}
inline double cos(double x) { return sin(x + PI / 2); }
inline double exp(double x) {
  if (x < -700) return 0;
  if (x > 700) x = 700;
  double n = floor(x * 1.4426950408889634 + 0.5);
  double r = x - n * 0.6931471805599453;
  double p = 1, t = 1;
  for (int i = 1; i < 14; i++) { t *= r / i; p += t; }
  int e = (int)n;
  while (e > 1000) { p *= 1.0715086071862673e301; e -= 1000; }   // 2^1000
  while (e < -1000) { p *= 9.332636185032189e-302; e += 1000; }  // 2^-1000
  u64 bits = (u64)(e + 1023) << 52;
  return p * XM_BITCAST(double, bits);
}
inline double log(double x) {
  if (x <= 0) return -745;
  u64 b = XM_BITCAST(u64, x);
  int e = (int)((b >> 52) & 0x7ff) - 1023;
  b = (b & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL;
  double m = XM_BITCAST(double, b);
  if (m > 1.4142135623730951) { m *= 0.5; e++; }
  double t = (m - 1) / (m + 1), t2 = t * t, s = 0, tp = t;
  for (int i = 1; i < 30; i += 2) { s += tp / i; tp *= t2; }
  return 2 * s + e * 0.6931471805599453;
}
inline double pow(double a, double b) { return a <= 0 ? 0 : exp(b * log(a)); }
inline double pow2(double x) { return exp(x * 0.6931471805599453); }
inline double tanh(double x) { if (x > 20) return 1; if (x < -20) return -1; double e = exp(2 * x); return (e - 1) / (e + 1); }
inline float fastTanh(float x) { if (x > 3) return 1; if (x < -3) return -1; float x2 = x * x; return x * (27 + x2) / (27 + 9 * x2); }

struct Rng {
  u32 s = 22222;
  void seed(u32 v) { s = v ? v : 1; }
  u32 next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  double bi() { return (next() >> 8) * (2.0 / 16777216.0) - 1.0; }   // [-1, 1)
  double uni() { return (next() >> 8) * (1.0 / 16777216.0); }       // [0, 1)
};

// RBJ biquad, double precision (used for rendering the sounds and in the voice tone filter)
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void set(int type, double f, double q, double sr, double gainDb = 0) {
    if (f > sr * 0.49) f = sr * 0.49;
    if (f < 10) f = 10;
    double w = TAU * f / sr, cs = cos(w), sn = sin(w), al = sn / (2 * q);
    double A = pow(10, gainDb / 40), a0;
    switch (type) {
      case 0: b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = b0; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;          // LP
      case 1: b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = b0; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;       // HP
      case 2: b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;                        // BP (0 dB peak)
      default: b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A; // peak
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
  }
  double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
  void reset() { z1 = z2 = 0; }
};
}  // namespace xm
