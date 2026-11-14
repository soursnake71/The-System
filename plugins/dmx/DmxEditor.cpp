#include "DmxEditor.h"

namespace dmx {

namespace col {
const juce::Colour face1 {0xff17181c}, face2 {0xff0a0a0c}, stripe {0x9e4070e0}, print {0xffe9ebf0}, dim {0xff9aa0ad}, led {0xffff2a1a}, ledBg {0xff1a0303},
    key1 {0xff46474d}, key2 {0xff202125}, key3 {0xff16171a}, red1 {0xffd8261c}, wood1 {0xff3a1c0c}, wood2 {0xff7a4424}, wood3 {0xff2e1408};
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

juce::Rectangle<float> DmxEditor::slot(int g) const {
    const float cw = (fadersR.getWidth() - 16 - 7 * 6) / 8.f, x = fadersR.getX() + 8 + g * (cw + 6);
    return {x + cw / 2 - 23, fadersR.getY() + 10, 46, fadersR.getHeight() - 10 - 30};
}
juce::Rectangle<float> DmxEditor::rowLabel(int v) const {
    const auto in = gridR.reduced(11, 9); const float top = in.getY() + 16, rh = (in.getHeight() - 16) / (float) e.voices();
    return {in.getX(), top + v * rh, 118, rh};
}
juce::Rectangle<float> DmxEditor::cell(int v, int s) const {
    const auto lab = rowLabel(v); const auto in = gridR.reduced(11, 9);
    const float cw = (in.getRight() - lab.getRight()) / 16.f; return {lab.getRight() + s * cw, lab.getY(), cw, lab.getHeight()};
}
juce::Rectangle<float> DmxEditor::voiceKey(int i) const {
    const float cw = (keysR.getWidth() - 10 * 9) / 11.f; const int r = i / 11, c = i % 11;
    return {keysR.getX() + c * (cw + 9), keysR.getY() + 23 + r * 80.f, cw, 44};
}
juce::Rectangle<float> DmxEditor::padKey(int i) const {
    const auto in = padR.reduced(12); const float cw = (in.getWidth() - 20) / 3.f, rh = (in.getHeight() - 30) / 4.f;
    return {in.getX() + (i % 3) * (cw + 10), in.getY() + (i / 3) * (rh + 10), cw, rh};
}
juce::Rectangle<float> DmxEditor::fnKey(int i) const {
    const float x0 = ctlR.getX() + 108, cw = (ctlR.getRight() - x0 - 24) / 4.f;
    return {x0 + (i % 4) * (cw + 8), ctlR.getY() + 22 + (i / 4) * 66.f, cw, 36};
}

void DmxEditor::drawKey(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, bool on, bool red, bool hitNow) {
    auto k = r.translated(0, hitNow ? 3.f : 0.f);
    g.setColour(juce::Colours::black); g.fillRoundedRectangle(r.translated(0, 4), 3);
    juce::ColourGradient cg(red ? (on ? col::red1 : juce::Colour(0xff5a1410)) : (on ? juce::Colour(0xff66676e) : col::key1), k.getX(), k.getY(),
                            red ? (on ? juce::Colour(0xff9a120c) : juce::Colour(0xff320806)) : col::key3, k.getX(), k.getBottom(), false);
    g.setGradientFill(cg); g.fillRoundedRectangle(k, 3);
    g.setColour(on ? juce::Colour(0xffff3a2a) : juce::Colours::black); g.drawRoundedRectangle(k, 3, on ? 1.5f : 1.f);
    if (label.isNotEmpty()) { g.setColour(on ? juce::Colour(0xffff5a4a) : col::print); g.setFont(sans(11)); g.drawFittedText(label.toUpperCase(), juce::Rectangle<int>((int) r.getX() - 4, (int) r.getBottom() + 5, (int) r.getWidth() + 8, 14), juce::Justification::centred, 1, .7f); }
}

void DmxEditor::paint(juce::Graphics& g) {
    const float s = (float) getWidth() / W;
    g.addTransform(juce::AffineTransform::scale(s));
    // face: black with blue pinstripes, wood ends
    g.setGradientFill(juce::ColourGradient(col::face1, 0, 0, col::face2, 0, H, false)); g.fillRect(0, 0, W, H);
    g.setColour(col::stripe); for (int y = 11; y < H; y += 12) g.fillRect(30, y, W - 60, 1);
    for (int side = 0; side < 2; ++side) { const float x = side ? W - 30.f : 0.f; g.setGradientFill(juce::ColourGradient(col::wood1, x, 0, col::wood3, x + 30, 0, false)); g.fillRect(x, 0.f, 30.f, (float) H);
        g.setColour(col::wood2.withAlpha(.6f)); g.fillRect(x + 9, 0.f, 6.f, (float) H); }
    // logo
    {   juce::Path p; juce::GlyphArrangement ga; ga.addLineOfText(juce::Font(juce::FontOptions(76.f, juce::Font::bold | juce::Font::italic)), "DMX", logoR.getX(), logoR.getY() + 95);
        ga.createPath(p); p.applyTransform(juce::AffineTransform::scale(1.18f, 1.f, logoR.getX(), 0).sheared(-.1f, 0).translated(10, 0));
        g.setColour(juce::Colour(0xff0b0b0d)); g.fillPath(p); g.setColour(juce::Colour(0xffe8e8ea)); g.strokePath(p, juce::PathStrokeType(2.4f));
        g.setColour(col::dim); g.setFont(sans(11)); g.drawText("OMEGA DIGITAL DRUMS", logoR.withTrimmedTop(112).withHeight(16), juce::Justification::left); }
    // faders
    g.setColour(juce::Colours::black.withAlpha(.6f)); g.fillRoundedRectangle(fadersR, 3);
    for (int i = 0; i < kGroups; ++i) {
        const auto sl = slot(i); g.setColour(juce::Colours::black); g.fillRoundedRectangle(sl.getCentreX() - 3, sl.getY() + 4, 6, sl.getHeight() - 8, 2);
        g.setColour(juce::Colour(0xff444444)); g.drawRoundedRectangle(sl.getCentreX() - 3, sl.getY() + 4, 6, sl.getHeight() - 8, 2, 1);
        const float v = e.fader[i], y = sl.getY() + 8 + (sl.getHeight() - 16) * (1 - v);
        juce::Rectangle<float> cap(sl.getCentreX() - 21, y - 12, 42, 24);
        g.setColour(juce::Colours::black.withAlpha(.6f)); g.fillRoundedRectangle(cap.translated(0, 3), 3);
        juce::ColourGradient cg(juce::Colour(0xfff2f2f2), 0, cap.getY(), juce::Colour(0xffc8c8cc), 0, cap.getBottom(), false); cg.addColour(.44, juce::Colour(0xffa4a4a8)); cg.addColour(.5, juce::Colour(0xff4e4e52));
        g.setGradientFill(cg); g.fillRoundedRectangle(cap, 3); g.setColour(juce::Colour(0xff111111)); g.drawRoundedRectangle(cap, 3, 1);
        g.setColour(col::print); g.setFont(sans(12)); g.drawText(kGroupNames[i], juce::Rectangle<float>(sl.getCentreX() - 40, fadersR.getBottom() - 26, 80, 18), juce::Justification::centred);
    }
    // LED display, message, mini keys
    {   juce::Rectangle<float> d(ledR.getRight() - 30 - 236, ledR.getY() + 10, 236, 56);
        g.setColour(col::ledBg); g.fillRoundedRectangle(d, 3); g.setColour(juce::Colours::black); g.drawRoundedRectangle(d, 3, 3);
        g.setColour(col::led); g.setFont(mono(42)); g.drawText(juce::String(e.led()), d.reduced(10, 0), juce::Justification::centredRight);
        g.setColour(juce::Colour(0xffb3140e)); g.fillRoundedRectangle(ledR.getRight() - 16, d.getCentreY() - 12, 14, 24, 2);
        g.setColour(juce::Colour(0xffcfd3dc)); g.setFont(sans(12));
        g.drawFittedText(toastT > 0 ? toast.toUpperCase() : juce::String(e.msg()).toUpperCase(), juce::Rectangle<int>((int) ledR.getX(), (int) d.getBottom() + 10, (int) ledR.getWidth(), 18), juce::Justification::centredRight, 1, .6f);
        const juce::String mn[3] = {juce::String("KIT: ") + kKitDefs[e.kit].name, "CLICK", "HELP"};
        for (int i = 0; i < 3; ++i) { auto r = mini(i); g.setColour(juce::Colours::black); g.fillRoundedRectangle(r.translated(0, 2), 2);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3c3d42), 0, r.getY(), juce::Colour(0xff1d1e22), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 2);
            g.setColour(i == 1 && e.click ? col::led : col::print); g.setFont(sans(11)); g.drawFittedText(mn[i], r.toNearestInt(), juce::Justification::centred, 1, .7f); }
    }
    // step grid: red LEDs
    g.setColour(juce::Colour(0xff100303)); g.fillRoundedRectangle(gridR, 3); g.setColour(juce::Colours::black); g.drawRoundedRectangle(gridR, 3, 3);
    const int now = e.running ? e.nowStep.load() : -1;
    for (int st = 0; st < 16; ++st) { const auto c = cell(0, st); g.setColour(st == now ? col::led : juce::Colour(0xff7a2a24)); g.setFont(mono(9)); g.drawText(juce::String(st + 1), juce::Rectangle<float>(c.getX(), gridR.getY() + 8, c.getWidth(), 14), juce::Justification::centred); }
    for (int v = 0; v < e.voices(); ++v) {
        const auto lab = rowLabel(v);
        if (v == e.sel) { g.setColour(juce::Colour(0x22ff321e)); g.fillRect(lab.withRight(gridR.getRight() - 11)); }
        g.setColour(juce::Colour(0xffff6a5a)); g.setFont(mono(12)); g.drawFittedText(juce::String(e.voiceName(v)).toUpperCase(), lab.reduced(3, 0).toNearestInt(), juce::Justification::centredLeft, 1, .8f);
        for (int st = 0; st < 16; ++st) {
            const auto c = cell(v, st); const float d = juce::jmin(16.f, c.getHeight() * .78f); juce::Rectangle<float> o(c.getCentreX() - d / 2, c.getCentreY() - d / 2, d, d);
            const int val = e.stepValue(v, st);
            if (val) { g.setColour(val == 2 ? juce::Colour(0xffff6a4a) : juce::Colour(0xffc8180e)); g.fillEllipse(o);
                g.setColour(juce::Colour(val == 2 ? 0x66ff4a2a : 0x44ff2a1a)); g.drawEllipse(o.expanded(2), 3); }
            g.setColour(val == 2 ? juce::Colour(0xffffb0a0) : st % 4 == 0 ? juce::Colour(0xff7a2a24) : juce::Colour(0xff3a1210)); g.drawEllipse(o, 1.5f);
            if (st == now) { g.setColour(juce::Colours::white.withAlpha(.55f)); g.drawEllipse(o.expanded(2.5f), 1.5f); }
        }
    }
    // keypad
    g.setColour(juce::Colours::black.withAlpha(.6f)); g.fillRoundedRectangle(padR, 3);
    for (int i = 0; i < 12; ++i) {
        const auto r = padKey(i); const char k = kPad[i]; const bool digit = k >= '0' && k <= '9'; const int n = k == '0' ? 9 : k - '1';
        const bool on = digit && n == e.current() && e.mode.empty();
        drawKey(g, r, {}, on);
        g.setColour(on ? col::led : col::print); g.setFont(mono(28)); g.drawText(juce::String::charToString((juce::juce_wchar) k), r, juce::Justification::centred);
        if (digit && e.pats[(size_t) e.kit][(size_t) n].used()) { g.setColour(juce::Colour(0xffff5a3a)); g.fillEllipse(r.getRight() - 12, r.getY() + 6, 6, 6); }
    }
    // voice keys
    for (int i = 0; i < e.voices(); ++i) drawKey(g, voiceKey(i), e.voiceName(i), i == e.sel, false, i == flashV && flashT > 0);
    drawKey(g, voiceKey(e.voices()), "ACCENT", e.acc, true);
    // play / stop and the function keys
    {   const auto r = playR(); juce::Rectangle<float> k(r.getX() + 6, r.getY(), 84, 76);
        g.setColour(juce::Colours::black); g.fillRoundedRectangle(k.translated(0, 5), 3);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white, 0, k.getY(), juce::Colour(0xffd4d4d4), 0, k.getBottom(), false)); g.fillRoundedRectangle(k, 3);
        if (e.internalPlay) { g.setColour(juce::Colours::white.withAlpha(.35f)); g.drawRoundedRectangle(k.expanded(4), 5, 4); }
        g.setColour(col::print); g.setFont(sans(12)); g.drawText("PLAY / STOP", juce::Rectangle<float>(r.getX() - 10, k.getBottom() + 10, 116, 16), juce::Justification::centred); }
    const bool fon[8] = {e.mode == "swing", e.mode == "song" || (e.useSong[(size_t) e.kit] && !e.songList().empty()), e.mode == "copy", e.mode == "tempo", e.rec, false, e.clearArm != 0, false};
    for (int i = 0; i < 8; ++i) drawKey(g, fnKey(i), kFnName[i], fon[i], i == 4);
    g.setColour(col::print); g.setFont(juce::Font(juce::FontOptions(22.f, juce::Font::bold | juce::Font::italic)));
    g.drawText("Omega", juce::Rectangle<float>(ctlR.getRight() - 120, ctlR.getBottom() - 30, 120, 26), juce::Justification::centredRight);
}

