#include "XR10Editor.h"
#include "BinaryData.h"

namespace xr10 {
using namespace xr;

static const juce::Colour kPanel(0xffe4e3ea), kInk(0xff26262c), kSub(0xff5c5b66), kKey(0xfff4f3f7), kSel(0xff3c5f94), kOn(0xffd2335a),
    kLcd(0xff2f3b42), kLcdText(0xffb4c8c6), kGhost(0xff9cb0d0), kBg(0xffcfcdd6);
static const int BASE_W = 1000, FACE_H = 686, PANEL_H = 480, BASE_H = FACE_H + PANEL_H, FOLD_W = 660;
static const char* const PART_LONG[NPARTS] = {"VAR A", "VAR B", "VAR C", "FILL A", "FILL B", "FILL C", "INTRO", "ENDING"};

static juce::Font mono(float h) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold)); }
static juce::Font sans(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }

// ============================================================ the faceplate
Face::Face(Processor& p) : proc(p) {
    plate = juce::ImageCache::getFromMemory(BinaryData::faceplate_jpg, BinaryData::faceplate_jpgSize);
    auto add = [&](int id, float x, float y, float w, float h, bool pad = false) { hots.push_back({id, x, y, w, h, pad}); };
    const int cur[6][2] = {{B_LEFT, 479}, {B_RIGHT, 532}, {B_UP, 584}, {B_DOWN, 636}, {B_MINUS, 687}, {B_PLUS, 727}};
    for (auto& c : cur) add(c[0], (float) c[1], 292, c[1] > 670 ? 34.f : 42.f, 17);
    const int kx[5] = {427, 479, 532, 584, 636};
    for (int d = 0; d < 10; d++) add(B_D0 + d, (float) kx[d % 5], d < 5 ? 328.f : 357.f, 42, 17);
    add(B_ENTER, 707, 327, 73, 17);
    const float colX[8] = {90, 177, 265, 354, 442, 532, 619, 707}, rowY[3] = {405, 454, 503};
    const int layout[8][3] = {{B_MODE, B_EDIT, B_PADBANK}, {B_ERASE, B_ACCENT, B_TC},
        {B_PAD1 + 2, B_PAD1 + 1, B_PAD1}, {B_PAD1 + 5, B_PAD1 + 4, B_PAD1 + 3}, {B_PAD1 + 8, B_PAD1 + 7, B_PAD1 + 6},
        {B_PAD1 + 11, B_PAD1 + 10, B_PAD1 + 9}, {B_PAD1 + 14, B_PAD1 + 13, B_PAD1 + 12}, {B_INTRO, B_BREAK, B_START}};
    for (int c = 0; c < 8; c++) for (int r = 0; r < 3; r++) add(layout[c][r], colX[c], rowY[r], 73, 37, layout[c][r] >= B_PAD1 && layout[c][r] <= B_PAD15);
    add(B_MODE_PATTERN, 497, 210, 35, 15); add(B_MODE_SONG, 532, 210, 35, 15); add(B_MODE_SOUND, 497, 231, 35, 15); add(B_MODE_UTILITY, 532, 231, 35, 15);
}
juce::Rectangle<float> Face::photo(float x, float y, float w, float h) const { return {(x - w / 2) * sx(), (y - h / 2) * sy(), w * sx(), h * sy()}; }

