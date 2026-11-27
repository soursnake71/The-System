#include "XR10Fold.h"

namespace xr10 {
using namespace xr;

static const juce::Colour fBg(0xff1f2226), fStrip(0xff2a2e34), fStripM(0xff33383f), fInk(0xffe9e9ee), fSub(0xff9aa0aa), fCap(0xffd9d9df),
    fMute(0xffe0a020), fSolo(0xff3fbf6a), fSel(0xff6d95d4);
static juce::Font sansF(float h, bool bold = false) { return juce::Font(juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain)); }
static float dbBar(float p) { return juce::jlimit(0.f, 1.f, (20.f * std::log10(juce::jmax(p, 1e-5f)) + 48.f) / 48.f); }

// ============================================================ mixer
juce::Rectangle<float> MixerView::faderArea(int i) const { const float w = stripW(); return {i * w + 4, 34, w * 0.45f, (float) getHeight() - 168}; }
juce::Rectangle<float> MixerView::panArea(int i) const { const float w = stripW(); return {i * w + 3, (float) getHeight() - 96, w - 6, 12}; }
juce::Rectangle<float> MixerView::sendArea(int i) const { const float w = stripW(); return {i * w + 3, (float) getHeight() - 66, w - 6, 12}; }
juce::Rectangle<float> MixerView::muteArea(int i) const { const float w = stripW(); return {i * w + 3, (float) getHeight() - 44, (w - 9) / 2, 18}; }
juce::Rectangle<float> MixerView::soloArea(int i) const { const float w = stripW(); return {i * w + 6 + (w - 9) / 2, (float) getHeight() - 44, (w - 9) / 2, 18}; }

