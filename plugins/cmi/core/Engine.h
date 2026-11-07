// Omega CMI engine: sixteen CMI-01A channel cards (eight for the keyboard, eight for the Page R tracks), the
// software that drives them (what the CMI's 6809s do: pitch and filter register writes, vibrato, portamento,
// envelope clock rates, segment stepping) and the Page R pattern sequencer.
#pragma once
#include "Cmi.h"
#include "Effects.h"
#include <functional>

namespace cmi {

constexpr int MAX_SOUNDS = 256;
constexpr int NO_VOICE = MAX_SOUNDS - 1;   // a silent voice, always empty: a track with no voice plays it
constexpr int STEPS = 16, TRACKS = 8, NPAT = 16, SONG_MAX = 64;
// cards: 8 for the keyboard, then two per track (Page R uses the first, a MIDI song both)
constexpr int SONG_TRK = 16;   // a MIDI song: one track per MIDI channel
// cards: 8 for the keyboard, then 32 shared by Page R (one per track, the first 8) and MIDI songs (any free one)
constexpr int KEY_CH = 8, TRK_CH = 8, SONG_CH = 32, NCH = KEY_CH + SONG_CH;

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
    void setBlend(float b) { blend = b; }

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
                    s = ramAt(msb, lsb);
                    if (blend > 0 && snd->mode == Mode::Sample) s = blended(s);
                    if (fadeLeft > 0) --fadeLeft;
                    if (++lsb == PAGE) { lsb = 0; pageEnd(); checkEndFade(); }
                } else s = cur;   // stopped: the DAC holds its last value while the envelope closes
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
            double fg = 1;
            if (fadeLeft >= 0) { const double u = fadeTotal > 0 ? (double) fadeLeft / fadeTotal : 0; fg = std::sin(u * PI / 2); fg *= fg; }
            out[i] += (float) (y * (env * vol) * fg * (1.0 / (255.0 * 255.0 * 128.0)));
            if (state == Idle) return;
        }
    }

