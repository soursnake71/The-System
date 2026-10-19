// XL-Desk VST3: the console's rear panel becomes the plugin's buses; every control is an automatable parameter.
#pragma once
#include <JuceHeader.h>
#include "Engine.h"

class XLDeskProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    XLDeskProcessor();
    ~XLDeskProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "XL-Desk"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor (message thread) ----
    juce::RangedAudioParameter* param (int i) const { return params[(size_t) i]; }
    int paramIndex (const juce::String& id) const { auto it = index.find (id); return it == index.end() ? -1 : it->second; }
    void setFromUi (int i, float v);
    void setType (const juce::String& where, const juce::String& type);
    juce::String typeAt (int where) const { return juce::String (engine.type (where)); }
    void setUi (const juce::String& json);
    juce::String getUi() const { return uiJson; }
    void setCfg (double micRef, double convRef);
    double micRef = -20, convRef = 24;
    xl::Meters& meters() { return engine.meters; }
    juce::var midiDevices() const;
    void armLearn (const juce::String& id) { learnId = id; }
    void sendMidiOut (const juce::MidiMessage& m);
    // UI-facing notifications produced by MIDI handling (learned bindings, MMC seen), drained by the editor
    juce::Array<juce::var> takeNotices() { const juce::ScopedLock l (noticeLock); auto n = notices; notices.clear(); return n; }

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    void handleMidi (const juce::MidiMessage& m);
    void applyMidi (const juce::String& target, int v7, int raw14);
    void action (const juce::String& id, int v);
    void mmc (int cmd);
    void openMidiPorts();
    void recallSnap (const juce::String& n);
    void notice (const juce::var& v) { const juce::ScopedLock l (noticeLock); notices.add (v); }

    xl::Engine engine;
    std::vector<juce::RangedAudioParameter*> params;
    std::unordered_map<juce::String, int> index;
    std::vector<float> values, lastFb;
    std::vector<const float*> src; std::vector<float*> sigOut;
    std::vector<std::vector<float>> scratch;
    juce::String uiJson;
    // MIDI settings, learned map and snapshots, parsed from the UI state
    struct MidiCfg { juce::String inId, outId; bool fb = true, pb = true, follow = false, mmcIn = true, mmcOut = false; int mmcId = 127; } mc;
    std::map<juce::String, juce::String> learnMap, defaultMap;
    juce::var snaps;
    juce::String learnId;
    std::unique_ptr<juce::MidiInput> hwIn; std::unique_ptr<juce::MidiOutput> hwOut; juce::String openIn, openOut;
    // MIDI from the audio thread and the hardware port, handled on the message thread
    struct Raw { juce::uint8 d[64]; int n; };
    juce::AbstractFifo inFifo { 1024 }, hwFifo { 1024 }; std::array<Raw, 1024> inBuf, hwBuf;   // host MIDI (audio thread), hardware port (MIDI thread)
    juce::AbstractFifo outFifo { 1024 }; std::array<Raw, 1024> outBuf;
    static void pushIn (juce::AbstractFifo& f, std::array<Raw, 1024>& buf, const juce::uint8* d, int n);
    std::vector<bool> fromMidi;
    juce::CriticalSection noticeLock, hwLock; juce::Array<juce::var> notices;
    struct HwCb : juce::MidiInputCallback { XLDeskProcessor* p; void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override { XLDeskProcessor::pushIn (p->hwFifo, p->hwBuf, m.getRawData(), m.getRawDataSize()); } } hwCb;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XLDeskProcessor)
};
