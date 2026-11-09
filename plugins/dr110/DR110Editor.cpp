#include "DR110Editor.h"

namespace dr110 {

using juce::Colour; using juce::Graphics; using juce::Path; using juce::Rectangle;

/* ---------------- the front panel: the layout of dr110/faceplate.js ---------------- */
namespace layout {
struct Key { const char* id; float x, y, w, h; };
const Key KEYS[] = {
    {"shift", 105, 733, 155, 105}, {"bank", 340, 733, 77, 105},
    {"n1", 465, 730, 75, 105}, {"n2", 590, 730, 75, 105}, {"n3", 712, 730, 75, 105}, {"n4", 832, 730, 75, 105},
    {"n5", 952, 730, 75, 105}, {"n6", 1070, 730, 75, 105}, {"n7", 1188, 730, 75, 105}, {"n8", 1305, 730, 75, 105},
    {"start", 1452, 695, 140, 132}, {"stop", 1663, 695, 140, 132},
    {"acc", 110, 905, 186, 122}, {"v0", 370, 905, 183, 122}, {"v1", 628, 905, 178, 122}, {"v2", 888, 905, 178, 122},
    {"v3", 1138, 905, 175, 122}, {"v4", 1385, 905, 173, 122}, {"v5", 1627, 905, 171, 122}};
constexpr int NK = (int) (sizeof(KEYS) / sizeof(KEYS[0]));
constexpr int K_SHIFT = 0, K_ACC = 12, K_V0 = 13;
struct KnobPos { const char* id; float x, y; };
const KnobPos KNOBS[] = {{"tempo", 1185, 400}, {"balance", 1380, 400}, {"accent", 1575, 400}, {"volume", 1768, 400}};
constexpr float KR = 66;
constexpr float GX = 310, GY = 240, CW = 33.4375f, CH = 32.6f, LX = 172, NY = 236, NX0 = 323.5f, NDX = 32.75f;
}
const Colour INK(0xff26302a), DIM(0x1f26302a), BLUE1(0xff3a95e6), BLUE2(0xff155ea6), TXT(0xff1b1b1b);

static juce::Font font(float h, bool bold = true, bool italic = false) {
    return juce::Font(juce::FontOptions(h, (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0)));
}
// SVG-style text: x, baseline y, anchor (-1 start, 0 middle, 1 end); fit squeezes it to that width
static void txt(Graphics& g, float x, float y, const juce::String& s, float size, Colour c = TXT, int anchor = 0, float fit = 0, bool bold = true, bool italic = false) {
    g.setColour(c); g.setFont(font(size, bold, italic));
    const float w = fit > 0 ? fit : juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), s) + 4;
    const float x0 = anchor < 0 ? x : anchor > 0 ? x - w : x - w / 2;
    juce::GlyphArrangement ga;
    ga.addFittedText(g.getCurrentFont(), s, x0, y - size * 0.82f, w, size * 1.05f, anchor < 0 ? juce::Justification::left : anchor > 0 ? juce::Justification::right : juce::Justification::centred, 1, 0.3f);
    ga.draw(g);
}
static Path poly(std::initializer_list<juce::Point<float>> pts) { Path p; bool first = true; for (auto q : pts) { if (first) p.startNewSubPath(q); else p.lineTo(q); first = false; } p.closeSubPath(); return p; }

static void seg7(Graphics& g, float x, float y, float w, float h, char ch) {
    const char* segs = "";
    switch (ch) {
        case '0': segs = "abcdef"; break; case '1': segs = "bc"; break; case '2': segs = "abged"; break; case '3': segs = "abgcd"; break;
        case '4': segs = "fgbc"; break; case '5': segs = "afgcd"; break; case '6': segs = "afgedc"; break; case '7': segs = "abc"; break;
        case '8': segs = "abcdefg"; break; case '9': segs = "abcdfg"; break; case '-': segs = "g"; break; case 'd': segs = "bcdeg"; break;
        case 'A': segs = "abcefg"; break; case 'U': segs = "bcdef"; break; default: break;
    }
    const float t = std::max(4.0f, w * 0.16f), H = h / 2;
    auto on = [&](char s) { return std::strchr(segs, s) != nullptr; };
    struct S { char id; Path p; };
    const S all[] = {
        {'a', poly({{x + t, y}, {x + w - t, y}, {x + w - t * 1.5f, y + t}, {x + t * 1.5f, y + t}})},
        {'d', poly({{x + t * 1.5f, y + h - t}, {x + w - t * 1.5f, y + h - t}, {x + w - t, y + h}, {x + t, y + h}})},
        {'g', poly({{x + t, y + H}, {x + t * 1.5f, y + H - t / 2}, {x + w - t * 1.5f, y + H - t / 2}, {x + w - t, y + H}, {x + w - t * 1.5f, y + H + t / 2}, {x + t * 1.5f, y + H + t / 2}})},
        {'f', poly({{x, y + t}, {x + t, y + t * 1.5f}, {x + t, y + H - t * .7f}, {x, y + H - t * .2f}})},
        {'b', poly({{x + w, y + t}, {x + w, y + H - t * .2f}, {x + w - t, y + H - t * .7f}, {x + w - t, y + t * 1.5f}})},
        {'e', poly({{x, y + H + t * .2f}, {x + t, y + H + t * .7f}, {x + t, y + h - t * 1.5f}, {x, y + h - t}})},
        {'c', poly({{x + w, y + H + t * .2f}, {x + w, y + h - t}, {x + w - t, y + h - t * 1.5f}, {x + w - t, y + H + t * .7f}})}};
    for (const auto& s : all) { g.setColour(on(s.id) ? INK : DIM); g.fillPath(s.p); }
}

