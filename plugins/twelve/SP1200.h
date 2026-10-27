// Omega Twelve SP engine in C++: a line-for-line port of twelve/sp1200.js (the SP-1200 / SP-12 audio path simulated
// at circuit level from the service-manual schematics), plus SPLive, the same path running on live input.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace twelve { namespace sp {

static const double CLOCK = 20e6 / 768, SLOT = 4.8e-6, VT = 0.025852, PI_ = 3.14159265358979323846;
static const int TABLE[32] = {50,53,56,59,63,67,71,75,80,84,90,95,101,107,113,120,127,135,143,151,160,170,180,191,202,214,227,241,255,255,255,255};

// netlists: service manual values (SP-1200 SK103 rev D; transcribed in libmd12, MIT)
inline const char* net(const std::string& name) {
    if (name == "aa") return "R in s1 10k; R s1 v11 10k; R v13 s1 10k; X 0 s1 v11; R v11 s12 19.1k; R s12 v12 21k; C s12 v12 1000p; X 0 s12 v12;"
        "R v12 s13 19.1k; C s13 v13 1000p; X 0 s13 v13; R v11 s2 107k; R v12 s2 118k; R v13 s2 14.7k;"
        "R s2 v21 10k; R v23 s2 10k; X 0 s2 v21; R v21 s22 13.3k; R s22 v22 43.2k; C s22 v22 1000p; X 0 s22 v22;"
        "R v22 s23 13.3k; C s23 v23 1000p; X 0 s23 v23; R v21 s3 124k; R v22 s3 402k; R v23 s3 47.5k;"
        "R s3 v31 10k; R v33 s3 10k; X 0 s3 v31; R v31 s32 12.1k; R s32 v32 162k; C s32 v32 1000p; X 0 s32 v32;"
        "R v32 s33 12.1k; C s33 v33 1000p; X 0 s33 v33; R v31 s4 6.19k; R v32 s4 82.5k; R v33 s4 3.92k;"
        "R s4 out 31.6k; C s4 out 1000p; X 0 s4 out";
    if (name == "out3") return "R in na 7.5k; C na 0 4700p; R na nb 3.6k; R nb nc 6.2k; C nc 0 1000p; C nb o1 0.01u; X nc o1 o1;"
        "R o1 nd 6.2k; R nd ne 6.2k; C ne 0 1000p; C nd out 0.01u; X ne out out";
    if (name == "out4") return "R in na 6.2k; C na 0 4700p; R na nb 3.0k; R nb nc 5.1k; C nc 0 1000p; C nb o1 0.01u; X nc o1 o1;"
        "R o1 nd 5.1k; R nd ne 5.1k; C ne 0 1000p; C nd out 0.01u; X ne out out";
    if (name == "out5") return "R in na 5.1k; C na 0 4700p; R na nb 2.4k; R nb nc 4.3k; C nc 0 1000p; C nb o1 0.01u; X nc o1 o1;"
        "R o1 nd 4.3k; R nd ne 4.3k; C ne 0 1000p; C nd out 0.01u; X ne out out";
    if (name == "out6") return "R in na 4.3k; C na 0 4700p; R na nb 2.2k; R nb nc 3.9k; C nc 0 1000p; C nb o1 0.01u; X nc o1 o1;"
        "R o1 nd 3.9k; R nd ne 3.9k; C ne 0 1000p; C nd out 0.01u; X ne out out";
    return "R in nout 3.3k; C nout nsum 10u; R nsum out 15k; C nsum out 470p; X 0 nsum out";   // dynout
}
inline double val(const std::string& s) {
    size_t i = 0; while (i < s.size() && (std::isdigit((unsigned char) s[i]) || s[i] == '.')) ++i;
    const double v = std::stod(s.substr(0, i)); const std::string u = s.substr(i);
    if (u == "Meg") return v * 1e6;
    if (u.empty()) return v;
    switch (u[0]) { case 'p': return v * 1e-12; case 'n': return v * 1e-9; case 'u': return v * 1e-6; case 'm': return v * 1e-3; case 'k': return v * 1e3; case 'M': return v * 1e6; default: return v; }
}

