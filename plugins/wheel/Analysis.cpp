// Omega Wheel of Steel analysis: BPM + first downbeat (onset envelope, autocorrelation, beat-by-beat line fit), key
// (chromagram + Krumhansl-Schmuckler) and the waveform summary. Plain C++17 with a small FFT of its own.
#include "Analysis.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <thread>

namespace wheel {

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr float kPiF = 3.14159265358979323846f;

// real FFT of 2^order points through a complex radix-2 FFT of half the size. Same conventions as juce::dsp::FFT:
// forward is unnormalised, inverse divides by N.
class RealFft {
public:
    explicit RealFft(int order) : N(1 << order), M(N / 2), tw((size_t) std::max(1, M / 2)), rt((size_t) M / 2 + 1), z((size_t) M), rev((size_t) M)
    {
        for (int k = 0; k < M / 2; ++k) tw[(size_t) k] = std::polar(1.0, -2 * kPiD * k / M);
        for (int k = 0; k <= M / 2; ++k) rt[(size_t) k] = std::polar(1.0, -2 * kPiD * k / N);
        int bits = 0;
        while ((1 << bits) < M) ++bits;
        for (int i = 0; i < M; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }
    int size() const { return N; }

    // x: N samples -> X: N/2 + 1 bins
    void forward(const float* x, std::complex<float>* X)
    {
        for (int i = 0; i < M; ++i) z[(size_t) rev[(size_t) i]] = { (double) x[2 * i], (double) x[2 * i + 1] };
        fft(false);
        for (int k = 0; k <= M / 2; ++k) {
            const int j = (M - k) % M;
            const std::complex<double> a = z[(size_t) k], b = std::conj(z[(size_t) j]);
            const std::complex<double> fe = 0.5 * (a + b), fo = std::complex<double>(0, -0.5) * (a - b);
            const std::complex<double> w = rt[(size_t) k];
            X[k] = std::complex<float>(fe + w * fo);
            if (k > 0 && k < M / 2) X[M - k] = std::complex<float>(std::conj(fe - w * fo));   // X[M-k] = conj(Fe - W^k Fo)
            else if (k == 0) X[M] = std::complex<float>(fe - fo);
        }
    }
    // magnitudes |X[k]|, k = 0..N/2
    void magnitudes(const float* x, float* mag)
    {
        spec.resize((size_t) M + 1);
        forward(x, spec.data());
        for (int k = 0; k <= M; ++k) mag[k] = std::abs(spec[(size_t) k]);
    }
    // X: N/2 + 1 bins -> x: N samples (divided by N)
    void inverse(const std::complex<float>* X, float* x)
    {
        for (int k = 0; k < M; ++k) {
            const std::complex<double> a = X[k], b = std::conj((std::complex<double>) X[M - k]);
            const std::complex<double> fe = 0.5 * (a + b), fo = 0.5 * (a - b) * std::conj(rtAt(k));
            z[(size_t) rev[(size_t) k]] = fe + std::complex<double>(0, 1) * fo;
        }
        fft(true);
        const double s = 1.0 / M;
        for (int i = 0; i < M; ++i) { x[2 * i] = (float) (z[(size_t) i].real() * s); x[2 * i + 1] = (float) (z[(size_t) i].imag() * s); }
    }

private:
    int N, M;
    std::vector<std::complex<double>> tw, rt, z;
    std::vector<int> rev;
    std::vector<std::complex<float>> spec;
    // e^{-2 pi i k / N} for k = 0..M-1
    std::complex<double> rtAt(int k) const { return k <= M / 2 ? rt[(size_t) k] : std::complex<double>(-rt[(size_t) (M - k)].real(), rt[(size_t) (M - k)].imag()); }
    // in-place iterative FFT of z (already in bit-reversed order)
    void fft(bool inv)
    {
        for (int len = 2; len <= M; len <<= 1) {
            const int half = len / 2, step = M / len;
            for (int i = 0; i < M; i += len)
                for (int k = 0; k < half; ++k) {
                    std::complex<double> w = tw[(size_t) (k * step)];
                    if (inv) w = std::conj(w);
                    const std::complex<double> u = z[(size_t) (i + k)], v = z[(size_t) (i + k + half)] * w;
                    z[(size_t) (i + k)] = u + v;
                    z[(size_t) (i + k + half)] = u - v;
                }
        }
    }
};

// low-pass (windowed sinc) and keep every factor-th sample
std::vector<float> decimate(const float* x, size_t n, int factor)
{
    if (factor <= 1) return std::vector<float>(x, x + n);
    const int H = 6 * factor;
    std::vector<float> h((size_t) (2 * H + 1));
    const double fc = 0.45 / factor;
    double sum = 0;
    for (int k = -H; k <= H; ++k) {
        const double s = k == 0 ? 2 * fc : std::sin(2 * kPiD * fc * k) / (kPiD * k);
        const double w = 0.42 + 0.5 * std::cos(kPiD * k / (H + 1)) + 0.08 * std::cos(2 * kPiD * k / (H + 1));
        h[(size_t) (k + H)] = (float) (s * w);
        sum += s * w;
    }
    for (auto& v : h) v = (float) (v / sum);
    const size_t m = n / (size_t) factor;
    std::vector<float> y(m);
    for (size_t i = 0; i < m; ++i) {
        const long c = (long) (i * (size_t) factor);
        float acc = 0;
        if (c - H >= 0 && c + H < (long) n) {
            const float* xp = x + c - H;
            for (size_t k = 0; k < h.size(); ++k) acc += h[k] * xp[k];
        } else {
            for (int k = -H; k <= H; ++k) {
                const long j = c + k;
                if (j >= 0 && j < (long) n) acc += h[(size_t) (k + H)] * x[j];
            }
        }
        y[i] = acc;
    }
    return y;
}

struct Onsets {
    std::vector<float> env, low, hi;   // combined onset strength, low-band (kick), mid + high (snare, hats)
    double fps = 0, sr = 0;
    int hop = 0, size = 0;
};

Onsets onsets(const std::vector<float>& x, double sr)
{
    Onsets o;
    o.sr = sr;
    o.size = sr > 16000 ? 1024 : 512;
    o.hop = o.size / 4;
    o.fps = sr / o.hop;
    const int order = o.size == 1024 ? 10 : 9, N = o.size, half = N / 2;
    RealFft fft(order);
    std::vector<float> win((size_t) N), buf((size_t) (2 * N)), prev((size_t) half + 1, 0.0f), cur((size_t) half + 1);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * kPiF * (float) i / (float) N);
    const double binHz = sr / N;
    const int lowEnd = std::max(2, (int) std::lround(150 / binHz)), midEnd = (int) std::lround(2000 / binHz), top = std::min(half, (int) std::lround(6000 / binHz));
    const long frames = (long) x.size() / o.hop + 1;
    std::vector<float> fl((size_t) frames), fm((size_t) frames), fh((size_t) frames);
    const float norm = 4.0f / (float) N;
    for (long i = 0; i < frames; ++i) {
        const long s0 = i * o.hop - half;   // frame i is centred on sample i * hop
        for (int k = 0; k < N; ++k) {
            const long j = s0 + k;
            buf[(size_t) k] = (j >= 0 && j < (long) x.size()) ? x[(size_t) j] * win[(size_t) k] : 0.0f;
        }
        std::fill(buf.begin() + N, buf.end(), 0.0f);
        fft.magnitudes(buf.data(), buf.data());
        float a = 0, b = 0, c = 0;
        for (int k = 1; k <= top; ++k) {
            cur[(size_t) k] = std::log1p(100.0f * buf[(size_t) k] * norm);
            const float d = cur[(size_t) k] - prev[(size_t) k];
            if (d > 0) (k < lowEnd ? a : k < midEnd ? b : c) += d;
        }
        std::swap(cur, prev);
        fl[(size_t) i] = a;
        fm[(size_t) i] = b;
        fh[(size_t) i] = c;
    }
    auto meanOf = [](const std::vector<float>& v) { double s = 0; for (float f : v) s += f; return (float) (s / (double) std::max<size_t>(1, v.size()) + 1e-6); };
    const float ml = meanOf(fl), mm = meanOf(fm), mh = meanOf(fh);
    o.env.resize((size_t) frames);
    o.low.resize((size_t) frames);
    o.hi.resize((size_t) frames);
    for (size_t i = 0; i < (size_t) frames; ++i) {
        o.low[i] = fl[i] / ml;
        o.hi[i] = fm[i] / mm + fh[i] / mh;
        o.env[i] = fl[i] / ml + fm[i] / mm + 0.5f * fh[i] / mh;
    }
    // subtract the local mean (+-0.25 s), keep the rise
    const int W = std::max(2, (int) (o.fps * 0.25));
    std::vector<double> cs((size_t) frames + 1, 0.0);
    for (size_t i = 0; i < (size_t) frames; ++i) cs[i + 1] = cs[i] + o.env[i];
    std::vector<float> e2((size_t) frames);
    for (long i = 0; i < frames; ++i) {
        const long a = std::max(0L, i - W), b = std::min(frames, i + W + 1);
        const float m = (float) ((cs[(size_t) b] - cs[(size_t) a]) / (double) (b - a));
        e2[(size_t) i] = std::max(0.0f, o.env[(size_t) i] - m);
    }
    o.env.swap(e2);
    return o;
}

float interp(const std::vector<float>& v, double x)
{
    if (x < 0) return 0;
    const size_t i = (size_t) x;
    if (i + 1 >= v.size()) return i < v.size() ? v[i] : 0.0f;
    const float f = (float) (x - (double) i);
    return v[i] * (1 - f) + v[i + 1] * f;
}

// the strongest point of v within [c - r, c + r], to sub-frame precision; returns the strength
float peakNear(const std::vector<float>& v, double c, double r, double& at)
{
    const long a = std::max(1L, (long) std::ceil(c - r)), b = std::min((long) v.size() - 2, (long) std::floor(c + r));
    float best = -1;
    long bi = -1;
    for (long i = a; i <= b; ++i) if (v[(size_t) i] > best) { best = v[(size_t) i]; bi = i; }
    if (bi < 0) { at = c; return 0; }
    const double y0 = v[(size_t) bi - 1], y1 = v[(size_t) bi], y2 = v[(size_t) bi + 1], den = y0 - 2 * y1 + y2;
    at = (double) bi + (std::abs(den) > 1e-9 ? std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0);
    return best;
}

// onset envelope frames lag the attacks they mark by about this much (measured on synthetic drums)
constexpr double kOnsetLagSeconds = 0.005;

double bpmFromOnsets(const Onsets& o, double& firstBeat, double hint)
{
    firstBeat = 0;
    const auto& env = o.env;
    const size_t n = env.size();
    if (n < (size_t) (o.fps * 4)) return 0;

    // smoothed copy for the autocorrelation
    std::vector<float> sm(n);
    for (size_t i = 0; i < n; ++i) sm[i] = 0.5f * env[i] + 0.25f * (i > 0 ? env[i - 1] : 0.0f) + 0.25f * (i + 1 < n ? env[i + 1] : 0.0f);
    double mean = 0;
    for (float v : sm) mean += v;
    mean /= (double) n;
    int order = 1;
    while ((size_t) (1 << order) < 2 * n) ++order;
    const size_t F = (size_t) 1 << order;
    RealFft fft(order);
    std::vector<float> buf(F, 0.0f);
    for (size_t i = 0; i < n; ++i) buf[i] = (float) (sm[i] - mean);
    std::vector<std::complex<float>> spec(F / 2 + 1);
    fft.forward(buf.data(), spec.data());
    for (auto& c : spec) c = std::complex<float>(c.real() * c.real() + c.imag() * c.imag(), 0.0f);   // power: its inverse is the autocorrelation
    fft.inverse(spec.data(), buf.data());
    const size_t maxLag = std::min(n - 1, (size_t) (o.fps * 60.0 / 30.0 * 4.5));
    std::vector<float> ac(maxLag + 1);
    const float ac0 = std::max(1e-9f, buf[0]);
    for (size_t l = 0; l <= maxLag; ++l) ac[l] = buf[l] / ac0 * (float) n / (float) (n - l);

    auto acAt = [&](double lag) { return lag < (double) maxLag ? std::max(0.0f, interp(ac, lag)) : 0.0f; };
    auto score = [&](double bpm) {
        const double L = o.fps * 60.0 / bpm;
        return acAt(L) + 0.5f * acAt(2 * L) + 0.25f * acAt(4 * L) + 0.25f * acAt(3 * L);
    };
    // mild preference for the 85..135 range (hip-hop .. house) over its double / half
    auto prior = [](double bpm) { const double d = std::log2(bpm / 112.0); return std::exp(-0.5 * d * d / (0.6 * 0.6)); };
    double best = 0, bestV = -1;
    for (double b = 70; b <= 180.0001; b += 0.05) {
        const double v = score(b) * prior(b);
        if (v > bestV) { bestV = v; best = b; }
    }
    if (best <= 0) return 0;
    if (hint > 0) {   // a tagged BPM settles the octave
        for (double m : { 0.5, 2.0, 2.0 / 3.0, 1.5 })
            if (std::abs(best * m - hint) < std::abs(best - hint) && std::abs(best * m - hint) < hint * 0.03) best *= m;
    }

    // refine: follow the beats outward from the middle and fit a straight line (period, phase) through the onsets
    double P = o.fps * 60.0 / best;
    const double centre = (double) n * 0.5;
    double phase = 0, bestPh = -1;
    for (double ph = 0; ph < P; ph += 0.25) {   // best phase over +-16 beats around the centre
        double s = 0;
        for (int k = -16; k <= 16; ++k) s += interp(env, centre + ph + k * P);
        if (s > bestPh) { bestPh = s; phase = ph; }
    }
    double a = centre + phase;   // position of beat k = a + k * P
    struct Pt { double k, x, w; };
    std::vector<Pt> pts;
    const int kMin = -(int) std::floor(a / P), kMax = (int) std::floor(((double) n - 1 - a) / P);
    for (int span = 16;; span *= 2) {
        const int k0 = std::max(kMin, -span), k1 = std::min(kMax, span);
        for (int pass = 0; pass < 2; ++pass) {
            pts.clear();
            for (int k = k0; k <= k1; ++k) {
                double at;
                const float w = peakNear(env, a + k * P, P * (pass == 0 ? 0.12 : 0.07), at);
                if (w > 0) pts.push_back({ (double) k, at, w });
            }
            if (pts.size() < 8) break;
            // weighted least squares, then once more without outliers
            for (int it = 0; it < 2; ++it) {
                double sw = 0, sk = 0, sx = 0, skk = 0, skx = 0;
                for (const auto& p : pts) { sw += p.w; sk += p.w * p.k; sx += p.w * p.x; skk += p.w * p.k * p.k; skx += p.w * p.k * p.x; }
                const double den = sw * skk - sk * sk;
                if (sw <= 0 || std::abs(den) < 1e-12) break;
                const double slope = (sw * skx - sk * sx) / den, icpt = (sx - slope * sk) / sw;
                if (slope > P * 0.97 && slope < P * 1.03) { P = slope; a = icpt; }
                std::vector<Pt> keep;
                for (const auto& p : pts) if (std::abs(p.x - (a + p.k * P)) < P * 0.05) keep.push_back(p);
                if (keep.size() < 8) break;
                pts.swap(keep);
            }
        }
        if (k0 == kMin && k1 == kMax) break;
    }
    double bpm = o.fps * 60.0 / P;
    if (std::abs(bpm - std::round(bpm)) < 0.05) bpm = std::round(bpm);
    else bpm = std::round(bpm * 100) / 100;
    P = o.fps * 60.0 / bpm;
    // phase for the final period: weighted mean over the inlier beats
    {
        double sw = 0, s = 0;
        for (const auto& p : pts) { sw += p.w; s += p.w * (p.x - p.k * P); }
        if (sw > 0) a = s / sw;
    }
    // where the music starts: first frame with a real attack
    float emax = 0;
    for (float v : env) emax = std::max(emax, v);
    double start = 0;
    for (size_t i = 0; i < n; ++i) if (env[i] > emax * 0.15f) { start = (double) i; break; }
    // downbeat: the beat (of 4) where the kick stands out most against snare / hats (beat 1 in hip-hop: kick, no
    // snare); when two or more beats look alike (four to the floor), the one the music starts on
    double lowSum[4] = { 0, 0, 0, 0 }, hiSum[4] = { 0, 0, 0, 0 };
    for (int k = kMin; k <= kMax; ++k) {
        double at;
        const int j = ((k % 4) + 4) % 4;
        lowSum[j] += peakNear(o.low, a + k * P, 2.0, at);
        hiSum[j] += peakNear(o.hi, a + k * P, 2.0, at);
    }
    double r[4];
    int db = 0;
    for (int j = 0; j < 4; ++j) {
        r[j] = lowSum[j] / (lowSum[j] + 0.5 * hiSum[j] + 1e-9);
        if (r[j] > r[db]) db = j;
    }
    {
        const int sj = (((int) std::lround((start - a) / P)) % 4 + 4) % 4;
        if (sj != db && r[sj] > r[db] - 0.03) db = sj;
    }
    const double bar = 4 * P;
    double d = a + db * P;
    d -= std::floor((d - (start - 0.3 * P)) / bar) * bar;   // first downbeat at or after the start
    firstBeat = d * o.hop / o.sr + kOnsetLagSeconds;
    if (firstBeat < 0) firstBeat += bar * o.hop / o.sr;
    return bpm;
}

int keyFromSignal(const std::vector<float>& x, double sr)
{
    // x at ~5.5 kHz (or a bit higher); FFT 8192 there is ~1.5 s with ~0.7 Hz bins
    const int order = sr > 8000 ? 14 : 13, N = 1 << order, hop = N / 2;
    if ((int) x.size() < N) return -1;
    RealFft fft(order);
    std::vector<float> win((size_t) N), buf((size_t) (2 * N));
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * kPiF * (float) i / (float) N);
    const double binHz = sr / N;
    const int k0 = (int) std::ceil(55.0 / binHz), k1 = std::min(N / 2 - 2, (int) std::floor(2000.0 / binHz));
    std::vector<std::vector<float>> mags;
    for (size_t s = 0; s + (size_t) N <= x.size(); s += (size_t) hop) {
        for (int i = 0; i < N; ++i) buf[(size_t) i] = x[s + (size_t) i] * win[(size_t) i];
        std::fill(buf.begin() + N, buf.end(), 0.0f);
        fft.magnitudes(buf.data(), buf.data());
        mags.emplace_back(buf.begin() + k0, buf.begin() + k1 + 1);
    }
    // tuning: where the spectral peaks sit relative to the 440 Hz grid
    double cx = 0, cy = 0;
    for (const auto& m : mags)
        for (size_t i = 1; i + 1 < m.size(); ++i)
            if (m[i] > m[i - 1] && m[i] >= m[i + 1] && m[i] > 1e-3f) {
                const double den = m[i - 1] - 2 * m[i] + m[i + 1];
                const double off = std::abs(den) > 1e-12 ? 0.5 * (m[i - 1] - m[i + 1]) / den : 0.0;
                const double f = ((double) (i + (size_t) k0) + off) * binHz, midi = 69 + 12 * std::log2(f / 440.0);
                const double ang = 2 * kPiD * (midi - std::round(midi));
                cx += m[i] * std::cos(ang);
                cy += m[i] * std::sin(ang);
            }
    const double tune = std::atan2(cy, cx) / (2 * kPiD);   // semitones, -0.5..0.5
    std::vector<int> pcOf((size_t) (k1 - k0 + 1));
    std::vector<float> wOf((size_t) (k1 - k0 + 1));
    for (int k = k0; k <= k1; ++k) {
        const double midi = 69 + 12 * std::log2(k * binHz / 440.0) - tune, r = std::round(midi), d = midi - r;
        pcOf[(size_t) (k - k0)] = ((int) r % 12 + 12) % 12;
        // only bins close to a semitone count; weaker high up (harmonics)
        const double c = std::cos(kPiD * d);
        wOf[(size_t) (k - k0)] = (float) (std::abs(d) < 0.35 ? c * c : 0.0) * (float) (k * binHz < 400 ? 1.0 : 400.0 / (k * binHz));
    }
    std::array<double, 12> chroma {};
    for (const auto& m : mags) {
        std::array<double, 12> c {};
        double tot = 0;
        for (size_t i = 1; i + 1 < m.size(); ++i) {
            if (m[i] < m[i - 1] || m[i] < m[i + 1]) continue;   // peaks only
            const double v = std::sqrt((double) m[i]) * wOf[i];
            c[(size_t) pcOf[i]] += v;
            tot += v;
        }
        if (tot <= 0) continue;
        for (int i = 0; i < 12; ++i) chroma[(size_t) i] += c[(size_t) i];
    }
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&](const double* prof, int root) {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) ((i + root) % 12)]; my += prof[i]; }
        mx /= 12;
        my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) {
            const double a = chroma[(size_t) ((i + root) % 12)] - mx, b = prof[i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return sxx > 0 ? sxy / std::sqrt(sxx * syy) : 0.0;
    };
    int best = -1;
    double bv = -2;
    for (int r = 0; r < 12; ++r) {
        const double a = corr(maj, r), b = corr(mnr, r);
        if (a > bv) { bv = a; best = r; }
        if (b > bv) { bv = b; best = 12 + r; }
    }
    return bv > 0.1 ? best : -1;
}

