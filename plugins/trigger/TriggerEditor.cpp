#include "TriggerEditor.h"

namespace trig {

static const juce::Colour kPanel(0xff16181d), kInk(0xffe8eaf0), kDim(0xff8b91a1), kAccent(0xffff5a1f), kGood(0xff46e08a),
    kBad(0xffff3b4e), kWell(0xff0d0e12), kLine(0xff2b2f3a);
static juce::Font font(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }
static constexpr int W = 940, H = 700;
static constexpr float kDbLo = -72, kDbHi = 0;

TrigLook::TrigLook() {
    setColour(juce::ResizableWindow::backgroundColourId, kPanel);
    setColour(juce::Label::textColourId, kInk);
    setColour(juce::Slider::textBoxTextColourId, kInk);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, kWell);
    setColour(juce::ComboBox::outlineColourId, kLine);
    setColour(juce::ComboBox::textColourId, kInk);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1a1d24));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2e38));
    setColour(juce::TextButton::buttonOnColourId, kAccent);
    setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc8ccd6));
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xff111318));
    setColour(juce::ToggleButton::textColourId, kDim);
    setColour(juce::ToggleButton::tickColourId, kAccent);
    setColour(juce::ToggleButton::tickDisabledColourId, kDim);
}

void TrigLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    const auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4);
    const float rad = juce::jmin(r.getWidth(), r.getHeight()) / 2, cx = r.getCentreX(), cy = r.getCentreY();
    juce::Path track; track.addCentredArc(cx, cy, rad - 2, rad - 2, 0, a0, a1, true);
    g.setColour(juce::Colour(0xff323747)); g.strokePath(track, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ang = a0 + pos * (a1 - a0);
    juce::Path arc; arc.addCentredArc(cx, cy, rad - 2, rad - 2, 0, a0, ang, true);
    g.setColour(kAccent); g.strokePath(arc, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float k = rad - 7;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4f5c), cx, cy - k, juce::Colour(0xff15171c), cx, cy + k, false));
    g.fillEllipse(cx - k, cy - k, k * 2, k * 2);
    g.setColour(juce::Colour(0xff0b0c0f)); g.drawEllipse(cx - k, cy - k, k * 2, k * 2, 1.2f);
    juce::Path p; p.addRoundedRectangle(-1.5f, -k + 2, 3.f, k * .55f, 1.5f);
    g.setColour(juce::Colour(0xfff2f2f2)); g.fillPath(p, juce::AffineTransform::rotation(ang).translated(cx, cy));
}

void TrigLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.5f);
    const bool on = b.getToggleState();
    g.setColour(on ? kAccent : juce::Colour(over ? 0xff353a46 : 0xff2a2e38).darker(down ? .2f : 0));
    g.fillRoundedRectangle(r, 5);
    g.setColour(juce::Colour(0xff0b0c0f)); g.drawRoundedRectangle(r, 5, 1);
}

// ---------------- scope ----------------

float Scope::yOf(float db) const {
    const float h = (float) getHeight() - 20;
    return 10 + (1 - (juce::jlimit(kDbLo, kDbHi, db) - kDbLo) / (kDbHi - kDbLo)) * h;
}
float Scope::dbAt(float y) const {
    const float h = (float) getHeight() - 20;
    return kDbLo + (1 - (y - 10) / h) * (kDbHi - kDbLo);
}

void Scope::pull() {
    std::vector<HitInfo> nh;
    ed.proc.pullDisplay(cols, nh, lastEnd, colLen);
    for (auto& h : nh) { hits.push_back(h); if (h.rejected) ++bleedShown; else ++hitsShown; }
    const size_t keep = (size_t) std::max(1, getWidth());
    if (cols.size() > keep) cols.erase(cols.begin(), cols.begin() + (long) (cols.size() - keep));
    const int64_t oldest = lastEnd - (int64_t) keep * colLen;
    hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const HitInfo& h) { return h.onset < oldest; }), hits.end());
}

