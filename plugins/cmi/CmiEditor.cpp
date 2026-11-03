#include "CmiEditor.h"

namespace cmi {

static const juce::Colour kInk(0xff4dffa8), kDim(0xff1f8a5a), kFaint(0xff0f3d28), kBg(0xff020a06), kCream(0xffd9d3bc), kCreamDark(0xffb9b29a);
static juce::Font mono(float h, bool bold = true) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }

static juce::String noteName(int n) { static const char* nm[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(nm[((n % 12) + 12) % 12]) + juce::String(n / 12 - 1); }

// ---------------------------------------------------------------------------------------------------------------
Screen::Screen(CmiEditor& e) : ed(e) { setSize(W, H); setWantsKeyboardFocus(false); }

Screen::Hit& Screen::hit(juce::Rectangle<float> r, std::function<void(const juce::MouseEvent&)> down, std::function<void(const juce::MouseEvent&)> drag) {
    hits.push_back({ r, std::move(down), std::move(drag), {} });
    return hits.back();
}

void Screen::mouseDown(const juce::MouseEvent& e) {
    active = -1;
    for (int i = (int) hits.size() - 1; i >= 0; --i)
        if (hits[(size_t) i].r.contains(e.position)) { active = i; if (hits[(size_t) i].down) hits[(size_t) i].down(e); break; }
    repaint();
}
void Screen::mouseDrag(const juce::MouseEvent& e) { if (active >= 0 && active < (int) hits.size() && hits[(size_t) active].drag) { hits[(size_t) active].drag(e); repaint(); } }
void Screen::mouseUp(const juce::MouseEvent&) { active = -1; }
void Screen::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    for (int i = (int) hits.size() - 1; i >= 0; --i)
        if (hits[(size_t) i].r.contains(e.position) && hits[(size_t) i].wheel) { hits[(size_t) i].wheel(w.deltaY); repaint(); return; }
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
        default: drawComposer(g); break;
    }
    // page index along the bottom
    const char* pages[] = { "2", "7", "D", "8", "R" };
    const char* names[] = { "LIBRARY", "CONTROL", "WAVEFORM", "SAMPLING", "COMPOSER" };
    float x = 20;
    for (int i = 0; i < 5; ++i) {
        const char p = pages[i][0];
        juce::Rectangle<float> r(x, (float) H - 26, 180, 22);
        button(g, r, juce::String("PAGE ") + pages[i] + " " + names[i], page == p, [this, p] { page = p; status = "READY"; repaint(); });
        x += 194;
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

// ---- Page 2: the disk directory ----
void Screen::drawLibrary(juce::Graphics& g) {
    auto& kit = ed.proc.kit;
    const int n = kit.bank.count.load(), cur = juce::roundToInt(ed.proc.get("sound"));
    const bool all = ed.proc.showFactory.load();
    std::vector<int> list;
    for (int i = 0; i < n; ++i) {
        const bool user = i >= Library::get().builtIn;
        if (user ? kit.userLoaded(i - Library::get().builtIn) : all) list.push_back(i);
    }
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("DISK DIRECTORY:  " + juce::String((int) list.size()) + " VOICES    CLICK TO LOAD THE KEYBOARD", 20, 58, 640, 20, juce::Justification::centredLeft);
    button(g, { 680, 56, 140, 22 }, "ALL", all, [this] { ed.proc.showFactory = true; libScroll = 0; });
    button(g, { 830, 56, 140, 22 }, "MINE ONLY", !all, [this] { ed.proc.showFactory = false; libScroll = 0; });
    g.drawHorizontalLine(82, 20, (float) W - 20);
    const int rows = 22, colW = 320, per = rows * 3;
    const int pages = std::max(1, ((int) list.size() + per - 1) / per);
    libScroll = juce::jlimit(0, pages - 1, libScroll);
    if (list.empty()) {
        g.setColour(kDim); g.drawText("No user voices yet: import your sounds on Page 8.", 20, 120, W - 40, 20, juce::Justification::centredLeft);
    }
    for (int k = 0; k < per && libScroll * per + k < (int) list.size(); ++k) {
        const int i = list[(size_t) (libScroll * per + k)];
        const Sound* s = kit.bank.at(i); if (!s) continue;
        const int c = k / rows, r = k % rows;
        juce::Rectangle<float> rr(20.f + c * colW, 88.f + r * 20.f, (float) colW - 12, 19);
        if (i == cur) { g.setColour(kInk); g.fillRect(rr); g.setColour(kBg); } else g.setColour(kInk);
        g.setFont(mono(16));
        juce::String cat(s->category); cat = cat.substring(0, 10);
        g.drawText(juce::String(i + 1).paddedLeft('0', 3) + " " + juce::String(s->name).paddedRight(' ', 9) + (s->mode == Mode::Waveform ? ".WF " : ".VC ") + cat, rr.reduced(4, 0), juce::Justification::centredLeft);
        hit(rr, [this, i](const juce::MouseEvent&) { ed.proc.loadSound(i); status = "LOADED"; });
    }
    if (pages > 1) {
        button(g, { (float) W - 160, (float) H - 104, 40, 20 }, "<", false, [this] { libScroll = juce::jmax(0, libScroll - 1); });
        g.setColour(kInk); g.setFont(mono(15));
        g.drawText(juce::String(libScroll + 1) + "/" + juce::String(pages), W - 116, H - 104, 52, 20, juce::Justification::centred);
        button(g, { (float) W - 60, (float) H - 104, 40, 20 }, ">", false, [this, pages] { libScroll = juce::jmin(pages - 1, libScroll + 1); });
    }
    if (const Sound* s = kit.bank.at(cur)) {
        g.setColour(kDim); g.drawHorizontalLine(H - 82, 20, (float) W - 20);
        g.setColour(kInk); g.setFont(mono(16));
        g.drawText(juce::String(s->name) + ": " + juce::String(s->title), 20, H - 78, W - 40, 20, juce::Justification::centredLeft);
        g.setColour(kDim);
        g.drawText("rate " + juce::String(juce::roundToInt(s->rate)) + " Hz   root " + noteName(s->root) + "   " + juce::String(s->pages()) + " pages (" + juce::String(s->ram.size()) +
                   " bytes)   loop " + (s->loopStart >= 0 ? juce::String(s->loopStart) + "-" + juce::String(s->loopEnd) : juce::String("none")), 20, H - 58, W - 40, 18,
                   juce::Justification::centredLeft);
    }
}

// ---- Page 7: control parameters as bar graphs ----
void Screen::bar(juce::Graphics& g, juce::Rectangle<float> r, const char* id, const juce::String& label, std::function<juce::String(float)> fmt) {
    const PDef* d = nullptr; for (const auto& p : paramDefs()) if (!std::strcmp(p.id, id)) d = &p;
    if (!d) return;
    const float v = ed.proc.get(id), u = (v - d->min) / (d->max - d->min);
    const bool toggle = d->step == 1 && d->min == 0 && d->max == 1;
    g.setColour(kInk); g.setFont(mono(16));
    g.drawText(label, r.withWidth(170), juce::Justification::centredLeft);
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
    };
    const std::vector<Row> right = {
        { "poly", "VOICES", fi }, { "bend", "BEND RANGE", fi }, { "master", "MASTER", f2 }, { "hiss", "NOISE FLOOR", f2 }, { "spread", "SPREAD", f2 },
        { "multi", "MULTI MIDI", {} }, { "sync", "SYNC EXT", {} }, { "tempo", "SPEED", [](float v) { return juce::String(v, 1); } },
        { "swing", "SWING", [](float v) { return juce::String(juce::roundToInt(v * 200)) + "%"; } }, { "songMode", "SONG MODE", {} },
    };
    for (size_t i = 0; i < left.size(); ++i) bar(g, { 20.f, 88.f + i * 30.f, 470, 26 }, left[i].id, left[i].label, left[i].fmt);
    for (size_t i = 0; i < right.size(); ++i) bar(g, { 510.f, 88.f + i * 30.f, 470, 26 }, right[i].id, right[i].label, right[i].fmt);
    g.setColour(kDim); g.setFont(mono(14));
    g.drawMultiLineText("Filter: the SSM2045 latch; the card adds the octave\nregister, so it follows the keyboard octave by octave.\n"
                        "Attack / damping: the envelope counter's clock.\nSegment: waveform voices step one segment per time.\n"
                        "Multi MIDI: channels 2-9 play Page R tracks 1-8.", 520, 412, 460);
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
    for (int c = 0; c < NCH; ++c) {
        const auto v = ed.proc.engine.view(c);
        juce::Rectangle<float> r(790.f + (c % 8) * 24.f, 384.f + (c / 8) * 92.f, 18, 80);
        g.setColour(kFaint); g.fillRect(r);
        if (v.state != 0) { g.setColour(c < KEY_CH ? kInk : kDim); const float lv = v.env / 255.f; g.fillRect(r.withTrimmedTop(r.getHeight() * (1 - lv))); }
    }
    g.setColour(kDim); g.setFont(mono(13));
    g.drawText("KEYBOARD 1-8", 790, 466, 200, 14, juce::Justification::centredLeft);
    g.drawText("PAGE R TRACKS 1-8", 790, 558, 200, 14, juce::Justification::centredLeft);
}

