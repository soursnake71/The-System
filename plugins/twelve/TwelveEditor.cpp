#include "TwelveEditor.h"

namespace twelve {

// the panel's colours (see twelve/index.html)
static const juce::Colour kCase(0xff3a3c3f), kFace(0xff5e6165), kFace2(0xff575a5e), kStrip(0xffd6d6d2), kPrint(0xffeef0f1), kDim(0xffb8bcc0),
    kRule(0xff4f93e6), kCream(0xffefe9d3), kCream2(0xffd9d2b8), kCreamEdge(0xff9d977f), kRed(0xffd4342c), kRed2(0xffa8231d), kLed(0xffff3b2c),
    kLedOff(0xff2c2a2a), kLcd(0xffb7c38b), kLcd2(0xffa9b67c), kLcdInk(0xff1b2310), kLcdDim(0xff5d6a3c);
static juce::Font font(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)).withHorizontalScale(.9f); }
static juce::Font mono(float h, bool bold = true) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }

// the fader pages: eight settings each (-1: the Multi Mode page, each pad's pitch)
static const char* PAGE_NAMES[8] = {"Tune/Decay", "Mix", "Multi Mode", "Sound", "Lo-Fi", "Master EQ", "EQ Shape", "Dynamics"};
static const int PAGES[8][8] = {
    {pid::semis, pid::cents, pid::decay, pid::a4, pid::rpm, pid::stretch, pid::grain, pid::smode},
    {pid::input, pid::drive, pid::pre, pid::alias, pid::mix, pid::output, pid::link, pid::mono},
    {-1, -1, -1, -1, -1, -1, -1, -1},
    {pid::machine, pid::bits, pid::rate, pid::ftype, pid::cutoff, pid::res, pid::fenv, pid::retrig},
    {pid::crackle, pid::hiss, pid::wow, pid::flutter, pid::tape, pid::age, pid::hum, pid::humHz},
    {pid::lsG, pid::p1F, pid::p1G, pid::p2F, pid::p2G, pid::p3F, pid::p3G, pid::hsG},
    {pid::lsF, pid::p1Q, pid::p2Q, pid::p3Q, pid::hsF, pid::match, pid::fOn, pid::calib},
    {pid::cThr, pid::cRat, pid::cAtk, pid::cRel, pid::cMake, pid::mGain, pid::lim, pid::mOn}};
static juce::String shortName(int id) {
    static const std::map<std::string, const char*> s = {{"semis", "TUNE"}, {"cents", "FINE"}, {"decay", "DECAY"}, {"a4", "A4"}, {"rpm", "ALIAS"}, {"stretch", "STRTCH"},
        {"grain", "GRAIN"}, {"smode", "MODE"}, {"input", "INPUT"}, {"drive", "DRIVE"}, {"pre", "PREAMP"}, {"alias", "UNFILT"}, {"mix", "MIX"}, {"output", "OUTPUT"},
        {"link", "LINK"}, {"mono", "MONO"}, {"machine", "SAMPLR"}, {"bits", "BITS"}, {"rate", "RATE"}, {"ftype", "FILTER"}, {"cutoff", "CUTOFF"}, {"res", "RESO"},
        {"fenv", "F.ENV"}, {"retrig", "RETRIG"}, {"crackle", "CRACKL"}, {"hiss", "HISS"}, {"wow", "WOW"}, {"flutter", "FLUTTR"}, {"tape", "SATUR"}, {"age", "AGE"},
        {"hum", "HUM"}, {"humHz", "MAINS"}, {"lsG", "LOW"}, {"p1F", "B1 FQ"}, {"p1G", "BAND 1"}, {"p2F", "B2 FQ"}, {"p2G", "BAND 2"}, {"p3F", "B3 FQ"}, {"p3G", "BAND 3"},
        {"hsG", "HIGH"}, {"lsF", "LOW FQ"}, {"p1Q", "B1 Q"}, {"p2Q", "B2 Q"}, {"p3Q", "B3 Q"}, {"hsF", "HI FQ"}, {"match", "MATCH"}, {"fOn", "FILT ON"}, {"calib", "CALIB"},
        {"cThr", "THRESH"}, {"cRat", "RATIO"}, {"cAtk", "ATTACK"}, {"cRel", "RELEAS"}, {"cMake", "MAKEUP"}, {"mGain", "GAIN"}, {"lim", "CEIL"}, {"mOn", "MASTER"}};
    auto it = s.find(PARAMS[id].id); return it != s.end() ? juce::String(it->second) : juce::String(PARAMS[id].name).toUpperCase();
}
static const char* FORMATS[][3] = {{"wav:16", "WAV 16", ".wav"}, {"wav:24", "WAV 24", ".wav"}, {"wav:32", "WAV 32F", ".wav"}, {"aiff:16", "AIFF 16", ".aif"}, {"aiff:24", "AIFF 24", ".aif"},
    {"flac:16", "FLAC 16", ".flac"}, {"flac:24", "FLAC 24", ".flac"}, {"ogg:9", "OGG HIGH", ".ogg"}, {"ogg:5", "OGG MED", ".ogg"}};
static const double RATES[] = {44100, 48000, 88200, 96000, 26041};
static const int LUFS[] = {-9, -12, -14, -16, -20};
static const int BANK_DEF[32] = {0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1, 8, 9, 10, 11, 12, 13, 14, 15, -16, -15, -14, -13, -12, -11, -10, -9};

