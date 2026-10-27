#include "TwelveEditor.h"

namespace twelve {

static const juce::Colour kPanel(0xffc7c9cb), kInk(0xff1d1f22), kMute(0xff55595f), kLcd(0xff0b0e0d), kGlow(0xff7dffb0), kGlowDim(0xff3d8a62),
    kAccent(0xffe2a83a), kStop(0xffd8402f), kCard(0xffd3d5d7);
static juce::Font font(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }
static juce::Font mono(float h) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain)); }

TwelveLook::TwelveLook() {
    setColour(juce::Slider::textBoxTextColourId, kInk);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, kInk);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xfff4f3f0));
    setColour(juce::ComboBox::textColourId, kInk);
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff8f9297));
    setColour(juce::ComboBox::arrowColourId, kInk);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff202224));
    setColour(juce::PopupMenu::textColourId, juce::Colour(0xffe8e8e8));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kGlow);
    setColour(juce::PopupMenu::highlightedTextColourId, kLcd);
    setColour(juce::TextButton::textColourOffId, kInk);
    setColour(juce::TextButton::textColourOnId, kInk);
    setColour(juce::ToggleButton::textColourId, kInk);
    setColour(juce::ToggleButton::tickColourId, kInk);
    setColour(juce::Label::backgroundWhenEditingColourId, juce::Colours::white);
    setColour(juce::TextEditor::textColourId, kInk);
}
void TwelveLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    const auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(3);
    const float rad = juce::jmin(r.getWidth(), r.getHeight()) / 2, cx = r.getCentreX(), cy = r.getCentreY(), ang = a0 + pos * (a1 - a0);
    juce::Path arc; arc.addCentredArc(cx, cy, rad - 1.5f, rad - 1.5f, 0, a0, ang, true);
    g.setColour(kAccent); g.strokePath(arc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float k = rad - 6;
    g.setColour(juce::Colours::black.withAlpha(.25f)); g.fillEllipse(cx - k + 1, cy - k + 3, k * 2, k * 2);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffbfaf6), cx, cy - k, juce::Colour(0xffd9d7d0), cx, cy + k, false));
    g.fillEllipse(cx - k, cy - k, k * 2, k * 2);
    g.setColour(juce::Colour(0xff8f9297)); g.drawEllipse(cx - k, cy - k, k * 2, k * 2, .8f);
    juce::Path p; p.addRoundedRectangle(-1.3f, -k + 2, 2.6f, k * .6f, 1.3f);
    g.setColour(kStop); g.fillPath(p, juce::AffineTransform::rotation(ang).translated(cx, cy));
}
void TwelveLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.f);
    const bool on = b.getToggleState(), stop = b.getName() == "stop", pri = b.getName() == "pri";
    juce::Colour top = juce::Colour(0xfff6f5f2), bot = juce::Colour(0xffdcdbd6);
    if (on) { top = juce::Colour(0xffffd27a); bot = kAccent; }
    if (stop) { top = juce::Colour(0xfff0675a); bot = kStop; }
    if (pri) { top = juce::Colour(0xff3c3f44); bot = juce::Colour(0xff222428); }
    if (over) { top = top.brighter(.05f); }
    if (down) r = r.translated(0, 1);
    g.setGradientFill(juce::ColourGradient(top, 0, r.getY(), bot, 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, 5);
    g.setColour(juce::Colour(0xff8f9297)); g.drawRoundedRectangle(r, 5, 1);
}

