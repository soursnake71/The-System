// Omega 7 front panel logic: a line-for-line port of seven/panel.js (buttons, data entry, the 2 x 16 display).
#pragma once
#include "Seven.h"
#include <functional>
#include <string>

namespace seven {

struct PanelHost {
    virtual ~PanelHost() = default;
    virtual Voice156& voice() = 0;
    virtual void setParam(int id, int v) = 0;
    virtual void setVoice(const Voice156& v) = 0;
    virtual Func& func() = 0;
    virtual void setFunc(int k, int v) = 0;
    virtual Bank* bank(bool cart) = 0;
    virtual void store(bool cart, int i, const Voice156& v) = 0;
    virtual void setBank(bool cart, const Bank& b) = 0;
    virtual SeqState& seq() = 0;
    virtual void seqCmd(const std::string& cmd, int a) = 0;     // changed, pattern, arp, play, stop, rec, cursor
    virtual void request(const std::string& kind, int arg) = 0;  // files and MIDI dumps
    virtual bool playing() = 0;
    virtual bool recording() = 0;
    virtual bool playerPlaying() = 0;
    virtual bool playerRec() = 0;
    virtual bool playerLoop() = 0;
    virtual int64_t nowMs() = 0;
};

static const char* const EDIT_BUTTONS[33] = {"", "OP 1 ON/OFF", "OP 2 ON/OFF", "OP 3 ON/OFF", "OP 4 ON/OFF", "OP 5 ON/OFF", "OP 6 ON/OFF", "ALGORITHM", "FEEDBACK", "LFO WAVE", "LFO SPEED",
    "LFO DELAY", "LFO PM DEPTH", "LFO AM DEPTH", "LFO KEY SYNC", "MOD SENSITIVITY", "OSC MODE/SYNC", "FREQ COARSE", "FREQ FINE", "OSC DETUNE", "EG RATE", "EG LEVEL",
    "BREAK POINT", "CURVE", "DEPTH", "RATE SCALING", "OUTPUT LEVEL", "KEY VELOCITY", "P EG RATE", "P EG LEVEL", "KEY TRANSPOSE", "VOICE NAME", "OP COPY"};
static const char* const FUNC_BUTTONS[33] = {"", "MASTER TUNE", "POLY/MONO", "P BEND RANGE", "P BEND STEP", "PORTA MODE", "PORTA GLISS", "PORTA TIME", "MIDI", "EDIT RECALL",
    "VOICE INIT", "LOAD FILE", "SAVE BANK", "SAVE VOICE", "CART > INT", "INT > CART", "BATTERY", "MW RANGE", "MW ASSIGN", "FC RANGE", "FC ASSIGN", "BC RANGE", "BC ASSIGN",
    "AT RANGE", "AT ASSIGN", "ARPEGGIATOR", "TEMPO/CLOCK", "SEQUENCER", "STEP EDIT", "MOTION", "DAC/EFFECTS", "MIDI FILE", "SYSEX"};
inline int editSubs(int n) { switch (n) { case 15: case 16: case 23: case 24: return 2; case 20: case 21: case 28: case 29: return 4; default: return 0; } }
inline int funcSubs(int n) { switch (n) { case 8: return 5; case 18: case 20: case 22: case 24: return 3; case 25: return 7; case 26: return 2; case 27: return 7; case 29: return 8; case 10: return 2; case 30: return 22; case 31: return 5; case 32: return 3; default: return 0; } }
static const char* const NAME_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ -./0";
inline std::vector<int> motionIds() { std::vector<int> a {-1}; for (int i = 0; i <= 144; ++i) a.push_back(i); for (int i = 0; i < 14; ++i) a.push_back(200 + i); for (int i = F::chorus; i < FCOUNT; ++i) a.push_back(200 + i); return a; }
static const int FX_KEYS[22] = {F::dac, F::hiss, F::chorus, F::chorusRate, F::delayMix, F::delayTime, F::dlSync, F::delayFb, F::dlHiCut, F::dlLoCut, F::dlModRate,
    F::dlModDepth, F::dlDuck, F::dlPing, F::revMix, F::rvProg, F::revSize, F::rvHigh, F::rvDelay, F::rvDiff, F::rvEr, F::rvCut};
static const char* const FX_NAMES[22] = {"", "DAC NOISE", "CHORUS DEPTH", "CHORUS RATE", "DELAY RETURN", "DELAY TIME", "DELAY SYNC", "DELAY FDBACK", "DLY HIGH CUT", "DLY LOW CUT",
    "DLY MOD RATE", "DLY MOD DPTH", "DLY DUCKING", "PING-PONG", "REV RETURN", "REV7", "REV TIME", "REV HIGH", "REV DELAY", "REV DIFFUSE", "REV ER LEVEL", "REV HIGH CUT"};
inline std::string motionName(int id) {
    if (id < 0) return "OFF";
    if (id >= 200) return id - 200 < FCOUNT ? FUNC_DEF[id - 200].name : "?";
    const auto& p = vparams()[(size_t) id];
    return p.op ? "OP" + std::to_string(p.op) + " " + p.shortName : std::string(p.shortName);
}

inline std::string pad(std::string s, size_t n) { if (s.size() >= n) return s.substr(0, n); return s + std::string(n - s.size(), ' '); }
inline std::string rpad(std::string s, size_t n) { if (s.size() >= n) return s.substr(s.size() - n); return std::string(n - s.size(), ' ') + s; }
inline std::string rpad(int v, size_t n) { return rpad(std::to_string(v), n); }
inline std::string line(const std::string& l, const std::string& r = "") { const int room = 16 - (int) r.size(); return pad(l, (size_t) std::max(0, room)) + r; }
inline std::string onOff(int v) { return v ? "ON" : "OFF"; }

class Panel {
public:
    enum Mode { play, edit, func };
    explicit Panel(PanelHost& host) : h(host) {}
    PanelHost& h;
    Mode mode = play; bool cart = false; int num = 0; bool edited = false, compare = false; Voice156 compareBuf {};
    bool protect[2] = {true, true}; int protectSel = -1;      // 0 int, 1 cart
    int op = 1, eBtn = 7, eSub = 0, fBtn = 1, fSub = 0;
    bool storing = false, naming = false; int nameCur = 0;
    bool hasRecall = false; Voice156 recallV {}; bool recallCart = false; int recallNum = 0;
    int copyTo = 2; int stepSel = 0, stepPage = 0; bool stepEdit = false; int held = 0;
    std::string msg1, msg2; int64_t msgUntil = 0;

