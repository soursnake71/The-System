// Drives the Omega Twelve processor directly (linked with the plugin's code): load a file, select, analyse, play from
// MIDI and the transport, auto master, export every format, resample, truncate, and restore a project with the sample in it.
#include "TwelvePlugin.h"
using namespace twelve;

static bool ok = true;
static void check(bool c, const juce::String& m) { std::printf("%s %s\n", c ? "ok  " : "FAIL", m.toRawUTF8()); ok &= c; }

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir(argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double sr = 48000; const int block = 256;
    // a 2-bar 96 BPM loop with an A minor pad, as a WAV file
    const juce::File wav = dir.getChildFile("twelve-loop.wav");
    {
        const int n = (int) (44100 * 5); juce::AudioBuffer<float> s(2, n);
        for (int i = 0; i < n; ++i) { const double t = i / 44100.0, b = std::fmod(t, .625);
            const double v = std::sin(6.2831853 * (50 + 120 * std::exp(-b * 30)) * b) * std::exp(-b * 9) * .7 + (std::sin(6.2831853 * 220 * t) + std::sin(6.2831853 * 261.63 * t) + std::sin(6.2831853 * 329.63 * t)) * .06;
            s.setSample(0, i, (float) v); s.setSample(1, i, (float) v); }
        wav.deleteFile(); juce::WavAudioFormat f; auto os = wav.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w(f.createWriterFor(os.get(), 44100, 2, 24, {}, 0)); os.release(); w->writeFromAudioSampleBuffer(s, 0, n);
    }
    auto proc = std::make_unique<TwelveProcessor>();
    proc->prepareToPlay(sr, block);
    proc->setParam(pid::mOn, 1);
    proc->apvts.getParameter("live")->setValueNotifyingHost(0);
    check(proc->loadFile(wav).isEmpty(), "load WAV");
    const auto res = proc->analyze(); check(std::abs(proc->tempo - 96) < 1.5, "detect: " + res);
    auto run = [&](double secs, std::vector<int> notes, bool play) {
        double rms = 0, pk = 0; bool nan = false; const int n = (int) (secs * sr);
        if (play) proc->play(0);
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(2, block); b.clear(); juce::MidiBuffer m;
            if (pos == 0) for (int k : notes) m.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8) 100), 0);
            proc->processBlock(b, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const float x = b.getSample(c, i); nan |= !std::isfinite(x); rms += x * x; pk = juce::jmax(pk, (double) std::abs(x)); }
            juce::Thread::sleep(3);
        }
        return std::make_tuple(std::sqrt(rms / (2.0 * n)), pk, nan);
    };
    juce::Thread::sleep(1500);
    auto [r1, p1, n1] = run(1, {60}, false); check(r1 > 1e-3 && !n1 && p1 <= 1, "MIDI middle C: rms " + juce::String(r1, 4));
    auto [r2, p2, n2] = run(1, {67, 53}, false); check(r2 > 1e-3 && !n2 && p2 <= 1, "MIDI pitched notes: rms " + juce::String(r2, 4));
    auto [r3, p3, n3] = run(1, {}, true); check(r3 > 1e-3 && proc->isPlaying(), "transport play: rms " + juce::String(r3, 4));
    proc->stopAll(); auto [r4, p4, n4] = run(.3, {}, false); check(r4 < 1e-4, "STOP ALL silences: rms " + juce::String(r4, 6));
    proc->setCurrentProgram(260); juce::Thread::sleep(1200);
    check(proc->autoMasterNow(-14).startsWith("Auto master"), "auto master");
    juce::Thread::sleep(1200);
    check(proc->lufsText().getDoubleValue() > -15.5 && proc->lufsText().getDoubleValue() < -12.5, "loudness after auto master: " + proc->lufsText());
    const bool warped = proc->warp(96, 90, true).startsWith("Warped");
    check(warped && std::abs(proc->snapshot()[pid::stretch] - 96.0 / 90) < 1e-3, "warp 96 -> 90 BPM, stretch " + juce::String(proc->snapshot()[pid::stretch], 4));
    juce::Thread::sleep(1500);
    for (auto [code, ext] : std::vector<std::pair<const char*, const char*>>{{"wav:16", ".wav"}, {"wav:24", ".wav"}, {"wav:32", ".wav"}, {"aiff:16", ".aif"}, {"aiff:24", ".aif"}, {"flac:16", ".flac"}, {"flac:24", ".flac"}, {"ogg:9", ".ogg"}}) {
        const auto f = dir.getChildFile(juce::String("twelve-export-") + juce::String(code).replace(":", "") + ext);
        const auto r = proc->exportTo(f, code, 44100, false);
        juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(f));
        check(rd && rd->lengthInSamples > 44100 && (int) rd->sampleRate == 44100, juce::String("export ") + code + ": " + r + (rd ? " (" + juce::String(rd->lengthInSamples) + " frames, " + juce::String(rd->bitsPerSample) + "-bit)" : ""));
    }
    proc->setSelection(0, (int64_t) (44100 * 1.25));
    const bool cut = proc->truncate().startsWith("Truncated");
    check(cut && proc->sourceLength() == 55125, "truncate to the first bar: " + juce::String(proc->sourceLength() / 44100.0, 3) + " s");
    juce::Thread::sleep(1500);
    check(proc->resample().startsWith("Resampled"), "resample");
    check((int) proc->snapshot()[pid::machine] == 6 && proc->snapshot()[pid::mOn] < .5, "resample resets effects and master");
    check(proc->undo() == "Undone", "undo");
    proc->analyze();
    if (auto* ed = proc->createEditor()) {
        juce::Thread::sleep(200);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        const auto f = dir.getChildFile("twelve-editor.png"); f.deleteFile(); juce::FileOutputStream fo(f); juce::PNGImageFormat().writeImageToStream(img, fo);
        check(img.getWidth() == ed->getWidth(), "editor picture " + f.getFullPathName()); delete ed;
    }
    juce::MemoryBlock st; proc->getStateInformation(st);
    auto p2b = std::make_unique<TwelveProcessor>(); p2b->setStateInformation(st.getData(), (int) st.getSize());
    check(p2b->hasSource() && p2b->sourceLength() == proc->sourceLength(), "project restores the sample (" + juce::String((int) st.getSize()) + " bytes of state)");
    p2b.reset(); proc.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
