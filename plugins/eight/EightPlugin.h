// Omega 808 as a plugin: the circuit-modelled TR-808 from eight/ (engine, REV7, TC 2290, the 808 sequencer), as a VST3
// instrument (AU on Mac, plus a standalone app).
// - Every engine parameter (eight::params(), the same keys as the app; IDs with '.' as '_') is automatable.
// - The 808 sequencer follows the host transport and tempo ("Sync to host"), or runs on its own START/STOP and TEMPO.
// - MIDI in plays the voices with the General MIDI drum map (velocity >= 100 is the 808 accent); program change picks basic
//   rhythm 1-12. MIDI out: a note per sequenced hit on channel 10, plus note 76 on every trigger-out pulse (the rear
//   TRIGGER OUT jack: Egyptian Lover's trick, here as MIDI to drive other gear).
// - Samples load per voice and are saved with the state, as are the bank (same JSON as the app's) and the switches.
//
// Engine API used (plugins/eight/Engine.h, Fx.h, Seq.h, the C++ port of eight/*.js):
//   eight::params() -> [{k, label, group, min, max, def, step, opts}], eight::voices(), eight::revPrograms()
//   eight::Pattern{scale, len, parts, steps(map track -> "x..x")}, Rhythm{A, B}, Bank{basic[12], fill[4], tempo, swing, fillEvery}
//   eight::StepInfo{step, part, rhythm, fill, hits(voice ids), acc, trig}
//   eight::Machine{eng (Eight808: set/get/trigger/loadSample/clearSample), seq (Seq808: setBank/select/setTempo/play/playing/
//   tap/setPositionBeats), process(L, R, n, onStep(offset, const StepInfo&))}
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>
#include "Engine.h"
#include "Fx.h"
#include "Seq.h"

namespace eight {

// the 12 sequencer tracks of the INSTRUMENT-SELECT knob, plus the trigger-out track
static const char* const TRACKS[] = {"AC", "BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH", "TRIG"};
static constexpr int kTracks = 13;

juce::String paramId(const juce::String& key);   // "BD.level" -> "BD_level"

class EightProcessor : public juce::AudioProcessor {
public:
    EightProcessor();
    ~EightProcessor() override = default;

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- used by the editor (message thread) ----
    void setParam(const juce::String& key, float value);          // plain value, with host notification
    float getParam(const juce::String& key) const;
    bool getStep(int track, int step);                            // in the rhythm being edited
    void toggleStep(int track, int step);
    int editLength();                                             // steps in the rhythm being edited (len * parts)
    juce::String loadSample(const juce::String& voice, const juce::File& f);   // "" or the error
    void clearSample(const juce::String& voice);
    juce::String sampleName(const juce::String& voice) const;
    void hit(const juce::String& voice, bool accent);            // play a voice from the editor
    // REC: play a voice and record it on the nearest step of the playing rhythm (an early hit waits for its step, so it
    // never flams); TRIG records the trigger-out click and points the trigger out at its own track. Returns a message.
    juce::String recHit(const juce::String& voice, bool accent);
    void selectRhythm(int basic);
    juce::String lcdText() const;
    double currentTempo() const { return tempoShown.load(); }

    // editor state, saved with the plugin
    std::atomic<int> track {1};       // INSTRUMENT-SELECT position (index into TRACKS)
    std::atomic<int> mode {3};        // 0 1st part, 1 2nd part, 3 play
    std::atomic<int> basic {0};       // 0..11
    std::atomic<int> runStep {-1};    // the running light (audio thread writes it)
    std::atomic<int> runRhythm {0};
    std::atomic<bool> running {false};
    std::atomic<bool> rec {false};                // REC on (the editor's real-time recording mode)
    std::atomic<bool> padAccent {false};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void pushParams(bool all);
    void applySelect();
    void applyBank();                                            // bank -> sequencer, under the callback lock
    juce::var bankToVar() const;
    void bankFromVar(const juce::var& v);
    void applyPresetJson(const juce::String& json);

    std::unique_ptr<Machine> m;
    double sr = 48000;
    Bank bank;
    std::vector<std::pair<std::string, std::atomic<float>*>> raw;     // engine keys and their parameter values
    std::vector<float> last;
    std::atomic<float>* pSync = nullptr; std::atomic<float>* pRun = nullptr; std::atomic<float>* pTempo = nullptr;
    std::atomic<float>* pVar = nullptr; std::atomic<float>* pIfVar = nullptr;
    int lastVar = -1, lastIfVar = -1, lastBasic = -1;
    bool hostWasPlaying = false; double lastPpq = -1, lastBpm = 0;
    std::atomic<long long> clock {0}, stepAt {0}, stepLen {0};   // samples: now (block start), the last step, the step before it
    std::atomic<double> tempoShown {120};
    int program = 0;

    // samples: kept for the state
    struct Sample { juce::String name; double sr = 0; std::vector<float> data; };
    std::map<juce::String, Sample> samples;
    mutable juce::CriticalSection sampleLock;
    juce::AudioFormatManager formats;

    // MIDI out note-offs still to send: (samples left, note)
    struct Off { int left; int note; };
    std::vector<Off> offs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EightProcessor)
};

}  // namespace eight