/* ---------------- look ---------------- */
TwelveLook::TwelveLook() {
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff202224));
    setColour(juce::PopupMenu::textColourId, juce::Colour(0xffe8e8e8));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kLcd);
    setColour(juce::PopupMenu::highlightedTextColourId, kLcdInk);
}
// a black knurled knob with a white line
void TwelveLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    const float rad = juce::jmin(w, h) / 2.f - 3, cx = x + w / 2.f, cy = y + h / 2.f, ang = a0 + pos * (a1 - a0);
    g.setColour(juce::Colours::black.withAlpha(.5f)); g.fillEllipse(cx - rad, cy - rad + 3, rad * 2, rad * 2);
    g.setColour(juce::Colour(0xff1a1a1a)); g.fillEllipse(cx - rad - 2, cy - rad - 2, rad * 2 + 4, rad * 2 + 4);
    for (int k = 0; k < 60; ++k) {   // knurling
        const float a = k * juce::MathConstants<float>::twoPi / 60;
        g.setColour(k % 2 ? juce::Colour(0xff161616) : juce::Colour(0xff2e2e2e));
        juce::Path p; p.addPieSegment(cx - rad, cy - rad, rad * 2, rad * 2, a, a + juce::MathConstants<float>::twoPi / 60, .7f); g.fillPath(p);
    }
    const float r2 = rad * .7f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b3b3b), cx - r2 * .3f, cy - r2 * .5f, juce::Colour(0xff121212), cx, cy + r2, true));
    g.fillEllipse(cx - r2, cy - r2, r2 * 2, r2 * 2);
    juce::Path ptr; ptr.addRoundedRectangle(-1.5f, -rad + 1, 3.f, rad * .55f, 1.5f);
    g.setColour(juce::Colour(0xfff2f2f2)); g.fillPath(ptr, juce::AffineTransform::rotation(ang).translated(cx, cy));
}
void PanelKey::paintButton(juce::Graphics& g, bool, bool down) {
    auto r = getLocalBounds().toFloat().reduced(1.5f); r.removeFromBottom(3);
    if (down) r.translate(0, 2);
    juce::Colour a, b, edge;
    if (kind == red) { a = juce::Colour(0xffef5a4f); b = kRed2; edge = juce::Colour(0xff6e1a14); }
    else if (kind == black) { a = juce::Colour(0xff3a3a3a); b = juce::Colour(0xff101010); edge = juce::Colours::black; }
    else { a = juce::Colour(0xfffbf7ea); b = kCream2; edge = kCreamEdge; }
    if (!down) { g.setColour(juce::Colour(0xff2d2e30)); g.fillRoundedRectangle(r.translated(0, 3), 3); }
    g.setGradientFill(juce::ColourGradient(a, r.getX(), r.getY(), b, r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 3);
    g.setColour(edge); g.drawRoundedRectangle(r, 3, 1);
}
void Pad::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(4);
    if (lit) r.translate(0, 2);
    g.setColour(juce::Colour(0xff1d1d1d)); g.fillRoundedRectangle(r.expanded(4), 5);
    g.setColour(lit ? juce::Colour(0xff6a6a6a) : juce::Colour(0xff454545)); g.drawRoundedRectangle(r.expanded(2), 5, 2);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3d3d3d), r.getX(), r.getY(), juce::Colour(0xff161616), r.getCentreX(), r.getCentreY(), false));
    g.fillRoundedRectangle(r, 4);
    g.setColour(juce::Colour(0xff0b0b0b)); g.fillRoundedRectangle(r.reduced(5), 3);
    g.setColour(juce::Colour(0xff2f2f2f)); g.drawRoundedRectangle(r.reduced(5), 3, 1);
}

/* ---------------- faders ---------------- */
TwelveEditor::Target TwelveEditor::faderTarget(int i) const {
    Target t;
    if (PAGES[page][i] < 0) { t.pad = bank * 8 + i; t.min = -24; t.max = 24; t.def = BANK_DEF[t.pad]; t.step = 1; t.name = juce::String("Pad ") + "ABCD"[bank] + juce::String(i + 1); t.shortName = juce::String::charToString("ABCD"[bank]) + juce::String(i + 1); return t; }
    const auto& p = PARAMS[PAGES[page][i]]; t.id = PAGES[page][i]; t.min = p.min; t.max = p.max; t.def = p.def; t.step = p.step; t.log = p.log; t.name = p.name; t.shortName = shortName(t.id);
    return t;
}
double TwelveEditor::targetValue(const Target& t) const { return t.pad >= 0 ? proc.padTune[(size_t) t.pad].load() : proc.snapshot()[t.id]; }
juce::String TwelveEditor::fmt(const Target& t, double v) const {
    if (t.pad >= 0) return (v > 0 ? "+" : "") + juce::String((int) std::lround(v)) + "st";
    if (t.id == pid::decay) return v >= 31 ? juce::String("OFF") : juce::String((int) std::lround(v));
    return fmtVal(t.id, v).replace(" ", "");
}
void TwelveEditor::setTarget(const Target& t, double v, bool gesture) {
    v = clampd(v, t.min, t.max); if (t.step > 0) v = std::round(v / t.step) * t.step;
    touchedName = t.name.toUpperCase(); touchedVal = fmt(t, v); touched = t.id;
    if (t.pad >= 0) { proc.padTune[(size_t) t.pad] = (float) v; repaintAll(); return; }
    auto* prm = proc.apvts.getParameter(PARAMS[t.id].id);
    if (gesture) prm->beginChangeGesture();
    if (t.id == pid::machine) { Params q = proc.snapshot(); setMachine(q, (int) v); for (int j : {pid::machine, pid::rate, pid::bits, pid::ftype, pid::cutoff, pid::res}) proc.setParam(j, q[j]); }
    else prm->setValueNotifyingHost(prm->convertTo0to1((float) v));
    repaintAll();
}
void TwelveEditor::endGesture(const Target& t) { if (t.pad < 0) proc.apvts.getParameter(PARAMS[t.id].id)->endChangeGesture(); }
double Fader::valueAt(int y) const {
    const auto t = ed.faderTarget(i); const auto tr = track();
    const double n = clampd((1 - (double) (y - tr.getY()) / tr.getHeight() - .04) / .92, 0, 1);
    return t.log ? t.min * std::pow(t.max / t.min, n) : t.min + n * (t.max - t.min);
}
void Fader::mouseDown(const juce::MouseEvent& e) {
    const auto t = ed.faderTarget(i);
    if (ed.learnArmed && t.id >= 0) { ed.proc.learnParam = t.id; ed.say("MOVE A CONTROL FOR " + t.shortName, 6000); ed.learnArmed = false; return; }
    const uint32_t now = juce::Time::getMillisecondCounter();
    if (now - lastDown < 320) { ed.setTarget(t, t.def, true); ed.endGesture(t); lastDown = 0; return; }
    lastDown = now; dragging = true; ed.setTarget(t, valueAt(e.y), true);
}
void Fader::mouseDrag(const juce::MouseEvent& e) { if (dragging) { const auto t = ed.faderTarget(i); ed.setTarget(t, valueAt(e.y), false); } }
void Fader::mouseUp(const juce::MouseEvent&) { if (dragging) { dragging = false; ed.endGesture(ed.faderTarget(i)); } }
void Fader::paint(juce::Graphics& g) {
    const auto t = ed.faderTarget(i); const double v = ed.targetValue(t);
    const double n = clampd(t.log ? std::log(v / t.min) / std::log(t.max / t.min) : (v - t.min) / (t.max - t.min), 0, 1);
    const auto tr = track(); const float cx = getWidth() / 2.f;
    g.setColour(kDim); g.setFont(mono(10, false)); g.drawText(ed.fmt(t, v).substring(0, 8), 0, 0, getWidth(), 13, juce::Justification::centred);
    g.setColour(juce::Colour(0xffc9cccf).withAlpha(.75f));
    for (int k = 0; k <= 8; ++k) { const float y = tr.getY() + tr.getHeight() * (.06f + .88f * k / 8); g.fillRect(6.f, y, getWidth() - 12.f, 1.f); }
    g.setColour(juce::Colour(0xff050505)); g.fillRoundedRectangle(cx - 3, (float) tr.getY() + 4, 6, (float) tr.getHeight() - 8, 3);
    const float y = tr.getY() + tr.getHeight() * (float) (.04 + (1 - n) * .92);
    juce::Rectangle<float> cap(cx - 19, y - 10, 38, 20);
    g.setColour(juce::Colours::black.withAlpha(.6f)); g.fillRoundedRectangle(cap.translated(0, 3), 2);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3a3a), 0, cap.getY(), juce::Colour(0xff050505), 0, cap.getCentreY() + 2, false)); g.fillRoundedRectangle(cap, 2);
    g.setColour(juce::Colour(0xfff0f0f0)); g.fillRect(cap.reduced(4, 0).withHeight(2).withCentre(cap.getCentre()));
    g.setColour(kPrint); g.setFont(font(11, true)); g.drawText(t.shortName, 0, getHeight() - 30, getWidth(), 14, juce::Justification::centred);
    g.setFont(font(11)); g.drawText(juce::String(i + 1), 0, getHeight() - 15, getWidth(), 14, juce::Justification::centred);
}

