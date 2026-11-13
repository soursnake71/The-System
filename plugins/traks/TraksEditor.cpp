#include "TraksEditor.h"
#include <map>

namespace traks {

using juce::Colour; using juce::Graphics; using juce::Path; using juce::Rectangle;

/* ---------------- the front panel: the layout of traks/faceplate.js (plate units 2000 x 916, + 60 cheeks) ---------------- */
namespace lay {
constexpr float CHEEK = 60;
struct KeyDef { int id; float cx, cy, s; int style; const char* cap; };   // style 0 dark top key, 1 keypad, 2 big key with the light bar
const KeyDef KEYS[] = {
    {K::SAVE, 254, 190, 56, 0, ""}, {K::LOAD, 358, 190, 56, 0, ""}, {K::SLOWER, 256, 392, 56, 0, ""}, {K::FASTER, 358, 392, 56, 0, ""},
    {K::SONGFN, 521, 147, 52, 0, ""}, {K::ENTER, 636, 386, 52, 0, ""}, {K::PATFN, 1247, 142, 52, 0, ""},
    {K::D0 + 1, 936, 194, 48, 1, "1"}, {K::D0 + 2, 1014, 194, 48, 1, "2"}, {K::D0 + 3, 1090, 194, 48, 1, "3"},
    {K::D0 + 4, 936, 271, 48, 1, "4"}, {K::D0 + 5, 1014, 271, 48, 1, "5"}, {K::D0 + 6, 1090, 271, 48, 1, "6"},
    {K::D0 + 7, 936, 347, 48, 1, "7"}, {K::D0 + 8, 1014, 347, 48, 1, "8"}, {K::D0 + 9, 1090, 347, 48, 1, "9"},
    {K::LEFT, 936, 422, 48, 1, "<"}, {K::D0, 1014, 422, 48, 1, "0"}, {K::RIGHT, 1090, 422, 48, 1, ">"},
    {K::RUN, 238, 772, 80, 2, ""}, {K::ACCENT, 416, 772, 80, 2, ""},
    {K::INST + 0, 604, 772, 80, 2, ""}, {K::INST + 1, 708, 772, 80, 2, ""}, {K::INST + 2, 802, 772, 80, 2, ""},
    {K::INST + 3, 905, 772, 80, 2, ""}, {K::INST + 4, 1000, 772, 80, 2, ""}, {K::INST + 5, 1104, 772, 80, 2, ""}, {K::INST + 6, 1200, 772, 80, 2, ""},
    {K::INST + 7, 1302, 772, 80, 2, ""}, {K::INST + 8, 1398, 772, 80, 2, ""},
    {K::INST + 9, 1504, 772, 80, 2, ""}, {K::INST + 10, 1603, 772, 80, 2, ""}, {K::INST + 11, 1701, 772, 80, 2, ""}, {K::INST + 12, 1800, 772, 80, 2, ""}};
constexpr int NK = (int) (sizeof(KEYS) / sizeof(KEYS[0]));
struct KnobDef { float x, y, r; };
const KnobDef KNOBS[] = {{217, 630, 27}, {400, 630, 27}, {1741, 150, 32}, {1741, 336, 32}};   // VOLUME, TUNING (per instrument), METRONOME, MASTER
struct BoxDef { float x, y, w, h; };
const BoxDef BOXES[] = {{196, 62, 222, 206}, {196, 276, 222, 194}, {431, 62, 433, 408}, {873, 62, 280, 408}, {1164, 62, 431, 408}, {1610, 62, 228, 408},
    {172, 558, 313, 330}, {552, 558, 101, 330}, {657, 558, 194, 330}, {858, 558, 189, 330}, {1056, 558, 192, 330}, {1254, 558, 192, 330}, {1455, 558, 394, 330}};
const float SONG_Y[9] = {186, 206, 226, 246, 265, 284, 303, 322, 341};
const float PAT_Y[9] = {181, 200, 219, 238, 258, 277, 296, 315, 334};
}
const Colour WHITE(0xfff0f0ee), PANEL(0xff161616), LEDON(0xffff2a3a), LEDOFF(0xff3a1418), SEGON(0xffff2440), SEGOFF(0xff260709);

static juce::Font font(float h, bool bold = true) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : 0)); }
static void txt(Graphics& g, float x, float y, const juce::String& s, float size, Colour c = WHITE, int anchor = 0, bool bold = true) {
    g.setColour(c); g.setFont(font(size, bold));
    const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), s) + 4;
    const float x0 = anchor < 0 ? x : anchor > 0 ? x - w : x - w / 2;
    g.drawText(s, Rectangle<float>(x0, y - size * 0.85f, w, size * 1.1f), anchor < 0 ? juce::Justification::left : anchor > 0 ? juce::Justification::right : juce::Justification::centred, false);
}
static Path poly(std::initializer_list<juce::Point<float>> pts) { Path p; bool first = true; for (auto q : pts) { if (first) p.startNewSubPath(q); else p.lineTo(q); first = false; } p.closeSubPath(); return p; }

