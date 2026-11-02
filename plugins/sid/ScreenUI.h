// Omega SID 64: the C64 screen. A 40 x 25 text screen at 320 x 200 pixels in the C64's 16 colours, shared by the
// plugin and the Android app (each draws the monitor and the keyboard around it and passes pointer events in
// screen pixels). Pages: PRG (presets), OS1-OS3, FLT, MOD, ARP (arp + echo), CHP (chip), MIX (mixer + scope), SEQ (tracker).
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
    static constexpr int W = 320, H = 200, COLS = 40, ROWS = 25;

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
        for (int r = 0; r < ROWS; ++r) for (int c = 0; c < COLS; ++c) {
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
            for (int y = std::max(0, q.y); y < std::min(H, q.y + q.h); ++y)
                for (int x = std::max(0, q.x); x < std::min(W, q.x + q.w); ++x) px[y * W + x] = q.col;
        if (page == 8) drawScope(px, 21 * 8 + 1, 15);
    }

    // ---- pointer, in screen pixels ----
    void down(float x, float y) {
        downX = lastX = x; downY = lastY = y; moved = false;
        hit = zoneAt(x, y);
        if (hit.kind == Z_PBAR) setFromBar(hit, x);
        if (hit.kind == Z_VFADER) setFromFader(hit.a, y);
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
            case Z_LIST: case Z_PRESET: listScroll = std::clamp((int) std::lround(startVal - dy / 8.f), 0, std::max(0, catSize() - LIST_ROWS)); break;
            case Z_PATLIST: case Z_PATITEM: patScroll = std::clamp((int) std::lround(startVal - dy / 8.f), 0, std::max(0, patternPresetCount() - 2 * LIST_ROWS)); break;
            default: break;
        }
    }
    void up(float x, float y) {
        move(x, y);
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
        } else if (z.kind == Z_LIST || z.kind == Z_PRESET) listScroll = std::clamp(listScroll - (int) steps, 0, std::max(0, catSize() - LIST_ROWS));
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
        if (idx < listScroll || idx >= listScroll + LIST_ROWS) listScroll = std::max(0, idx - LIST_ROWS / 2);
    }

