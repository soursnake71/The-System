#include "SevenEditor.h"
#include <set>

namespace seven {

namespace col {
const juce::Colour caseTop(0xff1c1c1e), caseBot(0xff0b0b0c), print(0xffe9ecec), dim(0xff9aa0a2), tan(0xffd7b183), teal(0xff5fd6c7), teal2(0xff3fb3a5), tealHi(0xffa6f3e8),
    red(0xffe34d55), purple(0xff8e86e6), blue(0xff7d97ee), brown(0xffd39a62), lcd(0xffc8cf86), lcd2(0xffb4bd70), ink(0xff1d2410), led(0xffff3b30);
}
static juce::Font sans(float size, bool bold = true) { return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)); }
static juce::Font monoFont(float size) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold)); }
static bool isBlack(int n) { const int k = n % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
static const juce::Rectangle<float> KEYS(282, 432, 1548, 352), CHART(930, 30, 640, 160), EGR(1580, 30, 120, 160), OLR(1710, 30, 112, 160),
    VOL(189, 252, 12, 122), DATA(300, 252, 12, 122), PW(112, 500, 40, 230), MW(196, 500, 40, 230), LCDR(884, 278, 306, 74), LEDR(800, 286, 74, 56), CART(680, 74, 200, 46);
static const int KLO = 36, KHI = 96;

SevenEditor::SevenEditor(SevenProcessor& p) : juce::AudioProcessorEditor(p), proc(p) {
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio(W / H);
    setResizeLimits(940, 400, 3760, 1600);
    setSize(1316, 560);
    setWantsKeyboardFocus(true);
    volPos = proc.func()[F::volume] / 99.0f;
    juce::Component::SafePointer<SevenEditor> self(this);
    proc.toast = [self](const juce::String& msg) {
        const juce::String text(msg);
        juce::MessageManager::callAsync([self, text]() { if (auto* e = self.getComponent()) e->say(text); });
    };
    buildHits();
    startTimerHz(30);
}
SevenEditor::~SevenEditor() { proc.toast = nullptr; stopTimer(); }

void SevenEditor::buildHits() {
    hits.clear();
    auto add = [this](const juce::String& id, juce::Rectangle<float> r, std::function<void()> fn, bool rep = false) { hits.push_back({r, std::move(fn), rep, id}); };
    add("no", {354, 336, 52, 26}, [this] { withPanel([](Panel& p) { p.no(); }); }, true);
    add("yes", {416, 336, 52, 26}, [this] { withPanel([](Panel& p) { p.yes(); }); }, true);
    add("store", {496, 262, 56, 26}, [this] { withPanel([](Panel& p) { p.storeBtn(); }); });
    add("protI", {570, 262, 56, 26}, [this] { withPanel([](Panel& p) { p.protectBtn(0); }); });
    add("protC", {642, 262, 56, 26}, [this] { withPanel([](Panel& p) { p.protectBtn(1); }); });
    add("opSel", {716, 262, 56, 26}, [this] { withPanel([](Panel& p) { p.opSelect(); }); });
    add("edit", {496, 334, 56, 26}, [this] { withPanel([](Panel& p) { p.editBtn(); }); });
    add("memI", {570, 334, 56, 26}, [this] { withPanel([](Panel& p) { p.memSelect(false); }); });
    add("memC", {642, 334, 56, 26}, [this] { withPanel([](Panel& p) { p.memSelect(true); }); });
    add("func", {716, 334, 56, 26}, [this] { withPanel([](Panel& p) { p.funcBtn(); }); });
    for (int i = 1; i <= 32; ++i) {
        const int r = i <= 16 ? 0 : 1, c = (i - 1) % 16;
        add("b" + juce::String(i), {1226.0f + c * 37, r ? 334.0f : 262.0f, 32, 26}, [this, i] { withPanel([i](Panel& p) { p.button(i); }); });
    }
    const char* tools[] = {"MEMORY", "SEQ / ARP", "MIDI FILES", "FX", "RANDOM", "ALL", "RESET", "HELP", "PANIC"};
    float x = 104;
    for (int i = 0; i < 9; ++i) {
        const float w = i == 5 ? 52.0f : 18 + 8.2f * (float) std::strlen(tools[i]) + (i == 4 || i == 6 ? 18.0f : 0.0f);
        add("tool" + juce::String(i), {x, 170, w, 26}, [this, i] {
            if (i == 0) memoryMenu();
            else if (i == 1) { showSeq = !showSeq; showHelp = false; }
            else if (i == 2) midiMenu();
            else if (i == 3) fxMenu();
            else if (i == 4) withPanel([](Panel& p) { p.random(); });
            else if (i == 5) withPanel([](Panel& p) { p.nextDicePart(); });
            else if (i == 6) withPanel([](Panel& p) { p.resetVoice(); });
            else if (i == 7) { showHelp = !showHelp; showSeq = false; }
            else { proc.panic(); lit.clear(); say("All notes off"); }
        });
        x += w + 8;
    }
    add("cart", CART, [this] { memoryMenu(); });
}

/* ---------------- painting ---------------- */
void SevenEditor::drawLabel(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size, juce::Colour c, bool bold, juce::Justification j) {
    g.setColour(c); g.setFont(sans(size, bold)); g.drawFittedText(t, r.toNearestInt(), j, 3, 0.7f);
}
void SevenEditor::drawButton(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool on, bool down, const juce::String& face) {
    if (down) r = r.translated(0, 1);
    g.setColour(juce::Colours::black.withAlpha(0.8f)); g.fillRoundedRectangle(r.translated(0, down ? 1.0f : 3.0f), 3);
    const juce::Colour top = on ? c.brighter(0.55f) : c, bot = on ? c.brighter(0.25f) : c.darker(0.25f);
    g.setGradientFill(juce::ColourGradient(top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 3);
    if (on) { g.setColour(c.brighter(0.8f).withAlpha(0.35f)); g.drawRoundedRectangle(r.expanded(2), 4, 2); }
    g.setColour(juce::Colours::white.withAlpha(0.5f)); g.drawLine(r.getX() + 3, r.getY() + 1, r.getRight() - 3, r.getY() + 1, 1);
    g.setColour(juce::Colours::black.withAlpha(0.12f)); g.fillRect(r.getX() + 6, r.getCentreY() - 1, r.getWidth() - 12, 2.0f);
    if (face.isNotEmpty()) drawLabel(g, face, r, 8, juce::Colour(0xc00b1b19), true);
}
void SevenEditor::paint(juce::Graphics& g) {
    const float sc = (float) getWidth() / W;
    g.addTransform(juce::AffineTransform::scale(sc));
    // case
    g.setGradientFill(juce::ColourGradient(col::caseTop, 0, 0, col::caseBot, 0, H, false)); g.fillRect(0.0f, 0.0f, W, H);
    for (float x : {0.0f, 1836.0f}) { g.setGradientFill(juce::ColourGradient(juce::Colour(0xff050505), x, 0, juce::Colour(0xff1d1d1f), x + 18, 0, false)); g.fillRoundedRectangle(x, 0, 44, H, 8); }
    for (auto pt : {juce::Point<float>(70, 24), {1810, 24}, {70, 386}, {1810, 386}}) { g.setColour(juce::Colour(0xff3a3a3c)); g.fillEllipse(pt.x - 6, pt.y - 6, 12, 12); g.setColour(juce::Colours::black); g.drawEllipse(pt.x - 6, pt.y - 6, 12, 12, 1); }
    g.setColour(juce::Colours::black); g.fillRect(44.0f, 205.0f, 1792.0f, 6.0f); g.fillRect(44.0f, 400.0f, 1792.0f, 22.0f);
    // logo
    g.setColour(col::print); g.setFont(sans(26)); g.drawText("OMEGA", juce::Rectangle<float>(108, 112, 120, 30), juce::Justification::left);
    {
        juce::Path p; juce::GlyphArrangement ga;
        ga.addLineOfText(juce::Font(juce::FontOptions(112.0f, juce::Font::bold | juce::Font::italic)), "7", 236, 146);
        ga.createPath(p);
        g.setColour(col::print); g.strokePath(p, juce::PathStrokeType(2.4f));
    }
    g.setFont(sans(9.5f)); g.drawText("6-OPERATOR FM ALGORITHM SYNTHESIZER", juce::Rectangle<float>(236, 150, 420, 14), juce::Justification::left);
    // tools
    const juce::StringArray tools {"MEMORY", "SEQ / ARP", "MIDI FILES", "FX", "RANDOM", juce::String("ALL,ALG,FREQ,ENV,LFO,FX").upToFirstOccurrenceOf(",", false, false), "RESET", "HELP", "PANIC"};
    const juce::StringArray partNames = juce::StringArray::fromTokens("ALL,ALG,FREQ,ENV,LFO,FX", ",", "");
    for (auto& h : hits) if (h.id.startsWith("tool")) {
        const int i = h.id.substring(4).getIntValue();
        const bool on = (i == 1 && showSeq) || (i == 7 && showHelp) || h.id == downId;
        g.setColour(on ? col::teal : juce::Colour(0xff232325)); g.fillRoundedRectangle(h.r, 3);
        g.setColour(juce::Colour(0xff3a3a3c)); g.drawRoundedRectangle(h.r, 3, 1);
        const juce::Colour ink = on ? juce::Colour(0xff0b2421) : col::print;
        auto r = h.r;
        if (i == 4) {   // a die
            const juce::Rectangle<float> d(r.getX() + 7, r.getCentreY() - 7, 14, 14);
            g.setColour(ink); g.drawRoundedRectangle(d, 3, 1.6f);
            for (auto pt : {juce::Point<float>(4, 4), {7, 7}, {10, 10}, {10, 4}, {4, 10}}) g.fillEllipse(d.getX() + pt.x - 1.4f, d.getY() + pt.y - 1.4f, 2.8f, 2.8f);
            r = r.withTrimmedLeft(18);
        } else if (i == 6) {   // a circular arrow
            juce::Path a; const float cx = r.getX() + 14, cy = r.getCentreY();
            a.addCentredArc(cx, cy, 6, 6, 0, 0.6f, 5.6f, true);
            g.setColour(ink); g.strokePath(a, juce::PathStrokeType(1.8f));
            juce::Path t; t.addTriangle(cx + 3.5f, cy - 9, cx + 7.5f, cy - 4, cx + 1.5f, cy - 3.5f); g.fillPath(t);
            r = r.withTrimmedLeft(18);
        }
        drawLabel(g, i == 5 ? partNames[proc.panel.dicePart] : tools[i], r, 11, ink);
    }
    {
        const juce::Rectangle<float> m(820, 178, 50, 8);
        g.setColour(juce::Colours::black); g.fillRoundedRectangle(m, 2);
        g.setColour(col::teal2); g.fillRoundedRectangle(m.withWidth(60 * juce::jlimit(0.0f, 1.0f, proc.peak.load())), 2);
    }
    // cartridge slot
    {
        const bool in = proc.bank(true) != nullptr;
        g.setColour(in ? juce::Colour(0xff2a3160) : juce::Colour(0xff050505)); g.fillRoundedRectangle(CART, 3);
        g.setColour(juce::Colour(0xff2b2b2d)); g.drawRoundedRectangle(CART, 3, 2);
        if (in) drawLabel(g, proc.cartName.isEmpty() ? "CARTRIDGE" : proc.cartName.upToLastOccurrenceOf(".", false, false).toUpperCase(), CART.reduced(6), 10, juce::Colour(0xffc9cff5));
        else { g.setColour(juce::Colours::black); g.fillRect(CART.getX() + 16, CART.getCentreY() - 1, CART.getWidth() - 32, 3.0f); }
        drawLabel(g, "CARTRIDGE", {680, 126, 200, 10}, 8, col::print, false);
    }
    drawChart(g); drawEg(g); drawOl(g);
    // sliders and labels
    drawLabel(g, "VOLUME", {160, 228, 70, 10}, 10, col::print); drawLabel(g, "DATA ENTRY", {262, 228, 90, 10}, 10, col::print);
    for (auto [r, pos] : {std::pair<juce::Rectangle<float>, float>(VOL, volPos), {DATA, dataPos}}) {
        g.setColour(juce::Colour(0xff020202)); g.fillRoundedRectangle(r, 3);
        for (int i = 0; i <= 10; ++i) { g.setColour(juce::Colour(0xff8a8f90)); g.fillRect(r.getRight() + 9, r.getY() + i * r.getHeight() / 10, 10.0f, 1.0f); }
        const float y = r.getY() + (1 - pos) * (r.getHeight() - 26);
        const juce::Rectangle<float> cap(r.getX() - 9, y, r.getWidth() + 18, 26);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b3b3d), 0, cap.getY(), juce::Colour(0xff0c0c0c), 0, cap.getBottom(), false)); g.fillRoundedRectangle(cap, 2);
        g.setColour(col::print); g.fillRect(cap.getX() + 3, cap.getY() + 12, cap.getWidth() - 6, 2.0f);
    }
    drawLabel(g, "-1 NO", {352, 320, 56, 10}, 8, col::print, false); drawLabel(g, "+1 YES", {414, 320, 56, 10}, 8, col::print, false);
    drawLabel(g, "STORE", {494, 244, 58, 10}, 10, col::print); drawLabel(g, "MEMORY PROTECT", {560, 230, 150, 10}, 10, col::print);
    drawLabel(g, "INTERNAL", {566, 246, 62, 9}, 8, col::print, false); drawLabel(g, "CARTRIDGE", {638, 246, 66, 9}, 8, col::print, false);
    drawLabel(g, "OPERATOR SELECT", {708, 236, 72, 22}, 8, col::print, false);
    drawLabel(g, "EDIT", {494, 316, 58, 10}, 10, col::print); drawLabel(g, "MEMORY SELECT", {560, 302, 150, 10}, 10, col::print);
    drawLabel(g, "INTERNAL", {566, 318, 62, 9}, 8, col::print, false); drawLabel(g, "CARTRIDGE", {638, 318, 66, 9}, 8, col::print, false); drawLabel(g, "FUNCTION", {704, 316, 80, 10}, 10, col::print);
    g.setColour(col::print); g.fillRect(570.0f, 368.0f, 128.0f, 3.0f); drawLabel(g, "PLAY", {610, 375, 48, 9}, 8, col::print, false);
    drawLabel(g, "VOICE / PARAMETER SELECT", {1226, 226, 592, 10}, 8, col::print, false); drawLabel(g, "FUNCTION", {1226, 384, 592, 10}, 8, col::tan, false);
    // the buttons
    Panel& pn = proc.panel;
    const int litBtn = pn.litButton();
    auto& q = proc.seq(); const int step = proc.stepShown.load();
    for (auto& h : hits) {
        if (h.id.startsWith("tool") || h.id == "cart") continue;
        const bool down = h.id == downId;
        bool on = false; juce::Colour c = col::teal; juce::String face;
        if (h.id == "store") { c = col::red; on = pn.storing; }
        else if (h.id == "protI") on = pn.protectSel == 0;
        else if (h.id == "protC") on = pn.protectSel == 1;
        else if (h.id == "opSel") { c = col::purple; face = "OP" + juce::String(pn.op); }
        else if (h.id == "edit") { c = col::blue; on = pn.mode == Panel::edit; face = pn.compare ? "COMPARE" : ""; }
        else if (h.id == "memI") on = pn.mode == Panel::play && !pn.cart;
        else if (h.id == "memC") on = pn.mode == Panel::play && pn.cart;
        else if (h.id == "func") { c = col::brown; on = pn.mode == Panel::func; }
        else if (h.id.startsWith("b")) {
            const int i = h.id.substring(1).getIntValue();
            on = i == litBtn;
            if (pn.mode == Panel::edit && i <= 6) on = on || ((proc.voice()[155] >> (6 - i)) & 1);
            if (pn.mode == Panel::func && pn.stepEdit && i <= 16) {
                const int k = pn.stepPage * 16 + i - 1; const auto& p = q.pats[(size_t) q.pat];
                on = k == pn.stepSel || (k < p.len && p.steps[(size_t) k].on && !p.steps[(size_t) k].notes.empty()) || k == step;
            }
            const int r = i <= 16 ? 0 : 1; juce::ignoreUnused(r);
            drawLabel(g, EDIT_BUTTONS[i], {h.r.getX() - 2, h.r.getY() - 24, 36, 21}, 6.5f, col::print, true, juce::Justification::centredBottom);
            drawLabel(g, FUNC_BUTTONS[i], {h.r.getX() - 2, h.r.getBottom() + 3, 36, 16}, 6.5f, col::tan, true, juce::Justification::centredTop);
            face = juce::String(i);
        }
        drawButton(g, h.r, c, on, down, h.id.startsWith("b") ? juce::String() : face);
        if (h.id.startsWith("b")) { g.setColour(juce::Colour(0xff0c2a26)); g.setFont(sans(10)); g.drawText(face, h.r.reduced(4, 2), juce::Justification::topLeft); }
    }
    // LED and LCD
    g.setColour(juce::Colour(0xff100303)); g.fillRoundedRectangle(LEDR, 3);
    g.setColour(col::led); g.setFont(monoFont(30));
    g.drawText(juce::String(pn.mode == Panel::play ? pn.num + 1 : pn.mode == Panel::edit ? (pn.naming ? 31 : pn.eBtn) : pn.fBtn), LEDR, juce::Justification::centred);
    g.setColour(juce::Colour(0xff2a2b23)); g.fillRoundedRectangle(LCDR, 3);
    const auto in = LCDR.reduced(8, 6);
    g.setGradientFill(juce::ColourGradient(col::lcd, 0, in.getY(), col::lcd2, 0, in.getBottom(), false)); g.fillRoundedRectangle(in, 2);
    std::pair<std::string, std::string> ln;
    { const juce::ScopedLock l(proc.lock); ln = pn.lines(); }
    g.setColour(col::ink); g.setFont(monoFont(19));
    g.drawText(ln.first, in.reduced(6, 3).withHeight(in.getHeight() / 2 - 2), juce::Justification::centredLeft, false);
    g.drawText(ln.second, in.reduced(6, 3).withTrimmedTop(in.getHeight() / 2 - 2), juce::Justification::centredLeft, false);
    // wheels
    drawLabel(g, "PITCH", {102, 482, 60, 10}, 8, col::print, false); drawLabel(g, "MODULATION", {176, 482, 80, 10}, 8, col::print, false);
    for (auto [r, v, bip] : {std::tuple<juce::Rectangle<float>, float, bool>(PW, pitchPos, true), {MW, modPos, false}}) {
        g.setColour(juce::Colour(0xff050505)); g.fillRoundedRectangle(r, 4);
        for (float y = r.getY(); y < r.getBottom(); y += 6) { g.setColour(juce::Colour(0xff2c2c2e)); g.fillRect(r.getX() + 6, y, r.getWidth() - 12, 3.0f); }
        const float f = bip ? (0.5f - v / 2) : (1 - v);
        g.setColour(col::print); g.fillRect(r.getX() + 6, r.getY() + f * (r.getHeight() - 6), r.getWidth() - 12, 6.0f);
    }
    if (showSeq) drawSeq(g); else drawKeys(g);
    if (showHelp) drawHelp(g);
    if (toastText.isNotEmpty() && juce::Time::getMillisecondCounter() < toastUntil) {
        const juce::Rectangle<float> t(W / 2 - 360, H - 70, 720, 40);
        g.setColour(juce::Colour(0xf0232427)); g.fillRoundedRectangle(t, 6);
        drawLabel(g, toastText, t.reduced(10, 4), 15, juce::Colours::white, false);
    }
}

