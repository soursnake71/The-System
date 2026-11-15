// Omega DMX as a plugin: an instrument (VST3, AU, standalone app) around DmxEngine.h. The kits are embedded;
// the sequencer follows the host's transport, or runs on its own from the panel's PLAY / STOP (the standalone app).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "DmxEngine.h"
#include "Presets.h"

namespace dmx {

juce::String fmtParam(int i, double v);

class DmxProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    DmxProcessor();
    ~DmxProcessor() override { stopTimer(); }
    void prepareToPlay(double sampleRate, int maxBlock) override { syncParams(); engine.prepare(sampleRate, maxBlock); }
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

    juce::AudioProcessorValueTreeState apvts;   // the nine faders and the 256 sound parameters (dmx/engine.js PARAMS), automatable
    Engine engine;
    void setFader(int g, float v);
    // the sound parameters: set (as the host would), a factory preset, the dice
    void setSound(int i, double v);
    void loadPreset(int i);
    void dice(int part);
    juce::String presetName { "Factory · Stock DMX" };
    bool edited = false;
    static juce::String pid(int i) { return kParamDefs[i].id; }
private:
    void timerCallback() override { syncParams(); }
    void syncParams();                          // parameter values into the engine (message thread: VOICE MOD changes re-render)
    std::array<std::atomic<float>*, kGroups> raw {};
    std::array<std::atomic<float>*, kParams> sraw {};
    bool loading = false;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
};

}  // namespace dmx
