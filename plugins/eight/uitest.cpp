// Omega 808's editor headless: ZOOM magnifies (a tap does not press), a key pressed in the lens acts, the board's
// pages and the loupe on them; writes pictures. usage: eight_ui_test <dir>
#include "EightPlugin.h"
#include "EightEditor.h"
#include <cstdio>

static bool ok = true;
static void check(bool c, const juce::String& m) { std::printf("%s %s\n", c ? "ok  " : "FAIL", m.toRawUTF8()); ok &= c; }

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir(argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();
    auto proc = std::make_unique<eight::EightProcessor>(); proc->prepareToPlay(48000, 256);
    auto* ed = dynamic_cast<eight::EightEditor*>(proc->createEditor());
    if (!ed) { std::puts("FAIL no editor"); return 1; }
    ed->setSize(2000, 1190);
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };
    auto shot = [&](const char* n) { pump(80); auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f); const auto f = dir.getChildFile(n); f.deleteFile(); juce::FileOutputStream o(f); juce::PNGImageFormat().writeImageToStream(img, o); };
    auto* L = &ed->loupe;
    auto tap = [&](juce::Point<float> p) {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent e(src, p, {}, 1, 0, 0, 0, 0, L, L, now, p, now, 1, false);
        L->mouseDown(e); L->mouseUp(e);
    };
    // the CIRCUIT key under the step strip (photo pixels; the editor is at the photo's size)
    const juce::Point<float> circuitKey(516 + 60, 628 + 32);
    check(!L->isActive(), "ZOOM starts off");
    L->setActive(true);
    tap(circuitKey);
    check(L->isOpen() && !ed->getChildComponent(1)->isVisible(), "with ZOOM on, a tap on CIRCUIT magnifies it and does not open the board");
    shot("eight-loupe.png");
    tap(L->lens().getCentre());
    pump(50);
    bool boardOn = false; for (auto* c : ed->getChildren()) if (dynamic_cast<eight::BoardView*>(c) && c->isVisible()) boardOn = true;
    check(boardOn, "CIRCUIT pressed inside the lens opens the circuit board");
    L->setActive(false); shot("eight-board.png");
    check(!L->isActive() && !L->isOpen(), "ZOOM off closes the lens");
    delete ed; proc.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
