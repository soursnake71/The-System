// Omega SID: a circuit-level model of the MOS 6581 and 8580 Sound Interface Device.
//
// Pure C++ (no JUCE), shared by the VST3 plugin and the Android app. The chip is clocked in batches of
// (fractional) PAL / NTSC cycles at an oversampled internal rate; the synth (SidSynth.h) decimates the result.
//
// What is modelled:
//  - 24-bit phase accumulators, the 23-bit noise LFSR clocked by accumulator bit 19, hard sync and ring
//    modulation wired 3->1, 1->2, 2->3, the TEST bit (holds the oscillator, refills the LFSR).
//  - Combined waveforms: the shorted waveform-selector outputs pull neighbouring bits down; each output bit is a
//    distance-weighted vote of its neighbours against a per-model threshold. Noise combined with another wave
//    writes zeros back into the LFSR, so it locks up the way the chip does.
//  - The envelope generator: the 15-bit rate counter with the real period table, the piecewise exponential decay
//    divider, the sustain comparator, hold-at-zero, and (optional) the rate-counter wrap-around "ADSR bug".
//  - R-2R ladder DACs for the waveform (12-bit), envelope (8-bit) and cutoff (11-bit): the 6581's 2R/R = 2.2 with
//    no termination gives its uneven steps; the 8580's ladder is terminated and linear. The 6581 waveform DAC
//    has its zero level at 0x380, so voices carry the chip's DC offset.
//  - The filter: the two-integrator loop (summer -> HP, integrator -> BP, integrator -> LP) solved as a
//    zero-delay-feedback state-variable filter. Each integrator's input resistor is an NMOS FET used as a
//    voltage-controlled resistor; on the 6581 its conductance depends on the signal on its source, so the cutoff
//    moves with the signal (the 6581's grit), and the op-amps clip asymmetrically. The 6581 cutoff curve (DAC ->
//    FET gate voltage -> conductance) has its floor at about 220 Hz and a strong upper bend; the 8580's is linear.
//    Resonance follows each model's feedback network.
//  - The mixer with voice 3 disconnect, the 4-bit master volume, and the C64 board's output RC filters.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace sid {

enum Model { MOS6581 = 0, MOS8580 = 1 };
constexpr double PAL_CLOCK = 985248.0, NTSC_CLOCK = 1022727.0;
constexpr double kPi = 3.14159265358979323846;

// control register bits
enum : uint8_t { GATE = 1, SYNC = 2, RING = 4, TEST = 8, TRI = 16, SAW = 32, PULSE = 64, NOISE = 128 };

// Weight of each bit of an R-2R ladder: solve the network for one bit switched on at a time. With r2r = 2 and the
// termination fitted, the weights are exact powers of two; the 6581 leaves the termination out and has 2R/R ~ 2.2.
inline void ladderWeights(double* w, int bits, double r2r, bool term) {
    const double R = 1, R2 = r2r * R, INF = 1e30;
    double sum = 0;
    for (int set = 0; set < bits; ++set) {
        double Vn = 1, Rn = term ? R2 : INF;
        for (int b = 0; b < set; ++b) Rn = Rn >= INF ? R + R2 : R + R2 * Rn / (R2 + Rn);
        if (Rn >= INF) Rn = R2;
        else { Rn = R2 * Rn / (R2 + Rn); Vn = Vn * Rn / R2; }
        for (int b = set + 1; b < bits; ++b) { Rn += R; const double I = Vn / Rn; Rn = R2 * Rn / (R2 + Rn); Vn = Rn * I; }
        w[set] = Vn; sum += Vn;
    }
    for (int b = 0; b < bits; ++b) w[b] /= sum;   // all bits on = 1.0
}

