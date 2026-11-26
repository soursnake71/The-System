// RX5 plugin editor: the faceplate of five/index.html drawn at its own size (1300 x 973) and scaled to the
// window (fixed aspect, resizable). Faders, the LCD with the same menus and pages as five/app.js (ACCENT 1 / 2 taps or
// the LCD halves step through items, -1/NO and +1/YES change values or confirm, DATA sets them, the keypad types
// numbers and quantize values, the printed legend opens a menu at that job), function keys, pads (velocity from
// where a pad is struck), file jobs through file choosers, and drag and drop (.o5cart / .syx / .mid / .bin / WAV
// on a pad / a kit folder or .zip).
#pragma once
#include "FivePlugin.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <set>

namespace five {

class FivePanel : public juce::Component, public juce::FileDragAndDropTarget {
public:
    static constexpr int W = 1300, H = 973, RAIL = 150;   // the faceplate; the rail of page keys at its left (not when folded)
    int totalWidth() const { return folded ? W : W + RAIL; }
    std::function<void()> onLayout;                       // the editor resizes when FOLD / EXPAND changes the width
    explicit FivePanel(FiveProcessor&);
    ~FivePanel() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override { if (magCol >= 0) repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray&, int x, int y) override;
    void tick();                                  // 30 Hz from the editor

    // for tests: press a key / pad by name ("START", "N5", "YES", "EDIT VOICE", "PAD3" ...), read the LCD
    void key(const juce::String& k);
    juce::String lcdText() const;
    void pressAt(juce::Point<float> p);           // a tap at component coordinates (rail, pages, loupe, faceplate)
    void releaseAt();
    juce::Point<float> facePoint(float x, float y) const { return { x + ox(), y }; }   // faceplate -> component (tests)
    void showView(const juce::String& v) { view = v; magCol = -1; loupeOn = false; repaint(); }
    void setFolded(bool f);
    void setZoom(bool z) { zoom = z; loupeOn = false; repaint(); }
    bool isFolded() const { return folded; }
    juce::String currentView() const { return view; }
    int viewPages();                              // pages of the open view
    juce::StringArray visibleRows();              // the rows on the open page (name: value)
    bool loupeOpen() const { return loupeOn; }
    bool pressRowKey(const juce::String& row, const juce::String& key);   // tests: a key on the open page's row
    bool pressRail(const juce::String& label);
    bool magnified() const { return magCol >= 0; }

    struct Item {
        double no = 0; juce::String name;
        std::function<juce::String()> ctx, l2;
        std::function<void(int, bool)> inc; std::function<void(double)> data; std::function<void()> yes, noFn;
        std::function<void(int, int)> pad; std::function<void(int)> digit, move;
        juce::String yesLabel;                    // the YES key's name on a page (e.g. M for mute)
    };
    struct Page {
        std::function<juce::StringArray()> lcd;   // line 1, line 2, optional cursor column
        std::function<void(int, bool)> pm; std::function<void(int)> digit, move; std::function<void(double)> data;
        std::function<void(int, int, int, int)> pad; bool padOnly = false, silentToggle = false;
    };

private:
    FiveProcessor& proc; Ui& U;
    State& S() { return proc.state(); }
    template <class F> void edit(F&& f) { const FiveProcessor::Edit l(proc.lock); f(proc.state()); }

    // regions
    enum Kind { kFader, kMode, kFn, kPad, kKeypad, kBig, kLcd, kLegend, kDice };
    struct Region { juce::Rectangle<float> r; Kind kind; juce::String id; double no = 0; int col = -1; };
    std::vector<Region> regions;
    const Region* hit(juce::Point<float>) const;
    void build();

    // menus and pages
    std::map<juce::String, std::vector<Item>> menus;
    std::map<juce::String, Page> pages;
    void buildMenus();
    Item numItem(double no, const juce::String& name, std::function<double()> get, std::function<void(double)> set, double lo, double hi,
                 double step = 1, double fine = 0, std::function<juce::String(double)> fmt = nullptr, int digits = 0, double curve = 0);
    Item listItem(double no, const juce::String& name, std::function<int()> get, std::function<void(int)> set, juce::StringArray list);
    Item actItem(double no, const juce::String& name, std::function<void()> run, std::function<juce::String()> l2 = nullptr);
    Item& curItem();
    Page& page();
    Page menuPage;
    void openMenu(const juce::String& m, double no = -1);
    void backToPlay();
    void flash(const juce::String& a, const juce::String& b, double ms = 1400);
    void paintLcdLines(juce::String& a, juce::String& b, int& cur);
    juce::String typed(int d, int max = 2);
    juce::String digits; double digitT = 0;