private:
    Host& host;
    struct Cell { char ch = ' '; uint8_t fg = LBLUE; bool inv = false; };
    Cell cell[ROWS][COLS];
    enum ZK { Z_NONE, Z_TAB, Z_PNAME, Z_PVAL, Z_PBAR, Z_CAT, Z_PRESET, Z_LIST, Z_BTN, Z_STEP_NOTE, Z_STEP_ACC, Z_STEP_SLIDE, Z_PATLIST, Z_PATITEM, Z_VFADER };
    struct Zone { int r = -1, c0 = 0, c1 = 0, kind = Z_NONE, a = -1; };
    struct Bar { int r, c0, c1; float frac; uint8_t col; bool center; };
    struct Rect { int x, y, w, h; uint32_t col; };
    std::vector<Zone> zones; std::vector<Bar> bars; std::vector<Rect> rects;
    static constexpr int FADER_TOP = 7 * 8, FADER_BOT = 18 * 8;   // the mixer's faders, in pixels
    static constexpr uint32_t DIM = 0xFF2A2066;
    std::vector<std::string> cats;
    int cat = 0, listScroll = 0, patScroll = 0; bool patList = false;
    uint64_t frame = 0, msgUntil = 0; std::string msg;
    Zone hit; float downX = 0, downY = 0, lastX = 0, lastY = 0, startNorm = 0, startVal = 0; bool moved = false;
    uint64_t lastTapFrame = 0; int lastTapParam = -1;
    static constexpr int LIST_ROWS = 17;

    enum Btn { B_PREV = 1, B_NEXT, B_INIT, B_RAND, B_UP, B_DN, B_PLAY, B_STOP, B_PAT_DN, B_PAT_UP, B_LEN_DN, B_LEN_UP, B_LOAD, B_CLEAR,
               B_RNDPAT, B_SHL, B_SHR, B_TR_DN, B_TR_UP, B_OCT_DN, B_OCT_UP, B_BACK, B_PANIC, B_COPY, B_PASTE };
    std::vector<Step> clip;

    int curPat() { return std::clamp((int) std::lround(host.get(P::spat)) - 1, 0, kPatterns - 1); }
    int catSize() { int n = 0; for (int i = 0; i < presetCount(); ++i) if (cats[(size_t) cat] == PRESETS[i].cat) ++n; return n; }

    // ---------------- text ----------------
    void clear() { for (auto& r : cell) for (auto& c : r) c = Cell {}; zones.clear(); bars.clear(); rects.clear(); }
    void put(int r, int c, const std::string& s, uint8_t col = LBLUE, bool inv = false) {
        if (r < 0 || r >= ROWS) return;
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
        return {};
    }

    // one parameter: NAME (cols 1-16) VALUE (18-27) BAR (28-38)
    void paramRow(int r, int id, const char* label = nullptr) {
        const ParamInfo& p = params()[id];
        const float v = host.get(id);
        put(r, 1, pad(label ? label : p.name, 16), LBLUE);
        zone(r, 0, 17, Z_PNAME, id);
        const std::string val = formatValue(id, v);
        if (p.kind == TOGGLE) {
            put(r, 18, v > .5f ? "ON " : "OFF", v > .5f ? WHITE : GREY);
            put(r, 28, v > .5f ? "[{]" : "[ ]", v > .5f ? YELLOW : GREY);
            zone(r, 18, 39, Z_PVAL, id);
            return;
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
        const float n = (x - z.c0 * 8.f) / ((38 - 28 + 1) * 8.f);
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
        static const char* tabs[10] = { "PRG", "OS1", "OS2", "OS3", "FLT", "MOD", "ARP", "CHP", "MIX", "SEQ" };
        for (int t = 0; t < 10; ++t) { put(3, t * 4, tabs[t], t == page ? BLUE : LBLUE, false); if (t == page) for (int k = 0; k < 3; ++k) { cell[3][t * 4 + k].inv = true; cell[3][t * 4 + k].fg = WHITE; } zone(3, t * 4, t * 4 + 3, Z_TAB, t); }
        switch (page) {
            case 0: pagePresets(); break;
            case 1: case 2: case 3: pageOsc(page - 1); break;
            case 4: pageFilter(); break;
            case 5: pageMod(); break;
            case 6: pageArp(); break;
            case 7: pageChip(); break;
            case 8: pageMix(); break;
            default: if (patList) pagePatterns(); else pageSeq(); break;
        }
        // help / messages
        if (page == 9 && !patList) return;   // the tracker uses the whole screen
        {
            if (frame < msgUntil) put(23, 0, pad(msg, 40), WHITE);
            else put(23, 0, page == 0 ? "TAP A PRESET.  DRAG THE LIST TO SCROLL." : page == 8 ? "DRAG FADERS. M=MUTE S=SOLO. 2X TAP=RESET" : "DRAG BARS. TAP TO TOGGLE. 2X TAP=RESET", GREY);
        }
        // the preset line
        char pl[64];
        std::snprintf(pl, sizeof pl, "%03d %s", s.program.load() + 1, s.presetName.c_str());
        button(24, 0, "<", B_PREV, WHITE, true);
        put(24, 2, pad(pl, 31), YELLOW);
        button(24, 34, ">", B_NEXT, WHITE, true);
        button(24, 36, "INI", B_INIT, LBLUE);
        zone(24, 2, 32, Z_TAB, 0);
    }

    void pagePresets() {
        for (int k = 0; k < (int) cats.size() && k < 18; ++k) {
            put(5 + k, 0, pad(cats[(size_t) k], 8), k == cat ? WHITE : LBLUE, k == cat);
            zone(5 + k, 0, 8, Z_CAT, k);
        }
        int idx = 0, row = 0; const int cur = host.synth().program.load();
        for (int i = 0; i < presetCount(); ++i) {
            if (cats[(size_t) cat] != PRESETS[i].cat) continue;
            if (idx >= listScroll && row < LIST_ROWS) {
                const bool sel = i == cur;
                put(5 + row, 10, pad(std::string(sel ? "{" : " ") + PRESETS[i].name, 30), sel ? WHITE : LBLUE);
                zone(5 + row, 10, 39, Z_PRESET, i);
                ++row;
            }
            ++idx;
        }
        // the list scrolls when dragged anywhere in it (presets also take taps)
        for (int r = 5; r < 5 + LIST_ROWS; ++r) zones.insert(zones.begin(), Zone { r, 10, 39, Z_LIST, 0 });
        const int n = catSize();
        if (n > LIST_ROWS) {
            char b[32]; std::snprintf(b, sizeof b, "%d-%d OF %d", listScroll + 1, std::min(n, listScroll + LIST_ROWS), n);
            put(22, 10, b, GREY);
            button(22, 26, " ^ ", B_UP, WHITE, true); button(22, 30, " v ", B_DN, WHITE, true);
        }
        button(22, 34, "RND", B_RAND, LGREEN, true);
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
        put(r++, 1, "---------------- ECHO ----------------", DGREY);
        for (int id : { P::emix, P::etime, P::esync, P::ediv, P::efb, P::etone }) paramRow(r++, id);
    }
    void pageChip() {
        int r = 5;
        for (int id : { P::model, P::curve, P::drive, P::clock, P::adsrbug, P::vol, P::quality, P::spread, P::gain }) paramRow(r++, id);
        ++r;
        regDump(r);
        button(22, 1, "PANIC: ALL NOTES OFF", B_PANIC, LRED, true);
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
            const int r = 9 + (i % 16), c = i < 16 ? 0 : 20;
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
        for (int k = 0; k < 2 * LIST_ROWS; ++k) {
            const int i = patScroll + k; if (i >= patternPresetCount()) break;
            const int r = 6 + (k % LIST_ROWS), c = k < LIST_ROWS ? 0 : 20;
            put(r, c, pad(PATTERNS[i].name, 19), LBLUE);
            zone(r, c, c + 19, Z_PATITEM, i);
        }
        for (int r = 6; r < 6 + LIST_ROWS; ++r) zones.insert(zones.begin(), Zone { r, 0, 39, Z_PATLIST, 0 });
    }
    // ---------------- the mixer ----------------
    void setFromFader(int id, float y) {
        host.set(id, fromNorm(id, (FADER_BOT - y) / (float) (FADER_BOT - FADER_TOP)));
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
        const int h = FADER_BOT - FADER_TOP, pos = (int) std::lround(toNorm(id, host.get(id)) * (h - 3));
        rects.push_back({ x, FADER_TOP, w, h, DIM });
        rects.push_back({ x + w / 2 - 1, FADER_TOP + 1, 2, h - 2, 0xFF1A1450 });   // the slot
        // 0 dB mark for the level faders
        const ParamInfo& p = params()[id];
        if (p.min < 0 && p.max > 0) { const int z = FADER_BOT - 2 - (int) std::lround(toNorm(id, 0) * (h - 3)); rects.push_back({ x - 2, z, 2, 1, PALETTE[GREY] }); }
        rects.push_back({ x + 1, FADER_BOT - 1 - pos, w - 2, pos, PALETTE[fill] });
        rects.push_back({ x, FADER_BOT - 3 - pos, w, 3, PALETTE[WHITE] });            // the cap
    }
    // a peak meter as LED segments: green, yellow from -6 dB, red from -1 dB
    void meter(int x, int w, float peak) {
        const float db = peak > 1e-6f ? 20.f * std::log10(peak) : -100.f;
        const float frac = std::clamp((db + 48.f) / 54.f, 0.f, 1.f);
        const int h = FADER_BOT - FADER_TOP, lit = (int) std::lround(frac * h);
        for (int y = 0; y + 2 <= h; y += 3) {
            const float at = (y + 1.f) / h;
            const float dbAt = at * 54.f - 48.f;
            const uint32_t on = dbAt > -1 ? PALETTE[LRED] : dbAt > -6 ? PALETTE[YELLOW] : PALETTE[LGREEN];
            rects.push_back({ x, FADER_BOT - y - 2, w, 2, y < lit ? on : DIM });
        }
    }
    void strip(int c, const char* name, int lvl, int pan, int mute, int solo, int meterIdx) {
        Synth& s = host.synth();
        put(5, c, name, WHITE);
        const ParamInfo& lp = params()[lvl];
        put(6, c, lp.unit[0] == 'D' ? dbText(host.get(lvl)) : std::to_string((int) std::lround(host.get(lvl) * 100)) + "%", YELLOW);
        zone(6, c, c + 4, Z_PVAL, lvl);
        const int x = c * 8;
        fader(x + 2, 14, lvl, LBLUE);
        if (meterIdx == Synth::M_OUTL) { meter(x + 20, 7, s.meters[Synth::M_OUTL].load()); meter(x + 29, 7, s.meters[Synth::M_OUTR].load()); }
        else meter(x + 22, 10, s.meters[(size_t) meterIdx].load());
        for (int r = 7; r < 18; ++r) zone(r, c, c + 2, Z_VFADER, lvl);
        if (pan >= 0) {
            // the pan: a little track with the position marked
            const float pv = host.get(pan);
            rects.push_back({ x + 2, 18 * 8 + 2, 36, 4, DIM });
            rects.push_back({ x + 19, 18 * 8 + 1, 2, 6, PALETTE[GREY] });
            rects.push_back({ x + 2 + (int) std::lround((pv + 1) * 0.5f * 32), 18 * 8, 4, 8, PALETTE[CYAN] });
            put(19, c + 1, panText(pv), CYAN);
            zone(18, c, c + 4, Z_PVAL, pan); zone(19, c, c + 4, Z_PVAL, pan);
        }
        if (mute >= 0) { const bool m = host.get(mute) > .5f; put(20, c + 1, "M", m ? LRED : GREY, m); zone(20, c, c + 1, Z_PVAL, mute); }
        if (solo >= 0) { const bool so = host.get(solo) > .5f; put(20, c + 3, "S", so ? YELLOW : GREY, so); zone(20, c + 3, c + 4, Z_PVAL, solo); }
    }
    void pageMix() {
        strip(0, "OSC1", P::m1lvl, P::m1pan, P::m1mute, P::m1solo, Synth::M_OSC1);
        strip(6, "OSC2", P::m2lvl, P::m2pan, P::m2mute, P::m2solo, Synth::M_OSC2);
        strip(12, "OSC3", P::m3lvl, P::m3pan, P::m3mute, P::m3solo, Synth::M_OSC3);
        strip(18, "FILT", P::mflvl, P::mfpan, P::mfmute, P::mfsolo, Synth::M_FILTER);
        strip(26, "ECHO", P::emix, -1, P::emute, -1, Synth::M_ECHO);
        strip(33, "MAIN", P::gain, -1, -1, -1, Synth::M_OUTL);
        put(19, 26, "RET", GREY); put(19, 33, "L R", GREY);
        // the channels that aren't sounding: an oscillator that's off, the filter with nothing routed into it
        for (int o = 0; o < 3; ++o) if (host.get(P::o1on + o * (P::o2on - P::o1on)) < .5f) for (int k = 0; k < 4; ++k) cell[5][o * 6 + k].fg = DGREY;
        if (!(host.get(P::o1flt) > .5f || host.get(P::o2flt) > .5f || host.get(P::o3flt) > .5f)) put(19, 19, " -- ", DGREY);
        for (int r = 5; r <= 20; ++r) { put(r, 24, "|", DGREY); put(r, 31, "|", DGREY); }
    }
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
            case B_UP: listScroll = std::max(0, listScroll - LIST_ROWS); break;
            case B_DN: listScroll = std::clamp(listScroll + LIST_ROWS, 0, std::max(0, catSize() - LIST_ROWS)); break;
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
            default: break;
        }
    }
};

}  // namespace sid
