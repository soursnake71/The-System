// Omega 808's editor: the TR-808 photo as the faceplate, with its knobs, switches, step keys and LEDs live on top, and the
// CIRCUIT view: the board, one section per page (buttons down the left and right, as in the app), with a vertical fader per
// circuit mod / send / effect setting and the sample LOAD buttons. Nothing scrolls.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "EightPlugin.h"

namespace eight {

class EightEditor;

/** a knob on the photo: draws only a pointer over the photo's own knob; drag up/down, double click resets */
class PhotoKnob : public juce::Component {
public:
    std::function<float()> get;                  // 0..1
    std::function<void(float)> set;              // 0..1
    std::function<void()> reset;
    int detents = 0;                             // > 0: a stepped knob
    float a0 = -135, a1 = 135;                   // pointer angle range, degrees from 12 o'clock
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override { if (reset) reset(); repaint(); }
private:
    float startV = 0;
};

/** an invisible hot spot over the photo, with an optional LED drawn at a point inside it */
class HotSpot : public juce::Component {
public:
    std::function<void()> onPress;
    std::function<int()> led;                    // 0 off, 1 dim, 2 lit
    juce::Point<float> ledAt {0.5f, 0.2f};       // relative position of the LED
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { if (onPress) onPress(); }
};

class FaceView : public juce::Component {
public:
    explicit FaceView(EightEditor& e);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
private:
    EightEditor& ed;
    juce::Image photo;
    juce::OwnedArray<PhotoKnob> knobs;
    juce::OwnedArray<HotSpot> spots;
    std::vector<juce::Rectangle<int>> knobRects, spotRects;   // in photo pixels (2000 x 1190)
    juce::TextButton circuit {"CIRCUIT"}, sync {"SYNC HOST"}, pads {"PADS"};
    juce::ComboBox presets;
    bool padMode = false;
    PhotoKnob* addKnob(int x, int y, int r);
    HotSpot* addSpot(int x, int y, int w, int h);
    juce::Rectangle<int> toScreen(juce::Rectangle<int> r) const;
};

class BoardView : public juce::Component {
public:
    explicit BoardView(EightEditor& e);
    void paint(juce::Graphics&) override;
    void resized() override;
    void showPage(const juce::String& id);
private:
    EightEditor& ed;
    juce::String page = "BD";
    juce::OwnedArray<juce::TextButton> nav;
    juce::TextButton stock {"STOCK ALL MODS"}, close {"CLOSE"};
    juce::OwnedArray<juce::Component> ctl;      // the open page's controls
    juce::OwnedArray<juce::Label> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> cAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> bAtt;
    juce::OwnedArray<juce::TextButton> loads;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> pageArea;
    void layoutPage();
};

/** faders with a cream (or orange, for sends) cap like the app's, and bold button text */
class EightLnF : public juce::LookAndFeel_V4 {
public:
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override { return 14; }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::Font(juce::FontOptions(juce::jmin(20.f, h * 0.42f), juce::Font::bold)); }
};

class EightEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit EightEditor(EightProcessor&);
    ~EightEditor() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void showBoard(bool on);
    EightProcessor& proc;
private:
    void timerCallback() override;
    EightLnF lnf;   // first, so it outlives the views that use it
    FaceView face;
    BoardView board;
};

}  // namespace eight
