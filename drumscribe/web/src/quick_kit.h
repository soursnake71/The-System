// Drum Scribe in the browser, "Quick" mode: no DrumSep. The Demucs drum stem is split by frequency into the stems the
// drum detector (src/drums.cpp, unchanged) reads: kick = below 120 Hz, snare = 200 Hz - 5 kHz, hi-hat = above 7 kHz;
// toms, ride and crash stay silent (their hits land in those three bands, or are missed). Zero-phase filters
// (Butterworth biquads run forwards then backwards), so the attacks don't move. Less accurate than DrumSep: see
// drumscribe/README.md for the measured cost.
#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "ds/kit.h"
#include "ds/types.h"

namespace ds::quick {

struct Biquad {
    double b0, b1, b2, a1, a2;
};

// RBJ cookbook Butterworth sections (Q per section for an order-4 Butterworth: 0.5412, 1.3066)
inline Biquad lowpass(double fc, double q, int sr)
{
    const double w = 2 * 3.14159265358979323846 * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
    return {(1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0};
}
inline Biquad highpass(double fc, double q, int sr)
{
    const double w = 2 * 3.14159265358979323846 * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
    return {(1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0};
}

inline void run(std::vector<double>& x, const Biquad& f, bool backwards)
{
    double z1 = 0, z2 = 0;  // transposed direct form II
    const long n = (long) x.size();
    for (long k = 0; k < n; ++k) {
        double& v = x[backwards ? n - 1 - k : k];
        const double y = f.b0 * v + z1;
        z1 = f.b1 * v - f.a1 * y + z2;
        z2 = f.b2 * v - f.a2 * y;
        v = y;
    }
}

// order-4 Butterworth low / high pass (two sections), forwards and backwards: zero phase, order 8 magnitude
inline Signal band(const std::vector<double>& mono, double hp_hz, double lp_hz, int sr)
{
    std::vector<double> x = mono;
    std::vector<Biquad> fs;
    for (double q : {0.5412, 1.3066}) {
        if (hp_hz > 0) fs.push_back(highpass(hp_hz, q, sr));
        if (lp_hz > 0) fs.push_back(lowpass(lp_hz, q, sr));
    }
    for (auto& f : fs) run(x, f, false);
    for (auto& f : fs) run(x, f, true);
    return Signal(x.begin(), x.end());
}

inline std::map<std::string, Signal> kit_from_drum_stem(const Stereo& drums, int sr = kSR)
{
    std::vector<double> mono(drums.size());
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5 * ((double) drums.l[i] + (drums.r.size() == drums.l.size() ? drums.r[i] : drums.l[i]));
    std::map<std::string, Signal> kit;
    kit["kick"] = band(mono, 0, 120, sr);
    kit["snare"] = band(mono, 200, 5000, sr);
    kit["hh"] = band(mono, 7000, 0, sr);
    for (const char* s : {"toms", "ride", "crash"}) kit[s] = Signal(mono.size(), 0.f);
    return kit;
}

}  // namespace ds::quick
