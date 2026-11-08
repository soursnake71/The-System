// Omega SID 64: the C64 screen. A 40 x 25 text screen at 320 x 200 pixels in the C64's 16 colours, shared by the
// plugin and the Android app (each draws the monitor and the keyboard around it and passes pointer events in
// screen pixels). Pages: PRG (presets), OS1-OS3, FLT, MOD, ARP (arp + echo), SMP (sampler), MIX (mixer + scope),
// SEQ (tracker), and CHP (chip) from the button on the info line.
//
// Touch: tap a tab, a preset, a toggle. Drag along a bar to set it; drag on a name or value for fine changes;
// tap a choice to step it (drag to scroll through). Double-tap a value for its default. In the sequencer, tap a
// note to switch the step on / off, drag it up / down to change the note, tap ! for accent and ~ for slide.
#pragma once
#include "SidFont.h"
#include "SidSynth.h"
#include <functional>

namespace sid {

static const uint32_t PALETTE[16] = { 0xFF000000, 0xFFFFFFFF, 0xFF68372B, 0xFF70A4B2, 0xFF6F3D86, 0xFF588D43, 0xFF352879, 0xFFB8C76F,
                                      0xFF6F4F25, 0xFF433900, 0xFF9A6759, 0xFF444444, 0xFF6C6C6C, 0xFF9AD284, 0xFF6C5EB5, 0xFF959595 };
enum Col { BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW, ORANGE, BROWN, LRED, DGREY, GREY, LGREEN, LBLUE, LGREY };

class ScreenUI {
public:
    // 40 columns always; 25 rows on a C64 (and in the plugin). A tall phone screen gets more rows (up to 64), so the
    // screen can fill the app with square pixels (up to 80 rows): lists, the tracker, the faders and the scope use them.
    static constexpr int W = 320, H = 200, COLS = 40, ROWS = 25, MAXROWS = 80;
    int rows = ROWS;
    int height() const { return rows * 8; }
    void setRows(int r) { rows = std::clamp(r, ROWS, MAXROWS); }

    // what the screen needs from the platform
    struct Host {
        virtual ~Host() = default;
        virtual Synth& synth() = 0;
        virtual float get(int id) = 0;
        virtual void set(int id, float v) = 0;        // from the UI: goes to the host's parameter (automation)
        virtual void loadPreset(int i) = 0;
        virtual void initPatch() = 0;
        virtual void randomize() = 0;
        virtual bool isPlugin() const = 0;
        // sampling: open a file (the platform's picker), record from the input / microphone
        virtual bool canLoadFiles() const { return true; }
        virtual void loadSampleFile() {}
        virtual void setRecording(bool on) { if (on) synth().startRecording(); else synth().stopRecording(); }
        virtual bool recording() { return synth().recOn.load(); }
        virtual float recordedSeconds() { return synth().recPos.load() / 48000.f; }
    };

    ScreenUI(Host& h) : host(h) {
        for (int i = 0; i < presetCount(); ++i) {
            const std::string c = PRESETS[i].cat;
            if (std::find(cats.begin(), cats.end(), c) == cats.end()) cats.push_back(c);
        }
    }

    int page = 0;
    std::function<void()> onPresetChanged;

    // ---- drawing: ARGB pixels, W x H ----
    void render(uint32_t* px) {
        ++frame;
        build();
        const uint32_t bg = PALETTE[BLUE];
        for (int r = 0; r < rows; ++r) for (int c = 0; c < COLS; ++c) {
            const Cell& cl = cell[r][c];
            const int code = (unsigned char) cl.ch; const uint8_t* g = FONT[(code < 32 || code > 127) ? 0 : code - 32];
            const uint32_t f = PALETTE[cl.fg & 15];
            for (int y = 0; y < 8; ++y) {
                uint32_t* row = px + (r * 8 + y) * W + c * 8;
                const uint8_t bits = cl.inv ? (uint8_t) ~g[y] : g[y];
                for (int x = 0; x < 8; ++x) row[x] = (bits >> (7 - x) & 1) ? f : bg;
            }
        }
        for (const Bar& b : bars) {
            const int x0 = b.c0 * 8, w = (b.c1 - b.c0 + 1) * 8, y0 = b.r * 8 + 1;
            const uint32_t on = PALETTE[b.col], off = 0xFF2A2066;
            int a = 0, z = (int) std::lround(b.frac * w);
            if (b.center) { const int mid = w / 2; a = std::min(mid, z); z = std::max(mid, z); }
            for (int y = 0; y < 6; ++y) for (int x = 0; x < w; ++x) {
                const bool fill = x >= a && x < z;
                px[(y0 + y) * W + x0 + x] = fill ? on : (y == 0 || y == 5 || x == 0 || x == w - 1) ? off : bg;
            }
        }
        for (const Rect& q : rects)
            for (int y = std::max(0, q.y); y < std::min(height(), q.y + q.h); ++y)
                for (int x = std::max(0, q.x); x < std::min(W, q.x + q.w); ++x) px[y * W + x] = q.col;
        if (page == 8) { const int sr = mixPanRow() + 3; drawScope(px, sr * 8 + 1, (rows - 2 - sr) * 8 - 1); }
    }

    // ---- pointer, in screen pixels ----
    // ---- for touch screens: the platform can show the selected setting big, and page buttons ----
    bool touchMode = false;      // forgiving taps: a touch between rows takes the nearest control
    int selected = -1;           // the setting touched last (highlighted on the screen)
    // pages: 0 PRG, 1-3 OSC, 4 FLT, 5 MOD, 6 ARP, 7 CHP, 8 MIX, 9 SEQ, 10 SMP, 11 FX, 12 REV, 13 GRID
    static constexpr int NPAGES = 14;
    static constexpr int PAGE_ORDER[NPAGES] = { 0, 1, 2, 3, 4, 5, 6, 10, 8, 11, 12, 9, 13, 7 };   // the tab order
    static const char* pageName(int i) { static const char* n[NPAGES] = { "PRG", "OS1", "OS2", "OS3", "FLT", "MOD", "ARP", "SMP", "MIX", "FX", "REV", "SEQ", "GRID", "CHP" }; return n[std::clamp(i, 0, NPAGES - 1)]; }
    void setPage(int pg) { page = pg; patList = false; if (pg == 0) showPreset(host.synth().program.load()); if (pg == 13) gridFit(); }
    void stepPage(int d) {
        int k = 0; for (int i = 0; i < NPAGES; ++i) if (PAGE_ORDER[i] == page) k = i;
        setPage(PAGE_ORDER[((k + d) % NPAGES + NPAGES) % NPAGES]);
    }
    void stepPreset(int d) { button(d < 0 ? B_PREV : B_NEXT); }
    // the selected setting: change it from a big control
    void setSelectedNorm(float n) { if (selected >= 0) host.set(selected, fromNorm(selected, n)); }
    void stepSelected(int d) {
        if (selected < 0) return;
        const ParamInfo& p = params()[selected]; const float v = host.get(selected);
        if (p.kind == TOGGLE) host.set(selected, v > .5f ? 0.f : 1.f);
        else if (p.kind == CHOICE || p.kind == INT) { float n = v + (float) d; if (p.kind == CHOICE) { if (n > p.max) n = p.min; if (n < p.min) n = p.max; } host.set(selected, std::clamp(n, p.min, p.max)); }
        else host.set(selected, fromNorm(selected, toNorm(selected, v) + d * 0.01f));
    }
    void resetSelected() { if (selected >= 0) host.set(selected, params()[selected].def); }