    Voice156& v() { return h.voice(); }
    void flash(const std::string& a, const std::string& b = "", int ms = 1400) { msg1 = line(a); msg2 = line(b); msgUntil = h.nowMs() + ms; }

    /* ----- mode buttons ----- */
    void memSelect(bool c) {
        protectSel = -1; storing = false; naming = false; stepEdit = false;
        if (compare) endCompare();
        if (c && !h.bank(true)) flash("CARTRIDGE", "NOT READY  (11)");
        cart = c; mode = play;
    }
    void editBtn() {
        protectSel = -1; storing = false;
        if (mode == edit) {
            if (compare) endCompare();
            else if (edited) { if (auto* b = h.bank(cart)) { compareBuf = v(); compare = true; h.setVoice((*b)[(size_t) num]); } }
            return;
        }
        mode = edit; naming = false; stepEdit = false;
    }
    void endCompare() { if (!compare) return; compare = false; h.setVoice(compareBuf); }
    void funcBtn() { protectSel = -1; storing = false; naming = false; if (compare) endCompare(); mode = func; }
    void storeBtn() { if (compare) endCompare(); storing = !storing; protectSel = -1; if (storing) flash("MEMORY STORE", std::string("PUSH 1-32 (") + (cart ? "CRT" : "INT") + ")", 4000); }
    void protectBtn(int which) { storing = false; protectSel = protectSel == which ? -1 : which; }
    void opSelect() { if (mode != edit) { flash("OPERATOR SELECT", "IN EDIT MODE"); return; } op = op % 6 + 1; }

    /* ----- numbered buttons ----- */
    void button(int n) {
        if (storing) { doStore(n - 1); return; }
        protectSel = -1;
        if (mode == play) { selectVoice(n - 1); return; }
        if (mode == edit) {
            if (compare) endCompare();
            if (naming) {
                if (n == 31) { naming = false; return; }
                if (n == 32) { const int c = v()[(size_t) (145 + nameCur)]; int sw = c; if (c >= 'a' && c <= 'z') sw = c - 32; else if (c >= 'A' && c <= 'Z') sw = c + 32; set(145 + nameCur, sw); return; }
                if (n == 30) { const int c = v()[(size_t) (145 + nameCur)]; set(145 + nameCur, c >= 48 && c < 57 ? c + 1 : '0'); return; }
                set(145 + nameCur, NAME_CHARS[n - 1]); nameCur = std::min(9, nameCur + 1); return;
            }
            if (n <= 6) { set(155, v()[155] ^ (1 << (6 - n))); return; }
            if (n == 31) { naming = true; nameCur = 0; eBtn = 31; return; }
            if (eBtn == n && editSubs(n)) eSub = (eSub + 1) % editSubs(n); else { eBtn = n; eSub = 0; }
            return;
        }
        if (stepEdit && n <= 22) { stepButton(n); return; }
        if (fBtn == n && funcSubs(n)) fSub = (fSub + 1) % funcSubs(n); else { fBtn = n; fSub = 0; }
        if (n == 28) { stepEdit = true; stepSel = 0; stepPage = 0; } else stepEdit = false;
        if (n == 16) flash("BATTERY VOLT", "= 3.0V", 1500);
    }
    void selectVoice(int i) {
        auto* b = h.bank(cart);
        if (!b) { flash("CARTRIDGE", "NOT READY  (11)"); return; }
        if (edited) { hasRecall = true; recallV = v(); recallCart = cart; recallNum = num; }
        num = i; edited = false;
        h.setVoice((*b)[(size_t) i]);
    }
    void doStore(int i) {
        storing = false;
        if (protect[cart ? 1 : 0]) { flash("MEMORY PROTECTED", cart ? "CARTRIDGE" : "INTERNAL"); return; }
        if (cart && !h.bank(true)) { flash("CARTRIDGE", "NOT READY  (11)"); return; }
        h.store(cart, i, v());
        num = i; edited = false;
        flash("STORED TO", std::string(cart ? "CRT " : "INT ") + std::to_string(i + 1));
    }

