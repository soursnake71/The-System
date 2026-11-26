// Omega Five sequencer / data model: a C++17 port of the parts of five/seq.js the plugin needs: the memory (patterns,
// songs, chains, key-assign set-ups, note map, MIDI settings, transport settings), editing (real-time / step write,
// clear, copy, time signature, song and chain steps), the sample-accurate Player (same tick -> sample formula, plus a
// host-transport follower), randomize (same xorshift32 draws as seq.js), SMF import / export and the MIDI helpers.
// No JUCE: the JSON / gzip / SysEx parts are in FiveCart.h and FiveSysex.h. The Player never allocates.
#pragma once
#include "Five.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace five {

constexpr int PPQN = 24, NUM_PATTERNS = 100, NUM_SONGS = 20, NUM_CHAINS = 3, CHAIN_MAX = 999, SONG_MAX = 999, NUM_SETUPS = 3;
constexpr int NUM_VOICES = NV, NUM_CHANNELS = NCH, CART_VERSION = 1;

struct QuantizeEntry { const char* name; int ticks; int key; };
static const QuantizeEntry QUANTIZE[11] = { {"1/2", 48, 0}, {"1/4", 24, 1}, {"1/6", 16, 2}, {"1/8", 12, 3}, {"1/12", 8, 4}, {"1/16", 6, 5},
    {"1/24", 4, 6}, {"1/32", 3, 7}, {"1/48", 2, 8}, {"1/96", 1, -1}, {"OFF", 1, 9} };
constexpr int NUM_QUANTIZE = 11, Q_1_16 = 5;
static const int QUANTIZE_KEYPAD[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10 };   // keypad digit -> QUANTIZE index

static const int RX5_NOTES[NV] = { 45, 52, 51, 53, 50, 48, 47, 57, 63, 60, 58, 56, 44, 49, 46, 43, 42, 41, 40, 59, 62, 61, 54, 55 };
static const int MIDI_GM_NOTES[NV] = { 35, 38, 37, 50, 48, 45, 41, 42, 51, 49, 54, 70, 36, 40, 75, 65, 66, 47, 43, 46, 53, 52, 39, 56 };
static const int GM_EXTRA_NOTE[3] = { 44, 57, 82 }, GM_EXTRA_VOICE[3] = { 7, 9, 11 };
constexpr int TUNABLE_LO = 36, TUNABLE_HI = 96, TUNABLE_CENTER = 60;

inline int clampInt(double x, int lo, int hi, int d) { return std::isfinite(x) ? (int) clampd(jsround(x), lo, hi) : d; }
inline std::string pad2(int n) { std::string s = std::to_string(n); return s.size() < 2 ? "0" + s : s; }

/* ---------------- memory ---------------- */
struct Event { int t = 0, v = 0, vel = 100, acc = 0; bool operator==(const Event& o) const { return t == o.t && v == o.v && vel == o.vel && acc == o.acc; } };
struct Pattern {
    std::string name; int beats = 4, unit = 4, bars = 1, swing = 50; std::vector<Event> events; std::array<int, NCH> volume;
    Pattern() { volume.fill(31); }
};
inline Pattern newPattern(int i = 0) { Pattern p; p.name = "PTN " + pad2(i); return p; }
struct SongStep {
    enum Type { Pat, Rep, Tempo, Vol, Mark } type = Pat;
    int n = 0, from = 0, count = 2, ch = 0, level = 31; double bpm = 120;
    static SongStep pat(int n) { SongStep s; s.type = Pat; s.n = n; return s; }
    static SongStep rep(int from, int count) { SongStep s; s.type = Rep; s.from = from; s.count = count; return s; }
    static SongStep tempo(double bpm) { SongStep s; s.type = Tempo; s.bpm = bpm; return s; }
    static SongStep vol(int ch, int level) { SongStep s; s.type = Vol; s.ch = ch; s.level = level; return s; }
    static SongStep mark() { SongStep s; s.type = Mark; return s; }
};
inline SongStep cleanStep(const SongStep& s) {
    SongStep o = s;
    switch (s.type) {
        case SongStep::Pat: o.n = clampInt(s.n, 0, NUM_PATTERNS - 1, 0); break;
        case SongStep::Rep: o.from = clampInt(s.from, 0, 9999, 0); o.count = clampInt(s.count, 1, 99, 2); break;
        case SongStep::Tempo: o.bpm = clampd(s.bpm != 0 && std::isfinite(s.bpm) ? s.bpm : 120, 40, 250); break;
        case SongStep::Vol: o.ch = clampInt(s.ch, 0, 11, 0); o.level = clampInt(s.level, 0, 31, 31); break;
        case SongStep::Mark: break;
    }
    return o;
}
struct Song { std::string name; double tempo = 120; std::vector<SongStep> steps; };
inline Song newSong(int i = 0) { Song s; s.name = std::string("SONG ") + (char) ('A' + i); return s; }
struct Chain { std::string name; std::vector<int> songs; };
inline Chain newChain(int i = 0) { Chain c; c.name = "CHAIN " + std::to_string(i + 1); return c; }
struct SetupKey { int voice = 0, pitch = 0, level = 0, attack = 0, decay = 0, acc1 = 2, acc2 = 4, out = 0; };
struct Setup { std::string name; std::array<SetupKey, NV> keys; };
inline Setup newSetup(int i = 0) { Setup s; s.name = "SETUP " + std::to_string(i + 1); for (int k = 0; k < NV; k++) { s.keys[(size_t) k].voice = k; s.keys[(size_t) k].out = k % NCH; } return s; }
struct MidiSettings {
    int rxCh = 10, txCh = 10; std::array<int, NV> voiceTxCh {}; bool channelMsg = true, clockIn = false, clockOut = false, notesOut = false;
    int deviceNo = 1; bool tunable = false; int tunableVoice = 0, egVel = 0, gateMs = 10; bool activeSense = true, songSelect = true;
};
struct RxBulk { bool present = false; std::string received, status; std::vector<std::string> messages; long long bytes = 0; int count = 0; };
struct UserSample { std::string name; double sr = 44100; std::vector<float> data; };

struct State {
    int version = CART_VERSION;
    std::array<Pattern, NUM_PATTERNS> patterns; std::array<Song, NUM_SONGS> songs; std::array<Chain, NUM_CHAINS> chains;
    std::array<int, NV> noteMap; MidiSettings midi; std::array<Setup, NUM_SETUPS> setups; int setup = 0;
    RxBulk rxBulk; std::string sync = "int"; double tempo = 120; int quantize = Q_1_16; int swing = 50;
    bool clickOn = false; double clickLevel = .5; double volume = 1;
    // engine parameters (seq.js keeps these as opaque engine-state pieces: state.voice[v], state.channel[ch], state.fx)
    std::array<VoiceParams, NV> voice; std::array<ChannelParams, NCH> channel; std::array<double, rCount> rev; std::array<double, dCount> dly;
    std::array<UserSample, NV> samples;   // empty data = the internal voice
    State() {
        for (int i = 0; i < NUM_PATTERNS; i++) patterns[(size_t) i] = newPattern(i);
        for (int i = 0; i < NUM_SONGS; i++) songs[(size_t) i] = newSong(i);
        for (int i = 0; i < NUM_CHAINS; i++) chains[(size_t) i] = newChain(i);
        for (int i = 0; i < NUM_SETUPS; i++) setups[(size_t) i] = newSetup(i);
        std::copy(RX5_NOTES, RX5_NOTES + NV, noteMap.begin());
        for (int v = 0; v < NV; v++) voice[(size_t) v] = voiceDefault(v);
        for (int c = 0; c < NCH; c++) channel[(size_t) c] = channelDefault(c);
        std::copy(REV_DEFAULTS, REV_DEFAULTS + rCount, rev.begin()); std::copy(DLY_DEFAULTS, DLY_DEFAULTS + dCount, dly.begin());
    }
};