void Face::paint(juce::Graphics& g) {
    g.drawImage(plate, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    if (!snap) return;
    const auto& st = snap->st;
    // LCD
    auto lcd = juce::Rectangle<float>(581 * sx(), 205 * sy(), 136 * sx(), 33 * sy());
    g.setColour(kLcd);
    g.fillRect(lcd);
    const float cw = lcd.getWidth() / 16.4f, ch = lcd.getHeight() / 2.15f;
    g.setFont(mono(ch * 0.98f));
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 16; i++) {
            const float x = lcd.getX() + cw * 0.2f + cw * i, y = lcd.getY() + lcd.getHeight() * (row == 0 ? 0.04f : 0.5f);
            g.setColour(kLcdText.withAlpha(0.07f));
            g.fillRect(x + cw * 0.06f, y + ch * 0.04f, cw * 0.88f, ch * 0.92f);
            g.setColour(kLcdText);
            g.drawText(juce::String::charToString((juce::juce_wchar) (unsigned char) st.lcd[row][i]), juce::Rectangle<float>(x, y, cw, ch), juce::Justification::centred, false);
        }
    // mode lights
    const float led[4][2] = {{497, 210}, {532, 210}, {497, 231}, {532, 231}};
    for (int i = 0; i < 4; i++) {
        auto r = photo(led[i][0], led[i][1], 23, 5);
        if (st.mode == i) { g.setColour(juce::Colour(0x66ff3d6e)); g.fillRoundedRectangle(r.expanded(3), 3); g.setColour(juce::Colour(0xffff3d6e)); }
        else g.setColour(juce::Colour(0xff4d5159));
        g.fillRoundedRectangle(r, 1.5f);
    }
    // knob caps with pointers
    auto knob = [&](float cx, float cy, float angle) {
        const float r = 33.5f * sx();
        juce::Point<float> c(cx * sx(), cy * sy());
        juce::ColourGradient gr(juce::Colour(0xffffffff), c.x - r * 0.3f, c.y - r * 0.4f, juce::Colour(0xffbfc2d0), c.x + r, c.y + r, true);
        g.setGradientFill(gr);
        g.fillEllipse(c.x - r, c.y - r, r * 2, r * 2);
        juce::Path p;
        p.addRoundedRectangle(-r * 0.06f, -r * 0.92f, r * 0.12f, r * 0.36f, r * 0.05f);
        g.setColour(juce::Colour(0xff24427a));
        g.fillPath(p, juce::AffineTransform::rotation(angle).translated(c));
    };
    knob(123, 217, juce::degreesToRadians(-135.f + volume * 2.7f));
    knob(252, 217, juce::degreesToRadians(dataAngle));
    // lit keys and the key being pressed
    auto lit = [&](int id) {
        const int part = st.part;
        switch (id) {
            case B_ACCENT: return st.accent != 0;
            case B_EDIT: return st.recording != 0;
            case B_START: return st.playing != 0;
            case B_ERASE: return st.erase != 0;
            case B_INTRO: return part >= P_INTRO && st.playing;
            case B_PAD1 + 11: return !st.recording && st.mode < 2 && part == P_VARA;
            case B_PAD1 + 10: return !st.recording && st.mode < 2 && part == P_VARB;
            case B_PAD1 + 9: return !st.recording && st.mode < 2 && part == P_VARC;
            case B_PAD1 + 14: return !st.recording && st.mode < 2 && part == P_FILLA;
            case B_PAD1 + 13: return !st.recording && st.mode < 2 && part == P_FILLB;
            case B_PAD1 + 12: return !st.recording && st.mode < 2 && part == P_FILLC;
        }
        return false;
    };
    for (auto& h : hots) {
        auto r = photo(h.x, h.y, h.w, h.h);
        if (lit(h.id)) { g.setColour(kOn); g.drawRoundedRectangle(r.reduced(1), 4, 2.f); }
        if (h.id == pressed) { g.setColour(juce::Colour(0x48282840)); g.fillRoundedRectangle(r, 4); }
    }
}
void Face::mouseDown(const juce::MouseEvent& e) {
    const float x = e.position.x / sx(), y = e.position.y / sy();
    lastX = e.position.x; lastY = e.position.y; acc = 0;
    if (juce::Point<float>(x - 123, y - 217).getDistanceFromOrigin() < 42) { dragKnob = 0; return; }
    if (juce::Point<float>(x - 252, y - 217).getDistanceFromOrigin() < 42) { dragKnob = 1; return; }
    for (auto& h : hots)
        if (std::abs(x - h.x) <= h.w / 2 + 2 && std::abs(y - h.y) <= h.h / 2 + 2) {
            pressed = h.id;
            if (h.pad) {
                const int vel = juce::roundToInt(127 - 70 * juce::jlimit(0.f, 1.f, (y - (h.y - h.h / 2)) / h.h));
                proc.with([&](Machine& m) { m.pad(h.id - B_PAD1, 1, vel); });
            } else proc.with([&](Machine& m) { m.button(h.id, 1); });
            repaint();
            return;
        }
}
void Face::mouseDrag(const juce::MouseEvent& e) {
    if (dragKnob < 0) return;
    const float d = (lastY - e.position.y) + (e.position.x - lastX);
    lastX = e.position.x; lastY = e.position.y;
    if (dragKnob == 0) {
        volume = juce::jlimit(0.f, 100.f, volume + d * 0.4f);
        proc.with([&](Machine& m) { m.put(K_VOLUME, juce::roundToInt(volume)); });
    } else {
        acc += d;
        while (std::abs(acc) >= 6) {
            const int s = acc > 0 ? 1 : -1;
            acc -= (float) s * 6;
            dataAngle += (float) s * 12;
            proc.with([&](Machine& m) { m.knob(s); });
        }
    }
    repaint();
}
void Face::mouseUp(const juce::MouseEvent&) {
    if (pressed >= 0) {
        const int id = pressed;
        proc.with([&](Machine& m) { if (id >= B_PAD1 && id <= B_PAD15) m.pad(id - B_PAD1, 0); else m.button(id, 0); });
    }
    pressed = -1; dragKnob = -1;
    repaint();
}
void Face::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const float x = e.position.x / sx(), y = e.position.y / sy();
    const int s = w.deltaY > 0 ? 1 : -1;
    if (juce::Point<float>(x - 123, y - 217).getDistanceFromOrigin() < 42) { volume = juce::jlimit(0.f, 100.f, volume + s * 3.f); proc.with([&](Machine& m) { m.put(K_VOLUME, juce::roundToInt(volume)); }); }
    else if (juce::Point<float>(x - 252, y - 217).getDistanceFromOrigin() < 42) { dataAngle += (float) s * 12; proc.with([&](Machine& m) { m.knob(s); }); }
    repaint();
}

// ============================================================ editor
static void styleButton(juce::TextButton& b, bool on = false) {
    b.setColour(juce::TextButton::buttonColourId, on ? kSel : kKey);
    b.setColour(juce::TextButton::textColourOffId, on ? juce::Colours::white : kInk);
    b.setColour(juce::TextButton::buttonOnColourId, kSel);
    b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}
static void styleBox(juce::ComboBox& c) {
    c.setColour(juce::ComboBox::backgroundColourId, kKey);
    c.setColour(juce::ComboBox::textColourId, kInk);
    c.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff9c9aa6));
    c.setColour(juce::ComboBox::arrowColourId, kSub);
}
static void styleSlider(juce::Slider& s, double lo, double hi) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    s.setRange(lo, hi, 1);
    s.setColour(juce::Slider::textBoxTextColourId, kInk);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::thumbColourId, kSel);
    s.setColour(juce::Slider::trackColourId, kGhost);
    s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xffc9c7d2));
}