void Scope::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();
    g.setColour(kWell); g.fillRoundedRectangle(b, 6);
    g.setFont(font(10));
    for (float db = -60; db <= 0; db += 12) {
        const float y = yOf(db);
        g.setColour(kLine); g.drawHorizontalLine((int) y, 34, b.getRight() - 4);
        g.setColour(kDim.withAlpha(.7f)); g.drawText(juce::String((int) db), 2, (int) y - 6, 28, 12, juce::Justification::centredRight);
    }
    // envelope, newest at the right
    const int w = getWidth(), n = (int) cols.size();
    juce::Path env; bool started = false;
    for (int i = 0; i < n; ++i) {
        const float x = (float) (w - n + i);
        if (x < 34) continue;
        const float y = yOf(20 * std::log10(std::max(cols[(size_t) i], 1e-6f)));
        if (!started) { env.startNewSubPath(x, (float) getHeight() - 10); started = true; }
        env.lineTo(x, y);
    }
    if (started) {
        env.lineTo((float) w, (float) getHeight() - 10); env.closeSubPath();
        g.setColour(kAccent.withAlpha(.22f)); g.fillPath(env);
        g.setColour(kAccent.withAlpha(.9f)); g.strokePath(env, juce::PathStrokeType(1.f));
    }
    // hits: green with the velocity, red if the sidechain called it bleed
    for (const auto& h : hits) {
        const float x = (float) w - (float) (lastEnd - h.onset) / (float) colLen;
        if (x < 34) continue;
        g.setColour(h.rejected ? kBad : kGood);
        g.drawVerticalLine((int) x, 10, (float) getHeight() - 10);
        g.fillEllipse(x - 3, yOf(h.peakDb) - 3, 6, 6);
        g.setFont(font(10, true));
        g.drawText(h.rejected ? juce::String("bleed") : juce::String(h.velocity), (int) x + 3, 10, 40, 12, juce::Justification::left);
    }
    // threshold (drag it)
    const float ty = yOf(ed.proc.apvts.getRawParameterValue("thresh")->load());
    g.setColour(juce::Colours::white.withAlpha(dragging ? .95f : .7f));
    const float dash[] = { 6, 4 };
    g.drawDashedLine(juce::Line<float>(34, ty, b.getRight() - 4, ty), dash, 2, 1.2f);
    g.setFont(font(11, true));
    g.drawText("THRESHOLD " + juce::String(ed.proc.apvts.getRawParameterValue("thresh")->load(), 1) + " dB  (drag)", w - 240, (int) ty - 16, 232, 14, juce::Justification::right);
}

void Scope::mouseDown(const juce::MouseEvent& e) {
    if (auto* p = ed.proc.apvts.getParameter("thresh")) { p->beginChangeGesture(); dragging = true; mouseDrag(e); }
}
void Scope::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging) return;
    if (auto* p = ed.proc.apvts.getParameter("thresh")) p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(-70.f, 0.f, dbAt((float) e.y))));
    repaint();
}
void Scope::mouseUp(const juce::MouseEvent&) {
    if (dragging) if (auto* p = ed.proc.apvts.getParameter("thresh")) p->endChangeGesture();
    dragging = false;
}

// ---------------- drag box ----------------

void DragBox::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(enabled ? kAccent.withAlpha(.18f) : kWell); g.fillRoundedRectangle(r, 5);
    const float dash[] = { 5, 3 };
    juce::Path p; p.addRoundedRectangle(r, 5);
    juce::Path d; juce::PathStrokeType(1.2f).createDashedStroke(d, p, dash, 2);
    g.setColour(enabled ? kAccent : kLine); g.fillPath(d);
    g.setColour(enabled ? kInk : kDim); g.setFont(font(12, true));
    g.drawText(text, getLocalBounds(), juce::Justification::centred);
}
void DragBox::mouseDrag(const juce::MouseEvent& e) {
    if (started || !enabled || !make || e.getDistanceFromDragStart() < 6) return;
    started = true;
    const auto f = make();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
}

// ---------------- editor ----------------

TriggerEditor::Knob& TriggerEditor::knob(const char* id, const juce::String& name) {
    auto k = std::make_unique<Knob>();
    k->s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k->s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 16);
    k->s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->s.setColour(juce::Slider::textBoxTextColourId, kInk);
    k->l.setText(name, juce::dontSendNotification);
    k->l.setJustificationType(juce::Justification::centred);
    k->l.setFont(font(11, true)); k->l.setColour(juce::Label::textColourId, kDim);
    k->a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, k->s);
    addAndMakeVisible(k->s); addAndMakeVisible(k->l);
    knobs.push_back(std::move(k));
    return *knobs.back();
}

