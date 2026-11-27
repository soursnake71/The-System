#include "SolinaEditor.h"

namespace solina {

static const juce::Colour BG(0xff151413), PANEL(0xff1e1c1a), LINE(0xff3a3632), ORG(0xffff7a1a), CREAM(0xffefe6d2), DIM(0xffa39b8b),
    LCDC(0xffbfe08a), LCDBG(0xff1b2414), RX(0xff2a2c30), WOOD(0xff7a4f2a), BLUE(0xff5aa0e0), RED(0xffe0453a), YEL(0xfff2c230);
enum { pgPanel, pgMixer, pgFx, pgSeq, pgMidi };

// ---------------- look ----------------
Look::Look() {
    setColour(juce::ResizableWindow::backgroundColourId, BG);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff262421)); setColour(juce::TextButton::buttonOnColourId, ORG);
    setColour(juce::TextButton::textColourOffId, CREAM); setColour(juce::TextButton::textColourOnId, juce::Colour(0xff1a1000));
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff262421)); setColour(juce::ComboBox::outlineColourId, LINE); setColour(juce::ComboBox::textColourId, CREAM);
    setColour(juce::PopupMenu::backgroundColourId, PANEL); setColour(juce::PopupMenu::highlightedBackgroundColourId, ORG);
    setColour(juce::Label::textColourId, CREAM); setColour(juce::Slider::textBoxTextColourId, LCDC); setColour(juce::Slider::textBoxBackgroundColourId, LCDBG);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff31402a));
    setColour(juce::TextEditor::backgroundColourId, LCDBG); setColour(juce::TextEditor::textColourId, LCDC); setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff31402a));
}
void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
    const float r = juce::jmin(w, h) * .5f - 3, cx = x + w * .5f, cy = y + h * .5f, ang = a0 + pos * (a1 - a0);
    const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0; const float from = bip ? (a0 + a1) * .5f : a0;
    juce::Path t; t.addCentredArc(cx, cy, r, r, 0, a0, a1, true); g.setColour(LINE); g.strokePath(t, juce::PathStrokeType(r * .2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path v; v.addCentredArc(cx, cy, r, r, 0, juce::jmin(from, ang), juce::jmax(from, ang), true);
    g.setColour(s.findColour(juce::Slider::rotarySliderFillColourId)); g.strokePath(v, juce::PathStrokeType(r * .2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float kr = r * .7f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4743), cx - kr * .4f, cy - kr * .5f, juce::Colour(0xff151413), cx + kr, cy + kr, true));
    g.fillEllipse(cx - kr, cy - kr, kr * 2, kr * 2); g.setColour(juce::Colours::black); g.drawEllipse(cx - kr, cy - kr, kr * 2, kr * 2, 1);
    g.setColour(CREAM); g.drawLine(cx, cy, cx + kr * .85f * std::sin(ang), cy - kr * .85f * std::cos(ang), juce::jmax(1.5f, r * .12f));
}
void Look::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s) {
    if (st != juce::Slider::LinearVertical) { LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, st, s); return; }
    const float cx = x + w * .5f; g.setColour(juce::Colour(0xff0c0c0d)); g.fillRoundedRectangle(cx - 2, (float) y, 4, (float) h, 2);
    g.setColour(juce::Colour(0xff555555)); for (int i = 0; i <= 10; i++) g.fillRect(x + w * .8f, y + h * i / 10.0f, 4.0f, 1.0f);
    const float cw = juce::jmin(w * .7f, 46.0f), ch = juce::jmin(22.0f, h * .12f);
    juce::Rectangle<float> cap(cx - cw / 2, pos - ch / 2, cw, ch);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd8d8d8), 0, cap.getY(), juce::Colour(0xff8e8e8e), 0, cap.getBottom(), false)); g.fillRoundedRectangle(cap, 3);
    g.setColour(juce::Colour(0xff222222)); g.fillRect(cap.getX() + cw * .1f, cap.getCentreY() - 1, cw * .8f, 2.0f);
}
void Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1);
    juce::Colour c = b.getToggleState() ? b.findColour(juce::TextButton::buttonOnColourId) : b.findColour(juce::TextButton::buttonColourId);
    if (over) c = c.brighter(.08f); if (down) c = c.brighter(.2f);
    g.setColour(c); g.fillRoundedRectangle(r, 4); g.setColour(LINE); g.drawRoundedRectangle(r, 4, 1);
}

