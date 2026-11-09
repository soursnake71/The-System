// Omega DR-110 as a plugin: the circuit model (DR110.h) played by your DAW. The DAW is the sequencer: MIDI notes
// trigger the voices (General MIDI drum notes by default; velocity at or above ACCENT VELOCITY is an accent, and the
// ACC and SWEEP notes work like the rear panel's trigger inputs). Outputs: the stereo mix plus the separate outputs
// BD, SD, CP (clap, knuckle) and HH/CY, as on the rear panel. The editor draws the machine's front panel and the
// voice-mod panel; the display follows the DAW's bar and shows the notes it plays.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include "DR110.h"

namespace dr110 {
#include "Presets.inc"
}

namespace dr110 {

class DR110Processor : public juce::AudioProcessor {
public:
    DR110Processor();
    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return kPresets; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override { return i >= 0 && i < kPresets ? juce::String(juce::CharPointer_UTF8(PRESETS[i].name)) : juce::String(); }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::String pidOf(int i) { return juce::String(params()[(size_t) i].id); }

    // ---- editor side (message thread)
    void hit(int v, bool accent) { const int w = qW.load(); q[(size_t) (w & 63)] = (v & 15) | (accent ? 16 : 0); qW = w + 1; }
    void sweepNow() { hit(7, false); }
    void releaseKey(int v) { const int w = qW.load(); q[(size_t) (w & 63)] = (v & 15) | 32; qW = w + 1; }
    // the dice: part 0 everything, 1 voices, 2 pad EQ, 3 ADSR + drive, 4 reverb + delay, 5 master (DICE_PARTS)
    void dice(int part);
    void setValue(int index, double v);
    double value(int index) const { return raw[(size_t) index] ? (double) raw[(size_t) index]->load() : cur[(size_t) index]; }
    std::function<void()> onProgram;          // the editor's preset menu follows program changes
    // the LCD: notes of the bar that's playing, by 16th (bits: 1 BD 2 SD 4 OH 8 CH 16 CY 32 HC 64 KN 128 ACC)
    std::array<std::atomic<int>, 16> grid{};
    std::atomic<int> hostStep{-1}, hostBar{0};
    std::atomic<float> hostBpm{120};
    std::atomic<bool> hostPlaying{false};
    std::array<std::atomic<float>, NV> meters{};
    int midiNote(int v) const { return notes[(size_t) v]; }

private:
    void readParams();
    void handleMidi(const juce::MidiMessage& m, double ppq);
    void markGrid(int v, bool acc, double ppq);
    std::unique_ptr<Engine> engine;
    Params cur{};
    std::array<std::atomic<float>*, NP> raw{};
    std::atomic<float>* accVel = nullptr;
    std::array<Sample, NV> samples;
    std::array<int, 64> q{}; std::atomic<int> qW{0}; int qR = 0;
    std::array<int, NV> notes{{36, 38, 46, 42, 49, 39, 37}};
    int accNote = 33, sweepNote = 34;
    bool accHeld = false;
    int lastBar = -1, program = 0;
    std::vector<float> scratch[8];
};

} // namespace dr110
