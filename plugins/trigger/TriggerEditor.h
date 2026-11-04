// Omega Drum Trigger's panel: the scrolling detection view (envelope, threshold you can drag, every hit marked),
// the detection and output knobs, and the two ways to get MIDI out: the hit recorder and Analyze file.
#pragma once
#include "TriggerPlugin.h"

namespace trig {

struct TrigLook : juce::LookAndFeel_V4 {
    TrigLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};

class TriggerEditor;

// the detection view: newest at the right
class Scope : public juce::Component {
public:
    explicit Scope(TriggerEditor& e) : ed(e) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void pull();
    int hitsShown = 0, bleedShown = 0;
private:
    float yOf(float db) const;
    float dbAt(float y) const;
    TriggerEditor& ed;
    std::vector<float> cols;            // newest last
    std::vector<HitInfo> hits;
    int64_t lastEnd = 0; int colLen = 110;
    bool dragging = false;
};

// a box you drag the MIDI out of, into the DAW
class DragBox : public juce::Component {
public:
    std::function<juce::File()> make;   // writes the file when the drag starts
    juce::String text = "DRAG MIDI";
    bool enabled = false;
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { started = false; }
private:
    bool started = false;
};

class TriggerEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TriggerEditor(TriggerProcessor&);
    ~TriggerEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    TriggerProcessor& proc;

private:
    void timerCallback() override;
    struct Knob {
        juce::Slider s; juce::Label l;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a;
    };
    Knob& knob(const char* id, const juce::String& name);
    void saveMidi(bool analyzed);
    void setStatus(const juce::String& s) { status.setText(s, juce::dontSendNotification); }

    TrigLook look;
    Scope scope { *this };
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::TextButton kick { "KICK" }, snare { "SNARE" }, tom { "TOM" }, learn { "LEARN THRESHOLD" };
    juce::TextButton side { "SIDECHAIN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sideA;
    juce::ComboBox monitor; juce::Label monitorL;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> monitorA;
    // recorder
    juce::TextButton recBtn { "REC" }, clearBtn { "CLEAR" }, saveRec { "SAVE .MID" };
    juce::Label recInfo;
    DragBox dragRec;
    // file analysis
    juce::TextButton analyzeBtn { "ANALYZE FILE" }, saveAn { "SAVE .MID" };
    juce::ToggleButton learnFile { "learn threshold first" };
    juce::Label anInfo;
    DragBox dragAn;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace trig