// a 7-segment digit (slanted), chars as on the hardware's LED displays
static const char* segsOf(char ch) {
    switch (ch) {
        case '0': case 'O': return "abcdef"; case '1': return "bc"; case '2': return "abged"; case '3': return "abgcd"; case '4': return "fgbc";
        case '5': case 'S': return "afgcd"; case '6': return "afgedc"; case '7': return "abc"; case '8': return "abcdefg"; case '9': return "abcdfg";
        case '-': return "g"; case 'E': return "afged"; case 'C': return "adef"; case 'c': return "ged"; case 'h': return "fegc"; case 'H': return "fbgec";
        case 'n': return "egc"; case 'P': return "abfge"; case 'o': return "cdeg"; case 'F': return "afge"; case 'r': return "eg"; case 'd': return "bcdeg";
        case 'L': return "fed"; case 'U': return "bcdef"; case 'u': return "cde"; case 'A': return "abcefg"; case 't': return "fged"; case 'b': return "fedcg";
        case '_': return "d"; default: return "";
    }
}
static void seg7(Graphics& g, float x, float y, float w, float h, char ch) {
    const char* segs = segsOf(ch);
    const float t = w * 0.17f, H = h / 2, sl = w * 0.12f;
    auto P = [&](float px, float py) { return juce::Point<float>(px + sl * (1 - (py - y) / h), py); };
    struct S { char id; Path p; };
    const S all[] = {
        {'a', poly({P(x + t, y), P(x + w - t, y), P(x + w - t * 1.6f, y + t), P(x + t * 1.6f, y + t)})},
        {'d', poly({P(x + t * 1.6f, y + h - t), P(x + w - t * 1.6f, y + h - t), P(x + w - t, y + h), P(x + t, y + h)})},
        {'g', poly({P(x + t, y + H), P(x + t * 1.6f, y + H - t / 2), P(x + w - t * 1.6f, y + H - t / 2), P(x + w - t, y + H), P(x + w - t * 1.6f, y + H + t / 2), P(x + t * 1.6f, y + H + t / 2)})},
        {'f', poly({P(x, y + t), P(x + t, y + t * 1.6f), P(x + t, y + H - t * .7f), P(x, y + H - t * .2f)})},
        {'b', poly({P(x + w, y + t), P(x + w, y + H - t * .2f), P(x + w - t, y + H - t * .7f), P(x + w - t, y + t * 1.6f)})},
        {'e', poly({P(x, y + H + t * .2f), P(x + t, y + H + t * .7f), P(x + t, y + h - t * 1.6f), P(x, y + h - t)})},
        {'c', poly({P(x + w, y + H + t * .2f), P(x + w, y + h - t), P(x + w - t, y + h - t * 1.6f), P(x + w - t, y + H + t * .7f)})}};
    for (const auto& s : all) { const bool on = std::strchr(segs, s.id) != nullptr; g.setColour(on ? SEGON : SEGOFF); g.fillPath(s.p); }
}
static void display2(Graphics& g, float x, float y, const juce::String& s) {
    g.setColour(Colour(0xff050505)); g.fillRoundedRectangle(x, y, 88, 56, 4);
    g.setColour(Colour(0xff1d0507)); g.fillRoundedRectangle(x + 4, y + 4, 80, 48, 3);
    for (int i = 0; i < 2; ++i) seg7(g, x + 12 + i * 37, y + 9, 28, 38, i < s.length() ? (char) s[i] : ' ');
}
static void led(Graphics& g, float x, float y, bool on, float r = 5) {
    if (on) { g.setColour(LEDON.withAlpha(0.35f)); g.fillEllipse(x - r * 2.2f, y - r * 2.2f, r * 4.4f, r * 4.4f); }
    g.setColour(on ? LEDON : LEDOFF); g.fillEllipse(x - r, y - r, r * 2, r * 2);
    g.setColour(juce::Colours::white.withAlpha(on ? 0.6f : 0.15f)); g.fillEllipse(x - r * 0.45f, y - r * 0.6f, r * 0.6f, r * 0.5f);
}
static void keyCap(Graphics& g, const lay::KeyDef& k, bool down) {
    const float s = k.s, x = k.cx - s / 2, y = k.cy - s / 2 + (down ? 3.0f : 0.0f);
    if (k.style == 1) {   // keypad: light grey caps with faint numbers
        g.setColour(Colour(0xff0b0b0b)); g.fillRoundedRectangle(x - 3, y - 3 + (down ? -3.0f : 0.0f), s + 6, s + 8, 4);
        g.setGradientFill(juce::ColourGradient(Colour(down ? 0xffbdbdbd : 0xffdcdcdc), x, y, Colour(0xffa9a9a9), x, y + s, false)); g.fillRoundedRectangle(x, y, s, s, 3);
        g.setColour(Colour(0xfff4f4f4)); g.drawRoundedRectangle(x + 1, y + 1, s - 2, s - 2, 3, 1.5f);
        const juce::String c(k.cap);
        if (c == "<" || c == ">") { const float d = c == "<" ? -1.0f : 1.0f; g.setColour(Colour(0xff8a8a8a)); g.fillPath(poly({{k.cx + d * 9, k.cy + (down ? 3.0f : 0.0f)}, {k.cx - d * 7, k.cy - 9 + (down ? 3.0f : 0.0f)}, {k.cx - d * 7, k.cy + 9 + (down ? 3.0f : 0.0f)}})); }
        else txt(g, k.cx, k.cy + 8 + (down ? 3.0f : 0.0f), c, 22, Colour(0xff9a9a9a));
        return;
    }
    g.setColour(Colour(0xff000000)); g.fillRoundedRectangle(x - 5, y - 5 + (down ? -3.0f : 0.0f), s + 10, s + 12, 5);
    g.setGradientFill(juce::ColourGradient(Colour(down ? 0xff202020 : 0xff333333), x, y, Colour(0xff151515), x + s, y + s, false)); g.fillRoundedRectangle(x, y, s, s, 3);
    g.setColour(Colour(0xff4a4a4a)); g.drawRoundedRectangle(x, y, s, s, 3, 1.5f);
    if (k.style == 2) {   // the light bar on the right of the big keys
        g.setGradientFill(juce::ColourGradient(Colour(0xffffffff), x + s * 0.82f, y, Colour(0xffbdbdbd), x + s * 0.82f, y + s, false));
        g.fillRoundedRectangle(x + s * 0.76f, y + s * 0.1f, s * 0.1f, s * 0.8f, 2);
    }
}
static void wood(Graphics& g, float x, float w, float h) {
    g.setGradientFill(juce::ColourGradient(Colour(0xff7a3c15), x, 0, Colour(0xffc4752f), x + w * 0.55f, 0, false)); g.fillRoundedRectangle(x, 0, w, h, 6);
    juce::Random r(7);
    for (int i = 0; i < 9; ++i) { const float gx = x + 6 + r.nextFloat() * (w - 12); g.setColour(Colour(0x30401a05)); g.drawLine(gx, 4, gx + r.nextFloat() * 4 - 2, h - 4, 1.5f + r.nextFloat() * 2); }
    g.setColour(Colour(0x40ffffff)); g.drawLine(x + w * 0.7f, 6, x + w * 0.7f, h - 6, 2);
}
static void knob(Graphics& g, float cx, float cy, float r, float n, bool scale) {
    if (scale) for (int i = 0; i <= 10; ++i) {
        const float a = juce::degreesToRadians(-135.0f + 27.0f * (float) i);
        g.setColour(WHITE); g.fillEllipse(cx + std::sin(a) * (r + 10) - 2.5f, cy - std::cos(a) * (r + 10) - 2.5f, 5, 5);
        if (i % 2 == 0 || i == 1 || i == 9) txt(g, cx + std::sin(a) * (r + 25), cy - std::cos(a) * (r + 25) + 6, juce::String(i), 15);
    }
    g.setColour(Colour(0x80000000)); g.fillEllipse(cx - r - 2, cy - r + 3, r * 2 + 4, r * 2 + 4);
    g.setColour(Colour(0xffe8e8e8)); g.fillEllipse(cx - r - 2, cy - r - 2, r * 2 + 4, r * 2 + 4);       // the white skirt
    g.setGradientFill(juce::ColourGradient(Colour(0xff3a3a3a), cx - r * 0.4f, cy - r * 0.5f, Colour(0xff050505), cx + r, cy + r, true));
    g.fillEllipse(cx - r * 0.86f, cy - r * 0.86f, r * 1.72f, r * 1.72f);
    const float a = juce::degreesToRadians(-135.0f + 270.0f * n);
    g.setColour(WHITE); g.drawLine(cx + std::sin(a) * r * 0.2f, cy - std::cos(a) * r * 0.2f, cx + std::sin(a) * r * 0.95f, cy - std::cos(a) * r * 0.95f, std::max(3.0f, r * 0.13f));
}

