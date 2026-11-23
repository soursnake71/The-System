// Omega Wheel of Steel editor. Everything is drawn with JUCE graphics (no image assets) in a dark Serato / Technics /
// Rane look. Components:
//   WaveView / Overview / InfoRow   the stacked scrolling 3-band waveforms (deck A above B), track info, overviews
//   Turntable (Platter, Tonearm)   an SL-1200: strobe platter + record (the scratch surface), start/stop, 33/45, pitch
//   MixerPanel                     trim, 3-band kill EQ, filter, PFL, channel faders, LED meters, crossfader
//   PadSection                     pad modes, 8 performance pads (MPC pad indices), loop controls, beat FX unit
//   LibraryPanel                   SD card / folder chooser, crates, track table, search; background listing
//   MidiPanel / LearnOverlay       MIDI map, MIDI learn, MPC help
// A 60 Hz timer repaints only the moving parts (waveforms, platters, meters); everything else polls its lit state.
#include "WheelEditor.h"
#include "WheelFonts.h"
#include "PhoneUI.h"
#include <cmath>

namespace wheel {
namespace {

using namespace juce;

// ------------------------------------------------------------------------------------------------ look
namespace pal {
const Colour bg      { 0xff0a0b0d };
const Colour panel   { 0xff141619 };
const Colour panel2  { 0xff1b1e22 };
const Colour raised  { 0xff24282d };
const Colour well    { 0xff07080a };
const Colour edge    { 0xff2b3036 };
const Colour edgeHi  { 0xff3a4048 };
const Colour text    { 0xffe8eaed };
const Colour dim     { 0xff9098a2 };
const Colour faint   { 0xff545b64 };
const Colour amber   { 0xffffa21f };
const Colour red     { 0xffff3b30 };
const Colour green   { 0xff34d058 };
const Colour cyan    { 0xff2fd4ff };
const Colour violet  { 0xffb06cff };
const Colour deck[2] { Colour(0xff2b9bff), Colour(0xffff7a29) };
}

String U(const char* utf8) { return String::fromUTF8(utf8); }
Font font(float h, bool bold = false) { return Font(FontOptions(h, bold ? Font::bold : Font::plain)); }
Font mono(float h, bool bold = false) { return Font(FontOptions(Font::getDefaultMonospacedFontName(), h, bold ? Font::bold : Font::plain)); }

template <class V> auto& at(V& v, int i) { return v[(size_t) i]; }

String fmtTime(double s, bool tenths)
{
    bool neg = s < 0; s = std::abs(s);
    int m = (int) (s / 60); double r = s - m * 60;
    String out = (neg ? "-" : "") + String(m).paddedLeft('0', 2) + ":" + String((int) r).paddedLeft('0', 2);
    if (tenths) out << "." << String((int) ((r - std::floor(r)) * 10));
    return out;
}

Colour cueColour(const HotCue& c) { return Colour((uint32) (0xff000000u | (c.rgb & 0xffffffu))); }

// Camelot wheel colours (Mixed In Key style: hue walks the wheel, A = minor deeper, B = major lighter)
Colour keyColour(int key)
{
    if (key < 0) return pal::faint;
    int n = String(camelot(key)).getIntValue();
    if (n < 1) n = 1;
    float hue = std::fmod(0.40f + (float) (n - 1) / 12.0f, 1.0f);
    return key >= 12 ? Colour::fromHSV(hue, .78f, .95f, 1) : Colour::fromHSV(hue, .55f, 1.0f, 1);
}

void glow(Graphics& g, Rectangle<float> r, Colour c, float radius)
{
    for (int i = 3; i >= 1; --i)
    {
        g.setColour(c.withAlpha(.07f * (float) (4 - i)));
        g.fillRoundedRectangle(r.expanded((float) i * radius / 3), radius + (float) i);
    }
}

// a shared dark look for the stock widgets (lists, table header, text boxes, combos, popup menus, scrollbars)
struct WheelLook : LookAndFeel_V4
{
    WheelLook()
    {
        setColourScheme({ pal::panel, pal::panel2, pal::panel2, pal::faint, pal::text, pal::raised, pal::text, pal::raised, pal::text });
        setColour(ListBox::backgroundColourId, pal::well);
        setColour(ListBox::outlineColourId, pal::edge);
        setColour(TextEditor::backgroundColourId, pal::well);
        setColour(TextEditor::outlineColourId, pal::edge);
        setColour(TextEditor::focusedOutlineColourId, pal::deck[0]);
        setColour(TextEditor::textColourId, pal::text);
        setColour(TextEditor::highlightColourId, pal::deck[0].withAlpha(.35f));
        setColour(CaretComponent::caretColourId, pal::deck[0]);
        setColour(ComboBox::backgroundColourId, pal::well);
        setColour(ComboBox::outlineColourId, pal::edge);
        setColour(ComboBox::textColourId, pal::text);
        setColour(ComboBox::arrowColourId, pal::dim);
        setColour(PopupMenu::backgroundColourId, pal::panel2);
        setColour(PopupMenu::highlightedBackgroundColourId, pal::deck[0].withAlpha(.4f));
        setColour(TableHeaderComponent::backgroundColourId, pal::panel2);
        setColour(TableHeaderComponent::textColourId, pal::dim);
        setColour(TableHeaderComponent::outlineColourId, pal::edge);
        setColour(TableHeaderComponent::highlightColourId, pal::raised);
        setColour(ScrollBar::thumbColourId, pal::edgeHi);
        setColour(TextButton::buttonColourId, pal::raised);
        setColour(TextButton::textColourOffId, pal::text);
        setColour(Label::textColourId, pal::text);
    }
    Font getComboBoxFont(ComboBox& b) override { return font(jmin(13.0f, (float) b.getHeight() * .62f), true); }
    Font getPopupMenuFont() override { return font(14); }
    void drawComboBox(Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box) override
    {
        auto r = Rectangle<float>(0, 0, (float) w, (float) h).reduced(.5f);
        g.setColour(pal::well); g.fillRoundedRectangle(r, 3);
        g.setColour(box.hasKeyboardFocus(true) ? pal::edgeHi : pal::edge); g.drawRoundedRectangle(r, 3, 1);
        Path p; float cx = (float) w - 9, cy = (float) h * .5f;
        p.addTriangle(cx - 3.5f, cy - 2, cx + 3.5f, cy - 2, cx, cy + 2.5f);
        g.setColour(pal::dim); g.fillPath(p);
    }
    void positionComboBoxText(ComboBox& box, Label& l) override
    {
        l.setBounds(2, 0, box.getWidth() - 16, box.getHeight());
        l.setFont(getComboBoxFont(box));
        l.setJustificationType(Justification::centredLeft);
    }
    void drawTableHeaderColumn(Graphics& g, TableHeaderComponent&, const String& name, int, int w, int h, bool over, bool, int flags) override
    {
        if (over) { g.setColour(pal::raised); g.fillRect(0, 0, w, h); }
        g.setColour(pal::edge); g.fillRect(w - 1, 3, 1, h - 6);
        g.setColour(pal::dim); g.setFont(font(11.5f, true));
        String n = name;
        if ((flags & TableHeaderComponent::sortedForwards) != 0) n << U("  \xe2\x96\xb4");
        if ((flags & TableHeaderComponent::sortedBackwards) != 0) n << U("  \xe2\x96\xbe");
        g.drawText(n.toUpperCase(), 6, 0, w - 10, h, Justification::centredLeft, true);
    }
    void drawScrollbar(Graphics& g, ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbPos, int thumbSize, bool over, bool) override
    {
        g.setColour(pal::well); g.fillRect(x, y, w, h);
        auto t = vertical ? Rectangle<int>(x + 2, thumbPos, w - 4, thumbSize) : Rectangle<int>(thumbPos, y + 2, thumbSize, h - 4);
        g.setColour(over ? pal::faint : pal::edgeHi); g.fillRoundedRectangle(t.toFloat(), 3);
    }
    int getDefaultScrollbarWidth() override { return 10; }
    void drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(.5f);
        ColourGradient gr(pal::raised.brighter(over ? .15f : .05f), 0, r.getY(), pal::panel2, 0, r.getBottom(), false);
        g.setGradientFill(gr); g.fillRoundedRectangle(r, 3);
        if (down) { g.setColour(Colours::white.withAlpha(.06f)); g.fillRoundedRectangle(r, 3); }
        g.setColour(pal::edgeHi); g.drawRoundedRectangle(r, 3, 1);
    }
    Font getTextButtonFont(TextButton&, int h) override { return font(jmin(13.0f, (float) h * .55f), true); }
};

// ------------------------------------------------------------------------------------------------ shared context
struct WaveCache
{
    const Track* key = nullptr;
    std::vector<uint32> argb, core;   // column colours: outer body and inner core
    std::vector<uint8> amp;           // 0..255 column height
    int n = 0;
    void build(const Track* t)
    {
        key = t; n = 0; argb.clear(); core.clear(); amp.clear();
        if (t == nullptr) return;
        const auto& w = t->wave;
        n = (int) jmin(w.low.size(), w.mid.size(), w.high.size());
        argb.resize((size_t) n); core.resize((size_t) n); amp.resize((size_t) n);
        for (size_t i = 0; i < (size_t) n; ++i)
        {
            float l = w.low[i] / 255.0f, m = w.mid[i] / 255.0f, h = w.high[i] / 255.0f;
            float pk = i < w.peak.size() ? w.peak[i] / 255.0f : jmax(l, m, h);
            // Serato 3-band: red lows, green mids, blue highs, hue from the band balance, brightness from the level
            float r = l, gg = m * .92f, b = h * 1.1f, mx = jmax(r, gg, b, 1.0e-3f);
            auto c = Colour::fromFloatRGBA(r / mx, gg / mx, jmin(1.0f, b / mx), 1.0f);
            c = c.withMultipliedBrightness(.42f + .58f * std::sqrt(pk));
            argb[i] = c.getARGB();
            core[i] = c.interpolatedWith(Colours::white, .35f + .3f * h).getARGB();
            amp[i] = (uint8) jlimit(0, 255, (int) (std::pow(pk, .8f) * 255.0f + .5f));
        }
    }
};

struct PButton;
struct Ctx
{
    explicit Ctx(WheelProcessor& p) : proc(p) {}
    WheelProcessor& proc;
    float S = 1;                               // UI scale (1 = 1400 x 900)
    float zoom = 8;                            // seconds visible in the scrolling waveforms (both decks)
    std::shared_ptr<const Track> trk[2];
    std::array<HotCue, kCues> cues[2];
    WaveCache wave[2];
    bool learnMode = false;
    std::vector<std::pair<Component::SafePointer<Component>, String>> learnables;
    std::vector<PButton*> buttons;             // polled for lit state
    std::function<File()> selectedFile;        // the library selection
    std::function<void()> learnChanged;

    RangedAudioParameter* par(const String& id) const { return proc.apvts.getParameter(id); }
    RangedAudioParameter* dp(int d, const char* id) const { return par(deckParam(d, id)); }
    static bool on(RangedAudioParameter* p) { return p != nullptr && p->getValue() >= .5f; }
    static float val(RangedAudioParameter* p) { return p != nullptr ? p->convertFrom0to1(p->getValue()) : 0.0f; }
    static void setNorm(RangedAudioParameter* p, float v)
    {
        if (p == nullptr) return;
        p->beginChangeGesture(); p->setValueNotifyingHost(jlimit(0.0f, 1.0f, v)); p->endChangeGesture();
    }
    static void setVal(RangedAudioParameter* p, float v) { if (p != nullptr) setNorm(p, p->convertTo0to1(v)); }
    static void toggle(RangedAudioParameter* p) { setNorm(p, on(p) ? 0.0f : 1.0f); }
    void send(Cmd::Type t, int d, double v = 0, bool o = false, int i = 0)
    {
        Cmd c; c.type = t; c.deck = d; c.v = v; c.on = o; c.i = i;
        proc.send(c);
    }
    void reg(Component& c, const String& pid) { learnables.push_back({ Component::SafePointer<Component>(&c), pid }); }
    bool rpm45(int d) const { return on(dp(d, "rpm45")); }
    // track seconds of record under one turn of the platter: 1.8 s (33 1/3 rpm). At 45 the same record just spins
    // faster, so a turn of the hand is still 1.8 s of it (the engine's DeckStatus::angle uses the same constant).
    static double secPerRev() { return 1.8; }
    double rangePct(int d) const
    {
        auto* p = dp(d, "range");
        if (p == nullptr) return 8;
        double v = p->getCurrentValueAsText().retainCharacters("0123456789.").getDoubleValue();
        if (v <= 0) v = val(p);
        if (v > 0 && v <= 1) v *= 100;
        if (v <= 0) v = 8;
        return v;
    }
    double pitchPct(int d) const
    {
        auto* p = dp(d, "pitch");
        if (p == nullptr) return 0;
        auto r = p->getNormalisableRange();
        double v = val(p);
        double span = jmax(std::abs(r.start), std::abs(r.end));
        return span > 0 ? v / span * rangePct(d) : 0;
    }
};

// ------------------------------------------------------------------------------------------------ controls
// a hardware-ish button: momentary or toggle via lambdas; lit state is polled
struct PButton : Component
{
    PButton(Ctx& c, String t, Colour lc, String pid = {}) : ctx(c), text(std::move(t)), litCol(lc)
    {
        ctx.buttons.push_back(this);
        if (pid.isNotEmpty()) ctx.reg(*this, pid);
        setRepaintsOnMouseActivity(true);
    }
    ~PButton() override { auto& v = ctx.buttons; v.erase(std::remove(v.begin(), v.end(), this), v.end()); }
    Ctx& ctx;
    String text, sub;
    Colour litCol;
    bool solid = false, round = false;
    float fontScale = 1;
    std::function<bool()> lit;
    std::function<void(bool)> press;
    std::function<void()> rightClick;
    std::function<String()> dynText;
    bool down = false, wasLit = false;
    void poll()
    {
        bool l = lit && lit();
        String t = dynText ? dynText() : text;
        if (l != wasLit || t != text) { wasLit = l; text = t; repaint(); }
    }
    void mouseDown(const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && rightClick) { rightClick(); return; }
        down = true; repaint();
        if (press) press(true);
    }
    void mouseUp(const MouseEvent&) override
    {
        if (!down) return;
        down = false; repaint();
        if (press) press(false);
    }
    void paint(Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.5f);
        float rad = round ? r.getHeight() * .5f : jmin(4.0f, r.getHeight() * .2f);
        bool l = wasLit;
        if (l && solid) glow(g, r, litCol, 4);
        ColourGradient gr(pal::raised.brighter(isMouseOver() ? .12f : 0), 0, r.getY(), pal::panel2.darker(.2f), 0, r.getBottom(), false);
        if (l && solid) gr = ColourGradient(litCol.brighter(.25f), 0, r.getY(), litCol.darker(.25f), 0, r.getBottom(), false);
        g.setGradientFill(gr); g.fillRoundedRectangle(r, rad);
        if (down) { g.setColour(Colours::black.withAlpha(.25f)); g.fillRoundedRectangle(r, rad); }
        g.setColour(l ? litCol.withAlpha(solid ? .9f : .85f) : Colours::black.withAlpha(.7f));
        g.drawRoundedRectangle(r, rad, l && !solid ? 1.4f : 1.0f);
        if (!l) { g.setColour(Colours::white.withAlpha(.07f)); g.drawLine(r.getX() + rad, r.getY() + 1, r.getRight() - rad, r.getY() + 1, 1); }
        float fh = jmin(13.0f * ctx.S, r.getHeight() * .52f) * fontScale;
        g.setFont(font(fh, true));
        g.setColour(l ? (solid ? Colours::black.withAlpha(.85f) : litCol.brighter(.3f)) : pal::dim.brighter(isMouseOver() ? .3f : 0));
        if (sub.isNotEmpty())
        {
            g.drawText(text, r.withTrimmedBottom(r.getHeight() * .42f), Justification::centredBottom, false);
            g.setFont(font(fh * .72f)); g.setColour(pal::faint.brighter(.2f));
            g.drawText(sub, r.withTrimmedTop(r.getHeight() * .6f), Justification::centredTop, false);
        }
        else g.drawFittedText(text, r.reduced(2, 0).toNearestInt(), Justification::centred, 2, .8f);
    }
};

// a Rane-style rotary knob attached to a parameter
struct Knob : Component
{
    Knob(Ctx& c, RangedAudioParameter* p, String cap, Colour acc, bool bipolar = false) : ctx(c), param(p), caption(std::move(cap)), accent(acc), bip(bipolar)
    {
        if (param != nullptr)
        {
            att = std::make_unique<ParameterAttachment>(*param, [this](float v) { value = param->convertTo0to1(v); repaint(); });
            att->sendInitialUpdate();
            ctx.reg(*this, param->getParameterID());
        }
        setRepaintsOnMouseActivity(true);
    }
    Ctx& ctx;
    RangedAudioParameter* param;
    String caption;
    Colour accent;
    bool bip;
    bool showKill = false;
    float value = 0, dragStart = 0;
    std::unique_ptr<ParameterAttachment> att;
    void set(float v, bool gesture)
    {
        if (att == nullptr) return;
        v = jlimit(0.0f, 1.0f, v);
        if (gesture) att->setValueAsPartOfGesture(param->convertFrom0to1(v));
        else att->setValueAsCompleteGesture(param->convertFrom0to1(v));
    }
    void mouseDown(const MouseEvent&) override { if (att) { att->beginGesture(); dragStart = value; } }
    void mouseDrag(const MouseEvent& e) override
    {
        float sens = e.mods.isShiftDown() ? 800.0f : 200.0f;
        set(dragStart + (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / (sens * ctx.S), true);
    }
    void mouseUp(const MouseEvent&) override { if (att) att->endGesture(); }
    void mouseDoubleClick(const MouseEvent&) override { if (param) set(param->getDefaultValue(), false); }
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails& w) override { set(value + w.deltaY * .08f, false); }
    void paint(Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        float capH = caption.isEmpty() ? 0 : jmin(13.0f * ctx.S, b.getHeight() * .24f);
        auto k = b.withTrimmedBottom(capH);
        float d = jmin(k.getWidth(), k.getHeight()) - 2, r = d * .5f;
        float cx = k.getCentreX(), cy = k.getCentreY();
        const float a0 = -2.4f, a1 = 2.4f;
        float av = a0 + value * (a1 - a0);
        float def = param != nullptr ? param->getDefaultValue() : 0;
        float aorg = bip ? a0 + def * (a1 - a0) : a0;
        // the panel ring: track and value arc
        Path track; track.addCentredArc(cx, cy, r - 1.5f, r - 1.5f, 0, a0, a1, true);
        g.setColour(Colours::black.withAlpha(.8f)); g.strokePath(track, PathStrokeType(3.2f * ctx.S));
        Path arc; arc.addCentredArc(cx, cy, r - 1.5f, r - 1.5f, 0, jmin(aorg, av), jmax(aorg, av), true);
        bool atDef = std::abs(value - def) < .004f;
        g.setColour(atDef && bip ? accent.withAlpha(.5f) : accent); g.strokePath(arc, PathStrokeType(2.4f * ctx.S, PathStrokeType::curved, PathStrokeType::rounded));
        // ticks at the ends and the centre
        g.setColour(pal::faint);
        for (float a : { a0, a1, bip ? aorg : 99.0f })
            if (a < 9) { auto p1 = Point<float>(cx, cy).getPointOnCircumference(r + 1, a), p2 = Point<float>(cx, cy).getPointOnCircumference(r + 3.5f * ctx.S, a); g.drawLine({ p1, p2 }, 1.2f); }
        // the knob: knurled black skirt, satin cap
        float kr = r * .74f;
        g.setColour(Colours::black.withAlpha(.6f)); g.fillEllipse(cx - kr + 1, cy - kr + 2, kr * 2, kr * 2);
        ColourGradient sk(Colour(0xff3a3e44), cx, cy - kr, Colour(0xff111215), cx, cy + kr, false);
        g.setGradientFill(sk); g.fillEllipse(cx - kr, cy - kr, kr * 2, kr * 2);
        g.setColour(Colours::white.withAlpha(.05f));
        for (int i = 0; i < 24; ++i)
        {
            float a = (float) i / 24 * MathConstants<float>::twoPi;
            g.drawLine({ Point<float>(cx, cy).getPointOnCircumference(kr * .82f, a), Point<float>(cx, cy).getPointOnCircumference(kr * .98f, a) }, 1);
        }
        float cr = kr * .72f;
        ColourGradient cap(Colour(0xff4a4f57), cx - cr * .4f, cy - cr, Colour(0xff1a1c20), cx + cr * .3f, cy + cr, true);
        g.setGradientFill(cap); g.fillEllipse(cx - cr, cy - cr, cr * 2, cr * 2);
        g.setColour(Colours::black.withAlpha(.6f)); g.drawEllipse(cx - cr, cy - cr, cr * 2, cr * 2, 1);
        auto p1 = Point<float>(cx, cy).getPointOnCircumference(kr * .25f, av), p2 = Point<float>(cx, cy).getPointOnCircumference(kr * .95f, av);
        g.setColour(Colours::white.withAlpha(isMouseOver() ? 1.0f : .92f)); g.drawLine({ p1, p2 }, jmax(1.6f, 2.2f * ctx.S));
        if (capH > 0)
        {
            bool kill = showKill && value < .005f;
            g.setFont(font(capH * .85f, true)); g.setColour(kill ? pal::red : pal::dim);
            g.drawText(kill ? "KILL" : caption, b.withTop(b.getBottom() - capH).expanded(6, 0), Justification::centred, false);
        }
        if (param == nullptr) { g.setColour(pal::bg.withAlpha(.6f)); g.fillRect(b); }
    }
};

