// Omega Solina as a plugin (VST3 / AU / standalone): the Solina.h engine, the Seq.h sequencer synced to the host,
// MIDI in (notes, velocity, sustain, bend, mod wheel, expression, program change, a CC for every control with MIDI learn)
// and MIDI out (the sequencer's notes, keys and controllers through), MIDI file import and export.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <map>
#include "Solina.h"
#include "Seq.h"

namespace solina {

#include "Presets.inc"

juce::String fmtVal(int id, double v);
Params randomPatch(juce::Random& r);

class SolinaProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    SolinaProcessor();
    ~SolinaProcessor() override = default;

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return kPresets; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override { return i >= 0 && i < kPresets ? juce::String(juce::CharPointer_UTF8(PRESETS[i].name)) : juce::String(); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- for the editor (message thread) ----
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboard;
    juce::String presetName = juce::CharPointer_UTF8("Classic \xc2\xb7 Stock Ensemble");
    void setParam(int id, double v);
    double getParam(int id) const { return apvts.getRawParameterValue(PARAMS[id].id)->load(); }
    void applyValues(const Params& p, const juce::String& name);
    void randomize();

    juce::SpinLock songLock;
    Song song;                       // edit under songLock
    void songChanged() { songVersion++; }
    std::atomic<int> songVersion{0};
    juce::String importMidi(const juce::File& f, bool quantize, bool skipDrums);   // "" or an error
    juce::String exportMidi(const juce::File& f, bool wholeSong);

    // transport and settings
    std::atomic<bool> playRequest{false}, stopRequest{false}, panicRequest{false}, recording{false}, quantize{true};
    std::atomic<bool> hostSync{true}, thru{true}, clockOut{false}, pcChangesPreset{true}, learn{false};
    std::atomic<int> inChannel{0}, outChannel{1};    // 0 = omni; 1-16
    std::atomic<int> learnTarget{-1}, learnedCC{-1};
    std::atomic<bool> learnChanged{false};
    std::atomic<bool> playing{false};
    std::atomic<int> playPat{0}; std::atomic<double> playTick{0};
    std::atomic<double> hostBpm{0};
    std::map<int, int> ccMap;        // cc -> param (message thread; copied to ccLive for the audio thread)
    void setCC(int cc, int param);
    void resetCC();
    juce::SpinLock logLock;
    juce::StringArray midiLog() { const juce::SpinLock::ScopedLockType l(logLock); return log; }

private:
    void timerCallback() override;
    std::unique_ptr<Engine> engine;
    SeqPlayer seq;
    Params last{};
    double fs = 44100;
    int program = 0;
    bool hostWasPlaying = false;
    std::array<int, 128> ccLive{};
    juce::SpinLock ccLock;
    struct Pending { int id; double v; };
    juce::AbstractFifo fifo{256}; std::array<Pending, 256> pend{};
    std::atomic<int> pcPending{-1};
    juce::StringArray log;
    juce::AbstractFifo logFifo{128}; std::array<juce::uint32, 128> logQ{};
};

} // namespace solina
