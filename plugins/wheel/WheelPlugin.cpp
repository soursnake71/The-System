// Omega Wheel of Steel processor: host parameters, the command FIFO to the audio thread, MPC pads, MIDI learn, track
// loading and project state. Threads: the GUI / timer (message thread) talks to the audio thread only through the
// Cmd FIFO and atomics; the audio thread never frees a track (the message thread keeps a reference until it is the
// last one) and never allocates.
#include "WheelPlugin.h"
#include "WheelEditor.h"
#include <cmath>

namespace wheel {

juce::String deckParam(int deck, const char* id) { return juce::String(deck == 0 ? "a_" : "b_") + id; }

namespace {

// parameter indices (order of pid::deckIds / pid::globalIds)
enum DP { pPlay, pCue, pPitch, pRange, pRpm45, pKeylock, pSync, pSlip, pQuant, pPlatter, pTouch, pNudge, pCensor, pStart, pBrake,
          pTrim, pHi, pMid, pLow, pFilter, pFader, pPfl, pXfassign, pFxtype, pFxbeats, pFxdepth, pFxparam, pFxon, kDeckParams };
enum GP { gXfader, gXfcurve, gFadercurve, gHamster, gMaster, gCuemix, gCuegain, gPadmode, gPadbase, gHostsync, kGlobalParams };
static_assert(kDeckParams == (int) (sizeof(pid::deckIds) / sizeof(pid::deckIds[0])), "deck parameter table out of step");
static_assert(kGlobalParams == (int) (sizeof(pid::globalIds) / sizeof(pid::globalIds[0])), "global parameter table out of step");
constexpr int kParams = 2 * kDeckParams + kGlobalParams;
constexpr int dpi(int deck, int k) { return deck * kDeckParams + k; }
constexpr int gpi(int g) { return 2 * kDeckParams + g; }
juce::String paramId(int idx) {
    return idx < 2 * kDeckParams ? deckParam(idx / kDeckParams, pid::deckIds[idx % kDeckParams]) : juce::String(pid::globalIds[idx - 2 * kDeckParams]);
}

// buttons-as-parameters: edges become actions (play / sync toggle on a press, the others follow down / up)
enum Btn { bPlay, bCue, bSync, bCensor, bTouch, kBtns };
constexpr int kBtnParam[kBtns] = { pPlay, pCue, pSync, pCensor, pTouch };
int btnOf(int k) { for (int b = 0; b < kBtns; ++b) if (kBtnParam[b] == k) return b; return -1; }

const double kRanges[3] = { .08, .16, .50 };
const double kFxBeats[9] = { 1. / 16, 1. / 8, .25, .5, .75, 1, 2, 4, 8 };
const double kLoopBeats[8] = { .25, .5, 1, 2, 4, 8, 16, 32 };
const double kRollBeats[8] = { 1. / 32, 1. / 16, 1. / 8, .25, .5, 1, 2, 4 };
const char* const kLoopNames[8] = { "1/4", "1/2", "1", "2", "4", "8", "16", "32" };
const char* const kRollNames[8] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "4" };
const char* const kTransportNames[8] = { "PLAY", "CUE", "SYNC", "CENSOR", "NUDGE -", "NUDGE +", "LOOP IN", "LOOP OUT" };

// MPC 4x4: pad index = row * 4 + column, row 0 at the bottom. Columns 0-1 deck A, 2-3 deck B.
int padDeck(int p) { return (p % 4) < 2 ? 0 : 1; }
int padSlot(int p) { return (p / 4) * 2 + (p % 2); }
int padIndexOf(int deck, int slot) { return (slot / 2) * 4 + deck * 2 + slot % 2; }

constexpr double kRevSeconds = 1.8;        // one platter revolution at 33 1/3 rpm
constexpr double kJogTicksPerRev = 128;    // relative CC ticks per revolution
constexpr double kReleaseSec = .06;        // hand lifts this long after the last platter movement
constexpr int kTargetPad = 1000;           // MIDI map target >= this: pad (target - kTargetPad)
constexpr int kMapSize = 16 * 128;         // channel * 128 + cc / note
enum MapMode : int { mAbs, mRel2c, mRel64, mDetect };

bool same(double a, double b) { return std::abs(a - b) < 1e-9; }

using Range = juce::NormalisableRange<float>;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
juce::String sgn(float v, int dec) { return (v > 0 ? "+" : "") + juce::String(v, dec); }

void addFloat(Layout& l, const juce::String& id, const juce::String& name, Range r, float def, std::function<juce::String(float)> fmt) {
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, r, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([fmt](float v, int) { return fmt(v); })));
}
void addBool(Layout& l, const juce::String& id, const juce::String& name, bool def) {
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, def));
}
void addChoice(Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& c, int def) {
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, name, c, def));
}