Editor::Editor(Processor& p) : AudioProcessorEditor(p), proc(p), face(p) {
    setLookAndFeel(&look);
    face.snap = &snap;
    content.addAndMakeVisible(face);
    const char* tabNames[5] = {"SOUNDS", "STEP GRID", "SONG", "SETUP", "EFFECTS"};
    for (int i = 0; i < 5; i++) {
        tabs[i].setButtonText(tabNames[i]);
        tabs[i].onClick = [this, i] { showTab(i); };
        content.addAndMakeVisible(tabs[i]);
        content.addChildComponent(pages[i]);
    }
    for (auto* b : {&play, &rec, &tap, &diceBeat, &diceSound}) { styleButton(*b); content.addAndMakeVisible(*b); }
    diceBeat.setTooltip("Make a new beat in a free user pattern");
    diceSound.setTooltip("A new take on the selected sound");
    diceBeat.onClick = [this] { const auto seed = (u32) juce::Random::getSystemRandom().nextInt(); proc.with([&](Machine& m) { m.randomBeat(seed); }); refreshPatterns(); };
    diceSound.onClick = [this] { const auto seed = (u32) juce::Random::getSystemRandom().nextInt(); proc.with([&](Machine& m) { m.randomSound(seed); }); waveFor = -1; };
    play.onClick = [this] { proc.with([](Machine& m) { m.put(K_PLAYING, !m.playing); }); };
    rec.onClick = [this] { proc.with([](Machine& m) { m.put(K_REC, !m.recording); }); refreshPatterns(); };
    tap.onClick = [this] {
        static juce::Array<double> taps;
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (int i = taps.size(); --i >= 0;) if (now - taps[i] > 2500) taps.remove(i);
        taps.add(now);
        if (taps.size() >= 2) { const double iv = (taps.getLast() - taps[0]) / (taps.size() - 1); proc.with([&](Machine& m) { m.put(K_TEMPO, juce::roundToInt(600000.0 / iv)); }); }
    };
    styleSlider(tempo, 40, 296);
    tempo.setSliderStyle(juce::Slider::IncDecButtons);
    tempo.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 52, 24);
    tempo.setColour(juce::Slider::textBoxBackgroundColourId, kKey);
    tempo.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff9c9aa6));
    tempo.onValueChange = [this] { if (!updating) proc.with([&](Machine& m) { m.put(K_TEMPO, (int) tempo.getValue() * 10); }); };
    tempoLabel.setColour(juce::Label::textColourId, kSub);
    content.addAndMakeVisible(tempo); content.addAndMakeVisible(tempoLabel);
    styleBox(patternBox);
    patternBox.onChange = [this] { if (!updating && patternBox.getSelectedId() > 0) proc.with([&](Machine& m) { m.selectPattern(patternBox.getSelectedId() - 1); }); };
    content.addAndMakeVisible(patternBox);
    buildVoice(); buildGrid(); buildSong(); buildSetup();
    // the effects page: the effect controls (built with the setup page) and a note on how the sends work
    pages[4].painter = [](juce::Graphics& g) { g.fillAll(kPanel); };
    for (juce::Component* c : {(juce::Component*) &fxType, (juce::Component*) &fxTime, (juce::Component*) &fxLevel, (juce::Component*) &effectVr,
                               (juce::Component*) &setupLabels[0], (juce::Component*) &setupLabels[1], (juce::Component*) &setupLabels[2], (juce::Component*) &setupLabels[3]})
        pages[4].addAndMakeVisible(c);
    fxHelp.setText("Each sound goes to the effect by its own FX SEND amount (SOUNDS page); snares, claps and toms have some to start with. "
                   "The XR10 itself has no built-in effect: it sends to an outboard unit from its EFFECT jack. Choose External to work that way: "
                   "the plugin's FX Send output carries the send.", juce::dontSendNotification);
    fxHelp.setColour(juce::Label::textColourId, kSub);
    fxHelp.setJustificationType(juce::Justification::topLeft);
    pages[4].addAndMakeVisible(fxHelp);
    addAndMakeVisible(content);
    refreshPatterns();
    proc.snapshot(snap, 0);
    face.volume = (float) snap.keys[K_VOLUME];
    const float startScale = juce::jlimit(0.4f, 2.0f, proc.editorScale);
    setResizable(true, true);
    // the fold-out: a mixer and the effects rack, opening beside the machine
    fold.reset(new FoldPanel(proc, snap));
    fold->mixer.onMainVolume = [this](float v) { face.volume = v; face.repaint(); };
    content.addChildComponent(*fold);
    styleButton(foldBtn);
    foldBtn.onClick = [this] { setFold(!proc.foldOpen); };
    content.addAndMakeVisible(foldBtn);
    setResizeLimits(baseW() * 4 / 10, BASE_H * 4 / 10, baseW() * 2, BASE_H * 2);
    getConstrainer()->setFixedAspectRatio((double) baseW() / BASE_H);
    setSize(juce::roundToInt(baseW() * startScale), juce::roundToInt(BASE_H * startScale));
    showTab(juce::jlimit(0, 3, proc.editorTab));
    startTimerHz(30);
}
Editor::~Editor() { stopTimer(); setLookAndFeel(nullptr); }

void Editor::paint(juce::Graphics& g) { g.fillAll(kBg); }
void Editor::resized() {
    if (getWidth() <= 0) return;
    const float s = (float) getWidth() / baseW();
    proc.editorScale = s;
    content.setBounds(0, 0, baseW(), BASE_H);
    fold->setVisible(proc.foldOpen);
    fold->setBounds(BASE_W, 0, FOLD_W, BASE_H);
    content.setTransform(juce::AffineTransform::scale(s));
    face.setBounds(0, 0, BASE_W, FACE_H);
    int y = FACE_H + 8;
    play.setBounds(12, y, 120, 30); rec.setBounds(138, y, 90, 30); tap.setBounds(234, y, 50, 30);
    diceBeat.setBounds(476, y, 100, 30); diceSound.setBounds(580, y, 104, 30);
    tempoLabel.setBounds(304, y, 56, 30); tempo.setBounds(356, y, 120, 30);
    patternBox.setBounds(BASE_W - 304, y, 292, 30);
    y += 38;
    // the effects live in the fold-out now (the rack), so the old effects page has no tab
    for (int k = 0; k < 4; k++) tabs[k].setBounds(12 + k * 200, y, 194, 28);
    tabs[4].setVisible(false);
    foldBtn.setBounds(12 + 4 * 200, y, 176, 28);
    foldBtn.setButtonText(proc.foldOpen ? "< FOLD IN" : "FOLD-OUT >");
    styleButton(foldBtn, proc.foldOpen);
    y += 34;
    for (auto& pg : pages) pg.setBounds(12, y, BASE_W - 24, BASE_H - y - 8);
    // voices page
    {
        auto& pg = pages[0];
        const int iw = 60, ih = 26;
        for (int i = 0; i < NSOUNDS; i++) inst[i].setBounds(8 + (i % 8) * (iw + 3), 8 + (i / 8) * (ih + 3), iw, ih);
        const int x0 = 8 + 8 * (iw + 3) + 12, w = pg.getWidth() - x0 - 8;
        voiceTitle.setBounds(x0, 4, w, 22);
        for (int p = 0; p < NVP; p++) {
            const int col = p % 2, row = p / 2, cx = x0 + col * (w / 2), cy = 30 + row * 25 + (p >= NVP_XR10 ? 6 : 0);
            paramLabels[p].setBounds(cx, cy, 70, 22);
            params[p].setBounds(cx + 70, cy, w / 2 - 78, 22);
        }
        waveArea = {x0, 30 + 8 * 25 + 10, w, 50};
        const int by = waveArea.getBottom() + 6;
        loadSmp.setBounds(x0, by, 110, 26); loadSet.setBounds(x0 + 114, by, 140, 26); romSmp.setBounds(x0 + 258, by, 84, 26); resetVoice.setBounds(x0 + 346, by, 90, 26);
        assignPad.setBounds(x0, by + 30, 200, 26);
    }
    // grid page
    {
        for (int i = 0; i < NPARTS; i++) partBtn[i].setBounds(8 + i * 72, 6, 68, 24);
        clearPart.setBounds(8 + 8 * 72 + 6, 6, 86, 24);
        midiImp.setBounds(8 + 8 * 72 + 96, 6, 92, 24); midiExp.setBounds(8 + 8 * 72 + 192, 6, 92, 24);
        midiDrag.setBounds(pages[1].getWidth() - 160, 34, 152, 24);
        int x = 8;
        for (int i = 0; i < 8; i++) barBtn[i].setBounds(x + i * 30, 34, 28, 24);
        x = 8 + 8 * 30 + 10;
        lenBox.setBounds(x, 34, 90, 24); beatsBox.setBounds(x + 96, 34, 66, 24); subBox.setBounds(x + 168, 34, 90, 24); swingBox.setBounds(x + 264, 34, 100, 24); bankBox.setBounds(x + 370, 34, 90, 24);
        gridArea = {8, 64, pages[1].getWidth() - 16, pages[1].getHeight() - 70};
    }
    // song page
    {
        songBox.setBounds(8, 6, 110, 26); songTempo.setBounds(124, 6, 200, 26); songAdd.setBounds(332, 6, 150, 26); songPlay.setBounds(488, 6, 100, 26);
        stepPattern.setBounds(8, 38, 190, 26); stepPart.setBounds(204, 38, 100, 26); stepRepeat.setBounds(310, 38, 160, 26); songDup.setBounds(476, 38, 110, 26); songDel.setBounds(592, 38, 100, 26);
        songMidi.setBounds(594, 6, 130, 26); songDrag.setBounds(730, 6, 200, 26);
        songArea = {8, 72, pages[2].getWidth() - 16, pages[2].getHeight() - 78};
    }
    fxHelp.setBounds(8, 76, pages[4].getWidth() - 16, 80);
    // setup page
    {
        juce::Component* rows[5][4] = {{&fxType, &fxTime, &fxLevel, &effectVr}, {&tcBox, &polyBox, &accVel, &padVel}, {&clickBox, &clickLvl, &countIn, nullptr},
                                       {&midiCh, &midiMap, &syncBox, nullptr}, {&exportBtn, &importBtn, &initBtn, nullptr}};
        int li = 0;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 4; c++)
                if (rows[r][c]) {
                    // the first row (the effect) lives on the effects page
                    const int x = 8 + c * 236, yy = 10 + (r == 0 ? 0 : r - 1) * 58;
                    if (li < 12 && setupLabels[li].getText().isNotEmpty()) setupLabels[li].setBounds(x, yy, 226, 18);
                    rows[r][c]->setBounds(x, yy + 20, 226, 26);
                    li++;
                }
    }
}