// faders: channel (vertical), pitch (vertical, + at the bottom like a Technics, centre detent), crossfader
struct Fader : Component
{
    enum Kind { channel, pitch, cross };
    Fader(Ctx& c, RangedAudioParameter* p, Kind k, Colour acc) : ctx(c), param(p), kind(k), accent(acc)
    {
        if (param != nullptr)
        {
            att = std::make_unique<ParameterAttachment>(*param, [this](float v) { value = param->convertTo0to1(v); repaint(); });
            att->sendInitialUpdate();
            ctx.reg(*this, param->getParameterID());
        }
    }
    Ctx& ctx;
    RangedAudioParameter* param;
    Kind kind;
    Colour accent;
    float value = 0, grab = 0;
    bool dragging = false;
    std::unique_ptr<ParameterAttachment> att;
    bool vertical() const { return kind != cross; }
    float capLen() const { return (vertical() ? (kind == pitch ? 26.0f : 30.0f) : 22.0f) * ctx.S; }
    Rectangle<float> trackArea() const
    {
        auto b = getLocalBounds().toFloat();
        float c = capLen() * .5f + 2;
        return vertical() ? b.reduced(0, c) : b.reduced(c, 0);
    }
    float toPix(float v) const
    {
        auto t = trackArea();
        if (kind == cross) return t.getX() + v * t.getWidth();
        if (kind == pitch) return t.getY() + v * t.getHeight();   // + (max) at the bottom
        return t.getBottom() - v * t.getHeight();
    }
    float fromPix(float px) const
    {
        auto t = trackArea();
        if (kind == cross) return (px - t.getX()) / jmax(1.0f, t.getWidth());
        if (kind == pitch) return (px - t.getY()) / jmax(1.0f, t.getHeight());
        return (t.getBottom() - px) / jmax(1.0f, t.getHeight());
    }
    void set(float v)
    {
        if (att == nullptr) return;
        v = jlimit(0.0f, 1.0f, v);
        if (kind == pitch && std::abs(v - .5f) < .012f) v = .5f;   // centre detent
        att->setValueAsPartOfGesture(param->convertFrom0to1(v));
    }
    void mouseDown(const MouseEvent& e) override
    {
        if (att == nullptr) return;
        att->beginGesture(); dragging = true;
        float px = vertical() ? e.position.y : e.position.x;
        float cp = toPix(value);
        if (std::abs(px - cp) <= capLen() * .5f) grab = px - cp;
        else { grab = 0; set(fromPix(px)); }
    }
    void mouseDrag(const MouseEvent& e) override
    {
        if (!dragging) return;
        float px = (vertical() ? e.position.y : e.position.x) - grab;
        if (e.mods.isShiftDown() && kind == pitch)
        {
            float start = toPix(value);
            px = start + (px - start) * .2f;
        }
        set(fromPix(px));
    }
    void mouseUp(const MouseEvent&) override { if (dragging && att) att->endGesture(); dragging = false; }
    void mouseDoubleClick(const MouseEvent&) override { if (att) att->setValueAsCompleteGesture(param->convertFrom0to1(param->getDefaultValue())); }
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails& w) override
    {
        if (att == nullptr) return;
        float step = (kind == pitch ? .004f : .03f) * (w.deltaY > 0 ? 1.0f : -1.0f) * (kind == pitch ? 1.0f : 1.0f);
        if (kind == pitch) step = -step;
        att->setValueAsCompleteGesture(param->convertFrom0to1(jlimit(0.0f, 1.0f, value + step)));
    }
    void paint(Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        auto t = trackArea();
        float S = ctx.S;
        // scale ticks
        g.setColour(pal::faint);
        int nt = kind == pitch ? 16 : 10;
        for (int i = 0; i <= nt; ++i)
        {
            float v = (float) i / (float) nt;
            float p = toPix(v);
            bool major = kind == pitch ? (i % 4 == 0) : (i % 5 == 0);
            float len = (major ? 7.0f : 4.0f) * S;
            if (vertical())
            {
                g.fillRect(b.getX(), p - .5f, len, 1.0f);
                g.fillRect(b.getRight() - len, p - .5f, len, 1.0f);
            }
            else g.fillRect(p - .5f, b.getY(), 1.0f, len);
        }
        // slot
        float sw = (kind == pitch ? 5.0f : 4.0f) * S;
        auto slot = vertical() ? Rectangle<float>(t.getCentreX() - sw * .5f, t.getY() - 4, sw, t.getHeight() + 8)
                               : Rectangle<float>(t.getX() - 4, t.getCentreY() - sw * .5f, t.getWidth() + 8, sw);
        g.setColour(Colours::black); g.fillRoundedRectangle(slot, sw * .5f);
        g.setColour(Colours::white.withAlpha(.06f)); g.drawRoundedRectangle(slot.expanded(1), sw * .5f, 1);
        if (kind == pitch)
        {
            // the centre detent: green when at 0 (like the SL-1200's quartz lock lamp)
            bool zero = std::abs(value - .5f) < .0015f;
            float cyy = toPix(.5f);
            g.setColour(zero ? pal::green : pal::faint);
            g.fillRect(b.getX(), cyy - 1, 9 * S, 2.0f); g.fillRect(b.getRight() - 9 * S, cyy - 1, 9 * S, 2.0f);
        }
        if (kind == cross)
        {
            float cxx = toPix(.5f);
            g.setColour(pal::faint); g.fillRect(cxx - .5f, b.getBottom() - 6 * S, 1.0f, 6 * S);
        }
        // cap
        float p = toPix(value), cl = capLen();
        Rectangle<float> cap;
        if (kind == channel) cap = { t.getCentreX() - b.getWidth() * .36f, p - cl * .5f, b.getWidth() * .72f, cl };
        else if (kind == pitch) cap = { t.getCentreX() - b.getWidth() * .42f, p - cl * .5f, b.getWidth() * .84f, cl };
        else cap = { p - cl * .5f, t.getCentreY() - b.getHeight() * .42f, cl, b.getHeight() * .84f };
        g.setColour(Colours::black.withAlpha(.55f)); g.fillRoundedRectangle(cap.translated(1.5f, 2.5f), 3);
        if (kind == pitch)
        {
            ColourGradient gr(Colour(0xffc9cdd2), cap.getX(), cap.getY(), Colour(0xff6c7179), cap.getX(), cap.getBottom(), false);
            g.setGradientFill(gr); g.fillRoundedRectangle(cap, 2.5f);
            g.setColour(Colours::black.withAlpha(.35f));
            for (int i = 1; i < 6; ++i) { float y = cap.getY() + cap.getHeight() * (float) i / 6; if (i != 3) g.drawLine(cap.getX() + 3, y, cap.getRight() - 3, y, 1); }
            g.setColour(Colour(0xff111111)); g.fillRect(cap.getX() + 2, cap.getCentreY() - 1.2f, cap.getWidth() - 4, 2.4f);
        }
        else
        {
            ColourGradient gr(Colour(0xff3b3f45), cap.getX(), cap.getY(), Colour(0xff121316), cap.getX(), cap.getBottom(), false);
            g.setGradientFill(gr); g.fillRoundedRectangle(cap, 3);
            g.setColour(Colours::white.withAlpha(.12f)); g.drawRoundedRectangle(cap.reduced(.5f), 3, 1);
            g.setColour(Colours::white.withAlpha(.92f));
            if (vertical()) g.fillRect(cap.getX() + 3, cap.getCentreY() - 1, cap.getWidth() - 6, 2.0f);
            else g.fillRect(cap.getCentreX() - 1, cap.getY() + 3, 2.0f, cap.getHeight() - 6);
        }
        if (param == nullptr) { g.setColour(pal::bg.withAlpha(.6f)); g.fillRect(b); }
    }
};

// a combo bound to a parameter: the parameter's own choices when it is discrete, else a fallback value list
struct ParamCombo : ComboBox
{
    ParamCombo(Ctx& c, RangedAudioParameter* p, StringArray labels = {}, std::vector<float> values = {}) : param(p)
    {
        setWantsKeyboardFocus(false);
        if (param == nullptr) { setEnabled(false); return; }
        int steps = param->getNumSteps();
        if (param->isDiscrete() && steps > 1 && steps <= 64)
        {
            auto all = param->getAllValueStrings();
            for (int i = 0; i < steps; ++i)
            {
                float n = (float) i / (float) (steps - 1);
                vals.push_back(param->convertFrom0to1(n));
                names.add(i < all.size() ? all[i] : param->getText(n, 32));
            }
        }
        else { names = labels; vals = values; }
        addItemList(names, 1);
        att = std::make_unique<ParameterAttachment>(*param, [this](float v) {
            int best = 0; float bd = 1e9f;
            for (size_t i = 0; i < vals.size(); ++i) if (std::abs(vals[i] - v) < bd) { bd = std::abs(vals[i] - v); best = (int) i; }
            setSelectedItemIndex(best, dontSendNotification);
        });
        att->sendInitialUpdate();
        onChange = [this] { int i = getSelectedItemIndex(); if (i >= 0 && (size_t) i < vals.size()) att->setValueAsCompleteGesture(vals[(size_t) i]); };
        c.reg(*this, param->getParameterID());
    }
    RangedAudioParameter* param;
    StringArray names;
    std::vector<float> vals;
    std::unique_ptr<ParameterAttachment> att;
};

// a stereo LED ladder
struct Meter : Component
{
    Meter(std::function<std::pair<float, float>()> src, int segs = 16) : source(std::move(src)), n(segs) { setInterceptsMouseClicks(false, false); }
    std::function<std::pair<float, float>()> source;
    int n;
    float lv[2] {}, hold[2] {}; int holdT[2] {};
    int lit[2] { -1, -1 }, held[2] { -1, -1 };
    static int segFor(float lin, int n)
    {
        if (lin <= 1.0e-5f) return 0;
        float db = Decibels::gainToDecibels(lin);
        float t = jmap(db, -42.0f, 3.0f, 0.0f, 1.0f);
        return jlimit(0, n, (int) std::ceil(t * (float) n));
    }
    void tick()
    {
        auto v = source();
        float in[2] { v.first, v.second };
        bool dirty = false;
        for (int c = 0; c < 2; ++c)
        {
            lv[c] = jmax(in[c], lv[c] * .86f);
            if (lv[c] >= hold[c]) { hold[c] = lv[c]; holdT[c] = 45; }
            else if (--holdT[c] < 0) hold[c] *= .93f;
            int s = segFor(lv[c], n), h = segFor(hold[c], n);
            if (s != lit[c] || h != held[c]) { lit[c] = s; held[c] = h; dirty = true; }
        }
        if (dirty) repaint();
    }
    void paint(Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(Colours::black); g.fillRoundedRectangle(b, 2);
        float colW = (b.getWidth() - 3) / 2, segH = (b.getHeight() - 2) / (float) n;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                Colour col = i >= n - 2 ? pal::red : i >= n - 5 ? pal::amber : pal::green;
                bool on = i < lit[c] || i == held[c] - 1;
                auto r = Rectangle<float>(b.getX() + 1 + (float) c * (colW + 1), b.getBottom() - 1 - (float) (i + 1) * segH + .8f, colW, segH - 1.2f);
                g.setColour(on ? col : col.withAlpha(.1f)); g.fillRect(r);
            }
    }
};

// ------------------------------------------------------------------------------------------------ waveforms
struct WaveView : Component
{
    WaveView(Ctx& c, int deck) : ctx(c), d(deck) { setOpaque(true); }
    Ctx& ctx;
    int d;
    bool scrubbing = false;
    float lastX = 0;
    double pxPerSec() const { return (double) getWidth() / (double) ctx.zoom; }
    void mouseDown(const MouseEvent& e) override
    {
        if (ctx.trk[d] == nullptr) return;
        scrubbing = true; lastX = e.position.x;
        ctx.send(Cmd::touch, d, 0, true);
    }
    void mouseDrag(const MouseEvent& e) override
    {
        if (!scrubbing) return;
        float dx = e.position.x - lastX; lastX = e.position.x;
        if (std::abs(dx) > 0) ctx.send(Cmd::move, d, -(double) dx / pxPerSec());
    }
    void mouseUp(const MouseEvent&) override { if (scrubbing) ctx.send(Cmd::touch, d, 0, false); scrubbing = false; }
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails& w) override
    {
        ctx.zoom = jlimit(1.5f, 32.0f, ctx.zoom * (w.deltaY > 0 ? .85f : 1.0f / .85f));
        if (auto* p = getParentComponent()) p->repaint();
    }
    void paint(Graphics& g) override
    {
        const int W = getWidth(), H = getHeight();
        const float cy = (float) H * .5f;
        ColourGradient bgg(Colour(0xff0d0f12), 0, 0, Colour(0xff060708), 0, cy, false);
        bgg.isRadial = false;
        g.setColour(Colour(0xff08090b)); g.fillRect(getLocalBounds());
        g.setColour(Colours::white.withAlpha(.035f)); g.fillRect(0.0f, cy - .5f, (float) W, 1.0f);
        const auto& st = ctx.proc.status(d);
        auto t = ctx.trk[d];
        const float cx = (float) W * .5f;
        if (t == nullptr)
        {
            g.setColour(pal::faint); g.setFont(font(13 * ctx.S, true));
            g.drawText(ctx.proc.loading[d].load() ? U("LOADING\xe2\x80\xa6") : String("DECK ") + (d == 0 ? "A" : "B") + U("  \xc2\xb7  LOAD A TRACK FROM THE LIBRARY"), getLocalBounds(), Justification::centred);
            g.setColour(Colours::white.withAlpha(.6f)); g.fillRect(cx - 1, 0.0f, 2.0f, (float) H);
            return;
        }
        const double pos = st.pos.load(), pps = pxPerSec();
        const double left = pos - cx / pps;
        const auto& wc = ctx.wave[d];
        // loop region
        double la = st.loopA.load(), lb = st.loopB.load();
        bool looping = st.looping.load() || st.rolling.load();
        if (la >= 0 && lb > la)
        {
            float x0 = (float) ((la - left) * pps), x1 = (float) ((lb - left) * pps);
            g.setColour((looping ? pal::green : pal::faint).withAlpha(looping ? .16f : .08f)); g.fillRect(x0, 0.0f, x1 - x0, (float) H);
            g.setColour((looping ? pal::green : pal::faint).withAlpha(.7f)); g.fillRect(x0, 0.0f, 1.5f, (float) H); g.fillRect(x1 - 1.5f, 0.0f, 1.5f, (float) H);
        }
        // the waveform: one column per pixel (max over the columns it covers), mirrored
        const float half = cy - 3;
        const double colPerPx = kWaveRate / pps;
        for (int x = 0; x < W; ++x)
        {
            double t0 = left + x / pps;
            int c0 = (int) std::floor(t0 * kWaveRate), c1 = jmax(c0 + 1, (int) std::floor((t0 + 1 / pps) * kWaveRate));
            if (c1 <= 0 || c0 >= wc.n) continue;
            c0 = jmax(0, c0); c1 = jmin(wc.n, c1);
            int best = c0; uint8 a = at(wc.amp, c0);
            for (int c = c0 + 1; c < c1; ++c) if (at(wc.amp, c) > a) { a = at(wc.amp, c); best = c; }
            float h = (float) a / 255.0f * half;
            if (h < .5f) continue;
            Colour col(at(wc.argb, best));
            if ((float) x < cx - 1) col = col.withMultipliedBrightness(.82f);   // played part a touch dimmer
            g.setColour(col); g.fillRect((float) x, cy - h, 1.0f, h * 2);
            g.setColour(Colour(at(wc.core, best)).withAlpha(.9f)); g.fillRect((float) x, cy - h * .38f, 1.0f, h * .76f);
        }
        ignoreUnused(colPerPx);
        // beatgrid
        if (t->bpm > 0)
        {
            double bl = 60.0 / t->bpm;
            long long k0 = (long long) std::ceil((left - t->firstBeat) / bl), k1 = (long long) std::floor((left + W / pps - t->firstBeat) / bl);
            if (k1 - k0 < 400)
                for (long long k = k0; k <= k1; ++k)
                {
                    float x = (float) ((t->firstBeat + (double) k * bl - left) * pps);
                    bool bar = ((k % 4) + 4) % 4 == 0;
                    g.setColour(Colours::white.withAlpha(bar ? .55f : .16f));
                    g.fillRect(x - .5f, 0.0f, 1.0f, (float) H);
                    if (bar)
                    {
                        Path tri; tri.addTriangle(x - 4, 0, x + 4, 0, x, 5); tri.addTriangle(x - 4, (float) H, x + 4, (float) H, x, (float) H - 5);
                        g.setColour(Colours::white.withAlpha(.75f)); g.fillPath(tri);
                    }
                }
        }
        // hot cues
        for (int i = 0; i < kCues; ++i)
        {
            const auto& c = at(ctx.cues[d], i);
            if (!c.set()) continue;
            float x = (float) ((c.t - left) * pps);
            if (x < -30 || x > (float) W + 2) continue;
            auto col = cueColour(c);
            g.setColour(col); g.fillRect(x - 1, 0.0f, 2.0f, (float) H);
            float fs = 13 * ctx.S;
            Rectangle<float> flag(x, 0, fs + 3, fs + 2);
            g.fillRect(flag);
            g.setColour(Colours::black); g.setFont(font(fs * .9f, true));
            g.drawText(String(i + 1), flag, Justification::centred);
        }
        // the main cue point (the CUE button)
        double cp = st.cuePoint.load();
        if (cp >= 0)
        {
            float x = (float) ((cp - left) * pps);
            Path p; p.addTriangle(x - 5, (float) H, x + 5, (float) H, x, (float) H - 7);
            g.setColour(Colours::white); g.fillPath(p);
        }
        // playhead
        g.setColour(Colours::black.withAlpha(.5f)); g.fillRect(cx - 2.5f, 0.0f, 5.0f, (float) H);
        g.setColour(st.touched.load() ? pal::amber : Colours::white); g.fillRect(cx - 1, 0.0f, 2.0f, (float) H);
        // the slip "ghost" position
        if (st.shadow.load() >= 0 && Ctx::on(ctx.dp(d, "slip")))
        {
            float x = (float) ((st.shadow.load() - left) * pps);
            if (std::abs(x - cx) > 2) { g.setColour(pal::violet.withAlpha(.8f)); g.fillRect(x - .5f, 0.0f, 1.0f, (float) H); }
        }
        // deck tag
        g.setColour(pal::deck[d].withAlpha(.9f)); g.fillRect(0.0f, 0.0f, 3.0f, (float) H);
    }
};

