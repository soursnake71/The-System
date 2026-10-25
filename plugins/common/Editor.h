// A front panel built from the Spec: a name plate, the preset menu, optional text box, then every control as a
// knob, a lit switch or a menu, grouped by section ("Filter: Cutoff" goes under FILTER).
#pragma once
#include "Plugin.h"

namespace omega {

class Look : public juce::LookAndFeel_V4 {
public:
    explicit Look(juce::Colour accent, juce::Colour panel);
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Colour accent, panel;
};

class Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    struct Cell {
        juce::String section;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> knob;
        std::unique_ptr<juce::TextButton> sw;
        std::unique_ptr<juce::ComboBox> menu;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ka;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;
    };
    Processor& proc;
    Look look;
    juce::ComboBox presets;
    juce::TextButton prev { "<" }, next { ">" };
    juce::TextEditor text;
    juce::Label textHint;
    juce::Viewport view;
    juce::Component body;
    std::vector<std::unique_ptr<Cell>> cells;
    std::vector<std::pair<juce::String, int>> heads;   // section title, y
    juce::OwnedArray<juce::Label> ownedHeads;
    int lastProgram = -1;
};

}  // namespace omega