void Editor::showTab(int t) {
    tab = t; proc.editorTab = t;
    for (int i = 0; i < 5; i++) { pages[i].setVisible(i == t); styleButton(tabs[i], i == t); }
}

void Editor::buildVoice() {
    auto& pg = pages[0];
    pg.painter = [this](juce::Graphics& g) { g.fillAll(kPanel); paintWave(g, waveArea); };
    for (int i = 0; i < NSOUNDS; i++) {
        inst[i].setButtonText(SOUND_NAMES[i]);
        styleButton(inst[i]);
        inst[i].onStateChange = [this, i] {
            if (inst[i].isDown() && !inst[i].getProperties().getWithDefault("held", false)) {
                inst[i].getProperties().set("held", true);
                proc.with([&](Machine& m) { m.put(K_SOUND, i); m.hitSound(i, 110); });
            } else if (!inst[i].isDown()) inst[i].getProperties().set("held", false);
        };
        pg.addAndMakeVisible(inst[i]);
    }
    voiceTitle.setFont(sans(15, true)); voiceTitle.setColour(juce::Label::textColourId, kInk);
    pg.addAndMakeVisible(voiceTitle);
    for (int p = 0; p < NVP; p++) {
        paramLabels[p].setText(juce::String(VPARAMS[p].name).replace("*", p >= NVP_XR10 ? " (extra)" : ""), juce::dontSendNotification);
        paramLabels[p].setFont(sans(12)); paramLabels[p].setColour(juce::Label::textColourId, kSub);
        styleSlider(params[p], VPARAMS[p].lo, VPARAMS[p].hi);
        params[p].onValueChange = [this, p] { if (!updating) proc.with([&](Machine& m) { m.setParam(snap.st.sound, p, (int) params[p].getValue()); }); };
        params[p].onDragEnd = [this] { proc.with([&](Machine& m) { m.trigger(snap.st.sound, 110); }); };
        params[p].textFromValueFunction = [p](double v) {
            const int i = (int) v;
            if (p == V_PAN) return i == 0 ? juce::String("center") : (i < 0 ? "L" + juce::String(-i) : "R" + juce::String(i));
            if (p == V_SWPOL) return juce::String(i ? "down" : "up");
            if (p == V_REVERSE || p == V_LOOP || p == V_VELFEEL) return juce::String(i ? "on" : "off");
            if (p == V_TONE && i >= 99) return juce::String("open");
            if (p == V_GROUP && !i) return juce::String("off");
            if (p == V_TUNE) return (i > 0 ? "+" : "") + juce::String(i) + " st";
            if (p == V_FINE) return (i > 0 ? "+" : "") + juce::String(i * 6.25, 2) + "c";
            return juce::String(i);
        };
        params[p].updateText();
        pg.addAndMakeVisible(paramLabels[p]); pg.addAndMakeVisible(params[p]);
    }
    for (auto* b : {&loadSmp, &romSmp, &resetVoice}) { styleButton(*b); pg.addAndMakeVisible(*b); }
    loadSmp.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Load a sample onto " + juce::String(SOUND_NAMES[snap.st.sound]), juce::File(), proc.formats.getWildcardForAllFormats());
        const int s = snap.st.sound;
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, s](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (!f.existsAsFile()) return;
            auto err = proc.loadSample(s, f);
            if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Omega XR10", err);
            waveFor = -1;
            proc.with([&](Machine& m) { m.trigger(s, 110); });
        });
    };
    styleButton(loadSet); pg.addAndMakeVisible(loadSet);
    loadSet.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Pick your XR-10 sample folder (files named KICK1, HAT CL, TOM2 M...)", juce::File());
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& fc) {
                juce::Array<juce::File> files;
                for (auto& f : fc.getResults()) {
                    if (f.isDirectory()) files.addArray(f.findChildFiles(juce::File::findFiles, true));
                    else if (f.existsAsFile()) files.add(f);
                }
                if (files.isEmpty()) return;
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Omega XR10", proc.loadSampleSet(files));
                waveFor = -1;
            });
    };
    romSmp.onClick = [this] { proc.with([&](Machine& m) { m.clearSample(snap.st.sound); }); waveFor = -1; };
    resetVoice.onClick = [this] { proc.with([&](Machine& m) { m.defaultVoice(snap.st.sound); m.trigger(snap.st.sound, 110); }); };
    styleBox(assignPad);
    assignPad.setTextWhenNothingSelected("Put this sound on pad...");
    for (int i = 0; i < NPADS; i++) assignPad.addItem("Pad " + juce::String(i + 1), i + 1);
    assignPad.onChange = [this] {
        const int pd = assignPad.getSelectedId() - 1;
        if (pd >= 0) proc.with([&](Machine& m) { m.pads[m.set[K_BANK]][pd] = (u8) snap.st.sound; });
        assignPad.setSelectedId(0, juce::dontSendNotification);
    };
    pg.addAndMakeVisible(assignPad);
}