// ---------------- knob / switch ----------------
Knob::Knob(SolinaEditor& e, int i, const juce::String& lb, bool vert) : id(i), vertical(vert), ed(e) {
    slider.k = this;
    slider.setSliderStyle(vert ? juce::Slider::LinearVertical : juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour(juce::Slider::rotarySliderFillColourId, ORG);
    slider.setDoubleClickReturnValue(true, PARAMS[i].def);
    slider.setMouseDragSensitivity(180);
    addAndMakeVisible(slider);
    label.setText(lb, juce::dontSendNotification); label.setJustificationType(juce::Justification::centred); label.setColour(juce::Label::textColourId, DIM);
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(e.proc.apvts, PARAMS[i].id, slider);
    slider.onValueChange = [this] { ed.lcd(juce::String(PARAMS[id].name) + ": " + fmtVal(id, slider.getValue())); repaint(); };
}
void Knob::S::mouseDown(const juce::MouseEvent& e) { if (k->ed.proc.learn) { k->ed.learnPick(k->id); return; } juce::Slider::mouseDown(e); }
void Knob::S::mouseDoubleClick(const juce::MouseEvent& e) { if (!k->ed.proc.learn) juce::Slider::mouseDoubleClick(e); }
void Knob::resized() {
    auto r = getLocalBounds(); const int th = juce::jlimit(10, 15, getHeight() / 5);
    label.setFont(juce::Font(juce::FontOptions((float) th * .82f))); label.setBounds(r.removeFromBottom(th * 2));
    slider.setBounds(r);
}
void Knob::paint(juce::Graphics& g) {
    if (ed.proc.learnTarget == id) { g.setColour(YEL); g.drawRect(getLocalBounds(), 2); }
    label.setText(label.getText().upToFirstOccurrenceOf("\n", false, false) + "\n" + fmtVal(id, slider.getValue()), juce::dontSendNotification);
}
Switch::Switch(SolinaEditor& e, int i, const juce::String& t, juce::Colour on) : juce::TextButton(t), id(i), ed(e) {
    setClickingTogglesState(true); setColour(juce::TextButton::buttonOnColourId, on);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(e.proc.apvts, PARAMS[i].id, *this);
    onClick = [this] { ed.lcd(juce::String(PARAMS[id].name) + ": " + (getToggleState() ? "ON" : "OFF")); };
}
void Switch::mouseDown(const juce::MouseEvent& e) { if (ed.proc.learn) { ed.learnPick(id); return; } juce::TextButton::mouseDown(e); }

// ---------------- piano roll ----------------
static juce::String noteName(int n) { static const char* N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; return juce::String(N[n % 12]) + juce::String(n / 12 - 1); }
void Roll::paint(juce::Graphics& g) {
    const int R = rows(); const float rh = getHeight() / (float) R, k = kw(), cw = (getWidth() - k) / 16.0f; const int b0 = view * BAR;
    g.fillAll(juce::Colour(0xff0f0e0d));
    for (int r = 0; r < R; r++) { const int n = low + R - 1 - r, pc = n % 12; const bool blk = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        g.setColour(blk ? juce::Colour(0xff121110) : juce::Colour(0xff191816)); g.fillRect(k, r * rh, getWidth() - k, rh);
        g.setColour(blk ? juce::Colour(0xff2a2826) : juce::Colour(0xffd9d1bf)); g.fillRect(0.0f, r * rh + .5f, k - 2, rh - 1);
        if (pc == 0) { g.setColour(juce::Colour(0xff5a5246)); g.setFont(juce::jmin(11.0f, rh * .8f)); g.drawText(noteName(n), 2, (int) (r * rh), (int) k, (int) rh, juce::Justification::centredLeft); } }
    for (int s = 0; s <= 16; s++) { g.setColour(s % 4 ? juce::Colour(0xff262422) : juce::Colour(0xff4a4540)); g.fillRect(k + s * cw, 0.0f, 1.0f, (float) getHeight()); }
    Pattern p; double tick = 0; bool pl; int pp;
    { const juce::SpinLock::ScopedLockType l(ed.proc.songLock); p = ed.proc.song.pats[ed.proc.song.cur]; pl = ed.proc.playing && ed.proc.playPat == ed.proc.song.cur; pp = 0; tick = ed.proc.playTick; }
    (void) pp;
    for (auto& nt : p.notes) { const int r = low + R - 1 - nt.n; if (r < 0 || r >= R) continue;
        const float x = k + (nt.t - b0) / (float) STEP * cw, w = nt.d / (float) STEP * cw; if (x + w < k || x > getWidth()) continue;
        const float xa = juce::jmax(k, x), xb = juce::jmin(x + w, (float) getWidth());
        g.setColour(ORG.withAlpha(.45f + .55f * nt.v / 127.0f)); g.fillRect(xa + 1, r * rh + 1, juce::jmax(3.0f, xb - xa - 2), rh - 2);
        if (x >= k) { g.setColour(juce::Colour(0xffffd2a8)); g.fillRect(x + 1, r * rh + 1, 2.0f, rh - 2); } }
    if (pl) { const double t = tick - b0; if (t >= 0 && t < BAR) { g.setColour(juce::Colour(0xff4caf50)); g.fillRect(k + (float) (t / STEP) * cw, 0.0f, 2.0f, (float) getHeight()); } }
    g.setColour(LINE); g.drawRect(getLocalBounds());
}
void Roll::mouseDown(const juce::MouseEvent& e) {
    const int R = rows(); const float rh = getHeight() / (float) R, k = kw(), cw = (getWidth() - k) / 16.0f; const int b0 = view * BAR;
    const int n = low + R - 1 - (int) (e.y / rh);
    if (e.x < k) { audition = n; ed.proc.keyboard.noteOn(1, n, .8f); return; }
    const double tk = b0 + (e.x - k) / cw * STEP;
    const juce::SpinLock::ScopedLockType l(ed.proc.songLock);
    auto& p = ed.proc.song.pats[ed.proc.song.cur];
    for (size_t i = 0; i < p.notes.size(); i++) { auto& nt = p.notes[i]; if (nt.n == n && tk >= nt.t && tk < nt.t + juce::jmax((double) nt.d, STEP * .5)) { delIdx = (int) i; return; } }
    const int t = (int) std::floor((tk - b0) / STEP) * STEP + b0; if (t >= patLen(p)) return;
    p.notes.push_back({t, n, len, vel}); dragNote = (int) p.notes.size() - 1; x0 = (float) e.x; d0 = len;
    ed.proc.songChanged(); repaint();
    audition = n; ed.proc.keyboard.noteOn(1, n, vel / 127.0f);
}
void Roll::mouseDrag(const juce::MouseEvent& e) {
    if (dragNote < 0) return; const float cw = (getWidth() - kw()) / 16.0f;
    const juce::SpinLock::ScopedLockType l(ed.proc.songLock); auto& p = ed.proc.song.pats[ed.proc.song.cur];
    if (dragNote < (int) p.notes.size()) p.notes[(size_t) dragNote].d = juce::jmax(STEP, (int) std::lround((d0 + (e.x - x0) / cw * STEP) / STEP) * STEP);
    repaint();
}
void Roll::mouseUp(const juce::MouseEvent&) {
    if (audition >= 0) { ed.proc.keyboard.noteOff(1, audition, 0); audition = -1; }
    if (delIdx >= 0) { const juce::SpinLock::ScopedLockType l(ed.proc.songLock); auto& p = ed.proc.song.pats[ed.proc.song.cur]; if (delIdx < (int) p.notes.size()) p.notes.erase(p.notes.begin() + delIdx); ed.proc.songChanged(); }
    delIdx = -1; dragNote = -1; repaint();
}

// ---------------- editor ----------------
Knob* SolinaEditor::K(int id, const juce::String& lb, int pg, bool vert) { auto* k = knobs.add(new Knob(*this, id, lb, vert)); addChildComponent(k); pages[(size_t) pg].push_back(k); return k; }
Switch* SolinaEditor::W(int id, const juce::String& lb, int pg, juce::Colour c) { auto* s = switches.add(new Switch(*this, id, lb, c)); addChildComponent(s); pages[(size_t) pg].push_back(s); return s; }
juce::TextButton* SolinaEditor::B(const juce::String& t, int pg, std::function<void()> f) {
    auto* b = new juce::TextButton(t); ownedButtons.add(b); b->onClick = std::move(f); addChildComponent(b); pages[(size_t) pg].push_back(b); return b;
}

SolinaEditor::SolinaEditor(SolinaProcessor& p) : AudioProcessorEditor(p), proc(p), kb(p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard) {
    setLookAndFeel(&look);
    lcdLabel.setColour(juce::Label::backgroundColourId, LCDBG); lcdLabel.setColour(juce::Label::textColourId, LCDC); lcdLabel.setMinimumHorizontalScale(.5f); lcdLabel.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold)));
    addAndMakeVisible(lcdLabel);
    fillPresets(); addAndMakeVisible(preset);
    preset.onChange = [this] { const int i = preset.getSelectedId() - 1; if (i >= 0 && i < kPresets) { proc.setCurrentProgram(i); lcd(juce::String(juce::CharPointer_UTF8(PRESETS[i].name))); } };
    for (auto* b : {&prev, &next, &dice, &play, &stop, &rec}) addAndMakeVisible(b);
    prev.onClick = [this] { preset.setSelectedId((proc.getCurrentProgram() + kPresets - 1) % kPresets + 1); };
    next.onClick = [this] { preset.setSelectedId((proc.getCurrentProgram() + 1) % kPresets + 1); };
    dice.onClick = [this] { proc.randomize(); preset.setText("Random sound", juce::dontSendNotification); lcd("Random sound"); };
    play.onClick = [this] { if (proc.playing) proc.stopRequest = true; else proc.playRequest = true; };
    stop.onClick = [this] { proc.stopRequest = true; proc.recording = false; rec.setToggleState(false, juce::dontSendNotification); };
    rec.setClickingTogglesState(true); rec.setColour(juce::TextButton::buttonOnColourId, RED);
    rec.onClick = [this] { proc.recording = rec.getToggleState(); };
    bpm.setSliderStyle(juce::Slider::LinearBar); bpm.setRange(30, 300, 1); bpm.setTextValueSuffix(" BPM"); addAndMakeVisible(bpm);
    { const juce::SpinLock::ScopedLockType l(proc.songLock); bpm.setValue(proc.song.bpm, juce::dontSendNotification); }
    bpm.onValueChange = [this] { const juce::SpinLock::ScopedLockType l(proc.songLock); proc.song.bpm = bpm.getValue(); };
    const char* tn[5] = {"PANEL", "MIXER", "FX", "SEQ", "MIDI"};
    for (int i = 0; i < 5; i++) { tabs[i].setButtonText(tn[i]); tabs[i].onClick = [this, i] { show(i); }; addAndMakeVisible(tabs[i]); }

    // panel: tabs, knobs, switches
    static const int TABS[6] = {pid::viola, pid::violin, pid::trumpet, pid::horn, pid::cello, pid::contra};
    static const char* TABN[6] = {"VIOLA", "VIOLIN", "TRUMPET", "HORN", "CELLO", "CONTRA BASS"};
    for (int i = 0; i < 6; i++) { auto* s = W(TABS[i], TABN[i], pgPanel, i == 2 || i == 3 ? ORG : juce::Colour(0xffe9e2d1)); s->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff3a2a1a)); panelBox.push_back(s); }
    for (auto [id, lb] : std::initializer_list<std::pair<int, const char*>>{{pid::cresc, "CRESCENDO"}, {pid::susLen, "SUSTAIN LENGTH"}, {pid::bassVol, "BASS VOLUME"}, {pid::tune, "TUNING"}, {pid::volume, "VOLUME"}})
        panelBox.push_back(K(id, lb, pgPanel));
    panelBox.push_back(W(pid::sus, "SUSTAIN", pgPanel)); panelBox.push_back(W(pid::ens, "MODULATION", pgPanel)); panelBox.push_back(W(pid::outSel, "LOW OUT", pgPanel));
    for (auto [id, lb] : std::initializer_list<std::pair<int, const char*>>{{pid::chorusHz, "Chorus"}, {pid::tremHz, "Tremolo Hz"}, {pid::depth, "Depth"}, {pid::trem, "Tremolo"}}) ensBox.push_back(K(id, lb, pgPanel));
    ensBox.push_back(W(pid::wide, "STEREO LINES", pgPanel));
    for (auto [id, lb] : std::initializer_list<std::pair<int, const char*>>{{pid::velSens, "Velocity"}, {pid::bendRange, "Bend"}, {pid::modDest, "Mod wheel"}}) playBox.push_back(K(id, lb, pgPanel));

    // mixer: RX5-style strips
    static const int MIX[6] = {1, 0, 2, 3, 4, 5};
    for (int s = 0; s < 6; s++) { const int c = MIX[s], b = CH_BASE[c];
        mixName[s].setText(juce::String(CH_NAMES[c]).toUpperCase(), juce::dontSendNotification); mixName[s].setJustificationType(juce::Justification::centred);
        mixName[s].setColour(juce::Label::backgroundColourId, LCDBG); mixName[s].setColour(juce::Label::textColourId, LCDC); addChildComponent(mixName[s]); pages[pgMixer].push_back(&mixName[s]);
        const char* names[7] = {"Pan", "Pitch", "Attack", "Decay", "Tone", "Echo", "Reverb"}; const int offs[7] = {oPan, oPit, oAtk, oDec, oTon, oEch, oRev};
        for (int i = 0; i < 7; i++) { auto* k = K(b + offs[i], names[i], pgMixer); if (i < 2) k->slider.setColour(juce::Slider::rotarySliderFillColourId, BLUE); if (i >= 5) k->slider.setColour(juce::Slider::rotarySliderFillColourId, LCDC); mixK[s].push_back(k); }
        mixF[s] = K(b + oLvl, "Level", pgMixer, true);
        mixS[s][0] = W(b + oEns, "ENS", pgMixer, BLUE); mixS[s][1] = W(b + oMut, "M", pgMixer, RED); mixS[s][2] = W(b + oSol, "S", pgMixer, YEL); }

    // effects
    for (auto [id, lb] : std::initializer_list<std::pair<int, const char*>>{{pid::phRate, "Rate"}, {pid::phDepth, "Depth"}, {pid::phFb, "Color"}, {pid::phMix, "Mix"}}) fxBox[0].push_back(K(id, lb, pgFx));
    fxBox[0].push_back(W(pid::phOn, "PHASER", pgFx));
    for (auto [id, lb] : std::initializer_list<std::pair<int, const char*>>{{pid::dlTime, "Time"}, {pid::dlSync, "Sync"}, {pid::dlFb, "Feedback"}, {pid::dlTone, "Tone"}, {pid::dlRet, "Return"}}) fxBox[1].push_back(K(id, lb, pgFx));
    fxBox[1].push_back(W(pid::dlPing, "PING-PONG", pgFx)); fxBox[1].push_back(W(pid::dlOn, "ECHO", pgFx));
    for (auto [id, lb] : std::initializer_list<std::pair<int, const char*>>{{pid::rvSize, "Size"}, {pid::rvDecay, "Decay"}, {pid::rvDamp, "Damping"}, {pid::rvPre, "Pre-delay"}, {pid::rvRet, "Return"}}) fxBox[2].push_back(K(id, lb, pgFx));
    fxBox[2].push_back(W(pid::rvOn, "REVERB", pgFx));
    for (auto [id, lb] : std::initializer_list<std::pair<int, const char*>>{{pid::eqLow, "Bass"}, {pid::eqMid, "Mid"}, {pid::eqHigh, "Treble"}, {pid::drive, "Drive"}, {pid::master, "Master"}}) fxBox[3].push_back(K(id, lb, pgFx));

    // sequencer
    for (int i = 0; i < NPAT; i++) { patBtn[i].setButtonText(juce::String(i + 1)); addChildComponent(patBtn[i]); pages[pgSeq].push_back(&patBtn[i]);
        patBtn[i].onClick = [this, i] { { const juce::SpinLock::ScopedLockType l(proc.songLock); if (chainEdit) proc.song.chain.push_back(i); else { proc.song.cur = i; roll.view = 0; } } proc.songChanged(); refreshSeq(); }; }
    for (int b = 1; b <= 4; b++) bars.addItem(juce::String(b) + (b == 1 ? " bar" : " bars"), b);
    bars.onChange = [this] { { const juce::SpinLock::ScopedLockType l(proc.songLock); proc.song.pats[proc.song.cur].bars = bars.getSelectedId(); } proc.songChanged(); refreshSeq(); };
    barView.onChange = [this] { roll.view = juce::jmax(0, barView.getSelectedId() - 1); roll.repaint(); };
    for (auto* c : {&bars, &barView}) { addChildComponent(c); pages[pgSeq].push_back(c); }
    addChildComponent(roll); pages[pgSeq].push_back(&roll);
    songModeBtn = B("PATTERN", pgSeq, [this] { { const juce::SpinLock::ScopedLockType l(proc.songLock); auto& s = proc.song; s.mode = s.mode ? 0 : 1; if (s.mode && s.chain.empty()) s.chain.push_back(s.cur); } proc.songChanged(); refreshSeq(); });
    chainEdBtn = B("CHAIN EDIT", pgSeq, [this] { chainEdit = !chainEdit; refreshSeq(); });
    seqSide = {songModeBtn, chainEdBtn};
    chainLabel.setColour(juce::Label::backgroundColourId, LCDBG); chainLabel.setColour(juce::Label::textColourId, LCDC); addChildComponent(chainLabel); pages[pgSeq].push_back(&chainLabel); seqSide.push_back(&chainLabel);
    seqSide.push_back(B("DEL", pgSeq, [this] { { const juce::SpinLock::ScopedLockType l(proc.songLock); if (!proc.song.chain.empty()) proc.song.chain.pop_back(); } proc.songChanged(); refreshSeq(); }));
    seqSide.push_back(B("CLR", pgSeq, [this] { { const juce::SpinLock::ScopedLockType l(proc.songLock); proc.song.chain.clear(); } proc.songChanged(); refreshSeq(); }));
    seqSide.push_back(B("+CUR", pgSeq, [this] { { const juce::SpinLock::ScopedLockType l(proc.songLock); proc.song.chain.push_back(proc.song.cur); } proc.songChanged(); refreshSeq(); }));
    const int LENS[6] = {24, 48, 96, 192, 384, 768}; const char* LN[6] = {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars"};
    for (int i = 0; i < 6; i++) noteLen.addItem(LN[i], LENS[i]); noteLen.setSelectedId(96); noteLen.onChange = [this] { roll.len = noteLen.getSelectedId(); };
    for (int v : {127, 100, 70, 40}) noteVel.addItem("vel " + juce::String(v), v); noteVel.setSelectedId(100); noteVel.onChange = [this] { roll.vel = noteVel.getSelectedId(); };
    for (auto* c : {&noteLen, &noteVel}) { addChildComponent(c); pages[pgSeq].push_back(c); seqSide.push_back(c); }
    seqSide.push_back(B("PITCH UP", pgSeq, [this] { roll.low = juce::jmin(96, roll.low + 6); roll.repaint(); }));
    seqSide.push_back(B("PITCH DOWN", pgSeq, [this] { roll.low = juce::jmax(12, roll.low - 6); roll.repaint(); }));
    quantBtn = B("QUANT", pgSeq, [this] { proc.quantize = !proc.quantize; refreshSeq(); }); seqSide.push_back(quantBtn);
    seqSide.push_back(B("RANDOM", pgSeq, [this] { std::mt19937 rng((unsigned) juce::Time::currentTimeMillis()); { const juce::SpinLock::ScopedLockType l(proc.songLock); proc.song.pats[proc.song.cur] = randomPattern(rng); } roll.view = 0; proc.songChanged(); refreshSeq(); lcd("Random progression"); }));
    seqSide.push_back(B("COPY", pgSeq, [this] { const juce::SpinLock::ScopedLockType l(proc.songLock); clip = std::make_unique<Pattern>(proc.song.pats[proc.song.cur]); }));
    seqSide.push_back(B("PASTE", pgSeq, [this] { if (!clip) return; { const juce::SpinLock::ScopedLockType l(proc.songLock); proc.song.pats[proc.song.cur] = *clip; } proc.songChanged(); refreshSeq(); }));
    seqSide.push_back(B("CLEAR", pgSeq, [this] { { const juce::SpinLock::ScopedLockType l(proc.songLock); proc.song.pats[proc.song.cur] = Pattern{}; } proc.songChanged(); refreshSeq(); }));
    seqSide.push_back(B("IMPORT .MID", pgSeq, [this] {
        chooser = std::make_unique<juce::FileChooser>("Import a MIDI file", juce::File(), "*.mid;*.midi");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult(); if (!f.existsAsFile()) return;
            const auto err = proc.importMidi(f, proc.quantize, true); lcd(err.isEmpty() ? "Imported " + f.getFileName() : err); roll.view = 0; refreshSeq(); });
    }));
    seqSide.push_back(B("EXPORT .MID", pgSeq, [this] {
        chooser = std::make_unique<juce::FileChooser>("Export a MIDI file", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Omega Solina.mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return; if (!f.hasFileExtension("mid")) f = f.withFileExtension("mid");
            const auto err = proc.exportMidi(f, true); lcd(err.isEmpty() ? "Saved " + f.getFileName() : err); });
    }));
    swing.setSliderStyle(juce::Slider::LinearBar); swing.setRange(0, 1, .01); swing.textFromValueFunction = [](double v) { return "Swing " + juce::String((int) std::lround(50 + v * 25)) + "%"; };
    swing.onValueChange = [this] { const juce::SpinLock::ScopedLockType l(proc.songLock); proc.song.swing = swing.getValue(); };
    swing.updateText(); addChildComponent(swing); pages[pgSeq].push_back(&swing); seqSide.push_back(&swing);

    // MIDI
    inCh.addItem("Omni", 100); for (int c = 1; c <= 16; c++) inCh.addItem("In " + juce::String(c), c); inCh.setSelectedId(proc.inChannel == 0 ? 100 : proc.inChannel.load());
    inCh.onChange = [this] { proc.inChannel = inCh.getSelectedId() == 100 ? 0 : inCh.getSelectedId(); };
    for (int c = 1; c <= 16; c++) outCh.addItem("Out " + juce::String(c), c); outCh.setSelectedId(proc.outChannel); outCh.onChange = [this] { proc.outChannel = outCh.getSelectedId(); };
    for (auto* c : {&inCh, &outCh}) { addChildComponent(c); pages[pgMidi].push_back(c); midiBox.push_back(c); }
    auto tog = [this](const char* t, std::atomic<bool>& flag) { auto* b = B(t, pgMidi, nullptr); b->setClickingTogglesState(true); b->setToggleState(flag, juce::dontSendNotification);
        b->onClick = [b, &flag] { flag = b->getToggleState(); }; midiBox.push_back(b); return b; };
    thruBtn = tog("THRU (keys + CCs to out)", proc.thru); clkBtn = tog("SEND MIDI CLOCK", proc.clockOut); syncBtn = tog("FOLLOW HOST TRANSPORT", proc.hostSync);
    pcBtn = tog("PROGRAM CHANGE = PRESET", proc.pcChangesPreset);
    learnBtn = B("MIDI LEARN", pgMidi, [this] { proc.learn = learnBtn->getToggleState(); proc.learnTarget = -1; lcd(proc.learn ? "Learn: click a control, then move a knob on your controller" : "MIDI learn off"); for (auto* k : knobs) k->repaint(); });
    learnBtn->setClickingTogglesState(true); midiBox.push_back(learnBtn);
    midiBox.push_back(B("DEFAULT CCs", pgMidi, [this] { proc.resetCC(); proc.learnChanged = true; }));
    midiBox.push_back(B("ALL NOTES OFF", pgMidi, [this] { for (int c = 1; c <= 16; c++) proc.keyboard.allNotesOff(c); proc.panicRequest = true; }));
    for (auto* l : {&ccText, &logText}) { l->setColour(juce::Label::backgroundColourId, LCDBG); l->setColour(juce::Label::textColourId, LCDC); l->setJustificationType(juce::Justification::topLeft);
        l->setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain))); addChildComponent(l); pages[pgMidi].push_back(l); }
    proc.learnChanged = true;

    kb.setAvailableRange(24, 108); kb.setOctaveForMiddleC(4); kb.setLowestVisibleKey(36); kb.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, ORG);
    kb.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, ORG.withAlpha(.3f)); addAndMakeVisible(kb);

    setResizable(true, true); setResizeLimits(760, 480, 2400, 1500); getConstrainer()->setFixedAspectRatio(1200.0 / 764.0);
    setSize(1200, 764);
    show(pgPanel); refreshSeq(); lcd(proc.presetName);
    startTimerHz(30);
}
SolinaEditor::~SolinaEditor() { setLookAndFeel(nullptr); }

