// Prophet-5 Rev 3 and Six-Trak: the machines around the user's firmware ROMs, and their voices.
// C++ port of the page's P5Machine, P5Voices, P5R3Engine, SixtrakMachine, SixtrakEngine.
#pragma once
#include <array>
#include <map>
#include <functional>
#include "Z80.h"
#include "Wms.h"

namespace farp {

class P5Machine : public Z80Bus, public Pit8253::Listener {
public:
    double cpuHz = 2.5e6;
    uint8_t rom[0xC00] = {}, nvram[0x400] = {}, ram[0x400] = {};
    bool recordEnabled = true;
    double cv[40] = {};
    bool gate[5] = {};
    int latch0 = 0, latch1 = 0, latch2 = 0, ledRow = 0, ledCol = 0;
    int digits[2] = {0, 0};
    uint8_t ledColumns[7] = {};
    uint8_t switches[16] = {};
    double pots[24] = {};
    bool releaseFootswitch = true;
    int tuneSelect = -1; bool tuning = false;
    std::function<double(int)> oscFreq;
    Pit8253 pit; Z80 cpu;
    double cycleAcc = 0; int switchRow = 0, potMuxAbc = 0, potMuxInh = 7, muxAbc = 0, shInh = 0x1F, dacLatch = 0;
    double vdacVolts = 0, adcVref = 0, adcVmux = 0;
    bool tuneCounterOut = false, ffQ = true, ffQn = true, ffD = false, ffClr = false, ffPre = false, cmpOut = true;
    double oscPhase[10] = {};
    int digitsLit[2] = {0, 0}; double digitsLitAt[2] = {0, 0}; bool nvDirty = false;

