// Omega Five as a plugin: the RX5-style drum machine (Five.h engine, FiveSeq.h sequencer) as an instrument with MIDI
// in and out. MIDI in plays the voices through the note map (or Tunable Note mode), records in REAL TIME WRITE, takes
// SysEx (bulk receive, Omega messages, identity) and, when synced to MIDI, clock / start / stop / SPP. MIDI out carries
// the sequencer's notes (note off velocity 64 after the gate time), clock, start/stop and SysEx. The sequencer follows
// the host transport when HOST SYNC is on, else it runs free from START / STOP. Main stereo out plus 12 optional
// individual mono outs (disabled by default). The whole memory is the plugin state (the .o5cart JSON).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <memory>
#include "FiveSysex.h"

namespace five {

// a waveform in the library: the 51 factory recordings, cartridge bank voices, kit files
struct Wave { std::string name; std::vector<float> data; double sr = 25000; bool rec = false; VoiceParams params; bool hasParams = false; };

// panel state that is not part of the cartridge (kept by the processor so it survives closing the editor)
struct Ui {
    enum Mode { Pattern, Song } mode = Pattern;
    int ptn = 0, song = 0, chain = 0, voice = 0, ch = 0;
    juce::String page = "ptn", menu; std::map<juce::String, int> item;
    bool rtw = false, step = false; int stepPos = 0; int acc = 0; bool chainPlay = false;
    int rnd = 0, exp = 0, impAt = -1, bankDir = 0, syx = 0, notePreset = 0, copyTo = 0;
    juce::String flash1, flash2; double flashUntil = 0;
};

class FiveProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    FiveProcessor();
    ~FiveProcessor() override;

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Omega Five"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---------------- message thread API (editor, tests) ----------------
    juce::AudioProcessorValueTreeState apvts;
    Ui ui;
    juce::SpinLock lock;                 // guards *st, player and engine (the audio thread takes it every block)
    State& state() { return *st; }       // read on the message thread; write under `lock` (ScopedEdit)
    struct ScopedEdit { FiveProcessor& p; juce::SpinLock::ScopedLockType l; explicit ScopedEdit(FiveProcessor& q) : p(q), l(q.lock) {} };

    // engine parameters: automatable ones go through the APVTS, the rest straight into the state
    double voiceParam(int v, int k) const;
    void setVoiceParam(int v, int k, double x);
    double channelParam(int ch, int k) const;
    void setChannelParam(int ch, int k, double x);
    double revParam(int k) const;
    void setRevParam(int k, double x);
    double dlyParam(int k) const;
    void setDlyParam(int k, double x);
    double tempo() const;
    void setTempo(double bpm);
    double volume() const;
    void setVolume(double x);
    double clickLevel() const;
    void setClickLevel(double x);
    bool hostSync() const;
    void setHostSync(bool on);

    // transport
    void start();          // START (pattern or song/chain per ui)
    void stop();
    void cont();
    bool running() const { return isRunning.load(); }
    Player::Pos position() const { const juce::SpinLock::ScopedLockType l(lock); return player.pos(); }
    void hitPad(int v, int vel, int acc);       // a pad: plays (and records in REAL TIME WRITE)
    void damp();
    std::atomic<float> chLevel[NCH];             // per-channel activity for the pad lights
    std::atomic<float> meterL { 0 }, meterR { 0 };

    // whole-memory operations (message thread)
    void replaceState(std::unique_ptr<State> s, bool applySamples = true);
    void stateChanged();                         // after an edit that touched engine params / samples in the state
    void applyVoiceSample(int v);                // (re)load one voice's waveform into the engine
    juce::String randomizeAll(int scope = 0);    // 0 all, 1 pattern, 2 voices, 3 fx
    juce::String loadCartFile(const juce::File&);
    juce::String saveCartFile(const juce::File&);
    juce::String importMidiFile(const juce::File&, int startPattern, int songSlot);
    juce::String exportMidiFile(const juce::File&, bool song);
    juce::File writePatternMidiTemp();           // for drag-out to the host
    juce::String loadWavIntoVoice(const juce::File&, int v);
    juce::String loadSyxFile(const juce::File&);
    juce::String saveSyxFile(const juce::File&, int what);   // 0 all (Omega), 1 hardware bulk, 2 pattern, 3 song
    juce::String loadBankFile(const juce::File&);
    juce::String saveBankFile(const juce::File&);
    juce::String loadKit(const juce::File&);     // a .zip, .bwpreset or folder of WAV/AIFF files
    juce::String transmitBulk(int what);         // over MIDI out
    void initAll();

    std::vector<Wave> library;                   // factory recordings first (index = kit.json order), then loaded waves
    std::array<juce::String, NV> waveName;       // the library wave each voice plays ("" = its user sample / silent)
    int findWave(const juce::String& name) const;
    void assignWave(int v, int libIndex);
    juce::String voiceSourceName(int v) const;

    // MIDI learnt by the editor: SysEx messages received/sent (for the LCD)
    juce::String lastMidiMessage() const { const juce::ScopedLock l(msgLock); return midiMsg; }
    std::atomic<bool> bulkWait { false };

private:
    void timerCallback() override;
    void syncEngine();                           // audio thread, under lock: state + APVTS -> engine
    void handleEvent(const PlayEvent& e, int n);
    void sendMidi(const juce::MidiMessage& m, int offset);
    void queueOut(const juce::MidiMessage& m);   // from the message thread
    void loadFactory();
    void pushParamsFromState();                  // state -> APVTS (message thread)

    std::unique_ptr<State> st; Player player; std::unique_ptr<Engine> engine; double sr = 48000;
    std::vector<std::unique_ptr<State>> trash;   // states replaced on the message thread (freed there)
    struct Auto { int target, index, key; juce::RangedAudioParameter* p = nullptr; std::atomic<float>* raw = nullptr; };
    std::vector<Auto> autos; std::atomic<float>* tempoRaw = nullptr; std::atomic<float>* volRaw = nullptr; std::atomic<float>* clickRaw = nullptr; std::atomic<float>* syncRaw = nullptr;
    Auto* findAuto(int target, int index, int key) const;
    double sampleOf(int v) const;
    std::atomic<bool> isRunning { false };
    std::atomic<int> startReq { 0 };             // 1 start, 2 stop, 3 continue
    std::atomic<bool> dampReq { false };
    struct PadHit { int v, vel, acc; }; std::array<PadHit, 64> padQ {}; std::atomic<int> padW { 0 }; int padR = 0;
    bool wasHostPlaying = false;
    // MIDI out: pending note offs (samples left), the block's output, and messages from the message thread
    struct Off { int ch, note; double left; }; std::array<Off, 256> offs {}; int nOffs = 0;
    juce::MidiBuffer* outBuf = nullptr;
    juce::SpinLock outLock; juce::MidiBuffer outQueue;
    // incoming SysEx: assembled on the audio thread, handled on the message thread
    juce::SpinLock inLock; std::vector<Bytes> sysexIn;
    BulkReceiver receiver;
    mutable juce::CriticalSection msgLock; juce::String midiMsg;
    std::vector<float> scratch;
    int lastSpp = 0;
};

}  // namespace five