Faceplate::Faceplate(TraksProcessor& p) : proc(p) { startTimerHz(30); }
juce::Point<float> Faceplate::toPlate(juce::Point<float> p) const { return {p.x * (float) TraksEditor::W / (float) getWidth() - lay::CHEEK, p.y * (float) TraksEditor::PH / (float) getHeight()}; }
int Faceplate::keyAt(juce::Point<float> p) const {
    for (int i = 0; i < lay::NK; ++i) { const auto& k = lay::KEYS[i]; if (std::abs(p.x - k.cx) <= k.s / 2 + 8 && std::abs(p.y - k.cy) <= k.s / 2 + 8) return i; }
    return -1;
}
int Faceplate::knobAt(juce::Point<float> p) const {
    for (int i = 0; i < 4; ++i) if (p.getDistanceFrom({lay::KNOBS[i].x, lay::KNOBS[i].y}) <= lay::KNOBS[i].r + 18) return i;
    return -1;
}
static juce::String knobParam(TraksProcessor& p, int k) {
    const int v = p.selected.load();
    switch (k) { case 0: return juce::String(VOICES[v]) + "lv"; case 1: return juce::String(VOICES[v]) + "tune"; case 2: return "metro"; default: return "volume"; }
}
float Faceplate::knobNorm(int k) const { return proc.apvts.getParameter(knobParam(proc, k))->getValue(); }
void Faceplate::setKnob(int k, float n) {
    auto* prm = proc.apvts.getParameter(knobParam(proc, k));
    prm->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, n));
    const double v = prm->convertFrom0to1(prm->getValue());
    if (k == 0) flash("u ", juce::String(juce::jlimit(0, 99, (int) std::round(v * 99))).paddedLeft('0', 2), 1200);
    else if (k == 1) { const int t = (int) std::round(v); flash(t < 0 ? " -" : "  ", juce::String(std::abs(t)).paddedLeft(' ', 2), 1200); }
    else flash("  ", juce::String(juce::jlimit(0, 99, (int) std::round(v * 10))).paddedLeft(' ', 2), 900);
    if (k <= 1) proc.key(200, true);          // while recording: the notes carry this sound's new values
}
void Faceplate::showTempo() {
    const int t = (int) std::round(proc.value(pid::tempo));
    const juce::String s = juce::String(t).paddedLeft(' ', 4);
    flash(s.substring(0, 2), s.substring(2), 1500);
}
void Faceplate::timerCallback() {
    if (repeatKey >= 0 && juce::Time::getMillisecondCounter() >= repeatAt) {
        proc.setValue(pid::tempo, juce::jlimit(40.0, 250.0, proc.value(pid::tempo) + (repeatKey == K::FASTER ? 1 : -1))); showTempo();
        repeatAt = juce::Time::getMillisecondCounter() + 70;
    }
    repaint();
}
void Faceplate::mouseDown(const juce::MouseEvent& e) {
    const auto p = toPlate(e.position);
    const int k = knobAt(p);
    if (k >= 0) { dragKnob = k; dragY = e.position.y; dragV0 = knobNorm(k); proc.apvts.getParameter(knobParam(proc, k))->beginChangeGesture(); return; }
    const int i = keyAt(p); if (i < 0) return;
    down = i; const int id = lay::KEYS[i].id;
    if (id == K::SLOWER || id == K::FASTER) {
        proc.setValue(pid::tempo, juce::jlimit(40.0, 250.0, proc.value(pid::tempo) + (id == K::FASTER ? 1 : -1))); showTempo();
        repeatKey = id; repeatAt = juce::Time::getMillisecondCounter() + 450;
    } else if (id == K::SAVE) { casLed = 0; casUntil = juce::Time::getMillisecondCounter() + 1500; if (onSave) onSave(); }
    else if (id == K::LOAD) { casLed = 1; casUntil = juce::Time::getMillisecondCounter() + 1500; if (onLoad) onLoad(); }
    else proc.key(id, true);
    repaint();
}
void Faceplate::mouseDrag(const juce::MouseEvent& e) {
    if (dragKnob < 0) return;
    setKnob(dragKnob, dragV0 + (dragY - e.position.y) / ((float) getHeight() * 0.4f));
}
void Faceplate::mouseUp(const juce::MouseEvent&) {
    if (dragKnob >= 0) proc.apvts.getParameter(knobParam(proc, dragKnob))->endChangeGesture();
    dragKnob = -1; repeatKey = -1;
    if (down >= 0) { const int id = lay::KEYS[down].id; if (id != K::SLOWER && id != K::FASTER && id != K::SAVE && id != K::LOAD) proc.key(id, false); }
    down = -1; repaint();
}
void Faceplate::mouseDoubleClick(const juce::MouseEvent& e) {
    const int k = knobAt(toPlate(e.position));
    if (k >= 0) { auto* prm = proc.apvts.getParameter(knobParam(proc, k)); prm->setValueNotifyingHost(prm->getDefaultValue()); }
}

