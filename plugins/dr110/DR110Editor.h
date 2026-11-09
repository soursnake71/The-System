// Omega DR-110 editor: the front panel (same layout and numbers as dr110/faceplate.js, 2000 x 1160 units) and, under
// it, the VOICE MOD, MIXER and REV 7 panels. The voice keys play the voices (ACCENT held = accented; SHIFT + HAND CLAP =
// knuckle); the display shows the DAW's bar with the notes played in it.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "DR110Plugin.h"

namespace dr110 {

class KnobLook : public juce::LookAndFeel_V4 {
public:
    juce::Colour pointer{0xffff7a1a};
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

class Faceplate : public juce::Component, private juce::Timer {
public:
    explicit Faceplate(DR110Processor& p);
    ~Faceplate() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    void timerCallback() override { repaint(); }
    juce::Point<float> toPlate(juce::Point<float> p) const { return {p.x * 2000.0f / (float) getWidth(), p.y * 1160.0f / (float) getHeight()}; }
    int keyAt(juce::Point<float>) const;
    int knobAt(juce::Point<float>) const;
    void drawLcd(juce::Graphics&);
    void press(int key);
    DR110Processor& proc;
    int down = -1, dragKnob = -1; float dragY = 0, dragV0 = 0;
    bool shiftHeld = false, shiftLatch = false, shiftUsed = false, accHeld = false;
    juce::String flash; juce::uint32 flashUntil = 0;
};

class DR110Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit DR110Editor(DR110Processor&);
    ~DR110Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Knob { std::unique_ptr<juce::Slider> s; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a; juce::String label, suffix; int tab; juce::Rectangle<int> r; std::function<juce::String(double)> fmt; };
    struct Tog { std::unique_ptr<juce::TextButton> b; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> a; juce::String suffix; int tab; juce::Rectangle<int> r; };
    struct Box { juce::String title; int tab; juce::Rectangle<int> r; };
    // suffix: a pad parameter ("lsF", ...) that follows the pad picked on the PAD page; id is then ignored
    Knob& addKnob(int tab, const juce::String& id, const juce::String& label, juce::Rectangle<int> r, bool white, const juce::String& suffix = {});
    void addTog(int tab, const juce::String& id, const juce::String& label, juce::Rectangle<int> r, const juce::String& suffix = {});
    void showTab(int t);
    void selectPad(int v);
    void timerCallback() override;
    void fillPresets();
    DR110Processor& proc;
    KnobLook look, lookWhite;
    Faceplate plate;
    std::vector<Knob> knobs; std::vector<Tog> togs; std::vector<Box> boxes;
    static constexpr int NT = 5;
    juce::TextButton tabs[NT], sweepBtn{"SWEEP"}, padBtn[NV], prevBtn{"<"}, nextBtn{">"}, diceBtn{"DICE"};
    juce::ComboBox presetBox, partBox;
    int tab = 0, pad = 0;
    juce::uint32 lastParamsHash = 0;
    static constexpr int W = 2000, PH = 1160, MH = 860, TOP = 150;     // design units: the plate, then the panels (from TOP)
};

} // namespace dr110