/* ---------------- display ---------------- */
std::pair<int64_t, int64_t> Lcd::view() const { const auto s = proc.selection(); return zoom ? s : std::make_pair<int64_t, int64_t>(0, proc.sourceLength()); }
int64_t Lcd::sampleAt(int x) const {
    const auto a = waveArea(); const auto v = view();
    return juce::jlimit<int64_t>(0, proc.sourceLength(), v.first + (int64_t) ((double) (x - a.getX()) / a.getWidth() * (double) (v.second - v.first)));
}
void Lcd::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff2a2c2e)); g.fillRoundedRectangle(r, 6);
    g.setColour(kLcd); g.fillRoundedRectangle(r.reduced(5), 4);
    const auto p = proc.snapshot();
    auto in = getLocalBounds().reduced(14, 10);
    g.setFont(mono(10)); g.setColour(kGlowDim); g.drawText(juce::String::fromUTF8("\xe2\x8a\x99 OMEGA SYSTEM \xc2\xb7 TWELVE"), in.removeFromTop(14), juce::Justification::left);
    const auto nm = proc.presetName().toUpperCase();
    auto tag = juce::Rectangle<int>(getWidth() - 18 - juce::jmin(260, 12 + nm.length() * 7), 10, juce::jmin(260, 12 + nm.length() * 7), 14);
    g.setColour(kGlow); g.fillRect(tag); g.setColour(kLcd); g.drawText(nm, tag, juce::Justification::centred, true);
    auto cell = [&](juce::Rectangle<int> c, const juce::String& lab, const juce::String& val, bool hi) {
        g.setFont(mono(9)); g.setColour(kGlowDim); g.drawText(lab, c.removeFromTop(12), juce::Justification::left);
        if (hi) { g.setColour(kGlow); g.fillRect(c.withWidth(juce::jmin(c.getWidth() - 6, 14 + val.length() * 11)).toFloat()); g.setColour(kLcd); }
        else g.setColour(kGlow);
        g.setFont(mono(19)); g.drawText(val, c.translated(3, 0), juce::Justification::centredLeft, true);
    };
    const int w = in.getWidth();
    auto row = [&](const juce::String (&lab)[4], const juce::String (&val)[4]) {
        auto rr = in.removeFromTop(38); const int cw[4] = { w * 32 / 100, w * 22 / 100, w * 24 / 100, w * 22 / 100 };
        for (int i = 0; i < 4; ++i) cell(rr.removeFromLeft(cw[i]), lab[i], val[i], i == 0);
    };
    const int ft = (int) p[pid::ftype];
    const double st = p[pid::semis] + p[pid::cents] / 100;
    row({"SAMPLER", "BITS", "RATE", "TUNE"}, {fmtVal(pid::machine, p[pid::machine]), juce::String((int) p[pid::bits]), juce::String(p[pid::rate] / 1000, 1) + "k", (st > 0 ? "+" : "") + juce::String(st, p[pid::cents] != 0 ? 2 : 0)});
    juce::String fname = fmtVal(pid::ftype, ft).upToFirstOccurrenceOf(" (", false, false);
    row({"FILTER", "CUTOFF", "RESONANCE", "MIX"}, {fname, ft >= 1 && ft <= 5 ? juce::String("FIXED") : fmtVal(pid::cutoff, p[pid::cutoff]), fmtVal(pid::res, p[pid::res]), fmtVal(pid::mix, p[pid::mix])});
    g.setColour(juce::Colour(0xff1f2a25)); g.drawHorizontalLine(in.getY() + 2, (float) in.getX(), (float) in.getRight());
    g.setFont(mono(11)); g.setColour(kGlow); g.drawText(info, in.removeFromTop(20).translated(0, 4), juce::Justification::left, true);
    // waveform
    const auto wa = waveArea(); g.setColour(juce::Colour(0xff1f2a25)); g.drawRect(wa);
    if (!proc.hasSource()) { g.setColour(kGlowDim); g.setFont(mono(12)); g.drawText("Import a sample (WAV, AIFF, FLAC, OGG, MP3...) or drop one here", wa, juce::Justification::centred); return; }
    const auto v = view(); const auto s = proc.selection(); const double span = (double) juce::jmax<int64_t>(1, v.second - v.first);
    auto X = [&](int64_t i) { return (float) wa.getX() + (float) ((double) (i - v.first) / span * wa.getWidth()); };
    g.setColour(juce::Colour(0xff12301f)); g.fillRect(juce::Rectangle<float>(X(s.first), (float) wa.getY(), X(s.second) - X(s.first), (float) wa.getHeight()));
    std::vector<float> mn, mx; proc.getPeaks(mn, mx, v.first, v.second, wa.getWidth());
    const float cy = (float) wa.getCentreY(), hh = wa.getHeight() * .5f;
    for (int x = 0; x < wa.getWidth(); ++x) {
        const int64_t at = v.first + (int64_t) (span * x / wa.getWidth());
        g.setColour(kGlow.withAlpha(at >= s.first && at <= s.second ? 1.f : .35f));
        g.fillRect((float) (wa.getX() + x), cy - mx[(size_t) x] * hh, 1.f, juce::jmax(1.f, (mx[(size_t) x] - mn[(size_t) x]) * hh));
    }
    g.setColour(kAccent);
    for (auto e : {s.first, s.second}) { g.fillRect(X(e) - 1, (float) wa.getY(), 2.f, (float) wa.getHeight()); g.fillRect(X(e) - 5, (float) wa.getY(), 10.f, 7.f); }
    if (proc.isPlaying()) { g.setColour(juce::Colours::white); g.fillRect(X(s.first + (int64_t) (proc.playFraction() * (double) (s.second - s.first))), (float) wa.getY(), 1.5f, (float) wa.getHeight()); }
    if (proc.isRendering()) { g.setColour(kGlow); g.setFont(mono(10)); g.drawText("RENDERING", wa.reduced(4), juce::Justification::topRight); }
}
void Lcd::mouseDown(const juce::MouseEvent& e) {
    if (!proc.hasSource() || !waveArea().contains(e.getPosition())) { mode = 0; return; }
    const auto s = proc.selection(); const auto wa = waveArea(); const auto v = view(); const double span = (double) juce::jmax<int64_t>(1, v.second - v.first);
    auto X = [&](int64_t i) { return wa.getX() + (int) ((double) (i - v.first) / span * wa.getWidth()); };
    startX = e.x; startI = sampleAt(e.x); moved = false;
    mode = std::abs(e.x - X(s.first)) < 10 ? 1 : std::abs(e.x - X(s.second)) < 10 ? 2 : 3;
}
void Lcd::mouseDrag(const juce::MouseEvent& e) {
    if (!mode) return;
    if (std::abs(e.x - startX) > 3) moved = true;
    if (!moved) return;
    const auto s = proc.selection(); const int64_t i = sampleAt(e.x), minLen = (int64_t) (proc.sourceSr() * .005);
    if (mode == 1) proc.setSelection(juce::jmin(i, s.second - minLen), s.second);
    else if (mode == 2) proc.setSelection(s.first, juce::jmax(i, s.first + minLen));
    else proc.setSelection(juce::jmin(startI, i), juce::jmax(juce::jmax(startI, i), juce::jmin(startI, i) + minLen));
}
void Lcd::mouseUp(const juce::MouseEvent& e) {
    if (!mode) return;
    if (!moved) { const auto s = proc.selection(); proc.play((double) (sampleAt(e.x) - s.first) / (double) juce::jmax<int64_t>(1, s.second - s.first)); }
    else if (onSelection) onSelection();
    mode = 0;
}

