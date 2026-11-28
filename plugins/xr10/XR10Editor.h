#pragma once
#include "XR10Plugin.h"
#include "XR10Fold.h"
#include "../common/Loupe.h"

namespace xr10 {

// the photo of the front panel with working keys, knobs, lights and screen
class Face : public juce::Component {
public:
    explicit Face(Processor& p);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    const Snapshot* snap = nullptr;
    float volume = 80, dataAngle = 0;

private:
    struct Hot { int id; float x, y, w, h; bool pad; };
    std::vector<Hot> hots;
    Processor& proc;
    juce::Image plate;
    int pressed = -1, dragKnob = -1;
    float lastX = 0, lastY = 0, acc = 0;
    juce::Rectangle<float> photo(float x, float y, float w, float h) const;
    float sx() const { return (float) getWidth() / 800.f; }
    float sy() const { return (float) getHeight() / 549.f; }
};

// no scrolling menus: every popup lays its items out in as many columns as it takes to fit on the screen
struct NoScrollLook : juce::LookAndFeel_V4 {
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override {
        return juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label).withMaximumNumColumns(12);
    }
};

class Editor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showTab(int t);
    void buildVoice();
    void buildGrid();
    void buildSong();
    void buildSetup();
    void refresh();
    void paintGrid(juce::Graphics&, juce::Rectangle<int>);
    void paintSong(juce::Graphics&, juce::Rectangle<int>);
    void paintWave(juce::Graphics&, juce::Rectangle<int>);
    void mouseDown(const juce::MouseEvent&) override;
    void refreshPatterns();
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override;
    void importMidiFile(const juce::File& f);
    juce::File writeMidiTemp(bool song);
    // a button you drag into the DAW: it hands over a .mid file
    struct DragOut : juce::TextButton {
        std::function<void()> onDragOut;
        bool fired = false;
        void mouseDown(const juce::MouseEvent& e) override { fired = false; juce::TextButton::mouseDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override { if (!fired && e.getDistanceFromDragStart() > 6 && onDragOut) { fired = true; onDragOut(); } }
    };

    NoScrollLook look;
    Processor& proc;
    Snapshot snap;
    juce::Component content;     // everything, drawn at the base size and scaled to the window
    juce::Component view;        // beside the rail: the machine, or the open page (what ZOOM magnifies)
    omega::Loupe loupe;
    juce::TextButton railKeys[9], expandBtn {"EXPAND"};
    Face face;
    struct Page : juce::Component {
        std::function<void(juce::Graphics&)> painter;
        std::function<void(const juce::MouseEvent&)> click;
        void paint(juce::Graphics& g) override { if (painter) painter(g); }
        void mouseDown(const juce::MouseEvent& e) override { if (click) click(e); }
    } pages[5];   // sounds, grid, song, setup, effects
    juce::TextButton tabs[5], play {"START / STOP"}, rec {"RECORD"}, tap {"TAP"}, diceBeat {"RANDOM BEAT"}, diceSound {"RANDOM SOUND"};
    juce::Label fxHelp;
    juce::TextButton foldBtn {"FOLD-OUT >"};
    std::unique_ptr<FoldPanel> fold;
    void setFold(bool open);
    int baseW() const;
    juce::ComboBox patternBox;
    juce::Slider tempo;
    juce::Label tempoLabel {{}, "TEMPO"};
    // voices
    juce::TextButton inst[xr::NSOUNDS];
    juce::Slider params[xr::NVP];
    juce::Label paramLabels[xr::NVP], voiceTitle;
    juce::TextButton loadSmp {"Load sample..."}, loadSet {"Load sample folder..."}, romSmp {"ROM sound"}, resetVoice {"Reset voice"};
    juce::ComboBox assignPad;
    juce::Rectangle<int> waveArea;
    std::vector<short> wave;
    int waveFor = -1, waveUser = -1;
    // grid
    juce::TextButton partBtn[xr::NPARTS], barBtn[8], clearPart {"Clear part"}, midiImp {"Import MIDI..."}, midiExp {"Export MIDI..."};
    DragOut midiDrag, songDrag;
    juce::ComboBox lenBox, beatsBox, subBox, bankBox, swingBox;
    juce::Rectangle<int> gridArea;
    int gridBar = 0;
    // song
    juce::ComboBox songBox, stepPattern, stepPart;
    juce::Slider stepRepeat, songTempo;
    juce::TextButton songMidi {"Export song MIDI..."};
    juce::TextButton songAdd {"Add current pattern"}, songDup {"Duplicate step"}, songDel {"Delete step"}, songPlay {"Play song"};
    juce::Rectangle<int> songArea;
    int songSel = 0, songFirst = 0;
    // setup
    juce::ComboBox fxType, tcBox, polyBox, midiCh, midiMap, syncBox, clickBox;
    juce::Slider effectVr, fxTime, fxLevel, clickLvl, accVel;
    juce::ToggleButton padVel {"Touch velocity on pads"}, countIn {"Count-in bar"};
    juce::TextButton exportBtn {"Save memory to a file..."}, importBtn {"Load memory file..."}, initBtn {"Initialize all"};
    juce::Label setupLabels[12];
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::String> patNames;
    int patNamesAge = 0, tab = 0;
    bool updating = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

}  // namespace xr10