    void down(float x, float y) {
        downX = lastX = x; downY = lastY = y; moved = false;
        hit = zoneAt(x, y);
        if (hit.a >= 0 && ((hit.kind >= Z_PNAME && hit.kind <= Z_PBAR) || hit.kind == Z_VFADER)) selected = hit.a;
        if (hit.kind == Z_PBAR) setFromBar(hit, x);
        if (hit.kind == Z_VFADER) setFromFader(hit.a, y);
        if (hit.kind == Z_GRID) gridDown(x, y);
        const bool isParam = hit.a >= 0 && ((hit.kind >= Z_PNAME && hit.kind <= Z_PBAR) || hit.kind == Z_VFADER);
        startNorm = isParam ? toNorm(hit.a, host.get(hit.a)) : 0;
        startVal = isParam ? host.get(hit.a) : 0;
        if (hit.kind == Z_STEP_NOTE) { Step s = host.synth().step(curPat(), hit.a); startVal = (float) s.note; }
        if (hit.kind == Z_LIST || hit.kind == Z_PRESET) startVal = (float) listScroll;
        if (hit.kind == Z_PATLIST || hit.kind == Z_PATITEM) startVal = (float) patScroll;
    }
    void move(float x, float y) {
        if (std::fabs(x - downX) > 3 || std::fabs(y - downY) > 3) moved = true;
        lastX = x; lastY = y;
        if (!moved) return;
        const float dx = x - downX, dy = y - downY;
        switch (hit.kind) {
            case Z_PBAR: setFromBar(hit, x); break;
            case Z_VFADER: setFromFader(hit.a, y); break;
            case Z_GRID: gridDrag(x, y); break;
            case Z_PNAME: case Z_PVAL: {
                const ParamInfo& p = params()[hit.a];
                if (p.kind == CHOICE || (p.kind == INT && p.max - p.min <= 32)) host.set(hit.a, std::clamp(std::round(startVal + dx / 12.f), p.min, p.max));
                else if (p.kind != TOGGLE) host.set(hit.a, fromNorm(hit.a, startNorm + dx / 200.f));
                break;
            }
            case Z_STEP_NOTE: {
                Step s = host.synth().step(curPat(), hit.a);
                s.note = std::clamp((int) std::lround(startVal - dy / 4.f), 0, 127); s.on = true;
                host.synth().setStep(curPat(), hit.a, s); break;
            }
            case Z_LIST: case Z_PRESET: listScroll = std::clamp((int) std::lround(startVal - dy / 8.f), 0, std::max(0, catSize() - listRows())); break;
            case Z_PATLIST: case Z_PATITEM: patScroll = std::clamp((int) std::lround(startVal - dy / 8.f), 0, std::max(0, patternPresetCount() - 2 * listRows())); break;
            default: break;
        }
    }
    void up(float x, float y) {
        move(x, y);
        // a sideways swipe off the controls that drag: the next / previous page
        const float dx = x - downX, dy = y - downY;
        const bool drags = (hit.kind >= Z_PNAME && hit.kind <= Z_PBAR) || hit.kind == Z_VFADER || hit.kind == Z_STEP_NOTE || hit.kind == Z_GRID;
        if (moved && !drags && std::fabs(dx) > 48 && std::fabs(dx) > 2 * std::fabs(dy)) { stepPage(dx < 0 ? 1 : -1); hit = {}; return; }
        if (!moved) tap(hit);
        hit = {};
    }
    // mouse wheel / two-finger scroll: fine steps on values, scrolling on lists
    void wheel(float x, float y, float steps) {
        const Zone z = zoneAt(x, y);
        if (z.kind >= Z_PNAME && z.kind <= Z_PBAR) {
            const ParamInfo& p = params()[z.a];
            if (p.kind == CONT) host.set(z.a, fromNorm(z.a, toNorm(z.a, host.get(z.a)) + steps * 0.01f));
            else host.set(z.a, std::clamp(host.get(z.a) + (steps > 0 ? 1.f : -1.f), p.min, p.max));
        } else if (z.kind == Z_LIST || z.kind == Z_PRESET) listScroll = std::clamp(listScroll - (int) steps, 0, std::max(0, catSize() - listRows()));
        else if (z.kind == Z_STEP_NOTE) {
            Step s = host.synth().step(curPat(), z.a); s.note = std::clamp(s.note + (steps > 0 ? 1 : -1), 0, 127); host.synth().setStep(curPat(), z.a, s);
        }
    }
    void message(const std::string& m) { msg = m; msgUntil = frame + 90; }
    void showPreset(int i) {
        if (i < 0 || i >= presetCount()) return;
        const std::string c = PRESETS[i].cat;
        for (int k = 0; k < (int) cats.size(); ++k) if (cats[(size_t) k] == c) cat = k;
        int idx = 0; for (int k = 0; k < i; ++k) if (PRESETS[k].cat == c) ++idx;
        if (idx < listScroll || idx >= listScroll + listRows()) listScroll = std::max(0, idx - listRows() / 2);
    }

private:
    Host& host;
    struct Cell { char ch = ' '; uint8_t fg = LBLUE; bool inv = false; };
    Cell cell[MAXROWS][COLS];
    enum ZK { Z_NONE, Z_TAB, Z_PNAME, Z_PVAL, Z_PBAR, Z_CAT, Z_PRESET, Z_LIST, Z_BTN, Z_STEP_NOTE, Z_STEP_ACC, Z_STEP_SLIDE, Z_PATLIST, Z_PATITEM, Z_VFADER, Z_GRID, Z_GRID_ACC, Z_GRID_SLD };
    struct Zone { int r = -1, c0 = 0, c1 = 0, kind = Z_NONE, a = -1; };
    struct Bar { int r, c0, c1; float frac; uint8_t col; bool center; };
    struct Rect { int x, y, w, h; uint32_t col; };
    std::vector<Zone> zones; std::vector<Bar> bars; std::vector<Rect> rects;
    // the mixer: faders from row 7 down to the pan row; extra rows go two thirds to the faders, a third to the scope
    int mixPanRow() const { const int ex = rows - ROWS; return 18 + ex - ex / 3; }
    int faderTop() const { return 7 * 8; }
    int faderBot() const { return mixPanRow() * 8; }
    static constexpr uint32_t DIM = 0xFF2A2066;
    std::vector<std::string> cats;
    int cat = 0, listScroll = 0, patScroll = 0; bool patList = false;
    uint64_t frame = 0, msgUntil = 0; std::string msg;
    Zone hit; float downX = 0, downY = 0, lastX = 0, lastY = 0, startNorm = 0, startVal = 0; bool moved = false;
    uint64_t lastTapFrame = 0; int lastTapParam = -1;
    int listRows() const { return rows - 8; }

    enum Btn { B_PREV = 1, B_NEXT, B_INIT, B_RAND, B_UP, B_DN, B_PLAY, B_STOP, B_PAT_DN, B_PAT_UP, B_LEN_DN, B_LEN_UP, B_LOAD, B_CLEAR,
               B_RNDPAT, B_SHL, B_SHR, B_TR_DN, B_TR_UP, B_OCT_DN, B_OCT_UP, B_BACK, B_PANIC, B_COPY, B_PASTE,
               B_SMP_LOAD, B_SMP_REC, B_SMP_PREV, B_SMP_NEXT, B_GRID_HALF, B_GRID_UP, B_GRID_DN };
    std::vector<Step> clip;

    int curPat() { return std::clamp((int) std::lround(host.get(P::spat)) - 1, 0, kPatterns - 1); }
    int catSize() { int n = 0; for (int i = 0; i < presetCount(); ++i) if (cats[(size_t) cat] == PRESETS[i].cat) ++n; return n; }

    // ---------------- text ----------------
    void clear() { for (auto& r : cell) for (auto& c : r) c = Cell {}; zones.clear(); bars.clear(); rects.clear(); }
    void put(int r, int c, const std::string& s, uint8_t col = LBLUE, bool inv = false) {
        if (r < 0 || r >= rows) return;
        for (size_t i = 0; i < s.size() && c + (int) i < COLS; ++i) if (c + (int) i >= 0) cell[r][c + (int) i] = { s[i], col, inv };
    }
    void zone(int r, int c0, int c1, int kind, int a) { zones.push_back({ r, c0, c1, kind, a }); }
    void button(int r, int c, const std::string& label, int id, uint8_t col = WHITE, bool inv = false) { put(r, c, label, col, inv); zone(r, c, c + (int) label.size() - 1, Z_BTN, id); }
    static std::string pad(std::string s, size_t n) { if (s.size() > n) s.resize(n); while (s.size() < n) s += ' '; return s; }
    static std::string hex2(int v) { char b[4]; std::snprintf(b, sizeof b, "%02X", v & 255); return b; }
    static std::string num(int v, int w) { char b[16]; std::snprintf(b, sizeof b, "%0*d", w, v); return b; }

