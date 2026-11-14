#include "DmxEditor.h"

namespace dmx {

namespace col {
const juce::Colour face1 {0xff16171b}, face2 {0xff0b0b0d}, stripe {0xb33a68d6}, print {0xffe6e8ee}, dim {0xff9aa0ad}, led {0xffff2a1a},
    key1 {0xff3e3f44}, key2 {0xff1d1e22}, key3 {0xff121315}, red1 {0xffc8221a}, red2 {0xff8a100a}, wood1 {0xff3a1c0c}, wood2 {0xff7a4424}, wood3 {0xff5a2e16};
}
static juce::Font sans(float h, bool bold = true) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }
static juce::Font mono(float h) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold)); }

DmxEditor::DmxEditor(DmxProcessor& p) : AudioProcessorEditor(p), proc(p), e(p.engine) {
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double) W / H);
    setResizeLimits(W / 2, H / 2, W * 2, H * 2);
    setSize(1024, 576);
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

// ---------------- layout ----------------
juce::Rectangle<float> DmxEditor::slot(int g) const {
    const float cw = (fadersR.getWidth() - 8 - 8 * 4) / 9.f, x = fadersR.getX() + 4 + g * (cw + 4);
    return {x + cw / 2 - 26, fadersR.getY(), 52, fadersR.getHeight() - 26};
}
juce::Rectangle<float> DmxEditor::rowLabel(int v) const {
    const auto in = matR.reduced(10, 8); const float top = in.getY() + 16, rh = (in.getHeight() - 16) / (float) e.voices();
    return {in.getX(), top + v * rh, 118, rh};
}
juce::Rectangle<float> DmxEditor::cell(int v, int s) const {
    const auto lab = rowLabel(v); const auto in = matR.reduced(10, 8);
    const float cw = (in.getRight() - lab.getRight()) / 16.f; return {lab.getRight() + s * cw, lab.getY(), cw, lab.getHeight()};
}
// the voice keys: one column group per fader group with voices, three keys to a column, widths shared out by columns
std::vector<std::pair<int, juce::Rectangle<float>>> DmxEditor::voiceKeys() const {
    std::vector<std::pair<int, juce::Rectangle<float>>> out;
    std::vector<std::vector<int>> gv(kMet);
    for (int v = 0; v < e.voices(); ++v) gv[(size_t) kKitDefs[e.kit].v[v].group].push_back(v);
    int units = 0, groups = 0; for (auto& g : gv) if (!g.empty()) { units += ((int) g.size() + 2) / 3; ++groups; }
    const float avail = matR.getWidth() - 8 - 22.f * (float) (groups - 1); float x = matR.getX() + 4;
    const float rowH = std::min(92.f, (matR.getHeight() - 24) / 3.f);
    for (auto& g : gv) {
        if (g.empty()) continue;
        const int u = ((int) g.size() + 2) / 3; const float gw = avail * (float) u / (float) units, cw = (gw - 10.f * (float) (u - 1)) / (float) u, kw = std::min(62.f, cw);
        for (size_t i = 0; i < g.size(); ++i) {
            const int c = (int) i / 3, r = (int) i % 3;
            out.push_back({g[i], {x + c * (cw + 10) + (cw - kw) / 2, matR.getY() + 24 + r * (rowH + 10), kw, 50}});
        }
        x += gw + 22;
    }
    return out;
}
std::vector<std::pair<int, juce::Rectangle<float>>> DmxEditor::groupHeads() const {
    std::vector<std::pair<int, juce::Rectangle<float>>> out; std::vector<int> n(kMet, 0);
    for (int v = 0; v < e.voices(); ++v) ++n[(size_t) kKitDefs[e.kit].v[v].group];
    int units = 0, groups = 0; for (int c : n) if (c) { units += (c + 2) / 3; ++groups; }
    const float avail = matR.getWidth() - 8 - 22.f * (float) (groups - 1); float x = matR.getX() + 4;
    for (int g = 0; g < kMet; ++g) { if (!n[(size_t) g]) continue; const float gw = avail * (float) ((n[(size_t) g] + 2) / 3) / (float) units; out.push_back({g, {x, matR.getY(), gw, 18}}); x += gw + 22; }
    return out;
}
juce::Rectangle<float> DmxEditor::padKey(int i) const {
    const juce::Rectangle<float> in(padR.getX() + 16, padR.getY() + 4, padR.getWidth() - 32, padR.getHeight() - 8);
    const float cw = (in.getWidth() - 24) / 3.f, rh = (in.getHeight() - 36) / 4.f;
    return {in.getX() + (i % 3) * (cw + 12), in.getY() + (i / 3) * (rh + 12), cw, rh};
}
juce::Rectangle<float> DmxEditor::fnKey(int i) const {
    const float cw = (fnR.getWidth() - 30) / 4.f;
    return {fnR.getX() + (i % 4) * (cw + 10) + (cw - 58) / 2, fnR.getY() + 18 + (i / 4) * 58.f, 58, 40};
}
juce::Rectangle<float> DmxEditor::smallKey(int i) const { return {smallR.getCentreX() - 29, smallR.getY() + i * 83.f, 58, 40}; }

