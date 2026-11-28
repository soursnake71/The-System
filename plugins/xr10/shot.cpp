// Pictures of the Omega XR10 window, one per rail key, with a pattern playing, and checks of the owner's layout
// (PLUGIN-LAYOUT.md): the rail opens each page, ZOOM magnifies (a tap does not press) and a press in the lens acts,
// FOLD shows the bare machine in a narrower window and EXPAND brings the rail back. <build>/xr10_shot <outdir>
#include "XR10Plugin.h"
#include "XR10Editor.h"
static bool ok = true;
static void check(bool c, const char* m) { std::printf("%s %s\n", c ? "ok  " : "FAIL", m); ok &= c; }
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    xr10::Processor p;
    p.prepareToPlay(48000, 256);
    p.setCurrentProgram(30);
    p.with([](xr::Machine& m) { m.start(); });
    juce::AudioBuffer<float> b(2, 256); juce::MidiBuffer midi;
    for (int i = 0; i < 48000 * 3 / 2 / 256; i++) p.processBlock(b, midi);
    p.foldOpen = false;
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    ed->setSize(1112, 686);
    auto pump = [&](int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); for (int i = 0; i < 20; i++) p.processBlock(b, midi); juce::MessageManager::getInstance()->runDispatchLoopUntil(60); };
    std::function<juce::Component*(juce::Component*, std::function<bool(juce::Component*)>)> find = [&](juce::Component* c, std::function<bool(juce::Component*)> f) -> juce::Component* {
        for (auto* ch : c->getChildren()) { if (f(ch)) return ch; if (auto* r = find(ch, f)) return r; }
        return nullptr;
    };
    auto key = [&](const char* n) { return dynamic_cast<juce::TextButton*>(find(ed.get(), [n](juce::Component* c) { auto* t = dynamic_cast<juce::TextButton*>(c); return t && t->getButtonText() == n && t->isVisible(); })); };
    auto shot = [&](const juce::String& n) {
        auto img = ed->createComponentSnapshot(ed->getLocalBounds());
        auto f = out.getChildFile("xr10-plugin-" + n + ".png"); f.deleteFile();
        juce::FileOutputStream os(f); juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("     %s\n", f.getFullPathName().toRawUTF8());
    };
    const char* names[7] = {"MACHINE", "SOUNDS", "STEPS", "SONG", "MIXER", "FX", "SETUP"};
    for (int t = 0; t < 7; t++) {
        auto* k = key(names[t]);
        check(k != nullptr, (juce::String("rail key ") + names[t]).toRawUTF8());
        if (!k) continue;
        k->triggerClick();
        if (t == 3) p.with([](xr::Machine& m) { m.songSet(0, 0, 30, 6, 1); m.songSet(0, 1, 30, 0, 4); m.songSet(0, 2, 30, 3, 1); m.songSet(0, 3, 31, 1, 4); m.songSet(0, 4, 31, 7, 1); });
        pump(250);
        shot(juce::String(t) + "-" + juce::String(names[t]).toLowerCase());
    }
    // ZOOM: a tap on START magnifies it and does not press it; START pressed in the lens stops the machine
    auto* loupe = dynamic_cast<omega::Loupe*>(find(ed.get(), [](juce::Component* c) { return dynamic_cast<omega::Loupe*>(c) != nullptr; }));
    key("MACHINE")->triggerClick(); pump(100);
    key("ZOOM")->triggerClick(); pump(100);
    check(loupe && loupe->isActive(), "ZOOM switches the lens on");
    if (loupe) {
        bool playing = false; p.with([&](xr::Machine& m) { playing = m.playing; });
        const juce::Point<float> start(707.f * 1000 / 800, 503.f * 686 / 549);   // START/STOP on the photo, in the view's coordinates
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto tapAt = [&](juce::Point<float> q) { const auto now = juce::Time::getCurrentTime(); juce::MouseEvent e(src, q, {}, 1, 0, 0, 0, 0, loupe, loupe, now, q, now, 1, false); loupe->mouseDown(e); loupe->mouseUp(e); };
        tapAt(start); pump(100);
        bool p2 = false; p.with([&](xr::Machine& m) { p2 = m.playing; });
        check(loupe->isOpen() && p2 == playing, "with ZOOM on, a tap on START magnifies it and does not press it");
        shot("zoom");
        tapAt(loupe->lens().getCentre()); pump(100);
        p.with([&](xr::Machine& m) { p2 = m.playing; });
        check(p2 != playing, "START pressed inside the lens acts");
        key("ZOOM")->triggerClick(); pump(50);
        check(!loupe->isActive(), "ZOOM again switches it off");
    }
    // FOLD / EXPAND
    const int w0 = ed->getWidth();
    key("FOLD")->triggerClick(); pump(100);
    check(ed->getWidth() < w0 && key("EXPAND") != nullptr && key("SOUNDS") == nullptr, "FOLD: the bare machine in a narrower window, with EXPAND");
    shot("fold");
    if (auto* e = key("EXPAND")) e->triggerClick();
    pump(100);
    check(key("SOUNDS") != nullptr && std::abs(ed->getWidth() - w0) <= 2, "EXPAND brings the rail back");
    ed.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
