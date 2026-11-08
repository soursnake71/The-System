// Omega SID 64: a digital reverb in the style of the Yamaha REV7 (1984).
//
// Not a measured copy of the unit: a reverb built to its outline. It runs at the REV7's internal rate of 31.25 kHz
// whatever the host runs at (band-limited to about 12 kHz on the way in and out, as the unit was), takes a mono
// input and gives a stereo output, and has the REV7's reverb family of programs and its parameters:
// reverb time, high ratio (how much faster the highs decay), initial delay, room size, ER / reverb balance,
// diffusion, density, a low-pass and a high-pass filter, and the gate length for the gate programs.
//
// Inside: a pre-delay line, early reflections from a multi-tap delay (a pattern per program, scaled by the room
// size, split left / right), input diffusion (all-passes, set by diffusion and density) into an eight-line feedback
// delay network with a Hadamard mix and a one-pole damping filter per line set so that the lows die away in the
// reverb time and the highs in reverb time x high ratio. The gate programs are early-reflection bursts: dense
// taps spread over the gate length at a flat level (gate) or rising (reverse), then nothing.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sid {

class Rev7 {
public:
    static constexpr double CORE_FS = 31250.0;
    enum Program { HALL, ROOM, VOCAL, PLATE, ER1, ER2, GATE, REVERSE };

    struct Settings {
        int program = HALL;
        double time = 2.2;        // s (RT60 of the lows)
        double highRatio = 0.6;   // 0.1..1: the highs decay in time x highRatio
        double delayMs = 20;      // initial delay
        double size = 2.0;        // room size for the early reflections (and a little on the tank)
        double balance = 0.5;     // 0 early reflections only .. 1 reverb only
        int diffusion = 8;        // 0..10
        int density = 3;          // 0..4
        double lpfHz = 10000, hpfHz = 20;
        double gateMs = 120;      // gate / reverse length
    };

    void prepare(double hostFs) {
        fs = hostFs; ratio = CORE_FS / fs;
        pre.assign(8192, 0.f); er.assign(32768, 0.f);
        for (size_t i = 0; i < 8; ++i) line[i].assign(4096, 0.f);
        for (size_t i = 0; i < 4; ++i) ap[i].assign(1024, 0.f);
        setBiquadLP(inLP[0], 11000.0 / fs); setBiquadLP(inLP[1], 11000.0 / fs);
        setBiquadLP(outLP[0][0], 12000.0 / fs); setBiquadLP(outLP[0][1], 12000.0 / fs);
        setBiquadLP(outLP[1][0], 12000.0 / fs); setBiquadLP(outLP[1][1], 12000.0 / fs);
        outLPon = fs > 26000;
        reset();
        apply(cur, true);
    }
    void reset() {
        std::fill(pre.begin(), pre.end(), 0.f); std::fill(er.begin(), er.end(), 0.f);
        for (auto& l : line) std::fill(l.begin(), l.end(), 0.f);
        for (auto& a : ap) std::fill(a.begin(), a.end(), 0.f);
        for (auto& b : inLP) b.z1 = b.z2 = 0;
        for (auto& c : outLP) for (auto& b : c) b.z1 = b.z2 = 0;
        for (auto& d : damp) d = 0;
        hist.fill({ 0, 0 }); acc = 0; xPrev = xCur = 0; lpState[0] = lpState[1] = hpState[0] = hpState[1] = hpIn[0] = hpIn[1] = 0;
    }
    // call before process (any thread that owns the audio); cheap when nothing changed
    void apply(const Settings& s, bool force = false) {
        auto ne = [](double a, double b) { return std::fabs(a - b) > 1e-9; };
        const bool changed = force || s.program != cur.program || ne(s.time, cur.time) || ne(s.highRatio, cur.highRatio) || ne(s.size, cur.size)
                          || s.diffusion != cur.diffusion || s.density != cur.density || ne(s.gateMs, cur.gateMs) || ne(s.delayMs, cur.delayMs)
                          || ne(s.lpfHz, cur.lpfHz) || ne(s.hpfHz, cur.hpfHz) || ne(s.balance, cur.balance);
        cur = s;
        if (!changed) return;
        // the tank: line lengths (ms) per program, gains for the lows and the highs from the reverb time
        static const double base[8] = { 29.7, 37.1, 41.1, 43.7, 53.0, 59.3, 67.9, 73.1 };
        const double scale = cur.program == HALL ? 1.0 : cur.program == ROOM ? 0.55 : cur.program == VOCAL ? 0.8 : cur.program == PLATE ? 0.42 : 0.7;
        const double sz = std::clamp(0.75 + 0.125 * cur.size, 0.8, 1.6);
        for (int i = 0; i < 8; ++i) {
            len[i] = std::clamp((int) std::lround(base[i] * scale * (cur.program <= PLATE ? sz : 1.0) * CORE_FS / 1000.0), 64, 4000);
            const double gLow = std::pow(10.0, -3.0 * len[i] / (std::max(0.1, cur.time) * CORE_FS));
            const double gHigh = std::pow(10.0, -3.0 * len[i] / (std::max(0.05, cur.time * std::clamp(cur.highRatio, 0.1, 1.0)) * CORE_FS));
            const double r = gHigh / gLow;
            dampA[i] = (1 - r) / (1 + r); gainL[i] = gLow;
        }
        // input diffusion: density sets how many all-passes, diffusion how strongly
        static const int apLen[4] = { 142, 107, 379, 277 };
        nAp = std::clamp(cur.density, 0, 4);
        apG = 0.75 * std::clamp(cur.diffusion, 0, 10) / 10.0;
        for (int i = 0; i < 4; ++i) apN[i] = (int) (apLen[i] * (cur.program == PLATE ? 0.7 : 1.0));
        preN = std::clamp((int) std::lround(cur.delayMs / 1000.0 * CORE_FS), 1, (int) pre.size() - 2);
        buildTaps();
        lpK = 1 - std::exp(-2 * 3.14159265 * std::min(cur.lpfHz, 14000.0) / CORE_FS);
        hpK = std::exp(-2 * 3.14159265 * std::max(cur.hpfHz, 10.0) / CORE_FS);
    }