/* ---------------- linear circuit -> discrete state space (MNA, trapezoidal) ---------------- */
class Circuit {
public:
    Circuit() = default;
    Circuit(const std::string& text, double fs, double capScale = 1) { compile(text, fs, capScale); }
    double process(double u) {
        double y = D * u;
        for (int k = 0; k < n; ++k) y += C[(size_t) k] * s[(size_t) k];
        for (int k = 0; k < n; ++k) { double v = B[(size_t) k] * u; const double* a = &A[(size_t) (k * n)]; for (int j = 0; j < n; ++j) v += a[j] * s[(size_t) j]; t[(size_t) k] = v; }
        std::swap(s, t);
        return y;
    }
    void reset() { std::fill(s.begin(), s.end(), 0.0); }
    double dc() { double y = 0; for (int i = 0; i < 200000; ++i) y = process(1); reset(); return y; }
    int n = 0;
private:
    void compile(const std::string& text, double fs, double capScale) {
        std::vector<std::vector<std::string>> els;
        std::string tok; std::stringstream all(text);
        while (std::getline(all, tok, ';')) { std::stringstream ls(tok); std::vector<std::string> e; std::string w; while (ls >> w) e.push_back(w); if (!e.empty()) els.push_back(e); }
        std::vector<std::string> names;
        auto id = [&](const std::string& nm) -> int { if (nm == "0") return -1; for (size_t i = 0; i < names.size(); ++i) if (names[i] == nm) return (int) i; names.push_back(nm); return (int) names.size() - 1; };
        for (auto& e : els) for (size_t i = 1; i < (e[0] == "X" ? 4u : 3u); ++i) id(e[i]);
        id("in"); id("out");
        int nOps = 0; std::vector<const std::vector<std::string>*> caps;
        for (auto& e : els) { if (e[0] == "X") nOps++; if (e[0] == "C") caps.push_back(&e); }
        const int N = (int) names.size(), M = N + 1 + nOps; const double T = 1 / fs;
        std::vector<double> G((size_t) (M * M), 0.0);
        auto g = [&](int r, int c) -> double& { return G[(size_t) (r * M + c)]; };
        auto stamp = [&](int a, int b, double v) { if (a >= 0) g(a, a) += v; if (b >= 0) g(b, b) += v; if (a >= 0 && b >= 0) { g(a, b) -= v; g(b, a) -= v; } };
        const double w0 = 2 * PI_ * 13000, warp = w0 / (2 / T * std::tan(w0 * T / 2));
        std::vector<double> gc;
        for (auto* e : caps) gc.push_back(2 * val((*e)[3]) * capScale * warp / T);
        for (auto& e : els) if (e[0] == "R") stamp(id(e[1]), id(e[2]), 1 / val(e[3]));
        for (size_t k = 0; k < caps.size(); ++k) stamp(id((*caps[k])[1]), id((*caps[k])[2]), gc[k]);
        const int vin = id("in"); g(vin, N) += 1; g(N, vin) = 1;
        int k0 = 0;
        for (auto& e : els) if (e[0] == "X") { const int r = N + 1 + k0++, p = id(e[1]), q = id(e[2]), o = id(e[3]); g(o, r) += 1; if (p >= 0) g(r, p) += 1; if (q >= 0) g(r, q) -= 1; }
        auto solve = [&](std::vector<double> x) {
            std::vector<double> A2 = G;
            auto a = [&](int r, int c) -> double& { return A2[(size_t) (r * M + c)]; };
            for (int c = 0; c < M; ++c) {
                int p = c; for (int r = c + 1; r < M; ++r) if (std::fabs(a(r, c)) > std::fabs(a(p, c))) p = r;
                if (p != c) { for (int k = 0; k < M; ++k) std::swap(a(c, k), a(p, k)); std::swap(x[(size_t) c], x[(size_t) p]); }
                for (int r = c + 1; r < M; ++r) { const double f = a(r, c) / a(c, c); if (f == 0) continue; for (int k = c; k < M; ++k) a(r, k) -= f * a(c, k); x[(size_t) r] -= f * x[(size_t) c]; }
            }
            for (int c = M - 1; c >= 0; --c) { double s2 = x[(size_t) c]; for (int k = c + 1; k < M; ++k) s2 -= a(c, k) * x[(size_t) k]; x[(size_t) c] = s2 / a(c, c); }
            return x;
        };
        n = (int) caps.size();
        std::vector<std::vector<double>> cols;
        for (int k = 0; k <= n; ++k) {
            std::vector<double> b((size_t) M, 0.0);
            if (k < n) { const int a = id((*caps[(size_t) k])[1]), c = id((*caps[(size_t) k])[2]); if (a >= 0) b[(size_t) a] += 1; if (c >= 0) b[(size_t) c] -= 1; } else b[(size_t) N] = 1;
            cols.push_back(solve(b));
        }
        auto vcap = [&](const std::vector<double>& x, int k) { const int a = id((*caps[(size_t) k])[1]), c = id((*caps[(size_t) k])[2]); return (a >= 0 ? x[(size_t) a] : 0.0) - (c >= 0 ? x[(size_t) c] : 0.0); };
        A.assign((size_t) (n * n), 0.0); B.assign((size_t) n, 0.0); C.assign((size_t) n, 0.0);
        const int out = id("out");
        for (int j = 0; j <= n; ++j) for (int k = 0; k < n; ++k) { const double v = 2 * gc[(size_t) k] * vcap(cols[(size_t) j], k) - (j == k ? 1 : 0); if (j < n) A[(size_t) (k * n + j)] = v; else B[(size_t) k] = v; }
        for (int j = 0; j < n; ++j) C[(size_t) j] = cols[(size_t) j][(size_t) out];
        D = cols[(size_t) n][(size_t) out];
        s.assign((size_t) n, 0.0); t.assign((size_t) n, 0.0);
    }
    std::vector<double> A, B, C, s, t; double D = 0;
};