void DmxEditor::timerCallback() {
    if (!e.toast.empty()) { Locked l(e.lock); toast = e.toast; e.toast.clear(); toastT = 60; }
    if (toastT > 0) --toastT;
    const int f = e.flashVoice.exchange(-1); if (f >= 0) { flashV = f; flashT = 3; }
    if (flashT > 0) --flashT;
    repaint();
}

void DmxEditor::mouseDown(const juce::MouseEvent& ev) {
    const auto p = toPanel(ev);
    for (int i = 0; i < kGroups; ++i) if (slot(i).expanded(10, 0).contains(p)) { dragFader = i; mouseDrag(ev); return; }
    for (int v = 0; v < e.voices(); ++v) {
        if (rowLabel(v).contains(p)) { e.sel = v; return; }
        for (int st = 0; st < 16; ++st) if (cell(v, st).contains(p)) {
            const int want = e.acc ? 2 : 1; paintValue = e.stepValue(v, st) == want ? 0 : want; paintRow = v; e.sel = v; e.toggleStep(v, st, paintValue);
            if (paintValue && !e.running) e.hit(v, kVel[paintValue]);
            return;
        }
    }
    for (int i = 0; i < e.voices(); ++i) if (voiceKey(i).withHeight(62).contains(p)) { e.sel = i; e.hit(i, kVel[e.acc ? 2 : 1]); return; }
    if (voiceKey(e.voices()).withHeight(62).contains(p)) { e.acc = !e.acc; return; }
    for (int i = 0; i < 12; ++i) if (padKey(i).contains(p)) { e.keypad(kPad[i]); return; }
    for (int i = 0; i < 8; ++i) if (fnKey(i).withHeight(54).contains(p)) { e.fn(kFn[i]); return; }
    if (playR().contains(p)) { e.internalPlay = !e.internalPlay.load(); return; }
    if (mini(0).contains(p)) {
        juce::PopupMenu m; for (int k = 0; k < kKits; ++k) m.addItem(k + 1, juce::String(kKitDefs[k].name) + " (" + juce::String(kKitDefs[k].voices) + " sounds)", true, k == e.kit);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) { if (r > 0) e.setKit(r - 1); });
        return;
    }
    if (mini(1).contains(p)) { e.click = !e.click; return; }
    if (mini(2).contains(p)) showHelp();
}

