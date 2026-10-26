#include "PluginEditor.h"

namespace
{
const juce::Colour bg (0xff030b08), card (0xff08170f), card2 (0xff0e2219), ink (0xffdcf7ea), dim (0xff7fa292), acc (0xff4dffa6), line (0xff1c3a2c), bad (0xffff6b5e);
juce::Colour kindColour (int k)
{
    static const juce::uint32 c[] = { 0xffff5ec8, 0xffffd23f, 0xff4fc3ff, 0xff9c8cff, 0xffff8a3d, 0xffb9c4c0, 0xff56615d };
    return juce::Colour (c[juce::jlimit (0, 6, k)]);
}
double nowSec() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
}

/* ---------------- radar ---------------- */
void Radar::timerCallback()
{
    const double t = nowSec();
    int s1, n1, s2, n2;
    const int ready = proc.flashFifo.getNumReady();
    proc.flashFifo.prepareToRead (ready, s1, n1, s2, n2);
    auto take = [&] (int start, int count) {
        for (int i = 0; i < count; ++i)
        {
            const auto& f = proc.flashes[(size_t) (start + i)];
            if (f.cell >= 0) litCells[f.cell] = t; else if (f.hash) lit[f.hash] = { t, f.lane };
        }
    };
    take (s1, n1); take (s2, n2);
    proc.flashFifo.finishedRead (n1 + n2);
    for (auto it = lit.begin(); it != lit.end();) it = t - it->second.first > 1 ? lit.erase (it) : std::next (it);
    for (auto it = litCells.begin(); it != litCells.end();) it = t - it->second > 1 ? litCells.erase (it) : std::next (it);
    sweep = std::fmod (sweep + 0.02f, juce::MathConstants<float>::twoPi);
    repaint();
}

void Radar::paint (juce::Graphics& g)
{
    using namespace sky;
    const float W = (float) juce::jmin (getWidth(), getHeight()), half = W / 2;
    const juce::Rectangle<float> box (0, 0, W, W);
    g.setColour (juce::Colour (0xff04100c)); g.fillRoundedRectangle (box, 10);
    const juce::ScopedLock sl (proc.model.lock);
    const double R = proc.model.settings.radius, sc = half / R;
    auto X = [&] (double x) { return (float) (half + x * sc); };
    auto Y = [&] (double y) { return (float) (half - y * sc); };
    const double t = nowSec();

    // heat grid and playhead
    float mx = 0; for (auto h : proc.model.heat) mx = juce::jmax (mx, h);
    const float cw = W / gridCols, ch = W / gridRows, th = proc.params.getRawParameterValue ("thresh")->load();
    for (int r = 0; r < gridRows; ++r)
        for (int c = 0; c < gridCols; ++c)
        {
            const float v = mx > 0 ? proc.model.heat[(size_t) (r * gridCols + c)] / mx : 0;
            const auto it = litCells.find (r * gridCols + c);
            const bool flash = it != litCells.end() && t - it->second < 0.2;
            g.setColour (flash ? juce::Colour (0x99c8ffe1) : acc.withAlpha (v >= th ? 0.1f + v * 0.45f : v * 0.12f));
            g.fillRect (c * cw + 1, (gridRows - 1 - r) * ch + 1, cw - 2, ch - 2);
        }
    const int step = proc.currentStep.load();
    if (step >= 0 && proc.running) { g.setColour (acc.withAlpha (0.55f)); g.drawRect (step * cw + 0.5f, 0.5f, cw - 1, W - 1, 1.5f); }

    // rings, cross, compass, sweep
    g.setColour (juce::Colour (0x2e78ffbe));
    for (int k = 1; k <= 4; ++k) { const float r = half * k / 4; g.drawEllipse (half - r, half - r, r * 2, r * 2, 1); }
    g.drawLine (half, 0, half, W); g.drawLine (0, half, W, half);
    juce::Path wedge; wedge.addPieSegment (box, sweep - 0.35f, sweep, 0);
    g.setColour (juce::Colour (0x1478ffbe)); g.fillPath (wedge);
    g.setColour (juce::Colour (0x8096ffc8)); g.setFont (juce::FontOptions (11.0f));
    g.drawText ("N", juce::Rectangle<float> (half - 10, 2, 20, 14), juce::Justification::centred);
    g.drawText ("S", juce::Rectangle<float> (half - 10, W - 16, 20, 14), juce::Justification::centred);
    g.drawText ("W", juce::Rectangle<float> (4, half - 16, 20, 14), juce::Justification::left);
    g.drawText ("E", juce::Rectangle<float> (W - 24, half - 16, 20, 14), juce::Justification::right);
    g.drawText (juce::String ((int) R) + " km radius", juce::Rectangle<float> (8, W - 16, 120, 14), juce::Justification::left);
    g.setColour (acc); g.fillEllipse (half - 3, half - 3, 6, 6);

    // planes, dead-reckoned between updates so they move smoothly
    drawn.clear();
    const auto nowMs = juce::Time::currentTimeMillis();
    for (auto& [id, p] : proc.model.planes)
    {
        const double el = juce::jlimit (0.0, 15.0, (nowMs - p.a.t) / 1000.0), km = p.a.gnd ? 0 : p.a.gs * 1.852 / 3600 * el, a = p.a.trk * juce::MathConstants<double>::pi / 180;
        const float px = X (p.x + std::sin (a) * km), py = Y (p.y + std::cos (a) * km);
        drawn.push_back ({ id, px, py });
        auto c = p.a.emg && ((int) (t * 3.3) % 2) ? bad : kindColour (p.kind);
        const bool isSel = id == selected;
        if (p.a.gnd && ! isSel) { g.setColour (c); g.fillRect (px - 1.5f, py - 1.5f, 3.0f, 3.0f); continue; }   // parked and taxiing traffic stays out of the way
        if (trails && p.hist.size() > 1)
        {
            juce::Path tr; tr.startNewSubPath (X (p.hist[0].x), Y (p.hist[0].y));
            for (size_t i = 1; i < p.hist.size(); ++i) tr.lineTo (X (p.hist[i].x), Y (p.hist[i].y));
            tr.lineTo (px, py);
            g.setColour (c.withAlpha (0.4f)); g.strokePath (tr, juce::PathStrokeType (1.2f));
        }
        if (auto it = lit.find (p.hash); it != lit.end() && t - it->second.first < 0.3)
        {
            const float k = (float) ((t - it->second.first) / 0.3), r = 6 + k * 16;
            g.setColour (juce::Colour (laneInfo[it->second.second].colour).withAlpha (1 - k)); g.drawEllipse (px - r, py - r, r * 2, r * 2, 2);
        }
        juce::Path pl; pl.startNewSubPath (0, -7); pl.lineTo (5, 5); pl.lineTo (0, 2.5f); pl.lineTo (-5, 5); pl.closeSubPath();
        g.setColour (c); g.fillPath (pl, juce::AffineTransform::rotation ((float) a).translated (px, py));
        if (isSel) { g.setColour (juce::Colours::white); g.drawEllipse (px - 11, py - 11, 22, 22, 1.5f); }
        if (labels || isSel)
        {
            g.setFont (juce::FontOptions (10.0f));
            g.setColour (c); g.drawText (p.a.cs, juce::Rectangle<float> (px + 8, py - 12, 90, 12), juce::Justification::left);
            g.setColour (ink.withAlpha (0.55f));
            g.drawText (p.a.gnd ? juce::String ("GND") : "FL" + juce::String ((int) std::round (p.a.alt / 100)).paddedLeft ('0', 3), juce::Rectangle<float> (px + 8, py, 90, 12), juce::Justification::left);
        }
    }
    if (proc.emergency) { g.setColour (bad); g.setFont (juce::FontOptions (13.0f, juce::Font::bold)); g.drawText ("EMERGENCY SQUAWK: key up a fourth", juce::Rectangle<float> (0, 18, W, 16), juce::Justification::centred); }

    if (auto it = proc.model.planes.find (selected); it != proc.model.planes.end())
    {
        const auto& p = it->second;
        juce::String s = (p.a.cs.isNotEmpty() ? p.a.cs : p.a.id) + "  " + p.a.type + "  " + (p.a.gnd ? juce::String ("on the ground") : juce::String ((int) p.a.alt) + " ft")
                       + "  " + juce::String ((int) p.a.gs) + " kt  " + juce::String ((int) p.a.trk) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0  ")) + juce::String ((int) p.a.vr) + " ft/min  "
                       + juce::String (p.dist, 1) + " km  -  " + kindName (p.kind) + (p.a.emg ? "  EMERGENCY" : "");
        g.setColour (card.withAlpha (0.9f)); g.fillRoundedRectangle (6, W - 40, W - 12, 22, 6);
        g.setColour (kindColour (p.kind)); g.setFont (juce::FontOptions (12.0f));
        g.drawText (s, juce::Rectangle<float> (12, W - 40, W - 24, 22), juce::Justification::centredLeft);
    }
    else selected.clear();
}