/* ---------------- the SSM2044 and its control circuit ---------------- */
class Envelope {
public:
    Envelope(double fs = 192000, double capScale = 1) : h(1 / fs), Cc(10e-6 * capScale) {
        gsum = 1 / 15e3 + 1 / 390.0 + 1 / 39e3 + 1 / 220e3;
        const long n = (long) std::ceil(1.5 / h); for (long i = 0; i < n; ++i) step();
        v13rest = pin13();
    }
    double gain = 5;
    double pin13() const { return (v / 15e3 - 15 / 39e3) / gsum; }
    double step() {
        const double v13 = pin13();
        const double iR = (15 - v) / 30e3 - (v - v13) / 15e3, id = diode(v - gain), dv = 1e-4, gd = (diode(v + dv - gain) - id) / dv;
        const double g = 1 / 30e3 + (1 / 15e3) * (1 - (1 / 15e3) / gsum) + gd;
        v = v + h * (iR - id) / (Cc + h * g);
        return v;
    }
    double octaves() const { return (v13rest - pin13()) / (VT * std::log(2.0)); }
private:
    double diode(double vd) const {
        if (vd <= 0) return 0;
        double i = std::max(1e-12, (vd - .6) / 100);
        for (int k = 0; k < 20; ++k) {
            const double f = i * 100 + nVt * std::log(i / Is + 1) - vd, d = 100 + nVt / (i + Is), ni = std::max(1e-15, i - f / d);
            if (std::fabs(ni - i) < 1e-12) { i = ni; break; }
            i = ni;
        }
        return i;
    }
    double h, Cc, v = 0, gsum, v13rest = 0, Is = 2.52e-9, nVt = 1.752 * VT;
};
class SSM2044 {
public:
    SSM2044(double fs = 192000, double res = .3) : h(1 / fs), k(res) {}
    double process(double x, double fc) {
        const double w = 2 * PI_ * fc, tv = 2 * VT; double inp = x - k * v[3];
        for (int s = 0; s < 4; ++s) {
            const double d = inp - v[(size_t) s], a = d / tv, ge = std::fabs(a) < 1e-6 ? 1 : std::tanh(a) / a;
            const double hw = h * w * ratio[(size_t) s] * ge;
            v[(size_t) s] = (v[(size_t) s] + hw * inp) / (1 + hw);
            inp = v[(size_t) s];
        }
        return v[3];
    }
    void reset() { v = {0, 0, 0, 0}; }
private:
    double h, k; std::array<double, 4> v {0, 0, 0, 0}; std::array<double, 4> ratio {1, 1, 1, .01e-6 / 820e-12};
};

