// Headless check of Omega DMX: load the .vst3 like a DAW, play MIDI notes, run the pattern from a host transport,
// change patterns by program change, round-trip the state and open the panel (its picture: dmx_proc_test). The engine is tested by plugins/dmx/test.cpp.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

struct Transport : juce::AudioPlayHead {
    bool playing = false; double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(playing); p.setBpm(bpm); p.setPpqPosition(ppq); p.setTimeSignature(TimeSignature {4, 4}); return p;
    }
};

// usage: dmx_host_test <Omega DMX.vst3> <outdir>
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) return 2;
    const juce::File out(juce::String::fromUTF8(argv[2])); out.createDirectory();
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found; juce::VST3PluginFormat vst3; vst3.findAllTypesForFile(found, juce::String::fromUTF8(argv[1]));
    if (found.isEmpty()) { std::puts("FAIL no plugin"); return 1; }
    juce::String err; const double sr = 48000; const int block = 256;
    auto inst = fm.createPluginInstance(*found[0], sr, block, err);
    if (!inst) { std::printf("FAIL load: %s\n", err.toRawUTF8()); return 1; }
    Transport tp; inst->setPlayHead(&tp);
    inst->enableAllBuses(); inst->prepareToPlay(sr, block);
    std::printf("%s | instrument %d | params %d | programs %d | out %d\n", found[0]->name.toRawUTF8(), (int) found[0]->isInstrument, (int) inst->getParameters().size(), inst->getNumPrograms(), inst->getTotalNumOutputChannels());
    bool ok = found[0]->isInstrument && inst->getParameters().size() >= 8 && inst->getNumPrograms() == 10;
    auto run = [&](double secs, std::vector<int> notes, const char* tag, int program = -1) {
        const int n = (int) (secs * sr); double rms = 0, pk = 0; bool nan = false; int hits = 0; bool loud = false;
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(2, block); b.clear(); juce::MidiBuffer midi;
            if (pos == 0) { if (program >= 0) midi.addEvent(juce::MidiMessage::programChange(1, program), 0); for (int k : notes) midi.addEvent(juce::MidiMessage::noteOn(10, k, (juce::uint8) 110), 0); }
            inst->processBlock(b, midi);
            if (tp.playing) tp.ppq += block / sr * tp.bpm / 60;
            float bp = 0;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const float x = b.getSample(c, i); if (!std::isfinite(x)) nan = true; rms += x * x; pk = juce::jmax(pk, (double) std::abs(x)); bp = juce::jmax(bp, std::abs(x)); }
            if (bp > .2f && !loud) ++hits; loud = bp > .05f;
        }
        rms = std::sqrt(rms / (2.0 * n));
        const bool good = !nan && rms > 1e-3 && pk < 4;   // several drums at once may pass 0 dBFS, as on the hardware and in the app
        std::printf("  %-34s rms %.4f peak %.3f loud blocks %d %s\n", tag, rms, pk, hits, good ? "ok" : "BAD"); ok &= good;
        return hits;
    };
    run(1, {36}, "MIDI kick (36)");
    run(1, {38, 42}, "MIDI snare + hat (38, 42)");
    run(1, {60, 61, 62, 63}, "MIDI voices from C3");
    tp.playing = true; tp.ppq = 0;
    run(4, {}, "host transport: pattern 1");
    run(4, {}, "program change 8 (Fills)", 7);
    tp.playing = false;
    juce::MemoryBlock st; inst->getStateInformation(st); inst->setStateInformation(st.getData(), (int) st.getSize());
    juce::MemoryBlock st2; inst->getStateInformation(st2);
    const bool same = st == st2; ok &= same;
    std::printf("  state %d bytes, round trip %s\n", (int) st.getSize(), same ? "same" : "DIFFERENT");
    if (auto* ed = inst->createEditorIfNeeded()) { std::printf("  editor %dx%d opens\n", ed->getWidth(), ed->getHeight()); delete ed; }
    inst->releaseResources(); inst.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
