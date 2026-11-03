// Omega CMI engine: sixteen CMI-01A channel cards (eight for the keyboard, eight for the Page R tracks), the
// software that drives them (what the CMI's 6809s do: pitch and filter register writes, vibrato, portamento,
// envelope clock rates, segment stepping) and the Page R pattern sequencer.
#pragma once
#include "Cmi.h"

namespace cmi {

constexpr int MAX_SOUNDS = 256;
constexpr int KEY_CH = 8, TRK_CH = 8, NCH = KEY_CH + TRK_CH;
constexpr int STEPS = 16, TRACKS = 8, NPAT = 16, SONG_MAX = 64;

// the sound library: slots hold pointers that are only ever swapped, never freed while the engine runs
struct Bank {
    std::array<std::atomic<const Sound*>, MAX_SOUNDS> slot {};
    std::atomic<int> count { 0 };
    const Sound* at(int i) const { return i >= 0 && i < count.load() ? slot[(size_t) i].load() : nullptr; }
};

// ---------------------------------------------------------------------------------------------------------------
// One channel card and the CPU's per-channel state
// ---------------------------------------------------------------------------------------------------------------
class Channel {
public:
    enum State { Idle, Playing, Releasing, Stealing };

    void prepare(double fs_, double mf_) { fs = fs_; mf = mf_; tickLen = std::max(1, (int) std::lround(fs / 1000.0)); flt.lastF = -1; reset(); }
    void reset() {
        state = Idle; run = false; cur = 0; env = 0; envClk = false; flt.reset(); dc = 0;
        std::fill(std::begin(corr), std::end(corr), 0.f); std::fill(std::begin(hist), std::end(hist), 0.f);
    }

    // a key: starts at once on a free card, or after the card has faded its current sound (at the next zero cross)
    void noteOn(const Sound* s, const Controls& c, int key, float velocity, double glideFrom, uint64_t stamp) {
        if (!s) return;
        pend = { s, c, key, velocity, glideFrom, true };
        age = stamp; gate = true; note = key;
        if (state == Idle || env == 0) startPending();
        else { state = Stealing; up = false; setEclk(0.003, false); envClk = true; }
    }
    void noteOff() {
        gate = false;
        if (state == Stealing) { pend.gate = false; return; }
        if (state == Playing && !held) release();
    }
    void setHeld(bool h) { held = h; if (!h && !gate && state == Playing) release(); }
    void kill() { if (state == Idle || !snd) return; pend.s = nullptr; state = Releasing; up = false; setEclk(0.004, false); envClk = true; }
    void setControls(const Controls& c) { ctl = c; }
    void setBend(double semis) { bend = semis; }
    void setMod(double m) { mod = m; }

    State st() const { return state; }
    int curNote() const { return note; }
    bool isGate() const { return gate; }
    uint64_t stamp() const { return age; }
    int envLevel() const { return env; }
    int page() const { return msb; }
    const Sound* sound() const { return snd; }
    double pitchNow() const { return pitch; }

    // adds this card into out[0..n)
    void render(float* out, int n) {
        if (state == Idle) return;
        const BlepTable& bt = BlepTable::get();
        for (int i = 0; i < n; ++i) {
            if (--tick <= 0) { tick = tickLen; cpuTick(); if (state == Idle) return; }
            // the channel clock: each edge loads the next RAM byte into the DAC
            phase += inc;
            while (phase >= 1.0) {
                phase -= 1.0;
                int s = cur;
                if (run) {
                    const int a = ((msb << 7) | lsb) & 0x3FFF;
                    s = a < (int) snd->ram.size() ? (int) snd->ram[(size_t) a] - 128 : 0;
                    if (++lsb == PAGE) { lsb = 0; pageEnd(); }
                } else s = 0;
                if (s != cur) {
                    const double frac = inc > 0 ? std::min(phase / inc, 0.999999) : 0;
                    const double p = frac * BL_PH; const int i0 = (int) p; const float f = (float) (p - i0);
                    const float d = (float) (s - cur);
                    for (int k = 0; k < BL_TAPS; ++k) corr[(ci + (unsigned) k) & 31] += d * (bt.r[i0][k] + (bt.r[i0 + 1][k] - bt.r[i0][k]) * f);
                    if ((s < 0) != (cur < 0) || s == 0) zeroCross();
                    cur = s;
                }
                if (!run) break;
            }
            hist[hi & 31] = (float) cur;
            const double dac = hist[(hi - BL_HALF) & 31] + corr[ci & 31];
            corr[ci & 31] = 0; ++ci; ++hi;
            // envelope counter
            if (envClk) clockEnvelope();
            // filter, then the two multiplying DACs
            const double y = flt.run(dac);
            out[i] += (float) (y * (env * vol) * (1.0 / (255.0 * 255.0 * 128.0)));
            if (state == Idle) return;
        }
    }

private:
    struct Pending { const Sound* s = nullptr; Controls c; int note = 60; float vel = 1; double glideFrom = -1; bool gate = true; };