/* ---------------- the display ---------------- */
Lcd::Lcd(TwelveEditor& e) : ed(e) {}
juce::Rectangle<int> Lcd::waveArea() const { return getLocalBounds().reduced(13, 0).withTrimmedTop(50).withTrimmedBottom(25); }
std::pair<int64_t, int64_t> Lcd::view() const {
    const auto s = ed.proc.selection(); const int64_t n = ed.proc.sourceLength();
    if (!ed.zoom) return {0, n};
    const int64_t pad = (s.second - s.first) / 25; return {juce::jmax<int64_t>(0, s.first - pad), juce::jmin(n, s.second + pad)};
}
float Lcd::xOf(int64_t i) const { const auto wa = waveArea(); const auto v = view(); return wa.getX() + (float) ((double) (i - v.first) / (double) juce::jmax<int64_t>(1, v.second - v.first) * wa.getWidth()); }
int64_t Lcd::sampleAt(int x) const {
    const auto wa = waveArea(); const auto v = view();
    return juce::jlimit<int64_t>(0, ed.proc.sourceLength(), v.first + (int64_t) ((double) (x - wa.getX()) / wa.getWidth() * (double) (v.second - v.first)));
}
void Lcd::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff1e2022)); g.fillRoundedRectangle(r, 4);
    const auto in = r.reduced(7);
    g.setGradientFill(juce::ColourGradient(kLcd, 0, in.getY(), kLcd2, 0, in.getBottom(), false)); g.fillRoundedRectangle(in, 2);
    g.setColour(juce::Colour(0xff8b9a62)); g.drawRoundedRectangle(in, 2, 1);
    const int x0 = 13, w = getWidth() - 26;
    g.setColour(kLcdInk); g.setFont(mono(14));
    g.drawText(ed.line1(), x0, 10, w, 16, juce::Justification::left, true);
    g.drawText(ed.proc.presetName().toUpperCase().substring(0, 24), x0 + w / 2, 10, w / 2, 16, juce::Justification::right, true);
    g.setFont(mono(17)); g.drawText(ed.line2(), x0, 28, w, 20, juce::Justification::left, true); g.drawText(ed.line2r(), x0, 28, w, 20, juce::Justification::right, true);
    const auto wa = waveArea();
    g.setColour(juce::Colour(0xff7f8d57)); g.drawHorizontalLine(wa.getY(), (float) wa.getX(), (float) wa.getRight()); g.drawHorizontalLine(wa.getBottom(), (float) wa.getX(), (float) wa.getRight());
    // info line and the output meter
    {
        juce::String info; const double sr = ed.proc.sourceSr();
        if (ed.proc.hasSource()) {
            const auto s = ed.proc.selection();
            info << (ed.proc.tempo > 0 ? juce::String(ed.proc.tempo, 1) : juce::String("--")) << "BPM " << (ed.proc.key.root >= 0 ? juce::String(ed.proc.key.name()) : juce::String("--"))
                 << "  IN " << juce::String((double) s.first / sr, 3) << "  OUT " << juce::String((double) s.second / sr, 3);
            if (ed.cursor >= 0) info << "  CUR " << juce::String((double) ed.cursor / sr, 3);
        } else info << "LIVE INPUT " << (ed.proc.apvts.getRawParameterValue("live")->load() > .5f ? "ON" : "OFF");
        g.setFont(mono(11, false)); g.setColour(kLcdInk); g.drawText(info, x0, getHeight() - 22, w - 70, 14, juce::Justification::left, true);
        const float db = 20 * std::log10(ed.proc.meterOut.load() + 1e-9f), f = juce::jlimit(0.f, 1.f, (db + 48) / 48);
        juce::Rectangle<float> m((float) (x0 + w - 62), (float) getHeight() - 19, 62, 8);
        g.setColour(juce::Colour(0xff9aa86d)); g.fillRect(m); g.setColour(kLcdInk); g.fillRect(m.withWidth(m.getWidth() * f));
    }
    // a menu on the display
    if (ed.menu >= 0 && !ed.entry) {
        const auto items = ed.menuItems(); const int rows = (int) (items.size() + 1) / 2; const float rh = wa.getHeight() / (float) juce::jmax(1, rows);
        g.setFont(mono(13));
        for (size_t k = 0; k < items.size(); ++k) {
            const auto& it = items[k]; juce::Rectangle<float> c(wa.getX() + (k % 2) * wa.getWidth() / 2.f, wa.getY() + 1 + (k / 2) * rh, wa.getWidth() / 2.f - 4, rh);
            const bool sel = ed.edit && ed.edit->n == it.n;
            if (sel) { g.setColour(kLcdInk); g.fillRect(c); }
            g.setColour(sel ? kLcd : kLcdInk); juce::String t = juce::String(it.n) + " " + it.name.toUpperCase();
            g.drawText(t, c.reduced(3, 0), juce::Justification::centredLeft, true);
            if (it.value) { g.setColour(sel ? kLcd : kLcdDim); g.drawText(it.value(), c.reduced(3, 0), juce::Justification::centredRight, true); }
        }
        return;
    }
    if (!ed.proc.hasSource()) { g.setColour(kLcdInk.withAlpha(.4f)); g.setFont(mono(13)); g.drawText("DISK 1 OR SAMPLE 1 LOADS A R_SAMPLE", wa, juce::Justification::centred); return; }
    const auto v = view(); const auto s = ed.proc.selection();
    const int top = wa.getY() + 11; const float H = (float) (wa.getBottom() - top), mid = top + H / 2;
    std::vector<float> mn, mx; ed.proc.getPeaks(mn, mx, v.first, v.second, wa.getWidth());
    for (int x = 0; x < wa.getWidth(); ++x) {
        const int64_t at = v.first + (int64_t) ((double) (v.second - v.first) * x / wa.getWidth());
        g.setColour(at >= s.first && at < s.second ? kLcdInk : kLcdInk.withAlpha(.32f));
        g.fillRect((float) (wa.getX() + x), mid - mx[(size_t) x] * H / 2, 1.f, juce::jmax(1.f, (mx[(size_t) x] - mn[(size_t) x]) * H / 2));
    }
    g.setFont(juce::Font(juce::FontOptions(10.f, juce::Font::bold)));
    for (int k = 0; k < 2; ++k) {
        const float x = xOf(k ? s.second : s.first); const juce::String t = k ? "OUT" : "IN"; const float fw = t.length() * 7.f + 6;
        const float fx = juce::jlimit((float) wa.getX(), (float) wa.getRight() - fw, k ? x - fw : x);
        g.setColour(kLcdInk); g.fillRect(x - .75f, (float) wa.getY(), 1.5f, (float) wa.getHeight()); g.fillRect(fx, (float) wa.getY(), fw, 11.f);
        g.setColour(kLcd); g.drawText(t, juce::Rectangle<float>(fx, (float) wa.getY(), fw, 11.f), juce::Justification::centred);
    }
    g.setColour(kLcdInk);
    if (ed.cursor >= 0) { const float x = xOf(ed.cursor); for (int y = top; y < wa.getBottom(); y += 4) g.fillRect(x - .5f, (float) y, 1.f, 2.f); }
    if (ed.proc.isPlaying()) g.fillRect(xOf(s.first + (int64_t) (ed.proc.playFraction() * (double) (s.second - s.first))) - 1, (float) top, 2.f, H);
}
void Lcd::mouseDown(const juce::MouseEvent& e) {
    const auto wa = waveArea(); mode = 0;
    if (ed.menu >= 0 && !ed.entry) {
        if (!wa.contains(e.getPosition())) return;
        const auto items = ed.menuItems(); const int rows = (int) (items.size() + 1) / 2;
        const int col = e.x < wa.getCentreX() ? 0 : 1, row = juce::jlimit(0, rows - 1, (int) ((e.y - wa.getY()) / (wa.getHeight() / (float) rows)));
        const size_t k = (size_t) (row * 2 + col); if (k < items.size()) ed.runItem(items[k].n);
        return;
    }
    if (!ed.proc.hasSource() || !wa.expanded(0, 8).contains(e.getPosition())) return;
    const auto s = ed.proc.selection(); const float dA = std::abs(e.x - xOf(s.first)), dB = std::abs(e.x - xOf(s.second));
    // the flags: grab the nearer one; anywhere else moves the cursor (nothing is selected for you)
    if (juce::jmin(dA, dB) < 12) mode = dA <= dB ? 1 : 2;
    else { mode = 3; ed.cursor = sampleAt(e.x); }
    repaint();
}
void Lcd::mouseDrag(const juce::MouseEvent& e) {
    if (!mode) return;
    const auto s = ed.proc.selection(); const int64_t i = sampleAt(e.x), minLen = (int64_t) (ed.proc.sourceSr() * .005);
    if (mode == 1) ed.proc.setSelection(juce::jmin(i, s.second - minLen), s.second);
    else if (mode == 2) ed.proc.setSelection(s.first, juce::jmax(i, s.first + minLen));
    else ed.cursor = i;
    repaint();
}
void Lcd::mouseUp(const juce::MouseEvent&) {
    if (mode == 1 || mode == 2) { const auto s = ed.proc.selection(); ed.say(juce::String(mode == 1 ? "IN " : "OUT ") + juce::String((double) (mode == 1 ? s.first : s.second) / ed.proc.sourceSr(), 3) + "S"); }
    mode = 0;
}

