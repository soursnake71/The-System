// The front panel is the photo of the pedal. Each of the four knob caps is cut out of the photo as a disc and
// drawn turned to the parameter's value; the footswitch toggles bypass. A slim strip below holds the presets and
// the plugin-only Mix / Stereo / Level controls.
#pragma once
#include "FlangerPlugin.h"

namespace flanger {

class MiniLook : public juce::LookAndFeel_V4 {
public:
    MiniLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
};

// a rotary control that paints nothing: the editor draws the photo knob under it
class HiddenKnob : public juce::Slider {
public:
    HiddenKnob() : juce::Slider(juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox) {}
    void paint(juce::Graphics&) override {}
};

class FootSwitch : public juce::Component {
public:
    std::function<void()> onPress;
    bool down = false;
    void mouseDown(const juce::MouseEvent&) override { down = true; if (onPress) onPress(); repaint(); }
    void mouseUp(const juce::MouseEvent&) override { down = false; repaint(); }
    void paint(juce::Graphics&) override;
};

class Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void snapshot(const juce::File& png, int showKnob = -1);   // used by the tests

    // photo geometry (pixels in the 480 x 370 photo; angles clockwise from 12 o'clock)
    static constexpr int kPhotoW = 480, kPhotoH = 370, kStripH = 44;
    struct KnobSpot { int param; float cx, cy, photoDeg; };
    static const KnobSpot kSpots[4];
    static constexpr float kDiscR = 38.f, kFeather = 3.f;

private:
    void timerCallback() override;
    void buildDiscs();
    float scale() const { return getWidth() / (float) kPhotoW; }

    Processor& proc;
    MiniLook look;
    juce::Image photo, scaledPhoto;
    juce::Image discs[4];
    HiddenKnob knobs[4];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt[4];
    FootSwitch foot;
    juce::Slider mini[3];
    juce::Label miniLabel[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> miniAtt[3];
    juce::ComboBox presets;
    juce::TextButton prev { "<" }, next { ">" };
    int lastProgram = -1, shownKnob = -1;
    bool lastOn = true;
};

}  // namespace flanger