inline int ticksPerBeat(const Pattern& p) { return (PPQN * 4) / p.unit; }
inline int barTicks(const Pattern& p) { return p.beats * ticksPerBeat(p); }
inline int patLen(const Pattern& p) { return p.bars * barTicks(p); }
inline void sortEvents(Pattern& p) { std::stable_sort(p.events.begin(), p.events.end(), [](const Event& a, const Event& b) { return a.t != b.t ? a.t < b.t : a.v < b.v; }); }
inline size_t lowerBound(const std::vector<Event>& ev, int t) {
    size_t lo = 0, hi = ev.size();
    while (lo < hi) { const size_t m = (lo + hi) >> 1; if (ev[m].t < t) lo = m + 1; else hi = m; }
    return lo;
}

/* ---------------- editing ---------------- */
inline int quantizeTick(double raw, int qIndex, int swing = 50) {
    const int g = (qIndex >= 0 && qIndex < NUM_QUANTIZE ? QUANTIZE[qIndex] : QUANTIZE[Q_1_16]).ticks;
    if (g <= 1) return (int) jsround(raw);
    const int step = (int) jsround(raw / g);
    int t = step * g;
    if (swing > 50 && (step & 1)) t += (int) jsround(g * (2 * swing / 100.0 - 1));
    return t;
}
inline Event* putEvent(Pattern& p, int t, int v, int vel, int acc) {
    auto& ev = p.events;
    size_t i = lowerBound(ev, t);
    for (size_t j = i; j < ev.size() && ev[j].t == t; j++) if (ev[j].v == v) { ev[j].vel = vel; ev[j].acc = acc; return &ev[j]; }
    while (i < ev.size() && ev[i].t == t && ev[i].v < v) i++;
    Event e; e.t = t; e.v = v; e.vel = vel; e.acc = acc;
    return &*ev.insert(ev.begin() + (std::ptrdiff_t) i, e);
}
inline Event recordHit(State& st, int pn, double rawTick, int v, int vel = 100, int acc = 0, int qIndex = -1) {
    Pattern& p = st.patterns[(size_t) pn]; const int len = patLen(p);
    int t = quantizeTick(rawTick, qIndex < 0 ? st.quantize : qIndex, p.swing) % len;
    if (t < 0) t += len;
    return *putEvent(p, t, v, clampInt(vel, 1, 127, 100), clampInt(acc, 0, 2, 0));
}
inline bool stepInsert(State& st, int pn, int t, int v, int vel = 100, int acc = 0) {
    Pattern& p = st.patterns[(size_t) pn];
    if (t < 0 || t >= patLen(p)) return false;
    putEvent(p, t, v, clampInt(vel, 1, 127, 100), clampInt(acc, 0, 2, 0)); return true;
}
inline int deleteEvents(State& st, int pn, int from, int to, int v = -1) {
    auto& ev = st.patterns[(size_t) pn].events; const size_t n = ev.size();
    ev.erase(std::remove_if(ev.begin(), ev.end(), [&](const Event& e) { return e.t >= from && e.t < to && (v < 0 || e.v == v); }), ev.end());
    return (int) (n - ev.size());
}
inline int stepDelete(State& st, int pn, int t, int v = -1) { return deleteEvents(st, pn, t, t + 1, v); }
inline void clearPattern(State& st, int pn) { Pattern& p = st.patterns[(size_t) pn]; const std::string name = p.name; p = newPattern(pn); p.name = name; p.swing = st.swing; }
inline void copyPattern(State& st, int src, int dst) { if (src != dst) st.patterns[(size_t) dst] = st.patterns[(size_t) src]; }
inline void trimPattern(Pattern& p) { const int len = patLen(p); p.events.erase(std::remove_if(p.events.begin(), p.events.end(), [&](const Event& e) { return e.t >= len; }), p.events.end()); }
inline void setBeats(State& st, int pn, int beats, int unit) { Pattern& p = st.patterns[(size_t) pn]; p.beats = clampInt(beats, 1, 32, 4); p.unit = unit == 4 || unit == 8 || unit == 16 ? unit : 4; trimPattern(p); }
inline void setBars(State& st, int pn, int bars) { Pattern& p = st.patterns[(size_t) pn]; p.bars = clampInt(bars, 1, 99, 1); trimPattern(p); }
inline void setSwing(State& st, int pn, int swing) { st.patterns[(size_t) pn].swing = clampInt(swing, 50, 75, 50); }
inline bool songInsertStep(State& st, int sn, int idx, const SongStep& step) {
    auto& steps = st.songs[(size_t) sn].steps;
    if ((int) steps.size() >= SONG_MAX) return false;
    idx = (int) clampd(idx, 0, (double) steps.size());
    for (size_t i = (size_t) idx; i < steps.size(); i++) if (steps[i].type == SongStep::Rep && steps[i].from >= idx) steps[i].from++;
    steps.insert(steps.begin() + idx, cleanStep(step));
    return true;
}
inline void songDeleteStep(State& st, int sn, int idx) {
    auto& steps = st.songs[(size_t) sn].steps;
    if (idx < 0 || idx >= (int) steps.size()) return;
    steps.erase(steps.begin() + idx);
    for (size_t i = (size_t) idx; i < steps.size(); i++) if (steps[i].type == SongStep::Rep && steps[i].from > idx) steps[i].from--;
}
inline void songSetStep(State& st, int sn, int idx, const SongStep& step) { auto& s = st.songs[(size_t) sn].steps; if (idx >= 0 && idx < (int) s.size()) s[(size_t) idx] = cleanStep(step); }
inline void chainSet(State& st, int cn, const std::vector<int>& songs) {
    auto& c = st.chains[(size_t) cn].songs; c.clear();
    for (size_t i = 0; i < songs.size() && i < (size_t) CHAIN_MAX; i++) c.push_back(clampInt(songs[i], 0, NUM_SONGS - 1, 0));
}
inline void chainInsert(State& st, int cn, int idx, int song) {
    auto& c = st.chains[(size_t) cn].songs; if ((int) c.size() >= CHAIN_MAX) return;
    c.insert(c.begin() + (int) clampd(idx, 0, (double) c.size()), clampInt(song, 0, NUM_SONGS - 1, 0));
}
inline void chainDelete(State& st, int cn, int idx) { auto& c = st.chains[(size_t) cn].songs; if (idx >= 0 && idx < (int) c.size()) c.erase(c.begin() + idx); }