/* ---------------- editor ---------------- */
PanelKey& TwelveEditor::addKey(const juce::String& name, PanelKey::Kind k, juce::Rectangle<int> r, std::function<void()> f) {
    auto key = std::make_unique<PanelKey>(k); key->setBounds(r); key->onClick = f; key->setTitle(name); content.addAndMakeVisible(*key);
    auto& ref = *key; named[name] = key.get(); keys.push_back(std::move(key)); return ref;
}
// section positions on the 1200 x 1000 panel (the same as the app's wide layout)
static const juce::Rectangle<int> R_SETUP(36, 86, 205, 264), R_DISK(257, 86, 205, 264), R_SYNC(478, 86, 120, 264), R_SAMPLE(614, 86, 215, 264), R_MC(845, 86, 319, 196),
    R_MBTN(845, 290, 319, 60), R_PROG(36, 358, 793, 128), R_KNOBS(845, 358, 319, 128), R_PERF(36, 494, 793, 292), R_KEYPAD(845, 494, 319, 292), R_PADS(36, 794, 793, 120), R_TRANS(845, 794, 319, 120);

TwelveEditor::TwelveEditor(TwelveProcessor& p) : AudioProcessorEditor(p), proc(p), lcd(*this) {
    setLookAndFeel(&look);
    content.draw = [this](juce::Graphics& g) { drawContent(g); };
    // tapping a printed menu line runs it, like the app
    content.click = [this](const juce::MouseEvent& e) {
        const juce::Rectangle<int> secs[4] = {R_SETUP, R_DISK, R_SYNC, R_SAMPLE};
        for (int m = 0; m < 4; ++m) {
            const auto r = secs[m]; const int lx = m < 2 ? r.getX() + 46 : r.getX(), ly = m < 2 ? 128 : 186;
            if (!r.contains(e.getPosition()) || e.y < ly || e.x < lx) continue;
            const int row = (e.y - ly) / 13; const int saved = menu; menu = m; const auto items = menuItems();
            if (row >= 0 && row < (int) items.size()) { runItem(items[(size_t) row].n); } else menu = saved;
            repaintAll(); return;
        }
        for (int i = 0; i < 8; ++i) if (juce::Rectangle<int>(82, 590 + i * 19, 130, 18).contains(e.getPosition())) { page = i; say(juce::String(PAGE_NAMES[i]).toUpperCase() + " FADERS", 1200); return; }
    };
    addAndMakeVisible(content);
    content.addAndMakeVisible(lcd); lcd.setBounds(R_MC.getX(), R_MC.getY() + 34, R_MC.getWidth(), R_MC.getHeight() - 34);
    // section keys
    const juce::Rectangle<int> secs[4] = {R_SETUP, R_DISK, R_SYNC, R_SAMPLE}; const char* names[4] = {"Set-up", "Disk", "Sync", "Sample"};
    for (int m = 0; m < 4; ++m) addKey(names[m], m == 3 ? PanelKey::red : PanelKey::cream, {secs[m].getX(), 147, 36, 38}, [this, m] { openMenu(m); });
    // master control
    const char* mk[4] = {"Tempo", "Left", "Right", "Enter"};
    for (int i = 0; i < 4; ++i) addKey(mk[i], PanelKey::cream, {R_MBTN.getX() + 4 + i * 92, R_MBTN.getY(), 36, 38}, [this, i] { if (i == 0) openMenu(4); else if (i == 1) arrow(-1); else if (i == 2) arrow(1); else enter(); });
    // keypad
    const char* kp[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "No", "0", "Yes"};
    for (int i = 0; i < 12; ++i) { const juce::String k = kp[i]; addKey("key" + k, PanelKey::cream, {R_KEYPAD.getX() + 40 + (i % 3) * 80 + 22, R_KEYPAD.getY() + 26 + (i / 3) * 69, 36, 38}, [this, k] { keypad(k); }); }
    // programming: Edit functions, or Master functions with the Master light on
    addKey("Shift", PanelKey::cream, {R_PROG.getX(), 412, 36, 38}, [this] { shift = !shift; say(shift ? "MASTER KEYS" : "EDIT KEYS", 1000); });
    auto both = [this](std::function<void()> a, std::function<void()> b) { return [this, a, b] { (shift ? b : a)(); }; };
    const std::function<void()> fa[8] = {[this] { setIn(); }, [this] { setOut(); }, [this] { zoom = false; say(proc.truncate().toUpperCase().substring(0, 30)); }, [this] { say(proc.normalize().toUpperCase().substring(0, 30)); },
        [this] { say(proc.reverse().toUpperCase()); }, [this] { say(proc.undo().toUpperCase()); }, [this] { job([this] { return proc.resample().upToFirstOccurrenceOf(".", false, false); }); },
        [this] { const bool b = proc.snapshot()[pid::bypass] < .5; proc.setParam(pid::bypass, b ? 1 : 0); say(b ? "BYPASS ON" : "BYPASS OFF"); }};
    const std::function<void()> fb[8] = {[this] { job([this] { return proc.autoMasterNow(LUFS[lufsIdx]); }); }, [this] { job([this] { return proc.matchEq(); }); },
        [this] { openFile("Reference track for EQ match", [this](juce::File f) { job([this, f] { return proc.loadReference(f); }); }); }, [this] { proc.clearMatch(); say("MATCH CLEARED"); },
        [this] { const bool on = proc.snapshot()[pid::mOn] < .5; proc.setParam(pid::mOn, on ? 1 : 0); say(on ? "MASTER ON" : "MASTER OFF"); }, [this] { proc.resetMaster(); say("MASTER RESET"); },
        [this] { proc.randomize(); say("RANDOM " + proc.presetName().toUpperCase()); }, [this] { showHelp(); }};
    for (int i = 0; i < 8; ++i) addKey("prog" + juce::String(i), PanelKey::cream, {170 + i * 82 + 23, 422, 36, 38}, both(fa[i], fb[i]));
    // performance: page key and faders
    addKey("Page", PanelKey::cream, {R_PERF.getX(), 624, 36, 38}, [this] { page = (page + 1) % 8; say(juce::String(PAGE_NAMES[page]).toUpperCase() + " FADERS", 1200); });
    for (int i = 0; i < 8; ++i) { auto f = std::make_unique<Fader>(*this, i); f->setBounds(230 + i * 75, 530, 75, 252); content.addAndMakeVisible(*f); faders.push_back(std::move(f)); }
    // pads and banks
    addKey("Bank", PanelKey::cream, {R_PADS.getX(), 834, 36, 38}, [this] { bank = (bank + 1) % 4; say(juce::String("BANK ") + "ABCD"[bank], 1000); });
    for (int i = 0; i < 8; ++i) {
        auto pd = std::make_unique<Pad>(); pd->setBounds(170 + i * 82 + 8, 812, 66, 64);
        pd->onHit = [this, i] { const int k = bank * 8 + i; const float st = proc.padTune[(size_t) k].load(); proc.hitPad(st, 1.f); if (menu < 0 && !entry && !edit) say(juce::String("PAD ") + "ABCD"[bank] + juce::String(i + 1) + "  " + (st > 0 ? "+" : "") + juce::String((int) st) + " ST", 900); };
        content.addAndMakeVisible(*pd); pads.push_back(std::move(pd));
    }
    // transport
    addKey("Repeat", PanelKey::black, {R_TRANS.getX() + 53 - 18, 836, 36, 38}, [this] { proc.loop = !proc.loop; say(proc.loop ? "REPEAT ON" : "REPEAT OFF", 900); });
    addKey("Run", PanelKey::black, {R_TRANS.getX() + 160 - 18, 836, 36, 38}, [this] {
        if (proc.isPlaying()) { proc.stopMain(); return; }
        const auto s = proc.selection(); proc.play(cursor > s.first && cursor < s.second ? (double) (cursor - s.first) / (double) (s.second - s.first) : 0); });
    addKey("Stop", PanelKey::red, {R_TRANS.getX() + 266 - 18, 836, 36, 38}, [this] { proc.stopAll(); say("STOP ALL"); });
    // knobs: Gain (input), Mix Volume (output), Master Volume (master gain)
    const std::pair<int, juce::Rectangle<int>> kn[3] = {{pid::input, {R_SAMPLE.getRight() - 62, 128, 60, 60}}, {pid::output, {R_KNOBS.getX() + 50, 384, 64, 64}}, {pid::mGain, {R_KNOBS.getX() + 205, 384, 64, 64}}};
    for (auto& [id, r] : kn) {
        auto s = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
        s->setRotaryParameters(juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
        s->setDoubleClickReturnValue(true, (double) PARAMS[id].def); s->setBounds(r); content.addAndMakeVisible(*s);
        const int pidx = id; s->onDragStart = [this, pidx] { if (learnArmed) { proc.learnParam = pidx; learnArmed = false; say("MOVE A CONTROL FOR " + shortName(pidx), 6000); } };
        s->onValueChange = [this, pidx] { touched = pidx; touchedName = juce::String(PARAMS[pidx].name).toUpperCase(); touchedVal = fmtVal(pidx, proc.snapshot()[pidx]); lcd.repaint(); };
        kAtt.push_back(std::make_unique<juce::SliderParameterAttachment>(*proc.apvts.getParameter(PARAMS[id].id), *s));
        knobs.push_back(std::move(s));
    }
    setResizable(true, true);
    setResizeLimits(BW / 3, BH / 3, BW * 2, BH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) BW / BH);
    setSize(BW * 3 / 4, BH * 3 / 4);
    startTimerHz(20);
}
TwelveEditor::~TwelveEditor() { setLookAndFeel(nullptr); }