static void icon(Graphics& g, int k, float cx, float cy) {
    g.setColour(juce::Colours::white);
    const juce::PathStrokeType st(5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    auto stroke = [&](const Path& p, float w = 5.5f) { g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); };
    auto line = [&](float x1, float y1, float x2, float y2, float w = 5.5f) { Path p; p.startNewSubPath(cx + x1, cy + y1); p.lineTo(cx + x2, cy + y2); stroke(p, w); };
    auto quad = [&](float x1, float y1, float qx, float qy, float x2, float y2, bool close, bool fill) {
        Path p; p.startNewSubPath(cx + x1, cy + y1); p.quadraticTo(cx + qx, cy + qy, cx + x2, cy + y2); if (close) p.closeSubPath(); if (fill) g.fillPath(p); else stroke(p); };
    switch (k) {
        case 12: { g.fillEllipse(cx - 8, cy + 14, 16, 16);
            for (float r : {26.0f, 46.0f, 66.0f}) { Path p; p.addCentredArc(cx, cy + 22, r, r, 0, -0.89f, 0.89f, true); stroke(p); } break; }
        case 13: { Path p; p.addEllipse(cx - 46, cy - 42, 68, 84); stroke(p);
            Path s; s.startNewSubPath(cx - 12, cy - 42); s.lineTo(cx + 26, cy - 42); s.addCentredArc(cx + 26, cy, 34, 42, 0, 0, juce::MathConstants<float>::pi, false); s.lineTo(cx - 12, cy + 42); stroke(s);
            Path in; in.addEllipse(cx - 32, cy - 28, 40, 56); stroke(in, 4); g.fillEllipse(cx + 17, cy + 11, 18, 18); line(26, 20, 26, 42, 4); break; }
        case 14: { Path p; p.addEllipse(cx - 52, cy - 32, 104, 28); stroke(p);
            line(-52, -18, -52, 16); line(52, -18, 52, 16); Path b; b.addCentredArc(cx, cy + 16, 52, 14, 0, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3, true); stroke(b);
            for (float dx : {-30.0f, 0.0f, 30.0f}) line(dx, -4, dx, 26, 3);
            break; }
        case 15: line(0, -44, 0, 44); quad(-58, -12, 0, -34, 58, -12, true, true); quad(-58, 12, 0, 34, 58, 12, true, true); line(-40, 44, 40, 44); break;
        case 16: { line(0, -40, 0, 40); Path p; p.startNewSubPath(cx - 58, cy); p.quadraticTo(cx, cy - 26, cx + 58, cy); p.quadraticTo(cx, cy + 26, cx - 58, cy); p.closeSubPath(); g.fillPath(p);
            g.setColour(Colour(0xff1d6fc4)); line(-58, 0, 58, 0, 3); g.setColour(juce::Colours::white); line(-40, 40, 40, 40); break; }
        case 17: quad(-66, -16, 0, -46, 66, -16, true, true); line(0, -30, 0, 46); line(-24, 46, 24, 46); break;
        case 18: { for (int s : {-1, 1}) { const float x0 = (float) s * 14;
                Path p; p.startNewSubPath(cx + x0, cy + 44); p.lineTo(cx + x0, cy - 6); p.quadraticTo(cx + x0, cy - 40, cx + x0 + s * 10, cy - 40);
                p.lineTo(cx + x0 + s * 30, cy - 40); p.quadraticTo(cx + x0 + s * 40, cy - 40, cx + x0 + s * 40, cy - 26); p.lineTo(cx + x0 + s * 40, cy + 44); stroke(p, 4.5f);
                for (float f : {10.0f, 20.0f, 30.0f}) line(x0 + s * f, -40, x0 + s * f, -8, 3); }
            line(-70, -30, -52, -18, 4); line(-74, 0, -54, 0, 4); line(70, -30, 52, -18, 4); line(74, 0, 54, 0, 4); break; }
        default: break;
    }
    juce::ignoreUnused(st);
}

Faceplate::Faceplate(DR110Processor& p) : proc(p) { startTimerHz(30); }

static float knobNorm(DR110Processor& p, int k) {
    if (k == 0) return (float) (std::log(juce::jlimit(40.0f, 250.0f, p.hostBpm.load()) / 40.0) / std::log(250.0 / 40.0));
    const char* ids[] = {"", "balance", "accent", "volume"};
    return p.apvts.getParameter(ids[k])->getValue();
}