// ---------------- drawing ----------------
void DmxEditor::drawKey(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, bool on, bool red, bool hitNow) {
    auto k = r.translated(0, hitNow ? 3.f : 0.f);
    g.setColour(juce::Colours::black); g.fillRoundedRectangle(r.translated(0, 4), 2);
    juce::ColourGradient cg(red ? col::red1 : (on ? juce::Colour(0xff5c5d64) : col::key1), k.getX(), k.getY(), red ? col::red2 : (on ? juce::Colour(0xff26272b) : col::key3), k.getX(), k.getBottom(), false);
    g.setGradientFill(cg); g.fillRoundedRectangle(k, 2);
    g.setColour(juce::Colours::white.withAlpha(.14f)); g.drawHorizontalLine((int) k.getY() + 1, k.getX() + 2, k.getRight() - 2);
    if (on) { g.setColour(juce::Colour(0x8cff321e)); g.drawRoundedRectangle(k.expanded(2), 3, 3); }
    g.setColour(on ? juce::Colour(0xffff3a2a) : juce::Colours::black); g.drawRoundedRectangle(k, 2, on ? 1.5f : 1.f);
    if (label.isNotEmpty()) { g.setColour(on ? juce::Colour(0xffff5a4a) : col::print); g.setFont(sans(11)); g.drawFittedText(label.toUpperCase(), juce::Rectangle<int>((int) r.getX() - 10, (int) r.getBottom() + 5, (int) r.getWidth() + 20, 14), juce::Justification::centred, 1, .7f); }
}

