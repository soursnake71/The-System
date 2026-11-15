// Omega DMX panel: the web app's landscape panel (dmx/index.html) drawn in JUCE and scaled with the window. Laid out like the
// DMX: nine faders and the LED along the top, the voice keys in columns under their groups, keypad at the right,
// the white PLAY / STOP and the function keys along the bottom. EDIT swaps the keys for the pattern's steps.
#pragma once
#include "DmxPlugin.h"

namespace dmx {

// the pages' knobs and buttons, drawn like the app's (red arc, dark key caps)
class DmxLook : public juce::LookAndFeel_V4 {
public:
    DmxLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
};
// a page over the keys, keypad and the keys by it (in panel units, scaled with the window): its headings and knob labels
class PageView : public juce::Component {
public:
    void paint(juce::Graphics&) override;
    struct Text { juce::String s; juce::Rectangle<float> r; bool head; };
    std::vector<Text> texts; juce::String title, sub;
};

class DmxEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit DmxEditor(DmxProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    ~DmxEditor() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

    static constexpr int W = 1280, H = 720;
    void showPage(int p);                              // -1: none; 0..5 VOICE MOD, SOUND, PAD EQ, FX, MASTER, PRESETS (same page again closes it)
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
    juce::Rectangle<float> smallKey(int i) const;      // TEMPO TUNE ACCENT FACTORY
    juce::Rectangle<float> playKey() const { return {playR.getCentreX() - 48, playR.getY() + 28, 96, 84}; }
    juce::Rectangle<float> helpR() const { return {infoR.getX() + 136, infoR.getY() + 90, 64, 25}; }
    juce::Rectangle<float> circR() const { return {infoR.getX(), infoR.getY() + 90, 132, 25}; }
    void drawKey(juce::Graphics&, juce::Rectangle<float> r, const juce::String& label, bool on, bool red = false, bool hitNow = false);
    void fnPress(const std::string& f);
    // the Omega pages (as on the Omega 110 and Omega Traks): VOICE MOD, SOUND, PAD EQ, FX, MASTER, PRESETS
    juce::Rectangle<float> pageR {58, 208, 1164, 322};
    juce::Rectangle<float> tabR(int i) const { return {infoR.getX() + (i % 3) * 68.f, infoR.getY() + 22 + (i / 3) * 30.f, 64, 25}; }
    void buildPage();
    void layoutPage();
    void rename(int v);
    void showHelp();

    DmxProcessor& proc;
    Engine& e;
    bool edit = false;
    int page = -1, pageCard = 0, family = 0;
    DmxLook look; PageView pageView;
    std::vector<std::unique_ptr<juce::Slider>> sliders; std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sAtt;
    std::vector<std::unique_ptr<juce::TextButton>> buttons; std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> bAtt;
    static constexpr const char* kPages[6] = {"VOICE MOD", "SOUND", "PAD EQ", "FX", "MASTER", "PRESETS"};
    int dragFader = -1, paintRow = -1, paintValue = 0, flashV = -1, flashT = 0;
    juce::String toast; int toastT = 0;
    std::unique_ptr<juce::AlertWindow> dialog;
    static constexpr const char* kPad = "789456123<0>";
    static constexpr const char* kFn[8] = {"swing", "song", "edit", "kit", "rec", "erase", "copy", "clear"};
    static constexpr const char* kFnName[8] = {"SWING", "SONG", "EDIT", "KIT", "RECORD", "ERASE", "COPY", "CLEAR"};
    static constexpr const char* kSmall[4] = {"tempo", "tune", "acc", "factory"};
    static constexpr const char* kSmallName[4] = {"TEMPO", "TUNE", "ACCENT", "FACTORY"};
};

}  // namespace dmx