/* ---------------- resampling ---------------- */
static const int OS = 4;
inline std::vector<double> lowpassFir(int taps, double cut) {
    std::vector<double> h((size_t) taps); const double m = (taps - 1) / 2.0; double s = 0;
    for (int i = 0; i < taps; ++i) { const double x = i - m, w = .42 - .5 * std::cos(2 * PI_ * i / (taps - 1)) + .08 * std::cos(4 * PI_ * i / (taps - 1)); h[(size_t) i] = (x != 0 ? std::sin(2 * PI_ * cut * x) / (PI_ * x) : 2 * cut) * w; s += h[(size_t) i]; }
    for (auto& v : h) v /= s;
    return h;
}
inline const std::vector<double>& decimFir() { static const std::vector<double> h = lowpassFir(127, .45 / OS); return h; }
inline std::vector<float> decimate(const std::vector<double>& y, size_t n) {
    const auto& h = decimFir(); std::vector<float> out(n); const long m = ((long) h.size() - 1) / 2;
    for (size_t i = 0; i < n; ++i) { const long c = (long) i * OS; double s = 0; for (size_t k = 0; k < h.size(); ++k) { const long j = c + (long) k - m; if (j >= 0 && j < (long) y.size()) s += h[k] * y[(size_t) j]; } out[i] = (float) s; }
    return out;
}

/* ---------------- calibrations (see sp1200.js) ---------------- */
struct Calib { double aaCap; double fixedCap[4]; double rest, res, dynCap, gain12; };
inline const Calib& calib(bool measured) {
    static const Calib manual {1, {1, 1, 1, 1}, 1000, .3, 1, 1}, meas {.905, {.925, .94, .92, .91}, 925, .3, .85, 1};
    return measured ? meas : manual;
}

