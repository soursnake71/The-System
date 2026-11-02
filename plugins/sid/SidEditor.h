#pragma once
#include "SidPlugin.h"
#include <map>

namespace sid {

// The window: a 1701-style monitor showing the C64 screen, and a breadbin keyboard with brown keys and the four
// function keys. Everything is drawn at one size (BW x BH) and scaled to the window.
class SidEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    static constexpr int BW = 1000, BH = 900;
    explicit SidEditor(SidProcessor&);
    ~SidEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override {}

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

private:
    void timerCallback() override;
    juce::Point<float> toDesign(juce::Point<float> p) const { const float s = (float) getWidth() / BW; return p / s; }
    juce::Point<float> toScreen(juce::Point<float> d) const;   // design -> C64 screen pixels
    int noteAt(juce::Point<float> d) const;                    // design -> MIDI note (or -1)
    int fkeyAt(juce::Point<float> d) const;
    void noteOn(int n) { proc.synth().post(Synth::EV_ON, n, 0.85f); }
    void noteOff(int n) { proc.synth().post(Synth::EV_OFF, n, 0); }
    void fkey(int k);
    void drawMonitor(juce::Graphics&);
    void drawKeyboard(juce::Graphics&);

    SidProcessor& proc;
    ScreenUI ui;
    juce::Image screen { juce::Image::ARGB, ScreenUI::W, ScreenUI::H, true };
    std::vector<uint32_t> px;
    int base = 48;                          // lowest key on the keyboard (C3)
    std::map<int, int> sourceNote;          // touch / mouse -> note held
    std::map<int, bool> sourceOnScreen;
    std::array<bool, 128> down {};
    std::map<int, int> qwertyHeld;          // key code -> note
    int fkeyDown = -1; int lastVersion = -1;
    static constexpr float SX = 244, SY = 99, SS = 1.6f;   // where the 320 x 200 screen sits, and its scale
};

}  // namespace sid
