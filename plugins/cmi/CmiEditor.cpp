#include "CmiEditor.h"

namespace cmi {

static const juce::Colour kInk(0xff4dffa8), kDim(0xff1f8a5a), kFaint(0xff0f3d28), kBg(0xff020a06), kCream(0xffd9d3bc), kCreamDark(0xffb9b29a);
static juce::Font mono(float h, bool bold = true) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }

static juce::String noteName(int n) { static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(nm[((n % 12) + 12) % 12]) + juce::String(n / 12 - 1); }

// ---------------------------------------------------------------------------------------------------------------
Screen::Screen(CmiEditor& e) : ed(e) { setSize(W, H); setWantsKeyboardFocus(true); }

Screen::Hit& Screen::hit(juce::Rectangle<float> r, std::function<void(const juce::MouseEvent&)> down, std::function<void(const juce::MouseEvent&)> drag) {
    hits.push_back({ r.translated(0, hitDy), std::move(down), std::move(drag), {} });
    return hits.back();
}

void Screen::mouseDown(const juce::MouseEvent& e) {
    active = -1;
    for (int i = (int) hits.size() - 1; i >= 0; --i)
        if (hits[(size_t) i].r.contains(e.position)) { active = i; if (hits[(size_t) i].down) hits[(size_t) i].down(e); break; }
    repaint();
}
void Screen::mouseDrag(const juce::MouseEvent& e) { if (active >= 0 && active < (int) hits.size() && hits[(size_t) active].drag) { hits[(size_t) active].drag(e); repaint(); } }
void Screen::mouseUp(const juce::MouseEvent& e) { if (active >= 0 && active < (int) hits.size() && hits[(size_t) active].up) hits[(size_t) active].up(e); active = -1; repaint(); }
void Screen::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    for (int i = (int) hits.size() - 1; i >= 0; --i)
        if (hits[(size_t) i].r.contains(e.position) && hits[(size_t) i].wheel) { wheelMods = e.mods; hits[(size_t) i].wheel(w.deltaY); repaint(); return; }
}

void Screen::button(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool inverse, std::function<void()> fn) {
    if (inverse) { g.setColour(kInk); g.fillRect(r); g.setColour(kBg); } else g.setColour(kInk);
    g.setFont(mono(17)); g.drawText(text, r, juce::Justification::centred);
    hit(r, [fn](const juce::MouseEvent&) { if (fn) fn(); });
}

void Screen::paint(juce::Graphics& g) {
    hits.clear();
    g.fillAll(kBg);
    drawHeader(g);
    switch (page) {
        case '2': drawLibrary(g); break;
        case '7': drawControls(g); break;
        case 'D': drawWave(g); break;
        case '8': drawSampling(g); break;
        case 'M': drawMixer(g); break;
        case 'F': drawFiles(g); break;
        case '5': drawPen(g); break;
        case 'R': drawPresets(g); break;
        case 'E': drawEffects(g); break;
        case 'K': drawKit(g); break;
        case 'P': drawPads(g); break;
        case 'W': drawEditor(g); break;
        default: drawComposer(g); break;
    }
    // page index along the bottom
    const char* pages[] = { "2", "7", "D", "5", "8", "P", "F", "M", "E", "S" };
    const char* names[] = { "VOICES", "CONTROL", "WAVE", "DRAW", "SAMPLE", "PADS", "FILES", "MIXER", "EFFECTS", "COMPOSER" };
    float x = 12;
    for (int i = 0; i < 10; ++i) {
        const char p = pages[i][0];
        juce::Rectangle<float> r(x, (float) H - 26, 95, 22);
        if (page == p) { g.setColour(kInk); g.fillRect(r); g.setColour(kBg); } else g.setColour(kInk);
        g.setFont(mono(15)); g.drawText(juce::String(pages[i]) + " " + names[i], r, juce::Justification::centred);
        hit(r, [this, p](const juce::MouseEvent&) { page = p; status = "READY"; repaint(); });
        x += 97.6f;
    }
    // CRT: scanlines and a soft vignette
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    for (int y = 0; y < H; y += 3) g.fillRect(0, y, W, 1);
    juce::ColourGradient vg(juce::Colours::transparentBlack, W / 2.f, H / 2.f, juce::Colours::black.withAlpha(0.45f), 0, 0, true);
    g.setGradientFill(vg); g.fillRect(getLocalBounds());
}

void Screen::drawHeader(juce::Graphics& g) {
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("INDEX", 12, 4, 100, 20, juce::Justification::centredLeft);
    g.drawText("COMMAND:", 12, 28, 100, 20, juce::Justification::centredLeft);
    g.fillRect(110.f, 4.f, (float) W - 122, 20.f);
    g.fillRect(110.f, 28.f, (float) W - 122, 20.f);
    g.setColour(kBg);
    g.drawText(juce::String("***  PAGE ") + page + "  " + status + "  ***", 110, 4, W - 122, 20, juce::Justification::centred);
    hit({ 12, 4, 100, 20 }, [this](const juce::MouseEvent&) { page = '2'; repaint(); });
}

// ---- Page 2: the disk directory: the voices in your library folder, or the ones loaded ----
void Screen::drawLibrary(juce::Graphics& g) {
    auto& kit = ed.proc.kit;
    const int n = kit.bank.count.load(), cur = juce::roundToInt(ed.proc.get("sound"));
    const bool lib = ed.proc.showCatalog.load();
    struct Row { int sound = -1, cat = -1; juce::String name, where; bool wf = false, loop = false; };
    std::vector<Row> list;
    const Sound* cs = isBlank(kit, cur) ? nullptr : kit.bank.at(cur);
    if (lib) {
        const juce::ScopedLock l(ed.proc.catLock);
        const auto& cv = ed.proc.catalog.voices;
        for (size_t i = 0; i < cv.size(); ++i) { Row r; r.cat = (int) i; r.name = cv[i].name; r.where = cv[i].disk; r.loop = cv[i].looped; list.push_back(r); }
    } else
        for (int i = 0; i < n; ++i)
            if (!isBlank(kit, i)) if (const Sound* s = kit.bank.at(i)) { Row r; r.sound = i; r.name = s->name; r.where = s->category; r.wf = s->mode == Mode::Waveform; list.push_back(r); }
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("DISK DIRECTORY:  " + juce::String((int) list.size()) + " VOICES    CLICK TO LOAD THE KEYBOARD", 20, 58, 640, 20, juce::Justification::centredLeft);
    button(g, { 680, 56, 140, 22 }, "LIBRARY", lib, [this] { ed.proc.showCatalog = true; libScroll = 0; });
    button(g, { 830, 56, 140, 22 }, "LOADED", !lib, [this] { ed.proc.showCatalog = false; libScroll = 0; });
    g.drawHorizontalLine(82, 20, (float) W - 20);
    const int rows = 22, colW = 320, per = rows * 3;
    const int pages = std::max(1, ((int) list.size() + per - 1) / per);
    libScroll = juce::jlimit(0, pages - 1, libScroll);
    if (list.empty()) {
        g.setColour(kDim);
        g.drawText(lib ? "No CMI library yet: set your library folder on Page F (your disk images and .VC files)." : "No voices loaded yet: pick one from LIBRARY, or import sounds on Page 8.",
                   20, 120, W - 40, 20, juce::Justification::centredLeft);
    }
    for (int k = 0; k < per && libScroll * per + k < (int) list.size(); ++k) {
        const Row row = list[(size_t) (libScroll * per + k)];
        const int c = k / rows, r = k % rows;
        juce::Rectangle<float> rr(20.f + c * colW, 88.f + r * 20.f, (float) colW - 12, 19);
        const bool on = row.sound >= 0 ? row.sound == cur : (cs && cs->name == row.name.toStdString() && cs->category == row.where.substring(0, 10).toStdString());
        if (on) { g.setColour(kInk); g.fillRect(rr); g.setColour(kBg); } else g.setColour(kInk);
        g.setFont(mono(16));
        g.drawText(juce::String(libScroll * per + k + 1).paddedLeft('0', 3) + " " + row.name.paddedRight(' ', 9) + (row.wf ? ".WF " : ".VC ") + row.where.substring(0, 10),
                   rr.reduced(4, 0), juce::Justification::centredLeft);
        hit(rr, [this, row](const juce::MouseEvent&) {
            const int v = row.sound >= 0 ? row.sound : ed.proc.loadCatalogVoice(row.cat);
            if (v >= 0) { ed.proc.loadSound(v); status = "LOADED"; } else status = "USER VOICES FULL";
        });
    }
    if (pages > 1) {
        button(g, { (float) W - 160, (float) H - 104, 40, 20 }, "<", false, [this] { libScroll = juce::jmax(0, libScroll - 1); });
        g.setColour(kInk); g.setFont(mono(15));
        g.drawText(juce::String(libScroll + 1) + "/" + juce::String(pages), W - 116, H - 104, 52, 20, juce::Justification::centred);
        button(g, { (float) W - 60, (float) H - 104, 40, 20 }, ">", false, [this, pages] { libScroll = juce::jmin(pages - 1, libScroll + 1); });
    }
    if (const Sound* s = cs) {
        g.setColour(kDim); g.drawHorizontalLine(H - 82, 20, (float) W - 20);
        g.setColour(kInk); g.setFont(mono(16));
        g.drawText(juce::String(s->name) + ": " + juce::String(s->title), 20, H - 78, W - 40, 20, juce::Justification::centredLeft);
        g.setColour(kDim);
        g.drawText("rate " + juce::String(juce::roundToInt(s->rate)) + " Hz   root " + noteName(s->root) + "   " + juce::String(s->pages()) + " pages (" + juce::String(s->ram.size()) +
                   " bytes)   loop " + (s->loopStart >= 0 ? juce::String(s->loopStart) + "-" + juce::String(s->loopEnd) : juce::String("none")), 20, H - 58, W - 40, 18,
                   juce::Justification::centredLeft);
    }
}

// ---- Page R: the factory presets. The IIx factory library by disk; each one found in your library folder ----
void Screen::drawPresets(juce::Graphics& g) {
    const auto& ps = factoryPresets();
    std::vector<int> pc; bool haveLib;
    { const juce::ScopedLock l(ed.proc.catLock); pc = ed.proc.presetCat; haveLib = !ed.proc.catalog.voices.empty(); }
    pc.resize(ps.size(), -1);
    int found = 0; for (int c : pc) found += c >= 0;
    const int cur = ed.proc.curPreset.load();
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("FACTORY PRESETS: " + juce::String((int) ps.size()) + "   IN YOUR LIBRARY: " + juce::String(found) + "   MISSING: " + juce::String((int) ps.size() - found),
               20, 58, 700, 20, juce::Justification::centredLeft);
    button(g, { 830, 56, 140, 22 }, "DISK...", false, [this] {
        juce::PopupMenu m; const auto d = factoryDisks();
        for (int i = 0; i < (int) d.size(); ++i) m.addItem(i + 1, juce::String(d[(size_t) i].first) + "   (" + juce::String(d[(size_t) i].second + 1) + ")");
        m.showMenuAsync(juce::PopupMenu::Options(), [this, d](int r) { if (r > 0) { presetScroll = d[(size_t) r - 1].second / 66; repaint(); } });
    });
    g.drawHorizontalLine(82, 20, (float) W - 20);
    const int rows = 22, colW = 320, per = rows * 3;
    const int pages = std::max(1, ((int) ps.size() + per - 1) / per);
    presetScroll = juce::jlimit(0, pages - 1, presetScroll);
    auto& lh = hit({ 20, 86, (float) W - 40, rows * 20.f + 4 });
    lh.wheel = [this, pages](float dy) { presetScroll = juce::jlimit(0, pages - 1, presetScroll + (dy > 0 ? -1 : 1)); };
    for (int k = 0; k < per && presetScroll * per + k < (int) ps.size(); ++k) {
        const int n = presetScroll * per + k;
        const FactoryPreset& p = ps[(size_t) n];
        const int c = k / rows, r = k % rows;
        juce::Rectangle<float> rr(20.f + c * colW, 88.f + r * 20.f, (float) colW - 12, 19);
        const bool have = pc[(size_t) n] >= 0, mine = ed.proc.presetChoice.count(n) > 0;
        if (n == cur) { g.setColour(kInk); g.fillRect(rr); g.setColour(kBg); } else g.setColour(have ? kInk : kDim.withAlpha(0.6f));
        g.setFont(mono(16));
        g.drawText(juce::String(n + 1).paddedLeft('0', 3) + (mine ? "*" : " ") + juce::String(p.name).paddedRight(' ', 9) + juce::String(p.file) + (have ? "" : " --"),
                   rr.reduced(4, 0), juce::Justification::centredLeft);
        hit(rr, [this, n](const juce::MouseEvent& e) {
            if (e.mods.isPopupMenu()) { presetMenu(n); return; }
            status = ed.proc.loadPreset(n) ? "PRESET " + juce::String(n + 1) : juce::String(factoryPresets()[(size_t) n].name) + " NOT IN YOUR LIBRARY";
        });
    }
    if (pages > 1) {
        button(g, { (float) W - 160, (float) H - 104, 40, 20 }, "<", false, [this] { presetScroll = juce::jmax(0, presetScroll - 1); });
        g.setColour(kInk); g.setFont(mono(15));
        g.drawText(juce::String(presetScroll + 1) + "/" + juce::String(pages), W - 116, H - 104, 52, 20, juce::Justification::centred);
        button(g, { (float) W - 60, (float) H - 104, 40, 20 }, ">", false, [this, pages] { presetScroll = juce::jmin(pages - 1, presetScroll + 1); });
    }
    g.setColour(kDim); g.drawHorizontalLine(H - 82, 20, (float) W - 20);
    g.setFont(mono(15));
    if (!haveLib) {
        g.setColour(kInk);
        g.drawText("Set your library folder on Page F: each preset is found there by its file name and disk.", 20, H - 78, W - 40, 20, juce::Justification::centredLeft);
    } else if (cur >= 0 && cur < (int) ps.size()) {
        const FactoryPreset& p = ps[(size_t) cur];
        juce::String where;
        { const juce::ScopedLock l(ed.proc.catLock); const int ci = pc[(size_t) cur]; if (ci >= 0 && ci < (int) ed.proc.catalog.voices.size()) where = juce::String(ed.proc.catalog.voices[(size_t) ci].name) + " on " + ed.proc.catalog.voices[(size_t) ci].disk; }
        g.setColour(kInk);
        const juce::String note = factoryNote(p.name);
        g.drawText("PRESET " + juce::String(cur + 1) + ": " + p.name + " (" + p.category + ", disk " + p.file + ")" + (note.isNotEmpty() ? ", " + note : juce::String()), 20, H - 78, W - 40, 20, juce::Justification::centredLeft);
        g.setColour(kDim); g.drawText("loaded from your library: " + where, 20, H - 58, W - 40, 18, juce::Justification::centredLeft);
    } else {
        g.setColour(kDim);
        g.drawText("click: load onto the keyboard (MIDI program = preset number)   right-click: match it to another library voice   * = matched by you", 20, H - 78, W - 40, 20, juce::Justification::centredLeft);
    }
}

void Screen::presetMenu(int n) {
    juce::PopupMenu m;
    m.addSectionHeader(juce::String(n + 1) + " " + factoryPresets()[(size_t) n].name + " (" + factoryPresets()[(size_t) n].file + ")");
    m.addItem(1, "Automatic (its name on its disk)", true, ed.proc.presetChoice.count(n) == 0);
    {
        const juce::ScopedLock l(ed.proc.catLock);
        const auto& cv = ed.proc.catalog.voices;
        std::map<std::string, juce::PopupMenu> byDisk;
        for (size_t i = 0; i < cv.size() && i < 9000; ++i) byDisk[cv[i].disk].addItem(20000 + (int) i, juce::String(cv[i].name) + (cv[i].looped ? "  (loop)" : ""));
        juce::PopupMenu lib; for (auto& kv : byDisk) lib.addSubMenu(juce::String(kv.first), kv.second);
        if (cv.empty()) m.addItem(-1, "No library: set your library folder on Page F", false);
        else m.addSubMenu("Match to a library voice", lib);
    }
    m.showMenuAsync(juce::PopupMenu::Options(), [this, n](int r) {
        if (r == 1) ed.proc.choosePresetVoice(n, -1);
        else if (r >= 20000) ed.proc.choosePresetVoice(n, r - 20000);
        else return;
        ed.proc.loadPreset(n); repaint();
    });
}

// ---- Page 7: control parameters as bar graphs ----
void Screen::bar(juce::Graphics& g, juce::Rectangle<float> r, const char* id, const juce::String& label, std::function<juce::String(float)> fmt) {
    const PDef* d = nullptr; for (const auto& p : paramDefs()) if (!std::strcmp(p.id, id)) d = &p;
    if (!d) return;
    const float v = ed.proc.get(id), u = (v - d->min) / (d->max - d->min);
    const bool toggle = d->step == 1 && d->min == 0 && d->max == 1;
    const int pi = paramIndex(id), cc = ed.proc.ccFor(pi);
    const bool learning = ed.proc.learnParam.load() == pi;
    g.setColour(learning ? juce::Colours::white : kInk); g.setFont(mono(16));
    g.drawText(label, r.withWidth(170), juce::Justification::centredLeft);
    g.setColour(learning ? juce::Colours::white : kDim); g.setFont(mono(11));
    g.drawText(learning ? "LEARN" : cc >= 0 ? "CC" + juce::String(cc) : "", r.withX(r.getX() + 130).withWidth(44), juce::Justification::centredRight);
    g.setFont(mono(16)); g.setColour(kInk);
    hit(r.withWidth(170), [this, id](const juce::MouseEvent&) { learnMenu(id); });
    auto tr = r.withTrimmedLeft(175).withTrimmedRight(90).reduced(0, 3);
    if (toggle) {
        auto box = tr.withWidth(70);
        if (v > 0.5f) { g.fillRect(box); g.setColour(kBg); }
        else g.drawRect(box, 1.f);
        g.drawText(v > 0.5f ? "ON" : "OFF", box, juce::Justification::centred);
        hit(box, [this, id, v](const juce::MouseEvent&) { ed.proc.set(id, v > 0.5f ? 0.f : 1.f); });
        return;
    }
    g.setColour(kFaint); g.fillRect(tr);
    g.setColour(kInk); g.fillRect(tr.withWidth(tr.getWidth() * juce::jlimit(0.f, 1.f, u)));
    for (int k = 1; k < 8; ++k) { g.setColour(kBg.withAlpha(0.6f)); g.fillRect(tr.getX() + tr.getWidth() * k / 8.f, tr.getY(), 1.f, tr.getHeight()); }
    g.setColour(kInk);
    g.drawText(fmt ? fmt(v) : juce::String(v, 2), r.withTrimmedLeft(r.getWidth() - 86), juce::Justification::centredRight);
    auto setFromX = [this, tr, d, id](const juce::MouseEvent& e) {
        float nu = juce::jlimit(0.f, 1.f, (e.position.x - tr.getX()) / tr.getWidth());
        float nv = d->min + nu * (d->max - d->min);
        if (d->step == 1) nv = std::round(nv);
        ed.proc.set(id, nv);
    };
    hit(tr.expanded(0, 3), setFromX, setFromX).wheel = [this, id, d, v](float dy) {
        const float stepv = d->step == 1 ? 1.f : (d->max - d->min) / 64.f;
        ed.proc.set(id, juce::jlimit(d->min, d->max, v + (dy > 0 ? stepv : -stepv)));
    };
}

void Screen::drawControls(juce::Graphics& g) {
    const Sound* s = ed.proc.kit.bank.at(juce::roundToInt(ed.proc.get("sound")));
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("CONTROL PARAMETERS    VOICE: " + (s ? juce::String(s->name) : juce::String("--")) + "    KEYBOARD CHANNELS 1-8", 20, 58, W - 40, 20, juce::Justification::centredLeft);
    g.drawHorizontalLine(80, 20, (float) W - 20);
    auto f2 = [](float v) { return juce::String(v, 2); };
    auto fi = [](float v) { return juce::String(juce::roundToInt(v)); };
    auto fs = [](float v) { const double t = envTime(v); return t < 1 ? juce::String(juce::roundToInt(t * 1000)) + "ms" : juce::String(t, 1) + "s"; };
    struct Row { const char* id; const char* label; std::function<juce::String(float)> fmt; };
    const std::vector<Row> left = {
        { "level", "LEVEL", [](float v) { return juce::String(juce::roundToInt(v * 255)); } },
        { "filter", "FILTER", fi }, { "attack", "ATTACK", fs }, { "damp", "DAMPING", fs }, { "sustain", "SUSTAIN", {} }, { "loop", "LOOP", {} },
        { "vib", "VIB DEPTH", [](float v) { return juce::String(juce::roundToInt(v * 100)) + "c"; } }, { "vibRate", "VIB SPEED", [](float v) { return juce::String(v, 1) + "Hz"; } },
        { "vibDelay", "VIB DELAY", [](float v) { return juce::String(v, 2) + "s"; } }, { "porta", "PORTAMENTO", [](float v) { return juce::String(v, 2) + "s"; } },
        { "velVol", "VEL>LEVEL", f2 }, { "velFilt", "VEL>FILTER", f2 }, { "transpose", "TRANSPOSE", fi }, { "fine", "FINE TUNE", [](float v) { return juce::String(juce::roundToInt(v)) + "c"; } },
        { "seg", "SEGMENT", [](float v) { return juce::String(juce::roundToInt(v * 1000)) + "ms"; } },
        { "blendOn", "LOOP BLEND", {} },
        { "blend", "BLEND AMOUNT", [](float v) { return juce::String(juce::roundToInt(v * 100)) + "%"; } },
    };
    const std::vector<Row> right = {
        { "poly", "VOICES", fi }, { "bend", "BEND RANGE", fi }, { "master", "MASTER", f2 }, { "hiss", "NOISE FLOOR", f2 }, { "spread", "SPREAD", f2 },
        { "multi", "MULTI MIDI", {} }, { "sync", "SYNC EXT", {} }, { "tempo", "SPEED", [](float v) { return juce::String(v, 1); } },
        { "swing", "SWING", [](float v) { return juce::String(juce::roundToInt(v * 200)) + "%"; } }, { "songMode", "SONG MODE", {} },
        { "songLevel", "SONG LEVEL", [](float v) { return juce::String(juce::roundToInt(v * 100)) + "%"; } },
    };
    for (size_t i = 0; i < left.size(); ++i) bar(g, { 20.f, 88.f + i * 30.f, 470, 26 }, left[i].id, left[i].label, left[i].fmt);
    for (size_t i = 0; i < right.size(); ++i) bar(g, { 510.f, 88.f + i * 30.f, 470, 26 }, right[i].id, right[i].label, right[i].fmt);
    g.setColour(kDim); g.setFont(mono(14));
    g.drawMultiLineText("Filter: the SSM2045 latch; the card adds the octave\nregister, so it follows the keyboard octave by octave.\n"
                        "Attack / damping: the envelope counter's clock.\nSegment: waveform voices step one segment per time.\n"
                        "Multi MIDI: channels 2-9 play Page S tracks 1-8.\nClick a name: MIDI learn (move a controller).", 520, 442, 460);
    button(g, { 520, 540, 220, 24 }, "RESET TO VOICE", false, [this] { ed.proc.loadSound(juce::roundToInt(ed.proc.get("sound"))); });
}