void Faceplate::paint(Graphics& g) {
    using namespace lay;
    g.addTransform(juce::AffineTransform::scale((float) getWidth() / (float) TraksEditor::W, (float) getHeight() / (float) TraksEditor::PH));
    const float H = (float) TraksEditor::PH;
    wood(g, 0, CHEEK, H); wood(g, CHEEK + 2000, CHEEK, H);
    g.addTransform(juce::AffineTransform::translation(CHEEK, 0));
    // the black panel, a fine texture
    g.setColour(PANEL); g.fillRect(0.0f, 0.0f, 2000.0f, H);
    { juce::Random r(3); for (int i = 0; i < 2600; ++i) { g.setColour(r.nextBool() ? Colour(0x0cffffff) : Colour(0x14000000)); g.fillRect(r.nextFloat() * 2000, r.nextFloat() * H, 2.0f, 2.0f); } }
    g.setGradientFill(juce::ColourGradient(Colour(0x22ffffff), 0, 0, Colour(0x00ffffff), 0, 60, false)); g.fillRect(0.0f, 0.0f, 2000.0f, 60.0f);
    for (const auto& b : BOXES) { g.setColour(WHITE); g.drawRoundedRectangle(b.x, b.y, b.w, b.h, 16, 3.2f); }
    const Display d = proc.display();
    const bool fl = juce::Time::getMillisecondCounter() < flashUntil;
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    // cassette, tempo
    txt(g, 307, 113, "CASSETTE", 17); led(g, 256, 144, casLed == 0 && now < casUntil); led(g, 358, 144, casLed == 1 && now < casUntil);
    txt(g, 254, 243, "SAVE", 16); txt(g, 358, 243, "LOAD", 16);
    txt(g, 307, 323, "TEMPO", 17); txt(g, 256, 446, "SLOWER", 15); txt(g, 358, 446, "FASTER", 15);
    // song functions
    txt(g, 561, 153, "SELECT  SONG  FUNCTIONS", 17, WHITE, -1);
    const char* sn[9] = {"SONG#", "ERASE  SONG", "COPY/APPEND  TO", "BUILD  MODE", "PATTERN#", "VOL  CHANGE", "TEMPO  CHANGE", "INSERT", "DELETE"};
    for (int i = 0; i < 9; ++i) { const bool sub = i >= 4; led(g, sub ? 612.0f : 572.0f, SONG_Y[i], (d.ls >> i) & 1, 4.5f); txt(g, sub ? 628.0f : 588.0f, SONG_Y[i] + 6, sn[i], 16, WHITE, -1); }
    g.setColour(WHITE); g.drawLine(572, 252, 572, 386, 2.5f); g.drawLine(572, 386, 606, 386, 2.5f);
    for (int i = 4; i < 9; ++i) g.drawLine(572, SONG_Y[i], 604, SONG_Y[i], 2.5f);
    txt(g, 698, 393, "ENTER", 16);
    // keypad and displays
    if (fl) { display2(g, 888, 72, flashL); display2(g, 1047, 72, flashR); }
    else { display2(g, 888, 72, d.l); display2(g, 1047, 72, d.r); }
    // pattern functions
    txt(g, 1294, 148, "SELECT  PATTERN  FUNCTIONS", 17, WHITE, -1);
    const char* pn[9] = {"PATTERN#", "RECORD  MODE", "ERASE  INSTRUMENT", "ERROR  CORRECT", "SWING%", "TIME  SIGNATURE", "#OF  MEASURES", "ERASE  PATTERN", "COPY / APPEND  TO"};
    for (int i = 0; i < 9; ++i) { const bool sub = i == 2 || i == 3; led(g, sub ? 1337.0f : 1293.0f, PAT_Y[i], (d.lp >> i) & 1, 4.5f); txt(g, sub ? 1352.0f : 1310.0f, PAT_Y[i] + 6, pn[i], 16, WHITE, -1); }
    g.setColour(WHITE); g.drawLine(1293, 206, 1293, PAT_Y[3], 2.5f); for (int i = 2; i < 4; ++i) g.drawLine(1293, PAT_Y[i], 1330, PAT_Y[i], 2.5f);
    // metronome / master volume
    txt(g, 1741, 232, "METRONOME", 15); txt(g, 1741, 258, "VOLUME", 15); txt(g, 1741, 418, "MASTER", 15); txt(g, 1741, 444, "VOLUME", 15);
    // the logo (the Omega's; the hardware's is in this place, in this style)
    {
        g.setColour(WHITE); g.setFont(juce::Font(juce::FontOptions(58.0f, juce::Font::bold | juce::Font::italic)));
        g.drawText("omegatraks", Rectangle<float>(1470, 486, 340, 60), juce::Justification::centredRight, false);
        for (int i = 0; i < 3; ++i) { Path a; a.addCentredArc(1822, 514, 8.0f + i * 7, 12.0f + i * 7, 0, 0.5f, 2.6f, true); g.strokePath(a, juce::PathStrokeType(2.2f)); }
    }
    // bottom left: per-instrument knobs, run / stop, accent
    txt(g, 217, 678, "VOLUME", 15); txt(g, 400, 678, "TUNING", 15);
    g.setColour(WHITE); g.drawLine(240, 682, 240, 694, 2); g.drawLine(240, 694, 260, 694, 2); g.drawLine(378, 694, 358, 694, 2); g.drawLine(378, 682, 378, 694, 2);
    txt(g, 309, 700, "PER INSTRUMENT", 13);
    led(g, 246, 722, d.run || d.rec, 5);
    txt(g, 238, 845, "RUN/STOP", 15); txt(g, 416, 845, "ACCENT", 15);
    // channels
    const char* chn[6] = {"CH 1", "CH 2", "CH 3", "CH 4", "CH 5", "CH 6"};
    for (int c = 0; c < 6; ++c) txt(g, BOXES[7 + c].x + BOXES[7 + c].w / 2, 620, chn[c], 17);
    for (int v = 0; v < NV; ++v) {
        const auto& k = KEYS[NK - NV + v];
        txt(g, k.cx - 6, 840, KEY_NAMES[v], 14);
        if (v == d.sel) { g.setColour(Colour(0x30ffffff)); g.fillRoundedRectangle(k.cx - 46, 860, 80, 5, 2); }
    }
    g.setColour(WHITE);
    for (auto br : {std::array<float, 3>{1094, 1194, 1146}, std::array<float, 3>{1292, 1392, 1344}}) {
        g.drawLine(br[0], 848, br[0], 862, 2); g.drawLine(br[0], 862, br[2] - 40, 862, 2); g.drawLine(br[2] + 40, 862, br[1], 862, 2); g.drawLine(br[1], 848, br[1], 862, 2);
    }
    txt(g, 1146, 868, "CYMBAL", 13); txt(g, 1344, 868, "HI HAT", 13);
    // keys and knobs
    for (int i = 0; i < NK; ++i) keyCap(g, KEYS[i], i == down);
    for (int k = 0; k < 4; ++k) knob(g, KNOBS[k].x, KNOBS[k].y, KNOBS[k].r, knobNorm(k), k >= 2);
}

