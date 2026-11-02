#include "SidEditor.h"

namespace sid {

namespace {
const juce::Colour beige(0xffcbbfad), beigeDark(0xffb3a691), beigeLine(0xff9c8f7b), bezel(0xff42484a), recess(0xff262624);
const juce::Colour keyBrown(0xff3f322b), keyBrownTop(0xff574639), keyTaupe(0xff7d7067), keyTaupeTop(0xff978a80);
const juce::Colour stripes[5] = { juce::Colour(0xffd8342b), juce::Colour(0xffef8a2c), juce::Colour(0xfff2d23a), juce::Colour(0xff4aa64a), juce::Colour(0xff2f74c0) };

// the keyboard well and the keys, in design coordinates
const juce::Rectangle<float> kWell(70, 668, 742, 196), fWell(842, 668, 92, 196);
constexpr int kWhite = 22;   // three octaves and a top C
bool isBlack(int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
juce::Rectangle<float> whiteRect(int i) { const float w = (kWell.getWidth() - 16) / kWhite; return { kWell.getX() + 8 + i * w, kWell.getY() + 8, w - 3, kWell.getHeight() - 16 }; }
juce::Rectangle<float> fkeyRect(int i) { return { fWell.getX() + 10, fWell.getY() + 8 + i * 46.5f, fWell.getWidth() - 20, 40 }; }
const char* fkeyName[4] = { "PLAY", "OCT-", "OCT+", "PANIC" };

void rainbow(juce::Graphics& g, juce::Rectangle<float> r) {
    const float h = r.getHeight() / 5;
    for (int i = 0; i < 5; ++i) { g.setColour(stripes[i]); g.fillRect(r.getX(), r.getY() + i * h, r.getWidth(), h * 0.72f); }
}
void keycap(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, juce::Colour top, bool pressed) {
    if (pressed) r = r.translated(0, 2);
    g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillRoundedRectangle(r.translated(0, pressed ? 1.f : 4.f), 5);
    g.setColour(base); g.fillRoundedRectangle(r, 5);
    auto face = r.reduced(r.getWidth() * 0.12f, 3).withTrimmedBottom(r.getHeight() * 0.10f);
    juce::ColourGradient gr(top.brighter(pressed ? 0.f : 0.12f), face.getX(), face.getY(), top.darker(0.15f), face.getX(), face.getBottom(), false);
    g.setGradientFill(gr); g.fillRoundedRectangle(face, 4);
}
}  // namespace

SidEditor::SidEditor(SidProcessor& p) : juce::AudioProcessorEditor(p), proc(p), ui(p) {
    px.resize((size_t) (ScreenUI::W * ScreenUI::H));
    setResizable(true, true);
    setResizeLimits(BW / 2, BH / 2, BW * 2, BH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) BW / BH);
    setSize(BW * 4 / 5, BH * 4 / 5);
    setWantsKeyboardFocus(true);
    ui.showPreset(proc.synth().program.load());
    startTimerHz(30);
}
SidEditor::~SidEditor() { for (int n = 0; n < 128; ++n) if (down[(size_t) n]) noteOff(n); }

void SidEditor::timerCallback() {
    if (proc.uiPresetVersion.load() != lastVersion) { lastVersion = proc.uiPresetVersion.load(); ui.showPreset(proc.synth().program.load()); }
    ui.render(px.data());
    juce::Image::BitmapData bd(screen, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < ScreenUI::H; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(bd.getLinePointer(y));
        std::memcpy(row, px.data() + y * ScreenUI::W, sizeof(uint32_t) * ScreenUI::W);
    }
    const float s = (float) getWidth() / BW;
    repaint(juce::Rectangle<float>(40, 30, 920, 600).transformedBy(juce::AffineTransform::scale(s)).getSmallestIntegerContainer());
}

void SidEditor::paint(juce::Graphics& g) {
    g.addTransform(juce::AffineTransform::scale((float) getWidth() / BW));
    // the room: a dark wall and a wooden desk
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2e2925), 0, 0, juce::Colour(0xff1b1714), 0, 700, false));
    g.fillRect(0, 0, BW, 708);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5a3d29), 0, 700, juce::Colour(0xff3a281b), 0, BH, false));
    g.fillRect(0, 700, BW, BH - 700);
    drawMonitor(g);
    drawKeyboard(g);
}