struct Overview : Component
{
    Overview(Ctx& c, int deck) : ctx(c), d(deck) { setOpaque(true); }
    Ctx& ctx;
    int d;
    Image img;
    const Track* imgFor = nullptr;
    void render()
    {
        imgFor = ctx.trk[d].get();
        int W = getWidth(), H = getHeight();
        img = Image(Image::RGB, jmax(1, W), jmax(1, H), true);
        Graphics g(img);
        g.fillAll(Colour(0xff08090b));
        const auto& wc = ctx.wave[d];
        if (wc.n == 0 || W <= 0) return;
        float cy = (float) H * .5f, half = cy - 1;
        for (int x = 0; x < W; ++x)
        {
            int c0 = (int) ((int64) x * wc.n / W), c1 = jmax(c0 + 1, (int) ((int64) (x + 1) * wc.n / W));
            c1 = jmin(c1, wc.n);
            int best = c0; uint8 a = 0;
            for (int c = c0; c < c1; ++c) if (at(wc.amp, c) > a) { a = at(wc.amp, c); best = c; }
            float h = (float) a / 255.0f * half;
            g.setColour(Colour(at(wc.argb, best))); g.fillRect((float) x, cy - h, 1.0f, h * 2);
        }
    }
    void resized() override { imgFor = nullptr; }
    double lenOf() const { return ctx.trk[d] ? ctx.trk[d]->length() : 0; }
    void seekTo(float x)
    {
        double len = lenOf();
        if (len <= 0) return;
        ctx.send(Cmd::seek, d, jlimit(0.0, len, (double) x / jmax(1, getWidth()) * len));
    }
    void mouseDown(const MouseEvent& e) override { seekTo(e.position.x); }
    void mouseDrag(const MouseEvent& e) override { seekTo(e.position.x); }
    void paint(Graphics& g) override
    {
        if (imgFor != ctx.trk[d].get() || img.getWidth() != getWidth() || img.getHeight() != getHeight()) render();
        g.drawImageAt(img, 0, 0);
        float W = (float) getWidth(), H = (float) getHeight();
        double len = lenOf();
        if (len > 0)
        {
            const auto& st = ctx.proc.status(d);
            float x = (float) (st.pos.load() / len) * W;
            g.setColour(Colours::black.withAlpha(.5f)); g.fillRect(0.0f, 0.0f, jlimit(0.0f, W, x), H);
            if (auto* t = ctx.trk[d].get(); t != nullptr && t->bpm > 0)
            {
                double bl = 60.0 / t->bpm * 4 * 16;   // every 16 bars
                g.setColour(Colours::white.withAlpha(.12f));
                for (double b = t->firstBeat; b < len; b += bl) g.fillRect((float) (b / len) * W, 0.0f, 1.0f, H * .25f);
            }
            for (int i = 0; i < kCues; ++i)
            {
                const auto& c = at(ctx.cues[d], i);
                if (!c.set()) continue;
                float cxp = (float) (c.t / len) * W;
                g.setColour(cueColour(c)); g.fillRect(cxp - 1, 0.0f, 2.0f, H);
            }
            double la = st.loopA.load(), lb = st.loopB.load();
            if (la >= 0 && lb > la) { g.setColour(pal::green.withAlpha(.35f)); g.fillRect((float) (la / len) * W, 0.0f, jmax(2.0f, (float) ((lb - la) / len) * W), H); }
            g.setColour(Colours::white); g.fillRect(x - 1, 0.0f, 2.0f, H);
            // the last 30 s flash red like Serato's end-of-track warning
            if (len - st.pos.load() < 30 && st.playing.load() && (Time::getMillisecondCounter() / 500) % 2 == 0)
            { g.setColour(pal::red.withAlpha(.6f)); g.drawRect(0.0f, 0.0f, W, H, 1.5f); }
        }
        else { g.setColour(pal::faint.withAlpha(.4f)); g.fillRect(0.0f, H * .5f - .5f, W, 1.0f); }
        g.setColour(pal::edge); g.drawRect(getLocalBounds());
    }
};

struct InfoRow : Component
{
    InfoRow(Ctx& c, int deck) : ctx(c), d(deck) {}
    Ctx& ctx;
    int d;
    String sig;
    void tick()
    {
        // repaint only when what is shown changes (time to 0.1 s, bpm to 0.01, flags)
        const auto& st = ctx.proc.status(d);
        String s; s << (ctx.trk[d] ? ctx.trk[d]->path.c_str() : "") << (int) (st.pos.load() * 10) << "|" << (int) (st.bpmNow.load() * 100) << "|"
                     << (int) st.synced.load() << (int) st.looping.load() << (int) ctx.proc.loading[d].load() << ctx.proc.loadError[d]
                     << (int) Ctx::on(ctx.dp(d, "keylock")) << (int) Ctx::on(ctx.dp(d, "slip")) << (int) Ctx::on(ctx.dp(d, "quant"))
                     << (int) (ctx.pitchPct(d) * 100) << (int) ctx.rpm45(d) << (int) st.censoring.load() << (int) st.rolling.load()
                     << (int) (ctx.proc.hostBpm() * 10);
        if (s != sig) { sig = s; repaint(); }
    }
    void badge(Graphics& g, Rectangle<float>& r, const String& t, bool onState, Colour c)
    {
        float w = (float) t.length() * 7.2f * ctx.S + 12 * ctx.S;
        auto b = r.removeFromRight(w).reduced(2, r.getHeight() * .2f);
        r.removeFromRight(3);
        g.setColour(onState ? c.withAlpha(.22f) : Colours::white.withAlpha(.03f)); g.fillRoundedRectangle(b, 3);
        g.setColour(onState ? c : pal::faint.withAlpha(.6f)); g.drawRoundedRectangle(b, 3, 1);
        g.setFont(font(10.5f * ctx.S, true)); g.drawText(t, b, Justification::centred);
    }
    void paint(Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        float S = ctx.S;
        ColourGradient bgc(pal::panel2, 0, 0, pal::panel, 0, r.getBottom(), false);
        g.setGradientFill(bgc); g.fillRect(r);
        g.setColour(pal::edge); g.fillRect(r.getX(), d == 0 ? r.getBottom() - 1 : r.getY(), r.getWidth(), 1.0f);
        const auto& st = ctx.proc.status(d);
        auto t = ctx.trk[d];
        // deck letter
        auto letter = r.removeFromLeft(r.getHeight()).reduced(4 * S);
        g.setColour(pal::deck[d]); g.fillRoundedRectangle(letter, 4);
        g.setColour(Colours::black); g.setFont(font(letter.getHeight() * .72f, true)); g.drawText(d == 0 ? "A" : "B", letter, Justification::centred);
        r.removeFromLeft(8 * S);
        // right block: remaining / elapsed, BPM + pitch, key, badges
        auto right = r.removeFromRight(560 * S);
        auto timeR = right.removeFromRight(118 * S);
        double len = t ? t->length() : 0, pos = st.pos.load();
        g.setFont(mono(19 * S, true)); g.setColour(len > 0 && len - pos < 30 ? pal::red : pal::text);
        g.drawText(t ? fmtTime(-(len - pos), true) : "-00:00.0", timeR.withTrimmedBottom(timeR.getHeight() * .38f), Justification::bottomRight);
        g.setFont(mono(11 * S)); g.setColour(pal::dim);
        g.drawText(t ? fmtTime(pos, true) + " / " + fmtTime(len, false) : "00:00.0", timeR.withTrimmedTop(timeR.getHeight() * .62f), Justification::topRight);
        right.removeFromRight(14 * S);
        auto bpmR = right.removeFromRight(92 * S);
        double bpm = st.bpmNow.load();
        if (bpm <= 0 && t && t->bpm > 0) bpm = t->bpm * (1 + ctx.pitchPct(d) / 100) * (ctx.rpm45(d) ? 1.35 : 1);
        g.setFont(mono(21 * S, true)); g.setColour(st.synced.load() ? pal::cyan : pal::text);
        g.drawText(bpm > 0 ? String(bpm, 1) : "---.-", bpmR.withTrimmedBottom(bpmR.getHeight() * .38f), Justification::bottomRight);
        double pp = ctx.pitchPct(d);
        g.setFont(mono(11 * S)); g.setColour(std::abs(pp) < .005 ? pal::dim : pal::amber);
        g.drawText((pp >= 0 ? "+" : "") + String(pp, 2) + U("%  \xc2\xb1") + String((int) std::round(ctx.rangePct(d))) + (ctx.rpm45(d) ? "  45" : ""),
                   bpmR.withTrimmedTop(bpmR.getHeight() * .62f), Justification::topRight);
        right.removeFromRight(12 * S);
        auto keyR = right.removeFromRight(70 * S).reduced(0, r.getHeight() * .22f);
        if (t && t->key >= 0)
        {
            auto kc = keyColour(t->key);
            g.setColour(kc.withAlpha(.18f)); g.fillRoundedRectangle(keyR, 4);
            g.setColour(kc); g.drawRoundedRectangle(keyR, 4, 1.2f);
            g.setFont(font(12.5f * S, true));
            g.drawText(String(camelot(t->key)) + "  " + String(keyName(t->key)), keyR, Justification::centred);
        }
        else { g.setColour(pal::faint); g.drawRoundedRectangle(keyR, 4, 1); g.setFont(font(12 * S, true)); g.drawText("KEY", keyR, Justification::centred); }
        right.removeFromRight(8 * S);
        badge(g, right, "SLIP", Ctx::on(ctx.dp(d, "slip")), pal::violet);
        badge(g, right, "KEY LOCK", Ctx::on(ctx.dp(d, "keylock")), pal::amber);
        badge(g, right, "SYNC", st.synced.load(), pal::cyan);
        badge(g, right, "Q", Ctx::on(ctx.dp(d, "quant")), pal::green);
        if (st.looping.load() || st.rolling.load()) badge(g, right, st.rolling.load() ? "ROLL" : "LOOP", true, pal::green);
        if (st.censoring.load()) badge(g, right, "CENSOR", true, pal::red);
        // title / artist / status
        auto txt = r.reduced(0, 3 * S);
        if (ctx.proc.loading[d].load())
        {
            g.setColour(pal::amber); g.setFont(font(15 * S, true));
            g.drawText(U("Loading\xe2\x80\xa6") + String::repeatedString(".", (int) (Time::getMillisecondCounter() / 300 % 3)), txt, Justification::centredLeft, true);
        }
        else if (ctx.proc.loadError[d].isNotEmpty() && t == nullptr)
        {
            g.setColour(pal::red); g.setFont(font(13 * S, true));
            g.drawText(ctx.proc.loadError[d], txt, Justification::centredLeft, true);
        }
        else if (t)
        {
            String title = String::fromUTF8(t->title.c_str()), artist = String::fromUTF8(t->artist.c_str());
            if (title.isEmpty()) title = File(String::fromUTF8(t->path.c_str())).getFileNameWithoutExtension();
            g.setColour(pal::text); g.setFont(font(15.5f * S, true));
            g.drawText(title, txt.withTrimmedBottom(txt.getHeight() * .45f), Justification::bottomLeft, true);
            g.setColour(pal::dim); g.setFont(font(12 * S));
            String sub = artist;
            if (ctx.proc.loadError[d].isNotEmpty()) sub = ctx.proc.loadError[d];
            if (t->fromSerato) sub << U("   \xe2\x97\x86 Serato");
            g.drawText(sub, txt.withTrimmedTop(txt.getHeight() * .58f), Justification::topLeft, true);
        }
        else { g.setColour(pal::faint); g.setFont(font(14 * S, true)); g.drawText("No track", txt, Justification::centredLeft); }
    }
};

// ------------------------------------------------------------------------------------------------ turntable
struct Platter : Component
{
    Platter(Ctx& c, int deck) : ctx(c), d(deck) { ctx.reg(*this, deckParam(d, "platter")); }
    Ctx& ctx;
    int d;
    bool hand = false;
    double lastAng = 0, handTurns = 0, shown = 0, lastEngine = 0, strobeT = 0, lastMs = 0;
    Image vinyl;
    bool hitTest(int x, int y) override
    {
        auto c = getLocalBounds().toFloat().getCentre();
        return c.getDistanceFrom({ (float) x, (float) y }) <= (float) getWidth() * .5f;
    }
    double angleOf(Point<float> p) const { auto c = getLocalBounds().toFloat().getCentre(); return std::atan2((double) (p.y - c.y), (double) (p.x - c.x)); }
    void mouseDown(const MouseEvent& e) override
    {
        hand = true; lastAng = angleOf(e.position); handTurns = shown;
        ctx.send(Cmd::touch, d, 0, true);
        repaint();
    }
    void mouseDrag(const MouseEvent& e) override
    {
        double a = angleOf(e.position), da = a - lastAng;
        while (da > MathConstants<double>::pi) da -= MathConstants<double>::twoPi;
        while (da < -MathConstants<double>::pi) da += MathConstants<double>::twoPi;
        lastAng = a;
        // ignore the jitter right on the spindle
        auto c = getLocalBounds().toFloat().getCentre();
        if (c.getDistanceFrom(e.position) < (float) getWidth() * .04f) return;
        double turns = da / MathConstants<double>::twoPi;
        handTurns += turns; shown = handTurns;
        ctx.send(Cmd::move, d, turns * Ctx::secPerRev());
        repaint();
    }
    void mouseUp(const MouseEvent&) override { hand = false; ctx.send(Cmd::touch, d, 0, false); }
    // advance the platter: the hand when touched, else the engine's angle (or its rate integrated)
    void tick()
    {
        double now = Time::getMillisecondCounterHiRes(), dt = lastMs > 0 ? jlimit(0.0, .1, (now - lastMs) / 1000.0) : 0;
        lastMs = now;
        strobeT += dt;
        // the engine's angle is in turns, wrapped to 0..1: unwrap it so the strobe sees continuous motion
        double a = ctx.proc.status(d).angle.load(), da = a - lastEngine;
        da -= std::round(da);
        lastEngine = a;
        if (!hand) shown += da;
        repaint();
    }
    void buildVinyl(int D)
    {
        vinyl = Image(Image::ARGB, D, D, true);
        Graphics g(vinyl);
        float R = (float) D * .5f, c = R;
        // platter rim with the strobe dots ring background
        ColourGradient rim(Colour(0xffb8bcc2), 0, 0, Colour(0xff4a4e55), (float) D, (float) D, false);
        g.setGradientFill(rim); g.fillEllipse(0, 0, (float) D, (float) D);
        g.setColour(Colour(0xff1d1f23)); g.fillEllipse(R * .06f, R * .06f, R * 1.88f, R * 1.88f);
        // slipmat edge and the record
        float rr = R * .9f;
        g.setColour(Colour(0xff101114)); g.fillEllipse(c - R * .93f, c - R * .93f, R * 1.86f, R * 1.86f);
        ColourGradient vin(Colour(0xff17181b), c, c, Colour(0xff050506), c + rr, c, true);
        g.setGradientFill(vin); g.fillEllipse(c - rr, c - rr, rr * 2, rr * 2);
        // grooves
        for (float r = rr * .97f; r > rr * .38f; r -= jmax(1.3f, rr * .012f))
        {
            float a = .05f + .05f * std::sin(r * .9f);
            g.setColour(Colours::white.withAlpha(a)); g.drawEllipse(c - r, c - r, r * 2, r * 2, .6f);
        }
        // track gaps
        for (float f : { .82f, .68f, .55f }) { g.setColour(Colours::black.withAlpha(.7f)); g.drawEllipse(c - rr * f, c - rr * f, rr * f * 2, rr * f * 2, 1.6f); }
        // the light reflection (static: the room light does not spin)
        for (int i = 0; i < 2; ++i)
        {
            Path sheen; float a0 = i == 0 ? -2.3f : .85f;
            sheen.addPieSegment(c - rr, c - rr, rr * 2, rr * 2, a0, a0 + .5f, .4f);
            ColourGradient sg(Colours::white.withAlpha(.09f), c, c, Colours::white.withAlpha(0.0f), c + rr * std::sin(a0 + .25f), c - rr * std::cos(a0 + .25f), true);
            g.setGradientFill(sg); g.fillPath(sheen);
        }
        g.setColour(Colours::black.withAlpha(.8f)); g.drawEllipse(c - rr, c - rr, rr * 2, rr * 2, 1.2f);
    }
    void paint(Graphics& g) override
    {
        int D = jmin(getWidth(), getHeight());
        if (D < 10) return;
        if (vinyl.getWidth() != D) buildVinyl(D);
        float R = (float) D * .5f;
        auto cc = getLocalBounds().toFloat().getCentre();
        float cx = cc.x, cy = cc.y;
        g.drawImageAt(vinyl, (int) (cx - R), (int) (cy - R));
        double turns = shown;
        float ang = (float) (std::fmod(turns, 1.0) * MathConstants<double>::twoPi);
        const auto& st = ctx.proc.status(d);
        // strobe dots: four rows; a row stands still when the platter turns at its nominal speed (the strobe illusion:
        // each row is drawn at (platter angle - nominal angle) modulo the dot spacing)
        {
            const int nd = 90;
            const double spacing = MathConstants<double>::twoPi / nd;
            const double nominal = (ctx.rpm45(d) ? 45.0 : 100.0 / 3.0) / 60.0 * MathConstants<double>::twoPi;   // rad/s
            const double ratios[4] = { 1.066, 1.033, 1.0, .967 };
            for (int row = 0; row < 4; ++row)
            {
                double off = std::fmod(turns * MathConstants<double>::twoPi - ratios[row] * nominal * strobeT, spacing);
                float rr = R * (.985f - (float) row * .022f), ds = jmax(1.2f, R * .014f);
                g.setColour(Colour(0xffe9ecef).withAlpha(row == 2 ? .85f : .6f));
                for (int i = 0; i < nd; ++i)
                {
                    float a = (float) (off + i * spacing);
                    g.fillRect(cx + rr * std::sin(a) - ds * .5f, cy - rr * std::cos(a) - ds * .5f, ds, ds);
                }
            }
        }
        // the label (spins)
        float lr = R * .33f;
        {
            Graphics::ScopedSaveState ss(g);
            g.addTransform(AffineTransform::rotation(ang, cx, cy));
            auto dc = pal::deck[d];
            ColourGradient lg(dc.brighter(.25f), cx, cy - lr, dc.darker(.6f), cx, cy + lr, false);
            g.setGradientFill(lg); g.fillEllipse(cx - lr, cy - lr, lr * 2, lr * 2);
            g.setColour(Colours::black.withAlpha(.35f)); g.drawEllipse(cx - lr * .93f, cy - lr * .93f, lr * 1.86f, lr * 1.86f, 1);
            auto t = ctx.trk[d];
            String title = t ? String::fromUTF8(t->title.c_str()) : String("OMEGA");
            if (t && title.isEmpty()) title = File(String::fromUTF8(t->path.c_str())).getFileNameWithoutExtension();
            String artist = t ? String::fromUTF8(t->artist.c_str()) : String("WHEEL OF STEEL");
            g.setColour(Colours::black.withAlpha(.85f));
            g.setFont(font(lr * .2f, true));
            g.drawFittedText(title.toUpperCase(), Rectangle<float>(cx - lr * .8f, cy - lr * .72f, lr * 1.6f, lr * .5f).toNearestInt(), Justification::centred, 2, .7f);
            g.setFont(font(lr * .15f));
            g.drawFittedText(artist, Rectangle<float>(cx - lr * .75f, cy + lr * .22f, lr * 1.5f, lr * .26f).toNearestInt(), Justification::centred, 1, .7f);
            g.setFont(font(lr * .13f, true));
            g.drawText(ctx.rpm45(d) ? "45 RPM" : U("33\xe2\x85\x93 RPM"), Rectangle<float>(cx - lr * .6f, cy + lr * .5f, lr * 1.2f, lr * .2f), Justification::centred);
            // the marker on the record edge Serato draws (and a sticker on the vinyl to see it turn)
            g.setColour(Colours::white.withAlpha(.9f));
            g.fillRoundedRectangle(cx - 1.5f, cy - R * .89f, 3.0f, R * .2f, 1.5f);
            g.setColour(dc.withAlpha(.9f));
            g.fillRoundedRectangle(cx - lr * .07f, cy - lr * .98f, lr * .14f, lr * .14f, 2);
        }
        // spindle
        float sr = R * .028f;
        g.setColour(Colour(0xffd8dbe0)); g.fillEllipse(cx - sr, cy - sr, sr * 2, sr * 2);
        g.setColour(Colours::black.withAlpha(.5f)); g.drawEllipse(cx - sr, cy - sr, sr * 2, sr * 2, 1);
        // hand on the record
        if (hand || st.touched.load())
        {
            g.setColour(pal::amber.withAlpha(.6f)); g.drawEllipse(cx - R * .9f, cy - R * .9f, R * 1.8f, R * 1.8f, 2.5f);
        }
    }
};