/* ---------------- knobs and buttons for the pages ---------------- */
void TraksLook::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    const float r = (float) std::min(w, h) / 2 - 4, cx = (float) x + (float) w / 2, cy = (float) y + (float) h / 2;
    g.setColour(WHITE.withAlpha(0.8f));
    for (int i = 0; i <= 10; ++i) { const float a = a0 + (a1 - a0) * (float) i / 10; g.fillEllipse(cx + std::sin(a) * r - 1.5f, cy - std::cos(a) * r - 1.5f, 3, 3); }
    const float kr = r * 0.8f;
    g.setColour(Colour(0xffe8e8e8)); g.fillEllipse(cx - kr, cy - kr, kr * 2, kr * 2);
    g.setGradientFill(juce::ColourGradient(Colour(0xff3c3c3c), cx - kr * 0.4f, cy - kr * 0.5f, Colour(0xff050505), cx + kr, cy + kr, true));
    g.fillEllipse(cx - kr * 0.86f, cy - kr * 0.86f, kr * 1.72f, kr * 1.72f);
    const float a = a0 + pos * (a1 - a0);
    Path p; p.startNewSubPath(cx + std::sin(a) * kr * 0.2f, cy - std::cos(a) * kr * 0.2f); p.lineTo(cx + std::sin(a) * kr * 0.95f, cy - std::cos(a) * kr * 0.95f);
    g.setColour(WHITE); g.strokePath(p, juce::PathStrokeType(std::max(2.0f, kr * 0.14f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
void TraksLook::drawButtonBackground(Graphics& g, juce::Button& b, const Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.5f);
    const bool on = b.getToggleState();
    g.setColour(Colour(0xff000000)); g.fillRoundedRectangle(r, 4);
    r = r.reduced(2.0f).translated(0, down ? 1.5f : 0);
    g.setGradientFill(juce::ColourGradient(Colour(over ? 0xff3d3d3d : 0xff2e2e2e), 0, r.getY(), Colour(0xff151515), 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, 3);
    g.setColour(on ? LEDON : Colour(0xff4a4a4a)); g.drawRoundedRectangle(r, 3, on ? 2.5f : 1.2f);
    if (on) { g.setColour(LEDON); g.fillEllipse(r.getX() + 6, r.getCentreY() - 4, 8, 8); }
}
void TraksLook::drawButtonText(Graphics& g, juce::TextButton& b, bool, bool) {
    g.setColour(WHITE);
    g.setFont(font(std::min(15.0f, (float) b.getHeight() * 0.42f)));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(b.getToggleState() ? 14 : 4, 2), juce::Justification::centred, 2, 0.6f);
}

/* ---------------- editor ---------------- */
static void drawEq(Graphics& g, Rectangle<float> r, const std::vector<std::array<double, 4>>& bands) {
    g.setColour(Colour(0xff1d0507)); g.fillRoundedRectangle(r, 8);
    g.setColour(Colour(0x30ff2440));
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0}) { const float x = r.getX() + (float) (std::log(f / 20) / std::log(1000.0)) * r.getWidth(); g.drawVerticalLine((int) x, r.getY(), r.getBottom()); }
    for (int d : {-12, -6, 0, 6, 12}) { const float y = r.getCentreY() - (float) d / 18 * r.getHeight() / 2; g.drawHorizontalLine((int) y, r.getX(), r.getRight()); }
    std::vector<Biquad> bq(bands.size());
    for (size_t k = 0; k < bands.size(); ++k) bq[k].set((int) bands[k][0], bands[k][1], bands[k][2], bands[k][3], 48000);
    Path p;
    for (int i = 0; i <= 200; ++i) {
        const double f = 20 * std::pow(1000.0, i / 200.0), w = 2 * PI * f / 48000; double mag = 0;
        for (const auto& b : bq) { if (!b.on) continue;
            const double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
            const double nr = b.b0 + b.b1 * cr + b.b2 * c2r, ni = b.b1 * ci + b.b2 * c2i, dr = 1 + b.a1 * cr + b.a2 * c2r, di = b.a1 * ci + b.a2 * c2i;
            mag += 10 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di)); }
        const float x = r.getX() + (float) i / 200 * r.getWidth(), y = r.getCentreY() - (float) juce::jlimit(-18.0, 18.0, mag) / 18 * (r.getHeight() / 2 - 4);
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    g.setColour(SEGON); g.strokePath(p, juce::PathStrokeType(3.5f));
}
static void drawAdsr(Graphics& g, Rectangle<float> r, double a, double d, double s, double rel) {
    g.setColour(Colour(0xff1d0507)); g.fillRoundedRectangle(r, 8);
    const bool hold = rel >= 3.999; const double dd = std::min(d, 1.5), total = a + dd + 0.4 + (hold ? 0.6 : std::min(rel, 2.0));
    auto X = [&](double t) { return r.getX() + 10 + (float) (t / total) * (r.getWidth() - 20); };
    auto Y = [&](double v) { return r.getBottom() - 10 - (float) v * (r.getHeight() - 20); };
    Path p; p.startNewSubPath(X(0), Y(a > 0 ? 0 : 1)); p.lineTo(X(a), Y(1));
    for (int i = 1; i <= 30; ++i) { const double t = dd * i / 30; p.lineTo(X(a + t), Y(s + (1 - s) * std::exp(-t / (d / 3)))); }
    const double sv = s + (1 - s) * std::exp(-dd / (d / 3)), t0 = a + dd + 0.4; p.lineTo(X(t0), Y(sv));
    if (hold) p.lineTo(X(total), Y(sv));
    else { const double rr = std::min(rel, 2.0); for (int i = 1; i <= 30; ++i) { const double t = rr * i / 30; p.lineTo(X(t0 + t), Y(sv * std::exp(-t / (rel / 3)))); } }
    g.setColour(SEGON); g.strokePath(p, juce::PathStrokeType(3.5f));
    txt(g, X(t0) + 6, r.getY() + 26, hold ? "release: hold" : "release", 18, Colour(0x99ff2440), -1);
}