void MixerView::range(int i, int part, double& lo, double& hi) const {
    if (i < NPADS) { if (part == 1) { lo = -15; hi = 15; } else { lo = 0; hi = 31; } return; }
    lo = 0; hi = i == NPADS + 1 ? 150 : 100;
}
double MixerView::value(int i, int part) const {
    if (i < NPADS) return snap.padParams[i][part];
    if (i == NPADS) return snap.keys[K_EFFECTVR];
    if (i == NPADS + 1) return snap.rackRet * 100;
    return snap.keys[K_VOLUME];
}
void MixerView::setFromMouse(int i, int part, juce::Point<float> pos) {
    double lo, hi; range(i, part, lo, hi);
    double f;
    if (part == 0) { auto r = faderArea(i); f = 1 - (pos.y - r.getY()) / r.getHeight(); }
    else { auto r = part == 1 ? panArea(i) : sendArea(i); f = (pos.x - r.getX()) / r.getWidth(); }
    const int v = juce::roundToInt(lo + juce::jlimit(0.0, 1.0, f) * (hi - lo));
    if (i < NPADS) {
        const int snd = snap.st.padSound[i], p = part == 0 ? V_LEVEL : (part == 1 ? V_PAN : V_SEND);
        proc.with([&](Machine& m) { m.setParam(snd, p, v); });
    } else if (i == NPADS) proc.with([&](Machine& m) { m.put(K_EFFECTVR, v); });
    else if (i == NPADS + 1) proc.withRack([&](xrack::Rack& r) { r.ret = v / 100.f; });
    else { proc.with([&](Machine& m) { m.put(K_VOLUME, v); }); if (onMainVolume) onMainVolume((float) v); }
    repaint();
}
void MixerView::mouseDown(const juce::MouseEvent& e) {
    const int i = juce::jlimit(0, NSTRIP - 1, (int) (e.position.x / stripW()));
    if (i < NPADS) {
        const int snd = snap.st.padSound[i];
        if (muteArea(i).contains(e.position)) { proc.with([&](Machine& m) { m.mute[snd] = !m.mute[snd]; }); return; }
        if (soloArea(i).contains(e.position)) { proc.with([&](Machine& m) { m.solo[snd] = !m.solo[snd]; }); return; }
        if (e.position.y < 30) { proc.with([&](Machine& m) { m.put(K_SOUND, snd); m.hitSound(snd, 110); }); return; }
        if (panArea(i).expanded(0, 6).contains(e.position)) { drag = i; dragPart = 1; setFromMouse(i, 1, e.position); return; }
        if (sendArea(i).expanded(0, 6).contains(e.position)) { drag = i; dragPart = 2; setFromMouse(i, 2, e.position); return; }
    }
    if (faderArea(i).expanded(stripW() * 0.3f, 8).contains(e.position)) { drag = i; dragPart = 0; setFromMouse(i, 0, e.position); }
}
void MixerView::mouseDrag(const juce::MouseEvent& e) { if (drag >= 0) setFromMouse(drag, dragPart, e.position); }
void MixerView::mouseDoubleClick(const juce::MouseEvent& e) {
    const int i = juce::jlimit(0, NSTRIP - 1, (int) (e.position.x / stripW()));
    if (i < NPADS && panArea(i).expanded(0, 6).contains(e.position)) { const int snd = snap.st.padSound[i]; proc.with([&](Machine& m) { m.setParam(snd, V_PAN, 0); }); }
}
void MixerView::paint(juce::Graphics& g) {
    g.fillAll(fBg);
    const float w = stripW();
    for (int i = 0; i < NSTRIP; i++) {
        const bool pad = i < NPADS;
        auto strip = juce::Rectangle<float>(i * w + 1, 0, w - 2, (float) getHeight());
        g.setColour(pad ? fStrip : fStripM);
        g.fillRoundedRectangle(strip, 4);
        // name
        juce::String name;
        if (pad) name = juce::String(i + 1) + "\n" + SOUND_NAMES[snap.st.padSound[i]];
        else name = i == NPADS ? "EFFECT\nSEND" : (i == NPADS + 1 ? "FX\nRETURN" : "MAIN\nVOL");
        g.setColour(pad && snap.st.padSound[i] == snap.st.sound ? fSel : fInk);
        g.setFont(sansF(9.5f, true));
        g.drawFittedText(name, strip.withHeight(30).reduced(1, 1).toNearestInt(), juce::Justification::centred, 2, 0.7f);
        // fader
        auto fa = faderArea(i);
        double lo, hi; range(i, 0, lo, hi);
        const float f = (float) ((value(i, 0) - lo) / (hi - lo));
        g.setColour(juce::Colour(0xff505660));
        g.fillRoundedRectangle(fa.getCentreX() - 2, fa.getY(), 4, fa.getHeight(), 2);
        const float cy = fa.getBottom() - f * fa.getHeight();
        g.setColour(fCap);
        g.fillRoundedRectangle(fa.getX(), cy - 6, fa.getWidth(), 12, 3);
        g.setColour(juce::Colour(0xff555a63));
        g.drawHorizontalLine(juce::roundToInt(cy), fa.getX() + 2, fa.getRight() - 2);
        // meter(s)
        auto meterRect = juce::Rectangle<float>(fa.getRight() + 3, fa.getY(), juce::jmax(4.f, w * 0.16f), fa.getHeight());
        auto drawMeter = [&](juce::Rectangle<float> r, float p) {
            g.setColour(juce::Colour(0xff15181b)); g.fillRoundedRectangle(r, 2);
            const float h = dbBar(p) * r.getHeight();
            juce::ColourGradient gr(juce::Colour(0xff3fbf6a), 0, r.getBottom(), juce::Colour(0xffe5484d), 0, r.getY(), false);
            gr.addColour(0.75, juce::Colour(0xffe8c547));
            g.setGradientFill(gr);
            g.fillRect(r.withTop(r.getBottom() - h));
        };
        if (pad) drawMeter(meterRect, snap.meter[snap.st.padSound[i]]);
        else if (i == NPADS) drawMeter(meterRect, snap.rackIn);
        else if (i == NPADS + 1) drawMeter(meterRect, snap.rackOut);
        else { drawMeter(meterRect.withWidth(meterRect.getWidth() / 2 - 0.5f), snap.outPeak[0]); drawMeter(meterRect.withTrimmedLeft(meterRect.getWidth() / 2 + 0.5f), snap.outPeak[1]); }
        // value
        g.setColour(fSub);
        g.setFont(sansF(9.5f));
        const int v = juce::roundToInt(value(i, 0));
        g.drawText(i == NPADS + 1 ? juce::String(v) + "%" : juce::String(v), juce::Rectangle<float>(i * w, fa.getBottom() + 2, w, 12), juce::Justification::centred);
        if (!pad) continue;
        // pan and send bars
        auto bar = [&](juce::Rectangle<float> r, double val, double l, double h, bool centre, const char* lbl) {
            g.setColour(fSub); g.setFont(sansF(8.5f));
            g.drawText(lbl, r.translated(0, -11).withHeight(10), juce::Justification::centred);
            g.setColour(juce::Colour(0xff15181b)); g.fillRoundedRectangle(r, 3);
            const float x = r.getX() + (float) ((val - l) / (h - l)) * r.getWidth();
            g.setColour(fSel);
            if (centre) g.fillRect(juce::jmin(x, r.getCentreX()), r.getY() + 2, std::abs(x - r.getCentreX()) + 1, r.getHeight() - 4);
            else g.fillRect(r.getX(), r.getY() + 2, x - r.getX(), r.getHeight() - 4);
            g.setColour(fCap); g.fillRect(x - 1, r.getY(), 2.f, r.getHeight());
        };
        bar(panArea(i), snap.padParams[i][1], -15, 15, true, "PAN");
        bar(sendArea(i), snap.padParams[i][2], 0, 31, false, "SEND");
        // mute / solo
        const int snd = snap.st.padSound[i];
        auto btn = [&](juce::Rectangle<float> r, bool on, juce::Colour c, const char* t) {
            g.setColour(on ? c : juce::Colour(0xff3c4149)); g.fillRoundedRectangle(r, 3);
            g.setColour(on ? juce::Colours::black : fInk); g.setFont(sansF(10, true)); g.drawText(t, r, juce::Justification::centred);
        };
        btn(muteArea(i), snap.mute[snd] != 0, fMute, "M");
        btn(soloArea(i), snap.solo[snd] != 0, fSolo, "S");
    }
}