void Radar::mouseDown (const juce::MouseEvent& e)
{
    float best = 26; juce::String id;
    for (auto& d : drawn) { const float dd = std::hypot (d.px - e.position.x, d.py - e.position.y); if (dd < best) { best = dd; id = d.id; } }
    selected = id;
}

/* ---------------- editor ---------------- */
SkyEditor::SkyEditor (SkyProcessor& p) : AudioProcessorEditor (&p), proc (p), radar (p)
{
    lnf.setColourScheme ({ bg, card, card2, line, ink, card2, ink, acc, ink });
    lnf.setColour (juce::TextEditor::backgroundColourId, card2); lnf.setColour (juce::TextEditor::outlineColourId, line);
    lnf.setColour (juce::ComboBox::backgroundColourId, card2); lnf.setColour (juce::ComboBox::outlineColourId, line);
    lnf.setColour (juce::TextButton::buttonColourId, card2); lnf.setColour (juce::TextButton::buttonOnColourId, acc); lnf.setColour (juce::TextButton::textColourOnId, bg);
    lnf.setColour (juce::Slider::thumbColourId, acc); lnf.setColour (juce::Slider::trackColourId, acc.withAlpha (0.5f)); lnf.setColour (juce::Slider::backgroundColourId, line);
    lnf.setColour (juce::Label::textColourId, ink); lnf.setColour (juce::ToggleButton::tickColourId, acc);
    lnf.setColour (juce::TabbedComponent::backgroundColourId, card); lnf.setColour (juce::TabbedComponent::outlineColourId, line);
    lnf.setColour (juce::TabbedButtonBar::tabOutlineColourId, line); lnf.setColour (juce::TabbedButtonBar::frontOutlineColourId, acc);
    setLookAndFeel (&lnf);

    title.setText (juce::String::fromUTF8 ("\xe2\x9c\x88  SKY SEQUENCER"), juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold)); title.setColour (juce::Label::textColourId, acc);
    status.setFont (juce::FontOptions (12.0f)); placeLbl.setFont (juce::FontOptions (12.0f)); placeLbl.setColour (juce::Label::textColourId, dim);
    for (auto* c : { (juce::Component*) &title, (juce::Component*) &status, (juce::Component*) &placeLbl, (juce::Component*) &radar, (juce::Component*) &tabs }) addAndMakeVisible (c);

    tabs.addTab ("Where", card, &where, false);
    tabs.addTab ("Lanes", card, &lanes, false);
    tabs.addTab ("Mixer", card, &mixerPage, false);
    tabs.addTab ("Presets", card, &presets, false);
    tabs.addTab ("OB-Xa", card, &synthPage, false);
    tabs.addTab ("Drums & FX", card, &drumsPage, false);
    tabs.addTab ("Music", card, &music, false);
    tabs.setTabBarDepth (30);

    auto addTo = [] (juce::Component& page, std::initializer_list<juce::Component*> cs) { for (auto* c : cs) page.addAndMakeVisible (c); };
    auto hint = [] (juce::Label& l, const juce::String& s) { l.setText (s, juce::dontSendNotification); l.setFont (juce::FontOptions (12.0f)); l.setColour (juce::Label::textColourId, dim); l.setJustificationType (juce::Justification::topLeft); };

    /* Where */
    hint (searchHint, "Search any place, address or airport, or type coordinates (51.47, -0.45 or 51.47N 0.45W), or paste a map link.");
    search.setTextToShowWhenEmpty ("e.g. Brooklyn Bridge, Reykjavik, KJFK, 34.05, -118.25", dim);
    search.onReturnKey = [this] { runSearch(); };
    go.onClick = [this] { runSearch(); };
    locate.onClick = [this] { locateMe(); };
    locate.setTooltip ("Finds you from your internet connection (approximate, usually your town)");
    results.setTextWhenNothingSelected ("Search results"); results.setTextWhenNoChoicesAvailable ("No results yet");
    results.onChange = [this] { const int i = results.getSelectedItemIndex(); if (i >= 0 && i < (int) found.size()) proc.setLocation (found[(size_t) i].lat, found[(size_t) i].lon, found[(size_t) i].name); refreshWhere(); };
    airportLbl.setText ("Busy airports", juce::dontSendNotification);
    { int id = 1; for (auto& a : sky::airports()) airportBox.addItem (a.name, id++); }
    airportBox.setTextWhenNothingSelected ("Pick an airport");
    airportBox.onChange = [this] { const auto list = sky::airports(); const int i = airportBox.getSelectedItemIndex(); if (i >= 0) proc.setLocation (list[(size_t) i].lat, list[(size_t) i].lon, list[(size_t) i].name); refreshWhere(); };
    latLbl.setText ("Lat", juce::dontSendNotification); lonLbl.setText ("Lon", juce::dontSendNotification);
    for (auto* e : { &latEd, &lonEd }) { e->setInputRestrictions (12, "0123456789.-"); e->onReturnKey = [this] { setLL.triggerClick(); }; }
    setLL.onClick = [this] {
        const double la = latEd.getText().getDoubleValue(), lo = lonEd.getText().getDoubleValue();
        if (std::abs (la) > 90 || std::abs (lo) > 180 || latEd.isEmpty() || lonEd.isEmpty()) { showMessage ("Latitude is -90 to 90, longitude -180 to 180", true); return; }
        proc.setLocation (la, lo, {}); refreshWhere(); showMessage ("Centred on " + juce::String (la, 4) + ", " + juce::String (lo, 4));
    };
    radiusLbl.setText ("Radius", juce::dontSendNotification);
    radius.setRange (5, 150, 1); radius.setTextValueSuffix (" km"); radius.setSliderStyle (juce::Slider::LinearHorizontal); radius.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    radius.onDragEnd = [this] { const auto f = proc.feedSettings(); proc.setFeed (f.source, radius.getValue(), f.localUrl); };
    radius.onValueChange = [this] { if (! radius.isMouseButtonDown()) radius.onDragEnd(); };
    feedLbl.setText ("Feed", juce::dontSendNotification);
    sourceBox.addItemList (sky::sourceNames(), 1);
    sourceBox.onChange = [this] { const auto f = proc.feedSettings(); proc.setFeed (sourceBox.getSelectedItemIndex(), f.radius, urlEd.getText()); refreshWhere(); resized(); };
    urlLbl.setText ("Receiver", juce::dontSendNotification);
    urlEd.onReturnKey = urlEd.onFocusLost = [this] { const auto f = proc.feedSettings(); proc.setFeed (f.source, f.radius, urlEd.getText().trim()); };
    windowLbl.setText ("Grid collects over", juce::dontSendNotification);
    windowBox.addItemList ({ "1 min", "2 min", "5 min", "10 min", "15 min", "30 min", "60 min" }, 1);
    comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "window", windowBox));
    threshLbl.setText ("Grid threshold", juce::dontSendNotification);
    thresh.setSliderStyle (juce::Slider::LinearHorizontal); thresh.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "thresh", thresh));
    freeze.setClickingTogglesState (true); freeze.onClick = [this] { proc.feed.frozen = freeze.getToggleState(); };
    freeze.setToggleState (proc.feed.frozen, juce::dontSendNotification);
    clearGrid.onClick = [this] { const juce::ScopedLock sl (proc.model.lock); proc.model.heat.fill (0); };
    for (auto* b : { &labelsBtn, &trailsBtn }) { b->setClickingTogglesState (true); b->setToggleState (true, juce::dontSendNotification); }
    labelsBtn.onClick = [this] { radar.labels = labelsBtn.getToggleState(); };
    trailsBtn.onClick = [this] { radar.trails = trailsBtn.getToggleState(); };
    msg.setFont (juce::FontOptions (12.0f));
    addTo (where, { &searchHint, &search, &go, &locate, &results, &msg, &airportLbl, &airportBox, &latLbl, &latEd, &lonLbl, &lonEd, &setLL, &radiusLbl, &radius,
                    &feedLbl, &sourceBox, &urlLbl, &urlEd, &windowLbl, &windowBox, &threshLbl, &thresh, &freeze, &clearGrid, &labelsBtn, &trailsBtn });
    where.layout = [this] (juce::Rectangle<int> r) {
        auto row = [&r] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (6); return x; };
        searchHint.setBounds (row (32));
        { auto x = row (28); go.setBounds (x.removeFromRight (64)); x.removeFromRight (6); search.setBounds (x); }
        { auto x = row (28); locate.setBounds (x.removeFromLeft (110)); x.removeFromLeft (6); results.setBounds (x); }
        msg.setBounds (row (18));
        { auto x = row (28); airportLbl.setBounds (x.removeFromLeft (96)); airportBox.setBounds (x); }
        { auto x = row (28); latLbl.setBounds (x.removeFromLeft (30)); latEd.setBounds (x.removeFromLeft (100)); x.removeFromLeft (10);
          lonLbl.setBounds (x.removeFromLeft (32)); lonEd.setBounds (x.removeFromLeft (100)); x.removeFromLeft (8); setLL.setBounds (x.removeFromLeft (56)); }
        { auto x = row (28); radiusLbl.setBounds (x.removeFromLeft (96)); radius.setBounds (x); }
        r.removeFromTop (6);
        { auto x = row (28); feedLbl.setBounds (x.removeFromLeft (96)); sourceBox.setBounds (x); }
        const bool local = sourceBox.getSelectedItemIndex() == sky::srcLocal;
        urlLbl.setVisible (local); urlEd.setVisible (local);
        if (local) { auto x = row (28); urlLbl.setBounds (x.removeFromLeft (96)); urlEd.setBounds (x); }
        r.removeFromTop (6);
        { auto x = row (28); windowLbl.setBounds (x.removeFromLeft (130)); windowBox.setBounds (x.removeFromLeft (100)); }
        { auto x = row (28); threshLbl.setBounds (x.removeFromLeft (130)); thresh.setBounds (x); }
        { auto x = row (28); for (auto* b : { &freeze, &clearGrid, &labelsBtn, &trailsBtn }) { b->setBounds (x.removeFromLeft (96)); x.removeFromLeft (6); } }
    };

    /* Lanes */
    hint (lanesHint, "Each behaviour is a lane with its own sound, rhythm, note length and MIDI channel. Auto-vary keeps lanes changing every few bars. Volume, pan and effects are on the Mixer.");
    autoLbl.setText ("Auto-vary", juce::dontSendNotification);
    autoBox.addItemList ({ "Off", "Every 4 bars", "Every 8 bars", "Every 16 bars" }, 1);
    comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "autobars", autoBox));
    lanes.addAndMakeVisible (lanesHint); lanes.addAndMakeVisible (autoLbl); lanes.addAndMakeVisible (autoBox);
    for (int i = 0; i < numLanes; ++i)
    {
        auto& rw = rows[(size_t) i]; const auto& li = laneInfo[i]; const juce::String id (li.id);
        rw.name.setText (juce::String (li.name) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + li.to, juce::dontSendNotification);
        rw.name.setColour (juce::Label::textColourId, juce::Colour (li.colour)); rw.name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        rw.engine.addItemList (sky::engineNames(), 1);
        for (int c = 1; c <= 16; ++c) rw.ch.addItem ("Ch " + juce::String (c), c);
        rw.drum.addItemList (sky::PresetLibrary::get().patternNames(), 1);
        rw.dvl.setText ("Drums", juce::dontSendNotification);
        for (auto* s : { &rw.vol, &rw.rev, &rw.dly, &rw.dv }) { s->setSliderStyle (juce::Slider::LinearHorizontal); s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0); s->setColour (juce::Slider::thumbColourId, juce::Colour (li.colour)); }
        rw.vl.setText ("Vol", juce::dontSendNotification); rw.rl.setText ("Rev", juce::dontSendNotification); rw.dl.setText ("Dly", juce::dontSendNotification);
        for (auto* l : { &rw.vl, &rw.rl, &rw.dl, &rw.dvl }) { l->setFont (juce::FontOptions (11.0f)); l->setColour (juce::Label::textColourId, dim); }
        rw.aOn = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, id + "_on", rw.on);
        rw.aEngine = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, id + "_engine", rw.engine);
        rw.aCh = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, id + "_ch", rw.ch);
        rw.rhythm.addItemList (sky::PresetLibrary::get().rhythmNames(), 1);
        rw.gl.setText ("Length", juce::dontSendNotification); rw.gl.setFont (juce::FontOptions (11.0f)); rw.gl.setColour (juce::Label::textColourId, dim);
        rw.gate.setSliderStyle (juce::Slider::LinearHorizontal); rw.gate.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0); rw.gate.setColour (juce::Slider::thumbColourId, juce::Colour (li.colour));
        rw.aRhythm = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, id + "_rhythm", rw.rhythm);
        rw.aGate = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id + "_gate", rw.gate);
        rw.aAuto = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, id + "_auto", rw.autoBtn);
        rw.rhythm.setTooltip ("Which 16th steps this lane plays on"); rw.gate.setTooltip ("Note length");
        // the arpeggio and the grid keep their own timing (turn rate, grid columns), so only the others get a rhythm
        rw.rhythm.setEnabled (sky::roleRhythms (id).size() > 0);
        rw.aDrum = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, id + "_drum", rw.drum);
        rw.aDv = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id + "_dv", rw.dv);
        rw.drum.setTooltip ("Drum loop this lane triggers: it starts on the next bar after the lane plays and stops after two quiet bars");
        addTo (lanes, { &rw.name, &rw.on, &rw.engine, &rw.ch, &rw.drum, &rw.dv, &rw.dvl, &rw.rhythm, &rw.gate, &rw.gl, &rw.autoBtn });
    }
    lanes.layout = [this] (juce::Rectangle<int> r) {
        lanesHint.setBounds (r.removeFromTop (34));
        { auto x = r.removeFromTop (26); autoLbl.setBounds (x.removeFromLeft (70)); autoBox.setBounds (x.removeFromLeft (150)); }
        r.removeFromTop (4);
        const int h = juce::jmin (84, r.getHeight() / numLanes);
        for (auto& rw : rows)
        {
            auto b = r.removeFromTop (h).reduced (0, 3);
            auto top = b.removeFromTop (26);
            rw.ch.setBounds (top.removeFromRight (74)); top.removeFromRight (6);
            rw.engine.setBounds (top.removeFromRight (110)); top.removeFromRight (6);
            rw.on.setBounds (top.removeFromRight (52));
            rw.name.setBounds (top);
            b.removeFromTop (3);
            { auto mid = b.removeFromTop (24); rw.drum.setBounds (mid.removeFromLeft (juce::jmin (220, mid.getWidth() / 2))); mid.removeFromLeft (10);
              rw.dvl.setBounds (mid.removeFromLeft (40)); rw.dv.setBounds (mid.reduced (2, 0)); }
            b.removeFromTop (3);
            { auto low = b.removeFromTop (24); rw.rhythm.setBounds (low.removeFromLeft (juce::jmin (220, low.getWidth() / 2))); low.removeFromLeft (10);
              rw.autoBtn.setBounds (low.removeFromRight (92)); rw.gl.setBounds (low.removeFromLeft (40)); rw.gate.setBounds (low.reduced (2, 0)); }
        }
    };

    /* Presets */
    const auto& lib = sky::PresetLibrary::get();
    hint (presetHint, "A preset sets the key, scale, tempo, every lane's sound and OB-Xa patch, the drum loops, the kit and the delay. Your location and feed stay as they are. "
                      "Everything is saved with your project.");
    for (int i = 0; i < lib.presets.size(); ++i) presetBox.addItem (juce::String (i + 1) + ". " + lib.presets[i]["name"].toString(), i + 1);
    presetBox.setTextWhenNothingSelected ("Pick a preset");
    presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (i >= 0) proc.loadPreset (i); };
    prevBtn.onClick = [this] { presetBox.setSelectedItemIndex (juce::jmax (0, presetBox.getSelectedItemIndex() - 1)); };
    nextBtn.onClick = [this] { presetBox.setSelectedItemIndex (juce::jmin (presetBox.getNumItems() - 1, presetBox.getSelectedItemIndex() + 1)); };
    genBtn.onClick = [this] { proc.generatePreset(); presetBox.setSelectedId (0, juce::dontSendNotification); };
    presetNow.setFont (juce::FontOptions (14.0f, juce::Font::bold)); presetNow.setColour (juce::Label::textColourId, acc);
    addTo (presets, { &presetHint, &presetNow, &presetBox, &prevBtn, &nextBtn, &genBtn });
    presets.layout = [this] (juce::Rectangle<int> r) {
        presetHint.setBounds (r.removeFromTop (48)); r.removeFromTop (6);
        presetNow.setBounds (r.removeFromTop (24)); r.removeFromTop (6);
        { auto x = r.removeFromTop (28); nextBtn.setBounds (x.removeFromRight (34)); x.removeFromRight (4); prevBtn.setBounds (x.removeFromRight (34)); x.removeFromRight (6); presetBox.setBounds (x); }
        r.removeFromTop (10); genBtn.setBounds (r.removeFromTop (30).removeFromLeft (200));
    };

    /* OB-Xa */
    hint (synthHint, "Two oscillators, a 12 or 24 dB resonant filter with its own envelope, an amp envelope and an LFO, in the style of the Oberheim OB-Xa. "
                     "Each lane has its own patch; set the lane's sound to OB-Xa on the Lanes tab.");
    for (int i = 0; i < numLanes; ++i) synthLane.addItem (juce::String (laneInfo[i].name) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + laneInfo[i].to, i + 1);
    synthLane.onChange = [this] { bindPatch (synthLane.getSelectedItemIndex()); };
    rndPatch.onClick = [this] { proc.randomPatch (juce::jmax (0, synthLane.getSelectedItemIndex())); };
    addTo (synthPage, { &synthHint, &synthLane, &rndPatch });
    for (int k = 0; k < 21; ++k)
    {
        const auto& s = sky::patchSpecs()[(size_t) k];
        pLabels[(size_t) k].setText (s.label, juce::dontSendNotification); pLabels[(size_t) k].setFont (juce::FontOptions (12.0f)); pLabels[(size_t) k].setColour (juce::Label::textColourId, dim);
        pSliders[(size_t) k].setSliderStyle (juce::Slider::LinearHorizontal); pSliders[(size_t) k].setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        pSliders[(size_t) k].setNumDecimalPlacesToDisplay (s.hi >= 100 ? 0 : s.hi >= 10 ? 1 : 3);
        pCombos[(size_t) k].addItemList (sky::choiceLabels (s.id), 1);
        synthPage.addAndMakeVisible (pLabels[(size_t) k]);
        synthPage.addChildComponent (s.integer ? (juce::Component&) pCombos[(size_t) k] : (juce::Component&) pSliders[(size_t) k]);
        if (s.integer) pCombos[(size_t) k].setVisible (true); else pSliders[(size_t) k].setVisible (true);
    }
    synthLane.setSelectedItemIndex (0, juce::sendNotificationSync);
    synthPage.layout = [this] (juce::Rectangle<int> r) {
        synthHint.setBounds (r.removeFromTop (34)); r.removeFromTop (4);
        { auto x = r.removeFromTop (28); rndPatch.setBounds (x.removeFromRight (130)); x.removeFromRight (8); synthLane.setBounds (x); }
        r.removeFromTop (8);
        const int colW = (r.getWidth() - 12) / 2, rowH = juce::jmin (26, r.getHeight() / 11);
        for (int k = 0; k < 21; ++k)
        {
            const int col = k < 11 ? 0 : 1, row = k < 11 ? k : k - 11;
            juce::Rectangle<int> c (r.getX() + col * (colW + 12), r.getY() + row * rowH, colW, rowH - 3);
            pLabels[(size_t) k].setBounds (c.removeFromLeft (88));
            (sky::patchSpecs()[(size_t) k].integer ? (juce::Component&) pCombos[(size_t) k] : (juce::Component&) pSliders[(size_t) k]).setBounds (c);
        }
    };

    /* Drums & FX */
    hint (kitHint, "Eight synthesized drums. Random kit builds a new one. Lanes trigger drum loops (Lanes tab); every hit lands on the 16th-note grid and is sent as MIDI on channel 10.");
    rndKit.onClick = [this] { proc.randomKit(); };
    addTo (drumsPage, { &kitHint, &rndKit });
    mainBeatLbl.setText ("Main beat", juce::dontSendNotification);
    { auto names = sky::PresetLibrary::get().patternNames(); names.set (0, "None"); mainBeatBox.addItemList (names, 1); }
    comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "mainbeat", mainBeatBox));
    mainBeatBox.setTooltip ("Plays all the time, under any loops the lanes trigger");
    rndBeat.onClick = [this] { mainBeatBox.setSelectedItemIndex (1 + juce::Random::getSystemRandom().nextInt (juce::jmax (1, mainBeatBox.getNumItems() - 1))); };
    addTo (drumsPage, { &mainBeatLbl, &mainBeatBox, &rndBeat });
    for (int k = 0; k < 24; ++k)
    {
        const auto& s = sky::kitSpecs()[(size_t) k];
        kLabels[(size_t) k].setText (s.label, juce::dontSendNotification); kLabels[(size_t) k].setFont (juce::FontOptions (12.0f)); kLabels[(size_t) k].setColour (juce::Label::textColourId, dim);
        kSliders[(size_t) k].setSliderStyle (juce::Slider::LinearHorizontal); kSliders[(size_t) k].setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
        kSliders[(size_t) k].setNumDecimalPlacesToDisplay (s.hi >= 100 ? 0 : 2);
        sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, juce::String ("kit_") + s.id, kSliders[(size_t) k]));
        kSliders[(size_t) k].textFromValueFunction = [dec = s.hi >= 100 ? 0 : 2] (double v) { return juce::String (v, dec); };
        kSliders[(size_t) k].updateText();
        addTo (drumsPage, { &kLabels[(size_t) k], &kSliders[(size_t) k] });
    }
    hint (fxHint, "Delay unit, locked to the tempo. Edge mode is the U2 guitar-rig echo: a driven, gently modulated dotted eighth on one side and a quarter-note tap on the other. Each lane's Dly knob sends into it.");
    dmodeLbl.setText ("Delay", juce::dontSendNotification); dmixLbl.setText ("Level", juce::dontSendNotification); dfbLbl.setText ("Feedback", juce::dontSendNotification); dtoneLbl.setText ("Tone", juce::dontSendNotification);
    dmodeBox.addItemList ({ "Simple (dotted eighth)", "Edge (U2 style)" }, 1);
    comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "dmode", dmodeBox));
    for (auto [s, id] : { std::pair { &dmix, "dmix" }, std::pair { &dfb, "dfb" }, std::pair { &dtone, "dtone" } })
    { s->setSliderStyle (juce::Slider::LinearHorizontal); s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18); sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, *s));
      s->textFromValueFunction = [] (double v) { return v >= 100 ? juce::String ((int) v) + " Hz" : juce::String (v, 2); }; s->updateText(); }
    addTo (drumsPage, { &fxHint, &dmodeLbl, &dmodeBox, &dmixLbl, &dmix, &dfbLbl, &dfb, &dtoneLbl, &dtone });
    drumsPage.layout = [this] (juce::Rectangle<int> r) {
        kitHint.setBounds (r.removeFromTop (34));
        { auto x = r.removeFromTop (26); rndKit.setBounds (x.removeFromLeft (110)); x.removeFromLeft (14); mainBeatLbl.setBounds (x.removeFromLeft (70));
          rndBeat.setBounds (x.removeFromRight (70)); x.removeFromRight (6); mainBeatBox.setBounds (x); }
        r.removeFromTop (6);
        const int colW = (r.getWidth() - 12) / 2, rowH = 21;
        for (int k = 0; k < 24; ++k)
        {
            juce::Rectangle<int> c (r.getX() + (k % 2) * (colW + 12), r.getY() + (k / 2) * rowH, colW, rowH - 3);
            kLabels[(size_t) k].setBounds (c.removeFromLeft (100)); kSliders[(size_t) k].setBounds (c);
        }
        r.removeFromTop (12 * rowH + 6);
        fxHint.setBounds (r.removeFromTop (34));
        { auto x = r.removeFromTop (26); dmodeLbl.setBounds (x.removeFromLeft (70)); dmodeBox.setBounds (x.removeFromLeft (240)); }
        r.removeFromTop (4);
        for (auto [s, l] : { std::pair { &dmix, &dmixLbl }, std::pair { &dfb, &dfbLbl }, std::pair { &dtone, &dtoneLbl } })
        { auto x = r.removeFromTop (24); l->setBounds (x.removeFromLeft (70)); s->setBounds (x); }
    };

    /* Mixer */
    {
        const char* ids[7] = { "arp", "rise", "fall", "drone", "grid", "hit", "drm" };
        static const char* knobNames[5] = { "Pan", "Rev", "Dly", "Gated", "Echo" };
        for (int c = 0; c < 7; ++c)
        {
            auto& st = strips[(size_t) c];
            const juce::String id (ids[c]);
            const auto col = c < 6 ? juce::Colour (laneInfo[c].colour) : juce::Colour (0xffe8e8e8);
            st.name.setText (c < 6 ? juce::String (laneInfo[c].name) : juce::String ("Drums"), juce::dontSendNotification);
            st.name.setFont (juce::FontOptions (11.0f, juce::Font::bold)); st.name.setColour (juce::Label::textColourId, col); st.name.setJustificationType (juce::Justification::centred);
            st.meter.colour = col;
            st.fader.setSliderStyle (juce::Slider::LinearVertical); st.fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16); st.fader.setColour (juce::Slider::thumbColourId, col);
            st.att.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id + "_vol", st.fader));
            st.fader.textFromValueFunction = [] (double v) { return v > 0.0001 ? juce::String (20 * std::log10 (v), 1) + " dB" : juce::String ("-inf"); };
            st.fader.updateText();
            int k = 0;
            for (auto [s, suffix] : { std::pair { &st.pan, "_pan" }, std::pair { &st.rev, "_rev" }, std::pair { &st.dly, "_dly" }, std::pair { &st.gtd, "_gtd" }, std::pair { &st.echo, "_echo" } })
            {
                s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
                s->setColour (juce::Slider::rotarySliderFillColourId, col); s->setColour (juce::Slider::thumbColourId, col);
                st.att.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id + suffix, *s));
                st.knobLabels[(size_t) k].setText (knobNames[k], juce::dontSendNotification); st.knobLabels[(size_t) k].setFont (juce::FontOptions (10.0f));
                st.knobLabels[(size_t) k].setColour (juce::Label::textColourId, dim); st.knobLabels[(size_t) k].setJustificationType (juce::Justification::centred);
                s->setPopupDisplayEnabled (true, true, nullptr);
                mixerPage.addAndMakeVisible (st.knobLabels[(size_t) k++]); mixerPage.addAndMakeVisible (s);
            }
            st.pan.setDoubleClickReturnValue (true, 0);
            for (auto* b : { &st.mute, &st.solo }) { b->setClickingTogglesState (true); mixerPage.addAndMakeVisible (b); }
            st.mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff8a3d)); st.solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffd23f));
            st.aMute = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, id + "_mute", st.mute);
            st.aSolo = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, id + "_solo", st.solo);
            addTo (mixerPage, { &st.name, &st.meter, &st.fader });
        }
        const char* mids[5] = { "master", "gtime", "gmix", "erate", "emix" };
        const char* mnames[5] = { "Master", "Gate time", "Gated level", "Echo rate", "Echo level" };
        for (int k = 0; k < 5; ++k)
        {
            masterLbls[(size_t) k].setText (mnames[k], juce::dontSendNotification); masterLbls[(size_t) k].setFont (juce::FontOptions (12.0f));
            masterSliders[(size_t) k].setSliderStyle (juce::Slider::LinearHorizontal); masterSliders[(size_t) k].setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 18);
            sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, mids[k], masterSliders[(size_t) k]));
            addTo (mixerPage, { &masterLbls[(size_t) k], &masterSliders[(size_t) k] });
        }
        masterSliders[1].textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 1000)) + " ms"; }; masterSliders[1].updateText();
        masterSliders[1].setTooltip ("Gated reverb: the big 80s drum room that stops dead after this time");
        masterSliders[3].setTooltip ("Random echoes: how often notes get thrown into a tempo echo (each channel's Echo knob scales it)");
    }
    mixerPage.layout = [this] (juce::Rectangle<int> r) {
        auto masterArea = r.removeFromBottom (5 * 22);
        r.removeFromBottom (6);
        const int w = r.getWidth() / 7;
        for (auto& st : strips)
        {
            auto c = r.removeFromLeft (w).reduced (2, 0);
            st.name.setBounds (c.removeFromTop (16));
            { auto ms = c.removeFromTop (20); st.mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (1, 0)); st.solo.setBounds (ms.reduced (1, 0)); }
            c.removeFromTop (4);
            const int knob = juce::jmin (44, c.getWidth() - 2), knobs = 5 * (knob + 10);
            auto faderArea = c.removeFromTop (juce::jmax (90, c.getHeight() - knobs));
            st.meter.setBounds (faderArea.removeFromLeft (6).withTrimmedBottom (18));
            st.fader.setBounds (faderArea);
            const std::array<juce::Slider*, 5> ks { &st.pan, &st.rev, &st.dly, &st.gtd, &st.echo };
            for (int k = 0; k < 5; ++k)
            {
                auto kc = c.removeFromTop (knob + 10);
                st.knobLabels[(size_t) k].setBounds (kc.removeFromBottom (11));
                ks[(size_t) k]->setBounds (kc.withSizeKeepingCentre (knob, knob));
            }
        }
        for (int k = 0; k < 5; ++k) { auto x = masterArea.removeFromTop (22); masterLbls[(size_t) k].setBounds (x.removeFromLeft (90)); masterSliders[(size_t) k].setBounds (x); }
    };

    /* Music */
    keyLbl.setText ("Key", juce::dontSendNotification); scaleLbl.setText ("Scale", juce::dontSendNotification);
    modeLbl.setText ("Play", juce::dontSendNotification); bpmLbl.setText ("Own tempo", juce::dontSendNotification); masterLbl.setText ("Master", juce::dontSendNotification);
    keyBox.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    scaleBox.addItemList (scaleNames(), 1);
    modeBox.addItemList ({ "With the DAW transport", "Always (own tempo when the DAW is stopped)" }, 1);
    comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "key", keyBox));
    comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "scale", scaleBox));
    comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "mode", modeBox));
    for (auto* s : { &bpm, &master }) { s->setSliderStyle (juce::Slider::LinearHorizontal); s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20); }
    sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "bpm", bpm));
    sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "master", master));
    hint (about, "In a DAW it locks to the project tempo and plays while the transport runs. Each 16th note it looks at what the planes are doing: "
                 "circling planes play arpeggios (turn rate = speed, direction = up or down, orbit size = range), climbs and descents play lines pitched by altitude, "
                 "cruising traffic holds pad chords, low and close planes and new arrivals play percussion, and the heat grid plays column by column, west to east. "
                 "An emergency squawk (7500/7600/7700) shifts the key up a fourth.\n\n"
                 "MIDI comes out on each lane's channel. Reaper, Bitwig, Cubase, FL Studio and Logic (as an instrument) can record or route it; in Ableton, use the built-in sounds, "
                 "or run the standalone app and send its MIDI to Live through a virtual port.\n\n"
                 "Flight data: adsb.lol, airplanes.live, adsb.fi, OpenSky Network (volunteer ADS-B receivers). Place search: OpenStreetMap Nominatim.");
    addTo (music, { &keyLbl, &keyBox, &scaleLbl, &scaleBox, &modeLbl, &modeBox, &bpmLbl, &bpm, &masterLbl, &master, &about });
    music.layout = [this] (juce::Rectangle<int> r) {
        auto row = [&r] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };
        { auto x = row (28); keyLbl.setBounds (x.removeFromLeft (80)); keyBox.setBounds (x.removeFromLeft (70)); x.removeFromLeft (14); scaleLbl.setBounds (x.removeFromLeft (50)); scaleBox.setBounds (x.removeFromLeft (170)); }
        { auto x = row (28); modeLbl.setBounds (x.removeFromLeft (80)); modeBox.setBounds (x); }
        { auto x = row (28); bpmLbl.setBounds (x.removeFromLeft (80)); bpm.setBounds (x); }
        { auto x = row (28); masterLbl.setBounds (x.removeFromLeft (80)); master.setBounds (x); }
        about.setBounds (r);
    };

    refreshWhere();
    setResizable (true, true);
    setResizeLimits (860, 600, 2000, 1400);
    setSize (1060, 680);
    startTimerHz (15);
}