struct Tonearm : Component
{
    Tonearm(Ctx& c, int deck) : ctx(c), d(deck) { setInterceptsMouseClicks(false, false); }
    Ctx& ctx;
    int d;
    Point<float> pivot, centre;
    float R = 100;
    float progress = -1;   // -1 = at rest
    void tick()
    {
        float p = -1;
        if (auto t = ctx.trk[d]; t != nullptr && t->length() > 0) p = (float) jlimit(0.0, 1.0, ctx.proc.status(d).pos.load() / t->length());
        if (std::abs(p - progress) > .002f) { progress = p; repaint(); }
    }
    void paint(Graphics& g) override
    {
        float S = ctx.S;
        float L = pivot.getDistanceFrom(centre) * .97f;
        Point<float> tip;
        auto restAt = pivot + Point<float>(R * .17f, R * .5f);
        if (progress < 0) tip = pivot + (restAt - pivot) / restAt.getDistanceFrom(pivot) * L;   // on the rest
        else
        {
            float rs = R * (.86f - .46f * progress);
            // the stylus point: |tip - centre| = rs and |tip - pivot| = L
            float dd = pivot.getDistanceFrom(centre);
            float a = (L * L - rs * rs + dd * dd) / (2 * dd), h = std::sqrt(jmax(0.0f, L * L - a * a));
            auto dir = (centre - pivot) / dd;
            auto mid = pivot + dir * a;
            Point<float> perp(-dir.y, dir.x);
            auto p1 = mid + perp * h, p2 = mid - perp * h;
            tip = p1.x > p2.x ? p1 : p2;
        }
        // arm rest
        auto rest = restAt;
        g.setColour(Colour(0xff2a2d31)); g.fillEllipse(rest.x - 6 * S, rest.y - 6 * S, 12 * S, 12 * S);
        g.setColour(Colour(0xff6a6f77)); g.drawEllipse(rest.x - 6 * S, rest.y - 6 * S, 12 * S, 12 * S, 1);
        // base
        float br = R * .19f;
        g.setColour(Colours::black.withAlpha(.5f)); g.fillEllipse(pivot.x - br + 2, pivot.y - br + 3, br * 2, br * 2);
        ColourGradient bg(Colour(0xff6d727a), pivot.x, pivot.y - br, Colour(0xff25282c), pivot.x, pivot.y + br, false);
        g.setGradientFill(bg); g.fillEllipse(pivot.x - br, pivot.y - br, br * 2, br * 2);
        g.setColour(Colour(0xff17191c)); g.fillEllipse(pivot.x - br * .62f, pivot.y - br * .62f, br * 1.24f, br * 1.24f);
        // height adjust ring numbers hint
        g.setColour(Colours::white.withAlpha(.25f)); g.drawEllipse(pivot.x - br * .85f, pivot.y - br * .85f, br * 1.7f, br * 1.7f, 1);
        // counterweight behind the pivot
        auto dir = (tip - pivot) / jmax(1.0f, tip.getDistanceFrom(pivot));
        auto cw = pivot - dir * (R * .2f);
        {
            Path w; w.addRoundedRectangle(-R * .07f, -R * .085f, R * .14f, R * .17f, 3);
            w.applyTransform(AffineTransform::rotation(std::atan2(dir.y, dir.x) + MathConstants<float>::halfPi).translated(cw.x, cw.y));
            ColourGradient wg(Colour(0xffd0d4d9), cw.x - 8, cw.y, Colour(0xff4c5057), cw.x + 8, cw.y, false);
            g.setGradientFill(wg); g.fillPath(w);
        }
        // the arm: a slight S (straight here with a bend at the headshell)
        auto bend = pivot + (tip - pivot) * .82f;
        Path arm; arm.startNewSubPath(cw); arm.lineTo(bend);
        float ha = std::atan2(dir.y, dir.x) + .38f;
        auto head = bend + Point<float>(std::cos(ha), std::sin(ha)) * tip.getDistanceFrom(pivot) * .18f;
        arm.lineTo(head);
        g.setColour(Colours::black.withAlpha(.45f)); g.strokePath(arm, PathStrokeType(5 * S, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation(3, 5));
        g.setColour(Colour(0xffc7cbd1)); g.strokePath(arm, PathStrokeType(3.6f * S, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour(Colours::white.withAlpha(.6f)); g.strokePath(arm, PathStrokeType(1.0f * S));
        // headshell
        Path hs; hs.addRoundedRectangle(-5 * S, -12 * S, 10 * S, 22 * S, 2);
        hs.applyTransform(AffineTransform::rotation(ha + MathConstants<float>::halfPi).translated(head.x, head.y));
        g.setColour(Colour(0xff1a1b1e)); g.fillPath(hs);
        g.setColour(Colour(0xff8a8f97)); g.strokePath(hs, PathStrokeType(1));
        // finger lift
        auto lift = head + Point<float>(std::cos(ha + 1.4f), std::sin(ha + 1.4f)) * 9 * S;
        g.setColour(Colour(0xffaab0b8)); g.drawLine({ head, lift }, 1.5f * S);
        g.setColour(Colour(0xff9da3ab)); g.fillEllipse(pivot.x - 4 * S, pivot.y - 4 * S, 8 * S, 8 * S);
    }
};

struct Turntable : Component
{
    Turntable(Ctx& c, int deck) : ctx(c), d(deck), platter(c, deck), arm(c, deck),
        pitchF(c, c.dp(deck, "pitch"), Fader::pitch, pal::deck[deck]),
        startStop(c, "START", pal::text, deckParam(deck, "play")),
        r33(c, "33", pal::amber, deckParam(deck, "rpm45")), r45(c, "45", pal::amber, deckParam(deck, "rpm45")),
        range(c, U("\xc2\xb1" "8"), pal::amber, deckParam(deck, "range")),
        cue(c, "CUE", pal::amber, deckParam(deck, "cue")), sync(c, "SYNC", pal::cyan, deckParam(deck, "sync")),
        keylock(c, "KEY LOCK", pal::amber, deckParam(deck, "keylock")), slip(c, "SLIP", pal::violet, deckParam(deck, "slip")),
        nudgeM(c, U("\xe2\x97\x80 NUDGE"), pal::text, deckParam(deck, "nudge")), nudgeP(c, U("NUDGE \xe2\x96\xb6"), pal::text, deckParam(deck, "nudge")),
        censor(c, "CENSOR", pal::red, deckParam(deck, "censor")), load(c, "LOAD", pal::deck[deck]), quant(c, "Q", pal::green, deckParam(deck, "quant"))
    {
        addAndMakeVisible(platter);
        for (auto* b : { &startStop, &r33, &r45, &range, &cue, &sync, &keylock, &slip, &nudgeM, &nudgeP, &censor, &load, &quant }) addAndMakeVisible(b);
        addAndMakeVisible(pitchF);
        addAndMakeVisible(arm);
        const auto& st = ctx.proc.status(d);
        startStop.lit = [&st] { return st.playing.load(); };
        startStop.press = [this](bool dn) { if (dn) ctx.send(Cmd::playPause, d); };
        r33.lit = [this] { return !ctx.rpm45(d); };
        r45.lit = [this] { return ctx.rpm45(d); };
        r33.press = [this](bool dn) { if (dn) Ctx::setNorm(ctx.dp(d, "rpm45"), 0); };
        r45.press = [this](bool dn) { if (dn) Ctx::setNorm(ctx.dp(d, "rpm45"), 1); };
        range.dynText = [this] { return U("\xc2\xb1") + String((int) std::round(ctx.rangePct(d))) + "%"; };
        range.press = [this](bool dn) { if (dn) cycleRange(); };
        cue.solid = true;
        cue.lit = [&st] { return !st.playing.load() || st.touched.load(); };
        cue.press = [this](bool dn) { ctx.send(Cmd::cue, d, 0, dn); };
        sync.lit = [&st] { return st.synced.load(); };
        sync.press = [this](bool dn) { if (dn) ctx.send(Cmd::sync, d, 0, false); };
        keylock.lit = [this] { return Ctx::on(ctx.dp(d, "keylock")); };
        keylock.press = [this](bool dn) { if (dn) Ctx::toggle(ctx.dp(d, "keylock")); };
        slip.lit = [this] { return Ctx::on(ctx.dp(d, "slip")); };
        slip.press = [this](bool dn) { if (dn) Ctx::toggle(ctx.dp(d, "slip")); };
        quant.lit = [this] { return Ctx::on(ctx.dp(d, "quant")); };
        quant.press = [this](bool dn) { if (dn) Ctx::toggle(ctx.dp(d, "quant")); };
        nudgeM.press = [this](bool dn) { ctx.send(Cmd::nudge, d, dn ? -1.0 : 0.0); };
        nudgeP.press = [this](bool dn) { ctx.send(Cmd::nudge, d, dn ? 1.0 : 0.0); };
        censor.lit = [&st] { return st.censoring.load(); };
        censor.press = [this](bool dn) { ctx.send(Cmd::censor, d, 0, dn); };
        load.press = [this](bool dn) { if (dn && ctx.selectedFile) { auto f = ctx.selectedFile(); if (f.existsAsFile()) ctx.proc.loadTrack(d, f); } };
        load.lit = [this] { return ctx.proc.loading[d].load(); };
        startStop.sub = "STOP";
    }
    Ctx& ctx;
    int d;
    Platter platter;
    Tonearm arm;
    Fader pitchF;
    PButton startStop, r33, r45, range, cue, sync, keylock, slip, nudgeM, nudgeP, censor, load, quant;
    Rectangle<float> pitchRead, lampR;
    void cycleRange()
    {
        auto* p = ctx.dp(d, "range");
        if (p == nullptr) return;
        int steps = p->getNumSteps();
        if (p->isDiscrete() && steps > 1 && steps <= 16)
        {
            int i = (int) std::round(p->getValue() * (float) (steps - 1));
            Ctx::setNorm(p, (float) ((i + 1) % steps) / (float) (steps - 1));
            return;
        }
        double cur = ctx.rangePct(d), next = cur < 12 ? 16 : cur < 30 ? 50 : 8;
        bool frac = p->getNormalisableRange().end <= 1.0f;
        Ctx::setVal(p, (float) (frac ? next / 100 : next));
    }
    void resized() override
    {
        float S = ctx.S;
        auto b = getLocalBounds().toFloat();
        float m = 10 * S, h = b.getHeight();
        float D = jmin(h - 2 * m, b.getWidth() * .56f);
        platter.setBounds(Rectangle<float>(m, (h - D) * .5f, D, D).toNearestInt());
        float pw = jmax(46.0f, 58 * S);
        auto pitchCol = Rectangle<float>(b.getRight() - m - pw, m, pw, h - 2 * m);
        range.setBounds(pitchCol.removeFromTop(22 * S).toNearestInt());
        pitchRead = pitchCol.removeFromTop(20 * S);
        pitchCol.removeFromBottom(4 * S);
        pitchF.setBounds(pitchCol.toNearestInt());
        // START·STOP in the bottom-left corner like the real deck, the strobe lamp in the top-left one
        float bw = D * .19f, bh = D * .115f;
        startStop.setBounds(Rectangle<float>(m * .6f, h - m * .6f - bh, bw, bh).toNearestInt());
        lampR = { m * .9f, m * .9f, D * .08f, D * .08f };
        // the middle column: tonearm on top, buttons under it
        float x0 = m + D + m * .6f, x1 = pitchCol.getX() - m * .8f;
        auto col = Rectangle<float>(x0, m, x1 - x0, h - 2 * m);
        arm.setBounds(getLocalBounds());
        arm.R = D * .5f;
        arm.centre = platter.getBounds().toFloat().getCentre();
        arm.pivot = { jmin(x1 - D * .12f, x0 + col.getWidth() * .55f), m + D * .13f };
        auto grid = col.withTrimmedTop(D * .42f);
        float rh = grid.getHeight() / 5, cw = grid.getWidth() / 2;
        auto cell = [&](int r, int c, int span = 1) { return Rectangle<float>(grid.getX() + (float) c * cw, grid.getY() + (float) r * rh, cw * (float) span, rh).reduced(2.5f * S, 2.5f * S).toNearestInt(); };
        cue.setBounds(cell(0, 0)); sync.setBounds(cell(0, 1));
        keylock.setBounds(cell(1, 0)); slip.setBounds(cell(1, 1));
        nudgeM.setBounds(cell(2, 0)); nudgeP.setBounds(cell(2, 1));
        censor.setBounds(cell(3, 0)); load.setBounds(cell(3, 1));
        auto lastRow = cell(4, 0, 2);
        int qw = lastRow.getWidth() / 3;
        r33.setBounds(lastRow.removeFromLeft(qw).reduced(1, 0)); r45.setBounds(lastRow.removeFromLeft(qw).reduced(1, 0)); quant.setBounds(lastRow.reduced(1, 0));
        // the tonearm sits on top of the platter (drawn over it, no mouse)
        arm.toFront(false);
        for (auto* bt : { &startStop, &r33, &r45, &range, &cue, &sync, &keylock, &slip, &nudgeM, &nudgeP, &censor, &load, &quant }) bt->toFront(false);
    }
    void paint(Graphics& g) override
    {
        float S = ctx.S;
        auto b = getLocalBounds().toFloat().reduced(2);
        // the plinth: satin black with a brushed top
        g.setColour(Colours::black.withAlpha(.6f)); g.fillRoundedRectangle(b.translated(0, 3), 8);
        ColourGradient pg(Colour(0xff2b2e33), b.getX(), b.getY(), Colour(0xff17191c), b.getX(), b.getBottom(), false);
        g.setGradientFill(pg); g.fillRoundedRectangle(b, 8);
        g.setColour(Colours::white.withAlpha(.018f));
        for (float y = b.getY() + 2; y < b.getBottom() - 2; y += 2) g.fillRect(b.getX() + 4, y, b.getWidth() - 8, 1.0f);
        g.setColour(Colours::white.withAlpha(.08f)); g.drawRoundedRectangle(b.reduced(.5f), 8, 1);
        g.setColour(pal::deck[d].withAlpha(.8f)); g.fillRoundedRectangle(b.getX() + 10, b.getY() + 1, 40 * S, 2.5f * S, 1);
        // a platter well shadow
        auto pb = platter.getBounds().toFloat();
        g.setColour(Colours::black.withAlpha(.55f)); g.fillEllipse(pb.expanded(3).translated(0, 3));
        // the strobe lamp
        bool motor = ctx.proc.status(d).playing.load();
        auto lc = lampR.getCentre();
        float lr = lampR.getWidth() * .5f;
        g.setColour(Colour(0xff0e0f11)); g.fillEllipse(lampR.expanded(3));
        ColourGradient lg(Colour(motor ? 0xffffb347 : 0xff6a3a10), lc.x, lc.y, Colour(motor ? 0xffd2400a : 0xff2a1606), lc.x + lr, lc.y + lr, true);
        g.setGradientFill(lg); g.fillEllipse(lampR);
        // deck name + brand
        // pitch readout
        double pp = ctx.pitchPct(d);
        g.setColour(Colours::black); g.fillRoundedRectangle(pitchRead.reduced(1), 3);
        g.setColour(std::abs(pp) < .005 ? pal::green : pal::amber); g.setFont(mono(12 * S, true));
        g.drawText((pp >= 0 ? "+" : "") + String(pp, 1) + "%", pitchRead, Justification::centred);
        g.setColour(pal::faint); g.setFont(font(9 * S, true));
        g.drawText("PITCH", pitchF.getBounds().toFloat().withHeight(10 * S).translated(0, -1), Justification::centred);
    }
};

// ------------------------------------------------------------------------------------------------ mixer
struct MixerPanel : Component
{
    explicit MixerPanel(Ctx& c) : ctx(c),
        xf(c, c.par("xfader"), Fader::cross, pal::text),
        xfCurve(c, c.par("xfcurve"), "X CURVE", pal::text), fCurve(c, c.par("fadercurve"), "CH CURVE", pal::text),
        master(c, c.par("master"), "MASTER", pal::text), cueMix(c, c.par("cuemix"), "CUE MIX", pal::text, true),
        cueGain(c, c.par("cuegain"), "PHONES", pal::text),
        hamster(c, "HAMSTER", pal::amber, "hamster"),
        mMaster([&c] { return std::make_pair(c.proc.meter(2), c.proc.meter(3)); }, 20)
    {
        for (int d = 0; d < 2; ++d)
        {
            auto& s = ch[d];
            s.trim = std::make_unique<Knob>(c, c.dp(d, "trim"), "TRIM", pal::text, true);
            s.hi = std::make_unique<Knob>(c, c.dp(d, "hi"), "HI", pal::deck[d], true);
            s.mid = std::make_unique<Knob>(c, c.dp(d, "mid"), "MID", pal::deck[d], true);
            s.low = std::make_unique<Knob>(c, c.dp(d, "low"), "LOW", pal::deck[d], true);
            s.filter = std::make_unique<Knob>(c, c.dp(d, "filter"), "FILTER", pal::violet, true);
            for (auto* k : { s.hi.get(), s.mid.get(), s.low.get() }) k->showKill = true;
            s.fader = std::make_unique<Fader>(c, c.dp(d, "fader"), Fader::channel, pal::deck[d]);
            s.pfl = std::make_unique<PButton>(c, "CUE", pal::amber, deckParam(d, "pfl"));
            s.xfa = std::make_unique<PButton>(c, "A", pal::text, deckParam(d, "xfassign"));
            const int dd = d;
            s.pfl->lit = [&c, dd] { return Ctx::on(c.dp(dd, "pfl")); };
            s.pfl->press = [&c, dd](bool dn) { if (dn) Ctx::toggle(c.dp(dd, "pfl")); };
            s.xfa->dynText = [&c, dd] { int v = (int) std::round(Ctx::val(c.dp(dd, "xfassign"))); return v <= 0 ? String("XF A") : v == 1 ? String("THRU") : String("XF B"); };
            s.xfa->press = [&c, dd](bool dn) {
                if (!dn) return;
                auto* p = c.dp(dd, "xfassign");
                if (p == nullptr) return;
                int v = ((int) std::round(Ctx::val(p)) + 1) % 3;
                Ctx::setVal(p, (float) v);
            };
            s.meter = std::make_unique<Meter>([&c, dd] { const auto& st = c.proc.status(dd); float m = c.proc.meter(dd); float pk = jmax(st.peakL.load(), st.peakR.load(), 1.0e-6f);
                                                         float k = m > 0 ? m / pk : 1.0f; return std::make_pair(st.peakL.load() * k, st.peakR.load() * k); }, 20);
            for (Component* comp : std::initializer_list<Component*> { s.trim.get(), s.hi.get(), s.mid.get(), s.low.get(), s.filter.get(), s.fader.get(), s.pfl.get(), s.xfa.get(), s.meter.get() })
                addAndMakeVisible(comp);
        }
        for (Component* comp : std::initializer_list<Component*> { &xf, &xfCurve, &fCurve, &master, &cueMix, &cueGain, &hamster, &mMaster }) addAndMakeVisible(comp);
        hamster.lit = [&c] { return Ctx::on(c.par("hamster")); };
        hamster.press = [&c](bool dn) { if (dn) Ctx::toggle(c.par("hamster")); };
    }
    struct Strip { std::unique_ptr<Knob> trim, hi, mid, low, filter; std::unique_ptr<Fader> fader; std::unique_ptr<PButton> pfl, xfa; std::unique_ptr<Meter> meter; };
    Ctx& ctx;
    Strip ch[2];
    Fader xf;
    Knob xfCurve, fCurve, master, cueMix, cueGain;
    PButton hamster;
    Meter mMaster;
    Rectangle<float> faderWell[2], xfWell;
    void tick() { ch[0].meter->tick(); ch[1].meter->tick(); mMaster.tick(); }
    void resized() override
    {
        float S = ctx.S;
        auto b = getLocalBounds().toFloat().reduced(8 * S, 8 * S);
        b.removeFromTop(10 * S);   // the name plate
        auto xfRow = b.removeFromBottom(46 * S);
        float cw = b.getWidth() * .2f, sw = (b.getWidth() - cw) / 2;
        auto left = b.removeFromLeft(sw), right = b.removeFromRight(sw), centre = b;
        auto layoutStrip = [&](Strip& s, Rectangle<float> r, bool isLeft) {
            float k = jmin(r.getWidth() * .42f, 42 * S);
            auto row1 = r.removeFromTop(k + 2);
            s.trim->setBounds(row1.removeFromLeft(row1.getWidth() * .5f).withSizeKeepingCentre(k, k).toNearestInt());
            s.filter->setBounds(row1.withSizeKeepingCentre(k, k).toNearestInt());
            r.removeFromTop(2 * S);
            // a tall LED meter on the inner side, from the EQ down to the fader
            auto meterR = isLeft ? r.removeFromRight(13 * S) : r.removeFromLeft(13 * S);
            s.meter->setBounds(meterR.reduced(0, 2 * S).toNearestInt());
            auto eqCol = r.removeFromTop((k + 1) * 3);
            for (auto* kk : { s.hi.get(), s.mid.get(), s.low.get() }) kk->setBounds(eqCol.removeFromTop(k + 1).withSizeKeepingCentre(k, k).toNearestInt());
            r.removeFromTop(4 * S);
            auto btns = r.removeFromTop(22 * S);
            s.pfl->setBounds(btns.removeFromLeft(btns.getWidth() * .5f).reduced(2 * S, 0).toNearestInt());
            s.xfa->setBounds(btns.reduced(2 * S, 0).toNearestInt());
            s.xfa->fontScale = .82f;
            r.removeFromTop(4 * S);
            r.removeFromBottom(12 * S);   // the channel name under the fader
            s.fader->setBounds(r.withSizeKeepingCentre(jmin(r.getWidth(), 46 * S), r.getHeight()).toNearestInt());
        };
        layoutStrip(ch[0], left, true);
        layoutStrip(ch[1], right, false);
        float k = jmin(centre.getWidth() * .82f, 44 * S);
        master.setBounds(centre.removeFromTop(k + 2).withSizeKeepingCentre(k, k).toNearestInt());
        centre.removeFromTop(4 * S);
        auto bottom = centre.removeFromBottom((k + 2) * 2);
        mMaster.setBounds(centre.withSizeKeepingCentre(16 * S, centre.getHeight()).toNearestInt());
        cueMix.setBounds(bottom.removeFromTop(k + 2).withSizeKeepingCentre(k, k).toNearestInt());
        cueGain.setBounds(bottom.withSizeKeepingCentre(k, k).toNearestInt());
        // crossfader row
        xfRow.removeFromTop(4 * S);
        float kk = jmin(xfRow.getHeight(), 46 * S);
        fCurve.setBounds(xfRow.removeFromLeft(kk + 8 * S).withSizeKeepingCentre(kk, kk).toNearestInt());
        xfCurve.setBounds(xfRow.removeFromLeft(kk + 8 * S).withSizeKeepingCentre(kk, kk).toNearestInt());
        auto hm = xfRow.removeFromRight(70 * S);
        hamster.setBounds(hm.withSizeKeepingCentre(hm.getWidth() - 6 * S, 24 * S).toNearestInt());
        xf.setBounds(xfRow.reduced(8 * S, 4 * S).toNearestInt());
        xfWell = xf.getBounds().toFloat().expanded(4 * S, 2 * S);
        for (int d = 0; d < 2; ++d) faderWell[d] = ch[d].fader->getBounds().toFloat().expanded(2 * S, 2 * S);
    }
    void paint(Graphics& g) override
    {
        float S = ctx.S;
        auto b = getLocalBounds().toFloat().reduced(2);
        g.setColour(Colours::black.withAlpha(.6f)); g.fillRoundedRectangle(b.translated(0, 3), 6);
        ColourGradient pg(Colour(0xff23262b), 0, b.getY(), Colour(0xff15171a), 0, b.getBottom(), false);
        g.setGradientFill(pg); g.fillRoundedRectangle(b, 6);
        g.setColour(Colours::white.withAlpha(.08f)); g.drawRoundedRectangle(b.reduced(.5f), 6, 1);
        // screws
        for (auto p : { b.getTopLeft() + Point<float>(8, 8), b.getTopRight() + Point<float>(-8, 8), b.getBottomLeft() + Point<float>(8, -8), b.getBottomRight() + Point<float>(-8, -8) })
        {
            g.setColour(Colour(0xff0d0e10)); g.fillEllipse(p.x - 3.5f, p.y - 3.5f, 7, 7);
            g.setColour(Colour(0xff50555c)); g.drawLine(p.x - 2.5f, p.y, p.x + 2.5f, p.y, 1);
        }
        g.setColour(pal::dim); g.setFont(font(10.5f * S, true));
        g.drawText(U("OMEGA  \xc2\xb7  ROTARY / SCRATCH MIXER"), b.withHeight(22 * S).translated(0, 4 * S), Justification::centred);
        // channel strip separators and labels
        for (int d = 0; d < 2; ++d)
        {
            auto fb = ch[d].fader->getBounds().toFloat();
            g.setColour(pal::deck[d]); g.setFont(font(11 * S, true));
            g.drawText(d == 0 ? U("1  \xc2\xb7  A") : U("2  \xc2\xb7  B"), Rectangle<float>(fb.getX() - 20 * S, fb.getBottom(), fb.getWidth() + 40 * S, 12 * S), Justification::centred);
        }
        g.setColour(Colours::black.withAlpha(.35f)); g.fillRoundedRectangle(xfWell, 4);
        g.setColour(pal::dim); g.setFont(font(9.5f * S, true));
        g.drawText("A", xfWell.withWidth(12 * S).translated(-12 * S, 0), Justification::centred);
        g.drawText("B", xfWell.withX(xfWell.getRight()).withWidth(12 * S), Justification::centred);
        auto mm = mMaster.getBounds().toFloat();
        g.drawText("MST", mm.withY(mm.getBottom() + 1).withHeight(11 * S).expanded(14 * S, 0), Justification::centred);
    }
};

// ------------------------------------------------------------------------------------------------ pads / loops / FX
struct PadButton : Component
{
    PadButton(Ctx& c, int deck, int s) : ctx(c), d(deck), slot(s)
    {
        index = (slot / 2) * 4 + d * 2 + slot % 2;   // MPC layout: pad 1 bottom-left, rows of 4 going up
        ctx.reg(*this, "pad:" + String(index));
        setRepaintsOnMouseActivity(true);
    }
    Ctx& ctx;
    int d, slot, index;
    bool down = false;
    String label, sig;
    Colour col;
    bool lit = false;
    void poll()
    {
        auto mode = ctx.proc.padMode();
        String l = ctx.proc.padLabel(index);
        Colour c; bool on = false;
        const auto& st = ctx.proc.status(d);
        switch (mode)
        {
            case PadMode::cues: { const auto& hc = at(ctx.cues[d], slot); on = hc.set(); c = on ? cueColour(hc) : pal::faint; if (on && !hc.name.empty()) l = String::fromUTF8(hc.name.c_str()); break; }
            case PadMode::loops: c = pal::green; on = st.looping.load(); break;
            case PadMode::rolls: c = pal::cyan; on = st.rolling.load(); break;
            case PadMode::transport:
                c = slot == 0 ? pal::green : slot == 1 ? pal::amber : slot == 2 ? pal::cyan : slot == 3 ? pal::red : slot >= 6 ? pal::green : pal::text;
                on = (slot == 0 && st.playing.load()) || (slot == 2 && st.synced.load()) || (slot == 3 && st.censoring.load()) || (slot >= 6 && st.looping.load());
                break;
            case PadMode::count:
            default: c = pal::faint; break;
        }
        String s; s << l << c.toString() << (int) on << (int) mode;
        if (s != sig) { sig = s; label = l; col = c; lit = on; repaint(); }
    }
    void mouseDown(const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (ctx.proc.padMode() == PadMode::cues) ctx.send(Cmd::clearCue, d, 0, false, slot);
            return;
        }
        down = true; repaint();
        ctx.proc.pad(index, true, 1.0f);
    }
    void mouseUp(const MouseEvent&) override { if (down) { down = false; repaint(); ctx.proc.pad(index, false); } }
    void paint(Graphics& g) override
    {
        float S = ctx.S;
        auto r = getLocalBounds().toFloat().reduced(2.5f);
        bool hot = lit || down;
        // rubber MPC-style pad: dark, with an LED colour wash when lit
        g.setColour(Colours::black.withAlpha(.7f)); g.fillRoundedRectangle(r.translated(0, 1.5f), 5);
        ColourGradient pg(Colour(0xff2c3035), r.getX(), r.getY(), Colour(0xff1b1d21), r.getX(), r.getBottom(), false);
        if (hot)
        {
            auto c = down ? col.brighter(.4f) : col;
            pg = ColourGradient(c.withMultipliedBrightness(.95f), r.getCentreX(), r.getCentreY(), c.withMultipliedBrightness(.45f), r.getX(), r.getY(), true);
            glow(g, r, c, 5);
        }
        g.setGradientFill(pg); g.fillRoundedRectangle(r, 5);
        g.setColour(hot ? col.brighter(.5f).withAlpha(.9f) : (isMouseOver() ? col.withAlpha(.8f) : col.withAlpha(.45f)));
        g.drawRoundedRectangle(r, 5, hot ? 1.5f : 1.2f);
        g.setColour(hot ? Colours::black.withAlpha(.8f) : pal::text.withAlpha(.85f));
        g.setFont(font(jmin(12.5f * S, r.getHeight() * .32f), true));
        g.drawFittedText(label, r.reduced(4, 2).toNearestInt(), Justification::centred, 2, .7f);
        g.setFont(font(8.5f * S, true)); g.setColour(hot ? Colours::black.withAlpha(.55f) : pal::faint);
        g.drawText("P" + String(index + 1), r.reduced(4, 2), Justification::bottomRight);
    }
};

struct PadSection : Component
{
    PadSection(Ctx& c, int deck) : ctx(c), d(deck),
        loopIn(c, "IN", pal::green), loopOut(c, "OUT", pal::green), reloop(c, "RELOOP", pal::green), halve(c, U("\xc2\xbd"), pal::green), dbl(c, U("\xc3\x97" "2"), pal::green),
        fxOn(c, "FX ON", pal::red, deckParam(deck, "fxon")),
        fxType(c, c.dp(deck, "fxtype"), { "OFF", "ECHO", "PING PONG", "REVERB", "FLANGER", "PHASER", "CRUSH", "GATE" }, { 0, 1, 2, 3, 4, 5, 6, 7 }),
        fxBeats(c, c.dp(deck, "fxbeats"), { "1/16", "1/8", "1/4", "1/2", "3/4", "1", "2", "4", "8" }, { .0625f, .125f, .25f, .5f, .75f, 1, 2, 4, 8 }),
        depth(c, c.dp(deck, "fxdepth"), "DEPTH", pal::red), fxParam(c, c.dp(deck, "fxparam"), "PARAM", pal::red)
    {
        const char* names[] = { "HOT CUE", "AUTO LOOP", "ROLL", "TRANSPORT" };
        const Colour cols[] = { pal::red, pal::green, pal::cyan, pal::amber };
        for (int m = 0; m < 4; ++m)
        {
            auto b = std::make_unique<PButton>(c, names[m], cols[m], m == 0 ? String("padmode") : String());
            b->lit = [&c, m] { return (int) c.proc.padMode() == m; };
            b->press = [&c, m](bool dn) { if (dn) Ctx::setVal(c.par("padmode"), (float) m); };
            addAndMakeVisible(*b);
            modes.push_back(std::move(b));
        }
        for (int s = 0; s < 8; ++s) { pads.push_back(std::make_unique<PadButton>(c, d, s)); addAndMakeVisible(*pads.back()); }
        const auto& st = ctx.proc.status(d);
        loopIn.press = [this](bool dn) { if (dn) ctx.send(Cmd::loopIn, d); };
        loopOut.press = [this](bool dn) { if (dn) ctx.send(Cmd::loopOut, d); };
        reloop.press = [this, &st](bool dn) { if (dn) ctx.send(st.looping.load() ? Cmd::loopExit : Cmd::reloop, d); };
        reloop.lit = [&st] { return st.looping.load(); };
        reloop.dynText = [&st] { return st.looping.load() ? String("EXIT") : String("RELOOP"); };
        halve.press = [this](bool dn) { if (dn) ctx.send(Cmd::loopHalve, d); };
        dbl.press = [this](bool dn) { if (dn) ctx.send(Cmd::loopDouble, d); };
        loopIn.lit = [&st] { return st.loopA.load() >= 0 && st.looping.load(); };
        loopOut.lit = loopIn.lit;
        fxOn.solid = true;
        fxOn.lit = [this] { return Ctx::on(ctx.dp(d, "fxon")); };
        fxOn.press = [this](bool dn) { if (dn) Ctx::toggle(ctx.dp(d, "fxon")); };
        for (Component* comp : std::initializer_list<Component*> { &loopIn, &loopOut, &reloop, &halve, &dbl, &fxOn, &fxType, &fxBeats, &depth, &fxParam }) addAndMakeVisible(comp);
    }
    Ctx& ctx;
    int d;
    std::vector<std::unique_ptr<PButton>> modes;
    std::vector<std::unique_ptr<PadButton>> pads;
    PButton loopIn, loopOut, reloop, halve, dbl, fxOn;
    ParamCombo fxType, fxBeats;
    Knob depth, fxParam;
    Rectangle<float> loopBox, fxBox, padBox;
    void poll() { for (auto& p : pads) p->poll(); }
    void resized() override
    {
        float S = ctx.S;
        auto b = getLocalBounds().toFloat().reduced(6 * S, 4 * S);
        b.removeFromTop(12 * S);   // section labels
        float W = b.getWidth();
        auto modeCol = b.removeFromLeft(W * .14f);
        float mh = modeCol.getHeight() / 4;
        for (int m = 0; m < 4; ++m) modes[(size_t) m]->setBounds(modeCol.removeFromTop(mh).reduced(2 * S, 1.5f * S).toNearestInt());
        b.removeFromLeft(6 * S);
        auto fx = b.removeFromRight(W * .27f);
        b.removeFromRight(6 * S);
        auto lp = b.removeFromRight(W * .16f);
        b.removeFromRight(6 * S);
        padBox = b;
        // 2 rows of 4 like Serato's pads: slots 1-4 on top
        float pw = b.getWidth() / 4, ph = b.getHeight() / 2;
        for (int s = 0; s < 8; ++s)
        {
            int row = s / 4, colm = s % 4;
            pads[(size_t) s]->setBounds(Rectangle<float>(b.getX() + (float) colm * pw, b.getY() + (float) row * ph, pw, ph).toNearestInt());
        }
        loopBox = lp;
        float lh = lp.getHeight() / 3, lw = lp.getWidth() / 2;
        loopIn.setBounds(Rectangle<float>(lp.getX(), lp.getY(), lw, lh).reduced(1.5f * S).toNearestInt());
        loopOut.setBounds(Rectangle<float>(lp.getX() + lw, lp.getY(), lw, lh).reduced(1.5f * S).toNearestInt());
        halve.setBounds(Rectangle<float>(lp.getX(), lp.getY() + lh, lw, lh).reduced(1.5f * S).toNearestInt());
        dbl.setBounds(Rectangle<float>(lp.getX() + lw, lp.getY() + lh, lw, lh).reduced(1.5f * S).toNearestInt());
        reloop.setBounds(Rectangle<float>(lp.getX(), lp.getY() + 2 * lh, lw * 2, lh).reduced(1.5f * S).toNearestInt());
        fxBox = fx;
        auto top = fx.removeFromTop(fx.getHeight() * .3f);
        fxType.setBounds(top.removeFromLeft(top.getWidth() * .6f).reduced(1.5f * S).toNearestInt());
        fxBeats.setBounds(top.reduced(1.5f * S).toNearestInt());
        auto onR = fx.removeFromRight(fx.getWidth() * .3f);
        fxOn.setBounds(onR.withSizeKeepingCentre(onR.getWidth() - 2 * S, jmin(onR.getHeight() * .6f, 30 * S)).toNearestInt());
        depth.setBounds(fx.removeFromLeft(fx.getWidth() * .5f).reduced(1 * S).toNearestInt());
        fxParam.setBounds(fx.reduced(1 * S).toNearestInt());
    }
    void paint(Graphics& g) override
    {
        float S = ctx.S;
        auto b = getLocalBounds().toFloat().reduced(2);
        ColourGradient pg(Colour(0xff1d2024), 0, b.getY(), Colour(0xff131518), 0, b.getBottom(), false);
        g.setGradientFill(pg); g.fillRoundedRectangle(b, 6);
        g.setColour(Colours::white.withAlpha(.07f)); g.drawRoundedRectangle(b.reduced(.5f), 6, 1);
        g.setFont(font(9.5f * S, true)); g.setColour(pal::faint);
        float y = b.getY() + 3 * S;
        auto lab = [&](Rectangle<float> r, const String& t) { g.drawText(t, Rectangle<float>(r.getX(), y, r.getWidth(), 11 * S), Justification::centredLeft); };
        lab(modes[0]->getBounds().toFloat(), "PAD MODE");
        lab(padBox, d == 0 ? U("PADS  \xc2\xb7  MPC 1-2 / 5-6 / 9-10 / 13-14") : U("PADS  \xc2\xb7  MPC 3-4 / 7-8 / 11-12 / 15-16"));
        lab(loopBox, "LOOP");
        lab(fxBox, "BEAT FX");
    }
};

// ------------------------------------------------------------------------------------------------ library
struct LibraryPanel : Component, ListBoxModel, KeyListener
{
    // the track table's model (a separate object: ListBoxModel and TableListBoxModel share getNumRows())
    struct Tracks : TableListBoxModel
    {
        explicit Tracks(LibraryPanel& o) : p(o) {}
        LibraryPanel& p;
        int getNumRows() override { return p.entries.size(); }
        void paintRowBackground(Graphics& g, int r, int w, int h, bool s) override { p.paintTrackRow(g, r, w, h, s); }
        void paintCell(Graphics& g, int r, int c, int w, int h, bool s) override { p.paintTrackCell(g, r, c, w, h, s); }
        void cellDoubleClicked(int r, int, const MouseEvent&) override { p.table.selectRow(r); p.loadToFreeDeck(); }
        void returnKeyPressed(int) override { p.loadToFreeDeck(); }
        void sortOrderChanged(int c, bool f) override { p.sortCol = c; p.sortFwd = f; p.applySort(); p.table.updateContent(); p.table.repaint(); }
    };
    using Component::keyPressed;
    explicit LibraryPanel(Ctx& c) : ctx(c), tracks(*this), pool(ThreadPoolOptions{}.withNumberOfThreads(1).withThreadName("wheel library"))
    {
        chooseBtn.setButtonText(U("Choose SD card / folder\xe2\x80\xa6"));
        chooseBtn.onClick = [this] { choose(); };
        loadA.setButtonText("LOAD A"); loadB.setButtonText("LOAD B");
        loadA.setTooltip("Load the selected track on deck A (Shift+Left)"); loadB.setTooltip("Load the selected track on deck B (Shift+Right)");
        loadA.onClick = [this] { loadSelected(0); };
        loadB.onClick = [this] { loadSelected(1); };
        search.setTextToShowWhenEmpty("Search title / artist / album", pal::faint);
        search.onTextChange = [this] { refreshTracks(); };
        search.onEscapeKey = [this] { search.setText({}, true); };
        search.addKeyListener(this);
        search.setFont(font(14));
        crates.setModel(this);
        crates.setRowHeight(22);
        crates.addKeyListener(this);
        table.setModel(&tracks);
        table.setRowHeight(22);
        table.addKeyListener(this);
        table.setMultipleSelectionEnabled(false);
        auto& h = table.getHeader();
        h.addColumn("#", 1, 36, 30, 60, TableHeaderComponent::notSortable);
        h.addColumn("Title", 2, 320, 80, 2000);
        h.addColumn("Artist", 3, 220, 60, 1000);
        h.addColumn("BPM", 4, 60, 40, 90);
        h.addColumn("Key", 5, 66, 40, 100);
        h.addColumn("Length", 6, 66, 40, 100);
        h.addColumn(U("\xe2\x97\x86"), 7, 28, 24, 40);
        h.setStretchToFitActive(true);
        h.setPopupMenuActive(false);
        for (Component* comp : std::initializer_list<Component*> { &chooseBtn, &loadA, &loadB, &search, &crates, &table }) addAndMakeVisible(comp);
        // what the processor already knows (state restore) or the last folder
        crateList = ctx.proc.library.crates();
        if (crateList.isEmpty() && ctx.proc.lastRoot.isDirectory()) setRoot(ctx.proc.lastRoot);
        else if (!crateList.isEmpty()) { crates.updateContent(); crates.selectRow(0); }
    }
    ~LibraryPanel() override { pool.removeAllJobs(true, 4000); }
    Ctx& ctx;
    Tracks tracks;
    ThreadPool pool;
    std::unique_ptr<FileChooser> chooser;
    TextButton chooseBtn, loadA, loadB;
    TextEditor search;
    ListBox crates { "crates" };
    TableListBox table { "tracks" };
    Array<Crate> crateList;
    Array<Entry> entries;
    int gen = 0;
    bool busy = false;
    String status;
    int sortCol = 0; bool sortFwd = true;

    void choose()
    {
        auto start = ctx.proc.lastRoot.isDirectory() ? ctx.proc.lastRoot : File::getSpecialLocation(File::userMusicDirectory);
        chooser = std::make_unique<FileChooser>("Choose the SD card, drive or music folder", start);
        Component::SafePointer<LibraryPanel> safe(this);
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [safe](const FileChooser& fc) {
            auto dir = fc.getResult();
            if (safe != nullptr && dir.isDirectory()) safe->setRoot(dir);
        });
    }
    void setRoot(const File& dir)
    {
        ctx.proc.lastRoot = dir;
        busy = true; status = "Scanning " + dir.getFullPathName() + U("\xe2\x80\xa6"); repaint();
        int g = ++gen;
        Component::SafePointer<LibraryPanel> safe(this);
        auto* lib = &ctx.proc.library;
        pool.addJob([safe, lib, dir, g] {
            lib->setRoot(dir);
            MessageManager::callAsync([safe, g] {
                if (safe == nullptr || g != safe->gen) return;
                safe->crateList = safe->ctx.proc.library.crates();
                safe->crates.updateContent(); safe->crates.repaint();
                safe->busy = false; safe->status = {};
                if (!safe->crateList.isEmpty()) safe->crates.selectRow(0);
                else { safe->entries.clear(); safe->table.updateContent(); safe->status = "No crates or audio found there"; }
                safe->repaint();
            });
        });
    }
    void refreshTracks()
    {
        int row = crates.getSelectedRow();
        if (row < 0 || row >= crateList.size()) { entries.clear(); table.updateContent(); table.repaint(); return; }
        auto crate = crateList[row];
        auto text = search.getText().trim();
        int g = ++gen;
        busy = true; status = "Reading " + crate.name + U("\xe2\x80\xa6"); repaint();
        Component::SafePointer<LibraryPanel> safe(this);
        auto* lib = &ctx.proc.library;
        pool.addJob([safe, lib, crate, text, g] {
            auto list = text.isEmpty() ? lib->list(crate) : lib->search(text, crate);
            MessageManager::callAsync([safe, g, list] {
                if (safe == nullptr || g != safe->gen) return;
                safe->entries = list; safe->applySort();
                safe->busy = false; safe->status = {};
                safe->table.updateContent(); safe->table.repaint();
                if (!list.isEmpty()) safe->table.selectRow(0);
                safe->repaint();
            });
        });
    }
    void applySort()
    {
        if (sortCol <= 1) return;
        auto key = [this](const Entry& e) -> String {
            switch (sortCol) { case 2: return e.title.toLowerCase(); case 3: return e.artist.toLowerCase(); case 4: return String(e.bpm, 2).paddedLeft('0', 8);
                               case 5: return e.keyText; case 6: return String(e.length, 1).paddedLeft('0', 9); case 7: return e.serato ? "1" : "0"; default: return {}; }
        };
        std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) { return sortFwd ? key(a) < key(b) : key(b) < key(a); });
    }
    File selectedFile() const
    {
        int r = table.getSelectedRow();
        return r >= 0 && r < entries.size() ? entries.getReference(r).file : File();
    }
    void loadSelected(int d) { auto f = selectedFile(); if (f.existsAsFile()) ctx.proc.loadTrack(d, f); }
    void loadToFreeDeck()
    {
        // double-click: the deck that is not playing (A if both are idle)
        bool aPlaying = ctx.proc.status(0).playing.load(), bPlaying = ctx.proc.status(1).playing.load();
        loadSelected(aPlaying && !bPlaying ? 1 : 0);
    }
    bool keyPressed(const KeyPress& k, Component*) override
    {
        if (k.getModifiers().isShiftDown() && k.getKeyCode() == KeyPress::leftKey) { loadSelected(0); return true; }
        if (k.getModifiers().isShiftDown() && k.getKeyCode() == KeyPress::rightKey) { loadSelected(1); return true; }
        if (k.getKeyCode() == KeyPress::upKey || k.getKeyCode() == KeyPress::downKey)
        {
            int n = entries.size();
            if (n == 0) return true;
            int r = jlimit(0, n - 1, table.getSelectedRow() + (k.getKeyCode() == KeyPress::upKey ? -1 : 1));
            table.selectRow(r); table.scrollToEnsureRowIsOnscreen(r);
            return true;
        }
        if (k.getKeyCode() == KeyPress::returnKey && table.getSelectedRow() >= 0) { loadToFreeDeck(); return true; }
        return false;
    }
    // crates
    int getNumRows() override { return crateList.size(); }
    void paintListBoxItem(int row, Graphics& g, int w, int h, bool sel) override
    {
        if (row < 0 || row >= crateList.size()) return;
        const auto& c = crateList.getReference(row);
        if (sel) { g.setColour(pal::deck[0].withAlpha(.28f)); g.fillRect(0, 0, w, h); g.setColour(pal::deck[0]); g.fillRect(0, 0, 3, h); }
        float x = 8.0f + (float) c.depth * 14.0f;
        String icon = c.kind == Crate::all ? U("\xe2\x99\xab") : c.kind == Crate::serato ? U("\xe2\x97\x86") : c.kind == Crate::playlist ? U("\xe2\x89\xa1") : c.kind == Crate::smart ? U("\xe2\x97\x87") : U("\xe2\x96\xb8");
        g.setColour(c.kind == Crate::serato || c.kind == Crate::smart ? pal::amber : pal::dim); g.setFont(font(12, true));
        g.drawText(icon, (int) x, 0, 14, h, Justification::centred);
        g.setColour(sel ? pal::text : pal::text.withAlpha(.85f)); g.setFont(font(13, c.kind == Crate::all));
        String name = c.name.fromLastOccurrenceOf(" / ", false, false);
        if (name.isEmpty()) name = c.name;
        g.drawText(name, (int) x + 18, 0, w - (int) x - 20, h, Justification::centredLeft, true);
    }
    void selectedRowsChanged(int) override { refreshTracks(); }
    // tracks
    void paintTrackRow(Graphics& g, int row, int w, int h, bool sel)
    {
        g.setColour(row % 2 ? Colour(0xff0e1013) : Colour(0xff0b0c0e)); g.fillRect(0, 0, w, h);
        if (row >= 0 && row < entries.size())
        {
            const auto& f = entries.getReference(row).file;
            for (int d = 0; d < 2; ++d)
                if (ctx.trk[d] && f.getFullPathName() == String::fromUTF8(ctx.trk[d]->path.c_str())) { g.setColour(pal::deck[d].withAlpha(.12f)); g.fillRect(0, 0, w, h); }
        }
        if (sel) { g.setColour(Colour(0xff2a3a4d)); g.fillRect(0, 0, w, h); g.setColour(pal::deck[0]); g.fillRect(0, 0, 3, h); }
    }
    void paintTrackCell(Graphics& g, int row, int col, int w, int h, bool sel)
    {
        if (row < 0 || row >= entries.size()) return;
        const auto& e = entries.getReference(row);
        String t; Colour c = sel ? pal::text : pal::text.withAlpha(.82f);
        Justification j = Justification::centredLeft;
        auto f = font(13);
        switch (col)
        {
            case 1:
            {
                t = String(row + 1); c = pal::faint; j = Justification::centredRight; f = mono(11.5f);
                for (int d = 0; d < 2; ++d)
                    if (ctx.trk[d] && e.file.getFullPathName() == String::fromUTF8(ctx.trk[d]->path.c_str())) { t = d == 0 ? "A" : "B"; c = pal::deck[d]; f = font(12, true); }
                break;
            }
            case 2: t = e.title.isNotEmpty() ? e.title : e.file.getFileNameWithoutExtension(); f = font(13, true); break;
            case 3: t = e.artist; c = sel ? pal::text : pal::dim; break;
            case 4: t = e.bpm > 0 ? String(e.bpm, 1) : String("-"); j = Justification::centredRight; f = mono(12); break;
            case 5:
            {
                int k = parseKey(e.keyText.toStdString());
                t = k >= 0 ? String(camelot(k)) + " " + String(keyName(k)) : e.keyText;
                c = k >= 0 ? keyColour(k) : pal::faint;
                f = font(12, true);
                break;
            }
            case 6: t = e.length > 0 ? fmtTime(e.length, false) : String("-"); j = Justification::centredRight; f = mono(12); break;
            case 7: t = e.serato ? U("\xe2\x97\x86") : ""; c = pal::amber; j = Justification::centred; break;
            default: break;
        }
        g.setColour(c); g.setFont(f);
        g.drawText(t, 6, 0, w - 10, h, j, true);
    }
    void resized() override
    {
        float S = ctx.S;
        auto b = getLocalBounds().reduced(6);
        auto bar = b.removeFromTop(jmax(24, (int) (26 * S)));
        chooseBtn.setBounds(bar.removeFromLeft((int) (200 * S)));
        bar.removeFromLeft(8);
        loadB.setBounds(bar.removeFromRight((int) (96 * S))); bar.removeFromRight(4);
        loadA.setBounds(bar.removeFromRight((int) (96 * S))); bar.removeFromRight(10);
        search.setBounds(bar.removeFromRight(jmin(bar.getWidth() / 2, (int) (320 * S))));
        rootArea = bar.toFloat();
        b.removeFromTop(5);
        crates.setBounds(b.removeFromLeft((int) (b.getWidth() * .22f)));
        b.removeFromLeft(5);
        table.setBounds(b);
        int rh = jmax(18, (int) (21 * S));
        crates.setRowHeight(rh); table.setRowHeight(rh);
        table.getHeader().setSize(table.getWidth(), rh + 2);
    }
    Rectangle<float> rootArea;
    void paint(Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(1);
        g.setColour(pal::panel); g.fillRoundedRectangle(b, 6);
        g.setColour(Colours::white.withAlpha(.06f)); g.drawRoundedRectangle(b.reduced(.5f), 6, 1);
        g.setFont(font(12 * ctx.S)); g.setColour(busy ? pal::amber : pal::dim);
        String s = status.isNotEmpty() ? status : (ctx.proc.library.root().isDirectory() ? ctx.proc.library.root().getFullPathName() + U("   \xc2\xb7   ") + String(entries.size()) + " tracks" : String("No library yet: pick your SD card, USB drive or music folder"));
        g.drawText(s, rootArea, Justification::centredLeft, true);
    }
};

