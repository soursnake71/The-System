// Headless check of Omega Seven's .vst3 like a DAW: programs, automation, the state round trip and a picture of the window.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

struct Transport : juce::AudioPlayHead {
    double ppq = 0, bpm = 120; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying(playing); p.setBpm(bpm); p.setPpqPosition(ppq); return p; }
};
// usage: seven_host_test <Omega Seven.vst3> <outdir>
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::puts("usage: seven_host_test plugin.vst3 outdir"); return 2; }
    const juce::File out(juce::String::fromUTF8(argv[2]));
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found; juce::VST3PluginFormat vst3; vst3.findAllTypesForFile(found, juce::String::fromUTF8(argv[1]));
    if (found.isEmpty()) { std::puts("FAIL no plugin"); return 1; }
    juce::String err; const double sr = 48000; const int block = 256;
    auto inst = fm.createPluginInstance(*found[0], sr, block, err);
    if (!inst) { std::printf("FAIL load: %s\n", err.toRawUTF8()); return 1; }
    Transport tp; inst->setPlayHead(&tp);
    inst->enableAllBuses(); inst->prepareToPlay(sr, block);
    std::printf("%s | params %d | programs %d | out %d\n", found[0]->name.toRawUTF8(), (int) inst->getParameters().size(), inst->getNumPrograms(), inst->getTotalNumOutputChannels());
    int fails = 0;
    auto run = [&](double secs, std::function<void(int, juce::MidiBuffer&)> ev, const char* tag, bool wantSound = true) {
        const int n = (int) (secs * sr); double rms = 0; bool nan = false;
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(2, block); b.clear(); juce::MidiBuffer m; if (ev) ev(pos, m);
            if (tp.playing) tp.ppq += block / sr * tp.bpm / 60;
            inst->processBlock(b, m);
            for (int i = 0; i < block; ++i) { const float x = b.getSample(0, i); if (!std::isfinite(x)) nan = true; rms += x * x; }
        }
        rms = std::sqrt(rms / n);
        const bool ok = !nan && (wantSound ? rms > 1e-4 : true);
        if (!ok) fails++;
        std::printf("  %-44s rms %.4f %s\n", tag, rms, ok ? "ok" : "FAIL");
    };
    auto chord = [](int pos, juce::MidiBuffer& m) { if (pos == 0) for (int k : {48, 55, 64}) m.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8) 100), 0); if (pos == 48000) for (int k : {48, 55, 64}) m.addEvent(juce::MidiMessage::noteOff(1, k), 0); };
    for (int p : {0, 1, 2, 13, 14}) { inst->setCurrentProgram(p); juce::Thread::sleep(80); juce::MessageManager::getInstance()->runDispatchLoopUntil(80); run(1.5, chord, ("program " + juce::String(p + 1) + " " + inst->getProgramName(p)).toRawUTF8()); }
    // VST3 carries no sysex, so banks and parameter changes over MIDI are checked by seven_proc_test; here: automation and state
    for (auto* prm : inst->getParameters()) if (prm->getName(64) == "ALGORITHM") { prm->setValueNotifyingHost(1.0f); std::printf("  automated %s -> %s\n", prm->getName(64).toRawUTF8(), prm->getCurrentValueAsText().toRawUTF8()); }
    run(1.0, chord, "after automating the algorithm");
    juce::MemoryBlock st; inst->getStateInformation(st); inst->setStateInformation(st.getData(), (int) st.getSize());
    std::printf("  state %d bytes restored\n", (int) st.getSize());
    run(1.0, chord, "after the state round trip");
    if (auto* ed = inst->createEditorIfNeeded()) {
        ed->setSize(1880, 800);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream fo(out.getChildFile("seven-editor.png")); fo.setPosition(0); fo.truncate();
        juce::PNGImageFormat().writeImageToStream(img, fo);
        std::printf("  editor %dx%d -> seven-editor.png\n", ed->getWidth(), ed->getHeight());
        delete ed;
    }
    inst->releaseResources(); inst.reset();
    std::puts(fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