SkyEditor::~SkyEditor() { setLookAndFeel (nullptr); }

void SkyEditor::bindPatch (int lane)
{
    if (lane < 0) return;
    patchSliderAtt.clear(); patchComboAtt.clear();
    for (int k = 0; k < 21; ++k)
    {
        const auto id = proc.patchParamId (lane, k);
        if (sky::patchSpecs()[(size_t) k].integer) patchComboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, id, pCombos[(size_t) k]));
        else
        {
            auto& sl = pSliders[(size_t) k];
            patchSliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, sl));
            const int dec = sl.getNumDecimalPlacesToDisplay();
            sl.textFromValueFunction = [dec] (double v) { return juce::String (v, dec); };   // the attachment's own text has too many digits
            sl.updateText();
        }
        pSliders[(size_t) k].setColour (juce::Slider::thumbColourId, juce::Colour (laneInfo[lane].colour));
    }
}

void SkyEditor::refreshWhere()
{
    const auto f = proc.feedSettings();
    sourceBox.setSelectedItemIndex (f.source, juce::dontSendNotification);
    radius.setValue (f.radius, juce::dontSendNotification);
    if (! latEd.hasKeyboardFocus (true)) latEd.setText (juce::String (f.lat, 4), false);
    if (! lonEd.hasKeyboardFocus (true)) lonEd.setText (juce::String (f.lon, 4), false);
    if (! urlEd.hasKeyboardFocus (true)) urlEd.setText (f.localUrl, false);
}

