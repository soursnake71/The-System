// resample / to_stereo_44k / load_wav against exact ground truth: exponential sine sweeps generated analytically at the
// source rate, converted to 44.1 kHz, compared with the same sweep evaluated analytically at the output times.
// SNR over the whole sweep and in the worst of 16 consecutive slices (each slice covers a part of the band).
// Also: rejection of a tone above the output Nyquist (aliasing), output length, the interpolated-phase path (an odd
// rate), mono/5.1 to stereo, and WAV round trips (float and 16-bit PCM).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include "ds/modules.h"

using namespace ds;
static int fails = 0, checks = 0;
static void check(const std::string& what, bool ok, const std::string& detail = "")
{
    ++checks;
    if (!ok) ++fails;
    std::printf("  %-58s %s %s\n", what.c_str(), ok ? "ok" : "FAIL", detail.c_str());
}

static const double PI = 3.14159265358979323846;

struct Sweep {
    double f1, f2, T, A = 0.5;
    double at(double t) const
    {
        if (t < 0 || t > T) return 0;
        const double K = std::log(f2 / f1);
        return A * std::sin(2 * PI * f1 * T / K * (std::exp(K * t / T) - 1));
    }
};

// SNR (dB) of y (at 44.1 kHz) against the sweep, skipping 50 ms at each end; worst slice in *worst
static double sweep_snr(const Signal& y, const Sweep& s, double* worst)
{
    const long a = (long) (0.05 * kSR), b = (long) ((s.T - 0.05) * kSR);
    double sig = 0, err = 0;
    *worst = 1e9;
    const int nsl = 16;
    for (int k = 0; k < nsl; ++k) {
        double ss = 0, ee = 0;
        for (long i = a + (b - a) * k / nsl; i < a + (b - a) * (k + 1) / nsl; ++i) {
            const double r = s.at((double) i / kSR), e = y[i] - r;
            ss += r * r;
            ee += e * e;
        }
        sig += ss;
        err += ee;
        *worst = std::min(*worst, 10 * std::log10(ss / std::max(ee, 1e-30)));
    }
    return 10 * std::log10(sig / std::max(err, 1e-30));
}

static void sweep_case(int sr, double f2, double min_db)
{
    Sweep s{20, f2, 6.0};
    Signal x((size_t) (s.T * sr));
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float) s.at((double) i / sr);
    Signal y = resample(x, sr, kSR);
    const long want = (long) (((int64_t) x.size() * kSR + sr - 1) / sr);
    check("length " + std::to_string(sr) + " -> 44100", (long) y.size() == want, std::to_string(y.size()));
    double worst;
    const double snr = sweep_snr(y, s, &worst);
    char d[160];
    std::snprintf(d, sizeof d, "SNR %.1f dB, worst slice %.1f dB (20 Hz - %.0f Hz)", snr, worst, f2);
    check("sweep " + std::to_string(sr) + " -> 44100 > " + std::to_string((int) min_db) + " dB", worst > min_db, d);
}

static void put(std::ofstream& o, uint32_t v, int n) { o.write(reinterpret_cast<const char*>(&v), n); }

int main()
{
    std::printf("resample (exact ground truth: analytic sweeps)\n");
    sweep_case(48000, 20000, 90);   // the common one (phones, video)
    sweep_case(96000, 20000, 90);
    sweep_case(88200, 20000, 90);
    sweep_case(32000, 14500, 90);   // 0.4535 x 32 kHz
    sweep_case(22050, 10000, 90);   // upsampling
    sweep_case(44056, 19900, 90);   // odd rate: interpolated phases

    {   // a 23 kHz tone at 48 kHz has nowhere to go at 44.1 kHz: it must vanish, not fold to 21.1 kHz
        Signal x(48000 * 2);
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.5 * std::sin(2 * PI * 23000.0 * i / 48000));
        Signal y = resample(x, 48000, kSR);
        double e = 0;
        long n = 0;
        for (size_t i = 4410; i + 4410 < y.size(); ++i, ++n) e += (double) y[i] * y[i];
        const double db = 10 * std::log10(e / n / 0.125);
        check("23 kHz tone 48k -> 44.1k rejected > 100 dB", db < -100, std::to_string(db) + " dB");
    }
    {
        Signal x(1000, 0.25f);
        check("same rate: unchanged", resample(x, kSR, kSR) == x);
    }
    {   // mono doubled, 5.1 downmixed, rate converted
        std::vector<float> mono(48000);
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = (float) (0.3 * std::sin(2 * PI * 1000.0 * i / 48000));
        Stereo s = to_stereo_44k(mono.data(), mono.size(), 1, 48000);
        bool same = s.l == s.r && s.size() == 44100;
        double e = 0;
        for (size_t i = 2000; i + 2000 < s.size(); ++i) e = std::max(e, std::fabs(s.l[i] - 0.3 * std::sin(2 * PI * 1000.0 * i / kSR)));
        check("mono 48k -> stereo 44.1k", same && e < 1e-5, "max error " + std::to_string(e));
        std::vector<float> six(6 * 100, 0.f);
        for (int i = 0; i < 100; ++i) { six[i * 6 + 0] = 0.1f; six[i * 6 + 2] = 0.2f; six[i * 6 + 3] = 0.9f; six[i * 6 + 5] = 0.3f; }
        Stereo d = to_stereo_44k(six.data(), 100, 6, kSR);
        check("5.1 downmix (no LFE)", std::fabs(d.l[50] - (0.1f + 0.70710678f * 0.2f)) < 1e-6 &&
                                          std::fabs(d.r[50] - 0.70710678f * (0.2f + 0.3f)) < 1e-6);
    }
    {   // WAV round trips
        Stereo s;
        for (int i = 0; i < 5000; ++i) { s.l.push_back((float) std::sin(i * 0.01)); s.r.push_back((float) std::cos(i * 0.013) * 0.5f); }
        save_wav("test_resample_f32.wav", s);
        int sr = 0;
        Stereo r = load_wav("test_resample_f32.wav", &sr);
        check("float WAV round trip exact", r.l == s.l && r.r == s.r && sr == kSR);
        std::ofstream o("test_resample_i16.wav", std::ios::binary);
        o.write("RIFF", 4); put(o, 36 + 4 * 3, 4); o.write("WAVE", 4);
        o.write("fmt ", 4); put(o, 16, 4); put(o, 1, 2); put(o, 2, 2); put(o, 48000, 4); put(o, 48000 * 4, 4); put(o, 4, 2); put(o, 16, 2);
        o.write("data", 4); put(o, 12, 4);
        const int16_t v[6] = {16384, -16384, 32767, -32768, 0, 1};
        o.write(reinterpret_cast<const char*>(v), 12);
        o.close();
        Stereo q = load_wav("test_resample_i16.wav", &sr);
        check("16-bit PCM WAV", q.size() == 3 && sr == 48000 && q.l[0] == 0.5f && q.r[0] == -0.5f && q.r[1] == -1.f &&
                                    q.r[2] == 1.f / 32768);
    }
    std::printf("\n%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
