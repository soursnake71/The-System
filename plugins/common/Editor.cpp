#include "Editor.h"

namespace omega {

Look::Look(juce::Colour a, juce::Colour p) : accent(a), panel(p) {
    setColour(juce::ResizableWindow::backgroundColourId, panel);
    setColour(juce::Label::textColourId, juce::Colour(0xffe8eaf0));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe8eaf0));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff111318));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff323747));
    setColour(juce::ComboBox::textColourId, juce::Colour(0xffe8eaf0));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1a1d24));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2e38));
    setColour(juce::TextButton::buttonOnColourId, accent);
    setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc8ccd6));
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xff111318));
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff111318));
    setColour(juce::TextEditor::textColourId, juce::Colour(0xffe8eaf0));
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff323747));
}

void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    const auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4);
    const float rad = juce::jmin(r.getWidth(), r.getHeight()) / 2, cx = r.getCentreX(), cy = r.getCentreY();
    // knob cap with a skirt, and a lit arc for the value
    juce::Path track; track.addCentredArc(cx, cy, rad - 2, rad - 2, 0, a0, a1, true);
    g.setColour(juce::Colour(0xff323747)); g.strokePath(track, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ang = a0 + pos * (a1 - a0);
    juce::Path arc; arc.addCentredArc(cx, cy, rad - 2, rad - 2, 0, a0, ang, true);
    g.setColour(accent); g.strokePath(arc, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float k = rad - 7;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4f5c), cx, cy - k, juce::Colour(0xff15171c), cx, cy + k, false));
    g.fillEllipse(cx - k, cy - k, k * 2, k * 2);
    g.setColour(juce::Colour(0xff0b0c0f)); g.drawEllipse(cx - k, cy - k, k * 2, k * 2, 1.2f);
    juce::Path p; p.addRoundedRectangle(-1.5f, -k + 2, 3.f, k * .55f, 1.5f);
    g.setColour(juce::Colour(0xfff2f2f2)); g.fillPath(p, juce::AffineTransform::rotation(ang).translated(cx, cy));
}

void Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.5f);
    const bool on = b.getToggleState();
    g.setColour(on ? accent : juce::Colour(over ? 0xff353a46 : 0xff2a2e38).darker(down ? .2f : 0));
    g.fillRoundedRectangle(r, 6);
    if (on) { g.setColour(accent.withAlpha(.35f)); g.drawRoundedRectangle(r.expanded(1), 7, 2); }
    g.setColour(juce::Colour(0xff0b0c0f)); g.drawRoundedRectangle(r, 6, 1);
}

static constexpr int kCellW = 92, kCellH = 104, kTop = 92;

Editor::Editor(Processor& p) : AudioProcessorEditor(p), proc(p), look(juce::Colour(p.spec().accent), juce::Colour(p.spec().panel)) {
    setLookAndFeel(&look);
    const auto& sp = p.spec();
    for (size_t i = 0; i < sp.presets.size(); ++i) presets.addItem(sp.presets[i].name, (int) i + 1);
    presets.setTextWhenNothingSelected("Presets");
    presets.onChange = [this] { const int id = presets.getSelectedId(); if (id > 0 && id - 1 != proc.getCurrentProgram()) proc.setCurrentProgram(id - 1); else if (id > 0) proc.setCurrentProgram(id - 1); };
    prev.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram((proc.getCurrentProgram() + n - 1) % n); };
    next.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram((proc.getCurrentProgram() + 1) % n); };
    addAndMakeVisible(presets); addAndMakeVisible(prev); addAndMakeVisible(next);
    presets.setVisible(!sp.presets.empty()); prev.setVisible(!sp.presets.empty()); next.setVisible(!sp.presets.empty());
    if (sp.hasText) {
        text.setMultiLine(true, true); text.setReturnKeyStartsNewLine(true); text.setText(p.getText(), false);
        text.setFont(juce::FontOptions(16.f));
        text.onTextChange = [this] { proc.setText(text.getText()); };
        textHint.setText(sp.textHint, juce::dontSendNotification);
        textHint.setColour(juce::Label::textColourId, juce::Colour(0xff8d94a6));
        addAndMakeVisible(text); addAndMakeVisible(textHint);
    }
    for (const auto& d : sp.params) {
        auto c = std::make_unique<Cell>();
        juce::String name = d.name; const int colon = name.indexOf(": ");
        if (colon > 0) { c->section = name.substring(0, colon); name = name.substring(colon + 2); }
        c->label = std::make_unique<juce::Label>(); c->label->setText(name.toUpperCase(), juce::dontSendNotification);
        c->label->setJustificationType(juce::Justification::centred); c->label->setFont(juce::FontOptions(11.5f, juce::Font::bold));
        c->label->setMinimumHorizontalScale(.6f);
        body.addAndMakeVisible(*c->label);
        if (!d.choices.empty()) {
            c->menu = std::make_unique<juce::ComboBox>();
            for (size_t k = 0; k < d.choices.size(); ++k) c->menu->addItem(d.choices[k].label, (int) k + 1);
            body.addAndMakeVisible(*c->menu);
            c->ca = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.state(), d.id, *c->menu);
        } else if (d.toggle) {
            c->sw = std::make_unique<juce::TextButton>(name); c->sw->setClickingTogglesState(true);
            c->label->setVisible(false);
            body.addAndMakeVisible(*c->sw);
            c->ba = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), d.id, *c->sw);
        } else {
            c->knob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            c->knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kCellW - 6, 18);
            c->knob->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            body.addAndMakeVisible(*c->knob);
            c->ka = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state(), d.id, *c->knob);
            c->knob->setDoubleClickReturnValue(true, d.def);
        }
        cells.push_back(std::move(c));
    }
    view.setViewedComponent(&body, false); view.setScrollBarsShown(true, false);
    addAndMakeVisible(view);
    const int cols = 7, n = (int) cells.size();
    const int rows = (n + cols - 1) / cols + 3;
    setSize(cols * kCellW + 24, juce::jmin(720, kTop + (sp.hasText ? 130 : 0) + rows * kCellH));
    setResizable(true, true); setResizeLimits(420, 300, 1600, 1400);
    startTimerHz(8);
    // test hook: OMEGA_SNAPSHOT=<dir> saves a picture of the panel (used by the headless host check)
    if (auto* dir = std::getenv("OMEGA_SNAPSHOT")) {
        resized();
        auto img = createComponentSnapshot(getLocalBounds(), true, 1.0f);
        auto f = juce::File(juce::String::fromUTF8(dir)).getChildFile(juce::String(p.spec().name).replaceCharacter(' ', '_') + ".png");
        f.deleteFile(); juce::PNGImageFormat png; if (auto os = f.createOutputStream()) png.writeImageToStream(img, *os);
    }
}

