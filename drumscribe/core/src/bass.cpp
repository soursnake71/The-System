// Drum Scribe core: bass transcription (monophonic bass stem -> notes with sample-accurate onsets, releases, pitch,
// slides, bends, velocity and accents).
//
// Pipeline
//  1. pitch: decimate to ~11 kHz, YIN difference function with a centred window (every lag compared symmetrically
//     around the frame centre, so the frame time does not depend on the lag), updated incrementally every ~1 ms;
//     pYIN-style candidate probabilities (beta prior over thresholds) + Viterbi over candidates/unvoiced with a
//     semitone-jump cost -> octave-robust f0 track on a 1 ms grid.
//  2. onsets: period-adaptive forward/backward energy ratio (window >= one period, so no ripple) -> candidates;
//     each refined to the sample with a long-term (pitch-period) predictor of the previous note: the residual is the
//     part of the signal the old note cannot explain, and the change point is found by CUSUM on it.
//     Candidates that are only kick bleed on a held note (same pitch, no dip, low novelty) are dropped.
//  3. per segment: release from a pitch-synchronous log-envelope (continuous hinge fit = where energy falls away), or
//     the next onset (legato); semitone DP over the pitch curve -> held levels; a level change without a new attack
//     is a slide (glide start fitted with an exponential-glide model through the YIN window), excursions that return
//     are bends; in-note pitch movement -> bends (decimated to 5 cent changes).
//  4. kick bleed / noise: unvoiced or < 30 ms voiced segments, down-sweeping fast-decaying thumps, very quiet notes
//     are dropped. Velocity from the note's peak level (dB, relative to the song's loud notes), accent when a note
//     is clearly louder than its neighbours.
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <cstdio>
#include <cstdlib>

#include "ds/modules.h"