// ------------------------------------------------------------------------------------------------ MIDI / MPC
struct MidiPanel : Component, Timer
{
    MidiPanel(Ctx& c) : ctx(c)
    {
        learnBtn.setButtonText("LEARN"); learnBtn.setClickingTogglesState(false);
        learnBtn.onClick = [this] { ctx.learnMode = !ctx.learnMode; if (ctx.learnChanged) ctx.learnChanged(); };
        clearBtn.setButtonText("CLEAR ALL MAPPINGS");
        clearBtn.onClick = [this] { ctx.proc.clearMappings(); refresh(); };
        closeBtn.setButtonText("CLOSE");
        closeBtn.onClick = [this] { setVisible(false); ctx.learnMode = false; if (ctx.learnChanged) ctx.learnChanged(); };
        map.setMultiLine(true); map.setReadOnly(true); map.setScrollbarsShown(true); map.setCaretVisible(false);
        map.setFont(mono(12.5f));
        help.setMultiLine(true); help.setReadOnly(true); help.setCaretVisible(false); help.setScrollbarsShown(true);
        help.setFont(font(13));
        help.setColour(TextEditor::backgroundColourId, Colours::transparentBlack);
        help.setColour(TextEditor::outlineColourId, Colours::transparentBlack);
        help.setText(
            U("PLAYING IT FROM AN MPC\n"
            "\xe2\x80\xa2 Load Omega Wheel of Steel as a plugin instrument on a track and arm it (MIDI to the plugin).\n"
            "\xe2\x80\xa2 PADS: the 16 pads are the performance pads. Left two columns = deck A, right two = deck B "
            "(pad 1 bottom-left). Pad base sets the note of MPC pad 1 (37 by default; 36 for chromatic C1). "
            "Bank B pads 1-4 switch the mode: Hot Cue / Auto Loop / Roll / Transport. The pads on screen show their MPC pad number (P1-P16).\n"
            "\xe2\x80\xa2 Q-LINKS: in the plugin's Q-Link / parameter mapping, pick parameters by name: "
            "\"Crossfader\", \"Deck A Channel Fader\", \"Deck B Channel Fader\", \"Deck A Platter (Scratch)\" / \"Deck B Platter (Scratch)\" "
            "(turn the Q-Link to scratch: it moves the record like your hand), \"Deck A Pitch\", \"Deck A Filter\", \"Deck A EQ High/Mid/Low\", "
            "\"Deck A Play\", \"Deck A Cue\", \"Pad Mode\" \xe2\x80\xa6 "
            "Every control on this screen is a parameter.\n"
            "\xe2\x80\xa2 FADER = CROSSFADER out of the box: the touch strip (pitch bend) or any fader sending CC 1, on any channel. "
            "The parameters are listed crossfader first, then the channel faders and the platters, so Q-Links assigned in order land there.\n"
            "\xe2\x80\xa2 A good scratch setup: touch strip = Crossfader, Q-Link 1 = Deck A Platter, Q-Link 2 = Deck A Fader, "
            "Q-Link 3 = Deck A Filter; pads in Hot Cue mode.\n"
            "\xe2\x80\xa2 MIDI channel 2 notes play deck A's 8 slots, channel 3 deck B's (note mod 8).\n\n"
            "MIDI LEARN (any controller)\n"
            "Press LEARN, click a control or pad on the screen, then move the knob / hit the pad. Learned controls override the defaults "
            "and are saved with the project (CLEAR ALL goes back to the defaults). Esc or LEARN again to finish."));
        for (Component* comp : std::initializer_list<Component*> { &learnBtn, &clearBtn, &closeBtn, &map, &help }) addAndMakeVisible(comp);
        startTimerHz(4);
    }
    Ctx& ctx;
    TextButton learnBtn, clearBtn, closeBtn;
    TextEditor map, help;
    void refresh()
    {
        auto s = ctx.proc.mappingSummary();
        if (s.isEmpty()) s = "(no learned mappings yet: the MPC pads and host parameters work without any)";
        if (s != map.getText()) map.setText(s, false);
        learnBtn.setColour(TextButton::buttonColourId, ctx.learnMode ? pal::amber : pal::raised);
        learnBtn.setColour(TextButton::textColourOffId, ctx.learnMode ? Colours::black : pal::text);
    }
    void timerCallback() override { if (isVisible()) refresh(); }
    void visibilityChanged() override { if (isVisible()) refresh(); }
    void mouseDown(const MouseEvent&) override {}
    void resized() override
    {
        auto b = getLocalBounds().reduced(16);
        b.removeFromTop(30);
        auto bar = b.removeFromBottom(30);
        closeBtn.setBounds(bar.removeFromRight(90)); bar.removeFromRight(8);
        learnBtn.setBounds(bar.removeFromLeft(110)); bar.removeFromLeft(8);
        clearBtn.setBounds(bar.removeFromLeft(180));
        b.removeFromBottom(10);
        auto top = b.removeFromTop(b.getHeight() / 3);
        b.removeFromTop(18);
        map.setBounds(top.withTrimmedTop(18));
        help.setBounds(b);
    }
    void paint(Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(Colours::black.withAlpha(.5f)); g.fillRoundedRectangle(b.translated(0, 6), 10);
        g.setColour(pal::panel2); g.fillRoundedRectangle(b, 10);
        g.setColour(pal::edgeHi); g.drawRoundedRectangle(b.reduced(.5f), 10, 1);
        g.setColour(pal::text); g.setFont(font(17, true));
        g.drawText("MIDI / MPC", 16, 12, 300, 24, Justification::centredLeft);
        g.setColour(pal::dim); g.setFont(font(11.5f, true));
        g.drawText("LEARNED MAPPINGS", map.getX(), map.getY() - 17, 300, 14, Justification::centredLeft);
        g.drawText("HELP", help.getX(), help.getY() - 17, 300, 14, Justification::centredLeft);
        auto l = ctx.proc.learning();
        if (l.isNotEmpty()) { g.setColour(pal::amber); g.drawText("waiting for MIDI: " + l, getWidth() - 360, 12, 344, 24, Justification::centredRight); }
    }
};