// ---- Page D: the waveform, page by page, in 3D ----
void Screen::drawWave(juce::Graphics& g) {
    const Sound* s = ed.proc.kit.bank.at(juce::roundToInt(ed.proc.get("sound")));
    if (!s) return;
    const int np = s->pages();
    if (s != waveSound) { waveSound = s; wavePage = s->loopStart >= 0 ? s->loopStart : np / 4; }
    wavePage = juce::jlimit(0, np - 1, wavePage);
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("WAVEFORM DISPLAY   VOICE: " + juce::String(s->name) + (s->mode == Mode::Waveform ? "   (128 SEGMENTS)" : "   (" + juce::String(np) + " PAGES OF 128)"), 20, 58, W - 40, 20,
               juce::Justification::centredLeft);
    // waterfall: back pages first, each one hides what's behind it
    const float x0 = 30, y0 = 560, w = 520, h = 70, dx = 220, dy = 380;
    const int stepP = np > 64 ? 2 : 1;
    for (int p = np - 1; p >= 0; p -= stepP) {
        const float u = np > 1 ? (float) p / (np - 1) : 0;
        const float bx = x0 + dx * u, by = y0 - dy * u;
        juce::Path line, fill;
        for (int i = 0; i < PAGE; ++i) {
            const float v = (s->ram[(size_t) (p * PAGE + i)] - 128) / 128.f;
            const float x = bx + w * i / (PAGE - 1.f), y = by - v * h * 0.5f;
            if (i == 0) { line.startNewSubPath(x, y); fill.startNewSubPath(x, by + h * 0.5f); }
            line.lineTo(x, y); fill.lineTo(x, y);
        }
        fill.lineTo(bx + w, by + h * 0.5f); fill.closeSubPath();
        g.setColour(kBg); g.fillPath(fill);
        const bool sel = std::abs(p - wavePage) < stepP;
        const bool inLoop = s->loopStart >= 0 && p >= s->loopStart && p <= s->loopEnd;
        g.setColour(sel ? juce::Colours::white : inLoop ? kInk : kDim);
        g.strokePath(line, juce::PathStrokeType(sel ? 1.6f : 1.0f));
    }
    hit({ x0, y0 - dy - h, w + dx, dy + h * 1.5f }, [this, np, y0, dy](const juce::MouseEvent& e) { wavePage = juce::jlimit(0, np - 1, juce::roundToInt((y0 - e.position.y) / dy * (np - 1))); },
        [this, np, y0, dy](const juce::MouseEvent& e) { wavePage = juce::jlimit(0, np - 1, juce::roundToInt((y0 - e.position.y) / dy * (np - 1))); });
    // the selected page, flat
    juce::Rectangle<float> pr(790, 100, 190, 120);
    g.setColour(kDim); g.drawRect(pr, 1.f);
    juce::Path pl;
    for (int i = 0; i < PAGE; ++i) {
        const float v = (s->ram[(size_t) (wavePage * PAGE + i)] - 128) / 128.f;
        const float x = pr.getX() + pr.getWidth() * i / (PAGE - 1.f), y = pr.getCentreY() - v * pr.getHeight() * 0.48f;
        if (i == 0) pl.startNewSubPath(x, y); else pl.lineTo(x, y);
    }
    g.setColour(kInk); g.strokePath(pl, juce::PathStrokeType(1.2f));
    g.setFont(mono(15));
    juce::StringArray info { "PAGE " + juce::String(wavePage + 1) + " / " + juce::String(np), "RATE  " + juce::String(juce::roundToInt(s->rate)) + " Hz", "ROOT  " + noteName(s->root),
                             "LOOP  " + (s->loopStart >= 0 ? juce::String(s->loopStart + 1) + "-" + juce::String(s->loopEnd + 1) : juce::String("NONE")),
                             "MODE  " + juce::String(s->mode == Mode::Waveform ? "WAVEFORM" : "SAMPLE"), "8 BIT  " + juce::String(s->ram.size()) + " BYTES" };
    for (int i = 0; i < info.size(); ++i) g.drawText(info[i], 790, 228 + i * 20, 200, 18, juce::Justification::centredLeft);
    // the sixteen channel cards
    g.drawText("CHANNEL CARDS", 790, 360, 200, 18, juce::Justification::centredLeft);
    for (int c = 0; c < 16; ++c) {
        const auto v = ed.proc.engine.view(c);
        juce::Rectangle<float> r(790.f + (c % 8) * 24.f, 384.f + (c / 8) * 92.f, 18, 80);
        g.setColour(kFaint); g.fillRect(r);
        if (v.state != 0) { g.setColour(c < KEY_CH ? kInk : kDim); const float lv = v.env / 255.f; g.fillRect(r.withTrimmedTop(r.getHeight() * (1 - lv))); }
    }
    g.setColour(kDim); g.setFont(mono(13));
    g.drawText("KEYBOARD 1-8", 790, 466, 200, 14, juce::Justification::centredLeft);
    g.drawText("PAGE S TRACKS 1-8", 790, 558, 200, 14, juce::Justification::centredLeft);
}

// ---- Page 8: sampling into the user voices ----
// ---- Page M: the mixer ----
// the mixer's strips: the keyboard, then the song's tracks (or Page S's eight)
std::vector<std::pair<int, juce::String>> Screen::mixStrips() {
    auto& eng = ed.proc.engine;
    const MidiSong* s = eng.song.load();
    std::vector<std::pair<int, juce::String>> strips { { 0, "KEYS" } };
    if (s) {   // the track's number and its voice (track names are often just "TRACK 3")
        for (int t = 0; t < SONG_TRK; ++t) {
            if (!s->used[t]) continue;
            const int v = eng.songVoice[t]; const Sound* snd = v == GM_KIT ? nullptr : ed.proc.kit.bank.at(v);
            strips.push_back({ 1 + TRACKS + t, juce::String(t + 1) + " " + (v == GM_KIT ? juce::String("KIT") : snd && !isBlank(ed.proc.kit, v) ? juce::String(snd->name) : juce::String(s->trackName[t]).trim()) });
        }
    }
    else for (int t = 0; t < TRACKS; ++t) { const Sound* snd = ed.proc.kit.bank.at(eng.seq.trk[t].sound); strips.push_back({ 1 + t, snd && !isBlank(ed.proc.kit, eng.seq.trk[t].sound) ? juce::String(snd->name) : juce::String(t + 1) }); }
    return strips;
}

// one vertical fader with its meter (drag, wheel for fine)
// a fader strip: its frame, a dB scale, a segmented LED meter (48 dB, the top segments white), the cap with grip lines
static void ledMeter(juce::Graphics& g, juce::Rectangle<float> r, float level) {
    constexpr int segs = 26;
    const float db = level > 1e-6f ? 20 * std::log10(level) : -100.f, sh = r.getHeight() / segs;
    for (int k = 0; k < segs; ++k) {
        const float th = -48.f + 51.f * k / (segs - 1);   // -48 .. +3 dB
        const bool lit = db >= th;
        g.setColour(!lit ? kFaint.withAlpha(0.6f) : k >= segs - 2 ? juce::Colours::white : k >= segs - 6 ? kInk : kDim.brighter(0.4f));
        g.fillRect(r.getX(), r.getBottom() - (k + 1) * sh + 1, r.getWidth(), sh - 1.5f);
    }
}
void Screen::vFader(juce::Graphics& g, float x, float w, float top, float h, const juce::String& label, float maxV, std::function<float()> get, std::function<void(float)> set, float meter, bool dbScale) {
    g.setColour(kFaint); g.drawRect(juce::Rectangle<float>(x + 1, top - 2, w - 4, h + 42), 1.f);
    g.setColour(kInk); g.setFont(mono(w < 52 ? 10.f : 12.f));
    const int chars = std::max(3, (int) (w / (w < 52 ? 6.2f : 7.4f)));
    g.drawText(label.substring(0, chars), (int) x, (int) top, (int) w - 2, 16, juce::Justification::centred);
    const juce::Rectangle<float> slot(x + w * 0.62f - 2, top + 22, 4, h);
    g.setColour(kFaint); g.fillRect(slot);
    // the scale: +3, 0, -6, -12, -24 dB
    g.setFont(mono(8));
    for (float dbv : { 3.f, 0.f, -6.f, -12.f, -24.f }) {
        const float gv = std::pow(10.f, dbv / 20), ty = slot.getBottom() - h * gv / maxV;
        if (gv > maxV) continue;
        g.setColour(dbv == 0 ? kInk : kDim); g.drawHorizontalLine((int) ty, slot.getRight() + 2, x + w - 4);
        if (w > 44) g.drawText(dbv > 0 ? "+" + juce::String((int) dbv) : juce::String((int) dbv), (int) (x + w - 20), (int) ty - 9, 16, 9, juce::Justification::centredRight);
    }
    ledMeter(g, { x + 4, slot.getY(), std::max(4.f, w * 0.18f), h }, meter);
    const float v = get(), y = slot.getBottom() - h * v / maxV;
    const juce::Rectangle<float> cap(slot.getCentreX() - std::min(14.f, w * 0.26f), y - 7, std::min(28.f, w * 0.52f), 14);
    g.setColour(kInk); g.fillRect(cap); g.setColour(kBg);
    for (int k = -1; k <= 1; ++k) g.drawHorizontalLine((int) (y + k * 3), cap.getX() + 3, cap.getRight() - 3);
    g.setColour(kDim); g.setFont(mono(10));
    g.drawText(dbScale ? (v <= 0.001f ? juce::String("-INF") : juce::String(20 * std::log10(v), 1)) : juce::String(juce::roundToInt(v / maxV * 100)) + "%",
               (int) x, (int) (slot.getBottom() + 3), (int) w - 2, 12, juce::Justification::centred);
    auto setY = [set, slot, maxV](const juce::MouseEvent& m) { set(m.getNumberOfClicks() > 1 ? 1.f : juce::jlimit(0.f, maxV, (slot.getBottom() - m.position.y) / slot.getHeight() * maxV)); };
    auto& hh = hit({ x, top + 16, w - 2, h + 12 }, setY, setY);
    hh.wheel = [get, set, maxV](float dy) { set(juce::jlimit(0.f, maxV, get() + (dy > 0 ? 0.02f : -0.02f) * maxV)); };
}

// ---- Page M: the mixer as an SSL console: grey strips, a send (red) and pan (blue) knob, mute (red) and solo
// (yellow), a long-throw fader with its scale, an LED meter, the scribble strip; the master with a red cap ----
static const juce::Colour sslPanel(0xff2b2e2d), sslEdge(0xff3d4140), sslGroove(0xff0d0f0e), sslCap(0xffc4c7c3), sslRed(0xffd2382c), sslBlue(0xff2f6fb8), sslYellow(0xffe7c23a),
    sslScribble(0xffe9e4cf), sslText(0xffdcdcd6);
static void sslKnob(juce::Graphics& g, float cx, float cy, float r, float v, juce::Colour cap, const juce::String& label) {
    g.setColour(juce::Colour(0xff141615)); g.fillEllipse(cx - r - 2, cy - r - 2, 2 * r + 4, 2 * r + 4);
    g.setColour(cap); g.fillEllipse(cx - r, cy - r, 2 * r, 2 * r);
    g.setColour(juce::Colours::white.withAlpha(0.2f)); g.fillEllipse(cx - r * 0.8f, cy - r * 0.8f, r * 1.1f, r * 1.1f);
    const double ang = juce::degreesToRadians(-135.0 + 270.0 * juce::jlimit(0.f, 1.f, v));
    g.setColour(juce::Colours::white); g.drawLine(cx, cy, cx + (float) std::sin(ang) * r * 0.9f, cy - (float) std::cos(ang) * r * 0.9f, 2.f);
    g.setColour(sslText); g.setFont(mono(9)); g.drawText(label, (int) (cx - 2 * r), (int) (cy + r + 2), (int) (4 * r), 10, juce::Justification::centred);
}
static void sslMeter(juce::Graphics& g, juce::Rectangle<float> r, float level) {
    constexpr int segs = 22; const float db = level > 1e-6f ? 20 * std::log10(level) : -100.f, sh = r.getHeight() / segs;
    g.setColour(juce::Colour(0xff070808)); g.fillRect(r);
    for (int k = 0; k < segs; ++k) {
        const float th = -42.f + 45.f * k / (segs - 1); const bool lit = db >= th;
        const juce::Colour col = k >= segs - 2 ? juce::Colour(0xffff3b30) : k >= segs - 6 ? juce::Colour(0xffffc93a) : juce::Colour(0xff3dff7a);
        g.setColour(lit ? col : col.withAlpha(0.13f)); g.fillRect(r.getX() + 1, r.getBottom() - (k + 1) * sh + 1, r.getWidth() - 2, sh - 1.5f);
    }
}
void Screen::sslStrip(juce::Graphics& g, float x, float sw, float top, float hgt, const juce::String& num, const juce::String& name, int idx, bool master) {
    auto& eng = ed.proc.engine;
    const juce::Rectangle<float> panel(x + 1, top, sw - 2, hgt);
    g.setColour(sslPanel); g.fillRect(panel); g.setColour(sslEdge); g.drawVerticalLine((int) panel.getX(), panel.getY(), panel.getBottom());
    g.setColour(master ? sslRed : sslText); g.setFont(mono(11)); g.drawText(num, panel.withHeight(16), juce::Justification::centred);
    const float kr = std::min(sw * 0.26f, 15.f);
    float y = top + 20;
    if (!master) {
        const float send = eng.fxSend[(size_t) idx].load(), pan = eng.panOver[(size_t) idx].load();
        sslKnob(g, x + sw / 2, y + kr, kr, send, sslRed, "SEND");
        auto sk = [this, idx, panel](const juce::MouseEvent& m) { ed.proc.engine.fxSend[(size_t) idx] = m.getNumberOfClicks() > 1 ? 0.15f : juce::jlimit(0.f, 1.f, (m.position.x - panel.getX()) / panel.getWidth()); };
        hit({ panel.getX(), y, panel.getWidth(), 2 * kr + 12 }, sk, sk);
        y += 2 * kr + 16;
        sslKnob(g, x + sw / 2, y + kr, kr, pan < 0 ? 0.5f : pan, sslBlue, pan < 0 ? juce::String("PAN") : "PAN " + juce::String(juce::roundToInt(pan * 100 - 50)));
        auto pk = [this, idx, panel](const juce::MouseEvent& m) { ed.proc.engine.panOver[(size_t) idx] = m.getNumberOfClicks() > 1 ? -1.f : juce::jlimit(0.f, 1.f, (m.position.x - panel.getX()) / panel.getWidth()); };
        hit({ panel.getX(), y, panel.getWidth(), 2 * kr + 12 }, pk, pk);
        y += 2 * kr + 16;
        const bool mu = eng.mute[(size_t) idx].load(), so = eng.solo[(size_t) idx].load();
        const float bw2 = (panel.getWidth() - 6) / 2;
        const juce::Rectangle<float> mb(panel.getX() + 2, y, bw2, 18), sb(mb.getRight() + 2, y, bw2, 18);
        g.setColour(mu ? sslRed : juce::Colour(0xff1a1c1b)); g.fillRect(mb); g.setColour(mu ? juce::Colours::white : sslText); g.setFont(mono(11)); g.drawText("M", mb, juce::Justification::centred);
        g.setColour(so ? sslYellow : juce::Colour(0xff1a1c1b)); g.fillRect(sb); g.setColour(so ? juce::Colours::black : sslText); g.drawText("S", sb, juce::Justification::centred);
        hit(mb, [this, idx, mu](const juce::MouseEvent&) { ed.proc.engine.mute[(size_t) idx] = !mu; });
        hit(sb, [this, idx, so](const juce::MouseEvent&) { ed.proc.engine.solo[(size_t) idx] = !so; });
        y += 26;
    } else y += 6;
    const float scrH = 18, fTop = y + 8, fh = std::max(40.f, top + hgt - scrH - 20 - fTop), gx = x + sw * 0.45f, maxV = 1.5f;
    const float v = master ? ed.proc.get("master") : eng.fader[(size_t) idx].load();
    g.setFont(mono(8));
    for (float dbv : { 3.f, 0.f, -5.f, -10.f, -20.f, -30.f }) {
        const float gv = std::pow(10.f, dbv / 20), ty = fTop + fh - fh * gv / maxV; if (gv > maxV) continue;
        g.setColour(sslText); g.drawHorizontalLine((int) ty, gx - 10, gx - 4);
        if (sw > 40) g.drawText(dbv > 0 ? "+" + juce::String((int) dbv) : juce::String((int) dbv), (int) (gx - 32), (int) ty - 5, 21, 10, juce::Justification::centredRight);
    }
    g.setColour(sslGroove); g.fillRect(gx - 2, fTop, 4.f, fh);
    sslMeter(g, { x + sw - 4 - std::max(6.f, sw * 0.14f), fTop, std::max(6.f, sw * 0.14f), fh }, master ? std::max(eng.meterL.load(), eng.meterR.load()) : eng.meter[(size_t) idx].load());
    const float cy = fTop + fh - fh * v / maxV, cw = std::min(13.f, sw * 0.24f);
    g.setColour(juce::Colour(0xff121413)); g.fillRect(gx - cw, cy - 10, 2 * cw, 20.f);
    g.setColour(master ? sslRed : sslCap); g.fillRect(gx - cw + 1, cy - 9, 2 * cw - 2, 18.f);
    g.setColour(master ? juce::Colours::white : juce::Colours::black); g.fillRect(gx - cw + 2, cy - 1, 2 * cw - 4, 2.f);
    g.setColour(sslText); g.setFont(mono(9));
    g.drawText(v <= 0.001f ? juce::String("-INF") : juce::String(20 * std::log10(v), 1), panel.withY(fTop + fh + 2).withHeight(12), juce::Justification::centred);
    auto fd = [this, idx, master, fTop, fh, maxV](const juce::MouseEvent& m) {
        const float nv = m.getNumberOfClicks() > 1 ? 1.f : juce::jlimit(0.f, maxV, (fTop + fh - m.position.y) / fh * maxV);
        if (master) ed.proc.set("master", nv); else ed.proc.engine.fader[(size_t) idx] = nv; };
    auto& fhit = hit({ panel.getX(), fTop - 10, panel.getWidth(), fh + 20 }, fd, fd);
    fhit.wheel = [this, idx, master, maxV](float dy) { if (master) ed.proc.set("master", juce::jlimit(0.f, maxV, ed.proc.get("master") + (dy > 0 ? 0.02f : -0.02f))); else { auto& f = ed.proc.engine.fader[(size_t) idx]; f = juce::jlimit(0.f, maxV, f.load() + (dy > 0 ? 0.02f : -0.02f)); } };
    const juce::Rectangle<float> scr(panel.getX() + 2, top + hgt - scrH - 2, panel.getWidth() - 4, scrH);
    g.setColour(sslScribble); g.fillRect(scr); g.setColour(juce::Colours::black); g.setFont(mono(10));
    g.drawText(name.substring(0, std::max(3, (int) (scr.getWidth() / 6.2f))), scr, juce::Justification::centred);
}
void Screen::drawMixer(juce::Graphics& g) {
    auto& eng = ed.proc.engine;
    const MidiSong* s = eng.song.load();
    const auto strips = mixStrips();
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText(juce::String("MIXER   ") + (s ? "SONG: " + juce::String(s->name).substring(0, 26) : juce::String("PAGE S PATTERNS")), 20, 58, 560, 20, juce::Justification::centredLeft);
    button(g, { 830, 56, 150, 22 }, "FX MIXER: E", false, [this] { page = 'E'; fxTab = 1; });
    if (s) button(g, { 690, 56, 130, 22 }, "DRUM KIT", false, [this] { kitBack = 'M'; page = 'K'; });
    button(g, { 570, 56, 110, 22 }, "RESET", false, [this] {
        auto& e = ed.proc.engine;
        for (int i = 0; i < Engine::STRIPS; ++i) { e.fader[(size_t) i] = 1; e.panOver[(size_t) i] = -1; e.mute[(size_t) i] = false; e.solo[(size_t) i] = false; e.fxSend[(size_t) i] = 0.15f; }
        ed.proc.set("master", 1.f); status = "MIXER RESET";
    });
    const juce::Rectangle<float> desk(16, 84, W - 32, 520);
    g.setColour(juce::Colour(0xff1c1e1d)); g.fillRect(desk);
    const float mw = 92;
    sslStrip(g, desk.getRight() - mw, mw, desk.getY() + 4, desk.getHeight() - 8, "MASTER", "MAIN MIX", -1, true);
    const float sw = std::min(80.f, (desk.getWidth() - mw - 8) / std::max(1, (int) strips.size()));
    float x = desk.getX() + 2; int n = 0;
    for (const auto& st : strips) { sslStrip(g, x, sw, desk.getY() + 4, desk.getHeight() - 8, n == 0 ? juce::String("KB") : juce::String(n), st.second, st.first, false); x += sw; ++n; }
}

// ---- Page E, FX MIXER: each strip's send to the REV7, and its return ----
void Screen::drawFxMixer(juce::Graphics& g) {
    auto& eng = ed.proc.engine;
    const auto strips = mixStrips();
    const bool on = eng.fxOn.load();
    const int n = (int) strips.size() + 1;
    const float sw = std::min(80.f, (W - 40.f) / n), top = 120, fh = 330;
    g.setColour(kDim); g.setFont(mono(13));
    g.drawText("SENDS TO THE REV7 (post fader)", 20, 92, 400, 18, juce::Justification::centredLeft);
    float x = 20;
    for (const auto& st : strips) {
        const int idx = st.first;
        vFader(g, x, sw, top, fh, st.second, 1.f, [this, idx] { return ed.proc.engine.fxSend[(size_t) idx].load(); }, [this, idx](float v) { ed.proc.engine.fxSend[(size_t) idx] = v; },
               eng.meter[(size_t) idx].load() * eng.fxSend[(size_t) idx].load(), false);
        x += sw;
    }
    vFader(g, W - 20 - sw, sw, top, fh, on ? "REV7" : "OFF", 1.5f, [this] { return (float) ed.proc.engine.fx.get("ret"); }, [this](float v) { ed.proc.engine.fx.set("ret", v); }, eng.fxMeter.load(), true);
    const float by = top + fh + 42;
    button(g, { 20, by, 140, 24 }, "ALL SENDS 0", false, [this] { for (auto& sd : ed.proc.engine.fxSend) sd = 0.f; });
    button(g, { 170, by, 140, 24 }, "ALL 15%", false, [this] { for (auto& sd : ed.proc.engine.fxSend) sd = 0.15f; });
    button(g, { 320, by, 140, 24 }, "ALL 30%", false, [this] { for (auto& sd : ed.proc.engine.fxSend) sd = 0.3f; });
    g.setColour(kDim); g.setFont(mono(12));
    g.drawText("REV7 fader: the return (its OUTPUT). Program, settings: REV7 tab", 480, (int) by, 500, 24, juce::Justification::centredLeft);
}

