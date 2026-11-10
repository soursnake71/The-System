// Drum transcription from the six kit stems (kick snare toms hh ride crash): every hit with a sample-accurate attack
// time, its length (until the sound dies away or is choked), the kit piece and a velocity.
//
// Per stem:
//  1. candidate hits: log-compressed spectral flux (1024-point frames, 256 hop) on a band-passed copy, adaptive
//     threshold, minimum spacing per instrument;
//  2. the attack on the waveform: a 0.6 ms trailing-max envelope, worked in power so a hit on top of a ring is measured
//     from the ring: the attack starts where the power has risen 0.15^2 of the way from the ring's level to the hit's
//     peak. Cymbals and toms also try a high-passed "stick" band and keep whichever shows the clearer attack;
//  3. gates: level under the stem's loud hits, how much the level jumps, the stick's attack for toms.
// Across stems: bleed rejection. A hit is another stem's sound leaking through the separator when that stem has a much
// louder attack at the same moment AND what the hit adds (spectrum after minus before, 1/3 octaves) sounds more like
// that stem than like any of this stem's own loud hits. Quiet hits that sound like their own stem (ghost notes, pedal
// hats under a ride) stay.
// Then the kit pieces: hi-hat open / closed / pedal, toms grouped by pitch, ride bow / bell, crash 1 / 2, snare / side
// stick, and velocities (36 dB of level -> 1..127 against the stem's loud hits).
// The thresholds were tuned against exact ground truth (tests/test_drums.cpp; drumscribe/engine is the reference).
#include <algorithm>
#include <cmath>
#include <complex>
#include <deque>
#include <numeric>
#include "ds/kit.h"
#include "ds/modules.h"

namespace ds {
namespace {

struct Params { double min_ioi, delta, floor_db, min_rise, tick_floor_db, band_lo, band_hi, tick; };
const std::map<std::string, Params>& params()
{
    static const std::map<std::string, Params> p = {
        {"kick", {0.030, 0.06, 40, 5.0, 60, 25, 6000, 0}},
        {"snare", {0.016, 0.05, 42, 4.0, 60, 80, 14000, 0}},
        {"toms", {0.025, 0.06, 36, 4.0, 24, 40, 8000, 1500}},
        {"hh", {0.025, 0.05, 40, 1.5, 60, 300, 18000, 3000}},
        {"ride", {0.035, 0.05, 36, 1.5, 60, 200, 18000, 4000}},
        {"crash", {0.060, 0.06, 26, 1.5, 60, 150, 18000, 3000}},
    };
    return p;
}
constexpr int N_FFT = 1024, HOP = 256;
constexpr double ONSET_FRACTION = 0.15;
constexpr double PI = 3.14159265358979323846;

struct Hit {
    DrumHit h;
    double next = 0;  // the next candidate in this stem (bounds the analysis windows)
    double tick_db = 0, ref_db = 0;
    std::map<std::string, double> f;
};

// ------------------------------------------------------------------ small DSP kit
void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * PI / (double) len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// |rfft(seg * hann(len(seg)), n)|
std::vector<double> mag_spectrum(const float* seg, size_t len, size_t n)
{
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < len && i < n; ++i) {
        const double w = len > 1 ? 0.5 - 0.5 * std::cos(2 * PI * (double) i / (double) (len - 1)) : 1.0;
        a[i] = seg[i] * w;
    }
    fft(a);
    std::vector<double> m(n / 2 + 1);
    for (size_t i = 0; i < m.size(); ++i) m[i] = std::abs(a[i]);
    return m;
}

struct Biquad {
    double b0, b1, b2, a1, a2;
    static Biquad make(bool high, double fc, double sr, double q)
    {
        const double w = 2 * PI * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        Biquad b{};
        if (high) { b.b0 = (1 + c) / 2; b.b1 = -(1 + c); b.b2 = (1 + c) / 2; }
        else { b.b0 = (1 - c) / 2; b.b1 = 1 - c; b.b2 = (1 - c) / 2; }
        b.b0 /= a0; b.b1 /= a0; b.b2 /= a0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0;
        return b;
    }
    void run(std::vector<double>& x, bool backward) const
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        const long n = (long) x.size();
        for (long k = 0; k < n; ++k) {
            const long i = backward ? n - 1 - k : k;
            const double in = x[(size_t) i], y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in; y2 = y1; y1 = y;
            x[(size_t) i] = y;
        }
    }
};

