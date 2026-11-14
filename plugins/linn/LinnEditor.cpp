#include "LinnEditor.h"
#include <map>

namespace linn {

using juce::Colour; using juce::Graphics; using juce::Path; using juce::Rectangle;

/* ---------------- the front panel: the layout of linn/faceplate.js (plate units 2000 x 1090, + 60 cheeks) ---------------- */
namespace lay {
constexpr float CHEEK = 60, PW = 2000, PH = 1090;
const float FN_X[8] = {262, 372, 454, 536, 618, 700, 782, 864}; constexpr float FN_Y = 432, FN_S = 62;
const float COL_X[7] = {372, 454, 536, 618, 700, 782, 864};
constexpr float TIM_Y = 552, EC_Y = 604, DIG_Y = 692, DIG_S = 62;
const float DISP[2][2] = {{222, 528}, {222, 646}};
constexpr float CAS_X = 115, STORE_Y = 432, LOAD_Y = 690;
const float TEMPO[3] = {1042, 424, 36}; constexpr float BPM_Y = 548, EXT_Y = 692, SIDE_S = 62;
const float TUNE_X[7] = {1180, 1268, 1356, 1444, 1532, 1620, 1708}; constexpr float TUNE_Y = 420, TUNE_R = 30;
const float VOL[3] = {1873, 424, 38};
constexpr float MIX_X0 = 1160, MIX_DX = 51.6f; const float PAN_Y[2] = {548, 694}, LVL_Y[2] = {800, 1020};
const float DRUM_X[8] = {104, 210, 316, 422, 528, 634, 740, 846}; constexpr float DRUM_TOP = 868, DRUM_BOT = 994, DRUM_S = 70;
const float PLAY[3] = {1042, 950, 84};
struct BoxDef { float x, y, w, h; };
const BoxDef BOXES[] = {{40, 330, 150, 440}, {205, 330, 760, 440}, {980, 330, 125, 440}, {1120, 330, 650, 180}, {1785, 330, 175, 180},
    {1120, 525, 840, 555}, {40, 790, 925, 290}, {980, 790, 125, 290}};
struct KeyDef { int id; float cx, cy, s; };
inline std::vector<KeyDef> keys() {
    std::vector<KeyDef> k;
    k.push_back({K::STORE, CAS_X, STORE_Y, FN_S}); k.push_back({K::LOAD, CAS_X, LOAD_Y, FN_S});
    for (int i = 0; i < 8; ++i) k.push_back({i, FN_X[i], FN_Y, FN_S});
    for (int i = 0; i < 7; ++i) k.push_back({K::D1 + i, COL_X[i], DIG_Y, DIG_S});
    k.push_back({K::BPM, 1042, BPM_Y, SIDE_S}); k.push_back({K::EXTSYNC, 1042, EXT_Y, SIDE_S});
    for (int i = 0; i < 8; ++i) k.push_back({K::DRUM + 8 + i, DRUM_X[i], DRUM_TOP, DRUM_S});
    for (int i = 0; i < 8; ++i) k.push_back({K::DRUM + i, DRUM_X[i], DRUM_BOT, DRUM_S});
    k.push_back({K::PLAY, PLAY[0], PLAY[1], PLAY[2]});
    return k;
}
struct KnobDef { float x, y, r; };
inline std::vector<KnobDef> knobs() {
    std::vector<KnobDef> k = {{TEMPO[0], TEMPO[1], TEMPO[2]}};
    for (float x : TUNE_X) k.push_back({x, TUNE_Y, TUNE_R});
    k.push_back({VOL[0], VOL[1], VOL[2]});
    return k;
}
const char* const FN_TOP[8] = {"SONG", "SONG#", "END", "DELETE", "INSERT", "<-", "->", "ENTER"};
const char* const FN_BOT[8] = {"PATTERN", "ERROR", "BEFORE", "AFTER", "COPY", "LENGTH", "ERASE", "RECORD"};
const char* const MIX_N[16][2] = {{"BASS", ""}, {"SNARE", ""}, {"SIDE", "STICK"}, {"HIHAT", ""}, {"HI", ""}, {"MID", ""}, {"LO", ""}, {"RIDE", ""}, {"CRASH", ""},
    {"CABA", "SA"}, {"TAMB", ""}, {"HI", ""}, {"LO", ""}, {"COW", "BELL"}, {"CLAPS", ""}, {"CLICK", ""}};
}
const Colour LN(0xfff25a2c), WHITE(0xfff0f0ee), PANEL(0xff141414), LEDON(0xffff2a3a), LEDOFF(0xff3a1418), SEGON(0xffff2440), SEGOFF(0xff260709);
static const int KNOB_P[9] = {pid::tempo, pid::tSN, pid::tHT, pid::tMT, pid::tLT, pid::tHC, pid::tLC, pid::hatDecay, pid::volume};

static juce::Font font(float h, bool bold = true) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : 0)); }
static void txt(Graphics& g, float x, float y, const juce::String& s, float size, Colour c = LN, int anchor = 0, bool bold = true) {
    g.setColour(c); g.setFont(font(size, bold));
    const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), s) + 4;
    const float x0 = anchor < 0 ? x : anchor > 0 ? x - w : x - w / 2;
    g.drawText(s, Rectangle<float>(x0, y - size * 0.85f, w, size * 1.1f), anchor < 0 ? juce::Justification::left : anchor > 0 ? juce::Justification::right : juce::Justification::centred, false);
}
static void line(Graphics& g, std::initializer_list<float> pts, float w = 2) {
    Path p; bool first = true; auto it = pts.begin();
    while (it != pts.end()) { const float x = *it++; const float y = *it++; if (first) p.startNewSubPath(x, y); else p.lineTo(x, y); first = false; }
    g.setColour(LN); g.strokePath(p, juce::PathStrokeType(w));
}
static Path poly(std::initializer_list<juce::Point<float>> pts) { Path p; bool first = true; for (auto q : pts) { if (first) p.startNewSubPath(q); else p.lineTo(q); first = false; } p.closeSubPath(); return p; }
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
    g.setColour(Colour(0xff050505)); g.fillRoundedRectangle(x, y, 104, 64, 4);
    g.setColour(Colour(0xff1d0507)); g.fillRoundedRectangle(x + 5, y + 5, 94, 54, 3);
    for (int i = 0; i < 2; ++i) seg7(g, x + 15 + i * 42, y + 10, 31, 44, i < s.length() ? (char) s[i] : ' ');
}
static void led(Graphics& g, float x, float y, bool on, float r = 6) {
    if (on) { g.setColour(LEDON.withAlpha(0.35f)); g.fillEllipse(x - r * 2.2f, y - r * 2.2f, r * 4.4f, r * 4.4f); }
    g.setColour(on ? LEDON : LEDOFF); g.fillEllipse(x - r, y - r, r * 2, r * 2);
    g.setColour(juce::Colours::white.withAlpha(on ? 0.6f : 0.15f)); g.fillEllipse(x - r * 0.45f, y - r * 0.6f, r * 0.6f, r * 0.5f);
}
static void keyCap(Graphics& g, float cx, float cy, float s, bool down) {
    const float x = cx - s / 2, y = cy - s / 2 + (down ? 3.0f : 0.0f);
    g.setColour(Colour(0xff000000)); g.fillRoundedRectangle(x - 5, y - 4 + (down ? -3.0f : 0.0f), s + 10, s + 12, 4);
    g.setGradientFill(juce::ColourGradient(Colour(down ? 0xff1c1c1c : 0xff2c2c2c), x, y, Colour(0xff0f0f0f), x, y + s, false)); g.fillRoundedRectangle(x, y, s, s, 3);
    g.setColour(Colour(0xff3c3c3c)); g.drawRoundedRectangle(x, y, s, s, 3, 1.5f);
    g.setColour(Colour(0x8cffffff)); g.fillRoundedRectangle(x + 4, y + 4, s - 8, 3.5f, 1.5f);    // the light edge along the top
}
static void wood(Graphics& g, float x, float w, float h) {
    g.setGradientFill(juce::ColourGradient(Colour(0xff7a3c15), x, 0, Colour(0xffc4752f), x + w * 0.55f, 0, false)); g.fillRoundedRectangle(x, 0, w, h, 6);
    juce::Random r(7);
    for (int i = 0; i < 9; ++i) { const float gx = x + 6 + r.nextFloat() * (w - 12); g.setColour(Colour(0x30401a05)); g.drawLine(gx, 4, gx + r.nextFloat() * 4 - 2, h - 4, 1.5f + r.nextFloat() * 2); }
    g.setColour(Colour(0x40ffffff)); g.drawLine(x + w * 0.7f, 6, x + w * 0.7f, h - 6, 2);
}
// a knurled silver knob
static void knob(Graphics& g, float cx, float cy, float r, float n) {
    g.setColour(Colour(0x99000000)); g.fillEllipse(cx - r - 3, cy - r + 1, r * 2 + 6, r * 2 + 6);
    g.setGradientFill(juce::ColourGradient(Colour(0xff4a4a4a), cx, cy - r, Colour(0xff121212), cx, cy + r, false)); g.fillEllipse(cx - r - 2, cy - r - 2, r * 2 + 4, r * 2 + 4);
    g.setColour(Colour(0x55000000)); for (int i = 0; i < 36; ++i) { const float a = (float) i * juce::MathConstants<float>::twoPi / 36; g.drawLine(cx + std::sin(a) * r * 0.8f, cy - std::cos(a) * r * 0.8f, cx + std::sin(a) * (r + 2), cy - std::cos(a) * (r + 2), 1.2f); }
    g.setGradientFill(juce::ColourGradient(Colour(0xffd6d6d4), cx - r * 0.3f, cy - r * 0.5f, Colour(0xff3c3c3a), cx + r * 0.7f, cy + r * 0.7f, true));
    g.fillEllipse(cx - r * 0.78f, cy - r * 0.78f, r * 1.56f, r * 1.56f);
    const float a = juce::degreesToRadians(-150.0f + 300.0f * n);
    g.setColour(Colour(0xfff4f4f2)); g.drawLine(cx + std::sin(a) * r * 0.15f, cy - std::cos(a) * r * 0.15f, cx + std::sin(a) * r * 0.92f, cy - std::cos(a) * r * 0.92f, std::max(3.0f, r * 0.12f));
}
static void slider(Graphics& g, float x, float y0, float y1, float n, bool pan) {
    g.setColour(Colour(0xff000000)); g.fillRoundedRectangle(x - 4, y0 - 4, 8, y1 - y0 + 8, 3);
    g.setColour(Colour(0xff2b2b2b)); g.drawRoundedRectangle(x - 4, y0 - 4, 8, y1 - y0 + 8, 3, 1);
    const float y = y1 - juce::jlimit(0.0f, 1.0f, n) * (y1 - y0), w = pan ? 26.0f : 30.0f, h = pan ? 18.0f : 26.0f;
    g.setGradientFill(juce::ColourGradient(Colour(0xfff2f2f0), x, y - h / 2, Colour(0xff6e6e6c), x, y + h / 2, false)); g.fillRoundedRectangle(x - w / 2, y - h / 2, w, h, 3);
    g.setColour(Colour(0xff000000)); g.drawRoundedRectangle(x - w / 2, y - h / 2, w, h, 3, 1.5f);
    g.setColour(Colour(0xff111111)); g.fillRect(x - w / 2, y - 1.5f, w, 3.0f);
}