Layout makeLayout() {
    Layout l;
    const auto db = [](float v) { return v <= -25.9f ? juce::String("KILL") : sgn(v, 1) + " dB"; };
    const auto pct = [](float v) { return juce::String(juce::roundToInt(v * 100)) + "%"; };
    const auto sec = [](float v) { return juce::String(v, 2) + " s"; };
    // EQ: knob centre = 0 dB, left half -26 dB (kill) .. 0, right half 0 .. +6
    const Range eq(-26.f, 6.f, [](float, float, float n) { return n < .5f ? -26.f * (1 - 2 * n) : 12.f * (n - .5f); },
                               [](float, float, float v) { return v < 0 ? (1 + v / 26.f) * .5f : .5f + v / 12.f; });
    Range start(.05f, 4.f); start.setSkewForCentre(.7f);
    Range brake(.05f, 6.f); brake.setSkewForCentre(.5f);
    for (int dk = 0; dk < 2; ++dk) {
        const juce::String D = dk == 0 ? "Deck A " : "Deck B ";
        const auto id = [dk](int k) { return deckParam(dk, pid::deckIds[k]); };
        addBool(l, id(pPlay), D + "Play", false);
        addBool(l, id(pCue), D + "Cue", false);
        addFloat(l, id(pPitch), D + "Pitch", Range(-1, 1), 0, [](float v) { return sgn(v * 100, 1) + "%"; });
        addChoice(l, id(pRange), D + "Pitch Range", { "8%", "16%", "50%" }, 0);
        addBool(l, id(pRpm45), D + "45 RPM", false);
        addBool(l, id(pKeylock), D + "Key Lock", false);
        addBool(l, id(pSync), D + "Sync", false);
        addBool(l, id(pSlip), D + "Slip", false);
        addBool(l, id(pQuant), D + "Quantize", true);
        addFloat(l, id(pPlatter), D + "Platter (Scratch)", Range(0, 1), .5f, [](float v) { return juce::String(juce::roundToInt(v * 360)) + " deg"; });
        addBool(l, id(pTouch), D + "Touch", false);
        addFloat(l, id(pNudge), D + "Nudge", Range(-1, 1), 0, [](float v) { return sgn(v * 4, 1) + "%"; });
        addBool(l, id(pCensor), D + "Censor", false);
        addFloat(l, id(pStart), D + "Start Time", start, .7f, sec);
        addFloat(l, id(pBrake), D + "Brake Time", brake, .35f, sec);
        addFloat(l, id(pTrim), D + "Trim", Range(-12, 12), 0, [](float v) { return sgn(v, 1) + " dB"; });
        addFloat(l, id(pHi), D + "EQ High", eq, 0, db);
        addFloat(l, id(pMid), D + "EQ Mid", eq, 0, db);
        addFloat(l, id(pLow), D + "EQ Low", eq, 0, db);
        addFloat(l, id(pFilter), D + "Filter", Range(-1, 1), 0, [](float v) {
            return std::abs(v) < .01f ? juce::String("OFF") : (v < 0 ? "LP " : "HP ") + juce::String(juce::roundToInt(std::abs(v) * 100)) + "%"; });
        addFloat(l, id(pFader), D + "Channel Fader", Range(0, 1), 1, pct);
        addBool(l, id(pPfl), D + "Headphone Cue", false);
        addChoice(l, id(pXfassign), D + "Crossfader Assign", { "A", "Thru", "B" }, dk == 0 ? 0 : 2);
        addChoice(l, id(pFxtype), D + "FX Type", { "Off", "Echo", "Ping Pong", "Reverb", "Flanger", "Phaser", "Bitcrush", "Gate" }, 0);
        addChoice(l, id(pFxbeats), D + "FX Beats", { "1/16", "1/8", "1/4", "1/2", "3/4", "1", "2", "4", "8" }, 4);
        addFloat(l, id(pFxdepth), D + "FX Depth", Range(0, 1), .5f, pct);
        addFloat(l, id(pFxparam), D + "FX Param", Range(0, 1), .5f, pct);
        addBool(l, id(pFxon), D + "FX On", false);
    }
    static_assert((int) FxType::count == 8, "FX Type choices out of step with FxType");
    addFloat(l, pid::globalIds[gXfader], "Crossfader", Range(-1, 1), 0, [](float v) {
        return std::abs(v) < .01f ? juce::String("Centre") : juce::String(v < 0 ? "A " : "B ") + juce::String(juce::roundToInt(std::abs(v) * 100)) + "%"; });
    addFloat(l, pid::globalIds[gXfcurve], "Crossfader Curve", Range(0, 1), .5f, pct);
    addFloat(l, pid::globalIds[gFadercurve], "Channel Fader Curve", Range(0, 1), .3f, pct);
    addBool(l, pid::globalIds[gHamster], "Hamster Switch", false);
    addFloat(l, pid::globalIds[gMaster], "Master Level", Range(-24, 6), 0, [](float v) { return sgn(v, 1) + " dB"; });
    addFloat(l, pid::globalIds[gCuemix], "Cue Mix", Range(0, 1), 0, [](float v) { return "Cue " + juce::String(juce::roundToInt((1 - v) * 100)) + " / Mst " + juce::String(juce::roundToInt(v * 100)); });
    addFloat(l, pid::globalIds[gCuegain], "Cue Level", Range(-24, 12), 0, [](float v) { return sgn(v, 1) + " dB"; });
    addChoice(l, pid::globalIds[gPadmode], "Pad Mode", { "Cues", "Loops", "Rolls", "Transport" }, 0);
    l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { pid::globalIds[gPadbase], 1 }, "Pad Base Note", 0, 108, 37));
    addBool(l, pid::globalIds[gHostsync], "Host Tempo Sync", false);
    return l;
}

Cmd mk(Cmd::Type t, int deck, int i = 0, double v = 0, bool on = false) {
    Cmd c {}; c.type = t; c.deck = deck; c.i = i; c.v = v; c.on = on; return c;
}

}  // namespace

/* ======================================================================================================== Impl == */
struct WheelProcessor::Impl {
    explicit Impl(WheelProcessor& p) : P(p) {
        for (int i = 0; i < kParams; ++i) {
            const auto id = paramId(i);
            prm[i] = P.apvts.getParameter(id); raw[i] = P.apvts.getRawParameterValue(id);
            jassert(prm[i] != nullptr && raw[i] != nullptr);
            isBool[i] = dynamic_cast<juce::AudioParameterBool*>(prm[i]) != nullptr;
            midiVal[i].store(-1); timerApplying[i].store(false);
        }
        for (int dk = 0; dk < 2; ++dk) {
            for (int b = 0; b < kBtns; ++b) { high[dk][b].store(false); presses[dk][b].store(0); btnIds[dk][b] = deckParam(dk, pid::deckIds[kBtnParam[b]]); }
            gridShift[dk].store(0); gridBpm[dk].store(0); gridShiftBase[dk].store(0); gridBpmBase[dk].store(0); gen[dk].store(0);
            snap[dk].seq.store(0); snap[dk].trk.store(nullptr);
            for (int i = 0; i < kCues; ++i) { snap[dk].t[i].store(-1); snap[dk].rgb[i].store(0); pubT[dk][i] = -1; pubRgb[dk][i] = 0; }
        }
        for (auto& h : heldMode) for (auto& m : h) m = -1;
        clearMaps();
    }

    WheelProcessor& P;
    juce::RangedAudioParameter* prm[kParams] {};
    std::atomic<float>* raw[kParams] {};
    bool isBool[kParams] {};
    std::atomic<float> midiVal[kParams];          // normalised value from MIDI (-1 none): used at once, pushed to the host by the timer
    std::atomic<bool> timerApplying[kParams];

    // ---- buttons as parameters
    juce::String btnIds[2][kBtns];
    std::atomic<bool> high[2][kBtns];
    std::atomic<uint32_t> presses[2][kBtns];
    uint32_t seen[2][kBtns] {};
    bool held[2][kBtns] {};
    std::atomic<bool> suppress { false };

    void buttonInput(int dk, int b, bool down, bool count) {
        const bool was = high[dk][b].exchange(down);
        if (down && !was && count) presses[dk][b].fetch_add(1);
    }