TriggerEditor::TriggerEditor(TriggerProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&look);
    addAndMakeVisible(scope);
    for (auto* b : { &kick, &snare, &tom }) addAndMakeVisible(*b);
    kick.onClick = [this] { proc.applyPreset(Preset::kick); setStatus("Kick: listens to the beater attack (600 Hz up). Now press LEARN THRESHOLD while the track plays, or drag the threshold line."); };
    snare.onClick = [this] { proc.applyPreset(Preset::snare); setStatus("Snare: listens to the crack and wires (150 Hz - 6 kHz). Now LEARN THRESHOLD while it plays, or drag the line."); };
    tom.onClick = [this] { proc.applyPreset(Preset::tom); setStatus("Tom: 70 Hz - 5 kHz. LEARN THRESHOLD while it plays, or drag the line."); };
    addAndMakeVisible(learn);
    learn.onClick = [this] {
        float db;
        if (proc.learnThreshold(db)) {
            if (auto* q = proc.apvts.getParameter("thresh")) q->setValueNotifyingHost(q->convertTo0to1(db));
            setStatus("Threshold set to " + juce::String(db, 1) + " dB: in the gap between the hits and the bleed it heard (" + juce::String(proc.candidatesHeard()) + " events).");
        } else
            setStatus("No clear gap between hits and bleed yet (" + juce::String(proc.candidatesHeard()) + " events heard). Play a few bars with soft and loud hits, then try again, or drag the line.");
    };

    knob("thresh", "THRESHOLD"); knob("sens", "SENSITIVITY"); knob("retrig", "RETRIGGER"); knob("hp", "HIGH-PASS"); knob("lp", "LOW-PASS");
    knob("sideratio", "BLEED LEVEL");
    knob("note", "NOTE"); knob("chan", "CHANNEL"); knob("len", "LENGTH"); knob("velceil", "VEL CEILING"); knob("velrange", "VEL RANGE"); knob("dyn", "DYNAMICS");
    knob("nudge", "REC NUDGE");

    side.setClickingTogglesState(true);
    sideA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "side", side);
    addAndMakeVisible(side);
    monitor.addItemList({ "Input", "Detection band", "Trigger clicks", "Mute" }, 1);
    monitorA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "monitor", monitor);
    addAndMakeVisible(monitor);
    monitorL.setText("MONITOR", juce::dontSendNotification); monitorL.setFont(font(11, true)); monitorL.setColour(juce::Label::textColourId, kDim);
    monitorL.setJustificationType(juce::Justification::centred); addAndMakeVisible(monitorL);

    // recorder
    recBtn.setClickingTogglesState(true); recBtn.setToggleState(proc.recording.load(), juce::dontSendNotification);
    recBtn.onClick = [this] { proc.recording = recBtn.getToggleState(); };
    clearBtn.onClick = [this] { proc.clearRecorded(); };
    saveRec.onClick = [this] { saveMidi(false); };
    for (auto* c : std::initializer_list<juce::Component*> { &recBtn, &clearBtn, &saveRec, &recInfo, &dragRec }) addAndMakeVisible(c);
    dragRec.make = [this] {
        const auto h = proc.recorded();
        return proc.writeTempMidi(h, true, proc.recordedBpm(), "Drum Trigger " + juce::String(proc.apvts.getRawParameterValue("note")->load(), 0));
    };
    // file analysis
    analyzeBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Drum track to analyze", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f.existsAsFile()) proc.analyzeFile(f, learnFile.getToggleState());
        });
    };
    saveAn.onClick = [this] { saveMidi(true); };
    learnFile.setToggleState(true, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &analyzeBtn, &saveAn, &learnFile, &anInfo, &dragAn, &status }) addAndMakeVisible(c);
    dragAn.make = [this] {
        const double bpm = proc.hostBpm() > 0 ? proc.hostBpm() : 120;
        return proc.writeTempMidi(proc.analyzed(), false, bpm, "Drum Trigger file " + juce::String(proc.apvts.getRawParameterValue("note")->load(), 0));
    };
    for (auto* l : { &recInfo, &anInfo, &status }) { l->setFont(font(12)); l->setColour(juce::Label::textColourId, kInk); l->setMinimumHorizontalScale(0.75f); }
    status.setColour(juce::Label::textColourId, kDim);
    setStatus("Put the plugin on the kick (or snare) track, pick KICK or SNARE, play the song and press LEARN THRESHOLD. "
              "Green lines are hits; tune until every hit has one and nothing else does.");

    setResizable(true, true);
    setResizeLimits(W * 3 / 4, H * 3 / 4, W * 2, H * 2);
    getConstrainer()->setFixedAspectRatio((double) W / H);
    setSize(W, H);
    startTimerHz(30);
}

TriggerEditor::~TriggerEditor() { stopTimer(); setLookAndFeel(nullptr); }

void TriggerEditor::saveMidi(bool analyzed) {
    const auto hits = analyzed ? proc.analyzed() : proc.recorded();
    if (hits.empty()) { setStatus(analyzed ? "Analyze a file first." : "Nothing recorded yet: play the song with REC on."); return; }
    chooser = std::make_unique<juce::FileChooser>("Save MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Drum Trigger.mid"), "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, hits, analyzed](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension(".mid");
        const double bpm = analyzed ? (proc.hostBpm() > 0 ? proc.hostBpm() : 120) : proc.recordedBpm();
        f.deleteFile();
        juce::FileOutputStream os(f);
        if (os.openedOk() && proc.makeMidi(hits, !analyzed, bpm).writeTo(os)) setStatus("Saved " + f.getFullPathName());
        else setStatus("Couldn't write " + f.getFullPathName());
    });
}