    /* ----- targets ----- */
    struct Target { int id = -1; int max = 0, min = 0; std::function<int()> get; std::function<void(int)> setf; std::function<void()> action, noAction; std::function<void(double)> slide; bool valid = false; };
    Target editTarget() {
        const int n = eBtn, ob = opBase(op), s = eSub;
        auto T = [](int id, int max) { Target t; t.id = id; t.max = max; t.valid = true; return t; };
        switch (n) {
            case 7: return T(134, 31); case 8: return T(135, 7); case 9: return T(142, 5); case 10: return T(137, 99);
            case 11: return T(138, 99); case 12: return T(139, 99); case 13: return T(140, 99); case 14: return T(141, 1);
            case 15: return s ? T(ob + 14, 3) : T(143, 7);
            case 16: return s ? T(136, 1) : T(ob + 17, 1);
            case 17: return T(ob + 18, 31); case 18: return T(ob + 19, 99); case 19: return T(ob + 20, 14);
            case 20: return T(ob + s, 99); case 21: return T(ob + 4 + s, 99); case 22: return T(ob + 8, 99);
            case 23: return T(ob + 11 + s, 3); case 24: return T(ob + 9 + s, 99); case 25: return T(ob + 13, 7);
            case 26: return T(ob + 16, 99); case 27: return T(ob + 15, 7); case 28: return T(126 + s, 99); case 29: return T(130 + s, 99);
            case 30: return T(144, 48);
            case 32: { Target t; t.valid = true; t.action = [this] { copyOp(); }; t.slide = [this](double x) { copyTo = 1 + (int) jsRound(x * 5); }; return t; }
        }
        return {};
    }
    void copyOp() {
        if (copyTo == op) { flash("OP COPY", "PICK ANOTHER OP"); return; }
        const int a = opBase(op), b = opBase(copyTo);
        for (int i = 0; i < 21; ++i) set(b + i, v()[(size_t) (a + i)]);
        flash("OP" + std::to_string(op) + " COPIED", "TO OP" + std::to_string(copyTo));
    }
    Target fn(int k) { Target t; t.valid = true; t.max = FUNC_DEF[k].max; t.get = [this, k] { return h.func()[k]; }; t.setf = [this, k](int x) { h.setFunc(k, x); }; return t; }
    Target act(std::function<void()> a, std::function<void()> no = nullptr) { Target t; t.valid = true; t.action = std::move(a); t.noAction = std::move(no); return t; }
    Target ref(int& field, int max, int min = 0) { Target t; t.valid = true; t.max = max; t.min = min; int* f = &field; t.get = [f] { return *f; }; t.setf = [this, f](int x) { *f = x; h.seqCmd("changed", 0); }; return t; }
    Target flag(bool& field) { Target t; t.valid = true; t.max = 1; bool* f = &field; t.get = [f] { return *f ? 1 : 0; }; t.setf = [this, f](int x) { *f = x != 0; h.seqCmd("changed", 0); }; return t; }
    Target funcTarget() {
        const int n = fBtn, s = fSub; SeqState& q = h.seq(); Arp& a = q.arp;
        switch (n) {
            case 1: return fn(F::tune); case 2: return fn(F::mono); case 3: return fn(F::pbRange); case 4: return fn(F::pbStep);
            case 5: return fn(F::portaMode); case 6: return fn(F::gliss); case 7: return fn(F::portaTime);
            case 8: switch (s) { case 0: return fn(F::rxCh); case 1: return fn(F::sysInfo); case 2: return fn(F::txCh); case 3: return act([this] { h.request("sendVoice", 0); }); default: return act([this] { h.request("sendBank", 0); }); }
            case 9: return act([this] { editRecall(); });
            case 10: if (s) return act([this] { random(); }); return act([this] { h.setVoice(initVoice()); edited = true; flash("VOICE INIT", "DONE"); });
            case 11: return act([this] { h.request("loadFile", 0); });
            case 12: return act([this] { h.request("saveBank", cart ? 1 : 0); });
            case 13: return act([this] { h.request("saveVoice", 0); });
            case 14: return act([this] { copyBank(true, false); });
            case 15: return act([this] { copyBank(false, true); });
            case 17: return fn(F::mwRange); case 19: return fn(F::fcRange); case 21: return fn(F::bcRange); case 23: return fn(F::atRange);
            case 18: case 20: case 22: case 24: {
                const int k = n == 18 ? F::mwAssign : n == 20 ? F::fcAssign : n == 22 ? F::bcAssign : F::atAssign, bit = 1 << s;
                Target t; t.valid = true; t.max = 1; t.get = [this, k, bit] { return (h.func()[k] & bit) ? 1 : 0; };
                t.setf = [this, k, bit](int x) { h.setFunc(k, x ? (h.func()[k] | bit) : (h.func()[k] & ~bit)); }; return t;
            }
            case 25: {
                if (s == 0) { Target t; t.valid = true; t.max = 1; t.get = [&a] { return a.on ? 1 : 0; }; t.setf = [this](int x) { h.seqCmd("arp", x); }; return t; }
                switch (s) { case 1: return ref(a.mode, 7); case 2: return ref(a.oct, 4, 1); case 3: return ref(a.rate, 5); case 4: return ref(a.gate, 100, 5); case 5: return flag(a.latch); default: return ref(a.swing, 75, 50); }
            }
            case 26: {
                if (s == 0) { Target t; t.valid = true; t.max = 300; t.min = 30; t.get = [&q] { return (int) q.tempo; }; t.setf = [this, &q](int x) { q.tempo = x; h.seqCmd("changed", 0); }; return t; }
                return flag(q.ext);
            }
            case 27: {
                Pattern& p = q.pats[(size_t) q.pat];
                switch (s) {
                    case 0: { Target t; t.valid = true; t.max = 15; t.get = [&q] { return q.pat; }; t.setf = [this](int x) { h.seqCmd("pattern", x); }; return t; }
                    case 1: return act([this] { h.seqCmd("play", 0); }, [this] { h.seqCmd("stop", 0); });
                    case 2: return act([this] { h.seqCmd("rec", 1); }, [this] { h.seqCmd("rec", 0); });
                    case 3: return ref(p.len, 64, 1); case 4: return ref(p.rate, 5); case 5: return ref(p.swing, 75, 50);
                    default: return flag(q.keyTrans);
                }
            }
            case 29: {
                Lane& l = q.pats[(size_t) q.pat].lanes[(size_t) (s & 3)];
                if (s >= 4) return flag(l.smooth);
                const auto ids = motionIds();
                Target t; t.valid = true; t.max = (int) ids.size() - 1;
                t.get = [&l, ids] { for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == l.param) return (int) i; return 0; };
                t.setf = [this, &l, ids](int x) { const int id = ids[(size_t) x]; if (id != l.param) { l.param = id; l.vals.fill(-1); } h.seqCmd("changed", 0); };
                return t;
            }
            case 30: return fn(FX_KEYS[s]);
            case 31: switch (s) {
                case 0: return act([this] { h.request("midiPlay", 0); }, [this] { h.request("midiStop", 0); });
                case 1: return act([this] { h.request("midiImport", 0); });
                case 2: return act([this] { h.request("midiExport", 0); });
                case 3: return act([this] { h.request("recStart", 0); }, [this] { h.request("recStop", 0); });
                default: { Target t; t.valid = true; t.max = 1; t.get = [this] { return h.playerLoop() ? 1 : 0; }; t.setf = [this](int x) { h.request("midiLoop", x); }; return t; }
            }
            case 32: switch (s) { case 0: return act([this] { h.request("sendBank", 0); }); case 1: return act([this] { h.request("requestBank", 0); }); default: return act([this] { h.request("requestVoice", 0); }); }
        }
        return {};
    }
    // the dice: a random voice into the edit buffer (STORE keeps it); reset: back to the voice as stored
    int dicePart = 0;   // what the dice rolls: all, algorithm, frequencies, envelopes, LFO, effects
    void random(uint32_t seed = 0) {
        if (!seed) seed = (uint32_t) (h.nowMs() * 2654435761u) ^ 0x9e3779b9u;
        if (dicePart == 5) {
            const auto f = randomFx(seed);
            int prog = 0, rev = 0, dly = 0;
            for (auto& kv : f) { h.setFunc(kv.first, kv.second); if (kv.first == F::rvProg) prog = kv.second; if (kv.first == F::revMix) rev = kv.second; if (kv.first == F::delayMix) dly = kv.second; }
            flash("RANDOM EFFECTS", std::string(RV_PROGS[rev ? prog : 0]) + (dly ? " + DELAY" : ""));
            return;
        }
        const Voice156 v = rollPart(v_(), seed, dicePart);
        h.setVoice(v); edited = true;
        flash(dicePart == 0 ? "RANDOM VOICE" : std::string("RANDOM ") + DICE_NAMES[dicePart], voiceName(v));
    }
    Voice156 v_() { return h.voice(); }
    void nextDicePart() { dicePart = (dicePart + 1) % 6; flash("DICE ROLLS", DICE_NAMES[dicePart]); }
    void resetVoice() {
        auto* b = h.bank(cart);
        if (!b) { flash("CARTRIDGE", "NOT READY  (11)"); return; }
        if (compare) endCompare();
        h.setVoice((*b)[(size_t) num]); edited = false;
        flash("VOICE RESET", std::string(cart ? "CRT " : "INT ") + std::to_string(num + 1));
    }
    void editRecall() {
        if (!hasRecall) { flash("EDIT RECALL", "NOTHING TO RECALL"); return; }
        cart = recallCart; num = recallNum; h.setVoice(recallV); edited = true; hasRecall = false;
        flash("EDIT RECALL", "DONE"); mode = edit;
    }
    void copyBank(bool fromCart, bool toCart) {
        auto* b = h.bank(fromCart);
        if (!b) { flash("CARTRIDGE", "NOT READY  (11)"); return; }
        if (protect[toCart ? 1 : 0]) { flash("MEMORY PROTECTED", toCart ? "CARTRIDGE" : "INTERNAL"); return; }
        h.setBank(toCart, *b);
        flash(fromCart ? "CART > INT" : "INT > CART", "COMPLETED");
    }