    void startPending() {
        snd = pend.s; ctl = pend.c; note = pend.note; vel = pend.vel; gate = pend.gate;
        target = note; pitch = (pend.glideFrom >= 0 && ctl.porta > 0.0005f) ? pend.glideFrom : note;
        t = 0; ws = 0; lsb = 0; msb = 0; phase = 1.0; run = true; ended = false;
        env = 0; up = true; state = Playing;
        setEclk(envTime(ctl.attack), true); envClk = true;
        flt.reset(); tick = 0;
        if (!gate) release();
    }

    void release() {
        if (state == Idle) return;
        state = Releasing; up = false;
        const double tm = envTime(ctl.damp);
        if (ctl.damp <= 0.01f) { zxKill = true; setEclk(0.002, false); }
        else setEclk(tm, false);
        envClk = true;
    }

    void setEclk(double seconds, bool rising) { eclk = envSweepClocks(rising) / std::max(seconds, 1e-4) / fs; }

    void clockEnvelope() {
        envAcc += eclk * envRate(env, up) / 64.0;
        int guard = 0;
        while (envAcc >= 1.0 && guard++ < 256) {
            envAcc -= 1.0;
            if (up) {
                if (env < 255) ++env;
                if (env >= 255) { envAcc = 0; atTop(); return; }
            } else {
                if (env > 0) --env;
                if (env <= 0) { envAcc = 0; atBottom(); return; }
            }
        }
    }
    void atTop() {
        envClk = false;
        if (state == Playing && ctl.sustain < 0.5f) {   // no sustain: damping begins at the peak
            up = false; setEclk(envTime(ctl.damp), false); envClk = true;
        }
    }
    void atBottom() {
        envClk = false;
        if (state == Stealing) { startPending(); return; }
        if (state == Releasing || state == Playing) finish();
    }
    void finish() { state = Idle; run = false; cur = 0; flt.reset(); std::fill(std::begin(corr), std::end(corr), 0.f); std::fill(std::begin(hist), std::end(hist), 0.f); }

    void zeroCross() { if (zxKill) { zxKill = false; env = 0; atBottom(); } }

    // the address counter rolled over into the next page
    void pageEnd() {
        if (snd->mode == Mode::Waveform) { msb = ws; return; }   // the page register is reloaded every cycle
        ++msb;
        const bool looping = ctl.loop > 0.5f && snd->loopStart >= 0 && snd->loopEnd >= snd->loopStart;
        if (looping && msb > snd->loopEnd) msb = snd->loopStart;
        else if (msb >= snd->pages()) { run = false; ended = true; if (state != Idle) { state = Releasing; up = false; env = 0; atBottom(); } }
    }

    // ~1 kHz: the CPU's real-time loop for this channel
    void cpuTick() {
        const double dt = tickLen / fs;
        t += dt;
        // portamento: constant time per glide, as the CMI does it
        if (pitch != target) {
            const double sp = ctl.porta > 0.0005f ? dt / ctl.porta * 12.0 : 1e9;
            pitch = pitch < target ? std::min(target, pitch + sp) : std::max(target, pitch - sp);
        }
        double vib = 0;
        const double depth = ctl.vibDepth + mod;
        if (depth > 0.0005) {
            vphase += ctl.vibRate * dt; vphase -= std::floor(vphase);
            const double fade = ctl.vibDelay > 0.001 ? clampd((t - ctl.vibDelay) / std::max(0.05, (double) ctl.vibDelay), 0, 1) : 1;
            vib = depth * fade * (vphase < 0.5 ? 4 * vphase - 1 : 3 - 4 * vphase);   // triangle
        }
        const double semis = pitch - snd->root + ctl.transpose + ctl.fine / 100.0 + bend + vib;
        regs = pitchRegsFor(snd->rate * std::pow(2.0, semis / 12.0), mf);
        inc = regs.clock / fs;
        // filter DAC: the octave register plus the latch
        const int latch = clampi((int) std::lround(ctl.filter - (1.0 - vel) * ctl.velFilt * 120.0), 0, 255);
        flt.set(clampi((regs.oct << 5) + latch, 0, 511), fs);
        vol = clampi((int) std::lround(255.0 * ctl.level * (1.0 - ctl.velVol * (1.0 - vel))), 0, 255);
        if (snd->mode == Mode::Waveform) {
            int seg = (int) (t / std::max(0.002f, ctl.segTime));
            const int last = snd->pages() - 1;
            if (snd->loopStart >= 0 && ctl.loop > 0.5f && seg > snd->loopEnd) {
                const int span = snd->loopEnd - snd->loopStart + 1;
                seg = snd->loopStart + (seg - snd->loopStart) % span;
            }
            ws = clampi(seg, 0, last);
        }
    }

