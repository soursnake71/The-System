// Omega CMI as a plugin: the keyboard instrument on MIDI channel 1 (or every channel), Page R's eight tracks
// following the host's transport (or their own clock), the library as programs, user samples on Page 8.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/Library.h"

namespace cmi {

struct PDef { const char* id; const char* name; float min, max, def, step; };
// the Page 7 controls of the keyboard instrument, then the machine's own settings
inline const std::vector<PDef>& paramDefs() {
    static const std::vector<PDef> d = {
        { "sound", "Sound", 0, MAX_SOUNDS - 1, 0, 1 },
        { "level", "Level", 0, 1, 0.85f, 0 },
        { "filter", "Filter", 0, 255, 255, 1 },
        { "attack", "Attack", 0, 31, 0, 0 },
        { "damp", "Damping", 0, 31, 10, 0 },
        { "sustain", "Sustain", 0, 1, 1, 1 },
        { "vib", "Vibrato depth", 0, 1, 0, 0 },
        { "vibRate", "Vibrato speed", 0.5f, 12, 5.5f, 0 },
        { "vibDelay", "Vibrato delay", 0, 2, 0.25f, 0 },
        { "porta", "Portamento", 0, 1, 0, 0 },
        { "velVol", "Velocity > level", 0, 1, 0.6f, 0 },
        { "velFilt", "Velocity > filter", 0, 1, 0.3f, 0 },
        { "transpose", "Transpose", -24, 24, 0, 1 },
        { "fine", "Fine tune", -100, 100, 0, 0 },
        { "loop", "Loop", 0, 1, 1, 1 },
        { "seg", "Segment time", 0.005f, 0.2f, 0.03f, 0 },
        { "poly", "Voices", 1, 8, 8, 1 },
        { "bend", "Bend range", 0, 12, 2, 1 },
        { "master", "Master", 0, 1.5f, 1, 0 },
        { "hiss", "Noise floor", 0, 4, 1, 0 },
        { "spread", "Stereo spread", 0, 1, 0.3f, 0 },
        { "multi", "Multitimbral MIDI", 0, 1, 0, 1 },
        { "sync", "Sync to host", 0, 1, 1, 1 },
        { "tempo", "Speed (BPM)", 40, 240, 120, 0 },
        { "swing", "Swing", 0, 0.5f, 0, 0 },
        { "songMode", "Song mode", 0, 1, 0, 1 },
    };
    return d;
}

class CmiProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    CmiProcessor();
    ~CmiProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return Library::get().builtIn; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- for the editor ----
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboard;
    Kit kit;
    Engine engine;
    float get(const char* id) const { auto* p = apvts.getRawParameterValue(id); return p ? p->load() : 0.f; }
    void set(const char* id, float v);
    void loadSound(int index);                   // a sound and the controls it was saved with
    juce::String loadSample(const juce::File& f, int slot, int root, bool loop);   // "" or the error
    void preview(int track) { previewReq = track; }
    void seqPlay(bool on) { engine.seqRun = on; }
    std::atomic<float> meter { 0 };

private:
    void timerCallback() override;
    Controls controlsNow() const;
    void writeSeq(juce::XmlElement&) const;
    void readSeq(const juce::XmlElement&);

    int program = 0;
    std::atomic<int> pendingProgram { -1 }, previewReq { -1 };
    juce::AudioFormatManager formats;
    std::vector<float> bufL, bufR;
    std::vector<std::atomic<float>*> raw;
};

}  // namespace cmi
