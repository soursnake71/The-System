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
    std::vector<double> SH;  // prefix sums of the high band (> ~1.2 kHz, causal) squared: pluck / attack transients
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
    double powh(long a, long b) const {
        if (b <= a) return 0;
        long aa = std::clamp(a, 0L, N), bb = std::clamp(b, 0L, N);
        return (SH[bb] - SH[aa]) / double(b - a);
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
// dir = -1: predict from one period earlier; dir = +1: from one period later (backward prediction)
Predictor fit_predictor(const Sig& s, long a, long b, double P0, int dir = -1, int steps = 40) {
    Predictor best;
    if (P0 <= 2 || b - a < 16) return best;
    double ex = 0;
    for (long n = a; n < b; ++n) ex += s.at(n) * s.at(n);
    if (ex <= 0) return best;
    double bestrho = 0.6;
    for (int i = -steps; i <= steps; ++i) {
        double P = P0 * (1 + 0.0005 * i);
        double xy = 0, yy = 0;
        for (long n = a; n < b; ++n) {
            double v = s.frac(n + dir * P);
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
    double hf = 0;       // high-band rise at the onset (dB): a pluck / filter-envelope transient
    double clarity = 0;  // peak of the unexplained part over its level before (dB): how clear the attack is
    bool decay = false;  // only the previous note dying away (release), no attack
    double cont = 0;     // phase continuity of the previous note across the onset (1 = it just goes on)
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
    auto X = [&](long n) { return s.at(n) - R(n); };  // the old note's prediction
    // change point of a residual series v: threshold above anything seen before s0, then CUSUM back to where the
    // excess starts. Returns -1 when there is no clear change.
    auto detect = [&](const std::vector<double>& v, long from, double* clar) -> long {
        auto V = [&](long n) { return std::fabs(v[n - pr0]); };
        double sig2 = 0, vmax = 0;
        long cnt = 0;
        for (long n = std::max(pr0, s0 - s.msl(10)); n < s0; ++n) { sig2 += V(n) * V(n); ++cnt; }
        for (long n = pr0; n < s0; ++n) vmax = std::max(vmax, V(n));
        sig2 = std::max(cnt ? sig2 / cnt : 0.0, ref_pow * 1e-9);
        double A = 0;
        for (long n = from; n < e1; ++n) A = std::max(A, V(n));
        if (clar) *clar = db(A * A) - db(sig2);
        if (A * A < 30 * sig2) return -1;
        double thr = std::max({4 * std::sqrt(sig2), 0.1 * A, 1.2 * vmax});
        long n1 = from;
        while (n1 < e1 - 1 && V(n1) <= thr) ++n1;
        double c = 0, cmin = 0;
        long nb = from;
        for (long n = from; n <= n1; ++n) {
            if (c < cmin) { cmin = c; nb = n; }
            c += V(n) * V(n) - 4 * sig2;
        }
        return nb;
    };
    double clar = 0;
    long nr = detect(r, s0, &clar);
    if (nr >= 0) {
        // the old note may only be dying away fast here (mute / gate-off before a gap): then the attack is where |x|
        // rises above the old note's prediction
        if (pr.P > 0) {
            double c2 = 0;
            long nq = detect(q, nr, &c2);
            long ne = nq >= 0 ? nq : std::min(e1, nr + s.msl(10));
            if (ne >= nr + s.msl(3)) {
                double ex = 0, eh = 0;
                for (long n = nr; n < ne; ++n) { ex += s.at(n) * s.at(n); eh += X(n) * X(n); }
                if (ex < 0.3 * eh) {  // [nr, ne) is only the old note dying away
                    if (nq >= 0) { nr = nq; clar = c2; }
                    else o.decay = true;
                }
            }
        }
        o.n = nr;
    }
    o.clarity = clar;
    if (const char* rd = std::getenv("DS_REFINE_DEBUG"))
        if (std::fabs(double(nc) / s.sr - std::atof(rd)) < 0.03)
            std::fprintf(stderr, "refine nc %.4f s0 %.4f prP %.1f g %.3f -> %.4f clar %.1f\n", double(nc) / s.sr,
                         double(s0) / s.sr, pr.P, pr.g, double(o.n) / s.sr, clar);
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

// where the waveform stops repeating with period P: a long-term predictor fitted on [fa, fb); its residual amplitude
// (pitch-synchronous RMS) over [fb, sb) is fitted with a hinge c + b * max(0, t - t0): after a glide starts the phase
// drift against the old period, and so the residual amplitude, grows linearly. Returns -1 when there is no clear break.
long periodicity_break(const Sig& s, long fa, long fb, long sb, double P) {
    if (fb - fa < s.ms(5) || sb - fb < s.ms(4)) return -1;
    Predictor pr = fit_predictor(s, fa, fb, P);
    if (pr.P <= 0) return -1;
    const long a0 = fa, a1 = std::min(s.N, sb + long(P));
    std::vector<double> RS(a1 - a0 + 1, 0.0);
    for (long n = a0; n < a1; ++n) {
        double v = s.at(n) - pr.g * s.frac(n - pr.P);
        RS[n - a0 + 1] = RS[n - a0] + v * v;
    }
    const long hp = std::max(1L, long(pr.P / 2));
    auto amp = [&](long n) {
        long u = std::clamp(n - hp, a0, a1), v = std::clamp(n + hp, a0, a1);
        return v > u ? std::sqrt((RS[v - a0] - RS[u - a0]) / (v - u)) : 0.0;
    };
    const long step = std::max(1L, s.msl(0.25));
    std::vector<double> t, y;
    for (long n = fb; n < sb; n += step) { t.push_back(double(n)); y.push_back(amp(n)); }
    if (t.size() < 8) return -1;
    double ymax = *std::max_element(y.begin(), y.end());
    size_t end = 0;
    while (end + 1 < y.size() && y[end] < 0.5 * ymax) ++end;
    end = std::max<size_t>(end, 6);
    double best = 1e300, bt = -1;
    const double base = amp(fb - hp - 1);
    for (size_t q = 0; q + 3 < end; ++q) {
        double t0 = t[q], sxx = 0, sxy = 0, sx = 0, sy = 0;
        size_t m = end + 1;
        for (size_t i = 0; i <= end; ++i) {
            double xv = std::max(0.0, t[i] - t0);
            sx += xv; sy += y[i]; sxx += xv * xv; sxy += xv * y[i];
        }
        double den = m * sxx - sx * sx;
        if (den <= 0) continue;
        double bb = (m * sxy - sx * sy) / den, cc = (sy - bb * sx) / m;
        if (bb <= 0) continue;
        double sse = 0;
        for (size_t i = 0; i <= end; ++i) {
            double r = y[i] - cc - bb * std::max(0.0, t[i] - t0);
            sse += r * r;
        }
        if (sse < best) { best = sse; bt = t0; }
    }
    if (const char* pd = std::getenv("DS_PBREAK_DEBUG"))
        if (std::fabs(double(fb) / s.sr - std::atof(pd)) < 0.05) {
            std::fprintf(stderr, "pbreak P %.1f g %.3f base %.2e ymax %.2e end %.4f bt %.4f\n", pr.P, pr.g, base, ymax,
                         t[end] / s.sr, bt / s.sr);
            for (size_t i = 0; i < t.size(); i += 4) std::fprintf(stderr, "   %.4f %.3e\n", t[i] / s.sr, y[i]);
        }
    if (bt < 0 || ymax < 3 * std::max(base, 1e-12)) return -1;
    return std::lround(bt);
}

// length (s) of the stretch around n that neither the old note (forward prediction, period PA) nor the new note
// (backward prediction, period PB) explains: ~0 for a new note starting at its pitch, the glide time for a slide
double transition_zone(const Sig& s, long n, double PA, double PB) {
    Predictor A = fit_predictor(s, n - s.msl(35), n - s.msl(5), PA, -1, 40);
    Predictor B = fit_predictor(s, n + s.msl(25), n + s.msl(55), PB, +1, 60);
    if (A.P <= 0 || B.P <= 0) return -1;
    const long a = n - s.msl(30), b = n + s.msl(60);
    std::vector<double> SA(b - a + 1, 0.0), SB(b - a + 1, 0.0), SX(b - a + 1, 0.0);
    for (long m = a; m < b; ++m) {
        double x = s.at(m), ra = x - A.g * s.frac(m - A.P), rb = x - B.g * s.frac(m + B.P);
        SA[m - a + 1] = SA[m - a] + ra * ra;
        SB[m - a + 1] = SB[m - a] + rb * rb;
        SX[m - a + 1] = SX[m - a] + x * x;
    }
    const long h = std::max(1L, long(std::max(A.P, B.P) / 2));
    long cnt = 0;
    for (long m = a + h; m < b - h; ++m) {
        long u = m - h - a, v = m + h - a;
        double ea = SA[v] - SA[u], eb = SB[v] - SB[u], ex = SX[v] - SX[u];
        if (std::min(ea, eb) > 0.2 * ex) ++cnt;
    }
    return double(cnt) / s.sr;
}

// time (s) the waveform spends at periods between PA and PB (inner 50 % of the interval) around n, from a short-window
// (one period) normalized autocorrelation every 0.5 ms: a glide passes through them, a re-attack jumps over them
double fine_intermediate(const Sig& s, long n, double PA, double PB) {
    const double lo = std::min(PA, PB), hi = std::max(PA, PB);
    const long L = long(hi) + 1;
    const long t0 = std::max(1L, long(lo * 0.97)), t1 = long(hi * 1.03) + 1;
    const double ilo = lo + 0.25 * (hi - lo), ihi = hi - 0.25 * (hi - lo);
    const long step = std::max(1L, s.msl(0.5));
    std::vector<double> c(t1 - t0 + 1);
    long cnt = 0;
    for (long m = n - s.msl(20); m < n + s.msl(70); m += step) {
        const long a = m - L / 2 - long(lo / 2);
        double e0 = 0;
        for (long j = a; j < a + L; ++j) e0 += s.at(j) * s.at(j);
        if (e0 <= 0) continue;
        long bt = -1;
        double bc = -1;
        for (long t = t0; t <= t1; ++t) {
            double xy = 0, e1 = 0;
            for (long j = a; j < a + L; ++j) {
                double u = s.at(j + t);
                xy += s.at(j) * u;
                e1 += u * u;
            }
            c[t - t0] = e1 > 0 ? xy / std::sqrt(e0 * e1) : 0;
            if (c[t - t0] > bc) { bc = c[t - t0]; bt = t; }
        }
        if (bt > t0 && bt < t1 && bc > 0.9) {
            double y0 = c[bt - 1 - t0], y1 = c[bt - t0], y2 = c[bt + 1 - t0], den = y0 - 2 * y1 + y2;
            double tf = bt + (den < 0 ? 0.5 * (y0 - y2) / den : 0.0);
            if (tf > ilo && tf < ihi) ++cnt;
        }
    }
    return double(cnt * step) / s.sr;
}

// does the note before n simply continue through n (something else, e.g. a kick, was laid on top)? Normalized
// correlation of x 70..100 ms after n with x a whole number of the old periods earlier (before n): a held note keeps
// its phase, a re-attack (even at the same pitch) does not.
double continuity(const Sig& s, long n, double P0, double back_ms) {
    if (P0 <= 2) return 0;
    const long e = n - s.msl(back_ms);
    Predictor pr = fit_predictor(s, e - s.msl(36), e, P0);
    if (pr.P <= 0) return 0;
    const long a = n + s.msl(70), b = n + s.msl(100);
    if (b >= s.N) return 0;
    const double kP = std::ceil((b - e) / pr.P) * pr.P;
    double xy = 0, xx = 0, yy = 0;
    for (long m = a; m < b; ++m) {
        double u = s.at(m), v = s.frac(m - kP);
        xy += u * v; xx += u * u; yy += v * v;
    }
    return (xx > 0 && yy > 0) ? xy / std::sqrt(xx * yy) : 0;
}

// Goertzel power of x[a, b) (Hann window) at frequency f
double tone_power(const Sig& s, long a, long b, double f) {
    const long n = b - a;
    if (n < 16) return 0;
    const double w = 2 * kPi * f / s.sr, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (long i = 0; i < n; ++i) {
        double h = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
        double s0 = h * s.at(a + i) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2);
}

// octave check: if the tracked pitch f has strong even harmonics but (almost) no odd ones above the fundamental,
// what was tracked is a sub-octave (e.g. kick bleed landing on f/2): returns 12, else 0
int octave_up(const Sig& s, long a, long b, double f) {
    if (b - a < long(2.5 * s.sr / f) || f * 7 > 0.45 * s.sr) return 0;
    double ev = 0, od = 0, tot = 0;
    for (int h = 1; h <= 8; ++h) {
        double p = tone_power(s, a, b, h * f);
        tot += p;
        if (h % 2 == 0) ev += p;
        else if (h > 1) od += p;
    }
    return (tot > 0 && ev > 0.3 * tot && od < 0.08 * ev) ? 12 : 0;
}

// ---------------------------------------------------------------------------------------------------- glide model
struct Glide { double t0 = 0, tau = 0, target = 0, rmse = 1e9; };

// fit p(t) = from (t < t0), target + (from - target) exp(-(t - t0)/tau), seen through the YIN window (box of Wsec)
Glide fit_glide(const std::vector<double>& t, const std::vector<double>& m, double from, double ta, double tb,
                double Wsec) {
    Glide best;
    if (t.size() < 4 || tb < ta) return best;
    const double h = Wsec / 2;
    for (double t0 = ta; t0 <= tb + 1e-9; t0 += 0.0005) {
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

// Sub-harmonic rescue: a short run of frames whose pitch is an integer fraction (1/2, 1/3, 1/4) of the pitch held
// just before or after it, while a candidate at that held pitch is still present in its frames, is interference
// (typically kick-drum bleed adding a low sine) pulling the tracker down: take the held pitch's candidate instead.
void rescue_subharmonics(Track& tr) {
    const long K = long(tr.f0.size());
    auto midi = [&](long k) { return tr.f0[k] > 0 ? hz_midi(tr.f0[k]) : std::nan(""); };
    // runs of voiced frames with pitch within 0.6 st of the run's first frame
    struct Run { long a, b; double m; };
    std::vector<Run> runs;
    for (long k = 0; k < K;) {
        if (tr.f0[k] <= 0) { ++k; continue; }
        long j = k;
        double m0 = midi(k);
        while (j + 1 < K && tr.f0[j + 1] > 0 && std::fabs(midi(j + 1) - m0) < 0.6) ++j;
        std::vector<double> v;
        for (long q = k; q <= j; ++q) v.push_back(midi(q));
        runs.push_back({k, j, median(v)});
        k = j + 1;
    }
    const long maxlen = std::lround(0.25 * tr.srd * tr.D / tr.H), gap = std::lround(0.03 * tr.srd * tr.D / tr.H);
    for (size_t i = 0; i < runs.size(); ++i) {
        const Run& R = runs[i];
        if (R.b - R.a > maxlen) continue;
        for (int side = 0; side < 2; ++side) {
            const Run* N = nullptr;
            if (side == 0 && i > 0 && R.a - runs[i - 1].b <= gap) N = &runs[i - 1];
            if (side == 1 && i + 1 < runs.size() && runs[i + 1].a - R.b <= gap) N = &runs[i + 1];
            if (!N || N->b - N->a < (R.b - R.a) / 2) continue;
            double d = N->m - R.m;
            int k = 0;
            if (std::fabs(d - 12) < 0.5) k = 2;
            else if (std::fabs(d - 19.02) < 0.5) k = 3;
            else if (std::fabs(d - 24) < 0.5) k = 4;
            if (!k) continue;
            const double ftgt = 440.0 * std::pow(2.0, (N->m - 69) / 12.0);
            long hit = 0;
            std::vector<float> nf(R.b - R.a + 1, 0.f), nd(R.b - R.a + 1, 1.f);
            for (long q = R.a; q <= R.b; ++q) {
                for (auto& c : tr.cands[q]) {
                    double f = tr.srd / c.tau;
                    if (std::fabs(12 * std::log2(f / ftgt)) < 0.7 && c.dp < 0.85) {
                        nf[q - R.a] = float(f);
                        nd[q - R.a] = c.dp;
                        ++hit;
                        break;
                    }
                }
            }
            if (hit * 10 < (R.b - R.a + 1) * 7) continue;
            for (long q = R.a; q <= R.b; ++q) {
                tr.f0[q] = nf[q - R.a];  // 0 (unvoiced) where the held pitch is missing
                tr.dp[q] = nd[q - R.a];
            }
            break;
        }
    }
}

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
    {  // causal 2nd-order Butterworth high-pass at 1.2 kHz
        const double f = 1200.0, q = std::sqrt(0.5), wc = 2 * kPi * f / sr, al = std::sin(wc) / (2 * q),
                     cw = std::cos(wc), a0 = 1 + al;
        const double b0 = (1 + cw) / 2 / a0, b1 = -(1 + cw) / a0, b2 = b0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        s.SH.assign(s.N + 1, 0.0);
        for (long n = 0; n < s.N; ++n) {
            double y = b0 * s.x[n] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = s.x[n]; y2 = y1; y1 = y;
            s.SH[n + 1] = s.SH[n] + y * y;
        }
    }

    Track tr = track_pitch(s, p);
    rescue_subharmonics(tr);
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
            double v = P > 0 ? std::max(0.0, std::fabs(s.x[n]) - std::fabs(s.frac(n - P))) : s.x[n];  // one-sided: no decays
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
    }
    // refine each onset to the sample
    for (size_t i = 0; i < ons.size(); ++i) {
        long prev = i > 0 ? ons[i - 1].n : -s.msl(100);
        long lo = std::max(0L, prev + s.msl(10));
        long kp = w.kof(std::max(0L, ons[i].n - s.msl(35)));
        double P = w.period(kp);
        if (P <= 0) P = w.period(kp - 5);
        refine_onset(s, ons[i], lo, P, ref_pow);
        // continuity, measured from before any disturbance just ahead of the onset
        for (double back : {4.0, 50.0, 90.0}) {
            double Pb = w.period(w.kof(ons[i].n - s.msl(back + 20)));
            if (Pb <= 0) Pb = P;
            ons[i].cont = std::max(ons[i].cont, continuity(s, ons[i].n, Pb, back));
        }
        // dip before the onset: backward-window level 30 ms earlier vs. the minimum just before the onset
        double L = Lk[std::clamp(w.kof(ons[i].n), 0L, K - 1)];
        long Ll = std::lround(std::min(L, s.ms(20)));
        double before = db(s.pow(ons[i].n - s.msl(30) - Ll, ons[i].n - s.msl(30)));
        double mn = 1e9;
        for (long n = ons[i].n - s.msl(30); n <= ons[i].n; n += s.msl(1)) mn = std::min(mn, db(s.pow(n - Ll, n)));
        ons[i].dip = before - mn > 6.0 || before < w.ref_db - 50;
        {
            const double heps = ref_pow * 1e-7;
            long n = ons[i].n;
            ons[i].hf = db(s.powh(n, n + s.msl(8)) + heps) - db(s.powh(n - s.msl(14), n - s.msl(2)) + heps);
        }
    }
    // dedupe (< 25 ms apart: keep the clearer attack)
    {
        std::sort(ons.begin(), ons.end(), [](const Onset& a, const Onset& b) { return a.n < b.n; });
        std::vector<Onset> o2;
        for (auto& o : ons) {
            if (!o2.empty() && o.n - o2.back().n < s.msl(25)) {
                if (o.clarity > o2.back().clarity) o2.back() = o;
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
    // is the pitch change from mb to ma around sample n a glide (slide) rather than a new note at its own pitch?
    // a glide leaves a stretch that neither the old nor the new period explains, and/or passes through intermediate
    // pitches; a re-attack switches from one periodicity to the other at once.
    auto glide_at = [&](long n, double mb, double ma) {
        auto per = [&](double m) { return sr / (440.0 * std::pow(2.0, (m - 69) / 12.0)); };
        double Z = 1e3 * transition_zone(s, n, per(mb), per(ma));
        double lo = std::min(ma, mb) + 0.25, hi = std::max(ma, mb) - 0.25;
        int inter = 0;
        for (long k = w.kof(n - s.msl(40)); k <= w.kof(n + s.msl(60)); ++k) {
            float m = w.midi(k);
            if (!std::isnan(m) && m > lo && m < hi) ++inter;
        }
        double im = inter * hop_ms;
        // the zone measure includes ~0.6 of the longer period from its pitch-synchronous smoothing; a long zone means
        // a glide (large intervals), otherwise a glide shows as time spent at intermediate periods (short-window
        // autocorrelation), which a re-attack jumps over
        double Zc = Z - 0.6e3 * std::max(per(mb), per(ma)) / sr;
        double fi = -1;
        bool g = Zc >= 20;
        if (!g) {
            fi = 1e3 * fine_intermediate(s, n, per(mb), per(ma));
            g = fi >= 5;
        }
        if (std::getenv("DS_BASS_DEBUG") && dbg_on(double(n) / sr))
            std::fprintf(stderr, "   glide_at %.4f %.2f->%.2f Z %.1f Zc %.1f inter %.1f fine %.1f -> %d\n", double(n) / sr, mb,
                         ma, Z, Zc, im, fi, g);
        return g;
    };
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
                std::fprintf(stderr, "cand %.4f cont %.2f str %.1f nov %.2f dip %d hf %.1f mb %.2f ma %.2f clar %.1f Z %.3f\n", double(o.n) / sr, o.cont,
                             o.strength, o.novelty, o.dip, o.hf, mb, ma, o.clarity,
                             (std::isnan(ma) || std::isnan(mb)) ? -1.0 : 1e3 * transition_zone(s, o.n, sr / (440.0 * std::pow(2.0, (mb - 69) / 12.0)), sr / (440.0 * std::pow(2.0, (ma - 69) / 12.0))));
            if (o.decay) continue;
            // weak or ambiguous attacks: a real attack stands far above what came before it (from silence > 30 dB,
            // a legato re-pluck > 20 dB); bleed on a held note leaves the note's phase running on
            if (o.clarity < 12) continue;
            if (!o.dip && o.clarity < 15) continue;
            if (o.cont > 0.75 && (!o.dip || o.clarity < 20)) continue;
            if (same && !o.dip && o.novelty < 0.5) continue;
            // a glide (pitch moving continuously through intermediate values) without a clear energy attack is a
            // slide inside a note, not a new attack
            if (!std::isnan(mb) && !std::isnan(ma) && std::fabs(ma - mb) >= 0.5 && !o.dip && o.strength < 6) {
                if (glide_at(o.n, mb, ma)) continue;
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
    const double hop_s = double(H) / sr;
    // segments that turn out not to be notes (unvoiced, kick thumps, bleed) give their onset up and are analysed again
    // as part of the note before them, so a thump on a held note does not cut it
    for (int pass = 0; pass < 4; ++pass) {
    raw.clear();
    std::vector<size_t> dropped;
    double prev_end = -1;  // end (s) of the previous kept note
    for (size_t i = 0; i < ons.size(); ++i) {
        const long on = ons[i].n;
        const long next = i + 1 < ons.size() ? ons[i + 1].n : s.N;
        const double ton = double(on) / sr, tnext = double(next) / sr;
        const double gfull = w.Wsec / 2 + 0.002;
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
            if (ks.size() * hop_s < 0.018) { dropped.push_back(i); continue; }
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
                const long k5 = std::max(1L, long(0.005 / hop_s)), kmax = kd + long(0.06 / hop_s);
                while (kb < k1 - long(L / 2 / H) && kb < kmax && e[kb - k0] > std::max(epk - 40, floor_db + 6) &&
                       e[kb + 1 - k0] <= e[kb - k0] + 0.5 && !(kb - k5 >= kd && e[kb - k0] > e[kb - k5 - k0] - 0.5))
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
                if (dbg(ton))
                    std::fprintf(stderr, "  release kd %.4f ka %.4f kb %.4f kq %.4f\n", w.tk(kd), w.tk(ka), w.tk(kb), fq * hop_s);
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
        if (ks.size() * hop_s < 0.018) { dropped.push_back(i); continue; }
        std::vector<double> tt, mm;
        for (long k : ks) { tt.push_back(w.tk(k)); mm.push_back(w.midi(k)); }
        // kick-bleed thump: low, decays fast, and its pitch either sweeps down or only becomes periodic late (the sweep
        // itself is too fast for the tracker); a bass note is periodic right away and holds its pitch
        {
            const double tv = tt[0];
            std::vector<double> early, late;
            for (size_t j = 0; j < tt.size(); ++j) {
                if (tt[j] < tv + 0.010) early.push_back(mm[j]);
                else if (tt[j] > tv + 0.030 && tt[j] < tv + 0.070) late.push_back(mm[j]);
            }
            long k120 = w.kof(on + s.msl(120));
            bool fast_decay = k120 > k1 || e[std::min(k120, k1) - k0] < epk - 12;
            bool low = !late.empty() && median(late) < 36.5;
            bool falls = !early.empty() && !late.empty() && median(early) - median(late) > 2.5;
            bool late_voicing = tv - ton > 0.025;
            if (dbg(ton))
                std::fprintf(stderr, "  thump? fast %d low %d falls %d late %d\n", fast_decay, low, falls, late_voicing);
            if (fast_decay && low && (falls || late_voicing)) { dropped.push_back(i); continue; }
        }
        if (epk < w.ref_db - 38) { dropped.push_back(i); continue; }  // far below the line: bleed / noise
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
            // stable: a flat core (all frames within 0.2 st of the run's median) of at least 25 ms; the level is then
            // that core (transition frames on either side are left to the glides)
            size_t bs = f, be = f, cs = f;
            bool in = false;
            for (size_t j = f; j <= g + 1; ++j) {
                bool ok = j <= g && std::fabs(mm[j] - L0.pitch) < 0.2;
                if (ok && !in) { cs = j; in = true; }
                if (!ok && in) {
                    in = false;
                    if (tt[j - 1] - tt[cs] > tt[be] - tt[bs] || !(be > bs)) { bs = cs; be = j - 1; }
                }
            }
            if (be > bs && tt[be] - tt[bs] + hop_s >= 0.025) {
                L0.stable = true;
                L0.k0 = long(bs);
                L0.k1 = long(be);
                std::vector<double> v2(mm.begin() + bs, mm.begin() + be + 1);
                L0.pitch = median(v2);
            }
            lv.push_back(L0);
            f = g + 1;
        }
        if (dbg(ton))
            for (auto& L0 : lv)
                std::fprintf(stderr, "  level %.4f-%.4f semi %d pitch %.2f stable %d\n", tt[L0.k0], tt[L0.k1], L0.semi,
                             L0.pitch, L0.stable);
        // note-level runs: the stable levels, plus the arrival of a final glide that has no time to settle
        std::vector<Level> nl;
        for (auto& L0 : lv)
            if (L0.stable) nl.push_back(L0);
        if (nl.empty()) {  // nothing held: one note at the pitch of the longest level
            Level bl = lv[0];
            for (auto& L0 : lv)
                if (L0.k1 - L0.k0 > bl.k1 - bl.k0) bl = L0;
            nl.push_back(bl);
        } else if (nl.back().k1 + 1 < long(F)) {
            long f0i = nl.back().k1 + 1, f1i = long(F) - 1;
            double dur = tt[f1i] - tt[f0i] + hop_s;
            std::vector<double> v;
            for (long f = f0i + (f1i - f0i) * 2 / 3; f <= f1i; ++f) v.push_back(mm[f]);
            double pm = median(v);
            if (dur >= 0.012 && std::fabs(pm - nl.back().pitch) >= 0.6) {
                Level al{f0i, f1i, int(std::lround(pm - w.tune)), pm, false};
                nl.push_back(al);
            }
        }
        auto glide_between = [&](const Level& A, const Level& B) {
            // frames from the last 40 ms of level A to the end of level B
            std::vector<double> t2, m2;
            double ta = std::max(tt[A.k0], tt[A.k1] - 0.04);
            for (long f = 0; f <= B.k1; ++f)
                if (tt[f] >= ta) { t2.push_back(tt[f]); m2.push_back(mm[f]); }
            return fit_glide(t2, m2, A.pitch, tt[A.k1] - w.Wsec / 2 - 0.01, tt[B.k0] + 0.005, w.Wsec);
        };
        // 20-80 % rise time of the observed transition from A to B (s)
        auto rise_time = [&](const Level& A, const Level& B) {
            double d = B.pitch - A.pitch, t20 = -1, t80 = -1;
            for (long f = A.k1; f <= B.k1; ++f) {
                double u = (mm[f] - A.pitch) / d;
                if (t20 < 0 && u >= 0.2) t20 = tt[f];
                if (t80 < 0 && u >= 0.8) { t80 = tt[f]; break; }
            }
            return (t20 < 0 || t80 < 0) ? 0.0 : t80 - t20;
        };
        // merge equal neighbours, drop slow, small excursions that return (bends)
        for (bool changed = true; changed;) {
            changed = false;
            for (size_t j = 0; j + 1 < nl.size(); ++j)
                if (nl[j].semi == nl[j + 1].semi) {
                    nl[j].k1 = nl[j + 1].k1;
                    nl[j].stable = nl[j].stable || nl[j + 1].stable;
                    nl.erase(nl.begin() + j + 1);
                    changed = true;
                    break;
                }
            if (changed) continue;
            for (size_t j = 1; j + 1 < nl.size(); ++j) {
                if (nl[j - 1].semi != nl[j + 1].semi || std::fabs(nl[j].pitch - nl[j - 1].pitch) > 3.5) continue;
                if (rise_time(nl[j - 1], nl[j]) > 0.040) {
                    nl.erase(nl.begin() + j);
                    changed = true;
                    break;
                }
            }
        }
        // abrupt switches without an attack to a harmonically related pitch (octave / twelfth / two octaves), or short
        // abrupt excursions that come back, are interference (kick bleed pulling the tracker to a sub-harmonic), not
        // notes: drop them and mask their frames
        std::vector<char> bad(F, 0);
        for (bool changed = true; changed;) {
            changed = false;
            for (size_t j = 1; j < nl.size() && !changed; ++j) {
                const Level &A = nl[j - 1], &B = nl[j];
                double d = std::fabs(B.pitch - A.pitch);
                bool harm = std::fabs(d - 12) < 0.5 || std::fabs(d - 19.02) < 0.5 || std::fabs(d - 24) < 0.5;
                bool back = j + 1 < nl.size() && nl[j + 1].semi == A.semi && tt[B.k1] - tt[B.k0] < 0.15;
                if (!harm && !back) continue;
                long nt = std::lround(0.5 * (tt[A.k1] + tt[B.k0]) * sr);
                // an excursion that returns within 60 ms, or an octave-related one that returns, is never a
                // played slide; otherwise only an abrupt switch is an artifact
                bool sure = back && (harm || tt[B.k1] - tt[B.k0] < 0.06);
                if (!sure && glide_at(nt, A.pitch, B.pitch)) continue;
                long f1 = j + 1 < nl.size() ? nl[j + 1].k0 - 1 : long(F) - 1;
                for (long f = A.k1 + 1; f <= f1; ++f) bad[f] = 1;
                if (j + 1 < nl.size() && nl[j + 1].semi == A.semi) {
                    nl[j - 1].k1 = nl[j + 1].k1;
                    nl.erase(nl.begin() + j, nl.begin() + j + 2);
                } else {
                    nl.erase(nl.begin() + j);
                }
                changed = true;
            }
        }
        // build the notes
        struct Part { double t0, t1; double pitch; Glide gin; bool slid; };
        std::vector<Part> parts;
        for (size_t j = 0; j < nl.size(); ++j) {
            Part pt;
            pt.t0 = ton;
            pt.slid = false;
            pt.pitch = nl[j].pitch;
            if (j > 0) {
                Glide gl = glide_between(nl[j - 1], nl[j]);
                {   // pin the glide start to where the old period breaks, then refit the shape
                    const Level& A = nl[j - 1];
                    double PA = sr / (440.0 * std::pow(2.0, (A.pitch - 69) / 12.0));
                    long fb = std::lround((tt[A.k1] - w.Wsec / 2) * sr);
                    fb = std::max(fb, on + s.msl(13));
                    long fa = std::max(on + s.msl(5), fb - s.msl(30));
                    long sb = std::max(fb + s.msl(5), std::lround((tt[A.k1] + w.Wsec / 2 + 0.01) * sr));
                    sb = std::min(sb, std::lround(tt[nl[j].k0] * sr));
                    long nb = periodicity_break(s, fa, fb, sb, PA);
                    if (dbg(ton))
                        std::fprintf(stderr, "  pbreak fa %.4f fb %.4f sb %.4f PA %.1f -> %.4f\n", double(fa) / sr,
                                     double(fb) / sr, double(sb) / sr, PA, nb >= 0 ? double(nb) / sr : -1.0);
                    if (nb >= 0) {
                        double t0 = double(nb) / sr;
                        std::vector<double> t2, m2;
                        double ta = std::max(tt[A.k0], tt[A.k1] - 0.04);
                        for (long f = 0; f <= nl[j].k1; ++f)
                            if (tt[f] >= ta) { t2.push_back(tt[f]); m2.push_back(mm[f]); }
                        Glide g2 = fit_glide(t2, m2, A.pitch, t0, t0, w.Wsec);
                        if (g2.rmse < 1e8) gl = g2;
                    }
                }
                pt.gin = gl;
                pt.t0 = gl.t0;
                pt.slid = glide_at(std::lround(gl.t0 * sr), nl[j - 1].pitch, nl[j].stable ? nl[j].pitch : gl.target);
                if (!pt.slid) {  // a new note at its own pitch without an energy attack: refine like an onset
                    Onset o;
                    o.n = std::lround(gl.t0 * sr);
                    double PA = sr / (440.0 * std::pow(2.0, (nl[j - 1].pitch - 69) / 12.0));
                    refine_onset(s, o, std::max(on + s.msl(10), o.n - s.msl(30)), PA, ref_pow);
                    pt.t0 = double(o.n) / sr;
                }
                if (dbg(ton))
                    std::fprintf(stderr, "  glide t0 %.4f tau %.4f target %.2f rmse %.3f from %.2f stable %d\n", gl.t0, gl.tau,
                                 gl.target, gl.rmse, nl[j - 1].pitch, nl[j].stable);
                if (!nl[j].stable) pt.pitch = gl.target;
                parts.back().t1 = pt.t0;
            }
            if (nl[j].stable) {  // held pitch: median of the later part of the level
                const Level& L0 = nl[j];
                std::vector<double> v;
                double tfrom = j > 0 ? pt.t0 + 3 * pt.gin.tau + w.Wsec / 2 : tt[L0.k0];
                for (long f = L0.k0; f <= L0.k1; ++f)
                    if (tt[f] >= tfrom) v.push_back(mm[f]);
                if (v.size() >= 5) pt.pitch = median(v);
            }
            pt.t1 = tend;
            parts.push_back(pt);
        }
        std::vector<int> oshift(parts.size(), 0);
        for (size_t j = 0; j < parts.size(); ++j) {
            double f = 440.0 * std::pow(2.0, (parts[j].pitch - 69) / 12.0);
            long a = std::lround((parts[j].t0 + 0.010) * sr), b = std::lround((parts[j].t1 - 0.005) * sr);
            b = std::min(b, a + s.msl(150));
            oshift[j] = octave_up(s, a, b, f);
            parts[j].pitch += oshift[j];
        }
        for (size_t j = 0; j < parts.size(); ++j) {
            Raw rn;
            const Part& pt = parts[j];
            rn.n.time = pt.t0;
            rn.n.end = pt.t1;
            rn.n.note = int(std::lround(pt.pitch));
            rn.n.cents = (pt.pitch - rn.n.note) * 100;
            rn.n.slide_to_next = j + 1 < parts.size() && parts[j + 1].slid;
            rn.attacked = j == 0 || !pt.slid;
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
                double from = nl[j - 1].pitch;
                for (double t = gl.t0; t < gl.t0 + 4 * gl.tau; t += 0.002)
                    pts.push_back({t, gl.target + oshift[j] + (from - gl.target) * std::exp(-(t - gl.t0) / gl.tau) - ref});
                tstart_obs = gl.t0 + 3 * gl.tau + w.Wsec / 2;
            }
            double tstop_obs = j + 1 < parts.size() ? parts[j + 1].t0 - w.Wsec / 2 : pt.t1;
            {   // observed frames, 7-frame median (drops single-frame glitches at attacks / releases)
                std::vector<std::pair<double, double>> ob;
                for (size_t f = 0; f < F; ++f)
                    if (!bad[f] && tt[f] >= tstart_obs + 0.004 && tt[f] <= tstop_obs - 0.004 && tr.dp[ks[f]] < 0.35)
                        ob.push_back({tt[f], mm[f]});
                for (size_t q = 0; q < ob.size(); ++q) {
                    std::vector<double> v;
                    for (size_t r = q >= 3 ? q - 3 : 0; r <= std::min(ob.size() - 1, q + 3); ++r) v.push_back(ob[r].second);
                    pts.push_back({ob[q].first, median(v) + oshift[j] - ref});
                }
            }
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
    if (dropped.empty() || pass == 3) break;
    for (size_t q = dropped.size(); q-- > 0;) ons.erase(ons.begin() + dropped[q]);
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
        // the neighbourhood's normal level: lower quartile of up to 8 attacked notes on either side (accents can be
        // dense, so not the median)
        for (size_t j = i, c = 0; j-- > 0 && c < 8;)
            if (raw[j].attacked) { nb.push_back(raw[j].peak_db); ++c; }
        for (size_t j = i + 1, c = 0; j < raw.size() && c < 8; ++j)
            if (raw[j].attacked) { nb.push_back(raw[j].peak_db); ++c; }
        if (nb.size() >= 3) raw[i].n.accent = raw[i].peak_db - percentile(nb, 0.25) >= 4.5;
        if (const char* dv = std::getenv("DS_ACCENT_DEBUG"))
            if (std::fabs(raw[i].n.time - std::atof(dv)) < 1.5)
                std::fprintf(stderr, "acc %.4f n %d peak %.1f med %.1f -> %d\n", raw[i].n.time, raw[i].n.note,
                             raw[i].peak_db, nb.empty() ? 0.0 : percentile(nb, 0.25), raw[i].n.accent);
    }
    for (auto& r : raw) out.push_back(std::move(r.n));
    if (p) p(1.0, "bass: done");
    return out;
}

}  // namespace ds