    P5Machine() {
        static const int def[24] = {0, 120, 0, 0, 0, 120, 120, 0, 240, 0, 240, 120, 240, 120, 0, 0, 10, 10, 240, 20, 10, 10, 240, 20};
        for (int i = 0; i < 24; i++) pots[i] = def[i] / 255.0;
        cpu.bus = this; pit.onOut = this;
        reset();
    }
    void pitOut(int i, bool v) override { if (i == 0) { tuneCounterOut = v; pit.setGate(2, !v); } }
    void reset() { std::fill(ram, ram + 0x400, 0); cpu.reset(); miscLatch(0); pit.setGate(0, false); pit.setGate(1, false); pit.setGate(2, true); cycleAcc = 0; }
    void run(double seconds) {
        cycleAcc += seconds * cpuHz;
        while (cycleAcc > 0) { int t = cpu.step(); cycleAcc -= t; pit.clock(1, t); pit.clock(2, t); if (oscFreq) advanceTuning(t / cpuHz); }
    }
    int memRead(int addr) override {
        int m = addr & 0x1FFF;
        if (m < 0x0C00) return rom[m]; if (m < 0x1000) return nvram[m - 0x0C00]; if (m < 0x1400) return ram[m - 0x1000];
        if (m >= 0x1800 && m < 0x1C00) return pit.read(m & 3); return 0xFF;
    }
    void memWrite(int addr, int v) override {
        int a = addr & 0x9FFF, m = a & 0x1FFF;
        if (a >= 0x8C00 && a < 0x9000) { if (recordEnabled) { if (nvram[a - 0x8C00] != v) nvDirty = true; nvram[a - 0x8C00] = (uint8_t)v; } return; }
        if (m >= 0x1000 && m < 0x1400) { ram[m - 0x1000] = (uint8_t)v; return; }
        if (m >= 0x1800 && m < 0x1C00) { pit.write(m & 3, v); return; }
    }
    int ioRead(int port) override {
        int p = port & 0x3F;
        if (p & 1) return switches[switchRow];
        if (p & 2) return ((tuneCounterOut ? 1 : 0) << 5) | ((recordEnabled ? 1 : 0) << 4) | ((ffQn ? 1 : 0) << 2) | ((releaseFootswitch ? 1 : 0) << 1) | 1;
        if (p & 4) { int lo = adcVmux < adcVref - 0.034 ? 1 : 0, hi = adcVmux > adcVref + 0.034 ? 1 : 0; return (hi << 1) | lo; }
        return 0xFF;
    }
    void ioWrite(int port, int v) override {
        int p = port & 0x3F;
        if (p < 0x30) {
            switch (p >> 3) {
                case 0: ledRow = v; for (int c = 0; c < 5; c++) if (ledCol & (1 << c)) ledColumns[c] = (uint8_t)v; latchDigits(); break;
                case 1: ledCol = v; for (int c = 0; c < 5; c++) if (v & (1 << c)) ledColumns[c] = (uint8_t)ledRow; latchDigits(); break;
                case 2: switchRow = v & 0x0F; break;
                case 3: potMuxAbc = v & 7; potMuxInh = (v >> 3) & 7; updateVmux(); break;
                case 4: miscLatch(v); break;
                default: cpu.irqLine = false;
            }
            return;
        }
        switch (p & 0x0B) {
            case 0x00: latch0 = v; break; case 0x01: latch1 = v; break; case 0x02: latch2 = v; break;
            case 0x03: muxAbc = v & 7; updateSh(); tuneControl(v); break;
            case 0x08: shInh = v & 0x1F; updateSh(); updateVmux(); break;
            case 0x09: for (int i = 0; i < 5; i++) gate[i] = ((v >> i) & 1) == 1; break;
            case 0x0A: dacLatch = (dacLatch & 0x3F80) | (v & 0x7F); updateVdac(); break;
            default: dacLatch = ((v & 0x7F) << 7) | (dacLatch & 0x7F); updateVdac();
        }
    }
    void latchDigits() {
        if (ledCol & 0x20) digits[0] = ledRow & 0x7F; if (ledCol & 0x40) digits[1] = ledRow & 0x7F;
        for (int i = 0; i < 2; i++) if (digits[i] != 0) { digitsLit[i] = digits[i]; digitsLitAt[i] = cpu.cycles; }
    }
    int shownDigit(int i) const { return cpu.cycles - digitsLitAt[i] < 0.05 * cpuHz ? digitsLit[i] : 0; }
    void updateVdac() {
        int inp = (((~dacLatch) << 2) | 3) & 0xFFFF; double iOut = 1.99997e-3 * (0xFFFF - inp) / 65535; vdacVolts = iOut * (5e3 + 332); updateSh();
        adcVref = vdacVolts * (20e3 / (5e3 * 0.36 + 18.2e3 + 20e3));
    }
    void updateSh() { if ((shInh & 0x1F) == 0x1F) return; for (int i = 0; i < 5; i++) if (!((shInh >> i) & 1)) cv[8 * i + muxAbc] = vdacVolts; }
    void updateVmux() { double sum = 0; int n = 0; for (int mux = 0; mux < 3; mux++) if (!((potMuxInh >> mux) & 1)) { sum += 5 * pots[8 * mux + potMuxAbc]; n++; } if (n > 0) adcVmux = sum / n; }
    void miscLatch(int v) { ffClr = (v & 1) == 0; ffPre = (v & 2) == 0; ffD = ((v >> 3) & 1) == 1; applyFlipFlopAsync(); pit.setGate(0, ((v >> 4) & 1) == 1); pit.setGate(1, ((v >> 5) & 1) == 1); }
    void applyFlipFlopAsync() { if (ffClr && ffPre) { setFf(true, true); return; } if (ffClr) setFf(false, true); else if (ffPre) setFf(true, false); }
    void setFf(bool q, bool qn) { bool falling = !qn && ffQn, rising = qn && !ffQn; ffQ = q; ffQn = qn; if (rising) pit.clockRise(0); if (falling) pit.clock(0, 1, true); }
    void tuneControl(int v) { tuning = !((v >> 3) & 1); int abc = v & 7; bool mux0 = !((v >> 4) & 1), mux1 = !((v >> 5) & 1); int sel = -1; if (mux0) sel = abc; else if (mux1 && abc < 2) sel = 8 + abc; tuneSelect = sel; }
    void advanceTuning(double dt) {
        if (tuneSelect < 0) { if (!cmpOut) { cmpOut = true; clockFf(); } return; }
        const double thresh = 0.148; double* ph = oscPhase;
        for (int i = 0; i < 10; i++) {
            double f = oscFreq(i), before = ph[i]; ph[i] += dt * std::max(0.0, f);
            if (i != tuneSelect) { if (ph[i] >= 1) ph[i] -= std::floor(ph[i]); continue; }
            if (!cmpOut) { if (ph[i] >= 1) { ph[i] -= std::floor(ph[i]); cmpOut = true; clockFf(); if (ph[i] >= thresh) cmpOut = false; } }
            else if (before < thresh && ph[i] >= thresh) { cmpOut = false; if (ph[i] >= 1) { ph[i] -= std::floor(ph[i]); cmpOut = true; clockFf(); if (ph[i] >= thresh) cmpOut = false; } }
            else if (ph[i] >= 1) { ph[i] -= std::floor(ph[i]); if (ph[i] >= thresh) cmpOut = false; }
        }
    }
    void clockFf() { if (ffClr || ffPre) return; setFf(ffD, !ffD); }
};

class P5Voices {
public:
    struct V { double phA = 0, phB = 0, fEnv = 0, aEnv = 0; bool fAttack = false, aAttack = false, gateWas = false; Cem f; };
    double fs = 48000; V voice[5]; double glide = 0; uint32_t noise = 12345; double RZ = 0; double bendMul = 1, xpMul = 1;
    P5Voices() { prepare(48000); }
    static double cv2cc(double cv, double r) { if (cv <= 0.3) return 0; if (cv >= 0.8) return (cv - 0.6) / r; double t = (cv - 0.3) / 0.5; return t * t * (0.2 / r); }
    static double cem3340(double ictrl, double rz, double rt, double rr, double cf) { const double VT = 0.0257, iom = (22 * VT / rt) * (1 - ictrl * rz / 3), vb = iom * 1.8e3, iref = 15 / rr; return 3 * iref * std::exp(-vb / VT) / (30 * cf); }
    static double egTau(double cv) { double vc = cv * 0.03029 - 0.2830; return 24.3e3 * 0.039e-6 * std::exp(-vc / 0.026); }
    void prepare(double f) { fs = f; for (auto& v : voice) v = V(); glide = 0; RZ = 0.69314718 * 3 * 5.62e3 * 100e3 / (22 * 1.8e3); }
    double freqOf(const P5Machine& m, int v, bool isA) const {
        double i = m.cv[24 + 2 * v + (isA ? 0 : 1)] / 100e3 + 15 / 357e3;
        if (isA) { if (!m.tuning) i += glide / 100e3; } else { if (m.latch0 & 0x40) i += glide / 100e3; if (m.latch2 & 0x20) i -= 7.5 / 100e3; }
        return cem3340(i, RZ, 5.62e3, 2.21e6, 1000e-12);
    }
    double oscFreq(const P5Machine& m, int i) const { return freqOf(m, i >> 1, (i & 1) == 0); }
    void render(P5Machine& m, float* out, int off, int n) {
        const double dt = 1 / fs; const double* cv = m.cv;
        auto C = cv2cc;
        const double gA = C(cv[12], 33e3) / C(10, 33e3), gB = C(cv[10], 33e3) / C(10, 33e3), gN = C(cv[14], 75e3) / C(10, 75e3), envAmt = C(cv[9], 5.1e3) / C(10, 5.1e3);
        const double pmEnv = C(cv[20], 3e3) / C(10, 3e3), pmOsc = C(cv[19], 5.6e3) / C(10, 5.6e3);
        const int L0 = m.latch0, L1 = m.latch1; const bool aPulse = L0 & 1, aRamp = L0 & 2, sync = L0 & 4, bRamp = L0 & 8, bTri = L0 & 0x10, bPulse = L0 & 0x20, pmFreqA = L1 & 1, pmPwA = L1 & 2, pmFilt = L1 & 4, filtKbd = L1 & 0x40;
        const double res = std::min(1.05, std::max(0.0, cv[15] / 10));
        auto E = egTau;
        const double kFA = 1 - std::exp(-dt / E(cv[0])), kFD = 1 - std::exp(-dt / E(cv[1])), kFR = 1 - std::exp(-dt / E(cv[3])), susF = cv[2] * 0.5;
        const double kAA = 1 - std::exp(-dt / E(cv[4])), kAD = 1 - std::exp(-dt / E(cv[5])), kAR = 1 - std::exp(-dt / E(cv[7])), susA = cv[6] * 0.5;
        const double pwA = std::min(0.98, std::max(0.02, cv[13] / 10)), pwB = std::min(0.98, std::max(0.02, cv[11] / 10));
        const double kG = 1 - std::exp(-dt / (0.0005 + 0.002 * std::exp(cv[16] * 0.6))), ampN = C(5, 3.3e3);
        const double bm = bendMul * xpMul; double fBv[5], fAv[5];
        for (int vi = 0; vi < 5; vi++) { fBv[vi] = freqOf(m, vi, false) * bm; fAv[vi] = freqOf(m, vi, true) * bm; }
        const double cutBase = cv[8] - 5;
        for (int s = 0; s < n; s++) {
            glide += (cv[21] - glide) * kG; double mix = 0;
            for (int vi = 0; vi < 5; vi++) {
                V& v = voice[vi]; const bool g = m.gate[vi];
                if (g && !v.gateWas) { v.fAttack = v.aAttack = true; } v.gateWas = g;
                if (!g) { v.fAttack = false; v.fEnv -= v.fEnv * kFR; } else if (v.fAttack) { v.fEnv += (6.5 - v.fEnv) * kFA; if (v.fEnv >= 5) { v.fEnv = 5; v.fAttack = false; } } else v.fEnv += (susF - v.fEnv) * kFD;
                if (!g) { v.aAttack = false; v.aEnv -= v.aEnv * kAR; } else if (v.aAttack) { v.aEnv += (6.5 - v.aEnv) * kAA; if (v.aEnv >= 5) { v.aEnv = 5; v.aAttack = false; } } else v.aEnv += (susA - v.aEnv) * kAD;
                if (!g && v.aEnv < 1e-4 && v.fEnv < 1e-3) continue;
                const double dB = std::min(0.45, fBv[vi] * dt); v.phB += dB; bool wrapB = false; if (v.phB >= 1) { v.phB -= 1; wrapB = true; }
                double oB = 0; if (bRamp) oB += 2 * v.phB - 1 - WmsDsp::blep(v.phB, dB); if (bTri) oB += 1 - 4 * std::abs(v.phB - 0.5);
                if (bPulse) { double pl = v.phB < pwB ? 1 : -1; pl += WmsDsp::blep(v.phB, dB); double t2 = v.phB - pwB; if (t2 < 0) t2 += 1; pl -= WmsDsp::blep(t2, dB); oB += pl * 0.75; }
                const double pm = pmEnv * (v.fEnv / 5) + pmOsc * oB; double fA = fAv[vi]; if (pmFreqA) fA *= std::pow(2.0, pm * 6);
                const double dA = std::min(0.45, fA * dt); v.phA += dA; if (v.phA >= 1) v.phA -= 1; if (sync && wrapB) v.phA = 0;
                double oA = 0; if (aRamp) oA += 2 * v.phA - 1 - WmsDsp::blep(v.phA, dA);
                if (aPulse) { const double pw = std::min(0.98, std::max(0.02, pwA + (pmPwA ? pm * 0.4 : 0))); double pl = v.phA < pw ? 1 : -1; pl += WmsDsp::blep(v.phA, dA); double t2 = v.phA - pw; if (t2 < 0) t2 += 1; pl -= WmsDsp::blep(t2, dA); oA += pl * 0.75; }
                noise = noise * 1664525u + 1013904223u; const double nz = (double)(noise >> 9) / 4194304 - 1;
                const double x = gA * oA + gB * oB + gN * nz;
                const double vf = cutBase + cv[34 + vi] + envAmt * v.fEnv * 1.6 + (pmFilt ? pm * 6 : 0) + (filtKbd ? glide : 0);
                const double fc = std::min(0.45 * fs, std::max(15.0, 20 * std::pow(2.0, vf + 5)));
                const double y = WmsDsp::cem(v.f, x * 0.5, fc, res, fs);
                mix += y * (C(v.aEnv, 3.3e3) / ampN);
            }
            out[off + s] = (float)(mix * 0.35);
        }
    }
};

// Maps keyboard notes onto a firmware synth's physical key range, shifting octaves to follow the player.
struct KeyFold {
    std::map<int, int> nk; int xp = 0;
    template <class Held, class SetXp>
    int keyOf(int n, bool on, int range, const Held& held, SetXp setXp) {
        if (!on) { auto it = nk.find(n); if (it != nk.end()) { int k = it->second; nk.erase(it); return k; } }
        int b = n - 36;
        if (on) { bool any = false; for (auto h : held) if (h) { any = true; break; }
            if (!any) { xp = b < 0 ? -(int)std::ceil(-b / 12.0) : (b >= range ? (int)std::ceil((b - range + 1) / 12.0) : 0); setXp(std::pow(2.0, xp)); } }
        int k = b - 12 * xp; while (k < 0) k += 12; while (k >= range) k -= 12;
        if (on) nk[n] = k; return k;
    }
};

struct Tap { int row, bit; double left; };

class P5R3Engine {
public:
    double sr; P5Machine m; P5Voices v; bool loaded = false, paused = false; double bootLeft = 0;
    std::array<int8_t, 61> held{}; std::vector<Tap> taps; std::vector<float> buf; int bn = 0, bi = 0; double level = 1; float scratch[64];
    KeyFold kf;
    explicit P5R3Engine(double sr_) : sr(sr_) { v.prepare(sr); m.oscFreq = [this](int i) { return v.oscFreq(m, i); }; buf.resize(4096); }
    bool load(const std::vector<uint8_t>& rom, const std::vector<uint8_t>* nv) {
        if (rom.size() != 0xC00) return false; std::copy(rom.begin(), rom.end(), m.rom);
        if (nv && nv->size() == 0x400) std::copy(nv->begin(), nv->end(), m.nvram);
        m.reset(); v.prepare(sr); loaded = true; bootLeft = 9; held.fill(0); return true;
    }
    void setSwitch(int row, int bit, bool down) { auto& r = m.switches[row]; r = (uint8_t)(down ? (r | (1 << bit)) : (r & ~(1 << bit))); }
    int keyOf(int n, bool on) { return kf.keyOf(n, on, 61, held, [this](double x) { v.xpMul = x; }); }
    void noteOn(int n) { if (!loaded) return; int k = keyOf(n, true); if (held[k]++ == 0) setSwitch(8 + (k >> 3), k & 7, true); }
    void noteOff(int n) { if (!loaded) return; int k = keyOf(n, false); if (held[k] > 0 && --held[k] == 0) setSwitch(8 + (k >> 3), k & 7, false); }
    void allOff() { kf.nk.clear(); for (int k = 0; k < 61; k++) if (held[k]) { held[k] = 0; setSwitch(8 + (k >> 3), k & 7, false); } }
    void tap(int row, int bit, double ms) { setSwitch(row, bit, true); taps.push_back({row, bit, (ms > 0 ? ms : 60) * 0.001}); }
    bool active() const { return loaded; }
    void block(int n) {
        if (!loaded || paused) { bn = 0; return; }
        if (bootLeft > 0) {
            double slice = std::min(bootLeft, 0.02);
            for (double t = 0; t < slice; t += 0.001) { m.run(0.001); v.render(m, scratch, 0, (int)std::lround(sr / 1000)); }
            bootLeft -= slice; bn = 0; return;
        }
        if ((int)buf.size() < n) buf.resize(n);
        for (int pos = 0; pos < n;) {
            int chunk = std::min(32, n - pos); double dt = chunk / sr; m.run(dt);
            for (auto& t : taps) if (t.left > 0 && (t.left -= dt) <= 0) setSwitch(t.row, t.bit, false);
            if (taps.size() > 8) taps.erase(std::remove_if(taps.begin(), taps.end(), [](const Tap& t) { return !(t.left > 0); }), taps.end());
            v.render(m, buf.data(), pos, chunk); pos += chunk;
        }
        bn = n; bi = 0;
    }
    double tick() { return bi < bn ? buf[bi++] * level : 0; }
    char digit(int i) const {
        static const int seg[13] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x7C, 0x27, 0x67}; static const char* ch = "0123456789679";
        int d = m.shownDigit(i) & 0x7F; if (d == 0) return ' ';
        for (int k = 0; k < 13; k++) if (seg[k] == d) return ch[k]; return '?';
    }
    void state(JsonOut& j) const {
        j.raw("{"); j.key("d"); std::string d; d += digit(0); d += digit(1); j.str(d);
        j.key("l0"); j.num(m.latch0); j.key("l1"); j.num(m.latch1); j.key("l2"); j.num(m.latch2);
        j.key("led"); j.raw("["); for (int i = 0; i < 7; i++) { if (i) j.raw(","); j.num(m.ledColumns[i]); } j.raw("]");
        j.key("boot"); j.raw(bootLeft > 0 ? "true" : "false"); j.key("tune"); j.raw(m.tuning ? "true" : "false"); j.raw("}");
    }
};