    Zone zoneAt(float x, float y) const {
        const int c = (int) (x / 8), r = (int) (y / 8);
        for (auto it = zones.rbegin(); it != zones.rend(); ++it) if (it->r == r && c >= it->c0 && c <= it->c1) return *it;
        if (!touchMode) return {};
        // a finger between two rows: the control whose row is nearest, within half a row either way
        const float fy = y / 8.f - 0.5f;
        Zone best; float bd = 0.75f;
        for (auto it = zones.rbegin(); it != zones.rend(); ++it) {
            if (c < it->c0 - 1 || c > it->c1 + 1) continue;
            const float d = std::fabs(it->r - fy);
            if (d < bd) { bd = d; best = *it; }
        }
        return best;
    }

    // one parameter: NAME (cols 1-16) VALUE (18-27) BAR (28-38)
    void paramRow(int r, int id, const char* label = nullptr) {
        const ParamInfo& p = params()[id];
        const float v = host.get(id);
        put(r, 1, pad(label ? label : p.name, 16), id == selected ? WHITE : LBLUE, id == selected);
        zone(r, 0, 17, Z_PNAME, id);
        const std::string val = formatValue(id, v);
        if (p.kind == TOGGLE) {
            put(r, 18, v > .5f ? "ON " : "OFF", v > .5f ? WHITE : GREY);
            put(r, 28, v > .5f ? "[{]" : "[ ]", v > .5f ? YELLOW : GREY);
            zone(r, 18, 39, Z_PVAL, id);
            return;
        }
        if (p.kind == CHOICE && val.size() > 9) {   // a long name takes the whole row; tap / drag still steps it
            put(r, 18, pad(val, 21), YELLOW); zone(r, 18, 39, Z_PVAL, id); return;
        }
        put(r, 18, pad(val, 10), p.kind == CHOICE ? YELLOW : WHITE);
        zone(r, 18, 27, Z_PVAL, id);
        if (p.kind == CHOICE) {
            auto c = choiceList(id); const int n = (int) c.size(), cur = (int) std::lround(v);
            // one block per choice, the current one lit
            for (int k = 0; k < n && 28 + k <= 38; ++k) put(r, 28 + k, k == cur ? "{" : "-", k == cur ? YELLOW : DGREY);
            zone(r, 28, 39, Z_PVAL, id);
            return;
        }
        bars.push_back({ r, 28, 38, toNorm(id, v), (uint8_t) (p.min < 0 && p.max > 0 ? CYAN : LGREEN), p.min < 0 && p.max > 0 });
        zone(r, 28, 39, Z_PBAR, id);
    }
    void toggleChip(int r, int c, int id, const char* label) {
        const bool on = host.get(id) > .5f;
        put(r, c, label, on ? BLUE : GREY, on);
        if (on) for (size_t i = 0; i < std::strlen(label); ++i) cell[r][c + (int) i].fg = YELLOW;
        zone(r, c, c + (int) std::strlen(label) - 1, Z_PVAL, id);
    }

    void setFromBar(const Zone& z, float x) {
        const float n = (x - z.c0 * 8.f) / ((z.c1 - z.c0) * 8.f);
        host.set(z.a, fromNorm(z.a, n));
    }

    // ---------------- pages ----------------
    void build() {
        clear();
        Synth& s = host.synth();
        // the start-up banner
        put(0, 4, "**** OMEGA SID 64 BASIC V2 ****", LBLUE);
        const int model = (int) std::lround(host.get(P::model));
        const int mode = (int) std::lround(host.get(P::mode));
        const int chips = mode == 0 ? (int) std::lround(host.get(P::poly)) : 1;
        char info[64];
        std::snprintf(info, sizeof info, " %s %s  %d SID%s  %s  %3.0f BPM", model ? "8580" : "6581", host.get(P::clock) > .5f ? "NTSC" : "PAL ",
                      chips, chips > 1 ? "S" : " ", choiceList(P::mode)[(size_t) std::clamp(mode, 0, 3)].c_str(), host.get(P::bpm));
        put(1, 0, info, LBLUE);
        put(2, 0, "READY.", LBLUE);
        if ((frame / 20) % 2 == 0) put(2, 6, "{", LBLUE);
        if (s.seqRunning()) put(2, 30, "{ RUNNING", LGREEN);
        // tabs
        // two rows of tabs: the sound on the first, mixing, effects, sequencing and the chip on the second
        for (int t = 0; t < NPAGES; ++t) {
            const int tr = t < 8 ? 3 : 4, tc = (t < 8 ? t : t - 8) * 5;
            const bool sel = PAGE_ORDER[t] == page;
            put(tr, tc, pageName(t), sel ? WHITE : LBLUE, sel);
            zone(tr, tc, tc + 4, Z_TAB, PAGE_ORDER[t]);
        }
        switch (page) {
            case 0: pagePresets(); break;
            case 1: case 2: case 3: pageOsc(page - 1); break;
            case 4: pageFilter(); break;
            case 5: pageMod(); break;
            case 6: pageArp(); break;
            case 7: pageChip(); break;
            case 8: pageMix(); break;
            case 9: if (patList) pagePatterns(); else pageSeq(); break;
            case 11: pageFx(); break;
            case 12: pageRev(); break;
            case 13: pageGrid(); break;
            default: pageSample(); break;
        }
        // help / messages
        if (page == 9 && !patList) return;   // the tracker uses the whole screen
        {
            if (frame < msgUntil) put(rows - 2, 0, pad(msg, 40), WHITE);
            else put(rows - 2, 0, page == 0 ? "TAP A PRESET.  DRAG THE LIST TO SCROLL." : page == 8 ? "DRAG FADERS. M=MUTE S=SOLO. 2X TAP=RESET" : page == 13 ? "TAP/DRAG: PAINT NOTES. TAP A NOTE: ERASE" : page == 10 ? sampleHelp() : "DRAG BARS. TAP TO TOGGLE. 2X TAP=RESET", GREY);
        }
        // the preset line
        char pl[64];
        std::snprintf(pl, sizeof pl, "%03d %s", s.program.load() + 1, s.presetName.c_str());
        const int last = rows - 1;
        button(last, 0, "<", B_PREV, WHITE, true);
        put(last, 2, pad(pl, 31), YELLOW);
        button(last, 34, ">", B_NEXT, WHITE, true);
        button(last, 36, "INI", B_INIT, LBLUE);
        zone(last, 2, 32, Z_TAB, 0);
    }

