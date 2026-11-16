// Omega Wheel of Steel: a two-turntable DJ instrument (VST3 / AU / standalone). Built to be played from an Akai MPC:
// every fader, knob and button is a host parameter (Q-Links map straight onto them), the 16 pads trigger hot cues,
// loops, rolls and transport by MIDI note, and any MIDI controller works through MIDI learn.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include "Engine.h"
#include "Library.h"

namespace wheel {

// host parameters (ids are stable: saved in projects). Deck params exist twice: "a_" and "b_" prefixes.
namespace pid {
    // per deck (index with deckParam(deck, ...))
    inline const char* const deckIds[] = { "play", "cue", "pitch", "range", "rpm45", "keylock", "sync", "slip", "quant",
                                           "platter", "touch", "nudge", "censor", "start", "brake",
                                           "trim", "hi", "mid", "low", "filter", "fader", "pfl", "xfassign",
                                           "fxtype", "fxbeats", "fxdepth", "fxparam", "fxon" };
    // global
    inline const char* const globalIds[] = { "xfader", "xfcurve", "fadercurve", "hamster", "master", "cuemix", "cuegain", "padmode", "padbase", "hostsync" };
}
juce::String deckParam(int deck, const char* id);   // "a_play" / "b_play"

// pad modes for the 16 MPC pads (left two columns = deck A, right two = deck B, 8 pads each)
enum class PadMode { cues, loops, rolls, transport, count };

// what a GUI / MIDI action asks the audio thread to do
struct Cmd {
    enum Type { load, playPause, play, cue, hotCue, clearCue, autoLoop, loopIn, loopOut, reloop, loopExit, loopHalve, loopDouble,
                loopMove, roll, savedLoop, touch, move, bend, nudge, censor, seek, sync, syncOff, gridShift, gridBpm, tapBpm,
                setCuePoint } type;
    // fields: hotCue/clearCue/savedLoop i = slot; autoLoop/roll/loopMove v = beats (roll: on = held); cue/touch/censor/
    // play on = down/on; move/bend/seek/gridShift v = seconds; nudge v = -1..1 (0 releases); gridBpm v = BPM;
    // tapBpm v = tap time in ms (Time::getMillisecondCounterHiRes) or 0 = now; sync: on = true turns sync on (and
    // matches tempo + phase now), on = false toggles; syncOff turns it off; setCuePoint v = seconds (unquantized).

    int deck = 0; int i = 0; double v = 0; bool on = false;
    std::shared_ptr<const Track> track;   // for load
};

class WheelProcessor : public juce::AudioProcessor, private juce::Timer, private juce::AudioProcessorValueTreeState::Listener {
public:
    WheelProcessor();
    ~WheelProcessor() override;
    void prepareToPlay(double sr, int block) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;   // main stereo out; optional "Cue" stereo out
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;   // params, library root, loaded files + positions, cues, MIDI map
    void setStateInformation(const void*, int) override;

    // ---- message thread API (editor) ----
    juce::AudioProcessorValueTreeState apvts;
    Library library;
    void send(const Cmd& c);                                  // lock-free FIFO to the audio thread
    void loadTrack(int deck, const juce::File& f);            // decodes on a background thread, then Cmd::load
    std::atomic<bool> loading[2] { {false}, {false} };
    juce::String loadError[2];
    std::shared_ptr<const Track> deckTrack(int d) const;      // the GUI's view of what is on each deck
    const DeckStatus& status(int deck) const { return engineDeck[deck].st; }
    std::array<HotCue, kCues> cuesOf(int d) const;            // snapshot (cues set while playing included)
    double hostBpm() const { return hostTempo.load(); }
    float meter(int ch) const;                                // 0,1 channels; 2,3 master L/R
    // pads (from GUI clicks or MIDI notes): pad 0..15, MPC layout (pad 1 bottom-left, rows of 4 going up: pad index =
    // row * 4 + column). Left two columns = deck A, right two = deck B; each deck's 8 slots go bottom-to-top, left
    // to right (slot = row * 2 + column % 2): A = pads 1,2,5,6,9,10,13,14, B = pads 3,4,7,8,11,12,15,16.
    //   Cues:      hot cue 1..8 (empty: set; set: jump / play)
    //   Loops:     auto loop 1/4, 1/2, 1, 2, 4, 8, 16, 32 beats (toggle)
    //   Rolls:     loop roll 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4 beats while held
    //   Transport: PLAY, CUE, SYNC, CENSOR, NUDGE -, NUDGE +, LOOP IN, LOOP OUT (looping: exit)
    // MIDI notes: on any channel but 2/3, note - padbase = pad (padbase 37 = MPC pad 1, 36 = chromatic C1);
    // padbase+16..+19 (MPC bank B pads 1-4) switch the pad mode Cues/Loops/Rolls/Transport. Channel 2 notes = deck A
    // slot (note % 8), channel 3 = deck B slot, in the current mode. Learned notes override all of this.
    void pad(int index, bool down, float velocity = 1);
    PadMode padMode() const;
    juce::String padLabel(int index) const;                   // what a pad does in the current mode
    // MIDI learn: next CC/note received is bound to the parameter (or "pad:<n>")
    void learn(const juce::String& paramIdOrPad);
    juce::String learning() const;
    void clearMappings();
    juce::String mappingSummary() const;
    juce::File lastRoot;

private:
    void timerCallback() override;
    void parameterChanged(const juce::String&, float) override;   // buttons-as-parameters -> Cmds (play, cue, sync...)
    struct Impl; std::unique_ptr<Impl> d;
    Deck engineDeck[2]; Fx fx[2]; Mixer mixer;
    std::atomic<double> hostTempo { 0 };
};

}  // namespace wheel
