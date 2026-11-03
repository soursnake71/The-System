// Drives the Omega CMI processor directly (no host): plays the keyboard and Page R, saves and restores state,
// loads a sample into a user voice, and writes a picture of each page of the screen. cmi_snapshot <outdir>
#include "../CmiPlugin.h"
#include "../CmiEditor.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(juce::String::fromUTF8(argc > 1 ? argv[1] : "."));
    out.createDirectory();
    cmi::CmiProcessor p;
    p.setPlayConfigDetails(0, 2, 48000, 512);
    p.prepareToPlay(48000, 512);
    bool ok = true;
    auto run = [&](int blocks, std::function<void(int, juce::MidiBuffer&)> ev) {
        double pk = 0;
        for (int b = 0; b < blocks; ++b) {
            juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer m;
            if (ev) ev(b, m);
            p.processBlock(buf, m);
            pk = std::max(pk, (double) buf.getMagnitude(0, 512));
        }
        return pk;
    };
    // keyboard
    const double k = run(200, [](int b, juce::MidiBuffer& m) { if (b == 0) for (int n : { 60, 64, 67 }) m.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8) 100), 0); if (b == 120) m.addEvent(juce::MidiMessage::allNotesOff(1), 0); });
    std::printf("keyboard chord peak %.3f\n", k); ok &= k > 0.05 && k < 1.0;
    // Page R on its own clock
    p.set("sync", 0); p.seqPlay(true);
    const double r = run(300, {});
    std::printf("Page R peak %.3f, step %d\n", r, p.engine.seqStep.load()); ok &= r > 0.05;
    // a user sample
    {
        juce::File wav = out.getChildFile("tone.wav"); wav.deleteFile();
        juce::WavAudioFormat fmt; std::unique_ptr<juce::OutputStream> os(wav.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), 44100, 1, 16, {}, 0));
        if (w) { os.release(); juce::AudioBuffer<float> b(1, 44100); for (int i = 0; i < 44100; ++i) b.setSample(0, i, 0.5f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / 44100.0)); w->writeFromAudioSampleBuffer(b, 0, 44100); }
        w.reset();
        const auto err = p.loadSample(wav, 0, 57, true);
        std::printf("sample: %s\n", err.isEmpty() ? "loaded" : err.toRawUTF8()); ok &= err.isEmpty();
    }
    // a folder of sounds: three WAVs, one with a root key and a loop in its smpl chunk
    {
        juce::File dir = out.getChildFile("import"); dir.deleteRecursively(); dir.createDirectory();
        for (int k = 0; k < 3; ++k) {
            juce::File wav = dir.getChildFile(juce::String("SND") + juce::String(k + 1) + ".wav");
            juce::StringPairArray md;
            if (k == 1) { md.set("MidiUnityNote", "45"); md.set("NumSampleLoops", "1"); md.set("Loop0Type", "0"); md.set("Loop0Start", "5120"); md.set("Loop0End", "12799"); }
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::OutputStream> os(wav.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), 25000, 1, 16, md, 0));
            if (!w) { ok = false; break; }
            os.release();
            juce::AudioBuffer<float> b(1, 14000); for (int i = 0; i < 14000; ++i) b.setSample(0, i, 0.4f * (float) std::sin(i * 0.05 * (k + 1)));
            w->writeFromAudioSampleBuffer(b, 0, 14000);
        }
        int n = 0;
        const auto msg = p.importFiles({ dir }, 60, false, n);
        const cmi::Sound* s2 = p.kit.user(2);   // slot 1 holds the tone above, so SND2 is in slot 3
        std::printf("folder import: %s; SND2 root %d loop %d-%d rate %.0f\n", msg.toRawUTF8(), s2 ? s2->root : -1, s2 ? s2->loopStart : -1, s2 ? s2->loopEnd : -1, s2 ? s2->rate : 0);
        ok &= n == 3 && s2 && s2->name == "SND2" && s2->root == 45 && s2->loopStart == 40 && s2->loopEnd == 99 && s2->rate == 25000;
    }
    // optional: real CMI disk images or .VC files given on the command line (cmi_snapshot <outdir> <disk.imd> ...)
    for (int i = 2; i < argc; ++i) {
        int n = 0;
        const auto msg = p.importFiles({ juce::File(juce::String::fromUTF8(argv[i])) }, 60, false, n);
        std::printf("import %s: %s\n", argv[i], msg.toRawUTF8());
        ok &= msg.contains("IMPORTED") && !msg.contains("UNREADABLE");
    }
    // state round trip keeps Page R and the user sample
    juce::MemoryBlock st; p.getStateInformation(st);
    cmi::CmiProcessor q; q.setStateInformation(st.getData(), (int) st.getSize());
    const bool same = q.songs.count() == p.songs.count() && q.currentSong() == p.currentSong() && std::memcmp(&q.engine.seq.pat[0], &p.engine.seq.pat[0], sizeof(p.engine.seq.pat)) == 0 && q.kit.userLoaded(0) && q.kit.loadedCount() == p.kit.loadedCount() && q.kit.user(2)->mode == p.kit.user(2)->mode && q.kit.user(2)->ram == p.kit.user(2)->ram;
    std::printf("state %d bytes, Page R and user voice restored: %s\n", (int) st.getSize(), same ? "yes" : "NO"); ok &= same;
    // pictures
    p.loadSound(cmi::Library::get().find("CHOIRAAH"));
    if (p.engine.song.load()) {   // a song was given: play some of it and check it sounds
        p.set("sync", 0); p.engine.songRun = true;
        const double pk = run(1200, {});
        std::printf("song %s: beat %.1f, peak %.3f\n", p.engine.song.load()->name.c_str(), p.engine.songBeat.load(), pk);
        ok &= pk > 0.05;
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto* ce = dynamic_cast<cmi::CmiEditor*>(ed.get());
    for (char pg : { 'R', '2', '7', 'D', '8' }) {
        run(20, [](int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0); });
        ce->showPage(pg);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File f = out.getChildFile(juce::String("page_") + pg + ".png"); f.deleteFile();
        juce::FileOutputStream fo(f); juce::PNGImageFormat png; png.writeImageToStream(img, fo);
        std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
    ed.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