void Editor::paintWave(juce::Graphics& g, juce::Rectangle<int> r) {
    g.setColour(kLcd);
    g.fillRoundedRectangle(r.toFloat(), 5);
    if (wave.empty()) return;
    g.setColour(kLcdText);
    const int w = r.getWidth();
    const size_t per = juce::jmax<size_t>(1, wave.size() / (size_t) w);
    const float mid = (float) r.getCentreY(), half = r.getHeight() * 0.46f;
    for (int x = 0; x < w; x++) {
        float mn = 0, mx = 0;
        for (size_t i = (size_t) x * per; i < (size_t) (x + 1) * per && i < wave.size(); i++) { const float v = wave[i] / 32768.f; mn = juce::jmin(mn, v); mx = juce::jmax(mx, v); }
        g.drawVerticalLine(r.getX() + x, mid - mx * half, mid - mn * half + 1);
    }
    g.setFont(sans(11));
    g.drawText(juce::String(wave.size() / (double) xr::ROM_RATE, 2) + " s, 16-bit" + (snap.user[snap.st.sound] ? ", your sample" : ""), r.reduced(6, 2), juce::Justification::topLeft);
}

void Editor::buildGrid() {
    auto& pg = pages[1];
    pg.painter = [this](juce::Graphics& g) { g.fillAll(kPanel); paintGrid(g, gridArea); };
    pg.click = [this](const juce::MouseEvent& e) {
        if (!gridArea.contains(e.getPosition())) return;
        const int nameW = 110, steps = snap.steps;
        int rows = 0; while (rows < Machine::GRID_ROWS && snap.grid[Machine::GRID_ROWS * 32 + rows] != 255) rows++;
        const float cw = (float) (gridArea.getWidth() - nameW) / steps, rh = (float) gridArea.getHeight() / Machine::GRID_ROWS;
        const int row = (int) ((e.y - gridArea.getY()) / rh), x = e.x - gridArea.getX();
        if (row < 0 || row >= rows) return;
        if (x < nameW) {
            // pick another sound for this row: its notes move with it
            juce::PopupMenu menu;
            for (int s = 0; s < NSOUNDS; s++) menu.addItem(s + 1, juce::String(s + 1) + " " + SOUND_NAMES[s], true, snap.grid[Machine::GRID_ROWS * 32 + row] == s);
            menu.showMenuAsync(juce::PopupMenu::Options().withMaximumNumColumns(12).withTargetComponent(&pages[1]).withMousePosition(), [this, row](int r) {
                if (r > 0) proc.with([&](Machine& m) { m.gridAssign(row, r - 1); });
                refreshPatterns();
            });
            return;
        }
        const int step = (int) ((x - nameW) / cw);
        if (step >= 0 && step < steps) { proc.with([&](Machine& m) { m.gridToggle(row, gridBar, step); }); refreshPatterns(); }
    };
    for (int i = 0; i < NPARTS; i++) {
        partBtn[i].setButtonText(PART_LONG[i]);
        partBtn[i].onClick = [this, i] { proc.with([&](Machine& m) { m.put(K_PART, i); }); };
        pg.addAndMakeVisible(partBtn[i]);
    }
    for (int i = 0; i < 8; i++) { barBtn[i].setButtonText(juce::String(i + 1)); barBtn[i].onClick = [this, i] { gridBar = i; }; pg.addChildComponent(barBtn[i]); }
    styleButton(clearPart);
    clearPart.onClick = [this] { proc.with([](Machine& m) { m.clearPart(); }); refreshPatterns(); };
    pg.addAndMakeVisible(clearPart);
    for (auto* b : {&midiImp, &midiExp}) { styleButton(*b); pg.addAndMakeVisible(*b); }
    styleButton(midiDrag); midiDrag.setButtonText("Drag part to DAW (MIDI)"); pg.addAndMakeVisible(midiDrag);
    midiDrag.setTooltip("Drag this onto a track in your DAW: the part comes as a drum MIDI clip");
    midiDrag.onDragOut = [this] { auto f = writeMidiTemp(false); if (f.existsAsFile()) performExternalDragDropOfFiles({f.getFullPathName()}, false, this); };
    midiImp.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Import a drum MIDI file into this part", juce::File(), "*.mid;*.midi");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { importMidiFile(fc.getResult()); });
    };
    midiExp.onClick = [this] {
        const juce::String name = "xr10-" + juce::String(snap.st.pattern + 1).paddedLeft('0', 2) + "-" + PART_LONG[snap.st.part % NPARTS] + ".mid";
        chooser = std::make_unique<juce::FileChooser>("Export this part as MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name.replace(" ", "")), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return;
            auto b = proc.exportMidi(false); f.withFileExtension("mid").replaceWithData(b.getData(), b.getSize());
        });
    };
    for (int i = 1; i <= 4; i++) lenBox.addItem(juce::String(i) + (i > 1 ? " bars" : " bar"), i);
    for (int i = 2; i <= 8; i++) beatsBox.addItem(juce::String(i) + "/4", i);
    subBox.addItem("16ths", 4); subBox.addItem("Triplets", 3);
    for (int i = 50; i <= 75; i++) swingBox.addItem("Swing " + juce::String(i) + "%", i);
    for (int i = 1; i <= 4; i++) bankBox.addItem("Bank " + juce::String(i), i);
    auto bind = [this](juce::ComboBox& c, int key, int offset) {
        styleBox(c);
        c.onChange = [this, &c, key, offset] { if (!updating) { proc.with([&](Machine& m) { m.put(key, c.getSelectedId() - offset); }); refreshPatterns(); } };
        pages[1].addAndMakeVisible(c);
    };
    bind(lenBox, K_LEN, 0); bind(beatsBox, K_BEATS, 0); bind(subBox, K_SUB, 0); bind(swingBox, K_SWING, 0); bind(bankBox, K_BANK, 1);
}

