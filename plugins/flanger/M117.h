// MXR M-117 Flanger, modeled block by block from its circuit: input buffer, anti-alias filter, a 1024-stage
// bucket-brigade delay run by a voltage-controlled clock, a triangle LFO (SPEED) whose depth (WIDTH) rides on a
// DC offset (MANUAL), the reconstruction filter, the REGEN loop from the filtered BBD output back into the BBD
// input, and the equal dry / delayed mixer that makes the notches.
//
// The BBD is simulated as a real clocked device, not as a digital delay line: every clock tick samples the
// filtered input into the first bucket and moves the oldest bucket out, at whatever clock rate the VCO is at
// (42 kHz .. 2 MHz, many ticks per host sample). The output is the BBD's staircase averaged over each host sample
// period, so a slow clock darkens and aliases like the hardware does. Charge-transfer loss, bucket clipping
// and the BBD's hiss are in the loop, which is what keeps full REGEN screaming without blowing up.
//
// Plain C++17, no JUCE, doubles throughout. flanger/engine.js is a line-for-line port of this file and
// flanger/tools/compare.mjs checks the two render the same samples.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace m117 {

// parameter order shared with engine.js, the presets and the plugin
enum Param { manual, width, speed, regen, bypass, mix, spread, level, kNumParams };

struct ParamInfo { const char* id; const char* name; double min, max, def; const char* unit; };
static const ParamInfo kParams[kNumParams] = {
    {"manual", "Manual", 0, 10, 5, ""},
    {"width", "Width", 0, 10, 5, ""},
    {"speed", "Speed", 0, 10, 3, ""},
    {"regen", "Regen", 0, 10, 5, ""},
    {"bypass", "Bypass", 0, 1, 0, ""},
    {"mix", "Mix", 0, 100, 50, "%"},
    {"spread", "Stereo", 0, 180, 0, "deg"},
    {"level", "Level", -24, 12, 0, "dB"},
};

// circuit constants
static constexpr int kStages = 1024;            // BBD stages; a sample moves one bucket per two clock phases
static constexpr int kCells = kStages / 2;      // so the delay is kStages / (2 * fclk) = kCells / fclk
static constexpr double kClockHi = 2048000.0;   // VCO top: 0.25 ms
static constexpr double kClockLo = 42667.0;     // VCO bottom: 12 ms
static constexpr double kLfoLo = 0.06, kLfoHi = 6.0;   // SPEED: one sweep in ~16 s .. 6 per second
static constexpr double kAntiAlias = 7500.0;    // pre and post 2-pole filters (Butterworth)
static constexpr double kPostPole = 12000.0;    // extra pole after the reconstruction filter
static constexpr double kRegenMax = 0.95;       // feedback at REGEN 10
static constexpr double kCti = 0.035;           // charge-transfer loss per clock tick
static constexpr double kBucketClip = 1.4;      // where the buckets saturate (full scale = 1)
static constexpr double kHiss = 0.00006;        // BBD noise, about -84 dBFS
static constexpr double kCoupling = 16.0;       // input coupling capacitor high-pass, Hz

// soft limit with a hard ceiling at +-lim (rational tanh)
inline double sat(double x, double lim) {
    double u = x / lim;
    if (u > 3) u = 3; else if (u < -3) u = -3;
    return lim * u * (27 + u * u) / (27 + 9 * u * u);
}

// TPT state-variable low-pass (Zavalishin)
struct LP2 {
    double a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set(double fc, double q, double fs) {
        const double g = std::tan(3.141592653589793 * fc / fs), k = 1 / q;
        a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    double run(double x) {
        const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        return v2;
    }
};

struct LP1 {
    double G = 0, s = 0;
    void set(double fc, double fs) { const double g = std::tan(3.141592653589793 * fc / fs); G = g / (1 + g); }
    void reset() { s = 0; }
    double run(double x) { const double v = (x - s) * G, y = v + s; s = y + v; return y; }
};

// one channel of the circuit: everything after the LFO
struct Channel {
    std::vector<double> cell = std::vector<double>(kCells, 0.0);
    int head = 0;
    double ph = 0;          // clock phase carried between host samples (0..1 of a tick)
    double hold = 0;        // the BBD output, held between ticks
    double cti = 0;         // charge-transfer loss state
    double xPrev = 0;       // BBD input at the previous host sample (ticks interpolate between samples)
    double wet = 0;         // filtered BBD output from the previous sample (feeds REGEN)
    LP1 dc, post1;
    LP2 pre, post;
    void reset() {
        for (auto& c : cell) c = 0;
        head = 0; ph = 0; hold = 0; cti = 0; xPrev = 0; wet = 0;
        dc.reset(); post1.reset(); pre.reset(); post.reset();
    }
};

class Engine {
public:
    double fs = 48000;
    double p[kNumParams];

    Engine() { for (int i = 0; i < kNumParams; ++i) p[i] = kParams[i].def; }

