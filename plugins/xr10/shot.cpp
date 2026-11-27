// Pictures of the Omega XR10 window, one per tab, with a pattern playing: <build>/xr10_shot <outdir>
#include "XR10Plugin.h"
#include "XR10Editor.h"
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    xr10::Processor p;
    p.prepareToPlay(48000, 256);
    p.setCurrentProgram(30);
    p.with([](xr::Machine& m) { m.start(); });
    juce::AudioBuffer<float> b(2, 256); juce::MidiBuffer midi;
    for (int i = 0; i < 48000 * 3 / 2 / 256; i++) p.processBlock(b, midi);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    ed->setSize(1000, 1096);
    const char* names[4] = {"VOICES (RX5)", "STEP GRID", "SONG", "SETUP"};
    for (int t = 0; t < 4; t++) {
        std::function<juce::TextButton*(juce::Component*)> find = [&](juce::Component* c) -> juce::TextButton* {
            for (auto* ch : c->getChildren()) {
                if (auto* tb = dynamic_cast<juce::TextButton*>(ch)) if (tb->getButtonText() == names[t]) return tb;
                if (auto* f = find(ch)) return f;
            }
            return nullptr;
        };
        if (auto* tb = find(ed.get())) tb->triggerClick();
        if (t == 2) p.with([](xr::Machine& m) { m.songSet(0, 0, 30, 6, 1); m.songSet(0, 1, 30, 0, 4); m.songSet(0, 2, 30, 3, 1); m.songSet(0, 3, 31, 1, 4); m.songSet(0, 4, 31, 7, 1); });
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        for (int i = 0; i < 20; i++) p.processBlock(b, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds());
        auto f = out.getChildFile("xr10-plugin-tab" + juce::String(t) + ".png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}