class SixtrakMachine : public Z80Bus, public Pit8253::Listener {
public:
    double cpuHz = 4e6, pitHz = 2e6;
    uint8_t rom[0x4000] = {}, nvA[0x1000] = {}, nvB[0x800] = {}, keys[16] = {};
    double valueKnob = 0, tuneKnob = 50, trackVolKnob = 100, speedKnob = 50, modWheel = 0, pitchWheel = 50; bool footswitch = false;
    int digitsRaw = 0, ledTrack = 0, ledBottom = 0, ledControl = 0; double fs = 48000; Z80 cpu; Pit8253 pit;
    double cycleAcc = 0, pitAcc = 0; int dac = 0, shVoices = 0x3F, param = 0, muxInput = 0, keyRow = 0, tuneLatch = 0; bool tuneCounterOut = false, tuneClockOn = false;
    double tuneHz = 0, tunePhase = 0; bool ffQ = false, ffQn = true; double muteGain = 0, modWheelV = 0, pitchWheelV = 0; uint32_t noise = 22222; bool nvDirty = false;
    double xpMul = 1;
    struct SH { double v = 0, target = 0, c = 1e-9; bool sampling = false; };
    struct Voice { double vcoCv = 0, resCv = 0, mixCv = 0, modCv = 0, pwCv = 0, waveCv = 0; SH gain, freq; double phase = 0, hp = 0, hpPrev = 0; Cem f; };
    Voice voice[6];

