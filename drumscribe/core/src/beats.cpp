// Beat and downbeat tracking from the transcribed events (no audio needed).
//
//  1. onset train: every drum hit / bass onset is an impulse weighted by instrument (kick, snare 1.0; crash 0.6;
//     toms 0.5; bass 0.5; hats, ride 0.4) and velocity.
//  2. global tempo: pairwise inter-onset histogram (1 ms bins, Gaussian smoothed) = autocorrelation of the train;
//     a harmonic comb (T, 2T, 3T, 4T) scores 50-220 BPM with a flat prior over 70-180 BPM; octave resolution between
//     T/2, T, 2T by how much of the kick/snare/bass energy falls on the beat grid ("backbeat test") x a log-tempo prior.
//  3. local tempo: the same comb on 6 s windows every 0.5 s, then Viterbi over a +-45 % log-tempo band (L1 penalty on
//     tempo change): follows drift, ritardando and sudden section tempo changes.
//  4. Ellis (2007) dynamic-programming beat tracking on a 200 Hz onset envelope with the time-varying period.
//  5. refinement: each beat is re-estimated from the hits that sit on beats nearby (weighted linear fit of hit time
//     vs beat number over a centred / left / right window, the one with the smallest residual wins: edge-preserving at
//     tempo changes). Sub-millisecond; per-instrument microtiming is kept relative to the ensemble.
//  6. extrapolation over [0, duration], meter (3, 4, or 2 dotted beats = 6/8) from beat-level pattern periodicity,
//     bar phase from kick-on-1 / snare-on-backbeat / crash / bass-on-1 evidence.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include "ds/modules.h"