namespace ds {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

double db(double p) { return 10.0 * std::log10(std::max(p, 1e-30)); }
double hz_midi(double f) { return 69.0 + 12.0 * std::log2(f / 440.0); }

double percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    size_t i = std::min(v.size() - 1, size_t(q * (v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + i, v.end());
    return v[i];
}
double median(std::vector<double> v) { return percentile(std::move(v), 0.5); }

// ---------------------------------------------------------------------------------------------------- signal
struct Sig {
    std::vector<double> x;   // DC-blocked input
    std::vector<double> S;   // prefix sums of x^2
    int sr = kSR;
    long N = 0;
    double ms(double m) const { return m * 1e-3 * sr; }
    long msl(double m) const { return std::lround(m * 1e-3 * sr); }
    // mean power over [a,b), samples outside the signal count as zero
    double pow(long a, long b) const {
        if (b <= a) return 0;
        long aa = std::clamp(a, 0L, N), bb = std::clamp(b, 0L, N);
        return (S[bb] - S[aa]) / double(b - a);
    }
    double at(long n) const { return (n >= 0 && n < N) ? x[n] : 0.0; }
    // 4-point cubic (Catmull-Rom) fractional read
    double frac(double t) const {
        long i = long(std::floor(t));
        double f = t - i;
        double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        return y1 + 0.5 * f * (y2 - y0 + f * (2 * y0 - 5 * y1 + 4 * y2 - y3 + f * (3 * (y1 - y2) + y3 - y0)));
    }
};

// ---------------------------------------------------------------------------------------------------- pitch
struct Cand { float tau, dp, p; };
struct Track {
    int D = 4, hop = 11, H = 44, W = 0, T = 0;
    double srd = 11025;
    std::vector<float> f0, dp, pdb;  // per grid frame (frame k centred at full-rate sample k*H)
    std::vector<std::vector<Cand>> cands;
    double tsec(long k) const { return double(k) * H / (srd * D); }
};

std::vector<float> decimate(const std::vector<double>& x, int D) {
    std::vector<float> y((x.size() + D - 1) / D);
    if (D == 1) {
        for (size_t i = 0; i < x.size(); ++i) y[i] = float(x[i]);
        return y;
    }
    const int half = 16 * D;
    std::vector<double> h(2 * half + 1);
    double fc = 0.45 / D, sum = 0;
    for (int j = 0; j <= 2 * half; ++j) {
        double t = j - half;
        double s = t == 0 ? 2 * fc : std::sin(2 * kPi * fc * t) / (kPi * t);
        double w = 0.42 - 0.5 * std::cos(2 * kPi * j / (2 * half)) + 0.08 * std::cos(4 * kPi * j / (2 * half));
        h[j] = s * w;
        sum += h[j];
    }
    for (auto& v : h) v /= sum;
    const long N = long(x.size());
    for (size_t m = 0; m < y.size(); ++m) {
        double acc = 0;
        long c = long(m) * D;
        long j0 = std::max(0L, half - c), j1 = std::min(2L * half, N - 1 - c + half);
        for (long j = j0; j <= j1; ++j) acc += h[j] * x[c + j - half];
        y[m] = float(acc);
    }
    return y;
}

Track track_pitch(const Sig& s, const Progress& p) {
    Track tr;
    tr.D = std::max(1, int(std::lround(s.sr / 11025.0)));
    tr.srd = double(s.sr) / tr.D;
    tr.hop = std::max(1, int(std::lround(0.001 * tr.srd)));
    tr.H = tr.hop * tr.D;
    tr.W = int(std::lround(0.034 * tr.srd));
    tr.T = int(std::ceil(tr.srd / 28.0));
    const int W = tr.W, T = tr.T, hop = tr.hop, Wh = W / 2;
    const int tmin = std::max(2, int(std::floor(tr.srd / 900.0)));
    std::vector<float> y = decimate(s.x, tr.D);
    const long M = long(y.size());
    const long OFF = T + W + hop + 8;
    std::vector<double> yp(M + 2 * OFF, 0.0);
    for (long i = 0; i < M; ++i) yp[i + OFF] = y[i];
    std::vector<double> Y2(yp.size() + 1, 0.0);
    for (size_t i = 0; i < yp.size(); ++i) Y2[i + 1] = Y2[i] + yp[i] * yp[i];
    auto Y = [&](long i) { return yp[i + OFF]; };

    const long K = M / hop + 1;
    tr.f0.assign(K, 0.f);
    tr.dp.assign(K, 1.f);
    tr.pdb.assign(K, -300.f);
    tr.cands.assign(K, {});

    // beta(2,18) prior over YIN thresholds 0.01..1.00
    std::vector<double> prior(100);
    double ps = 0;
    for (int i = 0; i < 100; ++i) {
        double th = 0.01 * (i + 1);
        prior[i] = th * std::pow(1 - th, 17);
        ps += prior[i];
    }
    for (auto& v : prior) v /= ps;

    std::vector<double> d(T + 2, 0.0), dpv(T + 2, 1.0);
    std::vector<int> a(T + 2), b(T + 2);
    for (int t = 0; t <= T + 1; ++t) { a[t] = t / 2; b[t] = t - t / 2; }
    auto full = [&](long c) {
        long j0 = c - Wh;
        for (int t = 1; t <= T + 1; ++t) {
            double acc = 0;
            for (long j = j0; j < j0 + W; ++j) {
                double v = Y(j - a[t]) - Y(j + b[t]);
                acc += v * v;
            }
            d[t] = acc;
        }
    };
    std::vector<double> powv(K);
    for (long k = 0; k < K; ++k) {
        long c = k * hop;
        if (k % 256 == 0) {
            full(c);
        } else {
            long jr = c - hop - Wh, ja = c - hop - Wh + W;  // remove [jr, jr+hop), add [ja, ja+hop)
            for (int t = 1; t <= T + 1; ++t) {
                const int at = a[t], bt = b[t];
                double acc = 0;
                for (long j = ja; j < ja + hop; ++j) {
                    double v = Y(j - at) - Y(j + bt);
                    acc += v * v;
                }
                for (long j = jr; j < jr + hop; ++j) {
                    double v = Y(j - at) - Y(j + bt);
                    acc -= v * v;
                }
                d[t] = std::max(0.0, d[t] + acc);
            }
        }
        double pw = (Y2[c - Wh + W + OFF] - Y2[c - Wh + OFF]) / W;
        powv[k] = pw;
        tr.pdb[k] = float(db(pw));
        // CMNDF
        double run = 0;
        dpv[0] = 1;
        for (int t = 1; t <= T + 1; ++t) {
            run += d[t];
            dpv[t] = run > 0 ? d[t] * t / run : 1.0;
        }
        auto& cs = tr.cands[k];
        for (int t = tmin; t <= T; ++t) {
            if (dpv[t] < dpv[t - 1] && dpv[t] <= dpv[t + 1] && dpv[t] < 0.85) {
                double den = d[t - 1] - 2 * d[t] + d[t + 1];
                double off = den > 0 ? 0.5 * (d[t - 1] - d[t + 1]) / den : 0.0;
                off = std::clamp(off, -0.5, 0.5);
                double dden = dpv[t - 1] - 2 * dpv[t] + dpv[t + 1];
                double dmin = dpv[t];
                if (dden > 0) {
                    double o2 = std::clamp(0.5 * (dpv[t - 1] - dpv[t + 1]) / dden, -0.5, 0.5);
                    dmin = dpv[t] - 0.25 * (dpv[t - 1] - dpv[t + 1]) * o2;
                }
                cs.push_back({float(t + off), float(std::max(0.0, dmin)), 0.f});
            }
        }
        if (cs.size() > 12) {  // keep the 12 deepest, in lag order
            std::vector<Cand> c2 = cs;
            std::nth_element(c2.begin(), c2.begin() + 11, c2.end(),
                             [](const Cand& u, const Cand& v) { return u.dp < v.dp; });
            float lim = c2[11].dp;
            std::vector<Cand> keep;
            for (auto& c0 : cs)
                if (c0.dp <= lim && keep.size() < 12) keep.push_back(c0);
            cs = keep;
        }
        if (!cs.empty()) {
            size_t gmin = 0;
            for (size_t i = 1; i < cs.size(); ++i)
                if (cs[i].dp < cs[gmin].dp) gmin = i;
            for (int i = 0; i < 100; ++i) {
                double th = 0.01 * (i + 1);
                bool found = false;
                for (auto& c0 : cs)
                    if (c0.dp < th) { c0.p += float(prior[i]); found = true; break; }
                if (!found) cs[gmin].p += float(prior[i]);
            }
        }
        if (p && k % 2000 == 0) p(0.6 * double(k) / K, "bass: pitch");
    }

    // voicing weight from frame power relative to the loud part of the song
    std::vector<double> pv;
    for (long k = 0; k < K; ++k)
        if (powv[k] > 0) pv.push_back(tr.pdb[k]);
    const double ref_db = pv.empty() ? -100 : percentile(pv, 0.99);
    const double floor_db = std::max(ref_db - 50.0, -110.0);

    // Viterbi over candidates (+ unvoiced state, index = cands.size())
    const double kVU = 2.5;
    std::vector<std::vector<int>> back(K);
    std::vector<double> prev_cost, cur_cost;
    std::vector<float> prev_tau;
    for (long k = 0; k < K; ++k) {
        auto& cs = tr.cands[k];
        double v = std::clamp((tr.pdb[k] - floor_db) / 12.0, 0.0, 1.0);
        size_t n = cs.size();
        cur_cost.assign(n + 1, 0);
        back[k].assign(n + 1, 0);
        // voicing from the best aperiodicity and the level; candidate choice from the pYIN threshold mass
        double dmin = 1;
        for (auto& c0 : cs) dmin = std::min(dmin, double(c0.dp));
        const double pvo = v * std::clamp((0.55 - dmin) / 0.4, 0.0, 1.0);
        for (size_t i = 0; i <= n; ++i) {
            double obs = i < n ? -std::log(pvo + 1e-3) - std::log(cs[i].p + 1e-3) : -std::log(1.0 - pvo + 1e-3);
            if (k == 0) {
                cur_cost[i] = obs;
                continue;
            }
            double best = 1e300;
            int bi = 0;
            size_t pn = prev_tau.size();
            for (size_t j = 0; j <= pn; ++j) {
                double tc;
                if (i == n && j == pn) tc = 0;
                else if (i == n || j == pn) tc = kVU;
                else {
                    double st = 12.0 * std::fabs(std::log2(double(cs[i].tau) / prev_tau[j]));
                    tc = st <= 0.25 ? 0.0 : std::min(6.0, 1.5 * (st - 0.25));
                }
                double c = prev_cost[j] + tc;
                if (c < best) { best = c; bi = int(j); }
            }
            cur_cost[i] = best + obs;
            back[k][i] = bi;
        }
        prev_cost = cur_cost;
        prev_tau.resize(n);
        for (size_t i = 0; i < n; ++i) prev_tau[i] = cs[i].tau;
    }
    if (K > 0) {
        int st = int(std::min_element(prev_cost.begin(), prev_cost.end()) - prev_cost.begin());
        for (long k = K - 1; k >= 0; --k) {
            auto& cs = tr.cands[k];
            if (st < int(cs.size())) {
                tr.f0[k] = float(tr.srd / cs[st].tau);
                tr.dp[k] = cs[st].dp;
            }
            st = back[k][st];
        }
    }
    return tr;
}

// ---------------------------------------------------------------------------------------------------- onsets
struct Predictor {
    double P = 0, g = 0;  // x^[n] = g * x[n - P]; P = 0 -> predicts zero
};

// fit a long-term predictor on [a, b) around period P0
Predictor fit_predictor(const Sig& s, long a, long b, double P0) {
    Predictor best;
    if (P0 <= 2 || b - a < 16) return best;
    double ex = 0;
    for (long n = a; n < b; ++n) ex += s.at(n) * s.at(n);
    if (ex <= 0) return best;
    double bestrho = 0.6;
    for (int i = -40; i <= 40; ++i) {
        double P = P0 * (1 + 0.0005 * i);
        double xy = 0, yy = 0;
        for (long n = a; n < b; ++n) {
            double v = s.frac(n - P);
            xy += s.at(n) * v;
            yy += v * v;
        }
        if (yy <= 0) continue;
        double rho = xy / std::sqrt(ex * yy);
        if (rho > bestrho) {
            bestrho = rho;
            best.P = P;
            best.g = xy / yy;
        }
    }
    return best;
}

struct Onset {
    long n = 0;          // refined attack start (sample)
    double strength = 0; // dB
    double novelty = 1;  // residual / signal energy just after the onset (1 = all new)
    bool dip = false;    // the level dropped just before the onset
};

// refine a coarse onset to the attack start
void refine_onset(const Sig& s, Onset& o, long lo, double Pprev, double ref_pow) {
    const long nc = o.n;
    const long s0 = std::max(lo, nc - s.msl(15)), s1 = std::min(s.N - 1, nc + s.msl(8));
    const long pr0 = std::max(lo - s.msl(10), s0 - s.msl(30));
    if (s1 <= s0) return;
    Predictor pr;
    if (Pprev > 0 && s.pow(pr0, s0) > ref_pow * 1e-6) {
        long a = std::max(pr0, long(lo - s.msl(10) + Pprev + 2));
        if (s0 - a > s.ms(4)) pr = fit_predictor(s, a, s0, Pprev);
    }
    const long e1 = std::min(s.N, s1 + s.msl(20));
    // r: residual of the previous note's prediction; q: its one-sided part (|x| above what the old note explains),
    // so a fast decay (mute, gate-off) of the old note is not mistaken for the new attack
    std::vector<double> r(e1 - pr0), q(e1 - pr0);
    for (long n = pr0; n < e1; ++n) {
        double xh = pr.P > 0 ? pr.g * s.frac(n - pr.P) : 0.0;
        r[n - pr0] = s.at(n) - xh;
        q[n - pr0] = std::max(0.0, std::fabs(s.at(n)) - std::fabs(xh));
    }
    auto R = [&](long n) { return r[n - pr0]; };
    auto Q = [&](long n) { return q[n - pr0]; };
    double sig2 = 0;
    long cnt = 0;
    for (long n = std::max(pr0, s0 - s.msl(10)); n < s0; ++n) { sig2 += Q(n) * Q(n); ++cnt; }
    sig2 = std::max(cnt ? sig2 / cnt : 0.0, ref_pow * 1e-9);
    double A = 0;
    for (long n = s0; n < e1; ++n) A = std::max(A, Q(n));
    if (A * A >= 30 * sig2) {
        double thr = std::max(4 * std::sqrt(sig2), 0.1 * A);
        long n1 = s0;
        while (n1 < e1 - 1 && Q(n1) <= thr) ++n1;
        double c = 0, cmin = 0;
        long nb = s0;
        for (long n = s0; n <= n1; ++n) {
            if (c < cmin) { cmin = c; nb = n; }
            c += Q(n) * Q(n) - 4 * sig2;
        }
        o.n = nb;
    }
    // novelty: how much of the signal right after the onset the previous note cannot explain
    if (pr.P > 0) {
        double er = 0, ex = 0;
        for (long n = o.n; n < std::min(e1, o.n + s.msl(20)); ++n) {
            er += R(n) * R(n);
            ex += s.at(n) * s.at(n);
        }
        o.novelty = ex > 0 ? er / ex : 1;
    }
}

// ---------------------------------------------------------------------------------------------------- glide model
struct Glide { double t0 = 0, tau = 0, target = 0, rmse = 1e9; };

// fit p(t) = from (t < t0), target + (from - target) exp(-(t - t0)/tau), seen through the YIN window (box of Wsec)
Glide fit_glide(const std::vector<double>& t, const std::vector<double>& m, double from, double ta, double tb,
                double Wsec) {
    Glide best;
    if (t.size() < 4 || tb <= ta) return best;
    const double h = Wsec / 2;
    for (double t0 = ta; t0 <= tb; t0 += 0.0005) {
        for (int it = 0; it < 28; ++it) {
            double tau = 0.0015 * std::pow(1.17, it);
            double sxy = 0, syy = 0;
            static thread_local std::vector<double> E;
            E.resize(t.size());
            for (size_t k = 0; k < t.size(); ++k) {
                double a = t[k] - h, b = t[k] + h, e;
                if (b <= t0) e = 1;
                else {
                    double p1 = std::max(0.0, t0 - a);
                    double lo = std::max(a, t0);
                    double p2 = tau * (std::exp(-(lo - t0) / tau) - std::exp(-(b - t0) / tau));
                    e = (p1 + p2) / (b - a);
                }
                E[k] = e;
                sxy += (m[k] - from * e) * (1 - e);
                syy += (1 - e) * (1 - e);
            }
            if (syy < 1e-9) continue;
            double tg = sxy / syy, sse = 0;
            for (size_t k = 0; k < t.size(); ++k) {
                double v = m[k] - from * E[k] - tg * (1 - E[k]);
                sse += v * v;
            }
            double rm = std::sqrt(sse / t.size());
            if (rm < best.rmse) best = {t0, tau, tg, rm};
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------------- notes
struct Level { long k0, k1; int semi; double pitch; bool stable; };

struct Work {
    const Sig& s;
    const Track& tr;
    double ref_pow, ref_db, tune = 0;
    double Wsec;
    Work(const Sig& s_, const Track& t_) : s(s_), tr(t_) {}
    double tk(long k) const { return double(k) * tr.H / s.sr; }
    long kof(long n) const { return std::lround(double(n) / tr.H); }
    float midi(long k) const {
        if (k < 0 || k >= long(tr.f0.size()) || tr.f0[k] <= 0) return kNaN;
        return float(hz_midi(tr.f0[k]));
    }
    double period(long k) const {  // samples, 0 if unvoiced
        if (k < 0 || k >= long(tr.f0.size()) || tr.f0[k] <= 0) return 0;
        return s.sr / tr.f0[k];
    }
};

}  // namespace

std::vector<BassNote> transcribe_bass(const Signal& bass, int sr, const Progress& p) {
    std::vector<BassNote> out;
    if (bass.size() < size_t(sr / 10) || sr <= 0) return out;
    Sig s;
    s.sr = sr;
    s.N = long(bass.size());
    s.x.resize(s.N);
    {  // causal DC blocker (~5 Hz): does not move attacks earlier
        double R = std::exp(-2 * kPi * 5.0 / sr), xm = 0, ym = 0;
        for (long n = 0; n < s.N; ++n) {
            double v = bass[n];
            ym = v - xm + R * ym;
            xm = v;
            s.x[n] = ym;
        }
    }
    s.S.assign(s.N + 1, 0.0);
    for (long n = 0; n < s.N; ++n) s.S[n + 1] = s.S[n] + s.x[n] * s.x[n];

    Track tr = track_pitch(s, p);
    Work w(s, tr);
    w.Wsec = double(tr.W) / tr.srd;
    const long K = long(tr.f0.size());
    const long H = tr.H;

    // reference level: the loud part of the song (10 ms power)
    {
        std::vector<double> pw;
        for (long n = 0; n + s.msl(10) <= s.N; n += s.msl(5)) pw.push_back(s.pow(n, n + s.msl(10)));
        w.ref_pow = std::max(percentile(pw, 0.99), 1e-20);
        w.ref_db = db(w.ref_pow);
    }
    const double ref_pow = w.ref_pow;

    // global tuning (circular mean of the deviation from equal temperament)
    {
        double cs = 0, sn = 0;
        for (long k = 0; k < K; ++k) {
            if (tr.f0[k] <= 0 || tr.dp[k] > 0.2) continue;
            double m = hz_midi(tr.f0[k]);
            cs += std::cos(2 * kPi * m);
            sn += std::sin(2 * kPi * m);
        }
        w.tune = (cs == 0 && sn == 0) ? 0 : std::atan2(sn, cs) / (2 * kPi);
    }
    if (p) p(0.65, "bass: onsets");

    // -------------------------------------------------------------- coarse onsets
    std::vector<double> Lk(K), O(K, 0);
    {
        const long nb = 40;
        for (long k = 0; k < K; ++k) {
            double mp = 0;
            for (long j = std::max(0L, k - nb); j <= std::min(K - 1, k + nb); ++j) mp = std::max(mp, w.period(j));
            Lk[k] = mp > 0 ? std::clamp(mp, s.ms(6), s.ms(34)) : s.ms(15);
        }
        const double eps = ref_pow * std::pow(10.0, -5.5);
        for (long k = 0; k < K; ++k) {
            long n = k * H, L = std::lround(Lk[k]);
            double ef = s.pow(n, n + L), eb = s.pow(n - L, n);
            O[k] = db(ef + eps) - db(eb + eps);
            if (ef < ref_pow * std::pow(10.0, -4.5)) O[k] = std::min(O[k], 0.0);
        }
    }
    // residual ODF: energy the previous note (one period back, pitch from 20 ms earlier) cannot explain, forward vs.
    // backward -> legato re-plucks at the same level, which the energy ratio misses
    std::vector<double> Orr(K, 0);
    {
        std::vector<double> SR(s.N + 1, 0.0);
        const long lag = std::lround(0.020 * sr / H);
        for (long n = 0; n < s.N; ++n) {
            long k = std::lround(double(n) / H) - lag;
            double P = w.period(k);
            double v = P > 0 ? s.x[n] - s.frac(n - P) : s.x[n];
            SR[n + 1] = SR[n] + v * v;
        }
        auto pr = [&](long a, long b) {
            long aa = std::clamp(a, 0L, s.N), bb = std::clamp(b, 0L, s.N);
            return b > a ? (SR[bb] - SR[aa]) / double(b - a) : 0.0;
        };
        const double eps = ref_pow * std::pow(10.0, -5.0);
        for (long k = 0; k < K; ++k) {
            long n = k * H, L = std::lround(Lk[k]);
            double ef = pr(n, n + L), eb = pr(n - L, n);
            Orr[k] = db(ef + eps) - db(eb + eps);
            if (s.pow(n, n + L) < ref_pow * std::pow(10.0, -4.0)) Orr[k] = 0;
        }
    }
    std::vector<Onset> ons;
    {
        const long r = std::lround(s.ms(10) / H);
        for (long k = 1; k + 1 < K; ++k) {
            if (O[k] < 3.0) continue;
            bool mx = true;
            for (long j = std::max(0L, k - r); j <= std::min(K - 1, k + r) && mx; ++j)
                if (O[j] > O[k] || (O[j] == O[k] && j < k)) mx = false;
            if (!mx) continue;
            Onset o;
            o.n = k * H;
            o.strength = O[k];
            ons.push_back(o);
        }
        for (long k = 1; k + 1 < K; ++k) {
            if (Orr[k] < 10.0) continue;
            bool mx = true;
            for (long j = std::max(0L, k - r); j <= std::min(K - 1, k + r) && mx; ++j)
                if (Orr[j] > Orr[k] || (Orr[j] == Orr[k] && j < k)) mx = false;
            if (!mx) continue;
            Onset o;
            o.n = k * H;
            o.strength = std::max(0.0, O[k]);
            ons.push_back(o);
        }
        std::sort(ons.begin(), ons.end(), [](const Onset& a, const Onset& b) { return a.n < b.n; });
        std::vector<Onset> o2;
        for (auto& o : ons) {
            if (!o2.empty() && o.n - o2.back().n < s.msl(20)) {
                if (o.strength > o2.back().strength) o2.back() = o;
                continue;
            }
            o2.push_back(o);
        }
        ons.swap(o2);
    }
    // refine each onset to the sample
    for (size_t i = 0; i < ons.size(); ++i) {
        long prev = i > 0 ? ons[i - 1].n : -s.msl(100);
        long lo = std::max(0L, prev + s.msl(10));
        long kp = w.kof(std::max(0L, ons[i].n - s.msl(35)));
        double P = w.period(kp);
        if (P <= 0) P = w.period(kp - 5);
        refine_onset(s, ons[i], lo, P, ref_pow);
        // dip before the onset: backward-window level 30 ms earlier vs. the minimum just before the onset
        double L = Lk[std::clamp(w.kof(ons[i].n), 0L, K - 1)];
        long Ll = std::lround(std::min(L, s.ms(20)));
        double before = db(s.pow(ons[i].n - s.msl(30) - Ll, ons[i].n - s.msl(30)));
        double mn = 1e9;
        for (long n = ons[i].n - s.msl(30); n <= ons[i].n; n += s.msl(1)) mn = std::min(mn, db(s.pow(n - Ll, n)));
        ons[i].dip = before - mn > 6.0 || before < w.ref_db - 50;
    }
    // dedupe (< 25 ms apart: keep the stronger)
    {
        std::sort(ons.begin(), ons.end(), [](const Onset& a, const Onset& b) { return a.n < b.n; });
        std::vector<Onset> o2;
        for (auto& o : ons) {
            if (!o2.empty() && o.n - o2.back().n < s.msl(25)) {
                if (o.strength > o2.back().strength) o2.back() = o;
                continue;
            }
            o2.push_back(o);
        }
        ons.swap(o2);
    }
    double dbgA = -1, dbgB = -1;
    if (const char* dv = std::getenv("DS_BASS_DEBUG")) std::sscanf(dv, "%lf,%lf", &dbgA, &dbgB);
    auto dbg_on = [&](double t) { return t >= dbgA && t <= dbgB; };
    const double hop_ms = 1e3 * H / sr;
    // drop onsets that are only bleed on a held note: same pitch before/after, no dip, previous note explains it
    {
        std::vector<Onset> o2;
        for (auto& o : ons) {
            auto medm = [&](long a, long b) {
                std::vector<double> v;
                for (long k = w.kof(a); k <= w.kof(b); ++k) {
                    float m = w.midi(k);
                    if (!std::isnan(m)) v.push_back(m);
                }
                return v.size() < 5 ? std::nan("") : median(v);
            };
            double mb = medm(o.n - s.msl(45), o.n - s.msl(20)), ma = medm(o.n + s.msl(20), o.n + s.msl(45));
            bool same = !std::isnan(mb) && !std::isnan(ma) && std::fabs(ma - mb) < 0.5;
            if (std::getenv("DS_BASS_DEBUG") && dbg_on(double(o.n) / sr))
                std::fprintf(stderr, "cand %.4f str %.1f nov %.2f dip %d mb %.2f ma %.2f\n", double(o.n) / sr, o.strength,
                             o.novelty, o.dip, mb, ma);
            if (same && !o.dip && o.novelty < 0.5) continue;
            // a glide (pitch moving continuously through intermediate values) without a clear energy attack is a
            // slide inside a note, not a new attack
            if (!std::isnan(mb) && !std::isnan(ma) && std::fabs(ma - mb) >= 0.5 && !o.dip && o.strength < 6) {
                double lo = std::min(ma, mb) + 0.3, hi = std::max(ma, mb) - 0.3;
                int inter = 0;
                for (long k = w.kof(o.n - s.msl(40)); k <= w.kof(o.n + s.msl(60)); ++k) {
                    float m = w.midi(k);
                    if (!std::isnan(m) && m > lo && m < hi) ++inter;
                }
                (void)inter;
                continue;
            }
            o2.push_back(o);
        }
        ons.swap(o2);
    }
    if (p) p(0.75, "bass: notes");
    double dbg0 = -1, dbg1 = -1;
    if (const char* dv = std::getenv("DS_BASS_DEBUG")) std::sscanf(dv, "%lf,%lf", &dbg0, &dbg1);
    auto dbg = [&](double t) { return t >= dbg0 && t <= dbg1; };
    if (dbg0 >= 0) {
        for (auto& o : ons)
            if (dbg(double(o.n) / sr))
                std::fprintf(stderr, "onset %.4f str %.1f nov %.2f dip %d\n", double(o.n) / sr, o.strength, o.novelty, o.dip);
        for (long k = 0; k < K; k += 2)
            if (dbg(w.tk(k)))
            {
                std::fprintf(stderr, "  t %.3f m %6.2f dp %.2f pdb %6.1f O %5.1f |", w.tk(k), w.midi(k), tr.dp[k], tr.pdb[k], O[k]);
                for (auto& c : tr.cands[k]) std::fprintf(stderr, " %.1f/%.2f/%.2f", c.tau, c.dp, c.p);
                std::fprintf(stderr, "\n");
            }
    }

    // -------------------------------------------------------------- segments -> notes
    struct Raw {
        BassNote n;
        double peak_db = -300;
        bool attacked = true;
    };
    std::vector<Raw> raw;
    double prev_end = -1;  // end (s) of the previous kept note
    const double hop_s = double(H) / sr;
    for (size_t i = 0; i < ons.size(); ++i) {
        const long on = ons[i].n;
        const long next = i + 1 < ons.size() ? ons[i + 1].n : s.N;
        const double ton = double(on) / sr, tnext = double(next) / sr;
        const double gfull = 0.8 * w.Wsec / 2;
        const double g1 = (prev_end < 0 || ton - prev_end >= 0.010) ? 0.003 : gfull;
        // voiced frames (conservative end guard)
        auto frames = [&](double ta, double tb, std::vector<long>& ks) {
            ks.clear();
            for (long k = std::max(0L, long(std::ceil(ta / hop_s))); k < K && w.tk(k) <= tb; ++k)
                if (tr.f0[k] > 0) ks.push_back(k);
        };
        std::vector<long> ks;
        frames(ton + g1, tnext - gfull, ks);
        if (ks.size() * hop_s < 0.025) {
            // maybe a short note followed closely by the next onset: relax the end guard
            frames(ton + g1, tnext - 0.003, ks);
            if (ks.size() * hop_s < 0.025) continue;
        }
        // period of the end part
        std::vector<double> pe;
        for (size_t j = ks.size() / 2; j < ks.size(); ++j) pe.push_back(w.period(ks[j]));
        double Pseg = median(pe);
        // ---- release: pitch-synchronous log envelope, hinge fit where it falls away
        long L = std::lround(Pseg * std::ceil(s.ms(4) / Pseg));
        long k0 = w.kof(on), k1 = std::min(K - 1, w.kof(next));
        std::vector<double> e(k1 - k0 + 1);
        for (long k = k0; k <= k1; ++k) {
            long n = k * H;
            e[k - k0] = db(s.pow(n - L / 2, n - L / 2 + L) + ref_pow * 1e-10);
        }
        long kpk = k0;
        for (long k = k0; k <= std::min(k1, k0 + long(0.08 / hop_s)); ++k)
            if (e[k - k0] > e[kpk - k0]) kpk = k;
        const double epk = e[kpk - k0];
        double tend = tnext;
        {
            const double floor_db = w.ref_db - 70;
            long kd = -1;
            for (long k = kpk + 1; k <= k1 - long(L / 2 / H) - 1; ++k)
                if (e[k - k0] < std::max(epk - 20, floor_db + 3)) { kd = k; break; }
            if (kd >= 0) {
                long ka = std::max(kpk + 2, kd - long(0.25 / hop_s));
                long kb = kd;
                // follow the fall to -40 dB, the floor, or the point where the level stops falling
                while (kb < k1 - long(L / 2 / H) && e[kb - k0] > std::max(epk - 40, floor_db + 6) &&
                       e[kb + 1 - k0] <= e[kb - k0] + 0.5)
                    ++kb;
                kb = std::max(kb, kd + 3);
                kb = std::min(kb, k1);
                double best = 1e300;
                long kq = kd;
                std::vector<double> sse(kb - ka + 1, 1e300);
                for (long q = ka + 2; q <= kb - 2; ++q) {
                    // continuous hinge: e = a + b*min(t-tq,0) + c*max(t-tq,0)
                    double A[3][3] = {{0}}, B[3] = {0};
                    for (long k = ka; k <= kb; ++k) {
                        double u = std::min<double>(k - q, 0), v = std::max<double>(k - q, 0);
                        double f[3] = {1, u, v}, y = e[k - k0];
                        for (int r = 0; r < 3; ++r) {
                            B[r] += f[r] * y;
                            for (int c = 0; c < 3; ++c) A[r][c] += f[r] * f[c];
                        }
                    }
                    // solve 3x3 (Cramer)
                    auto det3 = [](double M[3][3]) {
                        return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) -
                               M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                               M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
                    };
                    double dA = det3(A);
                    if (std::fabs(dA) < 1e-12) continue;
                    double sol[3];
                    for (int c = 0; c < 3; ++c) {
                        double M[3][3];
                        for (int r = 0; r < 3; ++r)
                            for (int cc = 0; cc < 3; ++cc) M[r][cc] = cc == c ? B[r] : A[r][cc];
                        sol[c] = det3(M) / dA;
                    }
                    double ss = 0;
                    for (long k = ka; k <= kb; ++k) {
                        double u = std::min<double>(k - q, 0), v = std::max<double>(k - q, 0);
                        double r = e[k - k0] - (sol[0] + sol[1] * u + sol[2] * v);
                        ss += r * r;
                    }
                    if (sol[2] > sol[1]) ss += 1e6;  // must fall faster after the knee
                    sse[q - ka] = ss;
                    if (ss < best) { best = ss; kq = q; }
                }
                double fq = kq;
                if (kq - 1 >= ka && kq + 1 <= kb && sse[kq - 1 - ka] < 1e299 && sse[kq + 1 - ka] < 1e299) {
                    double y0 = sse[kq - 1 - ka], y1 = sse[kq - ka], y2 = sse[kq + 1 - ka];
                    double den = y0 - 2 * y1 + y2;
                    if (den > 0) fq += std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5);
                }
                tend = std::min(tnext, fq * hop_s);
            }
        }
        if (tend - ton < 0.02) tend = std::min(tnext, ton + 0.02);
        if (dbg(ton)) {
            std::fprintf(stderr, "seg %.4f next %.4f L %ld P %.1f epk %.1f tend %.4f\n", ton, tnext, L, Pseg, epk, tend);
            for (long k = k0; k <= k1; ++k) std::fprintf(stderr, "   e %.3f %.1f\n", w.tk(k), e[k - k0]);
        }
        // ---- pitch curve inside [on + g1, end - g2]
        const double g2 = (tnext - tend >= 0.010) ? 0.003 : gfull;
        frames(ton + g1, tend - g2, ks);
        if (ks.size() < 8) frames(ton + 0.003, tend - 0.003, ks);
        if (ks.size() * hop_s < 0.025) continue;
        std::vector<double> tt, mm;
        for (long k : ks) { tt.push_back(w.tk(k)); mm.push_back(w.midi(k)); }
        // kick-bleed thump: starts well above its held pitch and sweeps down, decays fast
        {
            std::vector<double> early, late;
            for (size_t j = 0; j < tt.size(); ++j) {
                if (tt[j] < ton + 0.015) early.push_back(mm[j]);
                else if (tt[j] > ton + 0.045 && tt[j] < ton + 0.09) late.push_back(mm[j]);
            }
            long k120 = w.kof(on + s.msl(120));
            bool fast_decay = k120 > k1 || e[std::min(k120, k1) - k0] < epk - 12;
            if (!early.empty() && !late.empty() && median(early) - median(late) > 4 && fast_decay) continue;
        }
        if (epk < w.ref_db - 38) continue;  // far below the line: bleed / noise
        // ---- semitone DP -> levels
        const size_t F = mm.size();
        int smin = 1000, smax = -1000;
        for (double m : mm) {
            int r = int(std::lround(m - w.tune));
            smin = std::min(smin, r - 1);
            smax = std::max(smax, r + 1);
        }
        const int NS = smax - smin + 1;
        std::vector<double> cost(NS), nc(NS);
        std::vector<std::vector<short>> bk(F, std::vector<short>(NS));
        for (size_t f = 0; f < F; ++f) {
            double bestp = 1e300;
            int bj = 0;
            if (f > 0)
                for (int j = 0; j < NS; ++j)
                    if (cost[j] < bestp) { bestp = cost[j]; bj = j; }
            for (int sI = 0; sI < NS; ++sI) {
                double dv = mm[f] - w.tune - (smin + sI);
                double ob = std::min(dv * dv, 1.0);
                if (f == 0) { nc[sI] = ob; continue; }
                double stay = cost[sI], sw = bestp + 0.6;
                if (stay <= sw) { nc[sI] = stay + ob; bk[f][sI] = short(sI); }
                else { nc[sI] = sw + ob; bk[f][sI] = short(bj); }
            }
            cost = nc;
        }
        std::vector<int> stt(F);
        {
            int sI = int(std::min_element(cost.begin(), cost.end()) - cost.begin());
            for (size_t f = F; f-- > 0;) {
                stt[f] = smin + sI;
                sI = bk[f][sI];
            }
        }
        std::vector<Level> lv;
        for (size_t f = 0; f < F;) {
            size_t g = f;
            while (g + 1 < F && stt[g + 1] == stt[f]) ++g;
            Level L0{long(f), long(g), stt[f], 0, false};
            std::vector<double> v(mm.begin() + f, mm.begin() + g + 1);
            L0.pitch = median(v);
            double dur = tt[g] - tt[f] + hop_s;
            // stable: long enough and flat (spread of the middle part small)
            std::vector<double> dev;
            for (size_t j = f; j <= g; ++j) dev.push_back(std::fabs(mm[j] - L0.pitch));
            L0.stable = dur >= 0.03 && percentile(dev, 0.8) < 0.2;
            lv.push_back(L0);
            f = g + 1;
        }
        // note-level runs: the first stable level, later stable levels, and the final arrival
        std::vector<int> nl;
        for (int j = 0; j < int(lv.size()); ++j) {
            double dur = tt[lv[j].k1] - tt[lv[j].k0] + hop_s;
            bool last = j == int(lv.size()) - 1;
            if (lv[j].stable || (last && !nl.empty() && dur >= 0.010)) nl.push_back(j);
        }
        if (nl.empty()) {  // nothing held: one note at the pitch of the longest level
            int bj = 0;
            for (int j = 1; j < int(lv.size()); ++j)
                if (lv[j].k1 - lv[j].k0 > lv[bj].k1 - lv[bj].k0) bj = j;
            nl.push_back(bj);
        }
        // merge equal neighbours, drop slow excursions that return (bends)
        auto glide_between = [&](int ja, int jb) {
            // frames from the last 40 ms of level ja to the end of level jb
            const Level &A = lv[ja], &B = lv[jb];
            std::vector<double> t2, m2;
            double ta = std::max(tt[A.k0], tt[A.k1] - 0.04);
            for (long f = 0; f <= B.k1; ++f)
                if (tt[f] >= ta) { t2.push_back(tt[f]); m2.push_back(mm[f]); }
            return fit_glide(t2, m2, A.pitch, tt[A.k1] - w.Wsec / 2 - 0.01, tt[B.k0] + 0.005, w.Wsec);
        };
        for (bool changed = true; changed;) {
            changed = false;
            for (size_t j = 0; j + 1 < nl.size(); ++j)
                if (lv[nl[j]].semi == lv[nl[j + 1]].semi) {
                    nl.erase(nl.begin() + j + 1);
                    changed = true;
                    break;
                }
            if (changed) continue;
            for (size_t j = 1; j + 1 < nl.size(); ++j) {
                if (lv[nl[j - 1]].semi != lv[nl[j + 1]].semi) continue;
                Glide gl = glide_between(nl[j - 1], nl[j]);
                if (gl.tau > 0.035) {
                    nl.erase(nl.begin() + j);
                    changed = true;
                    break;
                }
            }
        }
        // build the notes
        struct Part { double t0, t1; double pitch; Glide gin; bool slid; };
        std::vector<Part> parts;
        for (size_t j = 0; j < nl.size(); ++j) {
            Part pt;
            pt.t0 = ton;
            pt.slid = false;
            pt.pitch = lv[nl[j]].pitch;
            if (j > 0) {
                Glide gl = glide_between(nl[j - 1], nl[j]);
                pt.gin = gl;
                pt.t0 = gl.t0;
                pt.slid = gl.tau >= 0.0025;
                if (dbg(ton))
                    std::fprintf(stderr, "  glide t0 %.4f tau %.4f target %.2f rmse %.3f from %.2f stable %d\n", gl.t0, gl.tau,
                                 gl.target, gl.rmse, lv[nl[j - 1]].pitch, lv[nl[j]].stable);
                if (!lv[nl[j]].stable) pt.pitch = gl.target;
                parts.back().t1 = pt.t0;
            }
            if (lv[nl[j]].stable) {  // held pitch: median of the later part of the level
                const Level& L0 = lv[nl[j]];
                std::vector<double> v;
                double tfrom = j > 0 ? pt.t0 + 3 * pt.gin.tau + w.Wsec / 2 : tt[L0.k0];
                for (long f = L0.k0; f <= L0.k1; ++f)
                    if (tt[f] >= tfrom) v.push_back(mm[f]);
                if (v.size() >= 5) pt.pitch = median(v);
            }
            pt.t1 = tend;
            parts.push_back(pt);
        }
        for (size_t j = 0; j < parts.size(); ++j) {
            Raw rn;
            const Part& pt = parts[j];
            rn.n.time = pt.t0;
            rn.n.end = pt.t1;
            rn.n.note = int(std::lround(pt.pitch));
            rn.n.cents = (pt.pitch - rn.n.note) * 100;
            rn.n.slide_to_next = j + 1 < parts.size() && parts[j + 1].slid;
            rn.attacked = j == 0;
            // peak level
            double pk = -300;
            for (long k = std::max(k0, w.kof(std::lround(pt.t0 * sr))); k <= std::min(k1, w.kof(std::lround(pt.t1 * sr))); ++k)
                pk = std::max(pk, e[k - k0]);
            rn.peak_db = j == 0 ? epk : pk;
            // bends: model glide at the start of a slid-into note, then the observed frames
            const double ref = pt.pitch;
            std::vector<PitchBend> pts;
            double tstart_obs = pt.t0;
            if (pt.slid) {
                const Glide& gl = pt.gin;
                double from = lv[nl[j - 1]].pitch;
                for (double t = gl.t0; t < gl.t0 + 4 * gl.tau; t += 0.002)
                    pts.push_back({t, gl.target + (from - gl.target) * std::exp(-(t - gl.t0) / gl.tau) - ref});
                tstart_obs = gl.t0 + 3 * gl.tau + w.Wsec / 2;
            }
            double tstop_obs = j + 1 < parts.size() ? parts[j + 1].t0 - w.Wsec / 2 : pt.t1;
            for (size_t f = 0; f < F; ++f)
                if (tt[f] >= tstart_obs && tt[f] <= tstop_obs && tr.dp[ks[f]] < 0.35)
                    pts.push_back({tt[f], mm[f] - ref});
            double mx = 0;
            for (auto& b : pts) mx = std::max(mx, std::fabs(b.semitones));
            if (mx >= 0.2 && !pts.empty()) {
                std::vector<PitchBend> dec;
                dec.push_back(pts.front());
                for (size_t q = 1; q < pts.size(); ++q)
                    if (std::fabs(pts[q].semitones - dec.back().semitones) > 0.05) dec.push_back(pts[q]);
                if (std::fabs(pts.back().semitones - dec.back().semitones) > 0.02) dec.push_back(pts.back());
                rn.n.bends = dec;
            }
            raw.push_back(rn);
        }
        prev_end = tend;
    }
    if (p) p(0.92, "bass: velocity");

    // -------------------------------------------------------------- velocity, accent
    std::vector<double> pk;
    for (auto& r : raw)
        if (r.attacked) pk.push_back(r.peak_db);
    const double top = pk.empty() ? 0 : percentile(pk, 0.95);
    for (size_t i = 0; i < raw.size(); ++i) {
        double v = 120 + (raw[i].peak_db - top) * (119.0 / 45.0);
        raw[i].n.velocity = int(std::clamp(std::lround(v), 1L, 127L));
        if (!raw[i].attacked) continue;
        std::vector<double> nb;
        for (int d = -1; d >= -4 && int(i) + d >= 0; --d)
            if (raw[i + d].attacked) nb.push_back(raw[i + d].peak_db);
        for (size_t j = i + 1, c = 0; j < raw.size() && c < 4; ++j)
            if (raw[j].attacked) { nb.push_back(raw[j].peak_db); ++c; }
        if (nb.size() >= 2) raw[i].n.accent = raw[i].peak_db - median(nb) >= 3.5;
    }
    for (auto& r : raw) out.push_back(std::move(r.n));
    if (p) p(1.0, "bass: done");
    return out;
}

}  // namespace ds
