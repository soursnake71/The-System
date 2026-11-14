// Omega Seven plugin window: the faceplate (the same layout as the web app's full view), drawn at 1880 x 800 and
// scaled to the window. Every press goes to the shared Panel logic.
#pragma once
#include "SevenPlugin.h"
#include <set>

namespace seven {

class SevenEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SevenEditor(SevenProcessor&);
    ~SevenEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

    static constexpr float W = 1880, H = 800;
    void showSequencer(bool on) { showSeq = on; showHelp = false; repaint(); }

private:
    struct Hit { juce::Rectangle<float> r; std::function<void()> fn; bool repeat = false; juce::String id; };
    void timerCallback() override;
    void buildHits();
    juce::Point<float> toDesign(juce::Point<float> p) const { const float s = (float) getWidth() / W; return p / s; }
    void drawButton(juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool lit, bool down, const juce::String& face = {});
    void drawLabel(juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, juce::Colour c, bool bold = true, juce::Justification j = juce::Justification::centred);
    void drawChart(juce::Graphics&);
    void drawAlg(juce::Graphics&, int alg, juce::Rectangle<float> cell, float s, bool lit);
    void drawEg(juce::Graphics&);
    void drawOl(juce::Graphics&);
    void drawKeys(juce::Graphics&);
    void drawSeq(juce::Graphics&);
    void drawHelp(juce::Graphics&);
    int keyAt(juce::Point<float> p) const;
    void memoryMenu();
    void midiMenu();
    void say(const juce::String& s) { toastText = s; toastUntil = juce::Time::getMillisecondCounter() + 3500; repaint(); }
    template <class F> void withPanel(F&& f) { const juce::ScopedLock l(proc.lock); f(proc.panel); }

    SevenProcessor& proc;
    std::vector<Hit> hits;
    juce::String downId; int repeatCount = 0; uint32_t downAt = 0; std::function<void()> repeatFn;
    int dragWhat = 0;   // 1 volume, 2 data entry, 3 pitch wheel, 4 mod wheel, 5 keys, 6 seq lane
    float volPos = 0.8f, dataPos = 0.5f, pitchPos = 0, modPos = 0;
    int keyDown = -1; std::set<int> lit;
    bool showSeq = false, showHelp = false; int seqPage = 0, laneDrag = -1;
    juce::String toastText; uint32_t toastUntil = 0;
    std::set<int> pcKeys;
    int pcOct = 0;
};

}  // namespace seven