// the monitor fills the width: the screen is the instrument's main surface
static const juce::Rectangle<float> tubeRect(52, 44, 896, 576);
void SidEditor::drawMonitor(juce::Graphics& g) {
    const juce::Rectangle<float> body(18, 10, 964, 690);
    g.setColour(juce::Colours::black.withAlpha(0.4f)); g.fillRoundedRectangle(body.translated(6, 10), 18);
    g.setGradientFill(juce::ColourGradient(beige.brighter(0.08f), 0, body.getY(), beigeDark, 0, body.getBottom(), false));
    g.fillRoundedRectangle(body, 18);
    g.setColour(beigeLine); g.drawRoundedRectangle(body.reduced(6), 14, 1.5f);
    g.drawLine(body.getX() + 8, 646, body.getRight() - 8, 646, 1.5f);
    // bezel, recess and tube
    g.setColour(bezel); g.fillRoundedRectangle(34, 26, 932, 612, 12);
    g.setColour(recess); g.fillRoundedRectangle(44, 36, 912, 592, 9);
    juce::Path tube; tube.addRoundedRectangle(tubeRect, 40.f);
    g.setColour(juce::Colour(PALETTE[LBLUE])); g.fillPath(tube);   // the C64's light-blue border
    g.saveState();
    g.reduceClipRegion(tube);
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImage(screen, juce::Rectangle<float>(SX, SY, ScreenUI::W * SS, ScreenUI::H * SS), juce::RectanglePlacement::stretchToFit);
    // scanlines, darker edges, the glass
    g.setColour(juce::Colours::black.withAlpha(0.12f));
    for (float y = tubeRect.getY(); y < tubeRect.getBottom(); y += SS) g.fillRect(tubeRect.getX(), y + SS * 0.6f, tubeRect.getWidth(), SS * 0.4f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::transparentBlack, 500, 332, juce::Colours::black.withAlpha(0.38f), 52, 44, true));
    g.fillPath(tube);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.08f), 120, 60, juce::Colours::transparentWhite, 420, 330, false));
    g.fillEllipse(40, 30, 600, 320);
    g.restoreState();
    // the badge strip: name plate with the rainbow, the model label, the power light
    g.setColour(juce::Colour(0xff3b3d3a)); g.fillRoundedRectangle(382, 656, 236, 34, 3);
    g.setColour(juce::Colours::white.withAlpha(0.92f)); g.setFont(juce::FontOptions(18.f, juce::Font::bold));
    g.drawText("omega", 392, 656, 110, 34, juce::Justification::centredLeft);
    rainbow(g, { 500, 662, 76, 22 });
    g.setColour(juce::Colour(0xff2a2a28)); g.fillRect(582.f, 660.f, 28.f, 26.f);
    g.setColour(juce::Colour(0xff8ee05a)); g.fillRect(589.f, 667.f, 14.f, 4.f);
    g.setColour(juce::Colour(0xff6e6458)); g.drawRect(760.f, 660.f, 130.f, 26.f, 1.5f);
    g.setFont(juce::FontOptions(10.f, juce::Font::bold));
    g.drawText("VIDEO MONITOR", 766, 661, 120, 12, juce::Justification::centredLeft);
    g.drawText("MODEL 6581", 766, 672, 120, 12, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff4c4640)); g.fillRect(912.f, 660.f, 26.f, 26.f);
}

