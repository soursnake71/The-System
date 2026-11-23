#include "FlangerEditor.h"
#include "BinaryData.h"

namespace flanger {

const Editor::KnobSpot Editor::kSpots[4] = {
    {m117::manual, 74.f, 90.f, -155.f},
    {m117::width, 182.f, 92.f, -18.f},
    {m117::speed, 284.f, 94.f, -127.f},
    {m117::regen, 392.f, 90.f, 77.f},
};
static constexpr float kLedX = 234.f, kLedY = 216.f, kLedR = 4.5f;      // a red LED above the footswitch
static constexpr float kFootX = 234.f, kFootY = 262.f, kFootR = 22.f;
static const juce::Colour kInk(0xffe9e6dc), kStrip(0xff17181b), kAccent(0xffd9d4c4);

MiniLook::MiniLook() {
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0d0e10));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a3c40));
    setColour(juce::ComboBox::textColourId, kInk);
    setColour(juce::ComboBox::arrowColourId, kInk);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1d1e22));
    setColour(juce::PopupMenu::textColourId, kInk);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff4a4d55));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2c31));
    setColour(juce::TextButton::textColourOffId, kInk);
    setColour(juce::Label::textColourId, kInk);
}

void MiniLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) {
    const auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(1.5f);
    const float rad = juce::jmin(r.getWidth(), r.getHeight()) / 2, cx = r.getCentreX(), cy = r.getCentreY();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b3d42), cx, cy - rad, juce::Colour(0xff060607), cx, cy + rad, false));
    g.fillEllipse(cx - rad, cy - rad, rad * 2, rad * 2);
    g.setColour(juce::Colour(0xff000000)); g.drawEllipse(cx - rad, cy - rad, rad * 2, rad * 2, 1.f);
    const float ang = a0 + pos * (a1 - a0);
    juce::Path p; p.addRoundedRectangle(-0.9f, -rad + 1.5f, 1.8f, rad * .6f, .9f);
    g.setColour(kAccent); g.fillPath(p, juce::AffineTransform::rotation(ang).translated(cx, cy));
}

void FootSwitch::paint(juce::Graphics& g) {
    if (!down) return;
    g.setColour(juce::Colours::black.withAlpha(.25f));
    g.fillEllipse(getLocalBounds().toFloat().reduced(getWidth() * .12f));
}

Editor::Editor(Processor& p) : AudioProcessorEditor(p), proc(p) {
    photo = juce::ImageCache::getFromMemory(BinaryData::faceplate_jpg, BinaryData::faceplate_jpgSize);
    for (int k = 0; k < 4; ++k) {
        auto& kn = knobs[k];
        const auto& def = m117::kParams[kSpots[k].param];
        kn.setRotaryParameters(juce::MathConstants<float>::pi * (2 - 150.f / 180), juce::MathConstants<float>::pi * (2 + 150.f / 180), true);
        kn.setMouseDragSensitivity(220);
        kn.setVelocityBasedMode(false);
        knobAtt[k] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state(), def.id, kn);
        kn.setDoubleClickReturnValue(true, def.def);
        kn.setPopupMenuEnabled(false);
        kn.onValueChange = [this] { repaint(); };
        kn.onDragStart = [this, k] { shownKnob = k; repaint(); };
        kn.onDragEnd = [this] { shownKnob = -1; repaint(); };
        addAndMakeVisible(kn);
    }
    foot.onPress = [this] {
        auto* b = proc.param(m117::bypass);
        b->beginChangeGesture(); b->setValueNotifyingHost(b->getValue() >= .5f ? 0.f : 1.f); b->endChangeGesture();
        repaint();
    };
    foot.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(foot);

    const int miniParams[3] = {m117::mix, m117::spread, m117::level};
    for (int k = 0; k < 3; ++k) {
        auto& s = mini[k];
        const auto& def = m117::kParams[miniParams[k]];
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag); s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        s.setLookAndFeel(&look);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        miniAtt[k] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state(), def.id, s);
        s.setDoubleClickReturnValue(true, def.def);
        const int pi = miniParams[k];
        auto update = [this, k, pi] {
            miniLabel[k].setText(juce::String(m117::kParams[pi].name).toUpperCase() + "\n" + Processor::formatValue(pi, (float) mini[k].getValue()), juce::dontSendNotification);
        };
        s.onValueChange = update; update();
        miniLabel[k].setLookAndFeel(&look);
        miniLabel[k].setJustificationType(juce::Justification::centredLeft);
        miniLabel[k].setInterceptsMouseClicks(false, false);
        addAndMakeVisible(s); addAndMakeVisible(miniLabel[k]);
    }

    for (int i = 0; i < kNumPresets; ++i) presets.addItem(juce::String::fromUTF8(kPresets[i].name), i + 1);
    presets.setTextWhenNothingSelected("Presets");
    presets.setLookAndFeel(&look);
    presets.onChange = [this] { const int id = presets.getSelectedId(); if (id > 0) proc.setCurrentProgram(id - 1); };
    prev.setLookAndFeel(&look); next.setLookAndFeel(&look);
    prev.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram((proc.getCurrentProgram() + n - 1) % n); };
    next.onClick = [this] { const int n = proc.getNumPrograms(); proc.setCurrentProgram((proc.getCurrentProgram() + 1) % n); };
    addAndMakeVisible(presets); addAndMakeVisible(prev); addAndMakeVisible(next);

    setResizable(true, true);
    setResizeLimits(kPhotoW, kPhotoH + kStripH, kPhotoW * 3, (kPhotoH + kStripH) * 3);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) kPhotoW / (kPhotoH + kStripH));
    setSize(kPhotoW * 3 / 2, (kPhotoH + kStripH) * 3 / 2);
    startTimerHz(20);
}

