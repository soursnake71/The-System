// Omega DMX circuit model: a line-for-line C++ port of dmx/circuit.js (see there for the circuit and its sources, MAME's
// oberheim/dmx.cpp with sound/dac76, flt_biquad, va_eg and machine/rescap). dmx/tools/compare.mjs checks both render
// the same samples. A kit sound is written into the voice card's ROM (resampled to the card's clock, peak-normalised,
// AM6070 codes, cut to the ROM's size) and played through the card: 555 clock, DAC held between ticks, RC decay on the
// reference current, three Sallen-Key low-passes, at 4x the output rate and decimated.
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dmx::circuit {

constexpr double PI = 3.14159265358979323846;
constexpr double VCC = 5, R3 = 1e3, R4 = 10e3, T1_MAX = 10e3, R5 = 3.3e3, R_555 = 5e3, VD_PITCH = .48, VD = .6, R8 = 2.7e3, R9 = 5.6e3;
constexpr double R_FB = 27e3, MIX_NORM = 27e3 / 10e3;
constexpr int LEVEL[8] = {0, 33, 99, 231, 495, 1023, 2079, 4191}, FULL = 8031;
enum Decay { DecayOff, DecayOn, DecayTr12 };
enum Early { EarlyOn, EarlyTr1 };
struct Card { double c2, c3, r12, r17; bool pitch; Decay decay; Early early; bool lowFilter; int rom; double mixL, mixR; };
enum CardId { Bass, Snare, Hat, STom, LTom, Perc1, Perc2, Cym, NumCards };
constexpr Card CARDS[NumCards] = {
    {3.3e-9, 3.3e-6, 750, 47, false, DecayOn, EarlyOn, true, 4096, 10e3, 10e3},
    {2.2e-9, 0, 1500, 15, false, DecayOff, EarlyTr1, false, 4096, 8.2e3, 20e3},
    {3.3e-9, 2.2e-6, 6800, 100e6, false, DecayTr12, EarlyTr1, false, 4096, 20e3, 8.2e3},
    {3.3e-9, 6.8e-6, 33e3, 18e3, true, DecayOn, EarlyOn, true, 4096, 6.8e3, 100e3},
    {3.3e-9, 6.8e-6, 33e3, 18e3, true, DecayOn, EarlyOn, true, 4096, 100e3, 6.8e3},
    {3.3e-9, 0, 1500, 30, false, DecayOff, EarlyTr1, false, 2048, 6.8e3, 100e3},
    {4.7e-9, 0, 1500, 30, false, DecayOff, EarlyTr1, true, 2048, 100e3, 6.8e3},
    {3.3e-9, 0, 680, 100, false, DecayOff, EarlyTr1, false, 16384, 8.2e3, 20e3}};
// Sallen-Key stages {R1, R2, C1 (feedback), C2 (to ground)}
constexpr double FILTERS[2][3][4] = {
    {{6.8e3, 6.2e3, 10e-9, 4.7e-9}, {9.1e3, 9.1e3, 10e-9, 560e-12}, {5.1e3, 5.1e3, 47e-9, 200e-12}},     // low
    {{4.7e3, 4.7e3, 10e-9, 4.7e-9}, {5.6e3, 5.6e3, 10e-9, 560e-12}, {3.3e3, 3.3e3, 47e-9, 200e-12}}};    // high

inline bool isZero(double d) { return !(d < 0) && !(d > 0); }   // exactly zero (as the app's d === 0)
inline double par2(double a, double b) { return 1 / (1 / a + 1 / b); }
inline double par3(double a, double b, double c) { return 1 / (1 / a + 1 / b + 1 / c); }
inline double potLaw(double x) { return (std::pow(81.0, x) - 1) / 80; }
inline double mixK() { return .8 / potLaw(.8); }

inline double cv(const Card& c, int mode) {
    if (!c.pitch || mode == 3) return VCC * 2 / 3;
    if (mode == 1) { const double a = 1 + c.r12 / c.r17; return (a * R5 + c.r12) * (2 * VCC - 3 * VD_PITCH) / (3 * a * R5 + 3 * c.r12 + 2 * a * R_555) + VD_PITCH; }
    return (R5 + c.r12) * (2 * VCC - 3 * VD_PITCH) / (3 * R5 + 3 * c.r12 + 2 * R_555) + VD_PITCH;
}
inline double rate(const Card& c, int mode, double tune) {
    const double trim = 100 - tune, rc = trim * T1_MAX / 100 + R4, v = cv(c, mode);
    const double rcCharge = rc * c.c2, rcDis = par2(rc, R3) * c.c2, ve = VCC * (R3 / (rc + R3));
    const double tHigh = -rcCharge * std::log((VCC - v) / (VCC - v / 2)), tLow = -rcDis * std::log((ve - v / 2) / (ve - v));
    return 1 / (tHigh + tLow);
}
inline double iref(const Card& c, int mode) {
    const double mx = VCC / (R8 + R9);
    if (c.pitch || mode == 3) return 1;
    const double a = c.r12, b = c.r17;
    if (mode == 1) return (a * b * VCC + R8 * a * VD + R8 * b * VD) / (R8 * a * b + a * b * R9 + R8 * b * R9 + R8 * a * R9) / mx;
    return (a * VCC + R8 * VD) / (a * R8 + R8 * R9 + a * R9) / mx;
}
// decay {time constant, counter start}; tau 0 = none
inline std::pair<double, int> decay(const Card& c, int mode) {
    const bool on = c.decay == DecayOn || (c.decay == DecayTr12 && mode != 3);
    if (!on || !(c.c3 > 0)) return {0, 0};
    const double r = c.pitch || mode == 3 ? R8 + R9 : mode == 1 ? R8 + par3(R9, c.r12, c.r17) : R8 + par2(R9, c.r12);
    const bool early = c.early == EarlyOn || (c.early == EarlyTr1 && mode == 1);
    return {r * c.c3, early ? 64 : 1024};
}
inline double decode(int code) {
    const int chord = (code >> 4) & 7, step = code & 15; const double v = (double) (LEVEL[chord] + step * (2 << chord)) / FULL;
    return (code & 128) ? v : -v;
}
inline double mag(int c) { return (double) (LEVEL[c >> 4] + (c & 15) * (2 << (c >> 4))) / FULL; }
inline int encode(double x) {
    const double m = std::min(1.0, std::fabs(x)); int lo = 0, hi = 127;
    while (hi - lo > 1) { const int mid = (lo + hi) >> 1; if (mag(mid) <= m) lo = mid; else hi = mid; }
    const int c = m - mag(lo) <= mag(hi) - m ? lo : hi;
    return (x >= 0 ? 128 : 0) | c;
}
inline std::vector<double> resample(const std::vector<double>& x, double from, double to) {
    const size_t n = (size_t) std::floor((double) x.size() * to / from); std::vector<double> out(n);
    const double ratio = from / to, fc = std::min(1.0, to / from), W = 16;
    for (size_t i = 0; i < n; ++i) {
        const double c = (double) i * ratio; const long k0 = (long) std::floor(c - W / fc), k1 = (long) std::ceil(c + W / fc); double s = 0;
        for (long k = std::max(0L, k0); k <= std::min((long) x.size() - 1, k1); ++k) {
            const double d = ((double) k - c) * fc, w = std::fabs(d) >= W ? 0 : .42 + .5 * std::cos(PI * d / W) + .08 * std::cos(2 * PI * d / W);
            s += x[(size_t) k] * fc * (isZero(d) ? 1 : std::sin(PI * d) / (PI * d)) * w;
        }
        out[i] = s;
    }
    return out;
}
struct Rom { std::vector<uint8_t> rom; int len = 0; };
inline Rom makeRom(const std::vector<double>& x, double sr, const Card& card) {
    const auto y = resample(x, sr, rate(card, 3, 50)); double pk = 0; for (double v : y) pk = std::max(pk, std::fabs(v));
    const int n = std::min(card.rom, (int) y.size()); Rom R; R.rom.assign((size_t) card.rom, 0);
    for (int i = 0; i < n; ++i) R.rom[(size_t) i] = (uint8_t) encode(pk > 0 ? y[(size_t) i] / pk : 0);
    int last = n; while (last > 0 && (R.rom[(size_t) last - 1] & 127) == 0) --last;
    R.len = last; return R;
}
inline bool skCoefs(const double* st, double fs, double* c) {
    const double r1 = st[0], r2 = st[1], c1 = st[2], c2 = st[3];
    const double fc = 1 / (2 * PI * std::sqrt(r1 * r2 * c1 * c2)), q = std::sqrt(r1 * r2 * c1 * c2) / (r1 * c2 + r2 * c2);
    if (fc >= fs / 2) return false;
    const double K = std::tan(PI * fc / fs), nrm = 1 / (1 + K / q + K * K), b0 = K * K * nrm;
    c[0] = b0; c[1] = 2 * b0; c[2] = b0; c[3] = 2 * (K * K - 1) * nrm; c[4] = (1 - K / q + K * K) * nrm; return true;
}
constexpr int OS = 4, TAPS = 63;
inline const std::array<double, TAPS>& decim() {
    static const std::array<double, TAPS> h = [] {
        std::array<double, TAPS> t {}; const double fc = .45 / OS * 2; double s = 0;
        for (int i = 0; i < TAPS; ++i) { const double d = i - (TAPS - 1) / 2.0, w = .42 - .5 * std::cos(2 * PI * i / (TAPS - 1)) + .08 * std::cos(4 * PI * i / (TAPS - 1));
            t[(size_t) i] = (isZero(d) ? fc : std::sin(PI * fc * d) / (PI * d)) * w; s += t[(size_t) i]; }
        for (auto& v : t) v /= s;
        return t; }();
    return h;
}
inline std::vector<float> render(const Rom& R, const Card& card, int mode, double tune, double fs) {
    const double f = rate(card, mode, tune), F = fs * OS, I0 = iref(card, mode); const auto dec = decay(card, mode);
    const double tEnd = R.len / f; const size_t n = (size_t) std::ceil((tEnd + .01) * F) + TAPS; std::vector<double> x(n);
    auto val = [&](long k) { return k < R.len ? decode(R.rom[(size_t) k]) : 0.0; };
    for (size_t i = 0; i < n; ++i) {
        const double t0 = (double) i / F, t1 = (double) (i + 1) / F; const long k0 = (long) std::floor(t0 * f), k1 = (long) std::floor(t1 * f); double v;
        if (k0 == k1) v = val(k0);
        else { v = val(k0) * ((double) (k0 + 1) / f - t0); for (long k = k0 + 1; k < k1; ++k) v += val(k) / f; v += val(k1) * (t1 - (double) k1 / f); v *= F; }
        double g = I0; const double tm = ((double) i + .5) / F;
        if (dec.first > 0) { const double td = dec.second / f; if (tm > td) g *= std::exp(-(tm - td) / dec.first); }
        x[i] = v * g;
    }
    for (int s = 0; s < 3; ++s) {
        double c[5]; if (!skCoefs(FILTERS[card.lowFilter ? 0 : 1][s], F, c)) continue; double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t i = 0; i < n; ++i) { const double y = c[0] * x[i] + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2; x2 = x1; x1 = x[i]; y2 = y1; y1 = y; x[i] = y; }
    }
    const size_t m = (n - TAPS) / OS; std::vector<float> out(m); const auto& h = decim();
    for (size_t j = 0; j < m; ++j) { double s = 0; const size_t b = j * OS; for (int k = 0; k < TAPS; ++k) s += h[(size_t) k] * x[b + (size_t) k]; out[j] = (float) s; }
    return out;
}
inline std::pair<double, double> pan(const Card& c) { return {R_FB / c.mixL / MIX_NORM, R_FB / c.mixR / MIX_NORM}; }
inline double metV(bool loud) { return (loud ? 2.5 : 1.25) / (VCC / (R8 + R9) * 3.8 * 2400); }

// which card and trigger mode a kit voice gets (as cardFor in circuit.js): by fader group, toms split small / large, hats by kind
struct Placement { CardId card; int mode; };
inline Placement cardFor(const char* const* files, const char* const* names, const int* groups, int n, int vi) {
    const int g = groups[vi];
    if (g == 3) { int count = 0, at = 0; for (int i = 0; i < n; ++i) if (groups[i] == 3) { if (i == vi) at = count; ++count; } return {at < (count + 1) / 2 ? STom : LTom, 3}; }
    if (g == 2) { std::string s = std::string(files[vi]) + " " + names[vi]; for (auto& ch : s) ch = (char) std::tolower((unsigned char) ch);
        return {Hat, s.find("open") != std::string::npos ? 3 : (s.find("half") != std::string::npos || s.find("medium") != std::string::npos) ? 2 : 1}; }
    static const CardId byGroup[7] = {Bass, Snare, Hat, STom, Cym, Perc1, Perc2};
    return {byGroup[g], 3};
}

}  // namespace dmx::circuit
