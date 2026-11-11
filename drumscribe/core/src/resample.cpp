// Band-limited sample-rate conversion (for decoders that hand over audio at the source's own rate, e.g. Android's
// MediaCodec, where there is no ffmpeg): a Kaiser-windowed sinc evaluated at the exact output times.
//   stopband 120 dB, flat to 0.4535 x the lower rate (20 kHz when one side is 44.1 kHz), cut-off at its Nyquist.
//   Rational ratios with a small numerator (48k, 96k, 32k, 22.05k, 16k, 8k ... -> 44.1k) use the exact polyphase
//   coefficients; anything else interpolates linearly between 4096 tabulated phases (error < -130 dB).
// Tested by tests/test_resample.cpp (sine sweeps against the analytic signal).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include "ds/modules.h"

namespace ds {

namespace {

double bessel_i0(double x)
{
    double sum = 1, term = 1;
    const double q = x * x / 4;
    for (int k = 1; k < 200; ++k) {
        term *= q / ((double) k * k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

struct Kernel {
    int half = 0;              // taps on each side of the output time (input samples)
    int phases = 0;            // rows in the table
    bool exact = false;        // one row per exact phase (else interpolate between rows)
    std::vector<float> table;  // (phases + 1) x (2 * half)
};

// row for output time i0 + frac: taps k = i0 - half + 1 ... i0 + half, t = frac + half - 1 - j
void fill_row(float* row, int half, double frac, double fc, double beta)
{
    const double pi = 3.14159265358979323846, i0b = bessel_i0(beta);
    double sum = 0;
    std::vector<double> h(2 * half);
    for (int j = 0; j < 2 * half; ++j) {
        const double t = frac + half - 1 - j;
        const double u = t / half;
        if (std::fabs(u) >= 1) { h[j] = 0; continue; }
        const double x = 2 * fc * t;
        const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(pi * x) / (pi * x);
        h[j] = 2 * fc * sinc * bessel_i0(beta * std::sqrt(1 - u * u)) / i0b;
        sum += h[j];
    }
    for (int j = 0; j < 2 * half; ++j) row[j] = (float) (h[j] / sum);  // unity gain at DC for every phase
}

}  // namespace

Signal resample(const Signal& x, int sr_in, int sr_out)
{
    if (sr_in <= 0 || sr_out <= 0) throw std::runtime_error("resample: bad sample rate");
    if (sr_in == sr_out || x.empty()) return x;
    const int64_t g = std::gcd(sr_in, sr_out);
    const int64_t L = sr_out / g, M = sr_in / g;  // output n sits at input time n * M / L

    const double lo = std::min(sr_in, sr_out);
    const double pass = 0.4535 * lo, stop = 0.5 * lo;           // Hz
    const double fc = 0.5 * (pass + stop) / sr_in;               // cut-off, cycles per input sample
    const double dw = 2 * 3.14159265358979323846 * (stop - pass) / sr_in;
    const double A = 120, beta = 0.1102 * (A - 8.7);
    const int N = (int) std::ceil((A - 7.95) / (2.285 * dw));
    Kernel k;
    k.half = N / 2 + 1;
    k.exact = L <= 2048;
    k.phases = k.exact ? (int) L : 4096;
    const int W = 2 * k.half;
    k.table.resize((size_t) (k.phases + 1) * W);
    for (int p = 0; p <= k.phases; ++p) fill_row(&k.table[(size_t) p * W], k.half, (double) p / k.phases, fc, beta);

    const int64_t n_in = (int64_t) x.size();
    const int64_t n_out = (n_in * L + M - 1) / M;
    Signal y((size_t) n_out);
    std::vector<float> co(W);
    for (int64_t n = 0; n < n_out; ++n) {
        const int64_t num = n * M;
        const int64_t i0 = num / L;
        const float* c;
        if (k.exact) {
            c = &k.table[(size_t) (num % L) * W];
        } else {
            const double pos = (double) (num % L) / L * k.phases;
            const int r = std::min((int) pos, k.phases - 1);
            const float w = (float) (pos - r);
            const float* a = &k.table[(size_t) r * W];
            const float* b = a + W;
            for (int j = 0; j < W; ++j) co[j] = a[j] + w * (b[j] - a[j]);
            c = co.data();
        }
        const int64_t k0 = i0 - k.half + 1;
        float acc = 0;
        if (k0 >= 0 && k0 + W <= n_in) {
            const float* s = x.data() + k0;
            for (int j = 0; j < W; ++j) acc += s[j] * c[j];
        } else {
            for (int j = 0; j < W; ++j) {
                const int64_t i = k0 + j;
                if (i >= 0 && i < n_in) acc += x[(size_t) i] * c[j];
            }
        }
        y[(size_t) n] = acc;
    }
    return y;
}

Stereo to_stereo_44k(const float* pcm, size_t frames, int channels, int sr)
{
    if (channels < 1) throw std::runtime_error("no audio channels");
    Stereo s;
    s.l.resize(frames);
    s.r.resize(frames);
    const float c = 0.70710678f;
    for (size_t i = 0; i < frames; ++i) {
        const float* f = pcm + i * channels;
        if (channels == 1) {
            s.l[i] = s.r[i] = f[0];
        } else if (channels == 6) {  // 5.1 (L R C LFE Ls Rs): ITU downmix without the LFE
            s.l[i] = f[0] + c * f[2] + c * f[4];
            s.r[i] = f[1] + c * f[2] + c * f[5];
        } else {
            s.l[i] = f[0];
            s.r[i] = f[1];
        }
    }
    if (sr != kSR) {
        s.l = resample(s.l, sr, kSR);
        s.r = resample(s.r, sr, kSR);
    }
    return s;
}

}  // namespace ds