    /* ----- YES / NO / data entry ----- */
    void yes() { inc(1); }
    void no() { inc(-1); }
    void inc(int d) {
        if (protectSel >= 0) { protect[protectSel] = d > 0; return; }
        if (mode == edit) {
            if (compare) return;
            if (naming) { nameCur = std::max(0, std::min(9, nameCur + d)); return; }
            auto t = editTarget(); if (!t.valid) return;
            if (t.action) { if (d > 0) t.action(); return; }
            set(t.id, std::max(t.min, std::min(t.max, (int) v()[(size_t) t.id] + d)));
            return;
        }
        if (mode == func) {
            if (stepEdit) { stepInc(d); return; }
            auto t = funcTarget(); if (!t.valid) return;
            if (t.action) { if (d > 0) t.action(); else if (t.noAction) t.noAction(); return; }
            t.setf(std::max(t.min, std::min(t.max, t.get() + d)));
            return;
        }
        selectVoice((num + d + 32) % 32);
    }
    void data(double x) {
        x = std::max(0.0, std::min(1.0, x));
        if (protectSel >= 0) return;
        if (mode == edit) {
            if (compare) return;
            if (naming) { set(145 + nameCur, 32 + (int) jsRound(x * 94)); return; }
            auto t = editTarget(); if (!t.valid) return;
            if (t.action) { if (t.slide) t.slide(x); return; }
            set(t.id, t.min + (int) jsRound(x * (t.max - t.min)));
            return;
        }
        if (mode == func) {
            if (stepEdit) { auto& st = curStep(); st.gate = std::max(5, (int) jsRound(5 + x * (TIE - 5))); h.seqCmd("changed", 0); return; }
            auto t = funcTarget(); if (!t.valid || t.action) return;
            t.setf(t.min + (int) jsRound(x * (t.max - t.min)));
        }
    }
    void set(int id, int val) { h.setParam(id, val); edited = true; }

