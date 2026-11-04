// Drives the Omega Drum Trigger processor directly: the hit recorder against a playing transport (song positions
// in seconds and beats), Analyze file on a WAV, the MIDI files both make (read back and checked note by note), the
// state round trip, and a picture of the panel.
// usage: trigger_proc_test <outdir>
#include "TriggerPlugin.h"
#include "TriggerEditor.h"
#define TRIGGER_TEST_NO_MAIN
#include "test.cpp"

struct FakePlayHead : juce::AudioPlayHead {
    double sr = 48000, bpm = 137, startSec = 2.0; int64_t sample = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        const double t = startSec + (double) sample / sr;
        p.setIsPlaying(playing); p.setBpm(bpm); p.setTimeInSeconds(t); p.setTimeInSamples((int64_t) std::llround(t * sr));
        p.setPpqPosition(t * bpm / 60.0);
        return p;
    }
};

static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

// every truth hit must be in the list (seconds, from the given origin) within tol, and nothing else
static bool compare(const char* what, const std::vector<int64_t>& truth, double sr, double origin, const std::vector<double>& got, double tolMs) {
    std::vector<bool> used(got.size(), false); int missed = 0; double worst = 0;
    for (auto t : truth) {
        const double ts = origin + (double) t / sr; int best = -1; double bd = 1e9;
        for (size_t j = 0; j < got.size(); ++j) if (!used[j] && std::abs(got[j] - ts) < bd) { bd = std::abs(got[j] - ts); best = (int) j; }
        if (best < 0 || bd * 1000 > tolMs) ++missed; else { used[(size_t) best] = true; worst = std::max(worst, bd * 1000); }
    }
    const int extra = (int) std::count(used.begin(), used.end(), false);
    const bool ok = missed == 0 && extra == 0;
    std::printf("  %-4s %-44s %zu/%zu  missed %d extra %d  worst %.3f ms\n", ok ? "ok" : "FAIL", what, got.size(), truth.size(), missed, extra, worst);
    return ok;
}

