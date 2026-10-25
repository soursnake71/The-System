// Building blocks shared by the effects, ported line for line from the web app's worklets (RACK_CODE in index.html).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace omega::dsp {

constexpr double PI = 3.14159265358979323846;
// JavaScript's Math.round (halves go up, also for negatives)
inline double jround(double x) { return std::floor(x + .5); }
inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline double sat(double x) { return x > 3 ? 1 : x < -3 ? -1 : x * (27 + x * x) / (27 + 9 * x * x); }
inline double tnh(double x) { return x < -3 ? -1 : x > 3 ? 1 : x * (27 + x * x) / (27 + 9 * x * x); }

// Math.random() stand-in, one per engine
struct Rand {
    uint32_t s = 0x9e3779b9u;
    double operator()() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) * (1.0 / 16777216.0); }
};

class DLine {
public:
    DLine() = default;
    explicit DLine(int n) { init(n); }
    void init(int n) { int m = 1; while (m < n + 4) m <<= 1; b.assign((size_t) m, 0.f); mask = m - 1; w = 0; }
    void push(double x) { w = (w + 1) & mask; b[(size_t) w] = (float) x; }
    double at(int d) const { return b[(size_t) ((w - d) & mask)]; }
    double lerp(double d) const { const int i = (int) std::floor(d); const double f = d - i; const double a = b[(size_t) ((w - i) & mask)], c = b[(size_t) ((w - i - 1) & mask)]; return a + (c - a) * f; }
    void clear() { std::fill(b.begin(), b.end(), 0.f); }
    int m() const { return mask; }
private:
    std::vector<float> b; int mask = 0, w = 0;
};

struct BQ {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void keepState(const BQ& o) { z1 = o.z1; z2 = o.z2; }
    static BQ lp(double fs, double f, double q) { const double w = 2 * PI * std::min(f, fs * .45) / fs, al = std::sin(w) / (2 * q), cw = std::cos(w), a0 = 1 + al;
        BQ r; r.b0 = (1 - cw) / 2 / a0; r.b1 = (1 - cw) / a0; r.b2 = (1 - cw) / 2 / a0; r.a1 = -2 * cw / a0; r.a2 = (1 - al) / a0; return r; }
    static BQ hp(double fs, double f, double q) { const double w = 2 * PI * std::min(f, fs * .45) / fs, al = std::sin(w) / (2 * q), cw = std::cos(w), a0 = 1 + al;
        BQ r; r.b0 = (1 + cw) / 2 / a0; r.b1 = -(1 + cw) / a0; r.b2 = (1 + cw) / 2 / a0; r.a1 = -2 * cw / a0; r.a2 = (1 - al) / a0; return r; }
};

// low-pass of n poles (pairs of biquads + an optional single pole)
class LPN {
public:
    LPN() = default;
    LPN(double fs_, double f, int n_, std::vector<double> qs_ = {}) { init(fs_, f, n_, std::move(qs_)); }
    void init(double fs_, double f, int n_, std::vector<double> qs_ = {}) { fs = fs_; n = n_; qs = qs_.empty() ? std::vector<double> {.54, .65, 1.3} : qs_; cur = -1; set(f); }
    void set(double f) {
        if (f == cur) return; cur = f; std::vector<BQ> nb;
        for (int k = 0; k < n / 2; ++k) { BQ q = BQ::lp(fs, f, k < (int) qs.size() ? qs[(size_t) k] : .707); if (k < (int) b.size()) q.keepState(b[(size_t) k]); nb.push_back(q); }
        b = nb; a = n % 2 ? 1 - std::exp(-2 * PI * std::min(f, fs * .45) / fs) : 0;
    }
    double run(double x) { for (auto& q : b) x = q.run(x); if (a != 0) { y += a * (x - y); x = y; } return x; }
private:
    double fs = 48000, cur = -1, a = 0, y = 0; int n = 2; std::vector<double> qs; std::vector<BQ> b;
};

// sample-and-hold at the unit's own clock
struct Hold { double ph = 1, v = 0; double run(double x, double ratio) { ph += ratio; if (ph >= 1) { ph -= std::floor(ph); v = x; } return v; } };

inline double tailOf(double ms, double fb) {
    return std::min(20.0, ms / 1000 * (fb > .01 ? std::log(1e-4) / std::log(std::min(.98, std::fabs(fb))) : 1) + .3);
}

}  // namespace omega::dsp