Editor::~Editor() {
    for (auto& s : mini) s.setLookAndFeel(nullptr);
    for (auto& l : miniLabel) l.setLookAndFeel(nullptr);
    presets.setLookAndFeel(nullptr); prev.setLookAndFeel(nullptr); next.setLookAndFeel(nullptr);
}

void Editor::buildDiscs() {
    const float s = scale();
    const int W = juce::roundToInt(kPhotoW * s), H = juce::roundToInt(kPhotoH * s);
    scaledPhoto = photo.rescaled(W, H, juce::Graphics::highResamplingQuality);
    scaledPhoto = scaledPhoto.convertedToFormat(juce::Image::ARGB);
    const float R = kDiscR * s, F = juce::jmax(1.f, kFeather * s);
    const int D = (int) std::ceil(R * 2) + 2;
    for (int k = 0; k < 4; ++k) {
        juce::Image d(juce::Image::ARGB, D, D, true);
        const float cx = kSpots[k].cx * s, cy = kSpots[k].cy * s, half = D / 2.f;
        juce::Image::BitmapData src(scaledPhoto, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst(d, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < D; ++y) for (int x = 0; x < D; ++x) {
            const float dx = x + .5f - half, dy = y + .5f - half, dist = std::sqrt(dx * dx + dy * dy);
            const float a = juce::jlimit(0.f, 1.f, (R - dist) / F);
            if (a <= 0) continue;
            const int sx = juce::jlimit(0, W - 1, (int) std::floor(cx + dx)), sy = juce::jlimit(0, H - 1, (int) std::floor(cy + dy));
            const auto c = src.getPixelColour(sx, sy);
            dst.setPixelColour(x, y, c.withAlpha(a));
        }
        discs[k] = d;
    }
}

void Editor::paint(juce::Graphics& g) {
    const float s = scale();
    g.fillAll(kStrip);
    if (scaledPhoto.isNull() || scaledPhoto.getWidth() != juce::roundToInt(kPhotoW * s)) buildDiscs();
    g.drawImageAt(scaledPhoto, 0, 0);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    for (int k = 0; k < 4; ++k) {
        const auto& sp = kSpots[k];
        const auto* prm = proc.param(sp.param);
        const float norm = prm->getValue();   // 0..1
        const float deg = -150.f + 300.f * norm - sp.photoDeg;
        const float half = discs[k].getWidth() / 2.f;
        g.drawImageTransformed(discs[k], juce::AffineTransform::translation(-half, -half)
            .rotated(juce::degreesToRadians(deg)).translated(sp.cx * s, sp.cy * s));
    }
    // LED: lit while the effect is on
    const bool on = proc.param(m117::bypass)->getValue() < .5f;
    const float lx = kLedX * s, ly = kLedY * s, lr = kLedR * s;
    if (on) {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0x90ff2a1a), lx, ly, juce::Colour(0x00ff2a1a), lx + lr * 4, ly, true));
        g.fillEllipse(lx - lr * 4, ly - lr * 4, lr * 8, lr * 8);
    }
    g.setColour(juce::Colour(0xff0c0c0c)); g.fillEllipse(lx - lr - s, ly - lr - s, (lr + s) * 2, (lr + s) * 2);
    g.setGradientFill(juce::ColourGradient(on ? juce::Colour(0xffffb0a0) : juce::Colour(0xff5a1c18), lx - lr * .3f, ly - lr * .3f,
                                           on ? juce::Colour(0xffe0180c) : juce::Colour(0xff2a0a08), lx + lr, ly + lr, true));
    g.fillEllipse(lx - lr, ly - lr, lr * 2, lr * 2);
    // strip
    const float top = kPhotoH * s;
    g.setColour(juce::Colour(0xff000000)); g.fillRect(0.f, top, (float) getWidth(), s);
}