void SolinaEditor::fillPresets() {
    preset.clear(juce::dontSendNotification); juce::String sec;
    for (int i = 0; i < kPresets; i++) { const juce::String n = juce::CharPointer_UTF8(PRESETS[i].name); const auto c = n.upToFirstOccurrenceOf(juce::CharPointer_UTF8(" \xc2\xb7 "), false, false);
        if (c != sec) { preset.addSectionHeading(c); sec = c; } preset.addItem(n.fromFirstOccurrenceOf(juce::CharPointer_UTF8(" \xc2\xb7 "), false, false), i + 1); }
    preset.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
}
void SolinaEditor::lcd(const juce::String& s) { lcdText = s; lcdHold = 75; lcdLabel.setText(s, juce::dontSendNotification); }
void SolinaEditor::learnPick(int id) { proc.learnTarget = id; lcd(juce::String("Learn: move a controller for ") + PARAMS[id].name); for (auto* k : knobs) k->repaint(); }

void SolinaEditor::show(int pg) {
    page = pg; if (pg == pgMidi) proc.learnChanged = true, timerCallback();
    for (int i = 0; i < 5; i++) { tabs[i].setToggleState(i == pg, juce::dontSendNotification); for (auto* c : pages[(size_t) i]) c->setVisible(i == pg); }
    resized(); repaint();
}
void SolinaEditor::refreshSeq() {
    Song s; { const juce::SpinLock::ScopedLockType l(proc.songLock); s = proc.song; }
    for (int i = 0; i < NPAT; i++) { patBtn[i].setToggleState(i == s.cur, juce::dontSendNotification); patBtn[i].setColour(juce::TextButton::buttonColourId, s.pats[i].notes.empty() ? juce::Colour(0xff262421) : juce::Colour(0xff4a3020)); }
    bars.setSelectedId(s.pats[s.cur].bars, juce::dontSendNotification);
    barView.clear(juce::dontSendNotification); for (int b = 0; b < s.pats[s.cur].bars; b++) barView.addItem("bar " + juce::String(b + 1) + " / " + juce::String(s.pats[s.cur].bars), b + 1);
    if (roll.view >= s.pats[s.cur].bars) roll.view = 0; barView.setSelectedId(roll.view + 1, juce::dontSendNotification);
    songModeBtn->setButtonText(s.mode ? "SONG" : "PATTERN"); songModeBtn->setToggleState(s.mode != 0, juce::dontSendNotification);
    chainEdBtn->setToggleState(chainEdit, juce::dontSendNotification); quantBtn->setToggleState(proc.quantize, juce::dontSendNotification);
    juce::String ch = "CHAIN:"; for (int c : s.chain) ch << " " << (c + 1); if (s.chain.empty()) ch << " (empty)"; chainLabel.setText(ch, juce::dontSendNotification);
    swing.setValue(s.swing, juce::dontSendNotification); bpm.setValue(s.bpm, juce::dontSendNotification);
    roll.repaint(); seenSong = proc.songVersion;
}

