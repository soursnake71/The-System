// Draws the Omega DR-110 editor (each panel) to PNGs without a DAW: dr110_ui_test <outdir>
#include "DR110Plugin.h"
#include "DR110Editor.h"
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) return 2;
    const juce::File out(juce::String::fromUTF8(argv[1]));
    dr110::DR110Processor p; p.prepareToPlay(48000, 512);
    p.hit(0, true); p.hit(3, false);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    for (int i = 0; i < 4; ++i) p.grid[(size_t) (i * 4)] = 1 | 8 | (i == 0 ? 128 : 0);
    p.grid[4] = p.grid[4].load() | 2 | 32; p.grid[12] = p.grid[12].load() | 2; p.grid[14] = 4 | 64;
    p.hostBar = 17; p.hostStep = 6;
    p.setCurrentProgram(13);                  // 808 Boom · Bass Extender
    p.dice(2);                                // roll the pad EQ
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    int tab = 0;
    for (auto* c : ed->getChildren()) if (auto* b = dynamic_cast<juce::TextButton*>(c))
        if (b->getButtonText() == "VOICE MOD" || b->getButtonText() == "MIXER" || b->getButtonText() == "PAD EQ / ADSR" || b->getButtonText() == "REV 7 / TC DELAY" || b->getButtonText() == "MASTER") {
        b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os(out.getChildFile("dr110-ui-" + juce::String(tab++) + ".png")); os.setPosition(0); os.truncate();
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    std::printf("saved %d pictures, %d x %d, program %d %s\n", tab, ed->getWidth(), ed->getHeight(), p.getCurrentProgram(), p.getProgramName(p.getCurrentProgram()).toRawUTF8());
    return 0;
}
