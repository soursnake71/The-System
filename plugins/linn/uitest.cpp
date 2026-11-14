// Draws the Omega Linn editor (each page) to PNGs without a DAW, after playing the front panel a little:
// linn_ui_test <outdir>
#include "LinnPlugin.h"
#include "LinnEditor.h"
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) return 2;
    const juce::File out(juce::String::fromUTF8(argv[1]));
    linn::LinnProcessor p; p.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    // pattern 22, play it, AFTER + C, percussion on, a hi tom
    auto press = [&](int k) { p.key(k, true); p.key(k, false); };
    press(linn::K::D1 + 1); press(linn::K::D1 + 1); press(linn::K::PLAY);
    p.key(linn::KP::AFTER, true); press(linn::K::D1 + 2); p.key(linn::KP::AFTER, false);
    press(linn::K::DRUM + linn::PERC_KEY); press(linn::K::DRUM + 11);
    juce::AudioBuffer<float> b(18, 512); juce::MidiBuffer m;
    for (int i = 0; i < 60; ++i) p.processBlock(b, m);
    p.setCurrentProgram(31);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
    int tab = 0;
    for (auto* c : ed->getChildren()) if (auto* bt = dynamic_cast<juce::TextButton*>(c))
        if (bt->getButtonText() == "SOUND / HI-HAT" || bt->getButtonText() == "PAD EQ / ADSR" || bt->getButtonText() == "MIXER" || bt->getButtonText() == "REV 7 / TC DELAY" || bt->getButtonText() == "MASTER" || bt->getButtonText() == "FILES / MIDI") {
        bt->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os(out.getChildFile("linn-ui-" + juce::String(tab++) + ".png")); os.setPosition(0); os.truncate();
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    ed->setSize(1600, ed->getHeight() * 1600 / ed->getWidth());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    { const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
      juce::FileOutputStream os(out.getChildFile("linn-ui-big.png")); os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os); }
    const auto d = p.display();
    std::printf("saved %d pictures, %d x %d, display [%s|%s] tim %d ec %d perc %d, program %s\n", tab, ed->getWidth(), ed->getHeight(), d.l.c_str(), d.r.c_str(), d.tim, d.ec, (int) d.perc, p.getProgramName(p.getCurrentProgram()).toRawUTF8());
    return 0;
}
