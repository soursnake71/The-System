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
    // state round trip keeps Page R and the user sample
    juce::MemoryBlock st; p.getStateInformation(st);
    cmi::CmiProcessor q; q.setStateInformation(st.getData(), (int) st.getSize());
    const bool same = std::memcmp(&q.engine.seq.pat[0], &p.engine.seq.pat[0], sizeof(p.engine.seq.pat)) == 0 && q.kit.userLoaded(0);
    std::printf("state %d bytes, Page R and user voice restored: %s\n", (int) st.getSize(), same ? "yes" : "NO"); ok &= same;
    // pictures
    p.loadSound(cmi::Library::get().find("CHOIRAAH"));
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
