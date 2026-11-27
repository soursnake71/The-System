// Omega Solina's plugin window: the same pages as the app (Panel, Mixer, FX, Seq, MIDI) over an on-screen keyboard,
// sized to fit without scrolling.
#pragma once
#include "SolinaPlugin.h"
#include <tuple>

namespace solina {

struct Look : juce::LookAndFeel_V4 {
    Look();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::Font(juce::FontOptions(juce::jmin(15.0f, h * .5f), juce::Font::bold)); }
};

class SolinaEditor;

// a knob bound to a parameter, with its name under it; in MIDI learn a click picks it instead
class Knob : public juce::Component {
public:
    Knob(SolinaEditor& e, int id, const juce::String& label, bool vertical = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    int id; bool vertical;
    struct S : juce::Slider { Knob* k = nullptr; void mouseDown(const juce::MouseEvent&) override; void mouseDoubleClick(const juce::MouseEvent&) override; } slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    SolinaEditor& ed;
};
// a switch bound to a parameter
class Switch : public juce::TextButton {
public:
    Switch(SolinaEditor& e, int id, const juce::String& text, juce::Colour on);
    void mouseDown(const juce::MouseEvent&) override;
    int id; SolinaEditor& ed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

class Roll : public juce::Component {
public:
    explicit Roll(SolinaEditor& e) : ed(e) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    int low = 48, view = 0, len = 96, vel = 100;
private:
    SolinaEditor& ed;
    int rows() const { return juce::jlimit(12, 37, getHeight() / 13); }
    float kw() const { return juce::jmin(42.0f, getWidth() * .08f); }
    int dragNote = -1, delIdx = -1, audition = -1, d0 = 0; float x0 = 0;
};

class SolinaEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SolinaEditor(SolinaProcessor&);
    ~SolinaEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void show(int page);
    void lcd(const juce::String& s);
    void learnPick(int id);
    SolinaProcessor& proc;
    Look look;
    juce::OwnedArray<Knob> knobs; juce::OwnedArray<Switch> switches;
    Roll roll{*this};
private:
    void timerCallback() override;
    void layoutPanel(juce::Rectangle<int>);
    void layoutMixer(juce::Rectangle<int>);
    void layoutFx(juce::Rectangle<int>);
    void layoutSeq(juce::Rectangle<int>);
    void layoutMidi(juce::Rectangle<int>);
    void refreshSeq();
    void fillPresets();
    Knob* K(int id, const juce::String& lb, int page, bool vertical = false);
    Switch* W(int id, const juce::String& lb, int page, juce::Colour c = juce::Colour(0xffff7a1a));
    juce::TextButton* B(const juce::String& t, int page, std::function<void()> f);
    int page = 0;
    juce::OwnedArray<juce::TextButton> ownedButtons;
    std::vector<std::tuple<juce::Rectangle<int>, juce::String, int>> boxes;
    std::vector<std::vector<juce::Component*>> pages{5};
    juce::Rectangle<float> logoArea;
    juce::Label lcdLabel, chainLabel, ccText, logText, bpmLabel;
    juce::ComboBox preset, bars, barView, noteLen, noteVel, inCh, outCh;
    juce::TextButton prev{"<"}, next{">"}, dice{"Random"}, play{"Play"}, stop{"Stop"}, rec{"Rec"};
    juce::TextButton tabs[5];
    juce::TextButton patBtn[NPAT];
    juce::Slider bpm, swing;
    juce::MidiKeyboardComponent kb;
    bool chainEdit = false;
    std::unique_ptr<Pattern> clip;
    int seenSong = -1;
    juce::String lcdText; int lcdHold = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton* songModeBtn = nullptr; juce::TextButton* chainEdBtn = nullptr; juce::TextButton* quantBtn = nullptr;
    juce::TextButton* thruBtn = nullptr; juce::TextButton* clkBtn = nullptr; juce::TextButton* syncBtn = nullptr; juce::TextButton* pcBtn = nullptr; juce::TextButton* learnBtn = nullptr;
    std::vector<juce::Component*> ensBox, panelBox, playBox, fxBox[4], seqSide, midiBox;
    std::vector<Knob*> mixK[6]; Knob* mixF[6] = {}; Switch* mixS[6][3] = {}; juce::Label mixName[6];
};

} // namespace solina
