// Omega XR10's fold-out: a panel that opens beside the machine with a mixer (a strip per pad of the bank, plus the
// EFFECT send, the rack's return and the main volume) and the four-slot effects rack.
#pragma once
#include "XR10Plugin.h"

namespace xr10 {

class MixerView : public juce::Component {
public:
    MixerView(Processor& p, const Snapshot& s) : proc(p), snap(s) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { drag = -1; }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::function<void(float)> onMainVolume;
    static const int NSTRIP = xr::NPADS + 3;
private:
    Processor& proc;
    const Snapshot& snap;
    int drag = -1, dragPart = 0;   // part: 0 fader, 1 pan, 2 send
    float stripW() const { return (float) getWidth() / NSTRIP; }
    juce::Rectangle<float> faderArea(int i) const;
    juce::Rectangle<float> panArea(int i) const;
    juce::Rectangle<float> sendArea(int i) const;
    juce::Rectangle<float> muteArea(int i) const;
    juce::Rectangle<float> soloArea(int i) const;
    void setFromMouse(int i, int part, juce::Point<float> pos);
    double value(int i, int part) const;
    void range(int i, int part, double& lo, double& hi) const;
};

class SlotView : public juce::Component {
public:
    SlotView(Processor& p, int slot);
    void paint(juce::Graphics&) override;
    void resized() override;
    void rebuild();               // after the unit or a preset changed
    void refresh(const Snapshot& s);
private:
    Processor& proc;
    const int slot;
    juce::ComboBox type, preset;
    juce::ToggleButton bypass {"bypass"}, parallel {"parallel"};
    struct Ctl { std::unique_ptr<juce::Component> c; std::unique_ptr<juce::Label> label; bool menu = false; int param = 0; };
    std::vector<Ctl> ctls;
    int shownType = -2;
    float peak = 0;
    bool updating = false;
};

class FoldPanel : public juce::Component {
public:
    FoldPanel(Processor& p, const Snapshot& s);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    MixerView mixer;
    std::unique_ptr<SlotView> slots[4];
private:
    Processor& proc;
    const Snapshot& snap;
};

}  // namespace xr10
