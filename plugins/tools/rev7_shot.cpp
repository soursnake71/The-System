// Draws the Omega REV7 front panel to a PNG without a host: rev7_shot <out.png> [scale]
#include "../common/Plugin.h"
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    omega::Processor p; p.prepareToPlay(48000, 512);
    p.setCurrentProgram(0);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    const float scale = argc > 2 ? (float) std::atof(argv[2]) : 2.f;
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, scale);
    juce::File f(juce::String::fromUTF8(argc > 1 ? argv[1] : "rev7.png")); f.deleteFile();
    juce::FileOutputStream os(f); juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("%dx%d\n", img.getWidth(), img.getHeight());
}