/* ---------------- song / chain cursor (no allocation: fixed repeat counters) ---------------- */
class SongCursor {
public:
    const State* st = nullptr; int sn = 0; int si = 0, at = -1; double tempo = 120; std::array<int, NCH> vol {};
    std::array<int, SONG_MAX + 1> cnt {};
    void init(const State& s, int song) { st = &s; sn = song; reset(); }
    void reset() {
        si = 0; at = -1; cnt.fill(0);
        tempo = sn >= 0 && sn < NUM_SONGS ? st->songs[(size_t) sn].tempo : 120;
        vol.fill(-1);
    }
    int next() {   // next pattern number, or -1 at the end
        if (sn < 0 || sn >= NUM_SONGS) return -1;
        const auto& steps = st->songs[(size_t) sn].steps; const int ns = (int) steps.size();
        int guard = 0;
        while (si < ns) {
            if (++guard > 100000) return -1;
            const SongStep& s = steps[(size_t) si];
            if (s.type == SongStep::Pat) { at = si++; return s.n; }
            if (s.type == SongStep::Rep) {
                const int c = si <= SONG_MAX ? cnt[(size_t) si] : 0;
                if (c + 1 < s.count && s.from < si) {
                    cnt[(size_t) si] = c + 1;
                    for (int k = s.from; k < si; k++) cnt[(size_t) k] = 0;
                    si = s.from;
                } else { cnt[(size_t) si] = 0; si++; }
                continue;
            }
            if (s.type == SongStep::Tempo) tempo = s.bpm;
            else if (s.type == SongStep::Vol) vol[(size_t) s.ch] = s.level;
            si++;
        }
        return -1;
    }
};
class Cursor {   // a song, or a chain of songs
public:
    bool chain = false; const State* st = nullptr; int n = 0, ci = 0; bool hasSong = false; SongCursor sc;
    void init(const State& s, bool isChain, int num) { st = &s; chain = isChain; n = num; reset(); }
    void reset() {
        if (!chain) { sc.init(*st, n); hasSong = true; return; }
        ci = 0; song();
    }
    void song() { const auto& list = st->chains[(size_t) n].songs; hasSong = ci < (int) list.size(); if (hasSong) sc.init(*st, list[(size_t) ci]); }
    int next() {
        if (!chain) return sc.next();
        while (hasSong) { const int r = sc.next(); if (r >= 0) return r; ci++; song(); }
        return -1;
    }
    double tempo() const { return hasSong ? sc.tempo : 120; }
    int vol(int ch) const { return hasSong ? sc.vol[(size_t) ch] : -1; }
    int at() const { return hasSong ? sc.at : -1; }
    int sn() const { return hasSong ? sc.sn : -1; }
};

struct Flat { std::vector<Event> events; std::vector<std::pair<int, double>> tempos; struct Sig { int t, beats, unit; }; std::vector<Sig> sigs; int length = 0; };
inline Flat flattenSong(const State& st, int sn) {
    SongCursor cur; cur.init(st, sn); Flat f; int t0 = 0, n, bars = 0; double bpm = -1; std::string sig;
    while ((n = cur.next()) >= 0) {
        const Pattern& p = st.patterns[(size_t) n];
        if (cur.tempo != bpm) { bpm = cur.tempo; f.tempos.push_back({t0, bpm}); }
        const std::string s = std::to_string(p.beats) + "/" + std::to_string(p.unit);
        if (s != sig) { sig = s; f.sigs.push_back({t0, p.beats, p.unit}); }
        for (const auto& e : p.events) { Event x = e; x.t += t0; f.events.push_back(x); }
        t0 += patLen(p);
        if (++bars > 100000) break;
    }
    f.length = t0; return f;
}

/* ---------------- Player ---------------- */
struct PlayEvent { int offset, v, vel, acc, click; };   // v = -1: metronome click (click 2 = bar downbeat, 1 = beat); v = -2: a tick

class Player {
public:
    enum Mode { PatternMode, SongMode, ChainMode };
    State* st = nullptr; Mode mode = PatternMode; int n = 0;
    bool running = false, ended = false, ext = false;
    bool hasCursor = false; Cursor cursor;
    int pat = 0, tick = 0, cur = -1, pass = 0; double wait = 0, tempo = 120, tempoOverride = 0, spt = 0;
    struct Skip { int pat, t, v, pass; }; std::array<Skip, 64> skip {}; int nSkip = 0;
    // host transport follower
    bool hostLocked = false; long long hostNext = 0;
    bool clockTicks = false;   // also emit {v:-2} on every tick

    explicit Player(State* s = nullptr) : st(s) { if (s) tempo = s->tempo; }
    void setState(State* s) { st = s; hasCursor = false; mode = PatternMode; n = 0; running = false; rewind(); }
    void setMode(Mode m, int num = 0) { mode = m; n = num; rewind(); }
    void setExternal(bool on) { ext = on; }
    bool rewind() {
        tick = 0; cur = -1; pass = 0; wait = 0; ended = false; nSkip = 0;
        if (mode == PatternMode) { hasCursor = false; pat = clampInt(n, 0, NUM_PATTERNS - 1, 0); tempo = st->tempo; return true; }
        cursor.init(*st, mode == ChainMode, n); hasCursor = true;
        const int first = cursor.next();
        if (first < 0) { ended = true; return false; }
        pat = first; tempo = cursor.tempo();
        return true;
    }
    void start() { running = rewind(); hostLocked = false; }
    void stop() { running = false; hostLocked = false; }
    void cont() { if (ended) { start(); return; } wait = 0; running = true; }
    void setTempo(double bpm) { tempoOverride = bpm > 0 ? clampd(bpm, 40, 250) : 0; }
    double bpm() const { return tempoOverride > 0 ? tempoOverride : (mode == PatternMode ? st->tempo : tempo); }

    template <class F> void advance(int nFrames, double sr, double bpmOverride, F&& emit) {
        if (bpmOverride > 0) tempoOverride = bpmOverride;
        if (!running || ext) return;
        while (running && wait < nFrames) {
            const int off = (int) std::min((double) nFrames - 1, std::max(0.0, std::ceil(wait - 1e-6)));
            fire(off, emit);
            wait += (sr * 60) / (bpm() * PPQN);
        }
        if (running) wait -= nFrames;
        spt = (sr * 60) / (bpm() * PPQN);
    }
    template <class F> void clock24(int offset, F&& emit) { if (running) fire(offset, emit); }
    void spp(int beats) { locate((long long) beats * 6); }
    void locate(long long abs) {
        const bool was = running;
        if (!rewind()) { running = false; return; }
        if (abs < 0) abs = 0;
        if (mode == PatternMode) tick = (int) (abs % patLen(st->patterns[(size_t) pat]));
        else {
            int guard = 0;
            for (;;) {
                const int L = patLen(st->patterns[(size_t) pat]);
                if (abs < L) { tick = (int) abs; break; }
                abs -= L;
                const int r = cursor.next();
                if (r < 0 || ++guard > 100000) { ended = true; running = false; return; }
                pat = r; tempo = cursor.tempo();
            }
        }
        cur = tick - 1;
        running = was;
    }
    // Follows the host: ppq at the start of this block, host tempo, block length. Ticks fall on ppq multiples of 1/24;
    // a jump (or a new start) relocates to the host position.
    template <class F> void hostBlock(double ppq0, double hostBpm, int nFrames, double sr, F&& emit) {
        if (hostBpm <= 0) hostBpm = 120;
        tempoOverride = clampd(hostBpm, 1, 999);
        const double t0 = ppq0 * PPQN, perSample = hostBpm * PPQN / (60 * sr), t1 = t0 + nFrames * perSample;
        long long k = (long long) std::ceil(t0 - 1e-7);
        if (!running) { if (ended && hostLocked) return; running = true; hostLocked = false; }
        if (!hostLocked || k != hostNext) { if (k < 0) { hostNext = k; hostLocked = true; } else { running = true; locate(k); hostLocked = true; } }
        for (; k < t1 && running; k++) {
            if (k < 0) continue;
            const int off = (int) clampd(std::ceil((k - t0) / perSample - 1e-6), 0, nFrames - 1);
            fire(off, emit);
        }
        hostNext = k;
        spt = 1 / perSample; wait = (k - t1) * spt;
    }
    void hostStopped() { hostLocked = false; running = false; }