    // card
    const Sound* snd = nullptr; Controls ctl; SSM2045 flt; PitchRegs regs;
    bool run = false, ended = false, envClk = false, up = true, zxKill = false;
    int lsb = 0, msb = 0, ws = 0, cur = 0, env = 0, vol = 0;
    double phase = 0, inc = 0, eclk = 0, envAcc = 0;
    float corr[32] {}, hist[32] {}; unsigned ci = 0, hi = 0;
    // cpu
    State state = Idle; Pending pend;
    double fs = 48000, mf = masterFreq(), t = 0, pitch = 60, target = 60, bend = 0, mod = 0, vphase = 0, dc = 0;
    int tick = 0, tickLen = 48, note = -1; float vel = 1; bool gate = false, held = false; uint64_t age = 0;
};

// ---------------------------------------------------------------------------------------------------------------
// Page R
// ---------------------------------------------------------------------------------------------------------------
struct PStep { uint8_t vel = 0; int8_t note = 0; };      // vel 0: no event; note: semitones from the sound's root
struct Pattern { PStep st[TRACKS][STEPS]; };
struct Track { int sound = 0; float level = 0.85f; bool audible = true; float pan = 0.5f; };

struct SeqData {
    Pattern pat[NPAT];
    Track trk[TRACKS];
    int song[SONG_MAX] {}; int songLen = 1;
    float swing = 0;   // 0..0.5 of a sixteenth
};

struct Transport { bool valid = false, playing = false; double ppq = 0, bpm = 120; };

// ---------------------------------------------------------------------------------------------------------------
// the machine
// ---------------------------------------------------------------------------------------------------------------
class Engine {
public:
    Bank* bank = nullptr;
    SeqData seq;

    // set from the host or the app's UI
    std::atomic<int> curPattern { 0 };
    std::atomic<bool> songMode { false }, extSync { true }, seqRun { false };
    std::atomic<double> tempo { 120 };
    std::atomic<int> seqStep { -1 }, seqPat { 0 }, seqBar { 0 };     // where Page R is (for the screen)

    void prepare(double sampleRate) {
        fs = sampleRate;
        for (auto& c : ch) c.prepare(fs, mf);
        hpA = std::exp(-2 * PI * 7.0 / fs);
        gpos = 0; lastStep = -1;
    }

    // keyboard instrument (call on the audio thread before render)
    void setKeyboard(int soundIndex, const Controls& c) {
        kbSound = soundIndex; kb = c;
        for (int i = 0; i < KEY_CH; ++i) ch[(size_t) i].setControls(kb);
    }
    void setPoly(int n) { poly = clampi(n, 1, KEY_CH); }
    void setBendRange(double s) { bendRange = s; }
    void setOutput(double vol, double hissAmt, double spreadAmt) { master = vol; hiss = hissAmt; spread = spreadAmt; }
    void setMultiTimbral(bool on) { multi = on; }

    // MIDI: true if handled
    bool midi(const uint8_t* m, int len) {
        if (len < 1) return false;
        const int st = m[0] & 0xF0, chn = m[0] & 0x0F;
        const bool toTrack = multi && chn >= 1 && chn <= TRACKS;
        if (st == 0x90 && len >= 3 && m[2] > 0) {
            if (toTrack) trackNote(chn - 1, m[1] - 60, m[2] / 127.f);
            else keyOn(m[1], m[2] / 127.f);
            return true;
        }
        if ((st == 0x80 || (st == 0x90 && len >= 3 && m[2] == 0)) && len >= 3) {
            if (toTrack) trackOff(chn - 1); else keyOff(m[1]);
            return true;
        }
        if (st == 0xE0 && len >= 3) {
            const double v = ((m[2] << 7) | m[1]) - 8192;
            const double b = v / 8192.0 * bendRange;
            for (int i = 0; i < KEY_CH; ++i) ch[(size_t) i].setBend(b);
            return true;
        }
        if (st == 0xB0 && len >= 3) {
            if (m[1] == 1) { for (int i = 0; i < KEY_CH; ++i) ch[(size_t) i].setMod(m[2] / 127.0); return true; }
            if (m[1] == 64) { sustainPedal(m[2] >= 64); return true; }
            if (m[1] == 120 || m[1] == 123) { allOff(m[1] == 120); return true; }
            return false;
        }
        return false;
    }