    /* ----- step edit ----- */
    Step& curStep() { auto& q = h.seq(); return q.pats[(size_t) q.pat].steps[(size_t) stepSel]; }
    void stepButton(int n) {
        auto& q = h.seq(); auto& p = q.pats[(size_t) q.pat];
        if (n <= 16) {
            const int i = stepPage * 16 + n - 1;
            if (i >= p.len) { flash("STEP " + std::to_string(i + 1), "PAST LENGTH " + std::to_string(p.len)); return; }
            if (stepSel == i) p.steps[(size_t) i].on = !p.steps[(size_t) i].on;
            stepSel = i; h.seqCmd("cursor", i); h.seqCmd("changed", 0);
            return;
        }
        if (n <= 20) { stepPage = n - 17; return; }
        if (n == 21) { auto& st = p.steps[(size_t) stepSel]; st.notes.clear(); st.gate = 75; h.seqCmd("changed", 0); flash("STEP " + std::to_string(stepSel + 1), "CLEARED"); return; }
        if (n == 22) { p = Pattern(); h.seqCmd("changed", 0); flash("PATTERN " + std::to_string(q.pat + 1), "CLEARED"); return; }
    }
    void stepInc(int d) { auto& q = h.seq(); auto& p = q.pats[(size_t) q.pat]; stepSel = (stepSel + d + p.len) % p.len; stepPage = stepSel / 16; h.seqCmd("cursor", stepSel); }
    bool stepNote(int n, int vel, bool down) {
        if (!(mode == func && stepEdit)) return false;
        auto& st = curStep();
        if (down) {
            if (!held) st.notes.clear();
            bool has = false; for (auto& x : st.notes) if (x.first == n) has = true;
            if ((int) st.notes.size() < STEP_NOTES && !has) st.notes.push_back({n, vel});
            st.on = true; held++;
        } else {
            held = std::max(0, held - 1);
            if (!held) { auto& q = h.seq(); auto& p = q.pats[(size_t) q.pat]; stepSel = (stepSel + 1) % p.len; stepPage = stepSel / 16; h.seqCmd("cursor", stepSel); }
        }
        h.seqCmd("changed", 0);
        return true;
    }

