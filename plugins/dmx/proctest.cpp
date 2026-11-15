// Omega DMX processor and panel without a host: kits embedded, a pattern played, and a picture of the panel.
// usage: dmx_proc_test <outdir>
#include "DmxPlugin.h"
#include "DmxEditor.h"
#include "Mixer.h"
#include <juce_audio_formats/juce_audio_formats.h>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir(argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName()); dir.createDirectory();
    dmx::DmxProcessor p; bool ok = true;
    for (int k = 0; k < dmx::kKits; ++k) for (int v = 0; v < dmx::kKitDefs[k].voices; ++v)
        if (p.engine.samples[(size_t) k][(size_t) v].d.empty()) { std::printf("FAIL kit %s voice %s not embedded\n", dmx::kKitDefs[k].id, dmx::kKitDefs[k].v[v].file); ok = false; }
    p.prepareToPlay(48000, 512); p.engine.internalPlay = true;
    double pk = 0; juce::MidiBuffer m;
    for (int i = 0; i < 200; ++i) { juce::AudioBuffer<float> b(2, 512); b.clear(); p.processBlock(b, m); pk = juce::jmax(pk, (double) b.getMagnitude(0, 512)); }
    std::printf("embedded kits ok, pattern peak %.3f, step %d\n", pk, p.engine.nowStep.load()); ok &= pk > .1;
    // Kit A through the circuit model (on by default for it): the embedded WAVs into the cards' ROMs and out in stereo
    p.engine.setKit(1); double pl = 0, pr = 0;
    for (int i = 0; i < 200; ++i) { juce::AudioBuffer<float> b(2, 512); b.clear(); p.processBlock(b, m); pl = juce::jmax(pl, (double) b.getMagnitude(0, 0, 512)); pr = juce::jmax(pr, (double) b.getMagnitude(1, 0, 512)); }
    std::printf("kit A, circuit %s: peak L %.3f R %.3f\n", p.engine.circuitOn() ? "on" : "off", pl, pr); ok &= p.engine.circuitOn() && pl > .1 && pr > .1 && std::abs(pl - pr) > .01;
    p.engine.fn("rec"); p.engine.acc = true;
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setSize(1280, 720);
    auto shot = [&](const juce::String& name) { auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        const auto f = dir.getChildFile(name); f.deleteFile(); juce::FileOutputStream fo(f); juce::PNGImageFormat().writeImageToStream(img, fo); };
    shot("dmx-editor.png");
    // the Omega pages, and a preset and the dice through the processor
    auto* de = dynamic_cast<dmx::DmxEditor*>(ed.get());
    const char* names[6] = {"mods", "sound", "padeq", "fx", "master", "presets"};
    p.loadPreset(75); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
    std::printf("preset %s: delay return %.3f in the engine\n", p.presetName.toRawUTF8(), p.engine.sp[(size_t) dmx::Mixer::index("dlLevel")]);
    ok &= std::abs(p.engine.sp[(size_t) dmx::Mixer::index("dlLevel")] - dmx::kPresetValues[75][dmx::Mixer::index("dlLevel")]) < 1e-3;
    for (int i = 0; i < 6; ++i) { de->showPage(i); shot(juce::String("dmx-page-") + names[i] + ".png"); de->showPage(i); }
    p.dice(0); std::printf("dice: %s\n", p.presetName.toRawUTF8());
    std::puts(ok ? "PASS" : "FAIL"); return ok ? 0 : 1;
}
