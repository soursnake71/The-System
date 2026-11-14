// Omega Linn firmware: line-for-line C++ port of linn/machine.js (the LinnDrum's front panel, its 49 patterns and 49
// songs, real-time recording with ERROR CORRECT and BEFORE / AFTER timing, ERASE, COPY, LENGTH, the song keys, the
// displays and LEDs). The plugin runs it on its audio thread. linn/tools/compare.mjs checks it against machine.js.
#pragma once
#include "Linn.h"
#include <algorithm>
#include <functional>
#include <string>

namespace linn {
constexpr int PPQ = 192;
constexpr int MAX_EV = 3000;
constexpr int MEM_CAP = 12000;
constexpr int NPAT = 49;
constexpr int DEF_LEN = 8 * PPQ;
namespace K { enum { SONGPAT = 0, SONGNUM, END, DELETE_, INSERT, LEFT, RIGHT, ENTER, D1, STORE = 15, LOAD, EXTSYNC, BPM, PLAY, DRUM }; }
namespace KP { enum { ERRCOR = 1, BEFORE, AFTER, COPY, LENGTH, ERASE, RECORD }; }
constexpr int NDRUM = 16, PERC_KEY = 7;
constexpr int DRUM_KEYS[NDRUM][2] = {{5, 5}, {2, 2}, {3, 3}, {4, 4}, {0, 0}, {1, 1}, {14, 14}, {-1, -1},
    {6, 15}, {7, 16}, {8, 17}, {9, 18}, {10, 19}, {11, 20}, {12, 21}, {13, 22}};
constexpr int EC_GRID[7] = {96, 64, 48, 32, 24, 16, 1};
inline const char* const EC_NAMES[7] = {"1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "HI"};
constexpr int SHUF16[6] = {0, 4, 8, 12, 16, 20};
constexpr int SHUF8[6] = {0, 8, 15, 23, 32, 40};
constexpr int TRIG_DIV[7] = {96, 64, 48, 32, 24, 16, 0};
inline int pnum(int i) { return (i / 7 + 1) * 10 + i % 7 + 1; }
inline int pidx(int n) { return (n / 10 - 1) * 7 + n % 10 - 1; }

struct Ev { int t = 0, s = 0; bool skip = false, del = false; };
inline bool evLess(const Ev& a, const Ev& b) { return a.t != b.t ? a.t < b.t : a.s < b.s; }
struct Pattern { int len = DEF_LEN, ec = 2, tim = 0, bef = 0; std::vector<Ev> ev; };
struct Song { std::vector<int> steps; };
struct Memory { std::vector<Pattern> pats; std::vector<Song> songs; bool repeat = true; };
#include "Factory.inc"
inline Memory defaultMemory() {
    Memory m; m.pats.resize(NPAT); m.songs.resize(NPAT);
    for (int i = 0; i < kFactoryPats; ++i) { Pattern& p = m.pats[(size_t) i]; const auto& f = FACTORY_PATS[i]; p.len = f.len; p.ec = f.ec; p.tim = f.tim;
        for (int k = 0; k < f.n; ++k) { Ev e; e.t = FACTORY_EV[f.at + k][0]; e.s = FACTORY_EV[f.at + k][1]; p.ev.push_back(e); } }
    for (int i = 0; i < kDemoSteps; ++i) m.songs[0].steps.push_back(DEMO_SONG[i]);
    return m;
}
inline int usedNotes(const Memory& m) { int a = 0; for (const auto& p : m.pats) a += (int) p.ev.size(); return a; }
inline int soundCh(int s) { return SOUNDS[s].ch; }

struct Display { std::string l = "  ", r = "  "; bool song = false; int tim = 0, ec = 2; bool play = false, rec = false, ext = false, perc = false; int beat = -1, sel = 0; bool repeat = true; };

class Machine {
public:
    Machine(Engine& eng, double sampleRate) : e(eng), sr(sampleRate) { mem = defaultMemory(); }
    Engine& e; double sr; Memory mem;
    int mode = 0, pat = 0, song = 0, cursor = 0, next = -1;
    int held = 0, used = 0; std::string entry; int trig = 2; bool perc = false, ext = false;
    bool running = false, recording = false; int ppat = 0, sstep = 0, tk = 0; double countdown = 0; int lenArm = -1;
    double bpm = 120; int eraseHeld = 0, sel = 0;
    long long now = 0; std::string flashL, flashR; long long flashUntil = 0;
    bool metroAlways = false; int version = 0; double clockAcc = 0;
    std::function<void(int)> onHit; std::function<void()> onClock, onStop, onStart, onTrig;

    void changed() { version++; }
    Pattern& curPat() { return mem.pats[(size_t) (running ? ppat : pat)]; }
    void flash(const std::string& l, const std::string& r, double ms) { flashL = l; flashR = r; flashUntil = now + (long long) jsRound(ms * sr / 1000); }
    bool isHeld(int k) const { return ((held >> k) & 1) == 1; }
    void key(int id, bool down) {
        if (id < 0 || id >= K::DRUM + NDRUM) return;
        if (down) used |= held;
        if (id < K::DRUM) { if (down) { held |= 1 << id; used &= ~(1 << id); } else held &= ~(1 << id); }
        if (id >= K::DRUM) { drum(id - K::DRUM, down); return; }
        if (!down) {
            if (id == K::SONGPAT && !((used >> id) & 1)) { if (running) stop(); mode = mode ? 0 : 1; cursor = 0; changed(); }
            if (id < K::D1 || id >= K::D1 + 7) entry.clear();
            return;
        }
        if (!(id >= K::D1 && id < K::D1 + 7)) entry.clear();
        if (id >= K::D1 && id < K::D1 + 7) { digit(id - K::D1 + 1); return; }
        switch (id) {
            case K::PLAY: play(); break;
            case K::EXTSYNC: ext = !ext; changed(); break;
            case K::END:
                if (mode == 1 && isHeld(K::SONGPAT)) { mem.repeat = !mem.repeat; e.beep(); flash(mem.repeat ? "rE" : "St", "P ", 900); changed(); }
                else if (mode == 1 && isHeld(K::ENTER)) { auto& s = mem.songs[(size_t) song]; s.steps.resize(std::min(s.steps.size(), (size_t) cursor)); changed(); }
                break;
            case K::DELETE_: if (mode == 1) { auto& s = mem.songs[(size_t) song]; if (cursor < (int) s.steps.size()) { s.steps.erase(s.steps.begin() + cursor); changed(); } } break;
            case K::LEFT:                  // LENGTH in pattern mode
                if (mode == 1) { cursor = std::max(0, cursor - 1); changed(); }
                else if (isHeld(KP::RECORD)) armLength();
                break;
            case K::RIGHT: if (mode == 1) { cursor = std::min((int) mem.songs[(size_t) song].steps.size(), cursor + 1); changed(); } break;
            default: break;
        }
    }
    void drum(int k, bool down) {
        if (k == PERC_KEY) { if (down) { perc = !perc; changed(); } return; }
        const int s = DRUM_KEYS[k][perc ? 1 : 0];
        if (!down) { eraseHeld &= ~(1 << s); e.release(soundCh(s)); return; }
        if (mode == 0 && isHeld(KP::ERASE)) {
            if (recording) { eraseHeld |= 1 << s; return; }
            auto& pt = mem.pats[(size_t) pat]; const size_t n = pt.ev.size();
            pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [s](const Ev& x) { return x.s == s; }), pt.ev.end());
            if (pt.ev.size() != n) { e.beep(); changed(); }
            return;
        }
        inst(s);
    }
    void inst(int s) {
        sel = soundCh(s);
        e.trigger(s);
        if (onHit) onHit(s);
        if (trig == 6 && s == 21 && onTrig) onTrig();
        if (running && recording && mode == 0) record(s);
    }
    void record(int s) {
        auto& pt = mem.pats[(size_t) ppat]; const int len = pt.len; const double spt_ = spt();
        if (usedNotes(mem) >= MEM_CAP || (int) pt.ev.size() >= MAX_EV) { flash("Fu", "LL", 900); e.beep(); return; }
        const double f = tk - countdown / spt_; const int q = EC_GRID[pt.ec], base = pt.ec <= 1 ? 96 : 48;
        int tq = (int) (std::floor(f / q + 0.5) * q);
        if (q > 1 && pt.bef > 0 && tq % (base * 2) == base) tq += (base == 96 ? SHUF8 : SHUF16)[pt.bef];
        while (tq >= len) tq -= len; if (tq < 0) tq += len;
        pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [tq, s](const Ev& x) { return x.t == tq && x.s == s; }), pt.ev.end());
        Ev ev; ev.t = tq; ev.s = s;
        const double ahead = std::fmod(std::fmod(tq - f, (double) len) + len, (double) len);
        if (ahead > 0 && ahead <= q + 20) ev.skip = true;
        pt.ev.push_back(ev); std::sort(pt.ev.begin(), pt.ev.end(), evLess);
        changed();
    }
    void edit(int p, int t, int s, bool on) {
        if (p < 0 || p >= NPAT) return; auto& pt = mem.pats[(size_t) p]; if (t < 0 || t >= pt.len || s < 0 || s > 22) return;
        pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [t, s](const Ev& x) { return x.t == t && x.s == s; }), pt.ev.end());
        if (on && (int) pt.ev.size() < MAX_EV) { Ev ev; ev.t = t; ev.s = s; pt.ev.push_back(ev); std::sort(pt.ev.begin(), pt.ev.end(), evLess); }
        changed();
    }
    void setLen(int p, int beats) {
        if (p < 0 || p >= NPAT) return; auto& pt = mem.pats[(size_t) p]; pt.len = std::max(1, std::min(99, beats)) * PPQ;
        pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [&](const Ev& x) { return x.t >= pt.len; }), pt.ev.end());
        if (running && tk >= curPat().len) tk = 0; changed();
    }
    void digit(int d) {
        const int m = mode;
        if (isHeld(K::BPM)) { trig = d - 1; changed(); return; }
        if (m == 0) {
            auto& pt = mem.pats[(size_t) pat];
            if (isHeld(KP::ERRCOR)) { pt.ec = d - 1; changed(); return; }
            if (isHeld(KP::BEFORE)) { if (d <= 6) { pt.bef = d - 1; changed(); } return; }
            if (isHeld(KP::AFTER)) { if (d <= 6) { (running ? mem.pats[(size_t) ppat] : pt).tim = d - 1; changed(); } return; }
        } else if (!isHeld(K::SONGNUM) && !isHeld(K::ENTER) && !isHeld(K::INSERT)) return;
        entry += (char) ('0' + d);
        if (entry.size() < 2) return;
        const int n = pidx(std::stoi(entry)); entry.clear();
        if (m == 0) {
            if (isHeld(KP::ERASE)) { mem.pats[(size_t) n] = Pattern(); e.beep(); flash("  ", std::to_string(pnum(n)), 700); if (running && ppat == n) tk = 0; changed(); return; }
            if (isHeld(KP::COPY)) { copy(pat, n); return; }
            selectPattern(n);
        } else {
            auto& s = mem.songs[(size_t) song];
            if (isHeld(K::SONGNUM)) { if (running) stop(); song = n; cursor = 0; }
            else if (isHeld(K::ENTER)) { if (cursor < (int) s.steps.size()) s.steps[(size_t) cursor] = n; else if (s.steps.size() < 99) s.steps.push_back(n); else { flash("Fu", "LL", 700); e.beep(); return; } cursor = std::min((int) s.steps.size(), cursor + 1); }
            else if (isHeld(K::INSERT)) { if (s.steps.size() < 99) { s.steps.insert(s.steps.begin() + cursor, n); cursor++; } else { flash("Fu", "LL", 700); e.beep(); return; } }
            changed();
        }
    }
    void selectPattern(int n) { if (running && mode == 0) next = n; else { pat = n; ppat = n; } pat = n; changed(); }
    void copy(int src, int dst) {
        Pattern& a = mem.pats[(size_t) src];
        if (dst == src) {
            if (a.len * 2 > 99 * PPQ || (int) a.ev.size() * 2 > MAX_EV) { err(); return; }
            const size_t n = a.ev.size(); for (size_t i = 0; i < n; ++i) { Ev x; x.t = a.ev[i].t + a.len; x.s = a.ev[i].s; a.ev.push_back(x); } a.len *= 2;
        } else if (mem.pats[(size_t) dst].ev.empty()) { Pattern c = a; for (auto& x : c.ev) x.skip = false; mem.pats[(size_t) dst] = c; }
        else {
            Pattern& b = mem.pats[(size_t) dst];
            if (a.len + b.len > 99 * PPQ || (int) (a.ev.size() + b.ev.size()) > MAX_EV) { err(); return; }
            for (const auto& y : a.ev) { Ev x; x.t = y.t + b.len; x.s = y.s; b.ev.push_back(x); } b.len += a.len;
        }
        if (usedNotes(mem) > MEM_CAP) { err(); return; }
        e.beep(); flash("  ", std::to_string(pnum(dst)), 700); changed();
    }
    void err() { flash("Er", "r ", 700); e.beep(); }
    void armLength() {
        if (!running) { ppat = pat; running = true; tk = 0; countdown = 0; clockAcc = 0; recording = false; if (onStart) onStart(); }
        lenArm = 0; changed();
    }
    void play() {
        if (lenArm >= 0) {
            const int beats = std::max(1, std::min(99, (int) jsRound((double) lenArm / PPQ)));
            auto& pt = mem.pats[(size_t) ppat]; pt.len = beats * PPQ;
            pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [&](const Ev& x) { return x.t >= pt.len; }), pt.ev.end());
            lenArm = -1; tk = 0; countdown = 0; std::string b = std::to_string(beats); if (b.size() < 2) b = " " + b; flash("  ", b, 900); changed(); return;
        }
        if (running) { stop(); return; }
        if (mode == 0) {
            const bool rec = isHeld(KP::RECORD);
            if (!rec && mem.pats[(size_t) pat].ev.empty()) { e.beep(); flash("  ", "--", 500); return; }
            start(rec, 0);
        } else {
            const auto& s = mem.songs[(size_t) song];
            if (s.steps.empty()) { e.beep(); flash("--", "--", 500); return; }
            start(false, isHeld(K::INSERT) ? std::min(cursor, (int) s.steps.size() - 1) : 0);
        }
    }
    double spt() const { return sr * 60 / bpm / PPQ; }
    void start(bool rec, int step) {
        running = true; tk = 0; countdown = 0; clockAcc = 0; lenArm = -1; eraseHeld = 0;
        for (auto& pt : mem.pats) for (auto& x : pt.ev) x.skip = false;
        if (mode == 1) { sstep = step; ppat = mem.songs[(size_t) song].steps[(size_t) sstep]; recording = false; }
        else { ppat = pat; next = -1; recording = rec; }
        if (onStart) onStart();
        changed();
    }
    void stop() { running = false; recording = false; lenArm = -1; next = -1; if (mode == 0) pat = ppat; if (onStop) onStop(); changed(); }
    void locate(double abs) {
        if (!running) return;
        if (mode == 0) { const int len = mem.pats[(size_t) ppat].len; const long long a = (long long) std::floor(abs); tk = (int) (((a % len) + len) % len); }
        else {
            const auto& s = mem.songs[(size_t) song]; long long a = std::max(0LL, (long long) std::floor(abs)); size_t i = 0;
            long long total = 0; for (int p : s.steps) total += mem.pats[(size_t) p].len;
            if (mem.repeat && total > 0) a %= total;
            while (i < s.steps.size()) { const int len = mem.pats[(size_t) s.steps[i]].len; if (a < len) break; a -= len; i++; }
            if (i >= s.steps.size()) { stop(); return; }
            sstep = (int) i; ppat = s.steps[i]; tk = (int) a;
        }
        countdown = 0;
    }
    double position() const {
        if (mode == 0) return tk;
        double a = 0; const auto& s = mem.songs[(size_t) song]; for (int i = 0; i < sstep && i < (int) s.steps.size(); ++i) a += mem.pats[(size_t) s.steps[(size_t) i]].len; return a + tk;
    }
    void tick() {
        auto& pt = mem.pats[(size_t) ppat]; const int len = pt.len, t = tk;
        const int base = pt.ec <= 1 ? 96 : 48, sw = (base == 96 ? SHUF8 : SHUF16)[pt.tim];
        bool removed = false;
        for (auto& x : pt.ev) {
            const int et = sw && x.t % (base * 2) == base ? x.t + sw : x.t;
            if (et != t) continue;
            if (recording && ((eraseHeld >> x.s) & 1)) { x.del = true; removed = true; continue; }
            if (x.skip) { x.skip = false; continue; }
            e.trigger(x.s);
            if (onHit) onHit(x.s);
            if (trig == 6 && x.s == 21 && onTrig) onTrig();
        }
        if (removed) { pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [](const Ev& x) { return x.del; }), pt.ev.end()); changed(); }
        if ((recording || metroAlways || lenArm >= 0) && t % PPQ == 0) e.click(t == 0 || (lenArm >= 0 && lenArm % (8 * PPQ) == 0));
        if (trig < 6 && t % TRIG_DIV[trig] == 0 && onTrig) onTrig();
        tk++;
        if (lenArm >= 0) { lenArm++; if (lenArm > 99 * PPQ) { lenArm = -1; err(); } return; }
        if (tk >= len) {
            tk = 0;
            if (mode == 1) {
                const auto& s = mem.songs[(size_t) song];
                sstep++;
                if (sstep >= (int) s.steps.size()) { if (mem.repeat && !s.steps.empty()) sstep = 0; else { stop(); return; } }
                ppat = s.steps[(size_t) sstep];
            } else if (next >= 0) { ppat = next; next = -1; }
        }
    }
    void clockTick() { if (running && ext) for (int i = 0; i < 8 && running; ++i) tick(); }
    // outs: 16 channels or nullptr (the pointers are advanced as the block is filled)
    void render(float* L, float* R, int n, float* const* outs) {
        int pos = 0; float* o[16];
        while (pos < n) {
            if (running && !ext && countdown <= 0) { tick(); countdown += spt(); continue; }
            int todo = n - pos;
            if (running && !ext) todo = std::max(1, std::min(todo, (int) std::ceil(countdown)));
            if (outs) for (int k = 0; k < 16; ++k) o[k] = outs[k] + pos;
            e.process(L + pos, R + pos, todo, outs ? o : nullptr);
            if (running && !ext) { countdown -= todo; clockOut(todo); }
            pos += todo; now += todo;
        }
    }
    void clockOut(int n) {
        if (!onClock) return;
        const double per = spt() * 8; clockAcc += n;
        while (clockAcc >= per) { clockAcc -= per; onClock(); }
    }
    Display display() {
        auto two = [](int n) { n = std::max(0, std::min(99, n)); std::string s = std::to_string(n); return s.size() < 2 ? "0" + s : s; };
        Display d; std::string l = "  ", r = "  ";
        Pattern& pt = curPat();
        if (isHeld(K::BPM)) { const std::string b = std::to_string((int) jsRound(bpm)); if (b.size() > 2) { l = std::string(" ") + b[0]; r = b.substr(1); } else r = (b.size() < 2 ? " " : "") + b; }
        else if (mode == 0) {
            r = !entry.empty() ? entry + "-" : std::to_string(pnum(running ? ppat : pat));
            if (isHeld(KP::RECORD) && !running) l = two((int) std::floor(99 * (1 - (double) usedNotes(mem) / MEM_CAP)));
            else if (lenArm >= 0) l = two(lenArm / PPQ + 1);
            else if (running) l = two(tk / PPQ + 1);
        } else {
            const auto& s = mem.songs[(size_t) song];
            if (isHeld(K::SONGNUM)) { l = "  "; r = !entry.empty() ? entry + "-" : std::to_string(pnum(song)); }
            else {
                const int st = running ? sstep : cursor;
                l = two(st + 1);
                r = !entry.empty() ? entry + "-" : running ? std::to_string(pnum(ppat)) : st < (int) s.steps.size() ? std::to_string(pnum(s.steps[(size_t) st])) : "En";
            }
        }
        if (now < flashUntil) { l = flashL; r = flashR; }
        d.l = l; d.r = r; d.song = mode == 1;
        d.tim = mode == 0 && isHeld(KP::BEFORE) ? pt.bef : pt.tim;
        d.ec = isHeld(K::BPM) ? trig : pt.ec;
        d.beat = running ? tk / PPQ : -1;
        d.play = running && (tk % PPQ) < PPQ / 2;
        d.rec = recording; d.ext = ext; d.perc = perc; d.sel = sel; d.repeat = mem.repeat;
        return d;
    }
};

} // namespace linn
