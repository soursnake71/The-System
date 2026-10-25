#pragma once
#include <JuceHeader.h>
#include <mutex>
#include <atomic>
#include "dsp/Core.h"
#include "dsp/Pads.h"
#include "dsp/Rack.h"

// The F.A.R.P. as a plugin: the C++ sound engine, driven by the page (its screens run in the editor's web view)
// and by MIDI from the host. The page's messages are cached so the sound comes back with the project even
// when the editor is never opened.
class FarpProcessor : public juce::AudioProcessor
{
public:
    FarpProcessor();
    ~FarpProcessor() override = default;

    // ---- juce::AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "The F.A.R.P."; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- the page (message thread) ----
    void fromPage (const juce::String& json);
    void setLs (const juce::String& key, const juce::String& rawJson);
    juce::String stateScript() const;          // window.__FARP_STATE for the page
    juce::File romDir, samplesDir;
    juce::String romListJson() const;
    juce::String sampleListJson() const;
    void pageReady();                          // the page has loaded: hand back the user's pad samples

    // engine -> page: filled on the audio thread, drained by the editor's timer
    struct Outbox {
        std::mutex m;
        std::vector<std::string> msgs;      // eng/nv replies
        float scope[1024] = {}, peaks[12] = {}; std::string state; bool scopeNew = false;
        std::vector<int> keys;              // MIDI notes to light: +note+1 on, -(note+1) off
        double time = 0, bpm = 0;
    } outbox;
    std::atomic<int> stateGeneration { 0 };  // bumps when the host restores a state, so the editor reloads the page

private:
    struct Route {
        std::string keys; bool splitOn = false; int point = 60;
        struct Z { bool str = false, syn = false, bas = false; int oct = 0; std::string inst; bool keysInst = true; } lo, hi;
        bool vr = false; int vrM = 7; std::string vrI;
    } route;
    struct Live { int n; int inst; };

    std::unique_ptr<farp::FarpDSP> dsp;
    std::unique_ptr<farp::PadSampler> pads;
    std::unique_ptr<farp::rack::Rack> rack;
    // the rack's return comes back one short chunk later (like the page's audio graph)
    static constexpr int RT = 4096, RT_LAT = 64;
    std::vector<float> rtL, rtR; int rtW = 0;
    float padL[64] = {}, padR[64] = {}, sendBuf[64] = {}, retL[64] = {}, retR[64] = {}, rackL[64] = {}, rackR[64] = {};
    double sr = 0;

    // page -> audio thread
    std::mutex inMutex;
    std::vector<std::shared_ptr<farp::Val>> inbox, toFree;
    std::vector<std::shared_ptr<farp::Val>> local, graveyard;

    // replay cache (message thread)
    struct Cached { std::string key, raw; std::shared_ptr<farp::Val> val; };
    std::vector<Cached> cache;
    std::map<std::string, farp::Val> params;
    std::map<juce::String, juce::String> ls;
    mutable std::mutex cacheMutex;

    // MIDI (audio thread)
    Live live[128];
    bool liveOn[128] = {};

    // the vocoder's voice: the plugin's input bus and spoken phrases from the page
    struct Phrase { std::shared_ptr<std::vector<float>> data; double fs = 22050; };
    std::map<int, Phrase> phrases;
    struct Play { Phrase ph; double pos = 0, step = 1, startAt = -1, stopAt = -1; bool loop = false, on = false; } play;
    bool useInput = false;
    struct VocCfg { bool on = false; std::string trig = "say"; int li = 0; std::vector<int> ids; } vcfg;
    bool sayDone = false; int heldMidi = 0;
    void vocTrigger();
    void say (int id, bool loop, double at);
    std::vector<float> voiceBuf;

    void applyOnAudio (const farp::Val& m);
    void routeKey (int k, int& n, int& m, int& inst) const;
    static int padOfNote (int n);
    void midiNote (int k, double vel, bool on);
    static std::string keyOf (const farp::Val& m);
    void rebuildEngine();
    void postOut (std::string&& s);
    void renderChunk (float* L, float* R, int n, const float* inL, const float* inR, int numIn);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FarpProcessor)
};
