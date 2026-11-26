// RX5 as a plugin: the RX5-style drum machine (Five.h engine, FiveSeq.h sequencer) as an instrument with MIDI
// in and out. MIDI in plays the voices through the note map (or Tunable Note mode), records in REAL TIME WRITE, takes
// SysEx (bulk receive, Omega messages, identity) and, when synced to MIDI, clock / start / stop / SPP. MIDI out carries
// the sequencer's and pads' notes (note off after the gate time), clock, start/stop and SysEx replies. With HOST SYNC
// on the sequencer follows the host transport while it plays; otherwise it runs free from START / STOP. Main stereo out
// plus 12 optional individual mono outs (disabled by default; an enabled one takes its channel out of the main mix).
// The whole memory is the plugin state (the .o5cart JSON, gzipped, plus a "plugin" object for the panel).
//
// Threads: `lock` guards *st, the Player and the Engine. The audio thread takes it every block; the message thread
// holds it only for short edits. Anything slow (JSON, randomize, SysEx dumps, resampling) works on a copy outside the
// lock and is swapped in with replaceState() / installSample().
#pragma once
#include "FiveSysex.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>

namespace five {

// a waveform the voices can play: the factory recordings (built in) and the cartridge library (state.library)
struct Wave { int id = 0; std::string name, kind, file; double sr = 25000; std::vector<float> data; };

// panel state kept by the processor so it survives closing the editor (the editor's menus read and write it)
struct Ui {
    int mode = 0;                        // 0 pattern, 1 song
    int ptn = 0, song = 0, chain = 0, voice = 0, ch = 0, pad = 0;
    bool chainPlay = false, rtw = false, protect = false;
    juce::String page = "ptn", menu;     // page "menu": the LCD shows MENUS[menu][item[menu]]
    std::map<juce::String, int> item, n; // per-menu item index; misc counters (copy target, slots, export choices...)
    int stepPos = 0, songStep = 0, chainStep = 0;
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
    const juce::String getProgramName(int) override { return "RX5"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---------------- message thread API (editor, tests) ----------------
    juce::AudioProcessorValueTreeState apvts;
    Ui ui;
    juce::SpinLock lock;
    using Edit = juce::SpinLock::ScopedLockType;          // hold while changing (or reading) the state
    State& state() { return *st; }
    const State& state() const { return *st; }

    // engine parameters: the automatable ones go through their APVTS parameter (and the state), the rest only to the state
    void setVoiceParam(int v, int k, double x);
    void setChannelParam(int ch, int k, double x);
    void setRevParam(int k, double x);                    // rProgram loads that program's values
    void setDlyParam(int k, double x);
    void setTempo(double bpm);
    void setVolume(double x);
    void setClickLevel(double x);
    bool hostSync() const;
    void setHostSync(bool on);
    void setSync(const std::string& s);                   // "int" | "midi"

    // transport
    void start();                                         // START: pattern, song or chain per ui
    void stop();
    void selectPattern(int n);                            // while a pattern plays: cued, it starts at the end of the current one
    void cont();
    bool running() const { return isRunning.load(); }
    Player::Pos position() const { const Edit l(lock); return player.pos(); }
    void hitPad(int pad, int vel, int acc);               // a pad (key A..X through the current set-up; records in RTW)
    void playVoice(int v, int vel, int acc, double pitchOff = 0, int ch = -1);
    void releaseVoice(int v);
    void damp();
    void setRtw(bool on);                                 // REAL TIME WRITE (reserves room so recording never allocates)
    std::atomic<float> chLevel[NCH];                      // per-channel activity for the pad lights
    std::atomic<float> meterL { 0 }, meterR { 0 };
    std::atomic<int> firedMask { 0 };                     // voices fired since the editor last looked (bits 0..23)

    // whole-memory operations (message thread); they return a message for the LCD ("" = ok)
    void replaceState(std::unique_ptr<State> s);          // swap in a new memory (samples and parameters follow)
    std::unique_ptr<State> copyState() const;
    void applyVoiceSample(int v);                         // (re)load one voice's waveform into the engine
    void applyAllSamples();
    juce::String randomizeAll(const std::string& scope = "all");
    void initAll();
    juce::String loadCartFile(const juce::File&);
    juce::String saveCartFile(const juce::File&);
    juce::String importMidiFile(const juce::File&, int startPattern, int songSlot);
    juce::String exportMidiFile(const juce::File&, bool song, const std::string& map = "gm");
    juce::String loadWavIntoVoice(const juce::File&, int v);
    juce::String loadSyxFile(const juce::File&);
    juce::String saveSyxFile(const juce::File&, int what);   // 0 all (Omega), 1 hardware bulk, 2 pattern, 3 song
    juce::String loadBankFile(const juce::File&);            // RX5 voice bank image -> the library
    juce::String saveBankFile(const juce::File&);            // the voices' waveforms as an RX5 bank image
    juce::String loadKit(const juce::File&);                 // a folder or .zip of WAV/AIFF files
    juce::String exportWav(const juce::File&, bool song, double sampleRate = 44100);
    juce::String transmitBulk(int what);                     // over MIDI out: 0 Omega all, 1 hardware bulk
    void sendSysex(const Bytes& m) { if (m.size() >= 2) queueOut(juce::MidiMessage::createSysExMessage(m.data() + 1, (int) m.size() - 2)); }

    // the waveform library: state.library first, then the factory recordings whose ids it doesn't use
    struct WaveRef { int id; juce::String name; bool lib; };
    std::vector<WaveRef> waveLib() const;
    const Wave* findWave(int id) const;                  // a factory waveform (unless state.library has that id)
    juce::String waveName(int v) const;
    void setWave(int v, int id);                          // EDIT VOICE WAVEFORM
    const std::vector<Wave>& factory() const { return factoryWaves; }
    int outChannel(int v) const;                          // key assign OUTPUT CH of the current set-up

    juce::String lastMidiMessage() const { const juce::ScopedLock l(msgLock); return midiMsg; }
    void setMessage(const juce::String& s) { const juce::ScopedLock l(msgLock); midiMsg = s; }
    std::function<void(const juce::String&, const juce::String&)> onFlash;   // the editor shows SysEx results etc.

    static juce::String chooseKitName(const juce::String& file);

private:
    void timerCallback() override;
    void syncEngine();                                    // audio thread, under lock: state + automation -> engine
    void buildEngine(double sampleRate);
    void emit(const PlayEvent& e, int n);
    void addOut(const juce::MidiMessage& m, int offset);
    void queueOut(const juce::MidiMessage& m);            // from the message thread
    void loadFactory();
    void pushParamsFromState();                           // state -> APVTS (message thread)
    void createParams(juce::AudioProcessorValueTreeState::ParameterLayout&);
    static BusesProperties buses() {
        auto b = BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true);
        for (int c = 0; c < NCH; c++) b = b.withOutput(juce::String(c + 1) + " " + juce::String(CHANNEL_NAMES[c]), juce::AudioChannelSet::mono(), false);
        return b;
    }
    void rebindPlayer(State* s);