    template <class F> void fire(int off, F&& emit) {
        Pattern* p = &st->patterns[(size_t) pat];
        if (tick >= patLen(*p)) {
            pass++; tick = 0;
            if (hasCursor) {
                const int r = cursor.next();
                if (r < 0) { running = false; ended = true; return; }
                pat = r; tempo = cursor.tempo();
                p = &st->patterns[(size_t) r];
            }
        }
        const int t = tick; const auto& ev = p->events;
        if (clockTicks) emit(PlayEvent { off, -2, 0, 0, 0 });   // one per tick (MIDI clock out)
        if (st->clickOn && t % ticksPerBeat(*p) == 0) emit(PlayEvent { off, -1, 0, 0, t % barTicks(*p) == 0 ? 2 : 1 });
        for (size_t i = lowerBound(ev, t); i < ev.size() && ev[i].t == t; i++) {
            const Event& e = ev[i];
            if (nSkip && skipped(e)) continue;
            const int ch = e.v % NCH;
            const int lvl = hasCursor && cursor.vol(ch) >= 0 ? cursor.vol(ch) : p->volume[(size_t) ch];
            if (lvl <= 0) continue;
            const int vel = lvl >= 31 ? e.vel : std::max(1, (int) jsround((e.vel * lvl) / 31.0));
            emit(PlayEvent { off, e.v, vel, e.acc, 0 });
        }
        cur = t; tick = t + 1;
    }
    bool skipped(const Event& e) {
        for (int k = 0; k < nSkip; k++) {
            Skip& s = skip[(size_t) k];
            if (s.pat != pat || s.t != e.t || s.v != e.v) continue;
            const int sp = s.pass;
            if (sp <= pass) { skip[(size_t) k] = skip[(size_t) --nSkip]; }
            return sp == pass;
        }
        return false;
    }
    double rawPos() const { const double s = spt > 0 ? spt : 1; return tick - std::max(0.0, wait) / s; }
    // the fractional tick `offset` samples into the coming block (call before advance())
    double rawPosAt(int offset) const { const double s = spt > 0 ? spt : 1; return tick - std::max(0.0, wait) / s + offset / s; }
    // real-time write (overdub) while looping a pattern; raw < 0: the position at the end of the last block
    bool record(int v, int vel, int acc, double raw = -1) {
        if (!running || mode != PatternMode) return false;
        Pattern& p = st->patterns[(size_t) pat]; const int len = patLen(p);
        const int q = quantizeTick(raw < 0 ? rawPos() : raw, st->quantize, p.swing);
        const Event e = recordHit(*st, pat, q, v, vel, acc, 9);
        if (nSkip < (int) skip.size()) {
            if (q >= len) skip[(size_t) nSkip++] = { pat, e.t, e.v, pass + 1 };
            else if (e.t >= tick) skip[(size_t) nSkip++] = { pat, e.t, e.v, pass };
        }
        return true;
    }
    struct Pos { int pattern, bar, beat, tick, song, step, chainPos; bool running, ended; double bpm; };
    Pos pos() const {
        const Pattern& p = st->patterns[(size_t) pat];
        const int t = std::max(0, cur), tpb = ticksPerBeat(p), bt = barTicks(p);
        return { pat, t / bt + 1, (t % bt) / tpb + 1, t % tpb, hasCursor ? cursor.sn() : -1, hasCursor ? cursor.at() : -1, mode == ChainMode ? cursor.ci : -1, running, ended, bpm() };
    }
};

/* ---------------- randomize (the same draws as seq.js) ---------------- */
struct Rand {
    uint32_t x;
    explicit Rand(uint32_t seed) : x(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
    double f() { return next() / 4294967296.0; }
    double r(double lo, double hi) { return lo + (hi - lo) * f(); }
    int i(int lo, int hi) { return lo + (int) std::floor((hi - lo + 1) * f()); }
    bool p(double prob) { return f() < prob; }
};
inline double fix2(double x) { return jsround(x * 100) / 100; }   // +x.toFixed(2)

struct Genre { const char* name; double tempo[2], swing[2]; std::vector<std::pair<int, std::array<double, 16>>> parts; };
inline const std::vector<Genre>& GENRES() {
    static const std::vector<Genre> g = {
        { "rock", {96, 140}, {50, 50}, {
            {0, {.95, 0, .1, 0, 0, 0, .25, 0, .85, 0, .4, 0, 0, 0, .15, 0}},
            {1, {0, 0, 0, 0, .98, 0, 0, .05, 0, 0, 0, 0, .98, 0, 0, .2}},
            {7, {.95, .05, .9, .05, .95, .05, .9, .05, .95, .05, .9, .05, .95, .05, .9, .05}},
            {19, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, .25, 0}} } },
        { "funk", {92, 115}, {50, 58}, {
            {0, {.95, 0, .2, .3, 0, 0, .3, 0, .4, .2, .6, 0, 0, .3, 0, .1}},
            {1, {0, 0, 0, .1, .98, 0, .15, .3, 0, .25, 0, .1, .98, 0, .2, .3}},
            {7, {.95, .7, .95, .7, .95, .7, .95, .7, .95, .7, .95, .7, .95, .7, .95, .7}},
            {10, {0, 0, .2, 0, 0, 0, .2, 0, 0, 0, .2, 0, 0, 0, .2, 0}},
            {19, {0, 0, 0, 0, 0, 0, 0, .3, 0, 0, 0, 0, 0, 0, 0, .3}} } },
        { "hiphop", {80, 98}, {56, 66}, {
            {0, {.98, 0, 0, .2, 0, 0, .3, .2, .1, .4, .6, 0, 0, 0, .2, 0}},
            {1, {0, 0, 0, 0, .98, 0, 0, 0, 0, 0, 0, 0, .98, 0, 0, .1}},
            {2, {0, 0, 0, 0, 0, 0, 0, .15, 0, 0, 0, 0, 0, .15, 0, 0}},
            {7, {.9, .2, .9, .3, .9, .2, .9, .3, .9, .2, .9, .3, .9, .2, .9, .4}} } },
        { "house", {118, 128}, {50, 56}, {
            {0, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, .1}},
            {7, {0, .2, 0, .5, 0, .2, 0, .5, 0, .2, 0, .5, 0, .2, 0, .5}},
            {11, {.6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6, .6}},
            {19, {0, 0, .95, 0, 0, 0, .95, 0, 0, 0, .95, 0, 0, 0, .95, 0}},
            {22, {0, 0, 0, 0, .95, 0, 0, 0, 0, 0, 0, 0, .95, 0, 0, 0}} } },
        { "electro", {108, 130}, {50, 50}, {
            {0, {.95, 0, 0, .3, 0, 0, .6, 0, 0, .3, .5, 0, 0, 0, 0, .2}},
            {1, {0, 0, 0, 0, .95, 0, 0, 0, 0, 0, 0, 0, .95, 0, 0, .1}},
            {7, {.9, .4, .9, .4, .9, .4, .9, .4, .9, .4, .9, .4, .9, .4, .9, .4}},
            {22, {0, 0, 0, 0, .5, 0, 0, 0, 0, 0, 0, 0, .5, 0, 0, 0}},
            {23, {0, 0, .15, 0, 0, 0, 0, .15, 0, 0, .15, 0, 0, 0, 0, 0}} } },
        { "breakbeat", {130, 170}, {50, 54}, {
            {0, {.98, 0, .3, 0, 0, 0, 0, 0, 0, 0, .9, .2, 0, 0, .2, 0}},
            {1, {0, 0, 0, 0, .98, 0, 0, .25, 0, .3, 0, 0, .9, 0, .1, .35}},
            {7, {0, .3, 0, .3, 0, .3, 0, .3, 0, .3, 0, .3, 0, .3, 0, .3}},
            {8, {.8, 0, .8, 0, .8, 0, .8, 0, .8, 0, .8, 0, .8, 0, .8, 0}} } },
    };
    return g;
}
inline std::string upper(std::string s) { for (auto& c : s) c = (char) std::toupper((unsigned char) c); return s; }