private:
    struct Pending { const Sound* s = nullptr; Controls c; int note = 60; float vel = 1; double glideFrom = -1; bool gate = true; };

    void startPending() {
        snd = pend.s; ctl = pend.c; note = pend.note; vel = pend.vel; gate = pend.gate;
        target = note; pitch = (pend.glideFrom >= 0 && ctl.porta > 0.0005f) ? pend.glideFrom : note;
        t = 0; ws = 0; lsb = 0; msb = 0; phase = 1.0; run = true; ended = false; fadeLeft = -1; fadeTotal = 0;
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
    // the end of a one-shot sound (most were cut off at 16 KB mid-wave): the last 40 ms fade to exactly nothing,
    // whatever the pitch, so the sound never stops on a step
    void checkEndFade() {
        if (fadeLeft >= 0 || snd->mode != Mode::Sample || (ctl.loop > 0.5f && snd->loopStart >= 0)) return;
        const int remaining = (snd->pages() - msb) * PAGE - lsb;
        if (remaining <= 0.04 * regs.clock || remaining <= PAGE) fadeTotal = fadeLeft = std::max(1, remaining);
    }
    int ramAt(int page, int i) const { const int a = ((page << 7) | i) & 0x3FFF; return a < (int) snd->ram.size() ? (int) snd->ram[(size_t) a] - 128 : 0; }
    // Loop blend (not on the original card, which just reloads its page counter at the loop end): a crossfade loop.
    // The last X samples of the loop fade into the X samples that lead into where the loop restarts, so the seam is
    // gone. With room before the loop start (X samples), the loop keeps its length and its page-boundary jump; with
    // none, the restart moves X samples into the loop (the loop is X shorter) and the end fades into its first X.
    struct LoopGeom { int start = 0, end = 0, len = 0, x = 0; bool shrink = false; };
    LoopGeom loopGeom() const {
        LoopGeom g;
        if (!(ctl.loop > 0.5f && snd->loopStart >= 0 && snd->loopEnd >= snd->loopStart)) return g;
        g.start = snd->loopStart * PAGE; g.end = std::min((snd->loopEnd + 1) * PAGE, snd->pages() * PAGE); g.len = g.end - g.start;
        g.x = (int) (blend * 0.5f * g.len);
        if (g.x < 16) { g.x = 0; return g; }
        g.shrink = g.start < g.x;
        return g;
    }
    int blended(int s) const {
        const LoopGeom g = loopGeom();
        if (g.x == 0) return s;
        const int pos = (msb << 7) | lsb, from = g.end - g.x;
        if (pos < from || pos >= g.end) return s;
        const double w = (pos - from + 0.5) / g.x;   // 0 -> 1 across the fade
        const int src = pos - g.len + (g.shrink ? g.x : 0);
        const int o = ramAt(src >> 7, src & (PAGE - 1));
        const double a = std::cos(w * PI * 0.5), b = std::sin(w * PI * 0.5);   // equal power
        return clampi((int) std::lround(s * a + o * b), -128, 127);
    }
    void pageEnd() {
        if (snd->mode == Mode::Waveform) { msb = ws; return; }   // the page register is reloaded every cycle
        ++msb;
        const bool looping = ctl.loop > 0.5f && snd->loopStart >= 0 && snd->loopEnd >= snd->loopStart;
        if (looping && msb > snd->loopEnd) {
            msb = snd->loopStart;   // the card: the page counter goes back to the loop's first page
            const LoopGeom g = blend > 0 ? loopGeom() : LoopGeom();
            if (g.shrink) { msb += g.x / PAGE; lsb = g.x % PAGE; }   // a blended loop with no room before it restarts X in
        } else if (msb >= snd->pages()) {
            run = false; ended = true;
            if (state != Idle) { state = Releasing; up = false; setEclk(0.003, false); envClk = true; if (env == 0) atBottom(); }
        }
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
    int fadeLeft = -1, fadeTotal = 0;
    float blend = 0;   // loop blend, 0 (the card as it was) .. 1, set by the engine
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
struct Track { int sound = NO_VOICE; float level = 0.85f; bool audible = true; float pan = 0.5f; };

struct SeqData {
    Pattern pat[NPAT];
    Track trk[TRACKS];
    int song[SONG_MAX] {}; int songLen = 1;
    float swing = 0;   // 0..0.5 of a sixteenth
};

struct Transport { bool valid = false, playing = false; double ppq = 0, bpm = 120; };

// A MIDI file as the composer plays it: every channel becomes one of the eight tracks, each on its own pair of
// cards with its own CMI voice (Song.h reads Standard MIDI Files into this).
struct SongEvent { double beat; uint8_t track, status, d1, d2; };
struct MidiSong {
    std::string name;
    std::vector<SongEvent> ev;                       // in time order
    std::vector<std::pair<double, double>> tempo;    // (beat, quarter-note BPM), from beat 0
    double length = 0;                               // beats
    double minLength = 0;                            // a song written from scratch keeps its bars even when empty
    int num = 4, den = 4;
    std::string trackName[SONG_TRK];
    int program[SONG_TRK] {};                        // General MIDI program, -1 for a drum channel
    bool used[SONG_TRK] {};
    std::vector<uint8_t> raw;                        // the file, to save it with a project
    uint32_t id = 0;                                 // the same for every edited version of one song
    double bpmAt(double beat) const {
        double b = 120;
        for (const auto& t : tempo) { if (t.first > beat + 1e-9) break; b = t.second; }
        return b;
    }
};
constexpr int GM_KIT = -2;   // a song track's voice: the drum kit (a voice per note)

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

    // MIDI songs: the one selected plays instead of the Page R patterns
    std::atomic<const MidiSong*> song { nullptr };
    std::atomic<bool> songRun { false }, songLoop { false };
    std::atomic<double> songBeat { 0 };                    // for the screen
    int songVoice[SONG_TRK] {};                            // a sound index, or GM_KIT
    bool songMute[SONG_TRK] {};
    std::atomic<double> songGain { 0.5 };
    std::atomic<float> loopBlend { 0.5f };   // crossfade at loop points, 0 (the original card: none) .. 1                // a whole band on 32 cards: headroom for the summing amp

    // the mixer: a fader (0..1.5), a pan (-1: as the MIDI file / the track says) and solo per strip, meters (peaks)
    // strips: 0 keyboard, 1..8 Page R tracks, 9..24 song tracks
    static constexpr int STRIPS = 1 + TRACKS + SONG_TRK;
    std::array<std::atomic<float>, STRIPS> fader, panOver, meter;
    std::array<std::atomic<bool>, STRIPS> solo, mute;
    std::atomic<float> meterL { 0 }, meterR { 0 };
    // the effects section: a REV7 on an aux send; each strip's send level (post fader and pan)
    Rev7Fx fx;
    std::atomic<bool> fxOn { true };
    std::array<std::atomic<float>, STRIPS> fxSend;
    std::atomic<float> fxMeter { 0 };
    Engine() { for (int i = 0; i < STRIPS; ++i) { fader[(size_t) i] = 1; panOver[(size_t) i] = -1; meter[(size_t) i] = 0; solo[(size_t) i] = false; mute[(size_t) i] = false; fxSend[(size_t) i] = 0.15f; } }
    // the effects settings, to carry over to an export's own engine
    struct FxSettings { bool on = true; std::array<float, STRIPS> send {}; std::string state; };
    FxSettings fxSettings() const { FxSettings f; f.on = fxOn.load(); for (int i = 0; i < STRIPS; ++i) f.send[(size_t) i] = fxSend[(size_t) i].load(); f.state = fx.save(); return f; }
    void applyFx(const FxSettings& f) { fxOn = f.on; for (int i = 0; i < STRIPS; ++i) fxSend[(size_t) i] = f.send[(size_t) i]; fx.load(f.state); }
    int stripOf(int card) const { return card < KEY_CH ? 0 : curSong ? 1 + TRACKS + clampi(cardTrack[card], 0, SONG_TRK - 1) : 1 + (card - KEY_CH) % TRACKS; }
    std::function<int(int note)> kitVoice;                 // GM drum note -> sound index

    void prepare(double sampleRate) {
        fs = sampleRate;
        for (auto& c : ch) c.prepare(fs, mf);
        fx.prepare(fs, 4096);
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
            const int len = song.load() || curSong ? songAdvance(n - done, tp, done) : seqAdvance(n - done, tp, done);
            renderRaw(L + done, R + done, len);
            done += len;
        }
    }

private:
    // ---- the MIDI song player ----
    void songAllOff() { for (int i = KEY_CH; i < NCH; ++i) ch[(size_t) i].noteOff(); for (auto& h : songHeld) h = false; }
    void songSeek(const MidiSong* s, double beat) {
        sidx = 0;
        for (int t = 0; t < SONG_TRK; ++t) { songVol[t] = 1; songPan[t] = 0.5; songBendV[t] = 0; }
        while (sidx < s->ev.size() && s->ev[sidx].beat < beat - 1e-9) {
            const auto& e = s->ev[sidx++];
            if ((e.status & 0xF0) == 0xB0 || (e.status & 0xF0) == 0xE0) songEvent(e);   // levels, pans and bends up to here
        }
    }
    Controls songControls(const Sound* snd, int t) const { Controls c = snd->ctl; c.level = (float) std::min(1.0, c.level * songVol[t] * songGain.load()); return c; }
    // a card for a note on track t: one already on this note, a free one, the oldest released, the oldest
    int songCard(int t, int note) {
        int best = -1; uint64_t oldest = UINT64_MAX;
        for (int i = KEY_CH; i < NCH; ++i) { const Channel& c = ch[(size_t) i]; if (c.st() != Channel::Idle && cardTrack[i] == t && cardNote[i] == note) return i; }
        for (int i = KEY_CH; i < NCH; ++i) if (ch[(size_t) i].st() == Channel::Idle) return i;
        for (int i = KEY_CH; i < NCH; ++i) { const Channel& c = ch[(size_t) i]; if (c.st() == Channel::Releasing && c.stamp() < oldest) { oldest = c.stamp(); best = i; } }
        if (best >= 0) return best;
        for (int i = KEY_CH; i < NCH; ++i) if (ch[(size_t) i].stamp() < oldest) { oldest = ch[(size_t) i].stamp(); best = i; }
        return best;
    }
    void songEvent(const SongEvent& e) {
        const int t = e.track, hi = e.status & 0xF0;
        if (t < 0 || t >= SONG_TRK) return;
        if (hi == 0x90 && e.d2 > 0) {
            if (songMute[t] || !bank) return;
            int idx = songVoice[t];
            if (idx == GM_KIT) idx = kitVoice ? kitVoice(e.d1) : -1;
            const Sound* snd = bank->at(idx);
            if (!snd) return;
            const int k = songCard(t, e.d1);
            if (k < 0) return;
            Channel& c = ch[(size_t) k];
            const int note = songVoice[t] == GM_KIT ? snd->root : e.d1;
            c.noteOn(snd, songControls(snd, t), note, e.d2 / 127.f, -1, ++stampN);
            c.setBend(songBendV[t]); c.setHeld(songHeld[t]);
            cardTrack[k] = t; cardNote[k] = e.d1;
            return;
        }
        if (hi == 0x80 || hi == 0x90) {
            for (int i = KEY_CH; i < NCH; ++i) { Channel& c = ch[(size_t) i]; if (cardTrack[i] == t && cardNote[i] == e.d1 && c.isGate()) c.noteOff(); }
            return;
        }
        auto each = [&](auto fn) { for (int i = KEY_CH; i < NCH; ++i) if (cardTrack[i] == t && ch[(size_t) i].st() != Channel::Idle) fn(ch[(size_t) i]); };
        if (hi == 0xB0) {
            if (e.d1 == 7 || e.d1 == 11) { songVol[t] = e.d2 / 127.0; each([&](Channel& c) { if (c.sound()) c.setControls(songControls(c.sound(), t)); }); }
            else if (e.d1 == 10) songPan[t] = e.d2 / 127.0;
            else if (e.d1 == 64) { songHeld[t] = e.d2 >= 64; each([&](Channel& c) { c.setHeld(songHeld[t]); }); }
            else if (e.d1 == 120 || e.d1 == 123) each([&](Channel& c) { c.noteOff(); });
            return;
        }
        if (hi == 0xE0) { songBendV[t] = (((e.d2 << 7) | e.d1) - 8192) / 8192.0 * 2.0; each([&](Channel& c) { c.setBend(songBendV[t]); }); }
    }
    int songAdvance(int maxLen, const Transport* tp, int offset) {
        const MidiSong* s = song.load();
        if (s != curSong) {
            // an edited version of the song that's playing: carry on from here
            if (s && curSong && s->id == curSong->id && s->id != 0) { songAllOff(); curSong = s; if (songWas) songSeek(s, sbeat); }
            else { songAllOff(); curSong = s; sbeat = 0; songWas = false; if (!s) { songBeat = 0; return maxLen; } }
        }
        const bool ext = extSync.load() && tp && tp->valid;
        const bool running = ext ? tp->playing : songRun.load();
        if (!running) {
            if (songWas) { songAllOff(); songWas = false; }
            if (!ext) sbeat = 0;
            return maxLen;
        }
        const double bpm = ext ? (tp->bpm > 0 ? tp->bpm : 120) : s->bpmAt(sbeat);
        const double bps = bpm / 60.0 / fs;   // beats per sample
        if (ext) {
            const double hb = tp->ppq + offset * bps;
            if (!songWas || hb < sbeat - 0.01 || hb > sbeat + 1.0) { songAllOff(); songSeek(s, hb); }
            sbeat = hb;
        } else if (!songWas) songSeek(s, sbeat);
        songWas = true;
        while (sidx < s->ev.size() && s->ev[sidx].beat <= sbeat + 1e-9) songEvent(s->ev[sidx++]);
        if (sidx >= s->ev.size() && sbeat >= s->length) {
            songAllOff();
            if (songLoop.load() && !ext) { sbeat = 0; songSeek(s, 0); }
            else { if (!ext) { songRun = false; sbeat = 0; } songWas = ext; songBeat = 0; return maxLen; }
        }
        const double next = sidx < s->ev.size() ? s->ev[sidx].beat : s->length;
        int len = clampi((int) std::ceil((next - sbeat) / bps), 1, maxLen);
        // tempo changes are events too
        for (const auto& tc : s->tempo) if (tc.first > sbeat + 1e-9) { len = std::min(len, std::max(1, (int) std::ceil((tc.first - sbeat) / bps))); break; }
        if (!ext) sbeat += len * bps;
        songBeat = sbeat;
        return len;
    }

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
        if ((int) fxL.size() < n) { fxL.resize((size_t) n); fxR.resize((size_t) n); retL.resize((size_t) n); retR.resize((size_t) n); }
        const bool fxRun = fxOn.load();
        bool anySend = false;
        if (fxRun) { std::fill(fxL.begin(), fxL.begin() + n, 0.f); std::fill(fxR.begin(), fxR.begin() + n, 0.f); }
        float* kbL = L; float* kbR = R;
        std::fill(kbL, kbL + n, 0.f); std::fill(kbR, kbR + n, 0.f);
        bool anySolo = false; for (const auto& sl : solo) anySolo |= sl.load();
        float peak[STRIPS] {};
        for (int i = 0; i < NCH; ++i) {
            Channel& c = ch[(size_t) i];
            if (c.st() == Channel::Idle) continue;
            std::fill(mono.begin(), mono.begin() + n, 0.f);
            c.setBlend(loopBlend.load());
            c.render(mono.data(), n);
            const int sIdx = stripOf(i);
            double pan;
            if (i < KEY_CH) pan = 0.5 + ((i & 1) ? 0.5 : -0.5) * spread * (0.4 + 0.6 * (i >> 1) / 3.0);
            else if (curSong) pan = songPan[clampi(cardTrack[i], 0, SONG_TRK - 1)];
            else pan = seq.trk[(i - KEY_CH) % TRACKS].pan;
            const float po = panOver[(size_t) sIdx].load();
            if (po >= 0) pan = po;
            const bool on = !mute[(size_t) sIdx].load() && (!anySolo || solo[(size_t) sIdx].load());
            const float gain = on ? fader[(size_t) sIdx].load() : 0.f;
            const float gl = (float) std::cos(pan * PI / 2) * 1.41421356f * gain, gr = (float) std::sin(pan * PI / 2) * 1.41421356f * gain;
            float pk = 0;
            for (int k = 0; k < n; ++k) { const float m = mono[(size_t) k]; kbL[k] += m * gl; kbR[k] += m * gr; pk = std::max(pk, std::fabs(m)); }
            const float sd = fxSend[(size_t) sIdx].load();
            if (fxRun && sd > 0 && gain > 0) { anySend = true; for (int k = 0; k < n; ++k) { const float m = mono[(size_t) k] * sd; fxL[(size_t) k] += m * gl; fxR[(size_t) k] += m * gr; } }
            peak[sIdx] = std::max(peak[sIdx], pk * gain * 0.5f * (float) master);
        }
        for (int i = 0; i < STRIPS; ++i) meter[(size_t) i] = std::max(peak[i], meter[(size_t) i].load() * 0.92f);
        // the REV7 return (it keeps running on silence to ring out; it sleeps by itself once its tail has gone)
        if (fxRun) {
            (void) anySend;
            fx.process(fxL.data(), fxR.data(), retL.data(), retR.data(), n);
            float fp = 0;
            for (int k = 0; k < n; ++k) { kbL[k] += retL[(size_t) k]; kbR[k] += retR[(size_t) k]; fp = std::max({ fp, std::fabs(retL[(size_t) k]), std::fabs(retR[(size_t) k]) }); }
            fxMeter = std::max(fp * 0.5f * (float) master, fxMeter.load() * 0.92f);
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
        float pl = 0, pr = 0; for (int k = 0; k < n; ++k) { pl = std::max(pl, std::fabs(kbL[k])); pr = std::max(pr, std::fabs(kbR[k])); }
        meterL = std::max(pl, meterL.load() * 0.92f); meterR = std::max(pr, meterR.load() * 0.92f);
    }

    // the summing amplifier: clean to 0.6, then rounding into its rails at 1
    static double soft(double x) { const double a = std::fabs(x); return a < 0.6 ? x : std::copysign(0.6 + 0.4 * std::tanh((a - 0.6) / 0.4), x); }
    double rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs * (1.0 / 4294967296.0); }

    std::array<Channel, NCH> ch;
    std::vector<float> mono, side, fxL, fxR, retL, retR;
    double fs = 48000, mf = masterFreq(), master = 1, hiss = 1, spread = 0.3, bendRange = 2, hpA = 0.999;
    double hlx = 0, lpx = 0, hrx = 0, rpx = 0;
    uint32_t rs = 0x1234567u;
    int kbSound = 0, poly = KEY_CH, lastKey = -1; Controls kb; bool pedal = false, multi = false;
    uint64_t stampN = 0;
    // sequencer
    double gpos = 0; long long lastStep = -1; bool wasRunning = false;
    double gateEnd[TRACKS] { -1, -1, -1, -1, -1, -1, -1, -1 };
    // song player
    const MidiSong* curSong = nullptr; double sbeat = 0; size_t sidx = 0; bool songWas = false;
    double songVol[SONG_TRK] {}, songPan[SONG_TRK] {}, songBendV[SONG_TRK] {}; bool songHeld[SONG_TRK] {};
    int cardTrack[NCH] {}, cardNote[NCH] {};
};

}  // namespace cmi