    // ---- command FIFO (message thread -> audio thread)
    static constexpr int kFifo = 512;
    juce::AbstractFifo fifo { kFifo };
    Cmd q[kFifo];
    juce::SpinLock sendLock;
    bool push(const Cmd& c) {
        const juce::SpinLock::ScopedLockType l(sendLock);
        int s1, n1, s2, n2;
        fifo.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 + n2 < 1) return false;
        q[n1 > 0 ? s1 : s2] = c;
        fifo.finishedWrite(1);
        return true;
    }
    void drain() {
        int s1, n1, s2, n2;
        fifo.prepareToRead(fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { Cmd c = std::move(q[s1 + i]); exec(c); }
        for (int i = 0; i < n2; ++i) { Cmd c = std::move(q[s2 + i]); exec(c); }
        fifo.finishedRead(n1 + n2);
    }

    // ---- audio state
    double sr = 48000; int cap = 0; juce::int64 now = 0;
    std::vector<float> scratch[4];
    double hostBpmNow = 0, hostPhase = -1;
    float lastPlat[2] {}; bool platInit[2] {}; std::atomic<bool> platResync { true };
    bool handOn[2] {}, touchHeld[2] {}; juce::int64 lastMove[2] {}; double relRev[2] {};
    float lastNudge[2] {};
    bool wasPlaying[2] {};
    std::atomic<double> gridShift[2], gridBpm[2];             // live grid edits since the track was loaded
    double taps[2][8] {}; int nTaps[2] {};
    MixerSettings ms; FxSettings fxs[2];

    float val(int i) const {
        const float m = midiVal[i].load(std::memory_order_relaxed);
        return m >= 0 ? prm[i]->convertFrom0to1(m) : raw[i]->load(std::memory_order_relaxed);
    }
    bool isOn(int i) const { return val(i) > .5f; }
    int choice(int i, int n) const { return juce::jlimit(0, n - 1, (int) std::lround(val(i))); }

    // ---- hot cue snapshot (audio thread writes, GUI reads; seqlock over atomics)
    struct Snap { std::atomic<uint32_t> seq; std::atomic<const void*> trk; std::atomic<double> t[kCues]; std::atomic<uint32_t> rgb[kCues]; } snap[2];
    const void* pubTrk[2] {}; double pubT[2][kCues]; uint32_t pubRgb[2][kCues];
    void publishCues() {
        for (int dk = 0; dk < 2; ++dk) {
            const auto& deck = P.engineDeck[dk];
            const void* tr = deck.track();
            bool changed = tr != pubTrk[dk];
            for (int i = 0; i < kCues && !changed; ++i) changed = !same(deck.cues[(size_t) i].t, pubT[dk][i]) || deck.cues[(size_t) i].rgb != pubRgb[dk][i];
            if (!changed) continue;
            auto& s = snap[dk];
            const uint32_t q0 = s.seq.load(std::memory_order_relaxed);
            s.seq.store(q0 + 1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            for (int i = 0; i < kCues; ++i) {
                pubT[dk][i] = deck.cues[(size_t) i].t; pubRgb[dk][i] = deck.cues[(size_t) i].rgb;
                s.t[i].store(pubT[dk][i], std::memory_order_relaxed); s.rgb[i].store(pubRgb[dk][i], std::memory_order_relaxed);
            }
            s.trk.store(tr, std::memory_order_relaxed);
            pubTrk[dk] = tr;
            s.seq.store(q0 + 2, std::memory_order_release);
        }
    }

    // ---- pads
    std::atomic<int> padModeOv { -1 };   // set from MIDI mode notes; the timer moves it into the parameter
    int heldMode[2][16];                  // [0] GUI, [1] MIDI: the mode a held pad was pressed in
    int padMode() const { const int o = padModeOv.load(); return o >= 0 ? o : choice(gpi(gPadmode), (int) PadMode::count); }

    template <class Emit> void padActions(int p, bool down, float vel, int mode, Emit&& emit) const {
        const int dk = padDeck(p), s = padSlot(p);
        switch (mode) {
            case (int) PadMode::cues: emit(mk(Cmd::hotCue, dk, s, 0, down)); break;
            case (int) PadMode::loops: if (down) emit(mk(Cmd::autoLoop, dk, s, kLoopBeats[s])); break;
            case (int) PadMode::rolls: emit(mk(Cmd::roll, dk, s, kRollBeats[s], down)); break;
            default:
                switch (s) {
                    case 0: if (down) emit(mk(Cmd::playPause, dk)); break;
                    case 1: emit(mk(Cmd::cue, dk, 0, 0, down)); break;
                    case 2: if (down) emit(mk(Cmd::sync, dk)); break;   // toggle
                    case 3: emit(mk(Cmd::censor, dk, 0, 0, down)); break;
                    case 4: emit(mk(Cmd::nudge, dk, 0, down ? -(.25 + .75 * vel) : 0.)); break;
                    case 5: emit(mk(Cmd::nudge, dk, 0, down ? (.25 + .75 * vel) : 0.)); break;
                    case 6: if (down) emit(mk(Cmd::loopIn, dk)); break;
                    default: if (down) emit(mk(P.engineDeck[dk].st.looping.load() ? Cmd::loopExit : Cmd::loopOut, dk)); break;
                }
        }
    }
    template <class Emit> void padPress(int ctx, int p, bool down, float vel, Emit&& emit) {
        if (p < 0 || p >= 16) return;
        int mode;
        if (down) { mode = padMode(); if (heldMode[ctx][p] >= 0) padActions(p, false, 0, heldMode[ctx][p], emit); heldMode[ctx][p] = mode; }
        else { mode = heldMode[ctx][p]; if (mode < 0) return; heldMode[ctx][p] = -1; }
        padActions(p, down, juce::jlimit(0.f, 1.f, vel), mode, emit);
    }
    void padInput(int p, bool down, float vel) { padPress(1, p, down, vel, [this](const Cmd& c) { exec(c); }); }
    void releaseAllPads() { for (int p = 0; p < 16; ++p) if (heldMode[1][p] >= 0) padInput(p, false, 0); }

    // ---- MIDI map (audio thread writes on learn, message thread reads for state / summary)
    std::atomic<int> ccMap[kMapSize], noteMap[kMapSize], ccMode[kMapSize];
    bool ccPadDown[kMapSize] {};
    std::atomic<int> learnTarget { -1 };
    juce::String learnName; juce::SpinLock learnLock;
    int detKey = -1, detN = 0, detV[3] {};
    void clearMaps() { for (int i = 0; i < kMapSize; ++i) { ccMap[i].store(-1); noteMap[i].store(-1); ccMode[i].store(mAbs); ccPadDown[i] = false; } }
    static bool isPlatter(int t) { return t >= 0 && t < 2 * kDeckParams && t % kDeckParams == pPlatter; }

    // a MIDI value for parameter t (normalised): buttons fire their edges now; everything takes effect this block
    void paramInput(int t, float norm) {
        if (t < 2 * kDeckParams) {
            const int b = btnOf(t % kDeckParams);
            if (b >= 0) { const bool h = norm >= .5f; buttonInput(t / kDeckParams, b, h, true); midiVal[t].store(h ? 1.f : 0.f); return; }
        }
        midiVal[t].store(juce::jlimit(0.f, 1.f, norm));
    }

    void handleMidi(const juce::MidiBuffer& midi) {
        for (const auto meta : midi) {
            const auto m = meta.getMessage();
            const int ch = juce::jlimit(1, 16, m.getChannel());
            if (m.isNoteOn() || m.isNoteOff()) {
                const int note = m.getNoteNumber(), key = (ch - 1) * 128 + note;
                const bool down = m.isNoteOn();
                const float vel = down ? m.getFloatVelocity() : 0.f;
                const int lt = learnTarget.load();
                if (down && lt >= 0) {
                    for (auto& x : noteMap) if (x.load() == lt) x.store(-1);
                    noteMap[key].store(lt); learnTarget.store(-1);
                    continue;
                }
                const int t = noteMap[key].load();
                if (t >= kTargetPad) { padInput(t - kTargetPad, down, vel); continue; }
                if (t >= 0) {
                    if (t < 2 * kDeckParams && btnOf(t % kDeckParams) >= 0) paramInput(t, down ? 1.f : 0.f);
                    else if (down) paramInput(t, isBool[t] ? (isOn(t) ? 0.f : 1.f) : vel);   // a note toggles a switch
                    continue;
                }
                if (ch == 2 || ch == 3) { padInput(padIndexOf(ch - 2, note % 8), down, vel); continue; }
                const int rel = note - (int) std::lround(val(gpi(gPadbase)));
                if (rel >= 0 && rel < 16) padInput(rel, down, vel);
                else if (rel >= 16 && rel < 20 && down) padModeOv.store(rel - 16);
            } else if (m.isController()) {
                const int cc = m.getControllerNumber(), v = m.getControllerValue(), key = (ch - 1) * 128 + cc;
                if (cc == 120 || cc == 123) { releaseAllPads(); continue; }
                const int lt = learnTarget.load();
                if (lt >= 0) {
                    for (int i = 0; i < kMapSize; ++i) if (ccMap[i].load() == lt) ccMap[i].store(-1);
                    ccMode[key].store(isPlatter(lt) ? mDetect : mAbs); ccMap[key].store(lt); ccPadDown[key] = false;
                    detKey = -1; learnTarget.store(-1);
                    continue;
                }
                const int t = ccMap[key].load();
                if (t < 0) continue;
                if (t >= kTargetPad) {
                    const bool down = v >= 64;
                    if (down != ccPadDown[key]) { ccPadDown[key] = down; padInput(t - kTargetPad, down, 1); }
                    continue;
                }
                if (isPlatter(t)) {
                    const int mode = ccMode[key].load(), dk = t / kDeckParams;
                    if (mode == mDetect) {
                        // relative encoders repeat a value (1/127 or 65/63 per tick); absolute knobs never send the same value twice
                        if (key != detKey) { detKey = key; detN = 0; }
                        detV[detN++] = v;
                        if (detN == 3) {
                            int r = -1;
                            if (detV[0] == detV[1] || detV[0] == detV[2]) r = detV[0]; else if (detV[1] == detV[2]) r = detV[1];
                            ccMode[key].store(r < 0 ? mAbs : std::abs(r - 64) < juce::jmin(r, 128 - r) ? mRel64 : mRel2c);
                            detKey = -1;
                        }
                        continue;
                    }
                    if (mode == mRel2c) { relRev[dk] += (v < 64 ? v : v - 128) / kJogTicksPerRev; continue; }
                    if (mode == mRel64) { relRev[dk] += (v - 64) / kJogTicksPerRev; continue; }
                }
                paramInput(t, (float) v / 127.f);
            } else if (m.isPitchWheel()) {
                // pitch bend (an MPC touch strip): channel 1 = deck A pitch, channel 2 = deck B
                if (ch <= 2) midiVal[dpi(ch - 1, pPitch)].store(juce::jlimit(0.f, 1.f, .5f + (float) (m.getPitchWheelValue() - 8192) / 16384.f));
            }
        }
    }

    // ---- sync: the host tempo (hostsync) or the other deck when it plays (Serato-style master)
    bool master(int dk, double& bpm, double& phase) const {
        if (isOn(gpi(gHostsync)) && hostBpmNow > 0) { bpm = hostBpmNow; phase = hostPhase; return true; }
        const auto& o = P.engineDeck[1 - dk];
        if (o.track() != nullptr && o.st.playing.load() && o.currentBpm() > 0
            && (!o.syncing() || dk == 1 || !P.engineDeck[dk].st.playing.load())) {
            bpm = o.currentBpm(); phase = o.beatPhase(); return true;
        }
        return false;
    }
    void syncNow(int dk, bool phaseToo) {
        double bpm = 0, ph = -1;
        if (master(dk, bpm, ph)) P.engineDeck[dk].syncTo(bpm, ph < 0 ? 0 : ph, phaseToo && ph >= 0);
    }
    void followSync() {
        for (int dk = 0; dk < 2; ++dk) {
            auto& deck = P.engineDeck[dk];
            const bool playing = deck.st.playing.load();
            if (deck.syncing() && !deck.st.touched.load()) syncNow(dk, playing && !wasPlaying[dk]);   // phase-lock on start
            wasPlaying[dk] = playing;
        }
    }

    // ---- the audio-thread side of every command
    void exec(const Cmd& c) {
        if (c.deck < 0 || c.deck > 1) return;
        auto& deck = P.engineDeck[c.deck];
        const int dk = c.deck, slot = juce::jlimit(0, kCues - 1, c.i);
        switch (c.type) {
            case Cmd::load:
                deck.load(c.track); P.fx[dk].reset();
                gridShift[dk].store(0); gridBpm[dk].store(0); nTaps[dk] = 0; handOn[dk] = false;
                break;
            case Cmd::playPause: deck.playPause(); break;
            case Cmd::play: deck.play(c.on); break;
            case Cmd::cue: deck.cue(c.on); break;
            case Cmd::hotCue: deck.hotCue(slot, c.on); break;
            case Cmd::clearCue: deck.clearCue(slot); break;
            case Cmd::autoLoop: deck.autoLoop(c.v); break;
            case Cmd::loopIn: deck.loopIn(); break;
            case Cmd::loopOut: deck.loopOut(); break;
            case Cmd::reloop: deck.reloop(); break;
            case Cmd::loopExit: deck.loopExit(); break;
            case Cmd::loopHalve: deck.loopHalve(); break;
            case Cmd::loopDouble: deck.loopDouble(); break;
            case Cmd::loopMove: deck.loopMove(c.v); break;
            case Cmd::roll: deck.roll(c.v, c.on); break;
            case Cmd::savedLoop: deck.savedLoop(slot); break;
            case Cmd::touch: deck.touch(c.on); break;
            case Cmd::move: deck.move(c.v); break;
            case Cmd::bend: deck.bend(c.v); break;
            case Cmd::nudge: deck.nudge((float) c.v); break;
            case Cmd::censor: deck.censor(c.on); break;
            case Cmd::seek: deck.seek(c.v); break;
            case Cmd::sync:
                if (c.on || !deck.syncing()) { deck.setSync(true); syncNow(dk, true); }
                else deck.setSync(false);
                break;
            case Cmd::syncOff: deck.setSync(false); break;
            case Cmd::gridShift: deck.gridShift(c.v); gridShift[dk].store(gridShift[dk].load() + c.v); break;
            case Cmd::gridBpm: if (c.v > 0) { deck.gridBpm(c.v); gridBpm[dk].store(c.v); } break;
            case Cmd::tapBpm: {
                const double t = c.v > 0 ? c.v : juce::Time::getMillisecondCounterHiRes();
                if (nTaps[dk] > 0 && (t - taps[dk][nTaps[dk] - 1] > 2000 || t <= taps[dk][nTaps[dk] - 1])) nTaps[dk] = 0;
                if (nTaps[dk] == 8) { for (int i = 1; i < 8; ++i) taps[dk][i - 1] = taps[dk][i]; nTaps[dk] = 7; }
                taps[dk][nTaps[dk]++] = t;
                if (nTaps[dk] >= 3) {
                    const double bpm = 60000.0 * (nTaps[dk] - 1) / (taps[dk][nTaps[dk] - 1] - taps[dk][0]);
                    if (bpm > 40 && bpm < 300) { deck.gridBpm(bpm); gridBpm[dk].store(bpm); }
                }
                break;
            }
            case Cmd::setCuePoint: {
                const bool qz = deck.set.quantize;
                deck.set.quantize = false;
                deck.seek(c.v); deck.cue(true); deck.cue(false);
                deck.set.quantize = qz;
                break;
            }
        }
    }

    // ---- parameters -> engine settings; button edges; nudge
    void applySettings() {
        for (int dk = 0; dk < 2; ++dk) {
            auto& s = P.engineDeck[dk].set;
            const auto I = [dk](int k) { return dpi(dk, k); };
            s.pitch = val(I(pPitch)); s.pitchRange = (float) kRanges[choice(I(pRange), 3)];
            s.rpm45 = isOn(I(pRpm45)); s.keyLock = isOn(I(pKeylock)); s.slip = isOn(I(pSlip)); s.quantize = isOn(I(pQuant));
            s.startTime = val(I(pStart)); s.brakeTime = val(I(pBrake));
            auto& c = ms.ch[dk];
            c.trim = val(I(pTrim)); c.hi = val(I(pHi)); c.mid = val(I(pMid)); c.low = val(I(pLow));
            c.filter = val(I(pFilter)); c.fader = val(I(pFader)); c.pfl = isOn(I(pPfl)); c.xfAssign = choice(I(pXfassign), 3);
            auto& f = fxs[dk];
            f.type = (FxType) choice(I(pFxtype), (int) FxType::count); f.beats = (float) kFxBeats[choice(I(pFxbeats), 9)];
            f.depth = val(I(pFxdepth)); f.param = val(I(pFxparam)); f.on = isOn(I(pFxon)) && f.type != FxType::off;
            const float nd = val(I(pNudge));
            if (std::abs(nd - lastNudge[dk]) > 1e-4f) { P.engineDeck[dk].nudge(nd); lastNudge[dk] = nd; }
        }
        ms.xfader = val(gpi(gXfader)); ms.xfCurve = val(gpi(gXfcurve)); ms.faderCurve = val(gpi(gFadercurve));
        ms.hamster = isOn(gpi(gHamster)); ms.master = val(gpi(gMaster)); ms.cueMix = val(gpi(gCuemix)); ms.cueGain = val(gpi(gCuegain));
    }
    void momentary(int dk, int b, bool down) {
        auto& deck = P.engineDeck[dk];
        if (b == bCue) deck.cue(down);
        else if (b == bCensor) deck.censor(down);
        else if (b == bTouch) {
            touchHeld[dk] = down;
            if (down) { if (!handOn[dk]) { deck.touch(true); handOn[dk] = true; } lastMove[dk] = now; }
        }
    }
    void handleButtons() {
        for (int dk = 0; dk < 2; ++dk)
            for (int b = 0; b < kBtns; ++b) {
                const uint32_t p = presses[dk][b].load(), np = p - seen[dk][b];
                seen[dk][b] = p;
                const bool h = high[dk][b].load();
                if (b == bPlay || b == bSync) {
                    if (np & 1u) exec(mk(b == bPlay ? Cmd::playPause : Cmd::sync, dk));
                    continue;
                }
                if (np > 0) { if (held[dk][b]) momentary(dk, b, false); momentary(dk, b, true); held[dk][b] = true; }
                if (held[dk][b] != h) { momentary(dk, b, h); held[dk][b] = h; }
            }
    }

    // ---- the platter parameter / jog: hand on the record, move by the delta, lift ~60 ms after it stops
    void handlePlatters() {
        const bool resync = platResync.exchange(false);
        for (int dk = 0; dk < 2; ++dk) {
            auto& deck = P.engineDeck[dk];
            const float v = val(dpi(dk, pPlatter));
            if (resync || !platInit[dk]) { lastPlat[dk] = v; platInit[dk] = true; }
            double delta = v - lastPlat[dk];
            lastPlat[dk] = v;
            if (delta > .5) delta -= 1; else if (delta < -.5) delta += 1;   // wrapped past 0 / 1
            if (std::abs(delta) > .25) delta = 0;                            // a jump (state recall, automation), not a hand
            delta += relRev[dk]; relRev[dk] = 0;
            if (std::abs(delta) > 1e-7) {
                if (!handOn[dk]) { deck.touch(true); handOn[dk] = true; }
                deck.move(delta * kRevSeconds);
                lastMove[dk] = now;
            }
            if (handOn[dk] && !touchHeld[dk] && (double) (now - lastMove[dk]) > kReleaseSec * sr) { deck.touch(false); handOn[dk] = false; }
        }
    }

    // ---- tracks (message thread)
    mutable juce::SpinLock trackLock;
    std::shared_ptr<const Track> current[2];
    juce::File currentFile[2];
    std::vector<std::shared_ptr<const Track>> keep;   // every track the audio thread may hold; freed here once only we hold it

    struct Restore { bool on = false; double pos = 0, cuePoint = -1, gridShift = 0, gridBpm = 0; bool hasCues = false; std::array<HotCue, kCues> cues; };
    struct Done { int deck = 0, gen = 0; juce::File file; std::shared_ptr<Track> track; juce::String err; Restore r; };
    juce::ThreadPool pool { juce::ThreadPoolOptions {}.withThreadName("Wheel loader").withNumberOfThreads(2) };
    std::atomic<int> gen[2];
    juce::CriticalSection doneLock; std::vector<Done> done;
    std::atomic<double> gridShiftBase[2], gridBpmBase[2];   // grid edits restored from the project (already in the Track)
    juce::CriticalSection rootLock; juce::File pendingRoot; bool rootPending = false;

    void startLoad(int dk, const juce::File& f, const Restore& r) {
        const int g = gen[dk].fetch_add(1) + 1;
        P.loading[dk].store(true);
        pool.addJob([this, dk, f, g, r] {
            Done x; x.deck = dk; x.gen = g; x.file = f; x.r = r;
            if (gen[dk].load() != g) return;
            x.track = P.library.load(f, x.err, [this, dk, g] { return gen[dk].load() != g; });
            const juce::ScopedLock l(doneLock);
            done.push_back(std::move(x));
        });
    }
    // a decoded track goes to its deck (false: no room in the FIFO yet)
    bool finishLoad(Done& x) {
        if (fifo.getFreeSpace() < (x.r.on ? 3 : 1)) return false;
        auto& t = *x.track;
        if (x.r.on) {
            if (x.r.hasCues) t.cues = x.r.cues;
            if (std::abs(x.r.gridShift) > 1e-9) t.firstBeat += x.r.gridShift;
            if (x.r.gridBpm > 0) t.bpm = x.r.gridBpm;
        }
        gridShiftBase[x.deck].store(x.r.on ? x.r.gridShift : 0.); gridBpmBase[x.deck].store(x.r.on ? x.r.gridBpm : 0.);
        if (t.path.empty()) t.path = x.file.getFullPathName().toStdString();
        Cmd c = mk(Cmd::load, x.deck); c.track = std::move(x.track);
        P.send(c);
        { const juce::SpinLock::ScopedLockType l(trackLock); currentFile[x.deck] = x.file; }
        if (x.r.on) {
            if (x.r.cuePoint >= 0) P.send(mk(Cmd::setCuePoint, x.deck, 0, x.r.cuePoint));
            P.send(mk(Cmd::seek, x.deck, 0, x.r.pos));
        }
        return true;
    }
};

/* ================================================================================================= processor == */
WheelProcessor::WheelProcessor()
    : AudioProcessor(BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Cue", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "WHEEL", makeLayout()) {
    d = std::make_unique<Impl>(*this);
    for (int dk = 0; dk < 2; ++dk) for (int b = 0; b < kBtns; ++b) apvts.addParameterListener(d->btnIds[dk][b], this);
    startTimerHz(30);
}

WheelProcessor::~WheelProcessor() {
    stopTimer();
    for (int dk = 0; dk < 2; ++dk) for (int b = 0; b < kBtns; ++b) apvts.removeParameterListener(d->btnIds[dk][b], this);
    for (auto& g : d->gen) g.fetch_add(1);   // cancel loads in flight
    d->pool.removeAllJobs(true, 10000);
}

juce::AudioProcessorEditor* WheelProcessor::createEditor() { return new WheelEditor(*this); }

bool WheelProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (const auto& in : l.inputBuses) if (!in.isDisabled()) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i)
        if (!l.outputBuses[i].isDisabled() && l.outputBuses[i] != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void WheelProcessor::prepareToPlay(double sr, int block) {
    auto& D = *d;
    D.sr = sr > 0 ? sr : 48000;
    D.cap = juce::jmax(256, block);
    for (auto& s : D.scratch) s.assign((size_t) D.cap, 0.f);
    for (int dk = 0; dk < 2; ++dk) { engineDeck[dk].prepare(D.sr, D.cap); fx[dk].prepare(D.sr, D.cap); }
    mixer.prepare(D.sr, D.cap);
    D.platResync.store(true);
}

void WheelProcessor::send(const Cmd& c) {
    if (c.type == Cmd::load) {
        // keep a reference here so the audio thread never drops the last one
        if (c.track) d->keep.push_back(c.track);
        const juce::SpinLock::ScopedLockType l(d->trackLock);
        d->current[juce::jlimit(0, 1, c.deck)] = c.track;
    }
    d->push(c);
}

void WheelProcessor::loadTrack(int deck, const juce::File& f) {
    deck = juce::jlimit(0, 1, deck);
    loadError[deck].clear();
    d->startLoad(deck, f, {});
}

std::shared_ptr<const Track> WheelProcessor::deckTrack(int dk) const {
    const juce::SpinLock::ScopedLockType l(d->trackLock);
    return d->current[juce::jlimit(0, 1, dk)];
}

std::array<HotCue, kCues> WheelProcessor::cuesOf(int dk) const {
    dk = juce::jlimit(0, 1, dk);
    std::array<HotCue, kCues> r;
    const auto trk = deckTrack(dk);
    if (trk == nullptr) return r;
    auto& s = d->snap[dk];
    double t[kCues] {}; uint32_t rgb[kCues] {}; const void* who = nullptr;
    for (int tries = 0; tries < 64; ++tries) {
        const uint32_t s1 = s.seq.load(std::memory_order_acquire);
        if (s1 & 1u) continue;
        for (int i = 0; i < kCues; ++i) { t[i] = s.t[i].load(std::memory_order_relaxed); rgb[i] = s.rgb[i].load(std::memory_order_relaxed); }
        who = s.trk.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.seq.load(std::memory_order_relaxed) == s1) break;
        who = nullptr;
    }
    if (who != trk.get()) return trk->cues;   // the audio thread hasn't taken this track yet
    for (size_t i = 0; i < (size_t) kCues; ++i) {
        r[i].t = t[i]; r[i].rgb = rgb[i];
        if (r[i].set() && same(trk->cues[i].t, t[i])) r[i].name = trk->cues[i].name;
    }
    return r;
}