inline Pattern genPattern(Rand& R, const Genre& g, const std::string& name, bool fill, bool crash, bool ride, int swing) {
    Pattern p = newPattern(); p.name = name; p.bars = 2; p.swing = swing;
    const int sixteenth = 6, bar = 96;
    for (int b = 0; b < p.bars; b++) {
        const int fillFrom = fill && b == p.bars - 1 ? (R.p(.5) ? 8 : 12) : 16;
        for (const auto& part : g.parts) {
            int v = part.first;
            if (ride && v == 7) v = 8;
            for (int s = 0; s < 16; s++) {
                if (s >= fillFrom && v != 0) continue;
                const double k = b == 1 ? R.r(.85, 1.1) : 1;
                if (!R.p(part.second[(size_t) s] * k)) continue;
                const int acc = s % 4 == 0 && R.p(.25) ? 1 : 0;
                const int vel = (int) jsround(clampd((s % 4 == 0 ? 100 : s % 2 == 0 ? 88 : 70) + R.r(-14, 14), 1, 127));
                putEvent(p, b * bar + quantizeTick(s * sixteenth, Q_1_16, swing), v, vel, acc);
            }
        }
        if (fillFrom < 16) {
            static const int toms[8] = { 3, 4, 5, 6, 15, 16, 17, 18 };
            int ti = R.i(0, 1);
            for (int s = fillFrom; s < 16; s++) {
                if (!R.p(.8)) continue;
                const int v = R.p(.35) ? 1 : toms[std::min(7, ti)];
                if (v != 1 && R.p(.6)) ti++;
                const int vel = (int) jsround(R.r(80, 120));
                const int acc = s == 15 && R.p(.5) ? 2 : 0;
                putEvent(p, b * bar + quantizeTick(s * sixteenth, Q_1_16, swing), v, vel, acc);
            }
        }
    }
    if (crash) putEvent(p, 0, 9, 110, 1);
    return p;
}
inline void randomVoice(Rand& R, int v, VoiceParams& o) {
    const bool metal = v == 7 || v == 8 || v == 9 || v == 19 || v == 20 || v == 21;
    { const bool a = R.p(.6); o[vPitch] = jsround(a ? R.r(-150, 150) : R.r(-500, 500)); }
    o[vLevel] = R.i(22, 31); o[vAr] = R.i(90, 99); o[vD1r] = R.i(0, 70); o[vD1l] = R.i(36, 60); o[vD2r] = R.i(metal ? 20 : 30, 80); o[vRr] = R.i(30, 90);
    o[vGate] = R.p(.5) ? 0 : jsround(R.r(100, metal ? 2000 : 800));
    o[vBendRate] = R.p(.25) ? R.i(10, 60) : 0; o[vBendRange] = R.p(.25) ? jsround(R.r(-1200, 600)) : 0;
    o[vReverse] = R.p(.06) ? 1 : 0; o[vLoop] = R.p(.04) ? 1 : 0;
}
inline void randomRev(Rand& R, std::array<double, rCount>& o) {
    const int program = R.i(0, 10);
    o[rProgram] = program; o[rTime] = fix2(R.r(.5, program >= 9 ? 1.5 : 5)); o[rHiRatio] = fix2(R.r(.3, 1)); o[rDelay] = jsround(R.r(0, 80));
    o[rDiffusion] = R.i(3, 10); o[rDensity] = R.i(40, 100); o[rErLevel] = fix2(R.r(.2, 1)); o[rRevLevel] = fix2(R.r(.4, 1));
    o[rHpf] = jsround(R.r(20, 300)); o[rLpf] = jsround(R.r(4000, 16000)); o[rGateTime] = jsround(R.r(100, 450));
}
inline void randomDly(Rand& R, std::array<double, dCount>& o) {
    const double tl = jsround(R.r(60, 700));
    o[dTimeL] = tl; o[dTimeR] = R.p(.5) ? tl : jsround(tl * R.r(.5, 1.5)); o[dSync] = R.p(.5) ? R.i(1, 5) : 0; o[dFeedback] = jsround(R.r(10, 70));
    o[dPingpong] = R.p(.5) ? 1 : 0; o[dHiCut] = jsround(R.r(3000, 14000)); o[dLoCut] = jsround(R.r(40, 400)); o[dModRate] = fix2(R.r(.1, 3));
    o[dModDepth] = fix2(R.r(0, .5)); o[dDuckThresh] = jsround(R.r(-40, -6)); o[dDuckRelease] = jsround(R.r(50, 500));
    o[dSpread] = fix2(R.r(.3, 1)); o[dMix] = fix2(R.r(.15, .5));
}
inline void randomChannel(Rand& R, int ch, ChannelParams& c) {
    const bool centre = ch == 0 || ch == 1 || ch == 10;
    c.fader = fix2(R.r(.6, .95)); c.pan = centre ? 0 : fix2(R.r(-.7, .7));
    c.sendRev = fix2(ch == 1 || ch == 10 ? R.r(.2, .6) : R.r(0, .3)); c.sendDly = fix2(R.p(.3) ? R.r(.1, .4) : 0); c.mute = false;
}
// scope: "all" | "pattern" | "voices" | "fx". Returns the genre.
inline std::string randomize(State& st, uint32_t seed, const std::string& scope = "all", int pattern = 0, int start = 0, int songSlot = 0, const std::string& forceGenre = "") {
    Rand R(seed); const auto& G = GENRES();
    int gi = -1; for (size_t k = 0; k < G.size(); k++) if (forceGenre == G[k].name) gi = (int) k;
    if (gi < 0) gi = R.i(0, (int) G.size() - 1);
    const Genre& g = G[(size_t) gi]; const std::string genre = g.name;
    const int swing = (int) jsround(R.r(g.swing[0], g.swing[1]));
    if (scope == "voices" || scope == "all") for (int v = 0; v < NV; v++) randomVoice(R, v, st.voice[(size_t) v]);
    if (scope == "fx" || scope == "all") { randomRev(R, st.rev); randomDly(R, st.dly); }
    if (scope == "all") for (int ch = 0; ch < NCH; ch++) randomChannel(R, ch, st.channel[(size_t) ch]);
    if (scope == "pattern") { const bool fill = R.p(.7); st.patterns[(size_t) clampInt(pattern, 0, NUM_PATTERNS - 1, 0)] = genPattern(R, g, upper(genre).substr(0, 8), fill, false, false, swing); }
    if (scope == "all") {
        const int s0 = clampInt(start, 0, NUM_PATTERNS - 4, 0), sn = clampInt(songSlot, 0, NUM_SONGS - 1, 0);
        const std::string tag = upper(genre.substr(0, 4));
        st.patterns[(size_t) s0] = genPattern(R, g, tag + " A", false, false, false, swing);
        st.patterns[(size_t) s0 + 1] = genPattern(R, g, tag + " A2", false, false, false, swing);
        st.patterns[(size_t) s0 + 2] = genPattern(R, g, tag + " FIL", true, false, false, swing);
        { const bool ride = R.p(.5); st.patterns[(size_t) s0 + 3] = genPattern(R, g, tag + " B", false, true, ride, swing); }
        const double tempo = jsround(R.r(g.tempo[0], g.tempo[1]));
        st.tempo = tempo;
        Song& s = st.songs[(size_t) sn]; s.name = upper(genre).substr(0, 8); s.tempo = tempo;
        s.steps = { SongStep::pat(s0), SongStep::pat(s0 + 1), SongStep::rep(0, 2), SongStep::pat(s0 + 2), SongStep::mark(), SongStep::pat(s0 + 3), SongStep::rep(5, 3),
            SongStep::pat(s0 + 2), SongStep::pat(s0) };
    }
    return genre;
}