// in learn mode: click any control to bind the next MIDI message to it
struct LearnOverlay : Component
{
    LearnOverlay(Ctx& c) : ctx(c) { setWantsKeyboardFocus(true); }
    Ctx& ctx;
    std::function<bool(Point<int>)> passThrough;   // the MIDI panel and the learn button stay clickable
    Component* hover = nullptr;
    String lastTarget;
    bool hitTest(int x, int y) override { return !(passThrough && passThrough({ x, y })); }
    Component* targetAt(Point<int> p, String& pid)
    {
        Component* best = nullptr; int64 bestArea = std::numeric_limits<int64>::max();
        for (auto& [comp, id] : ctx.learnables)
        {
            if (comp == nullptr || !comp->isShowing()) continue;
            auto r = getLocalArea(comp.getComponent(), comp->getLocalBounds());
            if (!r.contains(p)) continue;
            int64 a = (int64) r.getWidth() * r.getHeight();
            if (a < bestArea) { bestArea = a; best = comp.getComponent(); pid = id; }
        }
        return best;
    }
    void mouseMove(const MouseEvent& e) override { String pid; auto* t = targetAt(e.getPosition(), pid); if (t != hover) { hover = t; repaint(); } }
    void mouseDown(const MouseEvent& e) override
    {
        String pid;
        if (targetAt(e.getPosition(), pid) != nullptr) { ctx.proc.learn(pid); lastTarget = pid; repaint(); }
    }
    bool keyPressed(const KeyPress& k) override
    {
        if (k == KeyPress::escapeKey) { ctx.learnMode = false; if (ctx.learnChanged) ctx.learnChanged(); return true; }
        return false;
    }
    void paint(Graphics& g) override
    {
        g.fillAll(Colours::black.withAlpha(.28f));
        auto waiting = ctx.proc.learning();
        for (auto& [comp, id] : ctx.learnables)
        {
            if (comp == nullptr || !comp->isShowing()) continue;
            auto r = getLocalArea(comp.getComponent(), comp->getLocalBounds()).toFloat().reduced(1);
            bool isT = id == waiting && waiting.isNotEmpty();
            g.setColour(isT ? pal::amber : comp.getComponent() == hover ? pal::text : pal::amber.withAlpha(.35f));
            g.drawRoundedRectangle(r, 4, isT ? 2.5f : 1.2f);
            if (isT) { g.setColour(pal::amber.withAlpha(.18f)); g.fillRoundedRectangle(r, 4); }
        }
        auto bar = getLocalBounds().removeFromBottom(34).toFloat();
        g.setColour(pal::amber); g.fillRect(bar);
        g.setColour(Colours::black); g.setFont(font(14, true));
        g.drawText(waiting.isNotEmpty() ? "MIDI LEARN: now move a knob / hit a pad on your controller for  " + waiting + "   (Esc to finish)"
                                        : "MIDI LEARN: click a control, pad or platter, then move / hit the control on your MPC or controller   (Esc to finish)",
                   bar.reduced(12, 0), Justification::centred, true);
    }
};

// ------------------------------------------------------------------------------------------------ MPC screen
// The 320 x 240 view for the MPC's screen: everything sits inside a 12 px safe margin (the MPC's own menus can cover
// the borders), in bold blocks and heavy text: no hairlines, no small print. Two deck headers, two waveform strips,
// two platters with PLAY / CUE, a centre strip (pad mode, crossfader, browse, full view), 8 pads per deck, and a
// full-screen crate browser. Laid out at 320 x 240 and scaled if the host gives it more room.
struct MpcScreen : Component, Timer
{
    static constexpr int W = 320, H = 240, M = 12;
    MpcScreen(Ctx& c, LibraryPanel& l) : ctx(c), lib(l) { setOpaque(true); startTimerHz(30); }
    Ctx& ctx;
    LibraryPanel& lib;
    std::function<void()> onFull;
    bool browsing = false, inCrate = false;
    int scroll = 0, sel = -1;
    // gestures
    enum Grab { none, platterA, platterB, xfade, listDrag };
    Grab grab = none; double lastAng = 0; float grabY = 0, grabScroll = 0; bool moved = false;
    int padDown = -1;
    double spin[2] { 0, 0 };

    // ---- colours: pure black, white and two loud deck colours
    static Colour ink() { return Colours::white; }
    static Colour deckCol(int d) { return d == 0 ? Colour(0xff26a6ff) : Colour(0xffff8a1a); }
    static Colour grey() { return Colour(0xff3a3f46); }
    // the font is built into the plugin (DejaVu Sans Bold), so the screen looks the same on a box with no fonts installed
    static Typeface::Ptr face()
    {
        static Typeface::Ptr tf = Typeface::createSystemTypefaceFor(WheelFonts::DejaVuSansBold_ttf, (size_t) WheelFonts::DejaVuSansBold_ttfSize);
        return tf;
    }
    static Font bold(float h) { auto tf = face(); return tf != nullptr ? Font(FontOptions(tf).withHeight(h)) : Font(FontOptions(h, Font::bold)); }