namespace ds {
namespace {

enum Cls { KICK, SNARE, HH, TOMS, RIDE, CRASH, BASS, NCLS };
constexpr double kBaseW[NCLS] = {1.0, 1.0, 0.4, 0.5, 0.4, 0.6, 0.5};

int cls_of(const std::string& p, const std::string& stem)
{
    auto starts = [&](const char* s) { return p.rfind(s, 0) == 0; };
    if (p == "kick") return KICK;
    if (p == "snare" || p == "side_stick") return SNARE;
    if (starts("hh")) return HH;
    if (starts("tom") || starts("floor")) return TOMS;
    if (starts("ride")) return RIDE;
    if (starts("crash")) return CRASH;
    if (stem == "kick") return KICK;
    if (stem == "snare") return SNARE;
    if (stem == "hh") return HH;
    if (stem == "ride") return RIDE;
    if (stem == "crash") return CRASH;
    return TOMS;
}

struct Ev { double t, w; int c; };

std::vector<Ev> events(const std::vector<DrumHit>& drums, const std::vector<BassNote>& bass)
{
    std::vector<Ev> ev;
    for (auto& h : drums) {
        const int c = cls_of(h.piece, h.stem);
        const double v = std::max(1, std::min(127, h.velocity ? h.velocity : 90)) / 127.0;
        ev.push_back({h.time, kBaseW[c] * (0.35 + 0.65 * v), c});
    }
    for (auto& n : bass) {
        const double v = std::max(1, std::min(127, n.velocity ? n.velocity : 90)) / 127.0;
        ev.push_back({n.time, kBaseW[BASS] * (0.35 + 0.65 * v), BASS});
    }
    std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    return ev;
}

// ---------------------------------------------------------------- inter-onset (autocorrelation) histogram
constexpr double kMaxLag = 5.0;            // s
constexpr int kBins = 5001;                // 1 ms
constexpr double kMinLag = 0.06;

struct Pair { double t, lag, w; };

std::vector<Pair> pairs_of(const std::vector<Ev>& ev)
{
    std::vector<Pair> p;
    for (size_t i = 0; i < ev.size(); ++i)
        for (size_t j = i + 1; j < ev.size() && ev[j].t - ev[i].t <= kMaxLag; ++j) {
            const double l = ev[j].t - ev[i].t;
            if (l >= kMinLag) p.push_back({ev[i].t, l, ev[i].w * ev[j].w});
        }
    return p;
}

struct Hist {
    std::vector<double> h = std::vector<double>(kBins + 2, 0.0);
    double span = 1;  // length of the analysed stretch (overlap normalisation)
    void add(double lag, double w)
    {
        const double x = lag * 1000;
        const int i = (int) x;
        const double f = x - i;
        if (i < 0 || i + 1 >= kBins) return;
        h[i] += w * (1 - f);
        h[i + 1] += w * f;
    }
    // Gaussian-smoothed autocorrelation at lag L (s), sigma 10 ms, corrected for the shrinking overlap
    double at(double L) const
    {
        const double c = L * 1000;
        const int lo = std::max(0, (int) std::floor(c - 30)), hi = std::min(kBins - 1, (int) std::ceil(c + 30));
        double s = 0;
        for (int i = lo; i <= hi; ++i) { const double d = (i - c) / 10.0; s += h[i] * std::exp(-0.5 * d * d); }
        return s / std::max(0.25, 1 - L / span);
    }
    double comb(double T) const
    {
        static const double c[4] = {1.0, 0.5, 0.33, 0.25};
        double s = 0;
        for (int k = 1; k <= 4; ++k)
            if (k * T <= std::min(kMaxLag - 0.05, 0.75 * span)) s += c[k - 1] * at(k * T);
        return s;
    }
};

double range_prior(double bpm)  // flat 70..180, falls off outside (0.2 octave sd)
{
    double o = 0;
    if (bpm < 70) o = std::log2(70 / bpm);
    if (bpm > 180) o = std::log2(bpm / 180);
    return std::exp(-0.5 * (o / 0.2) * (o / 0.2));
}

// comb argmax over 50..220 BPM (flat prior 70..180) and the octave family around it, each re-centred on the comb
std::vector<double> tempo_candidates(const std::vector<Ev>& ev)
{
    Hist h;
    h.span = std::max(1.0, ev.back().t - ev.front().t);
    for (auto& p : pairs_of(ev)) h.add(p.lag, p.w);
    double best = 120, bs = -1;
    for (double b = 50; b <= 220; b += 0.1) {
        const double s = h.comb(60 / b) * range_prior(b);
        if (s > bs) { bs = s; best = b; }
    }
    std::vector<double> out;
    for (double m : {1.0, 0.5, 2.0, 2.0 / 3, 1.5}) {
        const double b = best * m;
        if (b < 50 || b > 230) continue;
        double lb = b, ls = -1;
        for (double x = b * 0.97; x <= b * 1.03; x += 0.05) {
            const double s = h.comb(60 / x);
            if (s > ls) { ls = s; lb = x; }
        }
        out.push_back(lb);
    }
    return out;
}

// ---------------------------------------------------------------- local tempo (windowed comb + Viterbi)
struct TempoCurve { std::vector<double> t, period; double at(double x) const; };
double TempoCurve::at(double x) const
{
    if (t.size() == 1 || x <= t.front()) return period.front();
    if (x >= t.back()) return period.back();
    const size_t i = std::upper_bound(t.begin(), t.end(), x) - t.begin();
    const double f = (x - t[i - 1]) / (t[i] - t[i - 1]);
    return period[i - 1] + f * (period[i] - period[i - 1]);
}

TempoCurve local_tempo(const std::vector<Ev>& ev, double g_bpm)
{
    std::vector<Pair> pr = pairs_of(ev);
    std::sort(pr.begin(), pr.end(), [](const Pair& a, const Pair& b) { return a.t < b.t; });
    const double W = std::max(6.0, 4.2 * 60 / g_bpm * 4 / 3), hop = 0.5;
    const double lo = ev.front().t, hi = ev.back().t;
    const int S = 109;  // log-tempo states, 0.01 octave apart, +-0.54 octave around the global tempo
    std::vector<double> bpm(S);
    for (int s = 0; s < S; ++s) bpm[s] = g_bpm * std::pow(2.0, (s - S / 2) * 0.01);
    std::vector<double> centres;
    std::vector<std::vector<double>> score;
    for (double c = lo; ; c += hop) {
        const double w0 = c - W / 2, w1 = c + W / 2;
        Hist h;
        h.span = W;
        auto it = std::lower_bound(pr.begin(), pr.end(), w0, [](const Pair& p, double v) { return p.t < v; });
        for (; it != pr.end() && it->t <= w1; ++it)
            if (it->t + it->lag <= w1) h.add(it->lag, it->w);
        std::vector<double> sc(S);
        double mx = 0;
        for (int s = 0; s < S; ++s) { sc[s] = h.comb(60 / bpm[s]); mx = std::max(mx, sc[s]); }
        for (auto& v : sc) v = mx > 0 ? v / mx : 0;
        centres.push_back(c);
        score.push_back(sc);
        if (c >= hi) break;
    }
    // Viterbi: maximise sum of normalised comb scores - lambda * |change in octaves| - mu * [changed]
    const double lambda = 3.0, mu = 0.02;
    const size_t N = centres.size();
    std::vector<std::vector<int>> back(N, std::vector<int>(S, 0));
    std::vector<double> acc = score[0];
    for (int s = 0; s < S; ++s) acc[s] -= 0.15 * std::abs(s - S / 2) * 0.01;  // mild pull to the global tempo
    for (size_t n = 1; n < N; ++n) {
        std::vector<double> nx(S);
        for (int s = 0; s < S; ++s) {
            double b = -1e18;
            int bi = s;
            for (int p = 0; p < S; ++p) {
                const double v = acc[p] - lambda * std::abs(s - p) * 0.01 - (p != s ? mu : 0);
                if (v > b) { b = v; bi = p; }
            }
            nx[s] = b + score[n][s];
            back[n][s] = bi;
        }
        acc = nx;
    }
    int s = (int) (std::max_element(acc.begin(), acc.end()) - acc.begin());
    TempoCurve tc;
    tc.t = centres;
    tc.period.assign(N, 0);
    for (size_t n = N; n-- > 0;) { tc.period[n] = 60 / bpm[s]; if (n) s = back[n][s]; }
    return tc;
}

// ---------------------------------------------------------------- Ellis DP
std::vector<double> dp_beats(const std::vector<Ev>& ev, const TempoCurve& tc)
{
    const double fs = 200, t0 = ev.front().t - 0.1, t1 = ev.back().t + 0.1;
    const int n = (int) std::ceil((t1 - t0) * fs) + 1;
    std::vector<double> O(n, 0.0);
    double mw = 0;
    for (auto& e : ev) mw += e.w;
    mw /= ev.size();
    for (auto& e : ev) {
        const double c = (e.t - t0) * fs;
        for (int i = std::max(0, (int) c - 6); i <= std::min(n - 1, (int) c + 7); ++i) {
            const double d = (i - c) / 2.0;
            O[i] += e.w / mw * std::exp(-0.5 * d * d);
        }
    }
    std::vector<double> P(n), logP(n);
    for (int i = 0; i < n; ++i) { P[i] = tc.at(t0 + i / fs) * fs; logP[i] = std::log(P[i]); }
    const int maxd = (int) std::ceil(2 * *std::max_element(P.begin(), P.end())) + 2;
    std::vector<double> logd(maxd + 1, 0.0);
    for (int d = 1; d <= maxd; ++d) logd[d] = std::log((double) d);
    const double alpha = 100;
    std::vector<double> C(n);
    std::vector<int> bk(n, -1);
    for (int t = 0; t < n; ++t) {
        const int dlo = std::max(1, (int) std::round(P[t] / 2)), dhi = (int) std::round(2 * P[t]);
        double best = -1e18;
        int bi = -1;
        for (int d = dlo; d <= dhi && t - d >= 0; ++d) {
            const double x = logd[d] - logP[t];
            const double v = C[t - d] - alpha * x * x;
            if (v > best) { best = v; bi = t - d; }
        }
        C[t] = O[t] + (bi >= 0 ? best : 0);
        bk[t] = bi;
    }
    const int span = (int) std::round(P[n - 1]);
    int e = n - 1;
    for (int t = std::max(0, n - 1 - span); t < n; ++t) if (C[t] > C[e]) e = t;
    std::vector<double> beats;
    for (int t = e; t >= 0; t = bk[t]) beats.push_back(t0 + t / fs);
    std::reverse(beats.begin(), beats.end());
    return beats;
}

// beat position (beat number, fractional) of time t on a beat list, extrapolating at both ends
double beat_pos(const std::vector<double>& b, double t)
{
    const size_t n = b.size();
    if (n < 2) return 0;
    if (t < b[0]) return (t - b[0]) / (b[1] - b[0]);
    if (t >= b[n - 1]) return (n - 1) + (t - b[n - 1]) / (b[n - 1] - b[n - 2]);
    const size_t i = std::upper_bound(b.begin(), b.end(), t) - b.begin() - 1;
    return i + (t - b[i]) / (b[i + 1] - b[i]);
}

// ---------------------------------------------------------------- sub-ms refinement
std::vector<double> refine(const std::vector<double>& beats, const std::vector<Ev>& ev)
{
    const int n = (int) beats.size();
    if (n < 3) return beats;
    std::vector<std::vector<std::pair<double, double>>> on(n);  // per beat: (time, weight) of the hits on it
    for (auto& e : ev) {
        const double p = beat_pos(beats, e.t);
        const double k = std::round(p);
        if (std::abs(p - k) < 0.15 && k >= 0 && k < n) on[(int) k].push_back({e.t, e.w});
    }
    std::vector<double> out = beats;
    for (int j = 0; j < n; ++j) {
        const double P = j + 1 < n ? beats[j + 1] - beats[j] : beats[j] - beats[j - 1];
        double best = 0, bcost = 1e18;
        bool ok = false;
        const int wins[3][2] = {{-2, 2}, {-3, 0}, {0, 3}};
        for (int w = 0; w < 3; ++w) {
            double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
            int cnt = 0, nx = 0;
            for (int x = wins[w][0]; x <= wins[w][1]; ++x) {
                const int k = j + x;
                if (k < 0 || k >= n || on[k].empty()) continue;
                ++nx;
                for (auto& [t, wt] : on[k]) { sw += wt; sx += wt * x; sy += wt * t; sxx += wt * x * x; sxy += wt * x * t; ++cnt; }
            }
            if (sw <= 0) continue;
            double a, b;
            if (nx >= 2) {
                const double den = sw * sxx - sx * sx;
                b = (sw * sxy - sx * sy) / den;
                a = (sy - b * sx) / sw;
                if (b < 0.6 * P || b > 1.6 * P) continue;
            } else {
                if (on[j].empty()) continue;  // only one beat with hits and it is not this one
                a = sy / sw; b = P;
            }
            double ssr = 0;
            for (int x = wins[w][0]; x <= wins[w][1]; ++x) {
                const int k = j + x;
                if (k < 0 || k >= n) continue;
                for (auto& [t, wt] : on[k]) { const double r = t - (a + b * x); ssr += wt * r * r; }
            }
            double cost = ssr / sw * (cnt > 2 ? (double) cnt / (cnt - 2) : 4.0) + 1e-8;
            if (w == 0) cost *= 0.8;
            if (nx < 2) cost += 1e-4;  // single-beat estimate: only when nothing better
            if (cost < bcost) { bcost = cost; best = a; ok = true; }
        }
        if (ok && std::abs(best - beats[j]) < 0.25 * P) out[j] = best;
    }
    // keep strictly increasing (a refined beat may not cross its neighbours)
    for (int j = 1; j < n; ++j)
        if (out[j] <= out[j - 1] + 0.3 * (beats[j] - beats[j - 1])) out[j] = beats[j] > out[j - 1] ? beats[j] : out[j - 1] + 0.3 * (beats[j] - beats[j - 1]);
    return out;
}

double beat_salience(const std::vector<double>& b, const std::vector<Ev>& ev)
{
    const int n = (int) b.size();
    std::vector<char> filled(n, 0);
    double on = 0, all = 0;
    int lo = n, hi = -1;
    for (auto& e : ev) {
        if (e.c != KICK && e.c != SNARE && e.c != BASS && e.c != CRASH) continue;
        const double p = beat_pos(b, e.t), k = std::round(p);
        all += e.w;
        if (std::abs(p - k) < 0.1) {
            on += e.w;
            if (k >= 0 && k < n) filled[(int) k] = 1;
        }
        const int kk = (int) std::max(0.0, std::min(n - 1.0, k));
        lo = std::min(lo, kk); hi = std::max(hi, kk);
    }
    if (all <= 0 || hi < lo) return 0;
    int f = 0;
    for (int i = lo; i <= hi; ++i) f += filled[i];
    const double fill = (double) f / (hi - lo + 1);
    const double bpm = 60.0 * (n - 1) / (b.back() - b.front());
    const double o = bpm < 60 ? std::log2(60 / bpm) : bpm > 190 ? std::log2(bpm / 190) : 0.0;
    const double pr = std::exp(-0.5 * (o / 0.3) * (o / 0.3));  // flat 60..190 BPM
    return on / all * fill * pr;
}

double median_of(std::vector<double> v)
{
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

void extrapolate(std::vector<double>& b, double end)
{
    if (b.empty()) return;
    if (b.size() == 1) b.push_back(b[0] + 0.5);
    std::vector<double> d0, d1;
    for (size_t i = 1; i < b.size() && i <= 4; ++i) d0.push_back(b[i] - b[i - 1]);
    for (size_t i = b.size() - 1; i >= 1 && d1.size() < 4; --i) d1.push_back(b[i] - b[i - 1]);
    const double p0 = median_of(d0), p1 = median_of(d1);
    std::vector<double> pre;
    for (double t = b.front() - p0; t >= -1e-9; t -= p0) pre.push_back(std::max(0.0, t));
    std::reverse(pre.begin(), pre.end());
    b.insert(b.begin(), pre.begin(), pre.end());
    while (b.back() < end) b.push_back(b.back() + p1);
}

// ---------------------------------------------------------------- meter and downbeats
void meter_and_downbeats(BeatGrid& g, const std::vector<Ev>& ev)
{
    const int n = (int) g.beats.size();
    g.downbeats.clear();
    if (n == 0) return;
    std::vector<std::array<double, NCLS>> F(n);
    for (auto& f : F) f.fill(0);
    double w13 = 0, w23 = 0, wbin = 0;
    int first = n, last = -1;
    for (auto& e : ev) {
        const double p = beat_pos(g.beats, e.t);
        const double k = std::round(p);
        const double d = p - std::floor(p);
        if (std::abs(p - k) < 0.12) {
            if (k >= 0 && k < n) { F[(int) k][e.c] += e.w; first = std::min(first, (int) k); last = std::max(last, (int) k); }
        } else if (e.c != CRASH) {
            if (std::abs(d - 1.0 / 3) < 0.05) w13 += e.w;
            else if (std::abs(d - 2.0 / 3) < 0.05) w23 += e.w;
            else if (std::abs(d - 0.25) < 0.05 || std::abs(d - 0.5) < 0.05 || std::abs(d - 0.75) < 0.05) wbin += e.w;
        }
    }
    int M = 4;
    if (last > first) {
        std::array<double, NCLS> mean{};
        for (int c = 0; c < NCLS; ++c) {
            double s = 0; int k = 0;
            for (int i = first; i <= last; ++i) if (F[i][c] > 0) { s += F[i][c]; ++k; }
            mean[c] = k ? s / k : 1;
        }
        auto D = [&](int lag) {
            double s = 0; int k = 0;
            for (int i = first; i + lag <= last; ++i, ++k)
                for (int c : {KICK, SNARE, CRASH, BASS, TOMS}) s += std::abs(F[i][c] - F[i + lag][c]) / mean[c] * (c == CRASH ? 0.5 : 1);
            return k ? s / k : 1e9;
        };
        const double d2 = D(2), d3 = D(3), d4 = D(4);
        const double off = w13 + w23 + wbin;
        const bool ternary = off > 0 && w13 > 0.2 * off && w23 > 0.2 * off && (w13 + w23) > 1.5 * wbin;
        if (d3 < 0.85 * std::min(d2, d4)) M = 3;
        else if (ternary && d2 <= 1.1 * d4) M = 2;
    }
    g.beats_per_bar = M;
    // bar phase
    auto wpos = [&](int pos, const std::array<double, NCLS>& f) {
        double s = 0;
        if (pos == 0) s += f[KICK] + 1.5 * f[CRASH] + 0.5 * f[BASS] - 0.7 * f[SNARE];
        if (M == 4 && (pos == 1 || pos == 3)) s += f[SNARE];
        if (M == 4 && pos == 2) s += 0.3 * f[KICK];
        if (M == 3 && pos > 0) s += 0.5 * f[SNARE];
        if (M == 2 && pos == 1) s += f[SNARE];
        return s;
    };
    int phase = 0;
    double bs = -1e18;
    for (int ph = 0; ph < M; ++ph) {
        double s = 0;
        for (int i = 0; i < n; ++i) s += wpos(((i - ph) % M + M) % M, F[i]);
        if (s > bs + 1e-12) { bs = s; phase = ph; }
    }
    for (int i = phase; i < n; i += M) g.downbeats.push_back(i);
}

void finish(BeatGrid& g)
{
    const size_t n = g.beats.size();
    g.bpm_at_beat.clear();
    for (size_t i = 0; i + 1 < n; ++i) g.bpm_at_beat.push_back(60.0 / (g.beats[i + 1] - g.beats[i]));
    g.bpm_at_beat.push_back(g.bpm_at_beat.empty() ? g.mean_bpm : g.bpm_at_beat.back());
    if (n >= 2) g.mean_bpm = 60.0 * (n - 1) / (g.beats.back() - g.beats.front());
}

BeatGrid constant_grid(const std::vector<Ev>& ev, double bpm, double end)
{
    BeatGrid g;
    g.mean_bpm = bpm;
    const double P = 60.0 / bpm;
    double phase = 0;
    if (!ev.empty()) {
        // coarse: phase maximising the Gaussian (15 ms) agreement of the hits with the grid
        double bs = -1;
        for (double ph = 0; ph < P; ph += 0.001) {
            double s = 0;
            for (auto& e : ev) {
                double d = std::fmod(e.t - ph, P);
                if (d < 0) d += P;
                d = std::min(d, P - d);
                s += e.w * std::exp(-0.5 * (d / 0.015) * (d / 0.015));
            }
            if (s > bs) { bs = s; phase = ph; }
        }
        // fine: weighted mean deviation of the hits within P/8 of the grid
        for (int it = 0; it < 3; ++it) {
            double sw = 0, sd = 0;
            for (auto& e : ev) {
                double d = std::remainder(e.t - phase, P);
                if (std::abs(d) < P / 8) { sw += e.w; sd += e.w * d; }
            }
            if (sw > 0) phase += sd / sw;
        }
        phase = std::fmod(phase, P);
        if (phase < 0) phase += P;
    }
    for (double t = phase; ; t += P) { g.beats.push_back(t); if (t >= end) break; }
    meter_and_downbeats(g, ev);
    finish(g);
    g.mean_bpm = bpm;
    return g;
}

}  // namespace

BeatGrid track_beats(const std::vector<DrumHit>& drums, const std::vector<BassNote>& bass, double duration,
                     double fixed_bpm)
{
    const std::vector<Ev> ev = events(drums, bass);
    double end = duration;
    if (!ev.empty()) end = std::max(end, ev.back().t);
    if (end <= 0) end = 1;
    if (fixed_bpm > 0) return constant_grid(ev, fixed_bpm, end);
    if (ev.size() < 6) return constant_grid(ev, 120, end);

    // track at every candidate of the octave family, keep the one where the beat-carrying instruments (kick, snare,
    // bass, crash) sit on the beats (on-share) AND the beats are filled by them (fill), times a mild log-tempo prior
    std::vector<double> b;
    double best = -1;
    for (double cand : tempo_candidates(ev)) {
        const TempoCurve tc = local_tempo(ev, cand);
        std::vector<double> x = dp_beats(ev, tc);
        if (x.size() < 3) continue;
        for (int it = 0; it < 2; ++it) x = refine(x, ev);
        const double sc = beat_salience(x, ev);
        if (std::getenv("DS_BEATS_DEBUG")) std::fprintf(stderr, "cand %.2f -> %zu beats, salience %.4f\n", cand, x.size(), sc);
        if (sc > best) { best = sc; b = x; }
    }
    if (b.size() < 3) return constant_grid(ev, 120, end);
    extrapolate(b, end);
    BeatGrid g;
    g.beats = b;
    finish(g);
    meter_and_downbeats(g, ev);
    return g;
}

}  // namespace ds