// ============================================================ rack slot
SlotView::SlotView(Processor& p, int s) : proc(p), slot(s) {
    for (auto* c : {&type, &preset}) {
        c->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff30343b));
        c->setColour(juce::ComboBox::textColourId, fInk);
        c->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff4a4f57));
        c->setColour(juce::ComboBox::arrowColourId, fSub);
        addAndMakeVisible(*c);
    }
    int n; const xrack::UnitType* T = xrack::unitTypes(n);
    type.addItem("Slot " + juce::String(slot + 1) + ": empty", 1);
    for (int k = 0; k < n; k++) type.addItem(T[k].name, k + 2);
    type.onChange = [this] {
        if (updating) return;
        const int t = type.getSelectedId() - 2;
        proc.withRack([&](xrack::Rack& r) { r.setType(slot, t); });
        rebuild();
    };
    preset.setTextWhenNothingSelected("Preset...");
    preset.onChange = [this] {
        if (updating || preset.getSelectedId() <= 0) return;
        const int k = preset.getSelectedId() - 1;
        proc.withRack([&](xrack::Rack& r) { r.applyPreset(slot, k); });
        rebuild();
    };
    for (auto* t : {&bypass, &parallel}) { t->setColour(juce::ToggleButton::textColourId, fInk); t->setColour(juce::ToggleButton::tickColourId, fSolo); t->setColour(juce::ToggleButton::tickDisabledColourId, fSub); addAndMakeVisible(*t); }
    bypass.onClick = [this] { const bool on = bypass.getToggleState(); proc.withRack([&](xrack::Rack& r) { r.slot[slot].bypass = on; }); };
    parallel.onClick = [this] { const bool on = parallel.getToggleState(); proc.withRack([&](xrack::Rack& r) { r.slot[slot].parallel = on; }); };
    rebuild();
}
void SlotView::rebuild() {
    const juce::ScopedValueSetter<bool> u(updating, true);
    for (auto& c : ctls) { removeChildComponent(c.c.get()); removeChildComponent(c.label.get()); }
    ctls.clear();
    int t = -1; bool by = false, par = false;
    std::unique_ptr<omega::Spec> spec; std::vector<double> vals;
    proc.withRack([&](xrack::Rack& r) {
        const auto& s = r.slot[slot];
        t = s.type; by = s.bypass; par = s.parallel;
        if (s.spec) { spec.reset(new omega::Spec(*s.spec)); vals = s.vals; }
    });
    shownType = t;
    type.setSelectedId(t + 2, juce::dontSendNotification);
    bypass.setToggleState(by, juce::dontSendNotification); parallel.setToggleState(par, juce::dontSendNotification);
    preset.clear(juce::dontSendNotification);
    preset.setVisible(spec && !spec->presets.empty()); bypass.setVisible(spec != nullptr); parallel.setVisible(spec != nullptr);
    if (!spec) { resized(); repaint(); return; }
    for (size_t k = 0; k < spec->presets.size(); k++) preset.addItem(spec->presets[k].name, (int) k + 1);
    for (size_t k = 0; k < spec->params.size(); k++) {
        const auto& pd = spec->params[k];
        Ctl c; c.param = (int) k;
        c.label.reset(new juce::Label({}, pd.name));
        c.label->setFont(sansF(9.5f)); c.label->setJustificationType(juce::Justification::centred); c.label->setColour(juce::Label::textColourId, fSub);
        c.label->setMinimumHorizontalScale(0.6f);
        const int pi = (int) k;
        if (!pd.choices.empty()) {
            auto* cb = new juce::ComboBox();
            for (size_t q = 0; q < pd.choices.size(); q++) { cb->addItem(pd.choices[q].label, (int) q + 1); if (pd.choices[q].value == vals[k]) cb->setSelectedId((int) q + 1, juce::dontSendNotification); }
            cb->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff30343b)); cb->setColour(juce::ComboBox::textColourId, fInk);
            cb->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff4a4f57));
            std::vector<double> cv; for (auto& ch : pd.choices) cv.push_back(ch.value);
            cb->onChange = [this, cb, cv, pi] { const int q = cb->getSelectedId() - 1; if (q >= 0 && q < (int) cv.size()) proc.withRack([&](xrack::Rack& r) { r.setParam(slot, pi, cv[(size_t) q]); }); };
            c.c.reset(cb); c.menu = true;
        } else {
            auto* sl = new juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            sl->setRange(pd.min, pd.max, pd.step > 0 ? pd.step : 0);
            sl->setValue(vals[k], juce::dontSendNotification);
            sl->setDoubleClickReturnValue(true, pd.def);
            sl->setPopupDisplayEnabled(true, true, nullptr);
            sl->setColour(juce::Slider::rotarySliderFillColourId, fSel); sl->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff4a4f57));
            sl->setColour(juce::Slider::thumbColourId, fCap);
            sl->onValueChange = [this, sl, pi] { if (!updating) { const double v = sl->getValue(); proc.withRack([&](xrack::Rack& r) { r.setParam(slot, pi, v); }); } };
            c.c.reset(sl);
        }
        addAndMakeVisible(*c.c); addAndMakeVisible(*c.label);
        ctls.push_back(std::move(c));
    }
    resized();
    repaint();
}
void SlotView::resized() {
    type.setBounds(18, 4, 250, 22);
    preset.setBounds(274, 4, 170, 22);
    bypass.setBounds(450, 4, 76, 22);
    parallel.setBounds(526, 4, 90, 22);
    const int cw = 56, ch = 56, cols = juce::jmax(1, (getWidth() - 8) / cw);
    int cell = 0;
    for (auto& c : ctls) {
        const int span = c.menu ? 2 : 1;
        if (cell % cols + span > cols) cell += cols - cell % cols;
        const int x = 4 + (cell % cols) * cw, y = 32 + (cell / cols) * ch;
        if (c.menu) { c.c->setBounds(x + 2, y + 10, cw * 2 - 4, 22); c.label->setBounds(x, y + 34, cw * 2, 14); }
        else { c.c->setBounds(x + 6, y, cw - 12, ch - 14); c.label->setBounds(x, y + ch - 15, cw, 14); }
        cell += span;
    }
}
void SlotView::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff23262b));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 6);
    g.setColour(juce::Colour(0xff3a3e45));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 6, 1);
    g.setColour(peak > 0.003f ? juce::Colour(0xff45e07a) : juce::Colour(0xff2c3a2f));
    g.fillEllipse(5, 10, 9, 9);
    if (shownType < 0) { g.setColour(fSub); g.setFont(sansF(11)); g.drawText("Pick a unit for this slot.", getLocalBounds().withTrimmedTop(30).reduced(10), juce::Justification::topLeft); }
}
void SlotView::refresh(const Snapshot& s) {
    if (s.slotType[slot] != shownType) rebuild();
    const bool by = (s.slotFlags[slot] & 1) != 0, par = (s.slotFlags[slot] & 2) != 0;
    if (bypass.getToggleState() != by) bypass.setToggleState(by, juce::dontSendNotification);
    if (parallel.getToggleState() != par) parallel.setToggleState(par, juce::dontSendNotification);
    setAlpha(by ? 0.6f : 1.f);
    if ((s.slotPeak[slot] > 0.003f) != (peak > 0.003f)) { peak = s.slotPeak[slot]; repaint(0, 0, 20, 30); }
    peak = s.slotPeak[slot];
}