    // ---- layout (logical 320 x 240 coordinates)
    static Rectangle<float> content() { return { (float) M, (float) M, (float) (W - 2 * M), (float) (H - 2 * M) }; }
    static Rectangle<float> header(int d) { return { (float) (d == 0 ? M : W / 2 + 4), (float) M, 144, 36 }; }
    static Rectangle<float> strip(int d) { return { (float) M, (float) (M + 40 + d * 24), (float) (W - 2 * M), 20 }; }
    static Rectangle<float> platter(int d) { return { d == 0 ? (float) M : (float) (W - M - 64), 100, 64, 64 }; }
    static Rectangle<float> playBtn(int d) { return { d == 0 ? (float) (M + 68) : (float) (W - M - 68 - 40), 100, 40, 30 }; }
    static Rectangle<float> cueBtn(int d) { return { d == 0 ? (float) (M + 68) : (float) (W - M - 68 - 40), 134, 40, 30 }; }
    static Rectangle<float> centre() { float x = (float) (M + 112), w = (float) (W - 2 * M - 224); return { x, 100, w, 64 }; }
    static Rectangle<float> modeBtn() { return centre().withHeight(20); }
    static Rectangle<float> xfader() { return centre().withY(122).withHeight(20); }
    static Rectangle<float> browseBtn() { auto c = centre(); return { c.getX(), 144, c.getWidth() / 2 - 2, 20 }; }
    static Rectangle<float> fullBtn() { auto c = centre(); return { c.getCentreX() + 2, 144, c.getWidth() / 2 - 2, 20 }; }
    static Rectangle<float> padRect(int d, int slot)
    {
        // 4 x 2 per deck: bottom row slots 1-4, top row 5-8 (like the pads under your fingers)
        int col = slot % 4, row = slot / 4;
        float x0 = d == 0 ? (float) M : (float) (W / 2 + 4), pw = (144 - 3 * 4) / 4.0f;
        return { x0 + (float) col * (pw + 4), (float) (H - M - 24 - row * 28), pw, 24 };
    }
    // deck slot (0..7) -> MPC pad index (0..15): left two columns deck A, right two deck B, rows bottom to top
    static int padIndex(int d, int slot) { return (slot / 2) * 4 + slot % 2 + (d == 1 ? 2 : 0); }

    Point<float> logical(Point<float> p) const { float s = (float) getWidth() / W; return p / jmax(.01f, s); }

    void timerCallback() override
    {
        for (int d = 0; d < 2; ++d) spin[d] = ctx.proc.status(d).angle.load();
        repaint();
    }

    // ---- drawing helpers
    static void block(Graphics& g, Rectangle<float> r, Colour fill, Colour border, float rad = 4)
    {
        g.setColour(fill); g.fillRoundedRectangle(r, rad);
        if (border.getAlpha() > 0) { g.setColour(border); g.drawRoundedRectangle(r.reduced(1), rad, 2); }
    }
    static void label(Graphics& g, const String& s, Rectangle<float> r, Colour c, float h, Justification j = Justification::centred)
    {
        g.setColour(c); g.setFont(bold(h)); g.drawFittedText(s, r.toNearestInt(), j, 1, .8f);
    }
    static String shortPad(const String& l)
    {
        if (l.startsWith("CUE ")) return l.fromFirstOccurrenceOf(" ", false, false);
        if (l.startsWith("LOOP ") && l != "LOOP IN" && l != "LOOP OUT") return l.fromFirstOccurrenceOf(" ", false, false);
        if (l.startsWith("ROLL ")) return l.fromFirstOccurrenceOf(" ", false, false);
        if (l == "PLAY") return U("\xe2\x96\xb6");
        if (l == "CENSOR") return "CENS";
        if (l == "NUDGE -") return "N-";
        if (l == "NUDGE +") return "N+";
        if (l == "LOOP IN") return "IN";
        if (l == "LOOP OUT") return "OUT";
        return l;
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colours::black);
        float s = (float) getWidth() / W;
        g.addTransform(AffineTransform::scale(s));
        auto hw = ctx.proc.hwBrowser();
        if (hw.mode != 0) { paintHwBrowser(g, hw); return; }
        if (browsing) { paintBrowser(g); return; }
        for (int d = 0; d < 2; ++d) { paintHeader(g, d); paintStrip(g, d); paintDeck(g, d); paintPads(g, d); }
        paintCentre(g);
    }

    void paintHeader(Graphics& g, int d)
    {
        auto r = header(d);
        const auto& st = ctx.proc.status(d);
        auto t = ctx.trk[d];
        bool playing = st.playing.load();
        // the deck the MPC Sample's buttons and knobs work on: a solid white bar under its header
        const bool focused = ctx.proc.mpcSampleActive() && ctx.proc.focusDeck() == d;
        block(g, r, Colour(0xff15181c), playing ? deckCol(d) : grey());
        if (focused) { g.setColour(ink()); g.fillRect(r.getX() + 2, r.getBottom() - 3, r.getWidth() - 4, 3.0f); }
        auto tag = r.removeFromLeft(20);
        g.setColour(focused ? ink() : deckCol(d)); g.fillRoundedRectangle(tag, 4);
        label(g, d == 0 ? "A" : "B", tag, Colours::black, 17);
        r = r.reduced(4, 2);
        String title = ctx.proc.loading[d].load() ? String("LOADING") : t ? String::fromUTF8(t->title.c_str()) : String("NO TRACK");
        if (t && title.isEmpty()) title = File(String::fromUTF8(t->path.c_str())).getFileNameWithoutExtension();
        label(g, title.toUpperCase(), r.removeFromTop(16), ink(), 13, Justification::centredLeft);
        auto row = r;
        double bpm = st.bpmNow.load();
        label(g, bpm > 0 ? String(bpm, 1) : String("---"), row.removeFromLeft(48), deckCol(d), 15, Justification::centredLeft);
        double rem = t ? t->length() - st.pos.load() : 0;
        bool late = t && rem < 30;
        label(g, t ? fmtTime(-rem, false) : String("--:--"), row, late ? Colour(0xffff3b30) : ink(), 15, Justification::centredRight);
    }

    void paintStrip(Graphics& g, int d)
    {
        auto r = strip(d);
        g.setColour(Colour(0xff101214)); g.fillRoundedRectangle(r, 3);
        auto t = ctx.trk[d];
        const auto& wc = ctx.wave[d];
        if (!t || wc.n == 0) { label(g, d == 0 ? "DECK A" : "DECK B", r, grey(), 12); return; }
        const double pos = ctx.proc.status(d).pos.load(), span = 5.0;   // seconds across the strip
        const float cx = r.getCentreX(), cy = r.getCentreY(), half = r.getHeight() / 2 - 1;
        const auto col = deckCol(d);
        // bold 2 px columns, deck colour, brighter after the playhead is passed
        for (float x = r.getX(); x < r.getRight(); x += 2)
        {
            double tt = pos + (x - cx) / r.getWidth() * span;
            int i = (int) (tt * kWaveRate);
            if (i < 0 || i >= wc.n) continue;
            float a = at(wc.amp, i) / 255.0f, hgt = jmax(1.0f, a * half);
            g.setColour(x < cx ? col.withMultipliedBrightness(.55f) : col);
            g.fillRect(x, cy - hgt, 2.0f, hgt * 2);
        }
        // bars: thick white ticks top and bottom
        if (t->bpm > 0)
        {
            double bar = 240.0 / t->bpm, b0 = std::ceil((pos - span / 2 - t->firstBeat) / bar);
            for (double k = b0;; k += 1)
            {
                double tb = t->firstBeat + k * bar; float x = cx + (float) ((tb - pos) / span) * r.getWidth();
                if (x > r.getRight()) break;
                g.setColour(ink()); g.fillRect(x - 1, r.getY(), 2.0f, 4.0f); g.fillRect(x - 1, r.getBottom() - 4, 2.0f, 4.0f);
            }
        }
        // hot cues: solid triangles
        for (int k = 0; k < kCues; ++k)
        {
            const auto& c = ctx.cues[d][(size_t) k];
            if (!c.set()) continue;
            float x = cx + (float) ((c.t - pos) / span) * r.getWidth();
            if (x < r.getX() || x > r.getRight()) continue;
            Path p; p.addTriangle(x - 5, r.getY(), x + 5, r.getY(), x, r.getY() + 8);
            g.setColour(cueColour(c)); g.fillPath(p);
        }
        // loop: bright bar along the bottom
        double la = ctx.proc.status(d).loopA.load(), lb = ctx.proc.status(d).loopB.load();
        if (ctx.proc.status(d).looping.load() && lb > la)
        {
            float xa = jmax(r.getX(), cx + (float) ((la - pos) / span) * r.getWidth()), xb = jmin(r.getRight(), cx + (float) ((lb - pos) / span) * r.getWidth());
            if (xb > xa) { g.setColour(Colour(0xff34d058)); g.fillRect(xa, r.getBottom() - 3, xb - xa, 3.0f); }
        }
        g.setColour(ink()); g.fillRect(cx - 1.5f, r.getY(), 3.0f, r.getHeight());
    }

    void paintDeck(Graphics& g, int d)
    {
        const auto& st = ctx.proc.status(d);
        auto r = platter(d);
        bool touched = st.touched.load() || (grab == (d == 0 ? platterA : platterB));
        g.setColour(Colour(0xff1c1f23)); g.fillEllipse(r);
        g.setColour(touched ? ink() : deckCol(d)); g.drawEllipse(r.reduced(2), 4);
        // label and a fat position marker that turns with the record
        auto c = r.getCentre(); float R = r.getWidth() / 2 - 6;
        g.setColour(deckCol(d)); g.fillEllipse(Rectangle<float>(R * .8f, R * .8f).withCentre(c));
        double a = spin[d] * MathConstants<double>::twoPi - MathConstants<double>::halfPi;
        Point<float> tip(c.x + (float) std::cos(a) * R, c.y + (float) std::sin(a) * R);
        g.setColour(ink()); g.drawLine(Line<float>(c, tip), 5);
        g.fillEllipse(Rectangle<float>(8, 8).withCentre(c));
        // play / cue
        bool playing = st.playing.load();
        auto pb = playBtn(d);
        block(g, pb, playing ? Colour(0xff34d058) : Colour(0xff15181c), playing ? Colours::transparentBlack : Colour(0xff34d058));
        Path p;
        if (playing) { p.addRectangle(pb.getCentreX() - 8, pb.getCentreY() - 8, 6, 16); p.addRectangle(pb.getCentreX() + 2, pb.getCentreY() - 8, 6, 16); }
        else p.addTriangle(pb.getCentreX() - 7, pb.getCentreY() - 9, pb.getCentreX() - 7, pb.getCentreY() + 9, pb.getCentreX() + 9, pb.getCentreY());
        g.setColour(playing ? Colours::black : Colour(0xff34d058)); g.fillPath(p);
        auto cb = cueBtn(d);
        block(g, cb, Colour(0xff15181c), Colour(0xffffa21f));
        label(g, "CUE", cb, Colour(0xffffa21f), 14);
    }

    void paintCentre(Graphics& g)
    {
        const char* names[] = { "CUES", "LOOPS", "ROLLS", "TRANS" };
        int pm = jlimit(0, 3, (int) ctx.proc.padMode());
        const bool sh = ctx.proc.shiftHeld();
        block(g, modeBtn(), sh ? ink() : Colour(0xffffa21f), Colours::transparentBlack);
        label(g, sh ? String("SHIFT") : String(names[pm]), modeBtn(), Colours::black, 13);
        // crossfader: thick track, big white cap
        auto x = xfader();
        g.setColour(grey()); g.fillRoundedRectangle(x.withSizeKeepingCentre(x.getWidth(), 6), 3);
        float v = Ctx::val(ctx.par("xfader"));
        float capX = x.getX() + 7 + (v + 1) / 2 * (x.getWidth() - 14);
        g.setColour(ink()); g.fillRoundedRectangle(Rectangle<float>(12, x.getHeight()).withCentre({ capX, x.getCentreY() }), 2);
        g.setColour(Colours::black); g.fillRect(capX - 1, x.getY() + 3, 2.0f, x.getHeight() - 6);
        block(g, browseBtn(), Colour(0xff15181c), ink());
        label(g, "LIB", browseBtn(), ink(), 12);
        block(g, fullBtn(), Colour(0xff15181c), ink());
        label(g, "FULL", fullBtn(), ink(), 12);
    }

    void paintPads(Graphics& g, int d)
    {
        bool cueMode = ctx.proc.padMode() == PadMode::cues;
        for (int sl = 0; sl < 8; ++sl)
        {
            auto r = padRect(d, sl);
            int idx = padIndex(d, sl);
            bool down = padDown == idx;
            Colour fill = Colour(0xff15181c), txt = ink(), border = grey();
            if (cueMode)
            {
                const auto& c = ctx.cues[d][(size_t) sl];
                if (c.set()) { fill = cueColour(c); txt = fill.getPerceivedBrightness() > .6f ? Colours::black : Colours::white; border = Colours::transparentBlack; }
            }
            else border = deckCol(d);
            if (down) { fill = ink(); txt = Colours::black; }
            block(g, r, fill, border, 3);
            label(g, shortPad(ctx.proc.padLabel(idx)), r.reduced(1), txt, 13);
        }
    }

    // ---- crate browser (full screen inside the margin)
    static Rectangle<float> backBtn() { return { (float) M, (float) M, 56, 28 }; }
    static Rectangle<float> loadBtn(int d) { return { (float) (W - M - 40 - (1 - d) * 44), (float) M, 40, 28 }; }
    static Rectangle<float> listArea() { return { (float) M, (float) (M + 32), (float) (W - 2 * M), (float) (H - 2 * M - 32) }; }
    static constexpr int rowH = 26;
    int rows() const { return inCrate ? lib.entries.size() : lib.crateList.size(); }

    void paintBrowser(Graphics& g)
    {
        block(g, backBtn(), Colour(0xff15181c), ink());
        label(g, inCrate ? "< BACK" : "CLOSE", backBtn(), ink(), 12);
        auto title = Rectangle<float>(backBtn().getRight() + 6, (float) M, 0, 28);
        if (inCrate)
        {
            for (int d = 0; d < 2; ++d)
            {
                bool ok = sel >= 0;
                block(g, loadBtn(d), ok ? deckCol(d) : Colour(0xff15181c), ok ? Colours::transparentBlack : grey());
                label(g, d == 0 ? "A" : "B", loadBtn(d), ok ? Colours::black : grey(), 17);
            }
            title = title.withRight(loadBtn(0).getX() - 6);
            int cr = lib.crates.getSelectedRow();
            label(g, cr >= 0 && cr < lib.crateList.size() ? lib.crateList[cr].name.toUpperCase() : String(), title, ink(), 13, Justification::centredLeft);
        }
        else
        {
            title = title.withRight((float) (W - M));
            label(g, "CRATES", title, ink(), 14, Justification::centredLeft);
        }
        auto area = listArea();
        if (rows() == 0)
        {
            String msg = lib.busy ? String("READING CARD...") : inCrate ? String("NO TRACKS HERE") : String("TAP TO CHOOSE SD CARD");
            block(g, area.withSizeKeepingCentre(area.getWidth(), 48), Colour(0xff15181c), lib.busy ? grey() : deckCol(0));
            label(g, msg, area.withSizeKeepingCentre(area.getWidth(), 48), ink(), 15);
            return;
        }
        g.saveState();
        g.reduceClipRegion(area.toNearestInt());
        for (int i = scroll / rowH; i < rows(); ++i)
        {
            auto r = Rectangle<float>(area.getX(), area.getY() + (float) (i * rowH - scroll), area.getWidth(), rowH - 3);
            if (r.getY() > area.getBottom()) break;
            bool on = inCrate && i == sel;
            block(g, r, on ? ink() : Colour(0xff15181c), Colours::transparentBlack, 3);
            Colour tc = on ? Colours::black : ink();
            if (inCrate)
            {
                const auto& e = lib.entries.getReference(i);
                auto rr = r.reduced(6, 0);
                auto bpmR = rr.removeFromRight(40);
                String nm = e.title.isNotEmpty() ? e.title : e.file.getFileNameWithoutExtension();
                if (e.artist.isNotEmpty()) nm << "  " << e.artist;
                label(g, nm.toUpperCase(), rr, tc, 13, Justification::centredLeft);
                label(g, e.bpm > 0 ? String(roundToInt(e.bpm)) : String("--"), bpmR, on ? Colours::black : deckCol(1), 13, Justification::centredRight);
                for (int d = 0; d < 2; ++d)
                    if (ctx.trk[d] && String::fromUTF8(ctx.trk[d]->path.c_str()) == e.file.getFullPathName())
                    { auto tag = r.withWidth(4); g.setColour(deckCol(d)); g.fillRect(tag); }
            }
            else
            {
                const auto& c = lib.crateList.getReference(i);
                auto rr = r.reduced(6, 0).withTrimmedLeft((float) jmin(3, c.depth) * 12);
                label(g, c.name.fromLastOccurrenceOf(" / ", false, false).toUpperCase(), rr, tc, 13, Justification::centredLeft);
            }
        }
        g.restoreState();
        // a fat scroll position bar
        int total = rows() * rowH;
        if (total > area.getHeight())
        {
            float frac = area.getHeight() / (float) total, top = (float) scroll / (float) total;
            g.setColour(deckCol(0)); g.fillRect(area.getRight() - 4, area.getY() + top * area.getHeight(), 4.0f, frac * area.getHeight());
        }
    }

    // the browser the MPC Sample's encoder drives (SAMPLE opens it, turn to move, press to open a crate / load the
    // track on the focused deck, SEQ goes back)
    void paintHwBrowser(Graphics& g, const WheelProcessor::HwBrowser& hw)
    {
        const int f = ctx.proc.focusDeck();
        auto top = Rectangle<float>((float) M, (float) M, (float) (W - 2 * M), 28);
        auto tag = top.removeFromRight(56);
        block(g, tag, deckCol(f), Colours::transparentBlack);
        label(g, f == 0 ? "TO A" : "TO B", tag, Colours::black, 13);
        label(g, hw.title.toUpperCase(), top.withTrimmedRight(6), ink(), 14, Justification::centredLeft);
        auto area = listArea();
        if (hw.rows.isEmpty())
        {
            String msg = hw.busy ? String("READING CARD...") : hw.mode == 1 ? String("NO SD CARD / MUSIC") : String("NO TRACKS HERE");
            block(g, area.withSizeKeepingCentre(area.getWidth(), 48), Colour(0xff15181c), grey());
            label(g, msg, area.withSizeKeepingCentre(area.getWidth(), 48), ink(), 15);
            return;
        }
        const int visible = (int) (area.getHeight() / rowH);
        const int first = jlimit(0, jmax(0, hw.rows.size() - visible), hw.cursor - visible / 2);
        for (int i = first; i < jmin(hw.rows.size(), first + visible); ++i)
        {
            auto r = Rectangle<float>(area.getX(), area.getY() + (float) ((i - first) * rowH), area.getWidth(), rowH - 3);
            const bool on = i == hw.cursor;
            block(g, r, on ? ink() : Colour(0xff15181c), Colours::transparentBlack, 3);
            auto rr = r.reduced(6, 0);
            if (hw.mode == 2 && i < hw.bpm.size())
                label(g, hw.bpm[i] > 0 ? String(roundToInt(hw.bpm[i])) : String("--"), rr.removeFromRight(40), on ? Colours::black : deckCol(1), 13, Justification::centredRight);
            label(g, hw.rows[i].toUpperCase(), rr, on ? Colours::black : ink(), 13, Justification::centredLeft);
        }
        if (hw.rows.size() > visible)
        {
            float frac = (float) visible / (float) hw.rows.size(), topF = (float) first / (float) hw.rows.size();
            g.setColour(deckCol(f)); g.fillRect(area.getRight() - 4, area.getY() + topF * area.getHeight(), 4.0f, frac * area.getHeight());
        }
    }

    void openBrowser() { browsing = true; inCrate = false; scroll = 0; sel = -1; repaint(); }
    void clampScroll() { scroll = jlimit(0, jmax(0, rows() * rowH - (int) listArea().getHeight()), scroll); }

    // ---- input
    double angleAt(int d, Point<float> p) const { auto c = platter(d).getCentre(); return std::atan2((double) (p.y - c.y), (double) (p.x - c.x)); }
    void setX(float lx)
    {
        auto x = xfader();
        float v = jlimit(-1.0f, 1.0f, (lx - x.getX() - 7) / (x.getWidth() - 14) * 2 - 1);
        if (auto* p = ctx.par("xfader")) p->setValueNotifyingHost(p->convertTo0to1(v));
    }
    void mouseDown(const MouseEvent& e) override
    {
        auto p = logical(e.position); moved = false;
        if (browsing)
        {
            if (backBtn().contains(p)) { if (inCrate) { inCrate = false; scroll = 0; sel = -1; } else browsing = false; repaint(); return; }
            if (inCrate) for (int d = 0; d < 2; ++d) if (loadBtn(d).contains(p) && sel >= 0 && sel < lib.entries.size())
            { lib.table.selectRow(sel); lib.loadSelected(d); browsing = false; repaint(); return; }
            if (listArea().contains(p))
            {
                if (rows() == 0 && !inCrate && !lib.busy) { lib.choose(); return; }
                grab = listDrag; grabY = p.y; grabScroll = (float) scroll;
            }
            return;
        }
        for (int d = 0; d < 2; ++d)
        {
            if (platter(d).contains(p))
            {
                grab = d == 0 ? platterA : platterB; lastAng = angleAt(d, p);
                ctx.send(Cmd::touch, d, 0, true); return;
            }
            if (playBtn(d).contains(p)) { ctx.send(Cmd::playPause, d); return; }
            if (cueBtn(d).contains(p)) { ctx.send(Cmd::cue, d, 0, true); padDown = 100 + d; return; }
            for (int sl = 0; sl < 8; ++sl)
                if (padRect(d, sl).contains(p))
                {
                    int idx = padIndex(d, sl);
                    if (e.mods.isPopupMenu() && ctx.proc.padMode() == PadMode::cues) { ctx.send(Cmd::clearCue, d, 0, false, sl); return; }
                    padDown = idx; ctx.proc.pad(idx, true, 1.0f); repaint(); return;
                }
        }
        if (modeBtn().contains(p)) { if (auto* m = ctx.par("padmode")) { int n = ((int) ctx.proc.padMode() + 1) % 4; m->setValueNotifyingHost(m->convertTo0to1((float) n)); } return; }
        if (xfader().expanded(0, 4).contains(p)) { grab = xfade; if (auto* x = ctx.par("xfader")) x->beginChangeGesture(); setX(p.x); return; }
        if (browseBtn().contains(p)) { openBrowser(); return; }
        if (fullBtn().contains(p)) { if (onFull) onFull(); return; }
    }
    void mouseDrag(const MouseEvent& e) override
    {
        auto p = logical(e.position);
        if (grab == platterA || grab == platterB)
        {
            int d = grab == platterA ? 0 : 1;
            double a = angleAt(d, p), da = a - lastAng;
            if (da > MathConstants<double>::pi) da -= MathConstants<double>::twoPi;
            if (da < -MathConstants<double>::pi) da += MathConstants<double>::twoPi;
            lastAng = a;
            ctx.send(Cmd::move, d, da / MathConstants<double>::twoPi * Ctx::secPerRev());
        }
        else if (grab == xfade) setX(p.x);
        else if (grab == listDrag)
        {
            if (std::abs(p.y - grabY) > 4) moved = true;
            scroll = (int) (grabScroll - (p.y - grabY)); clampScroll(); repaint();
        }
    }
    void mouseUp(const MouseEvent& e) override
    {
        auto p = logical(e.position);
        if (grab == platterA || grab == platterB) ctx.send(Cmd::touch, grab == platterA ? 0 : 1, 0, false);
        if (grab == xfade) if (auto* x = ctx.par("xfader")) x->endChangeGesture();
        if (grab == listDrag && !moved)
        {
            int i = (int) ((p.y - listArea().getY() + (float) scroll) / rowH);
            if (i >= 0 && i < rows())
            {
                if (!inCrate) { lib.crates.selectRow(i); inCrate = true; scroll = 0; sel = -1; }
                else sel = i;
            }
            repaint();
        }
        grab = none;
        if (padDown >= 100) ctx.send(Cmd::cue, padDown - 100, 0, false);
        else if (padDown >= 0) ctx.proc.pad(padDown, false);
        padDown = -1;
    }
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails& w) override
    {
        if (!browsing) return;
        scroll -= (int) (w.deltaY * 120); clampScroll(); repaint();
    }
};

}  // namespace