// the algorithm diagrams (the same geometry as app.js)
void SevenEditor::drawAlg(juce::Graphics& g, int ai, juce::Rectangle<float> cell, float s, bool on) {
    const Alg& a = algs()[(size_t) ai];
    float px[6] {}, py[6] {}; bool placed[6] {};
    std::function<int(int, int, int)> place = [&](int k, int row, int c) -> int {
        if (placed[k]) return 0;
        std::vector<int> ms; for (int m = 0; m < 6; ++m) if (((a.mod[k] >> m) & 1) && !placed[m]) ms.push_back(m);
        placed[k] = true; px[k] = (float) c; py[k] = (float) row;
        int w = 0; for (int m : ms) w += place(m, row + 1, c + w);
        px[k] = ms.empty() ? (float) c : (c + (c + std::max(1, w) - 1)) / 2.0f;
        return std::max(1, w);
    };
    int cols = 0; for (int k = 0; k < 6; ++k) if (a.car[k]) cols += place(k, 0, cols);
    for (int k = 0; k < 6; ++k) { int np = 0; float sx = 0; for (int p = 0; p < 6; ++p) if ((a.mod[p] >> k) & 1) { np++; sx += px[p]; } if (np > 1) px[k] = sx / np; }
    int rows = 0; for (int k = 0; k < 6; ++k) rows = std::max(rows, (int) py[k]); rows++;
    const float sx = std::min(s * 1.35f, (cell.getWidth() - 4) / std::max(1, cols)), sy = std::min(s * 1.45f, (cell.getHeight() - 12) / rows);
    const float x0 = cell.getX() + (cell.getWidth() - sx * (cols - 1)) / 2, yb = cell.getBottom() - 12 - s / 2;
    auto P = [&](int k) { return juce::Point<float>(x0 + px[k] * sx, yb - py[k] * sy); };
    const juce::Colour c = on ? col::teal : col::print;
    g.setColour(c);
    for (int k = 0; k < 6; ++k) for (int m = 0; m < 6; ++m) if ((a.mod[k] >> m) & 1) {
        const auto p1 = P(k), p2 = P(m); const float ym = (p1.y + p2.y) / 2;
        juce::Path pa; pa.startNewSubPath(p2.x, p2.y + s / 2); pa.lineTo(p2.x, ym); pa.lineTo(p1.x, ym); pa.lineTo(p1.x, p1.y - s / 2); g.strokePath(pa, juce::PathStrokeType(1.1f));
    }
    float xa = 1e9f, xb = -1e9f; const float yl = yb + s / 2 + 3;
    for (int k = 0; k < 6; ++k) if (a.car[k]) { const auto p = P(k); xa = std::min(xa, p.x); xb = std::max(xb, p.x); g.drawLine(p.x, p.y + s / 2, p.x, yl, 1.1f); }
    g.drawLine(xa, yl, xb, yl, 1.1f);
    { const auto f = P(a.fb), sp = P(a.fbs); const float r = s / 2 + 3; juce::Path pa; pa.startNewSubPath(sp.x + s / 2, sp.y); pa.lineTo(sp.x + r + 1, sp.y); pa.lineTo(sp.x + r + 1, f.y - r); pa.lineTo(f.x, f.y - r); pa.lineTo(f.x, f.y - s / 2); g.strokePath(pa, juce::PathStrokeType(1.1f)); }
    for (int k = 0; k < 6; ++k) { const auto p = P(k); const juce::Rectangle<float> b(p.x - s / 2, p.y - s / 2, s, s); if (on) g.fillRect(b); else g.drawRect(b, 1.1f); }
}
void SevenEditor::drawChart(juce::Graphics& g) {
    g.setColour(juce::Colour(0xffcfd3d3)); g.drawRoundedRectangle(CHART, 2, 1.5f);
    drawLabel(g, "ALGORITHM", {CHART.getX() + 6, CHART.getY() + 3, 100, 10}, 8, col::print, true, juce::Justification::left);
    const int cur = proc.voice()[134];
    for (int i = 0; i < 32; ++i) {
        const int r = i < 16 ? 0 : 1, c = i % 16;
        const juce::Rectangle<float> cell(CHART.getX() + c * 40.0f, CHART.getY() + 14 + r * 73.0f, 40, 70);
        if (i == cur) { g.setColour(col::teal.withAlpha(0.3f)); g.fillRoundedRectangle(cell.reduced(1, 0), 2); }
        drawAlg(g, i, cell, 6.5f, i == cur);
        drawLabel(g, juce::String(i + 1), {cell.getX(), cell.getBottom() - 9, 40, 9}, 8, i == cur ? col::teal : col::print);
    }
}
void SevenEditor::drawEg(juce::Graphics& g) {
    g.setColour(juce::Colour(0xffcfd3d3)); g.drawRoundedRectangle(EGR, 2, 1.5f);
    const auto& v = proc.voice(); const int ob = opBase(proc.panel.op);
    drawLabel(g, "EG  OP" + juce::String(proc.panel.op), {EGR.getX() + 5, EGR.getY() + 3, 100, 10}, 8, col::print, true, juce::Justification::left);
    int r[4], l[4]; for (int i = 0; i < 4; ++i) { r[i] = v[(size_t) (ob + i)]; l[i] = v[(size_t) (ob + 4 + i)]; }
    const float top = EGR.getY() + 30, bot = EGR.getBottom() - 30;
    auto dur = [](int x) { return 3 + (99 - x) * 0.22f; };
    auto Y = [&](int lv) { return bot - (bot - top) * lv / 99.0f; };
    const float total = dur(r[0]) + dur(r[1]) + dur(r[2]) + 14 + dur(r[3]), kx = (EGR.getWidth() - 12) / total;
    float x = EGR.getX() + 6; juce::Path p; p.startNewSubPath(x, Y(l[3]));
    x += dur(r[0]) * kx; p.lineTo(x, Y(l[0])); x += dur(r[1]) * kx; p.lineTo(x, Y(l[1])); x += dur(r[2]) * kx; p.lineTo(x, Y(l[2]));
    const float kOff = x + 14 * kx; p.lineTo(kOff, Y(l[2])); p.lineTo(kOff + dur(r[3]) * kx, Y(l[3]));
    g.setColour(juce::Colour(0xff777777)); g.drawLine(EGR.getX() + 6, bot, EGR.getRight() - 6, bot, 1); g.drawLine(kOff, top - 4, kOff, bot, 0.7f);
    g.setColour(col::teal); g.strokePath(p, juce::PathStrokeType(1.6f));
    drawLabel(g, "L " + juce::String(l[0]) + " " + juce::String(l[1]) + " " + juce::String(l[2]) + " " + juce::String(l[3]), {EGR.getX() + 4, EGR.getBottom() - 24, EGR.getWidth() - 8, 9}, 7, col::print, true, juce::Justification::left);
    drawLabel(g, "R " + juce::String(r[0]) + " " + juce::String(r[1]) + " " + juce::String(r[2]) + " " + juce::String(r[3]), {EGR.getX() + 4, EGR.getBottom() - 13, EGR.getWidth() - 8, 9}, 7, col::print, true, juce::Justification::left);
}
void SevenEditor::drawOl(juce::Graphics& g) {
    g.setColour(juce::Colour(0xffcfd3d3)); g.drawRoundedRectangle(OLR, 2, 1.5f);
    drawLabel(g, "LEVEL SCALING", {OLR.getX() + 5, OLR.getY() + 3, 100, 10}, 8, col::print, true, juce::Justification::left);
    const auto& v = proc.voice(); const int ob = opBase(proc.panel.op);
    const int bp = v[(size_t) (ob + 8)], ld = v[(size_t) (ob + 9)], rd = v[(size_t) (ob + 10)], lc = v[(size_t) (ob + 11)], rc = v[(size_t) (ob + 12)];
    const float mid = OLR.getY() + 80, x0 = OLR.getX() + 12, x1 = OLR.getRight() - 12, cx = x0 + (x1 - x0) * bp / 99.0f;
    g.setColour(juce::Colour(0xff777777)); g.drawLine(x0, mid, x1, mid, 1); g.drawLine(cx, OLR.getY() + 26, cx, OLR.getY() + 134, 0.7f);
    auto curve = [&](int side, int depth, int c) {
        juce::Path p;
        for (int i = 0; i <= 20; ++i) {
            const float t = i / 20.0f, x = side < 0 ? cx - (cx - x0) * t : cx + (x1 - cx) * t;
            float y = (c == 0 || c == 3) ? t : (std::exp(t * 3) - 1) / (std::exp(3.0f) - 1);
            y *= depth / 99.0f * 48; if (c < 2) y = -y;
            if (i == 0) p.startNewSubPath(x, mid - y); else p.lineTo(x, mid - y);
        }
        g.setColour(col::teal); g.strokePath(p, juce::PathStrokeType(1.6f));
    };
    curve(-1, ld, lc); curve(1, rd, rc);
    drawLabel(g, bpName(bp), {cx - 20, OLR.getY() + 138, 40, 9}, 7, col::print);
    drawLabel(g, juce::String(CURVES[lc]) + " " + juce::String(ld), {OLR.getX() + 4, OLR.getBottom() - 12, 50, 9}, 7, col::print, true, juce::Justification::left);
    drawLabel(g, juce::String(CURVES[rc]) + " " + juce::String(rd), {OLR.getRight() - 54, OLR.getBottom() - 12, 50, 9}, 7, col::print, true, juce::Justification::right);
}
void SevenEditor::drawKeys(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff050505)); g.fillRect(KEYS);
    int whites = 0; for (int n = KLO; n <= KHI; ++n) if (!isBlack(n)) whites++;
    const float ww = KEYS.getWidth() / whites;
    int i = 0;
    for (int n = KLO; n <= KHI; ++n) {
        if (isBlack(n)) continue;
        const juce::Rectangle<float> r(KEYS.getX() + i * ww, KEYS.getY(), ww, KEYS.getHeight());
        const bool on = lit.count(n) > 0 || proc.keys.isNoteOn(1, n);
        g.setGradientFill(juce::ColourGradient(on ? juce::Colour(0xffcde9e4) : juce::Colour(0xfff4f4f0), 0, r.getY(), on ? juce::Colour(0xffb8e7df) : juce::Colour(0xffe2e2dc), 0, r.getBottom(), false));
        g.fillRoundedRectangle(r.reduced(0.5f, 0), 3);
        g.setColour(juce::Colour(0xff1a1a1a)); g.drawRect(r, 1);
        if (n % 12 == 0) drawLabel(g, noteName(n), {r.getX(), r.getBottom() - 20, ww, 10}, 9, juce::Colour(0xff777777));
        i++;
    }
    i = 0;
    for (int n = KLO; n <= KHI; ++n) {
        if (isBlack(n)) { const float bw = ww * 0.6f; const juce::Rectangle<float> r(KEYS.getX() + i * ww - bw / 2, KEYS.getY(), bw, KEYS.getHeight() * 0.62f);
            const bool on = lit.count(n) > 0 || proc.keys.isNoteOn(1, n);
            g.setGradientFill(juce::ColourGradient(on ? juce::Colour(0xff1c4c46) : juce::Colour(0xff2b2b2b), 0, r.getY(), juce::Colour(0xff0a0a0a), 0, r.getBottom(), false)); g.fillRoundedRectangle(r, 3); }
        else i++;
    }
}
int SevenEditor::keyAt(juce::Point<float> p) const {
    if (!KEYS.contains(p)) return -1;
    int whites = 0; for (int n = KLO; n <= KHI; ++n) if (!isBlack(n)) whites++;
    const float ww = KEYS.getWidth() / whites;
    if (p.y < KEYS.getY() + KEYS.getHeight() * 0.62f) {
        int i = 0;
        for (int n = KLO; n <= KHI; ++n) { if (isBlack(n)) { const float bw = ww * 0.6f, x = KEYS.getX() + i * ww - bw / 2; if (p.x >= x && p.x < x + bw) return n; } else i++; }
    }
    const int wi = (int) ((p.x - KEYS.getX()) / ww);
    int i = 0; for (int n = KLO; n <= KHI; ++n) if (!isBlack(n)) { if (i == wi) return n; i++; }
    return -1;
}

