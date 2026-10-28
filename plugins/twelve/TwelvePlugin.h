// Omega Twelve as a plugin: live input through the sampler model, plus a sampler you load files into and play from
// MIDI (middle C = original pitch), the pads or the transport. Sounds are made as they play (Twelve.h LivePlayer), so
// every knob is heard at once; a background thread only prepares the selection (mono mix, timestretch).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include "Twelve.h"

namespace twelve {

juce::String fmtVal(int id, double v);

class TwelveProcessor : public juce::AudioProcessor, private juce::Thread, private juce::Timer {
public:
    TwelveProcessor();
    ~TwelveProcessor() override;

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
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return kPresets; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int i) override;
    const juce::String getProgramName(int i) override { return i >= 0 && i < kPresets ? juce::String(juce::CharPointer_UTF8(PRESETS[i].name)) : juce::String(); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- used by the editor (message thread) ----
    juce::AudioProcessorValueTreeState apvts;
    Params snapshot() const;
    void setParam(int id, double v);
    void applyValues(const Params& p, bool soundOnly);
    juce::String presetName() const { const juce::ScopedLock l(infoLock); return nameShown; }
    void setPresetName(const juce::String& s) { const juce::ScopedLock l(infoLock); nameShown = s; }
    void randomize();

    juce::String loadFile(const juce::File& f);           // "" on success, else the error
    bool hasSource() const { const juce::ScopedLock l(srcLock); return !src.empty(); }
    juce::String sourceName() const { const juce::ScopedLock l(srcLock); return srcName; }
    double sourceSr() const { const juce::ScopedLock l(srcLock); return srcSr; }
    int64_t sourceLength() const { const juce::ScopedLock l(srcLock); return src.empty() ? 0 : (int64_t) src[0].size(); }
    void getPeaks(std::vector<float>& mn, std::vector<float>& mx, int64_t a, int64_t b, int w) const;
    std::pair<int64_t, int64_t> selection() const { const juce::ScopedLock l(srcLock); return {selA, selB}; }
    void setSelection(int64_t a, int64_t b);
    juce::String truncate();
    juce::String normalize();
    juce::String reverse();
    juce::String undo();
    juce::String resample();
    int srcVersion() const { return version.load(); }

    void play(double fromFraction);
    void stopMain() { stopMainRequest = true; }
    void stopAll() { stopRequest = true; }
    // a pad or the on-screen keys: plays at once, semitones from the sample's own pitch
    void hitPad(double semis, float vel = 1) { const int w = padW.load(); padQ[(size_t) (w & 63)] = {semis, vel}; padW = w + 1; }
    // the pitch of each pad: four banks of eight (Multi Mode)
    std::array<std::atomic<float>, 32> padTune;
    bool isPlaying() const { return mainPlaying.load(); }
    double playFraction() const { return mainFrac.load(); }
    bool isRendering() const { return rendering.load(); }
    bool loop = false;

    // long jobs run on the pool; the callback comes back on the message thread
    void runJob(std::function<juce::String()> job, std::function<void(juce::String)> done);
    double tempo = 0; Key key;
    juce::String analyze();
    juce::String warp(double from, double to, bool stretchMode);
    juce::String transposeTo(int root);
    juce::String loadReference(const juce::File& f);
    juce::String matchEq();
    juce::String autoMasterNow(double target);
    void clearMatch();
    void resetMaster();
    juce::String exportTo(const juce::File& f, const juce::String& fmt, double fs, bool whole);
    juce::String refName;
    std::array<std::atomic<float>, 10> matchG;
    juce::String lufsText() const { const juce::ScopedLock l(infoLock); return outText; }

    // MIDI: CC numbers mapped to parameters (learned or default), MIDI learn, the last controller seen
    std::array<std::atomic<int>, 128> ccMap;
    std::atomic<int> learnParam { -1 }, lastCC { -1 };
    void resetCCMap();

private:
    void run() override;
    void timerCallback() override;   // applies MIDI CCs and program changes on the message thread
    void pushUndo();
    void afterSourceChange(bool selectAll);
    Chans renderSelection(bool withMaster, double& fs);   // the selection with the current settings, offline

    std::array<std::atomic<float>*, pid::count> raw {};
    std::atomic<float>* liveRaw = nullptr;
    int program = 0;
    juce::String nameShown { "Init" }, outText;
    juce::CriticalSection infoLock;

    // source
    mutable juce::CriticalSection srcLock;
    Chans src; double srcSr = 44100; juce::String srcName; int64_t selA = 0, selB = 0;
    struct Undo { Chans src; double sr; juce::String name; int64_t a, b; };
    std::vector<Undo> undos;
    std::atomic<int> version { 0 };
    std::array<double, 10> refBands {}; bool hasRef = false;
    juce::AudioFormatManager formats;
    juce::ThreadPool pool { 1 };

    // the selection prepared for the player (background thread -> audio thread)
    juce::SpinLock readyLock; std::shared_ptr<const Chans> readySrc; double readySr = 44100; bool readyNew = false;
    std::vector<double> prepKey;
    std::atomic<bool> rendering { false };
    std::atomic<double> hostFs { 48000 };

    // audio thread
    LivePlayer player; Params lastPP; bool ppPrimed = false; bool lastLoop = false;
    struct PadReq { double semis = 0; float vel = 1; };
    std::array<PadReq, 64> padQ; std::atomic<int> padW { 0 }; int padR = 0;
    std::atomic<bool> playRequest { false }, stopRequest { false }, stopMainRequest { false }, mainPlaying { false };
    std::atomic<double> playFrom { 0 }, mainFrac { 0 };
    Converter conv[2]; OutFilter filt[2]; Lofi lofi; Master master;
    sp::SPLive spl[2]; bool spMode = false;
    LiveStretch stretcher;
public:
    std::atomic<float> meterIn { 0 }, meterOut { 0 };   // peak levels for the editor's VU meters
private:
    std::array<std::atomic<float>, 128> ccVal; std::atomic<uint64_t> ccDirty[2];
    std::atomic<int> pendingProgram { -1 };
    std::atomic<double> bend { 0 };   // semitones
    std::atomic<int> bankMsb { 0 };
    double dc[2] {0, 0};
    Params lastP; bool primed = false;
    std::vector<float> lbuf, rbuf;
    std::array<float, 10> lastMatch {};
};

}  // namespace twelve
