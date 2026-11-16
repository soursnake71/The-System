// Omega Wheel of Steel DSP: two direct-drive decks, a two-channel DJ mixer and beat FX. Plain C++17, no JUCE, so it
// builds into the test runner too. Everything here runs on the audio thread; the processor feeds it Cmds.
#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include "Track.h"

namespace wheel {

// the platter: the SL-1200 MK2 feel. Motor torque gets to 33 1/3 in ~0.7 s (start time knob scales it), brake ~0.35 s
// (brake knob), a hand on the vinyl stops it dead and the record then follows the hand with a few ms of slip mat give;
// letting go, the motor pulls it back up. Speeds: 33 1/3 or 45 (45 = x1.35 the track's natural speed).
struct DeckSettings {
    float pitch = 0;          // -1..1 of the fader
    float pitchRange = .08f;  // .08, .16, .50
    bool rpm45 = false;
    bool keyLock = false;
    bool slip = false;
    bool quantize = true;     // cues / loops snap to the beatgrid
    float startTime = .7f;    // s to full speed (0.05 .. 4)
    float brakeTime = .35f;   // s to stop (0.05 .. 6)
};

struct DeckStatus {           // the audio thread writes, the GUI reads (relaxed atomics)
    std::atomic<double> pos { 0 }, rate { 0 }, shadow { 0 }, bpmNow { 0 }, angle { 0 };
    std::atomic<bool> playing { false }, touched { false }, looping { false }, rolling { false }, censoring { false }, synced { false };
    std::atomic<double> loopA { -1 }, loopB { -1 }, cuePoint { 0 };
    std::atomic<float> peakL { 0 }, peakR { 0 };
};

class Deck {
public:
    void prepare(double hostSr, int maxBlock);
    void load(std::shared_ptr<const Track> t);          // audio thread only (via Cmd); resets transport, cue at first beat
    const Track* track() const { return trk.get(); }
    DeckSettings set;
    DeckStatus st;

    // transport
    void playPause();                 // Start·Stop: motor on/off (with start/brake curves)
    void play(bool on);
    void cue(bool down);              // CDJ/Serato cue: stopped -> set cue (quantized); playing -> jump back & stop; held while stopped -> preview
    void seek(double sec);
    void nudge(float amount);         // -1..1 held: +-4 % (amount scales), 0 = release
    void bend(double secDelta);       // jog-wheel edge: push the record by secDelta seconds
    // hand on the record (scratch). touch(true) then move(sec) for every platter movement; touch(false) lets go.
    void touch(bool on);
    void move(double secDelta);
    void censor(bool on);             // plays reverse while held, then drops back where it would have been
    // hot cues
    void hotCue(int i, bool down);    // empty: set (quantized); set: jump & play (held while stopped = preview)
    void clearCue(int i);
    // loops (beats: 1/32 .. 32)
    void autoLoop(double beats);      // toggles; starting on the nearest beat when quantized
    void loopIn(); void loopOut(); void reloop(); void loopExit();
    void loopHalve(); void loopDouble(); void loopMove(double beats);
    void roll(double beats, bool on); // loop roll while held; slip back on release
    void savedLoop(int i);            // a Serato saved loop
    // sync: match the other deck's tempo (and phase); 'master' bpm comes from the other deck or the host
    void syncTo(double masterBpm, double masterBeatPhase, bool phase);   // one-shot
    void setSync(bool on) { syncOn = on; }
    bool syncing() const { return syncOn; }
    double currentBpm() const;        // as played
    double beatPhase() const;         // 0..1 within the current beat
    double beatLength() const;        // seconds at the track's own tempo

    // render n samples (stereo, added to nothing: overwrites) at the host rate
    void render(float* L, float* R, int n);

    // live cue/beat editing (Serato-style: the grid can be fixed on the fly)
    void gridShift(double sec); void gridBpm(double bpm);
    std::shared_ptr<const Track> trackPtr() const { return trk; }
    std::array<HotCue, kCues> cues;   // working copy (edits here); the processor copies back for state
private:
    std::shared_ptr<const Track> trk; double hostSr = 48000; bool syncOn = false; double bpmOverride = 0, gridOff = 0;
    // implementation state is up to Engine.cpp
    struct Impl; std::shared_ptr<Impl> d;
};

// DJ mixer, Serato/Rane style: per channel trim, 3-band isolator EQ (kill at the bottom), one-knob filter (left LP,
// right HP, resonant), channel fader with curve, crossfader with curve (smooth blend .. sharp scratch cut) and
// hamster switch, master gain with a soft limiter, cue (headphone) bus with cue/master blend.
struct ChannelSettings { float trim = 0 /*dB -12..12*/, hi = 0, mid = 0, low = 0 /*dB -26 (kill)..+6*/, filter = 0 /*-1..1*/, fader = 1; bool pfl = false; int xfAssign = 0 /*0 A side, 1 thru, 2 B side*/; };
struct MixerSettings { ChannelSettings ch[2]; float xfader = 0 /*-1..1*/; float xfCurve = .5f /*0 blend..1 cut*/; float faderCurve = .3f; bool hamster = false; float master = 0 /*dB*/; float cueMix = 0 /*0 cue..1 master*/; float cueGain = 0; };

// beat FX per deck (Serato-style: one FX unit per deck, beat-synced): echo, ping-pong, reverb, flanger, phaser,
// bitcrush, gater, brake/spinback handled by the deck itself.
enum class FxType { off, echo, pingpong, reverb, flanger, phaser, crush, gate, count };
struct FxSettings { FxType type = FxType::off; float beats = .75f /*0.0625..8*/; float depth = .5f /*wet*/; float param = .5f; bool on = false; };

class Fx {
public:
    void prepare(double sr, int maxBlock);
    void process(float* L, float* R, int n, const FxSettings& s, double bpm);  // in place
    void reset();
private: struct Impl; std::shared_ptr<Impl> d;
};

class Mixer {
public:
    void prepare(double sr, int maxBlock);
    // a, b: each deck's post-FX stereo; writes master and cue outputs (cueL/R may be null)
    void process(const float* aL, const float* aR, const float* bL, const float* bR, float* outL, float* outR, float* cueL, float* cueR, int n, const MixerSettings& s);
    std::atomic<float> meter[2] { {0}, {0} }, masterL { 0 }, masterR { 0 };   // peak, linear
private: struct Impl; std::shared_ptr<Impl> d;
};

}  // namespace wheel