// ================================================================================================ the editor
struct WheelEditor::Impl : KeyListener
{
    Impl(WheelEditor& e, WheelProcessor& p) : ed(e), ctx(p), mixer(ctx), library(ctx), midi(ctx), learnOverlay(ctx), mpc(ctx, library)
    {
        for (int d = 0; d < 2; ++d)
        {
            wave[d] = std::make_unique<WaveView>(ctx, d);
            over[d] = std::make_unique<Overview>(ctx, d);
            info[d] = std::make_unique<InfoRow>(ctx, d);
            deck[d] = std::make_unique<Turntable>(ctx, d);
            pads[d] = std::make_unique<PadSection>(ctx, d);
            for (Component* c : std::initializer_list<Component*> { wave[d].get(), over[d].get(), info[d].get(), deck[d].get(), pads[d].get() }) ed.addAndMakeVisible(c);
        }
        ed.addAndMakeVisible(mixer);
        ed.addAndMakeVisible(library);
        midiBtn.setButtonText("MIDI / MPC");
        midiBtn.onClick = [this] { midi.setVisible(!midi.isVisible()); if (midi.isVisible()) midi.toFront(true); };
        learnBtn.setButtonText("LEARN");
        learnBtn.onClick = [this] { ctx.learnMode = !ctx.learnMode; updateLearn(); };
        ed.addAndMakeVisible(midiBtn);
        ed.addAndMakeVisible(learnBtn);
        ed.addChildComponent(learnOverlay);
        ed.addChildComponent(midi);
        ctx.selectedFile = [this] { return library.selectedFile(); };
        ctx.learnChanged = [this] { updateLearn(); };
        learnOverlay.passThrough = [this](Point<int> pt) {
            auto q = pt + learnOverlay.getPosition();
            return (midi.isVisible() && midi.getBounds().contains(q)) || learnBtn.getBounds().contains(q) || midiBtn.getBounds().contains(q);
        };
        mpcBtn.setButtonText("MPC VIEW");
        mpcBtn.onClick = [this] { setCompact(true); };
        ed.addAndMakeVisible(mpcBtn);
        mpc.onFull = [this] { setCompact(false); };
        ed.addChildComponent(mpc);
        // the main view: the phone app's screen (PhoneDeckView) with its library browser over it
        host.openLibrary = [this] { libOpen = true; phoneLib->refresh(); layout(ed.getLocalBounds()); phoneLib->toFront(true); };
        host.closeLibrary = [this] { libOpen = false; layout(ed.getLocalBounds()); phone->grabKeyboardFocus(); };
        host.loadSelected = [this](int dk) {
            auto f = phoneLib->selectedFile();
            if (!f.existsAsFile()) return false;
            ctx.proc.loadTrack(dk, f); return true;
        };
        host.selectedTitle = [this] { return phoneLib->selectedTitle(); };
        host.midiStatus = [this] { return midiStatusText(); };
        host.showMpcScreen = [this] { setCompact(true); };
        phone = std::make_unique<PhoneDeckView>(ctx.proc, host);
        phoneLib = std::make_unique<PhoneLibraryView>(ctx.proc, host);
        ed.addChildComponent(*phone);
        ed.addChildComponent(*phoneLib);
        ed.addKeyListener(this);
        snapshot();
    }
    ~Impl() override { ed.removeKeyListener(this); }
    WheelEditor& ed;
    WheelLook look;
    Ctx ctx;
    std::unique_ptr<WaveView> wave[2];
    std::unique_ptr<Overview> over[2];
    std::unique_ptr<InfoRow> info[2];
    std::unique_ptr<Turntable> deck[2];
    std::unique_ptr<PadSection> pads[2];
    MixerPanel mixer;
    LibraryPanel library;
    MidiPanel midi;
    LearnOverlay learnOverlay;
    MpcScreen mpc;
    // three views: the phone app's screen (the main one), the 320 x 240 MPC screen (for an MPC's own display; its FULL
    // goes back to the phone screen) and the older Serato-style layout (only reachable from code now)
    bool compact = false;     // the 320 x 240 MPC screen
    bool phoneView = true;    // the phone app's screen
    bool libOpen = false;     // its library browser is showing
    PhoneHost host;
    std::unique_ptr<PhoneDeckView> phone;
    std::unique_ptr<PhoneLibraryView> phoneLib;
    TextButton midiBtn, learnBtn, mpcBtn;
    String midiStatusText() const;
    void setCompact(bool c)
    {
        compact = c; phoneView = !c;
        ctx.learnMode = false; updateLearn(); midi.setVisible(false);
        mpc.browsing = false; libOpen = false;
        ed.setResizable(true, !c);   // no corner grip over the MPC screen's margin
        if (c) { ed.setResizeLimits(MpcScreen::W, MpcScreen::H, 3200, 2000); ed.setSize(MpcScreen::W, MpcScreen::H); }
        else { ed.setResizeLimits(800, 373, 3200, 2000); ed.setSize(1400, 653); }
        layout(ed.getLocalBounds()); ed.repaint();
    }
    Rectangle<int> topBar;
    int frame = 0;
    String hostSig;

    void updateLearn()
    {
        learnOverlay.setVisible(ctx.learnMode);
        if (ctx.learnMode) { learnOverlay.toFront(false); if (midi.isVisible()) midi.toFront(false); learnOverlay.grabKeyboardFocus(); }
        learnBtn.setColour(TextButton::buttonColourId, ctx.learnMode ? pal::amber : pal::raised);
        learnBtn.setColour(TextButton::textColourOffId, ctx.learnMode ? Colours::black : pal::text);
        midiBtn.toFront(false); learnBtn.toFront(false);
        if (midi.isVisible()) midi.refresh();
    }
    bool keyPressed(const KeyPress& k, Component*) override
    {
        if (k == KeyPress::escapeKey && ctx.learnMode) { ctx.learnMode = false; updateLearn(); return true; }
        if (phoneView || compact) return false;   // the phone screen and its library handle their own keys
        return library.keyPressed(k, nullptr);
    }
    void snapshot()
    {
        for (int d = 0; d < 2; ++d)
        {
            auto t = ctx.proc.deckTrack(d);
            if (t != ctx.trk[d] || ctx.wave[d].key != t.get() || (t && ctx.wave[d].n == 0 && !t->wave.low.empty()))
            {
                ctx.trk[d] = t;
                ctx.wave[d].build(t.get());
                over[d]->imgFor = nullptr;
                over[d]->repaint(); info[d]->repaint(); library.table.repaint();
            }
            ctx.cues[d] = ctx.proc.cuesOf(d);
        }
    }
    void tick()
    {
        ++frame;
        snapshot();
        if (compact || phoneView) return;   // the MPC screen and the phone screen repaint themselves
        for (int d = 0; d < 2; ++d)
        {
            wave[d]->repaint();
            deck[d]->platter.tick();
            if (frame % 2 == 0) over[d]->repaint();
            if (frame % 3 == 0) { info[d]->tick(); deck[d]->arm.tick(); pads[d]->poll(); }
        }
        mixer.tick();
        if (frame % 3 == 1) for (auto* b : ctx.buttons) b->poll();
        if (frame % 6 == 0)
        {
            deck[0]->repaint(deck[0]->pitchRead.toNearestInt()); deck[1]->repaint(deck[1]->pitchRead.toNearestInt());
            deck[0]->repaint(deck[0]->lampR.expanded(4).toNearestInt()); deck[1]->repaint(deck[1]->lampR.expanded(4).toNearestInt());
            String hs; hs << (int) (ctx.proc.hostBpm() * 100) << ctx.proc.learning();
            if (hs != hostSig) { hostSig = hs; ed.repaint(topBar); }
            if (ctx.learnMode) learnOverlay.repaint();
        }
    }
    void layout(Rectangle<int> area)
    {
        if (phone != nullptr)
        {
            phone->setVisible(phoneView);
            phoneLib->setVisible(phoneView && libOpen);
            if (phoneView)
            {
                mpc.setVisible(false);
                for (int d = 0; d < 2; ++d)
                    for (Component* c : std::initializer_list<Component*> { wave[d].get(), over[d].get(), info[d].get(), deck[d].get(), pads[d].get() }) c->setVisible(false);
                for (Component* c : std::initializer_list<Component*> { &mixer, &library, &midiBtn, &learnBtn, &mpcBtn }) c->setVisible(false);
                phone->setBounds(area); phoneLib->setBounds(area);
                return;
            }
        }
        mpc.setVisible(compact);
        for (int d = 0; d < 2; ++d)
            for (Component* c : std::initializer_list<Component*> { wave[d].get(), over[d].get(), info[d].get(), deck[d].get(), pads[d].get() }) c->setVisible(!compact);
        for (Component* c : std::initializer_list<Component*> { &mixer, &library, &midiBtn, &learnBtn, &mpcBtn }) c->setVisible(!compact);
        if (compact)
        {
            // keep 4:3 and the safe margin: scale the 320 x 240 screen to fit, centred
            float s = jmin((float) area.getWidth() / MpcScreen::W, (float) area.getHeight() / MpcScreen::H);
            mpc.setBounds(area.withSizeKeepingCentre(roundToInt(MpcScreen::W * s), roundToInt(MpcScreen::H * s)));
            return;
        }
        float S = jlimit(.6f, 2.0f, jmin((float) area.getWidth() / 1400.0f, (float) area.getHeight() / 900.0f));
        ctx.S = S;
        auto b = area.reduced((int) (6 * S));
        topBar = b.removeFromTop((int) (30 * S));
        {
            auto tb = topBar;
            learnBtn.setBounds(tb.removeFromRight((int) (80 * S)).reduced(0, (int) (3 * S)));
            tb.removeFromRight(6);
            midiBtn.setBounds(tb.removeFromRight((int) (110 * S)).reduced(0, (int) (3 * S)));
            tb.removeFromRight(6);
            mpcBtn.setBounds(tb.removeFromRight((int) (100 * S)).reduced(0, (int) (3 * S)));
        }
        b.removeFromTop((int) (4 * S));
        int infoH = (int) (34 * S), ovH = (int) (24 * S);
        int waveH = jmax(56, (int) ((float) b.getHeight() * .1f));
        info[0]->setBounds(b.removeFromTop(infoH));
        over[0]->setBounds(b.removeFromTop(ovH).reduced(0, (int) (2 * S)));
        wave[0]->setBounds(b.removeFromTop(waveH));
        b.removeFromTop(2);
        wave[1]->setBounds(b.removeFromTop(waveH));
        over[1]->setBounds(b.removeFromTop(ovH).reduced(0, (int) (2 * S)));
        info[1]->setBounds(b.removeFromTop(infoH));
        b.removeFromTop((int) (6 * S));
        int deckH = (int) (272 * S), padH = (int) (100 * S);
        auto mid = b.removeFromTop(deckH + padH + (int) (4 * S));
        int mixW = (int) ((float) mid.getWidth() * .25f);
        int dw = (mid.getWidth() - mixW - (int) (12 * S)) / 2;
        auto colA = mid.removeFromLeft(dw), colB = mid.removeFromRight(dw);
        mid.reduce((int) (6 * S), 0);
        mixer.setBounds(mid);
        deck[0]->setBounds(colA.removeFromTop(deckH)); colA.removeFromTop((int) (4 * S)); pads[0]->setBounds(colA);
        deck[1]->setBounds(colB.removeFromTop(deckH)); colB.removeFromTop((int) (4 * S)); pads[1]->setBounds(colB);
        b.removeFromTop((int) (6 * S));
        library.setBounds(b);
        learnOverlay.setBounds(area);
        midi.setBounds(area.withSizeKeepingCentre(jmin(area.getWidth() - 40, 760), jmin(area.getHeight() - 40, 620)));
    }
    void paintTop(Graphics& g)
    {
        float S = ctx.S;
        auto r = topBar.toFloat();
        // wordmark
        g.setColour(pal::text); g.setFont(font(19 * S, true));
        g.drawText("OMEGA", r.withWidth(80 * S), Justification::centredLeft);
        g.setColour(pal::deck[0]); g.setFont(font(13 * S, true));
        g.drawText("WHEEL", r.withX(r.getX() + 80 * S).withWidth(52 * S), Justification::centredLeft);
        g.setColour(pal::dim); g.drawText("OF", r.withX(r.getX() + 130 * S).withWidth(22 * S), Justification::centredLeft);
        g.setColour(pal::deck[1]); g.drawText("STEEL", r.withX(r.getX() + 154 * S).withWidth(60 * S), Justification::centredLeft);
        // host tempo + pad mode
        double hb = ctx.proc.hostBpm();
        auto c = r.withSizeKeepingCentre(420 * S, r.getHeight());
        g.setColour(Colours::black); g.fillRoundedRectangle(c.reduced(0, 3 * S), 4);
        g.setColour(pal::edge); g.drawRoundedRectangle(c.reduced(0, 3 * S), 4, 1);
        g.setFont(mono(13 * S, true));
        g.setColour(pal::dim);
        g.drawText("HOST", c.withTrimmedLeft(12 * S), Justification::centredLeft);
        g.setColour(hb > 0 ? pal::green : pal::faint);
        g.drawText(hb > 0 ? String(hb, 2) + " BPM" : String("--- BPM"), c.withTrimmedLeft(60 * S).withWidth(120 * S), Justification::centredLeft);
        const char* modeNames[] = { "HOT CUE", "AUTO LOOP", "ROLL", "TRANSPORT" };
        int pm = jlimit(0, 3, (int) ctx.proc.padMode());
        g.setColour(pal::dim); g.drawText("PADS", c.withTrimmedLeft(200 * S), Justification::centredLeft);
        g.setColour(pal::amber); g.drawText(modeNames[pm], c.withTrimmedLeft(250 * S), Justification::centredLeft);
    }
};

WheelEditor::WheelEditor(WheelProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    d = std::make_unique<Impl>(*this, p);
    setLookAndFeel(&d->look);
    setWantsKeyboardFocus(true);
    // opens on the phone app's screen; an OS drawing the MPC's own display asks for the 320 x 240 screen
    setResizable(true, true);
    setResizeLimits(800, 373, 3200, 2000);
    setSize(1400, 653);
    startTimerHz(60);
}

WheelEditor::~WheelEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
    d.reset();
}

void WheelEditor::paint(Graphics& g)
{
    ColourGradient bg(pal::bg.brighter(.04f), 0, 0, pal::bg, 0, (float) getHeight(), false);
    g.setGradientFill(bg); g.fillAll();
    if (d->compact || d->phoneView) g.fillAll(Colours::black);
    else d->paintTop(g);
}

void WheelEditor::resized() { d->layout(getLocalBounds()); }

void WheelEditor::timerCallback() { d->tick(); }

// the 320 x 240 screen as an image, from an editor that is never put on a display (an OS that draws the screen itself)
Image WheelProcessor::renderScreen(int width, int height)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (screenEditor == nullptr) { auto* e = new WheelEditor(*this); e->showMpcScreen(); screenEditor.reset(e); }
    if (screenEditor->getWidth() != width || screenEditor->getHeight() != height) screenEditor->setSize(width, height);
    if (auto* ed = dynamic_cast<WheelEditor*>(screenEditor.get())) ed->refresh();
    return screenEditor->createComponentSnapshot(screenEditor->getLocalBounds(), true, 1.0f);
}

void WheelEditor::refresh() { d->snapshot(); d->mpc.timerCallback(); }
void WheelEditor::showMpcScreen() { if (!d->compact) d->setCompact(true); }

String WheelEditor::Impl::midiStatusText() const
{
    if (ctx.proc.mpcSampleActive()) return "MIDI: MPC Sample";
    return "MIDI: plug in an MPC or controller";
}

}  // namespace wheel

/* C entry point for an OS that hosts the plugin headless and draws the instrument's screen itself (the MPC Sample
   has no window system):

     int omega_wheel_screen(uint32_t* pixels, int width, int height, int strideBytes);

   renders the latest Omega Wheel of Steel instance's MPC screen (made for 320 x 240) into an XRGB8888 buffer, the
   format of the MPC Sample's display (DRM XR24). Returns 1 when drawn, 0 when there is no instance. Call it at the
   frame rate you want (about 20 fps suits the SPI panel), never from the audio thread. Find it with dlsym() on the
   plugin's .so. omega_wheel_api_version() returns 1. */
#if defined(_WIN32)
 #define OMEGA_WHEEL_EXPORT __declspec(dllexport)
#else
 #define OMEGA_WHEEL_EXPORT __attribute__((visibility("default")))
#endif
extern "C" OMEGA_WHEEL_EXPORT int omega_wheel_api_version() { return 1; }
extern "C" OMEGA_WHEEL_EXPORT int omega_wheel_screen(uint32_t* pixels, int width, int height, int strideBytes)
{
    using namespace juce;
    if (pixels == nullptr || width <= 0 || height <= 0 || strideBytes < width * 4) return 0;
    int ok = 0;
    auto draw = [&] {
        auto* p = wheel::WheelProcessor::latest();
        if (p == nullptr) return;
        Image img = p->renderScreen(width, height);
        Image::BitmapData bd(img, Image::BitmapData::readOnly);
        for (int y = 0; y < height; ++y)
        {
            auto* row = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(pixels) + (size_t) y * (size_t) strideBytes);
            for (int x = 0; x < width; ++x) row[x] = 0xff000000u | (bd.getPixelColour(x, y).getARGB() & 0xffffffu);
        }
        ok = 1;
    };
    auto* mm = MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || mm->isThisTheMessageThread()) draw();
    else mm->callFunctionOnMessageThread([](void* f) -> void* { (*static_cast<decltype(draw)*>(f))(); return nullptr; }, &draw);
    return ok;
}
