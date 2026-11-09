#include "DR110Editor.h"
#include <map>

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
    txt(g, 890, 137, "110", 66, Colour(0xfff2f2f2), 1, 118);
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
// an EQ curve from the engine's own biquads: bands {type, freq, gain dB, q}
static void drawEq(Graphics& g, Rectangle<float> r, const std::vector<std::array<double, 4>>& bands) {
    g.setColour(Colour(0xff141a12)); g.fillRoundedRectangle(r, 8);
    g.setColour(Colour(0x267cff8a));
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
    g.setColour(Colour(0xff7cff8a)); g.strokePath(p, juce::PathStrokeType(3.5f));
}
static void drawAdsr(Graphics& g, Rectangle<float> r, double a, double d, double s, double rel) {
    g.setColour(Colour(0xff141a12)); g.fillRoundedRectangle(r, 8);
    const bool hold = rel >= 3.999; const double dd = std::min(d, 1.5), total = a + dd + 0.4 + (hold ? 0.6 : std::min(rel, 2.0));
    auto X = [&](double t) { return r.getX() + 10 + (float) (t / total) * (r.getWidth() - 20); };
    auto Y = [&](double v) { return r.getBottom() - 10 - (float) v * (r.getHeight() - 20); };
    Path p; p.startNewSubPath(X(0), Y(a > 0 ? 0 : 1)); p.lineTo(X(a), Y(1));
    for (int i = 1; i <= 30; ++i) { const double t = dd * i / 30; p.lineTo(X(a + t), Y(s + (1 - s) * std::exp(-t / (d / 3)))); }
    const double sv = s + (1 - s) * std::exp(-dd / (d / 3)), t0 = a + dd + 0.4; p.lineTo(X(t0), Y(sv));
    if (hold) p.lineTo(X(total), Y(sv));
    else { const double rr = std::min(rel, 2.0); for (int i = 1; i <= 30; ++i) { const double t = rr * i / 30; p.lineTo(X(t0 + t), Y(sv * std::exp(-t / (rel / 3)))); } }
    g.setColour(Colour(0xff7cff8a)); g.strokePath(p, juce::PathStrokeType(3.5f));
    txt(g, X(t0) + 6, r.getY() + 26, hold ? "release: hold" : "release", 18, Colour(0x997cff8a), -1);
}