// ---- Page E: the effects section. A Yamaha REV7 (the Omega REV7 model) on the mixer's aux send ----
void Screen::drawEffects(juce::Graphics& g) {
    auto& eng = ed.proc.engine; auto& fx = eng.fx;
    const auto& sp = Rev7Fx::spec();
    const bool on = eng.fxOn.load();
    g.setColour(kInk); g.setFont(mono(17));
    button(g, { 20, 56, 120, 22 }, "REV7", fxTab == 0, [this] { fxTab = 0; });
    button(g, { 146, 56, 150, 22 }, "FX MIXER", fxTab == 1, [this] { fxTab = 1; });
    if (fxTab == 1) {
        button(g, { 560, 56, 110, 22 }, on ? "REV7 ON" : "REV7 OFF", on, [this, on] { ed.proc.engine.fxOn = !on; });
        g.setColour(kInk); g.setFont(mono(14));
        const int cp0 = fx.curPreset.load();
        g.drawText(cp0 >= 0 && cp0 < (int) sp.presets.size() ? juce::String(sp.presets[(size_t) cp0].name).substring(0, 34) : juce::String(omega::rev7ProgName((int) fx.get("prog"))) + " *", 680, 56, 300, 22, juce::Justification::centredRight);
        g.drawHorizontalLine(82, 20, (float) W - 20);
        drawFxMixer(g); return;
    }
    button(g, { 560, 56, 110, 22 }, on ? "REV7 ON" : "REV7 OFF", on, [this, on] { ed.proc.engine.fxOn = !on; });
    button(g, { 680, 56, 120, 22 }, "PRESET...", false, [this] { fxPresetMenu(); });
    button(g, { 810, 56, 110, 22 }, "SENDS", false, [this] { fxTab = 1; });
    {   // the dice: click a random preset, right-click (or shift-click) new random settings
        static int face = 5;
        juce::Rectangle<float> d(938, 50, 34, 34);
        g.setColour(kInk); g.drawRoundedRectangle(d.reduced(1), 5.f, 1.5f);
        static const int pips[7][6][2] = { {}, { {1, 1} }, { {0, 0}, {2, 2} }, { {0, 0}, {1, 1}, {2, 2} }, { {0, 0}, {2, 0}, {0, 2}, {2, 2} },
                                           { {0, 0}, {2, 0}, {1, 1}, {0, 2}, {2, 2} }, { {0, 0}, {0, 1}, {0, 2}, {2, 0}, {2, 1}, {2, 2} } };
        for (int k = 0; k < face; ++k) g.fillEllipse(d.getX() + 6 + pips[face][k][0] * 8.5f, d.getY() + 6 + pips[face][k][1] * 8.5f, 5.f, 5.f);
        hit(d, [this](const juce::MouseEvent& m) {
            face = 1 + juce::Random::getSystemRandom().nextInt(6);
            const bool settings = m.mods.isPopupMenu() || m.mods.isShiftDown();
            const std::string what = settings ? ed.proc.engine.fx.rollSettings() : ed.proc.engine.fx.rollPreset();
            juce::String nm(what); if (nm.contains(" / ")) nm = nm.fromFirstOccurrenceOf(" / ", false, false);
            status = "DICE: " + nm.toUpperCase();
        });
    }
    g.drawHorizontalLine(82, 20, (float) W - 20);
    // the LCD: memory number, program, preset
    const int prog = (int) fx.get("prog");
    juce::Rectangle<float> lcd(20, 90, 560, 44);
    g.setColour(kFaint); g.fillRect(lcd); g.setColour(kInk); g.drawRect(lcd, 1.f);
    g.setFont(mono(24));
    g.drawText(juce::String(prog).paddedLeft('0', 2) + "  " + omega::rev7ProgName(prog), lcd.reduced(10, 0).withHeight(28), juce::Justification::centredLeft);
    g.setFont(mono(13)); g.setColour(kDim);
    const int cp = fx.curPreset.load();
    g.drawText(cp >= 0 && cp < (int) sp.presets.size() ? juce::String(sp.presets[(size_t) cp].name) : juce::String(), lcd.reduced(10, 0).withTrimmedTop(26), juce::Justification::centredLeft);
    button(g, { 590, 100, 40, 26 }, "<", false, [this, prog] { ed.proc.engine.fx.set("prog", prog > 1 ? prog - 1 : 30); ed.proc.engine.fx.curPreset = -1; });
    button(g, { 636, 100, 40, 26 }, ">", false, [this, prog] { ed.proc.engine.fx.set("prog", prog < 30 ? prog + 1 : 1); ed.proc.engine.fx.curPreset = -1; });
    g.setColour(kDim); g.setFont(mono(13));
    g.drawText("PROGRAM", 690, 100, 100, 26, juce::Justification::centredLeft);
    const float lv = juce::jlimit(0.f, 1.f, eng.fxMeter.load());
    g.setColour(kFaint); g.fillRect(800.f, 108.f, 180.f, 10.f); g.setColour(lv > 0.95f ? juce::Colours::white : kInk); g.fillRect(800.f, 108.f, 180.f * lv, 10.f);
    // the edit parameters (the ones this program uses lit) and the panel's knobs and switches
    std::vector<int> ids;
    for (int i = 0; i < (int) sp.params.size(); ++i) if (Rev7Fx::shown(sp.params[(size_t) i]) && sp.params[(size_t) i].id != "prog") ids.push_back(i);
    const int rows = (int) (ids.size() + 1) / 2;
    const float rh = std::min(27.f, 440.f / rows);
    for (int k = 0; k < (int) ids.size(); ++k) {
        const int i = ids[(size_t) k];
        const auto& p = sp.params[(size_t) i];
        const bool lit = fx.relevant(p.id);
        const float x = k < rows ? 20.f : 510.f, y = 146.f + (k % rows) * rh;
        juce::Rectangle<float> r(x, y, 470, rh - 4);
        const double v = fx.get(i);
        g.setColour(lit ? kInk : kDim.withAlpha(0.5f)); g.setFont(mono(14));
        juce::String label(p.name); if (label.startsWith("Panel: ")) label = label.substring(7);
        g.drawText(label.toUpperCase(), r.withWidth(170), juce::Justification::centredLeft);
        g.drawText(Rev7Fx::format(p, v), r.withTrimmedLeft(380), juce::Justification::centredRight);
        juce::Rectangle<float> b(x + 175, y + 3, 200, rh - 10);
        if (p.toggle || !p.choices.empty()) {
            g.drawRect(b.withWidth(70), 1.f);
            if (p.toggle && v >= .5) { g.fillRect(b.withWidth(70).reduced(2)); }
            hit(b.withWidth(70), [this, i, p, v](const juce::MouseEvent&) {
                double nv = p.toggle ? (v >= .5 ? 0 : 1) : v + 1; if (!p.toggle && nv > p.max) nv = p.min;
                ed.proc.engine.fx.set(i, p.choices.empty() ? nv : p.choices[(size_t) juce::jlimit(0, (int) p.choices.size() - 1, (int) nv - (int) p.choices.front().value)].value);
            });
        } else {
            const float u = (float) ((v - p.min) / std::max(1e-9f, p.max - p.min));
            g.setColour(kFaint); g.fillRect(b);
            g.setColour(lit ? kInk : kDim); g.fillRect(b.withWidth(b.getWidth() * juce::jlimit(0.f, 1.f, u)));
            auto set = [this, i, p, b](const juce::MouseEvent& m) { ed.proc.engine.fx.set(i, p.min + (p.max - p.min) * juce::jlimit(0.f, 1.f, (m.position.x - b.getX()) / b.getWidth())); };
            auto& h = hit(b.expanded(0, 2), set, set);
            h.wheel = [this, i, p](float dy) { const double st = p.step > 0 ? p.step : (p.max - p.min) / 100; ed.proc.engine.fx.set(i, ed.proc.engine.fx.get(i) + (dy > 0 ? st : -st)); };
        }
    }
    g.setColour(kDim); g.setFont(mono(12));
    g.drawText("Sends: the FX MIXER tab. Dim: unused by this program. Dice: click random preset, right-click random settings.", 20, H - 50, W - 40, 16, juce::Justification::centredLeft);
}

void Screen::fxPresetMenu() {
    const auto& pr = Rev7Fx::spec().presets;
    juce::PopupMenu m; std::map<std::string, juce::PopupMenu> sub; std::vector<std::string> order;
    for (int k = 0; k < (int) pr.size(); ++k) {
        const std::string& n = pr[(size_t) k].name; const size_t sl = n.find(" / ");
        const std::string cat = sl == std::string::npos ? "Factory (ROM 1-30)" : n.substr(0, sl);
        if (!sub.count(cat)) order.push_back(cat);
        sub[cat].addItem(k + 1, sl == std::string::npos ? n : n.substr(sl + 3), true, ed.proc.engine.fx.curPreset.load() == k);
    }
    for (auto& c : order) m.addSubMenu(c, sub[c]);
    m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) { if (r > 0) { ed.proc.engine.fx.preset(r - 1); status = "REV7: " + juce::String(Rev7Fx::spec().presets[(size_t) r - 1].name).toUpperCase(); repaint(); } });
}