void Editor::paintGrid(juce::Graphics& g, juce::Rectangle<int> r) {
    const int nameW = 110, steps = snap.steps, sub = juce::jmax(1, snap.st.sub);
    const float cw = (float) (r.getWidth() - nameW) / steps, rh = (float) r.getHeight() / Machine::GRID_ROWS;
    const bool live = snap.st.playing && snap.st.bar == gridBar;
    for (int row = 0; row < Machine::GRID_ROWS; row++) {
        const int s = snap.grid[Machine::GRID_ROWS * 32 + row];
        if (s == 255) break;
        const float y = r.getY() + row * rh;
        g.setColour(kInk);
        g.setFont(sans(11.5f));
        g.drawText(juce::String(s + 1) + " " + SOUND_NAMES[s], juce::Rectangle<float>((float) r.getX(), y, (float) nameW - 4, rh), juce::Justification::centredLeft);
        for (int st = 0; st < steps; st++) {
            auto c = juce::Rectangle<float>(r.getX() + nameW + st * cw, y, cw, rh).reduced(1.2f);
            const int v = snap.grid[row * 32 + st];
            g.setColour(v == 3 ? kOn : v == 2 ? kSel : v == 1 ? kGhost : kKey);
            g.fillRoundedRectangle(c, 3);
            g.setColour(st % sub == 0 ? kSub : juce::Colour(0xffb3b1bd));
            g.drawRoundedRectangle(c, 3, 1);
            if (live && st / sub == snap.st.beat) { g.setColour(juce::Colour(0xfff6c445)); g.drawRoundedRectangle(c.expanded(0.5f), 3, 2); }
        }
    }
}

void Editor::buildSong() {
    auto& pg = pages[2];
    pg.painter = [this](juce::Graphics& g) { g.fillAll(kPanel); paintSong(g, songArea); };
    pg.click = [this](const juce::MouseEvent& e) {
        if (!songArea.contains(e.getPosition())) return;
        const int i = songFirst + (e.y - songArea.getY()) / 20;
        if (i >= 0 && i < snap.songLen) songSel = i;
        pages[2].repaint();
    };
    styleBox(songBox);
    for (int i = 0; i < NSONG; i++) songBox.addItem("Song " + juce::String(i + 1), i + 1);
    songBox.onChange = [this] { if (!updating) { proc.with([&](Machine& m) { m.put(K_SONG, songBox.getSelectedId() - 1); }); songSel = 0; } };
    styleSlider(songTempo, 0, 300);
    songTempo.textFromValueFunction = [](double v) { return v < 30 ? juce::String("tempo: pattern") : "tempo " + juce::String((int) v); };
    songTempo.setTextBoxStyle(juce::Slider::TextBoxRight, false, 96, 18);
    songTempo.updateText();
    songTempo.onValueChange = [this] { if (!updating) proc.with([&](Machine& m) { m.songTempo(snap.st.song, (int) songTempo.getValue()); }); };
    styleBox(stepPattern); styleBox(stepPart);
    for (int i = 0; i < NPARTS; i++) stepPart.addItem(PART_LONG[i], i + 1);
    styleSlider(stepRepeat, 1, 99);
    stepRepeat.textFromValueFunction = [](double v) { return "x" + juce::String((int) v); };
    stepRepeat.updateText();
    auto setStep = [this] {
        if (updating || songSel >= snap.songLen) return;
        proc.with([&](Machine& m) { m.songSet(snap.st.song, songSel, stepPattern.getSelectedId() - 1, stepPart.getSelectedId() - 1, (int) stepRepeat.getValue()); });
    };
    stepPattern.onChange = setStep; stepPart.onChange = setStep; stepRepeat.onValueChange = setStep;
    for (auto* b : {&songAdd, &songDup, &songDel, &songPlay}) styleButton(*b);
    songAdd.onClick = [this] { proc.with([&](Machine& m) { m.songSet(m.set[K_SONG], 999, m.curPat, m.mainVar, 1); songSel = m.songs[m.set[K_SONG]].len - 1; }); };
    songDup.onClick = [this] { proc.with([&](Machine& m) { m.songInsert(m.set[K_SONG], songSel); }); };
    songDel.onClick = [this] { proc.with([&](Machine& m) { m.songDelete(m.set[K_SONG], songSel); }); songSel = juce::jmax(0, songSel - 1); };
    styleButton(songMidi); styleButton(songDrag); songDrag.setButtonText("Drag song to DAW (MIDI)");
    songMidi.onClick = [this] {
        if (snap.songLen == 0) return;
        chooser = std::make_unique<juce::FileChooser>("Export the song as MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("xr10-song-" + juce::String(snap.st.song + 1) + ".mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return;
            auto b = proc.exportMidi(true); f.withFileExtension("mid").replaceWithData(b.getData(), b.getSize());
        });
    };
    songDrag.onDragOut = [this] { if (snap.songLen == 0) return; auto f = writeMidiTemp(true); if (f.existsAsFile()) performExternalDragDropOfFiles({f.getFullPathName()}, false, this); };
    pg.addAndMakeVisible(songMidi); pg.addAndMakeVisible(songDrag);
    songPlay.onClick = [this] { proc.with([](Machine& m) { if (m.playing) m.stop(); else { m.setMode(M_SONG); m.start(); } }); };
    for (juce::Component* c : {(juce::Component*) &songBox, (juce::Component*) &songTempo, (juce::Component*) &songAdd, (juce::Component*) &songPlay, (juce::Component*) &stepPattern,
                               (juce::Component*) &stepPart, (juce::Component*) &stepRepeat, (juce::Component*) &songDup, (juce::Component*) &songDel})
        pg.addAndMakeVisible(c);
}

void Editor::paintSong(juce::Graphics& g, juce::Rectangle<int> r) {
    g.setFont(sans(13));
    if (snap.songLen == 0) { g.setColour(kSub); g.drawText("Empty: pick a pattern and press \"Add current pattern\", or use SONG mode on the panel.", r.removeFromTop(24), juce::Justification::centredLeft); return; }
    const int rows = r.getHeight() / 20, first = juce::jlimit(0, juce::jmax(0, snap.songLen - rows), songSel - rows / 2);
    songFirst = first;
    for (int k = 0; k < rows && first + k < snap.songLen; k++) {
        const int i = first + k;
        auto row = juce::Rectangle<int>(r.getX(), r.getY() + k * 20, r.getWidth(), 19);
        const auto& s = snap.song[i];
        if (i == songSel) { g.setColour(kSel.withAlpha(0.18f)); g.fillRect(row); }
        if (snap.st.playing && snap.st.songStep == i && snap.st.mode == M_SONG) { g.setColour(juce::Colour(0xfff6c445)); g.drawRect(row, 2); }
        g.setColour(kInk);
        const juce::String pname = s.pattern < patNames.size() ? patNames[s.pattern] : juce::String();
        g.drawText(juce::String(i + 1).paddedLeft('0', 2) + "    P" + juce::String(s.pattern + 1).paddedLeft('0', 2) + " " + pname + "    " + PART_LONG[s.part % NPARTS] + "    x" + juce::String(s.repeat),
                   row.reduced(6, 0), juce::Justification::centredLeft);
    }
}