    void keyOn(int note, float vel) {
        const Sound* s = bank ? bank->at(kbSound) : nullptr;
        if (!s) return;
        // same key again: reuse its card
        int pick = -1;
        for (int i = 0; i < poly; ++i) if (ch[(size_t) i].st() != Channel::Idle && ch[(size_t) i].curNote() == note) { pick = i; break; }
        if (pick < 0) for (int i = 0; i < poly; ++i) if (ch[(size_t) i].st() == Channel::Idle) { pick = i; break; }
        if (pick < 0) {   // oldest released card, then the oldest card
            uint64_t best = UINT64_MAX;
            for (int i = 0; i < poly; ++i) if (ch[(size_t) i].st() == Channel::Releasing && ch[(size_t) i].stamp() < best) { best = ch[(size_t) i].stamp(); pick = i; }
            if (pick < 0) for (int i = 0; i < poly; ++i) if (ch[(size_t) i].stamp() < best) { best = ch[(size_t) i].stamp(); pick = i; }
        }
        if (pick < 0) return;
        const double from = (kb.porta > 0.0005f && lastKey >= 0) ? lastKey : -1;
        ch[(size_t) pick].noteOn(s, kb, note, vel, from, ++stampN);
        ch[(size_t) pick].setHeld(pedal);
        lastKey = note;
    }
    void keyOff(int note) {
        for (int i = 0; i < KEY_CH; ++i) if (ch[(size_t) i].isGate() && ch[(size_t) i].curNote() == note) ch[(size_t) i].noteOff();
    }
    void sustainPedal(bool on) { pedal = on; for (int i = 0; i < KEY_CH; ++i) ch[(size_t) i].setHeld(on); }
    void allOff(bool hard) {
        for (auto& c : ch) { if (hard) c.kill(); else c.noteOff(); }
        for (auto& g : gateEnd) g = -1;
    }

    // a Page R track (its own card)
    void trackNote(int t, int semis, float vel) {
        if (t < 0 || t >= TRACKS) return;
        const Track& tr = seq.trk[t];
        const Sound* s = bank ? bank->at(tr.sound) : nullptr;
        if (!s || !tr.audible) return;
        Controls c = s->ctl; c.level = std::min(1.f, c.level * tr.level / 0.85f);
        ch[(size_t) (KEY_CH + t)].noteOn(s, c, s->root + semis, vel, -1, ++stampN);
    }
    void trackOff(int t) { if (t >= 0 && t < TRACKS) ch[(size_t) (KEY_CH + t)].noteOff(); }

    // the screen: what each card is doing
    struct ChView { int state, env, page, note; };
    ChView view(int i) const { const Channel& c = ch[(size_t) i]; return { (int) c.st(), c.envLevel(), c.page(), c.curNote() }; }
    int activeVoices() const { int n = 0; for (auto& c : ch) n += c.st() != Channel::Idle; return n; }

    void render(float* L, float* R, int n, const Transport* tp = nullptr) {
        int done = 0;
        while (done < n) {
            const int len = seqAdvance(n - done, tp, done);
            renderRaw(L + done, R + done, len);
            done += len;
        }
    }

private:
    // runs Page R up to the next step boundary; returns how many samples to render before it
    int seqAdvance(int maxLen, const Transport* tp, int offset) {
        const bool ext = extSync.load() && tp && tp->valid;
        const bool running = ext ? tp->playing : seqRun.load();
        const int L = STEPS;
        if (!running) {
            if (wasRunning) { for (int t = 0; t < TRACKS; ++t) { trackOff(t); gateEnd[t] = -1; } wasRunning = false; seqStep = -1; }
            if (!ext) gpos = 0;
            lastStep = -1;
            return maxLen;
        }
        const double bpm = ext ? (tp->bpm > 0 ? tp->bpm : 120) : clampd(tempo.load(), 20, 400);
        const double stepsPerSample = bpm / 60.0 * 4.0 / fs;
        if (ext) gpos = tp->ppq * 4.0 + offset * stepsPerSample;
        // starting, or the host jumped (loop, locate): pick up at the next step
        if (!wasRunning || gpos < (double) lastStep - 0.01 || gpos > (double) lastStep + 2.5) {
            if (wasRunning) for (int t = 0; t < TRACKS; ++t) { trackOff(t); gateEnd[t] = -1; }
            wasRunning = true; lastStep = (long long) std::ceil(gpos - 1e-9) - 1;
        }

        // next step boundary (with swing on the off sixteenths)
        const long long next = lastStep + 1;
        const double sw = (next & 1) ? seq.swing * 0.5 : 0.0;
        const double fireAt = (double) next + sw;
        if (gpos >= fireAt - 1e-9) {
            fireStep(next, L);
            lastStep = next;
        }
        const double rem = ((double) (lastStep + 1) + (((lastStep + 1) & 1) ? seq.swing * 0.5 : 0.0)) - gpos;
        int len = (int) std::ceil(rem / stepsPerSample);
        len = clampi(len, 1, maxLen);
        // gates that end within this chunk
        for (int t = 0; t < TRACKS; ++t) if (gateEnd[t] >= 0 && gpos + len * stepsPerSample >= gateEnd[t]) {
            const int g = (int) std::ceil((gateEnd[t] - gpos) / stepsPerSample);
            if (g <= 0) { trackOff(t); gateEnd[t] = -1; } else len = std::min(len, g);
        }
        if (!ext) gpos += len * stepsPerSample;
        return len;
    }

