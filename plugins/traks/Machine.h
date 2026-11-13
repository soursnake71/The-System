// Omega Traks firmware: line-for-line C++ port of traks/machine.js (the front panel, the pattern and song memory and
// the sequencer). Runs on the audio thread; the editor queues key presses and reads display().
#pragma once
#include "Traks.h"
#include <functional>
#include <string>

namespace traks {

constexpr int PPQ = 48;
constexpr int MAX_EV = 4000;
namespace K { enum { SAVE = 0, LOAD, SLOWER, FASTER, SONGFN, ENTER, D0, LEFT = 16, RIGHT, PATFN, RUN, ACCENT, INST }; }
inline const char* const PAT_FNS[9] = {"PATTERN#", "RECORD MODE", "ERASE INSTRUMENT", "ERROR CORRECT", "SWING%", "TIME SIGNATURE", "# OF MEASURES", "ERASE PATTERN", "COPY / APPEND TO"};
inline const char* const SONG_FNS[9] = {"SONG#", "ERASE SONG", "COPY / APPEND TO", "BUILD MODE", "PATTERN#", "VOL CHANGE", "TEMPO CHANGE", "INSERT", "DELETE"};
constexpr int QUANTS[9] = {0, 4, 8, 12, 16, 24, 32, 48, 64};
constexpr int SWINGS[7] = {50, 54, 58, 63, 67, 71, 75};
constexpr int DENS[4] = {2, 4, 8, 16};

struct Ev { int t = 0, v = 0, a = 0; double vel = 1, lv = -1, tu = -99; int skip = 0, del = 0; };
struct Pattern { int num = 4, den = 4, meas = 1, swing = 50; std::vector<Ev> ev; };
struct Step { int p = 0, vol = -1, tempo = -1; };
struct Song { std::vector<Step> steps; };
struct Memory { std::vector<Pattern> pats; std::vector<Song> songs; };
inline int patLen(const Pattern& p) { return p.meas * p.num * (PPQ * 4 / p.den); }
inline int beatLen(const Pattern& p) { return PPQ * 4 / p.den; }
inline bool evLess(const Ev& a, const Ev& b) { return a.t != b.t ? a.t < b.t : a.v < b.v; }

struct Factory { const char* name; int swing; std::vector<std::pair<const char*, const char*>> rows; };
inline const std::vector<Factory>& factory() {
    static const std::vector<Factory> f = {
        {"ROCK", 50, {{"BS", "x.......x.x....."}, {"SN", "....x.......x..."}, {"CH", "x.x.x.x.x.x.x.x."}, {"OH", "..............A."}}},
        {"FUNK", 54, {{"BS", "x..x..x...x..x.."}, {"SN", "....A..x.x..A..."}, {"CH", "xxxxxxxxxxxxxx.x"}, {"OH", "..............x."}, {"RM", ".......x........"}}},
        {"ELECTRO", 50, {{"BS", "x.....x...x....."}, {"SN", "....A.......A..."}, {"CP", "....x.......x..."}, {"CH", "x.x.x.x.x.x.x.x."}, {"CB", "..x.....x...x..."}}},
        {"HIP HOP", 58, {{"BS", "x......x..x....."}, {"SN", "....A.......A..x"}, {"CH", "x.x.x.x.x.x.x.x."}, {"TB", "....x.......x..."}}},
        {"DISCO", 50, {{"BS", "x...x...x...x..."}, {"SN", "....x.......x..."}, {"CH", "x.x.x.x.x.x.x.x."}, {"OH", "..x...x...x...x."}, {"CP", "....x.......x..."}}},
        {"NEW WAVE", 50, {{"BS", "x...x...x...x..."}, {"SN", "....A.......A..."}, {"CH", "xxxxxxxxxxxxxxxx"}, {"T1", "..............xx"}}},
        {"SHUFFLE", 67, {{"BS", "x.....x.x......."}, {"SN", "....x.......x..."}, {"RD", "x.x.x.x.x.x.x.x."}, {"CH", "....x.......x..."}}},
        {"LATIN", 50, {{"BS", "x..x....x..x...."}, {"RM", "..x..x....x..x.."}, {"CB", "x...x.x...x.x..."}, {"CA", "x.xxx.xxx.xxx.xx"}, {"T2", "......x.......x."}}},
        {"REGGAE", 54, {{"BS", "........x......."}, {"RM", "........x......."}, {"CH", "..x...x...x...x."}, {"CA", "x.x.x.x.x.x.x.x."}}},
        {"BALLAD", 50, {{"BS", "x.........x....."}, {"SN", "........A......."}, {"RD", "x...x...x...x..."}, {"RM", "....x.......x..."}}},
        {"POP", 50, {{"BS", "x.......x.x....."}, {"SN", "....x.......x..."}, {"CP", "....x.......x..."}, {"CH", "x.x.x.x.x.x.x.x."}, {"TB", "x.x.x.x.x.x.x.x."}}},
        {"BOOGIE", 63, {{"BS", "x...x...x...x..."}, {"SN", "....x.......x..."}, {"CH", "x.xxx.xxx.xxx.xx"}, {"CB", "x..............."}}},
        {"MOTOWN", 54, {{"BS", "x..x....x..x...."}, {"SN", "....A.......A..."}, {"TB", "....x.......x..."}, {"CH", "x.x.x.x.x.x.x.x."}}},
        {"TOM FILL", 50, {{"SN", "x.xx............"}, {"T1", "....x.xx........"}, {"T2", "........x.xx.x.x"}, {"CR", "................"}, {"BS", "x..............."}}},
        {"CRASH IN", 50, {{"BS", "x.......x.x....."}, {"SN", "....x.......x..."}, {"CR", "A..............."}, {"CH", "..x.x.x.x.x.x.x."}}},
        {"BREAKDOWN", 50, {{"BS", "x..x...........x"}, {"CB", "x..x..x...x.x..."}, {"CP", "....x.......x..."}, {"CA", "..x...x...x...x."}}},
    };
    return f;
}
inline Pattern factoryPattern(int i) {
    const auto& f = factory()[(size_t) i]; Pattern p; p.swing = f.swing;
    for (const auto& row : f.rows) {
        int v = 0; for (int k = 0; k < NV; ++k) if (std::string(VOICES[k]) == row.first) v = k;
        const std::string s = row.second;
        for (int j = 0; j < 16 && j < (int) s.size(); ++j) if (s[(size_t) j] != '.') { Ev e; e.t = j * 12; e.v = v; e.a = s[(size_t) j] == 'A' ? 1 : 0; p.ev.push_back(e); }
    }
    std::sort(p.ev.begin(), p.ev.end(), evLess);
    return p;
}
inline Memory defaultMemory() {
    Memory m; m.pats.resize(100); m.songs.resize(100);
    for (int i = 0; i < (int) factory().size(); ++i) m.pats[(size_t) i] = factoryPattern(i);
    for (int p : {14, 0, 0, 1, 1, 13, 2, 2, 3, 3, 15, 14}) { Step s; s.p = p; m.songs[0].steps.push_back(s); }
    return m;
}

struct Display { std::string l = "  ", r = "  "; int lp = 0, ls = 0; bool run = false, rec = false; int beat = -1, sel = 0; };

class Machine {
public:
    Machine(Engine& eng, double sampleRate) : e(eng), sr(sampleRate) { mem = defaultMemory(); }
    Engine& e; double sr; Memory mem;
    int group = 0, patFn = 0, songFn = 0, pat = 0, song = 0, bstep = 0, quant = 16;
    std::string entry; int target = -1;
    bool running = false, recording = false; int ppat = 0, next = -1, sstep = 0, tk = 0; double countdown = 0; int pmeas = 0;
    double bpm = 120, songBpm = 0; bool ext = false;
    int held = 0; bool accHeld = false; int sel = 0, touched = 0;
    long long now = 0; std::string flashL, flashR; long long flashUntil = 0;
    bool metroAlways = false; int version = 0; double clockAcc = 0;
    std::function<void(int, bool, double)> onHit; std::function<void()> onClock, onStop, onStart;