/* ---------------- editor ---------------- */
juce::TextButton& TwelveEditor::button(const juce::String& text, std::function<void()> f) {
    auto b = std::make_unique<juce::TextButton>(text); b->onClick = f; addAndMakeVisible(*b);
    b->getProperties().set("btn", true);
    auto& ref = *b; btn[text] = b.get(); buttons.push_back(std::move(b)); return ref;
}
void TwelveEditor::addKnob(int id, juce::String section) {
    auto s = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow);
    s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 14);
    s->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s->setDoubleClickReturnValue(true, (double) PARAMS[id].def);
    addAndMakeVisible(*s);
    sAtt.push_back(std::make_unique<juce::SliderParameterAttachment>(*proc.apvts.getParameter(PARAMS[id].id), *s));
    auto l = std::make_unique<juce::Label>("", PARAMS[id].name); l->setFont(font(10)); l->setJustificationType(juce::Justification::centred); l->setColour(juce::Label::textColourId, kMute); addAndMakeVisible(*l);
    ctls.push_back({id, section, std::move(s), std::move(l)});
}
void TwelveEditor::addCombo(int id, juce::String section) {
    auto c = std::make_unique<juce::ComboBox>(); addAndMakeVisible(*c);
    int k = 1; for (auto* s : PARAMS[id].choices) c->addItem(juce::CharPointer_UTF8(s), k++);
    cAtt.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(*proc.apvts.getParameter(PARAMS[id].id), *c));
    auto l = std::make_unique<juce::Label>("", PARAMS[id].name); l->setFont(font(10)); l->setColour(juce::Label::textColourId, kMute); addAndMakeVisible(*l);
    ctls.push_back({id, section, std::move(c), std::move(l)});
}
void TwelveEditor::addToggle(int id, juce::String section) {
    auto b = std::make_unique<juce::TextButton>(PARAMS[id].name); b->setClickingTogglesState(true); addAndMakeVisible(*b);
    bAtt.push_back(std::make_unique<juce::ButtonParameterAttachment>(*proc.apvts.getParameter(PARAMS[id].id), *b));
    ctls.push_back({id, section, std::move(b), nullptr});
}

