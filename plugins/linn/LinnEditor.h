// Omega Linn editor: the front panel exactly as the hardware's (the layout of linn/faceplate.js: 2000 x 1090 units
// between wooden end cheeks), working: the keys (hold one and press another, as on the hardware), the 1-7 keys, the
// two LED displays, the TIMING and ERROR CORRECT LEDs, TEMPO, the tuning knobs, HIHAT DECAY, VOLUME, and the mixer's 16
// pan and level sliders. Under it, the Omega pages: SOUND / HI-HAT, PAD EQ / ADSR, MIXER, FX, MASTER, FILES, with the
// presets and the dice.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "LinnPlugin.h"
#include <map>
#include <set>

namespace linn {

class LinnLook : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

class Faceplate : public juce::Component, private juce::Timer {
public:
    explicit Faceplate(LinnProcessor& p);
    ~Faceplate() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::function<void()> onSave, onLoad;
private:
    void timerCallback() override;
    juce::Point<float> toPlate(juce::Point<float> p) const;
    int keyAt(juce::Point<float>) const;
    int knobAt(juce::Point<float>) const;
    int sliderAt(juce::Point<float>) const;           // 0-15 pan, 16-31 level
    juce::RangedAudioParameter* knobParam(int k) const;
    juce::RangedAudioParameter* sliderParam(int s) const;
    float knobNorm(int k) const;
    void setKnob(int k, float n);
    void setSlider(int s, float yPlate);
    void flash(const juce::String& l, const juce::String& r, int ms) { flashL = l; flashR = r; flashUntil = juce::Time::getMillisecondCounter() + (juce::uint32) ms; }
    LinnProcessor& proc;
    std::map<int, int> downKeys;                 // touch / mouse index -> key
    std::map<int, int> dragKnobs, dragSliders;   // touch index -> knob / slider
    std::map<int, float> dragY0, dragV0;
    juce::String flashL, flashR; juce::uint32 flashUntil = 0, casUntil = 0; int casLed = -1;
};

class LinnEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit LinnEditor(LinnProcessor&);
    ~LinnEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct Knob { std::unique_ptr<juce::Slider> s; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a; juce::String label, suffix; int tab; juce::Rectangle<int> r; std::function<juce::String(double)> fmt; };
    struct Tog { std::unique_ptr<juce::TextButton> b; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> a; juce::String suffix; int tab; juce::Rectangle<int> r; };
    struct Box { juce::String title; int tab; juce::Rectangle<int> r; };
    struct Btn { std::unique_ptr<juce::TextButton> b; int tab; juce::Rectangle<int> r; };
    Knob& addKnob(int tab, const juce::String& id, const juce::String& label, juce::Rectangle<int> r, const juce::String& suffix = {});
    void addTog(int tab, const juce::String& id, const juce::String& label, juce::Rectangle<int> r, const juce::String& suffix = {});
    juce::TextButton& addBtn(int tab, const juce::String& label, juce::Rectangle<int> r, std::function<void()> f);
    void showTab(int t);
    void selectChannel(int v);
    void timerCallback() override;
    void fillPresets();
    void setFolded(bool f);
    void saveTape(); void loadTape(); void loadSound(); void loadFolder(); void importMidi(bool asSong);
    void say(const juce::String& s) { message = s; repaint(); }
    bool folded = false;
    LinnProcessor& proc;
    LinnLook look;
    Faceplate plate;
    std::vector<Knob> knobs; std::vector<Tog> togs; std::vector<Box> boxes; std::vector<Btn> btns;
    static constexpr int NT = 6;
    juce::TextButton tabs[NT], chBtn[NV], prevBtn{"<"}, nextBtn{">"}, diceBtn{"DICE"}, foldBtn{"FOLD"};
    juce::ComboBox presetBox, partBox, ecBox, barsBox;
    juce::ToggleButton tempoTog{"Take the file's tempo"};
    std::unique_ptr<juce::FileChooser> chooser;
    int tab = 0, ch = 0, lastSel = -1;
    juce::uint32 lastParamsHash = 0;
    juce::String message;
public:
    static constexpr int W = 2120, PH = 1090, MH = 760, TOP = 150;
};

} // namespace linn