Faceplate::Faceplate(LinnProcessor& p) : proc(p) { startTimerHz(30); }
juce::Point<float> Faceplate::toPlate(juce::Point<float> p) const { return {p.x * (float) LinnEditor::W / (float) getWidth() - lay::CHEEK, p.y * (float) LinnEditor::PH / (float) getHeight()}; }
int Faceplate::keyAt(juce::Point<float> p) const {
    static const auto ks = lay::keys();
    for (int i = 0; i < (int) ks.size(); ++i) { const auto& k = ks[(size_t) i]; if (std::abs(p.x - k.cx) <= k.s / 2 + 12 && std::abs(p.y - k.cy) <= k.s / 2 + 12) return i; }
    return -1;
}
int Faceplate::knobAt(juce::Point<float> p) const {
    static const auto kn = lay::knobs();
    for (int i = 0; i < (int) kn.size(); ++i) if (p.getDistanceFrom({kn[(size_t) i].x, kn[(size_t) i].y}) <= kn[(size_t) i].r + 22) return i;
    return -1;
}
int Faceplate::sliderAt(juce::Point<float> p) const {
    for (int i = 0; i < 16; ++i) {
        const float x = lay::MIX_X0 + (float) i * lay::MIX_DX; if (std::abs(p.x - x) > 24) continue;
        if (p.y >= lay::PAN_Y[0] - 18 && p.y <= lay::PAN_Y[1] + 18) return i;
        if (p.y >= lay::LVL_Y[0] - 18 && p.y <= lay::LVL_Y[1] + 18) return 16 + i;
    }
    return -1;
}
juce::RangedAudioParameter* Faceplate::knobParam(int k) const { return proc.apvts.getParameter(params()[(size_t) KNOB_P[k]].id); }
juce::RangedAudioParameter* Faceplate::sliderParam(int s) const {
    const int i = s % 16; const bool pan = s < 16;
    const int idx = i < NV ? vp(i, pan ? vf::pan : vf::lv) : (pan ? pid::clickPan : pid::clickLv);
    return proc.apvts.getParameter(params()[(size_t) idx].id);
}
float Faceplate::knobNorm(int k) const {
    if (KNOB_P[k] == pid::tempo) return (float) knobOfTempo(proc.value(pid::tempo)) / 100;
    return (float) proc.value(KNOB_P[k]) / 100;
}
void Faceplate::setKnob(int k, float n) {
    n = juce::jlimit(0.0f, 1.0f, n);
    if (KNOB_P[k] == pid::tempo) {
        proc.setValue(pid::tempo, std::round(tempoOfKnob(n * 100)));
        const juce::String s = juce::String((int) std::round(proc.value(pid::tempo))).paddedLeft(' ', 4); flash(s.substring(0, 2), s.substring(2), 1200);
    } else { proc.setValue(KNOB_P[k], std::round(n * 1000) / 10); flash("  ", juce::String(juce::jlimit(0, 99, (int) std::round(n * 99))).paddedLeft('0', 2), 900); }
}
void Faceplate::setSlider(int s, float y) {
    const bool pan = s < 16; const float y0 = pan ? lay::PAN_Y[0] : lay::LVL_Y[0], y1 = pan ? lay::PAN_Y[1] : lay::LVL_Y[1];
    float n = juce::jlimit(0.0f, 1.0f, (y1 - y) / (y1 - y0)); if (pan) n = 1 - n;      // the pan slider: LEFT at the top
    auto* prm = sliderParam(s); prm->setValueNotifyingHost(prm->convertTo0to1(std::round(n * 100)));
}
void Faceplate::timerCallback() { repaint(); }
void Faceplate::mouseDown(const juce::MouseEvent& e) {
    const auto p = toPlate(e.position); const int src = e.source.getIndex();
    const int k = knobAt(p);
    if (k >= 0) { dragKnobs[src] = k; dragY0[src] = e.position.y; dragV0[src] = knobNorm(k); knobParam(k)->beginChangeGesture(); return; }
    const int sl = sliderAt(p);
    if (sl >= 0) { dragSliders[src] = sl; sliderParam(sl)->beginChangeGesture(); setSlider(sl, p.y); return; }
    const int i = keyAt(p); if (i < 0) return;
    static const auto ks = lay::keys();
    const int id = ks[(size_t) i].id; downKeys[src] = i;
    if (id == K::STORE) { casLed = 0; casUntil = juce::Time::getMillisecondCounter() + 1500; if (onSave) onSave(); }
    else if (id == K::LOAD) { casLed = 1; casUntil = juce::Time::getMillisecondCounter() + 1500; if (onLoad) onLoad(); }
    else proc.key(id, true);
    repaint();
}
void Faceplate::mouseDrag(const juce::MouseEvent& e) {
    const int src = e.source.getIndex();
    if (dragKnobs.count(src)) setKnob(dragKnobs[src], dragV0[src] + (dragY0[src] - e.position.y) / ((float) getHeight() * 0.4f));
    else if (dragSliders.count(src)) setSlider(dragSliders[src], toPlate(e.position).y);
}
void Faceplate::mouseUp(const juce::MouseEvent& e) {
    const int src = e.source.getIndex();
    if (dragKnobs.count(src)) { knobParam(dragKnobs[src])->endChangeGesture(); dragKnobs.erase(src); }
    if (dragSliders.count(src)) { sliderParam(dragSliders[src])->endChangeGesture(); dragSliders.erase(src); }
    if (downKeys.count(src)) { static const auto ks = lay::keys(); const int id = ks[(size_t) downKeys[src]].id; if (id != K::STORE && id != K::LOAD) proc.key(id, false); downKeys.erase(src); }
    repaint();
}
void Faceplate::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto p = toPlate(e.position);
    const int k = knobAt(p); if (k >= 0) { if (KNOB_P[k] == pid::tempo) proc.setValue(pid::tempo, 120); else { auto* prm = knobParam(k); prm->setValueNotifyingHost(prm->getDefaultValue()); } return; }
    const int s = sliderAt(p); if (s >= 0) { auto* prm = sliderParam(s); prm->setValueNotifyingHost(prm->getDefaultValue()); }
}