// the record controls: REC (the selected track, from fromBeat, after a count-in), the click, the count-in, the
// quantise for what's recorded, overdub or replace
void Screen::recordRow(juce::Graphics& g, float x, float y, double fromBeat) {
    auto& p = ed.proc;
    const bool rec = p.recording.load(), clk = p.engine.clickOn.load();
    const bool blink = rec && (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
    if (rec) { g.setColour(blink ? juce::Colours::white : kInk); g.fillRect(x, y, 60.f, 24.f); g.setColour(kBg); g.setFont(mono(17)); g.drawText("REC", juce::Rectangle<float>(x, y, 60, 24), juce::Justification::centred);
               hit({ x, y, 60, 24 }, [this](const juce::MouseEvent&) { status = ed.proc.stopRecording(); geFor = nullptr; }); }
    else button(g, { x, y, 60, 24 }, "REC", false, [this, fromBeat] { ed.proc.recFromBeat = fromBeat; status = ed.proc.startRecording(selTrack); });
    button(g, { x + 64, y, 70, 24 }, "CLICK", clk, [this, clk] { ed.proc.engine.clickOn = !clk; });
    button(g, { x + 138, y, 70, 24 }, "COUNT", p.recCountIn, [this] { ed.proc.recCountIn = !ed.proc.recCountIn; });
    const double q = p.rec.quant;
    button(g, { x + 212, y, 84, 24 }, q <= 0 ? "Q OFF" : q >= 0.5 ? "Q 1/8" : q >= 0.25 ? "Q 1/16" : "Q 1/32", q > 0, [this, q] { ed.proc.rec.quant = q <= 0 ? 0.5 : q >= 0.5 ? 0.25 : q >= 0.25 ? 0.125 : 0; });
    button(g, { x + 300, y, 90, 24 }, p.rec.replace ? "REPLACE" : "OVERDUB", p.rec.replace, [this] { ed.proc.rec.replace = !ed.proc.rec.replace; });
}

// ---- the DRUM KIT (Page K): for the song's kit, each drum's sound, tuning, level, pan, mute and solo, with meters ----
void Screen::kitVoiceMenu(int note, bool pads) {
    auto& p = ed.proc; auto& kit = p.kit;
    juce::PopupMenu m;
    if (rrAdding) m.addSectionHeader("ROUND ROBIN: THE VOICE TO ADD");
    else m.addItem(1, "Automatic (the CMI drum map)");
    if (pads && !rrAdding) { m.addItem(2, "Round robin: add a voice to cycle..."); if (!p.pads().p[(size_t) note].rr.empty()) m.addItem(3, "Round robin: clear this pad's"); }
    {   // the library's sounds of the same kind first (a snare pad: the snares)
        const juce::ScopedLock l(p.catLock);
        Part part = partOfNote(note);
        if (!p.catalog.voices.empty() && part != Part::None) {
            LibraryDrums ld(p.catalog);
            juce::PopupMenu same; for (int ci : ld.by[(size_t) part]) same.addItem(20000 + ci, juce::String(p.catalog.voices[(size_t) ci].name) + "  (" + p.catalog.voices[(size_t) ci].disk + ")");
            if (same.getNumItems() > 0) m.addSubMenu(juce::String(partName(part)) + "S IN YOUR LIBRARY", same);
            juce::PopupMenu all;
            for (int pi = 1; pi < PARTS; ++pi) { if (ld.by[(size_t) pi].empty()) continue; juce::PopupMenu sub; for (int ci : ld.by[(size_t) pi]) sub.addItem(20000 + ci, juce::String(p.catalog.voices[(size_t) ci].name)); all.addSubMenu(partName((Part) pi), sub); }
            m.addSubMenu("Every drum in your library, by kind", all);
        }
    }
    {   // your own samples
        juce::PopupMenu mine; for (int k = 0; k < Library::USER_SLOTS; ++k) if (kit.userLoaded(k) && !kit.user(k)->fromLibrary) mine.addItem(100 + kit.userSlot(k), juce::String(kit.user(k)->name));
        if (mine.getNumItems() > 0) m.addSubMenu("Your samples", mine);
    }
    {   // the voices loaded: drum disks first
        juce::PopupMenu loaded; std::map<std::string, juce::PopupMenu> cats;
        for (int i = 0; i < kit.bank.count.load(); ++i) { if (isBlank(kit, i)) continue; const Sound* s = kit.bank.at(i); if (s) cats[s->category].addItem(100 + i, juce::String(s->name)); }
        for (auto& kv : cats) loaded.addSubMenu(kv.first, kv.second);
        m.addSubMenu("Loaded voices", loaded);
    }
    {
        const juce::ScopedLock l(p.catLock);
        const auto& cv = p.catalog.voices;
        if (!cv.empty()) {
            std::map<std::string, juce::PopupMenu> byDisk; std::vector<std::string> drums, rest;
            for (size_t i = 0; i < cv.size() && i < 9000; ++i) {
                const std::string& d = cv[i].disk;
                if (!byDisk.count(d)) { const bool dr = d.rfind("DRUM", 0) == 0 || d.rfind("CYMBAL", 0) == 0 || d.rfind("PERCUS", 0) == 0 || d.rfind("BIGME", 0) == 0 || d.rfind("BIGML", 0) == 0; (dr ? drums : rest).push_back(d); }
                byDisk[d].addItem(20000 + (int) i, juce::String(cv[i].name));
            }
            juce::PopupMenu lib; for (auto& d : drums) lib.addSubMenu(d, byDisk[d]); lib.addSeparator(); for (auto& d : rest) lib.addSubMenu(d, byDisk[d]);
            m.addSubMenu("CMI library (drum disks first)", lib);
        }
    }
    m.showMenuAsync(juce::PopupMenu::Options(), [this, note, pads](int r) {
        auto& pr = ed.proc;
        if (r == 0) { rrAdding = false; return; }
        if (r == 2) { rrAdding = true; kitVoiceMenu(note, true); return; }
        if (r == 3) { pr.clearPadRR(note); status = "ROUND ROBIN CLEARED"; repaint(); return; }
        if (rrAdding) { rrAdding = false; if (r >= 20000) pr.addPadRR(note, -1, r - 20000); else if (r >= 100) pr.addPadRR(note, r - 100, -1); { const auto& rr = pr.pads().p[(size_t) note].rr; status = "ROUND ROBIN ON: " + juce::String(rr.empty() ? 1 : 2 + (int) std::count(rr.begin(), rr.end(), ';')) + " VOICES ON THIS PAD"; } pr.auditionKit(note); repaint(); return; }
        if (r == 1) { if (pads) pr.setPadVoice(note, -1); else pr.setKitVoice(note, -1); }
        else if (r >= 20000) { if (pads) pr.setPadCatalogVoice(note, r - 20000); else pr.setKitCatalogVoice(note, r - 20000); }
        else if (r >= 100) { if (pads) pr.setPadVoice(note, r - 100); else pr.setKitVoice(note, r - 100); }
        if (pads && r > 0) pr.padPreset = -1;
        repaint();
    });
}

// ---- the DRUM PADS (Page P): the kit every song's drums, MIDI channel 10 and these pads play, in General MIDI
// order (so a MIDI file's kick, snare and hats land right). Presets, the dice, your own samples. ----
void Screen::padPresetMenu() {
    juce::PopupMenu m; const auto& ps = kitPresets();
    for (size_t f = 0; f < ps.size(); f += 10) {
        juce::PopupMenu sub;
        for (size_t i = f; i < f + 10 && i < ps.size(); ++i) sub.addItem(1 + (int) i, juce::String(i + 1) + "  " + ps[i].name, true, ed.proc.padPreset == (int) i);
        m.addSubMenu(juce::String(f + 1) + "-" + juce::String(f + 10) + "  " + ps[f].name, sub);
    }
    m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) { if (r > 0) { status = ed.proc.padPresetLoad(r - 1); repaint(); } });
}
void Screen::padImportChooser(bool replaceKit) {
    ed.chooser = std::make_unique<juce::FileChooser>(replaceKit ? "A folder (or files) of drum samples: a new kit" : "Drum samples to add to the kit", ed.proc.samplesFolder(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    ed.chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                            [this, replaceKit](const juce::FileChooser& fc) { const auto res = fc.getResults(); if (res.isEmpty()) return; status = ed.proc.padImport(res, replaceKit); repaint(); });
}

// ---- the sample editor (Page W, from the pads): the pad's sound as the CMI's pages in 3D, the whole of it flat with
// the selection (drag across it), and the edits; the pad plays the edit as it stands ----
void Screen::drawEditor(juce::Graphics& g) {
    auto& p = ed.proc; auto& pe = p.padEd;
    g.setColour(kInk); g.setFont(mono(17));
    if (!pe.open) { g.drawText("NOTHING OPEN: PADS (P), EDIT A PAD, THEN TAP ONE", 20, 58, W - 40, 20, juce::Justification::centredLeft); button(g, { 20, 90, 120, 24 }, "PADS", false, [this] { page = 'P'; }); return; }
    const auto& e = pe.ed; const int len = e.len(), np = std::max(1, len / PAGE), a = e.a, b = e.b, ls = e.s.loopStart, le = e.s.loopEnd;
    g.drawText("SAMPLE EDIT   " + juce::String(e.s.name) + "   ON " + gmDrumName(pe.note) + " (" + juce::String(pe.note) + ")", 20, 56, W - 40, 20, juce::Justification::centredLeft);
    g.setColour(kDim); g.setFont(mono(13));
    g.drawText(juce::String(juce::roundToInt(e.s.rate)) + " Hz   " + juce::String(len) + " bytes   " + juce::String(len / std::max(1.0, e.s.rate), 3) + " s   SELECTION " + juce::String(a) + "-" + juce::String(b) +
               (ls >= 0 ? "   LOOP PAGES " + juce::String(ls + 1) + "-" + juce::String(le + 1) : juce::String()), 20, 78, W - 40, 16, juce::Justification::centredLeft);
    // 3D: page by page, the selection white, the loop in ink
    const juce::Rectangle<float> v3(20, 96, W - 40, 300);
    const float w = v3.getWidth() * 0.6f, dx = v3.getWidth() * 0.34f, h = v3.getHeight() * 0.2f, dy = v3.getHeight() * 0.7f;
    const float x0 = v3.getX(), y0 = v3.getBottom() - h * 0.6f;
    const int step = np > 64 ? 2 : 1;
    for (int pg = np - 1; pg >= 0; pg -= step) {
        const float u = np > 1 ? (float) pg / (np - 1) : 0.f, bx = x0 + dx * u, by = y0 - dy * u;
        juce::Path line, area;
        for (int i = 0; i < PAGE; i += 2) {
            const int k = pg * PAGE + i; const float v = k < len ? e.at(k) / 128.f : 0.f;
            const float x = bx + w * i / 127.f, y = by - v * h * 0.5f;
            if (i == 0) { line.startNewSubPath(x, y); area.startNewSubPath(x, by + h * 0.5f); }
            line.lineTo(x, y); area.lineTo(x, y);
        }
        area.lineTo(bx + w, by + h * 0.5f); area.closeSubPath();
        g.setColour(kBg); g.fillPath(area);
        const bool inSel = pg * PAGE + PAGE - 1 >= a && pg * PAGE < b && !(a == 0 && b == len), inLoop = ls >= 0 && pg >= ls && pg <= le;
        g.setColour(inSel ? juce::Colours::white : inLoop ? kInk : kDim); g.strokePath(line, juce::PathStrokeType(1.f));
    }
    // the whole sound flat, the selection on it
    const juce::Rectangle<float> ov(20, 404, W - 40, 70);
    g.setColour(kDim); g.drawRect(ov, 1.f);
    if (len > 0) {
        const float sx0 = ov.getX() + ov.getWidth() * a / len, sx1 = ov.getX() + ov.getWidth() * b / len;
        g.setColour(kInk.withAlpha(0.22f)); g.fillRect(juce::Rectangle<float>(sx0, ov.getY(), sx1 - sx0, ov.getHeight()));
        if (ls >= 0) { g.setColour(kInk); g.fillRect(ov.getX() + ov.getWidth() * ls * PAGE / len, ov.getBottom() - 4, ov.getWidth() * (std::min(len, (le + 1) * PAGE) - ls * PAGE) / len, 4.f); }
        g.setColour(kInk);
        const int cols = (int) ov.getWidth();
        for (int x = 0; x < cols; ++x) {
            const int i0 = (int) ((int64_t) x * len / cols), i1 = std::max(i0 + 1, (int) ((int64_t) (x + 1) * len / cols));
            int mn = 0, mx = 0; for (int i = i0; i < i1 && i < len; ++i) { mn = std::min(mn, e.at(i)); mx = std::max(mx, e.at(i)); }
            g.drawVerticalLine((int) ov.getX() + x, ov.getCentreY() - mx / 128.f * ov.getHeight() * 0.48f, ov.getCentreY() - mn / 128.f * ov.getHeight() * 0.48f + 1);
        }
        g.setColour(juce::Colours::white); g.fillRect(sx0 - 1, ov.getY(), 2.f, ov.getHeight()); g.fillRect(sx1 - 1, ov.getY(), 2.f, ov.getHeight());
    }
    auto pos = [ov, len](float x) { return juce::jlimit(0, len, juce::roundToInt((x - ov.getX()) / ov.getWidth() * len)); };
    hit(ov, [this, pos](const juce::MouseEvent& m) { edAnchor = pos(m.position.x); },
            [this, pos](const juce::MouseEvent& m) { ed.proc.padEd.ed.select(edAnchor, pos(m.position.x)); });
    // the edits
    struct B { const char* t; int op; float v; };
    const B bs[] = { { "PLAY", -1, 0 }, { "ALL", 0, 0 }, { "TRIM", 1, 0 }, { "CUT", 2, 0 }, { "SILENCE", 3, 0 }, { "NORMALISE", 4, 0 }, { "REVERSE", 5, 0 }, { "FADE IN", 6, 0 },
                     { "FADE OUT", 7, 0 }, { "-3 dB", 8, -3 }, { "+3 dB", 8, 3 }, { "LOOP SEL", 9, 0 }, { "NO LOOP", 10, 0 }, { "UNDO", 11, 0 }, { "REVERT", 12, 0 }, { "CLOSE", 13, 0 },
                     { "CHOP HITS", 14, 0 }, { "CHOP 4", 14, 4 }, { "CHOP 8", 14, 8 }, { "CHOP 16", 14, 16 } };
    const float bw = (W - 40 - 7 * 6) / 8.f;
    for (int i = 0; i < 20; ++i) {
        const B bb = bs[i];
        button(g, { 20 + (i % 8) * (bw + 6), 484.f + (i / 8) * 30.f, bw, 26 }, bb.t, i == 15, [this, bb] {
            auto& pr = ed.proc; auto& ee = pr.padEd.ed; const int note = pr.padEd.note;
            switch (bb.op) {
                case -1: pr.engine.padTap(note); return;
                case 0: ee.selectAll(); return; case 1: ee.trim(); break; case 2: ee.cut(); break; case 3: ee.silence(); break; case 4: ee.normalise(); break;
                case 5: ee.reverse(); break; case 6: ee.fadeIn(); break; case 7: ee.fadeOut(); break; case 8: ee.gain(bb.v); break; case 9: ee.loopSelection(); break;
                case 10: ee.noLoop(); break; case 11: ee.undoLast(); break;
                case 12: pr.padEd.revert(pr.kit, pr.engine); status = "PAD BACK AS IT WAS"; page = 'P'; return;
                case 13: status = "KEPT: " + juce::String(ee.s.name) + " ON THE PAD (YOUR SAMPLES)"; pr.padEd.close(); page = 'P'; return;
                case 14: { int made = 0; const int bk = pr.padEd.chop(pr.kit, pr.engine, (int) bb.v, &made);
                           if (bk < 0) { status = "NOTHING TO CHOP, OR NO FREE BANK"; return; }
                           pr.padBank = bk; pr.padSel = 0; status = juce::String(made) + " SLICES ON BANK " + juce::String::charToString((juce::juce_wchar) padBankLetter(bk)); return; }
            }
            pr.editCommit(); pr.engine.padTap(note);
        });
    }
    g.setColour(kDim); g.setFont(mono(12));
    g.drawText("drag across the strip to select; the edits work on the selection (ALL: the whole sound). CHOP: its slices onto a free bank's pads. CLOSE keeps the edit, REVERT puts the pad back.", 20, 576, W - 40, 16, juce::Justification::centredLeft);
}

void Screen::drawPads(juce::Graphics& g) {
    auto& p = ed.proc; auto& eng = p.engine;
    p.padBank = juce::jlimit(0, PAD_BANKS - 1, p.padBank); p.padSel = juce::jlimit(0, 15, p.padSel);
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("DRUM PADS   KIT: " + p.padKitName.substring(0, 22), 20, 58, 470, 20, juce::Justification::centredLeft);
    for (int b = 0; b < PAD_BANKS; ++b) {   // the banks: A-C the GM kit, D-H channel 10's other notes, I-T channels 11 and 12
        int used = 0; for (int i = 0; i < 16; ++i) used += !eng.padKit.p[(size_t) padNote(b, i)].voice.empty();
        const juce::Rectangle<float> br(20.f + b * 24.6f, 84, 22.5f, 22);
        if (p.padBank == b) { g.setColour(kInk); g.fillRect(br); } else g.setColour(used ? kInk : kFaint), g.drawRect(br, 1.f);
        g.setColour(p.padBank == b ? kBg : used ? kInk : kDim); g.setFont(mono(13)); g.drawText(juce::String::charToString((juce::juce_wchar) padBankLetter(b)), br, juce::Justification::centred);
        hit(br, [this, b](const juce::MouseEvent&) { ed.proc.padBank = b; });
    }
    // the pads, 4 x 4, pad 1 bottom left
    const float gx = 20, gy = 112, pw = 116, ph = 104, gap = 7;
    for (int i = 0; i < 16; ++i) {
        const int note = padNote(p.padBank, i), row = 3 - i / 4, col = i % 4;
        juce::Rectangle<float> r(gx + col * (pw + gap), gy + row * (ph + gap), pw, ph);
        const KitPiece kp = eng.padKit.p[(size_t) note];
        const float km = juce::jlimit(0.f, 1.f, eng.kitMeter[(size_t) note].load() * 2.5f);
        const bool sel = i == p.padSel;
        // like every button on the screen: a phosphor frame, lit (inverse) while it sounds
        if (km > 0.5f) { g.setColour(kInk); g.fillRect(r); }
        g.setColour(sel ? kInk : kDim); g.drawRect(r, sel ? 3.f : 1.f);
        g.setColour(km > 0.5f ? kBg : kInk); g.setFont(mono(12));
        g.drawText(juce::String(p.padBank * 16 + i + 1) + "  " + juce::String(note), r.reduced(6, 4), juce::Justification::topLeft);
        g.setFont(mono(15)); g.drawText(padName(note), r.reduced(4, 0).withTrimmedTop(26).withHeight(20), juce::Justification::centred);
        juce::String vn; bool chosen = !kp.voice.empty();
        if (chosen) vn = juce::String(kp.voice).upToFirstOccurrenceOf("@", false, false);
        else { const int a = eng.kitVoice ? eng.kitVoice(note) : -1; const Sound* snd = a >= 0 ? p.kit.bank.at(a) : nullptr; vn = snd ? "(" + juce::String(snd->name) + ")" : juce::String("--"); }
        g.setColour(km > 0.5f ? kBg : chosen ? kInk : kDim); g.setFont(mono(13));
        g.drawText(vn, r.reduced(4, 0).withTrimmedTop(52).withHeight(18), juce::Justification::centred);
        if (kp.tune != 0 || kp.mute || kp.solo) g.drawText((kp.tune != 0 ? juce::String(kp.tune > 0 ? "+" : "") + juce::String(kp.tune, 1) + " " : juce::String()) + (kp.mute ? "M " : "") + (kp.solo ? "S" : ""),
                                                            r.reduced(4, 0).withTrimmedTop(74).withHeight(16), juce::Justification::centred);
        auto& h = hit(r, [this, i, note, r](const juce::MouseEvent& e) {
            ed.proc.padSel = i;
            if (padEditPick) { padEditPick = false; ed.proc.engine.padTap(note); if (ed.proc.editPad(note)) { page = 'W'; status = "EDITING " + juce::String(ed.proc.padEd.ed.s.name); } else status = "NO SOUND ON THIS PAD TO EDIT"; return; }
            const int vel = juce::jlimit(30, 127, juce::roundToInt(127 - (e.position.y - r.getY()) / r.getHeight() * 80));   // harder near the top
            ed.proc.hitPad(note, vel);
        });
        h.up = [this, note](const juce::MouseEvent&) { ed.proc.hitPad(note, 0); };
    }
    // the kit: presets, the dice, your samples
    const float rx = 520, rw = 460;
    g.setColour(kInk); g.setFont(mono(14));
    button(g, { rx, 56, 40, 24 }, "<", false, [this] { status = ed.proc.padPresetLoad(ed.proc.padPreset < 0 ? 0 : ed.proc.padPreset - 1); });
    juce::Rectangle<float> pn(rx + 44, 56, 290, 24);
    g.setColour(kFaint); g.drawRect(pn, 1.f); g.setColour(kInk);
    g.drawText(p.padPreset >= 0 ? juce::String(p.padPreset + 1) + "  " + juce::String(kitPresets()[(size_t) p.padPreset].name) : juce::String("PRESETS (100)"), pn, juce::Justification::centred);
    hit(pn, [this](const juce::MouseEvent&) { padPresetMenu(); });
    button(g, { rx + 338, 56, 40, 24 }, ">", false, [this] { status = ed.proc.padPresetLoad(ed.proc.padPreset + 1); });
    button(g, { rx + 384, 56, 76, 24 }, "DICE", false, [this] { status = ed.proc.padRandom(); });
    button(g, { rx, 86, 150, 24 }, "NEW KIT: SAMPLES", false, [this] { padImportChooser(true); });
    button(g, { rx + 155, 86, 150, 24 }, "ADD SAMPLES", false, [this] { padImportChooser(false); });
    button(g, { rx + 310, 86, 150, 24 }, padEditPick ? "TAP A PAD" : "EDIT A PAD", padEditPick, [this] { padEditPick = !padEditPick; status = padEditPick ? "TAP THE PAD WHOSE SOUND YOU WANT TO EDIT" : "READY"; });
    g.setColour(kDim); g.setFont(mono(11));
    g.drawText("Samples become CMI voices: 8-bit, up to 30.2 kHz, named by kind (kick, snare, hat, BD, SD...)", rx, 112, rw, 14, juce::Justification::centredLeft);
    g.setColour(kDim); g.drawHorizontalLine(132, rx, rx + rw);
    // the pad chosen
    const int note = padNote(p.padBank, p.padSel);
    const KitPiece kp = eng.padKit.p[(size_t) note];
    g.setColour(kInk); g.setFont(mono(18));
    g.drawText("PAD " + juce::String::charToString((juce::juce_wchar) padBankLetter(p.padBank)) + juce::String(p.padSel + 1) + "   " + juce::String(padName(note)) + "   CH " + juce::String(10 + note / 128) + " NOTE " + juce::String(note % 128), rx, 140, rw, 24, juce::Justification::centredLeft);
    juce::String vn = kp.voice.empty() ? juce::String("AUTOMATIC: THE CMI DRUM MAP") : juce::String(kp.voice).replace("@", "  ");
    juce::Rectangle<float> vb(rx, 170, rw - 96, 28);
    button(g, { rx + rw - 90, 170, 90, 28 }, "EDIT", false, [this, note] { if (ed.proc.editPad(note)) { page = 'W'; status = "EDITING " + juce::String(ed.proc.padEd.ed.s.name); } else status = "NO SOUND ON THIS PAD TO EDIT"; });
    g.setColour(kInk); g.drawRect(vb, 1.f); g.setFont(mono(15)); g.drawText("SOUND: " + vn, vb.reduced(8, 0), juce::Justification::centredLeft);
    hit(vb, [this, note](const juce::MouseEvent&) { kitVoiceMenu(note, true); });
    auto edit = [this, note](std::function<void(KitPiece&)> f) { KitPiece q = ed.proc.pads().p[(size_t) note]; f(q); ed.proc.setPadPiece(note, q); };
    // tune, level, pan
    g.setFont(mono(15));
    g.drawText("TUNE " + juce::String(kp.tune > 0 ? "+" : "") + juce::String(kp.tune, 1), rx, 208, 160, 24, juce::Justification::centredLeft);
    button(g, { rx + 170, 208, 40, 24 }, "-1", false, [this, edit, note] { edit([](KitPiece& q) { q.tune = std::max(-24.f, q.tune - 1); }); ed.proc.auditionKit(note); });
    button(g, { rx + 214, 208, 50, 24 }, "-.1", false, [this, edit, note] { edit([](KitPiece& q) { q.tune = std::max(-24.f, std::round((q.tune - 0.1f) * 10) / 10); }); ed.proc.auditionKit(note); });
    button(g, { rx + 268, 208, 50, 24 }, "+.1", false, [this, edit, note] { edit([](KitPiece& q) { q.tune = std::min(24.f, std::round((q.tune + 0.1f) * 10) / 10); }); ed.proc.auditionKit(note); });
    button(g, { rx + 322, 208, 40, 24 }, "+1", false, [this, edit, note] { edit([](KitPiece& q) { q.tune = std::min(24.f, q.tune + 1); }); ed.proc.auditionKit(note); });
    auto barRow = [&](float y, const juce::String& label, float v, float maxV, std::function<void(float)> set, std::function<void()> reset) {
        g.setColour(kInk); g.setFont(mono(15)); g.drawText(label, (int) rx, (int) y, 160, 24, juce::Justification::centredLeft);
        juce::Rectangle<float> bb(rx + 170, y + 6, 290, 12);
        g.setColour(kFaint); g.fillRect(bb); g.setColour(kInk); g.fillRect(bb.withWidth(bb.getWidth() * juce::jlimit(0.f, 1.f, v / maxV)));
        auto f = [set, bb, maxV, reset](const juce::MouseEvent& e) { if (e.getNumberOfClicks() > 1) { reset(); return; } set(juce::jlimit(0.f, maxV, (e.position.x - bb.getX()) / bb.getWidth() * maxV)); };
        hit(bb.expanded(0, 6), f, f);
    };
    barRow(240, "LEVEL " + juce::String(juce::Decibels::gainToDecibels(kp.level, -60.f), 1) + " dB", kp.level, 2.f, [edit](float v) { edit([v](KitPiece& q) { q.level = v; }); }, [edit] { edit([](KitPiece& q) { q.level = 1; }); });
    barRow(270, kp.pan < 0 ? juce::String("PAN CENTRE") : "PAN " + juce::String(juce::roundToInt(kp.pan * 100 - 50)), kp.pan < 0 ? 0.5f : kp.pan, 1.f, [edit](float v) { edit([v](KitPiece& q) { q.pan = v; }); }, [edit] { edit([](KitPiece& q) { q.pan = -1; }); });
    button(g, { rx, 304, 110, 24 }, "MUTE", kp.mute, [edit] { edit([](KitPiece& q) { q.mute = !q.mute; }); });
    button(g, { rx + 116, 304, 110, 24 }, "SOLO", kp.solo, [edit] { edit([](KitPiece& q) { q.solo = !q.solo; }); });
    button(g, { rx + 232, 304, 110, 24 }, "AUTO", false, [this, note] { KitPiece q; ed.proc.setPadPiece(note, q); status = "PAD BACK TO THE CMI DRUM MAP"; });
    button(g, { rx + 348, 304, 112, 24 }, "PLAY", false, [this, note] { ed.proc.auditionKit(note); });
    {   // ADSR: this pad's or the whole kit's (drag a bar; double-click: back to the voice's own)
        const int target = adsrKit ? -1 : note;
        const float ev[4] = { target < 0 ? eng.padKit.env[0] : kp.a, target < 0 ? eng.padKit.env[1] : kp.d, target < 0 ? eng.padKit.env[2] : kp.s, target < 0 ? eng.padKit.env[3] : kp.r };
        button(g, { rx, 340, 70, 24 }, adsrKit ? "KIT" : "PAD", adsrKit, [this] { adsrKit = !adsrKit; });
        const char* nm[4] = { "A", "D", "S", "R" };
        const float aw = (rw - 76 - 18) / 4;
        for (int i = 0; i < 4; ++i) {
            const juce::Rectangle<float> ab(rx + 76 + i * (aw + 6), 340, aw, 24);
            const float v = ev[i], frac = v < 0 ? 0.f : i == 2 ? v : v / 31.f;
            g.setColour(kFaint); g.fillRect(ab); if (v >= 0) { g.setColour(kInk); g.fillRect(ab.withWidth(ab.getWidth() * frac)); }
            const double u = juce::jlimit(0.0, 31.0, (double) v) / 31.0, sec = std::max(0.0004, 12.0 * u * u * u);
            g.setColour(v >= 0 && frac > 0.5f ? kBg : kInk); g.setFont(mono(13));
            g.drawText(v < 0 ? juce::String(nm[i]) + " --" : i == 2 ? juce::String(nm[i]) + " " + juce::String(juce::roundToInt(v * 100)) + "%" : juce::String(nm[i]) + " " + (sec < 1 ? juce::String(juce::roundToInt(sec * 1000)) + "ms" : juce::String(sec, 1) + "s"), ab, juce::Justification::centred);
            auto f = [this, target, i, ab, note](const juce::MouseEvent& m) {
                if (m.getNumberOfClicks() > 1) { ed.proc.setPadEnv(target, i, -1); return; }
                const float uu = juce::jlimit(0.f, 1.f, (m.position.x - ab.getX()) / ab.getWidth()); ed.proc.setPadEnv(target, i, i == 2 ? uu : uu * 31); };
            auto& hh = hit(ab, f, f); hh.up = [this, note](const juce::MouseEvent&) { ed.proc.auditionKit(note); };
        }
    }
    {   // filter and LFO (the same PAD / KIT as the envelope; double-click a bar: back to the voice's own), round robin
        const int target = adsrKit ? -1 : note;
        const float fv[3] = { target < 0 ? eng.padKit.fx[0] : kp.f, target < 0 ? eng.padKit.fx[1] : kp.lr, target < 0 ? eng.padKit.fx[2] : kp.ld };
        const float mx[3] = { 255.f, 20.f, 12.f }; const char* nm[3] = { "FILTER", "LFO", "DEPTH" };
        const float aw = (rw - 76 - 12) / 3;
        for (int i = 0; i < 3; ++i) {
            const juce::Rectangle<float> ab(rx + i * (aw + 6), 372, aw, 24);
            const float v = fv[i], frac = v < 0 ? 0.f : v / mx[i];
            g.setColour(kFaint); g.fillRect(ab); if (v >= 0) { g.setColour(kInk); g.fillRect(ab.withWidth(ab.getWidth() * frac)); }
            g.setColour(v >= 0 && frac > 0.5f ? kBg : kInk); g.setFont(mono(13));
            g.drawText(v < 0 ? juce::String(nm[i]) + " --" : i == 0 ? "FILTER " + juce::String(juce::roundToInt(v)) : i == 1 ? "LFO " + juce::String(v, 1) + "Hz" : "DEPTH " + juce::String(v, 1), ab, juce::Justification::centred);
            auto f = [this, target, i, ab, mx](const juce::MouseEvent& m) {
                if (m.getNumberOfClicks() > 1) { ed.proc.setPadFx(target, i, -1); return; }
                ed.proc.setPadFx(target, i, juce::jlimit(0.f, 1.f, (m.position.x - ab.getX()) / ab.getWidth()) * mx[i]); };
            auto& hh = hit(ab, f, f); hh.up = [this, note](const juce::MouseEvent&) { ed.proc.auditionKit(note); };
        }
        const int nrr = kp.rr.empty() ? 0 : 1 + (int) std::count(kp.rr.begin(), kp.rr.end(), ';');
        button(g, { rx + rw - 70, 372, 70, 24 }, eng.padKit.rrOn ? "RR " + juce::String(nrr + 1) : "RR OFF", eng.padKit.rrOn, [this] { ed.proc.setRROn(!ed.proc.pads().rrOn); status = ed.proc.pads().rrOn ? "ROUND ROBIN ON: PADS WITH MORE VOICES CYCLE THEM (ADD ONE: SOUND)" : "ROUND ROBIN OFF"; });
    }
    g.setColour(kDim); g.drawHorizontalLine(404, rx, rx + rw);
    // the kit as a whole
    button(g, { rx, 416, 226, 24 }, eng.ch10Kit.load() ? "MIDI CH 10: THE KIT" : "MIDI CH 10: KEYBOARD", eng.ch10Kit.load(), [this] { ed.proc.engine.ch10Kit = !ed.proc.engine.ch10Kit.load(); });
    button(g, { rx + 234, 416, 226, 24 }, "CLEAR KIT", false, [this] { ed.proc.padsClear(); status = "PADS: THE CMI DRUM MAP"; });
    button(g, { rx, 446, 226, 24 }, "SONG'S OWN KIT (K)", false, [this] { kitBack = 'P'; page = 'K'; });
    button(g, { rx + 234, 446, 226, 24 }, "PLAY SONG", eng.songRun.load(), [this] { ed.proc.set("sync", 0.f); ed.proc.engine.songRun = !ed.proc.engine.songRun.load(); });
    // the kick's lift, humanize, recording the pads into a song
    {
        const float kl = eng.kickLift.load(), hu = eng.humanize.load();
        g.setColour(kInk); g.setFont(mono(15));
        g.drawText("KICK LIFT " + juce::String(juce::roundToInt(20 * std::log10(std::max(kl, 0.01f)))) + " dB", (int) rx, 480, 200, 24, juce::Justification::centredLeft);
        button(g, { rx + 170, 480, 40, 24 }, "-", false, [this] { auto& e = ed.proc.engine; e.kickLift = std::max(1.f, e.kickLift.load() / 1.4125f); });
        button(g, { rx + 214, 480, 40, 24 }, "+", false, [this] { auto& e = ed.proc.engine; e.kickLift = std::min(2.83f, e.kickLift.load() * 1.4125f); });
        g.drawText("HUMANIZE " + juce::String(juce::roundToInt(hu * 100)) + "%", (int) rx + 262, 480, 150, 24, juce::Justification::centredLeft);
        button(g, { rx + 380, 480, 38, 24 }, "-", false, [this] { auto& e = ed.proc.engine; e.humanize = std::max(0.f, e.humanize.load() - 0.25f); });
        button(g, { rx + 422, 480, 38, 24 }, "+", false, [this] { auto& e = ed.proc.engine; e.humanize = std::min(1.f, e.humanize.load() + 0.25f); });
        const bool rec = p.recording.load(), blink = rec && (juce::Time::getMillisecondCounter() / 400) % 2 == 0;
        juce::Rectangle<float> rb(rx, 512, 226, 28);
        if (blink) { g.setColour(juce::Colours::white); g.fillRect(rb); g.setColour(kBg); g.setFont(mono(17)); g.drawText("STOP: KEEP IT", rb, juce::Justification::centred); hit(rb, [this](const juce::MouseEvent&) { status = ed.proc.recordPads(); }); }
        else button(g, rb, rec ? "STOP: KEEP IT" : "REC DRUMS", rec, [this] { status = ed.proc.recordPads(); });
        button(g, { rx + 234, 512, 110, 28 }, "LOOP", eng.songLoop.load(), [this] { ed.proc.engine.songLoop = !ed.proc.engine.songLoop.load(); });
        button(g, { rx + 350, 512, 110, 28 }, "GRID", false, [this] { page = 'S'; songGrid = true; });
    }
    g.setColour(kDim); g.setFont(mono(12));
    const char* help[] = {
        "Banks A-C are the GM kit (a song's kick, snare, hats land there), D-H the",
        "rest of MIDI ch 10, I-T MIDI ch 11 and 12. REC DRUMS records the pads into",
        "the song; EDIT opens the sample editor (CHOP slices onto a free bank)." };
    for (int i = 0; i < 3; ++i) g.drawText(help[i], (int) rx, 548 + i * 15, (int) rw, 15, juce::Justification::centredLeft);
}

void Screen::drawKit(juce::Graphics& g) {
    auto& p = ed.proc; auto& eng = p.engine;
    const MidiSong* s = eng.song.load();
    g.setColour(kInk); g.setFont(mono(17));
    if (!s) { g.drawText("SONG KIT: load a song first (Page F). The kit for every song is on Page P (PADS).", 20, 58, W - 40, 20, juce::Justification::centredLeft);
              button(g, { 20, 90, 160, 24 }, "PADS", false, [this] { page = 'P'; }); return; }
    g.drawText("SONG KIT   " + juce::String(s->name).substring(0, 24), 20, 58, 420, 20, juce::Justification::centredLeft);
    button(g, { 420, 56, 94, 22 }, "PADS", false, [this] { page = 'P'; });
    button(g, { 520, 56, 120, 22 }, kitAll ? "ALL DRUMS" : "IN SONG", kitAll, [this] { kitAll = !kitAll; kitPage = 0; });
    button(g, { 646, 56, 100, 22 }, "RESET", false, [this] { ed.proc.resetKit(); status = "KIT BACK TO THE CMI DRUM MAP"; });
    button(g, { 752, 56, 110, 22 }, "PLAY", eng.songRun.load(), [this] { ed.proc.set("sync", 0.f); ed.proc.engine.songRun = !ed.proc.engine.songRun.load(); });
    button(g, { 868, 56, 112, 22 }, "BACK", false, [this] { page = kitBack; });
    const auto notes = p.kitNotes(kitAll);
    if (notes.empty()) { g.setColour(kDim); g.setFont(mono(14)); g.drawText("This song has no drum-kit track. Set a track's voice to the drum kit (VOICE... on Page S).", 20, 120, W - 40, 20, juce::Justification::centredLeft); return; }
    constexpr int per = 15;
    const int pages = ((int) notes.size() + per - 1) / per;
    kitPage = juce::jlimit(0, pages - 1, kitPage);
    const float sw = (W - 40.f) / per;
    for (int k = 0; k < per && kitPage * per + k < (int) notes.size(); ++k) {
        const int n = notes[(size_t) (kitPage * per + k)];
        const KitPiece kp = p.kitPiece(n);
        const float x = 20 + k * sw;
        // the drum
        g.setColour(kInk); g.setFont(mono(11));
        g.drawText(juce::String(n), (int) x, 88, (int) sw - 4, 12, juce::Justification::centred);
        g.drawFittedText(gmDrumName(n), (int) x, 100, (int) sw - 4, 24, juce::Justification::centredTop, 2);
        // its sound: chosen, or what the drum map plays
        juce::String vn; bool chosen = !kp.voice.empty();
        if (chosen) vn = juce::String(kp.voice).upToFirstOccurrenceOf("@", false, false);
        else { const int a = eng.kitVoice ? eng.kitVoice(n) : -1; const Sound* snd = a >= 0 ? p.kit.bank.at(a) : nullptr; vn = snd ? juce::String(snd->name) : juce::String("--"); }
        juce::Rectangle<float> vb(x + 2, 126, sw - 6, 20);
        if (chosen) { g.setColour(kInk); g.fillRect(vb); g.setColour(kBg); } else { g.setColour(kFaint); g.drawRect(vb, 1.f); g.setColour(kDim); }
        g.setFont(mono(10)); g.drawText(vn.substring(0, 8), vb, juce::Justification::centred);
        hit(vb, [this, n](const juce::MouseEvent&) { kitVoiceMenu(n); });
        // the pad
        juce::Rectangle<float> pad(x + 4, 150, sw - 10, 28);
        const float km = juce::jlimit(0.f, 1.f, eng.kitMeter[(size_t) n].load() * 2);
        g.setColour(kInk.withAlpha(0.25f + 0.75f * km)); g.fillRoundedRectangle(pad, 3.f);
        g.setColour(kBg); g.setFont(mono(11)); g.drawText("PLAY", pad, juce::Justification::centred);
        hit(pad, [this, n](const juce::MouseEvent&) { ed.proc.auditionKit(n); });
        // tune
        g.setColour(kInk); g.setFont(mono(11));
        g.drawText((kp.tune > 0 ? "+" : "") + juce::String((int) kp.tune), (int) x, 182, (int) sw - 4, 14, juce::Justification::centred);
        button(g, { x + 1, 182, 16, 14 }, "-", false, [this, n] { KitPiece q = ed.proc.kitPiece(n); q.tune = std::max(-24.f, q.tune - 1); ed.proc.setKitPiece(n, q); ed.proc.auditionKit(n); });
        button(g, { x + sw - 20, 182, 16, 14 }, "+", false, [this, n] { KitPiece q = ed.proc.kitPiece(n); q.tune = std::min(24.f, q.tune + 1); ed.proc.setKitPiece(n, q); ed.proc.auditionKit(n); });
        // level
        vFader(g, x, sw, 198, 250, "", 2.f, [this, n] { return ed.proc.kitPiece(n).level; }, [this, n](float v) { KitPiece q = ed.proc.kitPiece(n); q.level = v; ed.proc.setKitPiece(n, q); }, km, true);
        // pan, mute, solo
        const float py = 198 + 250 + 40;
        g.setColour(kFaint); g.fillRect(x + 3, py, sw - 8, 8.f);
        g.setColour(kp.pan < 0 ? kDim : kInk); g.fillRect(x + 3 + (sw - 12) * (kp.pan < 0 ? 0.5f : kp.pan), py - 2, 4.f, 12.f);
        auto pan = [this, n, x, sw](const juce::MouseEvent& m) { KitPiece q = ed.proc.kitPiece(n); q.pan = m.getNumberOfClicks() > 1 ? -1.f : juce::jlimit(0.f, 1.f, (m.position.x - x - 3) / (sw - 12)); ed.proc.setKitPiece(n, q); };
        hit({ x, py - 4, sw - 2, 16 }, pan, pan);
        button(g, { x + 1, py + 16, sw / 2 - 2, 20 }, "M", kp.mute, [this, n] { KitPiece q = ed.proc.kitPiece(n); q.mute = !q.mute; ed.proc.setKitPiece(n, q); });
        button(g, { x + sw / 2, py + 16, sw / 2 - 3, 20 }, "S", kp.solo, [this, n] { KitPiece q = ed.proc.kitPiece(n); q.solo = !q.solo; ed.proc.setKitPiece(n, q); });
    }
    const float y = (float) H - 58;
    if (pages > 1) {
        button(g, { 20, y, 40, 22 }, "<", false, [this] { kitPage = std::max(0, kitPage - 1); });
        g.setColour(kInk); g.setFont(mono(14)); g.drawText(juce::String(kitPage + 1) + "/" + juce::String(pages), 64, (int) y, 60, 22, juce::Justification::centred);
        button(g, { 128, y, 40, 22 }, ">", false, [this, pages] { kitPage = std::min(pages - 1, kitPage + 1); });
    }
    g.setColour(kDim); g.setFont(mono(12));
    g.drawText("this song only, on top of the pads' kit (Page P): click a sound to choose it; PLAY to hear it; double-click pan for the track's.", 180, (int) y, W - 200, 22, juce::Justification::centredLeft);
}

// ---- Page F: the library folder ----
// ---- Page F, SONGS: the song folder ----
// "1984 - Frankie Goes To Hollywood - Relax (alt)" -> artist, title
static std::pair<juce::String, juce::String> artistTitle(const juce::File& f) {
    juce::String n = f.getFileNameWithoutExtension().replaceCharacters("_", " ").trim();
    if (n.length() > 7 && n.substring(0, 4).containsOnly("0123456789") && n.substring(4, 7) == " - ") n = n.substring(7);
    const int d = n.indexOf(" - ");
    if (d > 0) return { n.substring(0, d).trim(), n.substring(d + 3).trim() };
    return { juce::String(), n };
}

void Screen::songSearchBox() {
    auto* w = new juce::AlertWindow("Search songs", "Part of a title or artist (empty: all)", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("q", songSearch); w->addButton("Search", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) { if (r == 1) { songSearch = w->getTextEditorContents("q").trim(); songPage = 0; } repaint(); }), true);
}