TwelveEditor::TwelveEditor(TwelveProcessor& p) : AudioProcessorEditor(p), proc(p), lcd(p) {
    setLookAndFeel(&look);
    addAndMakeVisible(lcd);
    lcd.onSelection = [] {};
    // presets: 25 families of 20, as submenus
    {
        auto* root = presets.getRootMenu(); juce::String fam; juce::PopupMenu sub;
        for (int i = 0; i < kPresets; ++i) {
            const juce::String f = juce::CharPointer_UTF8(PRESETS[i].family);
            if (f != fam) { if (fam.isNotEmpty()) root->addSubMenu(fam, sub); sub = juce::PopupMenu(); fam = f; }
            sub.addItem(i + 1, juce::CharPointer_UTF8(PRESETS[i].name));
        }
        root->addSubMenu(fam, sub);
    }
    presets.setTextWhenNothingSelected("Presets (500)");
    presets.onChange = [this] { const int id = presets.getSelectedId(); if (id > 0) proc.setCurrentProgram(id - 1); };
    addAndMakeVisible(presets);
    button(juce::String::fromUTF8("\xe2\x97\x80"), [this] { proc.setCurrentProgram((proc.getCurrentProgram() + kPresets - 1) % kPresets); presets.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification); });
    button(juce::String::fromUTF8("\xe2\x96\xb6"), [this] { proc.setCurrentProgram((proc.getCurrentProgram() + 1) % kPresets); presets.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification); });
    button("Random", [this] { proc.randomize(); presets.setSelectedId(0, juce::dontSendNotification); toast("Random: " + proc.presetName()); });

    auto openFile = [this](const juce::String& title, std::function<void(juce::File)> then) {
        chooser = std::make_unique<juce::FileChooser>(title, juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.caf;*.wma;*.mp4");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [then](const juce::FileChooser& c) { if (c.getResult().existsAsFile()) then(c.getResult()); });
    };
    button("Import", [this, openFile] { openFile("Import a sample", [this](juce::File f) { job([this, f] { auto e = proc.loadFile(f); if (e.isNotEmpty()) return e; return "Loaded " + f.getFileName() + " · " + proc.analyze(); }); }); }).setName("pri");
    button("Play", [this] { if (proc.isPlaying()) proc.stopAll(); else proc.play(0); });
    button("Loop", [this] { proc.loop = !proc.loop; });
    button("STOP ALL", [this] { proc.stopAll(); }).setName("stop");
    liveBtn.setTooltip("Run the plugin's audio input through the sampler model"); addAndMakeVisible(liveBtn);
    liveAtt = std::make_unique<juce::ButtonParameterAttachment>(*proc.apvts.getParameter("live"), liveBtn);
    button("Select all", [this] { proc.setSelection(0, proc.sourceLength()); lcd.zoom = false; });
    button("Zoom", [this] { lcd.zoom = !lcd.zoom; });
    button("Truncate", [this] { lcd.zoom = false; toast(proc.truncate()); });
    button("Normalize", [this] { toast(proc.normalize()); });
    button("Reverse", [this] { toast(proc.reverse()); });
    button("Undo", [this] { toast(proc.undo()); });
    button("Resample", [this] { job([this] { return proc.resample(); }); });

    for (int id : {pid::machine}) addCombo(id, "Sampler");
    for (int id : {pid::bits, pid::rate, pid::alias, pid::drive}) addKnob(id, "Sampler");
    addToggle(pid::mono, "Sampler");
    addCombo(pid::ftype, "Filter");
    for (int id : {pid::cutoff, pid::res, pid::fenv, pid::mix}) addKnob(id, "Filter");
    addToggle(pid::retrig, "Filter");
    for (int id : {pid::semis, pid::cents, pid::a4}) addKnob(id, "Tuning");
    for (int id : {pid::stretch, pid::grain, pid::input, pid::output}) addKnob(id, "Time / IO");
    for (int id : {pid::crackle, pid::hiss, pid::wow, pid::flutter, pid::tape, pid::age, pid::hum}) addKnob(id, "Lo-fi");
    addCombo(pid::humHz, "Lo-fi");
    for (int id : {pid::lsF, pid::lsG, pid::p1F, pid::p1G, pid::p1Q, pid::p2F, pid::p2G, pid::p2Q, pid::p3F, pid::p3G, pid::p3Q, pid::hsF, pid::hsG, pid::match,
                   pid::cThr, pid::cRat, pid::cAtk, pid::cRel, pid::cMake, pid::mGain, pid::lim}) addKnob(id, "Master");
    addToggle(pid::mOn, "Master");
    // machine choice sets the machine's rate, bits and filter, like the app
    if (auto* mc = dynamic_cast<juce::ComboBox*>(ctls[0].comp.get())) {
        auto prev = mc->onChange;
        mc->onChange = [this, mc, prev] { if (prev) prev(); Params q = proc.snapshot(); const int m = mc->getSelectedItemIndex(); if (m >= 0 && (int) q[pid::machine] == m) { setMachine(q, m); for (int id : {pid::rate, pid::bits, pid::ftype, pid::cutoff, pid::res}) proc.setParam(id, q[id]); } };
    }

    // tempo / key / warp
    for (auto* l : {&srcBpm, &tgtBpm}) { l->setEditable(true); l->setColour(juce::Label::backgroundColourId, juce::Colour(0xfff4f3f0)); l->setColour(juce::Label::outlineColourId, juce::Colour(0xff8f9297)); l->setFont(font(13)); addAndMakeVisible(*l); }
    tgtBpm.setText("90", juce::dontSendNotification);
    button("Detect tempo & key", [this] { job([this] { auto r = proc.analyze(); juce::MessageManager::callAsync([this] { if (proc.tempo > 0) srcBpm.setText(juce::String(proc.tempo, 1), juce::dontSendNotification); if (proc.key.root >= 0) toKey.setSelectedItemIndex(proc.key.root, juce::dontSendNotification); }); return r; }); }).setName("pri");
    warpMode.addItem("Stretch (keep pitch)", 1); warpMode.addItem("Speed (pitch moves)", 2); warpMode.setSelectedId(1); addAndMakeVisible(warpMode);
    button("Warp", [this] { toast(proc.warp(srcBpm.getText().getDoubleValue(), tgtBpm.getText().getDoubleValue(), warpMode.getSelectedId() == 1)); });
    for (int i = 0; i < 12; ++i) toKey.addItem(NOTE_NAMES[i], i + 1);
    toKey.setSelectedId(1); addAndMakeVisible(toKey);
    button("Transpose", [this] { toast(proc.transposeTo(toKey.getSelectedItemIndex())); });

    // master
    button("Load reference", [this, openFile] { openFile("Reference track for EQ match", [this](juce::File f) { job([this, f] { return proc.loadReference(f); }); }); });
    button("Match EQ", [this] { job([this] { return proc.matchEq(); }); });
    button("Clear match", [this] { proc.clearMatch(); toast("EQ match cleared"); });
    for (auto [t, id] : std::vector<std::pair<const char*, int>>{{"-9 LUFS", 1}, {"-12 LUFS", 2}, {"-14 LUFS", 3}, {"-16 LUFS", 4}, {"-20 LUFS", 5}}) lufs.addItem(t, id);
    lufs.setSelectedId(3); addAndMakeVisible(lufs);
    button("Auto master", [this] { const double t = lufs.getText().getDoubleValue(); job([this, t] { return proc.autoMasterNow(t); }); }).setName("pri");
    button("Reset master", [this] { proc.resetMaster(); toast("Master reset"); });

    // export
    int k = 1;
    for (auto* t : {"WAV 16-bit", "WAV 24-bit", "WAV 32-bit float", "AIFF 16-bit", "AIFF 24-bit", "FLAC 16-bit", "FLAC 24-bit", "OGG Vorbis (high)", "OGG Vorbis (medium)"}) fmt.addItem(t, k++);
    fmt.setSelectedId(2); addAndMakeVisible(fmt);
    expSr.addItem("44.1 kHz", 44100); expSr.addItem("48 kHz", 48000); expSr.addItem("88.2 kHz", 88200); expSr.addItem("96 kHz", 96000); expSr.addItem("26.04 kHz (SP clock)", 26041); expSr.setSelectedId(48000); addAndMakeVisible(expSr);
    expRange.addItem("Selection", 1); expRange.addItem("Whole file", 2); expRange.setSelectedId(1); addAndMakeVisible(expRange);
    button("Export", [this] {
        static const char* codes[] = {"wav:16", "wav:24", "wav:32", "aiff:16", "aiff:24", "flac:16", "flac:24", "ogg:9", "ogg:5"};
        static const char* exts[] = {".wav", ".wav", ".wav", ".aif", ".aif", ".flac", ".flac", ".ogg", ".ogg"};
        const int i = fmt.getSelectedItemIndex(); const juce::String code = codes[i], ext = exts[i];
        const double fs = expSr.getSelectedId(); const bool whole = expRange.getSelectedId() == 2;
        const auto base = proc.sourceName().upToLastOccurrenceOf(".", false, false) + " - " + proc.presetName().removeCharacters("\\/:*?\"<>|");
        chooser = std::make_unique<juce::FileChooser>("Export", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(base + ext), "*" + ext);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, code, fs, whole, ext](const juce::FileChooser& c) {
            auto f = c.getResult(); if (f == juce::File()) return;
            if (!f.hasFileExtension(ext)) f = f.withFileExtension(ext);
            toast("Exporting...");
            job([this, f, code, fs, whole] { return proc.exportTo(f, code, fs, whole); });
        });
    }).setName("pri");

    for (auto& b : buttons) if (b->getName() == "pri" || b->getName() == "stop") { b->setColour(juce::TextButton::textColourOffId, juce::Colours::white); b->setColour(juce::TextButton::textColourOnId, juce::Colours::white); }
    setSize(1180, 820);
    startTimerHz(15);
}
TwelveEditor::~TwelveEditor() { setLookAndFeel(nullptr); }

