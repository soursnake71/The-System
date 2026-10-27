// Omega Twelve as a plugin: live input through the sampler model, plus a sampler you load files into and play from
// MIDI (middle C = original pitch) or the transport. Renders happen on a background thread with Twelve.h.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include "Twelve.h"

namespace twelve {

juce::String fmtVal(int id, double v);

class TwelveProcessor : public juce::AudioProcessor, private juce::Thread {
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
    void stopAll() { stopRequest = true; }
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

private:
    void run() override;
    std::vector<double> soundKey(const Params& p);
    std::shared_ptr<const Chans> prepared(const Params& p, Chans& srcCopy, double& sr);
    void renderOffset(const Params& p, int off, bool main);
    void pushUndo();
    void afterSourceChange(bool selectAll);
    void requestNote(int off) { if (off >= -24 && off <= 24) noteBits.fetch_or(1ull << (off + 24)); }
    std::shared_ptr<const Chans> noteBuffer(int off, bool blocking);

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

    // renders
    juce::SpinLock notesLock;
    std::map<int, std::shared_ptr<const Chans>> notes;
    std::vector<double> renderedKey, pendingKey;
    double pendingSince = 0;
    std::shared_ptr<const Chans> prep; std::vector<double> prepKey; double prepSr = 44100;
    std::atomic<uint64_t> noteBits { 0 };
    std::atomic<bool> rendering { false };
    std::atomic<double> hostFs { 48000 };

    // audio thread
    struct Voice { std::shared_ptr<const Chans> buf; double pos = 0, gain = 1; int note = -1; bool main = false, active = false; };
    std::array<Voice, 24> voices;
    struct Pending { int note = 0, off = 0; float vel = 0; double age = 0; bool used = false; };
    std::array<Pending, 32> pend;
    std::atomic<bool> playRequest { false }, stopRequest { false }, mainPlaying { false };
    std::atomic<double> playFrom { 0 }, mainFrac { 0 };
    Converter conv[2]; OutFilter filt[2]; Lofi lofi; Master master;
    double dc[2] {0, 0};
    Params lastP; bool primed = false;
    std::vector<float> lbuf, rbuf;
    std::array<float, 10> lastMatch {};
};

}  // namespace twelve