/* ---------------- the machine ---------------- */
struct SampleOpts { double clock = CLOCK, level = 1, bits = 12, adcOffset = .5; bool measured = false; };
inline std::vector<uint16_t> sampleCodes(const std::vector<float>& src, double sr, const SampleOpts& o) {
    const double fo = sr * OS, foA = std::max(fo, CLOCK * 4);
    Circuit aa(net("aa"), foA, calib(o.measured).aaCap);
    const double aaDc = aa.dc(), gain = o.level / aaDc;
    const size_t n = src.size(); const double dur = n / sr;
    const size_t frames = std::min<size_t>(1u << 20, (size_t) std::floor(dur * o.clock));
    std::vector<uint16_t> codes(frames + 10, 0x800);
    const int drop = 12 - std::max(1, std::min(12, (int) o.bits));
    double prev = 0, tNext = 0; size_t k = 0;
    const long steps = (long) std::ceil(dur * foA);
    auto at = [&](long i) { return i >= 0 && i < (long) n ? (double) src[(size_t) i] : 0.0; };
    for (long i = 0; i < steps && k < frames; ++i) {
        const double t = i / foA, x = t * sr; const long j = (long) std::floor(x); const double f = x - j;
        const double y = aa.process((at(j) * (1 - f) + at(j + 1) * f) * gain);
        while (k < frames && tNext <= t) {
            const double a = 1 - (t - tNext) * foA, held = prev + (y - prev) * std::max(0.0, std::min(1.0, a));
            int c = (int) std::floor(held * 2048 + 2048 + o.adcOffset - .5);
            c = std::max(0, std::min(4095, c));
            if (drop) c = (c >> drop) << drop;
            codes[5 + k] = (uint16_t) c; k++; tNext = k / o.clock;
        }
        prev = y;
    }
    codes.resize(5 + k + 5);
    return codes;
}
// the last few samplings, keyed by the source buffer and the settings that matter
inline std::vector<uint16_t> codesFor(const std::vector<float>& src, double sr, const SampleOpts& o, double rpm) {
    struct Entry { const float* data; size_t len; double key[7]; std::shared_ptr<std::vector<uint16_t>> codes; };
    static std::mutex mu; static std::vector<Entry> cache;
    const double key[7] = { sr, o.clock, o.level, o.bits, rpm, o.adcOffset, o.measured ? 1.0 : 0.0 };
    {
        std::lock_guard<std::mutex> l(mu);
        for (auto& e : cache) if (e.data == src.data() && e.len == src.size() && std::memcmp(e.key, key, sizeof key) == 0) return *e.codes;
    }
    auto c = std::make_shared<std::vector<uint16_t>>(sampleCodes(src, rpm != 0 ? sr * std::pow(2.0, rpm / 12) : sr, o));
    std::lock_guard<std::mutex> l(mu);
    cache.insert(cache.begin(), Entry{ src.data(), src.size(), { key[0], key[1], key[2], key[3], key[4], key[5], key[6] }, c });
    if (cache.size() > 4) cache.pop_back();
    return *c;
}
inline int spStep(double cents, bool sp12) {
    const double st = cents / 100;
    if (std::fabs(st - std::round(st)) < 1e-6 && st >= -16 && st <= 15) { const int b = TABLE[(int) std::round(st) + 16]; return sp12 ? ((b | 1) + 1) : b + 1; }
    return (int) std::max(1.0, std::min(1024.0, std::round(128 * std::pow(2.0, cents / 1200))));
}
struct PlayOpts { double clock = CLOCK, cents = 0, rest = 0, res = -1, depth = 1, trigger = .0094, velocity = 1, decay = 1; bool sp12 = false, retrigger = false, measured = true; int output = 7, channel = 0; };
inline std::vector<long> onsetsOf(const std::vector<double>& y, double fo) {
    std::vector<long> out; double fe = 0, ss = 0; long last = -1000000000L;
    for (size_t i = 0; i < y.size(); ++i) {
        const double a = std::fabs(y[i]); fe = a > fe ? a : fe * (1 - 30 / fo); ss += (a - ss) * (2 / fo);
        if (fe > .05 && fe > ss * 4 && (long) i - last > .08 * fo) { if (i) out.push_back((long) i); last = (long) i; }
    }
    return out;
}
inline std::vector<float> playChannel(const std::vector<uint16_t>& codes, const PlayOpts& o, double fs) {
    const double fo = fs * OS; const int step = spStep(o.cents, o.sp12);
    const long last = (long) codes.size() - 5, frames = (long) std::floor((double) (last - 5) * 128 / step) + 5;
    const size_t len = (size_t) std::ceil((frames / o.clock + .02) * fo);
    std::vector<double> y(len);
    const double level = std::max(0.0, std::min(255.0, std::round(o.velocity * 255))) / 255;
    const double tick = 1.2288e-3; const int decayTicks = o.decay >= 1 ? 0 : std::max(1, (int) std::lround(o.decay * 400));
    const Calib& cal = calib(o.measured); const double slot = o.channel * SLOT;
    double held = 0, tk = slot, lvl = level, nextTick = tick; long k = 0, ticks = 0;
    auto val = [&](long idx) { return idx < last ? (codes[(size_t) idx] - 2048) / 2048.0 : 0.0; };
    for (size_t i = 0; i < len; ++i) {
        const double t0 = i / fo, t1 = (i + 1) / fo; double acc = 0, tp = t0;
        while (tk < t1) {
            acc += held * (tk - tp); tp = tk;
            if (decayTicks) while (nextTick <= tk) { nextTick += tick; if (++ticks % decayTicks == 0) lvl = std::max(0.0, lvl - 1 / 255.0); }
            held = k < frames ? val(5 + ((k * step) >> 7)) * lvl : 0;
            k++; tk = slot + k / o.clock;
        }
        acc += held * (t1 - tp); y[i] = acc * fo;
    }
    const int out = o.output;
    if (out >= 3 && out <= 6) { Circuit c(net("out" + std::to_string(out)), fo, cal.fixedCap[out - 3]); for (auto& v : y) v = c.process(v); }
    else if (out <= 2) {
        Envelope env(fo, cal.dynCap); SSM2044 ssm(fo, o.res < 0 ? cal.res : o.res); Circuit io(net("dynout"), fo);
        const double rest = o.rest > 0 ? o.rest : cal.rest; const long trig = std::lround(o.trigger * fo);
        const double pin1 = 5 * 200 / (33e3 + 200), ioNorm = -3.3e3 / 15e3 * cal.gain12;
        const auto onsets = o.retrigger ? onsetsOf(y, fo) : std::vector<long>();
        size_t next = 0; long tEnd = trig;
        for (size_t i = 0; i < len; ++i) {
            if (next < onsets.size() && onsets[next] == (long) i) { tEnd = (long) i + trig; next++; }
            env.gain = (long) i < tEnd ? 0 : 5; env.step();
            const double fc = std::min(rest * std::pow(2.0, o.depth * env.octaves()), fo * .45);
            const double s = ssm.process(y[i] * pin1, fc) / pin1;
            y[i] = io.process(s * ioNorm);
        }
    }
    return decimate(y, (size_t) std::ceil(len / (double) OS));
}