// zero-phase band-pass for the onset function: 4th-order Butterworth high-pass + low-pass, forwards and backwards
Signal bandpass(const Signal& x, double sr, double lo, double hi)
{
    hi = std::min(hi, 0.45 * sr);
    std::vector<double> y(x.begin(), x.end());
    const double qs[2] = {0.54119610, 1.30656296};
    for (int dir = 0; dir < 2; ++dir)
        for (double q : qs) {
            Biquad::make(true, lo, sr, q).run(y, dir == 1);
            Biquad::make(false, hi, sr, q).run(y, dir == 1);
        }
    return Signal(y.begin(), y.end());
}

// causal 2nd-order Butterworth high-pass (the "stick" band; causal so the attack isn't smeared backwards)
Signal highpass(const Signal& x, double sr, double fc)
{
    std::vector<double> y(x.begin(), x.end());
    Biquad::make(true, fc, sr, 0.70710678).run(y, false);
    return Signal(y.begin(), y.end());
}

double percentile(std::vector<double> v, double q)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double pos = q / 100.0 * (double) (v.size() - 1);
    const size_t i = (size_t) std::floor(pos);
    const double fr = pos - (double) i;
    return i + 1 < v.size() ? v[i] * (1 - fr) + v[i + 1] * fr : v[i];
}

double median(std::vector<double> v) { return percentile(std::move(v), 50); }