    void changed() { version++; }
    Pattern& curPat() { return mem.pats[(size_t) (running && group == 0 ? ppat : pat)]; }
    void flash(const std::string& l, const std::string& r, double ms) { flashL = l; flashR = r; flashUntil = now + (long long) jsRound(ms * sr / 1000); }
    bool recActive() const { return group == 0 && patFn >= 1 && patFn <= 3; }

    void key(int id, bool down) {
        if (id >= K::INST && id < K::INST + NV) { const int v = id - K::INST; if (down) inst(v, 1); else { held &= ~(1 << v); e.release(v); } return; }
        if (id == K::ACCENT) { accHeld = down; return; }
        if (!down) return;
        if (id >= K::D0 && id <= K::D0 + 9) { digit(id - K::D0); return; }
        switch (id) {
            case K::PATFN: if (group != 0) { group = 0; patFn = 0; } else patFn = (patFn + 1) % 9; entry.clear(); target = -1; recording = running && recActive(); break;
            case K::SONGFN: if (group != 1) { group = 1; songFn = 0; if (running) stop(); } else songFn = (songFn + 1) % 9; entry.clear(); target = -1; recording = false; break;
            case K::RUN: if (running) stop(); else start(); break;
            case K::LEFT: arrow(-1); break;
            case K::RIGHT: arrow(1); break;
            case K::ENTER: enter(); break;
            default: break;
        }
    }
    void inst(int v, double vel) {
        const bool acc = accHeld;
        held |= 1 << v; sel = v;
        e.trigger(v, acc, vel);
        if (onHit) onHit(v, acc, vel);
        if (running && recording && group == 0 && patFn != 2) record(v, acc, vel);
    }
    void record(int v, bool acc, double vel) {
        Pattern& pt = mem.pats[(size_t) ppat]; const int len = patLen(pt); const double spt_ = spt();
        const double f = tk - countdown / spt_;
        const int q = quant > 0 ? PPQ * 4 / quant : 1;
        int tq = (int) std::floor(f / q + 0.5) * q; while (tq >= len) tq -= len; if (tq < 0) tq += len;
        pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [&](const Ev& x) { return x.t == tq && x.v == v; }), pt.ev.end());
        if ((int) pt.ev.size() >= MAX_EV) return;
        const int dyn = (touched >> v) & 1;
        Ev ev; ev.t = tq; ev.v = v; ev.a = acc ? 1 : 0; ev.vel = vel; ev.lv = dyn ? e.p[(size_t) vp(v, vf::lv)] : -1; ev.tu = dyn ? e.p[(size_t) vp(v, vf::tune)] : -99;
        const double ahead = std::fmod(std::fmod(tq - f, (double) len) + len, (double) len);
        if (ahead > 0 && ahead <= q) ev.skip = 1;
        pt.ev.push_back(ev); std::sort(pt.ev.begin(), pt.ev.end(), evLess);
        changed();
    }
    void edit(int p, int t, int v, bool on, bool acc) {
        if (p < 0 || p >= (int) mem.pats.size()) return;
        Pattern& pt = mem.pats[(size_t) p]; if (t < 0 || t >= patLen(pt)) return;
        pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [&](const Ev& x) { return x.t == t && x.v == v; }), pt.ev.end());
        if (on && (int) pt.ev.size() < MAX_EV) { Ev e; e.t = t; e.v = v; e.a = acc ? 1 : 0; pt.ev.push_back(e); std::sort(pt.ev.begin(), pt.ev.end(), evLess); }
        changed();
    }
    void knobMoved() { if (recording) touched |= 1 << sel; }
    void digit(int d) {
        const int fk = group == 0 ? patFn : 10 + songFn;
        const size_t width = fk == 16 ? 3 : 2;
        entry += (char) ('0' + d); if (entry.size() > width) entry = entry.substr(entry.size() - width);
        if (entry.size() < width) return;
        const int n = std::stoi(entry);
        auto err = [&] { flash("Er", "r ", 600); };
        switch (fk) {
            case 0: case 1: case 2: selectPattern(n); entry.clear(); break;
            case 3: { bool ok = false; for (int qv : QUANTS) if (qv == n) ok = true; if (ok) { quant = n; changed(); } else err(); entry.clear(); break; }
            case 4: if (n >= 50 && n <= 75) { curPat().swing = n; changed(); } else err(); entry.clear(); break;
            case 5: if (n >= 1 && n <= 16) setSig(n, curPat().den); else err(); entry.clear(); break;
            case 6: if (n >= 1 && n <= 99) setMeas(n); else err(); entry.clear(); break;
            case 10: song = n; bstep = 0; entry.clear(); changed(); break;
            default: break;
        }
    }
    void selectPattern(int n) { if (running && group == 0) next = n; else { pat = n; ppat = n; } pat = n; changed(); }
    void trim(Pattern& pt) { const int len = patLen(pt); pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [&](const Ev& x) { return x.t >= len; }), pt.ev.end()); if (running && tk >= len) tk = 0; changed(); }
    void setSig(int num, int den) { Pattern& pt = curPat(); pt.num = num; pt.den = den; trim(pt); }
    void setMeas(int m) { Pattern& pt = curPat(); pt.meas = m; trim(pt); }
    static int idxOf(const int* a, int n, int v) { for (int i = 0; i < n; ++i) if (a[i] == v) return i; return -1; }
    void arrow(int d) {
        entry.clear();
        auto step = [&](int v, int lo, int hi) { return std::max(lo, std::min(hi, v + d)); };
        if (group == 0) {
            Pattern& pt = curPat();
            switch (patFn) {
                case 0: case 1: case 2: selectPattern((pat + d + 100) % 100); break;
                case 3: { const int i = idxOf(QUANTS, 9, quant); quant = QUANTS[step(i < 0 ? 4 : i, 0, 8)]; break; }
                case 4: { int i = 0; while (i < 6 && SWINGS[i] < pt.swing) i++; if (SWINGS[i] != pt.swing && d < 0) i++; pt.swing = SWINGS[step(i, 0, 6)]; break; }
                case 5: { const int i = idxOf(DENS, 4, pt.den); setSig(pt.num, DENS[step(i < 0 ? 1 : i, 0, 3)]); break; }
                case 6: setMeas(step(pt.meas, 1, 99)); break;
                case 7: case 8: target = (tgt() + d + 100) % 100; break;
                default: break;
            }
        } else {
            const Song& s = mem.songs[(size_t) song];
            if (songFn == 0) { song = (song + d + 100) % 100; bstep = 0; }
            else if (songFn <= 2) target = (tgt() + d + 100) % 100;
            else bstep = step(bstep, 0, (int) s.steps.size());
        }
        changed();
    }
    int tgt() const { return target >= 0 ? target : group == 0 ? pat : song; }
    static std::string two(int n) { n = std::max(0, std::min(99, n)); std::string s = std::to_string(n); return s.size() < 2 ? "0" + s : s; }
    void enter() {
        const int n = entry.empty() ? -1 : std::stoi(entry);
        entry.clear();
        if (group == 0) {
            if (patFn == 7) { const int t = n >= 0 ? n : tgt(); mem.pats[(size_t) t] = Pattern(); target = -1; flash("EP", two(t), 900); changed(); }
            else if (patFn == 8) {
                const int t = n >= 0 ? n : tgt();
                if (t == pat) { flash("Er", "r ", 600); return; }
                Pattern src = mem.pats[(size_t) pat]; Pattern& dst = mem.pats[(size_t) t];
                if (dst.ev.empty()) dst = src;
                else if (dst.meas + src.meas <= 99) {
                    const int off = patLen(dst); dst.meas += src.meas;
                    const double sc = (double) (PPQ * 4 / src.den) * src.num / ((double) (PPQ * 4 / dst.den) * dst.num);
                    for (const Ev& x : src.ev) { const int t2 = off + (int) std::floor(x.t / sc); if (t2 < patLen(dst) && (int) dst.ev.size() < MAX_EV) { Ev y = x; y.t = t2; dst.ev.push_back(y); } }
                    std::sort(dst.ev.begin(), dst.ev.end(), evLess);
                } else { flash("Er", "r ", 600); return; }
                target = -1; flash("CP", two(t), 900); changed();
            }
            else if (n >= 0 && patFn <= 2) selectPattern(n);
            return;
        }
        Song& s = mem.songs[(size_t) song];
        const int ns = (int) s.steps.size();
        switch (songFn) {
            case 0: if (n >= 0) { song = n; bstep = 0; } break;
            case 1: { const int t = n >= 0 ? n : tgt(); mem.songs[(size_t) t] = Song(); target = -1; flash("ES", two(t), 900); if (t == song) bstep = 0; break; }
            case 2: { const int t = n >= 0 ? n : tgt(); if (t == song) { flash("Er", "r ", 600); return; }
                Song& d = mem.songs[(size_t) t]; const auto add = s.steps; for (const auto& x : add) if (d.steps.size() < 255) d.steps.push_back(x);
                target = -1; flash("CP", two(t), 900); break; }
            case 3: case 4: { const int p = n >= 0 ? std::min(99, n) : pat;
                if (bstep < ns) s.steps[(size_t) bstep].p = p; else if (ns < 255) { Step st; st.p = p; s.steps.push_back(st); } else { flash("Fu", "LL", 600); return; }
                bstep = std::min((int) s.steps.size(), bstep + 1); break; }
            case 5: if (bstep < ns) s.steps[(size_t) bstep].vol = n >= 0 ? std::min(99, n) : -1; break;
            case 6: if (bstep < ns) s.steps[(size_t) bstep].tempo = n >= 0 ? std::max(40, std::min(250, n)) : -1; break;
            case 7: if (ns < 255) { Step st; st.p = n >= 0 ? std::min(99, n) : (bstep < ns ? s.steps[(size_t) bstep].p : pat); s.steps.insert(s.steps.begin() + bstep, st); } break;
            case 8: if (bstep < ns) s.steps.erase(s.steps.begin() + bstep); break;
            default: break;
        }
        changed();
    }
    double spt() const { const double b = songBpm > 0 ? songBpm : bpm; return sr * 60 / b / PPQ; }
    void start() {
        running = true; tk = 0; countdown = 0; pmeas = 0; clockAcc = 0; touched = 0; songBpm = 0; e.songGain = 1;
        for (auto& pt : mem.pats) for (auto& x : pt.ev) x.skip = 0;
        if (group == 1) {
            if (mem.songs[(size_t) song].steps.empty()) { running = false; flash("--", "--", 700); return; }
            sstep = 0; enterStep();
        } else { ppat = pat; next = -1; }
        recording = recActive();
        if (onStart) onStart();
    }
    void stop() { running = false; recording = false; songBpm = 0; e.songGain = 1; next = -1; if (onStop) onStop(); }
    void enterStep() {
        const Step& st = mem.songs[(size_t) song].steps[(size_t) sstep];
        ppat = st.p;
        if (st.vol >= 0) e.songGain = st.vol / 99.0;
        if (st.tempo > 0) songBpm = st.tempo;
    }
    void locate(double abs) {
        if (!running) return;
        if (group == 0) { const int len = patLen(mem.pats[(size_t) ppat]); tk = (((int) std::floor(abs) % len) + len) % len; pmeas = 0; }
        else {
            const Song& s = mem.songs[(size_t) song]; long a = std::max(0L, (long) std::floor(abs)); size_t i = 0;
            while (i < s.steps.size()) { const long len = patLen(mem.pats[(size_t) s.steps[i].p]); if (a < len) break; a -= len; i++; }
            if (i >= s.steps.size()) { stop(); return; }
            sstep = (int) i; enterStep(); tk = (int) a;
        }
        countdown = 0;
    }
    // absolute position (ticks from the start) of what's playing, for following the DAW
    double position() const {
        double a = tk - countdown / spt();
        if (group == 1) for (int i = 0; i < sstep && i < (int) mem.songs[(size_t) song].steps.size(); ++i) a += patLen(mem.pats[(size_t) mem.songs[(size_t) song].steps[(size_t) i].p]);
        return a;
    }
    void tick() {
        Pattern& pt = mem.pats[(size_t) ppat];
        const int len = patLen(pt), sw = pt.swing > 50 ? (int) std::floor((pt.swing - 50) / 50.0 * 12 + 0.5) : 0;
        const int t = tk;
        const bool erase = recording && patFn == 2 && held;
        bool removed = false;
        for (auto& x : pt.ev) {
            const int et = sw && x.t % 24 == 12 ? x.t + sw : x.t;
            if (et != t) continue;
            if (erase && ((held >> x.v) & 1)) { x.del = 1; removed = true; continue; }
            if (x.skip) { x.skip = 0; continue; }
            e.trigger(x.v, x.a != 0, x.vel, x.lv, x.tu);
            if (onHit) onHit(x.v, x.a != 0, x.vel);
        }
        if (removed) { pt.ev.erase(std::remove_if(pt.ev.begin(), pt.ev.end(), [](const Ev& x) { return x.del != 0; }), pt.ev.end()); changed(); }
        const int bl = beatLen(pt);
        if ((recording || metroAlways) && t % bl == 0) e.click(t % (bl * pt.num) == 0);
        tk++;
        if (tk >= len) {
            tk = 0; pmeas = 0;
            if (group == 1) {
                sstep++;
                if (sstep >= (int) mem.songs[(size_t) song].steps.size()) { stop(); return; }
                enterStep();
            } else if (next >= 0) { ppat = next; next = -1; }
        }
    }
    void clockTick() { if (running && ext) { tick(); if (running) tick(); } }
    void render(float* L, float* R, int n, float* const* outs) {
        int pos = 0;
        while (pos < n) {
            if (running && !ext && countdown <= 0) { tick(); countdown += spt(); continue; }
            int todo = n - pos;
            if (running && !ext) todo = std::max(1, std::min(todo, (int) std::ceil(countdown)));
            if (outs) { float* o[12]; for (int k = 0; k < 12; ++k) o[k] = outs[k] + pos; e.process(L + pos, R + pos, todo, o); }
            else e.process(L + pos, R + pos, todo, nullptr);
            if (running && !ext) { countdown -= todo; clockOut(todo); }
            pos += todo; now += todo;
        }
    }
    void clockOut(int n) {
        if (!onClock) return;
        const double per = spt() * 2; clockAcc += n;
        while (clockAcc >= per) { clockAcc -= per; onClock(); }
    }
    Display display() {
        Display d;
        auto ent = [&](size_t w) { std::string s = entry; while (s.size() < w) s = "-" + s; return s; };
        Pattern& pt = curPat();
        const int meas = running ? tk / (beatLen(pt) * pt.num) + 1 : 0;
        std::string l = "  ", r = "  ";
        if (group == 0) {
            switch (patFn) {
                case 0: case 1: case 2: l = running ? two(meas) : "  "; r = !entry.empty() ? ent(2) : two(running ? ppat : pat); break;
                case 3: l = "EC"; r = !entry.empty() ? ent(2) : (quant ? two(quant) : "oF"); break;
                case 4: l = "Sh"; r = !entry.empty() ? ent(2) : two(pt.swing); break;
                case 5: l = !entry.empty() ? ent(2) : two(pt.num); r = two(pt.den); break;
                case 6: l = "n "; r = !entry.empty() ? ent(2) : two(pt.meas); break;
                case 7: l = "EP"; r = !entry.empty() ? ent(2) : two(tgt()); break;
                case 8: l = "CP"; r = !entry.empty() ? ent(2) : two(tgt()); break;
                default: break;
            }
        } else {
            const Song& s = mem.songs[(size_t) song];
            const bool end = bstep >= (int) s.steps.size();
            const Step st = end ? Step() : s.steps[(size_t) bstep];
            switch (songFn) {
                case 0: l = running ? two(sstep + 1) : "  "; r = !entry.empty() ? ent(2) : two(song); break;
                case 1: l = "ES"; r = !entry.empty() ? ent(2) : two(tgt()); break;
                case 2: l = "CP"; r = !entry.empty() ? ent(2) : two(tgt()); break;
                case 3: case 4: case 7: case 8: l = running ? two(sstep + 1) : end ? "En" : two(bstep + 1); r = !entry.empty() ? ent(2) : running ? two(ppat) : end ? "--" : two(st.p); break;
                case 5: l = end ? "En" : two(bstep + 1); r = !entry.empty() ? ent(2) : end || st.vol < 0 ? "--" : two(st.vol); break;
                case 6: { std::string t; if (!entry.empty()) t = ent(3); else if (end || st.tempo < 0) t = "---"; else { t = std::to_string(st.tempo); while (t.size() < 3) t = " " + t; }
                    l = std::string(" ") + t[0]; r = t.substr(1); break; }
                default: break;
            }
        }
        if (now < flashUntil) { l = flashL; r = flashR; }
        int lp = 0, ls = 0;
        if (group == 0) { lp = 1 << patFn; if (patFn == 2 || patFn == 3) lp |= 2; }
        else { ls = 1 << songFn; if (songFn >= 4) ls |= 8; }
        const int bl = beatLen(pt);
        d.l = l; d.r = r; d.lp = lp; d.ls = ls; d.beat = running ? tk / bl : -1;
        d.run = running && (tk % bl) < bl / 2.0; d.rec = recording; d.sel = sel;
        return d;
    }
};

} // namespace traks