    void pagePresets() {
        for (int k = 0; k < (int) cats.size() && k < 18; ++k) {
            put(5 + k, 0, pad(cats[(size_t) k], 8), k == cat ? WHITE : LBLUE, k == cat);
            zone(5 + k, 0, 8, Z_CAT, k);
        }
        int idx = 0, row = 0; const int cur = host.synth().program.load();
        for (int i = 0; i < presetCount(); ++i) {
            if (cats[(size_t) cat] != PRESETS[i].cat) continue;
            if (idx >= listScroll && row < listRows()) {
                const bool sel = i == cur;
                put(5 + row, 10, pad(std::string(sel ? "{" : " ") + PRESETS[i].name, 30), sel ? WHITE : LBLUE);
                zone(5 + row, 10, 39, Z_PRESET, i);
                ++row;
            }
            ++idx;
        }
        // the list scrolls when dragged anywhere in it (presets also take taps)
        for (int r = 5; r < 5 + listRows(); ++r) zones.insert(zones.begin(), Zone { r, 10, 39, Z_LIST, 0 });
        const int n = catSize();
        if (n > listRows()) {
            char b[32]; std::snprintf(b, sizeof b, "%d-%d OF %d", listScroll + 1, std::min(n, listScroll + listRows()), n);
            put(rows - 3, 10, b, GREY);
            button(rows - 3, 26, " ^ ", B_UP, WHITE, true); button(rows - 3, 30, " v ", B_DN, WHITE, true);
        }
        button(rows - 3, 34, "RND", B_RAND, LGREEN, true);
    }
    void pageOsc(int o) {
        const int b = P::o1on + o * (P::o2on - P::o1on);
        put(5, 1, "OSCILLATOR " + std::to_string(o + 1), WHITE);
        toggleChip(5, 18, b, host.get(b) > .5f ? " ON  " : " OFF ");
        put(6, 1, "WAVEFORM", LBLUE);
        toggleChip(6, 18, b + 1, "TRI"); toggleChip(6, 23, b + 2, "SAW"); toggleChip(6, 28, b + 3, "PUL"); toggleChip(6, 33, b + 4, "NOI");
        int r = 7;
        for (int k : { 5, 6, 7, 8, 9 }) paramRow(r++, b + k);
        const char* src = o == 0 ? "RING MOD (OSC 3)" : o == 1 ? "RING MOD (OSC 1)" : "RING MOD (OSC 2)";
        const char* ssrc = o == 0 ? "SYNC TO OSC 3" : o == 1 ? "SYNC TO OSC 1" : "SYNC TO OSC 2";
        paramRow(r++, b + 10, src); paramRow(r++, b + 11, ssrc); paramRow(r++, b + 12);
        for (int k : { 13, 14, 15, 16 }) paramRow(r++, b + k);
        // the ADSR in the chip's own hex, and its shape
        char ad[48]; std::snprintf(ad, sizeof ad, "AD/SR $%X%X $%X%X", (int) host.get(b + 13), (int) host.get(b + 14), (int) host.get(b + 15), (int) host.get(b + 16));
        put(r + 1, 1, ad, GREY);
        drawEnvShape(r + 1, 20, b);
    }
    void drawEnvShape(int r, int c, int b) {
        // a tiny ADSR picture in block characters
        static const float t[16] = { .002f, .008f, .016f, .024f, .038f, .056f, .068f, .08f, .1f, .25f, .5f, .8f, 1, 3, 5, 8 };
        const float a = t[(int) host.get(b + 13)], d = 3 * t[(int) host.get(b + 14)], rr = 3 * t[(int) host.get(b + 16)];
        const float su = host.get(b + 15) / 15.f;
        const float tot = a + d + 0.5f + rr; const int w = 18;
        for (int x = 0; x < w; ++x) {
            const float tt = (x + .5f) / w * tot; float lv;
            if (tt < a) lv = tt / a; else if (tt < a + d) lv = 1 - (1 - su) * (tt - a) / d; else if (tt < a + d + .5f) lv = su; else lv = su * (1 - (tt - a - d - .5f) / rr);
            put(r, c + x, lv > .66f ? "{" : lv > .33f ? "}" : lv > .05f ? "_" : " ", LGREEN);
        }
    }
    void pageFilter() {
        int r = 5;
        paramRow(r++, P::cut); paramRow(r++, P::res);
        put(r, 1, "MODE", LBLUE); toggleChip(r, 18, P::lp, "LP"); toggleChip(r, 22, P::bp, "BP"); toggleChip(r, 26, P::hp, "HP"); toggleChip(r, 30, P::off3, "3OFF"); ++r;
        put(r, 1, "ROUTE", LBLUE); toggleChip(r, 18, P::o1flt, "OSC1"); toggleChip(r, 24, P::o2flt, "OSC2"); toggleChip(r, 30, P::o3flt, "OSC3"); ++r;
        for (int id : { P::fenv, P::fa, P::fd, P::fs, P::fr, P::key, P::flfo, P::velcut }) paramRow(r++, id);
        ++r;
        // the cutoff the chip is set to, in Hz
        const int model = (int) std::lround(host.get(P::model));
        Chip c; c.model = model ? MOS8580 : MOS6581; c.curve = host.get(P::curve); c.fc = (uint16_t) std::lround(host.get(P::cut)); c.setRate(192000);
        char b[64]; std::snprintf(b, sizeof b, "%s CUTOFF $%03X = %.0f HZ", model ? "8580" : "6581", c.fc, cutoffHz(c));
        put(r, 1, b, GREY);
    }
    static double cutoffHz(const Chip& c) {
        const Tables& T = Tables::get(); const double x = T.fc[c.model][c.fc & 0x7FF];
        if (c.model == MOS6581) return 220.0 + 15500.0 * std::pow(2.0, 0.8 * c.curve) * std::pow(x, 2.35 - 0.45 * c.curve);
        return 30.0 + 12470.0 * x;
    }
    void pageMod() {
        int r = 5;
        for (int id : { P::mode, P::poly, P::glide, P::bend, P::vellvl, P::hard, P::tick, P::mw, P::lrate, P::lshape, P::vib, P::vdel, P::prog, P::ptgt }) paramRow(r++, id);
    }
    void pageArp() {
        int r = 5;
        for (int id : { P::arp, P::chord, P::aspd, P::async, P::adiv, P::aoct }) paramRow(r++, id);
        put(r + 1, 1, "THE ECHO IS ON THE FX PAGE.", DGREY);
    }
    void pageChip() {
        int r = 5;
        for (int id : { P::model, P::curve, P::drive, P::clock, P::adsrbug, P::vol, P::quality, P::spread, P::gain, P::dboost }) paramRow(r++, id);
        regDump(r);
        button(rows - 3, 1, "PANIC: ALL NOTES OFF", B_PANIC, LRED, true);
    }
    void regDump(int r) {
        Synth& s = host.synth();
        for (int v = 0; v < 4; ++v) {
            char b[64]; std::snprintf(b, sizeof b, "D4%02X:", v * 7);
            std::string line = b;
            for (int k = 0; k < (v < 3 ? 7 : 4); ++k) line += " " + hex2(s.regs[(size_t) (v * 7 + k)].load());
            put(r + v, 1, line, v < 3 ? LBLUE : CYAN);
        }
        char b[48]; std::snprintf(b, sizeof b, "SIDS PLAYING: %d", s.activeVoices.load()); put(r + 4, 1, b, GREY);
    }