void Editor::buildSetup() {
    auto& pg = pages[3];
    pg.painter = [](juce::Graphics& g) { g.fillAll(kPanel); };
    const char* labels[12] = {"Effect", "Effect time", "Effect level", "EFFECT send (rear-panel knob)", "Timing correct", "Voices", "Accent velocity", nullptr, "Click", "Click level", nullptr, nullptr};
    for (int i = 0; i < 12; i++) if (labels[i]) { setupLabels[i].setText(labels[i], juce::dontSendNotification); setupLabels[i].setColour(juce::Label::textColourId, kSub); pg.addAndMakeVisible(setupLabels[i]); }
    const char* fx[7] = {"Room", "Hall", "Plate", "Gated reverb", "Delay", "External: FX Send bus only (as the XR10)", "Off"};
    for (int i = 0; i < 7; i++) fxType.addItem(fx[i], i + 1);
    const char* tc[7] = {"Quantize 1/4", "Quantize 1/8", "Quantize 1/8 triplet", "Quantize 1/16", "Quantize 1/16 triplet", "Quantize 1/32", "Quantize off"};
    for (int i = 0; i < 7; i++) tcBox.addItem(tc[i], i + 1);
    for (int v : {4, 6, 8, 12, 16, 24, 32}) polyBox.addItem(juce::String(v) + " voices" + (v == 8 ? " (XR10)" : ""), v);
    midiCh.addItem("MIDI channel: omni", 1);
    for (int i = 1; i <= 16; i++) midiCh.addItem("MIDI channel " + juce::String(i), i + 1);
    midiMap.addItem("Notes: General MIDI drums", 1); midiMap.addItem("Notes: 36 = sound 1 ... 99 = sound 64", 2); midiMap.addItem("Notes: 36 = pad 1 of the bank", 3);
    syncBox.addItem("Follow the host's transport", 2); syncBox.addItem("Internal clock", 1);
    clickBox.addItem("Click off", 1); clickBox.addItem("Click when recording", 2); clickBox.addItem("Click always", 3);
    auto bind = [this, &pg](juce::ComboBox& c, int key, int offset) {
        styleBox(c);
        c.onChange = [this, &c, key, offset] { if (!updating) proc.with([&](Machine& m) { m.put(key, c.getSelectedId() - offset); }); };
        pg.addAndMakeVisible(c);
    };
    bind(fxType, K_FXTYPE, 1); bind(tcBox, K_TC, 1); bind(polyBox, K_POLY, 0); bind(midiCh, K_MIDICH, 1); bind(midiMap, K_MIDIMAP, 1); bind(syncBox, K_SYNC, 1); bind(clickBox, K_CLICK, 1);
    auto bindS = [this, &pg](juce::Slider& s, int key, int lo, int hi) {
        styleSlider(s, lo, hi);
        s.onValueChange = [this, &s, key] { if (!updating) proc.with([&](Machine& m) { m.put(key, (int) s.getValue()); }); };
        pg.addAndMakeVisible(s);
    };
    bindS(effectVr, K_EFFECTVR, 0, 100); bindS(fxTime, K_FXTIME, 0, 99); bindS(fxLevel, K_FXLEVEL, 0, 99); bindS(clickLvl, K_CLICKLVL, 0, 99); bindS(accVel, K_ACCENTVEL, 1, 127);
    for (auto* t : {&padVel, &countIn}) { t->setColour(juce::ToggleButton::textColourId, kInk); t->setColour(juce::ToggleButton::tickColourId, kSel); t->setColour(juce::ToggleButton::tickDisabledColourId, kSub); pg.addAndMakeVisible(*t); }
    padVel.onClick = [this] { proc.with([&](Machine& m) { m.put(K_PADVEL, padVel.getToggleState()); }); };
    countIn.onClick = [this] { proc.with([&](Machine& m) { m.put(K_COUNTIN, countIn.getToggleState()); }); };
    for (auto* b : {&exportBtn, &importBtn, &initBtn}) { styleButton(*b); pg.addAndMakeVisible(*b); }
    exportBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Save the XR10 memory", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("omega-xr10.xr10"), "*.xr10");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            auto b = proc.saveMemory();
            f.replaceWithData(b.getData(), b.getSize());
        });
    };
    importBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Load an XR10 memory file (from the plugin or the Android app)", juce::File(), "*.xr10");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            juce::MemoryBlock b;
            if (!f.existsAsFile() || !f.loadFileAsData(b)) return;
            if (!proc.loadMemory(b)) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Omega XR10", "That isn't an XR10 memory file.");
            refreshPatterns(); waveFor = -1;
        });
    };
    initBtn.onClick = [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Initialize all", "Erase your patterns, songs, voice edits and samples?", "Erase", "Cancel", this,
            juce::ModalCallbackFunction::create([this](int r) { if (r) { proc.with([](Machine& m) { m.stop(); m.factoryReset(); }); refreshPatterns(); waveFor = -1; } }));
    };
}

void Editor::mouseDown(const juce::MouseEvent&) {}

juce::File Editor::writeMidiTemp(bool song) {
    auto b = proc.exportMidi(song);
    const juce::String name = song ? "XR10 song " + juce::String(snap.st.song + 1) : "XR10 " + juce::String(snap.st.pattern + 1).paddedLeft('0', 2) + " " + juce::String(snap.patName).trim() + " " + PART_LONG[snap.st.part % NPARTS];
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Omega XR10").getChildFile(juce::File::createLegalFileName(name) + ".mid");
    f.getParentDirectory().createDirectory();
    f.replaceWithData(b.getData(), b.getSize());
    return f;
}
void Editor::importMidiFile(const juce::File& f) {
    juce::MemoryBlock b;
    if (!f.existsAsFile() || !f.loadFileAsData(b)) return;
    const int r = proc.importMidi(b);
    refreshPatterns();
    if (r <= 0) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Omega XR10", r < 0 ? "That isn't a MIDI file." : "No drum notes in that file.");
}
// drop a .mid onto the window to import it, a .xr10 memory file to load it, or samples (or a folder of them) to put them on their sounds
void Editor::filesDropped(const juce::StringArray& paths, int, int) {
    juce::Array<juce::File> audio;
    for (auto& p : paths) {
        juce::File f(p);
        if (f.hasFileExtension("mid;midi")) { importMidiFile(f); return; }
        if (f.hasFileExtension("xr10")) { juce::MemoryBlock b; if (f.loadFileAsData(b)) proc.loadMemory(b); refreshPatterns(); waveFor = -1; return; }
        if (f.isDirectory()) audio.addArray(f.findChildFiles(juce::File::findFiles, true)); else audio.add(f);
    }
    if (!audio.isEmpty()) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Omega XR10", proc.loadSampleSet(audio)); waveFor = -1; }
}

