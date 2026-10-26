#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Sky.h"
#include "Synth.h"
#include "Presets.h"

struct LaneInfo { const char* id; const char* name; const char* to; juce::uint32 colour; int engine, channel; float vol, rev, dly; };
extern const LaneInfo laneInfo[6];
enum { lArp, lRise, lFall, lDrone, lGrid, lHit, numLanes };

class SkyProcessor : public juce::AudioProcessor
{
public:
    SkyProcessor();
    ~SkyProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Sky Sequencer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 4.0; }
    // presets live in the plugin's Presets tab, not as host programs: a host switching programs while it restores
    // a project would overwrite the restored settings
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void loadPreset (int index);
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // where and what to watch (saved with the project)
    void setLocation (double lat, double lon, const juce::String& placeName);
    void setFeed (int source, double radius, const juce::String& localUrl);
    sky::FeedSettings feedSettings() const;

    // presets and generators (message thread)
    void applyPreset (const juce::var& preset);
    void generatePreset();
    void randomKit();
    void randomPatch (int lane);
    juce::String presetName() const { return state.getProperty ("preset").toString(); }
    juce::String patchParamId (int lane, int spec) const { return juce::String (laneInfo[lane].id) + "_" + sky::patchSpecs()[(size_t) spec].id; }
    juce::String placeName() const { return state.getProperty ("place").toString(); }

    juce::AudioProcessorValueTreeState params;
    juce::ValueTree state { "SkyLocation" };
    sky::SkyModel model;
    sky::FeedThread feed { model };
    juce::ThreadPool lookups { 1 };   // place search and locate-me requests

    // for the radar: which planes just played, and the step the grid is on
    struct Flash { juce::uint32 hash; int lane, cell; };
    juce::AbstractFifo flashFifo { 256 };
    std::array<Flash, 256> flashes {};
    std::atomic<int> currentStep { -1 };
    std::atomic<bool> running { false }, emergency { false };
    std::atomic<double> currentBpm { 96 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void pushSettings();
    void playStep (juce::int64 step, int offset, juce::MidiBuffer& midi, double stepSec);
    void play (int lane, int note, float vel, double durSec, float pan, int offset, juce::MidiBuffer& midi, juce::uint32 hash, int cell = -1);
    int degToMidi (int deg, int base) const;
    void flushNoteOffs (juce::MidiBuffer&, int n);
    void allNotesOff (juce::MidiBuffer&, int offset);

    sky::Synth synth;
    sky::Snapshot snap, pending;
    juce::SpinLock snapLock; bool pendingFresh = false;

    struct NoteOff { juce::int64 at; int ch, note; };
    std::array<NoteOff, 512> offs {}; int numOffs = 0;
    struct ArpSlot { juce::uint32 hash; int i; };
    std::array<ArpSlot, 64> arp {};

    double sr = 48000, freePos = 0;
    juce::int64 sampleClock = 0, lastStep = -1;
    bool wasPlaying = false;
    std::array<std::atomic<float>*, numLanes> pOn {}, pEngine {}, pCh {}, pVol {}, pRev {}, pDly {}, pDrum {}, pDv {};
    std::array<std::array<std::atomic<float>*, 21>, numLanes> pPatch {};
    std::array<std::atomic<float>*, 18> pKit {};
    std::atomic<float> *pDMode = nullptr, *pDMix = nullptr, *pDFb = nullptr, *pDTone = nullptr;
    std::array<juce::int64, numLanes> lastFire {}; std::array<bool, numLanes> loopOn {};
    juce::int64 stepNow = 0;
    juce::Random rng;
    void setParam (const juce::String& id, float value);
    void drumStep (juce::int64 step, int offset, juce::MidiBuffer& midi, double stepSec);
    sky::Patch patchFor (int lane) const;
    std::atomic<float> *pKey = nullptr, *pScale = nullptr, *pBpm = nullptr, *pMode = nullptr, *pThresh = nullptr, *pWindow = nullptr, *pMaster = nullptr;
    struct Poller : juce::Timer { std::function<void()> fn; void timerCallback() override { fn(); } } poller;
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkyProcessor)
};

juce::StringArray scaleNames();
const std::vector<int>& scaleSteps (int index);
extern const int windowMinutes[7];