Editor::~Editor() { setLookAndFeel(nullptr); }

void Editor::timerCallback() {
    if (proc.getCurrentProgram() != lastProgram) { lastProgram = proc.getCurrentProgram(); presets.setSelectedId(lastProgram + 1, juce::dontSendNotification); }
    if (proc.spec().hasText && !text.hasKeyboardFocus(true) && text.getText() != proc.getText()) text.setText(proc.getText(), false);
}

void Editor::paint(juce::Graphics& g) {
    const auto& sp = proc.spec();
    g.fillAll(juce::Colour(sp.panel));
    // name plate
    auto plate = getLocalBounds().removeFromTop(kTop - 36).reduced(10, 8);
    g.setColour(juce::Colour(0xff0f1014)); g.fillRoundedRectangle(plate.toFloat(), 6);
    g.setColour(juce::Colour(sp.accent)); g.fillRect(plate.removeFromLeft(5).toFloat());
    plate.removeFromLeft(10);
    g.setFont(juce::FontOptions(22.f, juce::Font::bold)); g.setColour(juce::Colour(0xfff3f4f7));
    g.drawText(juce::String(sp.name).toUpperCase(), plate.removeFromTop(26), juce::Justification::bottomLeft);
    g.setFont(juce::FontOptions(12.f)); g.setColour(juce::Colour(0xff9aa1b2));
    g.drawText(sp.sub, plate, juce::Justification::topLeft);
    g.setColour(juce::Colour(sp.accent).withAlpha(.8f)); g.setFont(juce::FontOptions(11.f, juce::Font::bold));
    g.drawText("THE OMEGA SYSTEM", getLocalBounds().removeFromTop(kTop - 36).reduced(18, 10), juce::Justification::topRight);
}

void Editor::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(kTop - 36);
    auto bar = r.removeFromTop(32).reduced(10, 2);
    prev.setBounds(bar.removeFromLeft(32)); bar.removeFromLeft(4); next.setBounds(bar.removeFromRight(32)); bar.removeFromRight(4); presets.setBounds(bar);
    r.removeFromTop(4);
    if (proc.spec().hasText) { auto t = r.removeFromTop(126).reduced(10, 2); textHint.setBounds(t.removeFromBottom(18)); text.setBounds(t); }
    view.setBounds(r);
    const int w = r.getWidth() - 12, cols = juce::jmax(1, w / kCellW);
    int x = 0, y = 4; juce::String sec = "\x01";
    heads.clear();
    for (auto& c : cells) {
        if (c->section != sec) {
            if (x) { x = 0; y += kCellH; }
            sec = c->section;
            if (sec.isNotEmpty()) { heads.push_back({sec, y}); y += 22; }
        }
        const int span = c->menu && cols > 1 ? 2 : 1;
        if (x + span > cols) { x = 0; y += kCellH; }
        const juce::Rectangle<int> cell(6 + x * kCellW, y, kCellW * span, kCellH);
        auto in = cell.reduced(4);
        if (c->knob) { c->label->setBounds(in.removeFromTop(16)); c->knob->setBounds(in); }
        else if (c->menu) { c->label->setBounds(in.removeFromTop(16)); c->menu->setBounds(in.removeFromTop(28).translated(0, 22)); }
        else if (c->sw) { c->sw->setBounds(in.withSizeKeepingCentre(in.getWidth(), 34)); }
        x += span; if (x >= cols) { x = 0; y += kCellH; }
    }
    if (x) y += kCellH;
    body.setSize(w, y + 8);
    // section titles drawn as labels in the body
    ownedHeads.clear();
    for (auto& [t, hy] : heads) {
        auto* l = new juce::Label("head", t.toUpperCase()); l->setFont(juce::FontOptions(12.f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, juce::Colour(proc.spec().accent)); l->setBounds(8, hy, w - 16, 20);
        body.addAndMakeVisible(l);   // owned below
        ownedHeads.add(l);
    }
}

}  // namespace omega