    // keys
    void keyDown(const juce::String& k);
    void keyUp(const juce::String& k);
    void modeKey(const juce::String& k);
    void fnKey(const juce::String& k, bool shift);
    void hitPad(int i, int vel);
    std::set<juce::String> held; bool stopWasRun = false, shiftUsed = false, accUsed = false;
    int padVoice(int i);
    // the transport and the pages
    void selectPattern(int n); void selectSong(int n); void setTempoUi(double bpm); double curTempo();
    void qSet(int i); void qDigit(int d);
    void songInsert(const juce::String& type); void songDelete();
    bool inSongEdit(); bool inChainEdit();
    void songEditInc(int d, bool sh); void songEditMove(int d); void songEditData(double x); void songEditDigit(int d);
    juce::String stepDesc(const SongStep* s);
    void searchMark(bool play);
    void doRandom();
    void openName(std::function<juce::String()> get, std::function<void(const juce::String&)> set);
    struct NameEdit { juce::String back, backMenu; juce::String buf; int cur = 0; std::function<void(const juce::String&)> set; } nameState;
    void nameType(juce::juce_wchar c);
    void nameDone();
    bool protectedMsg();
    // multi voice (KEY ASSIGN 03)
    bool multiOn = false; int multiV = 0, multiParam = 0; double multiStep[4] = { 100, -1, -3, -3 };
    int padVel = 0; std::map<int, VoiceParams> stored;

    // files
    std::unique_ptr<juce::FileChooser> chooser;
    void pickFile(const juce::String& title, const juce::String& patterns, std::function<void(const juce::File&)> fn, bool save = false, const juce::String& defName = {}, bool dirs = false);
    void report(const juce::String& what, const juce::String& err);

    // painting / animation
    std::array<float, NV> padLit {}; int pressedPad = -1; juce::String pressedKey;
    struct Drag { juce::String id; float y0 = 0; double v0 = 0; } drag;
    double faderValue(const juce::String& id);
    void setFader(const juce::String& id, double v);
    float meter = 0;
    // the magnifier: a tap on the tiny printed legend shows that column at MAG x; a tap on a line there opens that job
    static constexpr float MAG = 3;
    int magCol = -1;
    struct MagLine { juce::Rectangle<float> r; juce::String text; bool head; juce::String menu; double no; };
    std::vector<MagLine> magLayout(juce::Rectangle<float>& box) const;
    void paintMag(juce::Graphics&);
    // the rail, the pages (every LCD command folded open, big) and the loupe (ZOOM): like Omega 110 and the app
    bool folded = false, zoom = false, loupeOn = false; juce::String view = "machine";
    juce::Point<float> loupeC; static constexpr float LOUPE = 3; juce::Rectangle<float> loupeRect() const;
    float ox() const { return folded ? 0.f : (float) RAIL; }
    struct Btn { juce::Rectangle<float> r; juce::String label; int style; std::function<void()> fn; bool on = false; };
    struct RowDraw { juce::Rectangle<float> r; juce::String name, value; bool head; };
    std::map<juce::String, std::vector<Item>> viewItems; std::map<juce::String, int> viewPage;
    void buildViews();
    std::vector<std::vector<int>> paginateView(const juce::String& v);
    void railLayout(std::vector<Btn>&);
    void pageLayout(std::vector<Btn>&, std::vector<RowDraw>&);
    void paintFace(juce::Graphics&);
    void paintBtn(juce::Graphics&, const Btn&);
    void facePress(juce::Point<float>);
    bool dragFromLoupe = false;
};

class FiveEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit FiveEditor(FiveProcessor&);
    ~FiveEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    FivePanel& panel() { return content; }
private:
    void timerCallback() override { content.tick(); }
    FiveProcessor& proc;
    FivePanel content;
};

}  // namespace five
