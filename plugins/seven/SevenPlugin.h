// Omega Seven as a plugin (VST3 / AU / standalone): the FM engine, internal memory and cartridge, the front panel, the
// arpeggiator and function sequencer (on its own tempo or locked to the host), the MIDI file player and recorder.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <optional>
#include "Panel.h"

namespace seven {

class SevenProcessor : public juce::AudioProcessor, public PanelHost, public SeqHost, private juce::Timer {
public:
    SevenProcessor();
    ~SevenProcessor() override;

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Omega Seven"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 32; }
    int getCurrentProgram() override { return panel.num; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- PanelHost (message thread) ----
    Voice156& voice() override { return edit; }
    void setParam(int id, int v) override;
    void setVoice(const Voice156& v) override;
    Func& func() override { return syn.func; }
    void setFunc(int k, int v) override;
    Bank* bank(bool c) override { return c ? (cart ? &*cart : nullptr) : &internal; }
    void store(bool c, int i, const Voice156& v) override;
    void setBank(bool c, const Bank& b) override;
    SeqState& seq() override { return sq.s; }
    void seqCmd(const std::string& cmd, int a) override;
    void request(const std::string& kind, int arg) override;
    bool playing() override { return sq.playing; }
    bool recording() override { return sq.rec; }
    bool playerPlaying() override { return player.playing; }
    bool playerRec() override { return recOn.load(); }
    bool playerLoop() override { return player.loop; }
    int64_t nowMs() override { return (int64_t) juce::Time::getMillisecondCounter(); }

    // ---- SeqHost (audio thread) ----
    void seqNoteOn(int n, int v) override;
    void seqNoteOff(int n) override;
    void seqParam(int id, int v) override;

    // ---- used by the editor ----
    Panel panel { *this };
    juce::CriticalSection lock;                 // held by the audio thread while it runs, and by any edit from the UI
    juce::MidiKeyboardState keys;
    std::atomic<int> bendUi { 0 }, wheelUi { 0 }; std::atomic<bool> ctlDirty { false };
    std::atomic<float> peak { 0 };
    std::atomic<int> stepShown { -1 };
    juce::String cartName;
    std::function<void(const juce::String&)> toast;   // the editor shows messages
    void openFile(const juce::File& f, const juce::String& kind);
    juce::String loadBytes(const juce::MemoryBlock& mb, const juce::String& name, const juce::String& kind);
    void saveBytes(const std::vector<uint8_t>& data, const juce::String& name, const juce::String& pattern);
    void ejectCart() { const juce::ScopedLock l(lock); cart.reset(); cartName.clear(); if (panel.cart) panel.cart = false; }
    void factoryReset();
    void noteFromUi(int n, int v, bool on);
    void panic();
    int voiceVer = 0;

private:
    void timerCallback() override;
    void applyHostParams();
    void syncParamsToHost();
    void handleMidi(const juce::MidiMessage& m, juce::MidiBuffer& out, int pos);
    void key(bool on, int n, int v);
    void renderTo(float* L, int from, int to);
    void stopSeq();
    juce::var seqToVar() const;
    void seqFromVar(const juce::var& v);

    Synth syn;
    Sequencer sq;
    FilePlayer player;
    Voice156 edit;                          // the edit buffer (the voice without motion moves)
    Bank internal {};
    std::optional<Bank> cart;
    std::vector<juce::AudioParameterInt*> vp;   // voice parameters 0..144 and 155 (operator on/off), by id (nullptr for names)
    std::vector<juce::AudioParameterInt*> fp;   // function parameters
    std::array<int, 156> lastV {}; std::array<int, FCOUNT> lastF {};
    std::atomic<bool> pushParams { false };
    double fs = 48000, toTick = 0; int clockDiv = 0;
    juce::MidiBuffer pendingOut;             // MIDI to the host from the sequencer during the block
    int curPos = 0;
    std::atomic<bool> recOn { false }; std::vector<RecEvent> recEvents; double recT0 = 0; int64_t frames = 0;
    std::vector<std::vector<uint8_t>> pendingMsgs;   // from the audio thread to the message thread (dumps received)
    std::atomic<int> pendingProgram { -1 };
    std::atomic<bool> seqDirty { false };
    bool lastHostPlaying = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

}  // namespace seven