// ============================================================ the fold-out
FoldPanel::FoldPanel(Processor& p, const Snapshot& s) : mixer(p, s), proc(p), snap(s) {
    addAndMakeVisible(mixer);
    for (int i = 0; i < 4; i++) { slots[i].reset(new SlotView(p, i)); addAndMakeVisible(*slots[i]); }
}
void FoldPanel::paint(juce::Graphics& g) {
    g.fillAll(fBg);
    g.setColour(fInk); g.setFont(sansF(13, true));
    g.drawText("MIXER", 12, 4, 200, 18, juce::Justification::centredLeft);
    g.setColour(fSub); g.setFont(sansF(10.5f));
    g.drawText("each pad's LEVEL, PAN and FX SEND; M mutes, S solos; tap a name to play it", 70, 4, getWidth() - 80, 18, juce::Justification::centredLeft);
    const int ry = mixer.getBottom() + 6;
    g.setColour(fInk); g.setFont(sansF(13, true));
    g.drawText("EFFECTS RACK", 12, ry, 200, 18, juce::Justification::centredLeft);
    g.setColour(fSub); g.setFont(sansF(10.5f));
    g.drawText("fed from the EFFECT send; slots in series, or parallel off the send; back in through FX RETURN", 120, ry, getWidth() - 130, 18, juce::Justification::centredLeft);
}
void FoldPanel::resized() {
    mixer.setBounds(8, 24, getWidth() - 16, 300);
    int y = mixer.getBottom() + 26;
    const int h = (getHeight() - y - 8) / 4;
    for (auto& s : slots) { s->setBounds(8, y, getWidth() - 16, h - 6); y += h; }
}
void FoldPanel::refresh() {
    mixer.repaint();
    for (auto& s : slots) s->refresh(snap);
}

}  // namespace xr10
