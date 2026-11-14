// Omega DMX panel: the web app's landscape panel (dmx/index.html) drawn in JUCE and scaled with the window. Laid out like the
// DMX: nine faders and the LED along the top, the voice keys in columns under their groups, keypad at the right,
// the white PLAY / STOP and the function keys along the bottom. EDIT swaps the keys for the pattern's steps.
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
    // the web app's landscape grid: columns 200 | 510 | 92 | 320, rows 176 | 322 | 160, gaps 14, inside the wood ends
    juce::Rectangle<float> logoR {58, 18, 200, 176}, fadersR {272, 18, 616, 176}, ledR {902, 18, 320, 176},
                           matR {58, 208, 724, 322}, smallR {796, 208, 92, 322}, padR {902, 208, 320, 322},
                           infoR {58, 544, 200, 160}, playR {272, 544, 616, 160}, fnR {902, 544, 320, 160};
    juce::Rectangle<float> slot(int g) const;
    juce::Rectangle<float> cell(int v, int s) const;
    juce::Rectangle<float> rowLabel(int v) const;
    std::vector<std::pair<int, juce::Rectangle<float>>> voiceKeys() const;   // voice, key face (label under it)
    std::vector<std::pair<int, juce::Rectangle<float>>> groupHeads() const;  // fader group, heading
    juce::Rectangle<float> padKey(int i) const;        // 0..11: 7 8 9 4 5 6 1 2 3 < 0 >
    juce::Rectangle<float> fnKey(int i) const;         // SWING SONG EDIT KIT / RECORD ERASE COPY CLEAR
    juce::Rectangle<float> smallKey(int i) const;      // TEMPO ACCENT FACTORY
    juce::Rectangle<float> playKey() const { return {playR.getCentreX() - 48, playR.getY() + 28, 96, 84}; }
    juce::Rectangle<float> helpR() const { return {infoR.getX(), infoR.getBottom() - 40, 70, 26}; }
    void drawKey(juce::Graphics&, juce::Rectangle<float> r, const juce::String& label, bool on, bool red = false, bool hitNow = false);
    void fnPress(const std::string& f);
    void rename(int v);
    void showHelp();

    DmxProcessor& proc;
    Engine& e;
    bool edit = false;
    int dragFader = -1, paintRow = -1, paintValue = 0, flashV = -1, flashT = 0;
    juce::String toast; int toastT = 0;
    std::unique_ptr<juce::AlertWindow> dialog;
    static constexpr const char* kPad = "789456123<0>";
    static constexpr const char* kFn[8] = {"swing", "song", "edit", "kit", "rec", "erase", "copy", "clear"};
    static constexpr const char* kFnName[8] = {"SWING", "SONG", "EDIT", "KIT", "RECORD", "ERASE", "COPY", "CLEAR"};
    static constexpr const char* kSmall[3] = {"tempo", "acc", "factory"};
    static constexpr const char* kSmallName[3] = {"TEMPO", "ACCENT", "FACTORY"};
};

}  // namespace dmx