void Editor::paintOverChildren(juce::Graphics& g) {
    if (shownKnob < 0) return;
    const float s = scale();
    const auto& sp = kSpots[shownKnob];
    const auto* prm = proc.param(sp.param);
    const juce::String txt = juce::String(m117::kParams[sp.param].name).toUpperCase() + "  " + prm->getCurrentValueAsText();
    g.setFont(juce::FontOptions(13.f * s, juce::Font::bold));
    const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), txt) + 14 * s, h = 22 * s;
    juce::Rectangle<float> r(sp.cx * s - w / 2, (sp.cy + kDiscR + 6) * s, w, h);
    r = r.withX(juce::jlimit(2.f, getWidth() - w - 2, r.getX()));
    g.setColour(juce::Colour(0xe0101113)); g.fillRoundedRectangle(r, 5 * s);
    g.setColour(kInk); g.drawText(txt, r, juce::Justification::centred);
}

void Editor::resized() {
    const float s = scale();
    for (int k = 0; k < 4; ++k) {
        const float R = kDiscR * s;
        knobs[k].setBounds(juce::Rectangle<float>(kSpots[k].cx * s - R, kSpots[k].cy * s - R, R * 2, R * 2).toNearestInt());
    }
    foot.setBounds(juce::Rectangle<float>((kFootX - kFootR) * s, (kFootY - kFootR) * s, kFootR * 2 * s, kFootR * 2 * s).toNearestInt());
    auto strip = juce::Rectangle<float>(0, kPhotoH * s, (float) getWidth(), kStripH * s).reduced(8 * s, 7 * s);
    const float bh = strip.getHeight();
    prev.setBounds(strip.removeFromLeft(bh).toNearestInt()); strip.removeFromLeft(3 * s);
    auto menu = strip.removeFromLeft(150 * s);
    strip.removeFromLeft(3 * s);
    next.setBounds(strip.removeFromLeft(bh).toNearestInt());
    presets.setBounds(menu.toNearestInt());
    strip.removeFromLeft(10 * s);
    const float cellW = strip.getWidth() / 3;
    for (int k = 0; k < 3; ++k) {
        auto c = strip.removeFromLeft(cellW);
        mini[k].setBounds(c.removeFromLeft(bh + 2 * s).expanded(0, s).toNearestInt());
        c.removeFromLeft(3 * s);
        miniLabel[k].setBounds(c.toNearestInt());
        miniLabel[k].setFont(juce::FontOptions(9.5f * s, juce::Font::bold));
        miniLabel[k].setMinimumHorizontalScale(.6f);
    }
    prev.setConnectedEdges(0);
    scaledPhoto = {};   // rebuilt at the new size on the next paint
}

void Editor::timerCallback() {
    if (proc.getCurrentProgram() != lastProgram) { lastProgram = proc.getCurrentProgram(); presets.setSelectedId(lastProgram + 1, juce::dontSendNotification); }
    const bool on = proc.param(m117::bypass)->getValue() < .5f;
    if (on != lastOn) { lastOn = on; repaint(); }
    int hover = -1;
    for (int k = 0; k < 4; ++k) if (knobs[k].isMouseOverOrDragging()) hover = k;
    if (hover != shownKnob) { shownKnob = hover; repaint(); }
}

void Editor::snapshot(const juce::File& png, int showKnob) {
    resized();
    timerCallback();
    shownKnob = showKnob;
    auto img = createComponentSnapshot(getLocalBounds(), true, 1.0f);
    png.deleteFile(); juce::PNGImageFormat f;
    if (auto os = png.createOutputStream()) f.writeImageToStream(img, *os);
}

}  // namespace flanger