// ---- Page F, SONGS: the song folder as a browser ----
void Screen::drawSongFiles(juce::Graphics& g) {
    const juce::File dir = ed.proc.songFolder();
    const auto all = ed.proc.songFiles();
    // filter and order
    struct Row { juce::File f; juce::String artist, title, key; };
    std::vector<Row> rows;
    for (const auto& f : all) {
        const auto at = artistTitle(f);
        if (songSearch.isNotEmpty() && !f.getFileNameWithoutExtension().containsIgnoreCase(songSearch)) continue;
        rows.push_back({ f, at.first, at.second, (songByArtist ? at.first + " " + at.second : at.second + " " + at.first).toUpperCase() });
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.key.compareNatural(b.key) < 0; });
    g.setColour(kDim); g.setFont(mono(12));
    g.drawText("SONG FOLDER: " + (dir == juce::File() ? juce::String("(none set)") : dir.getFullPathName()), 20, 84, 560, 16, juce::Justification::centredLeft);
    button(g, { 590, 82, 120, 20 }, "SET FOLDER", false, [this] {
        ed.chooser = std::make_unique<juce::FileChooser>("Your MIDI songs", ed.proc.songFolder());
        ed.chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
            if (fc.getResult().isDirectory()) { ed.proc.setSongFolder(fc.getResult()); songPage = 0; status = juce::String((int) ed.proc.songFiles().size()) + " SONGS FOUND"; } repaint(); });
    });
    button(g, { 716, 82, 90, 20 }, "RESCAN", false, [this] { ed.proc.rescanSongs(); songInfoCache.clear(); status = juce::String((int) ed.proc.songFiles().size()) + " SONGS FOUND"; });
    button(g, { 812, 82, 168, 20 }, songByArtist ? "ORDER: ARTIST" : "ORDER: TITLE", false, [this] { songByArtist = !songByArtist; songPage = 0; });
    // search and the letters
    button(g, { 20, 106, 110, 22 }, "SEARCH", songSearch.isNotEmpty(), [this] { songSearchBox(); });
    if (songSearch.isNotEmpty()) { g.setColour(kInk); g.setFont(mono(13)); g.drawText("\"" + songSearch + "\"", 136, 106, 120, 22, juce::Justification::centredLeft); button(g, { 258, 106, 30, 22 }, "X", false, [this] { songSearch.clear(); songPage = 0; }); }
    const int rowsPer = 17;
    const juce::String letters = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int k = 0; k < letters.length(); ++k) {
        const juce::juce_wchar ch = letters[k];
        int first = -1; for (int i = 0; i < (int) rows.size() && first < 0; ++i) { const auto c0 = rows[(size_t) i].key[0]; if (ch == '#' ? !juce::CharacterFunctions::isLetter(c0) : c0 == ch) first = i; }
        juce::Rectangle<float> r(296.f + k * 25.f, 106, 23, 22);
        g.setColour(first >= 0 ? kInk : kFaint); g.setFont(mono(14)); g.drawText(juce::String::charToString(ch), r, juce::Justification::centred);
        if (first >= 0) hit(r, [this, first, rowsPer](const juce::MouseEvent&) { songPage = first / rowsPer; });
    }
    g.drawHorizontalLine(132, 20, (float) W - 20);
    // the list
    const int pages = std::max(1, ((int) rows.size() + rowsPer - 1) / rowsPer);
    songPage = juce::jlimit(0, pages - 1, songPage);
    if (rows.empty()) { g.setColour(kDim); g.setFont(mono(15)); g.drawText(dir == juce::File() ? "Set the folder with your MIDI songs (it's searched inside too)." : songSearch.isNotEmpty() ? "Nothing matches." : "No MIDI files in that folder.", 20, 150, 580, 20, juce::Justification::centredLeft); }
    const int cur = ed.proc.currentSong();
    for (int k = 0; k < rowsPer && songPage * rowsPer + k < (int) rows.size(); ++k) {
        const Row& rw = rows[(size_t) (songPage * rowsPer + k)];
        const int li = ed.proc.loadedSongFor(rw.f);
        juce::Rectangle<float> r(20, 138.f + k * 24.f, 590, 23);
        const bool picked = rw.f == songSel, playing = li >= 0 && li == cur;
        if (picked) { g.setColour(kInk); g.fillRect(r); } else if (playing) { g.setColour(kFaint); g.fillRect(r); }
        g.setColour(picked ? kBg : kInk); g.setFont(mono(15));
        g.drawText((playing ? "> " : li >= 0 ? "* " : "  ") + rw.title.substring(0, 34), r.reduced(4, 0), juce::Justification::centredLeft);
        g.setColour(picked ? kBg : kDim); g.setFont(mono(12));
        g.drawText(rw.artist.substring(0, 26), r.reduced(6, 0), juce::Justification::centredRight);
        const juce::File f = rw.f;
        auto& hh = hit(r, [this, f](const juce::MouseEvent& m) {
            songSel = f;
            if (m.getNumberOfClicks() > 1 || m.mods.isPopupMenu()) {   // load and play
                int li2 = ed.proc.loadedSongFor(f);
                if (li2 < 0) { const auto err = ed.proc.importMidi(f); li2 = ed.proc.loadedSongFor(f); if (err.isNotEmpty()) status = err.toUpperCase(); }
                if (li2 >= 0) { ed.proc.chooseSong(li2); ed.proc.set("sync", 0.f); ed.proc.engine.songRun = true; status = "PLAYING " + f.getFileNameWithoutExtension().toUpperCase(); }
            }
            repaint();
        });
        hh.wheel = [this, pages](float dy) { songPage = juce::jlimit(0, pages - 1, songPage + (dy > 0 ? -1 : 1)); };
    }
    const float py = 138 + rowsPer * 24 + 4;
    button(g, { 20, py, 40, 22 }, "<", false, [this] { songPage = juce::jmax(0, songPage - 1); });
    g.setColour(kInk); g.setFont(mono(14)); g.drawText(juce::String(songPage + 1) + "/" + juce::String(pages) + "   " + juce::String((int) rows.size()) + " SONGS", 64, (int) py, 260, 22, juce::Justification::centredLeft);
    button(g, { 570, py, 40, 22 }, ">", false, [this, pages] { songPage = juce::jmin(pages - 1, songPage + 1); });
    g.setColour(kDim); g.setFont(mono(11)); g.drawText("double-click: play  > playing  * loaded", 250, (int) py, 320, 22, juce::Justification::centredLeft);
    // the song picked
    juce::Rectangle<float> dp(624, 138, 356, 412);
    g.setColour(kFaint); g.drawRect(dp, 1.f);
    if (songSel == juce::File()) { g.setColour(kDim); g.setFont(mono(13)); g.drawText("Pick a song to see what's in it.", dp.reduced(10), juce::Justification::centredTop); return; }
    const auto at = artistTitle(songSel);
    const juce::String key = songSel.getFullPathName();
    if (!songInfoCache.count(key)) {   // its tempo, length and tracks, read once
        juce::MemoryBlock mb; MidiSong ms; std::string err; juce::String info;
        if (songSel.loadFileAsData(mb) && parseSmf((const uint8_t*) mb.getData(), mb.getSize(), "x", ms, err)) {
            int used = 0; for (int t = 0; t < SONG_TRK; ++t) used += ms.used[t];
            double secs = 0; { double b = 0; const double step = 0.25; while (b < ms.length) { secs += step * 60.0 / ms.bpmAt(b); b += step; } }
            info << juce::String(ms.bpmAt(0), 1) << " BPM   " << ms.num << "/" << ms.den << "   " << (int) secs / 60 << ":" << juce::String((int) secs % 60).paddedLeft('0', 2) << "   " << used << " TRACKS\n";
            for (int t = 0; t < SONG_TRK; ++t) {   // its name, and what it plays (the role the library matcher sees)
                if (!ms.used[t]) continue;
                const int ro = roleOf(ms, t);
                const juce::String what = ms.program[t] < 0 || ro < 0 ? juce::String("drums") : juce::String(roles()[(size_t) ro].name);
                juce::String nm = juce::String(ms.trackName[t]).trim().substring(0, 18);
                int same = 0; for (int u = 0; u < SONG_TRK; ++u) same += ms.used[u] && ms.trackName[u] == ms.trackName[t];
                if (same > 2) nm.clear();
                info << juce::String(t + 1).paddedLeft(' ', 2) << " " << (nm.isEmpty() ? what : nm.paddedRight(' ', 19) + what) << "\n";
            }
        } else info = "Can't read it: " + juce::String(err);
        songInfoCache[key] = info;
    }
    g.setColour(kInk); g.setFont(mono(19)); g.drawFittedText(at.second, dp.reduced(10).removeFromTop(48).toNearestInt(), juce::Justification::topLeft, 2);
    g.setColour(kDim); g.setFont(mono(14)); g.drawText(at.first, dp.reduced(10).withTrimmedTop(48).removeFromTop(18), juce::Justification::centredLeft);
    auto lines = juce::StringArray::fromLines(songInfoCache[key]);
    g.setColour(kInk); g.setFont(mono(13)); if (lines.size()) g.drawText(lines[0], dp.reduced(10).withTrimmedTop(72).removeFromTop(18), juce::Justification::centredLeft);
    g.setColour(kDim); g.setFont(mono(12));
    for (int i = 1; i < lines.size() && i < 17; ++i) g.drawText(lines[i], (int) dp.getX() + 10, (int) dp.getY() + 94 + (i - 1) * 15, (int) dp.getWidth() - 20, 14, juce::Justification::centredLeft);
    const int li = ed.proc.loadedSongFor(songSel);
    const juce::File f = songSel;
    auto load = [this, f]() { int li2 = ed.proc.loadedSongFor(f); if (li2 < 0) { const auto err = ed.proc.importMidi(f); li2 = ed.proc.loadedSongFor(f); if (err.isNotEmpty()) status = err.toUpperCase(); } if (li2 >= 0) ed.proc.chooseSong(li2); return li2; };
    const float by = dp.getBottom() - 30;
    button(g, { dp.getX() + 8, by, 104, 24 }, li >= 0 ? "SELECT" : "LOAD", false, [this, load] { if (load() >= 0) status = "LOADED"; });
    button(g, { dp.getX() + 118, by, 104, 24 }, "PLAY", li >= 0 && li == ed.proc.currentSong() && ed.proc.engine.songRun.load(), [this, load] { if (load() >= 0) { ed.proc.set("sync", 0.f); ed.proc.engine.songRun = true; } });
    button(g, { dp.getX() + 228, by, 120, 24 }, "TO PAGE S", false, [this, load] { if (load() >= 0) page = 'S'; });
    if (ed.proc.engine.songRun.load()) button(g, { dp.getX() + 228, by - 30, 120, 24 }, "STOP", false, [this] { ed.proc.engine.songRun = false; });
}

void Screen::drawFiles(juce::Graphics& g) {
    button(g, { 20, 56, 200, 24 }, "SONGS", songFiles, [this] { songFiles = true; });
    button(g, { 230, 56, 200, 24 }, "CMI LIBRARY", !songFiles, [this] { songFiles = false; });
    if (songFiles) { drawSongFiles(g); return; }
    g.saveState(); g.addTransform(juce::AffineTransform::translation(0, 28)); hitDy = 28;
    const juce::File dir = ed.proc.libraryFolder();
    const auto files = ed.proc.libraryFiles();
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("LIBRARY FOLDER: " + (dir == juce::File() ? juce::String("(none set)") : dir.getFullPathName()), 20, 58, W - 40, 20, juce::Justification::centredLeft);
    { const juce::ScopedLock l(ed.proc.catLock); g.setColour(kDim); g.setFont(mono(13));
      const auto& ct = ed.proc.catalog;
      g.drawText("CMI LIBRARY: " + juce::String(ct.voices.size()) + " VOICES ON " + juce::String(ct.disks) + " DISKS" + (ct.hfeDisks ? " (" + juce::String(ct.hfeDisks) + " FROM HFE)" : juce::String()) +
                 (ct.hfeSetAside ? ", " + juce::String(ct.hfeSetAside) + " HFE UNREADABLE (THEIR OTHER COPIES USED)" : juce::String()), 20, 110, 690, 16, juce::Justification::centredLeft); g.setFont(mono(17)); g.setColour(kInk); }
    button(g, { 720, 106, 260, 22 }, ed.proc.hfeOnly ? "READ: HFE FIRST" : "READ: ANY FORMAT", ed.proc.hfeOnly, [this] {
        ed.proc.setHfeOnly(!ed.proc.hfeOnly);
        const juce::ScopedLock l(ed.proc.catLock);
        status = juce::String(ed.proc.catalog.voices.size()) + " VOICES ON " + juce::String(ed.proc.catalog.disks) + " DISKS";
    });
    button(g, { 20, 84, 170, 24 }, "SET FOLDER", false, [this] {
        ed.chooser = std::make_unique<juce::FileChooser>("Your CMI library: songs, disks, samples", ed.proc.libraryFolder());
        ed.chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
            if (fc.getResult().isDirectory()) { ed.proc.setLibraryFolder(fc.getResult()); status = juce::String((int) ed.proc.libraryFiles().size()) + " FILES FOUND"; filePage = 0; } repaint(); });
    });
    button(g, { 200, 84, 130, 24 }, "RESCAN", false, [this] { ed.proc.rescanLibrary(); status = juce::String((int) ed.proc.libraryFiles().size()) + " FILES FOUND"; });
    button(g, { 340, 84, 190, 24 }, "LOAD ALL SONGS", false, [this] {
        juce::Array<juce::File> a; for (auto& f : ed.proc.libraryFiles()) if (f.hasFileExtension("mid;midi;zip")) a.add(f);
        int n = 0; status = ed.proc.importFiles(a, rootKey, loopIt, n); });
    button(g, { 540, 84, 190, 24 }, "LOAD ALL SOUNDS", false, [this] {
        juce::Array<juce::File> a; for (auto& f : ed.proc.libraryFiles()) if (!f.hasFileExtension("mid;midi;zip")) a.add(f);
        int n = 0; status = ed.proc.importFiles(a, rootKey, loopIt, n); });
    button(g, { 740, 84, 240, 24 }, "REMOVE DUPLICATE SONGS", false, [this] { const int c = ed.proc.currentSong(); const int gone = ed.proc.songs.removeDuplicates(); if (!ed.proc.songs.at(c)) ed.proc.chooseSong(-1); status = juce::String(gone) + " DUPLICATES REMOVED"; });
    const int rows = 20, per = rows * 2;
    const int pages = std::max(1, ((int) files.size() + per - 1) / per);
    filePage = juce::jlimit(0, pages - 1, filePage);
    for (int k = 0; k < per && filePage * per + k < (int) files.size(); ++k) {
        const juce::File f = files[(size_t) (filePage * per + k)];
        const juce::String e = f.getFileExtension().toLowerCase();
        const juce::String tag = e == ".mid" || e == ".midi" ? "SONG" : e == ".zip" ? "ZIP " : e == ".vc" ? "VC  " : e == ".imd" || e == ".img" || e == ".bin" || e == ".raw" || e == ".dsk" || e == ".hfe" ? "DISK" : "WAV ";
        juce::Rectangle<float> r(20.f + (k / rows) * 480.f, 130.f + (k % rows) * 21.f, 470, 20);
        g.setColour(tag == "SONG" ? kInk : kDim); g.setFont(mono(14));
        g.drawText(tag + " " + f.getFileNameWithoutExtension().substring(0, 50), r, juce::Justification::centredLeft);
        hit(r, [this, f](const juce::MouseEvent&) { int n = 0; status = ed.proc.importFiles({ f }, rootKey, loopIt, n); });
    }
    if (files.empty()) { g.setColour(kDim); g.drawText("Set a folder with your MIDI songs, CMI disks (.IMD / .IMG), voice files and samples. Click a file to load it.", 20, 130, W - 40, 20, juce::Justification::centredLeft); }
    button(g, { 560, 576, 200, 22 }, "CLEAR ALL", false, [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Clear all settings?", "Every control, the mixer, the effects, the pads' kit and the patterns go back to how they start. Your folders, library, songs and samples stay.", "Clear all", "Cancel", nullptr,
            juce::ModalCallbackFunction::create([this](int r) { if (r) { ed.proc.resetAll(false); status = "ALL SETTINGS CLEARED"; repaint(); } }));
    });
    button(g, { 770, 576, 210, 22 }, "FACTORY RESET", false, [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Factory reset?", "Everything goes: your folders, the remembered library, songs, samples, kits and settings.", "Reset everything", "Cancel", nullptr,
            juce::ModalCallbackFunction::create([this](int r) { if (r) { ed.proc.resetAll(true); status = "FACTORY RESET: SET YOUR LIBRARY FOLDER"; repaint(); } }));
    });
    if (pages > 1) {
        const float y = 118 + rows * 21 + 4;
        button(g, { 20, y, 40, 22 }, "<", false, [this] { filePage = juce::jmax(0, filePage - 1); });
        g.setColour(kInk); g.setFont(mono(15)); g.drawText(juce::String(filePage + 1) + "/" + juce::String(pages), 64, (int) y, 70, 22, juce::Justification::centred);
        button(g, { 138, y, 40, 22 }, ">", false, [this, pages] { filePage = juce::jmin(pages - 1, filePage + 1); });
    }
    g.restoreState(); hitDy = 0;
}

// ---- Page 5: drawing with the light pen ----
// ---- Page 5: the light pen. Draw the start and the end cycle of a waveform voice; it morphs from one to the other.
// LIVE: every stroke goes straight to the PEN voice, which the keyboard and MIDI in play, so you hear it as you draw.
void Screen::drawPen(juce::Graphics& g) {
    auto& p = ed.proc; auto& pen = p.pen;
    g.setColour(kInk); g.setFont(mono(16));
    g.drawText("LIGHT PEN: DRAW THE START AND THE END; THE VOICE MORPHS FROM ONE TO THE OTHER", 20, 58, 760, 20, juce::Justification::centredLeft);
    button(g, { 860, 56, 120, 22 }, p.penLive ? "LIVE: ON" : "LIVE: OFF", p.penLive, [this] { ed.proc.penLive = !ed.proc.penLive; if (ed.proc.penLive) ed.proc.penChanged(); });
    static double lastLive = 0;
    for (int b = 0; b < 2; ++b) {
        const juce::Rectangle<float> r(20.f + b * 490.f, 92, 470, 290);
        g.setColour(b == penBox ? kInk : kDim); g.drawRect(r, b == penBox ? 2.f : 1.f);
        g.setColour(kFaint); g.drawHorizontalLine((int) r.getCentreY(), r.getX(), r.getRight());
        for (int q = 1; q < 4; ++q) g.drawVerticalLine((int) (r.getX() + r.getWidth() * q / 4), r.getY(), r.getBottom());
        g.setFont(mono(14)); g.setColour(kDim); g.drawText(b ? "END" : "START", (int) r.getX() + 6, (int) r.getY() + 4, 80, 16, juce::Justification::centredLeft);
        juce::Path path;
        for (int i = 0; i < PAGE; ++i) { const float x = r.getX() + r.getWidth() * i / (PAGE - 1.f), y = r.getCentreY() - pen.c[b][i] * r.getHeight() * 0.48f; if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y); }
        g.setColour(kInk); g.strokePath(path, juce::PathStrokeType(2.f));
        auto draw = [this, b, r](const juce::MouseEvent& m) {
            penBox = b;
            const int px = juce::jlimit(0, PAGE - 1, (int) std::lround((m.position.x - r.getX()) / r.getWidth() * (PAGE - 1)));
            const float pv = juce::jlimit(-1.f, 1.f, (r.getCentreY() - m.position.y) / (r.getHeight() * 0.48f));
            ed.proc.pen.line(b, lastPx < 0 ? px : lastPx, lastPx < 0 ? pv : lastPv, px, pv);
            lastPx = px; lastPv = pv;
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (now - lastLive > 120) { lastLive = now; ed.proc.penChanged(); }   // hear it while drawing
        };
        auto& h = hit(r, [this, draw](const juce::MouseEvent& m) { lastPx = -1; draw(m); }, draw);
        h.up = [this](const juce::MouseEvent&) { lastPx = -1; ed.proc.penChanged(); };
    }
    // the morph: eight steps from the start to the end
    for (int k = 0; k < 8; ++k) {
        const juce::Rectangle<float> r(20.f + k * 120.f, 390, 110, 34);
        g.setColour(kFaint); g.drawRect(r, 1.f);
        juce::Path path; const float u = k / 7.f;
        for (int i = 0; i < PAGE; i += 2) { const float v = pen.c[0][i] * (1 - u) + pen.c[1][i] * u; const float x = r.getX() + r.getWidth() * i / (PAGE - 1.f), y = r.getCentreY() - v * r.getHeight() * 0.45f; if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y); }
        g.setColour(kDim); g.strokePath(path, juce::PathStrokeType(1.f));
    }
    const int bx = penBox;
    auto act = [this](std::function<void()> f) { return [this, f] { f(); ed.proc.penChanged(); }; };
    const char* names[8] = { "SINE", "SAW", "SQUARE", "TRIANGLE", "PULSE", "ORGAN", "NOISE", "CLEAR" };
    const int kinds[8] = { 0, 1, 2, 3, 6, 7, 4, 5 };
    for (int k = 0; k < 8; ++k) { const int kind = kinds[k]; button(g, { 20.f + k * 120, 432, 110, 22 }, names[k], false, act([this, bx, kind] { ed.proc.pen.shape(bx, kind, (uint32_t) juce::Time::getMillisecondCounter()); })); }
    button(g, { 20, 460, 110, 22 }, "SMOOTH", false, act([this, bx] { ed.proc.pen.smooth(bx); }));
    button(g, { 140, 460, 110, 22 }, "FULL", false, act([this, bx] { ed.proc.pen.normalise(bx); }));
    button(g, { 260, 460, 110, 22 }, "INVERT", false, act([this, bx] { ed.proc.pen.invert(bx); }));
    button(g, { 380, 460, 110, 22 }, "REVERSE", false, act([this, bx] { ed.proc.pen.reverse(bx); }));
    button(g, { 500, 460, 230, 22 }, bx == 0 ? "COPY START TO END" : "COPY END TO START", false, act([this, bx] { ed.proc.pen.copy(bx, 1 - bx); }));
    button(g, { 740, 460, 240, 22 }, "FROM KEYBOARD VOICE", false, [this] { if (ed.proc.penFrom(juce::roundToInt(ed.proc.get("sound")))) { status = "THE KEYBOARD'S VOICE IS ON THE PEN"; ed.proc.penChanged(); } else status = "NO VOICE ON THE KEYBOARD"; });
    // save
    button(g, { 20, 494, 240, 26 }, "SAVE AS A VOICE", false, [this] { status = ed.proc.penSaveAs(); });
    button(g, { 270, 494, 240, 26 }, "EXPORT WAV...", false, [this] {
        ed.chooser = std::make_unique<juce::FileChooser>("Save the drawn voice as a WAV (it loops, root C3)", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("PEN.wav"), "*.wav");
        ed.chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult(); if (f != juce::File()) { status = ed.proc.penExport(f.withFileExtension("wav")); repaint(); } });
    });
    g.setColour(kDim); g.setFont(mono(12));
    g.drawText(p.penSlot >= 0 ? "PEN = voice " + juce::String(p.penSlot + 1) + ": the keys and MIDI play it" : "Draw: the keys and MIDI play it", 520, 494, 460, 26, juce::Justification::centredLeft);
}

