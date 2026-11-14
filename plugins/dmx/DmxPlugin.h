// Omega DMX as a plugin: an instrument (VST3, AU, standalone app) around DmxEngine.h. The kits are embedded;
// the sequencer follows the host's transport, or runs on its own from the panel's PLAY / STOP (the standalone app).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "DmxEngine.h"

namespace dmx {

class DmxProcessor : public juce::AudioProcessor {
public:
    DmxProcessor();
    void prepareToPlay(double sampleRate, int) override { engine.prepare(sampleRate); }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    // programs = the ten patterns of the kit (MIDI program change picks them too)
    int getNumPrograms() override { return kPatterns; }
    int getCurrentProgram() override { return engine.current(); }
    void setCurrentProgram(int i) override { if (i >= 0 && i < kPatterns) engine.keypad(i == 9 ? '0' : (char) ('1' + i)); }
    const juce::String getProgramName(int i) override { return i >= 0 && i < kPatterns ? juce::String(i + 1) + " " + engine.pats[(size_t) engine.kit][(size_t) i].name : juce::String(); }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;   // the eight faders, automatable
    Engine engine;
    void setFader(int g, float v);
private:
    std::array<std::atomic<float>*, kGroups> raw {};
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
};

}  // namespace dmx