struct Tables {
    float wave[2][4096];     // 12-bit waveform DAC, 0..1
    float env[2][256];       // 8-bit envelope DAC, 0..1
    float fc[2][2048];       // 11-bit cutoff DAC, 0..1
    uint16_t combo[2][4][4096];   // ST, PT, PS, PST for each model, indexed by the accumulator's top 12 bits
    static const Tables& get() { static Tables t; return t; }
private:
    Tables() {
        for (int m = 0; m < 2; ++m) {
            const double r = m == 0 ? 2.20 : 2.00; const bool term = m == 1;
            double w12[12], w8[8], w11[11];
            ladderWeights(w12, 12, r, term); ladderWeights(w8, 8, r, term); ladderWeights(w11, 11, r, term);
            for (int v = 0; v < 4096; ++v) { double s = 0; for (int b = 0; b < 12; ++b) if (v >> b & 1) s += w12[b]; wave[m][v] = (float) s; }
            for (int v = 0; v < 256; ++v) { double s = 0; for (int b = 0; b < 8; ++b) if (v >> b & 1) s += w8[b]; env[m][v] = (float) s; }
            for (int v = 0; v < 2048; ++v) { double s = 0; for (int b = 0; b < 11; ++b) if (v >> b & 1) s += w11[b]; fc[m][v] = (float) s; }
            buildCombos(m);
        }
    }
    // the pull-down model for combined waveforms
    void buildCombos(int m) {
        // per combination: threshold, neighbour falloff, how hard the pulse line pulls up
        struct P { double thr, dist, pulse; };
        const P p6581[4] = { {0.92, 0.80, 0}, {0.88, 1.10, 0.85}, {0.90, 1.00, 0.85}, {0.93, 0.90, 0.80} };
        const P p8580[4] = { {0.84, 0.55, 0}, {0.80, 0.80, 0.95}, {0.82, 0.70, 0.95}, {0.86, 0.65, 0.90} };
        const P* pp = m == 0 ? p6581 : p8580;
        for (int c = 0; c < 4; ++c) {
            const bool hasS = c == 0 || c >= 2, hasT = c == 0 || c == 1 || c == 3, hasP = c >= 1;
            double wt[12]; for (int d = 0; d < 12; ++d) wt[d] = 1.0 / (1.0 + d * d * pp[c].dist);
            for (int i = 0; i < 4096; ++i) {
                const int saw = i, tri = ((i & 0x800) ? ~i : i) << 1 & 0xFFF;
                double v[12];
                for (int b = 0; b < 12; ++b) {
                    double s = 0, n = 0;
                    if (hasS) { s += saw >> b & 1; n += 1; }
                    if (hasT) { s += tri >> b & 1; n += 1; }
                    if (hasP) { s += pp[c].pulse; n += pp[c].pulse; }
                    v[b] = s / n;
                }
                int out = 0;
                for (int b = 0; b < 12; ++b) {
                    double num = 0, den = 0;
                    for (int j = 0; j < 12; ++j) { const double w = wt[std::abs(b - j)]; num += v[j] * w; den += w; }
                    // a bit can only be high if every selected source drives it high
                    const bool own = (!hasS || (saw >> b & 1)) && (!hasT || (tri >> b & 1));
                    if (own && num / den > pp[c].thr - 0.04 * (b / 11.0)) out |= 1 << b;
                }
                combo[m][c][i] = (uint16_t) out;
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
struct Envelope {
    enum State { ATTACK, DECAY_SUSTAIN, RELEASE };
    static constexpr int rates[16] = { 9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251 };
    uint8_t attack = 0, decay = 0, sustain = 0, release = 0;
    State state = RELEASE; uint8_t level = 0; int rateCounter = 0, expCounter = 0; bool holdZero = true, gate = false;
    bool bug = false;   // let the 15-bit rate counter wrap when the period drops below it (the "ADSR bug")

    void setGate(bool g) {
        if (g == gate) return;
        gate = g;
        if (g) { state = ATTACK; holdZero = false; }
        else state = RELEASE;
    }
    int period() const { return rates[state == ATTACK ? attack : state == DECAY_SUSTAIN ? decay : release]; }
    static int expPeriod(int lv) { return lv > 0x5D ? 1 : lv > 0x36 ? 2 : lv > 0x1A ? 4 : lv > 0x0E ? 8 : lv > 0x06 ? 16 : lv > 0 ? 30 : 1; }
    void reset() { level = 0; rateCounter = 0; expCounter = 0; holdZero = true; state = gate ? ATTACK : RELEASE; if (gate) holdZero = false; }

    void clock(int cycles) {
        while (cycles > 0) {
            const int per = period();
            if (rateCounter >= per && !bug) rateCounter = per - 1;
            const int toGo = rateCounter < per ? per - rateCounter : 0x8000 - rateCounter + per;
            if (cycles < toGo) { rateCounter = (rateCounter + cycles) & 0x7FFF; return; }
            cycles -= toGo; rateCounter = 0;
            if (state == ATTACK) {
                expCounter = 0;
                if (level < 0xFF) ++level;
                if (level == 0xFF) state = DECAY_SUSTAIN;
                continue;
            }
            if (holdZero) continue;
            if (++expCounter < expPeriod(level)) continue;
            expCounter = 0;
            if (state == DECAY_SUSTAIN) { if (level != sustain * 0x11 && level > 0) --level; }
            else if (level > 0) --level;
            if (level == 0) holdZero = true;
        }
    }
};
constexpr int Envelope::rates[16];

// ---------------------------------------------------------------------------------------------------------------
struct Voice {
    // registers
    uint16_t freq = 0, pw = 0x800; uint8_t ctrl = 0;
    // state
    double acc = 0; uint32_t lfsr = 0x7FFFFF; bool msbRise = false; double riseFrac = 0; uint32_t prevAcc = 0;
    Envelope env;

    static constexpr double ACC = 16777216.0, MSB = 8388608.0;

    // advance the accumulator; the sync pass happens afterwards in Chip::tick
    void advance(double cycles) {
        prevAcc = (uint32_t) acc;
        if (ctrl & TEST) { acc = 0; lfsr = 0x7FFFFF; msbRise = false; return; }
        const double inc = freq * cycles, a = acc + inc;
        msbRise = acc < MSB && a >= MSB;
        if (msbRise) riseFrac = inc > 0 ? (a - MSB) / inc : 0;   // fraction of the batch left after the rise
        // the noise shift register clocks on each rising edge of accumulator bit 19 (whether or not noise is selected)
        {
            const int64_t k0 = (int64_t) ((acc + 524288.0) / 1048576.0), k1 = (int64_t) ((a + 524288.0) / 1048576.0);
            for (int64_t k = k0; k < k1; ++k) { const uint32_t bit = ((lfsr >> 22) ^ (lfsr >> 17)) & 1; lfsr = ((lfsr << 1) | bit) & 0x7FFFFF; }
        }
        acc = a >= ACC ? a - ACC : a;
    }
    uint32_t noiseOut() const {
        return ((lfsr >> 9) & 0x800) | ((lfsr >> 8) & 0x400) | ((lfsr >> 5) & 0x200) | ((lfsr >> 3) & 0x100)
             | ((lfsr >> 2) & 0x080) | ((lfsr << 1) & 0x040) | ((lfsr << 3) & 0x020) | ((lfsr << 4) & 0x010);
    }
    // the waveform generator's 12-bit output
    uint32_t wave12(int model, uint32_t ringAcc) {
        const uint32_t a = (uint32_t) acc & 0xFFFFFF, top = a >> 12;
        const int w = ctrl >> 4;
        const bool pulseHi = (ctrl & TEST) || top >= pw;
        auto tri = [&]() { uint32_t msb = a & 0x800000; if (ctrl & RING) msb ^= ringAcc & 0x800000; return ((msb ? ~a : a) >> 11) & 0xFFF; };
        const Tables& T = Tables::get();
        uint32_t out;
        switch (w & 7) {
            case 0: out = 0; break;
            case 1: out = tri(); break;
            case 2: out = top; break;
            case 3: out = T.combo[model][0][top]; break;
            case 4: out = pulseHi ? 0xFFF : 0; break;
            case 5: out = pulseHi ? T.combo[model][1][top] : 0; break;
            case 6: out = pulseHi ? T.combo[model][2][top] : 0; break;
            default: out = pulseHi ? T.combo[model][3][top] : 0; break;
        }
        if (w & 8) {
            const uint32_t n = noiseOut();
            if (w == 8) out = n;
            else {
                out &= n;
                // the combined output drives the LFSR's own output bits low: noise locks up
                const int src[8] = { 20, 18, 14, 11, 9, 5, 2, 0 };
                for (int i = 0; i < 8; ++i) if (!(out >> (11 - i) & 1)) lfsr &= ~(1u << src[i]);
            }
        }
        return out;
    }
};

// band-limited step residual for the saw and pulse edges (two samples wide)
inline double polyBlep(double t, double dt) {
    if (dt <= 0) return 0;
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
class Chip {
public:
    // registers
    Voice v[3];
    uint16_t fc = 1024; uint8_t res = 0, filt = 0, mode = 0x10, vol = 15;   // mode: LP 0x10, BP 0x20, HP 0x40, 3OFF 0x80
    // configuration
    Model model = MOS6581;
    double curve = 0;      // 6581 chip-to-chip cutoff variation: -1 dark ... +1 bright
    double drive = 0.4;    // how much the signal moves the filter's FET resistors (6581) / op-amp clipping
    double clock = PAL_CLOCK;
    // the mixer (outside the chip): each voice's level before it reaches the filter, the pan of its direct path,
    // and the filter output's level and pan (equal-power gains, 0.707 each at centre); peaks for the meters
    float vgain[3] { 1, 1, 1 }, vpl[3] { .7071f, .7071f, .7071f }, vpr[3] { .7071f, .7071f, .7071f };
    float fgain = 1, fpl = .7071f, fpr = .7071f;
    float vpk[3] {}, fpk = 0;

    // ---- digi playback: what C64 code does in its NMI handler, at a timer period counted in cycles ----
    // method 1: write the sample (4 bits) to the master volume; the mixer's DC offset makes it audible
    // method 2: write a $D418 value (volume + filter mode bits) from a table, with the voices held at DC (Mahoney)
    // method 3: reset oscillator 1 and set its pulse width from the sample (pulse-width modulation)
    // method 4: an external 8-bit DAC on the expansion port (Digimax)
    const uint8_t* digi = nullptr; const uint8_t* mahoney = nullptr;
    int digiStart = 0, digiEnd = 0, digiIdx = 0, digiMethod = 0;
    bool digiActive = false, digiLoop = false, digiGate = false, digiOneShot = true;
    double nmiPeriod = 126, nmiCount = 0;
    float mixDC = 0, digiLevel = 1;
    uint8_t baseVol = 15, baseMode = 0x10, digiVol = 15, digiMode = 0x10; int dmx = 128, pwmMax = 4095;
    void startDigi(const uint8_t* data, int a, int b, int method, bool loop, bool oneShot) {
        digi = data; digiStart = a; digiEnd = b; digiIdx = a; digiMethod = method; digiLoop = loop; digiOneShot = oneShot;
        digiGate = true; digiActive = data != nullptr && b > a; nmiCount = 0; dmx = 128;
        digiVol = vol; digiMode = mode;
    }
    void stopDigi() {
        // the voices a Mahoney or PWM digi held at full level go quiet, rather than ringing out as a note
        if (digiActive && (digiMethod == 2 || digiMethod == 3))
            for (int i = 0; i < (digiMethod == 2 ? 3 : 1); ++i) { v[i].env.level = 0; v[i].env.holdZero = true; v[i].env.gate = false; v[i].env.state = Envelope::RELEASE; }
        digiActive = false; vol = baseVol; mode = baseMode; dmx = 128;
    }
    void releaseDigi() { digiGate = false; if (!digiOneShot) stopDigi(); }
    // PWM: the synth rewrites the patch's pulse width every tick; keep the one the digi set
    uint16_t pwmPw = 0;
    uint16_t digiPwm(uint16_t) const { return pwmPw; }
    // after the synth writes the patch's registers: the digi keeps the registers it owns
    void keepDigiRegs() {
        if (!digiActive) return;
        if (digiMethod == 1) vol = digiVol;
        else if (digiMethod == 2) { vol = digiVol; mode = digiMode; }
    }

    void setRate(double internalRate) { fsI = internalRate; cyclesPerTick = clock / fsI; updateFilter(); }
    void setClock(double c) { clock = c; setRate(fsI); }
    void reset() {
        for (auto& x : v) { x.acc = 0; x.lfsr = 0x7FFFFF; x.env.gate = false; x.env.state = Envelope::RELEASE; x.env.level = 0; x.env.holdZero = true; }
        s1 = s2 = 0; lastHp = lastBp = 0; digiActive = false; dmx = 128;
    }
    bool silent() const {
        if (digiActive) return false;
        for (auto& x : v) if (x.env.level || x.env.gate) return false;
        return std::fabs(s1) + std::fabs(s2) < 1e-6;
    }
    void updateFilter() {
        const Tables& T = Tables::get();
        const double x = T.fc[model][fc & 0x7FF];
        double hz;
        if (model == MOS6581) {
            // DAC voltage on the FET gates; below the threshold the integrator resistors are the fixed ~220 Hz floor
            const double bright = std::pow(2.0, 0.8 * curve), expo = 2.35 - 0.45 * curve;
            hz = 220.0 + 15500.0 * bright * std::pow(x, expo);
            q = 0.707 + 1.2 * res / 15.0;
        } else {
            hz = 30.0 + 12470.0 * x;
            q = 1.0 / std::pow(2.0, (4.0 - res) / 8.0);
        }
        hz = std::min(hz, fsI * 0.42);
        g = std::tan(kPi * hz / fsI);
    }

    // one internal sample: advance every part of the chip by cyclesPerTick; the stereo output goes to L / R
    void tick(float& L, float& R) {
        cycAcc += cyclesPerTick;
        const int whole = (int) cycAcc; cycAcc -= whole;
        const double cyc = cyclesPerTick;
        for (auto& x : v) x.advance(cyc);
        // hard sync: voice i resets when its source's MSB rises (source of 1 is 3, of 2 is 1, of 3 is 2)
        for (int i = 0; i < 3; ++i) {
            const Voice& s = v[(i + 2) % 3];
            if ((v[i].ctrl & SYNC) && s.msbRise && !(v[i].ctrl & TEST)) v[i].acc = v[i].freq * cyc * s.riseFrac;
        }
        for (auto& x : v) x.env.clock(whole);
        if (digiActive) {
            nmiCount -= cyclesPerTick;
            while (nmiCount <= 0 && digiActive) { nmiCount += nmiPeriod; digiStep(); }
        }

        const Tables& T = Tables::get();
        const int m = model;
        const double zero = m == MOS6581 ? T.wave[0][0x380] : 0.5;
        double dl = 0, dr = 0, fin = 0;
        for (int i = 0; i < 3; ++i) {
            Voice& x = v[i];
            const uint32_t w = x.wave12(m, (uint32_t) v[(i + 2) % 3].acc);
            double y = T.wave[m][w];
            // band-limit the saw and pulse edges (the chip itself steps once per cycle; this keeps the oversampled
            // model from folding the steps back down)
            const int ws = x.ctrl >> 4 & 15;
            if (!(x.ctrl & TEST) && !(x.ctrl & SYNC)) {
                const double ph = x.acc / Voice::ACC, dt = x.freq * cyc / Voice::ACC;
                if (ws == 2) y -= 0.5 * polyBlep(ph, dt);
                else if (ws == 4 && x.pw > 0 && x.pw < 4096) {
                    y -= 0.5 * polyBlep(ph, dt);
                    double p2 = ph - x.pw / 4096.0; if (p2 < 0) p2 += 1;
                    y += 0.5 * polyBlep(p2, dt);
                }
            }
            const double out = (y - zero) * T.env[m][x.env.level] * vgain[i];
            if (filt >> i & 1) fin += out;
            else if (!(i == 2 && (mode & 0x80))) { dl += out * vpl[i]; dr += out * vpr[i]; }
            else continue;
            vpk[i] = std::max(vpk[i], (float) std::fabs(out));
        }
        // the summing node in front of the filter: resistive mixing, and on the 6581 an op-amp that runs out of swing
        fin *= 0.6;
        if (m == MOS6581) fin = softClip(fin, 1.4);
        double outF = 0;
        if (filt & 7) outF = filterTick(fin);
        else { s1 *= 0.999; s2 *= 0.999; }
        outF *= fgain;
        fpk = std::max(fpk, (float) std::fabs(outF));
        // master volume: a 4-bit multiplying DAC (the stereo split is the mixer's; the chip's output is mono)
        const double outG = vol / 15.0 * 0.72 * 1.41421356;
        // the mixer's DC offset moves with the volume register (that's what makes $D418 digis audible); the part that
        // doesn't move is taken out, as the output capacitor would, so chips waking up don't thump
        const double dc = mixDC * digiLevel * ((int) vol - (int) baseVol) / 15.0 * 0.72;
        const double ext = digiActive && digiMethod == 4 ? (dmx - 128) / 128.0 * 0.5 * digiLevel : 0.0;
        L = (float) ((dl * 0.6 + outF * fpl) * outG + dc + ext);
        R = (float) ((dr * 0.6 + outF * fpr) * outG + dc + ext);
    }

private:
    void digiStep() {
        if (digiIdx >= digiEnd) {
            if (digiLoop && digiGate) digiIdx = digiStart;
            else { stopDigi(); return; }
        }
        const uint8_t smp = digi[digiIdx++];
        switch (digiMethod) {
            case 1: vol = digiVol = (uint8_t) (smp >> 4); break;
            case 2: { const uint8_t r = mahoney ? mahoney[smp] : smp; vol = digiVol = r & 15; mode = digiMode = r & 0xF0; break; }
            case 3: v[0].acc = 0; v[0].pw = pwmPw = (uint16_t) (pwmMax - smp * pwmMax / 255); break;   // high for longer = louder
            case 4: dmx = smp; break;
            default: break;
        }
    }
    double fsI = 192000, cyclesPerTick = 5, cycAcc = 0;
    double g = 0.1, q = 0.707;
    double s1 = 0, s2 = 0, lastHp = 0, lastBp = 0;

    static double softClip(double x, double lim) { return lim * std::tanh(x / lim); }
    // asymmetric op-amp swing: NMOS op-amps pull down harder than they pull up
    static double opamp(double x) { return x > 0 ? 1.15 * std::tanh(x / 1.15) : 1.35 * std::tanh(x / 1.35); }

    double filterTick(double x) {
        const double k = 1.0 / q;
        double g1 = g, g2 = g;
        if (model == MOS6581) {
            // FET conductance falls as the source (the integrator input node) rises towards the gate voltage
            const double d = 0.35 * drive;
            g1 = g * std::clamp(1.0 - d * lastHp, 0.25, 1.9);
            g2 = g * std::clamp(1.0 - d * lastBp, 0.25, 1.9);
        }
        // zero-delay-feedback two-integrator loop (trapezoidal integrators)
        const double hp = (x - (k + g2) * s1 - s2) / (1.0 + g1 * k + g1 * g2);
        const double bp = g1 * hp + s1;
        const double lp = g2 * bp + s2;
        s1 = g1 * hp + bp;
        s2 = g2 * bp + lp;
        if (model == MOS6581) { s1 = opamp(s1 * (1 + 0.25 * drive)) / (1 + 0.25 * drive); s2 = opamp(s2); }
        else { s1 = 1.6 * std::tanh(s1 / 1.6); s2 = 1.6 * std::tanh(s2 / 1.6); }
        lastHp = hp; lastBp = bp;
        double y = 0;
        if (mode & 0x10) y += lp;
        if (mode & 0x20) y += bp;
        if (mode & 0x40) y += hp;
        return y;
    }
};

// The Mahoney table for one chip model, measured on the model itself: hold all three voices at DC (TEST + pulse,
// envelope at full), route voices 1 and 2 through the filter at its lowest cutoff, and for every $D418 value
// (volume + LP / BP / HP / 3OFF) let the circuit settle and read the output. Each 8-bit sample value then gets the
// $D418 value whose level is closest. It's only as linear as the chip is.
inline const uint8_t* mahoneyTable(int model) {
    struct T { uint8_t t[2][256]; T() {
        for (int m = 0; m < 2; ++m) {
            double lvl[256];
            for (int r = 0; r < 256; ++r) {
                Chip c; c.model = m ? MOS8580 : MOS6581; c.setRate(192000);
                c.mixDC = m ? 0.06f : 0.5f; c.baseVol = 0;
                for (auto& x : c.v) { x.ctrl = TEST | PULSE | GATE; x.env.gate = true; x.env.level = 255; x.env.state = Envelope::DECAY_SUSTAIN; x.env.sustain = 15; x.env.holdZero = false; }
                c.filt = 3; c.fc = 0; c.res = 0; c.updateFilter();
                c.vol = (uint8_t) (r & 15); c.mode = (uint8_t) (r & 0xF0);
                float L = 0, R = 0; double acc = 0;
                for (int k = 0; k < 2400; ++k) { c.tick(L, R); if (k >= 2000) acc += L; }
                lvl[r] = acc / 400;
            }
            double lo = 1e9, hi = -1e9; for (double x : lvl) { lo = std::min(lo, x); hi = std::max(hi, x); }
            for (int s = 0; s < 256; ++s) {
                const double want = lo + (hi - lo) * s / 255.0; int best = 0; double bd = 1e9;
                for (int r = 0; r < 256; ++r) { const double d = std::fabs(lvl[r] - want); if (d < bd) { bd = d; best = r; } }
                t[m][s] = (uint8_t) best;
            }
        }
    } };
    static const T table;
    return table.t[model ? 1 : 0];
}

}  // namespace sid
