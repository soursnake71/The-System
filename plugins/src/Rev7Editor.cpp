// The Omega REV7 panel: laid out like the hardware's front panel. Input meter, red MEMORY digits and the two-line LCD
// in the display window; the input level, EQ and mixing knobs under it; the program keys (REV1-4, E/R1-2, OTHERS,
// USER MEMORY), the edit keys (REV TIME, INITIAL DELAY, 1ST REF, F1-F3, -/+), the number pad with MEMORY, STORE,
// RECALL, ENTER, and MUTE, OUT PHASE, MIDI CTRL and BYPASS on the right.
//
// How the keys work (as on the unit):
//  - a program key recalls 1-6 (or user 31-36 when USER MEMORY is lit; OTHERS then gives 37)
//  - type a number on the pad, then RECALL (or ENTER) to recall it: 1-30 factory, 31-90 your memories
//  - type a number 31-90, then STORE, to save the current settings there (kept with your project)
//  - REV TIME / INITIAL DELAY / 1ST REF / F1-F3 pick a parameter (what F1-F3 are depends on the program type);
//    -/+ step it, or type a value and ENTER. Press the lit key again or MEMORY to go back to program numbers.
//  - click the LCD for a menu of every preset (factory, artists, your memories); the mouse wheel on it edits.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../common/Plugin.h"
#include "../dsp/Rev7.h"

namespace omega {

const char* rev7Name(int n);

namespace {

constexpr int W = 1100, H = 232;
const juce::Colour kFace(0xff1a1b1e), kGlass(0xff07080a), kText(0xffd9dadf), kDim(0xff8b8e96), kLcdBg(0xff2a3324), kLcdOn(0xffcfe8a8),
    kRed(0xffff2a1a), kGreen(0xff63e36b), kKey(0xff222326), kGrey(0xff8d8f93);

using K = dsp::Rev7::Kind;
// the parameter on each edit key for each program type: REV TIME, F1, F2, F3
const char* const* keysFor(K k) {
    static const char* REV[4] = {"rt", "hi", "lo", "diff"}, *ER[4] = {"live", "mode", "size", "diff"}, *DLY[4] = {"", "dlyL", "dlyR", "dlev"},
                      *ECHO[4] = {"fb", "dlyL", "dlyR", "hdamp"}, *FL[4] = {"fb", "mfreq", "mdep", "mdly"}, *RFL[4] = {"rt", "mfreq", "mdep", "mdly"},
                      *CH[4] = {"", "mfreq", "mdep", "amdep"}, *PH[4] = {"", "mfreq", "mdep", "mdly"}, *TR[4] = {"", "mfreq", "mdep", ""};
    switch (k) {
        case K::REV: return REV; case K::ER: return ER; case K::DLY: return DLY; case K::ECHO: return ECHO; case K::FLANGE: return FL;
        case K::RFLANGE: return RFL; case K::CHORUS: return CH; case K::PHASE: return PH; case K::TREM: case K::SYMPH: return TR;
    }
    return TR;
}

juce::String lcdValue(const juce::String& id, double v, K kind) {
    auto f1 = [](double x) { return juce::String(x, 1); };
    if (id == "rt") return "REV TIME=" + f1(v) + "s";
    if (id == "hi") return "HIGH : x " + f1(v);
    if (id == "lo") return "LOW  : x " + f1(v);
    if (id == "diff") return "DIFFUSION= " + juce::String((int) std::lround(v));
    if (id == "idly") return "INI.DLY=" + f1(v) + "ms";
    if (id == "fdly") return "1ST DLY=" + f1(v) + "ms";
    if (id == "flev") return "1ST LEVEL=" + juce::String((int) std::lround(v * 100)) + "%";
    if (id == "live") return "LIVENESS= " + juce::String((int) std::lround(v));
    if (id == "mode") { static const char* m[6] = {"S.HALL", "L.HALL", "RANDOM", "REVERSE", "PLATE", "SPRING"}; const int i = juce::jlimit(1, 6, (int) std::lround(v)); return "MODE=" + juce::String(i) + " " + m[i - 1]; }
    if (id == "size") return "ROOM SIZE=x" + f1(v);
    if (id == "dlyL") return "Lch DLY=" + f1(v) + "ms";
    if (id == "dlyR") return "Rch DLY=" + f1(v) + "ms";
    if (id == "dlev") return "LEVEL=" + juce::String((int) std::lround(v * 100)) + "%";
    if (id == "fb") return "F.B.GAIN=" + juce::String((int) std::lround(v * 100)) + "%";
    if (id == "hdamp") return "HIGH DAMP= " + juce::String((int) std::lround(v));
    if (id == "mfreq") return "MOD FREQ=" + f1(v) + "Hz";
    if (id == "mdep") return juce::String(kind == K::CHORUS ? "DM DEPTH=" : "MOD DEPTH=") + juce::String((int) std::lround(v * 100)) + "%";
    if (id == "mdly") return "DELAY=" + f1(v) + "ms";
    if (id == "amdep") return "AM DEPTH=" + juce::String((int) std::lround(v * 100)) + "%";
    return {};
}
bool isPercent(const juce::String& id) { return id == "flev" || id == "dlev" || id == "fb" || id == "mdep" || id == "amdep"; }

class PanelLook : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override {
        const float cx = x + w * .5f, cy = y + h * .5f, r = juce::jmin(w, h) * .5f - 2;
        g.setColour(juce::Colour(0xff050506)); g.fillEllipse(cx - r - 1.5f, cy - r - 1.5f, (r + 1.5f) * 2, (r + 1.5f) * 2);
        // knurled skirt and cap
        for (int i = 0; i < 24; ++i) { const float a = i * juce::MathConstants<float>::twoPi / 24; g.setColour(juce::Colour(0xff26272b));
            g.drawLine(cx + std::cos(a) * r * .78f, cy + std::sin(a) * r * .78f, cx + std::cos(a) * r, cy + std::sin(a) * r, 1.f); }
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3b40), cx, cy - r, juce::Colour(0xff0e0f11), cx, cy + r, false));
        g.fillEllipse(cx - r * .74f, cy - r * .74f, r * 1.48f, r * 1.48f);
        const float ang = a0 + pos * (a1 - a0);
        juce::Path p; p.addRoundedRectangle(-1.1f, -r, 2.2f, r * .62f, 1.f);
        g.setColour(juce::Colours::white); g.fillPath(p, juce::AffineTransform::rotation(ang).translated(cx, cy));
    }
};