void Faceplate::paint(Graphics& g) {
    using namespace layout;
    g.addTransform(juce::AffineTransform::scale((float) getWidth() / 2000.0f, (float) getHeight() / 1160.0f));
    g.fillAll(Colour(0xfff3f3f4));
    g.setGradientFill(juce::ColourGradient(Colour(0xffececee), 0, 30, Colour(0xffd8d8db), 0, 1120, false));
    g.fillRoundedRectangle(8, 30, 1932, 1090, 22); g.setColour(Colour(0xffbdbdc1)); g.drawRoundedRectangle(8, 30, 1932, 1090, 22, 3);
    g.setColour(Colour(0xffe4e4e7)); g.fillPath(poly({{993, 45}, {1895, 62}, {1895, 640}, {993, 640}}));
    g.setColour(Colour(0xffe6e6e9)); g.fillRoundedRectangle(55, 645, 1840, 455, 12); g.setColour(Colour(0xffc7c7cb)); g.drawRoundedRectangle(55, 645, 1840, 455, 12, 2);
    // black display block, logo, LCD
    g.setGradientFill(juce::ColourGradient(Colour(0xff2c2c2c), 0, 45, Colour(0xff0f0f0f), 0, 640, false)); g.fillRect(10.0f, 45.0f, 983.0f, 595.0f);
    g.setColour(Colour(0xffe6e6e6)); g.drawRect(125.0f, 72.0f, 58.0f, 56.0f, 5.0f);
    txt(g, 154, 118, juce::String::fromUTF8("\xce\xa9"), 44, Colour(0xffe6e6e6));
    {   // outlined OMEGA
        juce::GlyphArrangement ga; ga.addFittedText(font(64, true), "OMEGA", 198, 126 - 64 * 0.82f, 255, 64 * 1.05f, juce::Justification::left, 1, 0.3f);
        Path p; ga.createPath(p); g.setColour(Colour(0xffe6e6e6)); g.strokePath(p, juce::PathStrokeType(4.5f)); }
    txt(g, 890, 137, "DR-110", 62, Colour(0xfff2f2f2), 1, 195);
    g.setColour(Colour(0xff1d1d1d)); g.fillRoundedRectangle(103, 150, 815, 452, 22); g.setColour(Colour(0xff3b3b3b)); g.drawRoundedRectangle(103, 150, 815, 452, 22, 4);
    g.setColour(Colour(0xff0d0d0d)); g.fillRoundedRectangle(150, 162, 720, 428, 10);
    g.setGradientFill(juce::ColourGradient(Colour(0xffd9e4c8), 0, 172, Colour(0xffc6d3b0), 0, 578, false)); g.fillRoundedRectangle(165, 172, 690, 406, 6);
    drawLcd(g);
    // top labels and the power switch (on: the plugin is always on)
    g.setColour(Colour(0xffcfcfd2)); g.fillRect(1297.0f, 44.0f, 20.0f, 16.0f); g.fillRect(1512.0f, 44.0f, 20.0f, 16.0f);
    g.setColour(Colour(0xff2a2a2a));
    for (float x : {1307.0f, 1522.0f, 1700.0f}) g.fillPath(poly({{x - 9, 58}, {x + 9, 58}, {x, 78}}));
    txt(g, 1313, 116, "ACC TRIG OUT", 24, TXT, 0, 150); txt(g, 1522, 116, "DC 9V", 25); txt(g, 1700, 112, juce::String::fromUTF8("ON\xe2\x80\x94OFF"), 25); txt(g, 1700, 138, "POWER", 25);
    g.setColour(Colour(0xff202020)); g.fillRoundedRectangle(1660, 26, 80, 30, 6); g.setColour(Colour(0xff9a9a9a)); g.fillRoundedRectangle(1702, 29, 34, 24, 4);
    txt(g, 1062, 203, "Omega Rhythm", 80, Colour(0xff1f6fc4), -1, 385, true, true);
    txt(g, 1475, 205, "GRAPHIC", 60, Colour(0xff2a2a2a), -1, 265, false);
    // knobs
    const char* kl[4][4] = {{"TEMPO", "0", "SLOW", "FAST"}, {"BALANCE", "0", "5", "5"}, {"ACCENT", "5", "0", "10"}, {"VOLUME", "5", "0", "10"}};
    for (int k = 0; k < 4; ++k) {
        const float cx = KNOBS[k].x, cy = KNOBS[k].y;
        txt(g, cx, 292, kl[k][0], 36, TXT, 0, (float) std::strlen(kl[k][0]) * 22); txt(g, cx - 2, 330, kl[k][1], 30);
        txt(g, cx + (k == 0 ? -72.0f : -52.0f), 494, kl[k][2], k == 0 ? 32.0f : 30.0f); txt(g, cx + (k == 0 ? 60.0f : 50.0f), 494, kl[k][3], k == 0 ? 32.0f : 30.0f);
        g.setColour(Colour(0xff2a2a2a));
        for (int i = 0; i <= 10; ++i) { const float a = juce::degreesToRadians(-135.0f + 27.0f * (float) i), r2 = i % 5 == 0 ? 96.0f : 92.0f;
            g.drawLine(cx + std::sin(a) * 80, cy - std::cos(a) * 80, cx + std::sin(a) * r2, cy - std::cos(a) * r2, 3); }
        g.setColour(juce::Colours::black.withAlpha(0.35f)); g.fillEllipse(cx - KR - 2, cy + 6 - KR - 2, (KR + 2) * 2, (KR + 2) * 2);
        g.setColour(Colour(0xff1a1a1a)); g.fillEllipse(cx - KR, cy - KR, KR * 2, KR * 2);
        g.setColour(Colour(0xff2f2f2f));
        for (int i = 0; i < 36; ++i) { const float a = juce::degreesToRadians((float) i * 10); g.drawLine(cx + std::cos(a) * (KR - 6), cy + std::sin(a) * (KR - 6), cx + std::cos(a) * KR, cy + std::sin(a) * KR, 3); }
        g.setGradientFill(juce::ColourGradient(Colour(0xff4a4a4a), cx - 20, cy - 25, Colour(0xff050505), cx + KR, cy + KR, true)); g.fillEllipse(cx - KR + 8, cy - KR + 8, (KR - 8) * 2, (KR - 8) * 2);
        const float a = juce::degreesToRadians(-135.0f + 270.0f * knobNorm(proc, k));
        g.setColour(Colour(0xffff7a1a));
        Path ptr; ptr.startNewSubPath(cx + std::sin(a) * 18, cy - std::cos(a) * 18); ptr.lineTo(cx + std::sin(a) * 50, cy - std::cos(a) * 50);
        g.strokePath(ptr, juce::PathStrokeType(7, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    for (int i = 0; i < 6; ++i) { const float y = 545 + (float) i * 15.5f;
        g.setGradientFill(juce::ColourGradient(Colour(0xffcfcfd2), 0, y, Colour(0xfff4f4f6), 0, y + 9, false)); g.fillRect(996.0f, y, 890.0f, 9.0f);
        g.setColour(Colour(0xffb5b5b8)); g.drawRect(996.0f, y, 890.0f, 9.0f, 1.2f); }
    // legends
    g.setColour(Colour(0xff2a5b8c));
    { Path p; p.startNewSubPath(68, 860); p.lineTo(68, 666); p.lineTo(1420, 658); p.lineTo(1420, 712); p.lineTo(1302, 712); p.lineTo(300, 716); p.lineTo(300, 876); p.lineTo(68, 876);
      g.strokePath(p, juce::PathStrokeType(2.5f)); }
    g.setColour(TXT);
    { Path p; p.startNewSubPath(472, 690); p.lineTo(472, 680); p.lineTo(540, 680); p.startNewSubPath(590, 680); p.lineTo(656, 680); p.lineTo(656, 690);
      p.startNewSubPath(748, 690); p.lineTo(748, 680); p.lineTo(822, 680); p.startNewSubPath(914, 680); p.lineTo(988, 680); p.lineTo(988, 690); g.strokePath(p, juce::PathStrokeType(2.5f)); }
    txt(g, 378, 708, juce::String::fromUTF8("I\xc2\xb7II"), 26, TXT, 0, 38);
    auto leg = [&](float x, float y, const char* s, float w) { txt(g, x, y, s, 24, TXT, 0, w); };
    leg(565, 688, "PLAY", 50); leg(502, 708, "SONG", 62); leg(627, 708, "PATTERN", 100);
    leg(868, 688, "WRITE", 70); leg(750, 708, "SONG", 62); leg(869, 708, "STEP", 54); leg(989, 708, "TAP", 42);
    leg(1096, 690, "12", 26); g.drawLine(1088, 712, 1128, 668, 2.5f); leg(1118, 710, "16", 26);
    leg(1225, 686, "PATTERN", 100); leg(1225, 708, "CLEAR", 72); leg(1340, 686, "MEASURE", 102); leg(1340, 708, "RESET", 70);
    leg(436, 892, "DOWN", 70); leg(592, 892, "MEASURE", 108); leg(726, 892, "UP", 32); leg(976, 886, "ENTER", 78); leg(1712, 884, "KNUCKLE", 104);
    txt(g, 1225, 884, "D.C", 28, TXT, 0, 0, true, true);
    { Path a; a.startNewSubPath(490, 884); a.lineTo(522, 884); a.startNewSubPath(502, 874); a.lineTo(490, 884); a.lineTo(502, 894);
      a.startNewSubPath(664, 884); a.lineTo(696, 884); a.startNewSubPath(684, 874); a.lineTo(696, 884); a.lineTo(684, 894); g.strokePath(a, juce::PathStrokeType(6)); }
    // keys
    const char* names[] = {"ACCENT", "BASS DRUM", "SNARE DRUM", "O.HIHAT", "C.HIHAT", "CYMBAL", "HAND CLAP"};
    for (int k = 0; k < NK; ++k) {
        const auto& K = KEYS[k];
        g.setColour(Colour(0xff0d0d0d)); g.fillRoundedRectangle(K.x - 7, K.y - 7, K.w + 14, K.h + 16, 9);
        const bool isDown = k == down || (k == K_SHIFT && (shiftHeld || shiftLatch)) || (k == K_ACC && accHeld);
        const float dy = isDown ? 4.0f : 0.0f, x = K.x, y = K.y + dy, cx = x + K.w / 2, cy = y + K.h / 2;
        if (isDown) g.setGradientFill(juce::ColourGradient(Colour(0xff1a62a8), 0, y, Colour(0xff14548f), 0, y + K.h, false));
        else g.setGradientFill(juce::ColourGradient(BLUE1, 0, y, BLUE2, 0, y + K.h, false));
        g.fillRoundedRectangle(x, y, K.w, K.h, 5); g.setColour(Colour(0xff0f4a85)); g.drawRoundedRectangle(x, y, K.w, K.h, 5, 2);
        g.setColour(juce::Colours::white.withAlpha(0.25f)); g.fillRoundedRectangle(x + 5, y + 4, K.w - 10, 6, 3);
        const juce::String id(K.id);
        if (id == "shift") txt(g, cx, cy + 12, "SHIFT", 34, juce::Colours::white);
        else if (id == "bank") { for (int i = 0; i < 4; ++i) txt(g, cx + (i % 2 ? 15.0f : -15.0f), cy + (i < 2 ? -8.0f : 30.0f), juce::String::charToString((juce::juce_wchar) ('A' + i)), 28, juce::Colours::white);
            g.setColour(juce::Colours::white); g.drawLine(cx, cy - 34, cx, cy + 36, 3); g.drawLine(cx - 30, cy + 1, cx + 30, cy + 1, 3); }
        else if (id.startsWith("n")) {
            juce::GlyphArrangement ga; ga.addFittedText(font(74, false, true), id.substring(1), cx - 30, cy + 26 - 74 * 0.82f, 60, 78, juce::Justification::centred, 1, 0.5f);
            Path p; ga.createPath(p); g.setColour(juce::Colours::white); g.strokePath(p, juce::PathStrokeType(3.5f)); }
        else if (id == "start" || id == "stop") {
            g.setColour(juce::Colours::white);
            if (id == "start") g.fillEllipse(cx - 17, cy - 45, 34, 34); else g.drawEllipse(cx - 17, cy - 45, 34, 34, 4);
            txt(g, cx, cy + 34, id.toUpperCase(), 34, juce::Colours::white); }
        else icon(g, k, cx, cy);
        if (k >= K_ACC) { const juce::String nm(names[k - K_ACC]); txt(g, K.x + K.w / 2, K.y + K.h + 54, nm, 36, TXT, 0, std::min((float) nm.length() * 21, K.w + 40)); }
    }
}

void Faceplate::drawLcd(Graphics& g) {
    using namespace layout;
    const int step = proc.hostStep.load();
    for (int i = 0; i < 16; ++i) {
        const float x = NX0 + (float) i * NDX;
        if (i == step) { g.setColour(INK); g.fillRect(x - 15, NY - 26, 30.0f, 32.0f); txt(g, x, NY, juce::String(i + 1), 25, Colour(0xffcfdcbb)); }
        else txt(g, x, NY, juce::String(i + 1), 25, INK);
    }
    g.setColour(INK); g.drawRect(LX, GY, GX - LX - 4, CH * 5, 3.0f);
    const char* rows[] = {"C.HIHAT", "O.HIHAT", "SNARE DRUM", "BASS DRUM"};
    for (int r = 0; r < 5; ++r) {
        const float y = GY + (float) r * CH;
        if (r) { g.setColour(INK); g.drawLine(LX, y, GX - 4, y, 2.5f); }
        if (r < 4) txt(g, LX + 7, y + CH - 8, rows[r], 21, INK, -1, r >= 2 ? 120.0f : 90.0f);
        else { g.setColour(INK); g.drawLine(LX + 42, y + CH - 7, LX + 52, y + 8, 4); g.drawLine(LX + 82, y + CH - 7, LX + 92, y + 8, 4); }
    }
    g.setColour(INK); g.drawRect(GX, GY, CW * 16, CH * 5, 3.0f);
    for (int i = 1; i < 16; ++i) g.drawLine(GX + (float) i * CW, GY, GX + (float) i * CW, GY + CH * 5, 1.6f);
    for (int r = 1; r < 5; ++r) g.drawLine(GX, GY + (float) r * CH, GX + CW * 16, GY + (float) r * CH, 1.6f);
    const int map[5][2] = {{8, 0}, {4, 16}, {2, 32}, {1, 64}, {128, 0}};
    for (int i = 0; i < 16; ++i) for (int r = 0; r < 5; ++r) {
        const int bits = proc.grid[(size_t) i].load(); const float x = GX + (float) i * CW, y = GY + (float) r * CH;
        const bool a = bits & map[r][0], b = map[r][1] && (bits & map[r][1]);
        if (a) { g.setColour(INK); g.fillRect(x + 4, y + 4, CW - 8, CH - 8); }
        if (b) { if (a) { g.setColour(Colour(0xffcfdcbb)); g.fillRect(x + 10, y + 10, CW - 20, CH - 20); } else { g.setColour(INK); g.drawRect(x + 6, y + 6, CW - 12, CH - 12, 3.5f); } }
    }
    g.setColour(INK);
    g.drawRoundedRectangle(172, 418, 286, 154, 3, 3); g.drawLine(222, 418, 222, 572, 3); g.drawLine(172, 442, 458, 442, 2.5f);
    g.drawRoundedRectangle(470, 418, 135, 154, 3, 3); g.drawLine(523, 418, 523, 572, 3); g.drawLine(470, 442, 605, 442, 2.5f);
    g.drawRoundedRectangle(617, 418, 231, 154, 3, 3); g.drawLine(670, 418, 670, 572, 3); g.drawLine(617, 442, 670, 442, 2.5f);
    txt(g, 197, 436, "SONG", 15, INK); txt(g, 340, 436, "MEASURE", 15, INK); txt(g, 497, 436, "BANK", 15, INK); txt(g, 564, 436, "RHYTHM", 15, INK); txt(g, 644, 436, "MODE", 15, INK);
    // MEASURE: the DAW's bar
    const juce::String bar = juce::String(proc.hostBar.load() % 1000).paddedLeft(' ', 3);
    for (int k = 0; k < 3; ++k) seg7(g, 252 + (float) k * 66, 466, 46, 86, (char) bar[k]);
    seg7(g, 478, 466, 38, 86, 'd'); seg7(g, 542, 466, 44, 86, 'A');      // "dA": DAW
    const bool fl = juce::Time::getMillisecondCounter() < flashUntil;
    txt(g, 760, 488, fl ? flash.upToFirstOccurrenceOf("\n", false, false) : juce::String("DAW SYNC"), 27, INK, 0, 160);
    txt(g, 760, 522, fl ? flash.fromFirstOccurrenceOf("\n", false, false) : juce::String(proc.hostBpm.load(), 1) + " BPM", 27, INK, 0, 160);
    if (shiftHeld || shiftLatch) txt(g, 644, 560, "SHIFT", 13, INK);
    if (proc.hostPlaying.load()) { g.setColour(INK); g.fillPath(poly({{632, 466}, {632, 500}, {660, 483}})); }
}

int Faceplate::keyAt(juce::Point<float> p) const {
    for (int k = 0; k < layout::NK; ++k) { const auto& K = layout::KEYS[k]; if (Rectangle<float>(K.x - 7, K.y - 7, K.w + 14, K.h + 16).contains(p)) return k; }
    return -1;
}
int Faceplate::knobAt(juce::Point<float> p) const {
    for (int k = 1; k < 4; ++k) if (p.getDistanceFrom({layout::KNOBS[k].x, layout::KNOBS[k].y}) < layout::KR + 20) return k;
    return -1;
}
void Faceplate::press(int k) {
    const bool sh = shiftHeld || shiftLatch;
    if (k == layout::K_SHIFT) { shiftHeld = true; shiftUsed = false; return; }
    if (k == layout::K_ACC) { accHeld = true; return; }
    if (sh) { shiftUsed = true; shiftLatch = false; }
    if (k >= layout::K_V0) {
        int v = k - layout::K_V0;
        if (sh) { if (v == 5) v = 6; else if (v != 4) { flash = "SONG / MEASURE\nIN THE DAW"; flashUntil = juce::Time::getMillisecondCounter() + 1400; return; } }
        proc.hit(v, accHeld);
        return;
    }
    flash = "SEQUENCER\nIS THE DAW"; flashUntil = juce::Time::getMillisecondCounter() + 1400;
}
void Faceplate::mouseDown(const juce::MouseEvent& e) {
    const auto p = toPlate(e.position);
    const int k = keyAt(p);
    if (k >= 0) { down = k; press(k); repaint(); return; }
    dragKnob = knobAt(p);
    if (dragKnob > 0) { const char* ids[] = {"", "balance", "accent", "volume"}; auto* prm = proc.apvts.getParameter(ids[dragKnob]); dragV0 = prm->getValue(); dragY = e.position.y; prm->beginChangeGesture(); }
    else if (p.getDistanceFrom({layout::KNOBS[0].x, layout::KNOBS[0].y}) < layout::KR + 20) { flash = "TEMPO\nFROM THE DAW"; flashUntil = juce::Time::getMillisecondCounter() + 1400; }
}
void Faceplate::mouseDrag(const juce::MouseEvent& e) {
    if (dragKnob <= 0) return;
    const char* ids[] = {"", "balance", "accent", "volume"};
    proc.apvts.getParameter(ids[dragKnob])->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, dragV0 + (dragY - e.position.y) / ((float) getHeight() * 0.35f)));
}
void Faceplate::mouseUp(const juce::MouseEvent&) {
    if (dragKnob > 0) { const char* ids[] = {"", "balance", "accent", "volume"}; proc.apvts.getParameter(ids[dragKnob])->endChangeGesture(); }
    dragKnob = -1;
    if (down == layout::K_SHIFT) { shiftHeld = false; if (!shiftUsed) shiftLatch = !shiftLatch; }
    if (down == layout::K_ACC) accHeld = false;
    down = -1; repaint();
}
void Faceplate::mouseDoubleClick(const juce::MouseEvent& e) {
    const int k = knobAt(toPlate(e.position));
    if (k > 0) { const char* ids[] = {"", "balance", "accent", "volume"}; auto* prm = proc.apvts.getParameter(ids[k]); prm->setValueNotifyingHost(prm->getDefaultValue()); }
}

/* ---------------- knobs and buttons for the panels ---------------- */
void KnobLook::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    const float r = (float) std::min(w, h) / 2 - 4, cx = (float) x + (float) w / 2, cy = (float) y + (float) h / 2;
    g.setColour(Colour(0xff2a2a2a));
    for (int i = 0; i <= 10; ++i) { const float a = a0 + (a1 - a0) * (float) i / 10; g.drawLine(cx + std::sin(a) * r * 0.86f, cy - std::cos(a) * r * 0.86f, cx + std::sin(a) * r, cy - std::cos(a) * r, 1.5f); }
    const float kr = r * 0.78f;
    g.setGradientFill(juce::ColourGradient(Colour(0xff3c3c3c), cx - kr * 0.4f, cy - kr * 0.5f, Colour(0xff050505), cx + kr, cy + kr, true));
    g.fillEllipse(cx - kr, cy - kr, kr * 2, kr * 2);
    const float a = a0 + pos * (a1 - a0);
    Path p; p.startNewSubPath(cx + std::sin(a) * kr * 0.25f, cy - std::cos(a) * kr * 0.25f); p.lineTo(cx + std::sin(a) * kr * 0.9f, cy - std::cos(a) * kr * 0.9f);
    g.setColour(pointer); g.strokePath(p, juce::PathStrokeType(std::max(2.5f, kr * 0.16f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
void KnobLook::drawButtonBackground(Graphics& g, juce::Button& b, const Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.5f);
    const bool on = b.getToggleState();
    g.setColour(Colour(0xff0d0d0d)); g.fillRoundedRectangle(r, 5);
    r = r.reduced(2.5f).translated(0, down ? 1.5f : 0);
    if (on) g.setGradientFill(juce::ColourGradient(Colour(0xffffa040), 0, r.getY(), Colour(0xffe06a10), 0, r.getBottom(), false));
    else g.setGradientFill(juce::ColourGradient(over ? Colour(0xff4aa0ee) : BLUE1, 0, r.getY(), BLUE2, 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, 4);
}
void KnobLook::drawButtonText(Graphics& g, juce::TextButton& b, bool, bool) {
    g.setColour(b.getToggleState() ? Colour(0xff111111) : juce::Colours::white);
    g.setFont(font(std::min(15.0f, (float) b.getHeight() * 0.42f)));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 2), juce::Justification::centred, 2, 0.6f);
}

/* ---------------- editor ---------------- */
DR110Editor::DR110Editor(DR110Processor& p) : juce::AudioProcessorEditor(p), proc(p), plate(p) {
    lookWhite.pointer = Colour(0xfff4f4f4);
    addAndMakeVisible(plate);
    const char* tn[] = {"VOICE MOD", "MIXER", "REV 7 / EFFECTS"};
    for (int i = 0; i < 3; ++i) { tabs[i].setButtonText(tn[i]); tabs[i].setLookAndFeel(&look); tabs[i].setClickingTogglesState(false); tabs[i].onClick = [this, i] { showTab(i); }; addAndMakeVisible(tabs[i]); }
    // design units for the panels: x 0..2000, y 0..MH under the plate; boxes are 300 wide
    auto R = [](int x, int y, int w, int h) { return Rectangle<int>(x, y, w, h); };
    // ---- VOICE MOD (the boxed sections of the mod panel)
    const int bw = 318, k = 118, top = 120;
    const int bx[] = {20, 350, 680, 1010, 1340};
    boxes.push_back({"Kick", 0, R(bx[0], 80, bw, 680)}); boxes.push_back({"Snare", 0, R(bx[1], 80, bw, 680)}); boxes.push_back({"HiHat", 0, R(bx[2], 80, bw, 680)});
    boxes.push_back({"Cymbal", 0, R(bx[3], 80, bw, 250)}); boxes.push_back({"TRIG SWEEP (EXT / ACCENT)", 0, R(bx[3], 345, bw, 415)}); boxes.push_back({"Clap", 0, R(bx[4], 80, bw, 680)});
    auto col = [&](int b, int row) { return R(bx[b] + 20, top + row * 140, bw - 40, k); };
    addKnob(0, "BDtune", "PITCH", col(0, 0), true); addKnob(0, "bdSnap", "SNAP", col(0, 1), true); addKnob(0, "BDdec", "DECAY", col(0, 2), true);
    addTog(0, "bdBoost", "DECAY BOOST", R(bx[0] + 40, top + 430, bw - 80, 50)); addTog(0, "bdBass", "BASS BOOST", R(bx[0] + 40, top + 495, bw - 80, 50));
    addKnob(0, "SDtune", "PITCH", col(1, 0), true); addKnob(0, "sdSnap", "SNAP", col(1, 1), true); addKnob(0, "SDdec", "DECAY", col(1, 2), true);
    addTog(0, "sdBoost", "DECAY BOOST", R(bx[1] + 40, top + 430, bw - 80, 50)); addKnob(0, "sdMix", "MIX", R(bx[1] + 20, top + 495, bw - 40, k), true);
    addKnob(0, "CHtune", "PITCH (CH)", R(bx[2] + 10, top, 145, k), true); addKnob(0, "OHtune", "PITCH (OH)", R(bx[2] + 163, top, 145, k), true);
    addKnob(0, "hhFilter", "FILTER", col(2, 1), true); addKnob(0, "CHdec", "CH DECAY", R(bx[2] + 10, top + 280, 145, k), true); addKnob(0, "OHdec", "OH DECAY", R(bx[2] + 163, top + 280, 145, k), true);
    addKnob(0, "hhMix", "MIX", R(bx[2] + 20, top + 495, bw - 40, k), true);
    addKnob(0, "cyFilter", "FILTER", R(bx[3] + 10, top, 145, k), true); addKnob(0, "CYdec", "DECAY", R(bx[3] + 163, top, 145, k), true);
    addTog(0, "swTarget", "BD PITCH / CH DECAY", R(bx[3] + 30, 400, bw - 60, 50));
    addKnob(0, "swDecay", "DECAY", R(bx[3] + 10, 475, 145, k), true); addKnob(0, "swAmount", "AMOUNT", R(bx[3] + 163, 475, 145, k), true);
    sweepBtn.setLookAndFeel(&look); sweepBtn.onClick = [this] { proc.sweepNow(); }; addAndMakeVisible(sweepBtn);
    addKnob(0, "HCtune", "PITCH", col(4, 0), true); addKnob(0, "hcFilter", "FILTER", col(4, 1), true); addKnob(0, "HCdec", "DECAY", col(4, 2), true);
    addTog(0, "hcBoost", "DECAY BOOST", R(bx[4] + 40, top + 430, bw - 80, 50));
    boxes.push_back({"POWER / KNUCKLE", 0, R(1670, 80, 310, 680)});
    addTog(0, "supply", "BATTERIES 5.4 V", R(1700, top, 250, 50)); addTog(0, "src", "RECORDINGS", R(1700, top + 65, 250, 50));
    addKnob(0, "KNtune", "KNUCKLE PITCH", R(1690, top + 140, 270, k), true); addKnob(0, "KNdec", "KNUCKLE DECAY", R(1690, top + 280, 270, k), true);
    addKnob(0, "accVel", "ACCENT VELOCITY", R(1690, top + 420, 270, k), true);
    // ---- MIXER: a strip per voice
    for (int v = 0; v < NV; ++v) {
        const int x = 20 + v * 283;
        boxes.push_back({VOICE_NAMES[v], 1, R(x, 80, 270, 680)});
        const juce::String id(VOICES[v]);
        addKnob(1, id + "lv", "LEVEL", R(x + 10, 125, 250, 130), false);
        addKnob(1, id + "pan", "PAN", R(x + 10, 265, 120, 110), false); addKnob(1, id + "snd", "REV SEND", R(x + 140, 265, 120, 110), false);
        addKnob(1, id + "tune", "TUNE", R(x + 10, 390, 120, 110), false); addKnob(1, id + "dec", "DECAY", R(x + 140, 390, 120, 110), false);
        addTog(1, id + "mute", "MUTE", R(x + 40, 560, 190, 60));
    }
    // ---- REV 7 and the machine's own knobs
    boxes.push_back({"REV 7 DIGITAL REVERBERATOR", 2, R(20, 80, 1420, 680)});
    addKnob(2, "rvProg", "PROGRAM", R(50, 140, 220, 200), false); addKnob(2, "rvTime", "REV TIME", R(290, 140, 220, 200), false);
    addKnob(2, "rvHigh", "HIGH", R(530, 140, 220, 200), false); addKnob(2, "rvDelay", "DELAY", R(770, 140, 220, 200), false);
    addKnob(2, "rvDiff", "DIFFUSION", R(1010, 140, 220, 200), false); addKnob(2, "rvEr", "ER LEVEL", R(290, 400, 220, 200), false);
    addKnob(2, "rvCut", "HIGH CUT", R(530, 400, 220, 200), false); addKnob(2, "rvLevel", "RETURN", R(770, 400, 220, 200), false);
    boxes.push_back({"MASTER", 2, R(1460, 80, 520, 680)});
    addKnob(2, "balance", "BALANCE", R(1480, 140, 230, 200), false); addKnob(2, "accent", "ACCENT", R(1730, 140, 230, 200), false);
    addKnob(2, "volume", "VOLUME", R(1605, 400, 230, 200), false);
    showTab(0);
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double) W / (PH + MH));
    setResizeLimits(700, (int) (700.0 * (PH + MH) / W), 2400, (int) (2400.0 * (PH + MH) / W));
    setSize(1100, (int) (1100.0 * (PH + MH) / W));
}
DR110Editor::~DR110Editor() {
    for (auto& t : tabs) t.setLookAndFeel(nullptr);
    sweepBtn.setLookAndFeel(nullptr);
    for (auto& k : knobs) k.s->setLookAndFeel(nullptr);
    for (auto& t : togs) t.b->setLookAndFeel(nullptr);
}
void DR110Editor::addKnob(int t, const juce::String& id, const juce::String& label, Rectangle<int> r, bool white) {
    Knob k; k.s = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    k.s->setLookAndFeel(white ? &lookWhite : &look);
    k.s->setColour(juce::Slider::textBoxTextColourId, TXT); k.s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.s->setRotaryParameters(juce::degreesToRadians(-135.0f) + juce::MathConstants<float>::twoPi, juce::degreesToRadians(135.0f) + juce::MathConstants<float>::twoPi, true);
    k.s->setNumDecimalPlacesToDisplay(2);
    k.a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, *k.s);
    // read-outs: semitones, decay as a factor, seconds, ms, Hz, or two decimals
    if (id == "rvProg") k.s->textFromValueFunction = [](double v) { return juce::String(RV_PROGS[juce::jlimit(0, 4, (int) std::round(v))]); };
    else if (id.endsWith("tune")) k.s->textFromValueFunction = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " st"; };
    else if (id.endsWith("dec")) k.s->textFromValueFunction = [](double v) { return juce::String::fromUTF8("\xc3\x97") + juce::String(v, 2); };
    else if (id == "rvTime") k.s->textFromValueFunction = [](double v) { return juce::String(v, 1) + " s"; };
    else if (id == "rvDelay") k.s->textFromValueFunction = [](double v) { return juce::String((int) std::round(v)) + " ms"; };
    else if (id == "rvCut") k.s->textFromValueFunction = [](double v) { return juce::String(v / 1000.0, 1) + " kHz"; };
    else if (id == "accVel" || id == "rvDiff") k.s->textFromValueFunction = [](double v) { return juce::String((int) std::round(v)); };
    else if (id.endsWith("pan")) k.s->textFromValueFunction = [](double v) { return std::abs(v) < 0.005 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String((int) std::round(std::abs(v) * 100)); };
    else k.s->textFromValueFunction = [](double v) { return juce::String(v, 2); };
    k.s->updateText();
    k.label = label; k.tab = t; k.r = r;
    addAndMakeVisible(*k.s);
    knobs.push_back(std::move(k));
}
void DR110Editor::addTog(int t, const juce::String& id, const juce::String& label, Rectangle<int> r) {
    Tog g; g.b = std::make_unique<juce::TextButton>(label); g.b->setClickingTogglesState(true); g.b->setLookAndFeel(&look);
    g.a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, *g.b);
    g.tab = t; g.r = r; addAndMakeVisible(*g.b); togs.push_back(std::move(g));
}
void DR110Editor::showTab(int t) {
    tab = t;
    for (int i = 0; i < 3; ++i) tabs[i].setToggleState(i == t, juce::dontSendNotification);
    for (auto& k : knobs) k.s->setVisible(k.tab == t);
    for (auto& g : togs) g.b->setVisible(g.tab == t);
    sweepBtn.setVisible(t == 0);
    repaint();
}
void DR110Editor::paint(Graphics& g) {
    const float s = (float) getWidth() / (float) W;
    g.fillAll(Colour(0xff2b2b2d));
    g.saveState();
    g.addTransform(juce::AffineTransform::scale(s).translated(0, (float) plate.getBottom()));
    g.setGradientFill(juce::ColourGradient(Colour(0xffe9e9ec), 0, 0, Colour(0xffd7d7da), 0, (float) MH, false));
    g.fillRect(0, 0, W, MH);
    for (const auto& b : boxes) if (b.tab == tab) {
        g.setColour(Colour(0xff2a2a2a)); g.drawRoundedRectangle(b.r.toFloat(), 14, 3);
        txt(g, (float) b.r.getCentreX(), (float) b.r.getY() + 34, b.title, b.title.length() > 12 ? 22.0f : 34.0f, TXT, 0, std::min((float) b.r.getWidth() - 20, (float) b.title.length() * (b.title.length() > 12 ? 13.0f : 22.0f)));
    }
    for (const auto& k : knobs) if (k.tab == tab) txt(g, (float) k.r.getCentreX(), (float) k.r.getBottom() + 18, k.label, 17, TXT, 0, std::min((float) k.r.getWidth(), (float) k.label.length() * 10.5f));
    g.restoreState();
}
void DR110Editor::resized() {
    const float s = (float) getWidth() / (float) W;
    plate.setBounds(0, 0, getWidth(), (int) std::round(PH * s));
    const int y0 = plate.getBottom();
    auto sc = [&](Rectangle<int> r) { return Rectangle<int>((int) std::round(r.getX() * s), y0 + (int) std::round(r.getY() * s), (int) std::round(r.getWidth() * s), (int) std::round(r.getHeight() * s)); };
    for (int i = 0; i < 3; ++i) tabs[i].setBounds(sc({20 + i * 330, 12, 320, 56}));
    for (auto& k : knobs) { k.s->setBounds(sc(k.r)); k.s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, (int) (k.r.getWidth() * s), std::max(12, (int) (24 * s))); }
    for (auto& g : togs) g.b->setBounds(sc(g.r));
    sweepBtn.setBounds(sc({1010 + 90, 690, 140, 50}));
}

} // namespace dr110