    std::unique_ptr<State> st; Player player; std::unique_ptr<Engine> engine; double sr = 48000; int maxBlock = 512;
    std::vector<Wave> factoryWaves;
    std::array<int, NV> outCh {};
    // automation: target 0 voice, 1 channel, 2 rev, 3 dly, 4 master (index: 0 volume, 1 click, 2 tempo)
    struct Auto { int target, index, key; juce::RangedAudioParameter* p = nullptr; std::atomic<float>* raw = nullptr; float last = 0; };
    std::vector<Auto> autos; std::atomic<float>* syncRaw = nullptr;
    Auto* findAuto(int target, int index, int key);
    void setAuto(int target, int index, int key, double x);
    // what the engine was last given (so a state edit reaches it once)
    struct Applied { std::array<VoiceParams, NV> voice; std::array<ChannelParams, NCH> channel; std::array<double, rCount> rev; std::array<double, dCount> dly;
        double volume = -1, click = -1, tempo = -1; bool valid = false; } applied;
    std::atomic<bool> isRunning { false };
    std::atomic<int> transportReq { 0 }, reqMode { 0 }, reqN { 0 };   // 1 start, 2 stop, 3 continue
    std::atomic<bool> dampReq { false }; std::atomic<int> reqCue { -1 };
    struct Hit { int v, vel, acc, ch, rel; double pitch; };
    std::array<Hit, 128> hitQ {}; std::atomic<int> hitW { 0 }, hitR { 0 };
    bool wasHostPlaying = false, wasRunning = false;
    // MIDI out: pending note offs (samples left) and the block's output buffer
    struct Off { int ch, note; double left; }; std::array<Off, 256> offs {}; int nOffs = 0;
    juce::MidiBuffer* out = nullptr; int blockN = 0;
    juce::SpinLock outLock; juce::MidiBuffer outQueue;
    // incoming SysEx: collected on the audio thread, handled on the message thread
    juce::SpinLock inLock; std::vector<Bytes> sysexIn;
    BulkReceiver receiver;
    mutable juce::CriticalSection msgLock; juce::String midiMsg;
    std::vector<std::unique_ptr<State>> trash;            // old states, freed on the message thread
    std::vector<Engine::Prepared> trashSamples;
    std::atomic<bool> needUi { false };
};

}  // namespace five