    SixtrakMachine() { cpu.bus = this; pit.onOut = this; }
    void pitOut(int i, bool v) override { if (i == 0) { tuneCounterOut = v; pit.setGate(1, !v); if (v) tuneClockOn = false; } if (i == 2) cpu.irqLine = v; }
    void prepare(double f) { fs = f; for (auto& v : voice) { v.f = Cem(); v.hp = 0; v.hpPrev = 0; } }
    void reset() { cpu.reset(); pit.setGate(0, false); pit.setGate(1, true); pit.setGate(2, true); tuneControl(0); cycleAcc = 0; pitAcc = 0; }
    void run(double seconds) {
        cycleAcc += seconds * cpuHz; updateWheels(seconds); const double r = pitHz / cpuHz;
        while (cycleAcc > 0) {
            int t = cpu.step(); cycleAcc -= t; pitAcc += t * r; int ticks = (int)pitAcc;
            if (ticks > 0) { pitAcc -= ticks; pit.clock(1, ticks); pit.clock(2, ticks); }
            if (tuneClockOn) { tunePhase += t / cpuHz * tuneHz; while (tunePhase >= 1) { tunePhase -= 1; clockFf(); } }
        }
    }
    static double dbVolume(double v) { double db; if (v >= 4) db = 0; else if (v >= 2.5) db = (4 - v) * 20 / 1.5; else if (v > 0) db = std::min(90.0, 20 * std::pow(2.0, 2.5 - v)); else db = 90; return std::pow(0.891251, db); }
    static double vcoFreq(const Voice& v) { return 1.3 / (5 * 301e3 * 0.002e-6) * std::pow(2.0, -v.vcoCv / 0.75); }
    static double filtFreq(const Voice& v) { return 4.3e-5 / 0.033e-6 * std::pow(2.0, -v.freq.v / 0.375); }
    void render(float* out, int off, int n) {
        const double dt = 1 / fs, hpA = std::exp(-dt / (11e3 * 10e-6));
        double kG[6], kF[6]; for (int i = 0; i < 6; i++) { kG[i] = 1 - std::exp(-dt / (1e6 * voice[i].gain.c)); kF[i] = 1 - std::exp(-dt / (1e6 * voice[i].freq.c)); }
        for (int s = 0; s < n; s++) {
            double mix = 0; noise = noise * 1664525u + 1013904223u; const double nz = (double)(noise >> 9) / 4194304 - 1;
            for (int i = 0; i < 6; i++) {
                Voice& v = voice[i];
                v.gain.v += (v.gain.target - v.gain.v) * kG[i]; v.freq.v += (v.freq.target - v.freq.v) * kF[i];
                const double fg = dbVolume(v.gain.v); if (fg < 1e-4 && std::abs(v.hp) < 1e-5) continue;
                const double f = vcoFreq(v) * xpMul, d = std::min(0.45, f * dt); v.phase += d; if (v.phase >= 1) v.phase -= 1;
                const double tri = 1 - 4 * std::abs(v.phase - 0.5), saw = 2 * v.phase - 1 - WmsDsp::blep(v.phase, d), pw = std::min(1.0, std::max(0.0, 0.5 * v.pwCv));
                double pul = (v.phase < pw ? 1 : -1) + WmsDsp::blep(v.phase, d); { double t2 = v.phase - pw; if (t2 < 0) t2 += 1; pul -= WmsDsp::blep(t2, d); }
                if (pw <= 0) pul = -1; else if (pw >= 1) pul = 1;
                const bool triOn = v.waveCv >= -0.5 && v.waveCv < 1.9, sawOn = v.waveCv >= 0.35; double inner, outer;
                if (v.mixCv >= 0) { inner = dbVolume(3.55 - v.mixCv); outer = dbVolume(3.55 + 0.45 * 0.25 * v.mixCv); } else { inner = dbVolume(3.55 - 0.45 * 0.25 * v.mixCv); outer = dbVolume(3.55 + v.mixCv); }
                const double x = 0.25 * inner * pul + (triOn ? 0.403225 * inner * tri : 0) + (sawOn ? 0.3175 * inner * saw : 0) + 0.25 * outer * nz;
                const double mm = v.modCv < 0.01 ? 0 : v.modCv > 3.5 ? 1.98 : 1.98 * (v.modCv - 0.01) / 3.49, fc0 = filtFreq(v);
                const double fc = std::min(0.45 * fs, std::max(15.0, fc0 + fc0 * 0.5 * mm * tri)), res = v.resCv < 0 ? 0 : 1.6 * v.resCv;
                const double y = WmsDsp::cem(v.f, x, fc, std::min(1.05, std::max(0.0, res / 4)), fs);
                const double hp = hpA * (v.hp + y - v.hpPrev); v.hpPrev = y; v.hp = hp; mix += hp * fg;
            }
            out[off + s] = (float)(mix * 0.8 * muteGain);
        }
    }
    int memRead(int a) override { if (a < 0x4000) return rom[a]; if (a < 0x5000) return nvA[a - 0x4000]; if (a < 0x6000) return nvB[a & 0x7FF]; if (a >= 0xE000) return (a & 1) ? 0 : 2; return 0xFF; }
    void memWrite(int a, int v) override { if (a >= 0x4000 && a < 0x5000) { nvA[a - 0x4000] = (uint8_t)v; nvDirty = true; return; } if (a >= 0x5000 && a < 0x6000) { nvB[a & 0x7FF] = (uint8_t)v; nvDirty = true; } }
    double knobV(double x) const { return x * (5 * 10000.0 / 12000) / 100; }
    double dacV() const { return dac * -4.0 / 4095; }
    double dacInvV() const { const double rp1 = 111 * 100000.0 / 255, rp2 = 100000 - rp1, vx = (2200000 * rp1 * -6.5 + 2200000 * rp2 * 5) / (2200000 * rp1 + 2200000 * rp2 + rp1 * rp2); return -3010 * (dacV() / 3010 + vx / 2200000); }
    double muxOut() const {
        switch (muxInput) { case 0: return dacV(); case 1: return dacInvV(); case 2: return knobV(valueKnob); case 3: return knobV(tuneKnob);
            case 4: return modWheelV; case 5: return knobV(trackVolKnob); case 6: return pitchWheelV; default: return knobV(speedKnob); }
    }
    void updateWheels(double dt) {
        auto W = [dt](double cur, double value, double c) { if (value <= 0) return 0.0; const double rb = value * 0.3 * 100000 / 100, rt = 100000 - rb, target = 0.00974026 * value, req = rb * (rt + 21259.84) / (rt + 21259.84 + rb);
            return cur + (target - cur) * (1 - std::exp(-dt / std::max(1e-6, req * c))); };
        pitchWheelV = W(pitchWheelV, pitchWheel, 2.2e-6); modWheelV = W(modWheelV, modWheel, 0.1e-6);
    }
    int ioRead(int port) override {
        if ((port & 0x08) == 0) return pit.read(port & 3);
        if ((port & 0x09) == 0x09) { bool compOn = (muxInput & 6) != 0; return (ffQn ? 1 : 0) | ((tuneCounterOut ? 1 : 0) << 1) | ((compOn && muxOut() >= dacInvV() ? 1 : 0) << 2) | ((footswitch ? 0 : 1) << 3); }
        if ((port & 0x0A) == 0x0A) { int d = keys[keyRow & 0x0F]; if (keyRow < 4) { int o = 0; for (int r = 0; r < 4; r++) if (r != keyRow) o |= keys[r]; d &= ~o; } return d & 0xFF; }
        return 0xFF;
    }
    void ioWrite(int port, int v) override {
        if ((port & 0x1F) == 0x18) { digitsRaw = v; return; }
        if ((port & 0x1F) == 0x08) { ledControl = v; return; }
        if ((port & 0x08) == 0) { pit.write(port & 3, v); return; }
        switch (port & 0x0F) {
            case 0x09: dac = (dac & 0x0FC0) | ((v >> 2) & 0x3F); updateCvs(); break;
            case 0x0A: dac = (dac & 0x003F) | ((v & 0x3F) << 6); updateCvs(); break;
            case 0x0B: ledTrack = v; break; case 0x0C: ledBottom = v; break;
            case 0x0D: tuneControl(v); break;
            case 0x0E: shVoices = v & 0x3F; updateCvs(); break;
            case 0x0F: param = v & 7; muxInput = (((v >> 5) & 1) << 2) | (((v >> 6) & 1) << 1) | ((v >> 7) & 1); keyRow = (v >> 4) & 0x0F; updateCvs(); break;
        }
    }
    void updateCvs() {
        const double cv = muxOut();
        for (int i = 0; i < 6; i++) {
            Voice& v = voice[i]; const bool act = !((shVoices >> i) & 1);
            sh(v.gain, act && param == 1, cv); sh(v.freq, act && param == 3, cv); if (!act) continue;
            switch (param) { case 0: v.vcoCv = cv; break; case 2: v.resCv = cv; break; case 4: v.mixCv = cv; break; case 5: v.modCv = cv; break; case 6: v.pwCv = cv; break; case 7: v.waveCv = cv; break; }
        }
    }
    static void sh(SH& r, bool sampling, double cv) {
        const double cs = 1e-9, cl = 10e-9, cser = cs * cl / (cs + cl);
        if (sampling) { r.target = cv; r.c = cs; r.sampling = true; } else if (r.sampling) { r.target = (cl * r.target + cs * r.v) / (cl + cs); r.c = cser; r.sampling = false; }
    }
    void tuneControl(int v) {
        tuneLatch = v; muteGain = (v & 1) ? 1 : 0; bool pre = !((v >> 2) & 1), clr = !((v >> 4) & 1);
        if (pre && clr) setFf(true, true); else if (clr) setFf(false, true); else if (pre) setFf(true, false);
        bool g0 = ((v >> 1) & 1) == 1; if (g0 && !pit.c[0].gate) startTuneClock(); pit.setGate(0, g0);
    }
    void startTuneClock() {
        int best = -1; double most = -1; for (int i = 0; i < 6; i++) { double g = dbVolume(voice[i].gain.v); if (g > 0.1 && g > most) { most = g; best = i; } }
        if (best < 0) { tuneClockOn = false; return; } const Voice& v = voice[best]; double res = v.resCv < 0 ? 0 : 1.6 * v.resCv;
        tuneHz = res > 3 ? filtFreq(v) : vcoFreq(v); tunePhase = 0; tuneClockOn = true;
    }
    void clockFf() { if (!((tuneLatch >> 2) & 1) || !((tuneLatch >> 4) & 1)) return; setFf(ffQn, ffQ); }
    void setFf(bool q, bool qn) { bool falling = ffQn && !qn, rising = !ffQn && qn; ffQ = q; ffQn = qn; if (rising) pit.clockRise(0); if (falling) pit.clock(0, 1, true); }
};

