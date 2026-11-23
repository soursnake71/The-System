#include "EightEditor.h"
#include "BinaryData.h"

namespace eight {

static const juce::Colour kOrange(0xffff7a2e), kLed(0xffff3b2c), kPcb(0xff1d5a33), kPcb2(0xff123d23), kCream(0xffd8d2b4), kSilk(0xfff2f5ee);
static constexpr int PW = 2000, PH = 1190;   // the photo's size; every position below is in its pixels (from eight/app.js)
static const int COLX[] = {557, 668, 779, 890, 1001, 1112, 1223, 1335, 1447, 1558, 1670, 1781};
static const int STEPX[] = {530, 604, 678, 752, 826, 900, 973, 1047, 1121, 1195, 1269, 1343, 1418, 1492, 1566, 1640};

/* ---------------- photo knob ---------------- */
void PhotoKnob::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat().reduced(1);
    const float r = juce::jmin(b.getWidth(), b.getHeight()) / 2, cx = b.getCentreX(), cy = b.getCentreY();
    const float v = get ? juce::jlimit(0.f, 1.f, get()) : 0.f;
    const float a = juce::degreesToRadians(a0 + (a1 - a0) * v);
    g.setColour(juce::Colour(0xcc141414)); g.fillEllipse(cx - r * 0.62f, cy - r * 0.62f, r * 1.24f, r * 1.24f);
    g.setColour(kOrange);
    juce::Path p; p.addRoundedRectangle(-r * 0.06f, -r * 0.95f, r * 0.12f, r * 0.62f, r * 0.05f);
    g.fillPath(p, juce::AffineTransform::rotation(a).translated(cx, cy));
}
void PhotoKnob::mouseDown(const juce::MouseEvent&) { startV = get ? get() : 0.f; }
void PhotoKnob::mouseDrag(const juce::MouseEvent& e) {
    if (!set) return;
    float v = juce::jlimit(0.f, 1.f, startV - e.getDistanceFromDragStartY() / 200.f);
    if (detents > 1) v = std::round(v * (detents - 1)) / (detents - 1);
    set(v); repaint();
}

/* ---------------- hot spot ---------------- */
void HotSpot::paint(juce::Graphics& g) {
    if (!led) return;
    const int s = led(); if (!s) return;
    const float d = juce::jmax(6.f, getWidth() * 0.22f), x = getWidth() * ledAt.x, y = getHeight() * ledAt.y;
    g.setColour(s == 2 ? kLed : kLed.withAlpha(0.45f));
    g.fillEllipse(x - d / 2, y - d / 2, d, d);
    if (s == 2) { g.setColour(kLed.withAlpha(0.35f)); g.fillEllipse(x - d, y - d, d * 2, d * 2); }
}

/* ---------------- faceplate ---------------- */
PhotoKnob* FaceView::addKnob(int x, int y, int r) {
    auto* k = knobs.add(new PhotoKnob()); addAndMakeVisible(k); knobRects.push_back({x - r, y - r, 2 * r, 2 * r}); return k;
}
HotSpot* FaceView::addSpot(int x, int y, int w, int h) {
    auto* s = spots.add(new HotSpot()); addAndMakeVisible(s); spotRects.push_back({x, y, w, h}); return s;
}
juce::Rectangle<int> FaceView::toScreen(juce::Rectangle<int> r) const {
    const float k = getWidth() / (float) PW;
    return r.toFloat().transformedBy(juce::AffineTransform::scale(k)).toNearestInt();
}