void DmxEditor::paint(juce::Graphics& g) {
    const float s = (float) getWidth() / W;
    g.addTransform(juce::AffineTransform::scale(s));
    // face: black with blue pinstripes, wood ends
    g.setGradientFill(juce::ColourGradient(col::face1, 0, 0, col::face2, 0, H, false)); g.fillRect(0, 0, W, H);
    g.setColour(col::stripe); for (int y = 13; y < H; y += 14) g.fillRect(34, y, W - 68, 1);
    for (int side = 0; side < 2; ++side) {
        const float x = side ? W - 34.f : 0.f;
        juce::ColourGradient w(col::wood1, x, 0, juce::Colour(0xff2e1408), x + 34, 0, false); w.addColour(.3, col::wood2); w.addColour(.55, juce::Colour(0xff6a3a1c)); w.addColour(.8, col::wood3);
        g.setGradientFill(w); g.fillRect(x, 0.f, 34.f, (float) H);
    }
    // logo: big outlined letters
    {   juce::Path p; juce::GlyphArrangement ga; ga.addLineOfText(juce::Font(juce::FontOptions(70.f, juce::Font::bold | juce::Font::italic)), "DMX", logoR.getX(), logoR.getY() + 66);
        ga.createPath(p); p.applyTransform(juce::AffineTransform::scale(1.16f, 1.f, logoR.getX(), 0).sheared(-.14f, 0).translated(10, 0));
        g.setColour(juce::Colour(0xff0b0b0d)); g.fillPath(p); g.setColour(juce::Colour(0xffececee)); g.strokePath(p, juce::PathStrokeType(2.6f)); }
    // faders
    for (int i = 0; i < kGroups; ++i) {
        const auto sl = slot(i); const juce::Rectangle<float> tr(sl.getCentreX() - 2.5f, sl.getY() + 2, 5, sl.getHeight() - 4);
        g.setColour(juce::Colours::black); g.fillRect(tr); g.setColour(juce::Colour(0xff2c2c2c)); g.drawRect(tr.expanded(1), 1);
        const float v = e.fader[i], y = sl.getY() + 8 + (sl.getHeight() - 16) * (1 - v);
        juce::Rectangle<float> cap(sl.getCentreX() - 15, y - 15, 30, 30);
        g.setColour(juce::Colours::black.withAlpha(.75f)); g.fillRoundedRectangle(cap.translated(0, 4), 3);
        juce::ColourGradient cg(juce::Colour(0xffd8d8dc), 0, cap.getY(), juce::Colour(0xffc4c4c8), 0, cap.getBottom(), false);
        cg.addColour(.4, juce::Colour(0xff9a9aa0)); cg.addColour(.5, juce::Colour(0xff5a5a60)); cg.addColour(.58, juce::Colour(0xff8a8a90));
        g.setGradientFill(cg); g.fillRoundedRectangle(cap, 3); g.setColour(juce::Colour(0xff0a0a0a)); g.drawRoundedRectangle(cap, 3, 1);
        g.setColour(juce::Colours::white.withAlpha(.7f)); g.fillRect(cap.getX() + 3, cap.getCentreY() - 1, cap.getWidth() - 6, 2.f);
        g.setColour(col::print); g.setFont(sans(11)); g.drawFittedText(kGroupNames[i], juce::Rectangle<int>((int) sl.getCentreX() - 34, (int) fadersR.getBottom() - 18, 68, 14), juce::Justification::centred, 1, .7f);
    }
    // LED window and the red power switch
    {   juce::Rectangle<float> w(ledR.getX(), ledR.getY(), ledR.getWidth() - 36, 66);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff050505), 0, w.getY(), juce::Colour(0xff120707), 0, w.getBottom(), false)); g.fillRoundedRectangle(w, 2);
        g.setColour(juce::Colours::black); g.drawRoundedRectangle(w, 2, 2); g.setColour(juce::Colour(0xff2a2a2a)); g.drawRoundedRectangle(w.reduced(2), 2, 1);
        g.setColour(col::led.withAlpha(.25f)); g.setFont(mono(46)); g.drawText(juce::String(e.led()), w.reduced(14, 0).translated(0, 1), juce::Justification::centredRight);
        g.setColour(col::led); g.drawText(juce::String(e.led()), w.reduced(14, 0), juce::Justification::centredRight);
        juce::Rectangle<float> pw(ledR.getRight() - 22, w.getCentreY() - 20, 22, 40);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe0281c), 0, pw.getY(), juce::Colour(0xff8e0f08), 0, pw.getBottom(), false)); g.fillRoundedRectangle(pw, 3);
        g.setColour(juce::Colour(0x55ff3020)); g.drawRoundedRectangle(pw.expanded(3), 5, 4); }
    // the voice keys, or (EDIT) the pattern's steps as red lights
    if (!edit) {
        for (auto& [gi, r] : groupHeads()) { g.setColour(col::dim); g.setFont(sans(11)); g.drawText(kGroupNames[gi], r.withHeight(14), juce::Justification::centred);
            g.setColour(juce::Colour(0xff4a4d55)); g.fillRect(r.getX(), r.getY() + 16, r.getWidth(), 1.f); }
        for (auto& [v, r] : voiceKeys()) drawKey(g, r, e.voiceName(v), v == e.sel, false, v == flashV && flashT > 0);
    } else {
        g.setColour(juce::Colour(0xff0e0303)); g.fillRoundedRectangle(matR, 2); g.setColour(juce::Colours::black); g.drawRoundedRectangle(matR, 2, 2);
        const int now = e.running ? e.nowStep.load() : -1;
        for (int st = 0; st < 16; ++st) { const auto c = cell(0, st); g.setColour(st == now ? col::led : juce::Colour(0xff7a2a24)); g.setFont(mono(9)); g.drawText(juce::String(st + 1), juce::Rectangle<float>(c.getX(), matR.getY() + 6, c.getWidth(), 14), juce::Justification::centred); }
        for (int v = 0; v < e.voices(); ++v) {
            const auto lab = rowLabel(v);
            if (v == e.sel) { g.setColour(juce::Colour(0x22ff321e)); g.fillRect(lab.withRight(matR.getRight() - 10)); }
            g.setColour(juce::Colour(0xffff6a5a)); g.setFont(mono(12)); g.drawFittedText(juce::String(e.voiceName(v)).toUpperCase(), lab.reduced(3, 0).toNearestInt(), juce::Justification::centredLeft, 1, .8f);
            for (int st = 0; st < 16; ++st) {
                const auto c = cell(v, st); const float d = juce::jmin(16.f, c.getHeight() * .78f); juce::Rectangle<float> o(c.getCentreX() - d / 2, c.getCentreY() - d / 2, d, d);
                const int val = e.stepValue(v, st);
                if (val) { g.setColour(val == 2 ? juce::Colour(0xffff6a4a) : juce::Colour(0xffc8180e)); g.fillEllipse(o); g.setColour(juce::Colour(val == 2 ? 0x66ff4a2a : 0x44ff2a1a)); g.drawEllipse(o.expanded(2), 3); }
                g.setColour(val == 2 ? juce::Colour(0xffffb0a0) : st % 4 == 0 ? juce::Colour(0xff7a2a24) : juce::Colour(0xff3a1210)); g.drawEllipse(o, 1.5f);
                if (st == now) { g.setColour(juce::Colours::white.withAlpha(.55f)); g.drawEllipse(o.expanded(2.5f), 1.5f); }
            }
        }
    }
    // TEMPO / ACCENT / FACTORY, keypad
    const bool son[4] = {e.mode == "tempo", e.mode == "tune", e.acc, false};
    for (int i = 0; i < 4; ++i) drawKey(g, smallKey(i), kSmallName[i], son[i]);
    for (int i = 0; i < 12; ++i) {
        const auto r = padKey(i); const char k = kPad[i]; const bool digit = k >= '0' && k <= '9'; const int n = k == '0' ? 9 : k - '1';
        const bool on = digit && n == e.current() && e.mode.empty();
        drawKey(g, r, {}, on);
        g.setColour(on ? col::led : juce::Colour(0xffeeeeee)); g.setFont(sans(26)); g.drawText(juce::String::charToString((juce::juce_wchar) k), r, juce::Justification::centred);
        if (digit && e.pats[(size_t) e.kit][(size_t) n].used()) { g.setColour(juce::Colour(0xffff5a3a)); g.fillEllipse(r.getRight() - 12, r.getY() + 6, 6, 6); }
    }
    // the white PLAY / STOP
    {   const auto k = playKey();
        g.setColour(juce::Colours::black); g.fillRoundedRectangle(k.translated(0, 5), 2);
        juce::ColourGradient wg(juce::Colours::white, 0, k.getY(), juce::Colour(0xffc4c4c4), 0, k.getBottom(), false); wg.addColour(.7, juce::Colour(0xffdcdcdc));
        g.setGradientFill(wg); g.fillRoundedRectangle(k, 2); g.setColour(juce::Colours::black); g.drawRoundedRectangle(k, 2, 1);
        if (e.running) { g.setColour(juce::Colours::white.withAlpha(.3f)); g.drawRoundedRectangle(k.expanded(5), 5, 5); }
        g.setColour(col::print); g.setFont(sans(12)); g.drawText("PLAY / STOP", juce::Rectangle<float>(k.getX() - 30, k.getBottom() + 10, k.getWidth() + 60, 16), juce::Justification::centred); }
    // function keys, the header printed over them, the badge
    {   g.setColour(col::dim); g.setFont(sans(10)); const juce::Rectangle<float> h(fnR.getX(), fnR.getY(), fnR.getWidth(), 14);
        g.drawText("CHECK  -  PLAY  -  RECORD", h, juce::Justification::centred);
        g.setColour(juce::Colour(0xff4a4d55)); g.fillRect(h.getX(), h.getCentreY(), 80.f, 1.f); g.fillRect(h.getRight() - 80, h.getCentreY(), 80.f, 1.f);
        const bool fon[8] = {e.mode == "swing", e.mode == "song" || (e.useSong[(size_t) e.kit] && !e.songList().empty()), edit, false, e.rec, false, e.mode == "copy", e.clearArm != 0};
        for (int i = 0; i < 8; ++i) drawKey(g, fnKey(i), kFnName[i], fon[i], i == 4);
        g.setColour(col::print); g.setFont(juce::Font(juce::FontOptions(22.f, juce::Font::bold | juce::Font::italic)));
        g.drawText("Omega", juce::Rectangle<float>(fnR.getRight() - 100, fnR.getBottom() - 21, 100, 22), juce::Justification::centredRight);
        g.setColour(juce::Colour(0xffbbbbbb)); g.fillEllipse(fnR.getRight() - 124, fnR.getBottom() - 20, 20, 20); g.setColour(juce::Colours::white); g.fillEllipse(fnR.getRight() - 119, fnR.getBottom() - 15, 7, 7); }
    // what the LED can't say, and HELP
    g.setColour(juce::Colour(0xffcfd3dc)); g.setFont(sans(12));
    g.drawFittedText(toastT > 0 ? toast.toUpperCase() : juce::String(e.msg()).toUpperCase(), juce::Rectangle<int>((int) infoR.getX(), (int) infoR.getBottom() - 74, (int) (playR.getCentreX() - 70 - infoR.getX()), 28), juce::Justification::bottomLeft, 2, .7f);
    {   const auto r = helpR(); g.setColour(juce::Colours::black); g.fillRoundedRectangle(r.translated(0, 2), 2);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff34353a), 0, r.getY(), juce::Colour(0xff1b1c20), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 2);
        g.setColour(col::print); g.setFont(sans(11)); g.drawText("HELP", r, juce::Justification::centred); }
    {   const auto r = circR(); g.setColour(juce::Colours::black); g.fillRoundedRectangle(r.translated(0, 2), 2);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff34353a), 0, r.getY(), juce::Colour(0xff1b1c20), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 2);
        g.setColour(e.circuitOn() ? col::led : col::print); g.setFont(sans(11)); g.drawText(e.circuitOn() ? "CIRCUIT: DMX" : "CIRCUIT: CLEAN", r, juce::Justification::centred); }
}