void TwelveEditor::paint(juce::Graphics& g) { g.fillAll(kCase); }
void TwelveEditor::resized() {
    content.setBounds(0, 0, BW, BH);
    content.setTransform(juce::AffineTransform::scale((float) getWidth() / BW));
}

void TwelveEditor::timerCallback() {
    if (learning < 0 && proc.learnParam.load() >= 0) learning = proc.learnParam.load();
    if (learning >= 0 && proc.learnParam.load() < 0) {
        int cc = -1; for (int i = 0; i < 128; ++i) if (proc.ccMap[(size_t) i].load() == learning) cc = i;
        say(shortName(learning) + " ON CC " + juce::String(cc), 2500); learning = -1;
    }
    if (msg.isNotEmpty() && juce::Time::getMillisecondCounter() > msgUntil) msg.clear();
    lcd.repaint();
    // LEDs and faders follow the host and MIDI
    content.repaint(); for (auto& f : faders) f->repaint();
}

/* ---------------- display text ---------------- */
juce::String TwelveEditor::menuTitle() const { static const char* t[5] = {"SET-UP", "DISK", "SYNC", "SAMPLE", "TEMPO / KEY"}; return menu >= 0 ? t[menu] : ""; }
juce::String TwelveEditor::line1() const { return menu >= 0 ? menuTitle() : juce::String(PAGE_NAMES[page]).toUpperCase(); }
juce::String TwelveEditor::line2() const {
    if (entry) return entry->label + ": " + entry->text + "_";
    if (edit) return edit->label + ":";
    if (msg.isNotEmpty()) return msg;
    if (touched >= 0 || touchedName.isNotEmpty()) return touchedName;
    if (!proc.hasSource()) return "LOAD A R_SAMPLE";
    const Params p = proc.snapshot(); return fmtVal(pid::machine, p[pid::machine]).toUpperCase() + " " + fmtVal(pid::ftype, p[pid::ftype]).toUpperCase();
}
juce::String TwelveEditor::line2r() const {
    if (entry) return {};
    if (edit) return juce::String::fromUTF8("\xe2\x97\x80 ") + edit->show() + juce::String::fromUTF8(" \xe2\x96\xb6");
    if (msg.isNotEmpty()) return {};
    return touchedName.isNotEmpty() ? touchedVal : juce::String();
}

