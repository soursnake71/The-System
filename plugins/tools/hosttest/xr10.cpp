// Headless check of Omega XR10: load the .vst3 like a DAW, play its sounds from MIDI, run a pattern from the host's
// transport, round-trip the state and open the window.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

struct Head : juce::AudioPlayHead {
    bool playing = false; double ppq = 0, bpm = 100;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(playing); p.setPpqPosition(ppq); p.setBpm(bpm); return p;
    }
};
// usage: xr10_host_test <Omega XR10.vst3> <outdir>
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) return 2;
    const juce::File out(juce::String::fromUTF8(argv[2]));
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found; juce::VST3PluginFormat vst3; vst3.findAllTypesForFile(found, juce::String::fromUTF8(argv[1]));
    if (found.isEmpty()) { std::puts("FAIL no plugin"); return 1; }
    juce::String err; const double sr = 48000; const int block = 256;
    auto inst = fm.createPluginInstance(*found[0], sr, block, err);
    if (!inst) { std::printf("FAIL load: %s\n", err.toRawUTF8()); return 1; }
    Head head; inst->setPlayHead(&head);
    inst->enableAllBuses();
    inst->prepareToPlay(sr, block);
    int fails = 0;
    std::printf("%s | instrument %d | programs %d | out %d\n", found[0]->name.toRawUTF8(), (int) found[0]->isInstrument, inst->getNumPrograms(), inst->getTotalNumOutputChannels());
    auto run = [&](double secs, std::function<void(int, juce::MidiBuffer&)> midiAt) {
        double pk = 0, sendPk = 0; bool nan = false;
        const int n = (int) (secs * sr);
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(inst->getTotalNumOutputChannels(), block); b.clear();
            juce::MidiBuffer midi; if (midiAt) midiAt(pos, midi);
            inst->processBlock(b, midi);
            if (head.playing) head.ppq += block / sr * head.bpm / 60.0;
            for (int i = 0; i < block; i++) {
                for (int c = 0; c < 2; c++) { const float x = b.getSample(c, i); nan |= !std::isfinite(x); pk = juce::jmax(pk, (double) std::abs(x)); }
                if (b.getNumChannels() >= 4) sendPk = juce::jmax(sendPk, (double) std::abs(b.getSample(2, i)));
            }
        }
        if (nan) { std::puts("FAIL NaN"); fails++; }
        return std::make_pair(pk, sendPk);
    };
    // MIDI: GM snare
    auto r = run(1.0, [](int pos, juce::MidiBuffer& m) { if (pos == 0) m.addEvent(juce::MidiMessage::noteOn(10, 38, (juce::uint8) 120), 10); });
    std::printf("  MIDI note 38 (snare)   peak %.3f %s\n", r.first, r.first > 0.05 ? "ok" : "SILENT"); if (r.first <= 0.05) fails++;
    run(2.0, nullptr);
    // the host plays: the pattern runs
    inst->setCurrentProgram(10);   // FUNK 1
    head.playing = true; head.ppq = 0;
    r = run(4.0, nullptr);
    std::printf("  host transport, %-12s peak %.3f, FX send %.3f %s\n", inst->getProgramName(10).toRawUTF8(), r.first, r.second, r.first > 0.1 ? "ok" : "SILENT"); if (r.first <= 0.1) fails++;
    head.playing = false;
    r = run(3.0, nullptr);
    std::printf("  host stopped           peak (tail) %.3f\n", r.first);
    // state round trip
    juce::MemoryBlock st; inst->getStateInformation(st);
    inst->setStateInformation(st.getData(), (int) st.getSize());
    juce::MemoryBlock st2; inst->getStateInformation(st2);
    std::printf("  state %d bytes, round trip %s\n", (int) st.getSize(), st == st2 ? "ok" : "DIFFERS"); if (!(st == st2)) fails++;
    // the window opens (pictures of it come from xr10/shot.cpp, which runs the editor directly)
    if (auto* ed = inst->createEditorIfNeeded()) { std::printf("  editor %dx%d opens\n", ed->getWidth(), ed->getHeight()); delete ed; }
    else { std::puts("FAIL no editor"); fails++; }
    inst->releaseResources();
    std::puts(fails ? "FAILED" : "all ok");
    return fails ? 1 : 0;
}
