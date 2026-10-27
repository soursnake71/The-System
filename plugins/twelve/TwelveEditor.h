#pragma once
#include "TwelvePlugin.h"

namespace twelve {

// grey panel, cream knobs with a red pointer and an amber value arc, green-on-black display
struct TwelveLook : juce::LookAndFeel_V4 {
    TwelveLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};

class Lcd : public juce::Component, private juce::Timer {
public:
    explicit Lcd(TwelveProcessor& p) : proc(p) { startTimerHz(30); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool zoom = false;
    juce::String info;
    std::function<void()> onSelection;
private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<int> waveArea() const { return getLocalBounds().reduced(10).removeFromBottom(130); }
    std::pair<int64_t, int64_t> view() const;
    int64_t sampleAt(int x) const;
    TwelveProcessor& proc;
    int mode = 0; int64_t startI = 0; int startX = 0; bool moved = false;
};

class TwelveEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TwelveEditor(TwelveProcessor&);
    ~TwelveEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void toast(const juce::String& s) { msg = s; msgUntil = juce::Time::getMillisecondCounter() + 4000; repaint(); }
    void job(std::function<juce::String()> f) { proc.runJob(f, [this](juce::String r) { toast(r); }); }
    void addKnob(int id, juce::String section);
    void addCombo(int id, juce::String section);
    void addToggle(int id, juce::String section);
    juce::TextButton& button(const juce::String& text, std::function<void()> f);

    TwelveProcessor& proc;
    TwelveLook look;
    Lcd lcd;
    struct Ctl { int id; juce::String section; std::unique_ptr<juce::Component> comp; std::unique_ptr<juce::Label> label; };
    std::vector<Ctl> ctls;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sAtt;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> cAtt;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> bAtt;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    std::map<juce::String, juce::TextButton*> btn;
    juce::ComboBox presets, warpMode, toKey, lufs, fmt, expSr, expRange;
    juce::Label srcBpm, tgtBpm;
    juce::ToggleButton liveBtn { "Live input" };
    std::unique_ptr<juce::ButtonParameterAttachment> liveAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String msg; uint32_t msgUntil = 0;
    std::map<juce::String, juce::Rectangle<int>> sections;
};

}  // namespace twelve
