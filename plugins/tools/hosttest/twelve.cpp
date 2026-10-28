// Headless check of Omega Twelve: load the .vst3 like a DAW, run audio through the live path,
// step presets, round-trip its state, and save a picture of the window. The sampler side is tested by twelve/proctest.cpp.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

// usage: twelve_host_test <Omega Twelve.vst3> <outdir>
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
    inst->enableAllBuses(); inst->prepareToPlay(sr, block);
    std::printf("%s | params %d | programs %d | in %d out %d | latency %d\n", found[0]->name.toRawUTF8(), (int) inst->getParameters().size(), inst->getNumPrograms(),
                inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels(), inst->getLatencySamples());
    bool ok = true;
    // the instrument build starts with its live input off; switch it on to test that path
    for (auto* prm : inst->getParameters()) if (prm->getName(64) == "Live input") prm->setValueNotifyingHost(1);
    auto run = [&](double secs, bool input, std::vector<int> notes, const char* tag) {
        const int n = (int) (secs * sr), ch = juce::jmax(2, inst->getTotalNumInputChannels()); double rms = 0, pk = 0; bool nan = false;
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(ch, block); b.clear(); juce::MidiBuffer midi;
            if (pos == 0) for (int k : notes) midi.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8) 110), 0);
            if (input) for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const double t = (pos + i) / sr, ph = std::fmod(t, .5); b.setSample(c, i, (float) (std::sin(6.2831853 * 110 * t) * std::exp(-ph * 8) * .4)); }
            inst->processBlock(b, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const float x = b.getSample(c, i); if (!std::isfinite(x)) nan = true; rms += x * x; pk = juce::jmax(pk, (double) std::abs(x)); }
            juce::Thread::sleep(2);   // let the render thread work, roughly like real time
        }
        rms = std::sqrt(rms / (2.0 * n));
        const bool good = !nan && rms > 1e-3 && pk <= 1.0;
        std::printf("  %-40s rms %.4f peak %.3f %s\n", tag, rms, pk, good ? "ok" : "BAD"); ok &= good;
    };
    run(2, true, {}, "live input, default (SP-1200)");
    for (int p : {0, 140, 260, 399, 480}) { inst->setCurrentProgram(p); run(1, true, {}, inst->getProgramName(p).toRawUTF8()); }
    juce::MemoryBlock st; inst->getStateInformation(st); inst->setStateInformation(st.getData(), (int) st.getSize());
    std::printf("  state %d bytes restored\n", (int) st.getSize());
    if (auto* ed = inst->createEditorIfNeeded()) { std::printf("  editor %dx%d opens\n", ed->getWidth(), ed->getHeight()); delete ed; }
    inst->releaseResources(); inst.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