float WheelProcessor::meter(int ch) const {
    switch (ch) {
        case 0: return mixer.meter[0].load();
        case 1: return mixer.meter[1].load();
        case 2: return mixer.masterL.load();
        case 3: return mixer.masterR.load();
        default: return 0;
    }
}

/* ---------------- pads ---------------- */
void WheelProcessor::pad(int index, bool down, float velocity) {
    d->padPress(0, index, down, velocity, [this](const Cmd& c) { send(c); });
}
PadMode WheelProcessor::padMode() const { return (PadMode) d->padMode(); }
juce::String WheelProcessor::padLabel(int index) const {
    if (index < 0 || index >= 16) return {};
    const int s = padSlot(index);
    switch (d->padMode()) {
        case (int) PadMode::cues: return "CUE " + juce::String(s + 1);
        case (int) PadMode::loops: return juce::String("LOOP ") + kLoopNames[s];
        case (int) PadMode::rolls: return juce::String("ROLL ") + kRollNames[s];
        default: return kTransportNames[s];
    }
}

/* ---------------- MIDI learn ---------------- */
void WheelProcessor::learn(const juce::String& target) {
    int t = -1;
    if (target.startsWith("pad:")) t = kTargetPad + juce::jlimit(0, 15, target.fromFirstOccurrenceOf(":", false, false).getIntValue());
    else for (int i = 0; i < kParams; ++i) if (paramId(i) == target) { t = i; break; }
    { const juce::SpinLock::ScopedLockType l(d->learnLock); d->learnName = t >= 0 ? target : juce::String(); }
    d->learnTarget.store(t);
}
juce::String WheelProcessor::learning() const {
    if (d->learnTarget.load() < 0) return {};
    const juce::SpinLock::ScopedLockType l(d->learnLock);
    return d->learnName;
}
void WheelProcessor::clearMappings() { d->learnTarget.store(-1); d->clearMaps(); }