    void fireStep(long long g, int L) {
        if (g < 0) return;
        const long long bar = g / L; const int step = (int) (g % L);
        int p = curPattern.load();
        if (songMode.load() && seq.songLen > 0) p = seq.song[(size_t) (bar % seq.songLen)];
        p = clampi(p, 0, NPAT - 1);
        seqPat = p; seqStep = step; seqBar = (int) (songMode.load() && seq.songLen > 0 ? bar % seq.songLen : bar);
        for (int t = 0; t < TRACKS; ++t) {
            const PStep& s = seq.pat[p].st[t][step];
            if (s.vel == 0) continue;
            trackNote(t, s.note, s.vel / 127.f);
            gateEnd[t] = (double) g + 0.9;
        }
    }

    void renderRaw(float* L, float* R, int n) {
        if ((int) mono.size() < n) { mono.resize((size_t) n); side.resize((size_t) n); }
        float* kbL = L; float* kbR = R;
        std::fill(kbL, kbL + n, 0.f); std::fill(kbR, kbR + n, 0.f);
        for (int i = 0; i < NCH; ++i) {
            Channel& c = ch[(size_t) i];
            if (c.st() == Channel::Idle) continue;
            std::fill(mono.begin(), mono.begin() + n, 0.f);
            c.render(mono.data(), n);
            double pan;
            if (i < KEY_CH) pan = 0.5 + ((i & 1) ? 0.5 : -0.5) * spread * (0.4 + 0.6 * (i >> 1) / 3.0);
            else pan = seq.trk[i - KEY_CH].pan;
            const float gl = (float) std::cos(pan * PI / 2) * 1.41421356f, gr = (float) std::sin(pan * PI / 2) * 1.41421356f;
            for (int k = 0; k < n; ++k) { kbL[k] += mono[(size_t) k] * gl; kbR[k] += mono[(size_t) k] * gr; }
        }
        // output stage: coupling capacitor, summing amplifier, noise floor, level
        const double g = 0.5 * master;
        for (int k = 0; k < n; ++k) {
            const double nz = hiss > 0 ? (rnd() - 0.5) * hiss * 0.0006 : 0;
            double l = kbL[k] * g, r = kbR[k] * g;
            hlx = hpA * (hlx + l - lpx); lpx = l; l = hlx;
            hrx = hpA * (hrx + r - rpx); rpx = r; r = hrx;
            kbL[k] = (float) (soft(l) + nz);
            kbR[k] = (float) (soft(r) + nz);
        }
    }

    // the summing amplifier: clean to 0.6, then rounding into its rails at 1
    static double soft(double x) { const double a = std::fabs(x); return a < 0.6 ? x : std::copysign(0.6 + 0.4 * std::tanh((a - 0.6) / 0.4), x); }
    double rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs * (1.0 / 4294967296.0); }

    std::array<Channel, NCH> ch;
    std::vector<float> mono, side;
    double fs = 48000, mf = masterFreq(), master = 1, hiss = 1, spread = 0.3, bendRange = 2, hpA = 0.999;
    double hlx = 0, lpx = 0, hrx = 0, rpx = 0;
    uint32_t rs = 0x1234567u;
    int kbSound = 0, poly = KEY_CH, lastKey = -1; Controls kb; bool pedal = false, multi = false;
    uint64_t stampN = 0;
    // sequencer
    double gpos = 0; long long lastStep = -1; bool wasRunning = false;
    double gateEnd[TRACKS] { -1, -1, -1, -1, -1, -1, -1, -1 };
};

}  // namespace cmi