    void pageSeq() {
        Synth& s = host.synth();
        const int p = curPat(), len = std::clamp((int) std::lround(host.get(P::slen)), 1, kSteps);
        const bool run = s.seqRunning();
        button(5, 0, run ? "{STOP}" : "{PLAY}", run ? B_STOP : B_PLAY, run ? LRED : LGREEN, true);
        put(5, 7, "PAT", LBLUE); button(5, 11, "<", B_PAT_DN, WHITE, true); put(5, 13, num(p + 1, 2), WHITE); button(5, 16, ">", B_PAT_UP, WHITE, true);
        put(5, 19, "LEN", LBLUE); button(5, 23, "<", B_LEN_DN, WHITE, true); put(5, 25, num(len, 2), WHITE); button(5, 28, ">", B_LEN_UP, WHITE, true);
        button(5, 31, "LOAD", B_LOAD, YELLOW, true); button(5, 36, "CLR", B_CLEAR, LRED, true);
        // tempo, rate, swing, gate: drag the values
        auto val = [&](int r, int c, const char* lbl, int id, int w) {
            put(r, c, lbl, LBLUE); const int c0 = c + (int) std::strlen(lbl);
            put(r, c0, pad(formatValue(id, host.get(id)), (size_t) w), YELLOW); zone(r, c, c0 + w - 1, Z_PVAL, id);
        };
        const bool hostTempo = host.isPlugin() && host.get(P::shost) > .5f;
        val(6, 0, "BPM ", P::bpm, 8); if (hostTempo) put(6, 4, "HOST    ", GREY);
        val(6, 12, "RATE ", P::srate, 5); val(6, 23, "SWG ", P::swing, 4); val(6, 32, "GT ", P::sgate, 5);
        button(7, 0, "RND", B_RNDPAT, LGREEN, true); button(7, 4, "<<", B_SHL, WHITE, true); button(7, 7, ">>", B_SHR, WHITE, true);
        button(7, 10, "-1", B_TR_DN, WHITE, true); button(7, 13, "+1", B_TR_UP, WHITE, true);
        button(7, 16, "-12", B_OCT_DN, WHITE, true); button(7, 20, "+12", B_OCT_UP, WHITE, true);
        button(7, 24, "CPY", B_COPY, WHITE, true); button(7, 28, "PST", B_PASTE, WHITE, true);
        toggleChip(8, 0, P::strans, "KEYS TRANSPOSE");
        if (host.isPlugin()) toggleChip(8, 16, P::shost, "FOLLOW HOST");
        put(8, 29, "DRAG=PITCH", DGREY);
        const int playing = s.seqStepShown.load();
        for (int i = 0; i < kSteps; ++i) {
            const bool oneCol = rows >= 9 + kSteps;
            const int r = oneCol ? 9 + i : 9 + (i % 16), c = oneCol || i < 16 ? 0 : 20;
            const Step st = s.step(p, i);
            const bool inLen = i < len;
            const uint8_t dim = inLen ? LBLUE : DGREY;
            put(r, c, i == playing && run ? "~" : " ", WHITE);
            put(r, c + 1, num(i, 2), inLen ? GREY : DGREY);
            put(r, c + 4, st.on ? noteName(st.note) : "---", st.on ? (uint8_t) (inLen ? WHITE : GREY) : dim);
            put(r, c + 8, st.accent ? "!" : ".", st.accent ? (uint8_t) YELLOW : dim);
            put(r, c + 10, st.slide ? "~" : ".", st.slide ? (uint8_t) LGREEN : dim);
            if (i == playing && run) for (int k = 1; k < 12; ++k) cell[r][c + k].inv = true;
            zone(r, c, c + 7, Z_STEP_NOTE, i); zone(r, c + 8, c + 9, Z_STEP_ACC, i); zone(r, c + 10, c + 12, Z_STEP_SLIDE, i);
        }
    }
    void pagePatterns() {
        put(5, 0, "LOAD A PATTERN INTO PAT " + num(curPat() + 1, 2), WHITE);
        button(5, 34, "BACK", B_BACK, WHITE, true);
        for (int k = 0; k < 2 * listRows(); ++k) {
            const int i = patScroll + k; if (i >= patternPresetCount()) break;
            const int r = 6 + (k % listRows()), c = k < listRows() ? 0 : 20;
            put(r, c, pad(PATTERNS[i].name, 19), LBLUE);
            zone(r, c, c + 19, Z_PATITEM, i);
        }
        for (int r = 6; r < 6 + listRows(); ++r) zones.insert(zones.begin(), Zone { r, 0, 39, Z_PATLIST, 0 });
    }
    // ---------------- the sampler ----------------
    std::string sampleHelp() {
        if (host.recording()) { char b[48]; std::snprintf(b, sizeof b, "RECORDING %.1f S. TAP STOP.", host.recordedSeconds()); return b; }
        return host.get(P::dmode) < .5f ? "PICK A METHOD TO PLAY THE SAMPLE." : "NOTES PLAY THE SAMPLE (ROOT = ORIGINAL).";
    }
    void pageSample() {
        Synth& s = host.synth(); const SampleSource& src = s.source;
        const int m = (int) std::lround(host.get(P::dmode));
        const double rate = s.effectiveRate(digiRates()[std::clamp((int) std::lround(host.get(P::drate)), 0, 9)]);
        const size_t n = src.preview.size();
        const size_t bytes = src.previewBits == 4 ? (n + 1) / 2 : n;
        char b[64];
        std::snprintf(b, sizeof b, "%.0f HZ %dBIT %.1fK %.2fS", rate, src.previewBits, bytes / 1024.0, n / std::max(1.0, src.previewRate));
        put(5, 0, pad(src.name.empty() ? "NO SAMPLE" : src.name, 13), WHITE);
        put(5, 14, b, GREY);
        // the waveform as the C64 holds it: its levels, the start / end, and where it's playing
        const int grow = (rows - ROWS) / 2;   // extra rows: half to the waveform
        const int x0 = 4, w = W - 8, y0 = 6 * 8 + 1, h = (6 + grow) * 8 - 2;
        rects.push_back({ x0, y0, w, h, DIM });
        rects.push_back({ x0, y0 + h / 2, w, 1, 0xFF1A1450 });
        const float st = host.get(P::dstart), en = host.get(P::dend);
        for (int x = 0; x < w && n > 0; ++x) {
            const size_t a0 = (size_t) ((double) x / w * n), a1 = std::max(a0 + 1, (size_t) ((double) (x + 1) / w * n));
            int lo = 255, hi = 0;
            for (size_t i = a0; i < a1 && i < n; ++i) { lo = std::min(lo, (int) src.preview[i]); hi = std::max(hi, (int) src.preview[i]); }
            const int ya = y0 + h - 1 - hi * (h - 1) / 255, yb = y0 + h - 1 - lo * (h - 1) / 255;
            const float fx = (float) x / w; const bool in = fx >= st && fx <= en;
            rects.push_back({ x0 + x, ya, 1, std::max(1, yb - ya + 1), in ? PALETTE[LGREEN] : PALETTE[GREY] });
        }
        rects.push_back({ x0 + (int) (st * (w - 1)), y0, 1, h, PALETTE[YELLOW] });
        rects.push_back({ x0 + (int) (en * (w - 1)), y0, 1, h, PALETTE[LRED] });
        const int at = s.digiShown.load();
        if (at >= 0 && n > 0) rects.push_back({ x0 + (int) ((double) at / n * (w - 1)), y0, 1, h, PALETTE[WHITE] });
        // where it comes from
        const int br = 12 + grow;
        if (host.canLoadFiles()) button(br, 0, "LOAD", B_SMP_LOAD, YELLOW, true);
        const bool rec = host.recording();
        button(br, 5, rec ? "{STOP}" : "{REC}", B_SMP_REC, rec ? WHITE : LRED, true);
        if (rec && (frame / 10) % 2 == 0) put(br, 5, "{", LRED, true);
        button(br, 12, "<", B_SMP_PREV, WHITE, true);
        put(br, 14, pad(src.builtinIndex >= 0 ? src.name : "BUILT-IN", 10), CYAN);
        button(br, 25, ">", B_SMP_NEXT, WHITE, true);
        put(br, 28, m == DIGI_PWM ? "OSC1=CARRIER" : m == DIGI_MAHONEY ? "VOICES=DC" : "", DGREY);
        int r = br + 1;
        for (int id : { P::dmode, P::drate, P::droot, P::dstart, P::dend, P::dloop, P::dgate, P::dtrack, P::dlevel, P::dram }) paramRow(r++, id);
    }