void Screen::drawSampling(juce::Graphics& g) {
    auto& kit = ed.proc.kit;
    constexpr int per = 15;
    // the list: your own voices (sampled, imported, drawn), or the ones loaded from your library folder (Page R
    // presets, songs, kits: loaded by themselves as they're needed, kept by name with the project)
    std::vector<int> list;
    for (int k = 0; k < Library::USER_SLOTS; ++k) if (kit.userLoaded(k) && kit.user(k)->fromLibrary == showLibVoices) list.push_back(k);
    const int pages = juce::jmax(1, ((int) list.size() + per - 1) / per);
    userPage = juce::jlimit(0, pages - 1, userPage);
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("DISKS & SAMPLING   " + juce::String(kit.ownCount()) + " YOUR SAMPLES   " + juce::String(kit.libraryCount()) + " FROM THE LIBRARY   " +
               juce::String(Library::USER_SLOTS - kit.loadedCount()) + " FREE", 20, 58, W - 40, 20, juce::Justification::centredLeft);
    g.drawHorizontalLine(80, 20, (float) W - 20);
    g.setColour(kDim); g.setFont(mono(14));
    g.drawText("CMI disks (.IMD / .IMG / .HFE) and voice files (.VC) load exactly as the CMI holds them: the 16 KB, its loop, its pitch rule.",
               20, 84, W - 40, 16, juce::Justification::centredLeft);
    g.drawText("Audio files: kept sample for sample if they fit a card, WAV root and loop used. Import files, a folder, or drop them here.", 20, 100, W - 40, 16, juce::Justification::centredLeft);
    auto launch = [this](bool folder, int slot) {
        const int flags = juce::FileBrowserComponent::openMode | (folder ? juce::FileBrowserComponent::canSelectDirectories
                                                                          : juce::FileBrowserComponent::canSelectFiles | (slot < 0 ? juce::FileBrowserComponent::canSelectMultipleItems : 0));
        ed.chooser = std::make_unique<juce::FileChooser>(folder ? "Import a folder of sounds" : slot < 0 ? "Import sounds" : "Replace voice " + juce::String(slot + 1),
                                                         juce::File(), folder ? juce::String() : juce::String("*.imd;*.img;*.hfe;*.bin;*.raw;*.dsk;*.vc;*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi;*.zip"));
        ed.chooser->launchAsync(flags, [this, slot](const juce::FileChooser& fc) {
            const auto res = fc.getResults(); if (res.isEmpty()) return;
            if (slot >= 0) { const auto err = ed.proc.loadSample(res[0], slot, rootKey, loopIt); status = err.isEmpty() ? "SAMPLED" : err.toUpperCase(); }
            else { int n = 0; status = ed.proc.importFiles(res, rootKey, loopIt, n); }
            showLibVoices = false; repaint();
        });
    };
    button(g, { 20, 122, 200, 24 }, "IMPORT DISKS/FILES", false, [launch] { launch(false, -1); });
    button(g, { 230, 122, 170, 24 }, "IMPORT FOLDER", false, [launch] { launch(true, -1); });
    button(g, { 410, 122, 170, 24 }, "YOUR SAMPLES", !showLibVoices, [this] { showLibVoices = false; userPage = 0; });
    button(g, { 590, 122, 170, 24 }, "FROM LIBRARY", showLibVoices, [this] { showLibVoices = true; userPage = 0; });
    {   // your samples folder: loaded whenever the plugin starts
        const juce::File sf = ed.proc.samplesFolder();
        g.setColour(kDim); g.setFont(mono(12));
        g.drawText("SAMPLES FOLDER: " + (sf.isDirectory() ? sf.getFullPathName() : juce::String("not set (its sounds load as your voices every time)")), 20, 556, 700, 16, juce::Justification::centredLeft);
        button(g, { 760, 552, 206, 22 }, "SET SAMPLES FOLDER", false, [this] {
            ed.chooser = std::make_unique<juce::FileChooser>("Your samples folder: its sounds load as your voices", ed.proc.samplesFolder());
            ed.chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult(); if (!f.isDirectory()) return;
                ed.proc.setSamplesFolder(f); status = "SAMPLES FOLDER SET: " + juce::String(ed.proc.kit.ownCount()) + " YOUR SAMPLES"; repaint(); });
        });
    }
    button(g, { 770, 122, 196, 24 }, showLibVoices ? "UNLOAD UNUSED" : "CLEAR MINE", false, [this] {
        auto& kt = ed.proc.kit;
        if (showLibVoices) { status = juce::String(ed.proc.unloadUnusedLibrary()) + " LIBRARY VOICES UNLOADED"; return; }
        for (int k = 0; k < Library::USER_SLOTS; ++k) if (kt.userLoaded(k) && !kt.user(k)->fromLibrary) kt.clearUser(k);
        status = "YOUR SAMPLES CLEARED";
    });
    if (list.empty()) {
        g.setColour(kDim); g.setFont(mono(15));
        g.drawText(showLibVoices ? "None yet: they load from your library folder (Page F) as presets, songs and kits need them."
                                 : "None yet: import or drop sounds here, or sample on Page 8's keyboard.", 20, 160, W - 40, 22, juce::Justification::centredLeft);
    }
    for (int r = 0; r < per; ++r) {
        const int li = userPage * per + r;
        if (li >= (int) list.size()) break;
        const int k = list[(size_t) li];
        const float y = 156.f + r * 24.f;
        const Sound* s = kit.user(k);
        g.setColour(s->waiting ? kDim : kInk); g.setFont(mono(15));
        g.drawText(juce::String(li + 1).paddedLeft('0', 3), 20, (int) y, 50, 22, juce::Justification::centredLeft);
        const juce::String loopTxt = s->loopStart >= 0 ? juce::String(s->loopStart + 1) + "-" + juce::String(s->loopEnd + 1) : juce::String("-");
        g.drawText(s->waiting ? juce::String(s->name).paddedRight(' ', 9) + juce::String(s->category).paddedRight(' ', 11) + "not in your library folder yet"
                   : s->category != "User" ? juce::String(s->name).paddedRight(' ', 9) + juce::String(s->category).paddedRight(' ', 11) + (s->mode == Mode::Waveform ? "MODE 1  " : "MODE 4  ") + "loop " + loopTxt
                              : juce::String(s->name).paddedRight(' ', 9) + juce::String(juce::roundToInt(s->rate)).paddedLeft(' ', 5) + " Hz  " + juce::String(s->ram.size() / s->rate, 2) +
                                    " s  root " + noteName(s->root) + "  loop " + loopTxt,
                   80, (int) y, 590, 22, juce::Justification::centredLeft);
        if (!showLibVoices) button(g, { 680, y + 1, 90, 20 }, "REPLACE", false, [launch, k] { launch(false, k); });
        button(g, { 778, y + 1, 90, 20 }, "PLAY", false, [this, k] { ed.proc.loadSound(ed.proc.kit.userSlot(k)); status = "ON THE KEYBOARD: " + juce::String(ed.proc.kit.user(k)->name); });
        button(g, { 876, y + 1, 90, 20 }, "REMOVE", false, [this, k] { ed.proc.kit.clearUser(k); });
    }
    const float by = 156.f + per * 24.f + 4;
    button(g, { 20, by, 40, 22 }, "<", false, [this] { userPage = juce::jmax(0, userPage - 1); });
    g.setColour(kInk); g.setFont(mono(15));
    g.drawText(juce::String(userPage + 1) + " OF " + juce::String(pages), 64, (int) by, 90, 22, juce::Justification::centred);
    button(g, { 158, by, 40, 22 }, ">", false, [this, pages] { userPage = juce::jmin(pages - 1, userPage + 1); });
    g.drawText("ROOT KEY: " + noteName(rootKey) + " (wheel/drag; a WAV's own wins)", 230, (int) by, 470, 22, juce::Justification::centredLeft);
    auto& h = hit({ 230, by, 470, 22 }, [this](const juce::MouseEvent&) { dragRoot = rootKey; },
                  [this](const juce::MouseEvent& e) { rootKey = juce::jlimit(24, 96, dragRoot - e.getDistanceFromDragStartY() / 6); });
    h.wheel = [this](float dy) { rootKey = juce::jlimit(24, 96, rootKey + (dy > 0 ? 1 : -1)); };
    button(g, { 760, by, 206, 22 }, loopIt ? "AUTO LOOP: ON" : "AUTO LOOP: OFF", loopIt, [this] { loopIt = !loopIt; });
}

// ---- Page R ----
static void noteGlyph(juce::Graphics& g, float x, float y, float h, int vel, bool lit) {
    // a stem with a flag: tall for an accent, short for a ghost note
    const float s = vel >= 120 ? 1.f : vel >= 80 ? 0.78f : 0.55f;
    const float top = y + h * (1 - s) * 0.5f + 4, bot = y + h - 6;
    g.setColour(lit ? juce::Colours::white : kInk);
    g.fillRect(x, top, 2.5f, bot - top);
    juce::Path f; f.startNewSubPath(x + 2, top); f.quadraticTo(x + 9, top + 1, x + 10, top + 9);
    g.strokePath(f, juce::PathStrokeType(2.f));
    g.fillRect(x, bot - 2, 10.f, 2.5f);
}

void Screen::songMenu() {
    juce::PopupMenu m;
    const int cur = ed.proc.currentSong();
    m.addItem(1, "Page S patterns", true, cur < 0);
    for (int i = 0; i < ed.proc.songs.size(); ++i) if (const SongEntry* e = ed.proc.songs.at(i)) m.addItem(100 + i, juce::String(e->song->name), true, cur == i);
    m.addSeparator();
    m.addItem(2, "Import MIDI file...");
    m.addItem(4, "New empty song (8 bars, for the grid)");
    if (cur >= 0) m.addItem(3, "Remove this song");
    m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
        if (r == 1) ed.proc.chooseSong(-1);
        else if (r >= 100) ed.proc.chooseSong(r - 100);
        else if (r == 3) { const int c = ed.proc.currentSong(); ed.proc.chooseSong(-1); ed.proc.songs.remove(c); }
        else if (r == 4) { ed.proc.chooseSong(ed.proc.songs.add(emptySong("NEW SONG " + std::to_string(ed.proc.songs.count() + 1)), ed.proc.kit.bank)); songGrid = true; selTrack = 0; viewStart = 0; }
        else if (r == 2) {
            ed.chooser = std::make_unique<juce::FileChooser>("Import MIDI songs (or a zip of them)", juce::File(), "*.mid;*.midi;*.zip");
            ed.chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                    [this](const juce::FileChooser& fc) {
                                        int n = 0; status = ed.proc.importFiles(fc.getResults(), rootKey, loopIt, n);
                                        repaint();
                                    });
        }
        repaint();
    });
}

void Screen::exportMenu() {
    juce::PopupMenu m;
    const bool song = ed.proc.currentSong() >= 0;
    m.addItem(1, song ? "Song as a WAV file (24-bit)..." : "Page S song as a WAV file (24-bit)...");
    if (song) { m.addItem(2, "Stems: a WAV file per track..."); m.addItem(3, "Song as a MIDI file (with your edits)..."); }
    m.showMenuAsync(juce::PopupMenu::Options(), [this, song](int r) {
        if (r <= 0) return;
        const SongEntry* e = ed.proc.songs.at(ed.proc.currentSong());
        const juce::String base = song && e ? juce::String(e->song->name) : juce::String("PAGE S");
        const bool midi = r == 3;
        ed.chooser = std::make_unique<juce::FileChooser>(midi ? "Export MIDI" : "Export audio", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(base + (midi ? ".mid" : ".wav")),
                                                         midi ? "*.mid" : "*.wav");
        ed.chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                [this, r, midi](const juce::FileChooser& fc) {
                                    juce::File f = fc.getResult(); if (f == juce::File()) return;
                                    f = f.withFileExtension(midi ? "mid" : "wav");
                                    auto done = [this](juce::String msg) { status = msg.toUpperCase(); repaint(); };
                                    if (midi) ed.proc.exportMidi(f, done);
                                    else { status = "RENDERING..."; ed.proc.exportAudio(f, r == 2, done); }
                                    repaint();
                                });
    });
}

void Screen::learnMenu(const char* id) {
    const int pi = paramIndex(id); if (pi < 0) return;
    const int cc = ed.proc.ccFor(pi);
    juce::PopupMenu m;
    m.addItem(1, "MIDI learn: move a controller");
    if (cc >= 0) m.addItem(2, "Forget CC " + juce::String(cc));
    m.addItem(3, "Reset every control to the default CCs");
    m.showMenuAsync(juce::PopupMenu::Options(), [this, pi, cc](int r) {
        if (r == 1) { ed.proc.learnParam = pi; status = "MOVE A CONTROLLER FOR " + juce::String(paramDefs()[(size_t) pi].name).toUpperCase(); }
        else if (r == 2) ed.proc.ccMap[(size_t) cc] = -1;
        else if (r == 3) ed.proc.resetCCMap();
        repaint();
    });
}

// the grid: one track's notes as a piano roll; click to add, drag to move, drag the end to lengthen,
// right-click to delete, double-click for loud / soft; wheel scrolls, shift-wheel moves the octaves, ctrl-wheel zooms
void Screen::gridCommit() {
    SongEntry* e = ed.proc.songs.at(ed.proc.currentSong()); if (!e) return;
    editSong(ed.proc.engine, e, ge.notes); geFor = e->song.get(); dirty = false;
}
void Screen::gridAudition(int pitch) { ed.proc.audition(selTrack, pitch); }

// the piano roll for one track: keys down the side (click to hear), the other tracks faintly behind, a velocity lane
// and the whole song as a strip underneath. Tools: DRAW (click adds, drag moves, drag the end to lengthen),
// SELECT (drag a box), ERASE (click or drag over notes). Right-click deletes in any tool.
void Screen::drawGrid(juce::Graphics& g, juce::Rectangle<float> a) {
    auto& eng = ed.proc.engine;
    SongEntry* e = ed.proc.songs.at(ed.proc.currentSong());
    if (!e) return;
    const MidiSong& s = *e->song;
    if (e->song.get() != geFor && dragIdx < 0) { ge.setNotes(notesOf(s)); geFor = e->song.get(); }
    ge.fit();
    const int rows = gridRows;
    if (selTrack != gridTrack) {   // a new track: centre its notes, clear the selection
        gridTrack = selTrack; ge.clear();
        std::vector<int> ps; for (const auto& n : ge.notes) if (n.track == selTrack) ps.push_back(n.pitch);
        if (!ps.empty()) { std::nth_element(ps.begin(), ps.begin() + (long) ps.size() / 2, ps.end()); pitchLow = juce::jlimit(0, 127 - rows, ps[ps.size() / 2] - rows / 2); }
    }
    pitchLow = juce::jlimit(0, 127 - rows, pitchLow);
    const float keyW = 40, gx = a.getX() + keyW, gw = a.getWidth() - keyW;
    const float velH = 40, ovH = 12;
    juce::Rectangle<float> ga(gx, a.getY(), gw, a.getHeight() - velH - ovH - 8);
    const float rh = ga.getHeight() / rows;
    const double beatNow = eng.songBeat.load();
    if (gridFollow && eng.songRun.load() && dragIdx < 0 && !banding && (beatNow < viewStart || beatNow > viewStart + viewBeats)) viewStart = std::floor(beatNow / 4) * 4;
    auto xOf = [=](double b) { return gx + (float) ((b - viewStart) / viewBeats) * gw; };
    auto beatAt = [=](float x) { return viewStart + (x - gx) / gw * viewBeats; };
    auto pitchAt = [=](float y) { return pitchLow + (int) std::floor((ga.getBottom() - y) / rh); };
    auto snapTo = [this](double b) { return snap > 0 ? std::round(b / snap) * snap : b; };
    // keys and rows
    for (int r = 0; r < rows; ++r) {
        const int p = pitchLow + r; const float y = ga.getBottom() - (r + 1) * rh;
        const int k = p % 12; const bool black = k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
        g.setColour(black ? juce::Colour(0xff031208) : kBg); g.fillRect(gx, y, gw, rh);
        g.setColour(black ? juce::Colour(0xff0d2a1c) : juce::Colour(0xffcfe9d6).withAlpha(0.85f)); g.fillRect(a.getX(), y, black ? keyW * 0.62f : keyW - 2, rh - 0.5f);
        if (k == 0) { g.setColour(kBg); g.setFont(mono(std::min(11.f, rh))); g.drawText(noteName(p), (int) a.getX() + 2, (int) y, (int) keyW - 4, (int) rh, juce::Justification::centredRight); g.setColour(kFaint); g.drawHorizontalLine((int) (y + rh), gx, gx + gw); }
    }
    hit({ a.getX(), ga.getY(), keyW, ga.getHeight() }, [this, pitchAt](const juce::MouseEvent& m) { gridAudition(pitchAt(m.position.y)); });
    // beats and bars
    const double barBeats = s.num * 4.0 / s.den, step = snap > 0 ? snap : 0.25;
    for (double b = std::ceil(viewStart / step) * step; b <= viewStart + viewBeats; b += step) {
        const bool bar = std::fmod(b + 1e-6, barBeats) < 2e-6, beat = std::fmod(b + 1e-6, 1.0) < 2e-6;
        if (!beat && viewBeats > 48) continue;
        g.setColour(bar ? kDim : beat ? kFaint : juce::Colour(0xff08200f)); g.fillRect(xOf(b), ga.getY(), bar ? 1.5f : 1.f, ga.getHeight());
        if (bar) { g.setColour(kDim); g.setFont(mono(11)); g.drawText(juce::String(1 + (int) std::round(b / barBeats)), (int) xOf(b) + 2, (int) ga.getY() - 13, 40, 12, juce::Justification::centredLeft); }
    }
    // notes: the other tracks faintly, then this one (the selection white-edged)
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < ge.notes.size(); ++i) {
            const auto& n = ge.notes[i];
            if ((n.track == selTrack) != (pass == 1)) continue;
            if (pass == 0 && !gridGhost) continue;
            if (n.start + n.len < viewStart || n.start > viewStart + viewBeats || n.pitch < pitchLow || n.pitch >= pitchLow + rows) continue;
            const float y = ga.getBottom() - (n.pitch - pitchLow + 1) * rh, x0 = std::max(gx, xOf(n.start)), x1 = std::min(gx + gw, xOf(n.start + n.len));
            if (pass == 0) { g.setColour(kFaint.brighter(0.25f)); g.fillRect(x0, y + rh * 0.35f, std::max(1.f, x1 - x0), rh * 0.3f); continue; }
            const bool on = ge.sel[i] != 0;
            g.setColour((on ? juce::Colours::white : kInk).withAlpha(0.5f + 0.5f * n.vel / 127.f)); g.fillRect(x0, y + 1, std::max(3.f, x1 - x0 - 1), rh - 2);
            g.setColour(on ? juce::Colours::white : kBg); g.drawRect(x0, y + 1, std::max(3.f, x1 - x0 - 1), rh - 2, 1.f);
            if (x1 - x0 > 14) { g.setColour(kBg.withAlpha(0.8f)); g.fillRect(x1 - 4, y + 3, 2.f, rh - 6); }   // the end handle
        }
    if (beatNow >= viewStart && beatNow <= viewStart + viewBeats) { g.setColour(juce::Colours::white); g.fillRect(xOf(beatNow), ga.getY(), 1.5f, ga.getHeight()); }
    if (banding) { g.setColour(juce::Colours::white.withAlpha(0.15f)); auto br = juce::Rectangle<float>(bandA, bandB); g.fillRect(br); g.setColour(juce::Colours::white); g.drawRect(br, 1.f); }
    g.setColour(kInk); g.drawRect(ga, 1.f);

    auto& h = hit(ga, [=](const juce::MouseEvent& m) {
        grabKeyboardFocus();
        const double b = beatAt(m.position.x); const int p = pitchAt(m.position.y);
        const int idx = ge.at(selTrack, b, p, std::max(snap, 0.125) * 0.5);
        velIdx = -1;
        if (m.mods.isPopupMenu() || gridTool == 2) {   // erase
            if (idx >= 0) { ge.sel.assign(ge.notes.size(), 0); ge.sel[(size_t) idx] = 1; ge.erase(); dirty = true; }
            dragIdx = -2; return;
        }
        if (idx >= 0) {
            if (!ge.sel[(size_t) idx]) { if (!m.mods.isShiftDown()) ge.clear(); ge.sel[(size_t) idx] = 1; }
            else if (m.mods.isShiftDown()) { ge.sel[(size_t) idx] = 0; dragIdx = -1; return; }
            dragIdx = idx; dragFrom = ge.notes; dragOrig = ge.notes[(size_t) idx];
            dragMode = m.position.x > xOf(dragOrig.start + dragOrig.len) - 7 ? 1 : 0;
            newLen = dragOrig.len; gridAudition(p);
        } else if (gridTool == 1) {   // select: a box
            if (!m.mods.isShiftDown()) ge.clear();
            banding = true; bandA = bandB = m.position; dragIdx = -1;
        } else {   // draw: a new note, drag to lengthen
            ge.clear();
            const double st = std::max(0.0, snap > 0 ? std::floor(b / snap) * snap : b);
            dragIdx = ge.add(selTrack, st, newLen, p, 100); ge.sel[(size_t) dragIdx] = 1;
            dragFrom = ge.notes; dragOrig = ge.notes[(size_t) dragIdx]; dragMode = 2; dirty = true; gridAudition(p);
        }
        grabBeat = b; grabPitch = p;
    }, [=](const juce::MouseEvent& m) {
        const double b = beatAt(m.position.x); const int p = pitchAt(m.position.y);
        if (banding) { bandB = m.position; ge.selectRect(selTrack, beatAt(bandA.x), b, pitchAt(bandA.y), p, m.mods.isShiftDown()); return; }
        if (dragIdx == -2) { const int idx = ge.at(selTrack, b, p, 0.06); if (idx >= 0) { ge.sel.assign(ge.notes.size(), 0); ge.sel[(size_t) idx] = 1; ge.erase(); dirty = true; } return; }
        if (dragIdx < 0 || dragIdx >= (int) ge.notes.size() || dragFrom.size() != ge.notes.size()) return;
        if (dragMode == 0) {   // move the selection
            const double db = snapTo(b - grabBeat); const int dp = p - grabPitch;
            for (size_t i = 0; i < ge.notes.size(); ++i) if (ge.sel[i]) { ge.notes[i].start = std::max(0.0, dragFrom[i].start + db); ge.notes[i].pitch = juce::jlimit(0, 127, dragFrom[i].pitch + dp); }
            if (dp != 0 && ge.notes[(size_t) dragIdx].pitch != dragOrig.pitch + dp - 0) {}
        } else {               // lengthen (the selection by the same amount)
            const double end = std::max(dragOrig.start + std::max(snap, 1.0 / 32), snap > 0 ? std::ceil((b - 1e-9) / snap) * snap : b);
            const double d = end - (dragOrig.start + dragOrig.len);
            for (size_t i = 0; i < ge.notes.size(); ++i) if (ge.sel[i]) ge.notes[i].len = std::max(snap > 0 ? snap : 1.0 / 32, dragFrom[i].len + d);
            newLen = ge.notes[(size_t) dragIdx].len;
        }
        dirty = true;
    });
    h.up = [this](const juce::MouseEvent&) { banding = false; if (dirty) gridCommit(); dragIdx = -1; };
    h.wheel = [this](float dy) {
        if (wheelMods.isShiftDown()) pitchLow = juce::jlimit(0, 127 - gridRows, pitchLow + (dy > 0 ? 2 : -2));
        else if (wheelMods.isCommandDown() || wheelMods.isCtrlDown()) viewBeats = juce::jlimit(2.0, 512.0, viewBeats * (dy > 0 ? 0.8 : 1.25));
        else viewStart = std::max(0.0, viewStart + (dy > 0 ? -1 : 1) * viewBeats / 8);
    };
    // the velocity lane
    juce::Rectangle<float> vl(gx, ga.getBottom() + 4, gw, velH);
    g.setColour(juce::Colour(0xff041a0c)); g.fillRect(vl); g.setColour(kFaint); g.drawRect(vl, 1.f);
    g.setColour(kDim); g.setFont(mono(10)); g.drawText("VEL", (int) a.getX(), (int) vl.getY(), (int) keyW - 4, (int) velH, juce::Justification::centredRight);
    for (size_t i = 0; i < ge.notes.size(); ++i) {
        const auto& n = ge.notes[i]; if (n.track != selTrack || n.start < viewStart || n.start > viewStart + viewBeats) continue;
        const float x = xOf(n.start), hgt = (velH - 4) * n.vel / 127.f;
        g.setColour(ge.sel[i] ? juce::Colours::white : kInk); g.fillRect(x, vl.getBottom() - 2 - hgt, 3.f, hgt);
    }
    auto velSet = [=](const juce::MouseEvent& m) {
        const int v = juce::jlimit(1, 127, (int) std::lround((vl.getBottom() - 2 - m.position.y) / (velH - 4) * 127));
        if (velIdx < 0) {   // the note nearest the click (with the selection, if it's in it)
            double best = 1e9;
            for (size_t i = 0; i < ge.notes.size(); ++i) { const auto& n = ge.notes[i]; if (n.track != selTrack) continue; const double d = std::fabs(xOf(n.start) - m.position.x); if (d < best && d < 8) { best = d; velIdx = (int) i; } }
            if (velIdx < 0) return;
        }
        if (ge.sel[(size_t) velIdx]) { for (size_t i = 0; i < ge.notes.size(); ++i) if (ge.sel[i]) ge.notes[i].vel = v; }
        else ge.notes[(size_t) velIdx].vel = v;
        dirty = true;
    };
    auto& vh = hit(vl, [this, velSet](const juce::MouseEvent& m) { velIdx = -1; velSet(m); }, velSet);
    vh.up = [this](const juce::MouseEvent&) { if (dirty) gridCommit(); velIdx = -1; };
    // the whole song, this track: click or drag to move the view
    juce::Rectangle<float> ov(gx, vl.getBottom() + 4, gw, ovH);
    const double len = std::max(4.0, s.length);
    g.setColour(juce::Colour(0xff041a0c)); g.fillRect(ov);
    g.setColour(kDim); for (const auto& n : ge.notes) if (n.track == selTrack) g.fillRect(ov.getX() + (float) (n.start / len) * gw, ov.getY() + 2, std::max(1.f, (float) (n.len / len) * gw), ovH - 4);
    g.setColour(juce::Colours::white); g.drawRect(ov.getX() + (float) (viewStart / len) * gw, ov.getY(), std::max(3.f, (float) (viewBeats / len) * gw), ovH, 1.f);
    g.fillRect(ov.getX() + (float) (beatNow / len) * gw, ov.getY(), 1.f, ovH);
    auto ovSet = [this, ov, len, gw](const juce::MouseEvent& m) { viewStart = std::max(0.0, (m.position.x - ov.getX()) / gw * len - viewBeats / 2); gridFollow = false; };
    hit(ov, ovSet, ovSet);
}