TraksEditor::TraksEditor(TraksProcessor& p) : juce::AudioProcessorEditor(p), proc(p), plate(p) {
    addAndMakeVisible(plate);
    plate.onSave = [this] { saveTape(); };
    plate.onLoad = [this] { loadTape(); };
    const char* tn[NT] = {"SOUND", "PAD EQ / ADSR", "MIXER", "REV 7 / TC DELAY", "MASTER / MACHINE"};
    for (int i = 0; i < NT; ++i) { tabs[i].setButtonText(tn[i]); tabs[i].setLookAndFeel(&look); tabs[i].onClick = [this, i] { showTab(i); }; addAndMakeVisible(tabs[i]); }
    for (auto* b : {&prevBtn, &nextBtn, &diceBtn, &foldBtn, &loadBtn, &factBtn, &metroBtn}) { b->setLookAndFeel(&look); addAndMakeVisible(*b); }
    prevBtn.onClick = [this] { proc.setCurrentProgram((proc.getCurrentProgram() + kPresets - 1) % kPresets); };
    nextBtn.onClick = [this] { proc.setCurrentProgram((proc.getCurrentProgram() + 1) % kPresets); };
    diceBtn.onClick = [this] { proc.dice(partBox.getSelectedItemIndex()); };
    loadBtn.onClick = [this] { loadSound(); };
    factBtn.onClick = [this] { proc.resetSound(pad); repaint(); };
    metroBtn.setClickingTogglesState(true); metroBtn.setToggleState(proc.getMetroAlways(), juce::dontSendNotification);
    metroBtn.onClick = [this] { proc.setMetroAlways(metroBtn.getToggleState()); };
    fillPresets();
    presetBox.onChange = [this] { const int id = presetBox.getSelectedId(); if (id > 0 && id - 1 != proc.getCurrentProgram()) proc.setCurrentProgram(id - 1); };
    const char* parts[] = {"Everything", "Sounds", "Pad EQ", "ADSR + drive", "Reverb + delay", "Master EQ + drive"};
    for (int i = 0; i < 6; ++i) partBox.addItem(parts[i], i + 1);
    partBox.setSelectedItemIndex(0, juce::dontSendNotification);
    for (auto* c : {&presetBox, &partBox}) {
        c->setColour(juce::ComboBox::backgroundColourId, Colour(0xff1d0507)); c->setColour(juce::ComboBox::textColourId, SEGON);
        c->setColour(juce::ComboBox::outlineColourId, juce::Colours::black); c->setColour(juce::ComboBox::arrowColourId, SEGON);
        addAndMakeVisible(*c);
    }
    proc.onProgram = [this] {
        juce::Component::SafePointer<TraksEditor> safe(this);
        juce::MessageManager::callAsync([safe] { if (auto* ed = safe.getComponent()) ed->presetBox.setSelectedId(ed->proc.getCurrentProgram() + 1, juce::dontSendNotification); });
    };
    auto R = [](int x, int y, int w, int h) { return Rectangle<int>(x, y, w, h); };
    for (int v = 0; v < NV; ++v) { padBtn[v].setButtonText(KEY_NAMES[v]); padBtn[v].setLookAndFeel(&look); padBtn[v].onClick = [this, v] { selectPad(v); }; addAndMakeVisible(padBtn[v]); }
    // ---- SOUND: the sound picked (also by its key on the panel)
    boxes.push_back({"SOUND", 0, R(20, 80, 2080, 300)});
    const char* sk[][2] = {{"lv", "VOLUME"}, {"tune", "TUNING"}, {"dec", "DECAY"}, {"pan", "PAN"}, {"cut", "CUTOFF"}, {"res", "RESONANCE"}, {"start", "START"}, {"snd", "REVERB SEND"}, {"out", "OUTPUT"}};
    for (int i = 0; i < 9; ++i) addKnob(0, "", sk[i][1], R(40 + i * 225, 135, 200, 170), sk[i][0]);
    addTog(0, "", "REVERSE", R(40, 410, 250, 54), "rev"); addTog(0, "", "MUTE", R(310, 410, 250, 54), "mute");
    boxes.push_back({"ACCENT", 0, R(1200, 395, 900, 210)});
    addKnob(0, "accent", "ACCENT LEVEL", R(1260, 440, 200, 120)); addKnob(0, "accVel", "ACCENT VELOCITY (MIDI)", R(1560, 440, 240, 120));
    // ---- PAD EQ / ADSR
    boxes.push_back({"PARAMETRIC EQ", 1, R(20, 80, 1150, 530)}); boxes.push_back({"ADSR / DRIVE / DELAY SEND", 1, R(1190, 80, 910, 530)});
    const char* eqs[][2] = {{"lsF", "LOW FREQ"}, {"lsG", "LOW GAIN"}, {"pkF", "MID FREQ"}, {"pkG", "MID GAIN"}, {"pkQ", "MID Q"}, {"hsF", "HIGH FREQ"}, {"hsG", "HIGH GAIN"}};
    for (int i = 0; i < 7; ++i) addKnob(1, "", eqs[i][1], R(40 + i * 160, 400, 150, 150), eqs[i][0]);
    const char* envs[][2] = {{"atk", "ATTACK"}, {"dcy", "DECAY"}, {"sus", "SUSTAIN"}, {"rel", "RELEASE"}, {"drv", "DRIVE"}, {"dls", "DELAY SEND"}};
    for (int i = 0; i < 6; ++i) addKnob(1, "", envs[i][1], R(1205 + i * 148, 400, 140, 150), envs[i][0]);
    // ---- MIXER
    for (int v = 0; v < NV; ++v) {
        const int x = 20 + v * 160;
        boxes.push_back({KEY_NAMES[v], 2, R(x, 10, 150, 600)});
        const juce::String id(VOICES[v]);
        addKnob(2, id + "lv", "VOLUME", R(x + 5, 60, 140, 120));
        addKnob(2, id + "pan", "PAN", R(x + 15, 210, 120, 100)); addKnob(2, id + "snd", "REV", R(x + 15, 335, 120, 100));
        addKnob(2, id + "dls", "DELAY", R(x + 15, 460, 120, 100));
        addTog(2, id + "mute", "MUTE", R(x + 10, 572, 130, 34));
    }
    // ---- REV 7 / TC DELAY
    boxes.push_back({"REV 7 DIGITAL REVERBERATOR", 3, R(20, 10, 1040, 600)});
    const char* rv[][2] = {{"rvProg", "PROGRAM"}, {"rvTime", "REV TIME"}, {"rvHigh", "HIGH"}, {"rvDelay", "DELAY"}, {"rvDiff", "DIFFUSION"}, {"rvEr", "ER LEVEL"}, {"rvCut", "HIGH CUT"}, {"rvLevel", "RETURN"}};
    for (int i = 0; i < 8; ++i) addKnob(3, rv[i][0], rv[i][1], R(40 + (i % 4) * 250, 70 + (i / 4) * 270, 230, 190));
    boxes.push_back({"TC 2290-STYLE DYNAMIC DELAY", 3, R(1080, 10, 1020, 600)});
    const char* dl[][2] = {{"dlTime", "TIME"}, {"dlSync", "SYNC"}, {"dlFb", "FEEDBACK"}, {"dlHiCut", "HIGH CUT"}, {"dlLoCut", "LOW CUT"}, {"dlModRate", "MOD RATE"},
        {"dlModDepth", "MOD DEPTH"}, {"dlDuck", "DUCKING"}, {"dlLevel", "RETURN"}};
    for (int i = 0; i < 9; ++i) addKnob(3, dl[i][0], dl[i][1], R(1100 + (i % 3) * 330, 60 + (i / 3) * 175, 300, 125));
    addTog(3, "dlPing", "PING-PONG", R(1440, 560, 300, 44));
    // ---- MASTER / MACHINE
    boxes.push_back({"MASTER PARAMETRIC EQ", 4, R(20, 10, 1150, 600)});
    const char* meq[][2] = {{"mLsF", "LOW FREQ"}, {"mLsG", "LOW GAIN"}, {"mP1F", "MID 1 FREQ"}, {"mP1G", "MID 1 GAIN"}, {"mP1Q", "MID 1 Q"},
        {"mP2F", "MID 2 FREQ"}, {"mP2G", "MID 2 GAIN"}, {"mP2Q", "MID 2 Q"}, {"mHsF", "HIGH FREQ"}, {"mHsG", "HIGH GAIN"}};
    for (int i = 0; i < 10; ++i) addKnob(4, meq[i][0], meq[i][1], R(40 + (i % 5) * 225, 280 + (i / 5) * 165, 210, 120));
    boxes.push_back({"OVERDRIVE / CLIPPER", 4, R(1190, 10, 910, 230)});
    const char* odk[][2] = {{"odDrive", "DRIVE"}, {"odMode", "MODE"}, {"odMix", "MIX"}, {"odOut", "OUTPUT"}};
    for (int i = 0; i < 4; ++i) addKnob(4, odk[i][0], odk[i][1], R(1210 + i * 222, 55, 200, 140));
    boxes.push_back({"MACHINE", 4, R(1190, 255, 910, 355)});
    addKnob(4, "dac", "DAC", R(1210, 300, 200, 130)); addKnob(4, "tempo", "TEMPO (OWN CLOCK)", R(1430, 300, 220, 130));
    addTog(4, "interp", "SMOOTH PLAYBACK", R(1680, 300, 400, 44)); addTog(4, "poly", "POLY (NO CHANNEL CUT)", R(1680, 350, 400, 44));
    addTog(4, "filt", "OUTPUT FILTER BYPASS", R(1680, 400, 400, 44)); addTog(4, "follow", "FOLLOW DAW TRANSPORT", R(1680, 450, 400, 44));
    selectPad(proc.selected.load());
    showTab(0);
    setResizable(true, true);
    setFolded((bool) proc.apvts.state.getProperty("folded", false));
    startTimerHz(15);
}
TraksEditor::~TraksEditor() {
    stopTimer();
    proc.onProgram = nullptr;
    for (auto& t : tabs) t.setLookAndFeel(nullptr);
    for (auto& b : padBtn) b.setLookAndFeel(nullptr);
    for (auto* b : {&prevBtn, &nextBtn, &diceBtn, &foldBtn, &loadBtn, &factBtn, &metroBtn}) b->setLookAndFeel(nullptr);
    for (auto& k : knobs) { k.a.reset(); k.s->setLookAndFeel(nullptr); }
    for (auto& t : togs) t.b->setLookAndFeel(nullptr);
}
void TraksEditor::setFolded(bool f) {
    folded = f;
    proc.apvts.state.setProperty("folded", f, nullptr);
    foldBtn.setButtonText(f ? "EXPAND" : "FOLD");
    for (int i = 0; i < NT; ++i) tabs[i].setVisible(!f);
    for (auto* c : std::initializer_list<juce::Component*>{&prevBtn, &nextBtn, &diceBtn, &presetBox, &partBox}) c->setVisible(!f);
    if (f) { for (auto& k : knobs) k.s->setVisible(false); for (auto& g : togs) g.b->setVisible(false); for (auto& b : padBtn) b.setVisible(false);
        for (auto* b : {&loadBtn, &factBtn, &metroBtn}) b->setVisible(false); }
    else showTab(tab);
    const int h = f ? PH + 60 : PH + MH, w = getWidth() > 0 ? getWidth() : 1100;
    getConstrainer()->setFixedAspectRatio((double) W / h);
    setResizeLimits(700, (int) (700.0 * h / W), 2600, (int) (2600.0 * h / W));
    setSize(w, (int) std::round((double) w * h / W));
}
void TraksEditor::fillPresets() {
    presetBox.clear(juce::dontSendNotification);
    juce::String fam;
    for (int i = 0; i < kPresets; ++i) {
        const juce::String name(juce::CharPointer_UTF8(PRESETS[i].name));
        const juce::String f = name.upToFirstOccurrenceOf(juce::String::fromUTF8(" \xc2\xb7 "), false, false);
        if (f != fam) { fam = f; presetBox.addSectionHeading(fam); }
        presetBox.addItem(juce::String(i + 1).paddedLeft('0', 3) + " " + name.fromFirstOccurrenceOf(juce::String::fromUTF8(" \xc2\xb7 "), false, false), i + 1);
    }
    presetBox.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
}
TraksEditor::Knob& TraksEditor::addKnob(int t, const juce::String& id, const juce::String& label, Rectangle<int> r, const juce::String& suffix) {
    Knob k; k.s = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    k.s->setLookAndFeel(&look);
    k.s->setColour(juce::Slider::textBoxTextColourId, SEGON); k.s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.s->setRotaryParameters(juce::degreesToRadians(-135.0f) + juce::MathConstants<float>::twoPi, juce::degreesToRadians(135.0f) + juce::MathConstants<float>::twoPi, true);
    if (suffix.isEmpty()) k.a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, *k.s);
    const juce::String f = suffix.isNotEmpty() ? suffix : id;
    auto hz = [](double v) { return v >= 1000 ? juce::String(v / 1000.0, v >= 10000 ? 1 : 2) + " kHz" : juce::String((int) std::round(v)) + " Hz"; };
    auto secs = [](double v) { return v >= 1 ? juce::String(v, 2) + " s" : juce::String((int) std::round(v * 1000)) + " ms"; };
    auto pct = [](double v) { return juce::String((int) std::round(v * 100)) + "%"; };
    auto dB = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " dB"; };
    auto ends = [&](const char* e) { return f.endsWith(e); };
    std::function<juce::String(double)> fn = [](double v) { return juce::String(v, 2); };
    if (f == "rvProg") fn = [](double v) { return juce::String(RV_PROGS[juce::jlimit(0, 4, (int) std::round(v))]); };
    else if (f == "odMode") fn = [](double v) { return juce::String(OD_MODES[juce::jlimit(0, 2, (int) std::round(v))]); };
    else if (f == "dlSync") fn = [](double v) { return juce::String(DL_DIV_NAMES[juce::jlimit(0, 13, (int) std::round(v))]); };
    else if (f == "dac") fn = [](double v) { return juce::String(DAC_MODES[juce::jlimit(0, 2, (int) std::round(v))]); };
    else if (f == "out") fn = [](double v) { const int o = (int) std::round(v); return o == 0 ? juce::String("ITS CHANNEL") : "CH " + juce::String(o); };
    else if (f == "tempo") fn = [](double v) { return juce::String((int) std::round(v)) + " BPM"; };
    else if (f == "lv") fn = [](double v) { return juce::String(juce::jlimit(0, 99, (int) std::round(v * 99))); };
    else if (ends("tune")) fn = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " st"; };
    else if (ends("dec")) fn = [secs](double v) { return v >= 2.999 ? juce::String("FULL") : secs(v); };
    else if (f == "cut") fn = [hz](double v) { return v >= 19999 ? juce::String("OPEN") : hz(v); };
    else if (f == "rvTime") fn = [](double v) { return juce::String(v, 1) + " s"; };
    else if (f == "rvDelay" || f == "dlTime") fn = [](double v) { return juce::String((int) std::round(v)) + " ms"; };
    else if (ends("F") || ends("Cut")) fn = hz;
    else if (ends("G") || f == "odOut" || f == "accent") fn = dB;
    else if (f == "dlModRate") fn = [](double v) { return juce::String(v, 2) + " Hz"; };
    else if (ends("atk") || ends("dcy")) fn = secs;
    else if (ends("rel")) fn = [secs](double v) { return v >= 3.999 ? juce::String("HOLD") : secs(v); };
    else if (f == "sus" || f == "res" || f == "start" || ends("lv") || ends("drv") || ends("dls") || ends("snd") || f == "dlFb" || f == "dlModDepth" || f == "dlDuck" || f == "dlLevel" || f == "odDrive" || f == "odMix") fn = pct;
    else if (f == "accVel" || f == "rvDiff") fn = [](double v) { return juce::String((int) std::round(v)); };
    else if (ends("pan")) fn = [](double v) { return std::abs(v) < 0.005 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String((int) std::round(std::abs(v) * 100)); };
    if (ends("lv") && suffix.isEmpty()) fn = [](double v) { return juce::String(juce::jlimit(0, 99, (int) std::round(v * 99))); };
    k.s->textFromValueFunction = fn; k.fmt = fn;
    k.s->updateText();
    k.label = label; k.suffix = suffix; k.tab = t; k.r = r.translated(0, TOP);
    addAndMakeVisible(*k.s);
    knobs.push_back(std::move(k));
    return knobs.back();
}
void TraksEditor::addTog(int t, const juce::String& id, const juce::String& label, Rectangle<int> r, const juce::String& suffix) {
    Tog g; g.b = std::make_unique<juce::TextButton>(label); g.b->setClickingTogglesState(true); g.b->setLookAndFeel(&look);
    if (suffix.isEmpty()) g.a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, *g.b);
    g.tab = t; g.r = r.translated(0, TOP); g.suffix = suffix; addAndMakeVisible(*g.b); togs.push_back(std::move(g));
}
void TraksEditor::selectPad(int v) {
    pad = v; lastSel = v;
    for (int i = 0; i < NV; ++i) padBtn[i].setToggleState(i == v, juce::dontSendNotification);
    for (auto& k : knobs) if (k.suffix.isNotEmpty()) {
        k.a.reset();
        k.a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, juce::String(VOICES[v]) + k.suffix, *k.s);
        k.s->textFromValueFunction = k.fmt; k.s->updateText();
    }
    for (auto& g : togs) if (g.suffix.isNotEmpty()) { g.a.reset(); g.a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, juce::String(VOICES[v]) + g.suffix, *g.b); }
    repaint();
}
void TraksEditor::showTab(int t) {
    tab = t;
    if (folded) return;
    for (int i = 0; i < NT; ++i) tabs[i].setToggleState(i == t, juce::dontSendNotification);
    for (auto& k : knobs) k.s->setVisible(k.tab == t);
    for (auto& g : togs) g.b->setVisible(g.tab == t);
    for (auto& b : padBtn) b.setVisible(t <= 1);
    loadBtn.setVisible(t == 0); factBtn.setVisible(t == 0); metroBtn.setVisible(t == 4);
    resized(); repaint();
}
void TraksEditor::timerCallback() {
    // the sound picked on the panel (a key, a MIDI note) is the one on the SOUND and PAD pages
    const int s = proc.selected.load(); if (s != lastSel) selectPad(s);
    juce::uint32 h = 7;
    for (int i = 0; i < NP; ++i) h = h * 31 + (juce::uint32) (proc.value(i) * 1000);
    if (h != lastParamsHash) { lastParamsHash = h; repaint(); }
}
void TraksEditor::saveTape() {
    chooser = std::make_unique<juce::FileChooser>("Save the machine (memory, sounds, settings)", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("omega-traks.json"), "*.json");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult(); if (f == juce::File()) return; f.replaceWithText(proc.backupJson()); });
}
void TraksEditor::loadTape() {
    chooser = std::make_unique<juce::FileChooser>("Load a machine backup (.json, from the plugin or the app)", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult(); if (f.existsAsFile() && !proc.restoreJson(f.loadFileAsString())) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Omega Traks", "Not an Omega Traks backup."); });
}
void TraksEditor::loadSound() {
    chooser = std::make_unique<juce::FileChooser>(juce::String("A sound for ") + KEY_NAMES[pad] + " (burned to its ROM: 25 kHz, 8-bit companded)", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    const int v = pad;
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, v](const juce::FileChooser& fc) {
        const auto f = fc.getResult(); if (f.existsAsFile() && !proc.loadSound(v, f)) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Omega Traks", "Couldn't read that file."); repaint(); });
}
void TraksEditor::paint(Graphics& g) {
    const float s = (float) getWidth() / (float) W;
    g.fillAll(Colour(0xff101010));
    if (folded) return;
    g.saveState();
    g.addTransform(juce::AffineTransform::scale(s).translated(0, (float) plate.getBottom()));
    g.setColour(PANEL); g.fillRect(0, 0, W, MH);
    g.setColour(Colour(0xff050505)); g.fillRoundedRectangle(14, 80, W - 28, 58, 10);
    for (const auto& b : boxes) if (b.tab == tab) {
        const auto r = b.r.translated(0, TOP).toFloat();
        g.setColour(WHITE); g.drawRoundedRectangle(r, 14, 2.5f);
        txt(g, r.getCentreX(), r.getY() + 30, b.title, 20);
    }
    for (const auto& k : knobs) if (k.tab == tab) txt(g, (float) k.r.getCentreX(), (float) k.r.getBottom() + 22, k.label, 15);
    static const std::map<juce::String, int> index = [] { std::map<juce::String, int> m; const auto ps = params(); for (int i = 0; i < NP; ++i) m[juce::String(ps[(size_t) i].id)] = i; return m; }();
    auto v = [this](const juce::String& id) { const auto it = index.find(id); return it == index.end() ? 0.0 : proc.value(it->second); };
    if (tab == 0) {
        txt(g, 600, TOP + 445, juce::String("ROM: ") + proc.romName(pad), 18, SEGON, -1);
        txt(g, 40, TOP + 520, "Pick a sound with its key on the panel or here. LOAD SOUND burns a file into its ROM", 15, WHITE.withAlpha(0.7f), -1);
        txt(g, 40, TOP + 545, "(resampled to 25 kHz, 8-bit companded) and it plays through the machine like the original.", 15, WHITE.withAlpha(0.7f), -1);
    } else if (tab == 1) {
        const juce::String P(VOICES[pad]);
        drawEq(g, Rectangle<float>(40, TOP + 130, 1110, 230), {{0, v(P + "lsF"), v(P + "lsG"), 0.707}, {1, v(P + "pkF"), v(P + "pkG"), v(P + "pkQ")}, {2, v(P + "hsF"), v(P + "hsG"), 0.707}});
        drawAdsr(g, Rectangle<float>(1210, TOP + 130, 870, 230), v(P + "atk"), v(P + "dcy"), v(P + "sus"), v(P + "rel"));
    } else if (tab == 2) {
        for (int i = 0; i < NV; ++i) { const float m = proc.meters[(size_t) i].load(); const float db = 20 * std::log10(std::max(1e-4f, m)); const float h = juce::jlimit(0.0f, 1.0f, (db + 48) / 48) * 540;
            g.setColour(Colour(0xff000000)); g.fillRect(20.0f + i * 160 + 140, (float) TOP + 60, 6.0f, 540.0f);
            g.setColour(SEGON); g.fillRect(20.0f + i * 160 + 140, (float) TOP + 600 - h, 6.0f, h); }
    } else if (tab == 4)
        drawEq(g, Rectangle<float>(40, TOP + 60, 1110, 190), {{0, v("mLsF"), v("mLsG"), 0.707}, {1, v("mP1F"), v("mP1G"), v("mP1Q")}, {1, v("mP2F"), v("mP2G"), v("mP2Q")}, {2, v("mHsF"), v("mHsG"), 0.707}});
    g.restoreState();
}
void TraksEditor::resized() {
    const float s = (float) getWidth() / (float) W;
    plate.setBounds(0, 0, getWidth(), (int) std::round(PH * s));
    const int y0 = plate.getBottom();
    auto sc = [&](Rectangle<int> r) { return Rectangle<int>((int) std::round(r.getX() * s), y0 + (int) std::round(r.getY() * s), (int) std::round(r.getWidth() * s), (int) std::round(r.getHeight() * s)); };
    foldBtn.setBounds(sc({1880, folded ? 8 : 86, 220, 46}));
    for (int i = 0; i < NT; ++i) tabs[i].setBounds(sc({20 + i * 416, 12, 404, 56}));
    prevBtn.setBounds(sc({24, 86, 70, 46})); presetBox.setBounds(sc({100, 86, 900, 46})); nextBtn.setBounds(sc({1006, 86, 70, 46}));
    diceBtn.setBounds(sc({1110, 86, 200, 46})); partBox.setBounds(sc({1320, 86, 330, 46}));
    for (int v = 0; v < NV; ++v) padBtn[v].setBounds(sc({20 + v * 160, TOP + 15, 150, 50}));
    if (tab == 1) for (int v = 0; v < NV; ++v) padBtn[v].setBounds(sc({20 + v * 160, TOP + 15, 150, 50}));
    loadBtn.setBounds(sc({600, TOP + 470, 280, 54})); factBtn.setBounds(sc({900, TOP + 470, 280, 54}));
    metroBtn.setBounds(sc({1680, TOP + 500, 400, 44}));
    for (auto& k : knobs) { k.s->setBounds(sc(k.r)); k.s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, (int) (k.r.getWidth() * s), std::max(12, (int) (24 * s))); }
    for (auto& g : togs) g.b->setBounds(sc(g.r));
}

} // namespace traks