    // ---------------- the mixer ----------------
    void setFromFader(int id, float y) {
        host.set(id, fromNorm(id, (faderBot() - y) / (float) (faderBot() - faderTop())));
    }
    static std::string dbText(float v) {
        char b[16];
        if (v <= -47.9f) return "-INF";
        std::snprintf(b, sizeof b, std::fabs(v) < 9.95f ? "%+.1f" : "%+.0f", v); return b;
    }
    static std::string panText(float v) {
        char b[16]; const int p = (int) std::lround(v * 100);
        if (std::abs(p) < 2) return " C";
        std::snprintf(b, sizeof b, "%c%d", p < 0 ? 'L' : 'R', std::abs(p)); return b;
    }
    // a vertical fader in pixel columns x..x+w
    void fader(int x, int w, int id, uint8_t fill) {
        const int h = faderBot() - faderTop(), pos = (int) std::lround(toNorm(id, host.get(id)) * (h - 3));
        rects.push_back({ x, faderTop(), w, h, DIM });
        rects.push_back({ x + w / 2 - 1, faderTop() + 1, 2, h - 2, 0xFF1A1450 });   // the slot
        // 0 dB mark for the level faders
        const ParamInfo& p = params()[id];
        if (p.min < 0 && p.max > 0) { const int z = faderBot() - 2 - (int) std::lround(toNorm(id, 0) * (h - 3)); rects.push_back({ x - 2, z, 2, 1, PALETTE[GREY] }); }
        rects.push_back({ x + 1, faderBot() - 1 - pos, w - 2, pos, PALETTE[fill] });
        rects.push_back({ x, faderBot() - 3 - pos, w, 3, PALETTE[WHITE] });            // the cap
    }
    // a peak meter as LED segments: green, yellow from -6 dB, red from -1 dB
    void meter(int x, int w, float peak) {
        const float db = peak > 1e-6f ? 20.f * std::log10(peak) : -100.f;
        const float frac = std::clamp((db + 48.f) / 54.f, 0.f, 1.f);
        const int h = faderBot() - faderTop(), lit = (int) std::lround(frac * h);
        for (int y = 0; y + 2 <= h; y += 3) {
            const float at = (y + 1.f) / h;
            const float dbAt = at * 54.f - 48.f;
            const uint32_t on = dbAt > -1 ? PALETTE[LRED] : dbAt > -6 ? PALETTE[YELLOW] : PALETTE[LGREEN];
            rects.push_back({ x, faderBot() - y - 2, w, 2, y < lit ? on : DIM });
        }
    }
    // one mixer strip, w columns wide: the name, the level, a fader with its meter, pan, mute and solo
    void strip(int c, int w, const char* name, int lvl, int pan, int mute, int solo, int meterIdx) {
        Synth& s = host.synth();
        put(5, c, name, WHITE);
        const ParamInfo& lp = params()[lvl];
        put(6, c, lp.unit[0] == 'D' ? dbText(host.get(lvl)) : std::to_string((int) std::lround(host.get(lvl) * 100)) + "%", YELLOW);
        zone(6, c, c + w - 1, Z_PVAL, lvl);
        const int x = c * 8;
        fader(x + 2, 12, lvl, LBLUE);
        if (meterIdx == Synth::M_OUTL) { meter(x + 20, 7, s.meters[Synth::M_OUTL].load()); meter(x + 30, 7, s.meters[Synth::M_OUTR].load()); }
        else meter(x + 18, 8, s.meters[(size_t) meterIdx].load());
        const int pr = mixPanRow();
        for (int r = 7; r < pr; ++r) zone(r, c, c + 2, Z_VFADER, lvl);
        if (pan >= 0) {
            // the pan: a little track with the position marked
            const float pv = host.get(pan); const int tw = w * 8 - 6;
            rects.push_back({ x + 2, pr * 8 + 2, tw, 4, DIM });
            rects.push_back({ x + 1 + tw / 2, pr * 8 + 1, 2, 6, PALETTE[GREY] });
            rects.push_back({ x + 2 + (int) std::lround((pv + 1) * 0.5f * (tw - 4)), pr * 8, 4, 8, PALETTE[CYAN] });
            put(pr + 1, c + 1, panText(pv), CYAN);
            zone(pr, c, c + w - 1, Z_PVAL, pan); zone(pr + 1, c, c + w - 1, Z_PVAL, pan);
        }
        if (mute >= 0) { const bool m = host.get(mute) > .5f; put(pr + 2, c + 1, "M", m ? LRED : GREY, m); zone(pr + 2, c, c + 1, Z_PVAL, mute); }
        if (solo >= 0) { const bool so = host.get(solo) > .5f; put(pr + 2, c + 3, "S", so ? YELLOW : GREY, so); zone(pr + 2, c + 3, c + 4, Z_PVAL, solo); }
    }
    void pageMix() {
        strip(0, 5, "OSC1", P::m1lvl, P::m1pan, P::m1mute, P::m1solo, Synth::M_OSC1);
        strip(5, 5, "OSC2", P::m2lvl, P::m2pan, P::m2mute, P::m2solo, Synth::M_OSC2);
        strip(10, 5, "OSC3", P::m3lvl, P::m3pan, P::m3mute, P::m3solo, Synth::M_OSC3);
        strip(15, 5, "FILT", P::mflvl, P::mfpan, P::mfmute, P::mfsolo, Synth::M_FILTER);
        strip(21, 5, "ECHO", P::emix, -1, P::emute, -1, Synth::M_ECHO);
        strip(26, 5, "REV7", P::rret, -1, P::rmute, -1, Synth::M_REV);
        strip(32, 8, "MAIN", P::gain, -1, -1, -1, Synth::M_OUTL);
        const int pr = mixPanRow();
        put(pr + 1, 21, "RET", GREY); put(pr + 1, 26, "RET", GREY); put(pr + 1, 34, "L  R", GREY);
        // the channels that aren't sounding: an oscillator that's off, the filter with nothing routed into it
        for (int o = 0; o < 3; ++o) if (host.get(P::o1on + o * (P::o2on - P::o1on)) < .5f) for (int k = 0; k < 4; ++k) cell[5][o * 5 + k].fg = DGREY;
        if (!(host.get(P::o1flt) > .5f || host.get(P::o2flt) > .5f || host.get(P::o3flt) > .5f)) put(pr + 1, 16, "--", DGREY);
        for (int r = 5; r <= pr + 2; ++r) { put(r, 20, "|", DGREY); put(r, 31, "|", DGREY); }
    }

    // ---------------- the effects mixer: each channel's sends, the returns, the echo ----------------
    void sendRow(int r, const char* name, int e, int rv) {
        put(r, 1, name, LBLUE);
        for (int k = 0; k < 2; ++k) {
            const int id = k ? rv : e, c = k ? 24 : 8;
            put(r, c, pad(std::to_string((int) std::lround(host.get(id) * 100)) + "%", 4), WHITE); zone(r, c, c + 3, Z_PVAL, id);
            bars.push_back({ r, c + 4, c + 14, toNorm(id, host.get(id)), (uint8_t) (k ? CYAN : LGREEN), false });
            zone(r, c + 4, c + 15, Z_PBAR, id);
        }
    }
    void pageFx() {
        put(5, 1, "SEND", GREY); put(5, 8, "TO ECHO", LGREEN); put(5, 24, "TO REV7", CYAN);
        sendRow(6, "OSC 1", P::s1e, P::s1r); sendRow(7, "OSC 2", P::s2e, P::s2r);
        sendRow(8, "OSC 3", P::s3e, P::s3r); sendRow(9, "FILTER", P::sfe, P::sfr);
        put(10, 1, "--------------- RETURNS --------------", DGREY);
        int r = 11;
        paramRow(r++, P::emix, "ECHO RETURN"); paramRow(r++, P::emute, "ECHO MUTE"); paramRow(r++, P::erev);
        paramRow(r++, P::rret); paramRow(r++, P::rmute);
        put(r++, 1, "---------------- ECHO ----------------", DGREY);
        for (int id : { P::etime, P::esync, P::ediv, P::efb, P::etone }) paramRow(r++, id);
    }

    // ---------------- the REV7 ----------------
    void pageRev() {
        int r = 5;
        for (int id : { P::rprog, P::rtime, P::rhigh, P::rdly, P::rsize, P::rbal, P::rdiff, P::rdens, P::rlpf, P::rhpf, P::rgate, P::rret }) paramRow(r++, id);
        put(r, 1, "REV7 STYLE: 31.25 KHZ, 12 KHZ BAND", GREY);
        // a sketch of the response: the early reflections, then the decay (or the gate's burst)
        const int y0 = (r + 1) * 8 + 2, h = std::max(10, (rows - 3 - r - 1) * 8 - 4), x0 = 8, w = W - 16;
        if (h < 10 || y0 + h > height()) return;
        rects.push_back({ x0, y0, w, h, DIM });
        const int prog = (int) std::lround(host.get(P::rprog));
        const double rt = host.get(P::rtime), dly = host.get(P::rdly) / 1000.0, gate = host.get(P::rgate) / 1000.0;
        const double span = prog >= 6 ? std::max(0.3, gate * 2.0 + dly) : prog >= 4 ? 0.25 : rt * 1.1 + dly;
        const double bal = host.get(P::rbal);
        for (int x = 0; x < w; ++x) {
            const double t = span * x / w - dly; double lv = 0;
            if (t >= 0) {
                if (prog == 6) lv = t < gate ? 0.75 : 0;
                else if (prog == 7) lv = t < gate ? 0.15 + 0.85 * t / gate : 0;
                else if (prog >= 4) lv = std::exp(-t * 18) * (std::fmod(t * 300, 1.0) < 0.3 ? 1 : 0.4);
                else lv = (1 - bal) * std::exp(-t * 25) * (std::fmod(t * 400, 1.0) < 0.25 ? 1 : 0) + bal * std::pow(10.0, -3 * t / std::max(0.1, rt)) * std::min(1.0, t * 40);
            }
            const int bh = (int) std::lround(std::clamp(lv, 0.0, 1.0) * (h - 2));
            if (bh > 0) rects.push_back({ x0 + x, y0 + h - 1 - bh, 1, bh, PALETTE[prog >= 6 ? YELLOW : LGREEN] });
        }
    }