static std::vector<double> midiNoteTimes(const juce::MidiFile& mf0) {
    juce::MidiFile mf = mf0; mf.convertTimestampTicksToSeconds();
    std::vector<double> t;
    for (int i = 0; i < mf.getNumTracks(); ++i)
        for (auto* e : *mf.getTrack(i)) if (e->message.isNoteOn()) t.push_back(e->message.getTimeStamp());
    std::sort(t.begin(), t.end());
    return t;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();
    bool ok = true;
    const double sr = 48000;
    Scenario sc = doubleKick(sr, 501, 230, 32, -8, "double kick 32nds 230 bpm");

    trig::TriggerProcessor proc;
    proc.enableAllBuses();
    FakePlayHead ph; ph.sr = sr;
    proc.setPlayHead(&ph);
    proc.prepareToPlay(sr, 1024);
    proc.applyPreset(trig::Preset::kick);
    if (auto* q = proc.apvts.getParameter("thresh")) q->setValueNotifyingHost(q->convertTo0to1(-20));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    // ---- the recorder, transport playing from 2.0 s at 137 bpm ----
    const int lat = proc.getLatencySamples();
    const int64_t N = (int64_t) sc.trk.x.size() + lat + 4096;
    std::mt19937 rng(9); std::uniform_int_distribution<int> bs(64, 1024);
    int notes = 0;
    for (int64_t pos = 0; pos < N;) {
        const int len = (int) std::min<int64_t>(bs(rng), N - pos);
        juce::AudioBuffer<float> b(4, len); b.clear(); juce::MidiBuffer midi;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) b.setSample(c, i, pos + i < (int64_t) sc.trk.x.size() ? sc.trk.x[(size_t) (pos + i)] : 0.f);
        ph.sample = pos;
        proc.processBlock(b, midi);
        for (const auto m : midi) if (m.getMessage().isNoteOn()) ++notes;
        pos += len;
        if ((pos / len) % 50 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
    const auto rec = proc.recorded();
    std::vector<double> secs, beats;
    for (auto& h : rec) { secs.push_back(h.seconds); beats.push_back(h.ppq * 60.0 / ph.bpm); }
    ok &= compare("recorder: song seconds", sc.trk.truth, sr, ph.startSec, secs, 0.5);
    ok &= compare("recorder: song beats (as seconds)", sc.trk.truth, sr, ph.startSec, beats, 0.5);
    std::printf("       live MIDI notes out: %d\n", notes);
    ok &= notes == (int) sc.trk.truth.size();
    // its MIDI file: tempo from the host, notes at their song positions
    ok &= compare("recorder: .mid file note times", sc.trk.truth, sr, ph.startSec, midiNoteTimes(proc.makeMidi(rec, true, proc.recordedBpm())), 0.6);
    // playing the same part again re-records it instead of doubling it
    {
        trig::TriggerProcessor p2; p2.enableAllBuses(); FakePlayHead ph2 = ph; p2.setPlayHead(&ph2); p2.prepareToPlay(sr, 512);
        p2.applyPreset(trig::Preset::kick);
        if (auto* q = p2.apvts.getParameter("thresh")) q->setValueNotifyingHost(q->convertTo0to1(-20));
        for (int pass = 0; pass < 2; ++pass)
            for (int64_t pos = 0; pos < N; pos += 512) {
                const int len = (int) std::min<int64_t>(512, N - pos);
                juce::AudioBuffer<float> b(4, len); b.clear(); juce::MidiBuffer midi;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) b.setSample(c, i, pos + i < (int64_t) sc.trk.x.size() ? sc.trk.x[(size_t) (pos + i)] : 0.f);
                ph2.sample = pos; p2.processBlock(b, midi);
                if ((pos / 512) % 40 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        const bool same = p2.recorded().size() == sc.trk.truth.size();
        std::printf("  %-4s %-44s %zu hits after two passes\n", same ? "ok" : "FAIL", "recorder: second pass replaces, not doubles", p2.recorded().size());
        ok &= same;
    }

    // ---- Analyze file ----
    {
        const auto wav = out.getChildFile("trigger-test-kick.wav");
        wav.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(new juce::FileOutputStream(wav), sr, 1, 24, {}, 0));
        juce::AudioBuffer<float> b(1, (int) sc.trk.x.size());
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, sc.trk.x[(size_t) i]);
        w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); w.reset();
        proc.analyzeFile(wav, true);
        for (int i = 0; i < 600 && proc.analysisRunning(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        std::printf("       %s\n", proc.analysisStatus().toRawUTF8());
        const auto an = proc.analyzed();
        std::vector<double> t; for (auto& h : an) t.push_back(h.seconds);
        ok &= compare("analyze file (24-bit WAV, threshold learned)", sc.trk.truth, sr, 0, t, 0.5);
        ok &= compare("analyze file: .mid file note times", sc.trk.truth, sr, 0, midiNoteTimes(proc.makeMidi(an, false, 137)), 0.6);
        wav.deleteFile();
    }

    // ---- state ----
    {
        juce::MemoryBlock st; proc.getStateInformation(st);
        trig::TriggerProcessor p3; p3.setStateInformation(st.getData(), (int) st.getSize());
        const bool same = p3.recorded().size() == rec.size() && std::abs(p3.apvts.getRawParameterValue("thresh")->load() - proc.apvts.getRawParameterValue("thresh")->load()) < 0.01f;
        std::printf("  %-4s %-44s %d bytes, %zu recorded hits\n", same ? "ok" : "FAIL", "state round trip", (int) st.getSize(), p3.recorded().size());
        ok &= same;
    }

    // ---- the panel, with the last few seconds of hits on it ----
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        ed->setVisible(true);
        // replay the end of the track so the scope has something to show
        const int64_t from = (int64_t) sc.trk.x.size() - (int64_t) (2.2 * sr);
        for (int64_t pos = from; pos < (int64_t) sc.trk.x.size(); pos += 512) {
            const int len = (int) std::min<int64_t>(512, (int64_t) sc.trk.x.size() - pos);
            juce::AudioBuffer<float> b(4, len); b.clear(); juce::MidiBuffer midi;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) b.setSample(c, i, sc.trk.x[(size_t) (pos + i)]);
            proc.processBlock(b, midi);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream os(out.getChildFile("trigger-panel.png"));
        if (os.openedOk()) { os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os); }
        std::printf("       panel %dx%d -> %s\n", ed->getWidth(), ed->getHeight(), out.getChildFile("trigger-panel.png").getFullPathName().toRawUTF8());
    }
    proc.setPlayHead(nullptr);
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
