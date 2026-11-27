// Omega XR10 as a plugin (VST3 / AU / standalone): the XR10-style drum machine engine (xr10/core, the same C++ the
// Android app runs as WebAssembly), synced to the host's transport, played from MIDI and from its own front panel.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "../../xr10/core/xr10.h"
#include "../../xr10/rack/rack.h"

namespace xr10 {

// what the editor shows, copied from the engine under the lock
struct Snapshot {
    xr::Status st {};
    int keys[xr::NKEYS] {};
    int params[xr::NVP] {};
    bool user[xr::NSOUNDS] {};
    bool packed[xr::NSOUNDS] {};
    unsigned char grid[xr::Machine::GRID_ROWS * 33] {};
    int steps = 16, songLen = 0, songTempo = 0;
    xr::SongStep song[xr::MAXSTEP] {};
    char patName[10] {};
    // the fold-out
    int padParams[xr::NPADS][3] {};   // LEVEL, PAN, FX SEND of each pad's sound
    unsigned char mute[xr::NSOUNDS] {}, solo[xr::NSOUNDS] {};
    float meter[xr::NSOUNDS] {}, outPeak[2] {}, rackIn = 0, rackOut = 0, rackRet = 0.8f, slotPeak[4] {};
    int slotType[4] {}, slotFlags[4] {};
};

class Processor : public juce::AudioProcessor {
public:
    Processor();
    ~Processor() override = default;

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

    // the 99 patterns as programs
    int getNumPrograms() override { return xr::NPAT; }
    int getCurrentProgram() override;
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- used by the editor (message thread); every call takes the engine lock briefly ----
    template <class F> void with(F f) { const juce::SpinLock::ScopedLockType l(lock); f(*m); }
    template <class F> void withRack(F f) { const juce::SpinLock::ScopedLockType l(lock); f(rack); }   // the fold-out's effects rack
    xrack::Rack rack;
    float outPeak[2] {};
    bool foldOpen = false;
    void snapshot(Snapshot& s, int gridBar);
    void patternNames(std::vector<juce::String>& out);
    juce::String loadSample(int sound, const juce::File& f);
    void sampleWave(int sound, std::vector<short>& out);
    juce::MemoryBlock saveMemory();
    juce::MemoryBlock exportMidi(bool song);        // a Standard MIDI File of the current part, or the whole song
    int importMidi(const juce::MemoryBlock& b);      // into the current part: bars read, 0 nothing usable, -1 not MIDI
    juce::String loadSampleSet(const juce::Array<juce::File>& files);   // each file onto the sound its name says
    bool loadMemory(const juce::MemoryBlock& b);

    juce::AudioFormatManager formats;
    float editorScale = 0.8f;
    int editorTab = 0;

private:
    std::unique_ptr<xr::Machine> m;
    juce::SpinLock lock;
    double rate = 44100;
    std::vector<float> sendL, sendR, tmpR;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

}  // namespace xr10
