// Omega Drum Scribe: a song link (YouTube, Spotify, SoundCloud) or audio file in, every drum hit out as MIDI, in the
// DAW. The analysis runs in the Drum Scribe engine (drumscribe/engine, a local server: separating the drums takes
// PyTorch models that don't belong inside a plugin); this plugin talks to it, asks for the MIDI at the host's tempo so
// it lines up with the project, shows the hits, and lets you drag the MIDI onto a track or save it.
// Audio passes through untouched.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>

namespace scribe {

struct Hit { double time; int note; int vel; };

struct Piece { juce::String label; int note; int count; };

struct Result {
    juce::String title, midiName, splitName;
    double tempo = 120, duration = 0;
    std::vector<Hit> hits;
    std::vector<Piece> pieces;
    juce::File midiFile, splitFile;  // local copies (for dragging into the DAW)
};

// Talks to the engine on a background thread. The editor reads the status on a timer.
class Client : private juce::Thread {
public:
    Client() : juce::Thread("Drum Scribe client") {}
    ~Client() override { stopThread(4000); }

    struct Status { juce::String state = "idle", message, error; double progress = 0; };

    juce::String server = "http://127.0.0.1:8765", token;
    void startLink(const juce::String& link, bool fast, double tempo);
    void startFile(const juce::File& f, bool fast, double tempo);
    void resume(const juce::String& jobId);
    void retempo(double tempo);
    bool busy() const { return isThreadRunning(); }

    Status status() const { const juce::ScopedLock l(lock); return st; }
    std::shared_ptr<const Result> result() const { const juce::ScopedLock l(lock); return res; }
    juce::String jobId() const { const juce::ScopedLock l(lock); return job; }
    int version() const { return ver.load(); }

    juce::String health();  // "" if the engine answers, else why not
    static juce::File cacheDir();

private:
    void run() override;
    juce::String request(const juce::String& method, const juce::String& path, const juce::MemoryBlock* body,
                         const juce::String& contentType, juce::MemoryBlock* out, int timeoutMs = 20000);
    void set(const juce::String& state, const juce::String& msg, double p = -1);
    bool fetchResult();

    enum class Op { link, file, resume, retempo } op = Op::link;
    juce::String link;
    juce::File file;
    bool fast = false;
    double tempo = 0;
    juce::CriticalSection lock;
    Status st;
    juce::String job;
    std::shared_ptr<const Result> res;
    std::atomic<int> ver{0};
};

class Processor : public juce::AudioProcessor {
public:
    Processor();
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    double hostBpm() const { return bpm.load(); }

    Client client;
    juce::String lastLink;
    bool fast = false, useHostTempo = true, splitTracks = false;

private:
    std::atomic<double> bpm{0};
};

}  // namespace scribe