    // one host sample in (mono), one stereo sample out (wet only)
    void process(float in, float& outL, float& outR) {
        const float x = (float) biquad(inLP[1], biquad(inLP[0], in));
        xPrev = xCur; xCur = x;
        acc += ratio;
        while (acc >= 1.0) {
            acc -= 1.0;
            const double f = 1.0 - acc / ratio;               // where the core sample falls between the host samples
            const float xi = (float) (xPrev + (xCur - xPrev) * std::clamp(f, 0.0, 1.0));
            float l, r; core(xi, l, r);
            for (int k = 0; k < 3; ++k) hist[(size_t) k] = hist[(size_t) k + 1];
            hist[3] = { l, r };
        }
        // back to the host rate: Hermite between the two middle core samples (one core sample of latency)
        const float t = (float) acc;
        float o[2];
        for (int c = 0; c < 2; ++c) {
            const float y0 = hist[0][(size_t) c], y1 = hist[1][(size_t) c], y2 = hist[2][(size_t) c], y3 = hist[3][(size_t) c];
            const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2 * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            o[c] = ((c3 * t + c2) * t + c1) * t + c0;
            if (outLPon) o[c] = (float) biquad(outLP[c][1], biquad(outLP[c][0], o[c]));
        }
        outL = o[0]; outR = o[1];
    }

private:
    struct BQ { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
    static void setBiquadLP(BQ& q, double fc) {   // Butterworth, fc relative to the sample rate
        fc = std::min(fc, 0.45);
        const double w = std::tan(3.14159265358979 * fc), n = 1 / (1 + std::sqrt(2.0) * w + w * w);
        q.b0 = w * w * n; q.b1 = 2 * q.b0; q.b2 = q.b0; q.a1 = 2 * (w * w - 1) * n; q.a2 = (1 - std::sqrt(2.0) * w + w * w) * n;
    }
    static double biquad(BQ& q, double x) { const double y = q.b0 * x + q.z1; q.z1 = q.b1 * x - q.a1 * y + q.z2; q.z2 = q.b2 * x - q.a2 * y; return y; }