FaceView::FaceView(EightEditor& e) : ed(e) {
    photo = juce::ImageCache::getFromMemory(BinaryData::faceplate_jpg, BinaryData::faceplate_jpgSize);
    auto& P = ed.proc;
    auto paramKnob = [&](const juce::String& key, int x, int y, int r) {
        auto* prm = P.apvts.getParameter(paramId(key));
        auto* k = addKnob(x, y, r);
        k->get = [prm] { return prm ? prm->getValue() : 0.f; };
        k->set = [prm](float v) { if (prm) { prm->beginChangeGesture(); prm->setValueNotifyingHost(v); prm->endChangeGesture(); } };
        k->reset = [prm] { if (prm) prm->setValueNotifyingHost(prm->getDefaultValue()); };
    };
    // the twelve LEVEL knobs, TONE / TUNING / DECAY / SNAPPY, MASTER VOLUME
    for (int i = 0; i < 12; i++) paramKnob(i == 0 ? "AC.level" : juce::String(TRACKS[i]) + ".level", COLX[i], 268, 27);
    struct K { const char* k; int x, y; };
    for (auto& a : {K{"BD.tone", 668, 388}, K{"SD.tone", 779, 388}, K{"LT.tuning", 890, 388}, K{"MT.tuning", 1001, 388}, K{"HT.tuning", 1112, 388},
                    K{"CY.tone", 1558, 386}, K{"BD.decay", 668, 508}, K{"SD.snappy", 779, 508}, K{"CY.decay", 1558, 507}, K{"OH.decay", 1670, 507}})
        paramKnob(a.k, a.x, a.y, 24);
    paramKnob("master", 1775, 690, 44);
    // tempo
    {
        auto* prm = P.apvts.getParameter("tempo");
        auto* k = addKnob(250, 635, 78);
        k->get = [prm] { return prm->getValue(); };
        k->set = [prm](float v) { prm->setValueNotifyingHost(v); };
        k->reset = [prm] { prm->setValueNotifyingHost(prm->convertTo0to1(120.f)); };
    }
    // PATTERN WRITE mode: 1st part, 2nd part, play (manual play and compose are the app's)
    {
        auto* k = addKnob(228, 320, 44); k->detents = 3; k->a0 = -70; k->a1 = 60;
        k->get = [&P] { const int md = P.mode.load(); return md == 0 ? 0.f : md == 1 ? 0.5f : 1.f; };
        k->set = [&P](float v) { P.mode = v < 0.25f ? 0 : v < 0.75f ? 1 : 3; };
    }
    // INSTRUMENT-SELECT: AC BD SD LT MT HT RS CP CB CY OH CH (TRIG is on the board's TRIG OUT page in the app)
    {
        auto* k = addKnob(410, 318, 40); k->detents = 12; k->a0 = -165; k->a1 = 165;
        k->get = [&P] { return juce::jlimit(0, 11, P.track.load()) / 11.f; };
        k->set = [&P](float v) { P.track = juce::roundToInt(v * 11); };
    }
    // tom / conga, rim / claves, clap / maracas switches
    for (auto& [t, x] : std::initializer_list<std::pair<const char*, int>>{{"LT", 893}, {"MT", 1002}, {"HT", 1112}, {"RS", 1223}, {"CP", 1334}}) {
        const juce::String key = juce::String(t) + ".sel";
        auto* s = addSpot(x - 55, 482, 110, 134);
        s->onPress = [&P, key] { P.setParam(key, P.getParam(key) > 0.5f ? 0.f : 1.f); };
        s->led = [&P, key] { return P.getParam(key) > 0.5f ? 2 : 0; };   // lit over the upper (conga / claves / maracas) label
        s->ledAt = {0.5f, 0.1f};
    }
    // BASIC-VARIATION A / AB / B
    {
        auto* s = addSpot(225, 800, 100, 90);
        s->onPress = [&P] { auto* p = P.apvts.getParameter("variation"); const int v = (int) std::round(p->getValue() * 2); p->setValueNotifyingHost(((v + 1) % 3) / 2.f); };
    }
    // I/F-VARIATION A / B
    {
        auto* s = addSpot(1725, 800, 92, 60);
        s->onPress = [&P] { auto* p = P.apvts.getParameter("ifVariation"); p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.f : 1.f); };
    }
    // START / STOP
    {
        auto* s = addSpot(195, 940, 156, 82);
        s->onPress = [&P] { P.setParam("run", P.getParam("run") > 0.5f ? 0.f : 1.f); };
        s->led = [&P] { return P.running.load() ? 2 : 0; }; s->ledAt = {0.5f, 0.15f};
    }
    // the 16 step keys: write steps, pick rhythms in PLAY, or play the voices in PADS mode
    for (int i = 0; i < 16; i++) {
        auto* s = addSpot(STEPX[i] - 34, 928, 68, 106);
        s->ledAt = {0.5f, 20.f / 106.f};
        s->onPress = [this, &P, i] {
            if (padMode) { static const char* V[] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH", "LC", "MC", "HC", "CL", "MA"}; P.hit(V[i], false); return; }
            if (P.mode.load() <= 1) P.toggleStep(P.track.load(), i);
            else if (i < 12) P.selectRhythm(i);
        };
        s->led = [this, &P, i] {
            if (padMode) return 0;
            if (P.running.load() && P.runStep.load() % 16 == i) return 2;
            if (P.mode.load() <= 1) return P.getStep(P.track.load(), i) ? 2 : 0;
            return i == P.basic.load() ? 1 : 0;
        };
    }
    // buttons in the blank space under the sound strips, as in the app
    for (auto* b : {&circuit, &sync, &pads}) { addAndMakeVisible(b); b->setColour(juce::TextButton::buttonColourId, kCream); b->setColour(juce::TextButton::textColourOffId, juce::Colours::black); }
    circuit.onClick = [this] { ed.showBoard(true); };
    sync.setClickingTogglesState(false);
    sync.onClick = [&P] { P.setParam("syncHost", P.getParam("syncHost") > 0.5f ? 0.f : 1.f); };
    pads.onClick = [this] { padMode = !padMode; pads.setToggleState(padMode, juce::dontSendNotification); repaint(); };
    addAndMakeVisible(presets);
    for (int i = 0; i < P.getNumPrograms(); i++) presets.addItem(P.getProgramName(i), i + 1);
    presets.setTextWhenNothingSelected("PRESETS");
    presets.onChange = [this, &P] { if (presets.getSelectedId() > 0) P.setCurrentProgram(presets.getSelectedId() - 1); };
}

