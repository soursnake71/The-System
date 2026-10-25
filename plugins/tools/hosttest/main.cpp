#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

// usage: omega_host_test <plugin.vst3> <outdir>
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::puts("usage: omega_host_test plugin.vst3 outdir"); return 2; }
    const juce::File file(juce::String::fromUTF8(argv[1])), out(juce::String::fromUTF8(argv[2]));
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3; vst3.findAllTypesForFile(found, file.getFullPathName());
    if (found.isEmpty()) { std::printf("FAIL no plugin in %s\n", argv[1]); return 1; }
    juce::String err; const double sr = 48000; const int block = 512;
    auto inst = fm.createPluginInstance(*found[0], sr, block, err);
    if (!inst) { std::printf("FAIL load: %s\n", err.toRawUTF8()); return 1; }
    const bool synth = found[0]->isInstrument;
    inst->enableAllBuses();
    inst->prepareToPlay(sr, block);
    const int ch = juce::jmax(inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels());
    std::printf("%s | %s | params %d | programs %d | in %d out %d | midi %d\n", found[0]->name.toRawUTF8(), synth ? "instrument" : "effect",
                (int) inst->getParameters().size(), inst->getNumPrograms(), inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels(), (int) inst->acceptsMidi());
    auto render = [&](int program, double secs, const char* tag) {
        if (program >= 0) inst->setCurrentProgram(program);
        const int n = (int) (secs * sr); juce::AudioBuffer<float> all(2, n); all.clear();
        double rms = 0, pk = 0; bool nan = false;
        for (int pos = 0; pos < n; pos += block) {
            const int len = juce::jmin(block, n - pos);
            juce::AudioBuffer<float> b(ch, len); b.clear(); juce::MidiBuffer midi;
            if (pos == 0) for (int k : {48, 60, 64, 67}) midi.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8) 100), 0);
            if (pos <= n * 2 / 3 && pos + len > n * 2 / 3) for (int k : {48, 60, 64, 67}) midi.addEvent(juce::MidiMessage::noteOff(1, k), n * 2 / 3 - pos);
            if (!synth) for (int c = 0; c < juce::jmin(2, ch); ++c) for (int i = 0; i < len; ++i) {
                const double t = (pos + i) / sr, ph = std::fmod(t, .5);
                b.setSample(c, i, (float) (t < secs * .6 ? std::sin(2 * juce::MathConstants<double>::pi * 110 * t) * std::exp(-ph * 8) * .4 + std::sin(2 * juce::MathConstants<double>::pi * 1830 * t) * std::exp(-ph * 30) * .3 : 0));
            }
            inst->processBlock(b, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) { const float x = b.getSample(juce::jmin(c, b.getNumChannels() - 1), i); if (!std::isfinite(x)) nan = true; rms += x * x; pk = juce::jmax(pk, (double) std::abs(x)); all.setSample(c, pos + i, x); }
        }
        rms = std::sqrt(rms / (2.0 * n));
        std::printf("  %-34s rms %.4f peak %.3f %s\n", tag, rms, pk, nan ? "NaN!" : rms < 1e-4 ? "SILENT" : "ok");
        return !nan && rms >= 1e-4;
    };
    juce::Thread::sleep(500);   // speech renders off the audio thread
    bool ok = render(-1, 3, "default");
    for (int p = 0; p < juce::jmin(3, inst->getNumPrograms()); ++p) { juce::Thread::sleep(300); ok &= render(p, 3, inst->getProgramName(p).toRawUTF8()); }
    // state round trip
    juce::MemoryBlock st; inst->getStateInformation(st); inst->setStateInformation(st.getData(), (int) st.getSize());
    std::printf("  state %d bytes restored\n", (int) st.getSize());
    if (auto* ed = inst->createEditorIfNeeded()) { std::printf("  editor %dx%d\n", ed->getWidth(), ed->getHeight()); delete ed; }
    inst->releaseResources(); inst.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