// the grid page: track chips, tools, the roll, the edit buttons
void Screen::gridPage(juce::Graphics& g) {
    auto& eng = ed.proc.engine; auto& kit = ed.proc.kit;
    SongEntry* entry = ed.proc.songs.at(ed.proc.currentSong());
    const MidiSong* s = eng.song.load();
    if (!entry || !s) return;
    // track chips
    for (int t = 0; t < SONG_TRK; ++t) {
        juce::Rectangle<float> r(20.f + t * 60.f, 146, 58, 20);
        const int v = eng.songVoice[t]; const Sound* snd = v == GM_KIT ? nullptr : kit.bank.at(v);
        const juce::String nm = !s->used[t] ? juce::String("--") : v == GM_KIT ? juce::String("KIT") : snd ? juce::String(snd->name).substring(0, 5) : juce::String("?");
        if (t == selTrack) { g.setColour(kInk); g.fillRect(r); g.setColour(kBg); } else g.setColour(s->used[t] ? kInk : kDim);
        g.setFont(mono(11)); g.drawText(juce::String(t + 1) + " " + nm, r, juce::Justification::centred);
        if (t != selTrack) { g.setColour(kFaint); g.drawRect(r, 1.f); }
        hit(r, [this, t](const juce::MouseEvent& m) { selTrack = t; if (m.mods.isPopupMenu() || m.getNumberOfClicks() > 1) soundMenu([this, t](int idx) { ed.proc.setSongVoice(t, idx); }); });
    }
    // tools
    const char* tools[3] = { "DRAW", "SELECT", "ERASE" };
    for (int k = 0; k < 3; ++k) button(g, { 20.f + k * 74, 172, 70, 20 }, tools[k], gridTool == k, [this, k] { gridTool = k; });
    const char* sn = snap <= 0 ? "OFF" : snap >= 4 ? "BAR" : snap >= 1 ? "1/4" : snap >= 0.5 ? "1/8" : snap >= 0.25 ? "1/16" : "1/32";
    button(g, { 250, 172, 110, 20 }, juce::String("SNAP ") + sn, false, [this] { snap = snap <= 0 ? 4 : snap >= 4 ? 1 : snap >= 1 ? 0.5 : snap >= 0.5 ? 0.25 : snap >= 0.25 ? 0.125 : 0; });
    button(g, { 366, 172, 92, 20 }, "ROWS " + juce::String(gridRows), false, [this] { gridRows = gridRows == 24 ? 36 : gridRows == 36 ? 48 : 24; });
    button(g, { 464, 172, 40, 20 }, "-", false, [this] { viewBeats = std::min(512.0, viewBeats * 1.5); });
    button(g, { 508, 172, 40, 20 }, "+", false, [this] { viewBeats = std::max(2.0, viewBeats / 1.5); });
    button(g, { 554, 172, 92, 20 }, "FOLLOW", gridFollow, [this] { gridFollow = !gridFollow; });
    button(g, { 652, 172, 92, 20 }, "OTHERS", gridGhost, [this] { gridGhost = !gridGhost; });
    button(g, { 750, 172, 36, 20 }, "<", false, [this] { viewStart = std::max(0.0, viewStart - viewBeats / 2); gridFollow = false; });
    button(g, { 790, 172, 36, 20 }, ">", false, [this] { viewStart += viewBeats / 2; gridFollow = false; });
    button(g, { 832, 172, 32, 20 }, "^", false, [this] { pitchLow = juce::jlimit(0, 127 - gridRows, pitchLow + 12); });
    button(g, { 868, 172, 32, 20 }, "v", false, [this] { pitchLow = juce::jlimit(0, 127 - gridRows, pitchLow - 12); });
    button(g, { 906, 172, 74, 20 }, "TRACKS", false, [this] { songGrid = false; });
    drawGrid(g, { 20, 214, (float) W - 40, 326 });
    // edits: on the selection, or the whole track when nothing is selected
    const float by = (float) H - 92;
    struct B { const char* t; std::function<void()> f; };
    auto op = [this](std::function<void()> f, const char* msg) { return [this, f, msg] { f(); gridCommit(); status = msg; }; };
    const std::vector<B> bs = {
        { "UNDO", [this, entry] { if (undoSong(ed.proc.engine, entry)) status = "UNDONE"; } },
        { "ALL", [this] { ge.selectAll(selTrack); } },
        { "DEL", op([this] { if (!ge.any()) ge.selectAll(selTrack); ge.erase(); }, "DELETED") },
        { "COPY", [this] { ge.copy(); status = juce::String((int) GridEdit::clipboard().size()) + " NOTES COPIED"; } },
        { "PASTE", op([this] { const double b = ed.proc.engine.songBeat.load(); ge.paste(selTrack, std::max(0.0, snap > 0 ? std::round((b >= viewStart && b <= viewStart + viewBeats ? b : viewStart) / snap) * snap : viewStart)); }, "PASTED") },
        { "DUP", op([this] { ge.duplicate(snap); }, "DUPLICATED") },
        { "-12", op([this] { ge.transpose(selTrack, -12); }, "OCTAVE DOWN") }, { "-1", op([this] { ge.transpose(selTrack, -1); }, "DOWN") },
        { "+1", op([this] { ge.transpose(selTrack, 1); }, "UP") }, { "+12", op([this] { ge.transpose(selTrack, 12); }, "OCTAVE UP") },
        { "QUANT", op([this] { ge.quantize(selTrack, snap > 0 ? snap : 0.25); }, "QUANTIZED") },
        { "LEGATO", op([this] { ge.legato(selTrack); }, "LEGATO") },
        { "HUMAN", op([this] { ge.humanize(selTrack, 0.025, 12, juce::Random::getSystemRandom().nextInt()); }, "HUMANIZED: A LITTLE EARLY / LATE, SOFT / HARD") },
        { "VEL-", op([this] { ge.velocity(selTrack, -10); }, "SOFTER") }, { "VEL+", op([this] { ge.velocity(selTrack, 10); }, "LOUDER") },
    };
    for (size_t k = 0; k < bs.size(); ++k) button(g, { 20.f + k * 64.f, by, 61, 22 }, bs[k].t, false, bs[k].f);
    const int ns = ge.selected();
    if (ns) { g.setColour(juce::Colours::white); g.setFont(mono(11)); g.drawText(juce::String(ns) + " SELECTED", 830, 196, 150, 14, juce::Justification::centredRight); }
}

bool Screen::keyPressed(const juce::KeyPress& k) {
    if (!(page == 'S' && songGrid && ed.proc.engine.song.load())) return false;
    SongEntry* e = ed.proc.songs.at(ed.proc.currentSong()); if (!e) return false;
    const auto c = k.getKeyCode(); const bool cmd = k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown(), sh = k.getModifiers().isShiftDown();
    bool edit = true;
    if (c == juce::KeyPress::deleteKey || c == juce::KeyPress::backspaceKey) { if (!ge.any()) return true; ge.erase(); }
    else if (cmd && (c == 'Z' || c == 'z')) { undoSong(ed.proc.engine, e); edit = false; }
    else if (cmd && (c == 'A' || c == 'a')) { ge.selectAll(selTrack); edit = false; }
    else if (cmd && (c == 'C' || c == 'c')) { ge.copy(); edit = false; }
    else if (cmd && (c == 'V' || c == 'v')) ge.paste(selTrack, snap > 0 ? std::round(viewStart / snap) * snap : viewStart);
    else if (cmd && (c == 'D' || c == 'd')) ge.duplicate(snap);
    else if (c == juce::KeyPress::upKey) ge.transpose(selTrack, sh ? 12 : 1);
    else if (c == juce::KeyPress::downKey) ge.transpose(selTrack, sh ? -12 : -1);
    else if (c == juce::KeyPress::leftKey) ge.move(selTrack, -(snap > 0 ? snap : 0.25) * (sh ? 4 : 1));
    else if (c == juce::KeyPress::rightKey) ge.move(selTrack, (snap > 0 ? snap : 0.25) * (sh ? 4 : 1));
    else if (!cmd && (c == 'D' || c == 'd')) { gridTool = 0; edit = false; }
    else if (!cmd && (c == 'S' || c == 's')) { gridTool = 1; edit = false; }
    else if (!cmd && (c == 'E' || c == 'e')) { gridTool = 2; edit = false; }
    else if (c == juce::KeyPress::escapeKey) { ge.clear(); edit = false; }
    else return false;
    if (edit) gridCommit();
    repaint(); return true;
}

// a MIDI song on Page R: its sixteen tracks, their CMI voices, the whole song at a glance or one track on the grid
void Screen::drawMidiSong(juce::Graphics& g) {
    auto& eng = ed.proc.engine; auto& kit = ed.proc.kit;
    const MidiSong* s = eng.song.load();
    SongEntry* entry = ed.proc.songs.at(ed.proc.currentSong());
    if (!s || !entry) return;
    const bool ext = ed.proc.get("sync") > 0.5f;
    const double beat = eng.songBeat.load(), bpm = s->bpmAt(beat);
    const double barBeats = s->num * 4.0 / s->den;
    g.setColour(kInk); g.setFont(mono(17));
    g.drawRect(20.f, 54.f, (float) W - 40, 46.f, 1.f);
    g.drawText("SONG: " + juce::String(s->name).substring(0, 22), 28, 56, 300, 20, juce::Justification::centredLeft);
    g.fillRect(330.f, 57.f, 240.f, 19.f); g.setColour(kBg); g.drawText("REAL-TIME COMPOSER", 330, 57, 240, 19, juce::Justification::centred); g.setColour(kInk);
    g.drawText("Songs: " + juce::String(ed.proc.songs.count()), 640, 56, 340, 20, juce::Justification::centredRight);
    g.drawText("Speed: " + juce::String(bpm, 1), 28, 78, 180, 20, juce::Justification::centredLeft);
    g.drawText(juce::String("Sync: ") + (ext ? "EXT" : "INT"), 230, 78, 120, 20, juce::Justification::centredLeft);
    hit({ 230, 78, 120, 20 }, [this, ext](const juce::MouseEvent&) { ed.proc.set("sync", ext ? 0.f : 1.f); });
    g.drawText("Bar: " + juce::String(1 + (int) (beat / barBeats)) + " / " + juce::String((int) std::ceil(s->length / barBeats)), 380, 78, 220, 20, juce::Justification::centredLeft);
    g.drawText("Time: " + juce::String(s->num) + "/" + juce::String(s->den), 640, 78, 340, 20, juce::Justification::centredRight);
    g.setFont(mono(26)); g.drawText(juce::String(s->name).substring(0, 30), 28, 106, 520, 36, juce::Justification::centredLeft);
    g.setFont(mono(17));
    const bool loop = eng.songLoop.load();
    button(g, { 560, 112, 110, 24 }, loop ? "LOOP" : "ONCE", loop, [&eng, loop] { eng.songLoop = !loop; });
    button(g, { 680, 112, 140, 24 }, "EXPORT...", false, [this] { exportMenu(); });
    button(g, { 830, 112, 140, 24 }, "SONGS...", false, [this] { songMenu(); });

    if (songGrid) {
        gridPage(g);
        const float by2 = (float) H - 60;
        const bool running = eng.songRun.load();
        button(g, { 28, by2, 100, 24 }, "PLAY", running && !ext, [this, &eng] { ed.proc.set("sync", 0.f); eng.songRun = true; });
        button(g, { 140, by2, 100, 24 }, "STOP", !running, [this, &eng] { if (ed.proc.recording.load()) { status = ed.proc.stopRecording(); geFor = nullptr; } eng.songRun = false; });
        button(g, { 252, by2, 120, 24 }, "FROM VIEW", false, [this, &eng] { (void) eng; status = "PLAY FROM BAR " + juce::String(1 + (int) (viewStart / 4)); ed.proc.set("sync", 0.f); ed.proc.playSongFrom(viewStart); });
        if (!ext) { const MidiSong* sg = eng.song.load(); const double bb = sg ? sg->num * 4.0 / std::max(1, sg->den) : 4.0; recordRow(g, 380, by2, std::max(0.0, std::floor(viewStart / bb) * bb)); }
        button(g, { 775, by2, 195, 24 }, "PAGE S PATTERNS", false, [this] { ed.proc.chooseSong(-1); });
        return;
    }
    const float gy = 152, rh = 23, listW = 360;
    const int nT = SONG_TRK;
    g.drawRect(25.f, gy - 2, listW, rh * nT + 4, 1.f);
    for (int t = 0; t < nT; ++t) {
        const float y = gy + t * rh;
        const bool used = s->used[t], muted = eng.songMute[t];
        if (t == selTrack) { g.setColour(kFaint); g.fillRect(26.f, y, listW - 2, rh); }
        const int v = eng.songVoice[t];
        const Sound* snd = v == GM_KIT ? nullptr : kit.bank.at(v);
        g.setColour(used && !muted ? kInk : kDim); g.setFont(mono(15));
        g.drawText(juce::String(t + 1).paddedLeft('0', 2) + " " + (used ? juce::String(snd ? snd->name : std::string("DRUM KIT")).paddedRight(' ', 9) : juce::String("--       ")), 32, (int) y, 180, (int) rh,
                   juce::Justification::centredLeft);
        g.setFont(mono(12)); g.setColour(kDim);
        g.drawText(juce::String(s->trackName[t]).substring(0, 16) + (eng.songMono[t] && used && v != GM_KIT ? " MONO" : "") + (muted ? " MUTE" : ""), 200, (int) y, 180, (int) rh, juce::Justification::centredLeft);
        hit({ 26, y, listW - 2, rh }, [this, t](const juce::MouseEvent& m) {
            selTrack = t;
            if (m.mods.isPopupMenu() || m.getNumberOfClicks() > 1) soundMenu([this, t](int idx) { ed.proc.setSongVoice(t, idx); });
        });
    }
    juce::Rectangle<float> area(410, gy, 565, rh * nT);
    if (songGrid) drawGrid(g, area);
    else {
        // the whole song: a lane per track
        const float gx = area.getX(), gw = area.getWidth();
        int lo[SONG_TRK], hi[SONG_TRK]; for (int t = 0; t < SONG_TRK; ++t) { lo[t] = 127; hi[t] = 0; }
        for (const auto& e : s->ev) if ((e.status & 0xF0) == 0x90 && e.d2) { lo[e.track] = std::min(lo[e.track], (int) e.d1); hi[e.track] = std::max(hi[e.track], (int) e.d1); }
        const double len = std::max(1.0, s->length);
        for (int t = 0; t < nT; ++t) { if (t == selTrack) { g.setColour(kFaint); g.fillRect(gx, gy + t * rh, gw, rh); } g.setColour(kFaint); g.drawHorizontalLine((int) (gy + (t + 1) * rh), gx, gx + gw); }
        for (const auto& n : notesOf(*s)) {
            const int t = n.track; const float span = (float) std::max(1, hi[t] - lo[t]);
            const float y = gy + t * rh + 3 + (rh - 6) * (1.f - (n.pitch - lo[t]) / span);
            g.setColour(eng.songMute[t] ? kFaint : kDim);
            g.fillRect(gx + (float) (n.start / len) * gw, y - 1, std::max(1.f, (float) (n.len / len) * gw), 2.f);
        }
        g.setColour(juce::Colours::white); g.fillRect(gx + (float) (beat / len) * gw, gy, 1.5f, rh * nT);
        g.setColour(kInk); g.drawRect(area, 1.f);
        hit(area, [this, gy, rh](const juce::MouseEvent& m) { selTrack = juce::jlimit(0, SONG_TRK - 1, (int) ((m.position.y - gy) / rh)); if (m.getNumberOfClicks() > 1) songGrid = true; });
    }

    const float by = (float) H - 92;
    g.setFont(mono(17)); g.setColour(kInk);
    if (!songGrid) {
        const bool mute = eng.songMute[selTrack];
        button(g, { 28, by, 130, 22 }, mute ? "MUTED" : "AUDIBLE", !mute, [this, mute] { ed.proc.setSongMute(selTrack, !mute); });
        button(g, { 168, by, 130, 22 }, "VOICE...", false, [this] { const int t = selTrack; soundMenu([this, t](int idx) { ed.proc.setSongVoice(t, idx); }); });
        const bool haveLib = !ed.proc.catalog.voices.empty();
        button(g, { 308, by, 150, 22 }, haveLib ? "CMI LIBRARY" : "AUTO MATCH", false, [this, entry, haveLib] {
            if (haveLib) { const int n = ed.proc.connectToLibrary(ed.proc.currentSong()); status = juce::String(n) + " TRACKS ON CMI LIBRARY VOICES"; }
            else { autoSongVoices(*entry->song, ed.proc.kit.bank, entry->voice, entry->why); ed.proc.chooseSong(ed.proc.currentSong()); status = "SET A LIBRARY FOLDER (PAGE F) FOR THE ORIGINAL VOICES"; }
        });
        button(g, { 468, by, 130, 22 }, "EDIT GRID", false, [this] { songGrid = true; });
        button(g, { 608, by, 130, 22 }, "DRUM KIT", false, [this] { kitBack = 'S'; page = 'K'; });
        const bool mo = eng.songMono[selTrack];
        if (eng.songVoice[selTrack] != GM_KIT) button(g, { 748, by, 90, 22 }, mo ? "MONO" : "POLY", mo, [this, mo] { ed.proc.setSongMono(selTrack, !mo); status = mo ? "POLY: CHORDS" : "MONO: ONE NOTE AT A TIME"; });
        g.setColour(kDim); g.setFont(mono(12));
        g.drawText(juce::String(entry->why[selTrack]).substring(0, 20), 844, (int) by, 136, 22, juce::Justification::centredLeft);
    } else {
        button(g, { 28, by, 110, 22 }, "UNDO", false, [this, entry] { if (undoSong(ed.proc.engine, entry)) status = "UNDONE"; });
        const char* sn = snap >= 1 ? "1/4" : snap >= 0.5 ? "1/8" : snap >= 0.25 ? "1/16" : "1/32";
        button(g, { 148, by, 130, 22 }, juce::String("SNAP ") + sn, false, [this] { snap = snap >= 1 ? 0.125 : snap * 2; });
        button(g, { 288, by, 80, 22 }, "ZOOM-", false, [this] { viewBeats = std::min(512.0, viewBeats * 1.5); });
        button(g, { 378, by, 80, 22 }, "ZOOM+", false, [this] { viewBeats = std::max(2.0, viewBeats / 1.5); });
        button(g, { 468, by, 130, 22 }, "QUANTIZE", false, [this, entry] {
            auto n = notesOf(*entry->song); for (auto& x : n) if (x.track == selTrack) x.start = std::round(x.start / snap) * snap;
            editSong(ed.proc.engine, entry, n); status = "QUANTIZED";
        });
        button(g, { 608, by, 130, 22 }, "CLEAR TRACK", false, [this, entry] {
            auto n = notesOf(*entry->song); n.erase(std::remove_if(n.begin(), n.end(), [this](const SongNote& x) { return x.track == selTrack; }), n.end());
            editSong(ed.proc.engine, entry, n); status = "TRACK CLEARED (UNDO TO BRING BACK)";
        });
        button(g, { 748, by, 52, 22 }, "<", false, [this] { viewStart = std::max(0.0, viewStart - viewBeats); });
        button(g, { 806, by, 52, 22 }, ">", false, [this] { viewStart += viewBeats; });
        button(g, { 868, by, 102, 22 }, "TRACKS", false, [this] { songGrid = false; });
    }
    const float by2 = (float) H - 60;
    const bool running = eng.songRun.load();
    button(g, { 28, by2, 100, 24 }, "PLAY", running && !ext, [this, &eng] { ed.proc.set("sync", 0.f); eng.songRun = true; });
    button(g, { 140, by2, 100, 24 }, "STOP", !running, [this, &eng] { if (ed.proc.recording.load()) { status = ed.proc.stopRecording(); geFor = nullptr; } eng.songRun = false; });
    if (ext) { g.setColour(kDim); g.setFont(mono(14)); g.drawText("follows the host's transport", 260, (int) by2, 290, 24, juce::Justification::centredLeft); }
    else recordRow(g, 252, by2, 0);
    button(g, { 775, by2, 195, 24 }, "PAGE S PATTERNS", false, [this] { ed.proc.chooseSong(-1); });
}

