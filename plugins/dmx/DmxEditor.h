// Omega DMX panel: the web app's landscape panel (dmx/index.html) drawn in JUCE, scaled with the window.
#pragma once
#include "DmxPlugin.h"

namespace dmx {

class DmxEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit DmxEditor(DmxProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    static constexpr int W = 1280, H = 720;
private:
    void timerCallback() override;
    juce::Point<float> toPanel(const juce::MouseEvent& ev) const { const float s = (float) getWidth() / W; return {ev.position.x / s, ev.position.y / s}; }
    // layout (panel units), as the web app's landscape grid
    juce::Rectangle<float> logoR {52, 16, 190, 150}, fadersR {256, 16, 628, 150}, ledR {898, 16, 330, 150},
                           gridR {52, 180, 832, 314}, padR {898, 180, 330, 314}, keysR {52, 508, 832, 196}, ctlR {898, 508, 330, 196};
    juce::Rectangle<float> slot(int g) const;
    juce::Rectangle<float> cell(int v, int s) const;
    juce::Rectangle<float> rowLabel(int v) const;
    juce::Rectangle<float> voiceKey(int i) const;      // i == voices: ACCENT
    juce::Rectangle<float> padKey(int i) const;        // 0..11: 7 8 9 4 5 6 1 2 3 < 0 >
    juce::Rectangle<float> fnKey(int i) const;
    juce::Rectangle<float> playR() const { return {ctlR.getX(), ctlR.getY() + 30, 96, 110}; }
    juce::Rectangle<float> mini(int i) const { return {ledR.getX() + 18 + i * 100.f, ledR.getY() + 112, 92, 30}; }
    void drawKey(juce::Graphics&, juce::Rectangle<float> r, const juce::String& label, bool on, bool red = false, bool hitNow = false);
    void rename(int v);
    void showHelp();

    DmxProcessor& proc;
    Engine& e;
    int dragFader = -1, paintRow = -1, paintValue = 0, flashV = -1, flashT = 0;
    juce::String toast; int toastT = 0;
    std::unique_ptr<juce::AlertWindow> dialog;
    static constexpr const char* kPad = "789456123<0>";
    static constexpr const char* kFn[8] = {"swing", "song", "copy", "tempo", "rec", "erase", "clear", "factory"};
    static constexpr const char* kFnName[8] = {"SWING", "SONG", "COPY", "TEMPO", "RECORD", "ERASE", "CLEAR", "FACTORY"};
};

}  // namespace dmx