void FaceView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    g.drawImage(photo, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    // the readout, in the strip left of "Rhythm Composer"
    const auto r = toScreen({300, 80, 680, 122}).toFloat();   // the empty strip at the top, between POWER and the logo
    g.setColour(juce::Colour(0xee0a110d)); g.fillRoundedRectangle(r, r.getHeight() * 0.08f);
    g.setColour(juce::Colour(0xffffb15a));
    g.setFont(juce::FontOptions(r.getHeight() * 0.34f, juce::Font::bold));
    g.drawFittedText(ed.proc.lcdText() + (padMode ? "  PADS" : ""), r.reduced(r.getHeight() * 0.12f).toNearestInt(), juce::Justification::centredLeft, 2);
    sync.setToggleState(ed.proc.getParam("syncHost") > 0.5f, juce::dontSendNotification);
}

void FaceView::resized() {
    for (int i = 0; i < knobs.size(); i++) knobs[i]->setBounds(toScreen(knobRects[(size_t) i]));
    for (int i = 0; i < spots.size(); i++) spots[i]->setBounds(toScreen(spotRects[(size_t) i]));
    const int y = 628, h = 64;   // under the instrument strips, left of "Rhythm Composer"
    circuit.setBounds(toScreen({516, y, 120, h})); sync.setBounds(toScreen({644, y, 120, h})); pads.setBounds(toScreen({772, y, 90, h}));
    presets.setBounds(toScreen({870, y, 140, h}));
}

void FaceView::refresh() { for (auto* k : knobs) k->repaint(); for (auto* s : spots) s->repaint(); repaint(toScreen({300, 80, 680, 122})); }