juce::String WheelProcessor::mappingSummary() const {
    const auto name = [this](int t) { return t >= kTargetPad ? "Pad " + juce::String(t - kTargetPad + 1) : d->prm[t]->getName(64); };
    const int base = (int) std::lround(d->val(gpi(gPadbase)));
    juce::String s;
    s << "Pads: notes " << base << "-" << base + 15 << " (MPC pad 1-16), mode notes " << base + 16 << "-" << base + 19
      << " (Cues/Loops/Rolls/Transport); ch 2 / ch 3 notes = deck A / B slots (note % 8); pitch bend ch 1 / 2 = deck A / B pitch\n";
    for (int k = 0; k < kMapSize; ++k) {
        const int t = d->ccMap[k].load();
        if (t < 0) continue;
        const int mode = d->ccMode[k].load();
        s << "CC " << (k % 128) << " ch " << (k / 128 + 1) << " -> " << name(t)
          << (mode == mRel2c ? " (relative)" : mode == mRel64 ? " (relative, 64 = centre)" : mode == mDetect ? " (turn it to detect)" : "") << "\n";
    }
    for (int k = 0; k < kMapSize; ++k) {
        const int t = d->noteMap[k].load();
        if (t >= 0) s << "Note " << (k % 128) << " ch " << (k / 128 + 1) << " -> " << name(t) << "\n";
    }
    return s.trimEnd();
}

