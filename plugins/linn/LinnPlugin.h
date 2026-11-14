// Omega Linn as a plugin: the LinnDrum circuit model (Linn.h) with its whole front panel, mixer and sequencer
// (Machine.h) working inside your DAW. PLAY/STOP follows the DAW's transport (FOLLOW DAW) or runs on its own; MIDI notes
// play the 23 sounds (General MIDI drum notes, velocity picks the dynamic) and record into a pattern while recording.
// Outputs: the stereo mix plus OUT 1-8 (bass, snare, sidestick, hihat, toms, cymbals, congas, percussion), like the
// hardware's individual outputs. LOAD FOLDER loads a kit; IMPORT MIDI puts a MIDI file in a pattern or a song.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include "Machine.h"
#include "Kit.h"

namespace linn {
#include "Presets.inc"
}

namespace linn {

class LinnProcessor : public juce::AudioProcessor {
public:
    LinnProcessor();
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
    juce::String backupJson();
    bool restoreJson(const juce::String&);
    // sounds: one file into a ROM group, a whole folder (a kit, by the files' names), or the built-in ROM back
    bool loadSound(int g, const juce::File&);
    juce::String loadFolder(const juce::File& dir);
    void resetSounds();
    void resetSound(int g);
    juce::String romName(int g) const;
    // a MIDI file into the pattern shown, or as the song shown (cut into patterns of `bars` bars); ec: ERROR CORRECT or -1
    juce::String importMidi(const juce::File&, bool asSong, int bars, int ec, bool takeTempo);
    int groupOfChannel(int v) const { return v == 0 && value(pid::bassChip) >= 0.5 ? G::BASS2 : CH_GROUP[v]; }
    void setMetroAlways(bool b) { metroAlways = b; }
    bool getMetroAlways() const { return metroAlways; }
    int shownPattern() const { return shownPat.load(); }

private:
    void readParams();
    void handleMidi(const juce::MidiMessage& m);
    void publish();
    juce::var memToVar();
    void varToMem(const juce::var&);
    static int noteToSound(int n, int vel);
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Machine> machine;
    juce::CriticalSection memLock;
    Params cur{};
    std::array<std::atomic<float>*, NP> raw{};
    std::atomic<float>* follow = nullptr;
    Rom factoryRom, rom;
    std::array<juce::String, NG_ROM> romNames;
    std::array<bool, NG_ROM> userRom{};
    std::atomic<bool> romDirty{false}, metroAlways{false};
    std::atomic<int> shownPat{0};
    std::array<int, 256> q{}; std::atomic<int> qW{0}; int qR = 0;
    juce::SpinLock dispLock; Display disp;
    bool hostWasPlaying = false;
    int program = 0;
    std::vector<float> scratch[16];
};

} // namespace linn