void SkyEditor::showMessage (const juce::String& m, bool isBad)
{
    msg.setText (m, juce::dontSendNotification);
    msg.setColour (juce::Label::textColourId, isBad ? bad : acc);
}

void SkyEditor::runSearch()
{
    const auto q = search.getText().trim();
    if (q.isEmpty()) { showMessage ("Type a place name, an address or coordinates", true); return; }
    double la, lo;
    if (sky::parseCoordinates (q, la, lo)) { proc.setLocation (la, lo, {}); refreshWhere(); showMessage ("Centred on " + juce::String (la, 4) + ", " + juce::String (lo, 4)); return; }
    showMessage ("Searching..."); go.setEnabled (false);
    proc.lookups.addJob ([q, sp = juce::Component::SafePointer<SkyEditor> (this)] {
        juce::String err;
        auto res = sky::searchPlaces (q, err);
        juce::MessageManager::callAsync ([sp, res, err] {
            if (sp == nullptr) return;
            sp->go.setEnabled (true);
            sp->found = res; sp->results.clear (juce::dontSendNotification);
            if (res.empty()) { sp->showMessage (err, true); return; }
            int id = 1; for (auto& p : res) sp->results.addItem (p.name, id++);
            sp->results.setSelectedItemIndex (0, juce::sendNotificationSync);   // go to the best match; the list has the others
            sp->showMessage (res.size() > 1 ? juce::String ((int) res.size()) + " places found. Showing the first; pick another from the list." : juce::String ("Found it"));
        });
    });
}