// ---- Page 8: sampling into the user voices ----
void Screen::drawSampling(juce::Graphics& g) {
    auto& kit = ed.proc.kit;
    constexpr int per = 16;
    const int pages = (Library::USER_SLOTS + per - 1) / per;
    userPage = juce::jlimit(0, pages - 1, userPage);
    g.setColour(kInk); g.setFont(mono(17));
    g.drawText("DISKS & SAMPLING   " + juce::String(kit.loadedCount()) + " OF " + juce::String(Library::USER_SLOTS) + " USER VOICES", 20, 58, W - 40, 20,
               juce::Justification::centredLeft);
    g.drawHorizontalLine(80, 20, (float) W - 20);
    g.setColour(kDim); g.setFont(mono(14));
    g.drawText("CMI disks (.IMD / .IMG) and voice files (.VC) load exactly as the CMI holds them: the 16 KB, its loop, its pitch rule.",
               20, 84, W - 40, 16, juce::Justification::centredLeft);
    g.drawText("Audio files: kept sample for sample if they fit a card, WAV root and loop used. Import files, a folder, or drop them here.", 20, 100, W - 40, 16, juce::Justification::centredLeft);
    auto launch = [this](bool folder, int slot) {
        const int flags = juce::FileBrowserComponent::openMode | (folder ? juce::FileBrowserComponent::canSelectDirectories
                                                                          : juce::FileBrowserComponent::canSelectFiles | (slot < 0 ? juce::FileBrowserComponent::canSelectMultipleItems : 0));
        ed.chooser = std::make_unique<juce::FileChooser>(folder ? "Import a folder of sounds" : slot < 0 ? "Import sounds" : "Sample into USER" + juce::String(slot + 1),
                                                         juce::File(), folder ? juce::String() : juce::String("*.imd;*.img;*.bin;*.raw;*.dsk;*.vc;*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"));
        ed.chooser->launchAsync(flags, [this, slot](const juce::FileChooser& fc) {
            const auto res = fc.getResults(); if (res.isEmpty()) return;
            if (slot >= 0) { const auto err = ed.proc.loadSample(res[0], slot, rootKey, loopIt); status = err.isEmpty() ? "SAMPLED" : err.toUpperCase(); }
            else { int n = 0; status = ed.proc.importFiles(res, rootKey, loopIt, n); }
            repaint();
        });
    };
    button(g, { 20, 122, 200, 24 }, "IMPORT DISKS/FILES", false, [launch] { launch(false, -1); });
    button(g, { 230, 122, 200, 24 }, "IMPORT FOLDER", false, [launch] { launch(true, -1); });
    button(g, { 440, 122, 160, 24 }, "CLEAR ALL", false, [this] { for (int k = 0; k < Library::USER_SLOTS; ++k) ed.proc.kit.clearUser(k); status = "USER VOICES CLEARED"; });
    for (int r = 0; r < per; ++r) {
        const int k = userPage * per + r;
        if (k >= Library::USER_SLOTS) break;
        const float y = 156.f + r * 24.f;
        const Sound* s = kit.user(k);
        const bool loaded = kit.userLoaded(k);
        g.setColour(loaded ? kInk : kDim); g.setFont(mono(15));
        g.drawText("USER" + juce::String(k + 1).paddedLeft('0', 2), 20, (int) y, 80, 22, juce::Justification::centredLeft);
        const bool cmiVoice = loaded && s && s->category != "User";
        const juce::String loopTxt = s && s->loopStart >= 0 ? juce::String(s->loopStart + 1) + "-" + juce::String(s->loopEnd + 1) : juce::String("-");
        g.drawText(!loaded || !s ? juce::String("--")
                   : cmiVoice ? juce::String(s->name).paddedRight(' ', 9) + juce::String(s->category).paddedRight(' ', 11) + (s->mode == Mode::Waveform ? "MODE 1  " : "MODE 4  ") + "loop " + loopTxt
                              : juce::String(s->name).paddedRight(' ', 9) + juce::String(juce::roundToInt(s->rate)).paddedLeft(' ', 5) + " Hz  " + juce::String(s->ram.size() / s->rate, 2) +
                                    " s  root " + noteName(s->root) + "  loop " + loopTxt,
                   110, (int) y, 560, 22, juce::Justification::centredLeft);
        button(g, { 680, y + 1, 90, 20 }, "LOAD", false, [launch, k] { launch(false, k); });
        button(g, { 778, y + 1, 90, 20 }, "PLAY", false, [this, k] { ed.proc.loadSound(ed.proc.kit.userSlot(k)); status = "LOADED"; });
        button(g, { 876, y + 1, 90, 20 }, "CLEAR", false, [this, k] { ed.proc.kit.clearUser(k); });
    }
    const float by = 156.f + per * 24.f + 4;
    button(g, { 20, by, 40, 22 }, "<", false, [this] { userPage = juce::jmax(0, userPage - 1); });
    g.setColour(kInk); g.setFont(mono(15));
    g.drawText(juce::String(userPage * per + 1) + "-" + juce::String(juce::jmin(Library::USER_SLOTS, (userPage + 1) * per)), 64, (int) by, 90, 22, juce::Justification::centred);
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

void Screen::drawComposer(juce::Graphics& g) {
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
    if (ext) { g.setColour(kDim); g.setFont(mono(14)); g.drawText("follows the host's transport", 260, (int) by2, 290, 24, juce::Justification::centredLeft); }
}

void Screen::soundMenu(std::function<void(int)> chosen) {
    juce::PopupMenu m; std::map<std::string, juce::PopupMenu> cats; std::vector<std::string> order;
    auto& kit = ed.proc.kit;
    for (int i = 0; i < kit.bank.count.load(); ++i) {
        const Sound* s = kit.bank.at(i); if (!s) continue;
        if (!cats.count(s->category)) order.push_back(s->category);
        cats[s->category].addItem(i + 1, juce::String(s->name) + "  " + juce::String(s->title).substring(0, 40));
    }
    for (auto& c : order) m.addSubMenu(c, cats[c]);
    m.showMenuAsync(juce::PopupMenu::Options(), [chosen](int r) { if (r > 0 && chosen) chosen(r - 1); });
}

void Screen::command(const juce::String& raw) {
    const auto c = raw.trim().toUpperCase();
    auto words = juce::StringArray::fromTokens(c, " ", "");
    if (words.isEmpty()) return;
    const auto w0 = words[0], arg = words.size() > 1 ? words[1] : juce::String();
    auto& eng = ed.proc.engine;
    if (w0 == "PLAY") { ed.proc.seqPlay(true); ed.proc.set("sync", 0.f); page = 'R'; }
    else if (w0 == "STOP") ed.proc.seqPlay(false);
    else if (w0 == "PAGE" || (w0.length() == 1 && juce::String("27D8R").contains(w0))) { const auto p = w0 == "PAGE" ? arg : w0; if (p.isNotEmpty() && juce::String("27D8R").contains(p.substring(0, 1))) page = p[0]; }
    else if (w0 == "LOAD" || w0 == "VOICE") {
        const int idx = Library::get().find(arg.toStdString());
        if (idx >= 0) ed.proc.loadSound(idx); else { status = "NO VOICE " + arg; repaint(); return; }
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
    cmdLine.setTextToShowWhenEmpty("type: PLAY  STOP  LOAD ORCHHIT  SPEED 110  PATTERN 3  PAGE 7", kBg.withAlpha(0.55f));
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
    for (auto& f : files) { const juce::File x(f); if (x.isDirectory() || x.hasFileExtension("wav;aif;aiff;flac;mp3;ogg;vc;imd;img;bin;raw;dsk;hfe")) return true; }
    return false;
}

void CmiEditor::filesDropped(const juce::StringArray& files, int, int) {
    juce::Array<juce::File> items; for (auto& f : files) items.add(juce::File(f));
    int n = 0;
    screen.status = proc.importFiles(items, screen.rootKey, screen.loopIt, n);
    screen.page = '8'; screen.repaint();
}

juce::AudioProcessorEditor* CmiProcessor::createEditor() { return new CmiEditor(*this); }

}  // namespace cmi
