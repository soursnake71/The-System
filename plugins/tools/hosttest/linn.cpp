// Headless check of Omega Linn like a DAW would use it: load the .vst3, turn on all its outputs, play a drum pattern
// from MIDI (General MIDI drum notes, dynamics by velocity), check every output bus (the mix and OUT 1-8), save / restore the state, and save a picture of
// the editor. usage: linn_host_test <Omega Linn.vst3> <outdir>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::puts("usage: linn_host_test plugin.vst3 outdir"); return 2; }
    const juce::File out(juce::String::fromUTF8(argv[2]));
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3; vst3.findAllTypesForFile(found, juce::String::fromUTF8(argv[1]));
    if (found.isEmpty()) { std::puts("FAIL no plugin"); return 1; }
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::String err; const double sr = 48000; const int block = 480;
    auto inst = fm.createPluginInstance(*found[0], sr, block, err);
    if (!inst) { std::printf("FAIL load: %s\n", err.toRawUTF8()); return 1; }
    inst->enableAllBuses();
    inst->prepareToPlay(sr, block);
    const int ch = inst->getTotalNumOutputChannels();
    std::printf("%s | %s | params %d | outputs %d channels on %d buses\n", found[0]->name.toRawUTF8(), found[0]->isInstrument ? "instrument" : "effect",
        (int) inst->getParameters().size(), ch, inst->getBusCount(false));
    int fails = 0;
    auto run = [&](const char* tag) {
        // 2 bars at 120 BPM: every sound of the LinnDrum (GM notes), dynamics by velocity
        const int n = (int) (sr * 4); std::vector<double> rms((size_t) ch, 0.0); double pk = 0; bool bad = false;
        const int notes[] = {36, 42, 38, 42, 36, 46, 37, 39, 48, 45, 43, 51, 49, 69, 54, 63, 64, 56, 40, 35};
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(ch, block); b.clear(); juce::MidiBuffer midi;
            const int stepLen = (int) (sr * 0.125);
            for (int i = 0; i < block; ++i) if ((pos + i) % stepLen == 0) {
                const int s = (pos + i) / stepLen;
                midi.addEvent(juce::MidiMessage::noteOn(10, notes[s % 20], (juce::uint8) (s % 4 == 0 ? 127 : 90)), i);
                midi.addEvent(juce::MidiMessage::noteOff(10, notes[s % 20]), std::min(block - 1, i + 100));
            }
            inst->processBlock(b, midi);
            for (int c = 0; c < ch; ++c) for (int i = 0; i < block; ++i) { const float x = b.getSample(c, i); if (!std::isfinite(x)) bad = true; rms[(size_t) c] += x * x; pk = std::max(pk, (double) std::abs(x)); }
        }
        std::printf("  %-22s peak %.3f%s\n", tag, pk, bad ? " NaN!" : "");
        const char* names[] = {"Mix", "OUT1", "OUT2", "OUT3", "OUT4", "OUT5", "OUT6", "OUT7", "OUT8"};
        for (int bus = 0; bus < inst->getBusCount(false); ++bus) {
            const int c0 = inst->getChannelIndexInProcessBlockBuffer(false, bus, 0);
            const double r = std::sqrt((rms[(size_t) c0] + rms[(size_t) c0 + 1]) / (2.0 * n));
            std::printf("    %-6s rms %.4f %s\n", bus < 9 ? names[bus] : "?", r, r < 1e-4 ? "SILENT" : "ok");
            if (r < 1e-4 || bad) ++fails;
        }
    };
    run("stock");
    for (auto* p : inst->getParameters()) if (auto* rp = dynamic_cast<juce::AudioProcessorParameterWithID*>(p)) {
        if (rp->paramID == "kit" || rp->paramID == "poly") p->setValue(1.0f);
        if (rp->paramID == "SNrev") p->setValue(1.0f);
        if (rp->paramID == "hatMode") p->setValue(1.0f);
    }
    run("set 2, poly, LM-1 hat, reversed snare");
    juce::MemoryBlock st; inst->getStateInformation(st);
    inst->setStateInformation(st.getData(), (int) st.getSize());
    std::printf("  state %d bytes, restored\n", (int) st.getSize());
    if (auto* ed = inst->createEditorIfNeeded()) {
        ed->setVisible(true);
        for (int i = 0; i < 6; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os(out.getChildFile("linn-editor.png")); os.setPosition(0); os.truncate();
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("  editor %d x %d saved\n", ed->getWidth(), ed->getHeight());
        delete ed;
    } else { std::puts("  FAIL no editor"); ++fails; }
    inst->releaseResources();
    std::puts(fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
