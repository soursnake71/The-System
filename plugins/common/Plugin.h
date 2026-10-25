// The JUCE side shared by every Omega plugin: parameters and presets from the Spec, MIDI split into the engine
// at the right sample, host tempo, state save/restore, and a knob-panel editor.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Spec.h"
#include "Format.h"

namespace omega {

Spec makeSpec();   // each plugin defines this

class Processor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener {
public:
    Processor();
    ~Processor() override;

    const Spec& spec() const { return sp; }
    juce::AudioProcessorValueTreeState& state() { return apvts; }

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return sp.name; }
    bool acceptsMidi() const override { return sp.midiIn || sp.synth; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return sp.synth ? 2.0 : 8.0; }

    int getNumPrograms() override { return juce::jmax(1, (int) sp.presets.size()); }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override { return i >= 0 && i < (int) sp.presets.size() ? juce::String(sp.presets[(size_t) i].name) : juce::String("Default"); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::String getText() const { const juce::ScopedLock l(textLock); return text; }
    void setText(const juce::String& t);

private:
    void parameterChanged(const juce::String&, float) override { dirty = true; }
    void pushValues();
    static juce::AudioProcessorValueTreeState::ParameterLayout layoutFor(const Spec&);
    static BusesProperties busesFor(const Spec&);

    Spec sp;
    juce::AudioProcessorValueTreeState apvts;
    std::unique_ptr<Engine> engine;
    std::vector<juce::RangedAudioParameter*> plist;
    std::vector<double> vals;
    juce::AudioBuffer<float> inBuf, scBuf, outBuf;
    std::atomic<bool> dirty { true };
    int program = 0;
    juce::String text;
    juce::CriticalSection textLock;
    std::atomic<bool> textDirty { false };
};

}  // namespace omega