/* ---------------- live input through the same path ---------------- */
// Upsample to 4x, the anti-alias circuit, sample-and-hold and the converter at the SP clock (with the 45 rpm trick:
// sampled slower by the playback step and played out at the clock, repeating samples, as the hardware does), the DAC
// staircase in its channel slot, the output circuit, back down to the host rate. Hits retrigger the SSM2044.
class SPLive {
public:
    SPLive(double fs = 48000) : fs(fs), fo(fs * OS) {}
    struct Settings { double clock = CLOCK, level = 1, bits = 12, rpm = 0, rest = 0, res = -1, depth = 1; int output = 7; bool measured = true, sp12 = false; };
    void setup(const Settings& s) {
        const bool rebuild = !built || s.output != st.output || s.measured != st.measured || s.clock != st.clock;
        st = s;
        if (rebuild) {
            const Calib& cal = calib(s.measured);
            aa = Circuit(net("aa"), fo, cal.aaCap); aaDc = aa.dc();
            if (s.output >= 3 && s.output <= 6) outC = Circuit(net("out" + std::to_string(s.output)), fo, cal.fixedCap[s.output - 3]);
            env = std::make_unique<Envelope>(fo, cal.dynCap); ssm = SSM2044(fo, s.res < 0 ? cal.res : s.res); io = Circuit(net("dynout"), fo);
            up.assign(decimFir().size(), 0.0); down.assign(decimFir().size(), 0.0); ring.assign(1 << 12, 0x800);
            built = true;
        }
        step = spStep(-s.rpm * 100, s.sp12);
        adcRate = s.clock * step / 128.0;
        if (rebuild || s.res != lastRes) { ssm = SSM2044(fo, s.res < 0 ? calib(s.measured).res : s.res); lastRes = s.res; }
    }
    int latency() const { return (int) (decimFir().size() / OS) + 2; }
    void trigger() { tEnd = (long) (.0094 * fo); }
    float process(float x) {
        const auto& h = decimFir(); const int taps = (int) h.size();
        double out = 0;
        for (int p = 0; p < OS; ++p) {
            // polyphase interpolation: zero-stuffed input through the lowpass, gain OS
            pushRing(up, upPos, p == 0 ? x * OS : 0.0);
            double u = 0; for (int k = 0; k < taps; ++k) u += h[(size_t) k] * up[(size_t) ((upPos - 1 - k + taps * 2) % taps)];
            const double y = runSample(u);
            pushRing(down, downPos, y);
            if (p == OS - 1) { for (int k = 0; k < taps; ++k) out += h[(size_t) k] * down[(size_t) ((downPos - 1 - k + taps * 2) % taps)]; }
        }
        return (float) out;
    }
private:
    static void pushRing(std::vector<double>& r, int& pos, double v) { r[(size_t) pos] = v; pos = (pos + 1) % (int) r.size(); }
    double runSample(double u) {
        const double tStep = 1 / fo, a = aa.process(u * st.level / aaDc);
        // converter: samples at adcRate into a ring of codes
        tA += tStep;
        while (tA >= 1 / adcRate) {
            tA -= 1 / adcRate; const double fr = 1 - tA * fo, held = aPrev + (a - aPrev) * std::max(0.0, std::min(1.0, fr));
            int c = (int) std::floor(held * 2048 + 2048); c = std::max(0, std::min(4095, c));
            const int drop = 12 - std::max(1, std::min(12, (int) st.bits)); if (drop) c = (c >> drop) << drop;
            ring[(size_t) (w++ & 4095)] = (uint16_t) c;
        }
        aPrev = a;
        // DAC: frames at the clock, reading the codes through the 7-bit counter
        tD += tStep; double acc = 0, tp = 0;
        while (tD >= 1 / st.clock) {
            tD -= 1 / st.clock; const double at = tStep - tD; acc += dac * (at - tp); tp = at;
            frac += step; const long adv = frac >> 7; frac &= 127; rd += adv;
            if (rd > w - 2) rd = w - 2;
            if (rd < w - 4000) rd = w - 4000;
            dac = (ring[(size_t) (rd & 4095)] - 2048) / 2048.0;
        }
        acc += dac * (tStep - tp);
        double y = acc * fo;
        // output
        if (st.output >= 3 && st.output <= 6) y = outC.process(y);
        else if (st.output <= 2) {
            const double ay = std::fabs(y); fe = ay > fe ? ay : fe * (1 - 30 / fo); ss += (ay - ss) * (2 / fo); since++;
            if (fe > .05 && fe > ss * 4 && since > .08 * fo) { since = 0; tEnd = (long) (.0094 * fo); }
            env->gain = tEnd > 0 ? 0 : 5; if (tEnd > 0) tEnd--; env->step();
            const double rest = st.rest > 0 ? st.rest : calib(st.measured).rest, fc = std::min(rest * std::pow(2.0, st.depth * env->octaves()), fo * .45);
            const double pin1 = 5 * 200 / (33e3 + 200);
            y = io.process(ssm.process(y * pin1, fc) / pin1 * (-3.3e3 / 15e3));
        }
        return y;
    }
    double lastRes = -2;
    double fs, fo, aaDc = 1, adcRate = CLOCK, tA = 0, tD = 0, aPrev = 0, dac = 0, fe = 0, ss = 0;
    long w = 8, rd = 0, frac = 0, since = 1000000, tEnd = 0; int step = 128, upPos = 0, downPos = 0; bool built = false;
    Settings st;
    Circuit aa, outC, io; std::unique_ptr<Envelope> env; SSM2044 ssm;
    std::vector<double> up, down; std::vector<uint16_t> ring;
};

}}  // namespace twelve::sp
