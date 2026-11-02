// Omega SID 64 as a VST3 / AU instrument (and a standalone app): the SidSynth engine behind JUCE parameters,
// with the C64 screen (ScreenUI.h) in a monitor and a breadbin keyboard.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "ScreenUI.h"

namespace sid {

class SidProcessor : public juce::AudioProcessor, public ScreenUI::Host, private juce::AsyncUpdater, private juce::Timer {
public:
    SidProcessor();
    ~SidProcessor() override = default;

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return presetCount(); }
    int getCurrentProgram() override { return engine.program.load(); }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ScreenUI::Host (message thread)
    Synth& synth() override { return engine; }
    float get(int id) override { return raw[(size_t) id]->load(); }
    void set(int id, float v) override;
    void loadPreset(int i) override { setCurrentProgram(i); }
    void initPatch() override;
    void randomize() override;
    bool isPlugin() const override { return !juce::JUCEApplicationBase::isStandaloneApp(); }
    void loadSampleFile() override;
    float recordedSeconds() override { return (float) (engine.recPos.load() / std::max(1.0, getSampleRate())); }
    juce::String loadSampleFrom(const juce::File& f);   // "" when it worked

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<int> uiPresetVersion { 0 };   // bumped when a preset loads, so the editor's screen follows

private:
    void handleAsyncUpdate() override;   // program changes from MIDI
    void timerCallback() override { engine.pollSample(); }   // converts the sample again when its settings change
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::FileChooser> chooser;
    void applyValues(const std::array<float, NP>& v);
    Synth engine;
    std::array<std::atomic<float>*, NP> raw {};
    std::atomic<int> pendingProgram { -1 };
    juce::AudioBuffer<float> tmp;
};

}  // namespace sid