void Screen::drawComposer(juce::Graphics& g) {
    if (ed.proc.engine.song.load()) { drawMidiSong(g); return; }
    auto& eng = ed.proc.engine; auto& sq = eng.seq; auto& kit = ed.proc.kit;
    const int pat = eng.curPattern.load();
    const bool ext = ed.proc.get("sync") > 0.5f;
    int used = 0; for (auto& p : sq.pat) { bool any = false; for (auto& t : p.st) for (auto& s : t) any |= s.vel > 0; used += any; }
    g.setColour(kInk); g.setFont(mono(17));
    g.drawRect(20.f, 54.f, (float) W - 40, 46.f, 1.f);
    const Sound* ks = kit.bank.at(juce::roundToInt(ed.proc.get("sound")));
    g.drawText("FILE: " + (ks ? juce::String(ks->name) : juce::String("--")) + " .RS", 28, 56, 300, 20, juce::Justification::centredLeft);
    g.fillRect(330.f, 57.f, 240.f, 19.f); g.setColour(kBg); g.drawText("REAL-TIME COMPOSER", 330, 57, 240, 19, juce::Justification::centred); g.setColour(kInk);
    g.drawText("Total patterns: " + juce::String(NPAT), 640, 56, 340, 20, juce::Justification::centredRight);
    const float tempo = ed.proc.get("tempo");
    g.drawText("Speed: " + juce::String(tempo, 1), 28, 78, 180, 20, juce::Justification::centredLeft);
    auto& sp = hit({ 28, 78, 180, 20 }, [](const juce::MouseEvent&) {}, [this, tempo](const juce::MouseEvent& e) { ed.proc.set("tempo", juce::jlimit(40.f, 240.f, tempo - e.getDistanceFromDragStartY() * 0.1f)); });
    sp.wheel = [this, tempo](float dy) { ed.proc.set("tempo", juce::jlimit(40.f, 240.f, tempo + (dy > 0 ? 1.f : -1.f))); };
    g.drawText(juce::String("Sync: ") + (ext ? "EXT" : "INT"), 230, 78, 120, 20, juce::Justification::centredLeft);
    hit({ 230, 78, 120, 20 }, [this, ext](const juce::MouseEvent&) { ed.proc.set("sync", ext ? 0.f : 1.f); });
    const int stepNow = eng.seqStep.load();
    g.drawText("Bar: " + juce::String(stepNow < 0 ? 0 : 1 + eng.seqBar.load()), 380, 78, 120, 20, juce::Justification::centredLeft);
    g.drawText("Used: " + juce::String(used) + " Left: " + juce::String(NPAT - used), 640, 78, 340, 20, juce::Justification::centredRight);

    // pattern number, time, swing
    g.drawText("Pattern:", 28, 110, 110, 30, juce::Justification::centredLeft);
    g.setFont(mono(34)); g.drawText(juce::String(pat + 1), 140, 104, 60, 40, juce::Justification::centred);
    g.setFont(mono(17));
    button(g, { 200, 112, 26, 24 }, "<", false, [&eng, pat] { eng.curPattern = juce::jmax(0, pat - 1); });
    button(g, { 230, 112, 26, 24 }, ">", false, [&eng, pat] { eng.curPattern = juce::jmin(NPAT - 1, pat + 1); });
    g.drawText("Time:", 290, 110, 60, 30, juce::Justification::centredLeft);
    g.setFont(mono(34)); g.drawText("4/4", 350, 104, 80, 40, juce::Justification::centredLeft);
    g.setFont(mono(17));
    const float sw = ed.proc.get("swing");
    g.drawText("Comment: swing " + juce::String(juce::roundToInt(sw * 200)) + "%", 460, 110, 300, 30, juce::Justification::centredLeft);
    auto& swh = hit({ 460, 110, 300, 30 }, [](const juce::MouseEvent&) {}, [this, sw](const juce::MouseEvent& e) { ed.proc.set("swing", juce::jlimit(0.f, 0.5f, sw - e.getDistanceFromDragStartY() * 0.002f)); });
    swh.wheel = [this, sw](float dy) { ed.proc.set("swing", juce::jlimit(0.f, 0.5f, sw + (dy > 0 ? 0.01f : -0.01f))); };
    const bool songOn = ed.proc.get("songMode") > 0.5f;
    button(g, { 800, 112, 170, 24 }, songOn ? "SONG MODE ON" : "SONG MODE OFF", songOn, [this, songOn] { ed.proc.set("songMode", songOn ? 0.f : 1.f); });

    const float gx = 410, gw = 565, gy = 152, rh = 46;
    if (!songEditor) {
        // track names
        g.drawRect(25.f, gy - 2, 360.f, rh * TRACKS + 32, 1.f);
        for (int t = 0; t < TRACKS; ++t) {
            const float y = gy + t * rh;
            const Sound* s = kit.bank.at(sq.trk[t].audible ? sq.trk[t].sound : sq.trk[t].sound);
            if (t == selTrack) { g.setColour(kFaint); g.fillRect(26.f, y, 358.f, rh); }
            g.setColour(sq.trk[t].audible ? kInk : kDim); g.setFont(mono(19));
            g.drawText(s ? juce::String(s->name) : juce::String("--"), 48, (int) y, 300, (int) rh, juce::Justification::centredLeft);
            if (!sq.trk[t].audible) g.drawText("MUTE", 290, (int) y, 80, (int) rh, juce::Justification::centredRight);
            hit({ 26, y, 358, rh }, [this, t](const juce::MouseEvent& e) {
                selTrack = t;
                if (e.mods.isPopupMenu()) soundMenu([this, t](int idx) { ed.proc.engine.seq.trk[t].sound = idx; });
                else if (e.getNumberOfClicks() > 1) soundMenu([this, t](int idx) { ed.proc.engine.seq.trk[t].sound = idx; });
                else ed.proc.preview(t);
            });
            g.setColour(kInk); g.setFont(mono(34));
            g.drawText(juce::String(t + 1), 385, (int) y, 24, (int) rh, juce::Justification::centred);
        }
        g.setFont(mono(17)); g.drawText("Voices", 48, (int) (gy + rh * TRACKS + 4), 200, 24, juce::Justification::centredLeft);
        // the grid
        g.setColour(kInk); g.drawRect(gx, gy - 2, gw, rh * TRACKS + 2, 1.f);
        for (int t = 0; t <= TRACKS; ++t) g.drawHorizontalLine((int) (gy + t * rh), gx, gx + gw);
        const float cw = gw / STEPS;
        const bool playingHere = stepNow >= 0 && eng.seqPat.load() == pat;
        if (playingHere) { g.setColour(kFaint); g.fillRect(gx + stepNow * cw, gy, cw, rh * TRACKS); }
        for (int s = 0; s < STEPS; s += 4) {
            g.setColour(kInk);
            juce::Path tri; const float x = gx + s * cw + 4;
            tri.addTriangle(x, gy + rh * TRACKS + 14, x + 6, gy + rh * TRACKS + 4, x + 12, gy + rh * TRACKS + 14);
            g.fillPath(tri);
        }
        for (int t = 0; t < TRACKS; ++t)
            for (int s = 0; s < STEPS; ++s) {
                const PStep& st = sq.pat[pat].st[t][s];
                juce::Rectangle<float> cell(gx + s * cw, gy + t * rh, cw, rh);
                if (st.vel) {
                    noteGlyph(g, cell.getX() + 6, cell.getY(), rh, st.vel, playingHere && s == stepNow);
                    if (st.note != 0) { g.setColour(kDim); g.setFont(mono(12)); g.drawText((st.note > 0 ? "+" : "") + juce::String(st.note), cell.withTrimmedLeft(14).withTrimmedTop(rh - 18), juce::Justification::centredLeft); }
                }
                auto& h = hit(cell, [this, t, s, pat](const juce::MouseEvent& e) {
                    auto& c = ed.proc.engine.seq.pat[pat].st[t][s];
                    selTrack = t;
                    if (e.mods.isPopupMenu()) { c.vel = c.vel == 0 ? 127 : c.vel >= 120 ? 60 : c.vel >= 80 ? 127 : 100; if (c.vel == 60) c.vel = 60; return; }
                    if (c.vel) c = {}; else c = { 100, 0 };
                });
                h.wheel = [this, t, s, pat](float dy) { auto& c = ed.proc.engine.seq.pat[pat].st[t][s]; if (c.vel) c.note = (int8_t) juce::jlimit(-24, 24, c.note + (dy > 0 ? 1 : -1)); };
            }
    } else {
        // the song: up to 64 slots of pattern numbers
        g.setColour(kInk); g.setFont(mono(17));
        g.drawText("SONG: " + juce::String(sq.songLen) + " BARS   click: next pattern, right-click: previous", 28, (int) gy, 700, 24, juce::Justification::centredLeft);
        for (int i = 0; i < SONG_MAX; ++i) {
            juce::Rectangle<float> r(28.f + (i % 16) * 59.f, gy + 34 + (i / 16) * 70.f, 54, 60);
            const bool inSong = i < sq.songLen;
            const bool now = songOn && stepNow >= 0 && inSong && i == eng.seqBar.load();
            g.setColour(inSong ? kInk : kFaint); g.drawRect(r, 1.f);
            if (inSong) { g.setFont(mono(28)); g.setColour(now ? juce::Colours::white : kInk); g.drawText(juce::String(sq.song[i] + 1), r, juce::Justification::centred); }
            hit(r, [this, i](const juce::MouseEvent& e) {
                auto& q = ed.proc.engine.seq;
                if (i >= q.songLen) { q.songLen = i + 1; return; }
                q.song[i] = (q.song[i] + (e.mods.isPopupMenu() ? NPAT - 1 : 1)) % NPAT;
            });
        }
        button(g, { 28, gy + 320, 160, 24 }, "LENGTH -", false, [this] { auto& q = ed.proc.engine.seq; q.songLen = juce::jmax(1, q.songLen - 1); });
        button(g, { 200, gy + 320, 160, 24 }, "LENGTH +", false, [this] { auto& q = ed.proc.engine.seq; q.songLen = juce::jmin(SONG_MAX, q.songLen + 1); });
    }

    // bottom rows
    const float by = (float) H - 92;
    g.setColour(kInk); g.setFont(mono(17));
    const bool aud = sq.trk[selTrack].audible;
    g.drawText(juce::String("Audible: ") + (aud ? "ON" : "OFF"), 28, (int) by, 170, 22, juce::Justification::centredLeft);
    hit({ 28, by, 170, 22 }, [this, aud](const juce::MouseEvent&) { ed.proc.engine.seq.trk[selTrack].audible = !aud; });
    auto& P = sq.pat[pat];
    button(g, { 230, by, 110, 22 }, "INSERT", false, [this, pat] { auto& q = ed.proc.engine.seq; if (pat + 1 < NPAT) { q.pat[pat + 1] = q.pat[pat]; ed.proc.engine.curPattern = pat + 1; status = "COPIED"; } });
    button(g, { 350, by, 110, 22 }, "DELETE", false, [this, &P] { for (auto& s : P.st[selTrack]) s = {}; });
    button(g, { 470, by, 110, 22 }, "ZERO", false, [&P] { for (auto& t : P.st) for (auto& s : t) s = {}; });
    button(g, { 590, by, 110, 22 }, "FILL", false, [this, &P] { int k = 0; for (auto& s : P.st[selTrack]) s = { (uint8_t) (k++ % 2 ? 80 : 110), 0 }; });
    button(g, { 710, by, 110, 22 }, "HALF", false, [this, &P] { int k = 0; for (auto& s : P.st[selTrack]) { if (k % 2) s = {}; ++k; } });
    g.setColour(kDim); g.setFont(mono(13));
    g.drawText("click: note  right-click: accent  wheel: pitch", 830, (int) by - 2, 160, 26, juce::Justification::centredLeft);
    const float by2 = (float) H - 60;
    const bool running = ext ? eng.seqStep.load() >= 0 : eng.seqRun.load();
    button(g, { 28, by2, 100, 24 }, "PLAY", running && !ext, [this] { ed.proc.seqPlay(true); ed.proc.set("sync", 0.f); });
    button(g, { 140, by2, 100, 24 }, "STOP", !running, [this] { ed.proc.seqPlay(false); });
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("editor:", 560, (int) by2, 90, 24, juce::Justification::centredRight);
    button(g, { 655, by2, 110, 24 }, "PATTERN", !songEditor, [this] { songEditor = false; });
    button(g, { 775, by2, 90, 24 }, "SONG", songEditor, [this] { songEditor = true; });
    button(g, { 875, by2, 95, 24 }, "MIDI...", false, [this] { songMenu(); });
    button(g, { 252, by2, 150, 24 }, "REC NEW SONG", false, [this] { ed.proc.recFromBeat = 0; selTrack = 0; status = ed.proc.startRecording(0); });
    button(g, { 655, (float) H - 122, 150, 24 }, "EXPORT WAV", false, [this] { exportMenu(); });
    if (ext) { g.setColour(kDim); g.setFont(mono(14)); g.drawText("follows the host's transport", 260, (int) by2, 290, 24, juce::Justification::centredLeft); }
}

void Screen::soundMenu(std::function<void(int)> chosen) {
    juce::PopupMenu m; std::map<std::string, juce::PopupMenu> cats; std::vector<std::string> order;
    auto& kit = ed.proc.kit;
    for (int i = 0; i < kit.bank.count.load(); ++i) {
        if (isBlank(kit, i)) continue;
        const Sound* s = kit.bank.at(i); if (!s) continue;
        if (!cats.count(s->category)) order.push_back(s->category);
        cats[s->category].addItem(i + 1, juce::String(s->name) + "  " + juce::String(s->title).substring(0, 40));
    }
    if (!order.empty()) { juce::PopupMenu loaded; for (auto& c : order) loaded.addSubMenu(c, cats[c]); m.addSubMenu("Loaded voices", loaded); }
    const bool songTrack = page == 'S' && ed.proc.engine.song.load();
    if (songTrack) m.addItem(10000, "Drum kit (a voice per GM drum note)");
    {
        const juce::ScopedLock l(ed.proc.catLock);
        const auto& cv = ed.proc.catalog.voices;
        if (!cv.empty()) {
            std::map<std::string, juce::PopupMenu> byDisk;
            for (size_t i = 0; i < cv.size() && i < 9000; ++i) byDisk[cv[i].disk].addItem(20000 + (int) i, juce::String(cv[i].name) + (cv[i].looped ? "  (loop)" : ""));
            juce::PopupMenu lib; for (auto& kv : byDisk) lib.addSubMenu(juce::String(kv.first), kv.second);
            m.addSeparator(); m.addSubMenu("CMI library (your disks)", lib);
        } else m.addItem(-1, "No CMI library: set your library folder on Page F", false);
    }
    m.showMenuAsync(juce::PopupMenu::Options(), [this, chosen, songTrack](int r) {
        if (r >= 20000) {
            if (songTrack) { ed.proc.useCatalogVoice(selTrack, r - 20000); repaint(); return; }
            const int v = ed.proc.loadCatalogVoice(r - 20000);
            if (v >= 0 && chosen) chosen(v); else if (v < 0) status = "USER VOICES FULL";
            repaint(); return;
        }
        if (r == 10000 && chosen) chosen(GM_KIT); else if (r > 0 && r < 10000 && chosen) chosen(r - 1);
    });
}

void Screen::command(const juce::String& raw) {
    const auto c = raw.trim().toUpperCase();
    auto words = juce::StringArray::fromTokens(c, " ", "");
    if (words.isEmpty()) return;
    const auto w0 = words[0], arg = words.size() > 1 ? words[1] : juce::String();
    auto& eng = ed.proc.engine;
    if (w0 == "PLAY") { ed.proc.seqPlay(true); ed.proc.set("sync", 0.f); page = 'S'; }
    else if (w0 == "STOP") ed.proc.seqPlay(false);
    else if (w0 == "PAGE" || (w0.length() == 1 && juce::String("27D58FMRSE").contains(w0))) { const auto p = w0 == "PAGE" ? arg : w0; if (p.isNotEmpty() && juce::String("27D58FMRSE").contains(p.substring(0, 1))) page = p[0]; }
    else if (w0 == "LOAD" || w0 == "VOICE") {
        int idx = findVoice(ed.proc.kit.bank, arg.toStdString());
        if (idx < 0) { const juce::ScopedLock l(ed.proc.catLock); idx = loadCatalogNamed(ed.proc.kit, ed.proc.catalog, arg.toStdString()); }
        if (idx >= 0) ed.proc.loadSound(idx); else { status = "NO VOICE " + arg; repaint(); return; }
    }
    else if (w0 == "PRESET") {
        int n = arg.getIntValue() - 1;
        if (!arg.containsOnly("0123456789")) { n = -1; const auto& ps = factoryPresets(); for (int i = 0; i < (int) ps.size(); ++i) if (arg == ps[(size_t) i].name) { n = i; break; } }
        if (!ed.proc.loadPreset(n)) { status = "PRESET NOT IN YOUR LIBRARY"; repaint(); return; }
        page = 'R';
    }
    else if (w0 == "SPEED" || w0 == "TEMPO") ed.proc.set("tempo", (float) juce::jlimit(40.0, 240.0, arg.getDoubleValue()));
    else if (w0 == "PATTERN") eng.curPattern = juce::jlimit(0, NPAT - 1, arg.getIntValue() - 1);
    else if (w0 == "SONG") ed.proc.set("songMode", arg == "OFF" ? 0.f : 1.f);
    else if (w0 == "SYNC") ed.proc.set("sync", arg == "INT" ? 0.f : 1.f);
    else { status = "?SYNTAX ERROR"; repaint(); return; }
    status = "READY"; repaint();
}

// ---------------------------------------------------------------------------------------------------------------
// the cream monitor around the screen
static void paintBezel(juce::Graphics& g, float w, float h) {
    juce::Rectangle<float> r(0, 0, w, h);
    g.setColour(juce::Colour(0xff151515)); g.fillRect(r);
    g.setGradientFill(juce::ColourGradient(kCream, 0, 0, kCreamDark, 0, h, false));
    g.fillRoundedRectangle(r.reduced(6), 22);
    g.setColour(juce::Colour(0xff0a0a0a)); g.fillRoundedRectangle(juce::Rectangle<float>(34, 26, 1032, 672), 40);
    g.setColour(juce::Colour(0xff22201a)); g.fillRoundedRectangle(juce::Rectangle<float>(44, 704, 1012, 102), 6);
    g.setColour(juce::Colour(0xff6a6656)); g.setFont(mono(16));
    g.drawText("OMEGA  CMI", (int) w - 260, (int) h - 66, 200, 20, juce::Justification::centredRight);
    g.setFont(mono(11, false));
    g.drawText("CHANNEL CARD MODEL  SERIES II", (int) w - 300, (int) h - 44, 240, 14, juce::Justification::centredRight);
    g.drawText("light pen: mouse   keyboard: MIDI or click", 60, (int) h - 52, 400, 14, juce::Justification::centredLeft);
}

CmiEditor::CmiEditor(CmiProcessor& p)
    : juce::AudioProcessorEditor(p), proc(p), screen(*this), keys(p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard) {
    addAndMakeVisible(screen);
    screen.addAndMakeVisible(cmdLine);
    cmdLine.setFont(mono(16)); cmdLine.setColour(juce::TextEditor::backgroundColourId, kInk); cmdLine.setColour(juce::TextEditor::textColourId, kBg);
    cmdLine.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack); cmdLine.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    cmdLine.setColour(juce::CaretComponent::caretColourId, kBg);
    cmdLine.setBounds(112, 28, Screen::W - 126, 20);
    cmdLine.setTextToShowWhenEmpty("type: PRESET 5  LOAD ORCH5  PLAY  STOP  SPEED 110  PAGE S", kBg.withAlpha(0.55f));
    cmdLine.onReturnKey = [this] { screen.command(cmdLine.getText()); cmdLine.clear(); };
    addAndMakeVisible(keys);
    keys.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, kCream);
    keys.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff202020));
    keys.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, kInk.withAlpha(0.7f));
    keys.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, kInk.withAlpha(0.25f));
    keys.setAvailableRange(24, 108);
    keys.setLowestVisibleKey(36);
    keys.setOctaveForMiddleC(4);
    look.setColour(juce::PopupMenu::backgroundColourId, kBg);
    look.setColour(juce::PopupMenu::textColourId, kInk);
    look.setColour(juce::PopupMenu::highlightedBackgroundColourId, kInk);
    look.setColour(juce::PopupMenu::highlightedTextColourId, kBg);
    setLookAndFeel(&look);
    setResizable(true, true);
    setResizeLimits(550, 440, 2200, 1760);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(1100.0 / 880.0);
    setSize(1100, 880);
    startTimerHz(25);
}

CmiEditor::~CmiEditor() { setLookAndFeel(nullptr); }

void CmiEditor::paint(juce::Graphics& g) {
    const float s = getWidth() / 1100.f;
    g.addTransform(juce::AffineTransform::scale(s));
    paintBezel(g, 1100, 880);
}

void CmiEditor::resized() {
    const float s = getWidth() / 1100.f;
    screen.setTransform(juce::AffineTransform::translation(50, 42).scaled(s));
    keys.setTransform(juce::AffineTransform::translation(50, 710).scaled(s));
    keys.setBounds(0, 0, 1000, 90);
    keys.setKeyWidth(1000.f / 50.f);
}

void CmiEditor::timerCallback() { screen.repaint(); }

bool CmiEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (auto& f : files) { const juce::File x(f); if (x.isDirectory() || x.hasFileExtension("wav;aif;aiff;flac;mp3;ogg;vc;imd;img;bin;raw;dsk;hfe;mid;midi;zip")) return true; }
    return false;
}

void CmiEditor::filesDropped(const juce::StringArray& files, int, int) {
    juce::Array<juce::File> items; for (auto& f : files) items.add(juce::File(f));
    if (screen.page == 'P') { screen.status = proc.padImport(items, false); screen.repaint(); return; }   // samples onto the pads
    int n = 0;
    screen.status = proc.importFiles(items, screen.rootKey, screen.loopIt, n);
    screen.page = proc.engine.song.load() && n == 0 ? 'S' : '8'; screen.repaint();
}

juce::AudioProcessorEditor* CmiProcessor::createEditor() { return new CmiEditor(*this); }

}  // namespace cmi
