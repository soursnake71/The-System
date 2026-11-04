// Omega Drum Trigger: listens to a drum track (one close mic) and turns every hit into a MIDI note, for sample
// replacement. Three ways to get the MIDI:
//   - live MIDI out of the plugin (sent at the hit's real start; the DAW compensates the plugin's latency),
//   - the hit recorder: plays along with the transport and keeps every hit at its song position, then
//     drag the MIDI out of the plugin or save a .mid file,
//   - Analyze file: load the bounced drum track and get the whole song's MIDI in one go.
// The detector itself is trigger/Detector.h (no JUCE), tested by trigger/test.cpp.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <mutex>
#include "Detector.h"
#include "Presets.h"

namespace trig {

// one hit as the editor and the recorder see it
struct HitInfo {
    int64_t onset = 0;      // input sample (detector clock)
    float peakDb = -120;
    int velocity = 0;
    bool rejected = false;  // dropped as sidechain bleed
};

// a recorded or analyzed hit, placed in the song
struct SongHit {
    double ppq = 0;         // quarter notes from the song start (recorder, when the host gives a tempo)
    double seconds = 0;     // seconds from the song start (or from the file start)
    int velocity = 100;
};

class TriggerProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    TriggerProcessor();
    ~TriggerProcessor() override;

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- for the editor (message thread) ----
    void applyPreset(Preset p);
    Settings currentSettings() const;
    double sampleRate() const { return sr; }
    int latencyMs10() const { return (int) std::lround(10000.0 * detector.latency() / sr); }

    // the scrolling display: one column per kColumn samples (the detection envelope), plus the hits
    static constexpr int kColumnsPerSecond = 400;
    void pullDisplay(std::vector<float>& columns, std::vector<HitInfo>& hits, int64_t& lastColumnEnd, int& columnLen);

    // Learn: threshold from the candidates heard so far (live) — false if there's no clear gap yet
    bool learnThreshold(float& db);
    int candidatesHeard() const { const std::lock_guard<std::mutex> l(candMutex); return (int) candidates.size(); }

    // the recorder
    std::atomic<bool> recording { true };
    std::vector<SongHit> recorded() const { const std::lock_guard<std::mutex> l(recMutex); return rec; }
    void clearRecorded() { const std::lock_guard<std::mutex> l(recMutex); rec.clear(); }
    double recordedBpm() const { return recBpm.load(); }

    // Analyze file (runs on a background thread; the editor polls analysisStatus)
    void analyzeFile(const juce::File& f, bool learnFirst);
    juce::String analysisStatus() const { const std::lock_guard<std::mutex> l(anMutex); return anStatus; }
    std::vector<SongHit> analyzed() const { const std::lock_guard<std::mutex> l(anMutex); return anHits; }
    bool analysisRunning() const { return anRunning.load(); }
    double hostBpm() const { return lastBpm.load(); }

    // MIDI files from a list of hits; nudgeMs moves every note
    juce::MidiFile makeMidi(const std::vector<SongHit>& hits, bool usePpq, double bpm) const;
    juce::File writeTempMidi(const std::vector<SongHit>& hits, bool usePpq, double bpm, const juce::String& name) const;

private:
    void timerCallback() override;
    void readParams(Settings& s) const;

    Detector detector, learner;
    double sr = 44100;
    int latency = 0;

    // audio delay so the passed-through audio stays lined up with the (late) MIDI
    juce::AudioBuffer<float> delayBuf; int delayPos = 0;
    std::vector<float> mono, sideMono, bandMon;
    Band monitorBand;
    Settings lastSettings; bool settingsValid = false;

    // notes waiting to be sent (absolute output sample times)
    struct Pending { int64_t at; int note, channel, vel; bool on; };
    std::array<Pending, 256> pending {}; int nPending = 0;
    int lastNote = -1, lastChannel = 1; bool noteHeld = false;
    float clickPhase = 0, clickEnv = 0;

    // audio thread -> message thread
    juce::AbstractFifo colFifo { 8192 }; std::vector<float> colData = std::vector<float>(8192);
    juce::AbstractFifo hitFifo { 1024 }; std::vector<HitInfo> hitData = std::vector<HitInfo>(1024);
    juce::AbstractFifo candFifo { 1024 }; std::vector<float> candData = std::vector<float>(1024);
    struct RecItem { double ppq, seconds; int vel; bool hasPpq; double bpm; };
    juce::AbstractFifo recFifo { 2048 }; std::vector<RecItem> recData = std::vector<RecItem>(2048);
    std::atomic<int64_t> colEnd { 0 };
    int colLen = 110, colCount = 0;

    mutable std::mutex candMutex; std::vector<float> candidates;
    mutable std::mutex recMutex; std::vector<SongHit> rec;
    std::atomic<double> recBpm { 120 }, lastBpm { 0 };

    mutable std::mutex anMutex; juce::String anStatus; std::vector<SongHit> anHits;
    std::atomic<bool> anRunning { false };
    std::unique_ptr<juce::Thread> anThread;
};

} // namespace trig