class SixtrakEngine {
public:
    double sr; SixtrakMachine m; bool loaded = false, paused = false; std::array<int8_t, 49> held{}; std::vector<Tap> taps; std::vector<float> buf; int bn = 0, bi = 0; double level = 5;
    struct BootStep { double secs; int action; }; std::vector<BootStep> boot; double bootT = 0; float scratch[64]; KeyFold kf;
    explicit SixtrakEngine(double sr_) : sr(sr_) { m.prepare(sr); buf.resize(4096); }
    bool load(const std::vector<uint8_t>& rom, const std::vector<uint8_t>* a, const std::vector<uint8_t>* b) {
        if (rom.size() != 0x4000) return false; std::copy(rom.begin(), rom.end(), m.rom);
        if (a && a->size() == 0x1000) std::copy(a->begin(), a->end(), m.nvA);
        if (b && b->size() == 0x800) std::copy(b->begin(), b->end(), m.nvB);
        m.prepare(sr); m.reset(); loaded = true; held.fill(0);
        boot = {{1, 0}, {0.1, 1}, {0.15, 2}, {12, 3}}; bootT = 0; return true;
    }
    void bootAction(int a) {
        if (a == 1) m.keys[1] |= 1 << 5; else if (a == 2) m.keys[0] |= 1 << 6;
        else if (a == 3) { m.keys[0] &= (uint8_t)~(1 << 6); m.keys[1] &= (uint8_t)~(1 << 5); }
    }
    int keyOf(int n, bool on) { return kf.keyOf(n, on, 49, held, [this](double x) { m.xpMul = x; }); }
    void setKey(int row, int bit, bool down) { auto& r = m.keys[row]; r = (uint8_t)(down ? (r | (1 << bit)) : (r & ~(1 << bit))); }
    void noteOn(int n) { if (!loaded) return; int k = keyOf(n, true); if (held[k]++ == 0) setKey(8 + (k >> 3), k & 7, true); }
    void noteOff(int n) { if (!loaded) return; int k = keyOf(n, false); if (held[k] > 0 && --held[k] == 0) setKey(8 + (k >> 3), k & 7, false); }
    void allOff() { kf.nk.clear(); for (int k = 0; k < 49; k++) if (held[k]) { held[k] = 0; setKey(8 + (k >> 3), k & 7, false); } }
    void tap(int row, int bit, double ms, int holdRow, int holdBit) {
        double msv = ms > 0 ? ms : 80;
        if (holdRow >= 0) { setKey(holdRow, holdBit, true); taps.push_back({holdRow, holdBit, (msv + 60) * 0.001}); }
        setKey(row, bit, true); taps.push_back({row, bit, msv * 0.001});
    }
    bool active() const { return loaded; }
    void block(int n) {
        if (!loaded || paused) { bn = 0; return; }
        if (!boot.empty()) {
            int ms = (int)std::lround(sr / 1000);
            for (int i = 0; i < 20 && !boot.empty(); i++) {
                if (bootT == 0 && boot[0].action) bootAction(boot[0].action);
                m.run(0.001); m.render(scratch, 0, ms); bootT += 0.001;
                if (bootT >= boot[0].secs) { boot.erase(boot.begin()); bootT = 0; }
            }
            bn = 0; return;
        }
        if ((int)buf.size() < n) buf.resize(n);
        for (int pos = 0; pos < n;) {
            int chunk = std::min(32, n - pos); double dt = chunk / sr; m.run(dt);
            for (auto& t : taps) if (t.left > 0 && (t.left -= dt) <= 0) setKey(t.row, t.bit, false);
            if (taps.size() > 8) taps.erase(std::remove_if(taps.begin(), taps.end(), [](const Tap& t) { return !(t.left > 0); }), taps.end());
            m.render(buf.data(), pos, chunk); pos += chunk;
        }
        bn = n; bi = 0;
    }
    double tick() { return bi < bn ? buf[bi++] * level : 0; }
    void state(JsonOut& j) const {
        int d = m.digitsRaw, a = d & 15, b = d >> 4; std::string s; s += a <= 9 ? (char)('0' + a) : ' '; s += b <= 9 ? (char)('0' + b) : ' ';
        j.raw("{"); j.key("d"); j.str(s); j.key("led"); j.raw("["); j.num(m.ledTrack); j.raw(","); j.num(m.ledBottom); j.raw(","); j.num(m.ledControl); j.raw("]");
        j.key("boot"); j.raw(boot.empty() ? "false" : "true"); j.raw("}");
    }
};

} // namespace farp