void DmxEditor::timerCallback() {
    if (!e.toast.empty()) { Locked l(e.lock); toast = e.toast; e.toast.clear(); toastT = 60; }
    if (toastT > 0) --toastT;
    const int f = e.flashVoice.exchange(-1); if (f >= 0) { flashV = f; flashT = 3; }
    if (flashT > 0) --flashT;
    const int t = e.takePendingTune(); if (t >= 0) e.setTune(e.sel, t);   // TUNE typed on the keypad: re-render that voice's card
    repaint();
}

// ---------------- mouse and keys ----------------
void DmxEditor::fnPress(const std::string& f) {
    if (f == "edit") { edit = !edit; return; }
    if (f == "acc") { e.acc = !e.acc; return; }
    if (f == "kit") {
        juce::PopupMenu m; for (int k = 0; k < kKits; ++k) m.addItem(k + 1, juce::String(kKitDefs[k].name) + " (" + juce::String(kKitDefs[k].voices) + " sounds)", true, k == e.kit);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) { if (r > 0) e.setKit(r - 1); });
        return;
    }
    e.fn(f);
}
void DmxEditor::mouseDown(const juce::MouseEvent& ev) {
    const auto p = toPanel(ev);
    for (int i = 0; i < kGroups; ++i) if (slot(i).contains(p)) { dragFader = i; mouseDrag(ev); return; }
    if (edit) for (int v = 0; v < e.voices(); ++v) {
        if (rowLabel(v).contains(p)) { e.sel = v; e.hit(v, kVel[e.acc ? 2 : 1]); return; }
        for (int st = 0; st < 16; ++st) if (cell(v, st).contains(p)) {
            const int want = e.acc ? 2 : 1; paintValue = e.stepValue(v, st) == want ? 0 : want; paintRow = v; e.sel = v; e.toggleStep(v, st, paintValue);
            if (paintValue && !e.running) e.hit(v, kVel[paintValue]);
            return;
        }
    }
    if (!edit) for (auto& [v, r] : voiceKeys()) if (r.withHeight(68).contains(p)) { e.sel = v; e.hit(v, kVel[e.acc ? 2 : 1]); return; }
    for (int i = 0; i < 4; ++i) if (smallKey(i).withHeight(58).contains(p)) { fnPress(kSmall[i]); return; }
    if (circR().contains(p)) { e.setCircuit(!e.circuitOn()); return; }
    for (int i = 0; i < 12; ++i) if (padKey(i).contains(p)) { e.keypad(kPad[i]); return; }
    for (int i = 0; i < 8; ++i) if (fnKey(i).withHeight(58).contains(p)) { fnPress(kFn[i]); return; }
    if (playKey().contains(p)) { e.internalPlay = !e.internalPlay.load(); return; }
    if (helpR().contains(p)) showHelp();
}
void DmxEditor::mouseDrag(const juce::MouseEvent& ev) {
    const auto p = toPanel(ev);
    if (dragFader >= 0) { const auto sl = slot(dragFader); proc.setFader(dragFader, 1 - (p.y - sl.getY() - 8) / (sl.getHeight() - 16)); return; }
    if (paintRow >= 0) for (int st = 0; st < 16; ++st) { const auto c = cell(paintRow, st); if (p.x >= c.getX() && p.x < c.getRight() && e.stepValue(paintRow, st) != paintValue) e.toggleStep(paintRow, st, paintValue); }
}
void DmxEditor::mouseUp(const juce::MouseEvent&) { dragFader = -1; paintRow = -1; }
void DmxEditor::mouseDoubleClick(const juce::MouseEvent& ev) {
    const auto p = toPanel(ev);
    if (edit) { for (int v = 0; v < e.voices(); ++v) if (rowLabel(v).contains(p)) { rename(v); return; } }
    else for (auto& [v, r] : voiceKeys()) if (r.withHeight(68).contains(p)) { rename(v); return; }
}
bool DmxEditor::keyPressed(const juce::KeyPress& k) {
    const auto c = k.getTextCharacter();
    if (c == ' ') { e.internalPlay = !e.internalPlay.load(); return true; }
    if (c >= '0' && c <= '9') { e.keypad((char) c); return true; }
    return false;
}

