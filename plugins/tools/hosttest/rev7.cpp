// Headless check of Omega REV7: load the .vst3 like a DAW, run audio through every program, round-trip the state,
// and save a picture of the front panel. usage: rev7_host_test <Omega REV7.vst3> <outdir>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::puts("usage: rev7_host_test plugin.vst3 outdir"); return 2; }
    const juce::File file(juce::String::fromUTF8(argv[1])), out(juce::String::fromUTF8(argv[2]));
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found; juce::VST3PluginFormat vst3; vst3.findAllTypesForFile(found, file.getFullPathName());
    if (found.isEmpty()) { std::puts("FAIL no plugin"); return 1; }
    juce::String err; const double sr = 48000; const int block = 512;
    auto inst = fm.createPluginInstance(*found[0], sr, block, err);
    if (!inst) { std::printf("FAIL load: %s\n", err.toRawUTF8()); return 1; }
    inst->enableAllBuses(); inst->prepareToPlay(sr, block);
    std::printf("%s | params %d | programs %d\n", found[0]->name.toRawUTF8(), (int) inst->getParameters().size(), inst->getNumPrograms());
    int fails = 0;
    for (int p = 0; p < inst->getNumPrograms(); ++p) {
        inst->setCurrentProgram(p);
        double pk = 0, e = 0; bool nan = false; const int n = (int) (sr * 3);
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(2, block); juce::MidiBuffer midi;
            for (int i = 0; i < block; ++i) { const double t = (pos + i) / sr; const float x = t < 1 ? (float) (std::sin(t * 2 * juce::MathConstants<double>::pi * 220) * std::exp(-std::fmod(t, .25) * 12) * .5) : 0.f; b.setSample(0, i, x); b.setSample(1, i, x); }
            inst->processBlock(b, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const float v = b.getSample(c, i); if (!std::isfinite(v)) nan = true; pk = std::max(pk, (double) std::fabs(v)); e += v * v; }
        }
        const bool bad = nan || pk > 3 || e < 1e-3;
        if (bad) { fails++; std::printf("FAIL %s peak %.3f\n", inst->getProgramName(p).toRawUTF8(), pk); }
    }
    juce::MemoryBlock mb; inst->getStateInformation(mb); inst->setStateInformation(mb.getData(), (int) mb.getSize());
    inst->setCurrentProgram(0);
    if (auto* ed = inst->createEditorIfNeeded()) {
        std::printf("editor %dx%d\n", ed->getWidth(), ed->getHeight());
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.f);
        juce::FileOutputStream os(out.getChildFile("rev7-panel.png")); os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os);
        delete ed;
    } else { fails++; std::puts("FAIL no editor"); }
    std::printf(fails ? "%d failed\n" : "all programs ok\n", fails);
    return fails ? 1 : 0;
}