/* ---------------- note maps and MIDI helpers ---------------- */
static const double ACC_GAIN[3] = { 1, 1.413, 1.995 };
inline int accentVel(int vel, int acc) { return (int) std::min(127.0, jsround(vel * ACC_GAIN[acc < 0 || acc > 2 ? 0 : acc])); }
inline bool isGmMap(const std::array<int, NV>& m) { return std::equal(m.begin(), m.end(), MIDI_GM_NOTES); }
inline bool isRx5Map(const std::array<int, NV>& m) { return std::equal(m.begin(), m.end(), RX5_NOTES); }
inline std::array<int, NV> noteMapFor(const State& st, const std::string& which) {
    std::array<int, NV> m = st.noteMap;
    if (which == "rx5") std::copy(RX5_NOTES, RX5_NOTES + NV, m.begin()); else if (which == "gm") std::copy(MIDI_GM_NOTES, MIDI_GM_NOTES + NV, m.begin());
    return m;
}
// note -> voice (-1 = none): inverted table, lowest voice wins; GM extras when the table is the GM preset
inline std::array<int, 128> noteToVoiceMap(const State& st, const std::string& which = "state") {
    const auto nm = noteMapFor(st, which); std::array<int, 128> m; m.fill(-1);
    if (isGmMap(nm)) for (int k = 0; k < 3; k++) m[(size_t) GM_EXTRA_NOTE[k]] = GM_EXTRA_VOICE[k];
    for (int v = NV - 1; v >= 0; v--) if (nm[(size_t) v] >= 0 && nm[(size_t) v] < 128) m[(size_t) nm[(size_t) v]] = v;
    return m;
}
inline int txChannel(const State& st, int v) { const int c = st.midi.voiceTxCh[(size_t) v]; return c >= 1 && c <= 16 ? c : st.midi.txCh; }
// midiIn for one channel note message: the voices it plays (VOICE mode: every voice mapped to the note, plus GM extras;
// PITCH mode: midi.tunableVoice at (note-60)*100 cents, notes 36..96 only). Returns the count written into out/pitch.
inline int noteVoices(const State& st, int note, int* out, double* pitch) {
    const auto& m = st.midi;
    if (m.tunable) {
        if (note < TUNABLE_LO || note > TUNABLE_HI) return 0;
        out[0] = clampInt(m.tunableVoice, 0, NV - 1, 0); pitch[0] = (note - TUNABLE_CENTER) * 100.0; return 1;
    }
    int n = 0;
    for (int v = 0; v < NV; v++) if (st.noteMap[(size_t) v] == note) { out[n] = v; pitch[n] = 0; n++; }
    if (!n && isGmMap(st.noteMap)) for (int k = 0; k < 3; k++) if (GM_EXTRA_NOTE[k] == note) { out[0] = GM_EXTRA_VOICE[k]; pitch[0] = 0; n = 1; }
    return n;
}
inline bool sysexDeviceOk(const uint8_t* msg, size_t len, int deviceNo) {
    if (!deviceNo || len < 3) return true;
    if (msg[1] == 0x43) return (msg[2] & 0x0F) == ((deviceNo - 1) & 0x0F);
    if (msg[1] == 0x7E || msg[1] == 0x7F) return msg[2] == 0x7F || msg[2] == ((deviceNo - 1) & 0x7F);
    return true;
}

/* ---------------- SMF ---------------- */
constexpr int SMF_DIV = 96;
inline void vlq(uint32_t n, std::vector<uint8_t>& out) {
    uint8_t st[5]; int k = 0; st[k++] = n & 0x7f;
    while ((n >>= 7)) st[k++] = (uint8_t) ((n & 0x7f) | 0x80);
    while (k) out.push_back(st[--k]);
}
inline void chunk(const char* id, const std::vector<uint8_t>& data, std::vector<uint8_t>& out) {
    out.insert(out.end(), id, id + 4); const uint32_t n = (uint32_t) data.size();
    out.push_back((uint8_t) (n >> 24)); out.push_back((uint8_t) (n >> 16)); out.push_back((uint8_t) (n >> 8)); out.push_back((uint8_t) n);
    out.insert(out.end(), data.begin(), data.end());
}
struct TrEv { long long t; int o; std::vector<uint8_t> b; };
inline std::vector<uint8_t> trackBytes(const std::vector<TrEv>& evs) {
    std::vector<uint8_t> out; long long last = 0;
    for (const auto& e : evs) { vlq((uint32_t) (e.t - last), out); out.insert(out.end(), e.b.begin(), e.b.end()); last = e.t; }
    return out;
}
inline std::vector<uint8_t> metaText(int type, const std::string& s) { std::vector<uint8_t> b { 0xFF, (uint8_t) type }; vlq((uint32_t) s.size(), b); b.insert(b.end(), s.begin(), s.end()); return b; }
// what: "pattern" | "song"; map: "gm" | "rx5" | "state"
inline std::vector<uint8_t> exportSMF(const State& st, const std::string& what = "pattern", int n = 0, const std::string& map = "gm") {
    const auto nm = noteMapFor(st, map);
    Flat flat;
    if (what == "song") flat = flattenSong(st, n);
    else { const Pattern& p = st.patterns[(size_t) n]; flat.events = p.events; flat.tempos = { {0, st.tempo} }; flat.sigs = { {0, p.beats, p.unit} }; flat.length = patLen(p); }
    const std::string name = what == "song" ? st.songs[(size_t) n].name : st.patterns[(size_t) n].name;
    const int K = SMF_DIV / PPQN; const long long end = (long long) flat.length * K;
    std::vector<TrEv> t0 { { 0, 0, metaText(0x03, name) } };
    for (const auto& tp : flat.tempos) { const int us = (int) jsround(60e6 / tp.second); t0.push_back({ (long long) tp.first * K, 0, { 0xFF, 0x51, 3, (uint8_t) ((us >> 16) & 255), (uint8_t) ((us >> 8) & 255), (uint8_t) (us & 255) } }); }
    for (const auto& sg : flat.sigs) t0.push_back({ (long long) sg.t * K, 0, { 0xFF, 0x58, 4, (uint8_t) sg.beats, (uint8_t) std::lround(std::log2((double) sg.unit)), 24, 8 } });
    std::stable_sort(t0.begin(), t0.end(), [](const TrEv& a, const TrEv& b) { return a.t < b.t; });
    t0.push_back({ end, 0, { 0xFF, 0x2F, 0 } });
    struct N { int t, note; Event e; long long next = -1; };
    std::vector<N> notes; std::vector<Event> evs = flat.events;
    std::stable_sort(evs.begin(), evs.end(), [](const Event& a, const Event& b) { return a.t != b.t ? a.t < b.t : a.v < b.v; });
    std::map<long long, bool> seen;
    for (const auto& e : evs) { const int note = nm[(size_t) e.v]; const long long key = (long long) e.t * 256 + note; if (seen.count(key)) continue; seen[key] = true; notes.push_back({ e.t, note, e }); }
    std::map<int, int> nextSame;
    for (int i = (int) notes.size() - 1; i >= 0; i--) { auto it = nextSame.find(notes[(size_t) i].note); if (it != nextSame.end()) notes[(size_t) i].next = it->second; nextSame[notes[(size_t) i].note] = notes[(size_t) i].t; }
    std::vector<TrEv> tr;
    for (const auto& x : notes) {
        const long long on = (long long) x.t * K;
        const long long dur = std::max(1LL, std::min((long long) 6 * K, (x.next >= 0 ? x.next * K : end) - on));
        if (x.e.acc) tr.push_back({ on, 1, { 0xFF, 0x7F, 6, 0x7D, 0x4F, 0x35, (uint8_t) x.note, (uint8_t) x.e.acc, (uint8_t) x.e.vel } });
        tr.push_back({ on, 2, { 0x99, (uint8_t) x.note, (uint8_t) accentVel(x.e.vel, x.e.acc) } });
        tr.push_back({ on + dur, 0, { 0x89, (uint8_t) x.note, 0x40 } });
    }
    std::stable_sort(tr.begin(), tr.end(), [](const TrEv& a, const TrEv& b) { return a.t != b.t ? a.t < b.t : a.o < b.o; });
    const long long trEnd = std::max(end, tr.empty() ? 0LL : tr.back().t);
    std::vector<TrEv> t1 { { 0, 0, metaText(0x03, "DRUMS") } }; t1.insert(t1.end(), tr.begin(), tr.end()); t1.push_back({ trEnd, 0, { 0xFF, 0x2F, 0 } });
    std::vector<uint8_t> out; chunk("MThd", { 0, 1, 0, 2, (uint8_t) ((SMF_DIV >> 8) & 255), (uint8_t) (SMF_DIV & 255) }, out);
    chunk("MTrk", trackBytes(t0), out); chunk("MTrk", trackBytes(t1), out);
    return out;
}