void SidEditor::drawKeyboard(juce::Graphics& g) {
    juce::Graphics::ScopedSaveState keep(g);
    g.addTransform(juce::AffineTransform::translation(0, KDY));
    // the breadbin
    juce::Path bin;
    bin.startNewSubPath(60, 596); bin.lineTo(940, 596); bin.quadraticTo(968, 600, 972, 640);
    bin.lineTo(980, 868); bin.quadraticTo(980, 892, 952, 892); bin.lineTo(48, 892); bin.quadraticTo(20, 892, 20, 868);
    bin.lineTo(28, 640); bin.quadraticTo(32, 600, 60, 596); bin.closeSubPath();
    g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillPath(bin, juce::AffineTransform::translation(4, 8));
    g.setGradientFill(juce::ColourGradient(beige.brighter(0.05f), 0, 596, beigeDark.darker(0.05f), 0, 892, false));
    g.fillPath(bin);
    // the ridges along the back
    g.setColour(beigeLine);
    for (int i = 0; i < 5; ++i) g.drawLine(440.f, 606.f + i * 9.f, 950.f, 606.f + i * 9.f, 1.6f);
    g.setColour(beige.brighter(0.2f));
    for (int i = 0; i < 5; ++i) g.drawLine(440.f, 608.f + i * 9.f, 950.f, 608.f + i * 9.f, 1.f);
    // badge
    g.setColour(juce::Colour(0xff8e8576)); g.fillRoundedRectangle(70, 610, 300, 34, 17);
    g.setColour(juce::Colour(0xff595249)); g.fillRoundedRectangle(74, 613, 292, 28, 14);
    g.setColour(beige.brighter(0.15f)); g.setFont(juce::FontOptions(17.f, juce::Font::bold));
    g.drawText("omega", 92, 613, 100, 28, juce::Justification::centredLeft);
    rainbow(g, { 190, 617, 90, 20 });
    g.setColour(beige.brighter(0.15f));
    g.drawText("SID 64", 284, 613, 80, 28, juce::Justification::centredLeft);
    // power light
    g.setColour(juce::Colour(0xff595249)); g.fillRoundedRectangle(820, 640, 100, 18, 9);
    g.setColour(beige.brighter(0.15f)); g.setFont(juce::FontOptions(10.f, juce::Font::bold)); g.drawText("POWER", 830, 640, 50, 18, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffe0241a)); g.fillEllipse(893, 643, 12, 12);
    // key wells
    g.setColour(beigeLine.darker(0.3f)); g.fillRoundedRectangle(kWell.expanded(3), 6); g.fillRoundedRectangle(fWell.expanded(3), 6);
    g.setColour(juce::Colour(0xff2c2622)); g.fillRoundedRectangle(kWell, 5); g.fillRoundedRectangle(fWell, 5);
    // white keys (brown caps), then the raised sharps
    int wi = 0;
    for (int n = base; wi < kWhite; ++n) {
        if (isBlack(n)) continue;
        const auto r = whiteRect(wi++);
        keycap(g, r, keyBrown, keyBrownTop, down[(size_t) n]);
        if (n % 12 == 0) { g.setColour(juce::Colours::white.withAlpha(0.8f)); g.setFont(juce::FontOptions(11.f, juce::Font::bold)); g.drawText("C" + juce::String(n / 12 - 1), r.withTrimmedTop(r.getHeight() - 24), juce::Justification::centred); }
    }
    wi = 0;
    for (int n = base; wi < kWhite; ++n) {
        if (!isBlack(n)) { ++wi; continue; }
        const auto r0 = whiteRect(wi - 1);
        juce::Rectangle<float> r(r0.getRight() - r0.getWidth() * 0.3f, r0.getY() - 2, r0.getWidth() * 0.62f, r0.getHeight() * 0.58f);
        keycap(g, r, keyTaupe.darker(0.2f), keyTaupeTop, down[(size_t) n]);
    }
    // function keys
    for (int i = 0; i < 4; ++i) {
        const auto r = fkeyRect(i);
        keycap(g, r, keyTaupe, keyTaupeTop, fkeyDown == i);
        g.setColour(juce::Colours::white.withAlpha(0.85f)); g.setFont(juce::FontOptions(11.f, juce::Font::bold));
        const bool playing = i == 0 && proc.synth().seqRunning();
        g.drawText("f" + juce::String(i * 2 + 1), r.withTrimmedBottom(18).translated(0, 2), juce::Justification::centred);
        g.setFont(juce::FontOptions(9.f, juce::Font::bold));
        g.drawText(playing ? "STOP" : fkeyName[i], r.withTrimmedTop(20), juce::Justification::centred);
    }
    g.setColour(juce::Colour(0xff6e6458)); g.setFont(juce::FontOptions(10.f));
    g.drawText("KEYS: A W S E D F T G Y H U J K  -  Z / X OCTAVE", 70, 870, 742, 16, juce::Justification::centredLeft);
}

juce::Point<float> SidEditor::toScreen(juce::Point<float> d) const { return { (d.x - SX) / SS, (d.y - SY) / SS }; }

int SidEditor::noteAt(juce::Point<float> d) const {
    d.y -= KDY;
    if (!kWell.contains(d)) return -1;
    // sharps first (they sit on top)
    int wi = 0;
    for (int n = base; wi < kWhite; ++n) {
        if (!isBlack(n)) { ++wi; continue; }
        const auto r0 = whiteRect(wi - 1);
        const juce::Rectangle<float> r(r0.getRight() - r0.getWidth() * 0.3f, r0.getY() - 2, r0.getWidth() * 0.62f, r0.getHeight() * 0.58f);
        if (r.contains(d)) return n;
    }
    wi = 0;
    for (int n = base; wi < kWhite; ++n) {
        if (isBlack(n)) continue;
        if (whiteRect(wi++).expanded(1.5f, 0).contains(d)) return n;
    }
    return -1;
}
int SidEditor::fkeyAt(juce::Point<float> d) const { d.y -= KDY; for (int i = 0; i < 4; ++i) if (fkeyRect(i).contains(d)) return i; return -1; }

