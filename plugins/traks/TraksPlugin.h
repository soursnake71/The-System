// Omega Traks as a plugin: the Drumtraks circuit model (Traks.h) with its whole front panel and sequencer (Machine.h)
// working inside your DAW. RUN / STOP follows the DAW's transport (FOLLOW DAW), or runs on its own; MIDI notes play
// the 13 sounds (General MIDI drum notes, velocity-sensitive, accent above ACCENT VELOCITY) and record into a pattern
// in RECORD MODE. Outputs: the stereo mix plus CH1-CH6, the machine's six channel outputs.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include "Machine.h"

namespace traks {
#include "Presets.inc"
}

namespace traks {

class TraksProcessor : public juce::AudioProcessor {
public:
    TraksProcessor();
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
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return kPresets; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override { return i >= 0 && i < kPresets ? juce::String(juce::CharPointer_UTF8(PRESETS[i].name)) : juce::String(); }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- editor side (message thread)
    void key(int id, bool down) { const int w = qW.load(); q[(size_t) (w & 255)] = id | (down ? 0x100 : 0); qW = w + 1; }
    Display display() { const juce::SpinLock::ScopedLockType l(dispLock); return disp; }
    void dice(int part);
    void setValue(int index, double v);
    double value(int index) const { return raw[(size_t) index] ? (double) raw[(size_t) index]->load() : 0.0; }
    std::function<void()> onProgram;
    std::array<std::atomic<float>, NV> meters{};
    std::atomic<int> selected{0};
    // the whole memory, settings and ROMs as the app's backup (.json): the CASSETTE keys
    juce::String backupJson();
    bool restoreJson(const juce::String&);
    // a sound file onto a voice (resampled to the ROM clock and companded to 8 bits), or the factory ROM back
    bool loadSound(int v, const juce::File&);
    void resetSound(int v);
    juce::String romName(int v) const { return romNames[(size_t) v]; }
    // the built-in ROM: the Drumtraks recordings; a sound with no recording is empty (never synthesized)
    juce::String factoryName(int v) const { return factoryRom[(size_t) v].codes.size() <= 2 ? "EMPTY (no recording yet)" : "Drumtraks"; }
    void setMetroAlways(bool b) { metroAlways = b; }
    bool getMetroAlways() const { return metroAlways; }

private:
    void readParams();
    void handleMidi(const juce::MidiMessage& m);
    void publish();
    juce::var memToVar();
    void varToMem(const juce::var&);
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Machine> machine;
    juce::CriticalSection memLock;            // the machine's memory: the audio thread and state save / load
    Params cur{};
    std::array<std::atomic<float>*, NP> raw{};
    std::atomic<float>* accVel = nullptr; std::atomic<float>* follow = nullptr;
    std::vector<Rom> factoryRom, rom;
    std::array<juce::String, NV> romNames;
    std::atomic<bool> romDirty{false}, metroAlways{false};
    std::array<int, 256> q{}; std::atomic<int> qW{0}; int qR = 0;
    juce::SpinLock dispLock; Display disp;
    bool hostWasPlaying = false;
    int program = 0;
    std::vector<float> scratch[12];
    juce::var pendingMem; bool hasPendingMem = false;
};

} // namespace traks
