// Drum Scribe core: bass transcription (monophonic bass stem -> notes with sample-accurate onsets, releases, pitch,
// slides, bends, velocity and accents).
//
// Pipeline
//  1. pitch: decimate to ~11 kHz, YIN difference function with a centred window (every lag compared symmetrically
//     around the frame centre, so the frame time does not depend on the lag), updated incrementally every ~1 ms;
//     pYIN-style candidate mass (beta prior over thresholds) + Viterbi over candidates/unvoiced with a semitone-jump
//     cost; a sub-harmonic rescue pass undoes short drops to f/2, f/3, f/4 (kick bleed) between held pitches.
//  2. onsets: candidates from a period-adaptive energy ratio and from the residual of a one-period-back predictor
//     (one- and two-sided); each refined to the sample by CUSUM on the residual of a long-term (pitch-period)
//     predictor of the previous note, with a mute/gate-off of the old note told apart from the new attack. Candidates
//     are dropped when the previous note's phase runs on through them (bleed laid on top), when the attack is not
//     clear, or when the pitch moves there by a glide (slide, not an attack).
//  3. per segment: release = continuous hinge fit on a pitch-synchronous log envelope where energy falls away, or
//     the next onset (legato); semitone DP over the pitch curve -> held levels; a level change without an attack is
//     a slide when it is a glide (transition zone that neither the old nor the new period explains, or time spent at
//     intermediate periods), else a new note; the glide start is pinned where the old period breaks (hinge on the
//     predictor residual) and its shape fitted through the YIN window; bends/excursions that return are bends;
//     octave check from odd/even harmonic energy.
//  4. kick bleed / noise: thumps (low, fast decay, late voicing or falling pitch, little energy above 250 Hz) and
//     unvoiced or very quiet segments give their onset up and the segmentation is redone. Velocity from the note's
//     level 30-70 ms in (dB, relative to the song's loud notes; kick-inflated levels corrected from the harmonics
//     above 250 Hz); accent when >= 4.5 dB over the lower quartile of nearby notes of similar pitch.
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <cstdio>
#include <cstdlib>  // DEBUG

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
    std::vector<double> SA;  // prefix sums of the signal above ~250 Hz squared: note level without kick-drum bleed
    std::vector<double> xb;  // the signal above ~250 Hz (causal 4th-order high-pass): pluck / attack brightness
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
    double powa(long a, long b) const {
        if (b <= a) return 0;
        long aa = std::clamp(a, 0L, N), bb = std::clamp(b, 0L, N);
        return (SA[bb] - SA[aa]) / double(b - a);
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
    double clarity = 0;  // peak of the unexplained part over its level before (dB): how clear the attack is
    bool decay = false;  // only the previous note dying away (release), no attack
    double bright = 0;   // rise of the band above 250 Hz at the onset (dB): a pick / finger / filter-envelope attack
    bool band = false;   // found by the band (brightness) detector
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
            if (nq >= nr + s.msl(3)) {
                double ex = 0, eh = 0;
                for (long n = nr; n < nq; ++n) { ex += s.at(n) * s.at(n); eh += X(n) * X(n); }
                if (ex < 0.3 * eh) { nr = nq; clar = c2; }  // [nr, nq) is only the old note dying away
            } else if (nq < 0) {
                // nothing rises above the old note: a release (it dies away) or a softer re-attack (a new note
                // stays): look 10-25 ms on
                double ex = 0, eh = 0;
                for (long n = nr + s.msl(10); n < nr + s.msl(25); ++n) {
                    double xh = pr.g * s.frac(n - pr.P);
                    ex += s.at(n) * s.at(n);
                    eh += xh * xh;
                }
                if (ex < 0.07 * eh) o.decay = true;
            }
        }
        o.n = nr;
    }
    o.clarity = clar;
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