void SkyEditor::locateMe()
{
    showMessage ("Finding you..."); locate.setEnabled (false);
    proc.lookups.addJob ([sp = juce::Component::SafePointer<SkyEditor> (this)] {
        sky::Place pl; juce::String err;
        const bool ok = sky::locateByIp (pl, err);
        juce::MessageManager::callAsync ([sp, ok, pl, err] {
            if (sp == nullptr) return;
            sp->locate.setEnabled (true);
            if (! ok) { sp->showMessage (err, true); return; }
            sp->proc.setLocation (pl.lat, pl.lon, pl.name);
            sp->refreshWhere();
            sp->showMessage ("Centred on " + pl.name + " (approximate). For an exact spot, type coordinates.");
        });
    });
}

void SkyEditor::timerCallback()
{
    // meters every tick, the rest twice a second
    for (int c = 0; c < 7; ++c)
    {
        auto& m = strips[(size_t) c].meter;
        m.level = juce::jmax (proc.takePeak (c < 6 ? c : 7), m.level * 0.8f);
        if (tabs.getCurrentContentComponent() == &mixerPage) m.repaint();
    }
    if (++tick % 8 != 0) return;
    juce::String st; bool ok;
    juce::int64 last;
    { const juce::ScopedLock sl (proc.model.lock); st = proc.model.status; ok = proc.model.statusOk; last = proc.model.lastUpdate; }
    if (last) st << "  -  updated " << (int) ((juce::Time::currentTimeMillis() - last) / 1000) << "s ago";
    st << "  -  " << (proc.running ? juce::String ((int) std::round (proc.currentBpm.load())) + " BPM" : juce::String ("press play in your DAW"));
    status.setText (st, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, ok ? acc : bad);
    presetNow.setText ("Now: " + proc.presetName(), juce::dontSendNotification);
    placeLbl.setText (proc.placeName().isNotEmpty() ? proc.placeName() : juce::String ("Finding your location..."), juce::dontSendNotification);
    if (! latEd.hasKeyboardFocus (true) && ! lonEd.hasKeyboardFocus (true)) refreshWhere();
}

void SkyEditor::paint (juce::Graphics& g) { g.fillAll (bg); }

void SkyEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (26);
    title.setBounds (top.removeFromLeft (200)); placeLbl.setBounds (top);
    r.removeFromTop (6);
    const int side = juce::jmin (r.getHeight() - 26, r.getWidth() - 400);
    auto left = r.removeFromLeft (side);
    radar.setBounds (left.removeFromTop (side));
    status.setBounds (left.removeFromTop (24));
    r.removeFromLeft (10);
    tabs.setBounds (r);
}