    struct Tap { int d; float gl, gr; };
    std::vector<Tap> taps;
    // early reflection patterns: (ms, gain, pan -1..1), scaled by the room size; gate / reverse spread over the length
    void buildTaps() {
        taps.clear();
        const double s = std::clamp(cur.size, 0.5, 10.0);
        auto add = [&](double ms, double g, double pan) {
            const int d = std::clamp((int) std::lround(ms / 1000.0 * CORE_FS), 1, (int) er.size() - 2);
            const double a = (pan + 1) * 0.25 * 3.14159265;
            taps.push_back({ d, (float) (g * std::cos(a)), (float) (g * std::sin(a)) });
        };
        uint32_t seed = 0x5EED1u + (uint32_t) cur.program * 977u;
        auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed & 0xFFFF) / 65535.0; };
        switch (cur.program) {
            case GATE: case REVERSE: {
                const double L = std::clamp(cur.gateMs, 10.0, 400.0);
                const int n = 28;
                for (int i = 0; i < n; ++i) {
                    const double pos = (i + 0.5 * rnd()) / n;
                    const double g = cur.program == GATE ? 0.55 + 0.15 * rnd() : 0.15 + 0.85 * pos;
                    add(pos * L, g * 0.42, rnd() * 2 - 1);
                }
                break;
            }
            case ER2:   // a random, denser pattern
                for (int i = 0; i < 18; ++i) add((2 + 38 * rnd()) * s, (0.9 - 0.04 * i) * 0.35, rnd() * 2 - 1);
                break;
            default: {  // a hall-like pattern: early, sparse, falling (also used by the reverb programs)
                static const double ms[12] = { 4.3, 7.9, 11.2, 13.6, 17.5, 21.3, 24.9, 29.1, 33.8, 37.2, 42.6, 47.9 };
                static const double pan[12] = { -0.8, 0.7, -0.3, 0.9, -0.9, 0.4, -0.5, 0.8, -0.1, 0.6, -0.7, 0.2 };
                for (int i = 0; i < 12; ++i) add(ms[i] * s, (1.0 - 0.06 * i) * 0.32, pan[i]);
                break;
            }
        }
    }

    void core(float x, float& L, float& R) {
        // pre-delay
        pre[(size_t) preW] = x;
        int rp = preW - preN; if (rp < 0) rp += (int) pre.size();
        const float d = pre[(size_t) rp];
        if (++preW >= (int) pre.size()) preW = 0;
        // early reflections
        er[(size_t) erW] = d;
        float el = 0, eR = 0;
        for (const Tap& t : taps) { int k = erW - t.d; if (k < 0) k += (int) er.size(); const float v = er[(size_t) k]; el += v * t.gl; eR += v * t.gr; }
        if (++erW >= (int) er.size()) erW = 0;
        float ll = 0, lr = 0;
        const bool gated = cur.program == GATE || cur.program == REVERSE;
        const bool erOnly = cur.program == ER1 || cur.program == ER2;
        if (!gated && !erOnly) {
            // diffusion, then the tank
            float u = d;
            for (int i = 0; i < nAp; ++i) {
                auto& b = ap[(size_t) i]; int& w = apW[i];
                int k = w - apN[i]; if (k < 0) k += (int) b.size();
                const float dl = b[(size_t) k];
                const float v = u + (float) apG * dl;
                b[(size_t) w] = v; u = dl - (float) apG * v;
                if (++w >= (int) b.size()) w = 0;
            }
            float o[8];
            for (int i = 0; i < 8; ++i) { int k = lw[i] - len[i]; if (k < 0) k += (int) line[(size_t) i].size(); o[i] = line[(size_t) i][(size_t) k]; }
            // Hadamard 8 (normalised)
            float h[8]; for (int i = 0; i < 8; ++i) h[i] = o[i];
            for (int s = 1; s < 8; s <<= 1) for (int i = 0; i < 8; i += 2 * s) for (int j = i; j < i + s; ++j) { const float a = h[j], b = h[j + s]; h[j] = a + b; h[j + s] = a - b; }
            const float norm = 0.35355339f;
            for (int i = 0; i < 8; ++i) {
                float v = h[i] * norm;
                damp[i] = (float) ((1 - dampA[i]) * v + dampA[i] * damp[i]);
                v = (float) (damp[i] * gainL[i]) + u * (i & 1 ? -0.5f : 0.5f);
                line[(size_t) i][(size_t) lw[i]] = v;
                if (++lw[i] >= (int) line[(size_t) i].size()) lw[i] = 0;
            }
            ll = (o[0] + o[2] + o[4] + o[6]) * 0.45f; lr = (o[1] + o[3] + o[5] + o[7]) * 0.45f;
        }
        const float bal = (float) std::clamp(cur.balance, 0.0, 1.0);
        float yl, yr;
        if (gated || erOnly) { yl = el; yr = eR; }
        else { yl = el * (1 - bal) * 1.2f + ll * bal * 1.6f; yr = eR * (1 - bal) * 1.2f + lr * bal * 1.6f; }
        // the output filters
        float y[2] = { yl, yr };
        for (int c = 0; c < 2; ++c) {
            lpState[c] += (float) lpK * (y[c] - lpState[c]);
            const float lp = lpState[c];
            hpState[c] = (float) hpK * (hpState[c] + lp - hpIn[c]); hpIn[c] = lp;
            y[c] = hpState[c];
        }
        L = y[0]; R = y[1];
    }

    double fs = 48000, ratio = 31250.0 / 48000, acc = 0;
    Settings cur;
    std::vector<float> pre, er; int preW = 0, preN = 600, erW = 0;
    std::array<std::vector<float>, 8> line; int lw[8] {}, len[8] {}; double dampA[8] {}, gainL[8] {}; float damp[8] {};
    std::array<std::vector<float>, 4> ap; int apW[4] {}, apN[4] {}, nAp = 3; double apG = 0.6;
    BQ inLP[2], outLP[2][2]; bool outLPon = true;
    std::array<std::array<float, 2>, 4> hist {};
    float xPrev = 0, xCur = 0;
    double lpK = 0.8, hpK = 0.996;
    float lpState[2] {}, hpState[2] {}, hpIn[2] {};
};

}  // namespace sid
