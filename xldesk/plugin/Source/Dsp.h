// XL-Desk DSP building blocks, a line-for-line port of xldesk/dsp/console.js (the web engine), so both sound the same.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "Spec.generated.h"

namespace xl {
constexpr double PI = 3.14159265358979323846;
inline double dB (double d)  { return std::isinf (d) && d < 0 ? 0.0 : std::pow (10.0, d / 20.0); }
inline double dBu (double d) { return std::pow (10.0, (d - xlspec::REF) / 20.0); }
inline double toDbu (double a) { return 8.685889638 * std::log (a + 1e-12) + xlspec::REF; }

// xorshift noise, uniform -1..1
struct Rng { uint32_t s = 0x9e3779b9u; float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s / 2147483648.0f; } };
extern thread_local Rng rng;

// SuperAnalogue stages are clean to the rails: linear to +26 dBu, then a short knee to about +27.5 dBu
inline float rail (float x)
{
    static const float R = (float) dBu (26.0), K = .25f;
    const float a = std::abs (x);
    if (a <= R) return x;
    const float y = R + K * std::tanh ((a - R) / K);
    return x < 0 ? -y : y;
}
inline float ratanh (float x) { return x > 3 ? 1.f : x < -3 ? -1.f : x * (27 + x * x) / (27 + 9 * x * x); }

struct BQ
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    BQ& set (double B0, double B1, double B2, double A0, double A1, double A2) { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; return *this; }
    inline float run (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
    void reset() { z1 = z2 = 0; }
    BQ& lp (double f, double q, double fs) { const double w = 2 * PI * f / fs, c = std::cos (w), a = std::sin (w) / (2 * q); return set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a); }
    BQ& hp (double f, double q, double fs) { const double w = 2 * PI * f / fs, c = std::cos (w), a = std::sin (w) / (2 * q); return set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a); }
    BQ& lp1 (double f, double fs) { const double K = std::tan (PI * f / fs); return set (K, K, 0, K + 1, K - 1, 0); }
    BQ& hp1 (double f, double fs) { const double K = std::tan (PI * f / fs); return set (1, -1, 0, K + 1, K - 1, 0); }
    BQ& peak (double f, double q, double g, double fs) { const double A = std::pow (10.0, g / 40), w = 2 * PI * f / fs, c = std::cos (w), a = std::sin (w) / (2 * q); return set (1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A); }
    // first-order (6 dB/octave) shelves, cut mirrors boost
    BQ& lshelf1 (double f, double g, double fs) { const double K = std::tan (PI * f / fs), V = std::pow (10.0, g / 20), a = g >= 0 ? V : 1, b = g >= 0 ? 1 : 1 / V; return set (1 + a * K, a * K - 1, 0, 1 + b * K, b * K - 1, 0); }
    BQ& hshelf1 (double f, double g, double fs) { const double K = std::tan (PI * f / fs), V = std::pow (10.0, g / 20), a = g >= 0 ? V : 1, b = g >= 0 ? 1 : 1 / V; return set (a + K, K - a, 0, b + K, K - b, 0); }
};

/* VHD preamp stage: clean SuperAnalogue gain, or the Variable Harmonic Drive blend of 2nd (asymmetric) and 3rd
   (symmetric) harmonics that grows with the level hitting the stage. 2x oversampled. */
struct VHD
{
    static constexpr int OS = 2;
    BQ up[3], dn[3];
    double dcA = 0, dc = 0, dcx = 0, th1 = 0, th2 = 0, thA = 0, thB = 0;
    void prepare (double fs)
    {
        const double qs[3] = { .5176, .7071, 1.9319 };
        for (int i = 0; i < 3; ++i) { up[i].lp (20000, qs[i], fs * OS); dn[i].lp (20000, qs[i], fs * OS); }
        dcA = 1 / (1 + 2 * PI * 8 / fs); thA = std::exp (-1 / (.004 * fs)); thB = std::exp (-1 / (.09 * fs));
    }
    void thump() { th1 += dBu (4); th2 += dBu (4); }   // +48 V switching transient
    void run (float* b, int N, float gain, bool vhd, float drive, float noise)
    {
        const float k2 = .5f, k3 = 2.4f, bl = drive;
        for (int i = 0; i < N; ++i)
        {
            float x = (b[i] + noise * rng.next()) * gain;
            if (vhd)
            {
                float y = 0;
                for (int o = 0; o < OS; ++o)
                {
                    float u = up[2].run (up[1].run (up[0].run (o == 0 ? x * OS : 0.f)));
                    const float t = ratanh (u), e = t + k2 * t * t, od = ratanh (k3 * u) / k3;
                    u = (1 - bl) * e + bl * od;
                    const float v = dn[2].run (dn[1].run (dn[0].run (u)));
                    if (o == 0) y = v;
                }
                const double h = dcA * (dc + y - dcx); dcx = y; dc = h; x = (float) h;
            }
            th1 *= thA; th2 *= thB;
            b[i] = rail (x + (float) (th2 - th1));
        }
    }
};

/* True-peak detector for the bus bargraphs: 4x windowed-sinc interpolation (BS.1770 style) so the meter reads the
   analogue bus, not just the samples. */
struct TruePeak
{
    static constexpr int TAPS = 8, PH = 4;
    float z[TAPS] {};
    static const float (&table())[PH][TAPS];
    float run (const float* b, int N);
};
} // namespace xl