void SolinaEditor::timerCallback() {
    if (lcdHold > 0 && --lcdHold == 0) lcdLabel.setText(proc.presetName, juce::dontSendNotification);
    play.setToggleState(proc.playing, juce::dontSendNotification); play.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4caf50));
    if (proc.songVersion != seenSong) refreshSeq();
    if (proc.hostSync && proc.hostBpm > 0 && std::abs(bpm.getValue() - proc.hostBpm) > .5) bpm.setValue(proc.hostBpm, juce::dontSendNotification);
    if (page == pgSeq) { roll.repaint(); for (int i = 0; i < NPAT; i++) patBtn[i].setColour(juce::TextButton::textColourOffId, proc.playing && proc.playPat == i ? juce::Colour(0xff4caf50) : CREAM); }
    for (int s = 0; s < 6; s++) { static const int MIX[6] = {1, 0, 2, 3, 4, 5}; mixName[s].setColour(juce::Label::textColourId, proc.getParam(CH_TAB[MIX[s]]) >= .5 ? LCDC : juce::Colour(0xff5d6b4a)); }
    if (proc.learnChanged.exchange(false) || ccText.getText().isEmpty()) {
        juce::String t; for (auto& [c, p] : proc.ccMap) t << "CC " << c << "  " << PARAMS[p].name << "\n";
        t << "CC 1  Mod wheel\nCC 11  Expression\nCC 64  Sustain pedal\nCC 120/123  All notes off"; ccText.setText(t, juce::dontSendNotification);
        if (proc.learn) lcd("Learned"); for (auto* k : knobs) k->repaint();
    }
    if (page == pgMidi) { auto lg = proc.midiLog(); logText.setText(lg.isEmpty() ? "Incoming MIDI shows here." : lg.joinIntoString("\n"), juce::dontSendNotification); }
}