    // ---------------- the grid: the pattern as a piano roll ----------------
    bool gridHalf = false; int gridTop = 72;
    int gridRows() const { return std::max(6, rows - 12); }   // note rows between the header and the lanes
    int gridStep(float x) const { const int c = (int) (x / 8) - 4; return c < 0 || c >= 32 ? -1 : c / 2 + (gridHalf ? 16 : 0); }
    int gridNoteAt(float y) const { const int r = std::clamp((int) (y / 8) - 8, 0, gridRows() - 1); return gridTop - r; }
    void gridFit() {   // show where the pattern's notes are
        Synth& s = host.synth(); const int p = curPat(); int lo = 128, hi = -1;
        for (int i = 0; i < kSteps; ++i) { const Step st = s.step(p, i); if (st.on) { lo = std::min(lo, st.note); hi = std::max(hi, st.note); } }
        const int n = gridRows();
        if (hi < 0) gridTop = 60 + n / 2;
        else if (hi - lo + 1 <= n) gridTop = hi + (n - (hi - lo + 1)) / 2;
        else gridTop = hi;
        gridTop = std::clamp(gridTop, n - 1, 127);
    }
    bool gridErase = false; int gridLast = -1;
    void gridApply(int stepIdx, int note) {
        if (stepIdx < 0 || stepIdx >= kSteps) return;
        Synth& s = host.synth(); Step st = s.step(curPat(), stepIdx);
        if (gridErase) { if (st.on && st.note == note) { st.on = false; s.setStep(curPat(), stepIdx, st); } }
        else { st.on = true; st.note = note; s.setStep(curPat(), stepIdx, st); }
    }
    void gridDown(float x, float y) {
        const int i = gridStep(x), n = gridNoteAt(y); if (i < 0) return;
        const Step st = host.synth().step(curPat(), i);
        gridErase = st.on && st.note == n;                       // tapping a note erases; anywhere else paints
        gridApply(i, n); gridLast = i * 256 + n;
    }
    void gridDrag(float x, float y) {
        const int i = gridStep(x), n = gridNoteAt(y); if (i < 0) return;
        if (i * 256 + n == gridLast) return;
        gridLast = i * 256 + n;
        if (!gridErase) { const Step st = host.synth().step(curPat(), i); if (st.on && st.note == n) return; }
        gridApply(i, n);
    }
    void pageGrid() {
        Synth& s = host.synth();
        const int p = curPat(), len = std::clamp((int) std::lround(host.get(P::slen)), 1, kSteps), off = gridHalf ? 16 : 0;
        const bool run = s.seqRunning();
        button(5, 0, run ? "{STOP}" : "{PLAY}", run ? B_STOP : B_PLAY, run ? LRED : LGREEN, true);
        put(5, 7, "PAT", LBLUE); button(5, 11, "<", B_PAT_DN, WHITE, true); put(5, 13, num(p + 1, 2), WHITE); button(5, 16, ">", B_PAT_UP, WHITE, true);
        put(5, 19, "LEN", LBLUE); button(5, 23, "<", B_LEN_DN, WHITE, true); put(5, 25, num(len, 2), WHITE); button(5, 28, ">", B_LEN_UP, WHITE, true);
        button(5, 36, "CLR", B_CLEAR, LRED, true);
        button(6, 0, gridHalf ? "STEPS 17-32" : "STEPS 1-16 ", B_GRID_HALF, WHITE, true);
        put(6, 13, "NOTES", LBLUE); button(6, 19, " ^ ", B_GRID_UP, WHITE, true); button(6, 23, " v ", B_GRID_DN, WHITE, true);
        button(6, 28, "RND", B_RNDPAT, LGREEN, true); button(6, 32, "<<", B_SHL, WHITE, true); button(6, 35, ">>", B_SHR, WHITE, true);
        // the beat numbers
        for (int i = 0; i < 16; i += 4) put(7, 4 + 2 * i, std::to_string((off + i) / 4 + 1), GREY);
        const int nr = gridRows(), playing = s.seqStepShown.load();
        const uint32_t beatCol = 0xFF3A2F92, laneDim = 0xFF241B60;
        for (int k = 0; k < nr; ++k) {
            const int r = 8 + k, note = gridTop - k;
            const bool blackKey = isBlackNote(note);
            put(r, 0, noteName(note), note % 12 == 0 ? WHITE : blackKey ? DGREY : GREY);
            zone(r, 4, 35, Z_GRID, k);
            for (int i = 0; i < 16; ++i) {
                const int si = off + i; const Step st = s.step(p, si);
                const int x = (4 + 2 * i) * 8 + 1, y = r * 8 + 1;
                uint32_t col = si >= len ? 0xFF1C1650 : (i % 4 == 0 ? beatCol : blackKey ? laneDim : DIM);
                if (st.on && st.note == note) col = si >= len ? PALETTE[GREY] : PALETTE[st.accent ? YELLOW : LGREEN];
                rects.push_back({ x, y, 14, 6, col });
                if (st.on && st.note == note && st.slide) rects.push_back({ x + 14, y + 2, 2, 2, PALETTE[LGREEN] });
                if (k == 0 && st.on && st.note > gridTop) rects.push_back({ x + 4, y - 1, 6, 2, PALETTE[CYAN] });           // above the view
                if (k == nr - 1 && st.on && st.note < note) rects.push_back({ x + 4, y + 5, 6, 2, PALETTE[CYAN] });       // below
            }
        }
        // the playing step
        if (run && playing >= off && playing < off + 16) rects.push_back({ (4 + 2 * (playing - off)) * 8, 8 * 8, 1, nr * 8, PALETTE[WHITE] });
        // accent and slide lanes
        const int ra = 8 + nr, rs = ra + 1;
        put(ra, 0, "ACC", YELLOW); put(rs, 0, "SLD", LGREEN);
        for (int i = 0; i < 16; ++i) {
            const int si = off + i; const Step st = s.step(p, si);
            put(ra, 4 + 2 * i, st.accent ? "!" : ".", st.accent ? YELLOW : DGREY);
            put(rs, 4 + 2 * i, st.slide ? "~" : ".", st.slide ? LGREEN : DGREY);
            zone(ra, 4 + 2 * i, 5 + 2 * i, Z_GRID_ACC, si); zone(rs, 4 + 2 * i, 5 + 2 * i, Z_GRID_SLD, si);
        }
    }
    static bool isBlackNote(int n) { const int k = ((n % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

    void drawScope(uint32_t* px, int y0, int h) {
        Synth& s = host.synth();
        const int x0 = 8, w = W - 16;
        const uint32_t grid = 0xFF2A2066, line = PALETTE[LGREEN];
        for (int x = x0; x < x0 + w; x += 2) px[(y0 + h / 2) * W + x] = grid;
        // trigger on a rising zero crossing so the wave stands still
        const int wpos = s.scopeW.load();
        int start = wpos - 512 + 1;
        for (int k = 0; k < 200; ++k) {
            const int i = wpos - 300 - k;
            if (s.scope[(size_t) ((i - 1) & 511)].load() < 0 && s.scope[(size_t) (i & 511)].load() >= 0) { start = i; break; }
        }
        int prevY = -1;
        for (int x = 0; x < w; ++x) {
            const float v = s.scope[(size_t) ((start + x) & 511)].load();
            const int y = std::clamp(y0 + h / 2 - (int) std::lround(v * h * 0.9f), y0, y0 + h - 1);
            if (prevY < 0) prevY = y;
            for (int yy = std::min(y, prevY); yy <= std::max(y, prevY); ++yy) px[yy * W + x0 + x] = line;
            prevY = y;
        }
    }

    // ---------------- taps ----------------
    void tap(const Zone& z) {
        Synth& s = host.synth();
        switch (z.kind) {
            case Z_TAB: page = z.a; patList = false; if (page == 0) showPreset(s.program.load()); break;
            case Z_CAT: cat = z.a; listScroll = 0; break;
            case Z_PRESET: case Z_LIST: {
                const Zone pz = zoneAtKind(lastX, lastY, Z_PRESET);
                if (pz.kind == Z_PRESET) { host.loadPreset(pz.a); message("LOAD\"" + std::string(PRESETS[pz.a].name) + "\",8,1"); if (onPresetChanged) onPresetChanged(); }
                break;
            }
            case Z_PNAME: case Z_PVAL: case Z_PBAR: case Z_VFADER: {
                const ParamInfo& p = params()[z.a];
                if (lastTapParam == z.a && frame - lastTapFrame < 12 && p.kind != TOGGLE) { host.set(z.a, p.def); lastTapParam = -1; break; }
                lastTapParam = z.a; lastTapFrame = frame;
                if (p.kind == TOGGLE) host.set(z.a, host.get(z.a) > .5f ? 0.f : 1.f);
                else if (p.kind == CHOICE && z.kind != Z_PBAR && z.kind != Z_VFADER) { const float n = host.get(z.a) + 1; host.set(z.a, n > p.max ? p.min : n); }
                break;
            }
            case Z_STEP_NOTE: { Step st = s.step(curPat(), z.a); st.on = !st.on; s.setStep(curPat(), z.a, st); break; }
            case Z_STEP_ACC: { Step st = s.step(curPat(), z.a); st.accent = !st.accent; s.setStep(curPat(), z.a, st); break; }
            case Z_STEP_SLIDE: { Step st = s.step(curPat(), z.a); st.slide = !st.slide; s.setStep(curPat(), z.a, st); break; }
            case Z_GRID_ACC: { Step st = s.step(curPat(), z.a); st.accent = !st.accent; s.setStep(curPat(), z.a, st); break; }
            case Z_GRID_SLD: { Step st = s.step(curPat(), z.a); st.slide = !st.slide; s.setStep(curPat(), z.a, st); break; }
            case Z_PATLIST: case Z_PATITEM: {
                const Zone pz = zoneAtKind(lastX, lastY, Z_PATITEM);
                if (pz.kind == Z_PATITEM) {
                    s.loadPatternPreset(pz.a, curPat()); host.set(P::slen, (float) s.patLen[curPat()]);
                    patList = false; message("PATTERN: " + std::string(PATTERNS[pz.a].name));
                }
                break;
            }
            case Z_BTN: button(z.a); break;
            default: break;
        }
    }
    Zone zoneAtKind(float x, float y, int kind) const {
        const int c = (int) (x / 8), r = (int) (y / 8);
        for (const Zone& z : zones) if (z.kind == kind && z.r == r && c >= z.c0 && c <= z.c1) return z;
        return {};
    }
    void button(int id) {
        Synth& s = host.synth(); const int p = curPat(), len = std::clamp((int) std::lround(host.get(P::slen)), 1, kSteps);
        auto stepPresetBy = [&](int d) {
            const int n = presetCount(); const int i = ((s.program.load() + d) % n + n) % n;
            host.loadPreset(i); showPreset(i); message("LOAD\"" + std::string(PRESETS[i].name) + "\",8,1"); if (onPresetChanged) onPresetChanged();
        };
        auto transpose = [&](int d) { for (int i = 0; i < kSteps; ++i) { Step st = s.step(p, i); st.note = std::clamp(st.note + d, 0, 127); s.setStep(p, i, st); } };
        switch (id) {
            case B_PREV: stepPresetBy(-1); break;
            case B_NEXT: stepPresetBy(1); break;
            case B_INIT: host.initPatch(); message("NEW"); if (onPresetChanged) onPresetChanged(); break;
            case B_RAND: host.randomize(); message("RANDOM PATCH"); if (onPresetChanged) onPresetChanged(); break;
            case B_UP: listScroll = std::max(0, listScroll - listRows()); break;
            case B_DN: listScroll = std::clamp(listScroll + listRows(), 0, std::max(0, catSize() - listRows())); break;
            case B_PLAY: s.post(Synth::EV_SEQ_PLAY, 0, 0); break;
            case B_STOP: s.post(Synth::EV_SEQ_STOP, 0, 0); break;
            case B_PAT_DN: host.set(P::spat, (float) std::max(1, p)); break;
            case B_PAT_UP: host.set(P::spat, (float) std::min(kPatterns, p + 2)); break;
            case B_LEN_DN: host.set(P::slen, (float) std::max(1, len - 1)); break;
            case B_LEN_UP: host.set(P::slen, (float) std::min(kSteps, len + 1)); break;
            case B_LOAD: patList = true; break;
            case B_BACK: patList = false; break;
            case B_CLEAR: for (int i = 0; i < kSteps; ++i) { Step st; st.note = 48; s.setStep(p, i, st); } break;
            case B_RNDPAT: {
                static const int scale[7] = { 0, 2, 3, 5, 7, 8, 10 };
                uint32_t r = (uint32_t) (frame * 2654435761u) | 1;
                auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
                const int root = 36 + (int) (rnd() % 12);
                for (int i = 0; i < kSteps; ++i) {
                    Step st; st.on = rnd() % 100 < 70; st.note = root + scale[rnd() % 7] + 12 * (int) (rnd() % 3 == 0);
                    st.accent = rnd() % 100 < 25; st.slide = rnd() % 100 < 15; s.setStep(p, i, st);
                }
                message("RANDOM PATTERN"); break;
            }
            case B_SHL: { Step f = s.step(p, 0); for (int i = 0; i < len - 1; ++i) s.setStep(p, i, s.step(p, i + 1)); s.setStep(p, len - 1, f); break; }
            case B_SHR: { Step l = s.step(p, len - 1); for (int i = len - 1; i > 0; --i) s.setStep(p, i, s.step(p, i - 1)); s.setStep(p, 0, l); break; }
            case B_TR_DN: transpose(-1); break;
            case B_TR_UP: transpose(1); break;
            case B_OCT_DN: transpose(-12); break;
            case B_OCT_UP: transpose(12); break;
            case B_COPY: clip.clear(); for (int i = 0; i < kSteps; ++i) clip.push_back(s.step(p, i)); message("PATTERN COPIED"); break;
            case B_PASTE: if (!clip.empty()) { for (int i = 0; i < kSteps; ++i) s.setStep(p, i, clip[(size_t) i]); message("PATTERN PASTED"); } break;
            case B_PANIC: s.post(Synth::EV_PANIC, 0, 0); message("ALL NOTES OFF"); break;
            case B_SMP_LOAD: host.loadSampleFile(); break;
            case B_GRID_HALF: gridHalf = !gridHalf; break;
            case B_GRID_UP: gridTop = std::min(127, gridTop + 12); break;
            case B_GRID_DN: gridTop = std::max(gridRows() - 1, gridTop - 12); break;
            case B_SMP_REC:
                if (host.recording()) { host.setRecording(false); message("SAMPLED: " + s.source.name); }
                else { host.setRecording(true); }
                break;
            case B_SMP_PREV: case B_SMP_NEXT: {
                const int b = s.source.builtinIndex < 0 ? 0 : s.source.builtinIndex + (id == B_SMP_NEXT ? 1 : -1);
                s.loadBuiltin(b); if (host.get(P::dmode) < .5f) host.set(P::dmode, DIGI_VOL4);
                message("LOAD\"" + s.source.name + "\",8,1"); break;
            }
            default: break;
        }
    }
};

}  // namespace sid