/* ---------------- the sequencer page (where the keys are) ---------------- */
static juce::Rectangle<float> seqBtn(int i) { return {300.0f + i * 112.0f, 440, 104, 28}; }
static juce::Rectangle<float> seqCell(int i) { return {300.0f + i * 95.0f, 484, 90, 70}; }
static juce::Rectangle<float> laneCell(int lane, int i) { return {460.0f + i * 85.0f, 572.0f + lane * 52.0f, 80, 46}; }
void SevenEditor::drawSeq(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff111214)); g.fillRect(KEYS);
    const juce::ScopedLock l(proc.lock);
    auto& q = proc.seq(); auto& p = q.pats[(size_t) q.pat]; auto& pn = proc.panel; const int step = proc.stepShown.load();
    const juce::String labels[] = {proc.playing() ? "STOP" : "PLAY", proc.recording() ? "RECORDING" : "RECORD", "PATTERN -", "PATTERN +", "PAGE " + juce::String(seqPage + 1) + "/4",
        q.arp.on ? "ARP ON" : "ARP OFF", "IMPORT MIDI", "EXPORT MIDI", "CLEAR PAT", "TEMPO -", "TEMPO +", "KEYS"};
    for (int i = 0; i < 12; ++i) {
        const auto r = seqBtn(i); const bool on = (i == 0 && proc.playing()) || (i == 1 && proc.recording()) || (i == 5 && q.arp.on);
        g.setColour(on ? col::teal : juce::Colour(0xff2a2b2e)); g.fillRoundedRectangle(r, 4);
        drawLabel(g, labels[i], r, 11, on ? juce::Colour(0xff04211d) : col::print);
    }
    drawLabel(g, "PATTERN " + juce::String(q.pat + 1) + "   " + juce::String((int) q.tempo) + " BPM" + (q.ext ? " (HOST)" : "") + "   LENGTH " + juce::String(p.len) + "   STEP " + RATE_NAMES[p.rate]
        + "   ARP " + ARP_MODES[q.arp.mode] + " x" + juce::String(q.arp.oct) + "   (FUNCTION 25-29 on the panel set the rest)", {300, 562, 1500, 8}, 9, col::dim, false, juce::Justification::left);
    for (int i = 0; i < 16; ++i) {
        const int k = seqPage * 16 + i; const auto r = seqCell(i);
        if (k >= p.len) continue;
        const auto& st = p.steps[(size_t) k];
        const bool sel = pn.mode == Panel::func && pn.stepEdit && pn.stepSel == k;
        g.setColour(k == step ? col::teal : !st.notes.empty() ? juce::Colour(0xff173b37) : juce::Colour(0xff101113)); g.fillRoundedRectangle(r, 3);
        if (!st.on) { g.setColour(juce::Colours::black.withAlpha(0.6f)); g.fillRoundedRectangle(r, 3); }
        g.setColour(sel ? col::tan : juce::Colour(0xff2b2c2f)); g.drawRoundedRectangle(r, 3, sel ? 2.0f : 1.0f);
        juce::String t = juce::String(k + 1) + "\n"; for (auto& nv : st.notes) t << noteName(nv.first) << " ";
        g.setColour(k == step ? juce::Colour(0xff04211d) : col::print); g.setFont(monoFont(11)); g.drawFittedText(t, r.reduced(4).toNearestInt(), juce::Justification::topLeft, 4);
        g.setColour(col::tan); g.fillRect(r.getX(), r.getBottom() - 3, r.getWidth() * std::min(100, st.gate) / 100.0f, 3.0f);
    }
    for (int li = 0; li < LANES; ++li) {
        const auto& ln = p.lanes[(size_t) li];
        drawLabel(g, "LANE " + juce::String(li + 1) + "  " + motionName(ln.param) + (ln.smooth ? " ~" : ""), {300, laneCell(li, 0).getY(), 150, 46}, 10, ln.param >= 0 ? col::print : col::dim, true, juce::Justification::centredLeft);
        const int mx = ln.param < 0 ? 99 : ln.param >= 200 ? FUNC_DEF[ln.param - 200].max : vparams()[(size_t) ln.param].max;
        for (int i = 0; i < 16; ++i) {
            const int k = seqPage * 16 + i; const auto r = laneCell(li, i);
            if (k >= p.len) continue;
            g.setColour(juce::Colour(0xff1d1f23)); g.fillRect(r);
            const int v = ln.vals[(size_t) k];
            if (v >= 0) { g.setColour(col::purple); g.fillRect(r.withTrimmedTop(r.getHeight() * (1 - v / (float) std::max(1, mx)))); }
        }
    }
    drawLabel(g, "Tap a step to select it (again: on / off) and play notes to write them. Drag in a lane to draw values (bottom edge clears). Pick a lane's parameter with FUNCTION 29.",
        {300, 776, 1500, 8}, 9, col::dim, false, juce::Justification::left);
}
void SevenEditor::drawHelp(juce::Graphics& g) {
    const juce::Rectangle<float> r(120, 215, 1640, 575);
    g.setColour(juce::Colour(0xf2141517)); g.fillRoundedRectangle(r, 8);
    const char* text =
        "PLAYING  Push MEMORY SELECT INTERNAL or CARTRIDGE, then 1-32. YES / NO step through the voices. Program changes from the host or MIDI pick voices 1-32.\n\n"
        "EDITING  Push EDIT, then the button for the parameter (printed above it), and move DATA ENTRY or push YES (+1) / NO (-1). Pushed again, 20 / 21 / 28 / 29 step through\n"
        "the four rates or levels, 23 / 24 left then right curve and depth, 15 pitch then amplitude mod sensitivity, 16 oscillator mode then key sync. OPERATOR SELECT picks the operator;\n"
        "buttons 1-6 switch operators on and off; 31 names the voice (the buttons type letters, YES / NO move, 31 again ends); 32 copies an operator. EDIT again = COMPARE.\n\n"
        "STORING  MEMORY PROTECT (then NO) turns protection off; STORE, then 1-32. FUNCTION 9 recalls edits you left for another voice.\n\n"
        "FUNCTION  1 tune  2 poly/mono  3-4 bend range/step  5-7 portamento  8 MIDI channels, sys info, send voice / bank  9 edit recall  10 voice init (again: random)  11 load .syx / .mid\n"
        "12 save bank  13 save voice  14 cart > int  15 int > cart  17-24 wheel, foot, breath, aftertouch range and assign  25 arpeggiator  26 tempo / host sync\n"
        "27 sequencer (pattern, play, record, length, step, swing, key transpose)  28 step edit  29 motion lanes  30 converter (12-bit Mark I / 16-bit clean), then chorus, delay and reverb  31 MIDI files  32 sysex\n\n"
        "RANDOM (the die) rolls a new voice (STORE keeps it); the button beside it picks what it rolls (ALL, ALG, FREQ, ENV, LFO, FX); RESET goes back to the stored voice.\nFX: chorus, then the Omega 110's TC 2290-style dynamic delay and REV7-style reverb (Hall, Room, Vocal, Plate, Early Ref).\n\n"
        "FILES  Standard 32-voice bank dumps (4104 bytes) and single voices (163 bytes) load and save, so banks from the original instrument and librarians work here and back.\n"
        "All voice and function parameters can be automated by the host. With FUNCTION 26 set to MIDI/HOST, the sequencer and arpeggiator follow the host's tempo and transport.";
    g.setColour(col::print); g.setFont(sans(14, false));
    g.drawFittedText(text, r.reduced(28, 22).toNearestInt(), juce::Justification::topLeft, 40, 1.0f);
}