    /* ----- display ----- */
    std::pair<std::string, std::string> lines() {
        if (msgUntil && h.nowMs() < msgUntil) return {msg1, msg2};
        msgUntil = 0;
        if (protectSel >= 0) return {pad("MEMORY PROTECT", 16), line(protectSel == 0 ? "INTERNAL" : "CARTRIDGE", onOff(protect[protectSel]))};
        if (mode == edit) { auto l = editLines(); return {pad(l.first, 16), pad(l.second, 16)}; }
        if (mode == func) { auto l = funcLines(); return {pad(l.first, 16), pad(l.second, 16)}; }
        auto& q = h.seq(); auto& f = h.func();
        std::string flags; flags += q.arp.on ? 'A' : ' '; flags += h.playing() ? (h.recording() ? 'R' : '>') : ' '; flags += f[F::mono] ? 'M' : 'P';
        const std::string l1 = std::string(cart ? "C" : "I") + rpad(num + 1, 2) + (edited ? "*" : " ") + rpad(flags, 12);
        return {pad(l1, 16), pad(voiceName(v()), 16)};
    }
    std::pair<std::string, std::string> editLines() {
        auto& vv = v(); const int ob = opBase(op), s = eSub;
        std::string bits; for (int o = 1; o <= 6; ++o) bits += ((vv[155] >> (6 - o)) & 1) ? '1' : '0';
        const std::string l1 = "ALG" + rpad(vv[134] + 1, 2) + " " + bits + " OP" + std::to_string(op);
        if (compare) return {"COMPARE   I" + rpad(num + 1, 2) + "  ", voiceName(vv) + "      "};
        if (naming) { const auto nm = voiceName(vv); return {"VOICE NAME   " + rpad(nameCur + 1, 2) + " ", nm.substr(0, (size_t) nameCur) + "[" + nm[(size_t) nameCur] + "]" + nm.substr((size_t) nameCur + 1)}; }
        std::string l2;
        auto V = [&](int i) { return (int) vv[(size_t) i]; };
        switch (eBtn) {
            case 7: l2 = line("ALGORITHM", "=" + rpad(V(134) + 1, 3)); break;
            case 8: l2 = line("FEEDBACK", "=" + rpad(V(135), 3)); break;
            case 9: l2 = std::string("LFO WAVE=") + WAVES[V(142)]; break;
            case 10: l2 = line("LFO SPEED", "=" + rpad(V(137), 3)); break;
            case 11: l2 = line("LFO DELAY", "=" + rpad(V(138), 3)); break;
            case 12: l2 = line("LFO PM DEPTH", "=" + rpad(V(139), 3)); break;
            case 13: l2 = line("LFO AM DEPTH", "=" + rpad(V(140), 3)); break;
            case 14: l2 = line("LFO KEY SYNC", "=" + onOff(V(141))); break;
            case 15: l2 = s ? line("A MOD SENS", "=" + rpad(V(ob + 14), 3)) : line("P MOD SENS", "=" + rpad(V(143), 3)); break;
            case 16: l2 = s ? line("OSC KEY SYNC", "=" + onOff(V(136))) : std::string("OSC MODE=") + (V(ob + 17) ? "FIXED" : "RATIO"); break;
            case 17: l2 = line("F COARSE", "=" + rpad(freqText(vv, op), 7)); break;
            case 18: l2 = line("F FINE", "=" + rpad(freqText(vv, op), 7)); break;
            case 19: { const int d = V(ob + 20) - 7; l2 = line("OSC DETUNE", "=" + rpad((d > 0 ? "+" : "") + std::to_string(d), 3)); break; }
            case 20: l2 = line("EG RATE  " + std::to_string(s + 1), "=" + rpad(V(ob + s), 3)); break;
            case 21: l2 = line("EG LEVEL " + std::to_string(s + 1), "=" + rpad(V(ob + 4 + s), 3)); break;
            case 22: l2 = line("BREAK POINT", "=" + rpad(bpName(V(ob + 8)), 4)); break;
            case 23: l2 = line(std::string(s ? "R" : "L") + " CURVE", std::string("=") + CURVES[V(ob + 11 + s)]); break;
            case 24: l2 = line(std::string(s ? "R" : "L") + " DEPTH", "=" + rpad(V(ob + 9 + s), 3)); break;
            case 25: l2 = line("RATE SCALING", "=" + rpad(V(ob + 13), 3)); break;
            case 26: l2 = line("OUTPUT LEVEL", "=" + rpad(V(ob + 16), 3)); break;
            case 27: l2 = line("KEY VELOCITY", "=" + rpad(V(ob + 15), 3)); break;
            case 28: l2 = line("P EG RATE  " + std::to_string(s + 1), "=" + rpad(V(126 + s), 3)); break;
            case 29: l2 = line("P EG LEVEL " + std::to_string(s + 1), "=" + rpad(V(130 + s), 3)); break;
            case 30: l2 = line("MIDDLE C", "=" + rpad(noteName(60 + V(144) - 24), 4)); break;
            case 32: l2 = line("COPY TO OP" + std::to_string(copyTo), "?YES"); break;
            default: l2 = "OP" + std::to_string(eBtn) + " ON/OFF";
        }
        return {l1, l2};
    }
    static std::string fx1(double v) { char b[16]; std::snprintf(b, sizeof b, "%.1f", v); return b; }
    std::string fxLine(int s, Func& f) {
        const int k = FX_KEYS[s], v = f[k];
        switch (k) {
            case F::dac: return v ? "DAC=16BIT CLEAN" : "DAC=12BIT MARK I";
            case F::rvProg: return v < REV7_FULL ? line("REV7", std::string("=") + RV_PROGS[v]) : pad(rpad(v - REV7_FULL + 1, 2) + " " + RV_PROGS[v], 16);
            case F::dlSync: return line("DELAY SYNC", std::string("=") + DL_DIV_NAMES[v]);
            case F::dlPing: return line("PING-PONG", "=" + onOff(v));
            case F::delayTime: return line("DELAY TIME", "=" + rpad(v, 4) + "ms");
            case F::revSize: return line("REV TIME", "=" + fx1(std::max(3, v) / 10.0) + "s");
            case F::rvHigh: return line("REV HIGH", "=" + fx1(std::max(1, v) / 10.0));
            case F::rvDelay: return line("REV DELAY", "=" + rpad(v, 3) + "ms");
            case F::rvCut: return line("REV HIGH CUT", "=" + fx1(std::max(10, v) / 10.0) + "k");
            case F::dlHiCut: return line("DLY HIGH CUT", "=" + fx1(std::max(10, v) / 10.0) + "k");
            case F::dlLoCut: return line("DLY LOW CUT", "=" + rpad(std::max(2, v) * 10, 4));
            default: return line(FX_NAMES[s], "=" + rpad(v, 3));
        }
    }
    std::pair<std::string, std::string> funcLines() {
        if (stepEdit) return stepLines();
        const int n = fBtn, s = fSub; auto& f = h.func(); auto& q = h.seq(); auto& a = q.arp;
        const std::string title = "F" + rpad(n, 2) + " " + FUNC_BUTTONS[n];
        static const char* ASSIGN[3] = {"PITCH", "AMP", "EG BIAS"};
        auto asg = [&](int k) { return line(ASSIGN[s], "=" + onOff((f[k] >> s) & 1)); };
        std::string l2;
        switch (n) {
            case 1: { const int c = (int) jsRound((f[F::tune] - 64) * 75 / 64.0); l2 = line("MASTER TUNE", (c > 0 ? "+" : "") + std::to_string(c) + "c"); break; }
            case 2: l2 = f[F::mono] ? "MONO MODE" : "POLY MODE"; break;
            case 3: l2 = line("P BEND RANGE", "=" + rpad(f[F::pbRange], 3)); break;
            case 4: l2 = line("P BEND STEP", "=" + rpad(f[F::pbStep], 3)); break;
            case 5: l2 = f[F::mono] ? (f[F::portaMode] ? "FULL TIME PORTA" : "FINGERED PORTA") : (f[F::portaMode] ? "SUS-KEY P FOLLOW" : "SUS-KEY P RETAIN"); break;
            case 6: l2 = line("GLISSANDO", "=" + onOff(f[F::gliss])); break;
            case 7: l2 = line("PORTA TIME", "=" + rpad(f[F::portaTime], 3)); break;
            case 8: switch (s) { case 0: l2 = line("MIDI CH", "=" + (f[F::rxCh] ? rpad(f[F::rxCh], 3) : std::string("OMNI"))); break; case 1: l2 = std::string("SYS INFO ") + (f[F::sysInfo] ? "AVAIL" : "UNAVAIL"); break;
                case 2: l2 = line("MIDI TX CH", "=" + rpad(f[F::txCh] + 1, 3)); break; case 3: l2 = "SEND VOICE?  YES"; break; default: l2 = "SEND BANK ?  YES"; } break;
            case 9: l2 = "EDIT RECALL? YES"; break;
            case 10: l2 = s ? "RANDOM VOICE?YES" : "VOICE INIT ? YES"; break;
            case 11: l2 = "LOAD .SYX ?  YES"; break;
            case 12: l2 = std::string("SAVE ") + (cart ? "CRT" : "INT") + " BANK?YES"; break;
            case 13: l2 = "SAVE VOICE ? YES"; break;
            case 14: l2 = "CART>INT ?   YES"; break;
            case 15: l2 = "INT>CART ?   YES"; break;
            case 16: l2 = "BATTERY  = 3.0V"; break;
            case 17: l2 = line("MW RANGE", "=" + rpad(f[F::mwRange], 3)); break; case 18: l2 = asg(F::mwAssign); break;
            case 19: l2 = line("FC RANGE", "=" + rpad(f[F::fcRange], 3)); break; case 20: l2 = asg(F::fcAssign); break;
            case 21: l2 = line("BC RANGE", "=" + rpad(f[F::bcRange], 3)); break; case 22: l2 = asg(F::bcAssign); break;
            case 23: l2 = line("AT RANGE", "=" + rpad(f[F::atRange], 3)); break; case 24: l2 = asg(F::atAssign); break;
            case 25: switch (s) { case 0: l2 = line("ARP", "=" + onOff(a.on)); break; case 1: l2 = std::string("MODE=") + ARP_MODES[a.mode]; break; case 2: l2 = line("OCTAVES", "=" + rpad(a.oct, 3)); break;
                case 3: l2 = line("RATE", std::string("=") + RATE_NAMES[a.rate]); break; case 4: l2 = line("GATE", "=" + rpad(a.gate, 3) + "%"); break; case 5: l2 = line("LATCH", "=" + onOff(a.latch)); break;
                default: l2 = line("SWING", "=" + rpad(a.swing, 3) + "%"); } break;
            case 26: l2 = s ? std::string("CLOCK=") + (q.ext ? "MIDI/HOST" : "INTERNAL") : line("TEMPO", "=" + rpad((int) q.tempo, 3) + "BPM"); break;
            case 27: { auto& p = q.pats[(size_t) q.pat];
                switch (s) { case 0: l2 = line("PATTERN", "=" + rpad(q.pat + 1, 3)); break; case 1: l2 = h.playing() ? "PLAYING   NO=STP" : "STOPPED  YES=PLY"; break; case 2: l2 = "REC " + onOff(h.recording()) + " YES/NO"; break;
                    case 3: l2 = line("LENGTH", "=" + rpad(p.len, 3)); break; case 4: l2 = line("STEP", std::string("=") + RATE_NAMES[p.rate]); break; case 5: l2 = line("SWING", "=" + rpad(p.swing, 3) + "%"); break;
                    default: l2 = line("KEY TRANSP", "=" + onOff(q.keyTrans)); }
                break; }
            case 29: { auto& l = q.pats[(size_t) q.pat].lanes[(size_t) (s & 3)]; l2 = s < 4 ? "L" + std::to_string(s + 1) + "=" + motionName(l.param) : line("L" + std::to_string((s & 3) + 1) + " SMOOTH", "=" + onOff(l.smooth)); break; }
            case 30: l2 = fxLine(s, f); break;
            case 31: switch (s) { case 0: l2 = std::string("PLAY ") + (h.playerPlaying() ? "ON  NO=STOP" : "OFF YES=FILE"); break; case 1: l2 = "IMPORT>PAT ? YES"; break; case 2: l2 = "EXPORT PAT ? YES"; break;
                case 3: l2 = std::string("RECORD ") + (h.playerRec() ? "ON  NO=SAVE" : "OFF YES=GO"); break; default: l2 = line("LOOP", "=" + onOff(h.playerLoop())); } break;
            case 32: l2 = s == 0 ? "SEND BANK  ? YES" : s == 1 ? "REQUEST BANK?YES" : "REQUEST VOICE?YS"; break;
        }
        return {title, l2};
    }
    std::pair<std::string, std::string> stepLines() {
        auto& q = h.seq(); auto& p = q.pats[(size_t) q.pat]; auto& st = p.steps[(size_t) stepSel];
        std::string notes; for (auto& x : st.notes) { if (!notes.empty()) notes += ' '; notes += noteName(x.first); }
        if (notes.empty() && st.on) notes = "REST";
        const std::string g = st.gate >= TIE ? "TIE" : std::to_string(st.gate) + "%";
        return {"P" + rpad(q.pat + 1, 2) + " STEP " + rpad(stepSel + 1, 2) + std::string(st.on ? " " : " OFF").substr(0, 4) + rpad(g, 4).substr(0, 4), pad(st.on ? notes : "STEP OFF", 16)};
    }
    // the lit button (1-32), and the step lights while step editing
    int litButton() const { if (mode == play) return num + 1; if (mode == edit) return naming ? 31 : eBtn; if (stepEdit) return 17 + stepPage; return fBtn; }
};

}  // namespace seven