/* ---------------- parameters ---------------- */
void WheelProcessor::parameterChanged(const juce::String& id, float v) {
    for (int dk = 0; dk < 2; ++dk)
        for (int b = 0; b < kBtns; ++b)
            if (id == d->btnIds[dk][b]) {
                if (d->timerApplying[dpi(dk, kBtnParam[b])].load()) return;   // MIDI already fired this edge
                d->buttonInput(dk, b, v > .5f, !d->suppress.load());
                return;
            }
}

/* ---------------- timer (message thread) ---------------- */
void WheelProcessor::timerCallback() {
    auto& D = *d;
    // values that came in by MIDI: tell the host / GUI
    for (int i = 0; i < kParams; ++i) {
        float m = D.midiVal[i].load();
        if (m < 0) continue;
        if (std::abs(D.prm[i]->getValue() - m) > 1e-6f) {
            D.timerApplying[i].store(true);
            D.prm[i]->setValueNotifyingHost(m);
            D.timerApplying[i].store(false);
        }
        D.midiVal[i].compare_exchange_strong(m, -1.f);
    }
    {
        int o = D.padModeOv.load();
        if (o >= 0) {
            auto* p = D.prm[gpi(gPadmode)];
            p->setValueNotifyingHost(p->convertTo0to1((float) o));
            D.padModeOv.compare_exchange_strong(o, -1);
        }
    }
    // finished loads
    std::vector<Impl::Done> ready;
    { const juce::ScopedLock l(D.doneLock); ready.swap(D.done); }
    std::vector<Impl::Done> later;
    for (auto& x : ready) {
        if (D.gen[x.deck].load() != x.gen) continue;
        if (x.track == nullptr) {
            loading[x.deck].store(false);
            loadError[x.deck] = x.err.isNotEmpty() ? x.err : "Couldn't load " + x.file.getFileName();
            continue;
        }
        if (D.finishLoad(x)) { loading[x.deck].store(false); loadError[x.deck].clear(); }
        else later.push_back(std::move(x));
    }
    if (!later.empty()) {
        const juce::ScopedLock l(D.doneLock);
        for (auto& x : later) D.done.push_back(std::move(x));
    }
    // tracks nobody but us holds any more
    D.keep.erase(std::remove_if(D.keep.begin(), D.keep.end(), [](const std::shared_ptr<const Track>& t) { return t.use_count() == 1; }), D.keep.end());
    // the library root from a recalled project
    juce::File root; bool doRoot = false;
    { const juce::ScopedLock l(D.rootLock); if (D.rootPending) { root = D.pendingRoot; D.rootPending = false; doRoot = true; } }
    if (doRoot && root.isDirectory()) library.setRoot(root);
}