/* ---------------- menus ---------------- */
void SevenEditor::memoryMenu() {
    juce::PopupMenu m;
    m.addItem(1, "Load .syx into the cartridge...");
    m.addItem(2, "Load .syx into internal memory...");
    m.addItem(3, "Save internal bank...");
    m.addItem(4, "Save cartridge bank...", proc.bank(true) != nullptr);
    m.addItem(5, "Save the edit voice...");
    m.addItem(6, "Eject cartridge", proc.bank(true) != nullptr);
    m.addSeparator();
    m.addItem(7, "Memory protect internal", true, proc.panel.protect[0]);
    m.addItem(8, "Memory protect cartridge", true, proc.panel.protect[1]);
    m.addItem(9, "Factory voices into internal memory");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
        switch (r) {
            case 1: proc.request("loadFile", 0); break;
            case 2: proc.request("loadInt", 0); break;
            case 3: proc.request("saveBank", 0); break;
            case 4: proc.request("saveBank", 1); break;
            case 5: proc.request("saveVoice", 0); break;
            case 6: proc.ejectCart(); break;
            case 7: withPanel([](Panel& p) { p.protect[0] = !p.protect[0]; }); break;
            case 8: withPanel([](Panel& p) { p.protect[1] = !p.protect[1]; }); break;
            case 9: if (proc.panel.protect[0]) say("Turn memory protect internal off first"); else { proc.factoryReset(); say("Factory voices loaded"); } break;
        }
        repaint();
    });
}
// effects: starting points and the converter; each amount is on FUNCTION 30 (pushed again) and automatable by the host
void SevenEditor::fxMenu() {
    struct Pre { const char* name; std::vector<std::pair<int, int>> v; };
    static const std::vector<Pre> pres = {
        {"Dry", {{F::chorus, 0}, {F::delayMix, 0}, {F::revMix, 0}}},
        {"Chorus", {{F::chorus, 55}, {F::chorusRate, 30}, {F::delayMix, 0}, {F::revMix, 0}}},
        {"Studio", {{F::chorus, 35}, {F::chorusRate, 25}, {F::delayMix, 18}, {F::dlSync, 6}, {F::delayFb, 30}, {F::dlDuck, 50}, {F::revMix, 25}, {F::rvProg, 1}, {F::revSize, 14}, {F::rvHigh, 6}, {F::rvDelay, 12}}},
        {"Big hall", {{F::chorus, 25}, {F::delayMix, 0}, {F::revMix, 45}, {F::rvProg, 0}, {F::revSize, 36}, {F::rvHigh, 6}, {F::rvDelay, 30}, {F::rvDiff, 9}, {F::rvEr, 35}, {F::rvCut, 90}}},
        {"Plate", {{F::chorus, 0}, {F::delayMix, 0}, {F::revMix, 40}, {F::rvProg, 3}, {F::revSize, 20}, {F::rvHigh, 8}, {F::rvDelay, 5}, {F::rvDiff, 10}, {F::rvCut, 110}}},
        {"Dub echo", {{F::chorus, 0}, {F::delayMix, 55}, {F::dlSync, 8}, {F::delayFb, 70}, {F::dlHiCut, 30}, {F::dlLoCut, 25}, {F::dlModDepth, 30}, {F::dlDuck, 70}, {F::dlPing, 1}, {F::revMix, 20}, {F::rvProg, 0}, {F::revSize, 30}}},
        {"Dream", {{F::chorus, 70}, {F::chorusRate, 18}, {F::delayMix, 30}, {F::dlSync, 9}, {F::delayFb, 60}, {F::dlModDepth, 60}, {F::revMix, 50}, {F::rvProg, 2}, {F::revSize, 60}, {F::rvHigh, 7}, {F::rvDelay, 40}}}};
    juce::PopupMenu m;
    for (int i = 0; i < (int) pres.size(); ++i) m.addItem(1 + i, juce::String("Effects: ") + pres[(size_t) i].name);
    m.addSeparator();
    juce::PopupMenu prog;
    for (int p = 0; p < 35; ++p) { if (p == REV7_FULL) prog.addSeparator(); prog.addItem(100 + p, p < REV7_FULL ? juce::String("110: ") + RV_PROGS[p] : juce::String(p - REV7_FULL + 1) + " " + RV_PROGS[p], true, proc.func()[F::rvProg] == p); }
    m.addSubMenu("REV7 program (the 110's five, then the 30 factory programs)", prog);
    juce::PopupMenu sync;
    for (int d = 0; d < 14; ++d) sync.addItem(60 + d, d ? juce::String("Delay sync ") + DL_DIV_NAMES[d] : juce::String("Delay time in ms"), true, proc.func()[F::dlSync] == d);
    m.addSubMenu("Delay sync", sync);
    m.addItem(10, "Mark I 12-bit converter", true, proc.func()[F::dac] == 0);
    m.addSeparator();
    m.addItem(11, "Every amount: FUNCTION 30 (push again for each page), or automate from the host", false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
        if (r >= 1 && r <= (int) pres.size()) { for (auto& kv : pres[(size_t) r - 1].v) proc.setFunc(kv.first, kv.second); say(juce::String("Effects: ") + pres[(size_t) r - 1].name); }
        else if (r == 10) proc.setFunc(F::dac, proc.func()[F::dac] ? 0 : 1);
        else if (r >= 100 && r < 135) proc.setFunc(F::rvProg, r - 100);
        else if (r >= 60 && r < 74) proc.setFunc(F::dlSync, r - 60);
        repaint();
    });
}
void SevenEditor::midiMenu() {
    juce::PopupMenu m;
    m.addItem(1, "Play a MIDI file...");
    m.addItem(2, "Stop the MIDI file", proc.playerPlaying());
    m.addItem(3, "Loop the MIDI file", true, proc.playerLoop());
    m.addSeparator();
    m.addItem(4, "Import a MIDI file into the pattern...");
    m.addItem(5, "Export the pattern as a MIDI file...");
    m.addSeparator();
    m.addItem(6, proc.playerRec() ? "Stop recording and save..." : "Record performance");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
        if (r == 1) proc.request("midiPlay", 0); else if (r == 2) proc.request("midiStop", 0); else if (r == 3) proc.request("midiLoop", proc.playerLoop() ? 0 : 1);
        else if (r == 4) proc.request("midiImport", 0); else if (r == 5) proc.request("midiExport", 0); else if (r == 6) proc.request(proc.playerRec() ? "recStop" : "recStart", 0);
    });
}