/* ---------------- menus ---------------- */
std::vector<TwelveEditor::Item> TwelveEditor::menuItems() const {
    auto* self = const_cast<TwelveEditor*>(this);
    std::vector<Item> v;
    auto choice = [self](int n, const juce::String& label, std::function<juce::String()> show, std::function<void(int)> step) {
        return Item{n, label, [self, n, label, show, step] { auto e = std::make_unique<Edit>(); e->label = label.toUpperCase(); e->show = show; e->step = step; e->n = n; self->edit = std::move(e); self->repaintAll(); }, show};
    };
    if (menu == 0) {
        for (int i = 0; i < 8; ++i) v.push_back({i + 1, PAGE_NAMES[i], [self, i] { self->page = i; self->menu = -1; self->repaintAll(); }, nullptr});
        v.push_back({9, "Calibration", [self] { const bool m = self->proc.snapshot()[pid::calib] < .5; self->proc.setParam(pid::calib, m ? 1 : 0); }, [self] { return self->proc.snapshot()[pid::calib] > .5 ? juce::String("MEASURED") : juce::String("MANUAL"); }});
        v.push_back({0, "Help", [self] { self->menu = -1; self->showHelp(); }, nullptr});
    } else if (menu == 1) {
        v.push_back({1, "Load Sample", [self] { self->menu = -1; self->openFile("Load a sample", [self](juce::File f) { self->job([self, f] { auto e = self->proc.loadFile(f); if (e.isNotEmpty()) return e; self->cursor = -1; return "Loaded " + f.getFileName() + " " + self->proc.analyze(); }); }); }, nullptr});
        v.push_back({2, "Export Audio", [self] { self->menu = -1; self->doExport(); }, nullptr});
        v.push_back(choice(3, "Format", [self] { return juce::String(FORMATS[self->fmtIdx][1]); }, [self](int d) { self->fmtIdx = (self->fmtIdx + d + 9) % 9; }));
        v.push_back(choice(4, "Rate", [self] { return juce::String(RATES[self->srIdx] / 1000, 1) + "K"; }, [self](int d) { self->srIdx = (self->srIdx + d + 5) % 5; }));
        v.push_back(choice(5, "Range", [self] { return juce::String(self->wholeFile ? "WHOLE" : "IN-OUT"); }, [self](int) { self->wholeFile = !self->wholeFile; }));
        v.push_back({6, "Presets", [self] { self->menu = -1; self->showPresets(); }, nullptr});
        v.push_back({7, "Live Input", [self] { auto* prm = self->proc.apvts.getParameter("live"); prm->setValueNotifyingHost(prm->getValue() > .5f ? 0.f : 1.f); }, [self] { return juce::String(self->proc.apvts.getRawParameterValue("live")->load() > .5f ? "ON" : "OFF"); }});
        v.push_back({8, "Random", [self] { self->proc.randomize(); self->say("RANDOM " + self->proc.presetName().toUpperCase()); }, nullptr});
        v.push_back({9, "Reference", [self] { self->openFile("Reference track for EQ match", [self](juce::File f) { self->job([self, f] { return self->proc.loadReference(f); }); }); }, nullptr});
        v.push_back({0, "Init Sound", [self] { Params d; self->proc.applyValues(d, true); self->proc.setPresetName("Init"); self->say("INIT"); }, nullptr});
    } else if (menu == 2) {
        v.push_back({1, "MIDI Learn", [self] { self->learnArmed = !self->learnArmed; self->proc.learnParam = -1; self->say(self->learnArmed ? "TOUCH A FADER OR KNOB" : "LEARN OFF", 3000); }, [self] { return juce::String(self->learnArmed ? "ARMED" : ""); }});
        v.push_back({2, "Default Map", [self] { self->proc.resetCCMap(); self->say("DEFAULT MIDI MAP"); }, nullptr});
        v.push_back({3, "Live Input", [self] { auto* prm = self->proc.apvts.getParameter("live"); prm->setValueNotifyingHost(prm->getValue() > .5f ? 0.f : 1.f); }, [self] { return juce::String(self->proc.apvts.getRawParameterValue("live")->load() > .5f ? "ON" : "OFF"); }});
        v.push_back({4, "Notes: C3 = Pitch", [self] { self->say("MIDI C3 PLAYS THE R_SAMPLE", 2500); }, nullptr});
        v.push_back({0, "Help", [self] { self->menu = -1; self->showHelp(); }, nullptr});
    } else if (menu == 3) {
        v.push_back({1, "Load Sample", [self] { self->menu = 1; self->runItem(1); }, nullptr});
        v.push_back({2, "Set In", [self] { self->setIn(); }, nullptr});
        v.push_back({3, "Set Out", [self] { self->setOut(); }, nullptr});
        v.push_back({4, "Select All", [self] { self->proc.setSelection(0, self->proc.sourceLength()); self->zoom = false; self->say("ALL SELECTED"); }, nullptr});
        v.push_back({5, "Truncate", [self] { self->zoom = false; self->cursor = -1; self->say(self->proc.truncate().toUpperCase().substring(0, 30)); }, nullptr});
        v.push_back({6, "Normalize", [self] { self->say(self->proc.normalize().toUpperCase().substring(0, 30)); }, nullptr});
        v.push_back({7, "Reverse", [self] { self->say(self->proc.reverse().toUpperCase()); }, nullptr});
        v.push_back({8, "Re-Sample", [self] { self->job([self] { return self->proc.resample().upToFirstOccurrenceOf(".", false, false); }); }, nullptr});
        v.push_back({9, "Zoom", [self] { self->zoom = !self->zoom; }, [self] { return juce::String(self->zoom ? "ON" : ""); }});
        v.push_back({0, "Undo", [self] { self->say(self->proc.undo().toUpperCase()); }, nullptr});
    } else if (menu == 4) {
        auto entryOf = [self](const juce::String& label, std::function<void(double)> done) { auto e = std::make_unique<Entry>(); e->label = label; e->done = done; self->entry = std::move(e); self->repaintAll(); };
        v.push_back({1, "Detect", [self] { self->job([self] { auto r = self->proc.analyze(); if (self->proc.tempo > 0) self->srcBpm = self->proc.tempo; if (self->proc.key.root >= 0) self->toKey = self->proc.key.root; return r; }); }, [self] { return self->proc.tempo > 0 ? juce::String(self->proc.tempo, 1) : juce::String(); }});
        v.push_back({2, "Sample BPM", [self, entryOf] { entryOf("SAMPLE BPM", [self](double x) { self->srcBpm = x; }); }, [self] { return self->srcBpm > 0 ? juce::String(self->srcBpm, 1) : juce::String("--"); }});
        v.push_back({3, "Half", [self] { self->srcBpm /= 2; }, nullptr});
        v.push_back({4, "Double", [self] { self->srcBpm *= 2; }, nullptr});
        v.push_back({5, "Target BPM", [self, entryOf] { entryOf("TARGET BPM", [self](double x) { self->tgtBpm = x; }); }, [self] { return juce::String(self->tgtBpm, 1); }});
        v.push_back(choice(6, "Warp Mode", [self] { return juce::String(self->stretchWarp ? "STRETCH" : "SPEED"); }, [self](int) { self->stretchWarp = !self->stretchWarp; }));
        v.push_back({7, "Warp", [self] { self->say(self->proc.warp(self->srcBpm, self->tgtBpm, self->stretchWarp).toUpperCase().substring(0, 30)); }, nullptr});
        v.push_back(choice(8, "To PanelKey", [self] { return juce::String(NOTE_NAMES[self->toKey]); }, [self](int d) { self->toKey = (self->toKey + d + 12) % 12; }));
        v.push_back({9, "Transpose", [self] { self->say(self->proc.transposeTo(self->toKey).toUpperCase().substring(0, 30)); }, nullptr});
        v.push_back(choice(0, "Loudness", [self] { return juce::String(LUFS[self->lufsIdx]) + " LUFS"; }, [self](int d) { self->lufsIdx = (self->lufsIdx + d + 5) % 5; }));
    }
    return v;
}
void TwelveEditor::runItem(int n) {
    edit.reset();
    for (auto& it : menuItems()) if (it.n == n) { it.run(); break; }
    repaintAll();
}
void TwelveEditor::openMenu(int m) { edit.reset(); entry.reset(); menu = menu == m ? -1 : m; touched = -1; touchedName.clear(); repaintAll(); }
void TwelveEditor::keypad(const juce::String& k) {
    if (entry) {
        if (k == "Yes") { const double v = entry->text.getDoubleValue(); auto e = std::move(entry); if (v > 0) e->done(v); }
        else if (k == "No") { if (entry->text.isNotEmpty()) entry->text = entry->text.dropLastCharacters(1); else entry.reset(); }
        else if (entry->text.length() < 6) entry->text << k;
        repaintAll(); return;
    }
    if (edit) { if (k == "Yes" || k == "No") edit.reset(); repaintAll(); return; }
    if (menu >= 0) { if (k == "No") openMenu(menu); else if (k != "Yes") runItem(k.getIntValue()); return; }
    if (k == "Yes" || k == "No") return;
    // no menu: a preset number
    auto e = std::make_unique<Entry>(); e->label = "PRESET #"; e->text = k;
    e->done = [this](double v) { const int i = (int) std::lround(v) - 1; if (i >= 0 && i < kPresets) { proc.setCurrentProgram(i); say("P" + juce::String(i + 1) + " LOADED", 900); } else say("NO SUCH PRESET"); };
    entry = std::move(e); repaintAll();
}
void TwelveEditor::stepPreset(int d) { proc.setCurrentProgram((proc.getCurrentProgram() + kPresets + d) % kPresets); say("P" + juce::String(proc.getCurrentProgram() + 1) + " LOADED", 900); }
void TwelveEditor::arrow(int d) {
    if (edit) { edit->step(d); repaintAll(); return; }
    if (entry || menu >= 0) return;
    stepPreset(d);
}
void TwelveEditor::enter() {
    if (entry) { keypad("Yes"); return; }
    if (edit) { edit.reset(); repaintAll(); return; }
    if (menu >= 0) { openMenu(menu); return; }
    showPresets();
}
void TwelveEditor::setIn() {
    if (!proc.hasSource()) { say("LOAD A R_SAMPLE"); return; }
    const auto s = proc.selection(); const int64_t c = cursor < 0 ? 0 : cursor;
    if (c >= s.second - 32) { say("CURSOR IS PAST OUT"); return; }
    proc.setSelection(c, s.second); say("IN " + juce::String((double) c / proc.sourceSr(), 3) + "S");
}
void TwelveEditor::setOut() {
    if (!proc.hasSource()) { say("LOAD A R_SAMPLE"); return; }
    const auto s = proc.selection(); const int64_t c = cursor < 0 ? proc.sourceLength() : cursor;
    if (c <= s.first + 32) { say("CURSOR IS BEFORE IN"); return; }
    proc.setSelection(s.first, c); say("OUT " + juce::String((double) c / proc.sourceSr(), 3) + "S");
}
void TwelveEditor::openFile(const juce::String& title, std::function<void(juce::File)> then) {
    chooser = std::make_unique<juce::FileChooser>(title, juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.caf;*.wma;*.mp4");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [then](const juce::FileChooser& c) { if (c.getResult().existsAsFile()) then(c.getResult()); });
}
void TwelveEditor::doExport() {
    if (!proc.hasSource()) { say("LOAD A R_SAMPLE"); return; }
    const juce::String code = FORMATS[fmtIdx][0], ext = FORMATS[fmtIdx][2]; const double fs = RATES[srIdx]; const bool whole = wholeFile;
    const auto base = proc.sourceName().upToLastOccurrenceOf(".", false, false) + " - " + proc.presetName().removeCharacters("\\/:*?\"<>|");
    chooser = std::make_unique<juce::FileChooser>("Export", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(base + ext), "*" + ext);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, code, fs, whole, ext](const juce::FileChooser& c) {
        auto f = c.getResult(); if (f == juce::File()) return;
        if (!f.hasFileExtension(ext)) f = f.withFileExtension(ext);
        say("EXPORTING", 20000);
        job([this, f, code, fs, whole] { return proc.exportTo(f, code, fs, whole); });
    });
}
void TwelveEditor::showPresets() {
    juce::PopupMenu root, sub; juce::String fam;
    for (int i = 0; i < kPresets; ++i) {
        const juce::String f = juce::CharPointer_UTF8(PRESETS[i].family);
        if (f != fam) { if (fam.isNotEmpty()) root.addSubMenu(fam, sub); sub = juce::PopupMenu(); fam = f; }
        sub.addItem(i + 1, juce::String(i + 1) + " " + juce::String(juce::CharPointer_UTF8(PRESETS[i].name)), true, i == proc.getCurrentProgram());
    }
    root.addSubMenu(fam, sub);
    root.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&lcd), [this](int r) { if (r > 0) { proc.setCurrentProgram(r - 1); say("P" + juce::String(r) + " LOADED", 900); } });
}
void TwelveEditor::showHelp() {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::NoIcon, "Omega Twelve",
        "Pads play the sample at each pad's pitch: Bank A 0 to +7, B -8 to -1, C +8 to +15, D -16 to -9 (set any pad on the Multi Mode page).\n\n"
        "The faders control the lit page; Performance steps through the pages, or click a page name. Double-click a fader to reset it.\n\n"
        "Display: click the waveform to place the cursor; drag the IN and OUT flags, or press Set In / Set Out to put them at the cursor.\n\n"
        "Set-up, Disk, Sync, Sample and Tempo open menus on the display: click an item or press its number. Choices change with the arrows; numbers are typed on the keypad and confirmed with Yes. "
        "With no menu open the arrows change preset, the keypad types a preset number and Enter opens the preset list.\n\n"
        "Programming keys: the Edit functions, or the Master functions with the Master light on. Run/Stop plays the selection, Tap/Repeat loops it.\n\n"
        "MIDI: C3 plays the sample at its own pitch. Everything plays live: turn anything and you hear it at once.");
}