// a front-panel key: dark (or grey for the number pad), its name, an optional "-31-" under it, and a lamp
class Key : public juce::Button {
public:
    Key(juce::String name, juce::String subText = {}, bool isGrey = false) : juce::Button(name), sub(std::move(subText)), grey(isGrey) {}
    std::function<bool()> lit;
    juce::String sub; bool grey;
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(1.f);
        g.setColour(juce::Colour(0xff050506)); g.fillRoundedRectangle(r, 2.5f);
        auto face = r.reduced(1.5f).translated(0, down ? .8f : 0);
        juce::Colour c = grey ? kGrey : kKey; if (over) c = c.brighter(.12f);
        g.setGradientFill(juce::ColourGradient(c.brighter(.15f), face.getX(), face.getY(), c.darker(.25f), face.getX(), face.getBottom(), false));
        g.fillRoundedRectangle(face, 2.f);
        g.setColour(grey ? juce::Colour(0xff1b1c1f) : kText);
        g.setFont(juce::FontOptions(grey && getName().length() <= 1 ? 15.f : getName().length() > 10 ? 8.f : 9.5f, juce::Font::bold));
        auto tr = face.reduced(2, 3);
        if (sub.isNotEmpty()) {
            g.drawFittedText(getName(), tr.removeFromTop(tr.getHeight() * .55f).toNearestInt(), juce::Justification::centred, 2);
            g.setFont(juce::FontOptions(8.5f)); g.drawFittedText(sub, tr.toNearestInt(), juce::Justification::centred, 1);
        } else g.drawFittedText(getName(), tr.toNearestInt(), juce::Justification::centred, 2, .5f);
        if (lit && lit()) { g.setColour(kRed); g.fillEllipse(face.getX() + 3, face.getY() + 3, 4.5f, 4.5f); g.setColour(kRed.withAlpha(.3f)); g.fillEllipse(face.getX() + 1, face.getY() + 1, 8.5f, 8.5f); }
    }
};