/* ---------------- mouse and keys ---------------- */
void SevenEditor::mouseDown(const juce::MouseEvent& e) {
    const auto p = toDesign(e.position);
    grabKeyboardFocus();
    if (showHelp) { showHelp = false; repaint(); return; }
    for (auto& h : hits) if (h.r.contains(p)) {
        downId = h.id; h.fn(); downAt = juce::Time::getMillisecondCounter(); repeatFn = h.repeat ? h.fn : nullptr; repeatCount = 0;
        repaint(); return;
    }
    if (VOL.expanded(12, 4).contains(p)) { dragWhat = 1; mouseDrag(e); return; }
    if (DATA.expanded(12, 4).contains(p)) { dragWhat = 2; mouseDrag(e); return; }
    if (PW.contains(p)) { dragWhat = 3; mouseDrag(e); return; }
    if (MW.contains(p)) { dragWhat = 4; mouseDrag(e); return; }
    if (CHART.contains(p)) {
        const int c = (int) ((p.x - CHART.getX()) / 40), r = (int) ((p.y - CHART.getY() - 14) / 73);
        if (c >= 0 && c < 16 && r >= 0 && r < 2) withPanel([&](Panel& pn) { if (pn.mode != Panel::edit) pn.flash("ALGORITHM", "PUSH EDIT FIRST"); else { pn.eBtn = 7; pn.set(134, r * 16 + c); } });
        repaint(); return;
    }
    if (showSeq && KEYS.contains(p)) {
        for (int i = 0; i < 12; ++i) if (seqBtn(i).contains(p)) {
            switch (i) {
                case 0: proc.seqCmd(proc.playing() ? "stop" : "play", 0); break;
                case 1: proc.seqCmd("rec", proc.recording() ? 0 : 1); break;
                case 2: proc.seqCmd("pattern", proc.seq().pat - 1); break;
                case 3: proc.seqCmd("pattern", proc.seq().pat + 1); break;
                case 4: seqPage = (seqPage + 1) % 4; break;
                case 5: proc.seqCmd("arp", proc.seq().arp.on ? 0 : 1); break;
                case 6: proc.request("midiImport", 0); break;
                case 7: proc.request("midiExport", 0); break;
                case 8: { const juce::ScopedLock l(proc.lock); proc.seq().pats[(size_t) proc.seq().pat] = Pattern(); break; }
                case 9: { const juce::ScopedLock l(proc.lock); proc.seq().tempo = std::max(30.0, proc.seq().tempo - 1); break; }
                case 10: { const juce::ScopedLock l(proc.lock); proc.seq().tempo = std::min(300.0, proc.seq().tempo + 1); break; }
                case 11: showSeq = false; break;
            }
            repaint(); return;
        }
        for (int i = 0; i < 16; ++i) if (seqCell(i).contains(p)) {
            const int k = seqPage * 16 + i;
            withPanel([&](Panel& pn) {
                auto& q = proc.seq(); auto& pt = q.pats[(size_t) q.pat]; if (k >= pt.len) return;
                if (pn.mode == Panel::func && pn.stepEdit && pn.stepSel == k) pt.steps[(size_t) k].on = !pt.steps[(size_t) k].on;
                pn.mode = Panel::func; pn.fBtn = 28; pn.stepEdit = true; pn.stepSel = k; pn.stepPage = k / 16; proc.seqCmd("cursor", k);
            });
            repaint(); return;
        }
        for (int li = 0; li < LANES; ++li) for (int i = 0; i < 16; ++i) if (laneCell(li, i).contains(p)) { dragWhat = 6; laneDrag = li; mouseDrag(e); return; }
        return;
    }
    const int n = keyAt(p);
    if (n >= 0) {
        const float f = juce::jlimit(0.0f, 1.0f, (p.y - KEYS.getY()) / KEYS.getHeight());
        keyDown = n; lit.insert(n); dragWhat = 5; proc.noteFromUi(n, juce::jlimit(20, 127, (int) (30 + f * 97)), true); repaint();
    }
}
void SevenEditor::mouseDrag(const juce::MouseEvent& e) {
    const auto p = toDesign(e.position);
    auto frac = [&](juce::Rectangle<float> r) { return juce::jlimit(0.0f, 1.0f, 1 - (p.y - r.getY()) / r.getHeight()); };
    switch (dragWhat) {
        case 1: volPos = frac(VOL); proc.setFunc(F::volume, (int) std::round(volPos * 99)); break;
        case 2: dataPos = frac(DATA); withPanel([this](Panel& pn) { pn.data(dataPos); }); break;
        case 3: pitchPos = (frac(PW) - 0.5f) * 2; proc.bendUi = juce::jlimit(-8192, 8191, (int) (pitchPos * 8192)); proc.ctlDirty = true; break;
        case 4: modPos = frac(MW); proc.wheelUi = (int) std::round(modPos * 127); proc.ctlDirty = true; break;
        case 5: { const int n = keyAt(p); if (n >= 0 && n != keyDown) { if (keyDown >= 0) { proc.noteFromUi(keyDown, 0, false); lit.erase(keyDown); } keyDown = n; lit.insert(n); proc.noteFromUi(n, 100, true); } break; }
        case 6: {
            for (int i = 0; i < 16; ++i) {
                const auto r = laneCell(laneDrag, i);
                if (p.x < r.getX() || p.x >= r.getRight()) continue;
                const juce::ScopedLock l(proc.lock);
                auto& q = proc.seq(); auto& pt = q.pats[(size_t) q.pat]; auto& ln = pt.lanes[(size_t) laneDrag]; const int k = seqPage * 16 + i;
                if (ln.param < 0) { proc.panel.flash("MOTION LANE " + std::to_string(laneDrag + 1), "SET WITH FUNC 29"); break; }
                const int mx = ln.param >= 200 ? FUNC_DEF[ln.param - 200].max : vparams()[(size_t) ln.param].max;
                const float f = 1 - (p.y - r.getY()) / r.getHeight();
                ln.vals[(size_t) k] = f < 0.04f ? -1 : (int) std::round(juce::jlimit(0.0f, 1.0f, f) * mx);
            }
            break;
        }
        default: break;
    }
    repaint();
}
void SevenEditor::mouseUp(const juce::MouseEvent&) {
    if (dragWhat == 5 && keyDown >= 0) { proc.noteFromUi(keyDown, 0, false); lit.erase(keyDown); keyDown = -1; }
    if (dragWhat == 3) { pitchPos = 0; proc.bendUi = 0; proc.ctlDirty = true; }
    dragWhat = 0; downId.clear(); repeatFn = nullptr; repaint();
}
// computer keys: A W S E D F T G Y H U J K O L P ;  (Z / X octave); typing names while naming a voice
bool SevenEditor::keyPressed(const juce::KeyPress& k) {
    const auto c = k.getTextCharacter();
    if (proc.panel.mode == Panel::edit && proc.panel.naming && c >= 32 && c < 127) { withPanel([c](Panel& p) { p.set(145 + p.nameCur, (int) c); p.nameCur = std::min(9, p.nameCur + 1); }); repaint(); return true; }
    if (c == 'z') { pcOct = std::max(-3, pcOct - 1); return true; }
    if (c == 'x') { pcOct = std::min(3, pcOct + 1); return true; }
    return keyStateChanged(true);
}
bool SevenEditor::keyStateChanged(bool) {
    static const char* map = "awsedftgyhujkolp;";
    bool used = false;
    for (int i = 0; map[i]; ++i) {
        const int n = 60 + pcOct * 12 + i; const bool down = juce::KeyPress::isKeyCurrentlyDown(map[i]);
        if (down && !pcKeys.count(n)) { pcKeys.insert(n); proc.noteFromUi(n, 100, true); used = true; }
        else if (!down && pcKeys.count(n)) { pcKeys.erase(n); proc.noteFromUi(n, 0, false); used = true; }
    }
    return used;
}
void SevenEditor::timerCallback() {
    if (repeatFn && juce::Time::getMillisecondCounter() - downAt > 420) { repeatFn(); repeatCount++; }
    repaint();
    proc.peak = proc.peak.load() * 0.85f;
}

}  // namespace seven