void DmxEditor::mouseDrag(const juce::MouseEvent& ev) {
    const auto p = toPanel(ev);
    if (dragFader >= 0) { const auto sl = slot(dragFader); proc.setFader(dragFader, 1 - (p.y - sl.getY() - 8) / (sl.getHeight() - 16)); return; }
    if (paintRow >= 0) for (int st = 0; st < 16; ++st) if (cell(paintRow, st).withY(rowLabel(paintRow).getY() - 200).withHeight(400).contains(p) && cell(paintRow, st).getX() <= p.x && p.x < cell(paintRow, st).getRight())
        if (e.stepValue(paintRow, st) != paintValue) e.toggleStep(paintRow, st, paintValue);
}
void DmxEditor::mouseUp(const juce::MouseEvent&) { dragFader = -1; paintRow = -1; }
void DmxEditor::mouseDoubleClick(const juce::MouseEvent& ev) {
    const auto p = toPanel(ev);
    for (int v = 0; v < e.voices(); ++v) if (rowLabel(v).contains(p)) { rename(v); return; }
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
        "Grid: click a light for a hit (ACCENT lit: an accent), again to clear; drag to paint. Double-click a name to rename it.\n"
        "Voice keys play the sounds; with RECORD lit and the pattern running they record where you play.\n"
        "Keypad: 1-9 and 0 pick the ten patterns (a dot marks the ones with hits), < > step. A new pattern starts when the playing one ends.\n"
        "TEMPO then type it on the keypad, or < > (the host's tempo rules while it plays). SWING steps 50-71%.\n"
        "SONG then numbers add patterns, ERASE takes the last off; SONG again to leave. COPY then a number copies the pattern there.\n"
        "ERASE clears the picked voice, CLEAR the pattern (twice), FACTORY restores the factory pattern.\n"
        "The sequencer follows the host's transport; PLAY / STOP runs it on its own (and in the standalone app).\n"
        "MIDI: General MIDI drum notes (36 kick, 38 snare, 42 hat...) and every voice from C3 (60) up; program change picks the pattern.",
        "OK", this);
}

}  // namespace dmx