// refine a brightness candidate on the band above 250 Hz: CUSUM back to where its energy starts to rise
void refine_band_onset(const Sig& s, Onset& o, long lo, double ref_band) {
    const long nc = o.n;
    const long s0 = std::max(lo, nc - s.msl(15)), e1 = std::min(s.N, nc + s.msl(10));
    const long p0 = std::max(lo - s.msl(10), s0 - s.msl(25));
    if (e1 <= s0 || s0 - p0 < s.msl(3)) return;
    auto B = [&](long n) { return (n >= 0 && n < s.N) ? s.xb[n] * s.xb[n] : 0.0; };
    double sig2 = 0;
    for (long n = p0; n < s0; ++n) sig2 += B(n);
    sig2 = std::max(sig2 / (s0 - p0), ref_band * 1e-8);
    double A = 0;
    for (long n = s0; n < e1; ++n) A = std::max(A, B(n));
    o.clarity = db(A) - db(sig2);
    if (A < 10 * sig2) return;
    long n1 = s0;
    while (n1 < e1 - 1 && B(n1) <= std::max(9 * sig2, 0.05 * A)) ++n1;
    double c = 0, cmin = 0;
    long nb = s0;
    for (long n = s0; n <= n1; ++n) {
        if (c < cmin) { cmin = c; nb = n; }
        c += B(n) - 3 * sig2;
    }
    o.n = nb;
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
    const long L = long(lo) + 1;
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
        if (bt > t0 && bt < t1 && bc > 0.7) {
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
    if (!(tot > 0 && ev > 0.3 * tot && od < 0.08 * ev)) return 0;
    // ... and the "fundamental" is bleed (a kick's sine): it dies away much faster than the even partials (a real
    // bass whose pickup position starves the odd partials keeps all of them decaying together)
    const long m = (a + b) / 2;
    if (m - a < long(2 * s.sr / f)) return 0;
    double f1a = tone_power(s, a, m, f), f1b = tone_power(s, m, b, f);
    double f2a = tone_power(s, a, m, 2 * f), f2b = tone_power(s, m, b, 2 * f);
    double drop1 = db(f1a + 1e-30) - db(f1b + 1e-30), drop2 = db(f2a + 1e-30) - db(f2b + 1e-30);
    return drop1 - drop2 > 6 ? 12 : 0;
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
    {  // 4th-order high-pass at 250 Hz (two biquads): the level of the bass harmonics above kick-drum bleed
        const double f = 250.0, wc = 2 * kPi * f / sr, cw = std::cos(wc);
        std::vector<double> y(s.x);
        for (double q : {0.5412, 1.3066}) {
            const double al = std::sin(wc) / (2 * q), a0 = 1 + al;
            const double b0 = (1 + cw) / 2 / a0, b1 = -(1 + cw) / a0, b2 = b0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (long n = 0; n < s.N; ++n) {
                double v = b0 * y[n] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = y[n]; y2 = y1; y1 = v;
                y[n] = v;
            }
        }
        s.SA.assign(s.N + 1, 0.0);
        for (long n = 0; n < s.N; ++n) s.SA[n + 1] = s.SA[n] + y[n] * y[n];
        s.xb = std::move(y);
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
    std::vector<double> Orr(K, 0), Orr2(K, 0);  // one-sided (rises only) and two-sided (any change)
    for (int two = 0; two < 2; ++two) {
        std::vector<double>& Ot = two ? Orr2 : Orr;
        std::vector<double> SR(s.N + 1, 0.0);
        const long lag = std::lround(0.020 * sr / H);
        for (long n = 0; n < s.N; ++n) {
            long k = std::lround(double(n) / H) - lag;
            double P = w.period(k);
            double v = P <= 0 ? s.x[n] : two ? s.x[n] - s.frac(n - P) : std::max(0.0, std::fabs(s.x[n]) - std::fabs(s.frac(n - P)));
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
            Ot[k] = db(ef + eps) - db(eb + eps);
            if (s.pow(n, n + L) < ref_pow * std::pow(10.0, -4.0)) Ot[k] = 0;
        }
    }
    // brightness ODF: the band above 250 Hz, 8 ms after vs. 2-14 ms before. A plucked / picked note re-attacks the
    // upper partials even when the overall level hardly moves (compressed legato playing); a slide does not.
    std::vector<double> Ob(K, 0);
    double ref_band = 0;
    {
        std::vector<double> pw;
        for (long n = 0; n + s.msl(10) <= s.N; n += s.msl(5)) pw.push_back(s.powa(n, n + s.msl(10)));
        ref_band = std::max(percentile(pw, 0.99), 1e-24);
        const double eps = ref_band * 1e-5;
        for (long k = 0; k < K; ++k) {
            // windows of at least one period: the upper partials of a low, resonant tone pulse once per period
            long n = k * H, L = std::max(s.msl(8), std::lround(Lk[k]));
            double af = s.powa(n, n + L);
            Ob[k] = af < ref_band * 1e-4 ? 0.0 : db(af + eps) - db(s.powa(n - L - s.msl(2), n - s.msl(2)) + eps);
        }
    }
    auto bright_at = [&](long n) {
        double b = -100;
        for (long k = w.kof(n - s.msl(3)); k <= w.kof(n + s.msl(5)); ++k)
            if (k >= 0 && k < K) b = std::max(b, Ob[k]);
        return b;
    };
    std::vector<Onset> ons;
    {
        const long r = std::lround(s.ms(10) / H);
        for (long k = 1; k + 1 < K; ++k) {  // brightness candidates
            if (Ob[k] < 8.0) continue;
            bool mx = true;
            for (long j = std::max(0L, k - r); j <= std::min(K - 1, k + r) && mx; ++j)
                if (Ob[j] > Ob[k] || (Ob[j] == Ob[k] && j < k)) mx = false;
            if (!mx) continue;
            Onset o;
            o.n = k * H;
            o.strength = std::max(0.0, O[k]);
            o.band = true;
            ons.push_back(o);
        }
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
        for (int pass = 0; pass < 2; ++pass)
        for (long k = 1; k + 1 < K; ++k) {
            const std::vector<double>& Ov = pass == 0 ? Orr : Orr2;
            if (Ov[k] < 10.0) continue;
            bool mx = true;
            for (long j = std::max(0L, k - r); j <= std::min(K - 1, k + r) && mx; ++j)
                if (Ov[j] > Ov[k] || (Ov[j] == Ov[k] && j < k)) mx = false;
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
        if (ons[i].band) refine_band_onset(s, ons[i], lo, ref_band);
        else refine_onset(s, ons[i], lo, P, ref_pow);
        ons[i].bright = bright_at(ons[i].n);
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
    }
    // dedupe (< 25 ms apart: keep the clearer attack)
    {
        std::sort(ons.begin(), ons.end(), [](const Onset& a, const Onset& b) { return a.n < b.n; });
        std::vector<Onset> o2;
        for (auto& o : ons) {
            if (!o2.empty() && o.n - o2.back().n < s.msl(25)) {
                // keep the one where the attack (brightness) is, else the clearer change
                // (an attack out of a gap - dip - is never displaced by a later one)
                bool bo = o.bright >= 8, bb = o2.back().bright >= 8;
                if (!o2.back().dip && (bo != bb ? bo : o.clarity > o2.back().clarity)) o2.back() = o;
                continue;
            }
            o2.push_back(o);
        }
        ons.swap(o2);
    }
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
        double fi = 1e3 * fine_intermediate(s, n, per(mb), per(ma));
        if (std::getenv("BASS_DBG"))
            std::fprintf(stderr, "glide_at %.4f %.2f->%.2f Z %.1f Zc %.1f im %.1f fi %.1f\n", double(n) / sr, mb, ma, Z, Zc, im, fi);
        if (Zc >= 20 || im >= 25) return true;
        return fi >= 3.5;
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
            if (std::getenv("BASS_DBG"))
                std::fprintf(stderr, "cand %.4f str %.1f nov %.2f dip %d clar %.1f cont %.2f decay %d bright %.1f band %d mb %.2f ma %.2f\n",
                             double(o.n) / sr, o.strength, o.novelty, o.dip, o.clarity, o.cont, o.decay, o.bright, o.band, mb, ma);
            if (o.decay) continue;
            if (o.bright >= 8 && !(o.cont > 0.75)) { o2.push_back(o); continue; }  // a real (bright) attack
            // weak or ambiguous attacks: a real attack stands far above what came before it (from silence > 30 dB,
            // a legato re-pluck > 20 dB); bleed on a held note leaves the note's phase running on
            if (o.clarity < 12) continue;
            // no energy rise, the old note still explains what follows, and the pitch is lost right after: the start
            // of a fast glide (slide), not an attack
            if (!o.dip && o.strength < 3 && o.novelty < 0.3 && !std::isnan(mb) && std::isnan(ma)) continue;
            if (!o.dip && o.clarity < 15) continue;
            if (o.cont > 0.75 && (!o.dip || o.clarity < 20)) continue;
            if (same && !o.dip && (o.novelty < 0.25 || o.cont > 0.6)) continue;
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

    // -------------------------------------------------------------- segments -> notes
    struct Raw {
        BassNote n;
        double peak_db = -300;  // full band
        double peak_hp = -300;  // above 250 Hz
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
            }
        }
        if (tend - ton < 0.02) tend = std::min(tnext, ton + 0.02);
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
            const double hpr = db(s.powa(on, on + s.msl(80)) + 1e-30) - db(s.pow(on, on + s.msl(80)) + 1e-30);
            // ... and a kick is (nearly) a pure low sine: little energy above 250 Hz, unlike a bass note's harmonics
            if (fast_decay && low && (falls || late_voicing) && hpr < -15) { dropped.push_back(i); continue; }
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
                bool band_attack = false;
                {   // a pick / finger attack (the band above 250 Hz jumps) during the change: a re-attacked note, not a
                    // slide - on a real bass the fretting hand moves first and the pluck lands 10-60 ms later
                    double bm = -100;
                    long bk = -1;
                    const double te = std::min(gl.t0 + 0.06, tend - 0.01);
                    for (long k = w.kof(std::lround((gl.t0 - 0.005) * sr)); k <= w.kof(std::lround(te * sr)); ++k)
                        if (k >= 0 && k < K && Ob[k] > bm) { bm = Ob[k]; bk = k; }
                    if (std::getenv("BASS_DBG"))
                        std::fprintf(stderr, "trans %.4f %.2f->%.2f slid %d brightmax %.1f at %.4f\n", gl.t0,
                                     nl[j - 1].pitch, nl[j].pitch, pt.slid, bm, bk >= 0 ? w.tk(bk) : -1.0);
                    if (pt.slid && bm >= 5 && bk >= 0) {
                        pt.slid = false;
                        Onset o;
                        o.n = bk * H;
                        refine_band_onset(s, o, std::max(on + s.msl(10), o.n - s.msl(30)), ref_band);
                        pt.t0 = double(o.n) / sr;
                        band_attack = true;
                    }
                }
                if (!pt.slid && !band_attack) {  // a new note at its own pitch without an energy attack: refine like an onset
                    Onset o;
                    o.n = std::lround(gl.t0 * sr);
                    double PA = sr / (440.0 * std::pow(2.0, (nl[j - 1].pitch - 69) / 12.0));
                    refine_onset(s, o, std::max(on + s.msl(10), o.n - s.msl(30)), PA, ref_pow);
                    pt.t0 = double(o.n) / sr;
                }
                if (!nl[j].stable) {  // still arriving: extrapolate toward the fitted target, at most half a semitone
                    double dir = nl[j].pitch > nl[j - 1].pitch ? 1 : -1;
                    pt.pitch = nl[j].pitch + dir * std::clamp(dir * (gl.target - nl[j].pitch), 0.0, 0.5);
                }
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
        // a "slide" that the octave check puts back on the previous pitch was the tracker dropping an octave
        for (size_t j = 1; j < parts.size();) {
            if (parts[j].slid && std::lround(parts[j].pitch) == std::lround(parts[j - 1].pitch)) {
                parts[j - 1].t1 = parts[j].t1;
                parts.erase(parts.begin() + j);
                oshift.erase(oshift.begin() + j);
            } else {
                ++j;
            }
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
            // peak level of the harmonics above 250 Hz in the first 60 ms (pitch-synchronous window)
            double pk = -300;
            {
                long a0 = std::lround(pt.t0 * sr), b0 = std::lround(pt.t1 * sr);
                long a = b0 - a0 > s.msl(45) ? a0 + s.msl(30) : a0, b = std::min(b0, a0 + s.msl(70));
                for (long n = a; n <= b; n += s.msl(1)) pk = std::max(pk, db(s.powa(n - L / 2, n - L / 2 + L) + ref_pow * 1e-12));
            }
            // full band, 30-70 ms into the note: past the attack transient and most of a coincident kick's thump
            double pkf = -300;
            {
                long a = std::lround(pt.t0 * sr), b = std::lround(pt.t1 * sr);
                long fa = b - a > s.msl(45) ? a + s.msl(30) : a, fb = std::min(b, a + s.msl(70));
                for (long k = std::max(k0, w.kof(fa)); k <= std::min(k1, w.kof(fb)); ++k) pkf = std::max(pkf, e[k - k0]);
            }
            rn.peak_db = pkf;
            rn.peak_hp = pk;
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
                    for (size_t r = q >= 5 ? q - 5 : 0; r <= std::min(ob.size() - 1, q + 5); ++r) v.push_back(ob[r].second);
                    pts.push_back({ob[q].first, median(v) + oshift[j] - ref});
                }
            }
            double mx = 0;
            for (auto& b : pts) mx = std::max(mx, std::fabs(b.semitones));
            // only real bends: half a semitone or more (pitch drift, string inharmonicity and kick bleed move the
            // tracked pitch by a few tens of cents, which would turn into hundreds of wobbling bend events), and
            // written in 10-cent steps
            if (mx >= 0.5 && !pts.empty()) {
                std::vector<PitchBend> dec;
                dec.push_back(pts.front());
                for (size_t q = 1; q < pts.size(); ++q)
                    if (std::fabs(pts[q].semitones - dec.back().semitones) > 0.10) dec.push_back(pts[q]);
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
    {   // a note whose full-band peak stands out from its harmonics' level by much more than usual has bleed (a kick)
        // on its attack: estimate its level from the harmonics instead
        // (the full-band / harmonics ratio depends on the pitch, so compare with notes of similar pitch)
        std::vector<double> fix(raw.size(), 0);
        for (size_t i = 0; i < raw.size(); ++i) {
            std::vector<double> d;
            for (auto& r : raw)
                if (std::abs(r.n.note - raw[i].n.note) <= 5) d.push_back(r.peak_db - r.peak_hp);
            if (d.size() < 5) continue;
            double md = median(d);
            if (raw[i].peak_db - raw[i].peak_hp > md + 3) fix[i] = raw[i].peak_hp + md;
        }
        for (size_t i = 0; i < raw.size(); ++i)
            if (fix[i] != 0) raw[i].peak_db = fix[i];
    }
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
        // (level depends on the pitch - string, filter - so prefer neighbours within 7 semitones)
        for (int pass = 0; pass < 2 && nb.size() < 4; ++pass) {
            nb.clear();
            const int span = pass == 0 ? 7 : 1000, cnt = pass == 0 ? 12 : 8;
            for (size_t j = i, c = 0; j-- > 0 && c < size_t(cnt);)
                if (raw[j].attacked && std::abs(raw[j].n.note - raw[i].n.note) <= span) { nb.push_back(raw[j].peak_db); ++c; }
            for (size_t j = i + 1, c = 0; j < raw.size() && c < size_t(cnt); ++j)
                if (raw[j].attacked && std::abs(raw[j].n.note - raw[i].n.note) <= span) { nb.push_back(raw[j].peak_db); ++c; }
        }
        if (nb.size() >= 3) raw[i].n.accent = raw[i].peak_db - percentile(nb, 0.25) >= 4.5;
    }
    for (auto& r : raw) out.push_back(std::move(r.n));
    if (p) p(1.0, "bass: done");
    return out;
}

}  // namespace ds