int factorFor(double sr, double target) { return std::max(1, (int) std::lround(sr / target)); }

}  // namespace

double detectBpm(const std::vector<float>& mono, double sr, double& firstBeat)
{
    firstBeat = 0;
    if (mono.empty() || sr <= 0) return 0;
    const int f = factorFor(sr, 11025);
    const auto ds = decimate(mono.data(), mono.size(), f);
    return bpmFromOnsets(onsets(ds, sr / f), firstBeat, 0);
}

int detectKey(const std::vector<float>& mono, double sr)
{
    if (mono.empty() || sr <= 0) return -1;
    const int f = factorFor(sr, 5512.5);
    return keyFromSignal(decimate(mono.data(), mono.size(), f), sr / f);
}

void buildWave(const Track& t, Wave& w)
{
    const size_t n = t.L.size();
    const size_t cols = (size_t) std::ceil((double) n * kWaveRate / t.sr);
    w.low.assign(cols, 0);
    w.mid.assign(cols, 0);
    w.high.assign(cols, 0);
    w.peak.assign(cols, 0);
    if (n == 0 || cols == 0) return;
    // RBJ biquads: low = LP 250 x2, mid = HP 250 + LP 2500, high = HP 2500 x2
    struct BQ {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set(double sr, double f, bool high)
        {
            const double w0 = 2 * kPiD * f / sr, c = std::cos(w0), al = std::sin(w0) / (2 * 0.7071), a0 = 1 + al;
            if (high) { b0 = (float) ((1 + c) / 2 / a0); b1 = (float) (-(1 + c) / a0); }
            else { b0 = (float) ((1 - c) / 2 / a0); b1 = (float) ((1 - c) / a0); }
            b2 = b0;
            a1 = (float) (-2 * c / a0);
            a2 = (float) ((1 - al) / a0);
        }
        float operator()(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    BQ l1, l2, m1, m2, h1, h2;
    l1.set(t.sr, 250, false); l2.set(t.sr, 250, false);
    m1.set(t.sr, 250, true); m2.set(t.sr, 2500, false);
    h1.set(t.sr, 2500, true); h2.set(t.sr, 2500, true);
    std::vector<float> fl(cols), fm(cols), fh(cols), fp(cols);
    const bool st = t.R.size() == n;
    for (size_t c = 0; c < cols; ++c) {
        const size_t s0 = (size_t) ((double) c * t.sr / kWaveRate), s1 = std::min(n, (size_t) ((double) (c + 1) * t.sr / kWaveRate));
        float a = 0, b = 0, d = 0, p = 0;
        for (size_t i = s0; i < s1; ++i) {
            const float l = t.L[i], r = st ? t.R[i] : l, x = 0.5f * (l + r);
            a = std::max(a, std::abs(l2(l1(x))));
            b = std::max(b, std::abs(m2(m1(x))));
            d = std::max(d, std::abs(h2(h1(x))));
            p = std::max(p, std::max(std::abs(l), std::abs(r)));
        }
        fl[c] = a; fm[c] = b; fh[c] = d; fp[c] = p;
    }
    // scale so the loud part of the track reaches the top (99th percentile -> 245)
    auto pct = [](std::vector<float> v) {
        if (v.empty()) return 0.0f;
        const size_t k = (size_t) ((double) (v.size() - 1) * 0.99);
        std::nth_element(v.begin(), v.begin() + (long) k, v.end());
        return v[k];
    };
    const float pp = std::max(1e-4f, pct(fp)), sp = 245.0f / pp;
    auto fill = [&](const std::vector<float>& src, std::vector<uint8_t>& dst, float scale) {
        for (size_t c = 0; c < cols; ++c) dst[c] = (uint8_t) std::clamp((int) std::lround(src[c] * scale), 0, 255);
    };
    auto bandScale = [&](const std::vector<float>& v) { return std::min(245.0f / std::max(1e-6f, pct(v)), sp * 4.0f); };
    fill(fp, w.peak, sp);
    fill(fl, w.low, bandScale(fl));
    fill(fm, w.mid, bandScale(fm));
    fill(fh, w.high, bandScale(fh));
}


void analyseTrack(Track& t, bool haveGrid, double tagBpm, const std::function<bool()>& stop)
{
    auto cancelled = [&] { return stop && stop(); };
    std::thread waveThread([&t] { buildWave(t, t.wave); });
    {
        const bool st = t.R.size() == t.L.size();
        std::vector<float> mono(t.L.size());
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (t.L[i] + (st ? t.R[i] : t.L[i]));
        const int fac = factorFor(t.sr, 11025);
        std::vector<float> ds = decimate(mono.data(), mono.size(), fac);
        mono = {};
        const double dsr = t.sr / fac;
        if (! haveGrid && ! cancelled()) {
            double fb = 0;
            double bpm = bpmFromOnsets(onsets(ds, dsr), fb, tagBpm);
            if (bpm > 0) {
                if (tagBpm > 0 && std::abs(bpm - tagBpm) < 0.1) bpm = tagBpm;
                t.bpm = bpm;
                t.firstBeat = fb;
            }
        }
        if (t.key < 0 && ! cancelled()) t.key = keyFromSignal(decimate(ds.data(), ds.size(), 2), dsr / 2);
    }
    waveThread.join();
}

}  // namespace wheel
