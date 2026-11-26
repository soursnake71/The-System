// RX5 plugin editor (see FiveEditor.h). The menus and pages follow five/app.js item for item.
#include "FiveEditor.h"

namespace five {

/* ================================================================================================ small helpers */
static juce::String p2(int n) { return juce::String(n).paddedLeft('0', 2); }
static juce::String p3(int n) { return juce::String(n).paddedLeft('0', 3); }
static juce::String L16(const juce::String& s) { return (s + juce::String::repeatedString(" ", 16)).substring(0, 16); }
static juce::String songL(int n) { return juce::String::charToString((juce::juce_wchar) ('A' + n)); }
static juce::String vname(int v) { return VOICE_NAMES[juce::jlimit(0, NV - 1, v)]; }
static juce::String sgn(double x) { return (x > 0 ? "+" : "") + juce::String((int) jsround(x)); }
static juce::String pct(double x) { return juce::String((int) jsround(x * 100)) + " %"; }
static juce::String fmtCents(double c) { return (c >= 0 ? "+" : "-") + juce::String((int) jsround(std::fabs(c))).paddedLeft('0', 4) + " cent"; }
static juce::String fmtPan(double p) { const int n = (int) jsround(p * 7); return n == 0 ? juce::String("CENTER") : n < 0 ? "LEFT " + juce::String(-n) : "RIGHT " + juce::String(n); }
static juce::String noteName(int n) { static const char* N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(N[n % 12]) + juce::String(n / 12 - 2); }
static juce::String QN(int i) { return QUANTIZE[juce::jlimit(0, NUM_QUANTIZE - 1, i)].name; }
static int QT(int i) { return QUANTIZE[juce::jlimit(0, NUM_QUANTIZE - 1, i)].ticks; }
static const char* const STEP_CH[NCH] = { "BD", "SD", "RIM", "TOM1", "TOM2", "TOM3", "TOM4", "HH", "RIDE", "CRSH", "TB/C", "SH/C" };
static const char* const CH_LBL[NCH] = { "BD", "SD", "RIM", "TOM 1", "TOM 2", "TOM 3", "TOM 4", "HI HAT", "RIDE", "CRASH", "TMBRN\nCLAPS", "SHKR\nCWBEL" };
static const char* const PAD_LBL[NCH] = { "BD", "SD", "RIM", "TOM 1", "TOM 2", "TOM 3", "TOM 4", "HI HAT", "RIDE", "CRASH", "TAMBOURINE\nCLAPS", "SHAKER\nCOWBELL" };
static const char* const SYNCS[2] = { "int", "midi" };
static const char* const BULKS[6] = { "ALL", "PATTERN", "SONG", "VOICES", "FX", "RX5 DUMP" };
static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

// panel geometry (five/app.js, from the photo)
static float GX(int k) { return 98.3f + 32.9f * k; }
static const float FX[5] = { 328, 423, 518, 613, 707 }, FY[4] = { 571, 606, 641, 676 }, KX[3] = { 800, 893, 987 };
static const float PAD_X[12] = { 135, 231, 327, 422, 517, 612, 707, 801, 896, 991, 1085, 1180 };
static const std::vector<std::pair<const char*, float>> MODE_KEYS = { {"PTN", 140}, {"SONG", 191}, {"EDIT\nVOICE", 243}, {"CAS", 294}, {"CART", 345}, {"MIDI", 396}, {"REV", 447},
    {"DAMP", 497}, {"DLY", 547}, {"MULTI", 820}, {"INT\nSYNC", 921}, {"MIDI\nSYNC", 972}, {"HOST\nSYNC", 1022}, {"RUN", 1174} };
static const std::vector<std::vector<const char*>> FKEYS = {
    {"PATTERN", "SONG"}, {"REAL TIME WRITE", "EDIT SONG"}, {"STEP WRITE", "INSERT"}, {"EDIT VOICE"}, {"KEY ASSIGN"},
    {"QUANTIZE", "DELETE"}, {"SWING", "REPEAT"}, {"CLICK", "TEMPO CHANGE"}, {"CHAIN"}, {"CASSETTE", "CARTRIDGE"},
    {"EDIT PATTERN", "VOLUME CHANGE"}, {"CLEAR"}, {"COPY"}, {"SYNC"}, {"MIDI"}, {"REVERSE"}, {"DAMP"}, {"TEMPO"}, {"UTILITY"}, {"JOB"} };
static const char* const KEYPAD[12][2] = { {"7", "1/32"}, {"8", "1/48"}, {"9", "OFF"}, {"4", "1/12"}, {"5", "1/16"}, {"6", "1/24"}, {"1", "1/4"}, {"2", "1/6"}, {"3", "1/8"}, {"-1", "NO"}, {"0", "1/2"}, {"+1", "YES"} };
// the printed legend: columns of [header, menu, [text, item]]
struct LegendLine { const char* text; double no; };
struct LegendGroup { const char* head; const char* menu; std::vector<LegendLine> lines; };
static const std::vector<std::pair<float, std::vector<LegendGroup>>> LEGEND = {
    {128, {{"SONG", "song", {{"01 SEARCH MARK", 1}}}, {"EDIT SONG", "esong", {{"01 SEARCH MARK", 1}, {"02 SET MARK", 2}, {"03 SONG NAME", 3}, {"04 INITIAL TEMPO", 4}}}}},
    {200, {{"EDIT VOICE", "voice", {{"01 SELECT VOICE", 1}, {"   WAVEFORM", 1.1}, {"02 PITCH", 2}, {"03 EG  1 AR 2 D1R 3 D1L", 3.1}, {"   4 D2R 5 RR 6 GATE", 3.4}, {"04 BEND 1 RATE 2 RANGE", 4.1}}}}},
    {292, {{" ", "voice", {{"05 VOICE LEVEL", 5}, {"06 LOOP", 6}, {"07 STORE VOICE", 7}, {"08 INITIALIZE VOICE", 8}, {"09 RECALL EDIT", 9}, {"10 REVERSE", 10}, {"11 LOAD WAV", 11}, {"12 BITS", 12}}}}},
    {372, {{"CHAIN", "chain", {{"01 PLAY", 1}, {"02 EDIT", 2}, {"03 CHAIN NAME", 3}, {"04 CLEAR", 4}}}}},
    {430, {{"UTILITY", "util", {{"01 REMAINING PATTERN", 1}, {"02 REMAINING SONG", 2}, {"03 MEMORY PROTECT", 3}, {"04 CHANGE VOICE", 4}, {"05 CLEAR ALL PATTERNS", 5}, {"06 CLEAR ALL SONGS", 6}}}}},
    {521, {{"KEY ASSIGN", "key", {{"01 VOICE ASSIGN", 1}, {"02 PARAM ASSIGN(*)", 2.1}, {"03 MULTI VOICE", 3}, {"04 MULTI STEP", 4}, {"05 ACCENT LEVEL", 5.1}, {"06 OUTPUT CH ASSIGN", 6}}}}},
    {614, {{" ", "key", {{"07 SAVE KEY DATA", 7}, {"08 LOAD KEY DATA", 8}, {"09 COPY VOICE", 9}, {"*1 PITCH 2 LEVEL", 2.1}, {"  3 ATTACK 4 DECAY", 2.3}, {"10 PAN", 10}, {"11 REV  12 DLY SEND", 11}, {"13 PAD VELOCITY", 13}}}}},
    {688, {{"CASSETTE/CARTRIDGE", "cart", {{"21 SAVE FILE", 21}, {"22 LOAD FILE", 22}, {"23 LOAD WAV VOICE", 23}, {"24 VOICE BANK", 24}, {"25 LOAD KIT", 25}}}}},
    {790, {{"MIDI", "midi", {{"01 CH MESSAGE", 1}, {"02 RECEIVE CH", 2}, {"03 TRANSMIT CH", 3}, {"04 NOTE NUMBER ASSIGN", 4.1}, {"05 INITIALIZE NOTE", 5}, {"06 EG VELOCITY", 6}, {"07 GATE TIME", 7}}}}},
    {893, {{" ", "midi", {{"08 DEVICE NUMBER", 8}, {"09 RECEIVE BULK", 9}, {"10 TRANSMIT BULK", 10}, {"11 CLOCK OUT", 11}, {"12 NOTES OUT", 12}, {"15 SYX FILE", 15}}}}},
    {972, {{"JOB", "job", {{"01 IMPORT MIDI", 1}, {"02 EXPORT MIDI", 2}, {"03 EXPORT WAV", 3}, {"04 RANDOMIZE", 4}, {"05 INIT ALL", 5}}}}},
};

/* ================================================================================================ construction */
FivePanel::FivePanel(FiveProcessor& p) : proc(p), U(p.ui) {
    setSize(W, H); setWantsKeyboardFocus(true);
    build(); buildMenus();
    proc.onFlash = [this](const juce::String& a, const juce::String& b) { flash(a, b, 1600); };
}
FivePanel::~FivePanel() { proc.onFlash = nullptr; }

void FivePanel::build() {
    regions.clear();
    auto add = [&](float x, float y, float w, float h, Kind k, const juce::String& id, double no = 0) { regions.push_back({ { x, y, w, h }, k, id, no }); };
    const char* left[4] = { "VOLUME", "CLICK", "TEMPO", "DATA" };
    for (int i = 0; i < 4; i++) add(GX(i) - 15, 160, 30, 138, kFader, left[i]);
    for (int c = 0; c < NCH; c++) add(GX(23 + c) - 15, 160, 30, 138, kFader, "CH" + juce::String(c));
    for (const auto& m : MODE_KEYS) add(m.second - 15.5f, 412, 31, 18, kMode, juce::String(m.first).replace("\n", " "));
    add(576, 400, 180, 46, kLcd, "LCD");
    for (size_t i = 0; i < FKEYS.size(); i++) add(FX[i % 5] - 40, FY[i / 5] - 10.5f, 80, 21, kFn, FKEYS[i][0]);
    for (int i = 0; i < 12; i++) { const juce::String n = KEYPAD[i][0]; add(KX[i % 3] - 40, FY[i / 3] - 10.5f, 80, 21, kKeypad, n == "-1" ? "NO" : n == "+1" ? "YES" : "N" + n); }
    add(95, 563, 82, 56, kBig, "ACC1"); add(95, 634, 82, 56, kBig, "ACC2"); add(1140, 562, 82, 56, kBig, "STOP"); add(1140, 634, 82, 56, kBig, "START");
    for (int i = 0; i < NV; i++) add(PAD_X[i % 12] - 40, i < 12 ? 721.f : 794.f, 80, 58, kPad, "PAD" + juce::String(i));
    add(1160, 498, 36, 28, kDice, "RANDOM");
    for (const auto& col : LEGEND) {
        float y = 460;
        for (const auto& g : col.second) {
            add(col.first, y, 88, 9, kLegend, g.menu, -1); y += 9;
            for (const auto& l : g.lines) { add(col.first, y, 88, 7.7f, kLegend, g.menu, l.no); y += 7.7f; }
        }
    }
}
const FivePanel::Region* FivePanel::hit(juce::Point<float> p) const {
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) if (it->r.contains(p)) return &*it;
    return nullptr;
}

/* ================================================================================================ items */
FivePanel::Item FivePanel::numItem(double no, const juce::String& name, std::function<double()> get, std::function<void(double)> set, double lo, double hi,
                                   double step, double fine, std::function<juce::String(double)> fmt, int nd, double curve) {
    if (fine <= 0) fine = step;
    auto snap = [=](double x) { return clampd(jsround(x / fine) * fine, lo, hi); };
    Item it; it.no = no; it.name = name;
    it.l2 = [=] { return fmt ? fmt(get()) : juce::String((int) jsround(get())); };
    it.inc = [=](int d, bool sh) { set(snap(get() + d * (sh ? fine : step))); };
    it.data = [=](double x) { set(snap(lo + (hi - lo) * (curve > 0 ? std::pow(x, curve) : x))); };
    if (nd) it.digit = [=](int d) { set(clampd(typed(d, nd).getIntValue(), lo, hi)); };
    return it;
}
FivePanel::Item FivePanel::listItem(double no, const juce::String& name, std::function<int()> get, std::function<void(int)> set, juce::StringArray list) {
    Item it; it.no = no; it.name = name; const int n = list.size();
    it.l2 = [=] { return list[juce::jlimit(0, n - 1, get())]; };
    it.inc = [=](int d, bool) { set(juce::jlimit(0, n - 1, get() + d)); };
    it.data = [=](double x) { set((int) jsround(x * (n - 1))); };
    return it;
}
FivePanel::Item FivePanel::actItem(double no, const juce::String& name, std::function<void()> run, std::function<juce::String()> l2) {
    Item it; it.no = no; it.name = name; it.yes = std::move(run); it.l2 = l2 ? l2 : [] { return juce::String(" sure? NO / YES"); }; return it;
}

