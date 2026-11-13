// Draws the Omega Traks editor (each page) to PNGs without a DAW, after playing the front panel a little:
// traks_ui_test <outdir>
#include "TraksPlugin.h"
#include "TraksEditor.h"
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) return 2;
    const juce::File out(juce::String::fromUTF8(argv[1]));
    traks::TraksProcessor p; p.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    // select pattern 03, start it, step the pattern functions to SWING%
    auto press = [&](int k) { p.key(k, true); p.key(k, false); };
    press(traks::K::D0); press(traks::K::D0 + 3); press(traks::K::PATFN); press(traks::K::RUN);
    press(traks::K::INST + 1);
    juce::AudioBuffer<float> b(14, 512); juce::MidiBuffer m;
    for (int i = 0; i < 60; ++i) p.processBlock(b, m);
    p.setCurrentProgram(42);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
    int tab = 0;
    for (auto* c : ed->getChildren()) if (auto* bt = dynamic_cast<juce::TextButton*>(c))
        if (bt->getButtonText() == "SOUND" || bt->getButtonText() == "PAD EQ / ADSR" || bt->getButtonText() == "MIXER" || bt->getButtonText() == "REV 7 / TC DELAY" || bt->getButtonText() == "MASTER / MACHINE") {
        bt->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os(out.getChildFile("traks-ui-" + juce::String(tab++) + ".png")); os.setPosition(0); os.truncate();
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    ed->setSize(1600, ed->getHeight() * 1600 / ed->getWidth());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    { const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
      juce::FileOutputStream os(out.getChildFile("traks-ui-big.png")); os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os); }
    const auto d = p.display();
    std::printf("saved %d pictures, %d x %d, display [%s|%s] lp %d, program %s\n", tab, ed->getWidth(), ed->getHeight(), d.l.c_str(), d.r.c_str(), d.lp, p.getProgramName(p.getCurrentProgram()).toRawUTF8());
    return 0;
}
