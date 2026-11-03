// Draws the Omega REV7 front panel to a PNG without a host: rev7_shot <out.png> [scale]
// It also rolls the panel's dice 200 times (presets and random settings) with audio running, and fails on bad output.
#include "../common/Plugin.h"
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    omega::Processor p; p.prepareToPlay(48000, 512);
    p.setCurrentProgram(0);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    // roll the dice: find it on the panel and press it, both ways
    std::function<juce::Component*(juce::Component*)> findDice = [&](juce::Component* c) -> juce::Component* {
        if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(c)) if (t->getTooltip().startsWith("Dice")) return c;
        for (auto* k : c->getChildren()) if (auto* d = findDice(k)) return d; return nullptr; };
    int bad = 0;
    if (auto* dice = findDice(ed.get())) {
        juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer midi;
        for (int i = 0; i < 200; ++i) {
            auto e = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {10, 10}, i % 2 ? juce::ModifierKeys::shiftModifier : juce::ModifierKeys(),
                                      1.f, 0, 0, 0, 0, dice, dice, juce::Time::getCurrentTime(), {10, 10}, juce::Time::getCurrentTime(), 1, false);
            dice->mouseDown(e);
            float pk = 0;
            for (int blk = 0; blk < 60; ++blk) {
                for (int c = 0; c < 2; ++c) for (int n = 0; n < 512; ++n) b.setSample(c, n, blk < 10 ? (float) std::sin(n * .07) * .5f : 0.f);
                p.processBlock(b, midi);
                for (int c = 0; c < 2; ++c) for (int n = 0; n < 512; ++n) { const float v = b.getSample(c, n); if (!std::isfinite(v)) pk = 99; pk = std::max(pk, std::fabs(v)); }
            }
            if (pk > 3) { bad++; std::printf("FAIL roll %d: %s peak %.2f\n", i, p.getProgramName(p.getCurrentProgram()).toRawUTF8(), pk); }
        }
        std::printf("dice: 200 rolls, %d bad\n", bad);
    } else { std::puts("FAIL no dice"); bad++; }
    p.setCurrentProgram(0);
    const float scale = argc > 2 ? (float) std::atof(argv[2]) : 2.f;
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, scale);
    juce::File f(juce::String::fromUTF8(argc > 1 ? argv[1] : "rev7.png")); f.deleteFile();
    juce::FileOutputStream os(f); juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("%dx%d\n", img.getWidth(), img.getHeight());
    return bad ? 1 : 0;
}