struct SmfEvent { long long t; int meta = -1; std::vector<uint8_t> data; int type = 0, ch = 0, d1 = 0, d2 = 0; };
struct Smf { int format = 0, ntrks = 0, division = 96; std::vector<std::vector<SmfEvent>> tracks; std::string error; };
inline Smf parseSMF(const uint8_t* u8, size_t size) {
    Smf r; size_t p = 0;
    auto u32 = [&]() { uint32_t v = 0; for (int k = 0; k < 4; k++) v = (v << 8) | (p < size ? u8[p++] : 0); return v; };
    auto u16 = [&]() { uint32_t v = 0; for (int k = 0; k < 2; k++) v = (v << 8) | (p < size ? u8[p++] : 0); return (int) v; };
    if (size < 14 || std::string((const char*) u8, 4) != "MThd") { r.error = "not a Standard MIDI File (no MThd)"; return r; }
    p = 4; const uint32_t hlen = u32(); const size_t hs = p;
    r.format = u16(); r.ntrks = u16(); r.division = u16();
    if (r.division & 0x8000) { r.error = "SMPTE time division is not supported (only ticks-per-quarter-note files)"; return r; }
    if (!r.division) { r.error = "bad SMF division 0"; return r; }
    p = hs + hlen;
    while (p + 8 <= size && (int) r.tracks.size() < r.ntrks) {
        const std::string id((const char*) u8 + p, 4); p += 4;
        const uint32_t len = u32(); const size_t end = std::min(size, p + len);
        if (id != "MTrk") { p = end; continue; }
        std::vector<SmfEvent> ev; long long t = 0; int status = 0;
        auto rd = [&]() { uint32_t v = 0; uint8_t b; do { b = p < end ? u8[p++] : 0; v = (v << 7) | (b & 0x7f); } while ((b & 0x80) && p < end); return v; };
        while (p < end) {
            t += rd();
            const uint8_t b = u8[p];
            if (b == 0xFF) {
                p++; const int type = p < end ? u8[p++] : 0; const uint32_t l = rd();
                SmfEvent e; e.t = t; e.meta = type; const size_t a = std::min(p, end), z = std::min(p + l, end); e.data.assign(u8 + a, u8 + z); ev.push_back(e); p += l;
                if (type == 0x2F) break;
                continue;
            }
            if (b == 0xF0 || b == 0xF7) { p++; const uint32_t l = rd(); p += l; continue; }
            if (b & 0x80) { status = b; p++; } else if (!status) { r.error = "SMF: data byte without running status"; return r; }
            const int hi = status & 0xF0, ch = (status & 15) + 1;
            SmfEvent e; e.t = t; e.type = hi; e.ch = ch; e.d1 = p < end ? u8[p++] : 0; e.d2 = hi == 0xC0 || hi == 0xD0 ? 0 : (p < end ? u8[p++] : 0);
            ev.push_back(e);
        }
        p = end;
        r.tracks.push_back(std::move(ev));
    }
    return r;
}
struct ImportResult { bool ok = false; std::string error, map, report; std::vector<int> patterns; int song = 0; double tempo = 120; };
// channel: 0 = auto (10 if it has notes, else any), -1 = any, 1..16; map: "auto" | "gm" | "rx5" | "state"; quantize: -1 = none
inline ImportResult importSMF(State& st, const uint8_t* data, size_t size, int channel = 0, int startPattern = 0, int songSlot = 0, int quantize = -1, std::string mapName = "auto") {
    ImportResult res; const Smf smf = parseSMF(data, size);
    if (!smf.error.empty()) { res.error = smf.error; return res; }
    auto R = [&](long long t) { return (int) jsround((double) t * PPQN / smf.division); };
    struct Note { long long raw; int ch, note, vel; }; std::vector<Note> notes; std::vector<std::pair<int, double>> tempos; struct Sig { int t, num, den; }; std::vector<Sig> sigs;
    std::map<long long, std::pair<int, int>> markers; std::string trackName; int eot = 0;
    for (const auto& tr : smf.tracks) for (const auto& e : tr) {
        if (e.meta == 0x2F) eot = std::max(eot, R(e.t));
        else if (e.meta == 0x51 && e.data.size() >= 3) tempos.push_back({ R(e.t), 60e6 / ((e.data[0] << 16) | (e.data[1] << 8) | e.data[2]) });
        else if (e.meta == 0x58 && e.data.size() >= 2) sigs.push_back({ R(e.t), e.data[0], 1 << e.data[1] });
        else if (e.meta == 0x03 && trackName.empty()) { trackName.assign(e.data.begin(), e.data.end()); while (!trackName.empty() && trackName.back() == ' ') trackName.pop_back(); while (!trackName.empty() && trackName[0] == ' ') trackName.erase(0, 1); }
        else if (e.meta == 0x7F && e.data.size() >= 6 && e.data[0] == 0x7D && e.data[1] == 0x4F && e.data[2] == 0x35) markers[e.t * 256 + e.data[3]] = { e.data[4], e.data[5] };
        else if (e.meta < 0 && e.type == 0x90 && e.d2 > 0) notes.push_back({ e.t, e.ch, e.d1, e.d2 });
    }
    int chSel = channel;
    if (chSel == 0) { chSel = -1; for (const auto& n : notes) if (n.ch == 10) chSel = 10; }
    if (mapName == "auto") {
        std::vector<std::string> cands { "gm", "rx5" }; if (!isGmMap(st.noteMap) && !isRx5Map(st.noteMap)) cands.push_back("state");
        int best = -1;
        for (const auto& c : cands) { const auto mp = noteToVoiceMap(st, c); int hits = 0; for (const auto& x : notes) if ((chSel < 0 || x.ch == chSel) && mp[(size_t) (x.note & 127)] >= 0) hits++; if (hits > best) { best = hits; mapName = c; } }
    }
    const auto map = noteToVoiceMap(st, mapName);
    int otherCh = 0, unmapped = 0; std::vector<Event> evs;
    for (const auto& n : notes) {
        if (chSel >= 0 && n.ch != chSel) { otherCh++; continue; }
        const int v = map[(size_t) (n.note & 127)];
        if (v < 0) { unmapped++; continue; }
        auto mk = markers.find(n.raw * 256 + n.note);
        int t = R(n.raw);
        if (quantize >= 0) t = std::max(0, quantizeTick(t, quantize, 50));
        Event e; e.t = t; e.v = v; e.vel = mk != markers.end() ? mk->second.second : n.vel; e.acc = mk != markers.end() ? mk->second.first : 0; evs.push_back(e);
    }
    std::stable_sort(tempos.begin(), tempos.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::stable_sort(sigs.begin(), sigs.end(), [](const Sig& a, const Sig& b) { return a.t < b.t; });
    int lastT = eot - 1; for (const auto& e : evs) lastT = std::max(lastT, e.t);
    auto sigAt = [&](int t) { Sig s { 0, 4, 4 }; for (const auto& x : sigs) if (x.t <= t) s = x; return s; };
    auto toPU = [&](const Sig& s) { double beats = s.num; int unit = s.den; if (unit < 4) { beats = beats * 4 / unit; unit = 4; } else if (unit > 16) { beats = std::max(1.0, jsround(beats * 16 / unit)); unit = 16; } return std::make_pair((int) clampd(jsround(beats), 1, 32), unit); };
    struct Bar { int t, len, beats, unit; std::vector<Event> ev; int pn = -1; };
    std::vector<Bar> bars;
    for (int t = 0; t <= lastT || bars.empty();) {
        const auto pu = toPU(sigAt(t)); const int L = pu.first * PPQN * 4 / pu.second;
        bars.push_back({ t, L, pu.first, pu.second, {}, -1 });
        t += L;
        if (bars.size() > 10000) break;
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Event& a, const Event& b) { return a.t != b.t ? a.t < b.t : a.v < b.v; });
    size_t bi = 0;
    for (const auto& e : evs) {
        while (bi + 1 < bars.size() && e.t >= bars[bi + 1].t) bi++;
        Bar& b = bars[bi]; const int rel = e.t - b.t;
        if (rel >= b.len) continue;
        auto dup = std::find_if(b.ev.begin(), b.ev.end(), [&](const Event& x) { return x.t == rel && x.v == e.v; });
        if (dup != b.ev.end()) { if (e.vel > dup->vel) { dup->vel = e.vel; dup->acc = e.acc; } }
        else { Event x = e; x.t = rel; b.ev.push_back(x); }
    }
    const int start = clampInt(startPattern, 0, NUM_PATTERNS - 1, 0); songSlot = clampInt(songSlot, 0, NUM_SONGS - 1, 0);
    std::map<std::string, int> sigMap; std::vector<int> used; int dropped = 0;
    for (auto& b : bars) {
        std::stable_sort(b.ev.begin(), b.ev.end(), [](const Event& x, const Event& y) { return x.t != y.t ? x.t < y.t : x.v < y.v; });
        std::string key = std::to_string(b.beats) + "/" + std::to_string(b.unit) + "|";
        for (size_t k = 0; k < b.ev.size(); k++) { const auto& e = b.ev[k]; key += (k ? ";" : "") + std::to_string(e.t) + "," + std::to_string(e.v) + "," + std::to_string(e.vel) + "," + std::to_string(e.acc); }
        if (!sigMap.count(key)) {
            if (start + (int) used.size() >= NUM_PATTERNS) { b.pn = -1; dropped++; continue; }
            const int pn = start + (int) used.size(); Pattern p = newPattern(pn);
            std::string num = std::to_string(used.size() + 1); while (num.size() < 3) num = "0" + num;
            p.name = "MID " + num; p.beats = b.beats; p.unit = b.unit; p.events = b.ev;
            st.patterns[(size_t) pn] = p; sigMap[key] = pn; used.push_back(pn);
        }
        b.pn = sigMap[key];
    }
    const double tempo0 = clampd(!tempos.empty() && tempos[0].first == 0 ? tempos[0].second : 120, 40, 250);
    std::vector<SongStep> steps; double curBpm = tempo0; size_t ti = 0, i = 0;
    std::vector<const Bar*> valid; for (const auto& b : bars) if (b.pn >= 0) valid.push_back(&b);
    while (i < valid.size()) {
        const Bar& b = *valid[i]; double bpm = curBpm;
        while (ti < tempos.size() && tempos[ti].first < b.t + b.len) { bpm = clampd(tempos[ti].second, 40, 250); ti++; }
        if (std::fabs(bpm - curBpm) > 1e-6) { steps.push_back(SongStep::tempo(jsround(bpm * 100) / 100)); curBpm = bpm; }
        size_t run = 1;
        auto tempoInBar = [&](const Bar& x) { for (const auto& tp : tempos) if (tp.first >= x.t && tp.first < x.t + x.len && std::fabs(tp.second - curBpm) > 1e-6) return true; return false; };
        while (i + run < valid.size() && valid[i + run]->pn == b.pn && run < 99 && !tempoInBar(*valid[i + run])) run++;
        const int at = (int) steps.size();
        steps.push_back(SongStep::pat(b.pn));
        if (run > 1) steps.push_back(SongStep::rep(at, (int) run));
        i += run;
    }
    Song& s = st.songs[(size_t) songSlot]; s.name = (trackName.empty() ? std::string("MIDI IMP") : trackName).substr(0, 8); s.tempo = jsround(tempo0 * 100) / 100; s.steps = steps;
    res.ok = true; res.patterns = used; res.song = songSlot; res.tempo = tempo0; res.map = mapName;
    res.report = "SMF format " + std::to_string(smf.format) + ", " + std::to_string(smf.tracks.size()) + " track(s), " + std::to_string(smf.division) + " tpq; " +
        std::to_string(evs.size()) + " notes imported, " + std::to_string(unmapped) + " unmapped, " + std::to_string(otherCh) + " on other channels; " +
        std::to_string(bars.size()) + " bars -> " + std::to_string(used.size()) + " patterns" + (dropped ? ", " + std::to_string(dropped) + " bars dropped" : "");
    return res;
}

}  // namespace five