void Editor::refreshPatterns() { patNamesAge = 0; }

void Editor::refresh() {
    const juce::ScopedValueSetter<bool> u(updating, true);
    const auto& st = snap.st;
    styleButton(play, st.playing != 0);
    rec.setColour(juce::TextButton::buttonColourId, st.recording ? kOn : kKey);
    rec.setColour(juce::TextButton::textColourOffId, st.recording ? juce::Colours::white : kInk);
    if (!tempo.isMouseButtonDown()) tempo.setValue(st.tempo10 / 10, juce::dontSendNotification);
    if (patternBox.getNumItems() != NPAT || patNamesAge == 0) {
        proc.patternNames(patNames);
        patternBox.clear(juce::dontSendNotification);
        stepPattern.clear(juce::dontSendNotification);
        for (int i = 0; i < NPAT; i++) {
            const auto name = juce::String(i + 1).paddedLeft('0', 2) + " " + patNames[(size_t) i];
            patternBox.addItem(name + (i >= NPRESET ? "  (user)" : ""), i + 1);
            stepPattern.addItem(name, i + 1);
        }
    }
    if (++patNamesAge > 90) patNamesAge = 0;
    patternBox.setSelectedId(st.pattern + 1, juce::dontSendNotification);
    // voices
    for (int i = 0; i < NSOUNDS; i++) {
        styleButton(inst[i], i == st.sound);
        if (snap.user[i] && i != st.sound) inst[i].setColour(juce::TextButton::buttonColourId, juce::Colour(0xfff6dbe3));
    }
    voiceTitle.setText("Voice " + juce::String(st.sound + 1) + ": " + SOUND_NAMES[st.sound] + (snap.user[st.sound] ? "  (your sample)" : snap.packed[st.sound] ? "  (XR-10 sample)" : "  (synthesized)"), juce::dontSendNotification);
    for (int p = 0; p < NVP; p++) if (!params[p].isMouseButtonDown()) params[p].setValue(snap.params[p], juce::dontSendNotification);
    const int userFlag = snap.user[st.sound] ? 1 : 0;
    if (waveFor != st.sound || waveUser != userFlag) { waveFor = st.sound; waveUser = userFlag; proc.sampleWave(st.sound, wave); pages[0].repaint(waveArea); }
    // grid
    for (int i = 0; i < NPARTS; i++) styleButton(partBtn[i], i == st.part);
    if (gridBar >= st.bars) gridBar = 0;
    for (int i = 0; i < 8; i++) { barBtn[i].setVisible(i < st.bars); styleButton(barBtn[i], i == gridBar); }
    lenBox.setSelectedId(snap.keys[K_LEN], juce::dontSendNotification);
    beatsBox.setSelectedId(snap.keys[K_BEATS], juce::dontSendNotification);
    subBox.setSelectedId(snap.keys[K_SUB], juce::dontSendNotification);
    swingBox.setSelectedId(snap.keys[K_SWING], juce::dontSendNotification);
    bankBox.setSelectedId(st.bank + 1, juce::dontSendNotification);
    // song
    songBox.setSelectedId(st.song + 1, juce::dontSendNotification);
    if (!songTempo.isMouseButtonDown()) songTempo.setValue(snap.songTempo, juce::dontSendNotification);
    if (songSel >= snap.songLen) songSel = juce::jmax(0, snap.songLen - 1);
    const bool has = snap.songLen > 0;
    for (juce::Component* c : {(juce::Component*) &stepPattern, (juce::Component*) &stepPart, (juce::Component*) &stepRepeat, (juce::Component*) &songDup, (juce::Component*) &songDel}) c->setEnabled(has);
    if (has) {
        stepPattern.setSelectedId(snap.song[songSel].pattern + 1, juce::dontSendNotification);
        stepPart.setSelectedId(snap.song[songSel].part + 1, juce::dontSendNotification);
        if (!stepRepeat.isMouseButtonDown()) stepRepeat.setValue(snap.song[songSel].repeat, juce::dontSendNotification);
    }
    // setup
    fxType.setSelectedId(snap.keys[K_FXTYPE] + 1, juce::dontSendNotification);
    tcBox.setSelectedId(snap.keys[K_TC] + 1, juce::dontSendNotification);
    polyBox.setSelectedId(snap.keys[K_POLY], juce::dontSendNotification);
    midiCh.setSelectedId(snap.keys[K_MIDICH] + 1, juce::dontSendNotification);
    midiMap.setSelectedId(snap.keys[K_MIDIMAP] + 1, juce::dontSendNotification);
    syncBox.setSelectedId(snap.keys[K_SYNC] + 1, juce::dontSendNotification);
    clickBox.setSelectedId(snap.keys[K_CLICK] + 1, juce::dontSendNotification);
    if (!effectVr.isMouseButtonDown()) effectVr.setValue(snap.keys[K_EFFECTVR], juce::dontSendNotification);
    if (!fxTime.isMouseButtonDown()) fxTime.setValue(snap.keys[K_FXTIME], juce::dontSendNotification);
    if (!fxLevel.isMouseButtonDown()) fxLevel.setValue(snap.keys[K_FXLEVEL], juce::dontSendNotification);
    if (!clickLvl.isMouseButtonDown()) clickLvl.setValue(snap.keys[K_CLICKLVL], juce::dontSendNotification);
    if (!accVel.isMouseButtonDown()) accVel.setValue(snap.keys[K_ACCENTVEL], juce::dontSendNotification);
    padVel.setToggleState(snap.keys[K_PADVEL] != 0, juce::dontSendNotification);
    countIn.setToggleState(snap.keys[K_COUNTIN] != 0, juce::dontSendNotification);
}

int Editor::baseW() const { return BASE_W + (proc.foldOpen ? FOLD_W : 0); }
void Editor::setFold(bool open) {
    const float s = (float) getWidth() / baseW();
    proc.foldOpen = open;
    setResizeLimits(baseW() * 4 / 10, BASE_H * 4 / 10, baseW() * 2, BASE_H * 2);
    getConstrainer()->setFixedAspectRatio((double) baseW() / BASE_H);
    setSize(juce::roundToInt(baseW() * s), juce::roundToInt(BASE_H * s));
    resized();
}
void Editor::timerCallback() {
    proc.snapshot(snap, gridBar);
    refresh();
    if (proc.foldOpen) fold->refresh();
    face.repaint();
    pages[tab].repaint();
}

}  // namespace xr10