// the small square push switches (MONO/STEREO, EQ ON/OFF)
class Switch : public juce::ToggleButton {
public:
    void paintButton(juce::Graphics& g, bool, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(2);
        g.setColour(juce::Colour(0xff050506)); g.fillRect(r);
        auto f = r.reduced(2).translated(0, getToggleState() || down ? 1.5f : 0);
        g.setColour(getToggleState() ? juce::Colour(0xff2c2d31) : juce::Colour(0xff3b3c41)); g.fillRect(f);
        g.setColour(juce::Colour(0xff55565c)); g.drawRect(f, 1.f);
    }
};

class Panel : public juce::Component, private juce::Timer {
public:
    explicit Panel(Processor& p) : proc(p), st(p.state()) {
        setLookAndFeel(&look);
        // ---- knobs and switches along the bottom of the display section
        struct KD { const char* id; const char* top; const char* lo; const char* hi; const char* mid; int x; };
        const KD kd[8] = {{"in", "INPUT\nLEVEL", "0", "10", "", 186}, {"eqlf", "FREQ", "50", "700", "100   300", 268}, {"eqlg", "LEVEL", "-15", "+15", "", 312},
                          {"eqmf", "FREQ", ".35", "5", "0.8    2", 356}, {"eqmg", "LEVEL", "-15", "+15", "", 400}, {"eqhf", "FREQ", "2", "20", "      10", 444},
                          {"eqhg", "LEVEL", "-15", "+15", "", 488}, {"omix", "MIXING", "DIRECT", "REV", "", 538}};
        for (const auto& k : kd) {
            auto* s = knobs.add(new juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
            s->setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
            s->setBounds(k.x - 15, 162, 30, 30); s->setPopupDisplayEnabled(true, true, this);
            addAndMakeVisible(s); kat.add(new juce::AudioProcessorValueTreeState::SliderAttachment(st, k.id, *s));
            if (auto* d = proc.spec().params.data() + proc.spec().index(k.id)) s->setDoubleClickReturnValue(true, d->def);
            labels.push_back({k.x, k.top, k.lo, k.hi, k.mid});
        }
        auto sw = [&](const char* id, int x) { auto* b = switches.add(new Switch()); b->setBounds(x - 9, 168, 18, 18); addAndMakeVisible(b);
                                               bat.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(st, id, *b)); return b; };
        sw("mono", 140)->setTooltip("MONO / STEREO: in mono the left input feeds both sides");
        sw("eq", 226)->setTooltip("EQ ON / OFF: the parametric EQ ahead of the converter (wet only)");
        sw("circ", 96)->setTooltip("VINTAGE: the 31.25 kHz / 16-bit converters, 12 kHz LC filters and emphasis (off: clean)");

        // ---- program keys, edit keys, pad, memory keys, output keys
        const int top = 22, dy = 46, kw = 42, kh = 38;
        auto key = [&](Key* k, int x, int y, std::function<void()> fn, std::function<bool()> lit = {}) {
            keys.add(k); k->setBounds(x, y, kw, kh); k->onClick = std::move(fn); k->lit = std::move(lit); addAndMakeVisible(k); return k; };
        const int c1 = 604, c2 = 650, c3 = 708, c4 = 754, pad = 812, mem = 950, out = 1010;
        const char* progKeys[6] = {"REV1", "REV2", "REV3", "REV4", "E/R1", "E/R2"};
        for (int i = 0; i < 6; ++i) {
            const int x = i < 4 ? c1 : c2, y = top + (i < 4 ? i : i - 4) * dy;
            key(new Key(progKeys[i], "-" + juce::String(31 + i) + "-"), x, y, [this, i] { direct(i + 1); }, [this, i] { return !userMode && shownProg() == i + 1 && !isUserSlot(); });
        }
        key(new Key("OTHERS", "-37-"), c2, top + 2 * dy, [this] { if (userMode) recall(37); else { edit = {}; entry.clear(); lcdMsg("PROGRAM No.?", 1.5); } });
        key(new Key("USER\nMEMORY"), c2, top + 3 * dy, [this] { userMode = !userMode; }, [this] { return userMode; });
        key(new Key("REV\nTIME"), c3, top, [this] { pickKey(0); }, [this] { return edit == cur(0) && edit.isNotEmpty(); });
        key(new Key("INITIAL\nDELAY"), c3, top + dy, [this] { pick("idly"); }, [this] { return edit == "idly"; });
        key(new Key("1ST\nREF"), c3, top + 2 * dy, [this] { pick(edit == "fdly" ? "flev" : "fdly"); }, [this] { return edit == "fdly" || edit == "flev"; });
        for (int f = 1; f <= 3; ++f) key(new Key("F" + juce::String(f)), c4, top + (f - 1) * dy, [this, f] { pickKey(f); }, [this, f] { return edit.isNotEmpty() && edit == cur(f); });
        auto* minus = key(new Key("-"), c3, top + 3 * dy, [this] { step(-1); }); minus->setRepeatSpeed(380, 60);
        auto* plus = key(new Key("+"), c4, top + 3 * dy, [this] { step(1); }); plus->setRepeatSpeed(380, 60);
        const char* padKeys[12] = {"7", "8", "9", "4", "5", "6", "1", "2", "3", "0", ".", "CLEAR"};
        for (int i = 0; i < 12; ++i) {
            const juce::String t = padKeys[i];
            key(new Key(t, {}, true), pad + (i % 3) * 46, top + (i / 3) * dy, [this, t] { type(t); });
        }
        key(new Key("MEMORY"), mem, top, [this] { edit = {}; entry.clear(); });
        key(new Key("STORE"), mem, top + dy, [this] { store(); });
        key(new Key("RECALL"), mem, top + 2 * dy, [this] { enter(true); });
        key(new Key("ENTER", {}, true), mem, top + 3 * dy, [this] { enter(false); });
        auto tog = [this](const char* id) { if (auto* q = st.getParameter(id)) q->setValueNotifyingHost(q->getValue() >= .5f ? 0.f : 1.f); };
        auto on = [this](const char* id) { auto* q = st.getRawParameterValue(id); return q && q->load() >= .5f; };
        key(new Key("MUTE"), out, top, [tog] { tog("mute"); }, [on] { return on("mute"); });
        key(new Key("OUT\nPHASE"), out, top + dy, [tog] { tog("phase"); }, [on] { return on("phase"); });
        key(new Key("MIDI\nCTRL"), out, top + 2 * dy, [this] { lcdMsg("MIDI: HOST PROG CHG", 2.5); });
        key(new Key("BYPASS"), out, top + 3 * dy, [tog] { tog("bypass"); }, [on] { return on("bypass"); });
        for (auto* k : keys) k->setWantsKeyboardFocus(false);
        setWantsKeyboardFocus(true);
        startTimerHz(30);
    }
    ~Panel() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override {
        // rack face with ears
        g.fillAll(juce::Colour(0xff0d0d0f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff232428), 0, 0, juce::Colour(0xff141518), 0, (float) H, false));
        g.fillRect(0, 0, W, H);
        for (int y = 0; y < H; y += 2) { g.setColour(juce::Colour(0x06ffffff)); g.drawHorizontalLine(y, 30, W - 30); }   // brushed
        for (int x : {14, W - 14}) for (int y : {28, H - 28}) { g.setColour(juce::Colour(0xffd8d8d8)); g.fillEllipse(x - 7.f, y - 7.f, 14, 14); g.setColour(juce::Colour(0xff0b0b0c)); g.fillEllipse(x - 4.5f, y - 4.5f, 9, 9); }
        // display window
        const juce::Rectangle<float> win(46, 18, 540, 98);
        g.setColour(kGlass); g.fillRoundedRectangle(win, 3); g.setColour(juce::Colour(0xff2c2d31)); g.drawRoundedRectangle(win, 3, 1.2f);
        g.setColour(kText); g.setFont(juce::FontOptions(20.f, juce::Font::bold)); g.drawText("OMEGA", 58, 34, 120, 24, juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(9.f)); g.setColour(kDim); g.drawText("DIGITAL REVERBERATOR", 58, 60, 140, 12, juce::Justification::centredLeft);
        g.drawText("REV7 MODEL", 58, 72, 140, 12, juce::Justification::centredLeft);
        // input meter
        static const char* ml[8] = {"CLIP", "0", "-3", "-6", "-10", "-15", "-20", "-30"}; static const float th[8] = {-.3f, -1.5f, -3, -6, -10, -15, -20, -30};
        const float db = 20 * std::log10(std::max(1e-6f, lvl));
        for (int i = 0; i < 8; ++i) {
            const bool litLed = db >= th[i]; const float y = 26 + i * 10.5f;
            g.setColour((i == 0 ? kRed : kGreen).withAlpha(litLed ? 1.f : .13f)); g.fillRect(212.f, y, 12.f, 6.5f);
            g.setColour(kDim); g.setFont(juce::FontOptions(7.f)); g.drawText(ml[i], 226, (int) y - 2, 22, 10, juce::Justification::centredLeft);
        }
        // red MEMORY digits
        juce::String num = entry.isNotEmpty() && edit.isEmpty() ? entry : juce::String(isUserSlot() ? userSlot : shownProg());
        if (entry.isNotEmpty() && edit.isEmpty() && blink) num = {};
        g.setColour(juce::Colour(0xff160303)); g.fillRect(262, 34, 56, 44);
        const juce::String two = num.length() >= 2 ? num.getLastCharacters(2) : num.paddedLeft(' ', 2);
        for (int i = 0; i < 2; ++i) sevenSeg(g, two[i], 267.f + i * 25, 38, 20, 36);
        g.setColour(kDim); g.setFont(juce::FontOptions(8.f)); g.drawText("MEMORY", 262, 82, 56, 10, juce::Justification::centred);
        // LCD
        g.setColour(kLcdBg); g.fillRect(lcdArea); g.setColour(juce::Colour(0xff0a0c09)); g.drawRect(lcdArea, 2.f);
        g.setColour(kLcdOn); g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 15.f, juce::Font::plain));
        g.drawText(line1(), lcdArea.reduced(10, 6).removeFromTop(26), juce::Justification::centredLeft);
        g.drawText(line2(), lcdArea.reduced(10, 6).removeFromBottom(26), juce::Justification::centredLeft);
        // labels under the display
        g.setColour(kDim); g.setFont(juce::FontOptions(8.f, juce::Font::bold));
        auto lab = [&](const juce::String& t, int x, int y, int w = 60) { g.drawFittedText(t, x - w / 2, y, w, 18, juce::Justification::centred, 2); };
        lab("POWER", 62, 128); lab("VINTAGE", 96, 132); lab("MONO\nSTEREO", 140, 128); lab("EQ\nON/OFF", 226, 128);
        lab("LO", 290, 128); lab("MID", 378, 128); lab("HI", 466, 128);
        g.setColour(kDim.withAlpha(.6f));
        for (int xc : {290, 378, 466}) { g.drawHorizontalLine(137, (float) xc - 34, (float) xc - 12); g.drawHorizontalLine(137, (float) xc + 12, (float) xc + 34); }
        g.setColour(kDim);
        for (const auto& l : labels) {
            g.setFont(juce::FontOptions(7.5f, juce::Font::bold)); g.drawFittedText(l.top, l.x - 28, 140, 56, 20, juce::Justification::centredBottom, 2);
            g.setFont(juce::FontOptions(7.f)); g.drawText(l.lo, l.x - 34, 194, 30, 10, juce::Justification::centredRight); g.drawText(l.hi, l.x + 4, 194, 30, 10, juce::Justification::centredLeft);
        }
        // power switch (lit)
        g.setColour(juce::Colour(0xff050506)); g.fillRect(48, 152, 28, 28); g.setColour(juce::Colour(0xff2d2e32)); g.fillRect(50, 154, 24, 24);
        g.setColour(kDim); g.drawText("ON/OFF", 40, 184, 44, 10, juce::Justification::centred);
        // edit key "ON" lamp label, as printed above F1
        g.setColour(kRed.withAlpha(.8f)); g.setFont(juce::FontOptions(7.f, juce::Font::bold)); g.drawText("ON", 754, 12, 42, 9, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override { if (lcdArea.contains(e.position)) presetMenu(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (lcdArea.contains(e.position) && edit.isNotEmpty()) step(w.deltaY > 0 ? 1 : -1); }
    bool keyPressed(const juce::KeyPress& k) override {
        const auto c = k.getTextCharacter();
        if ((c >= '0' && c <= '9') || c == '.') { type(juce::String::charToString(c)); return true; }
        if (k == juce::KeyPress::returnKey) { enter(false); return true; }
        if (k == juce::KeyPress::backspaceKey || k == juce::KeyPress::escapeKey) { type("CLEAR"); return true; }
        if (c == '+' || c == '=') { step(1); return true; } if (c == '-') { step(-1); return true; }
        return false;
    }

private:
    struct Lab { int x; juce::String top, lo, hi, mid; };
    double val(const juce::String& id) const {
        auto* q = st.getParameter(id); if (!q) return 0;
        const int i = proc.spec().index(id.toStdString()); const auto& d = proc.spec().params[(size_t) i];
        const double raw = q->convertFrom0to1(q->getValue());
        return d.choices.empty() ? raw : d.choices[(size_t) juce::jlimit(0, (int) d.choices.size() - 1, (int) std::lround(raw))].value;
    }
    void setVal(const juce::String& id, double v) {
        auto* q = st.getParameter(id); if (!q) return;
        const auto& d = proc.spec().params[(size_t) proc.spec().index(id.toStdString())];
        double raw = v;
        if (!d.choices.empty()) { raw = d.defChoice; for (size_t c = 0; c < d.choices.size(); ++c) if (std::fabs(d.choices[c].value - v) < 1e-6) raw = (double) c; }
        else raw = juce::jlimit((double) d.min, (double) d.max, v);
        q->beginChangeGesture(); q->setValueNotifyingHost(q->convertTo0to1((float) raw)); q->endChangeGesture();
    }
    int shownProg() const { return (int) std::lround(val("prog")); }
    K kind() const { return dsp::Rev7::prog(shownProg()).kind; }
    juce::String cur(int key) const { return keysFor(kind())[key]; }
    bool isUserSlot() const { return userSlot >= 31 && userSlotProg == shownProg() && userDirty == false; }

    void lcdMsg(const juce::String& m, double secs) { msg = m; msgUntil = juce::Time::getMillisecondCounterHiRes() + secs * 1000; }
    juce::String line1() const {
        if (msgUntil > juce::Time::getMillisecondCounterHiRes()) return msg;
        if (edit.isEmpty() && entry.isNotEmpty()) return "PROGRAM No. " + entry;
        return name();
    }
    juce::String line2() const {
        if (edit.isNotEmpty()) {
            if (entry.isNotEmpty()) return lcdValue(edit, 0, kind()).upToFirstOccurrenceOf("=", true, false).upToFirstOccurrenceOf(":", true, false) + " " + entry + (blink ? "_" : " ");
            return lcdValue(edit, val(edit), kind());
        }
        const juce::String k0 = cur(0).isNotEmpty() ? cur(0) : cur(1);
        return lcdValue(k0, val(k0), kind());
    }
    juce::String name() const {
        if (isUserSlot()) return "USER " + juce::String(userSlot) + " " + rev7Name(shownProg());
        const int pi = proc.getCurrentProgram(); const auto& ps = proc.spec().presets;
        if (pi >= 30 && pi < (int) ps.size()) {   // an artist preset, while its program is still selected
            for (const auto& [id, x] : ps[(size_t) pi].values) if (id == "prog" && (int) x == shownProg()) { juce::String nm(ps[(size_t) pi].name); if (nm.contains(" / ")) nm = nm.fromFirstOccurrenceOf(" / ", false, false); return nm.toUpperCase().substring(0, 20); }
        }
        return rev7Name(shownProg());
    }

    void direct(int n) { if (userMode) recall(30 + n); else recall(n); }
    void recall(int n) {
        edit = {}; entry.clear();
        if (n >= 1 && n <= 30) { proc.setCurrentProgram(n - 1); userSlot = 0; return; }
        if (n >= 31 && n <= 90) {
            auto mems = st.state.getChildWithName("REV7USER"); auto m = mems.getChildWithName("M" + juce::String(n));
            if (!m.isValid()) { lcdMsg("No." + juce::String(n) + " EMPTY", 1.5); return; }
            for (const auto& d : proc.spec().params) if (m.hasProperty(juce::Identifier(d.id))) setVal(d.id, (double) m[juce::Identifier(d.id)]);
            userSlot = n; userSlotProg = shownProg(); userDirty = false; return;
        }
        lcdMsg("No." + juce::String(n) + " ?", 1.2);
    }
    void store() {
        const int n = entry.getIntValue();
        if (edit.isNotEmpty() || n < 31 || n > 90) { lcdMsg("STORE: TYPE 31-90", 1.8); entry.clear(); return; }
        auto mems = st.state.getOrCreateChildWithName("REV7USER", nullptr);
        auto m = mems.getOrCreateChildWithName("M" + juce::String(n), nullptr);
        for (const auto& d : proc.spec().params) if (d.id != "mute" && d.id != "bypass") m.setProperty(juce::Identifier(d.id), val(d.id), nullptr);
        userSlot = n; userSlotProg = shownProg(); userDirty = false; entry.clear();
        lcdMsg("STORED No." + juce::String(n), 1.5);
    }
    void pickKey(int k) { const juce::String id = cur(k); if (id.isEmpty()) { lcdMsg("---- NO PARAMETER", 1.2); return; } pick(id); }
    void pick(const juce::String& id) { entry.clear(); edit = (edit == id && id != "fdly" && id != "flev") ? juce::String() : id; }
    void step(int dir) {
        if (edit.isEmpty()) { // step through the programs
            const int n = juce::jlimit(1, 30, shownProg() + dir); if (n != shownProg()) recall(n); return; }
        const auto& d = proc.spec().params[(size_t) proc.spec().index(edit.toStdString())];
        setVal(edit, val(edit) + dir * (d.choices.empty() ? d.step : 1.0)); userDirty = true;
    }
    void type(const juce::String& t) {
        if (t == "CLEAR") { entry.clear(); return; }
        if (t == "." && (edit.isEmpty() || entry.contains("."))) return;
        if (entry.length() < (edit.isEmpty() ? 2 : 5)) entry += t;
    }
    void enter(bool recallKey) {
        if (edit.isNotEmpty() && !recallKey) {
            if (entry.isNotEmpty()) { double x = entry.getDoubleValue(); if (isPercent(edit)) x /= 100; setVal(edit, x); userDirty = true; }
            entry.clear(); return;
        }
        if (entry.isNotEmpty()) recall(entry.getIntValue());
    }
    void presetMenu() {
        juce::PopupMenu m, fac, usr; const auto& ps = proc.spec().presets;
        std::vector<std::pair<juce::String, juce::PopupMenu>> cats;   // one submenu per "Category / Name" group, in order
        for (int i = 0; i < (int) ps.size(); ++i) {
            const juce::String full = ps[(size_t) i].name; const bool sel = i == proc.getCurrentProgram();
            if (i < 30) { fac.addItem(i + 1, full, true, sel); continue; }
            const juce::String cat = full.upToFirstOccurrenceOf(" / ", false, false), nm = full.fromFirstOccurrenceOf(" / ", false, false);
            auto it = std::find_if(cats.begin(), cats.end(), [&](const auto& c) { return c.first == cat; });
            if (it == cats.end()) { cats.push_back({cat, {}}); it = cats.end() - 1; }
            it->second.addItem(i + 1, nm.isEmpty() ? full : nm, true, sel);
        }
        auto mems = st.state.getChildWithName("REV7USER");
        for (int n = 31; n <= 90; ++n) if (mems.getChildWithName("M" + juce::String(n)).isValid()) usr.addItem(1000 + n, "USER " + juce::String(n));
        if (usr.getNumItems() == 0) usr.addItem(-1, "(type 31-90, then STORE)", false);
        m.addSubMenu("Factory 1-30", fac); for (auto& [c, sub] : cats) m.addSubMenu(c, sub); m.addSubMenu("User memory 31-90", usr);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(lcdArea.toNearestInt())),
                        [this](int r) { if (r >= 1000) recall(r - 1000); else if (r > 0) { edit = {}; entry.clear(); proc.setCurrentProgram(r - 1); userSlot = 0; } });
    }
    static void sevenSeg(juce::Graphics& g, juce::juce_wchar ch, float x, float y, float w, float h) {
        static const uint8_t seg[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
        const uint8_t s = ch >= '0' && ch <= '9' ? seg[ch - '0'] : ch == '-' ? 0x40 : 0;
        const float t = 3.2f, hh = h / 2;
        const juce::Rectangle<float> r[7] = {{x + t, y, w - 2 * t, t}, {x + w - t, y + t, t, hh - t}, {x + w - t, y + hh, t, hh - t}, {x + t, y + h - t, w - 2 * t, t},
                                             {x, y + hh, t, hh - t}, {x, y + t, t, hh - t}, {x + t, y + hh - t / 2, w - 2 * t, t}};
        for (int i = 0; i < 7; ++i) { g.setColour(s >> i & 1 ? kRed : juce::Colour(0xff2a0806)); g.fillRoundedRectangle(r[i].translated(-(r[i].getY() - y) * .08f + 2, 0), 1.f); }
    }
    void timerCallback() override {
        lvl = proc.engineLevel(); blink = (juce::Time::getMillisecondCounter() / 350) % 2 == 0;
        // leave a user slot shown only until something is changed by hand
        if (userSlot >= 31 && userSlotProg != shownProg()) userSlot = 0;
        repaint();
    }

    Processor& proc;
    juce::AudioProcessorValueTreeState& st;
    PanelLook look;
    juce::OwnedArray<juce::Slider> knobs; juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> kat;
    juce::OwnedArray<Switch> switches; juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bat;
    juce::OwnedArray<Key> keys;
    std::vector<Lab> labels;
    const juce::Rectangle<float> lcdArea { 330, 34, 244, 62 };
    juce::String edit, entry, msg; double msgUntil = 0;
    bool userMode = false, blink = false, userDirty = false; int userSlot = 0, userSlotProg = 0; float lvl = 0;
};

class Rev7Editor : public juce::AudioProcessorEditor {
public:
    explicit Rev7Editor(Processor& p) : AudioProcessorEditor(p), panel(p) {
        addAndMakeVisible(panel); panel.setBounds(0, 0, W, H);
        setResizable(true, true); setResizeLimits(W / 2, H / 2, W * 2, H * 2);
        if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / H);
        setSize(W, H);
    }
    void resized() override { panel.setTransform(juce::AffineTransform::scale((float) getWidth() / W)); }
private:
    Panel panel;
};

}  // namespace

juce::AudioProcessorEditor* makeCustomEditor(Processor& p) { return new Rev7Editor(p); }

}  // namespace omega