/* ---------------- circuit board ---------------- */
struct Sect { const char* id; const char* nav; const char* name; std::vector<const char*> voices; };
static const std::vector<Sect>& sects() {
    static const std::vector<Sect> S = {
        {"BD", "BD", "BASS DRUM", {"BD"}}, {"SD", "SD", "SNARE DRUM", {"SD"}}, {"TOMS", "TOMS", "TOMS / CONGAS", {"LT", "MT", "HT", "LC", "MC", "HC"}},
        {"RSCL", "RIM", "RIM / CLAVES", {"RS", "CL"}}, {"CPMA", "CLAP", "CLAP / MARACAS", {"CP", "MA"}}, {"CB", "BELL", "COWBELL", {"CB"}},
        {"CY", "CYM", "CYMBAL", {"CY"}}, {"HH", "HATS", "HI-HATS", {"OH", "CH"}}, {"ACC", "ACCENT", "ACCENT", {}}, {"TRIG", "TRIG OUT", "TRIGGER OUT", {"TRIG"}},
        {"MACH", "MACHINE", "MACHINE", {}}, {"REV", "REV 7", "REV 7 DIGITAL REVERBERATOR", {}}, {"DLY", "2290", "2290 DYNAMIC DIGITAL DELAY", {}}};
    return S;
}
// which board page a parameter lives on (the app's sectionOf); "" = on the faceplate
static juce::String sectionOf(const juce::String& k) {
    static const std::map<juce::String, juce::String> MOD = {{"BD", "BD"}, {"SD", "SD"}, {"TOM", "TOMS"}, {"CONGA", "TOMS"}, {"RS", "RSCL"}, {"CL", "RSCL"},
        {"CP", "CPMA"}, {"MA", "CPMA"}, {"CB", "CB"}, {"CY", "CY"}, {"HH", "HH"}, {"CH", "HH"}, {"OH", "HH"}, {"AC", "ACC"}, {"TRIG", "TRIG"}};
    static const std::map<juce::String, juce::String> SEND = {{"BD", "BD"}, {"SD", "SD"}, {"LT", "TOMS"}, {"MT", "TOMS"}, {"HT", "TOMS"}, {"RS", "RSCL"},
        {"CP", "CPMA"}, {"CB", "CB"}, {"CY", "CY"}, {"OH", "HH"}, {"CH", "HH"}, {"TRIG", "TRIG"}};
    if (k.startsWith("mod.")) {
        const auto g = k.fromFirstOccurrenceOf(".", false, false).upToFirstOccurrenceOf(".", false, false);
        if (!k.fromFirstOccurrenceOf(".", false, false).contains(".")) return "MACH";
        auto i = MOD.find(g); return i == MOD.end() ? "MACH" : i->second;
    }
    if (k.startsWith("rev.send.") || k.startsWith("dly.send.")) { auto i = SEND.find(k.fromLastOccurrenceOf(".", false, false)); return i == SEND.end() ? "MACH" : i->second; }
    if (k.startsWith("rev.")) return "REV";
    if (k.startsWith("dly.")) return "DLY";
    if (k == "master" || k.endsWith(".level") || k.endsWith(".tone") || k.endsWith(".decay") || k.endsWith(".snappy") || k.endsWith(".tuning") || k.endsWith(".sel")) return {};
    return "MACH";
}

BoardView::BoardView(EightEditor& e) : ed(e) {
    int i = 0;
    for (auto& s : sects()) {
        auto* b = nav.add(new juce::TextButton(s.nav)); addAndMakeVisible(b);
        const juce::String id = s.id;
        b->onClick = [this, id] { showPage(id); };
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0b1f12)); b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffd25a));
        b->setColour(juce::TextButton::textColourOffId, kSilk); b->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        i++;
    }
    for (auto* b : {&stock, &close}) { addAndMakeVisible(b); b->setColour(juce::TextButton::buttonColourId, kCream); b->setColour(juce::TextButton::textColourOffId, juce::Colours::black); }
    close.onClick = [this] { ed.showBoard(false); };
    stock.onClick = [this] { for (auto& p : params()) if (juce::String(p.key).startsWith("mod.")) ed.proc.setParam(p.key, (float) p.def); };
    showPage("BD");
}

void BoardView::paint(juce::Graphics& g) {
    g.setGradientFill(juce::ColourGradient(kPcb, 0, 0, kPcb2, (float) getWidth(), (float) getHeight(), false)); g.fillAll();
    g.setColour(juce::Colour(0x553f9a5c));
    for (int i = 0; i < 18; i++) { const float y = 60.f + i * getHeight() / 19.f, x = (float) ((i * 137) % juce::jmax(1, getWidth() - 200));
        g.drawLine(x, y, x + 160, y, 3); g.drawLine(x + 160, y, x + 190, y + 30, 3); }
    g.setColour(kSilk); g.setFont(juce::FontOptions(getHeight() * 0.05f, juce::Font::bold));
    g.drawText("TR-808 MAIN BOARD", 16, 0, getWidth() / 2, getHeight() / 12, juce::Justification::centredLeft);
    for (auto& s : sects()) if (page == juce::String(s.id)) {
        g.setColour(juce::Colour(0x66ffffff)); g.drawRoundedRectangle(pageArea.toFloat().expanded(4), 8, 2);
        g.setColour(kSilk); g.setFont(juce::FontOptions(getHeight() * 0.035f, juce::Font::bold));
        g.drawText(s.name, pageArea.getX(), pageArea.getY() - (int) (getHeight() * 0.045f), pageArea.getWidth(), (int) (getHeight() * 0.04f), juce::Justification::centredLeft);
    }
}