/* ---------------- audio ---------------- */
void WheelProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    auto& D = *d;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (D.cap <= 0) { midi.clear(); return; }

    // host tempo (the MPC's BPM) and beat phase
    double hb = 0, ph = -1;
    if (auto* ph0 = getPlayHead())
        if (auto pos = ph0->getPosition()) {
            if (auto b = pos->getBpm()) hb = *b;
            if (auto ppq = pos->getPpqPosition()) if (pos->getIsPlaying()) ph = *ppq - std::floor(*ppq);
        }
    hostTempo.store(hb);
    D.hostBpmNow = hb; D.hostPhase = ph;

    D.drain();
    D.handleMidi(midi);
    midi.clear();
    D.applySettings();
    D.handleButtons();
    D.handlePlatters();
    D.followSync();

    auto main = getBusBuffer(buffer, false, 0);
    float* outL = main.getNumChannels() > 0 ? main.getWritePointer(0) : nullptr;
    float* outR = main.getNumChannels() > 1 ? main.getWritePointer(1) : nullptr;
    float* cueL = nullptr; float* cueR = nullptr;
    if (getBusCount(false) > 1 && getBus(false, 1) != nullptr && getBus(false, 1)->isEnabled()) {
        auto cue = getBusBuffer(buffer, false, 1);
        if (cue.getNumChannels() > 1) { cueL = cue.getWritePointer(0); cueR = cue.getWritePointer(1); }
    }
    float* aL = D.scratch[0].data(); float* aR = D.scratch[1].data();
    float* bL = D.scratch[2].data(); float* bR = D.scratch[3].data();
    double fxBpm[2];
    for (int dk = 0; dk < 2; ++dk) { const double b = engineDeck[dk].currentBpm(); fxBpm[dk] = b > 0 ? b : hb > 0 ? hb : 120; }
    for (int done = 0; done < n && outR != nullptr;) {
        const int m = juce::jmin(D.cap, n - done);
        engineDeck[0].render(aL, aR, m); engineDeck[1].render(bL, bR, m);
        fx[0].process(aL, aR, m, D.fxs[0], fxBpm[0]); fx[1].process(bL, bR, m, D.fxs[1], fxBpm[1]);
        mixer.process(aL, aR, bL, bR, outL + done, outR + done, cueL ? cueL + done : nullptr, cueR ? cueR + done : nullptr, m, D.ms);
        done += m;
    }
    D.now += n;
    D.publishCues();
}