void Faceplate::paint(Graphics& g) {
    using namespace lay;
    g.addTransform(juce::AffineTransform::scale((float) getWidth() / (float) LinnEditor::W, (float) getHeight() / (float) LinnEditor::PH));
    wood(g, 0, CHEEK, PH); wood(g, CHEEK + PW, CHEEK, PH);
    g.addTransform(juce::AffineTransform::translation(CHEEK, 0));
    g.setColour(PANEL); g.fillRect(0.0f, 0.0f, PW, PH);
    { juce::Random r(3); for (int i = 0; i < 2800; ++i) { g.setColour(r.nextBool() ? Colour(0x0cffffff) : Colour(0x14000000)); g.fillRect(r.nextFloat() * PW, r.nextFloat() * PH, 2.0f, 2.0f); } }
    g.setGradientFill(juce::ColourGradient(Colour(0x1effffff), 0, 0, Colour(0x00ffffff), 0, 70, false)); g.fillRect(0.0f, 0.0f, PW, 70.0f);
    // the back-edge jacks' legend
    txt(g, 668, 34, "IN", 14); txt(g, 732, 34, "OUT", 14); txt(g, 700, 58, "CASSETTE", 14);
    txt(g, 808, 34, "IN", 14); txt(g, 872, 34, "OUT", 14); txt(g, 840, 58, "SYNC", 14);
    txt(g, 990, 34, "TRIGGER", 14); txt(g, 990, 58, "OUTPUT", 14); txt(g, 1100, 34, "REMOTE", 14); txt(g, 1100, 58, "PLAY/STOP", 14);
    for (int i = 0; i < 5; ++i) txt(g, 1210.0f + i * 64, 34, juce::String(i + 1), 14);
    line(g, {1180, 52, 1236, 52}); line(g, {1394, 52, 1450, 52}); txt(g, 1316, 58, "TRIGGER INPUTS", 14);
    // the logo: the Omega's, where the hardware has its own, in the same style (solid first word, outlined second)
    line(g, {30, 205, 236, 205}, 3.5f); line(g, {30, 232, 236, 232}, 3.5f); line(g, {926, 205, 1970, 205}, 3.5f); line(g, {926, 232, 1970, 232}, 3.5f);
    {
        const juce::Font lf(juce::FontOptions(juce::Font::getDefaultSerifFontName(), 118.0f, juce::Font::bold));
        juce::GlyphArrangement a1, a2; a1.addLineOfText(lf, "Omega", 250, 262);
        const float w1 = a1.getBoundingBox(0, -1, true).getRight();
        a2.addLineOfText(lf, "Drum", w1, 262);
        Path p1, p2; a1.createPath(p1); a2.createPath(p2);
        g.setColour(LN); g.fillPath(p1); g.strokePath(p2, juce::PathStrokeType(3.4f));
    }
    for (const auto& b : BOXES) { g.setColour(LN); g.drawRoundedRectangle(b.x, b.y, b.w, b.h, 10, 3.2f); }
    const Display d = proc.display();
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    const bool fl = now < flashUntil;
    std::set<int> down; { static const auto ks = keys(); for (const auto& kv : downKeys) down.insert(ks[(size_t) kv.second].id); }
    // CASSETTE
    txt(g, CAS_X, 358, "CASSETTE", 19);
    txt(g, CAS_X, 500, "STORE", 15); led(g, CAS_X, 520, casLed == 0 && now < casUntil);
    txt(g, CAS_X, 566, "VERIFY", 15); line(g, {CAS_X, 530, CAS_X, 550}); line(g, {CAS_X, 575, CAS_X, 598});
    led(g, CAS_X, 610, casLed == 1 && now < casUntil); txt(g, CAS_X, 640, "LOAD", 15);
    // CREATE RHYTHM PATTERN / SONG
    txt(g, 585, 358, "CREATE RHYTHM PATTERN / SONG", 19);
    for (int i = 0; i < 8; ++i) {
        txt(g, FN_X[i], 392, FN_TOP[i], i == 5 || i == 6 ? 20.0f : 15.0f);
        if (i == 1) { txt(g, FN_X[i], 487, "ERROR", 13); txt(g, FN_X[i], 500, "CORRECT", 13); } else txt(g, FN_X[i], 492, FN_BOT[i], 14);
    }
    led(g, 308, 386, d.song); led(g, 314, 487, !d.song);
    line(g, {COL_X[0] - 30, 510, COL_X[6] + 54, 510, COL_X[6] + 54, 634, COL_X[0] - 30, 634, COL_X[0] - 30, 510}, 2.4f);
    txt(g, COL_X[0] - 6, 530, "STRAIGHT", 13, LN, -1); line(g, {COL_X[1] + 10, 525, COL_X[4] + 30, 525});
    g.fillPath(poly({{COL_X[1] + 10, 525}, {COL_X[1] + 20, 519}, {COL_X[1] + 20, 531}})); g.fillPath(poly({{COL_X[4] + 30, 525}, {COL_X[4] + 20, 519}, {COL_X[4] + 20, 531}}));
    txt(g, COL_X[5] + 26, 530, "SHUFFLE", 13); txt(g, COL_X[2] + 40, 522, "TIMING", 12);
    for (int i = 0; i < 6; ++i) { led(g, COL_X[i] - 14, TIM_Y, d.tim == i); txt(g, COL_X[i] + 6, TIM_Y + 7, juce::String::charToString((juce::juce_wchar) ('A' + i)), 17, LN, -1); }
    for (int i = 0; i < 7; ++i) { led(g, COL_X[i] - 20, EC_Y, d.ec == i); txt(g, COL_X[i] - 8, EC_Y + 6, EC_NAMES[i], 14, LN, -1); }
    display2(g, DISP[0][0], DISP[0][1], fl ? flashL : juce::String(d.l)); txt(g, DISP[0][0] + 52, 610, "STEP#/BEAT", 13);
    display2(g, DISP[1][0], DISP[1][1], fl ? flashR : juce::String(d.r)); txt(g, DISP[1][0] + 52, 728, "PATTERN", 13);
    for (int i = 0; i < 7; ++i) txt(g, COL_X[i], 651, juce::String(i + 1), 15);
    line(g, {COL_X[0] - 30, 748, COL_X[1], 748}); line(g, {COL_X[5], 748, COL_X[6] + 30, 748}); txt(g, 618, 754, "SELECT RHYTHM PATTERN", 15);
    // TEMPO
    txt(g, 1042, 358, "TEMPO", 19); txt(g, 1042, 600, "BPM/TRIG.", 14); led(g, 1042, 626, d.ext); txt(g, 1042, 752, "EXT. SYNC", 14);
    // TUNING, VOLUME
    txt(g, 1445, 358, "TUNING", 19); line(g, {1140, 352, 1395, 352}, 2.4f); line(g, {1495, 352, 1750, 352}, 2.4f);
    const char* tn[6] = {"SNARE", "HI", "MID", "LO", "HI", "LO"};
    for (int i = 0; i < 6; ++i) txt(g, TUNE_X[i], 476, tn[i], 14);
    txt(g, TUNE_X[6], 470, "HIHAT", 13); txt(g, TUNE_X[6], 486, "DECAY", 13);
    line(g, {TUNE_X[1] - 20, 488, TUNE_X[3] + 20, 488}); line(g, {TUNE_X[4] - 20, 488, TUNE_X[5] + 20, 488});
    txt(g, TUNE_X[2], 503, "TOMS", 13); txt(g, (TUNE_X[4] + TUNE_X[5]) / 2, 503, "CONGAS", 13);
    txt(g, VOL[0], 358, "VOLUME", 19);
    // MIXER
    for (int i = 0; i < 16; ++i) {
        const float x = MIX_X0 + (float) i * MIX_DX;
        if (MIX_N[i][1][0]) { txt(g, x, 722, MIX_N[i][0], 11); txt(g, x, 735, MIX_N[i][1], 11); } else txt(g, x, 728, MIX_N[i][0], 11);
        const int lvIdx = i < NV ? vp(i, vf::lv) : pid::clickLv, panIdx = i < NV ? vp(i, vf::pan) : pid::clickPan;
        slider(g, x, PAN_Y[0], PAN_Y[1], 1 - (float) proc.value(panIdx) / 100, true);
        slider(g, x, LVL_Y[0], LVL_Y[1], (float) proc.value(lvIdx) / 100, false);
    }
    for (float yy : {558.0f, 690.0f}) for (float xx : {1131.0f, 1950.0f}) { g.saveState(); g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, xx, yy)); txt(g, xx, yy + 4, yy < 600 ? "LEFT" : "RIGHT", 11); g.restoreState(); }
    auto mx = [](int i) { return MIX_X0 + (float) i * MIX_DX; };
    line(g, {mx(4) - 16, 712, mx(4) - 16, 706, mx(6) + 16, 706, mx(6) + 16, 712}); line(g, {mx(11) - 16, 712, mx(11) - 16, 706, mx(12) + 16, 706, mx(12) + 16, 712});
    txt(g, mx(5), 702, "TOMS", 11); txt(g, (mx(11) + mx(12)) / 2, 702, "CONGAS", 11);
    line(g, {mx(0) - 16, 748, mx(3), 748}); line(g, {mx(5), 748, mx(8) + 16, 748}); line(g, {mx(9) - 16, 748, mx(10), 748}); line(g, {mx(13), 748, mx(14) + 16, 748});
    txt(g, mx(4), 753, "DRUMS", 12); txt(g, (mx(11) + mx(12)) / 2, 753, "PERCUSSION", 12);
    line(g, {MIX_X0 - 20, 1050, 1470, 1050}, 2.4f); line(g, {1610, 1050, 1940, 1050}, 2.4f); txt(g, 1540, 1057, "MIXER", 19);
    // the drum keys
    struct Top { const char* n; int a, b; const char *la, *lb; };
    const Top top[] = {{"CABASA", 0, 1, "1", "2"}, {"TAMBOURINE", 2, 3, "1", "2"}, {"CONGA", 4, 5, "HI", "LO"}, {"COWBELL", 6, 6, "", ""}, {"CLAPS", 7, 7, "", ""}};
    for (const auto& t : top) {
        if (t.a == t.b) { txt(g, DRUM_X[t.a], 822, t.n, 14); continue; }
        line(g, {DRUM_X[t.a] - 30, 820, DRUM_X[t.a] - 30, 812, DRUM_X[t.b] + 30, 812, DRUM_X[t.b] + 30, 820}); txt(g, (DRUM_X[t.a] + DRUM_X[t.b]) / 2, 808, t.n, 13);
        txt(g, DRUM_X[t.a], 826, t.la, 13); txt(g, DRUM_X[t.b], 826, t.lb, 13);
    }
    struct Mid { const char* n; int a, b; const char* ls[3]; };
    const Mid mid[] = {{"HIHAT", 0, 2, {"1", "2", "OPEN"}}, {"TOMS", 3, 5, {"HI", "MID", "LO"}}, {"RIDE", 6, 7, {"1", "2", ""}}};
    for (const auto& m : mid) {
        for (int k = 0; k <= m.b - m.a; ++k) txt(g, DRUM_X[m.a + k], 923, m.ls[k], 13);
        const float c = (DRUM_X[m.a] + DRUM_X[m.b]) / 2;
        line(g, {DRUM_X[m.a] - 34, 926, DRUM_X[m.a] - 34, 936, c - 34, 936}); line(g, {c + 34, 936, DRUM_X[m.b] + 34, 936, DRUM_X[m.b] + 34, 926}); txt(g, c, 941, m.n, 13);
    }
    led(g, DRUM_X[7] + 44, 826, d.perc, 6);
    struct Bot { const char* n; int a, b; const char* ls[3]; };
    const Bot bot[] = {{"SIDESTICK", 0, 0, {}}, {"SNARE", 1, 3, {"1", "2", "3"}}, {"BASS", 4, 5, {"1", "2", ""}}, {"CRASH", 6, 6, {}}, {"PERCUSSION", 7, 7, {}}};
    for (const auto& b : bot) {
        if (b.a == b.b) { txt(g, DRUM_X[b.a], 1052, b.n, 14); continue; }
        for (int k = 0; k <= b.b - b.a; ++k) txt(g, DRUM_X[b.a + k], 1048, b.ls[k], 13);
        const float c = (DRUM_X[b.a] + DRUM_X[b.b]) / 2;
        line(g, {DRUM_X[b.a] - 34, 1052, DRUM_X[b.a] - 34, 1062, c - 40, 1062}); line(g, {c + 40, 1062, DRUM_X[b.b] + 34, 1062, DRUM_X[b.b] + 34, 1052}); txt(g, c, 1067, b.n, 13);
    }
    // PLAY/STOP
    led(g, PLAY[0], 850, d.play || d.rec, 7); txt(g, PLAY[0], 1052, "PLAY/STOP", 16);
    // keys and knobs
    static const auto ks = keys(); for (const auto& k : ks) keyCap(g, k.cx, k.cy, k.s, down.count(k.id) > 0);
    static const auto kn = knobs(); for (int k = 0; k < (int) kn.size(); ++k) knob(g, kn[(size_t) k].x, kn[(size_t) k].y, kn[(size_t) k].r, knobNorm(k));
}

