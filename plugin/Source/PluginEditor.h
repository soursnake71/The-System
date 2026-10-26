#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

/** The radar: planes, trails, the heat grid and its playhead, flashes when a plane plays. */
class Radar : public juce::Component, private juce::Timer
{
public:
    explicit Radar (SkyProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool labels = true, trails = true;
private:
    void timerCallback() override;
    SkyProcessor& proc;
    juce::String selected;
    std::map<juce::uint32, std::pair<double, int>> lit;    // plane hash -> (time, lane)
    std::map<int, double> litCells;
    float sweep = 0;
    struct Drawn { juce::String id; float px, py; };
    std::vector<Drawn> drawn;
};

/** A channel meter: a bar that jumps up to the peak and falls back. */
struct Meter : juce::Component
{
    float level = 0;
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b1f15)); g.fillRoundedRectangle (b, 2);
        const float h = b.getHeight() * juce::jlimit (0.0f, 1.0f, std::sqrt (level));
        g.setColour (level > 0.9f ? juce::Colour (0xffff5e5e) : colour); g.fillRoundedRectangle (b.removeFromBottom (h), 2);
    }
    juce::Colour colour { 0xff4dffa6 };
};

class SkyEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SkyEditor (SkyProcessor&);
    ~SkyEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void runSearch();
    void locateMe();
    void showMessage (const juce::String&, bool bad = false);
    void refreshWhere();

    SkyProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    Radar radar;
    juce::Label title, status, placeLbl;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    struct Page : juce::Component { std::function<void (juce::Rectangle<int>)> layout; void resized() override { if (layout) layout (getLocalBounds().reduced (10)); } };
    Page where, lanes, music, presets, synthPage, drumsPage, mixerPage;

    // Where
    juce::Label searchHint, msg, latLbl, lonLbl, radiusLbl, feedLbl, urlLbl, windowLbl, threshLbl, airportLbl;
    juce::TextEditor search, latEd, lonEd, urlEd;
    juce::TextButton go { "Find" }, locate { "Locate me" }, setLL { "Set" }, freeze { "Freeze grid" }, clearGrid { "Clear grid" }, labelsBtn { "Labels" }, trailsBtn { "Trails" };
    juce::ComboBox results, airportBox, sourceBox, windowBox;
    juce::Slider radius, thresh;
    std::vector<sky::Place> found;

    // Lanes
    struct LaneRow
    {
        juce::Label name; juce::ToggleButton on { "On" }; juce::ComboBox engine, ch, drum, rhythm; juce::Slider vol, rev, dly, dv, gate; juce::Label vl, rl, dl, dvl, gl;
        juce::ToggleButton autoBtn { "Auto-vary" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aAuto;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aRhythm;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aGate;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aOn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aEngine, aCh, aDrum;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aVol, aRev, aDly, aDv;
    };
    std::array<LaneRow, numLanes> rows;
    juce::Label lanesHint, autoLbl;
    juce::ComboBox autoBox;

    // Mixer: six lanes and the drums, then master and the space effects
    struct Strip
    {
        juce::Label name; Meter meter; juce::Slider fader, pan, rev, dly, gtd, echo; juce::TextButton mute { "M" }, solo { "S" };
        std::array<juce::Label, 5> knobLabels;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aMute, aSolo;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> att;
    };
    std::array<Strip, 7> strips;
    std::array<juce::Label, 5> masterLbls;
    std::array<juce::Slider, 5> masterSliders;
    int tick = 0;

    // Presets
    juce::Label presetHint, presetNow;
    juce::ComboBox presetBox;
    juce::TextButton genBtn { "Generate a preset" }, prevBtn { "<" }, nextBtn { ">" };

    // OB-Xa (edits one lane's patch at a time)
    juce::Label synthHint;
    juce::ComboBox synthLane;
    juce::TextButton rndPatch { "Random patch" };
    std::array<juce::Label, 21> pLabels;
    std::array<juce::Slider, 21> pSliders;
    std::array<juce::ComboBox, 21> pCombos;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> patchSliderAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> patchComboAtt;
    void bindPatch (int lane);

    // Drums & FX
    juce::Label kitHint, fxHint, dmodeLbl, dmixLbl, dfbLbl, dtoneLbl;
    juce::TextButton rndKit { "Random kit" };
    std::array<juce::Label, 24> kLabels;
    std::array<juce::Slider, 24> kSliders;
    juce::Label mainBeatLbl;
    juce::ComboBox mainBeatBox;
    juce::TextButton rndBeat { "Random" };
    juce::ComboBox dmodeBox;
    juce::Slider dmix, dfb, dtone;

    // Music
    juce::Label keyLbl, scaleLbl, modeLbl, bpmLbl, masterLbl, about;
    juce::ComboBox keyBox, scaleBox, modeBox;
    juce::Slider bpm, master;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkyEditor)
};