    void prepare(double sampleRate) {
        fs = sampleRate;
        const double nyq = fs * 0.45;
        for (auto& c : ch) {
            c.dc.set(kCoupling, fs);
            c.pre.set(kAntiAlias < nyq ? kAntiAlias : nyq, 0.7071067811865476, fs);
            c.post.set(kAntiAlias < nyq ? kAntiAlias : nyq, 0.7071067811865476, fs);
            c.post1.set(kPostPole < nyq ? kPostPole : nyq, fs);
        }
        smooth = 1 - std::exp(-1 / (0.025 * fs));   // pots and the bypass fade: ~25 ms
        reset();
    }

    void reset() {
        for (auto& c : ch) c.reset();
        lfo = 0; rng = 0x9E3779B9u;
        sManual = p[manual] / 10; sWidth = p[width] / 10; sRegen = regenGain(); sMix = p[mix] / 100; sLevel = levelGain();
        sOn = p[bypass] >= 0.5 ? 0 : 1;
    }

    void set(int i, double v) { if (i >= 0 && i < kNumParams) p[i] = v; }

    double lfoHz() const { return kLfoLo * std::pow(kLfoHi / kLfoLo, p[speed] / 10); }
    // delay in seconds for a control voltage 0..1 (0 = shortest)
    static double clockFor(double cv) { return kClockHi * std::pow(kClockLo / kClockHi, cv); }

    // in/out may alias. Mono: pass the same pointers for both channels (inR may equal inL).
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        const double dl = lfoHz() / fs;
        const double tManual = p[manual] / 10, tWidth = p[width] / 10, tRegen = regenGain(), tMix = p[mix] / 100, tLevel = levelGain();
        const double tOn = p[bypass] >= 0.5 ? 0 : 1, off = p[spread] / 360;
        for (int i = 0; i < n; ++i) {
            sManual += (tManual - sManual) * smooth; sWidth += (tWidth - sWidth) * smooth; sRegen += (tRegen - sRegen) * smooth;
            sMix += (tMix - sMix) * smooth; sLevel += (tLevel - sLevel) * smooth; sOn += (tOn - sOn) * smooth;
            lfo += dl; if (lfo >= 1) lfo -= 1;
            const double xl = inL[i], xr = inR[i];
            const double yl = run(ch[0], xl, cvAt(lfo));
            const double yr = run(ch[1], xr, cvAt(lfo + off));
            const double gl = (xl * (1 - sMix) + yl * sMix) * sLevel, gr = (xr * (1 - sMix) + yr * sMix) * sLevel;
            outL[i] = (float) (xl + (gl - xl) * sOn);
            outR[i] = (float) (xr + (gr - xr) * sOn);
        }
    }

    double lfoPhase() const { return lfo; }

private:
    Channel ch[2];
    double lfo = 0, smooth = 0.001;
    double sManual = 0.5, sWidth = 0.5, sRegen = 0, sMix = 0.5, sLevel = 1, sOn = 1;
    uint32_t rng = 0x9E3779B9u;

    double regenGain() const { const double r = p[regen] / 10; return kRegenMax * r; }
    double levelGain() const { return std::pow(10.0, p[level] / 20); }

    // triangle LFO into the clock VCO's control input: MANUAL sets the offset, WIDTH the depth;
    // the VCO's control range runs out softly at both ends
    double cvAt(double phase) const {
        if (phase >= 1) phase -= 1;
        const double tri = 1 - 4 * std::fabs(phase - 0.5);    // -1 .. 1, starts at the bottom
        double cv = sManual + 0.5 * sWidth * tri;
        cv = 0.5 + 0.5 * sat(2 * cv - 1, 1.0);
        return cv;
    }

    double noise() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (rng / 4294967296.0) * 2 - 1;
    }

    double run(Channel& c, double x, double cv) {
        // input buffer (coupling cap) and the REGEN sum, into the anti-alias filter and the first bucket
        const double in = x - c.dc.run(x);
        const double bbdIn = c.pre.run(in + sRegen * c.wet);
        // clock ticks during this sample: each samples the input (interpolated to the tick's time) into the
        // first bucket and moves the oldest bucket to the output, which holds until the next tick
        const double ticks = clockFor(cv) / fs;
        double acc = 0, tPrev = 0, t = (1 - c.ph) / ticks;   // time of the next tick, in host samples
        while (t <= 1) {
            acc += c.hold * (t - tPrev);
            const double s = sat(c.xPrev + (bbdIn - c.xPrev) * t, kBucketClip);
            const double out = c.cell[(size_t) c.head];
            c.cell[(size_t) c.head] = s;
            c.head = c.head + 1 == kCells ? 0 : c.head + 1;
            c.cti += (out - c.cti) * (1 - kCti);
            c.hold = c.cti;
            tPrev = t;
            t += 1 / ticks;
        }
        acc += c.hold * (1 - tPrev);
        c.ph += ticks; c.ph -= std::floor(c.ph);
        c.xPrev = bbdIn;
        // reconstruction filter; its output is the wet signal and the REGEN tap
        const double y = c.post1.run(c.post.run(acc + kHiss * noise()));
        c.wet = y;
        return y;
    }
};

}  // namespace m117