/* ---------------- knobs and buttons for the pages ---------------- */
void LinnLook::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    const float r = (float) std::min(w, h) / 2 - 4, cx = (float) x + (float) w / 2, cy = (float) y + (float) h / 2;
    g.setColour(LN.withAlpha(0.85f));
    for (int i = 0; i <= 10; ++i) { const float a = a0 + (a1 - a0) * (float) i / 10; g.fillEllipse(cx + std::sin(a) * r - 1.5f, cy - std::cos(a) * r - 1.5f, 3, 3); }
    const float kr = r * 0.8f;
    g.setColour(Colour(0xff1a1a1a)); g.fillEllipse(cx - kr, cy - kr, kr * 2, kr * 2);
    g.setGradientFill(juce::ColourGradient(Colour(0xffd6d6d4), cx - kr * 0.3f, cy - kr * 0.5f, Colour(0xff3c3c3a), cx + kr * 0.7f, cy + kr * 0.7f, true));
    g.fillEllipse(cx - kr * 0.78f, cy - kr * 0.78f, kr * 1.56f, kr * 1.56f);
    const float a = a0 + pos * (a1 - a0);
    Path p; p.startNewSubPath(cx + std::sin(a) * kr * 0.15f, cy - std::cos(a) * kr * 0.15f); p.lineTo(cx + std::sin(a) * kr * 0.75f, cy - std::cos(a) * kr * 0.75f);
    g.setColour(Colour(0xff111111)); g.strokePath(p, juce::PathStrokeType(std::max(2.0f, kr * 0.14f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
void LinnLook::drawButtonBackground(Graphics& g, juce::Button& b, const Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.5f);
    const bool on = b.getToggleState();
    g.setColour(Colour(0xff000000)); g.fillRoundedRectangle(r, 4);
    r = r.reduced(2.0f).translated(0, down ? 1.5f : 0);
    g.setGradientFill(juce::ColourGradient(Colour(over ? 0xff383838 : 0xff2c2c2c), 0, r.getY(), Colour(0xff0f0f0f), 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, 3);
    g.setColour(Colour(0x73ffffff)); g.fillRoundedRectangle(r.getX() + 4, r.getY() + 3, r.getWidth() - 8, 3, 1.5f);
    g.setColour(on ? LEDON : Colour(0xff3c3c3c)); g.drawRoundedRectangle(r, 3, on ? 2.5f : 1.2f);
    if (on) { g.setColour(LEDON); g.fillEllipse(r.getX() + 6, r.getCentreY() - 4, 8, 8); }
}
void LinnLook::drawButtonText(Graphics& g, juce::TextButton& b, bool, bool) {
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
}

LinnEditor::LinnEditor(LinnProcessor& p) : juce::AudioProcessorEditor(p), proc(p), plate(p) {
    addAndMakeVisible(plate);
    plate.onSave = [this] { saveTape(); };
    plate.onLoad = [this] { loadTape(); };
    const char* tn[NT] = {"SOUND / HI-HAT", "PAD EQ / ADSR", "MIXER", "REV 7 / TC DELAY", "MASTER", "FILES / MIDI"};
    for (int i = 0; i < NT; ++i) { tabs[i].setButtonText(tn[i]); tabs[i].setLookAndFeel(&look); tabs[i].onClick = [this, i] { showTab(i); }; addAndMakeVisible(tabs[i]); }
    for (auto* b : {&prevBtn, &nextBtn, &diceBtn, &foldBtn}) { b->setLookAndFeel(&look); addAndMakeVisible(*b); }
    prevBtn.onClick = [this] { proc.setCurrentProgram((proc.getCurrentProgram() + kPresets - 1) % kPresets); };
    nextBtn.onClick = [this] { proc.setCurrentProgram((proc.getCurrentProgram() + 1) % kPresets); };
    diceBtn.onClick = [this] { proc.dice(partBox.getSelectedItemIndex()); };
    foldBtn.onClick = [this] { setFolded(!folded); };
    fillPresets();
    presetBox.onChange = [this] { const int id = presetBox.getSelectedId(); if (id > 0 && id - 1 != proc.getCurrentProgram()) proc.setCurrentProgram(id - 1); };
    const char* parts[] = {"Everything", "Sounds", "Pad EQ", "ADSR + drive", "Reverb + delay", "Master EQ + drive"};
    for (int i = 0; i < 6; ++i) partBox.addItem(parts[i], i + 1);
    partBox.setSelectedItemIndex(0, juce::dontSendNotification);
    ecBox.addItem("as played", 1); for (int i = 0; i < 7; ++i) ecBox.addItem(juce::String("correct to ") + EC_NAMES[i], i + 2);
    ecBox.setSelectedId(4, juce::dontSendNotification);
    for (int i = 1; i <= 8; ++i) barsBox.addItem(juce::String(i) + (i == 1 ? " bar" : " bars") + " a pattern", i);
    barsBox.setSelectedId(2, juce::dontSendNotification);
    tempoTog.setToggleState(true, juce::dontSendNotification); tempoTog.setColour(juce::ToggleButton::textColourId, WHITE); tempoTog.setColour(juce::ToggleButton::tickColourId, LEDON);
    addAndMakeVisible(tempoTog);
    for (auto* c : {&presetBox, &partBox, &ecBox, &barsBox}) {
        c->setColour(juce::ComboBox::backgroundColourId, Colour(0xff1d0507)); c->setColour(juce::ComboBox::textColourId, SEGON);
        c->setColour(juce::ComboBox::outlineColourId, juce::Colours::black); c->setColour(juce::ComboBox::arrowColourId, SEGON);
        addAndMakeVisible(*c);
    }
    proc.onProgram = [this] {
        juce::Component::SafePointer<LinnEditor> safe(this);
        juce::MessageManager::callAsync([safe] { if (auto* ed = safe.getComponent()) ed->presetBox.setSelectedId(ed->proc.getCurrentProgram() + 1, juce::dontSendNotification); });
    };
    auto R = [](int x, int y, int w, int h) { return Rectangle<int>(x, y, w, h); };
    for (int v = 0; v < NV; ++v) { chBtn[v].setButtonText(CH_KEYS[v]); chBtn[v].setLookAndFeel(&look); chBtn[v].onClick = [this, v] { selectChannel(v); }; addAndMakeVisible(chBtn[v]); }
    // ---- SOUND / HI-HAT: the channel picked (also by its key on the panel)
    boxes.push_back({"SOUND", 0, R(20, 80, 1340, 300)});
    const char* sk[][2] = {{"tune", "TUNE"}, {"dec", "DECAY"}, {"cut", "CUTOFF"}, {"res", "RESONANCE"}, {"start", "START"}, {"snd", "REVERB SEND"}};
    for (int i = 0; i < 6; ++i) addKnob(0, "", sk[i][1], R(40 + i * 220, 135, 200, 170), sk[i][0]);
    addKnob(0, "", "OUTPUT", R(40, 400, 200, 120), "out");
    addTog(0, "", "REVERSE", R(260, 410, 200, 50), "rev"); addTog(0, "", "MUTE", R(470, 410, 200, 50), "mute");
    addBtn(0, "LOAD SOUND...", R(700, 470, 300, 50), [this] { loadSound(); });
    addBtn(0, "BUILT-IN SOUND", R(1020, 470, 300, 50), [this] { proc.resetSound(proc.groupOfChannel(ch)); repaint(); });
    boxes.push_back({"HI-HAT / MACHINE", 0, R(1380, 80, 720, 530)});
    addKnob(0, "hatMode", "HAT MODE", R(1400, 135, 220, 150)); addKnob(0, "hatDecay", "HIHAT DECAY", R(1640, 135, 200, 150)); addKnob(0, "mux", "MUX TRIM", R(1860, 135, 200, 150));
    addKnob(0, "kit", "SOUND SET", R(1400, 345, 220, 130)); addKnob(0, "tempo", "TEMPO (OWN CLOCK)", R(1640, 345, 200, 130));
    addTog(0, "bassChip", "BASS CHIP: ALT", R(1860, 340, 220, 46)); addTog(0, "poly", "POLY", R(1860, 395, 220, 46));
    addTog(0, "follow", "FOLLOW DAW", R(1860, 450, 220, 46));
    auto& mb = addBtn(0, "CLICK ALWAYS", R(1860, 505, 220, 46), [this] { proc.setMetroAlways(!proc.getMetroAlways()); });
    mb.setClickingTogglesState(true); mb.setToggleState(proc.getMetroAlways(), juce::dontSendNotification);
    // ---- PAD EQ / ADSR
    boxes.push_back({"PARAMETRIC EQ", 1, R(20, 80, 1150, 530)}); boxes.push_back({"ADSR / DRIVE / DELAY SEND", 1, R(1190, 80, 910, 530)});
    const char* eqs[][2] = {{"lsF", "LOW FREQ"}, {"lsG", "LOW GAIN"}, {"pkF", "MID FREQ"}, {"pkG", "MID GAIN"}, {"pkQ", "MID Q"}, {"hsF", "HIGH FREQ"}, {"hsG", "HIGH GAIN"}};
    for (int i = 0; i < 7; ++i) addKnob(1, "", eqs[i][1], R(40 + i * 160, 400, 150, 150), eqs[i][0]);
    const char* envs[][2] = {{"atk", "ATTACK"}, {"dcy", "DECAY"}, {"sus", "SUSTAIN"}, {"rel", "RELEASE"}, {"drv", "DRIVE"}, {"dls", "DELAY SEND"}};
    for (int i = 0; i < 6; ++i) addKnob(1, "", envs[i][1], R(1205 + i * 148, 400, 140, 150), envs[i][0]);
    // ---- MIXER: the 15 channels, big
    for (int v = 0; v < NV; ++v) {
        const int x = 20 + v * 139;
        boxes.push_back({CH_KEYS[v], 2, R(x, 10, 130, 600)});
        const juce::String id(CHS[v]);
        addKnob(2, id + "lv", "VOLUME", R(x + 5, 60, 120, 120));
        addKnob(2, id + "pan", "PAN", R(x + 10, 210, 110, 100)); addKnob(2, id + "snd", "REV", R(x + 10, 335, 110, 100));
        addKnob(2, id + "dls", "DELAY", R(x + 10, 460, 110, 100));
        addTog(2, id + "mute", "MUTE", R(x + 8, 572, 114, 34));
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
    // ---- MASTER
    boxes.push_back({"MASTER PARAMETRIC EQ", 4, R(20, 10, 1150, 600)});
    const char* meq[][2] = {{"mLsF", "LOW FREQ"}, {"mLsG", "LOW GAIN"}, {"mP1F", "MID 1 FREQ"}, {"mP1G", "MID 1 GAIN"}, {"mP1Q", "MID 1 Q"},
        {"mP2F", "MID 2 FREQ"}, {"mP2G", "MID 2 GAIN"}, {"mP2Q", "MID 2 Q"}, {"mHsF", "HIGH FREQ"}, {"mHsG", "HIGH GAIN"}};
    for (int i = 0; i < 10; ++i) addKnob(4, meq[i][0], meq[i][1], R(40 + (i % 5) * 225, 280 + (i / 5) * 165, 210, 120));
    boxes.push_back({"OVERDRIVE / CLIPPER", 4, R(1190, 10, 910, 230)});
    const char* odk[][2] = {{"odDrive", "DRIVE"}, {"odMode", "MODE"}, {"odMix", "MIX"}, {"odOut", "OUTPUT"}};
    for (int i = 0; i < 4; ++i) addKnob(4, odk[i][0], odk[i][1], R(1210 + i * 222, 55, 200, 140));
    boxes.push_back({"OUTPUT", 4, R(1190, 255, 910, 355)});
    addKnob(4, "volume", "MASTER VOLUME", R(1240, 310, 240, 170)); addKnob(4, "clickLv", "CLICK", R(1520, 310, 240, 170));
    // ---- FILES / MIDI
    boxes.push_back({"STORE / LOAD", 5, R(20, 10, 660, 280)});
    addBtn(5, "STORE (.json)", R(50, 70, 290, 54), [this] { saveTape(); }); addBtn(5, "LOAD (.json)", R(360, 70, 290, 54), [this] { loadTape(); });
    boxes.push_back({"SOUNDS", 5, R(20, 310, 660, 300)});
    addBtn(5, "LOAD FOLDER (A KIT)...", R(50, 370, 600, 54), [this] { loadFolder(); });
    addBtn(5, "BUILT-IN SOUNDS", R(50, 440, 600, 54), [this] { proc.resetSounds(); say("Built-in sounds back."); });
    boxes.push_back({"MIDI FILE IN", 5, R(700, 10, 1400, 600)});
    addBtn(5, "INTO PATTERN...", R(740, 300, 400, 60), [this] { importMidi(false); }); addBtn(5, "AS A SONG...", R(1160, 300, 400, 60), [this] { importMidi(true); });
    selectChannel(proc.selected.load());
    showTab(0);
    setResizable(true, true);
    setFolded((bool) proc.apvts.state.getProperty("folded", false));
    startTimerHz(15);
}
LinnEditor::~LinnEditor() {
    stopTimer();
    proc.onProgram = nullptr;
    for (auto& t : tabs) t.setLookAndFeel(nullptr);
    for (auto& b : chBtn) b.setLookAndFeel(nullptr);
    for (auto* b : {&prevBtn, &nextBtn, &diceBtn, &foldBtn}) b->setLookAndFeel(nullptr);
    for (auto& k : knobs) { k.a.reset(); k.s->setLookAndFeel(nullptr); }
    for (auto& t : togs) t.b->setLookAndFeel(nullptr);
    for (auto& b : btns) b.b->setLookAndFeel(nullptr);
}
void LinnEditor::setFolded(bool f) {
    folded = f;
    proc.apvts.state.setProperty("folded", f, nullptr);
    foldBtn.setButtonText(f ? "EXPAND" : "FOLD");
    for (int i = 0; i < NT; ++i) tabs[i].setVisible(!f);
    for (auto* c : std::initializer_list<juce::Component*>{&prevBtn, &nextBtn, &diceBtn, &presetBox, &partBox}) c->setVisible(!f);
    if (f) { for (auto& k : knobs) k.s->setVisible(false); for (auto& g : togs) g.b->setVisible(false); for (auto& b : chBtn) b.setVisible(false);
        for (auto& b : btns) b.b->setVisible(false); for (auto* c : std::initializer_list<juce::Component*>{&ecBox, &barsBox, &tempoTog}) c->setVisible(false); }
    else showTab(tab);
    const int h = f ? PH + 60 : PH + MH, w = getWidth() > 0 ? getWidth() : 1100;
    getConstrainer()->setFixedAspectRatio((double) W / h);
    setResizeLimits(700, (int) (700.0 * h / W), 2600, (int) (2600.0 * h / W));
    setSize(w, (int) std::round((double) w * h / W));
}
void LinnEditor::fillPresets() {
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
LinnEditor::Knob& LinnEditor::addKnob(int t, const juce::String& id, const juce::String& label, Rectangle<int> r, const juce::String& suffix) {
    Knob k; k.s = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    k.s->setLookAndFeel(&look);
    k.s->setColour(juce::Slider::textBoxTextColourId, SEGON); k.s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.s->setRotaryParameters(juce::degreesToRadians(-150.0f) + juce::MathConstants<float>::twoPi, juce::degreesToRadians(150.0f) + juce::MathConstants<float>::twoPi, true);
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
    else if (f == "hatMode") fn = [](double v) { return juce::String(HAT_MODES[juce::jlimit(0, 2, (int) std::round(v))]); };
    else if (f == "kit") fn = [](double v) { return juce::String("SET ") + juce::String(v >= 0.5 ? 2 : 1); };
    else if (f == "out") fn = [](double v) { const int o = (int) std::round(v); return o == 0 ? juce::String("ITS OWN") : "OUT " + juce::String(o); };
    else if (f == "tempo") fn = [](double v) { return juce::String((int) std::round(v)) + " BPM"; };
    else if (ends("tune")) fn = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " st"; };
    else if (ends("dec")) fn = [secs](double v) { return v >= 2.999 ? juce::String("FULL") : secs(v); };
    else if (f == "cut") fn = [hz](double v) { return v >= 19999 ? juce::String("OPEN") : hz(v); };
    else if (f == "rvTime") fn = [](double v) { return juce::String(v, 1) + " s"; };
    else if (f == "rvDelay" || f == "dlTime") fn = [](double v) { return juce::String((int) std::round(v)) + " ms"; };
    else if (ends("F") || ends("Cut")) fn = hz;
    else if (ends("G") || f == "odOut") fn = dB;
    else if (f == "dlModRate") fn = [](double v) { return juce::String(v, 2) + " Hz"; };
    else if (ends("atk") || ends("dcy")) fn = secs;
    else if (ends("rel")) fn = [secs](double v) { return v >= 3.999 ? juce::String("HOLD") : secs(v); };
    else if (ends("lv") || f == "volume" || f == "mux" || f == "hatDecay") fn = [](double v) { return juce::String((int) std::round(v)); };
    else if (ends("pan")) fn = [](double v) { return std::abs(v - 50) < 0.5 ? juce::String("C") : (v < 50 ? "L" : "R") + juce::String((int) std::round(std::abs(v - 50) * 2)); };
    else if (f == "sus" || f == "res" || f == "start" || ends("drv") || ends("dls") || ends("snd") || f == "dlFb" || f == "dlModDepth" || f == "dlDuck" || f == "dlLevel" || f == "odDrive" || f == "odMix") fn = pct;
    else if (f == "rvDiff") fn = [](double v) { return juce::String((int) std::round(v)); };
    k.s->textFromValueFunction = fn; k.fmt = fn;
    k.s->updateText();
    k.label = label; k.suffix = suffix; k.tab = t; k.r = r.translated(0, TOP);
    addAndMakeVisible(*k.s);
    knobs.push_back(std::move(k));
    return knobs.back();
}
void LinnEditor::addTog(int t, const juce::String& id, const juce::String& label, Rectangle<int> r, const juce::String& suffix) {
    Tog g; g.b = std::make_unique<juce::TextButton>(label); g.b->setClickingTogglesState(true); g.b->setLookAndFeel(&look);
    if (suffix.isEmpty()) g.a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, *g.b);
    g.tab = t; g.r = r.translated(0, TOP); g.suffix = suffix; addAndMakeVisible(*g.b); togs.push_back(std::move(g));
}
juce::TextButton& LinnEditor::addBtn(int t, const juce::String& label, Rectangle<int> r, std::function<void()> f) {
    Btn b; b.b = std::make_unique<juce::TextButton>(label); b.b->setLookAndFeel(&look); b.b->onClick = std::move(f);
    b.tab = t; b.r = r.translated(0, TOP); addAndMakeVisible(*b.b); btns.push_back(std::move(b)); return *btns.back().b;
}
void LinnEditor::selectChannel(int v) {
    ch = v; lastSel = v;
    for (int i = 0; i < NV; ++i) chBtn[i].setToggleState(i == v, juce::dontSendNotification);
    for (auto& k : knobs) if (k.suffix.isNotEmpty()) {
        k.a.reset();
        k.a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, juce::String(CHS[v]) + k.suffix, *k.s);
        k.s->textFromValueFunction = k.fmt; k.s->updateText();
    }
    for (auto& g : togs) if (g.suffix.isNotEmpty()) { g.a.reset(); g.a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, juce::String(CHS[v]) + g.suffix, *g.b); }
    repaint();
}
void LinnEditor::showTab(int t) {
    tab = t;
    if (folded) return;
    for (int i = 0; i < NT; ++i) tabs[i].setToggleState(i == t, juce::dontSendNotification);
    for (auto& k : knobs) k.s->setVisible(k.tab == t);
    for (auto& g : togs) g.b->setVisible(g.tab == t);
    for (auto& b : btns) b.b->setVisible(b.tab == t);
    for (auto& b : chBtn) b.setVisible(t <= 1);
    for (auto* c : std::initializer_list<juce::Component*>{&ecBox, &barsBox, &tempoTog}) c->setVisible(t == 5);
    resized(); repaint();
}
void LinnEditor::timerCallback() {
    const int s = proc.selected.load(); if (s != lastSel) selectChannel(s);
    juce::uint32 h = 7;
    for (int i = 0; i < NP; ++i) h = h * 31 + (juce::uint32) (proc.value(i) * 1000);
    if (h != lastParamsHash) { lastParamsHash = h; repaint(); }
}
void LinnEditor::saveTape() {
    chooser = std::make_unique<juce::FileChooser>("Store the machine (memory, sounds, settings)", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("omega-linn.json"), "*.json");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult(); if (f == juce::File()) return; f.replaceWithText(proc.backupJson()); say("Stored " + f.getFileName()); });
}
void LinnEditor::loadTape() {
    chooser = std::make_unique<juce::FileChooser>("Load a machine backup (.json, from the plugin or the app)", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult(); if (!f.existsAsFile()) return; say(proc.restoreJson(f.loadFileAsString()) ? "Loaded " + f.getFileName() : juce::String("Not an Omega Linn backup.")); });
}
void LinnEditor::loadSound() {
    const int g = proc.groupOfChannel(ch);
    chooser = std::make_unique<juce::FileChooser>(juce::String("A sound for the ") + GROUPS[g] + " ROM", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, g](const juce::FileChooser& fc) {
        const auto f = fc.getResult(); if (!f.existsAsFile()) return; say(proc.loadSound(g, f) ? f.getFileName() + " is in the " + GROUPS[g] + " ROM" : juce::String("Couldn't read that file.")); });
}
void LinnEditor::loadFolder() {
    chooser = std::make_unique<juce::FileChooser>("A folder of drum sounds (a kit)", juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
        const auto d = fc.getResult(); if (!d.isDirectory()) return; say(proc.loadFolder(d)); });
}
void LinnEditor::importMidi(bool asSong) {
    chooser = std::make_unique<juce::FileChooser>(asSong ? "A MIDI file to make a song of" : "A MIDI file to put in the pattern", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, asSong](const juce::FileChooser& fc) {
        const auto f = fc.getResult(); if (!f.existsAsFile()) return;
        say(proc.importMidi(f, asSong, barsBox.getSelectedId(), ecBox.getSelectedId() - 2, tempoTog.getToggleState())); });
}
void LinnEditor::paint(Graphics& g) {
    const float s = (float) getWidth() / (float) W;
    g.fillAll(Colour(0xff101010));
    if (folded) return;
    g.saveState();
    g.addTransform(juce::AffineTransform::scale(s).translated(0, (float) plate.getBottom()));
    g.setColour(PANEL); g.fillRect(0, 0, W, MH);
    g.setColour(Colour(0xff050505)); g.fillRoundedRectangle(14, 80, W - 28, 58, 10);
    for (const auto& b : boxes) if (b.tab == tab) {
        const auto r = b.r.translated(0, TOP).toFloat();
        g.setColour(LN); g.drawRoundedRectangle(r, 12, 2.5f);
        txt(g, r.getCentreX(), r.getY() + 30, b.title, 20);
    }
    for (const auto& k : knobs) if (k.tab == tab) txt(g, (float) k.r.getCentreX(), (float) k.r.getBottom() + 22, k.label, 15, WHITE);
    static const std::map<juce::String, int> index = [] { std::map<juce::String, int> m; const auto ps = params(); for (int i = 0; i < NP; ++i) m[juce::String(ps[(size_t) i].id)] = i; return m; }();
    auto v = [this](const juce::String& id) { const auto it = index.find(id); return it == index.end() ? 0.0 : proc.value(it->second); };
    if (tab == 0) {
        txt(g, 40, TOP + 560, juce::String(GROUPS[proc.groupOfChannel(ch)]) + " ROM: " + proc.romName(proc.groupOfChannel(ch)), 18, SEGON, -1);
        txt(g, 1400, TOP + 590, "HAT MODE: LINN keeps the ROM counting through quick hits; LM-1 LOOP runs it round.", 13, WHITE.withAlpha(0.7f), -1);
    } else if (tab == 1) {
        const juce::String P(CHS[ch]);
        drawEq(g, Rectangle<float>(40, TOP + 130, 1110, 230), {{0, v(P + "lsF"), v(P + "lsG"), 0.707}, {1, v(P + "pkF"), v(P + "pkG"), v(P + "pkQ")}, {2, v(P + "hsF"), v(P + "hsG"), 0.707}});
        drawAdsr(g, Rectangle<float>(1210, TOP + 130, 870, 230), v(P + "atk"), v(P + "dcy"), v(P + "sus"), v(P + "rel"));
    } else if (tab == 2) {
        for (int i = 0; i < NV; ++i) { const float m = proc.meters[(size_t) i].load(); const float db = 20 * std::log10(std::max(1e-4f, m)); const float h = juce::jlimit(0.0f, 1.0f, (db + 48) / 48) * 540;
            g.setColour(Colour(0xff000000)); g.fillRect(20.0f + i * 139 + 120, (float) TOP + 60, 6.0f, 540.0f);
            g.setColour(SEGON); g.fillRect(20.0f + i * 139 + 120, (float) TOP + 600 - h, 6.0f, h); }
    } else if (tab == 4)
        drawEq(g, Rectangle<float>(40, TOP + 60, 1110, 190), {{0, v("mLsF"), v("mLsG"), 0.707}, {1, v("mP1F"), v("mP1G"), v("mP1Q")}, {1, v("mP2F"), v("mP2G"), v("mP2Q")}, {2, v("mHsF"), v("mHsG"), 0.707}});
    else if (tab == 5) {
        txt(g, 740, TOP + 80, "Drum notes (General MIDI; velocity picks the dynamic) into the pattern shown,", 16, WHITE.withAlpha(0.8f), -1);
        txt(g, 740, TOP + 106, "or as the song shown, cut into patterns (empty ones, 61-77 first).", 16, WHITE.withAlpha(0.8f), -1);
        txt(g, 740, TOP + 420, "Pattern shown: " + juce::String(pnum(proc.shownPattern())), 18, SEGON, -1);
        txt(g, 50, TOP + 560, "LOAD FOLDER: each file goes on its sound by name (kick, snare, sst, ohh, tom, conga...).", 13, WHITE.withAlpha(0.7f), -1);
    }
    if (message.isNotEmpty()) { juce::StringArray ls; ls.addLines(message); for (int i = 0; i < ls.size() && i < 3; ++i) txt(g, 740, TOP + 480 + i * 26.0f, ls[i].substring(0, 110), 16, SEGON, -1); }
    g.restoreState();
}
void LinnEditor::resized() {
    const float s = (float) getWidth() / (float) W;
    plate.setBounds(0, 0, getWidth(), (int) std::round(PH * s));
    const int y0 = plate.getBottom();
    auto sc = [&](Rectangle<int> r) { return Rectangle<int>((int) std::round(r.getX() * s), y0 + (int) std::round(r.getY() * s), (int) std::round(r.getWidth() * s), (int) std::round(r.getHeight() * s)); };
    foldBtn.setBounds(sc({1880, folded ? 8 : 86, 220, 46}));
    for (int i = 0; i < NT; ++i) tabs[i].setBounds(sc({20 + i * 347, 12, 337, 56}));
    prevBtn.setBounds(sc({24, 86, 70, 46})); presetBox.setBounds(sc({100, 86, 900, 46})); nextBtn.setBounds(sc({1006, 86, 70, 46}));
    diceBtn.setBounds(sc({1110, 86, 200, 46})); partBox.setBounds(sc({1320, 86, 330, 46}));
    for (int v = 0; v < NV; ++v) chBtn[v].setBounds(sc({20 + v * 139, TOP + 15, 132, 50}));
    ecBox.setBounds(sc({740, TOP + 160, 400, 50})); barsBox.setBounds(sc({1160, TOP + 160, 400, 50})); tempoTog.setBounds(sc({740, TOP + 230, 600, 44}));
    for (auto& k : knobs) { k.s->setBounds(sc(k.r)); k.s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, (int) (k.r.getWidth() * s), std::max(12, (int) (24 * s))); }
    for (auto& g : togs) g.b->setBounds(sc(g.r));
    for (auto& b : btns) b.b->setBounds(sc(b.r));
}

} // namespace linn