void SolinaEditor::paint(juce::Graphics& g) {
    g.fillAll(BG);
    g.setColour(juce::Colour(0xff4a2e17)); g.fillRect(0, (int) (getHeight() * .075f), getWidth(), 2);
    { auto lr = logoArea.reduced(2, 3); g.setColour(CREAM); g.drawRoundedRectangle(lr, 5, 2); g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultSerifFontName(), lr.getHeight() * .78f, juce::Font::bold | juce::Font::italic)));
      g.drawText("solina", lr.reduced(4, 0), juce::Justification::centred); }
    auto box = [&](juce::Rectangle<int> r, const juce::String& t, juce::Colour fill, juce::Colour edge, float th = 1) {
        g.setColour(fill); g.fillRoundedRectangle(r.toFloat(), 7); g.setColour(edge); g.drawRoundedRectangle(r.toFloat(), 7, th);
        if (t.isNotEmpty()) { g.setColour(DIM); g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold))); g.drawText(t, r.reduced(8, 4).removeFromTop(14), juce::Justification::left); } };
    for (auto& [r, t, k] : boxes) box(r, t, k == 1 ? juce::Colour(0xff121110) : k == 2 ? RX : PANEL, k == 1 ? WOOD : LINE, k == 1 ? 3.0f : 1.0f);
    if (page == pgPanel && !boxes.empty()) { auto r = std::get<0>(boxes[0]); g.setColour(CREAM); g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultSerifFontName(), r.getHeight() * .09f, juce::Font::bold | juce::Font::italic)));
        g.drawText("solina", r.reduced(14, 8), juce::Justification::bottomRight); }
}

