// Omega MXR Flanger: the M-117 circuit model (M117.h) as a plugin, with the pedal's photo as its front panel.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "M117.h"

namespace flanger {

struct Preset { const char* name; const char* desc; double v[m117::kNumParams]; };
extern const Preset kPresets[];
extern const int kNumPresets;

class Processor : public juce::AudioProcessor {
public:
    Processor();
    ~Processor() override = default;

    juce::AudioProcessorValueTreeState& state() { return apvts; }
    juce::RangedAudioParameter* param(int i) { return params[(size_t) i]; }

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Omega MXR Flanger"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return kNumPresets; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    static juce::String formatValue(int param, float v);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::AudioProcessorValueTreeState apvts;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<std::atomic<float>*> raw;
    m117::Engine engine;
    juce::AudioBuffer<float> scratch;   // second output channel when the output is mono
    int program = 0;
};

}  // namespace flanger