void TriggerEditor::timerCallback() {
    scope.pull(); scope.repaint();
    const auto rec = proc.recorded();
    recInfo.setText(juce::String((int) rec.size()) + " hits recorded" + (proc.recording.load() ? "  (records while the DAW plays)" : "  (paused)"), juce::dontSendNotification);
    dragRec.enabled = !rec.empty(); dragRec.repaint();
    const auto an = proc.analyzed();
    anInfo.setText(proc.analysisStatus().isEmpty() ? juce::String("Bounce the drum track, then analyze the file: MIDI for the whole song at once.") : proc.analysisStatus(), juce::dontSendNotification);
    dragAn.enabled = !an.empty() && !proc.analysisRunning(); dragAn.repaint();
    analyzeBtn.setEnabled(!proc.analysisRunning());
}

void TriggerEditor::paint(juce::Graphics& g) {
    g.fillAll(kPanel);
    const float sx = getWidth() / (float) W;
    g.addTransform(juce::AffineTransform::scale(sx));
    g.setColour(kInk); g.setFont(font(22, true));
    g.drawText("OMEGA", 20, 14, 90, 28, juce::Justification::left);
    g.setColour(kAccent); g.drawText("DRUM TRIGGER", 104, 14, 200, 28, juce::Justification::left);
    g.setColour(kDim); g.setFont(font(11));
    g.drawText("audio to MIDI for sample replacement  |  latency " + juce::String(proc.latencyMs10() / 10.0, 1) + " ms (compensated by the DAW)", 20, 40, 520, 14, juce::Justification::left);
    g.setColour(kGood); g.setFont(font(12, true));
    g.drawText(juce::String(scope.hitsShown) + " hits" + (scope.bleedShown ? "   " + juce::String(scope.bleedShown) + " bleed rejected" : juce::String()), W - 330, 40, 310, 14, juce::Justification::right);
    auto head = [&](const char* t, int x, int y, int w) {
        g.setColour(kDim); g.setFont(font(11, true)); g.drawText(t, x, y, w, 14, juce::Justification::left);
        g.setColour(kLine); g.drawHorizontalLine(y + 16, (float) x, (float) (x + w));
    };
    head("DETECTION", 20, 318, 520); head("BLEED (SIDECHAIN)", 560, 318, 360);
    head("MIDI OUT", 20, 452, 900);
    head("HIT RECORDER  -  plays along with the song", 20, 588, 440); head("ANALYZE FILE  -  whole song at once", 480, 588, 440);
}

void TriggerEditor::resized() {
    const float sx = getWidth() / (float) W;
    auto place = [&](juce::Component& c, int x, int y, int w, int h) {
        c.setTransform(juce::AffineTransform::scale(sx));
        c.setBounds(x, y, w, h);
    };
    place(kick, 560, 14, 64, 26); place(snare, 628, 14, 64, 26); place(tom, 696, 14, 64, 26); place(learn, 772, 14, 148, 26);
    place(scope, 20, 62, 900, 248);
    auto putKnob = [&](int i, int x, int y) { place(knobs[(size_t) i]->l, x, y, 96, 14); place(knobs[(size_t) i]->s, x, y + 14, 96, 92); };
    for (int i = 0; i < 5; ++i) putKnob(i, 20 + i * 104, 338);
    place(side, 572, 370, 120, 28); putKnob(5, 720, 338);
    status.setJustificationType(juce::Justification::topLeft);
    for (int i = 0; i < 6; ++i) putKnob(6 + i, 20 + i * 104, 472);
    place(monitorL, 650, 472, 130, 14); place(monitor, 650, 500, 130, 26);
    putKnob(12, 810, 472);
    // recorder
    place(recBtn, 20, 610, 60, 28); place(clearBtn, 86, 610, 64, 28); place(saveRec, 156, 610, 84, 28); place(dragRec, 246, 610, 214, 28);
    place(recInfo, 20, 642, 440, 18);
    // analysis
    place(analyzeBtn, 480, 610, 120, 28); place(learnFile, 606, 610, 150, 28); place(saveAn, 760, 610, 80, 28); place(dragAn, 846, 610, 74, 28);
    dragAn.text = "DRAG";
    place(anInfo, 480, 642, 440, 18);
    place(status, 20, 664, 900, 32);
}

} // namespace trig