/* ---------------- the panel ---------------- */
void TwelveEditor::drawContent(juce::Graphics& g) {
    g.fillAll(kCase);
    g.setGradientFill(juce::ColourGradient(kFace, 0, 82, kFace2, 0, 920, false)); g.fillRoundedRectangle(22, 82, 1156, 836, 6);
    auto strip = [&](juce::Rectangle<float> r) { g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe1e1dd), 0, r.getY(), kStrip, 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 4); };
    strip({36, 16, 1128, 62}); strip({36, 924, 1128, 46});
    g.setColour(juce::Colour(0xff121212));
    g.setFont(font(50, true)); g.drawText("OMEGA", 62, 18, 220, 58, juce::Justification::centredLeft);
    g.setFont(font(32, true)); g.drawText("12", 222, 30, 60, 44, juce::Justification::centredLeft);
    g.setFont(font(10)); g.drawText("S  A  M  P  L  I  N  G      P  E  R  C  U  S  S  I  O  N", 300, 46, 420, 14, juce::Justification::centredLeft);
    g.setFont(font(32)); g.drawText("Omega Twelve", 62, 926, 400, 42, juce::Justification::centredLeft);
    // section titles with the blue rule
    auto title = [&](juce::Rectangle<int> r, const juce::String& t) { g.setColour(kPrint); g.setFont(font(22, true)); g.drawText(t, r.getX(), r.getY(), r.getWidth(), 26, juce::Justification::bottomLeft); g.setColour(kRule); g.fillRect(r.getX(), r.getY() + 29, r.getWidth(), 3); };
    title(R_SETUP, "Set-up"); title(R_DISK, "Disk"); title(R_SYNC, "Sync"); title(R_SAMPLE, "Sample"); title(R_MC, "Master Control"); title(R_PROG, "Programming"); title(R_PERF, "Performance");
    auto led = [&](float x, float y, bool on) {
        g.setColour(juce::Colour(0xff1c1c1c)); g.fillEllipse(x - 1.5f, y - 1.5f, 12, 12);
        if (on) { g.setColour(kLed.withAlpha(.35f)); g.fillEllipse(x - 4, y - 4, 17, 17); }
        g.setColour(on ? kLed : kLedOff); g.fillEllipse(x, y, 9, 9);
    };
    auto lbl = [&](const juce::String& t, juce::Rectangle<int> r, float h = 11, bool dimmed = false) { g.setColour(kPrint.withAlpha(dimmed ? .45f : 1.f)); g.setFont(font(h)); g.drawFittedText(t, r, juce::Justification::centred, 2); };
    // section keys' LEDs and the printed menus
    const juce::Rectangle<int> secs[4] = {R_SETUP, R_DISK, R_SYNC, R_SAMPLE};
    for (int m = 0; m < 4; ++m) {
        led((float) secs[m].getX() + 13, 130, menu == m);
        const int saved = menu; menu = m; const auto items = menuItems(); menu = saved;
        const int lx = m < 2 ? secs[m].getX() + 46 : secs[m].getX(), ly = m < 2 ? 128 : 186;
        g.setColour(kPrint); g.setFont(font(12));
        for (size_t k = 0; k < items.size(); ++k) g.drawText(juce::String(items[k].n) + " " + items[k].name, lx, ly + (int) k * 13, secs[m].getRight() - lx, 13, juce::Justification::centredLeft, true);
    }
    lbl("Gain", {R_SAMPLE.getRight() - 62, 188, 60, 14});
    const juce::String ml[4] = {"Tempo", juce::String::fromUTF8("\xe2\x97\x80"), juce::String::fromUTF8("\xe2\x96\xb6"), "Enter"};
    for (int i = 0; i < 4; ++i) lbl(ml[i], {R_MBTN.getX() + 4 + i * 92 - 20, R_MBTN.getY() + 42, 76, 14});
    lbl("Mix Volume", {R_KNOBS.getX() + 32, 452, 100, 14}); lbl("Master Volume", {R_KNOBS.getX() + 187, 452, 100, 14});
    const char* kp[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "No", "0", "Yes"};
    for (int i = 0; i < 12; ++i) lbl(kp[i], {R_KEYPAD.getX() + 40 + (i % 3) * 80, R_KEYPAD.getY() + 8 + (i / 3) * 69, 80, 14});
    // programming
    led(82, 418, !shift); led(82, 442, shift != 0); g.setColour(kPrint); g.setFont(font(12)); g.drawText("Edit", 96, 413, 60, 18, juce::Justification::centredLeft); g.drawText("Master", 96, 437, 60, 18, juce::Justification::centredLeft);
    const char* top[8] = {"Set In", "Set Out", "Truncate", "Normalize", "Reverse", "Undo", "Re-Sample", "Bypass"};
    const char* bot[8] = {"Auto Master", "Match EQ", "Reference", "Clear Match", "Master On/Off", "Master Reset", "Random Preset", "Help"};
    for (int i = 0; i < 8; ++i) { const int cx = 170 + i * 82; lbl(top[i], {cx, 394, 82, 26}, 11.5f, shift != 0); lbl(bot[i], {cx, 462, 82, 24}, 11.5f, shift == 0); }
    // performance: the pages
    for (int i = 0; i < 8; ++i) { led(84, 594 + i * 19.f, i == page); g.setColour(kPrint); g.setFont(font(13)); g.drawText(PAGE_NAMES[i], 99, 589 + i * 19, 120, 18, juce::Justification::centredLeft); }
    // pads: banks, numbers; transport LEDs and labels
    for (int b = 0; b < 4; ++b) { led(84, 818 + b * 19.f, b == bank); g.setColour(kPrint); g.setFont(font(12)); g.drawText(juce::String::charToString("ABCD"[b]), 99, 813 + b * 19, 20, 18, juce::Justification::centredLeft); }
    for (int i = 0; i < 8; ++i) lbl(juce::String(i + 1), {170 + i * 82, 880, 82, 14});
    const float tx[3] = {53, 160, 266}; const char* tl[3] = {"Tap/Repeat", "Run/Stop", "Stop All"}; const bool tOn[3] = {proc.loop, proc.isPlaying(), proc.snapshot()[pid::bypass] > .5};
    for (int i = 0; i < 3; ++i) { led(R_TRANS.getX() + tx[i] - 4.5f, 816, tOn[i]); lbl(tl[i], {R_TRANS.getX() + (int) tx[i] - 50, 880, 100, 14}); }
}

}  // namespace twelve
