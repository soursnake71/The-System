// Omega CMI: a circuit-level model of the Fairlight CMI Series II channel cards (CMI-01A) and master card (CMI-02).
// Plain C++17, no framework: the VST3 plugin (plugins/cmi) and the Android app (android-cmi) both use these headers.
//
// What is modelled, from the CMI mainframe service manual as captured by MAME's cmi01a / cmi drivers:
//  - master card: the 34.291712 MHz master oscillator and the master tune divider ((0xF00 | tune) * MOSC/2 / 4096)
//  - per channel: a 10-bit pitch register and a 3-bit octave divider that set the channel's sample clock
//      f = ((0x800 | pitch << 1) * mf / 4096) / (2 << (7 ^ oct)) / 16
//    so every note plays the stored sound faster or slower (no interpolation: the pitch IS the sample rate)
//  - 16 KB of 8-bit waveform RAM, addressed as 128 pages of 128 samples (a 7-bit counter plus a page register):
//    loops and segment changes happen only on page boundaries
//  - an 8-bit DAC (zero-order hold at the channel clock; the steps are band-limited exactly, so what you hear is the
//    analogue staircase with its images, as the real card makes it)
//  - the SSM2045 dual two-pole low-pass, cutoff 6410 * 1.02162^((oct << 5) + latch - 256) Hz (the service manual's
//    curve), second section 1.2247x higher, both with the card's fixed damping. The octave register feeds the
//    filter DAC, so the filter tracks the keyboard an octave at a time, as on the real card
//  - the 8-bit envelope up/down counter, clocked through the card's 6-bit rate multiplier, whose rate depends on the
//    counter's own top four bits (fast at the bottom of an attack, slow at the top; fast at the top of a decay, slow
//    at the bottom: the CMI's piecewise-exponential envelopes)
//  - envelope loads gated by the zero-cross detector (ZX), so voices start and stop without clicks
//  - the two multiplying DACs (envelope x volume latch), 8 bits each
//  - the analogue output: coupling capacitor, the summing amplifier's soft limit, the noise floor
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace cmi {

constexpr double PI = 3.14159265358979323846;
constexpr double MOSC = 34291712.0;     // master oscillator (Hz)
constexpr int RAM_SIZE = 16384;         // bytes of waveform RAM per channel card
constexpr int PAGE = 128;               // samples per page (7-bit address counter)
constexpr int PAGES = RAM_SIZE / PAGE;  // 128

inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

// ---- master card: the frequency every channel's pitch divider counts from ----
inline double masterFreq(int tune = 0x80) { return double(0xF00 | (tune & 0xFF)) * (MOSC / 2.0) / 4096.0; }

// ---- channel card pitch registers ----
struct PitchRegs { int oct = 0, pitch = 0; double clock = 0; };

inline double channelClock(int oct, int pitch, double mf) {
    double c = double(0x800 | (pitch << 1)) * mf / 4096.0;
    if (!(oct & 8)) c /= double(2 << ((7 ^ oct) & 7));
    return c / 16.0;
}

// The registers the CPU writes for a wanted sample clock: the octave that puts the 11-bit count in range, then the
// nearest 10-bit pitch. The result is the clock the card actually runs at (steps of about 0.85 cent).
inline PitchRegs pitchRegsFor(double want, double mf) {
    const double k = mf / 4096.0 / 16.0;
    PitchRegs r;
    for (int o = 0; o < 8; ++o) {
        const double div = double(2 << (7 ^ o));
        const double x = want * div / k;
        if (x < 4096.0 || o == 7) {
            const double xc = clampd(x, 2048.0, 4094.0);
            r.oct = o; r.pitch = clampi((int) std::lround((xc - 2048.0) / 2.0), 0, 1023);
            r.clock = channelClock(o, r.pitch, mf);
            return r;
        }
    }
    return r;
}

// ---- SSM2045 filter on the channel card ----
// fval: the filter DAC input, (octave << 5) + filter latch. Returns the cutoff of the first section (Hz).
inline double filterCutoff(int fval) { return std::min(14000.0, 6410.0 * std::pow(1.02162, fval - 256)); }

struct Biquad2 {   // one two-pole low-pass section, H(z) = (1 + 2z^-1 + z^-2) / (k0 + k1 z^-1 + k2 z^-2)
    double k0 = 1, k1 = 0, k2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double run(double x) { const double y = (x + 2 * x1 + x2 - k1 * y1 - k2 * y2) / k0; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
    void reset() { x1 = x2 = y1 = y2 = 0; }
};

struct SSM2045 {
    Biquad2 a, b; int lastF = -1; double lastFs = 0;
    void set(int fval, double fs) {
        if (fval == lastF && fs == lastFs) return;
        lastF = fval; lastFs = fs;
        double fc = filterCutoff(fval);
        fc = std::min(fc, fs * 0.42);
        const double f0 = fc * 0.916;                       // -6 dB point, where the bilinear map is matched
        const double w1 = 2 * PI * fc, w2 = 2 * PI * fc * 1.22474487139159;
        const double a1 = 1.81659021245849, a2 = 1.48323969741913;   // damping of the two sections (card capacitors)
        const double zc = 2 * PI * f0 / std::tan(PI * std::min(f0, fs * 0.45) / fs);
        const double ma0 = a1 / w1, ma1 = 1 / (w1 * w1), mb0 = a2 / w2, mb1 = 1 / (w2 * w2);
        const double za0 = ma1 * zc * zc, za1 = ma0 * zc, zb0 = mb1 * zc * zc, zb1 = mb0 * zc;
        a.k0 = za0 + za1 + 1; a.k1 = -2 * za0 + 2; a.k2 = za0 - za1 + 1;
        b.k0 = zb0 + zb1 + 1; b.k1 = -2 * zb0 + 2; b.k2 = zb0 - zb1 + 1;
    }
    double run(double x) { return b.run(a.run(x)); }
    void reset() { a.reset(); b.reset(); }
};

// ---- envelope counter rate multiplier ----
// The card divides the envelope clock (ECLK) by a 6-bit rate multiplier whose rate word is built from the counter's
// top four bits: rising, ((~env >> 2) & 0x3C) | 3; falling, ((env >> 2) & 0x3C) | 3. Out of every 64 ECLKs, that
// many reach the counter.
inline int envRate(int env, bool up) { return ((up ? (~env >> 2) : (env >> 2)) & 0x3C) | 3; }

// ECLKs needed for a full sweep (0 -> 255 rising, 255 -> 0 falling): lets the software pick ECLK for a time
inline double envSweepClocks(bool up) {
    double s = 0;
    if (up) for (int e = 0; e < 255; ++e) s += 64.0 / envRate(e, true);
    else for (int e = 255; e > 0; --e) s += 64.0 / envRate(e, false);
    return s;
}

// The CMI's control values (0..31 on the Page 7 bar graphs) as seconds for a full sweep
inline double envTime(double v) { const double u = clampd(v, 0, 31) / 31.0; return std::max(0.0004, 12.0 * u * u * u); }

// ---- band-limited step (the DAC's staircase, alias-free at the host rate) ----
// Linear-phase residual of a Blackman-windowed sinc step, BL_HALF taps either side, BL_PH sub-sample phases.
constexpr int BL_HALF = 8, BL_TAPS = 2 * BL_HALF, BL_PH = 64;

struct BlepTable {
    float r[BL_PH + 1][BL_TAPS];
    BlepTable() {
        // integrate the windowed sinc finely
        const int over = 256, n = BL_TAPS * over;
        std::vector<double> h((size_t) n + 1), c((size_t) n + 1);
        const double cut = 0.92;   // fraction of Nyquist
        double sum = 0;
        for (int i = 0; i <= n; ++i) {
            const double t = (double) i / over - BL_HALF;    // samples
            const double x = PI * cut * t;
            const double s = std::fabs(t) < 1e-9 ? 1.0 : std::sin(x) / x;
            const double w = 0.42 + 0.5 * std::cos(PI * t / BL_HALF) + 0.08 * std::cos(2 * PI * t / BL_HALF);
            h[(size_t) i] = s * w;
        }
        for (int i = 0; i <= n; ++i) { sum += h[(size_t) i]; c[(size_t) i] = sum; }
        for (int i = 0; i <= n; ++i) c[(size_t) i] /= sum;
        for (int p = 0; p <= BL_PH; ++p) {
            const double frac = (double) p / BL_PH;     // how far before the output sample the step happened (0..1)
            for (int k = 0; k < BL_TAPS; ++k) {
                const double tau = k - BL_HALF + frac;  // time of output tap after the step
                const double pos = (tau + BL_HALF) * over;
                const int i = clampi((int) std::floor(pos), 0, n - 1);
                const double f = pos - i;
                const double bl = (tau <= -BL_HALF) ? 0 : (tau >= BL_HALF) ? 1 : c[(size_t) i] + (c[(size_t) i + 1] - c[(size_t) i]) * f;
                r[p][k] = (float) (bl - (tau >= 0 ? 1.0 : 0.0));
            }
        }
    }
    static const BlepTable& get() { static const BlepTable t; return t; }
};

// ---- sounds: what goes into a channel card's RAM ----
enum class Mode : uint8_t { Sample = 0, Waveform = 1 };

// Page 7 control values for one instrument
struct Controls {
    float level = 0.85f;     // volume latch, 0..1
    float filter = 255;      // filter latch 0..255 (the octave register is added by the card)
    float attack = 0;        // 0..31
    float damp = 10;         // release, 0..31
    float sustain = 1;       // 1: hold while the key is down; 0: the damping starts as soon as the attack ends
    float vibDepth = 0;      // semitones
    float vibRate = 5.5f;    // Hz
    float vibDelay = 0.25f;  // s
    float porta = 0;         // s
    float velVol = 0.6f;     // keyboard velocity -> volume, 0..1
    float velFilt = 0.3f;    // velocity -> filter latch, 0..1
    float transpose = 0;     // semitones
    float fine = 0;          // cents
    float loop = 1;          // loop the sound's loop pages while held
    float segTime = 0.03f;   // waveform mode: seconds per segment
};

struct Sound {
    std::string name;        // eight characters, as on the disk directory
    std::string title;       // what it is
    std::string category;
    Mode mode = Mode::Sample;
    std::vector<uint8_t> ram;   // offset binary (0x80 = silence), a whole number of pages, at most RAM_SIZE
    double rate = 20000;     // sample clock that plays it at its root key (waveform mode: 128 x the root frequency)
    int root = 60;
    int loopStart = -1, loopEnd = -1;   // pages, inclusive; -1: no loop
    Controls ctl;            // the controls it was saved with
    int pages() const { return (int) ram.size() / PAGE; }
    // the key (fractional) that plays the sample at the speed it was recorded: a voice from a CMI disk (the pitch rule:
    // clock = 128 x the key's frequency) was sampled at the Series II's 30.2 kHz, so that's A#3 and 21 cents (the
    // extractor's WAVs of the same voices are all 30.2 kHz too); a sample of your own, at its root. Drums and Page S's
    // tracks play here: at A4 (the voice's reference) a CMI drum ran about nine semitones fast.
    double naturalKey() const {
        if (mode == Mode::Sample && std::fabs(rate - 128.0 * 440.0) < 1 && root == 69) return 69.0 + 12.0 * std::log2(30200.0 / (128.0 * 440.0));
        return (double) root;
    }
};

}  // namespace cmi