void SolinaEditor::resized() {
    auto r = getLocalBounds(); const int H = getHeight();
    auto top = r.removeFromTop((int) (H * .075f)).reduced(6, 5); const int th = top.getHeight();
    logoArea = top.removeFromLeft((int) (th * 2.6f)).toFloat();
    for (int i = 4; i >= 0; i--) tabs[i].setBounds(top.removeFromRight((int) (th * 1.55f)).reduced(2, 0));
    top.removeFromRight(6);
    rec.setBounds(top.removeFromRight((int) (th * 1.15f)).reduced(2, 0)); stop.setBounds(top.removeFromRight((int) (th * 1.15f)).reduced(2, 0)); play.setBounds(top.removeFromRight((int) (th * 1.15f)).reduced(2, 0));
    bpm.setBounds(top.removeFromRight((int) (th * 2.3f)).reduced(2, 0));
    prev.setBounds(top.removeFromLeft((int) (th * .8f)).reduced(2, 0)); preset.setBounds(top.removeFromLeft(juce::jlimit(130, 300, top.getWidth() / 2)).reduced(2, 0));
    next.setBounds(top.removeFromLeft((int) (th * .8f)).reduced(2, 0)); dice.setBounds(top.removeFromLeft((int) (th * 1.9f)).reduced(2, 0));
    lcdLabel.setBounds(top.reduced(4, 0));
    kb.setBounds(r.removeFromBottom((int) (H * .2f)).reduced(6, 4));
    kb.setKeyWidth(juce::jmax(14.0f, kb.getWidth() / 36.0f));
    boxes.clear();
    auto pg = r.reduced(6);
    switch (page) { case pgPanel: layoutPanel(pg); break; case pgMixer: layoutMixer(pg); break; case pgFx: layoutFx(pg); break; case pgSeq: layoutSeq(pg); break; default: layoutMidi(pg); }
}
static void row(std::vector<juce::Component*> cs, juce::Rectangle<int> r, int gap = 4) {
    if (cs.empty()) return; const int w = (r.getWidth() - gap * ((int) cs.size() - 1)) / (int) cs.size();
    for (auto* c : cs) { c->setBounds(r.removeFromLeft(w)); r.removeFromLeft(gap); }
}
void SolinaEditor::layoutPanel(juce::Rectangle<int> r) {
    auto front = r.removeFromLeft((int) (r.getWidth() * .56f)); r.removeFromLeft(8);
    boxes.push_back({front, "STRINGS - BRASS - BASS", 1});
    auto f = front.reduced(12, 22);
    row({panelBox.begin(), panelBox.begin() + 6}, f.removeFromTop((int) (f.getHeight() * .36f)), 6); f.removeFromTop(8);
    auto sw = f.removeFromBottom(30); row({panelBox[11], panelBox[12], panelBox[13]}, sw.removeFromLeft(sw.getWidth() * 3 / 5), 6);
    row({panelBox.begin() + 6, panelBox.begin() + 11}, f.reduced(0, 4));
    auto e = r.removeFromTop(r.getHeight() / 2).withTrimmedBottom(4); boxes.push_back({e, "ENSEMBLE (CONTROL CIRCUIT)", 0});
    auto ei = e.reduced(8, 20); ensBox[4]->setBounds(ei.removeFromTop(24).removeFromRight(130));
    row({ensBox.begin(), ensBox.begin() + 4}, ei);
    auto p = r.withTrimmedTop(4); boxes.push_back({p, "PLAYING", 0});
    row(playBox, p.reduced(8, 22));
}
void SolinaEditor::layoutMixer(juce::Rectangle<int> r) {
    const int w = (r.getWidth() - 5 * 6) / 6;
    for (int s = 0; s < 6; s++) {
        auto st = r.removeFromLeft(w); r.removeFromLeft(6); boxes.push_back({st, "", 2});
        auto in = st.reduced(5); mixName[s].setBounds(in.removeFromTop(22)); in.removeFromTop(4);
        auto bt = in.removeFromBottom(26); row({mixS[s][0], mixS[s][1], mixS[s][2]}, bt, 3); in.removeFromBottom(4);
        auto fader = in.removeFromRight(in.getWidth() * 2 / 5); mixF[s]->setBounds(fader);
        const int kh = in.getHeight() / 4;
        for (int i = 0; i < 7; i++) { const int col = i < 4 ? 0 : 1, rw = i < 4 ? i : i - 4; mixK[s][(size_t) i]->setBounds(in.getX() + col * in.getWidth() / 2, in.getY() + rw * kh, in.getWidth() / 2, kh); }
    }
}
void SolinaEditor::layoutFx(juce::Rectangle<int> r) {
    const char* T[4] = {"PHASER", "ECHO", "REVERB", "MASTER"};
    auto rowA = r.removeFromTop(r.getHeight() / 2);
    juce::Rectangle<int> R4[4] = {rowA.removeFromLeft(rowA.getWidth() / 2).reduced(4), rowA.reduced(4), r.removeFromLeft(r.getWidth() / 2).reduced(4), r.reduced(4)};
    for (int i = 0; i < 4; i++) {
        boxes.push_back({R4[i], T[i], 0}); auto in = R4[i].reduced(8, 4); auto head = in.removeFromTop(24); in.removeFromTop(4);
        std::vector<juce::Component*> ks, ss; for (auto* c : fxBox[i]) (dynamic_cast<Knob*>(c) ? ks : ss).push_back(c);
        for (auto* s : ss) s->setBounds(head.removeFromRight(100).reduced(2, 0));
        row(ks, in);
    }
}
void SolinaEditor::layoutSeq(juce::Rectangle<int> r) {
    auto top = r.removeFromTop(30); barView.setBounds(top.removeFromRight(110).reduced(2, 0)); bars.setBounds(top.removeFromRight(90).reduced(2, 0));
    std::vector<juce::Component*> pb; for (auto& b : patBtn) pb.push_back(&b); row(pb, top, 2);
    r.removeFromTop(6);
    auto side = r.removeFromRight(juce::jmin(260, r.getWidth() / 4)); r.removeFromRight(6);
    roll.setBounds(r);
    const int lh = juce::jmax(22, side.getHeight() / 11);
    auto line = [&] { auto l = side.removeFromTop(lh).reduced(0, 2); return l; };
    row({seqSide[0], seqSide[1]}, line()); seqSide[2]->setBounds(line()); row({seqSide[3], seqSide[4], seqSide[5]}, line());
    row({seqSide[6], seqSide[7]}, line()); row({seqSide[8], seqSide[9]}, line()); row({seqSide[10], seqSide[11]}, line());
    row({seqSide[12], seqSide[13], seqSide[14]}, line()); row({seqSide[15], seqSide[16]}, line()); seqSide[17]->setBounds(line());
}
void SolinaEditor::layoutMidi(juce::Rectangle<int> r) {
    auto a = r.removeFromLeft(r.getWidth() / 3).reduced(4); auto b = r.removeFromLeft(r.getWidth() / 2).reduced(4); auto c = r.reduced(4);
    boxes.push_back({a, "PORTS AND SETTINGS", 0}); boxes.push_back({b, "CONTROLLERS (CC)", 0}); boxes.push_back({c, "MONITOR", 0});
    auto in = a.reduced(8, 22); const int lh = juce::jmax(24, in.getHeight() / (int) midiBox.size());
    row({midiBox[0], midiBox[1]}, in.removeFromTop(lh).reduced(0, 2));
    for (size_t i = 2; i < midiBox.size(); i++) midiBox[i]->setBounds(in.removeFromTop(lh).reduced(0, 2));
    ccText.setBounds(b.reduced(8, 22)); logText.setBounds(c.reduced(8, 22));
}

} // namespace solina