/* ---------------- state ---------------- */
void WheelProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto& D = *d;
    auto st = apvts.copyState();
    juce::ValueTree x("WHEEL");
    x.setProperty("root", library.root().getFullPathName(), nullptr);
    x.setProperty("lastRoot", lastRoot.getFullPathName(), nullptr);
    for (int dk = 0; dk < 2; ++dk) {
        juce::ValueTree dt("DECK");
        dt.setProperty("index", dk, nullptr);
        const auto trk = deckTrack(dk);
        juce::File f;
        { const juce::SpinLock::ScopedLockType l(D.trackLock); f = D.currentFile[dk]; }
        if (trk != nullptr) {
            if (f == juce::File() && !trk->path.empty()) f = juce::File(juce::String(juce::CharPointer_UTF8(trk->path.c_str())));
            dt.setProperty("file", f.getFullPathName(), nullptr);
            dt.setProperty("pos", status(dk).pos.load(), nullptr);
            dt.setProperty("cue", status(dk).cuePoint.load(), nullptr);
            dt.setProperty("gridShift", D.gridShiftBase[dk].load() + D.gridShift[dk].load(), nullptr);
            const double gb = D.gridBpm[dk].load() > 0 ? D.gridBpm[dk].load() : D.gridBpmBase[dk].load();
            dt.setProperty("gridBpm", gb, nullptr);
            const auto cues = cuesOf(dk);
            for (int i = 0; i < kCues; ++i) {
                const auto& c = cues[(size_t) i];
                if (!c.set()) continue;
                juce::ValueTree ct("CUE");
                ct.setProperty("i", i, nullptr); ct.setProperty("t", c.t, nullptr); ct.setProperty("rgb", (juce::int64) c.rgb, nullptr);
                ct.setProperty("name", juce::String(juce::CharPointer_UTF8(c.name.c_str())), nullptr);
                dt.appendChild(ct, nullptr);
            }
            dt.setProperty("cues", true, nullptr);
        }
        x.appendChild(dt, nullptr);
    }
    for (int k = 0; k < kMapSize; ++k) {
        for (int kind = 0; kind < 2; ++kind) {
            const int t = (kind == 0 ? D.ccMap[k] : D.noteMap[k]).load();
            if (t < 0) continue;
            juce::ValueTree m("MAP");
            m.setProperty("kind", kind == 0 ? "cc" : "note", nullptr);
            m.setProperty("ch", k / 128 + 1, nullptr); m.setProperty("num", k % 128, nullptr);
            m.setProperty("target", t >= kTargetPad ? "pad:" + juce::String(t - kTargetPad) : paramId(t), nullptr);
            if (kind == 0) m.setProperty("mode", D.ccMode[k].load(), nullptr);
            x.appendChild(m, nullptr);
        }
    }
    st.appendChild(x, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
}

void WheelProcessor::setStateInformation(const void* data, int size) {
    auto& D = *d;
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    auto st = juce::ValueTree::fromXml(*xml);
    if (!st.isValid()) return;
    auto x = st.getChildWithName("WHEEL");
    if (x.isValid()) st.removeChild(x, nullptr);

    // parameters: no button edges from the recall, and the momentary controls come back released
    D.suppress.store(true);
    apvts.replaceState(st);
    for (int dk = 0; dk < 2; ++dk) {
        for (int b = 0; b < kBtns; ++b) { auto* p = D.prm[dpi(dk, kBtnParam[b])]; if (p->getValue() > .5f) p->setValueNotifyingHost(0); }
        auto* nd = D.prm[dpi(dk, pNudge)];
        if (std::abs(nd->getValue() - nd->getDefaultValue()) > 1e-6f) nd->setValueNotifyingHost(nd->getDefaultValue());
    }
    D.suppress.store(false);
    D.platResync.store(true);
    if (!x.isValid()) return;

    lastRoot = juce::File(x.getProperty("lastRoot").toString());
    const juce::String root = x.getProperty("root").toString();
    if (root.isNotEmpty()) {
        const juce::File rf(root);
        if (lastRoot == juce::File()) lastRoot = rf;
        const juce::ScopedLock l(D.rootLock);
        D.pendingRoot = rf; D.rootPending = true;
    }

    D.learnTarget.store(-1);
    D.clearMaps();
    for (auto c : x) {
        if (c.hasType("MAP")) {
            const int ch = juce::jlimit(1, 16, (int) c.getProperty("ch", 1)), num = juce::jlimit(0, 127, (int) c.getProperty("num", 0));
            const juce::String tg = c.getProperty("target").toString();
            int t = -1;
            if (tg.startsWith("pad:")) t = kTargetPad + juce::jlimit(0, 15, tg.fromFirstOccurrenceOf(":", false, false).getIntValue());
            else for (int i = 0; i < kParams; ++i) if (paramId(i) == tg) { t = i; break; }
            if (t < 0) continue;
            const int key = (ch - 1) * 128 + num;
            if (c.getProperty("kind").toString() == "note") D.noteMap[key].store(t);
            else { D.ccMap[key].store(t); D.ccMode[key].store(juce::jlimit((int) mAbs, (int) mDetect, (int) c.getProperty("mode", 0))); }
        } else if (c.hasType("DECK")) {
            const int dk = juce::jlimit(0, 1, (int) c.getProperty("index", 0));
            const juce::String path = c.getProperty("file").toString();
            if (path.isEmpty()) continue;
            const juce::File f(path);
            if (!f.existsAsFile()) { loadError[dk] = "Not found: " + path; continue; }
            Impl::Restore r;
            r.on = true;
            r.pos = c.getProperty("pos", 0); r.cuePoint = c.getProperty("cue", -1);
            r.gridShift = c.getProperty("gridShift", 0); r.gridBpm = c.getProperty("gridBpm", 0);
            r.hasCues = c.getProperty("cues", false);
            for (auto cc : c)
                if (cc.hasType("CUE")) {
                    const int i = (int) cc.getProperty("i", -1);
                    if (i < 0 || i >= kCues) continue;
                    auto& hc = r.cues[(size_t) i];
                    hc.t = cc.getProperty("t", -1); hc.rgb = (uint32_t) (juce::int64) cc.getProperty("rgb", 0xCC0000);
                    hc.name = cc.getProperty("name").toString().toStdString();
                }
            loadError[dk].clear();
            D.startLoad(dk, f, r);
        }
    }
}

}  // namespace wheel

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new wheel::WheelProcessor(); }
