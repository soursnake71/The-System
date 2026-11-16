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
                loopMove, roll, savedLoop, touch, move, bend, nudge, censor, seek, sync, syncOff, gridShift, gridBpm, tapBpm } type;
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
    const DeckStatus& status(int d) const { return engineDeck[d].st; }
    std::array<HotCue, kCues> cuesOf(int d) const;            // snapshot (cues set while playing included)
    double hostBpm() const { return hostTempo.load(); }
    float meter(int ch) const;                                // 0,1 channels; 2,3 master L/R
    // pads (from GUI clicks or MIDI notes): pad 0..15, MPC layout (pad 1 bottom-left)
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