void SidEditor::fkey(int k) {
    Synth& s = proc.synth();
    switch (k) {
        case 0: s.post(s.seqPlay.load() ? Synth::EV_SEQ_STOP : Synth::EV_SEQ_PLAY, 0, 0); break;
        case 1: base = std::max(12, base - 12); break;
        case 2: base = std::min(84, base + 12); break;
        default: s.post(Synth::EV_PANIC, 0, 0); break;
    }
    repaint();
}

void SidEditor::mouseDown(const juce::MouseEvent& e) {
    const auto d = toDesign(e.position);
    const int src = e.source.getIndex();
    if (tubeRect.contains(d)) {
        const auto sp = toScreen(d); ui.down(sp.x, sp.y); sourceOnScreen[src] = true; return;
    }
    const int f = fkeyAt(d);
    if (f >= 0) { fkeyDown = f; fkey(f); return; }
    const int n = noteAt(d);
    if (n >= 0) { sourceNote[src] = n; down[(size_t) n] = true; noteOn(n); repaint(); }
}
void SidEditor::mouseDrag(const juce::MouseEvent& e) {
    const auto d = toDesign(e.position);
    const int src = e.source.getIndex();
    if (sourceOnScreen[src]) { const auto sp = toScreen(d); ui.move(sp.x, sp.y); return; }
    auto it = sourceNote.find(src);
    if (it == sourceNote.end()) return;
    const int n = noteAt(d);
    if (n != it->second) {   // glide across the keys
        if (it->second >= 0) { down[(size_t) it->second] = false; noteOff(it->second); }
        if (n >= 0) { down[(size_t) n] = true; noteOn(n); }
        it->second = n; repaint();
    }
}
void SidEditor::mouseUp(const juce::MouseEvent& e) {
    const auto d = toDesign(e.position);
    const int src = e.source.getIndex();
    if (sourceOnScreen[src]) { const auto sp = toScreen(d); ui.up(sp.x, sp.y); sourceOnScreen[src] = false; return; }
    if (fkeyDown >= 0) { fkeyDown = -1; repaint(); }
    auto it = sourceNote.find(src);
    if (it != sourceNote.end()) { if (it->second >= 0) { down[(size_t) it->second] = false; noteOff(it->second); } sourceNote.erase(it); repaint(); }
}
void SidEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const auto d = toDesign(e.position);
    if (!tubeRect.contains(d)) return;
    const auto sp = toScreen(d);
    ui.wheel(sp.x, sp.y, w.deltaY > 0 ? 1.f : w.deltaY < 0 ? -1.f : 0.f);
}

// the computer keyboard plays notes like a tracker: A W S E D F T G Y H U J K = C to C
static int qwertyOffset(int key) {
    static const char* row = "AWSEDFTGYHUJKOLP";
    for (int i = 0; row[i]; ++i) if (key == row[i] || key == row[i] + 32) return i;
    return -1;
}
bool SidEditor::keyPressed(const juce::KeyPress& k) {
    const int c = k.getKeyCode();
    if (c == 'Z' || c == 'z') { base = std::max(12, base - 12); repaint(); return true; }
    if (c == 'X' || c == 'x') { base = std::min(84, base + 12); repaint(); return true; }
    if (c == juce::KeyPress::spaceKey) { fkey(0); return true; }
    const int off = qwertyOffset(c);
    if (off >= 0 && !qwertyHeld.count(c)) { const int n = base + 12 + off; qwertyHeld[c] = n; down[(size_t) n] = true; noteOn(n); repaint(); return true; }
    return off >= 0;
}
bool SidEditor::keyStateChanged(bool) {
    bool any = false;
    for (auto it = qwertyHeld.begin(); it != qwertyHeld.end();) {
        if (!juce::KeyPress::isKeyCurrentlyDown(it->first) && !juce::KeyPress::isKeyCurrentlyDown(it->first + 32)) {
            down[(size_t) it->second] = false; noteOff(it->second); it = qwertyHeld.erase(it); any = true;
        } else ++it;
    }
    if (any) repaint();
    return any;
}

}  // namespace sid