void BoardView::showPage(const juce::String& id) {
    page = id;
    for (int i = 0; i < nav.size(); i++) nav[i]->setToggleState(id == sects()[(size_t) i].id, juce::dontSendNotification);
    sAtt.clear(); cAtt.clear(); bAtt.clear(); ctl.clear(); labels.clear(); loads.clear();
    auto& P = ed.proc;
    for (auto& p : params()) {
        if (sectionOf(p.key) != id) continue;
        const juce::String pid = paramId(p.key);
        auto* lab = labels.add(new juce::Label({}, juce::String(p.label).upToFirstOccurrenceOf(" (", false, false)));
        lab->setJustificationType(juce::Justification::centred); lab->setColour(juce::Label::textColourId, kSilk);
        lab->setMinimumHorizontalScale(0.6f); addAndMakeVisible(lab);
        if (!p.opts.empty()) {
            auto* c = new juce::ComboBox(); ctl.add(c); addAndMakeVisible(c);
            int n = 1; for (auto& o : p.opts) c->addItem(o, n++);
            cAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(P.apvts, pid, *c));
        } else if (p.step == 1 && p.max - p.min == 1) {
            auto* b = new juce::TextButton("OFF"); ctl.add(b); addAndMakeVisible(b);
            b->setClickingTogglesState(true); b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd42c20));
            b->onStateChange = [b] { b->setButtonText(b->getToggleState() ? "ON" : "OFF"); };
            bAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(P.apvts, pid, *b));
        } else {
            auto* s = new juce::Slider(juce::Slider::LinearVertical, juce::Slider::TextBoxBelow); ctl.add(s); addAndMakeVisible(s);
            s->setColour(juce::Slider::thumbColourId, juce::String(p.key).contains(".send.") ? kOrange : kCream);
            s->setColour(juce::Slider::trackColourId, juce::Colour(0xff0b0d0c)); s->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff0b0d0c));
            s->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffffeeb8)); s->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0b1f12));
            s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            sAtt.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(P.apvts, pid, *s));
            const int dec = p.max - p.min > 50 ? 0 : p.max - p.min > 5 ? 1 : 2;
            s->textFromValueFunction = [dec](double v) { return juce::String(v, dec); };
            s->updateText();
        }
    }
    // sample slots for this page's voices
    for (auto& s : sects()) if (id == juce::String(s.id)) for (auto* v : s.voices) {
        const juce::String voice = v;
        auto* b = loads.add(new juce::TextButton()); addAndMakeVisible(b);
        auto label = [this, b, voice] { const auto nm = ed.proc.sampleName(voice); b->setButtonText(voice + (nm.isEmpty() ? "  LOAD SAMPLE" : "  " + nm + "  (CLEAR)")); };
        label();
        b->setColour(juce::TextButton::buttonColourId, kCream); b->setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        b->onClick = [this, voice, label] {
            if (ed.proc.sampleName(voice).isNotEmpty()) { ed.proc.clearSample(voice); label(); return; }
            chooser = std::make_unique<juce::FileChooser>("Sample for " + voice, juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, voice, label](const juce::FileChooser& fc) {
                const auto f = fc.getResult(); if (!f.existsAsFile()) return;
                const auto err = ed.proc.loadSample(voice, f);
                if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Omega 808", err);
                label();
            });
        };
    }
    layoutPage(); repaint();
}

void BoardView::resized() {
    const int H = getHeight(), W = getWidth(), head = H / 12, nw = juce::jmax(60, W / 10), gap = 6;
    close.setBounds(W - 130 - 10, 8, 130, head - 14); stock.setBounds(W - 300 - 20, 8, 170, head - 14);
    const int half = 7, bh = (H - head - 16 - gap * (half - 1)) / half;
    for (int i = 0; i < nav.size(); i++) {
        const bool left = i < half; const int r = left ? i : i - half;
        nav[i]->setBounds(left ? 8 : W - nw - 8, head + 8 + r * (bh + gap), nw, bh);
    }
    pageArea = juce::Rectangle<int>(nw + 24, head + (int) (H * 0.07f), W - 2 * nw - 48, H - head - (int) (H * 0.07f) - 16);
    layoutPage();
}

