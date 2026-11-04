// The CMI's screen: green phosphor in a cream monitor, the light pen is the mouse.
// Pages: 2 (library), 7 (controls), D (the waveform in 3D), 8 (sampling), R (the real-time composer).
#pragma once
#include "CmiPlugin.h"

namespace cmi {

class CmiEditor;

class Screen : public juce::Component {
public:
    static constexpr int W = 1000, H = 640;
    explicit Screen(CmiEditor& e);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    char page = 'R';
    int rootKey = 60; bool loopIt = false;
    juce::String status = "READY";

private:
    struct Hit {
        juce::Rectangle<float> r;
        std::function<void(const juce::MouseEvent&)> down, drag, up;
        std::function<void(float)> wheel;
    };
    juce::ModifierKeys wheelMods; float hitDy = 0;
    std::vector<Hit> hits; int active = -1;
    Hit& hit(juce::Rectangle<float> r, std::function<void(const juce::MouseEvent&)> down = {}, std::function<void(const juce::MouseEvent&)> drag = {});

    void drawHeader(juce::Graphics&);
    void drawLibrary(juce::Graphics&);
    void drawControls(juce::Graphics&);
    void drawWave(juce::Graphics&);
    void drawSampling(juce::Graphics&);
    void drawMixer(juce::Graphics&);
    void drawFiles(juce::Graphics&);
    void drawSongFiles(juce::Graphics&);
    void drawPen(juce::Graphics&);
    void drawComposer(juce::Graphics&);
    void drawMidiSong(juce::Graphics&);
    void drawGrid(juce::Graphics&, juce::Rectangle<float> area);
    void songMenu();
    void exportMenu();
    void learnMenu(const char* id);
    void bar(juce::Graphics&, juce::Rectangle<float> r, const char* id, const juce::String& label, std::function<juce::String(float)> fmt);
    void button(juce::Graphics&, juce::Rectangle<float> r, const juce::String& text, bool inverse, std::function<void()> fn);
    void soundMenu(std::function<void(int)> chosen);
    void command(const juce::String& cmd);

    CmiEditor& ed;
    int wavePage = 0, selTrack = 0, libScroll = 0, userPage = 0, dragRoot = 60;
    // the song editing grid (a piano roll of one track)
    bool songGrid = false; int gridTrack = -1;
    float cycle[2][128] {}; int penBox = 0, lastPx = -1; float lastPv = 0; int filePage = 0, songPage = 0; bool songFiles = true;
    double viewStart = 0, viewBeats = 16, snap = 0.25; int pitchLow = 48;
    std::vector<SongNote> work; int dragIdx = -1, dragMode = 0; SongNote dragOrig {}; double grabBeat = 0; int grabPitch = 0; bool dirty = false; const Sound* waveSound = nullptr;
    bool songEditor = false;
    friend class CmiEditor;
};

class CmiEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit CmiEditor(CmiProcessor&);
    ~CmiEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void showPage(char p) { screen.page = p; screen.repaint(); }
    void showSongGrid(bool on, int track = 0) { screen.songGrid = on; screen.selTrack = track; screen.repaint(); }
    CmiProcessor& proc;
    juce::TextEditor cmdLine;
    std::unique_ptr<juce::FileChooser> chooser;
private:
    void timerCallback() override;
    Screen screen;
    juce::MidiKeyboardComponent keys;
    juce::LookAndFeel_V4 look;
};

}  // namespace cmi