void DmxEditor::rename(int v) {
    dialog = std::make_unique<juce::AlertWindow>("Rename " + juce::String(kKitDefs[e.kit].v[v].name), "Empty for the original name.", juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor("name", e.voiceName(v)); dialog->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey)); dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const int kit = e.kit;
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, v, kit](int r) {
        if (r == 1 && dialog) { Locked l(e.lock); e.names[(size_t) kit][(size_t) v] = dialog->getTextEditorContents("name").trim().substring(0, 16).toStdString(); }
        dialog.reset(); }), false);
}
void DmxEditor::showHelp() {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Omega DMX",
        "Voice keys play the sounds, in columns under their faders; double-click one to rename it. With RECORD lit and the pattern running they record where you play (ACCENT lit: accented).\n"
        "EDIT shows the pattern's steps in place of the keys: click a light for a hit, again to clear; drag to paint. Click a name to hear and pick the voice.\n"
        "Keypad: 1-9 and 0 pick the ten patterns (a dot marks the ones with hits), < > step. A new pattern starts when the playing one ends.\n"
        "TEMPO then type it on the keypad, or < > (the host's tempo rules while it plays). SWING steps 50-71%.\n"
        "SONG then numbers add patterns, ERASE takes the last off; SONG again to leave. COPY then a number copies the pattern there.\n"
        "ERASE clears the picked voice, CLEAR the pattern (twice), FACTORY restores the factory pattern. KIT picks the kit.\n"
        "TUNE then 00-99 on the keypad, or < > for 5: the picked voice's pitch trimmer (50 = centre); higher is faster and shorter, as on the hardware.\n"
        "CIRCUIT: DMX plays each sound as a DMX voice card would (ROM of AM6070 codes at the card's 555 clock, decay on the DAC's reference, three Sallen-Key filters, the DMX's stereo mix and audio-taper faders); CLEAN plays them as recorded. The DMX kit starts clean: it was recorded from a DMX.\n"
        "Faders: the seven voice groups, the Metronome (down: no click) and the Volume.\n"
        "The sequencer follows the host's transport; PLAY / STOP runs it on its own (and in the standalone app).\n"
        "MIDI: General MIDI drum notes (36 kick, 38 snare, 42 hat...) and every voice from C3 (60) up; program change picks the pattern.",
        "OK", this);
}

}  // namespace dmx