// one row of faders (two rows when there are many), the sample buttons in a column on the right: always fits, never scrolls
void BoardView::layoutPage() {
    if (pageArea.isEmpty()) return;
    auto area = pageArea.reduced(8);
    if (!loads.isEmpty()) {
        auto col = area.removeFromRight(juce::jmin(260, area.getWidth() / 3));
        const int h = juce::jmin(40, col.getHeight() / juce::jmax(1, loads.size()) - 6);
        for (auto* b : loads) { b->setBounds(col.removeFromTop(h)); col.removeFromTop(6); }
        area.removeFromRight(10);
    }
    const int n = ctl.size(); if (!n) return;
    const int rows = (area.getWidth() / n < 64 && n > 6) ? 2 : 1, perRow = (n + rows - 1) / rows;
    const int cw = area.getWidth() / perRow, rh = area.getHeight() / rows, lh = juce::jmin(34, rh / 6);
    for (int i = 0; i < n; i++) {
        const int r = i / perRow, c = i % perRow;
        juce::Rectangle<int> cell(area.getX() + c * cw, area.getY() + r * rh, cw, rh);
        labels[i]->setFont(juce::FontOptions(juce::jlimit(11.f, 16.f, cw * 0.2f), juce::Font::bold));
        labels[i]->setBounds(cell.removeFromBottom(lh));
        auto* comp = ctl[i];
        if (dynamic_cast<juce::Slider*>(comp)) { comp->setBounds(cell.reduced(juce::jmax(2, cw / 10), 2)); dynamic_cast<juce::Slider*>(comp)->setTextBoxStyle(juce::Slider::TextBoxBelow, false, cw - 4, 20); }
        else comp->setBounds(cell.withSizeKeepingCentre(juce::jmin(cw - 6, 120), 36));
    }
}

/* ---------------- look ---------------- */
void EightLnF::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s) {
    if (st != juce::Slider::LinearVertical) { LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0, 0, st, s); return; }
    const float cx = x + w * 0.5f, tw = juce::jmin(46.f, w * 0.8f);
    g.setColour(juce::Colour(0xff0b0d0c)); g.fillRoundedRectangle(cx - tw / 2, (float) y - 10, tw, (float) h + 20, 6);
    g.setColour(juce::Colour(0xff6d7470).withAlpha(0.5f));
    for (int i = 0; i <= 10; i++) { const float yy = y + h * i / 10.f; g.drawHorizontalLine((int) yy, cx - tw * 0.38f, cx + tw * 0.38f); }
    g.setColour(juce::Colours::black); g.fillRect(cx - 2, (float) y, 4.f, (float) h);
    const juce::Colour cap = s.findColour(juce::Slider::thumbColourId);
    const juce::Rectangle<float> r(cx - tw * 0.46f, pos - 12, tw * 0.92f, 24);
    g.setGradientFill(juce::ColourGradient(cap.brighter(0.3f), r.getX(), r.getY(), cap.darker(0.3f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 3);
    g.setColour(juce::Colour(0xff2a2a2a)); g.fillRect(r.getX(), r.getCentreY() - 1.5f, r.getWidth(), 3.f);
}

/* ---------------- editor ---------------- */
EightEditor::EightEditor(EightProcessor& p) : AudioProcessorEditor(p), proc(p), face(*this), board(*this) {
    lnf.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b1f12));
    setLookAndFeel(&lnf);
    addAndMakeVisible(face); addChildComponent(board);
    setResizable(true, true);
    setResizeLimits(600, 357, 2400, 1428);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) PW / PH);
    setSize(1100, 655);
    startTimerHz(30);
}
EightEditor::~EightEditor() { setLookAndFeel(nullptr); }
void EightEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colours::black); }
void EightEditor::resized() { face.setBounds(getLocalBounds()); board.setBounds(getLocalBounds()); }
void EightEditor::showBoard(bool on) { board.setVisible(on); face.setVisible(!on); }
void EightEditor::timerCallback() { if (face.isVisible()) face.refresh(); }

}  // namespace eight