void FivePanel::buildMenus() {
    auto vget = [this](int k) { return S().voice[(size_t) U.voice][k]; };
    auto vItem = [&](double no, const juce::String& n, int k, double step = 1, double fine = 0, std::function<juce::String(double)> fmt = nullptr) {
        auto it = numItem(no, n, [=] { return vget(k); }, [this, k](double x) { proc.setVoiceParam(U.voice, k, x); }, VOICE_RANGES[k][0], VOICE_RANGES[k][1], step, fine, fmt);
        it.ctx = [this] { return vname(U.voice); }; it.pad = [this](int, int v) { U.voice = v; }; return it;
    };
    auto vBool = [&](double no, const juce::String& n, int k) {
        auto it = listItem(no, n, [=] { return vget(k) >= .5 ? 1 : 0; }, [this, k](int x) { proc.setVoiceParam(U.voice, k, x); }, { "OFF", "ON" });
        it.ctx = [this] { return vname(U.voice); }; it.pad = [this](int, int v) { U.voice = v; }; return it;
    };
    auto keyOf = [this](int i) -> SetupKey& { return S().setups[(size_t) clampInt(S().setup, 0, NUM_SETUPS - 1, 0)].keys[(size_t) juce::jlimit(0, NV - 1, i)]; };
    auto keyItem = [&](double no, const juce::String& n, int SetupKey::*f, double lo, double hi, double step = 1, double fine = 0, std::function<juce::String(double)> fmt = nullptr) {
        auto it = numItem(no, n, [=] { return (double) (keyOf(U.pad).*f); }, [=](double x) { edit([&](State&) { keyOf(U.pad).*f = (int) jsround(x); }); }, lo, hi, step, fine, fmt);
        it.ctx = [this] { return "key" + juce::String::charToString((juce::juce_wchar) ('A' + U.pad)); }; it.pad = [this](int i, int) { U.pad = i; }; return it;
    };
    auto cctx = [this] { return juce::String(CHANNEL_NAMES[U.ch]).substring(0, 7); };
    auto padCh = [this](int, int v) { U.ch = v % NCH; };
    auto chItem = [&](double no, const juce::String& n, int k, double lo, double hi, double step, std::function<juce::String(double)> fmt) {
        auto it = numItem(no, n, [this, k] { const auto& c = S().channel[(size_t) U.ch]; return k == cPan ? c.pan : k == cSendRev ? c.sendRev : c.sendDly; },
                          [this, k](double x) { proc.setChannelParam(U.ch, k, x); }, lo, hi, step, 0, fmt);
        it.ctx = cctx; it.pad = padCh; return it;
    };
    auto rItem = [&](double no, const juce::String& n, int k, double step, std::function<juce::String(double)> fmt, double curve = 0) {
        return numItem(no, n, [this, k] { return S().rev[(size_t) k]; }, [this, k](double x) { proc.setRevParam(k, x); }, REV_RANGES[k][0], REV_RANGES[k][1], step, 0, fmt, 0, curve);
    };
    auto dItem = [&](double no, const juce::String& n, int k, double step, std::function<juce::String(double)> fmt, double curve = 0) {
        return numItem(no, n, [this, k] { return S().dly[(size_t) k]; }, [this, k](double x) { proc.setDlyParam(k, x); }, DLY_RANGES[k][0], DLY_RANGES[k][1], step, 0, fmt, 0, curve);
    };
    auto song = [this]() -> Song& { return S().songs[(size_t) U.song]; };
    auto pat = [this]() -> Pattern& { return S().patterns[(size_t) U.ptn]; };
    auto ptnCtx = [this] { return "P" + p2(U.ptn); };

    menus["song"] = { actItem(1, "SEARCH MARK", [this] { searchMark(true); }, [] { return juce::String("YES: next mark"); }) };
    {
        Item hidden; hidden.no = 0; hidden.name = "EDIT SONG";
        Item name; name.no = 3; name.name = "SONG NAME"; name.l2 = [=] { return juce::String(song().name); };
        name.yes = [=] { openName([=] { return juce::String(song().name); }, [=](const juce::String& n) { edit([&](State& s) { s.songs[(size_t) U.song].name = n.substring(0, 16).toStdString(); }); }); };
        menus["esong"] = { hidden, actItem(1, "SEARCH MARK", [this] { searchMark(false); }, [] { return juce::String("YES: next mark"); }),
            actItem(2, "SET MARK", [this] { if (protectedMsg()) return; edit([&](State& s) { songInsertStep(s, U.song, U.songStep, SongStep::mark()); }); flash("MARK SET", "at step " + p3(U.songStep + 1)); },
                    [this] { return "YES: at s" + p3(U.songStep + 1); }),
            name,
            numItem(4, "INITIAL TEMPO", [=] { return song().tempo; }, [=](double x) { edit([&](State& s) { s.songs[(size_t) U.song].tempo = clampd(x, 40, 250); }); }, 40, 250, 1, .1,
                    [](double x) { return juce::String(x, 1) + " BPM"; }, 3) };
    }
    {
        Item sel; sel.no = 1; sel.name = "SELECT VOICE"; sel.ctx = [this] { return vname(U.voice); };
        sel.l2 = [this] { return vname(U.voice) + (S().samples[(size_t) U.voice].data.empty() ? " (INT)" : " (USER)"); };
        sel.inc = [this](int d, bool) { U.voice = (U.voice + d + NV) % NV; }; sel.data = [this](double x) { U.voice = (int) jsround(x * 23); }; sel.pad = [this](int, int v) { U.voice = v; };
        Item wave; wave.no = 1.1; wave.name = "WAVEFORM"; wave.ctx = sel.ctx; wave.pad = sel.pad; wave.l2 = [this] { return proc.waveName(U.voice); };
        wave.inc = [this](int d, bool) {
            const auto L = proc.waveLib(); if (L.empty()) return;
            const int cur = S().wave[(size_t) U.voice] >= 0 ? S().wave[(size_t) U.voice] : U.voice; int i = 0;
            for (size_t k = 0; k < L.size(); k++) if (L[k].id == cur) i = (int) k;
            proc.setWave(U.voice, L[(size_t) ((i + d + (int) L.size()) % (int) L.size())].id);
        };
        auto gateFmt = [](double x) { return x < 100 ? juce::String("OFF (plays out)") : juce::String((int) jsround(x)).paddedLeft('0', 4) + " ms"; };
        Item store = actItem(7, "STORE VOICE", [this] { stored[U.voice] = S().voice[(size_t) U.voice]; flash("STORED", vname(U.voice)); });
        Item init = actItem(8, "INIT VOICE", [this] { const auto d = voiceDefault(U.voice); for (int k = 0; k < vCount; k++) proc.setVoiceParam(U.voice, k, d[k]); flash("INITIALIZED", vname(U.voice)); });
        Item recall = actItem(9, "RECALL EDIT", [this] { const auto it = stored.find(U.voice); if (it == stored.end()) { flash("NOTHING STORED", vname(U.voice)); return; }
            for (int k = 0; k < vCount; k++) proc.setVoiceParam(U.voice, k, it->second[k]); flash("RECALLED", vname(U.voice)); });
        Item wav = actItem(11, "LOAD WAV", [this] { const int v = U.voice; pickFile("Load a sample into " + vname(v), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", [this, v](const juce::File& f) { report("LOADED " + vname(v), proc.loadWavIntoVoice(f, v)); }); },
                           [] { return juce::String("YES: pick a file"); });
        for (Item* it : { &store, &init, &recall, &wav }) { it->ctx = sel.ctx; it->pad = sel.pad; }
        auto bits = listItem(12, "BITS", [=] { return vget(vBits) < 10 ? 0 : 1; }, [this](int x) { proc.setVoiceParam(U.voice, vBits, x ? 12 : 8); }, { "8 BIT", "12 BIT" }); bits.ctx = sel.ctx; bits.pad = sel.pad;
        menus["voice"] = { sel, wave, vItem(2, "PITCH", vPitch, 100, 10, fmtCents), vItem(3.1, "EG AR", vAr), vItem(3.2, "EG D1R", vD1r), vItem(3.3, "EG D1L", vD1l), vItem(3.4, "EG D2R", vD2r),
            vItem(3.5, "EG RR", vRr), vItem(3.6, "EG GATE", vGate, 100, 10, gateFmt), vItem(4.1, "BEND RATE", vBendRate), vItem(4.2, "BEND RANGE", vBendRange, 100, 10, fmtCents),
            vItem(5, "VOICE LEVEL", vLevel), vBool(6, "LOOP", vLoop), store, init, recall, vBool(10, "REVERSE", vReverse), bits, wav };
    }
    {
        Item play; play.no = 1; play.name = "PLAY"; play.ctx = [this] { return "CHAIN " + juce::String(U.chain + 1); };
        play.l2 = [this] { return juce::String((int) S().chains[(size_t) U.chain].songs.size()) + " songs   YES"; };
        play.noFn = [this] { U.chain = (U.chain + 1) % NUM_CHAINS; }; play.yes = [this] { U.mode = 1; U.chainPlay = true; proc.start(); };
        Item ed; ed.no = 2; ed.name = "EDIT"; ed.ctx = [this] { return "C" + juce::String(U.chain + 1) + " #" + p2(U.chainStep + 1); };
        ed.l2 = [this] { const auto& c = S().chains[(size_t) U.chain].songs; U.chainStep = juce::jlimit(0, (int) c.size(), U.chainStep);
            if (U.chainStep >= (int) c.size()) return "-- END -- +" + songL(U.song); const int n = c[(size_t) U.chainStep]; return "SONG " + songL(n) + " " + juce::String(S().songs[(size_t) n].name).substring(0, 8); };
        ed.inc = [this](int d, bool) { if (protectedMsg()) return; edit([&](State& s) { auto& c = s.chains[(size_t) U.chain].songs;
            if (U.chainStep >= (int) c.size()) { if (d > 0 && (int) c.size() < CHAIN_MAX) { chainInsert(s, U.chain, (int) c.size(), U.song); U.chainStep = (int) c.size() - 1; } return; }
            c[(size_t) U.chainStep] = (c[(size_t) U.chainStep] + d + NUM_SONGS) % NUM_SONGS; }); };
        ed.move = [this](int d) { U.chainStep = juce::jlimit(0, (int) S().chains[(size_t) U.chain].songs.size(), U.chainStep + d); };
        auto setSong = [this](int n) { edit([&](State& s) { auto& c = s.chains[(size_t) U.chain].songs; if (U.chainStep < (int) c.size()) c[(size_t) U.chainStep] = n; else U.song = n; }); };
        ed.data = [=](double x) { setSong((int) jsround(x * 19)); };
        ed.digit = [=](int d) { const int n = typed(d).getIntValue() - 1; if (n >= 0 && n < NUM_SONGS) setSong(n); };
        Item nm; nm.no = 3; nm.name = "CHAIN NAME"; nm.l2 = [this] { return juce::String(S().chains[(size_t) U.chain].name); };
        nm.yes = [this] { openName([this] { return juce::String(S().chains[(size_t) U.chain].name); }, [this](const juce::String& n) { edit([&](State& s) { s.chains[(size_t) U.chain].name = n.substring(0, 16).toStdString(); }); }); };
        auto clr = actItem(4, "CLEAR", [this] { if (protectedMsg()) return; edit([&](State& s) { chainSet(s, U.chain, {}); }); U.chainStep = 0; flash("CHAIN CLEARED", "CHAIN " + juce::String(U.chain + 1)); });
        clr.ctx = play.ctx;
        menus["chain"] = { play, ed, nm, clr };
    }
    {
        Item r1; r1.no = 1; r1.name = "REMAIN PATTERN"; r1.l2 = [this] { int u = 0; for (const auto& p : S().patterns) if (!p.events.empty()) u++; return juce::String(NUM_PATTERNS - u) + " free " + juce::String(u) + " used"; };
        Item r2; r2.no = 2; r2.name = "REMAIN SONG"; r2.l2 = [this] { int u = 0; for (const auto& s : S().songs) if (!s.steps.empty()) u++; return juce::String(NUM_SONGS - u) + " free " + juce::String(u) + " used"; };
        Item cv; cv.no = 4; cv.name = "CHANGE VOICE"; cv.ctx = [this] { return vname(U.voice); }; cv.pad = [this](int, int v) { U.voice = v; };
        cv.l2 = [this] { return S().samples[(size_t) U.voice].data.empty() ? juce::String("INTERNAL VOICE") : juce::String("USER>INT? YES"); };
        cv.yes = [this] { if (S().samples[(size_t) U.voice].data.empty()) return; edit([&](State& s) { s.samples[(size_t) U.voice] = UserSample(); }); proc.applyVoiceSample(U.voice); flash("INTERNAL VOICE", vname(U.voice)); };
        menus["util"] = { r1, r2, listItem(3, "MEM PROTECT", [this] { return U.protect ? 1 : 0; }, [this](int x) { U.protect = x != 0; }, { "OFF", "ON" }), cv,
            actItem(5, "CLR ALL PTNS", [this] { if (protectedMsg()) return; edit([&](State& s) { for (int i = 0; i < NUM_PATTERNS; i++) clearPattern(s, i); }); flash("ALL PATTERNS", "CLEARED"); }),
            actItem(6, "CLR ALL SONGS", [this] { if (protectedMsg()) return; edit([&](State& s) { for (int i = 0; i < NUM_SONGS; i++) s.songs[(size_t) i] = newSong(i); }); flash("ALL SONGS", "CLEARED"); }) };
    }
    {
        Item va; va.no = 1; va.name = "VOICE ASSIGN"; va.ctx = [this] { return "key" + juce::String::charToString((juce::juce_wchar) ('A' + U.pad)); }; va.pad = [this](int i, int) { U.pad = i; };
        va.l2 = [=] { const int v = keyOf(U.pad).voice; return ":" + juce::String(S().samples[(size_t) v].data.empty() ? "Int-" : "Usr-") + vname(v); };
        va.inc = [=](int d, bool) { edit([&](State&) { auto& k = keyOf(U.pad); k.voice = (k.voice + d + NV) % NV; }); };
        va.data = [=](double x) { edit([&](State&) { keyOf(U.pad).voice = (int) jsround(x * 23); }); };
        Item mv; mv.no = 3; mv.name = "MULTI VOICE"; mv.l2 = [this] { return multiOn ? vname(multiV) + " A-X" : juce::String("OFF"); };
        mv.inc = [this](int d, bool) { const int k = (multiOn ? multiV + 1 : 0) + d; multiOn = k > 0 && k <= NV; if (multiOn) multiV = (k - 1 + NV) % NV; };
        mv.pad = [this](int, int v) { if (!multiOn) multiV = v; };
        Item ms; ms.no = 4; ms.name = "MULTI STEP"; static const char* MP[4] = { "PITCH", "LEVEL", "ATTACK", "DECAY" };
        ms.l2 = [this] { return juce::String(MP[multiParam]) + " " + sgn(multiStep[multiParam]); };
        ms.inc = [this](int d, bool sh) { const bool p = multiParam == 0; multiStep[multiParam] = clampd(multiStep[multiParam] + d * (p ? (sh ? 10 : 100) : 1), p ? -1200 : -10, p ? 1200 : 10); };
        ms.noFn = [this] { multiParam = (multiParam + 1) % 4; }; ms.data = [this](double x) { multiParam = juce::jmin(3, (int) (x * 4)); };
        Item save; save.no = 7; save.name = "SAVE KEY DATA"; save.l2 = [this] { return "to set " + juce::String(U.n["slot"] % 3 + 1) + "    YES"; };
        save.noFn = [this] { U.n["slot"] = (U.n["slot"] + 1) % 3; };
        save.yes = [this] { const int to = U.n["slot"] % 3; edit([&](State& s) { if (to != s.setup) s.setups[(size_t) to] = s.setups[(size_t) s.setup]; s.setup = to; }); flash("KEY DATA SAVED", "set " + juce::String(to + 1)); };
        Item load; load.no = 8; load.name = "LOAD KEY DATA"; load.l2 = [this] { const int k = U.n["slot"] % 3; return "set " + juce::String(k + 1) + (k == S().setup ? "*" : " ") + "     YES"; };
        load.noFn = save.noFn; load.yes = [this] { edit([&](State& s) { s.setup = U.n["slot"] % 3; }); flash("KEY DATA LOADED", "set " + juce::String(S().setup + 1)); };
        Item cp; cp.no = 9; cp.name = "COPY VOICE"; cp.ctx = [this] { return vname(U.voice); }; cp.pad = [this](int, int v) { U.voice = v; };
        cp.l2 = [this] { return ">" + vname(U.n["copyTo"]) + "  YES"; }; cp.noFn = [this] { U.n["copyTo"] = (U.n["copyTo"] + 1) % NV; };
        cp.yes = [this] { const int to = U.n["copyTo"]; const auto src = S().voice[(size_t) U.voice]; for (int k = 0; k < vCount; k++) proc.setVoiceParam(to, k, src[k]); flash("COPIED", vname(U.voice) + ">" + vname(to)); };
        auto pv = numItem(13, "PAD VELOCITY", [this] { return (double) padVel; }, [this](double x) { padVel = (int) x; }, 0, 127, 1, 0, [](double x) { return x > 0 ? "fixed " + juce::String((int) x) : juce::String("TOUCH (pad pos)"); });
        menus["key"] = { va, keyItem(2.1, "KEY PITCH", &SetupKey::pitch, -3600, 2400, 100, 10, fmtCents), keyItem(2.2, "KEY LEVEL", &SetupKey::level, -31, 31, 1, 0, sgn),
            keyItem(2.3, "KEY ATTACK", &SetupKey::attack, -63, 63, 1, 0, sgn), keyItem(2.4, "KEY DECAY", &SetupKey::decay, -63, 63, 1, 0, sgn), mv, ms,
            keyItem(5.1, "ACCENT 1", &SetupKey::acc1, -31, 31, 1, 0, [](double x) { return sgn(x) + " level"; }), keyItem(5.2, "ACCENT 2", &SetupKey::acc2, -31, 31, 1, 0, [](double x) { return sgn(x) + " level"; }),
            keyItem(6, "OUTPUT CH", &SetupKey::out, 0, 11, 1, 0, [](double x) { return "ch " + p2((int) x + 1) + " " + STEP_CH[(int) x]; }), save, load, cp,
            chItem(10, "PAN", cPan, -1, 1, 1.0 / 7, fmtPan), chItem(11, "REV SEND", cSendRev, 0, 1, .01, pct), chItem(12, "DLY SEND", cSendDly, 0, 1, .01, pct), pv };
    }
    {
        auto saveF = actItem(21, "SAVE FILE", [this] { pickFile("Save the cartridge", "*.o5cart", [this](const juce::File& f) { report("SAVED", proc.saveCartFile(f.withFileExtension("o5cart"))); }, true, "rx5.o5cart"); },
                             [] { return juce::String("YES: .o5cart"); });
        auto loadF = actItem(22, "LOAD FILE", [this] { pickFile("Load a cartridge", "*.o5cart;*.json", [this](const juce::File& f) { report("LOADED", proc.loadCartFile(f)); }); }, [] { return juce::String("YES: pick a file"); });
        auto wav = actItem(23, "LOAD WAV", [this] { const int v = U.voice; pickFile("Load a sample into " + vname(v), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", [this, v](const juce::File& f) { report("LOADED " + vname(v), proc.loadWavIntoVoice(f, v)); }); },
                           [] { return juce::String("YES: into voice"); });
        wav.ctx = [this] { return vname(U.voice); }; wav.pad = [this](int, int v) { U.voice = v; };
        Item bank; bank.no = 24; bank.name = "VOICE BANK"; bank.l2 = [this] { return juce::String(U.n["bankDir"] ? "SAVE .bin" : "LOAD .bin") + " YES"; };
        bank.noFn = [this] { U.n["bankDir"] = U.n["bankDir"] ? 0 : 1; };
        bank.yes = [this] {
            if (U.n["bankDir"]) pickFile("Save the voices as an RX5 bank", "*.bin", [this](const juce::File& f) { report("BANK SAVED", proc.saveBankFile(f.withFileExtension("bin"))); }, true, "rx5-bank.bin");
            else pickFile("Load an RX5 voice bank", "*.bin;*.rom", [this](const juce::File& f) { report("BANK LOADED", proc.loadBankFile(f)); });
        };
        Item kit; kit.no = 25; kit.name = "LOAD KIT"; kit.l2 = [this] { return juce::String(U.n["kitZip"] ? "ZIP/WAV" : "FOLDER ") + " YES NO:<>"; };
        kit.noFn = [this] { U.n["kitZip"] = U.n["kitZip"] ? 0 : 1; };
        kit.yes = [this] {
            if (U.n["kitZip"]) pickFile("Load a kit (.zip or WAV)", "*.zip;*.wav;*.aif;*.aiff", [this](const juce::File& f) { report(proc.lastMidiMessage(), proc.loadKit(f)); });
            else pickFile("Load a kit folder", "*", [this](const juce::File& f) { report(proc.lastMidiMessage(), proc.loadKit(f)); }, false, {}, true);
        };
        menus["cart"] = { saveF, loadF, wav, bank, kit };
    }
    {
        Item nv; nv.no = 4.1; nv.name = "NOTE VOICE"; nv.ctx = [this] { return vname(U.voice); }; nv.pad = [this](int, int v) { U.voice = v; };
        nv.l2 = [this] { const int n = S().noteMap[(size_t) U.voice]; return noteName(n) + " " + p3(n); };
        nv.inc = [this](int d, bool sh) { edit([&](State& s) { auto& n = s.noteMap[(size_t) U.voice]; n = juce::jlimit(0, 127, n + d * (sh ? 12 : 1)); }); };
        nv.data = [this](double x) { edit([&](State& s) { s.noteMap[(size_t) U.voice] = (int) jsround(x * 127); }); };
        Item np; np.no = 4.2; np.name = "NOTE PITCH"; np.l2 = [this] { return S().midi.tunable ? vname(S().midi.tunableVoice) + " C1-C6" : juce::String("OFF (VOICE mode)"); };
        np.inc = [this](int d, bool) { edit([&](State& s) { s.midi.tunable = d > 0; }); }; np.pad = [this](int, int v) { edit([&](State& s) { s.midi.tunableVoice = v; }); };
        Item in; in.no = 5; in.name = "INIT NOTE"; in.l2 = [this] { return juce::String(U.n["notePreset"] ? "GM" : "RX5") + " table   YES"; };
        in.noFn = [this] { U.n["notePreset"] = U.n["notePreset"] ? 0 : 1; };
        in.yes = [this] { const bool gm = U.n["notePreset"] != 0; edit([&](State& s) { std::copy(gm ? MIDI_GM_NOTES : RX5_NOTES, (gm ? MIDI_GM_NOTES : RX5_NOTES) + NV, s.noteMap.begin()); }); flash("NOTES INITIALIZED", gm ? "GM" : "RX5"); };
        Item rb; rb.no = 9; rb.name = "RECEIVE BULK"; rb.l2 = [] { return juce::String("YES: send request"); };
        rb.yes = [this] { proc.sendSysex(dumpRequest(S().midi.deviceNo ? S().midi.deviceNo : 1)); flash("DUMP REQUEST", "sent, waiting"); };
        Item tb; tb.no = 10; tb.name = "TRANSMIT BULK"; tb.l2 = [this] { return juce::String(BULKS[U.n["bulk"] % 6]) + " YES"; };
        tb.noFn = [this] { U.n["bulk"] = (U.n["bulk"] + 1) % 6; };
        tb.yes = [this] {
            const int b = U.n["bulk"] % 6; auto s = proc.copyState();
            if (b == 5) { report("TRANSMIT BULK", proc.transmitBulk(1)); return; }
            proc.sendSysex(b == 0 ? omegaFull(*s) : b == 1 ? omegaPattern(*s, U.ptn) : b == 2 ? omegaSong(*s, U.song) : b == 3 ? omegaVoices(*s) : omegaFx(*s));
            flash("TRANSMITTED", BULKS[b]);
        };
        Item syx; syx.no = 15; syx.name = "SYX FILE"; syx.l2 = [this] { static const char* W4[5] = { "LOAD .syx", "SAVE ALL", "SAVE RX5", "SAVE PTN", "SAVE SONG" }; return juce::String(W4[U.n["syx"] % 5]) + " YES"; };
        syx.noFn = [this] { U.n["syx"] = (U.n["syx"] + 1) % 5; };
        syx.yes = [this] {
            const int w = U.n["syx"] % 5;
            if (!w) pickFile("Load a SysEx file", "*.syx", [this](const juce::File& f) { report("SYX LOADED", proc.loadSyxFile(f)); });
            else pickFile("Save a SysEx file", "*.syx", [this, w](const juce::File& f) { report("SYX SAVED", proc.saveSyxFile(f.withFileExtension("syx"), w - 1)); }, true, "rx5.syx");
        };
        auto mb = [this](bool MidiSettings::*f) { return [this, f] { return S().midi.*f ? 1 : 0; }; };
        auto ms = [this](bool MidiSettings::*f) { return [this, f](int x) { edit([&](State& s) { s.midi.*f = x != 0; }); }; };
        auto mi = [this](int MidiSettings::*f) { return [this, f] { return (double) (S().midi.*f); }; };
        auto msi = [this](int MidiSettings::*f) { return [this, f](double x) { edit([&](State& s) { s.midi.*f = (int) jsround(x); }); }; };
        menus["midi"] = { listItem(1, "CH MESSAGE", mb(&MidiSettings::channelMsg), ms(&MidiSettings::channelMsg), { "OFF", "ON" }),
            numItem(2, "RECEIVE CH", mi(&MidiSettings::rxCh), msi(&MidiSettings::rxCh), 1, 16, 1, 0, [](double x) { return "ch " + p2((int) x); }),
            numItem(3, "TRANSMIT CH", mi(&MidiSettings::txCh), msi(&MidiSettings::txCh), 1, 16, 1, 0, [](double x) { return "ch " + p2((int) x); }),
            nv, np, in,
            numItem(6, "EG VELOCITY", mi(&MidiSettings::egVel), msi(&MidiSettings::egVel), 0, 4, 1, 0, [](double x) { return x > 0 ? p2((int) x) : juce::String("OFF"); }),
            numItem(7, "GATE TIME", mi(&MidiSettings::gateMs), msi(&MidiSettings::gateMs), 10, 1000, 10, 0, [](double x) { return juce::String(x / 1000, 2) + " s"; }),
            numItem(8, "DEVICE NUMBER", mi(&MidiSettings::deviceNo), msi(&MidiSettings::deviceNo), 0, 16, 1, 0, [](double x) { return x > 0 ? p2((int) x) : juce::String("OFF"); }),
            rb, tb, listItem(11, "CLOCK OUT", mb(&MidiSettings::clockOut), ms(&MidiSettings::clockOut), { "OFF", "ON" }),
            listItem(12, "NOTES OUT", mb(&MidiSettings::notesOut), ms(&MidiSettings::notesOut), { "OFF", "ON" }), syx };
    }
    {
        Item imp; imp.no = 1; imp.name = "IMPORT MIDI";
        auto impStart = [this] { if (U.n.count("impAt") && U.n["impAt"] >= 0) return U.n["impAt"]; int k = 0; for (int i = 0; i < NUM_PATTERNS; i++) if (!S().patterns[(size_t) i].events.empty()) k = i + 1; return juce::jmin(k, NUM_PATTERNS - 1); };
        imp.l2 = [=] { return "to " + p2(impStart()) + " SONG " + songL(U.song); }; imp.noFn = [=] { U.n["impAt"] = (impStart() + 1) % NUM_PATTERNS; };
        imp.yes = [=] { const int at = impStart(), sn = U.song; pickFile("Import a MIDI file", "*.mid;*.midi;*.smf", [this, at, sn](const juce::File& f) { report("MIDI IMPORTED", proc.importMidiFile(f, at, sn)); }); };
        auto expName = [this] { return U.n["exp"] ? "SONG " + songL(U.song) : "PTN " + p2(U.ptn); };
        Item em; em.no = 2; em.name = "EXPORT MIDI"; em.l2 = [=] { return expName() + " YES"; }; em.noFn = [this] { U.n["exp"] = U.n["exp"] ? 0 : 1; };
        em.yes = [this] { const bool sg = U.n["exp"] != 0; pickFile("Export a MIDI file", "*.mid", [this, sg](const juce::File& f) { report("MIDI EXPORTED", proc.exportMidiFile(f.withFileExtension("mid"), sg)); }, true, sg ? "rx5-song.mid" : "rx5-pattern.mid"); };
        Item ew; ew.no = 3; ew.name = "EXPORT WAV"; ew.l2 = em.l2; ew.noFn = em.noFn;
        ew.yes = [this] { const bool sg = U.n["exp"] != 0; pickFile("Export a WAV file", "*.wav", [this, sg](const juce::File& f) { report("WAV EXPORTED", proc.exportWav(f.withFileExtension("wav"), sg)); }, true, sg ? "rx5-song.wav" : "rx5-pattern.wav"); };
        Item rnd; rnd.no = 4; rnd.name = "RANDOMIZE"; rnd.l2 = [this] { const int r = U.n["rnd"] % 4; return (r == 0 ? juce::String("ALL") : r == 1 ? "PATTERN " + p2(U.ptn) : r == 2 ? juce::String("VOICES") : juce::String("FX")) + " YES"; };
        rnd.noFn = [this] { U.n["rnd"] = (U.n["rnd"] + 1) % 4; }; rnd.yes = [this] { doRandom(); };
        menus["job"] = { imp, em, ew, rnd, actItem(5, "INIT ALL", [this] { if (protectedMsg()) return; proc.stop(); proc.initAll(); U.ptn = 0; U.song = 0; flash("MEMORY", "INITIALIZED"); }) };
    }
    {
        auto pe = [this](std::function<void(State&)> f) { if (protectedMsg()) return; edit(f); };
        auto beats = numItem(1, "BEATS", [=] { return (double) pat().beats; }, [=](double x) { pe([&](State& s) { setBeats(s, U.ptn, (int) x, s.patterns[(size_t) U.ptn].unit); }); }, 1, BEATS_MAX, 1, 0, nullptr, 2);
        juce::StringArray units; for (int u : TS_UNITS) units.add("1/" + juce::String(u));
        auto unit = listItem(2, "UNIT", [=] { int i = 1; for (int k = 0; k < 8; k++) if (TS_UNITS[k] == pat().unit) i = k; return i; },
                             [=](int x) { pe([&](State& s) { setBeats(s, U.ptn, s.patterns[(size_t) U.ptn].beats, TS_UNITS[x]); }); }, units);
        auto bars = numItem(3, "BARS", [=] { return (double) pat().bars; }, [=](double x) { pe([&](State& s) { setBars(s, U.ptn, (int) x); }); }, 1, BARS_MAX, 1, 0, nullptr, 2);
        Item nm; nm.no = 4; nm.name = "NAME"; nm.l2 = [=] { return juce::String(pat().name); };
        nm.yes = [=] { openName([=] { return juce::String(pat().name); }, [=](const juce::String& n) { edit([&](State& s) { s.patterns[(size_t) U.ptn].name = n.substring(0, 16).toStdString(); }); }); };
        auto vol = numItem(5, "VOLUME", [=] { return (double) pat().volume[(size_t) U.ch]; }, [=](double x) { edit([&](State& s) { s.patterns[(size_t) U.ptn].volume[(size_t) U.ch] = (int) x; }); }, 0, 31);
        vol.ctx = cctx; vol.pad = padCh;
        for (Item* it : { &beats, &unit, &bars, &nm }) it->ctx = ptnCtx;
        menus["eptn"] = { beats, unit, bars, nm, vol };
    }
    {
        juce::StringArray progs; for (int i = 0; i < NUM_REV_PROGRAMS; i++) progs.add(REV_PROGRAMS[i].name);
        auto send = chItem(13, "REV SEND", cSendRev, 0, 1, .01, pct);
        menus["rev"] = { listItem(1, "PROGRAM", [this] { return (int) S().rev[rProgram]; }, [this](int x) { proc.setRevParam(rProgram, x); }, progs),
            rItem(2, "REV TIME", rTime, .1, [](double x) { return juce::String(x, 1) + " s"; }, 2), rItem(3, "HIGH RATIO", rHiRatio, .1, [](double x) { return juce::String(x, 1); }),
            rItem(4, "DELAY", rDelay, 1, [](double x) { return juce::String((int) x) + " ms"; }), rItem(5, "DIFFUSION", rDiffusion, 1, nullptr), rItem(6, "DENSITY", rDensity, 1, [](double x) { return juce::String((int) x) + " %"; }),
            rItem(7, "ER LEVEL", rErLevel, .01, pct), rItem(8, "REV LEVEL", rRevLevel, .01, pct),
            rItem(9, "HPF", rHpf, 10, [](double x) { return x < 25 ? juce::String("THRU") : juce::String((int) x) + " Hz"; }, 2),
            rItem(10, "LPF", rLpf, 500, [](double x) { return x >= 16000 ? juce::String("THRU") : juce::String(x / 1000, 1) + " kHz"; }, 2),
            rItem(11, "GATE TIME", rGateTime, 10, [](double x) { return juce::String((int) x) + " ms"; }), rItem(12, "RETURN LEVEL", rLevel, .01, pct), send };
    }
    {
        juce::StringArray syncs; for (auto* s : DLY_SYNC) syncs.add(juce::String(s) == "off" ? "OFF" : s);
        menus["dly"] = { listItem(0, "DELAY", [this] { return S().dly[dOn] >= .5 ? 1 : 0; }, [this](int x) { proc.setDlyParam(dOn, x); }, { "OFF", "ON" }),
            dItem(1, "TIME L", dTimeL, 1, [](double x) { return juce::String((int) x) + " ms"; }, 2), dItem(2, "TIME R", dTimeR, 1, [](double x) { return juce::String((int) x) + " ms"; }, 2),
            listItem(3, "TEMPO SYNC", [this] { return (int) S().dly[dSync]; }, [this](int x) { proc.setDlyParam(dSync, x); }, syncs),
            dItem(4, "FEEDBACK", dFeedback, 1, [](double x) { return juce::String((int) x) + " %"; }),
            listItem(5, "PING PONG", [this] { return S().dly[dPingpong] >= .5 ? 1 : 0; }, [this](int x) { proc.setDlyParam(dPingpong, x); }, { "OFF", "ON" }),
            dItem(6, "HI CUT", dHiCut, 500, [](double x) { return juce::String(x / 1000, 1) + " kHz"; }, 2), dItem(7, "LO CUT", dLoCut, 10, [](double x) { return juce::String((int) x) + " Hz"; }, 2),
            dItem(8, "MOD RATE", dModRate, .05, [](double x) { return juce::String(x, 2) + " Hz"; }), dItem(9, "MOD DEPTH", dModDepth, .01, pct),
            dItem(10, "DUCK THRESH", dDuckThresh, 1, [](double x) { return x <= -60 ? juce::String("OFF") : juce::String((int) x) + " dB"; }),
            dItem(11, "DUCK RELEASE", dDuckRelease, 10, [](double x) { return juce::String((int) x) + " ms"; }, 2), dItem(11.1, "DUCK DEPTH", dDuckDepth, 1, [](double x) { return juce::String((int) x) + " dB"; }) };
    }

    /* ---------------- the menu page ---------------- */
    menuPage.lcd = [this] {
        if (U.menu == "esong" && curItem().no == 0) {
            const auto& steps = S().songs[(size_t) U.song].steps; U.songStep = juce::jlimit(0, (int) steps.size(), U.songStep);
            const SongStep* st = U.songStep < (int) steps.size() ? &steps[(size_t) U.songStep] : nullptr;
            return juce::StringArray { "SONG " + songL(U.song) + " s" + p3(U.songStep + 1) + "/" + p3((int) steps.size()), st ? stepDesc(st) : "-- END -- +P" + p2(digits.isNotEmpty() ? digits.getIntValue() : U.ptn) };
        }
        auto& it = curItem();
        const int whole = (int) std::floor(it.no + 1e-9), sub = (int) jsround((it.no - whole) * 10);
        const juce::String num = (whole >= 10 ? juce::String(whole) : p2(whole)) + (sub ? "-" + juce::String(sub) : "");
        const juce::String l1 = num + " " + it.name + (it.ctx ? " " + it.ctx() : "");
        return juce::StringArray { l1, it.l2 ? it.l2() : juce::String() };
    };
    menuPage.pm = [this](int d, bool sh) {
        auto& it = curItem(); if (U.menu == "esong" && it.no == 0) { songEditInc(d, sh); return; }
        if (it.yes) { if (d > 0) it.yes(); else if (it.noFn) it.noFn(); else flash("CANCELLED", ""); }
        else if (it.inc) it.inc(d, sh); else if (it.noFn && d < 0) it.noFn();
    };
    menuPage.move = [this](int d) {
        auto& it = curItem(); if (U.menu == "esong" && it.no == 0) { songEditMove(d); return; }
        if (it.move) { it.move(d); return; }
        const int n = (int) menus[U.menu].size(); U.item[U.menu] = (juce::jlimit(0, n - 1, U.item[U.menu]) + d + n) % n; digits.clear();
    };
    menuPage.pad = [this](int i, int v, int, int) { auto& it = curItem(); if (U.menu == "esong" && it.no == 0) { edit([&](State& s) { auto& st = s.songs[(size_t) U.song].steps; if (U.songStep < (int) st.size() && st[(size_t) U.songStep].type == SongStep::Vol) st[(size_t) U.songStep].ch = v % NCH; }); return; } if (it.pad) it.pad(i, v); };
    menuPage.data = [this](double x) { auto& it = curItem(); if (U.menu == "esong" && it.no == 0) { songEditData(x); return; } if (it.data) it.data(x); };
    menuPage.digit = [this](int d) {
        auto& it = curItem(); if (U.menu == "esong" && it.no == 0) { songEditDigit(d); return; }
        if (it.digit) { it.digit(d); return; }
        // type a job number (two digits) to jump to it
        const auto s = typed(d, 2); if (s.length() < 2) return;
        auto& list = menus[U.menu]; for (size_t k = 0; k < list.size(); k++) if ((int) std::floor(list[k].no) == s.getIntValue() && list[k].no >= 1) { U.item[U.menu] = (int) k; digits.clear(); return; }
    };

    /* ---------------- the other pages ---------------- */
    auto playLine = [this] {
        const bool run = proc.running(); const auto pos = proc.position();
        const auto& p = run && pos.song < 0 ? S().patterns[(size_t) pos.pattern] : S().patterns[(size_t) U.ptn];
        return " " + p2(p.beats) + " /" + juce::String(p.unit).paddedLeft(' ', 2) + " :bar" + p2(run ? pos.bar : 1);
    };
    pages["ptn"].lcd = [=] {
        const bool run = proc.running(); const auto pos = proc.position();
        const int now = run && pos.song < 0 ? pos.pattern : U.ptn; const bool cue = run && U.ptn != now;
        const juce::String nx = digits.length() == 1 ? digits + "*" : cue ? p2(U.ptn) : juce::String("**");
        return juce::StringArray { "PLAY PTN" + p2(now) + ":nxt" + nx, playLine() };
    };
    pages["ptn"].digit = [this](int d) { const auto s = typed(d); if (s.length() == 2) { selectPattern(s.getIntValue()); digits.clear(); } };
    pages["ptn"].pm = [this](int d, bool) { selectPattern(U.ptn + d); };
    pages["ptn"].data = [this](double x) { selectPattern((int) jsround(x * 99)); };
    pages["songplay"].lcd = [this] {
        const bool run = proc.running(); const auto pos = proc.position();
        const int sn = run && pos.song >= 0 ? pos.song : U.song; const auto& s = S().songs[(size_t) sn];
        const juce::String l1 = (U.chainPlay ? "CHAIN" + juce::String(U.chain + 1) + " SONG " : juce::String("PLAY SONG ")) + songL(sn) + ":" + juce::String(s.name);
        if (run && pos.song >= 0) return juce::StringArray { l1, "s" + p3(pos.step + 1) + " P" + p2(pos.pattern) + " b" + p2(pos.bar) + " " + juce::String((int) jsround(pos.bpm)) };
        return juce::StringArray { l1, digits.length() == 1 ? "song " + digits + "*" : p3((int) s.steps.size()) + " steps " + juce::String(curTempo(), 1) };
    };
    pages["songplay"].digit = [this](int d) { const auto s = typed(d); if (s.length() == 2) { const int n = s.getIntValue() - 1; if (n >= 0 && n < NUM_SONGS) selectSong(n); digits.clear(); } };
    pages["songplay"].pm = [this](int d, bool) { selectSong(U.song + d); };
    pages["songplay"].data = [this](double x) { selectSong((int) jsround(x * 19)); };
    pages["rtw"].lcd = [=] { return juce::StringArray { "RTW PTN" + p2(U.ptn) + " Q" + QN(S().quantize), proc.running() ? playLine() + " " + juce::String(proc.position().beat) : U.protect ? juce::String("MEMORY PROTECTED") : juce::String(" START to record") }; };
    pages["rtw"].digit = [this](int d) { qDigit(d); }; pages["rtw"].pm = [this](int d, bool) { qSet(S().quantize + d); }; pages["rtw"].data = [this](double x) { qSet((int) jsround(x * 10)); };
    pages["step"].lcd = [this] {
        const auto& p = S().patterns[(size_t) U.ptn]; const int g = QT(S().quantize), len = patLen(p), bt = barTicks(p);
        U.stepPos = juce::jlimit(0, len - 1, U.stepPos);
        const int idx = U.stepPos / g, w0 = idx / 16 * 16, sv = U.n["stepVoice"];
        const int bar = U.stepPos / bt + 1, sib = (U.stepPos % bt) / g + 1; juce::String l2;
        for (int k = 0; k < 16; k++) {
            const int t = (w0 + k) * g; if (t >= len) { l2 += " "; continue; }
            const Event* e = nullptr; for (const auto& x : p.events) if (x.t == t && x.v == sv) e = &x;
            l2 += e ? (e->acc == 2 ? "#" : e->acc == 1 ? "O" : "o") : (t % ticksPerBeat(p) == 0 ? ":" : ".");
        }
        return juce::StringArray { vname(sv).substring(0, 6).paddedRight(' ', 6) + p2(bar) + ":" + p2(sib) + " " + QN(S().quantize), l2, juce::String(idx - w0) };
    };
    pages["step"].pm = [this](int d, bool) { const auto& p = S().patterns[(size_t) U.ptn]; const int g = QT(S().quantize), len = patLen(p); U.stepPos = (((U.stepPos / g) + d) * g % len + len) % len; };
    pages["step"].move = [this](int d) { pages["step"].pm(d, false); };
    pages["step"].digit = [this](int d) { qDigit(d); };
    pages["step"].data = [this](double x) { const auto& p = S().patterns[(size_t) U.ptn]; const int g = QT(S().quantize), n = (patLen(p) + g - 1) / g; U.stepPos = juce::jmin(n - 1, (int) (x * n)) * g; };
    pages["step"].pad = [this](int, int v, int vel, int acc) {
        if (protectedMsg()) return;
        U.n["stepVoice"] = v;
        edit([&](State& s) {
            bool had = false; for (const auto& e : s.patterns[(size_t) U.ptn].events) if (e.t == U.stepPos && e.v == v) had = true;
            if (had && !acc) stepDelete(s, U.ptn, U.stepPos, v); else stepInsert(s, U.ptn, U.stepPos, v, padVel ? padVel : vel, acc);
        });
    };
    pages["step"].silentToggle = true;
    pages["quant"].lcd = [this] { return juce::StringArray { "QUANTIZE", "      " + QN(S().quantize) }; };
    pages["quant"].digit = [this](int d) { qDigit(d); }; pages["quant"].pm = [this](int d, bool) { qSet(S().quantize + d); }; pages["quant"].data = [this](double x) { qSet((int) jsround(x * 10)); };
    pages["swing"].lcd = [this] { return juce::StringArray { "SWING   PTN" + p2(U.ptn), "      " + juce::String(S().patterns[(size_t) U.ptn].swing) + " %" }; };
    pages["swing"].pm = [this](int d, bool) { edit([&](State& s) { int i = 0; for (int k = 0; k < 6; k++) if (SWING_VALUES[k] == s.patterns[(size_t) U.ptn].swing) i = k; setSwing(s, U.ptn, SWING_VALUES[juce::jlimit(0, 5, i + d)]); }); };
    pages["swing"].digit = [this](int d) { const auto s = typed(d); if (s.length() == 2) edit([&](State& st) { setSwing(st, U.ptn, s.getIntValue()); }); };
    pages["swing"].data = [this](double x) { edit([&](State& s) { setSwing(s, U.ptn, (int) jsround(50 + 25 * x)); }); };
    pages["click"].lcd = [this] { return juce::StringArray { "CLICK", juce::String(S().clickOn ? "ON " : "OFF") + "  level " + juce::String((int) jsround(S().clickLevel * 100)) + "%" }; };
    pages["click"].pm = [this](int d, bool) { edit([&](State& s) { s.clickOn = d > 0; }); };
    pages["tempo"].lcd = [this] { return juce::StringArray { "TEMPO" + (U.mode == 1 ? "  SONG " + songL(U.song) : juce::String()) + (proc.hostSync() ? " HOST" : ""), "    " + juce::String(curTempo(), 1) + " BPM" }; };
    pages["tempo"].pm = [this](int d, bool sh) { setTempoUi(curTempo() + d * (sh ? .1 : 1)); };
    pages["tempo"].digit = [this](int d) { const auto s = typed(d, 3); if (s.length() == 3 || s.getIntValue() >= 40) setTempoUi(s.getIntValue()); };
    pages["tempo"].data = [this](double x) { setTempoUi(40 + 210 * x); };
    pages["clear"].lcd = [this] { return juce::StringArray { U.mode == 1 ? "CLEAR SONG " + songL(U.song) : "CLEAR PTN" + p2(U.ptn), " sure? NO / YES" }; };
    pages["clear"].pm = [this](int d, bool) {
        if (d < 0) { backToPlay(); return; }
        if (protectedMsg()) return;
        if (U.mode == 1) { edit([&](State& s) { s.songs[(size_t) U.song] = newSong(U.song); }); flash("SONG " + songL(U.song), "CLEARED"); }
        else { edit([&](State& s) { clearPattern(s, U.ptn); }); flash("PTN" + p2(U.ptn), "CLEARED"); }
        backToPlay();
    };
    pages["copy"].lcd = [this] { const int d = U.n["copyDst"]; return U.mode == 1 ? juce::StringArray { "COPY SONG " + songL(U.song) + " TO", "   SONG " + songL(d) + "  YES" } : juce::StringArray { "COPY PTN" + p2(U.ptn) + " TO", "   PTN" + p2(d) + "  YES" }; };
    pages["copy"].digit = [this](int d) { const auto s = typed(d); if (s.length() == 2) U.n["copyDst"] = U.mode == 1 ? juce::jlimit(0, NUM_SONGS - 1, s.getIntValue() - 1) : s.getIntValue(); };
    pages["copy"].data = [this](double x) { U.n["copyDst"] = (int) jsround(x * (U.mode == 1 ? 19 : 99)); };
    pages["copy"].pm = [this](int d, bool) {
        auto& dst = U.n["copyDst"];
        if (d < 0) { dst = (dst + 1) % (U.mode == 1 ? NUM_SONGS : NUM_PATTERNS); return; }
        if (protectedMsg()) return;
        if (U.mode == 1) { edit([&](State& s) { s.songs[(size_t) dst] = s.songs[(size_t) U.song]; }); flash("COPIED", "SONG " + songL(U.song) + ">" + songL(dst)); }
        else { edit([&](State& s) { copyPattern(s, U.ptn, dst); }); flash("COPIED", "PTN" + p2(U.ptn) + ">" + p2(dst)); }
    };
    pages["sync"].lcd = [this] { return juce::StringArray { "SYNC MODE", S().sync == "midi" ? juce::String("  MIDI CLOCK") : proc.hostSync() ? juce::String("  INT + HOST") : juce::String("  INT") }; };
    pages["sync"].pm = [this](int d, bool) { const int i = (S().sync == "midi" ? 2 : proc.hostSync() ? 1 : 0) + d; const int k = (i + 3) % 3; proc.setSync(k == 2 ? "midi" : "int"); proc.setHostSync(k == 1); };
    pages["sync"].data = [this](double x) { const int k = (int) jsround(x * 2); proc.setSync(k == 2 ? "midi" : "int"); proc.setHostSync(k == 1); };
    pages["reverse"].lcd = [this] { return juce::StringArray { "REVERSE  " + vname(U.voice), S().voice[(size_t) U.voice][vReverse] >= .5 ? juce::String("  ON  (pad: flip)") : juce::String("  OFF (pad: flip)") }; };
    pages["reverse"].pm = [this](int d, bool) { proc.setVoiceParam(U.voice, vReverse, d > 0 ? 1 : 0); };
    pages["reverse"].pad = [this](int, int v, int, int) { proc.setVoiceParam(v, vReverse, S().voice[(size_t) v][vReverse] >= .5 ? 0 : 1); };
    pages["name"].lcd = [this] { return juce::StringArray { "NAME  YES = done", nameState.buf, juce::String(nameState.cur) }; };
    pages["name"].pad = [this](int i, int, int, int) { nameType((juce::juce_wchar) ('A' + i)); };
    pages["name"].digit = [this](int d) { nameType((juce::juce_wchar) ('0' + d)); };
    pages["name"].move = [this](int d) { nameState.cur = juce::jlimit(0, 15, nameState.cur + d); };
    pages["name"].pm = [this](int d, bool) { if (d > 0) nameDone(); else { auto& s = nameState; s.cur = juce::jmax(0, s.cur - 1); s.buf = s.buf.substring(0, s.cur) + " " + s.buf.substring(s.cur + 1); } };
    pages["name"].padOnly = true;
}

FivePanel::Item& FivePanel::curItem() {
    auto& list = menus[U.menu]; if (list.empty()) { static Item none; return none; }
    auto& i = U.item[U.menu]; i = juce::jlimit(0, (int) list.size() - 1, i); return list[(size_t) i];
}
FivePanel::Page& FivePanel::page() { if (U.page == "menu") return menuPage; auto it = pages.find(U.page); return it != pages.end() ? it->second : pages["ptn"]; }
void FivePanel::openMenu(const juce::String& m, double no) {
    U.page = "menu"; U.menu = m; digits.clear();
    if (no >= 0) { auto& list = menus[m]; for (size_t k = 0; k < list.size(); k++) if (std::fabs(list[k].no - no) < 1e-6) { U.item[m] = (int) k; break; } }
}
void FivePanel::backToPlay() { U.page = U.mode == 1 ? "songplay" : "ptn"; U.menu = {}; }
void FivePanel::flash(const juce::String& a, const juce::String& b, double ms) { U.flash1 = a; U.flash2 = b; U.flashUntil = nowMs() + ms; repaint(); }
juce::String FivePanel::typed(int d, int max) {
    if (nowMs() - digitT > 1600) digits.clear();
    digitT = nowMs(); digits = (digits + juce::String(d)).getLastCharacters(max); return digits;
}
bool FivePanel::protectedMsg() { if (U.protect) { flash("MEMORY PROTECTED", "UTILITY 03: OFF"); return true; } return false; }
void FivePanel::report(const juce::String& what, const juce::String& err) {
    if (err.isEmpty()) flash(what.substring(0, 16), "", 1800); else flash("ERROR", err.substring(0, 16), 3000);
    repaint();
}

/* ================================================================================================ transport and pages */
void FivePanel::selectPattern(int n) { proc.selectPattern((n + NUM_PATTERNS) % NUM_PATTERNS); }
void FivePanel::selectSong(int n) { U.song = (n + NUM_SONGS) % NUM_SONGS; U.chainPlay = false; }
double FivePanel::curTempo() {
    if (proc.running()) return proc.position().bpm;
    return U.mode == 1 ? S().songs[(size_t) U.song].tempo : S().tempo;
}
void FivePanel::setTempoUi(double bpm) {
    bpm = clampd(jsround(bpm * 10) / 10, 40, 250);
    if (U.mode == 1 && !proc.running()) edit([&](State& s) { s.songs[(size_t) U.song].tempo = bpm; });
    else proc.setTempo(bpm);
}
void FivePanel::qSet(int i) { edit([&](State& s) { s.quantize = juce::jlimit(0, NUM_QUANTIZE - 1, i); }); }
void FivePanel::qDigit(int d) { qSet(QUANTIZE_KEYPAD[d]); const int g = QT(S().quantize); U.stepPos = U.stepPos / g * g; }
bool FivePanel::inSongEdit() { return U.page == "menu" && U.menu == "esong" && curItem().no == 0; }
bool FivePanel::inChainEdit() { return U.page == "menu" && U.menu == "chain" && curItem().no == 2; }
juce::String FivePanel::stepDesc(const SongStep* st) {
    if (!st) return "-- END --";
    switch (st->type) {
        case SongStep::Pat: return "PTN " + p2(st->n) + " " + juce::String(S().patterns[(size_t) st->n].name).substring(0, 8);
        case SongStep::Rep: return "REPEAT s" + p3(st->from + 1) + " x" + juce::String(st->count);
        case SongStep::Tempo: return "TEMPO " + juce::String(st->bpm, 1);
        case SongStep::Vol: return "VOL " + juce::String(STEP_CH[st->ch]) + " " + juce::String(st->level);
        case SongStep::Mark: return "=== MARK ===";
    }
    return "?";
}
void FivePanel::songEditInc(int d, bool sh) {
    if (protectedMsg()) return;
    edit([&](State& s) {
        auto& steps = s.songs[(size_t) U.song].steps;
        if (U.songStep >= (int) steps.size()) {
            if (d > 0 && (int) steps.size() < SONG_MAX) { const int n = digits.isNotEmpty() ? juce::jlimit(0, 99, digits.getIntValue()) : U.ptn; songInsertStep(s, U.song, (int) steps.size(), SongStep::pat(n)); U.songStep = (int) steps.size(); }
            return;
        }
        SongStep x = steps[(size_t) U.songStep];
        if (x.type == SongStep::Pat) x.n = (x.n + d + 100) % 100;
        else if (x.type == SongStep::Rep) x.count = juce::jlimit(1, 99, x.count + d);
        else if (x.type == SongStep::Tempo) x.bpm = clampd(jsround((x.bpm + d * (sh ? .1 : 1)) * 10) / 10, 40, 250);
        else if (x.type == SongStep::Vol) x.level = juce::jlimit(0, 31, x.level + d);
        songSetStep(s, U.song, U.songStep, x);
    });
    digits.clear();
}
void FivePanel::songEditMove(int d) { U.songStep = juce::jlimit(0, (int) S().songs[(size_t) U.song].steps.size(), U.songStep + d); digits.clear(); }
void FivePanel::songEditData(double x) {
    if (U.protect) return;
    edit([&](State& s) {
        auto& steps = s.songs[(size_t) U.song].steps; if (U.songStep >= (int) steps.size()) return;
        SongStep y = steps[(size_t) U.songStep];
        if (y.type == SongStep::Pat) y.n = (int) jsround(x * 99); else if (y.type == SongStep::Rep) y.count = 1 + (int) jsround(x * 98);
        else if (y.type == SongStep::Tempo) y.bpm = jsround(40 + 210 * x); else if (y.type == SongStep::Vol) y.level = (int) jsround(x * 31); else return;
        songSetStep(s, U.song, U.songStep, y);
    });
}
void FivePanel::songEditDigit(int d) {
    const auto& steps = S().songs[(size_t) U.song].steps; const bool rep = U.songStep < (int) steps.size() && steps[(size_t) U.songStep].type == SongStep::Rep;
    const auto t = typed(d, rep ? 3 : 2);
    if (U.songStep >= (int) steps.size() || U.protect) return;
    edit([&](State& s) {
        SongStep y = s.songs[(size_t) U.song].steps[(size_t) U.songStep];
        if (y.type == SongStep::Pat) y.n = juce::jlimit(0, 99, t.getIntValue()); else if (y.type == SongStep::Rep) y.from = juce::jlimit(0, U.songStep, t.getIntValue() - 1);
        else if (y.type == SongStep::Vol) y.level = juce::jlimit(0, 31, t.getIntValue()); else return;
        songSetStep(s, U.song, U.songStep, y);
    });
}
void FivePanel::songInsert(const juce::String& type) {
    if (inChainEdit()) { if (type == "pat") edit([&](State& s) { chainInsert(s, U.chain, U.chainStep, U.song); }); return; }
    if (!inSongEdit()) { U.mode = 1; openMenu("esong", 0); return; }
    if (protectedMsg()) return;
    const int i = U.songStep;
    edit([&](State& s) {
        auto& steps = s.songs[(size_t) U.song].steps; if ((int) steps.size() >= SONG_MAX) return;
        SongStep st;
        if (type == "pat") st = SongStep::pat(U.ptn);
        else if (type == "rep") { int from = 0; for (int k = i - 1; k >= 0; k--) if (steps[(size_t) k].type == SongStep::Mark) { from = k + 1; break; } st = SongStep::rep(from, 2); }
        else if (type == "tempo") st = SongStep::tempo(s.tempo);
        else st = SongStep::vol(U.ch, 31);
        songInsertStep(s, U.song, i, st);
    });
    flash(type == "pat" ? "INSERT" : type == "rep" ? "REPEAT" : type == "tempo" ? "TEMPO CHANGE" : "VOLUME CHANGE", "at s" + p3(i + 1), 900);
}
void FivePanel::songDelete() {
    if (inChainEdit()) { if (protectedMsg()) return; edit([&](State& s) { chainDelete(s, U.chain, U.chainStep); }); return; }
    if (!inSongEdit()) { U.mode = 1; openMenu("esong", 0); return; }
    if (protectedMsg()) return;
    if (U.songStep < (int) S().songs[(size_t) U.song].steps.size()) { edit([&](State& s) { songDeleteStep(s, U.song, U.songStep); }); flash("DELETE", "at s" + p3(U.songStep + 1), 900); }
}
void FivePanel::searchMark(bool play) {
    const auto& steps = S().songs[(size_t) U.song].steps;
    const auto pos = proc.position(); const int from = play ? (pos.song == U.song && pos.step >= 0 ? pos.step : -1) : U.songStep;
    int m = -1; for (int k = from + 1; k < (int) steps.size() && m < 0; k++) if (steps[(size_t) k].type == SongStep::Mark) m = k;
    if (m < 0) for (int k = 0; k < (int) steps.size() && m < 0; k++) if (steps[(size_t) k].type == SongStep::Mark) m = k;
    if (m < 0) { flash("NO MARK", "SONG " + songL(U.song)); return; }
    if (!play) { U.songStep = m; openMenu("esong", 0); return; }
    flash("MARK s" + p3(m + 1), "edit: SONG 01");
}
void FivePanel::doRandom() {
    if (protectedMsg()) return;
    static const char* SC[4] = { "all", "pattern", "voices", "fx" };
    const int r = U.n["rnd"] % 4; const auto g = proc.randomizeAll(SC[r]);
    flash("RANDOM " + juce::String(SC[r]).toUpperCase(), g + (r == 0 ? " " + juce::String((int) jsround(S().tempo)) + "BPM" : juce::String()), 1800);
}
void FivePanel::openName(std::function<juce::String()> get, std::function<void(const juce::String&)> set) {
    nameState.back = U.page; nameState.backMenu = U.menu; nameState.buf = get().paddedRight(' ', 16).substring(0, 16); nameState.cur = 0; nameState.set = std::move(set); U.page = "name";
}
void FivePanel::nameType(juce::juce_wchar c) { auto& s = nameState; s.buf = s.buf.substring(0, s.cur) + juce::String::charToString(c) + s.buf.substring(s.cur + 1); s.cur = juce::jmin(15, s.cur + 1); }
void FivePanel::nameDone() { const auto n = nameState.buf.trimEnd(); if (nameState.set) nameState.set(n); U.page = nameState.back; U.menu = nameState.backMenu; flash("NAME", n.isEmpty() ? "(blank)" : n, 900); }

/* ================================================================================================ keys */
int FivePanel::padVoice(int i) { return multiOn ? multiV : S().setups[(size_t) clampInt(S().setup, 0, NUM_SETUPS - 1, 0)].keys[(size_t) i].voice; }
void FivePanel::hitPad(int i, int vel) {
    if (U.page != "ptn" && U.page != "songplay") U.flashUntil = 0;
    auto& pg = page();
    if (pg.padOnly) { pg.pad(i, 0, vel, 0); return; }
    const int v = padVoice(i), acc = held.count("ACC2") ? 2 : held.count("ACC1") ? 1 : 0; if (acc) accUsed = true;
    if (padVel) vel = padVel;
    U.pad = i; U.voice = v; U.ch = v % NCH;
    if (!pg.silentToggle || !proc.running()) {
        if (multiOn) {
            const int n = i - 12; double pitch = 0;
            if (multiParam == 0) pitch = n * multiStep[0];
            proc.playVoice(v, vel, acc, pitch);
        } else proc.hitPad(i, vel, acc);
        if (U.rtw && U.page == "rtw" && proc.running() && U.protect) flash("MEMORY PROTECTED", "");
    }
    if (pg.pad) pg.pad(i, v, vel, acc);
}
void FivePanel::modeKey(const juce::String& k) {
    if (k == "PTN") { U.mode = 0; backToPlay(); U.chainPlay = false; }
    else if (k == "SONG") { U.mode = 1; backToPlay(); }
    else if (k == "EDIT VOICE") openMenu("voice");
    else if (k == "CAS") openMenu("cart", 21);
    else if (k == "CART") openMenu("cart", 22);
    else if (k == "MIDI") openMenu("midi");
    else if (k == "REV") openMenu("rev");
    else if (k == "DLY") openMenu("dly");
    else if (k == "DAMP") { proc.damp(); flash("DAMP", "all voices off", 700); }
    else if (k == "MULTI") { multiOn = !multiOn; if (multiOn) multiV = U.voice; openMenu("key", 3); }
    else if (k == "INT SYNC") { proc.setSync("int"); proc.setHostSync(false); }
    else if (k == "MIDI SYNC") proc.setSync("midi");
    else if (k == "HOST SYNC") { proc.setSync("int"); proc.setHostSync(!proc.hostSync()); }
    else if (k == "RUN") { if (proc.running()) proc.stop(); else proc.start(); }
}
void FivePanel::fnKey(const juce::String& k, bool sh) {
    const bool songish = U.mode == 1 || sh;
    if (k == "PATTERN") { if (sh) { U.mode = 1; U.page = "songplay"; } else { U.mode = U.mode == 1 ? 0 : 1; backToPlay(); } U.menu = {}; U.chainPlay = false; }
    else if (k == "REAL TIME WRITE") { if (songish) { U.mode = 1; openMenu("esong", 0); } else { U.page = "rtw"; proc.setRtw(true); } }
    else if (k == "STEP WRITE") { if (songish) songInsert("pat"); else { U.page = "step"; U.n["stepVoice"] = U.voice; } }
    else if (k == "EDIT VOICE") openMenu("voice");
    else if (k == "KEY ASSIGN") openMenu("key");
    else if (k == "QUANTIZE") { if (songish) songDelete(); else U.page = "quant"; }
    else if (k == "SWING") { if (songish) songInsert("rep"); else U.page = "swing"; }
    else if (k == "CLICK") { if (songish) songInsert("tempo"); else U.page = "click"; }
    else if (k == "CHAIN") openMenu("chain");
    else if (k == "CASSETTE") openMenu("cart");
    else if (k == "EDIT PATTERN") { if (songish) songInsert("vol"); else openMenu("eptn"); }
    else if (k == "CLEAR") { if (U.page != "rtw") U.page = "clear"; }
    else if (k == "COPY") { U.page = "copy"; U.n["copyDst"] = U.mode == 1 ? (U.song + 1) % NUM_SONGS : (U.ptn + 1) % NUM_PATTERNS; }
    else if (k == "SYNC") U.page = "sync";
    else if (k == "MIDI") openMenu("midi");
    else if (k == "REVERSE") { U.page = "reverse"; proc.setVoiceParam(U.voice, vReverse, S().voice[(size_t) U.voice][vReverse] >= .5 ? 0 : 1); }
    else if (k == "DAMP") { proc.damp(); flash("DAMP", "all voices off", 700); }
    else if (k == "TEMPO") U.page = "tempo";
    else if (k == "UTILITY") openMenu("util");
    else if (k == "JOB") openMenu("job");
    if (U.page != "rtw") proc.setRtw(false);
}
void FivePanel::keyDown(const juce::String& k) {
    if (k == "STOP") { held.insert(k); stopWasRun = proc.running(); shiftUsed = false; if (stopWasRun) proc.stop(); repaint(); return; }
    const bool sh = held.count("STOP") > 0; if (sh) shiftUsed = true;
    held.insert(k);
    auto& pg = page();
    if (U.flashUntil > nowMs() && k != "ACC1" && k != "ACC2") U.flashUntil = 0;
    if (k == "START") { if (U.page == "name") nameDone(); else proc.start(); }
    else if (k == "YES" || k == "NO") { if (pg.pm) pg.pm(k == "YES" ? 1 : -1, sh); }
    else if (k.startsWith("N") && k.length() == 2) { if (pg.digit) pg.digit(k.getLastCharacters(1).getIntValue()); }
    else if (k == "ACC1" || k == "ACC2") { accUsed = false; if (U.page == "name") { nameType(k == "ACC1" ? 'Y' : 'Z'); accUsed = true; } }
    else if (k == "RANDOM") { if (U.page == "menu" && U.menu == "job" && curItem().no == 4) doRandom(); else { U.n["rnd"] = 0; openMenu("job", 4); } }
    else if (k.startsWith("M:")) modeKey(k.substring(2));
    else fnKey(k, sh);
    repaint();
}
void FivePanel::keyUp(const juce::String& k) {
    if (!held.count(k)) return;
    held.erase(k);
    if (k == "STOP" && !stopWasRun && !shiftUsed) proc.cont();
    if ((k == "ACC1" || k == "ACC2") && !accUsed) { auto& pg = page(); if (pg.move) pg.move(k == "ACC1" ? -1 : 1); }
    repaint();
}
void FivePanel::key(const juce::String& k) {
    if (k.startsWith("PAD")) { hitPad(k.substring(3).getIntValue(), 110); return; }
    if (k.startsWith("LEGEND:")) { const auto m = k.substring(7).upToFirstOccurrenceOf(":", false, false); openMenu(m, k.fromLastOccurrenceOf(":", false, false).getDoubleValue()); return; }
    keyDown(k); keyUp(k);
}

/* ================================================================================================ mouse, keyboard, drops */
double FivePanel::faderValue(const juce::String& id) {
    const auto& s = S();
    if (id == "VOLUME") return s.volume;
    if (id == "CLICK") return s.clickLevel;
    if (id == "TEMPO") return (curTempo() - 40) / 210;
    if (id == "DATA") return U.n["dataV"] / 1000.0;
    return s.channel[(size_t) id.substring(2).getIntValue()].fader;
}
void FivePanel::setFader(const juce::String& id, double v) {
    v = clampd(v, 0, 1);
    if (id == "VOLUME") proc.setVolume(v);
    else if (id == "CLICK") proc.setClickLevel(v);
    else if (id == "TEMPO") setTempoUi(40 + 210 * v);
    else if (id == "DATA") { U.n["dataV"] = (int) jsround(v * 1000); auto& pg = page(); if (pg.data) pg.data(v); }
    else proc.setChannelParam(id.substring(2).getIntValue(), cFader, v);
}
void FivePanel::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    const auto p = e.position; const Region* r = hit(p); if (!r) return;
    switch (r->kind) {
        case kFader: drag = { r->id, p.y, faderValue(r->id) }; break;
        case kPad: { const float y = (p.y - r->r.getY()) / r->r.getHeight(); pressedPad = r->id.substring(3).getIntValue(); hitPad(pressedPad, (int) jsround(clampd(127 - y * 96, 16, 127))); break; }
        case kMode: pressedKey = r->id; keyDown("M:" + r->id); break;
        case kFn: case kKeypad: case kBig: case kDice: pressedKey = r->id; keyDown(r->id); break;
        case kLcd: { auto& pg = page(); if (pg.move) pg.move(p.x < r->r.getCentreX() ? -1 : 1); break; }
        case kLegend: { if (r->id == "song" || r->id == "esong") U.mode = 1; openMenu(r->id, r->no); break; }
    }
    repaint();
}
void FivePanel::mouseDrag(const juce::MouseEvent& e) {
    if (drag.id.isEmpty()) return;
    setFader(drag.id, drag.v0 - (e.position.y - drag.y0) / (138 - 40));
    repaint();
}
void FivePanel::mouseUp(const juce::MouseEvent&) {
    drag = {}; pressedPad = -1;
    if (pressedKey.isNotEmpty()) { const auto k = pressedKey; pressedKey.clear(); keyUp(held.count("M:" + k) ? "M:" + k : k); }
    repaint();
}
void FivePanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const Region* r = hit(e.position); const int d = w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0; if (!d || !r) return;
    if (r->kind == kLcd) { auto& pg = page(); if (pg.pm) pg.pm(d, e.mods.isShiftDown()); }
    else if (r->kind == kFader) setFader(r->id, faderValue(r->id) + d * .02);
    repaint();
}
bool FivePanel::keyPressed(const juce::KeyPress& k) {
    static const juce::String PADS = "qwertyuiop[]asdfghjkl;'\\";
    const auto c = k.getTextCharacter(); const int pi = PADS.indexOfChar(c);
    if (pi >= 0) { hitPad(pi, 110); repaint(); return true; }
    if (c >= '0' && c <= '9') { key("N" + juce::String::charToString(c)); return true; }
    if (c == '-' || c == '_') { key("NO"); return true; }
    if (c == '=' || c == '+') { key("YES"); return true; }
    if (k.getKeyCode() == juce::KeyPress::returnKey) { key("START"); return true; }
    if (k.getKeyCode() == juce::KeyPress::spaceKey) { if (proc.running()) proc.stop(); else proc.start(); repaint(); return true; }
    if (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey) { auto& pg = page(); if (pg.move) pg.move(k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1); repaint(); return true; }
    if (k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey) { auto& pg = page(); if (pg.pm) pg.pm(k.getKeyCode() == juce::KeyPress::upKey ? 1 : -1, k.getModifiers().isShiftDown()); repaint(); return true; }
    return false;
}
void FivePanel::filesDropped(const juce::StringArray& files, int x, int y) {
    if (files.isEmpty()) return;
    const juce::File f(files[0]); const Region* r = hit({ (float) x, (float) y });
    const auto ext = f.getFileExtension().toLowerCase();
    if (r && r->kind == kPad && (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".ogg" || ext == ".mp3")) {
        const int v = padVoice(r->id.substring(3).getIntValue()); report("LOADED " + vname(v), proc.loadWavIntoVoice(f, v)); return;
    }
    if (ext == ".o5cart" || ext == ".json") report("LOADED", proc.loadCartFile(f));
    else if (ext == ".syx") report("SYX LOADED", proc.loadSyxFile(f));
    else if (ext == ".mid" || ext == ".midi") report("MIDI IMPORTED", proc.importMidiFile(f, 0, U.song));
    else if (ext == ".bin" || ext == ".rom") report("BANK LOADED", proc.loadBankFile(f));
    else if (f.isDirectory() || ext == ".zip") report(proc.lastMidiMessage(), proc.loadKit(f));
    else if (ext == ".wav" || ext == ".aif" || ext == ".aiff") report("LOADED " + vname(U.voice), proc.loadWavIntoVoice(f, U.voice));
}
void FivePanel::pickFile(const juce::String& title, const juce::String& patterns, std::function<void(const juce::File&)> fn, bool save, const juce::String& defName, bool dirs) {
    const auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(defName);
    chooser = std::make_unique<juce::FileChooser>(title, defName.isNotEmpty() ? start : start.getParentDirectory(), patterns);
    const int flags = (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting : juce::FileBrowserComponent::openMode)
        | (dirs ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles);
    juce::Component::SafePointer<FivePanel> self(this);
    chooser->launchAsync(flags, [self, fn](const juce::FileChooser& c) { if (!self) return; const auto f = c.getResult(); if (f != juce::File()) fn(f); self->repaint(); });
}

/* ================================================================================================ animation */
void FivePanel::tick() {
    const int m = proc.firedMask.exchange(0);
    for (int i = 0; i < NV; i++) {
        const int v = padVoice(i);
        if (m & (1 << v)) padLit[(size_t) i] = 1.f; else padLit[(size_t) i] *= .7f;
    }
    meter = juce::jmax(proc.meterL.exchange(0), proc.meterR.exchange(0));
    if (U.flashUntil > 0 && nowMs() > U.flashUntil) U.flashUntil = 0;
    repaint(juce::Rectangle<int>(560, 390, 210, 70)); repaint(juce::Rectangle<int>(90, 400, 1140, 40)); repaint(juce::Rectangle<int>(90, 715, 1140, 145));
    repaint(juce::Rectangle<int>(60, 150, 1200, 160));
}

/* ================================================================================================ painting */
static juce::Font font(float size, bool bold = false) { return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)); }
void FivePanel::paintLcdLines(juce::String& a, juce::String& b, int& cur) {
    cur = -1;
    if (U.flashUntil > nowMs()) { a = U.flash1; b = U.flash2; return; }
    const auto r = page().lcd ? page().lcd() : juce::StringArray { "RX5", "" };
    a = r[0]; b = r[1]; if (r.size() > 2) cur = r[2].getIntValue();
}
juce::String FivePanel::lcdText() const {
    auto* self = const_cast<FivePanel*>(this); juce::String a, b; int c; self->paintLcdLines(a, b, c); return L16(a) + "\n" + L16(b);
}
void FivePanel::paint(juce::Graphics& g) {
    using C = juce::Colour;
    // body, ribs, lip, front
    g.fillAll(C(0xff2e2e2c));
    g.setGradientFill(juce::ColourGradient::vertical(C(0xffd9d9d2), 0, C(0xff141414), 9)); g.fillRect(0, 0, W, 312);
    auto slotG = [](int k) { return (k >= 0 && k <= 3) || (k >= 23 && k <= 34); };
    for (int k = -3; k <= 36; k++) {
        const float left = GX(k) + 4.5f, w = 24;
        juce::ColourGradient rg(C(0xff1e1e1e), left, 0, C(0xff1a1a1a), left + w, 0, false); rg.addColour(.14, C(0xff555555)); rg.addColour(.7, C(0xff4d4d4d)); rg.addColour(.88, C(0xff3d3d3d));
        g.setGradientFill(rg);
        if (slotG(k) || slotG(k + 1)) {
            g.fillRoundedRectangle(left, 8, w, 157, 2);
            const float nl = slotG(k) ? left + 3 : left, nw = w - (slotG(k) ? 3 : 0) - (slotG(k + 1) ? 3 : 0);
            g.fillRoundedRectangle(nl, 165, nw, 133, 3);
            g.setColour(C(0xff1a1a1a)); for (float y = 173; y < 296; y += 7) { if (slotG(k)) g.fillRect(nl, y, 6.f, 2.f); if (slotG(k + 1)) g.fillRect(nl + nw - 6, y, 6.f, 2.f); }
        } else g.fillRoundedRectangle(left, 8, w, 290, 3);
    }
    g.setGradientFill(juce::ColourGradient::vertical(C(0xff6a6a67), 300, C(0xff5a5a57), 346)); g.fillRoundedRectangle(0, 300, W, 46, 10);
    g.setGradientFill(juce::ColourGradient::vertical(C(0xff575754), 340, C(0xff353533), 973)); g.fillRect(0, 340, W, 633);
    // faders
    g.setColour(C(0xffe9e9e6));
    for (const auto& r : regions) if (r.kind == kFader) {
        const double v = clampd(faderValue(r.id), 0, 1); const float cy = r.r.getY() + 22 + (float) ((138 - 40) * (1 - v));
        g.setColour(C(0xff0a0a0a)); g.fillRoundedRectangle(r.r.getX() + 7, cy - 17, 16, 34, 2);
        g.setColour(C(0xff333333)); for (float y = cy - 15; y < cy + 15; y += 4) g.fillRect(r.r.getX() + 8, y, 14.f, 2.f);
        g.setGradientFill(juce::ColourGradient::vertical(C(0xfff2eee2), cy - 1, C(0xffb9b5aa), cy + 3)); g.fillRoundedRectangle(r.r.getX() + 8, cy - 1, 14, 4, 1);
        g.setColour(C(0xffe9e9e6)); g.setFont(font(8, true));
        const juce::String id = r.id;
        if (id.startsWith("CH")) { const int c = id.substring(2).getIntValue(); g.drawFittedText(CH_LBL[c], (int) r.r.getCentreX() - 30, 317, 60, 20, juce::Justification::centredTop, 2); }
        else g.drawText(id, (int) r.r.getCentreX() - 30, id == "CLICK" || id == "DATA" ? 326 : 316, 60, 10, juce::Justification::centred);
    }
    // the display window
    g.setGradientFill(juce::ColourGradient::vertical(C(0xff232220), 392, C(0xff201f1d), 534)); g.fillRoundedRectangle(95, 392, 1124, 142, 5);
    g.setColour(C(0xff000000)); g.drawRoundedRectangle(95, 392, 1124, 142, 5, 1);
    // mode keys
    const auto pos = proc.position(); const bool run = proc.running();
    auto lit = [&](const juce::String& k) {
        if (k == "PTN") return U.mode == 0; if (k == "SONG") return U.mode == 1;
        if (k == "EDIT VOICE") return U.page == "menu" && U.menu == "voice";
        if (k == "CAS" || k == "CART") return U.page == "menu" && U.menu == "cart";
        if (k == "MIDI") return U.page == "menu" && U.menu == "midi"; if (k == "REV") return U.page == "menu" && U.menu == "rev"; if (k == "DLY") return U.page == "menu" && U.menu == "dly";
        if (k == "MULTI") return multiOn; if (k == "INT SYNC") return S().sync != "midi"; if (k == "MIDI SYNC") return S().sync == "midi"; if (k == "HOST SYNC") return proc.hostSync();
        if (k == "RUN") return run && !(pos.tick < 4 && pos.beat == 1);
        if (k == "DAMP") return held.count("M:DAMP") > 0;
        return false;
    };
    for (const auto& r : regions) if (r.kind == kMode) {
        const bool on = lit(r.id);
        g.setGradientFill(juce::ColourGradient::vertical(on ? C(0xffff8a92) : C(0xffdcdcd8), r.r.getY(), on ? C(0xffd23d4a) : C(0xffa9a9a6), r.r.getBottom()));
        g.fillRoundedRectangle(r.r, 2);
        if (on) { g.setColour(C(0x55ff304a)); g.drawRoundedRectangle(r.r.expanded(2), 3, 2); }
        g.setColour(C(0xff222222)); g.setFont(font(7, true));
        juce::String lbl = r.id; for (const auto& m : MODE_KEYS) if (juce::String(m.first).replace("\n", " ") == r.id) lbl = m.first;
        g.drawFittedText(lbl, r.r.toNearestInt(), juce::Justification::centred, 2);
    }
    // LCD
    {
        juce::String a, b; int cur; paintLcdLines(a, b, cur); a = L16(a); b = L16(b);
        const juce::Rectangle<float> lr(576, 400, 180, 46);
        g.setGradientFill(juce::ColourGradient(C(0xffd6e41e), lr.getCentreX(), lr.getCentreY(), C(0xffc4d212), lr.getX(), lr.getY(), true)); g.fillRoundedRectangle(lr, 2);
        g.setColour(C(0xff2a3300)); g.drawRoundedRectangle(lr, 2, 2);
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 17.5f, juce::Font::plain)));
        const bool blink = std::fmod(nowMs(), 1000) < 500;
        for (int row = 0; row < 2; row++) {
            const auto& s = row ? b : a;
            for (int i = 0; i < 16; i++) {
                const float x = 580 + i * 11.05f, y = 404 + row * 20.f;
                g.setColour(C(0x14a8bd0c)); g.fillRect(x, y, 10.8f, 19.f);
                const bool inv = row == 1 && i == cur && blink;
                if (inv) { g.setColour(C(0xff6f8200)); g.fillRect(x, y, 10.8f, 19.f); }
                g.setColour(inv ? C(0xffd6e41e) : C(0xff6f8200));
                g.drawText(s.substring(i, i + 1), juce::Rectangle<float>(x, y, 10.8f, 19.f), juce::Justification::centred, false);
            }
        }
        // peak meter under the LCD
        g.setColour(C(0xff1a1a18)); g.fillRect(576.f, 449.f, 180.f, 3.f);
        g.setColour(meter > .99f ? C(0xffff5050) : C(0xff9ad31e)); g.fillRect(576.f, 449.f, 180.f * juce::jmin(1.f, meter), 3.f);
    }
    // legend
    {
        for (const auto& col : LEGEND) {
            float y = 460;
            for (const auto& gr : col.second) {
                g.setColour(C(0xffe9e9e6)); g.setFont(font(7.6f, true)); g.drawText(gr.head, (int) col.first, (int) y, 90, 9, juce::Justification::left);
                if (juce::String(gr.head).trim().isNotEmpty()) { g.setColour(C(0xffd8d8d8)); g.fillRect(col.first, y + 9, juce::jmin(70.f, 1040.f - col.first), .8f); }
                y += 9; g.setFont(font(7));
                for (const auto& l : gr.lines) {
                    const bool on = U.page == "menu" && U.menu == juce::String(gr.menu) && std::fabs(curItem().no - l.no) < 1e-6;
                    if (on) { g.setColour(C(0x2effffff)); g.fillRect(col.first - 1, y, 90.f, 7.7f); }
                    g.setColour(C(0xffe9e9e6)); g.drawText(l.text, (int) col.first, (int) y, 92, 8, juce::Justification::left);
                    y += 7.7f;
                }
            }
        }
        g.setColour(C(0xfff4f4f2)); g.setFont(font(6.9f)); g.drawText("DIGITAL RHYTHM PROGRAMMER", 1046, 466, 180, 8, juce::Justification::left);
        juce::Path five; juce::GlyphArrangement ga; ga.addLineOfText(juce::Font(juce::FontOptions(46, juce::Font::bold | juce::Font::italic)), "RX5", 1040, 516); ga.createPath(five);
        g.strokePath(five, juce::PathStrokeType(1.3f));
        // dice
        g.setGradientFill(juce::ColourGradient::vertical(C(0xffdcdcd8), 498, C(0xffb4a8a5), 518)); g.fillRoundedRectangle(1168, 498, 20, 20, 3);
        g.setColour(C(0xff222222)); g.drawRoundedRectangle(1170.5f, 500.5f, 15, 15, 2.5f, 1.2f);
        for (auto pt : { juce::Point<float>(1174, 504), { 1182, 504 }, { 1178, 508 }, { 1174, 512 }, { 1182, 512 } }) g.fillEllipse(pt.x - 1.5f, pt.y - 1.5f, 3, 3);
        g.setColour(C(0xffe9e9e6)); g.setFont(font(6, true)); g.drawText("RANDOMIZE", 1150, 520, 56, 8, juce::Justification::centred);
    }
    // function keys, keypad, big keys
    for (const auto& r : regions) {
        const bool dn = pressedKey == r.id && (r.kind == kFn || r.kind == kKeypad || r.kind == kBig);
        auto rr = r.r.translated(0, dn ? 1.f : 0.f);
        if (r.kind == kFn) {
            const bool sel = (U.page == "rtw" && r.id == "REAL TIME WRITE") || (U.page == "step" && r.id == "STEP WRITE") || (U.page == "menu" && ((U.menu == "voice" && r.id == "EDIT VOICE") || (U.menu == "key" && r.id == "KEY ASSIGN") ||
                (U.menu == "chain" && r.id == "CHAIN") || (U.menu == "cart" && r.id == "CASSETTE") || (U.menu == "eptn" && r.id == "EDIT PATTERN") || (U.menu == "midi" && r.id == "MIDI") || (U.menu == "util" && r.id == "UTILITY") ||
                (U.menu == "job" && r.id == "JOB") || (U.menu == "esong" && r.id == "REAL TIME WRITE"))) ||
                (U.page == "quant" && r.id == "QUANTIZE") || (U.page == "swing" && r.id == "SWING") || (U.page == "click" && r.id == "CLICK") || (U.page == "clear" && r.id == "CLEAR") || (U.page == "copy" && r.id == "COPY") ||
                (U.page == "sync" && r.id == "SYNC") || (U.page == "reverse" && r.id == "REVERSE") || (U.page == "tempo" && r.id == "TEMPO");
            g.setColour(sel ? C(0xffb8b8b8) : C(0xff9a9a97)); g.fillRoundedRectangle(rr.expanded(1.5f), 3);
            g.setColour(C(0xff0a0a0a)); g.drawRoundedRectangle(rr.expanded(2.f), 3, 1);
            g.setGradientFill(juce::ColourGradient::vertical(C(0xff4a4a47), rr.getY(), C(0xff1c1c1b), rr.getBottom())); g.fillRoundedRectangle(rr, 2);
            g.setColour(C(0xffececec)); g.setFont(font(6.6f, true));
            juce::String lbl; for (const auto& f : FKEYS) if (r.id == f[0]) { lbl = f[0]; if (f.size() > 1) lbl += juce::String("\n") + f[1]; }
            g.drawFittedText(lbl, rr.toNearestInt(), juce::Justification::centred, 2);
        } else if (r.kind == kKeypad) {
            const bool red = r.id == "YES" || r.id == "NO";
            g.setGradientFill(juce::ColourGradient::vertical(red ? C(0xfff6826a) : C(0xfffbe9cf), rr.getY(), red ? C(0xffe06a44) : C(0xffe4c898), rr.getBottom()));
            g.fillRoundedRectangle(rr, 2); g.setColour(red ? C(0xff6a2416) : C(0xff8b7457)); g.drawRoundedRectangle(rr, 2, 1);
            int i = 0; for (int k = 0; k < 12; k++) { const juce::String n = KEYPAD[k][0]; if ((n == "-1" ? "NO" : n == "+1" ? "YES" : "N" + n) == r.id) i = k; }
            g.setColour(red ? C(0xff5a1a0d) : C(0xff7a6650)); g.setFont(font(9.5f));
            g.drawText(juce::String(KEYPAD[i][0]) + (red ? "    " : "      ") + KEYPAD[i][1], rr, juce::Justification::centred);
        } else if (r.kind == kBig) {
            const bool acc = r.id.startsWith("ACC"), stopK = r.id == "STOP", down = held.count(r.id) > 0 || dn;
            const C top = acc ? C(0xff9ad8ab) : stopK ? C(0xffa0a3ef) : C(0xfff88b6e), bot = acc ? C(0xff78c48e) : stopK ? C(0xff8580bd) : C(0xffe06a44);
            g.setGradientFill(juce::ColourGradient::vertical(down ? top.brighter(.1f) : top, rr.getY(), bot, rr.getBottom())); g.fillRoundedRectangle(rr, 3);
            g.setColour(C(0x88000000)); g.drawRoundedRectangle(rr, 3, 1);
            g.setColour(juce::Colours::white); g.setFont(font(10.5f, true));
            g.drawFittedText(acc ? (r.id == "ACC1" ? "ACCENT 1" : "ACCENT 2") : stopK ? "STOP/\nCONTINUE" : "START", rr.toNearestInt(), juce::Justification::centred, 2);
            if (acc) { g.setFont(font(7, true)); g.drawText(r.id == "ACC1" ? "Y" : "Z", rr.translated(6, 4).withSize(10, 8), juce::Justification::topLeft); }
        }
    }
    g.setColour(C(0xffe9e9e6)); g.setFont(font(8, true));
    g.drawText("SHIFT/COMPARE", 1141, 620, 80, 10, juce::Justification::centred); g.drawText("ENTER", 1141, 692, 80, 10, juce::Justification::centred);
    g.setColour(C(0xffd9d9d9)); g.fillRect(566.f, 558.f, 1.f, 132.f); g.fillRect(283.f, 658.f, 283.f, 1.f); g.fillRect(283.f, 658.f, 1.f, 8.f);
    // pads
    for (const auto& r : regions) if (r.kind == kPad) {
        const int i = r.id.substring(3).getIntValue(); const bool dn = pressedPad == i; auto rr = r.r.translated(0, dn ? 2.f : 0.f);
        g.setColour(C(0xff0a0a0a)); g.fillRoundedRectangle(rr.expanded(3), 4); g.setColour(C(0xff626260)); g.drawRoundedRectangle(rr.expanded(1.5f), 3, 1.5f);
        g.setGradientFill(juce::ColourGradient::vertical(C(0xff3a3937), rr.getY(), C(0xff31302e), rr.getBottom())); g.fillRoundedRectangle(rr, 3);
        if (padLit[(size_t) i] > .02f) { g.setColour(juce::Colours::white.withAlpha(.1f * padLit[(size_t) i])); g.fillRoundedRectangle(rr, 3); }
        g.setColour(C(0xffe8e8e8)); g.setFont(font(7.5f, true)); g.drawText(juce::String::charToString((juce::juce_wchar) ('A' + i)), rr.translated(9, 8).withSize(20, 10), juce::Justification::topLeft);
        if (multiOn) { g.setColour(C(0xff9a9a96)); g.setFont(font(6.5f)); g.drawText(vname(multiV), rr.reduced(4).removeFromBottom(10), juce::Justification::centredRight); }
    }
    g.setColour(C(0xffe9e9e6)); g.setFont(font(9, true));
    for (int i = 0; i < 12; i++) g.drawFittedText(juce::String(PAD_LBL[i]) + "\n" + juce::String(i + 1), (int) PAD_X[i] - 50, 867, 100, 32, juce::Justification::centredTop, 3);
}

/* ================================================================================================ editor */
FiveEditor::FiveEditor(FiveProcessor& p) : AudioProcessorEditor(p), proc(p), content(p) {
    addAndMakeVisible(content);
    setResizable(true, true);
    setResizeLimits(520, 389, 2600, 1946);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) FivePanel::W / FivePanel::H);
    setSize(1040, 778);
    startTimerHz(30);
}
FiveEditor::~FiveEditor() { stopTimer(); }
void FiveEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0c0c0c)); }
void FiveEditor::resized() { content.setTransform(juce::AffineTransform::scale((float) getWidth() / FivePanel::W)); }

}  // namespace five
