#pragma once
#include "TwelvePlugin.h"

namespace twelve {

// the panel: grey face, cream keys, black knobs and pads, the yellow-green display
struct TwelveLook : juce::LookAndFeel_V4 {
    TwelveLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
};

// a square key: cream, red or black
struct PanelKey : juce::Button {
    enum Kind { cream, red, black } kind;
    explicit PanelKey(Kind k = cream) : juce::Button({}), kind(k) {}
    void paintButton(juce::Graphics&, bool over, bool down) override;
};

class TwelveEditor;

// the display: two text lines, the waveform with the IN / OUT flags and the cursor, or a menu; an info line
class Lcd : public juce::Component {
public:
    explicit Lcd(TwelveEditor& e);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    juce::Rectangle<int> waveArea() const;
    std::pair<int64_t, int64_t> view() const;
    int64_t sampleAt(int x) const;
    float xOf(int64_t i) const;
    TwelveEditor& ed; int mode = 0;
};

// one of the eight faders: it controls whatever the lit page puts on it
class Fader : public juce::Component {
public:
    Fader(TwelveEditor& e, int index) : ed(e), i(index) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    double valueAt(int y) const;
    juce::Rectangle<int> track() const { return getLocalBounds().withTrimmedTop(16).withTrimmedBottom(32); }
    TwelveEditor& ed; int i; uint32_t lastDown = 0; bool dragging = false;
};

struct Pad : juce::Component {
    std::function<void()> onHit; bool lit = false;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { lit = true; repaint(); if (onHit) onHit(); }
    void mouseUp(const juce::MouseEvent&) override { lit = false; repaint(); }
};

// everything is laid out at one size on this canvas, which is scaled to the window: the window is resizable
struct Canvas : juce::Component {
    std::function<void(juce::Graphics&)> draw;
    std::function<void(const juce::MouseEvent&)> click;
    void paint(juce::Graphics& g) override { if (draw) draw(g); }
    void mouseUp(const juce::MouseEvent& e) override { if (click) click(e); }
};

class TwelveEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    static constexpr int BW = 1200, BH = 1000;   // the layout's own size
    explicit TwelveEditor(TwelveProcessor&);
    ~TwelveEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // ---- shared with the display and the faders ----
    TwelveProcessor& proc;
    struct Target { int id = -1, pad = -1; double min = 0, max = 1, def = 0, step = 0; bool log = false; juce::String name, shortName; };
    Target faderTarget(int i) const;
    double targetValue(const Target& t) const;
    void setTarget(const Target& t, double v, bool gesture);
    void endGesture(const Target& t);
    juce::String fmt(const Target& t, double v) const;
    struct Item { int n; juce::String name; std::function<void()> run; std::function<juce::String()> value; };
    std::vector<Item> menuItems() const;
    juce::String menuTitle() const;
    void runItem(int n);
    juce::String line1() const, line2() const, line2r() const;
    void say(const juce::String& s, int ms = 1800) { msg = s; msgUntil = juce::Time::getMillisecondCounter() + (uint32_t) ms; repaintAll(); }
    void repaintAll() { content.repaint(); lcd.repaint(); for (auto& f : faders) f->repaint(); }
    int menu = -1;   // 0 set-up, 1 disk, 2 sync, 3 sample, 4 tempo
    struct Edit { juce::String label; std::function<juce::String()> show; std::function<void(int)> step; int n = -1; };
    std::unique_ptr<Edit> edit;
    struct Entry { juce::String label, text; std::function<void(double)> done; };
    std::unique_ptr<Entry> entry;
    int64_t cursor = -1; bool zoom = false; int touched = -1; juce::String touchedName, touchedVal;
    void setIn(); void setOut();
    bool learnArmed = false;

private:
    Canvas content;
    Lcd lcd;
    TwelveLook look;
    std::vector<std::unique_ptr<PanelKey>> keys;
    std::vector<std::unique_ptr<Fader>> faders;
    std::vector<std::unique_ptr<Pad>> pads;
    std::vector<std::unique_ptr<juce::Slider>> knobs;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> kAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    PanelKey* keyAt(const juce::String& name) { auto it = named.find(name); return it == named.end() ? nullptr : it->second; }
    std::map<juce::String, PanelKey*> named;
    PanelKey& addKey(const juce::String& name, PanelKey::Kind k, juce::Rectangle<int> r, std::function<void()> f);
    void drawContent(juce::Graphics&);
    void timerCallback() override;
    void job(std::function<juce::String()> f) { proc.runJob(f, [this](juce::String r) { say(r.toUpperCase().substring(0, 40), 2500); }); }
    void openMenu(int m);
    void keypad(const juce::String& k);
    void arrow(int d);
    void enter();
    void openFile(const juce::String& title, std::function<void(juce::File)> then);
    void doExport();
    void showPresets();
    void showHelp();
    void stepPreset(int d);
    int page = 0, bank = 0, shift = 0, learning = -1;
    int fmtIdx = 1, srIdx = 1, lufsIdx = 2, toKey = 9; bool wholeFile = false, stretchWarp = true;
    double srcBpm = 0, tgtBpm = 90;
    juce::String msg; uint32_t msgUntil = 0;
    bool wasPlaying = false;
};

}  // namespace twelve