DR110Editor::DR110Editor(DR110Processor& p) : juce::AudioProcessorEditor(p), proc(p), plate(p) {
    lookWhite.pointer = Colour(0xfff4f4f4);
    addAndMakeVisible(plate);
    const char* tn[NT] = {"VOICE MOD", "MIXER", "PAD EQ / ADSR", "REV 7 / TC DELAY", "MASTER"};
    for (int i = 0; i < NT; ++i) { tabs[i].setButtonText(tn[i]); tabs[i].setLookAndFeel(&look); tabs[i].onClick = [this, i] { showTab(i); }; addAndMakeVisible(tabs[i]); }
    // presets and the dice
    for (auto* b : {&prevBtn, &nextBtn, &diceBtn}) { b->setLookAndFeel(&look); addAndMakeVisible(*b); }
    prevBtn.onClick = [this] { proc.setCurrentProgram((proc.getCurrentProgram() + kPresets - 1) % kPresets); };
    nextBtn.onClick = [this] { proc.setCurrentProgram((proc.getCurrentProgram() + 1) % kPresets); };
    diceBtn.onClick = [this] { proc.dice(partBox.getSelectedItemIndex()); };
    fillPresets();
    presetBox.onChange = [this] { const int id = presetBox.getSelectedId(); if (id > 0 && id - 1 != proc.getCurrentProgram()) proc.setCurrentProgram(id - 1); };
    const char* parts[] = {"Everything", "Voices + mods", "Pad EQ", "ADSR + drive", "Reverb + delay", "Master EQ + drive"};
    for (int i = 0; i < 6; ++i) partBox.addItem(parts[i], i + 1);
    partBox.setSelectedItemIndex(0, juce::dontSendNotification);
    for (auto* c : {&presetBox, &partBox}) {
        c->setColour(juce::ComboBox::backgroundColourId, Colour(0xff141a12)); c->setColour(juce::ComboBox::textColourId, Colour(0xff7cff8a));
        c->setColour(juce::ComboBox::outlineColourId, juce::Colours::black); c->setColour(juce::ComboBox::arrowColourId, Colour(0xff7cff8a));
        addAndMakeVisible(*c);
    }
    proc.onProgram = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<DR110Editor>(this)] { if (sp) sp->presetBox.setSelectedId(sp->proc.getCurrentProgram() + 1, juce::dontSendNotification); }); };

    auto R = [](int x, int y, int w, int h) { return Rectangle<int>(x, y, w, h); };
    // ---- VOICE MOD
    const int bw = 318, k = 118, top = 70;
    const int bx[] = {20, 350, 680, 1010, 1340};
    for (int i : {0, 1, 2, 4}) boxes.push_back({i == 0 ? "Kick" : i == 1 ? "Snare" : i == 2 ? "HiHat" : "Clap", 0, R(bx[i], 30, bw, 680)});
    boxes.push_back({"Cymbal", 0, R(bx[3], 30, bw, 250)}); boxes.push_back({"TRIG SWEEP (EXT / ACCENT)", 0, R(bx[3], 295, bw, 415)});
    auto col = [&](int b, int row) { return R(bx[b] + 20, top + row * 140, bw - 40, k); };
    auto half = [&](int b, int row, int side) { return R(bx[b] + 10 + side * 153, top + row * 140, 145, k); };
    addKnob(0, "BDtune", "PITCH", half(0, 0, 0), true); addKnob(0, "bdSnap", "SNAP", half(0, 0, 1), true);
    addKnob(0, "BDdec", "DECAY", half(0, 1, 0), true); addKnob(0, "bdExtend", "BASS EXTEND", half(0, 1, 1), true);
    addTog(0, "bdBoost", "DECAY BOOST", R(bx[0] + 40, top + 290, bw - 80, 50)); addTog(0, "bdBass", "BASS BOOST", R(bx[0] + 40, top + 355, bw - 80, 50));
    addTog(0, "bdEnv", "ENV SHAPE", R(bx[0] + 40, top + 420, bw - 80, 50));
    addKnob(0, "SDtune", "PITCH", col(1, 0), true); addKnob(0, "sdSnap", "SNAP", col(1, 1), true); addKnob(0, "SDdec", "DECAY", col(1, 2), true);
    addTog(0, "sdBoost", "DECAY BOOST", R(bx[1] + 40, top + 430, bw - 80, 50)); addKnob(0, "sdMix", "MIX", R(bx[1] + 20, top + 495, bw - 40, k), true);
    addKnob(0, "CHtune", "PITCH (CH)", half(2, 0, 0), true); addKnob(0, "OHtune", "PITCH (OH)", half(2, 0, 1), true);
    addKnob(0, "hhFilter", "FILTER", col(2, 1), true); addKnob(0, "CHdec", "CH DECAY", half(2, 2, 0), true); addKnob(0, "OHdec", "OH DECAY", half(2, 2, 1), true);
    addKnob(0, "hhMix", "MIX", R(bx[2] + 20, top + 495, bw - 40, k), true);
    addKnob(0, "cyFilter", "FILTER", half(3, 0, 0), true); addKnob(0, "CYdec", "DECAY", half(3, 0, 1), true);
    addTog(0, "swTarget", "BD PITCH / CH DECAY", R(bx[3] + 30, 350, bw - 60, 50));
    addKnob(0, "swDecay", "DECAY", R(bx[3] + 10, 425, 145, k), true); addKnob(0, "swAmount", "AMOUNT", R(bx[3] + 163, 425, 145, k), true);
    sweepBtn.setLookAndFeel(&look); sweepBtn.onClick = [this] { proc.sweepNow(); }; addAndMakeVisible(sweepBtn);
    addKnob(0, "HCtune", "PITCH", col(4, 0), true); addKnob(0, "hcFilter", "FILTER", col(4, 1), true); addKnob(0, "HCdec", "DECAY", col(4, 2), true);
    addTog(0, "hcBoost", "DECAY BOOST", R(bx[4] + 40, top + 430, bw - 80, 50));
    boxes.push_back({"POWER / KNUCKLE", 0, R(1670, 30, 310, 680)});
    addTog(0, "supply", "BATTERIES 5.4 V", R(1700, top, 250, 50)); addTog(0, "src", "RECORDINGS", R(1700, top + 65, 250, 50));
    addKnob(0, "KNtune", "KNUCKLE PITCH", R(1690, top + 140, 270, k), true); addKnob(0, "KNdec", "KNUCKLE DECAY", R(1690, top + 280, 270, k), true);
    addKnob(0, "accVel", "ACCENT VELOCITY", R(1690, top + 420, 270, k), true);
    // ---- MIXER
    for (int v = 0; v < NV; ++v) {
        const int x = 20 + v * 283;
        boxes.push_back({VOICE_NAMES[v], 1, R(x, 30, 270, 680)});
        const juce::String id(VOICES[v]);
        addKnob(1, id + "lv", "LEVEL", R(x + 10, 75, 250, 130), false);
        addKnob(1, id + "pan", "PAN", R(x + 10, 230, 120, 110), false); addKnob(1, id + "snd", "REV SEND", R(x + 140, 230, 120, 110), false);
        addKnob(1, id + "tune", "TUNE", R(x + 10, 370, 120, 110), false); addKnob(1, id + "dec", "DECAY", R(x + 140, 370, 120, 110), false);
        addKnob(1, id + "dls", "DELAY SEND", R(x + 75, 505, 120, 110), false);
        addTog(1, id + "mute", "MUTE", R(x + 40, 640, 190, 50));
    }
    // ---- PAD EQ / ADSR: the pad picked here
    for (int v = 0; v < NV; ++v) { padBtn[v].setButtonText(juce::String(VOICE_NAMES[v]).toUpperCase()); padBtn[v].setLookAndFeel(&look); padBtn[v].onClick = [this, v] { selectPad(v); }; addAndMakeVisible(padBtn[v]); }
    boxes.push_back({"PARAMETRIC EQ", 2, R(20, 100, 1080, 610)}); boxes.push_back({"ADSR / DRIVE / DELAY SEND", 2, R(1120, 100, 860, 610)});
    const char* eqs[][2] = {{"lsF", "LOW FREQ"}, {"lsG", "LOW GAIN"}, {"pkF", "MID FREQ"}, {"pkG", "MID GAIN"}, {"pkQ", "MID Q"}, {"hsF", "HIGH FREQ"}, {"hsG", "HIGH GAIN"}};
    for (int i = 0; i < 7; ++i) addKnob(2, "", eqs[i][1], R(40 + i * 150, 480, 140, 150), false, eqs[i][0]);
    const char* envs[][2] = {{"atk", "ATTACK"}, {"dcy", "DECAY"}, {"sus", "SUSTAIN"}, {"rel", "RELEASE"}, {"drv", "DRIVE"}, {"dls", "DELAY SEND"}};
    for (int i = 0; i < 6; ++i) addKnob(2, "", envs[i][1], R(1135 + i * 140, 480, 130, 150), false, envs[i][0]);
    // ---- REV 7 / TC DELAY
    boxes.push_back({"REV 7 DIGITAL REVERBERATOR", 3, R(20, 30, 1000, 680)});
    const char* rv[][2] = {{"rvProg", "PROGRAM"}, {"rvTime", "REV TIME"}, {"rvHigh", "HIGH"}, {"rvDelay", "DELAY"}, {"rvDiff", "DIFFUSION"}, {"rvEr", "ER LEVEL"}, {"rvCut", "HIGH CUT"}, {"rvLevel", "RETURN"}};
    for (int i = 0; i < 8; ++i) addKnob(3, rv[i][0], rv[i][1], R(40 + (i % 4) * 240, 100 + (i / 4) * 300, 220, 200), false);
    boxes.push_back({"TC 2290-STYLE DYNAMIC DELAY", 3, R(1040, 30, 940, 680)});
    const char* dl[][2] = {{"dlTime", "TIME"}, {"dlSync", "SYNC"}, {"dlFb", "FEEDBACK"}, {"dlHiCut", "HIGH CUT"}, {"dlLoCut", "LOW CUT"}, {"dlModRate", "MOD RATE"},
        {"dlModDepth", "MOD DEPTH"}, {"dlDuck", "DUCKING"}, {"dlLevel", "RETURN"}};
    for (int i = 0; i < 9; ++i) addKnob(3, dl[i][0], dl[i][1], R(1060 + (i % 3) * 300, 85 + (i / 3) * 195, 280, 140), false);
    addTog(3, "dlPing", "PING-PONG", R(1360, 650, 300, 50));
    // ---- MASTER
    boxes.push_back({"MASTER PARAMETRIC EQ", 4, R(20, 30, 1200, 680)});
    const char* meq[][2] = {{"mLsF", "LOW FREQ"}, {"mLsG", "LOW GAIN"}, {"mP1F", "MID 1 FREQ"}, {"mP1G", "MID 1 GAIN"}, {"mP1Q", "MID 1 Q"},
        {"mP2F", "MID 2 FREQ"}, {"mP2G", "MID 2 GAIN"}, {"mP2Q", "MID 2 Q"}, {"mHsF", "HIGH FREQ"}, {"mHsG", "HIGH GAIN"}};
    for (int i = 0; i < 10; ++i) addKnob(4, meq[i][0], meq[i][1], R(40 + (i % 5) * 235, 330 + (i / 5) * 190, 220, 140), false);
    boxes.push_back({"OVERDRIVE / CLIPPER", 4, R(1240, 30, 740, 330)});
    const char* odk[][2] = {{"odDrive", "DRIVE"}, {"odMode", "MODE"}, {"odMix", "MIX"}, {"odOut", "OUTPUT"}};
    for (int i = 0; i < 4; ++i) addKnob(4, odk[i][0], odk[i][1], R(1255 + i * 180, 90, 170, 200), false);
    boxes.push_back({"MACHINE", 4, R(1240, 380, 740, 330)});
    const char* mk[][2] = {{"balance", "BALANCE"}, {"accent", "ACCENT"}, {"volume", "VOLUME"}};
    for (int i = 0; i < 3; ++i) addKnob(4, mk[i][0], mk[i][1], R(1270 + i * 235, 440, 220, 200), false);
    selectPad(0);
    showTab(0);
    // EXPAND / FOLD: the whole machine, or the front panel alone (remembered with the session)
    foldBtn.setLookAndFeel(&look); foldBtn.onClick = [this] { setFolded(!folded); }; addAndMakeVisible(foldBtn);
    setResizable(true, true);
    setFolded((bool) proc.apvts.state.getProperty("folded", false));
    startTimerHz(15);
}
DR110Editor::~DR110Editor() {
    stopTimer();
    proc.onProgram = nullptr;
    for (auto& t : tabs) t.setLookAndFeel(nullptr);
    for (auto& b : padBtn) b.setLookAndFeel(nullptr);
    for (auto* b : {&sweepBtn, &prevBtn, &nextBtn, &diceBtn, &foldBtn}) b->setLookAndFeel(nullptr);
    for (auto& k : knobs) { k.a.reset(); k.s->setLookAndFeel(nullptr); }
    for (auto& t : togs) t.b->setLookAndFeel(nullptr);
}
void DR110Editor::setFolded(bool f) {
    folded = f;
    proc.apvts.state.setProperty("folded", f, nullptr);
    foldBtn.setButtonText(f ? "EXPAND" : "FOLD");
    for (int i = 0; i < NT; ++i) tabs[i].setVisible(!f);
    for (auto* c : std::initializer_list<juce::Component*>{&prevBtn, &nextBtn, &diceBtn, &presetBox, &partBox}) c->setVisible(!f);
    if (f) { for (auto& k : knobs) k.s->setVisible(false); for (auto& g : togs) g.b->setVisible(false); for (auto& b : padBtn) b.setVisible(false); sweepBtn.setVisible(false); }
    else showTab(tab);
    const int h = f ? PH : PH + MH, w = getWidth() > 0 ? getWidth() : 1100;
    getConstrainer()->setFixedAspectRatio((double) W / h);
    setResizeLimits(700, (int) (700.0 * h / W), 2400, (int) (2400.0 * h / W));
    setSize(w, (int) std::round((double) w * h / W));
}
void DR110Editor::fillPresets() {
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
DR110Editor::Knob& DR110Editor::addKnob(int t, const juce::String& id, const juce::String& label, Rectangle<int> r, bool white, const juce::String& suffix) {
    Knob k; k.s = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    k.s->setLookAndFeel(white ? &lookWhite : &look);
    k.s->setColour(juce::Slider::textBoxTextColourId, TXT); k.s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
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
    else if (ends("tune")) fn = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " st"; };
    else if (ends("dec")) fn = [](double v) { return juce::String::fromUTF8("\xc3\x97") + juce::String(v, 2); };
    else if (f == "rvTime") fn = [](double v) { return juce::String(v, 1) + " s"; };
    else if (f == "rvDelay" || f == "dlTime") fn = [](double v) { return juce::String((int) std::round(v)) + " ms"; };
    else if (ends("F") || ends("Cut")) fn = hz;
    else if (ends("G") || f == "odOut") fn = dB;
    else if (f == "dlModRate") fn = [](double v) { return juce::String(v, 2) + " Hz"; };
    else if (ends("atk") || ends("dcy")) fn = secs;
    else if (ends("rel")) fn = [secs](double v) { return v >= 3.999 ? juce::String("HOLD") : secs(v); };
    else if (f == "sus" || ends("drv") || ends("dls") || ends("snd") || ends("sus") || f == "dlFb" || f == "dlModDepth" || f == "dlDuck" || f == "dlLevel" || f == "odDrive" || f == "odMix" || f == "bdExtend") fn = pct;
    else if (f == "accVel" || f == "rvDiff") fn = [](double v) { return juce::String((int) std::round(v)); };
    else if (ends("pan")) fn = [](double v) { return std::abs(v) < 0.005 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String((int) std::round(std::abs(v) * 100)); };
    k.s->textFromValueFunction = fn; k.fmt = fn;
    k.s->updateText();
    k.label = label; k.suffix = suffix; k.tab = t; k.r = r.translated(0, TOP);
    addAndMakeVisible(*k.s);
    knobs.push_back(std::move(k));
    return knobs.back();
}
void DR110Editor::addTog(int t, const juce::String& id, const juce::String& label, Rectangle<int> r, const juce::String& suffix) {
    Tog g; g.b = std::make_unique<juce::TextButton>(label); g.b->setClickingTogglesState(true); g.b->setLookAndFeel(&look);
    g.a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, *g.b);
    g.tab = t; g.r = r.translated(0, TOP); g.suffix = suffix; addAndMakeVisible(*g.b); togs.push_back(std::move(g));
}
void DR110Editor::selectPad(int v) {
    pad = v;
    for (int i = 0; i < NV; ++i) padBtn[i].setToggleState(i == v, juce::dontSendNotification);
    for (auto& k : knobs) if (k.suffix.isNotEmpty()) {
        k.a.reset();
        k.a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, juce::String(VOICES[v]) + k.suffix, *k.s);
        k.s->textFromValueFunction = k.fmt;          // the attachment brings the parameter's own text: put ours back
        k.s->updateText();
    }
    repaint();
}
void DR110Editor::showTab(int t) {
    tab = t;
    if (folded) return;
    for (int i = 0; i < NT; ++i) tabs[i].setToggleState(i == t, juce::dontSendNotification);
    for (auto& k : knobs) k.s->setVisible(k.tab == t);
    for (auto& g : togs) g.b->setVisible(g.tab == t);
    for (auto& b : padBtn) b.setVisible(t == 2);
    sweepBtn.setVisible(t == 0);
    repaint();
}
// the curves follow the knobs (and presets / the dice)
void DR110Editor::timerCallback() {
    if (tab != 2 && tab != 4) return;
    juce::uint32 h = 7;
    for (int i = 0; i < NP; ++i) h = h * 31 + (juce::uint32) (proc.value(i) * 1000);
    if (h != lastParamsHash) { lastParamsHash = h; repaint(); }
}
void DR110Editor::paint(Graphics& g) {
    const float s = (float) getWidth() / (float) W;
    g.fillAll(Colour(0xff2b2b2d));
    if (folded) return;
    g.saveState();
    g.addTransform(juce::AffineTransform::scale(s).translated(0, (float) plate.getBottom()));
    g.setGradientFill(juce::ColourGradient(Colour(0xffe9e9ec), 0, 0, Colour(0xffd7d7da), 0, (float) MH, false));
    g.fillRect(0, 0, W, MH);
    g.setColour(Colour(0xff1d1d1f)); g.fillRoundedRectangle(14, 80, W - 28, 58, 10);
    for (const auto& b : boxes) if (b.tab == tab) {
        const auto r = b.r.translated(0, TOP).toFloat();
        g.setColour(Colour(0xff2a2a2a)); g.drawRoundedRectangle(r, 14, 3);
        txt(g, r.getCentreX(), r.getY() + 34, b.title, b.title.length() > 12 ? 22.0f : 34.0f, TXT, 0, std::min(r.getWidth() - 20, (float) b.title.length() * (b.title.length() > 12 ? 13.0f : 22.0f)));
    }
    for (const auto& k : knobs) if (k.tab == tab) txt(g, (float) k.r.getCentreX(), (float) k.r.getBottom() + 18, k.label, 17, TXT, 0, std::min((float) k.r.getWidth(), (float) k.label.length() * 10.5f));
    static const std::map<juce::String, int> index = [] { std::map<juce::String, int> m; const auto ps = params(); for (int i = 0; i < NP; ++i) m[juce::String(ps[(size_t) i].id)] = i; return m; }();
    auto v = [this](const juce::String& id) { const auto it = index.find(id); return it == index.end() ? 0.0 : proc.value(it->second); };
    if (tab == 2) {
        const juce::String P(VOICES[pad]);
        drawEq(g, Rectangle<float>(40, TOP + 150, 1040, 300), {{0, v(P + "lsF"), v(P + "lsG"), 0.707}, {1, v(P + "pkF"), v(P + "pkG"), v(P + "pkQ")}, {2, v(P + "hsF"), v(P + "hsG"), 0.707}});
        drawAdsr(g, Rectangle<float>(1140, TOP + 150, 820, 300), v(P + "atk"), v(P + "dcy"), v(P + "sus"), v(P + "rel"));
    } else if (tab == 4)
        drawEq(g, Rectangle<float>(40, TOP + 80, 1160, 220), {{0, v("mLsF"), v("mLsG"), 0.707}, {1, v("mP1F"), v("mP1G"), v("mP1Q")}, {1, v("mP2F"), v("mP2G"), v("mP2Q")}, {2, v("mHsF"), v("mHsG"), 0.707}});
    g.restoreState();
}
void DR110Editor::resized() {
    const float s = (float) getWidth() / (float) W;
    plate.setBounds(0, 0, getWidth(), (int) std::round(PH * s));
    foldBtn.setBounds((int) std::round(1720 * s), (int) std::round(1112 * s), (int) std::round(230 * s), (int) std::round(46 * s));
    const int y0 = plate.getBottom();
    auto sc = [&](Rectangle<int> r) { return Rectangle<int>((int) std::round(r.getX() * s), y0 + (int) std::round(r.getY() * s), (int) std::round(r.getWidth() * s), (int) std::round(r.getHeight() * s)); };
    for (int i = 0; i < NT; ++i) tabs[i].setBounds(sc({20 + i * 392, 12, 382, 56}));
    prevBtn.setBounds(sc({24, 86, 70, 46})); presetBox.setBounds(sc({100, 86, 900, 46})); nextBtn.setBounds(sc({1006, 86, 70, 46}));
    diceBtn.setBounds(sc({1110, 86, 200, 46})); partBox.setBounds(sc({1320, 86, 330, 46}));
    for (int v = 0; v < NV; ++v) padBtn[v].setBounds(sc({20 + v * 283, TOP + 30, 270, 56}));
    for (auto& k : knobs) { k.s->setBounds(sc(k.r)); k.s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, (int) (k.r.getWidth() * s), std::max(12, (int) (24 * s))); }
    for (auto& g : togs) g.b->setBounds(sc(g.r));
    sweepBtn.setBounds(sc({1010 + 90, TOP + 640, 140, 50}));
}

} // namespace dr110