double rms_db(const Signal& x, long a, long b)
{
    a = std::max(0L, a);
    b = std::min((long) x.size(), b);
    if (b - a < 4) return -180;
    double s = 0;
    for (long i = a; i < b; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return 20 * std::log10(std::max(std::sqrt(s / (double) (b - a)), 1e-9));
}

// trailing max of |x| over 0.6 ms: follows the attack sample by sample without the zero crossings
std::vector<float> envelope(const Signal& x, int sr)
{
    const size_t w = (size_t) std::max(2, (int) (sr * 0.0006));
    std::vector<float> e(x.size());
    std::deque<size_t> dq;
    for (size_t i = 0; i < x.size(); ++i) {
        const float v = std::fabs(x[i]);
        while (!dq.empty() && std::fabs(x[dq.back()]) <= v) dq.pop_back();
        dq.push_back(i);
        while (dq.front() + w <= i) dq.pop_front();
        e[i] = std::fabs(x[dq.front()]);
    }
    return e;
}

std::vector<double> onset_strength(const Signal& x)
{
    const size_t n = 1 + x.size() / HOP;
    std::vector<double> win(N_FFT);
    for (int i = 0; i < N_FFT; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos(2 * PI * i / (N_FFT - 1));
    std::vector<std::vector<float>> mag(n, std::vector<float>(N_FFT / 2 + 1));
    std::vector<double> fmax(n);
    std::vector<std::complex<double>> a(N_FFT);
    for (size_t k = 0; k < n; ++k) {
        for (int i = 0; i < N_FFT; ++i) {
            const long s = (long) (k * HOP) + i - N_FFT / 2;
            a[(size_t) i] = (s >= 0 && s < (long) x.size()) ? x[(size_t) s] * win[(size_t) i] : 0.0;
        }
        fft(a);
        double m = 0;
        for (int i = 0; i <= N_FFT / 2; ++i) { mag[k][(size_t) i] = (float) std::abs(a[(size_t) i]); m = std::max(m, (double) mag[k][(size_t) i]); }
        fmax[k] = m;
    }
    const double ref = std::max(percentile(fmax, 99.5), 1e-6);
    std::vector<double> flux(n, 0.0);
    for (size_t k = 1; k < n; ++k) {
        double s = 0;
        for (int i = 0; i <= N_FFT / 2; ++i) {
            const double d = std::log1p(1000.0 * mag[k][(size_t) i] / ref) - std::log1p(1000.0 * mag[k - 1][(size_t) i] / ref);
            if (d > 0) s += d;
        }
        flux[k] = s;
    }
    double top = percentile(flux, 99.5);
    if (top <= 0) top = 1;
    for (auto& v : flux) v /= top;
    return flux;
}

std::vector<long> pick_peaks(const std::vector<double>& odf, int sr, double min_ioi, double delta)
{
    const double fps = (double) sr / HOP;
    const long wmax = std::max(1L, std::lround(min_ioi * fps)), wavg = std::lround(0.10 * fps);
    const long n = (long) odf.size();
    std::vector<double> csum(odf.size() + 1, 0.0);
    for (size_t i = 0; i < odf.size(); ++i) csum[i + 1] = csum[i] + odf[i];
    std::vector<long> peaks;
    long last = -1000000000;
    for (long c = 0; c < n; ++c) {
        double lm = 0;  // constant (zero) padding at the edges
        for (long j = c - wmax; j <= c + wmax; ++j) lm = std::max(lm, (j >= 0 && j < n) ? odf[(size_t) j] : 0.0);
        double avg = 0;  // nearest padding at the edges
        for (long j = c - wavg; j <= c + wavg; ++j) avg += odf[(size_t) std::clamp(j, 0L, n - 1)];
        avg /= (double) (2 * wavg + 1);
        if (!(odf[(size_t) c] >= lm && odf[(size_t) c] >= avg + delta && odf[(size_t) c] > delta)) continue;
        if (c - last >= wmax) { peaks.push_back(c); last = c; }
        else if (odf[(size_t) c] > odf[(size_t) peaks.back()]) { peaks.back() = c; last = c; }
    }
    return peaks;
}

struct Refined { double t; long peak; double ratio; };

// sample-accurate attack start near t_coarse, inside [lo_t, hi_t]
bool refine(const std::vector<float>& env, int sr, double tc, double lo_t, double hi_t, Refined& r)
{
    const long lo = std::max(0L, (long) (lo_t * sr)), hi = std::min((long) env.size() - 1, (long) (hi_t * sr));
    const long c = (long) (tc * sr);
    const long s0 = std::max(lo, c - (long) (0.012 * sr)), s1 = std::min(hi, c + (long) (0.025 * sr));
    if (s1 - s0 < 8) return false;
    float m = 0;
    for (long i = s0; i < s1; ++i) m = std::max(m, env[(size_t) i]);
    if (m <= 0) return false;
    long p = s0;
    while (env[(size_t) p] < m * 0.7f) ++p;  // the first big maximum
    while (p + 1 < s1 && env[(size_t) (p + 1)] >= env[(size_t) p]) ++p;
    const long fl0 = std::max(lo, s0 - (long) (0.010 * sr));
    const size_t L = (size_t) (p + 1 - fl0);
    std::vector<double> pw(L);
    for (size_t i = 0; i < L; ++i) pw[i] = (double) env[(size_t) fl0 + i] * env[(size_t) fl0 + i];
    // 2 ms moving average (nearest padding), its minimum (ignoring the last 0.5 ms) is the ring's level
    const long w = std::max(1L, (long) (0.002 * sr));
    const long lw = w / 2, rw = w - 1 - lw;
    std::vector<double> cs(L + 1, 0.0);
    for (size_t i = 0; i < L; ++i) cs[i + 1] = cs[i] + pw[i];
    auto at = [&](long i) { return pw[(size_t) std::clamp(i, 0L, (long) L - 1)]; };
    double base = 1e300;
    const long lim = std::max(1L, (long) L - (long) (0.0005 * sr));
    for (long i = 0; i < lim; ++i) {
        double s = 0;
        const long a = i - lw, b = i + rw;
        if (a >= 0 && b < (long) L) s = cs[(size_t) b + 1] - cs[(size_t) a];
        else for (long j = a; j <= b; ++j) s += at(j);
        base = std::min(base, s / (double) w);
    }
    const double peak = pw[L - 1], th = base + ONSET_FRACTION * ONSET_FRACTION * (peak - base);
    long i = (long) L - 1;
    while (i > 0 && pw[(size_t) (i - 1)] >= th) --i;
    double t;
    if (i > 0 && pw[(size_t) i] != pw[(size_t) (i - 1)]) {
        const double fr = std::clamp((th - pw[(size_t) (i - 1)]) / (pw[(size_t) i] - pw[(size_t) (i - 1)]), 0.0, 1.0);
        t = ((double) (fl0 + i - 1) + fr) / sr;
    } else t = (double) (fl0 + i) / sr;
    r = {t, p, peak / std::max(base, 1e-20)};
    return true;
}

std::vector<Hit> detect_stem(const std::string& name, const Signal& x, int sr)
{
    const Params& P = params().at(name);
    const auto odf = onset_strength(bandpass(x, sr, P.band_lo, P.band_hi));
    const auto peaks = pick_peaks(odf, sr, P.min_ioi, P.delta);
    const auto env = envelope(x, sr);
    Signal xt;
    std::vector<float> env_t;
    if (P.tick > 0) { xt = highpass(x, sr, P.tick); env_t = envelope(xt, sr); }
    std::vector<double> coarse;
    for (long k : peaks) coarse.push_back((double) (k * HOP) / sr);
    std::vector<Hit> hits;
    double prev = -1;
    const long n5 = (long) (0.005 * sr);
    for (size_t k = 0; k < coarse.size(); ++k) {
        const double tc = coarse[k], nxt = k + 1 < coarse.size() ? coarse[k + 1] : (double) x.size() / sr;
        const double lo_t = prev >= 0 ? std::max(prev + 0.004, tc - 0.020) : tc - 0.020, hi_t = nxt - 0.004;
        Refined r{}, rt{};
        bool ok = refine(env, sr, tc, lo_t, hi_t, r);
        if (!env_t.empty() && refine(env_t, sr, tc, lo_t, hi_t, rt) && (!ok || rt.ratio > r.ratio)) { r = rt; ok = true; }
        if (!ok) continue;
        if (prev >= 0 && r.t - prev < P.min_ioi * 0.5) continue;
        const long a = (long) (r.t * sr);
        const long end = std::min((long) ((nxt - 0.001) * sr), a + (long) (0.030 * sr));
        double lev = -180, lev_t = -180;
        const long stop = std::max(a + 1, end - n5 + 1), step = std::max(1L, n5 / 2);
        for (long s = a; s < stop; s += step) {
            lev = std::max(lev, rms_db(x, s, s + n5));
            if (!xt.empty()) lev_t = std::max(lev_t, rms_db(xt, s, s + n5));
        }
        const Signal& rs = xt.empty() ? x : xt;
        if (xt.empty()) lev_t = lev;
        const long pa = std::max(prev >= 0 ? (long) ((prev + 0.002) * sr) : 0L, a - (long) (0.025 * sr));
        const double pre = rms_db(rs, pa, a - (long) (0.001 * sr));
        Hit h;
        h.h.stem = name;
        h.h.time = r.t;
        h.h.level_db = (float) lev;
        h.h.rise_db = (float) (lev_t - pre);
        h.next = nxt;
        h.tick_db = lev_t;
        hits.push_back(std::move(h));
        prev = r.t;
    }
    if (hits.empty()) return hits;
    std::vector<double> lv, tv;
    for (auto& h : hits) { lv.push_back(h.h.level_db); tv.push_back(h.tick_db); }
    const double ref = percentile(lv, 98), tref = percentile(tv, 98);
    std::vector<Hit> keep;
    for (auto& h : hits)
        if (h.h.level_db >= ref - P.floor_db && h.h.rise_db >= P.min_rise && h.h.level_db > -75 && h.tick_db >= tref - P.tick_floor_db) {
            h.ref_db = ref;
            keep.push_back(h);
        }
    return keep;
}

// ------------------------------------------------------------------ bleed
const int NB = 25;
double edge(int i) { return 40.0 * std::pow(2.0, i / 3.0); }

std::vector<double> band_power(const Signal& x, long a, long b, int sr)
{
    std::vector<double> e(NB, 0.0);
    a = std::max(0L, a);
    b = std::min((long) x.size(), b);
    if (b - a < 32) return e;
    const size_t n = 4096;
    auto m = mag_spectrum(x.data() + a, (size_t) (b - a), n);
    std::vector<double> cs(m.size() + 1, 0.0);
    for (size_t i = 0; i < m.size(); ++i) cs[i + 1] = cs[i] + m[i] * m[i] / (double) (b - a);
    const double df = (double) sr / (double) n;
    auto idx = [&](double f) { return std::min(m.size(), (size_t) std::ceil(f / df - 1e-9)); };  // searchsorted left
    for (int i = 0; i < NB; ++i) {
        const size_t i0 = idx(edge(i)), i1 = std::max(i0 + 1, idx(edge(i + 1)));
        e[(size_t) i] = cs[std::min(i1, m.size())] - cs[std::min(i0, m.size())];
    }
    return e;
}

struct Added { std::vector<double> prof; double add_db, after_db; };

// what the hit at t ADDS: per band, the power in the 20 ms after t minus the 20 ms before
Added added(const Signal& x, int sr, double t)
{
    const long a = (long) (t * sr), n = (long) (0.02 * sr);
    auto after = band_power(x, a, a + n, sr);
    auto before = a >= n ? band_power(x, a - n, a, sr) : std::vector<double>(NB, 0.0);
    Added r;
    r.prof.resize(NB);
    double ta = 0, tt = 0, mx = -1e300;
    for (int i = 0; i < NB; ++i) {
        const double ad = std::max(after[(size_t) i] - before[(size_t) i], 0.0);
        ta += ad;
        tt += after[(size_t) i];
        r.prof[(size_t) i] = 10 * std::log10(std::max(ad, after[(size_t) i] * 1e-3) + 1e-24);
        mx = std::max(mx, r.prof[(size_t) i]);
    }
    for (auto& v : r.prof) v = std::max(v - mx, -40.0);
    r.add_db = 10 * std::log10(ta + 1e-24);
    r.after_db = 10 * std::log10(tt + 1e-24);
    return r;
}

std::vector<double> unit(std::vector<double> v)
{
    const double mean = std::accumulate(v.begin(), v.end(), 0.0) / (double) v.size();
    double nn = 0;
    for (auto& x : v) { x -= mean; nn += x * x; }
    nn = std::sqrt(nn) + 1e-12;
    for (auto& x : v) x /= nn;
    return v;
}

double dot(const std::vector<double>& a, const std::vector<double>& b)
{
    double s = 0;
    for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
    return s;
}

double top3_mean(std::vector<double> s)
{
    if (s.empty()) return -1;
    std::sort(s.begin(), s.end());
    const size_t k = std::min<size_t>(3, s.size());
    double m = 0;
    for (size_t i = s.size() - k; i < s.size(); ++i) m += s[i];
    return m / (double) k;
}

void remove_bleed(std::map<std::string, std::vector<Hit>>& hits, const std::map<std::string, Signal>& stems, int sr,
                  const Progress& prog)
{
    const double louder_db = 10;
    std::map<std::string, std::vector<std::vector<double>>> prof;
    std::map<std::string, std::vector<double>> add_db;
    std::map<std::string, std::vector<size_t>> ex;  // exemplar indices
    std::map<std::string, std::vector<double>> times;
    for (auto& [name, hl] : hits) {
        auto& t = times[name];
        for (auto& h : hl) t.push_back(h.h.time);
    }
    for (auto& [name, hl] : hits) {
        const Signal& x = stems.at(name);
        for (auto& h : hl) {
            auto a = added(x, sr, h.h.time);
            prof[name].push_back(unit(a.prof));
            add_db[name].push_back(a.add_db);
        }
        if (hl.empty()) continue;
        double ref = -1e9;
        for (auto& h : hl) ref = std::max(ref, (double) h.h.level_db);
        std::vector<size_t> strong, idx;
        for (size_t i = 0; i < hl.size(); ++i) if (hl[i].h.level_db >= ref - 15) strong.push_back(i);
        for (size_t i : strong) {
            bool masked = false;  // something much louder in another stem at the same moment
            for (auto& [o, ol] : hits) {
                if (o == name) continue;
                for (auto& oh : ol)
                    if (std::fabs(oh.h.time - hl[i].h.time) < 0.010 && oh.h.level_db >= hl[i].h.level_db + 6) { masked = true; break; }
                if (masked) break;
            }
            if (!masked) idx.push_back(i);
        }
        if (idx.size() < 3) idx = strong;
        if (idx.size() > 300) {
            std::vector<size_t> sub;
            for (size_t k = 0; k < 300; ++k) sub.push_back(idx[k * idx.size() / 300]);
            idx = sub;
        }
        ex[name] = idx;
    }
    auto near_hit = [&](const std::string& o, double t) {
        auto& v = times[o];
        auto it = std::lower_bound(v.begin(), v.end(), t);
        return (it != v.end() && *it - t <= 0.006) || (it != v.begin() && t - *(it - 1) <= 0.006);
    };
    std::map<std::pair<std::string, long>, Added> cache;
    size_t done = 0, total = 0;
    for (auto& kv : hits) total += kv.second.size();
    for (auto& [name, hl] : hits) {
        std::vector<Hit> kept;
        for (size_t i = 0; i < hl.size(); ++i) {
            const auto& h = hl[i];
            const auto& v = prof[name][i];
            double sx = -2;
            bool bleed = false;
            for (auto& [other, y] : stems) {
                if (other == name) continue;
                const long key = (long) std::llround(h.h.time * sr);
                auto it = cache.find({other, key});
                if (it == cache.end()) it = cache.emplace(std::make_pair(other, key), added(y, sr, h.h.time)).first;
                const Added& oy = it->second;
                if (oy.add_db < oy.after_db - 3 && !near_hit(other, h.h.time)) continue;  // no attack in Y here
                const double d = add_db[name][i] - oy.after_db;
                if (d > -louder_db) continue;
                if (d < -(name == "snare" ? 32.0 : 24.0)) { bleed = true; break; }
                if (sx < -1.5) {
                    std::vector<double> s;
                    for (size_t j : ex[name]) if (j != i) s.push_back(dot(prof[name][j], v));
                    sx = s.empty() ? 0.0 : top3_mean(s);
                }
                double so = -1;
                if (ex.count(other)) {
                    std::vector<double> s;
                    for (size_t j : ex[other]) s.push_back(dot(prof[other][j], v));
                    so = top3_mean(s);
                }
                if (std::max(dot(unit(oy.prof), v), so) > sx) { bleed = true; break; }
            }
            if (!bleed) kept.push_back(h);
            if (prog && (++done % 256) == 0) prog(0.6 + 0.3 * (double) done / (double) total, "Rejecting bleed between the drums");
        }
        hl = std::move(kept);
    }
}

// ------------------------------------------------------------------ kit pieces
struct Spec { std::vector<double> mag; double df; };

Spec spectrum(const Signal& x, int sr, const Hit& h, double max_len)
{
    const long a = (long) (h.h.time * sr);
    const long len = std::max(64L, std::min((long) x.size() - a, (long) (std::min(max_len, std::max(0.01, h.next - h.h.time - 0.002)) * sr)));
    std::vector<float> seg((size_t) len, 0.f);
    for (long i = 0; i < len && a + i < (long) x.size(); ++i) seg[(size_t) i] = x[(size_t) (a + i)];
    size_t n = 4096;
    while (n < (size_t) len) n <<= 1;
    return {mag_spectrum(seg.data(), seg.size(), n), (double) sr / (double) n};
}

double centroid(const Spec& s, double lo)
{
    double num = 0, den = 0;
    for (size_t i = 0; i < s.mag.size(); ++i) {
        const double f = (double) i * s.df;
        if (f < lo) continue;
        num += f * s.mag[i] * s.mag[i];
        den += s.mag[i] * s.mag[i];
    }
    return num / std::max(den, 1e-12);
}

double log_flatness(const Spec& s, double lo, double hi)
{
    double lg = 0, ar = 0;
    int n = 0;
    for (size_t i = 0; i < s.mag.size(); ++i) {
        const double f = (double) i * s.df;
        if (f < lo || f > hi) continue;
        const double p = s.mag[i] * s.mag[i] + 1e-12;
        lg += std::log(p); ar += p; ++n;
    }
    if (!n) return 0;
    return lg / n - std::log(ar / n);
}

double pitch(const Spec& s, double lo = 45, double hi = 450)
{
    long best = -1;
    for (size_t i = 0; i < s.mag.size(); ++i) {
        const double f = (double) i * s.df;
        if (f >= lo && f <= hi && (best < 0 || s.mag[i] > s.mag[(size_t) best])) best = (long) i;
    }
    if (best <= 0) return 0;
    if (best + 1 < (long) s.mag.size()) {
        const double a = std::log(s.mag[(size_t) best - 1] + 1e-12), b = std::log(s.mag[(size_t) best] + 1e-12), c = std::log(s.mag[(size_t) best + 1] + 1e-12);
        const double den = a - 2 * b + c, d = den != 0 ? 0.5 * (a - c) / den : 0.0;
        return ((double) best + d) * s.df;
    }
    return (double) best * s.df;
}

// seconds from the peak until the level fell drop_db (<0: never, minus the span looked at)
double decay_time(const Signal& x, int sr, const Hit& h, double drop_db = 15)
{
    const long a = (long) (h.h.time * sr);
    const long stop = std::min((long) x.size(), a + (long) (std::min(0.6, std::max(0.01, h.next - h.h.time - 0.003)) * sr));
    const long n = (long) (0.003 * sr), hop = n / 2;
    const double span = (double) (stop - a) / sr;
    if (stop - a < 3 * n) return -span;
    std::vector<double> lv;
    for (long s = a; s + n <= stop; s += hop) lv.push_back(rms_db(x, s, s + n));
    const size_t lim = std::max<size_t>(2, std::min(lv.size(), (size_t) (0.03 * sr / hop)));
    const size_t pk = (size_t) (std::max_element(lv.begin(), lv.begin() + (long) std::min(lim, lv.size())) - lv.begin());
    for (size_t i = pk; i < lv.size(); ++i)
        if (lv[i] < lv[pk] - drop_db) return (double) (i - pk) * hop / sr;
    return -span;
}

struct Split { double th, lo, hi; };

// robust 1-D split in two groups (medians, MADs); false if the values don't clearly form two groups
bool two_groups(std::vector<double> v, double min_share, double min_sep, Split& out)
{
    if (v.size() < 8) return false;
    std::sort(v.begin(), v.end());
    const size_t n = v.size(), kmin = (size_t) std::max(2.0, std::ceil(min_share * (double) n));
    auto cost = [&](size_t a, size_t b) {
        std::vector<double> s(v.begin() + (long) a, v.begin() + (long) b);
        const double m = median(s);
        double c = 0;
        for (double x : s) c += std::fabs(x - m);
        return c;
    };
    double best = 1e300;
    size_t bk = 0;
    for (size_t k = kmin; k + kmin <= n; ++k) {
        const double c = cost(0, k) + cost(k, n);
        if (c < best) { best = c; bk = k; }
    }
    if (!bk) return false;
    std::vector<double> a(v.begin(), v.begin() + (long) bk), b(v.begin() + (long) bk, v.end());
    const double ma = median(a), mb = median(b);
    auto mad = [](const std::vector<double>& s, double m) { std::vector<double> d; for (double x : s) d.push_back(std::fabs(x - m)); return median(d); };
    double md = 1.4826 * std::sqrt((std::pow(mad(a, ma), 2) * (double) a.size() + std::pow(mad(b, mb), 2) * (double) b.size()) / (double) n);
    md = std::max(md, 1e-3 * (std::fabs(ma) + std::fabs(mb)) + 1e-9);
    if ((mb - ma) / md < min_sep) return false;
    out = {(v[bk - 1] + v[bk]) / 2, ma, mb};
    return true;
}

void articulate(const std::string& name, std::vector<Hit>& hl, const Signal& x, int sr)
{
    if (hl.empty()) return;
    if (name == "kick") for (auto& h : hl) h.h.piece = "kick";
    if (name == "snare") {
        std::vector<double> fl;
        for (auto& h : hl) { h.f["flat"] = log_flatness(spectrum(x, sr, h, 0.05), 200, 10000); fl.push_back(h.f["flat"]); h.h.piece = "snare"; }
        Split g{};
        if (two_groups(fl, 0.03, 3.5, g) && g.hi - g.lo > 2.0)  // a clearly tonal group: cross stick
            for (auto& h : hl) if (h.f["flat"] < g.th) h.h.piece = "side_stick";
    }
    if (name == "hh") {
        std::vector<double> cents;
        for (auto& h : hl) {
            h.f["decay"] = decay_time(x, sr, h);
            h.f["centroid"] = centroid(spectrum(x, sr, h, 0.04), 200);
            cents.push_back(h.f["centroid"]);
        }
        std::sort(cents.begin(), cents.end());
        const double bright = median(std::vector<double>(cents.begin() + (long) cents.size() / 2, cents.end()));
        for (auto& h : hl) {
            const double d = h.f["decay"];
            if (d > 0.09 || (d < 0 && -d >= 0.07)) h.h.piece = "hh_open";   // still ringing 90 ms on
            else if (h.f["centroid"] < 0.4 * bright) h.h.piece = "hh_pedal";  // dull chick
            else h.h.piece = "hh_closed";
        }
    }
    if (name == "toms") {
        std::vector<size_t> hs;
        std::vector<double> semis;
        for (size_t i = 0; i < hl.size(); ++i) {
            const double p = pitch(spectrum(x, sr, hl[i], 0.15));
            hl[i].f["pitch"] = p;
            hl[i].h.piece = "tom_himid";
            if (p > 0) { hs.push_back(i); semis.push_back(12 * std::log2(p / 55.0)); }
        }
        if (!hs.empty()) {
            std::vector<size_t> order(hs.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return semis[a] < semis[b]; });
            std::vector<std::vector<size_t>> groups{{order[0]}};
            for (size_t k = 1; k < order.size(); ++k) {
                if (semis[order[k]] - semis[order[k - 1]] > 1.8) groups.push_back({});
                groups.back().push_back(order[k]);
            }
            auto centre = [&](const std::vector<size_t>& g) { std::vector<double> s; for (auto i : g) s.push_back(semis[i]); return median(s); };
            bool changed = true;
            while (changed && groups.size() > 1) {
                changed = false;
                long small = -1;
                for (size_t i = 0; i < groups.size(); ++i)
                    if ((double) groups[i].size() < std::max(3.0, 0.03 * (double) hs.size())) { small = (long) i; break; }
                if (small < 0 && groups.size() > 6) {
                    small = 0;
                    for (size_t i = 1; i < groups.size(); ++i) if (groups[i].size() < groups[(size_t) small].size()) small = (long) i;
                }
                if (small >= 0) {
                    auto g = groups[(size_t) small];
                    groups.erase(groups.begin() + small);
                    long j = small - 1;
                    if (j < 0 || ((size_t) small < groups.size() && std::fabs(centre(groups[(size_t) small]) - centre(g)) < std::fabs(centre(groups[(size_t) j]) - centre(g))))
                        j = small;
                    groups[(size_t) j].insert(groups[(size_t) j].end(), g.begin(), g.end());
                    changed = true;
                }
            }
            std::sort(groups.begin(), groups.end(), [&](auto& a, auto& b) { return centre(a) > centre(b); });
            static const std::vector<std::vector<std::string>> layout = {
                {}, {}, {"tom_himid", "floor_high"}, {"tom_himid", "tom_low", "floor_high"},
                {"tom_high", "tom_himid", "tom_low", "floor_high"},
                {"tom_high", "tom_himid", "tom_lowmid", "tom_low", "floor_high"},
                {"tom_high", "tom_himid", "tom_lowmid", "tom_low", "floor_high", "floor_low"}};
            std::vector<std::string> names;
            if (groups.size() == 1) {
                const double p = 55.0 * std::pow(2.0, centre(groups[0]) / 12);
                names = {p >= 140 ? "tom_himid" : p >= 100 ? "tom_low" : "floor_high"};
            } else names = layout[groups.size()];
            for (size_t g = 0; g < groups.size(); ++g)
                for (auto i : groups[g]) hl[hs[i]].h.piece = names[g];
        }
    }
    if (name == "ride") {
        std::vector<double> fl;
        for (auto& h : hl) { h.f["flat"] = log_flatness(spectrum(x, sr, h, 0.08), 800, 12000); fl.push_back(h.f["flat"]); h.h.piece = "ride"; }
        Split g{};
        if (two_groups(fl, 0.03, 3.0, g)) for (auto& h : hl) if (h.f["flat"] < g.th) h.h.piece = "ride_bell";
    }
    if (name == "crash") {
        std::vector<double> lc;
        for (auto& h : hl) { h.f["lc"] = std::log(centroid(spectrum(x, sr, h, 0.10), 300)); lc.push_back(h.f["lc"]); h.h.piece = "crash1"; }
        Split g{};
        if (two_groups(lc, 0.05, 3.0, g)) for (auto& h : hl) if (h.f["lc"] > g.th) h.h.piece = "crash2";
    }
    // velocity: 36 dB of level against the stem's loud hits -> 1..127
    std::vector<double> lv;
    for (auto& h : hl) lv.push_back(h.h.level_db);
    const double ref = percentile(lv, 97), range = 36;
    for (auto& h : hl) {
        h.h.velocity = (int) std::clamp(std::lround(1 + 126 * (h.h.level_db - (ref - range)) / range), 1L, 127L);
        h.h.note = note_of(h.h.piece);
    }
}

// note length: until the sound has fallen 30 dB under its peak (or to the stem's noise floor), or the stem's next hit
// (a hi-hat closing chokes the open one; a drum's next stroke ends the previous note)
void note_lengths(const std::string& name, std::vector<Hit>& hl, const Signal& x, int sr)
{
    const long n = (long) (0.003 * sr);
    std::vector<double> frames;
    for (long s = 0; s + n <= (long) x.size(); s += n * 8) frames.push_back(rms_db(x, s, s + n));
    const double floor_db = percentile(frames, 10);
    for (size_t i = 0; i < hl.size(); ++i) {
        const double t = hl[i].h.time;
        double next = (double) x.size() / sr;  // the same piece's next stroke (any hi-hat stroke chokes a hi-hat)
        for (size_t j = i + 1; j < hl.size(); ++j)
            if (name == "hh" || hl[j].h.piece == hl[i].h.piece) { next = hl[j].h.time; break; }
        const long a = (long) (t * sr), stop = std::min((long) x.size(), (long) (std::min(next, t + 4.0) * sr));
        // the level the hit sat on (bleed, a ring): once the sound is back down near it, it's over
        const double before = rms_db(x, a - (long) (0.025 * sr), a - (long) (0.001 * sr));
        double peak = -180, end = (double) stop / sr;
        for (long s = a; s + n <= stop; s += n) {
            const double l = rms_db(x, s, s + n);
            peak = std::max(peak, l);
            // hats: a quiet pedal chick under a ride's bleed is over once it is back down at that bleed
            const double gone = name == "hh" ? std::max({peak - 30, floor_db + 3, std::min(before + 3, peak - 6)})
                                             : std::max(peak - 30, floor_db + 3);
            if (s - a > (long) (0.01 * sr) && l < gone) { end = (double) s / sr; break; }
        }
        hl[i].h.end = std::max(t + 0.010, std::min(end, next - 0.001));
    }
}

}  // namespace

std::vector<DrumHit> transcribe_drums(const std::map<std::string, Signal>& kit, int sr, const Progress& prog)
{
    std::map<std::string, std::vector<Hit>> hits;
    int k = 0;
    for (auto& [name, x] : kit) {
        if (!params().count(name)) continue;
        if (prog) prog(0.6 * k / (double) kit.size(), "Finding the " + name + " hits");
        hits[name] = detect_stem(name, x, sr);
        ++k;
    }
    remove_bleed(hits, kit, sr, prog);
    std::vector<DrumHit> out;
    for (auto& [name, hl] : hits) {
        const Signal& x = kit.at(name);
        articulate(name, hl, x, sr);
        note_lengths(name, hl, x, sr);
        for (auto& h : hl) out.push_back(h.h);
    }
    std::sort(out.begin(), out.end(), [](const DrumHit& a, const DrumHit& b) { return a.time < b.time; });
    if (prog) prog(1.0, std::to_string(out.size()) + " drum hits");
    return out;
}

}  // namespace ds
