// Omega Traks editor: the front panel exactly as the hardware's (same layout as traks/faceplate.js: 2000 x 916 units
// between wooden end cheeks), working: keys, keypad, the two LED displays, the function LEDs, the four knobs. Under
// it, the Omega pages: SOUND, PAD EQ / ADSR, MIXER, FX, MASTER, with the presets and the dice.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "TraksPlugin.h"

namespace traks {

class TraksLook : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

class Faceplate : public juce::Component, private juce::Timer {
public:
    explicit Faceplate(TraksProcessor& p);
    ~Faceplate() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::function<void()> onSave, onLoad;
    void showTempo();
private:
    void timerCallback() override;
    juce::Point<float> toPlate(juce::Point<float> p) const;
    int keyAt(juce::Point<float>) const;
    int knobAt(juce::Point<float>) const;
    float knobNorm(int k) const;
    void setKnob(int k, float n);
    void flash(const juce::String& l, const juce::String& r, int ms) { flashL = l; flashR = r; flashUntil = juce::Time::getMillisecondCounter() + (juce::uint32) ms; }
    TraksProcessor& proc;
    int down = -1, dragKnob = -1; float dragY = 0, dragV0 = 0;
    int repeatKey = -1; juce::uint32 repeatAt = 0;
    juce::String flashL, flashR; juce::uint32 flashUntil = 0, casUntil = 0; int casLed = -1;
    Display last;
};

class TraksEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TraksEditor(TraksProcessor&);
    ~TraksEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Knob { std::unique_ptr<juce::Slider> s; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a; juce::String label, suffix; int tab; juce::Rectangle<int> r; std::function<juce::String(double)> fmt; };
    struct Tog { std::unique_ptr<juce::TextButton> b; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> a; juce::String suffix; int tab; juce::Rectangle<int> r; };
    struct Box { juce::String title; int tab; juce::Rectangle<int> r; };
    Knob& addKnob(int tab, const juce::String& id, const juce::String& label, juce::Rectangle<int> r, const juce::String& suffix = {});
    void addTog(int tab, const juce::String& id, const juce::String& label, juce::Rectangle<int> r, const juce::String& suffix = {});
    void showTab(int t);
    void selectPad(int v);
    void timerCallback() override;
    void fillPresets();
    void setFolded(bool f);
    void saveTape(); void loadTape(); void loadSound();
    bool folded = false;
    TraksProcessor& proc;
    TraksLook look;
    Faceplate plate;
    std::vector<Knob> knobs; std::vector<Tog> togs; std::vector<Box> boxes;
    static constexpr int NT = 5;
    juce::TextButton tabs[NT], padBtn[NV], prevBtn{"<"}, nextBtn{">"}, diceBtn{"DICE"}, foldBtn{"FOLD"}, loadBtn{"LOAD SOUND..."}, factBtn{"BUILT-IN SOUND"}, metroBtn{"METRONOME ALWAYS"};
    juce::ComboBox presetBox, partBox;
    std::unique_ptr<juce::FileChooser> chooser;
    int tab = 0, pad = 0, lastSel = -1;
    juce::uint32 lastParamsHash = 0;
public:
    static constexpr int W = 2120, PH = 916, MH = 760, TOP = 150;
};

} // namespace traks