void TwelveEditor::timerCallback() {
    btn["Loop"]->setToggleState(proc.loop, juce::dontSendNotification);
    btn["Play"]->setToggleState(proc.isPlaying(), juce::dontSendNotification);
    btn["Zoom"]->setToggleState(lcd.zoom, juce::dontSendNotification);
    juce::String info;
    if (proc.hasSource()) {
        const auto s = proc.selection(); const double sr = proc.sourceSr();
        info << "FILE " << proc.sourceName() << "   LEN " << juce::String((double) proc.sourceLength() / sr, 2) << "s   SEL " << juce::String((double) s.first / sr, 3) << "-" << juce::String((double) s.second / sr, 3) << "s";
        if (proc.tempo > 0) info << "   BPM " << juce::String(proc.tempo, 1);
        if (proc.key.root >= 0) info << "   KEY " << proc.key.name();
        info << "   OUT " << proc.lufsText();
    }
    lcd.info = info;
    if (msg.isNotEmpty() && juce::Time::getMillisecondCounter() > msgUntil) { msg.clear(); repaint(); }
}

void TwelveEditor::paint(juce::Graphics& g) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd4d6d8), 0, 0, juce::Colour(0xffb9bbbe), 0, (float) getHeight(), false));
    g.fillAll();
    g.setColour(kInk); g.setFont(font(24, true)); g.drawText("OMEGA TWELVE", 14, 8, 260, 28, juce::Justification::left);
    g.setColour(kMute); g.setFont(font(10)); g.drawText(juce::String::fromUTF8("12-bit vintage sampler emulator \xc2\xb7 SP-1200 audio path \xc2\xb7 MIDI: middle C = original pitch"), 14, 34, 500, 12, juce::Justification::left);
    for (auto& [name, r] : sections) {
        g.setColour(kCard); g.fillRoundedRectangle(r.toFloat(), 7);
        g.setColour(juce::Colour(0xff3b3e43)); g.setFont(font(10, true)); g.drawText(name.toUpperCase(), r.reduced(10, 6).removeFromTop(12), juce::Justification::left);
    }
    if (msg.isNotEmpty()) {
        auto r = juce::Rectangle<int>(0, 0, juce::jmin(getWidth() - 40, 40 + msg.length() * 7), 34).withCentre({getWidth() / 2, getHeight() - 30});
        g.setColour(juce::Colour(0xee111111)); g.fillRoundedRectangle(r.toFloat(), 8);
        g.setColour(juce::Colour(0xffe9ffe9)); g.setFont(font(13)); g.drawText(msg, r.reduced(10, 0), juce::Justification::centred, true);
    }
}

void TwelveEditor::resized() {
    sections.clear();
    const juce::String L = juce::String::fromUTF8("\xe2\x97\x80"), R = juce::String::fromUTF8("\xe2\x96\xb6");
    // top bar
    btn[L]->setBounds(560, 10, 34, 30); presets.setBounds(598, 10, 400, 30); btn[R]->setBounds(1002, 10, 34, 30); btn["Random"]->setBounds(1042, 10, 124, 30);
    // display and transport
    lcd.setBounds(10, 50, 700, 300);
    int x = 10;
    for (auto* n : {"Import", "Play", "Loop", "STOP ALL"}) { const int w = juce::String(n) == "STOP ALL" ? 100 : 72; btn[n]->setBounds(x, 356, w, 30); x += w + 6; }
    liveBtn.setBounds(x + 6, 356, 110, 30);
    x = 10;
    for (auto* n : {"Select all", "Zoom", "Truncate", "Normalize", "Reverse", "Undo", "Resample"}) { btn[n]->setBounds(x, 392, 92, 28); x += 98; }
    // tempo/key/warp and export on the right
    auto rr = juce::Rectangle<int>(720, 50, 450, 180); sections["Tempo / key / warp"] = rr;
    btn["Detect tempo & key"]->setBounds(730, 74, 150, 28); srcBpm.setBounds(888, 74, 70, 28); tgtBpm.setBounds(966, 74, 70, 28);
    warpMode.setBounds(730, 110, 200, 28); btn["Warp"]->setBounds(938, 110, 80, 28);
    toKey.setBounds(730, 146, 90, 28); btn["Transpose"]->setBounds(828, 146, 100, 28);
    rr = juce::Rectangle<int>(720, 236, 450, 184); sections["Export"] = rr;
    fmt.setBounds(730, 260, 210, 28); expSr.setBounds(948, 260, 160, 28); expRange.setBounds(730, 296, 140, 28); btn["Export"]->setBounds(878, 296, 110, 28);
    // knob sections
    auto place = [&](const juce::String& sec, juce::Rectangle<int> area, int cols) {
        sections[sec] = area;
        auto in = area.reduced(8, 6); in.removeFromTop(14);
        int i = 0; const int cw = in.getWidth() / cols, ch = 74;
        for (auto& c : ctls) {
            if (c.section != sec) continue;
            const int span = dynamic_cast<juce::Slider*>(c.comp.get()) ? 1 : 2;
            if (i % cols + span > cols) i += cols - i % cols;
            auto cell = juce::Rectangle<int>(in.getX() + (i % cols) * cw, in.getY() + (i / cols) * ch, cw * span, ch);
            i += span - 1;
            if (auto* s = dynamic_cast<juce::Slider*>(c.comp.get())) { s->setBounds(cell.withTrimmedBottom(12).reduced(2, 0)); c.label->setBounds(cell.removeFromBottom(13)); }
            else if (dynamic_cast<juce::ComboBox*>(c.comp.get())) { c.label->setBounds(cell.getX() + 2, cell.getY() + 6, cell.getWidth() - 6, 14); c.comp->setBounds(cell.getX() + 2, cell.getY() + 22, cell.getWidth() - 6, 26); }
            else c.comp->setBounds(cell.reduced(8, 22));
            ++i;
        }
    };
    place("Sampler", {10, 428, 470, 96}, 8);
    place("Filter", {486, 428, 470, 96}, 8);
    place("Tuning", {962, 428, 208, 96}, 3);
    place("Time / IO", {10, 530, 250, 96}, 4);
    place("Lo-fi", {266, 530, 530, 96}, 9);
    // master: two rows of knobs and its buttons
    place("Master", {10, 632, 1160, 178}, 15);
    sections["Master tools"] = {802, 530, 368, 96};
    btn["Load reference"]->setBounds(810, 552, 120, 28); btn["Match EQ"]->setBounds(934, 552, 112, 28); btn["Clear match"]->setBounds(1050, 552, 112, 28);
    lufs.setBounds(810, 588, 120, 28); btn["Auto master"]->setBounds(934, 588, 112, 28); btn["Reset master"]->setBounds(1050, 588, 112, 28);
}

}  // namespace twelve
