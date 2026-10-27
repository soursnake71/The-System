// SPQuencer1200 engine: plays SP-1200 segments and songs (24 PPQ events: tick, sound, level, tune) in time with the
// DAW or on its own clock, sends each note to the MIDI output for an E-mu or Rossum SP-1200, and plays preview sounds.
// The rules match the editor (spquencer/app.js): segment swing moves notes exactly on the 2nd 8th of a beat,
// beats restart at every bar, songs expand repeats, sub-songs and links. No JUCE here, so it can be tested alone.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace spq {

constexpr int PPQ = 24, NSND = 32, NSEG = 100, NSONG = 100, NMIX = 8;
// E-mu SP-1200 semitone table (index = semitones + 16), playback increment / 128; from libmd12 (MIT)
static const int SP_TUNE[32] = {50,53,56,59,63,67,71,75,80,84,90,95,101,107,113,120,127,135,143,151,160,170,180,191,202,214,227,241,255,255,255,255};

template <typename T> static T clampv(T v, T a, T b) { return v < a ? a : v > b ? b : v; }

struct Event { int t = 0, s = 0, l = 31, u = 0; };

struct Segment {
    bool used = false;
    int len = 2, num = 4, den = 4, swing = 12;
    double tempo = 90;
    std::vector<Event> events;
    std::vector<std::vector<int>> at;   // event indices by the tick they play on

    int ticksPerBar() const { return num * 96 / den; }
    int ticks() const { return len * ticksPerBar(); }
    int played(int t) const {
        if (swing <= 12) return t;
        const int bt = ticksPerBar(), rel = t % bt;
        return rel % PPQ == 12 && rel - 12 + swing < bt ? t - 12 + swing : t;
    }
    void index() {
        at.assign((size_t) std::max(1, ticks()), {});
        for (size_t i = 0; i < events.size(); ++i) {
            const int p = played(events[i].t);
            if (p >= 0 && p < ticks()) at[(size_t) p].push_back((int) i);
        }
    }
};

struct Step { std::string k; double v = 0; };
struct Song { bool used = false; double tempo = 90; std::vector<Step> steps; };
struct Sample { std::vector<float> data; double rate = 0; };

struct Sound {
    std::string name;
    bool decayMode = false;
    int tune = 0, decay = 31, note = -1, start = 0, end = 0, loop = -1;
    const Sample* sample = nullptr;   // owned by the processor, never freed while it runs
};

struct Project {
    Segment seg[NSEG];
    Song song[NSONG];
    Sound snd[NSND];
    int mix[NMIX][NSND];
    int ch = 1, base = 36, trigCh = 16;
    std::string hostMode = "seg";     // seg | song | notes | off
    bool audio = true;
    int curSeg = 1, curSong = 1, curMix = 0;

    Project() { for (auto& m : mix) for (auto& v : m) v = 24; }
    int noteOf(int i) const { return clampv(snd[i].note >= 0 ? snd[i].note : base + i, 0, 127); }
};

struct SeqEntry { int seg; double tempo; int mix; int idx; long start; };

// a song as the segments it plays, in order (repeats, sub-songs and links expanded), with each one's start tick
struct SongExpander {
    const Project& p;
    double tempo; int mix;
    std::vector<SeqEntry> out;

    static size_t matchEnd(const std::vector<Step>& steps, size_t i) {
        int d = 0;
        for (size_t j = i + 1; j < steps.size(); ++j) {
            if (steps[j].k == "rep") ++d;
            else if (steps[j].k == "rend") { if (!d) return j; --d; }
        }
        return steps.size();
    }
    // returns true when an End or a Link stopped the song
    bool run(int n, size_t a, size_t b, int depth, int topIdx) {
        const auto& steps = p.song[n].steps;
        for (size_t i = a; i < b && out.size() < 4000; ++i) {
            const Step& s = steps[i];
            const int idx = topIdx >= 0 ? topIdx : (int) i, v = (int) s.v;
            if (s.k == "seg") { if (v >= 0 && v < NSEG && p.seg[v].used) out.push_back({v, tempo, mix, idx, 0}); }
            else if (s.k == "tempo") tempo = clampv(s.v, 30.0, 250.0);
            else if (s.k == "mix") mix = clampv(v - 1, 0, NMIX - 1);
            else if (s.k == "rep") {
                const size_t e = matchEnd(steps, i);
                for (int r = 0; r < clampv(v, 1, 99); ++r) if (run(n, i + 1, e, depth, topIdx)) return true;
                i = e;
            }
            else if (s.k == "sub" || s.k == "link") {
                if (depth < 6 && v != n && v >= 0 && v < NSONG && p.song[v].used) run(v, 0, p.song[v].steps.size(), depth + 1, idx);
                if (s.k == "link") return true;
            }
            else if (s.k == "end") return true;
        }
        return false;
    }
};

static std::vector<SeqEntry> songSequence(const Project& p, int n) {
    std::vector<SeqEntry> out;
    if (n < 0 || n >= NSONG || !p.song[n].used) return out;
    SongExpander x { p, p.song[n].tempo, p.curMix, {} };
    x.run(n, 0, p.song[n].steps.size(), 0, -1);
    out = std::move(x.out);
    long t = 0;
    for (auto& e : out) { e.start = t; t += p.seg[e.seg].ticks(); }
    return out;
}

struct MidiMsg { int offset; uint8_t b[3]; };

// one preview voice: a loaded sample played the SP way (drop-sample pitch from the semitone table), or a small synth
struct Voice {
    enum Kind { None, Smp, Kick, Snare, Clap, Cymbal, Hat, Tom, Click, Cowbell, Bass, Stab, Zap, Other };
    Kind kind = None;
    int sound = -1;
    double t = 0, dt = 0, len = 1, hold = 1, gain = 0, rate = 1;
    double pos = 0, inc = 1, ph1 = 0, ph2 = 0, ph3 = 0, f0 = 100, f1 = 100, nlen = 0, nf = 0, hp = 0, lp = 0, lp2 = 0;
    const Sample* smp = nullptr; int st = 0, en = 0, lo = -1;
    uint32_t seed = 22222;

    float noise() { seed = seed * 1664525u + 1013904223u; return (float) ((int32_t) seed) / 2147483648.0f; }
    static double expEnv(double t, double len) { return t >= len ? 0.0 : std::exp(-6.9 * t / len); }

    float render() {
        if (kind == None) return 0;
        double y = 0;
        if (kind == Smp) {
            const int i = (int) pos;
            if (i >= en) { if (lo >= 0) { pos = lo + (pos - en); } else { kind = None; return 0; } }
            y = smp->data[(size_t) std::min((int) pos, en - 1)];
            pos += inc;
            double e = 1;
            if (t > hold) e = std::exp(-6.9 * (t - hold) / std::max(1e-4, len - hold));
            if (t >= len) { kind = None; return 0; }
            y *= e;
        } else {
            const double twoPi = 6.283185307179586;
            auto sweep = [&](double a, double b) { return b + (a - b) * std::exp(-t / 0.05); };
            const double n = noise();
            switch (kind) {
                case Kick: ph1 += twoPi * sweep(f0, f1) * dt; y = std::sin(ph1) * expEnv(t, len); break;
                case Tom: ph1 += twoPi * sweep(f0, f1) * dt; y = std::sin(ph1) * expEnv(t, len) * .9; break;
                case Snare: {
                    ph1 += twoPi * sweep(f0, f1) * dt; hp = .7 * (hp + n - lp); lp = n;
                    y = std::asin(std::sin(ph1)) * .6 * expEnv(t, len * .6) + hp * .7 * expEnv(t, len); break;
                }
                case Clap: { lp = lp + .25 * (n - lp); const double bp = n - lp; const double burst = t < .033 ? (std::fmod(t, .011) < .006 ? 1 : .2) : expEnv(t - .033, len); y = bp * .9 * burst; break; }
                case Cymbal: case Hat: { hp = .5 * (hp + n - lp); lp = n; lp2 = .5 * (lp2 + hp - ph3); ph3 = hp; y = lp2 * expEnv(t, len) * .8; break; }
                case Click: ph1 += twoPi * f0 * dt; y = std::asin(std::sin(ph1)) * .7 * expEnv(t, len); break;
                case Cowbell: ph1 += f0 * dt; ph2 += f0 * 1.51 * dt; y = ((std::fmod(ph1, 1.0) < .5 ? .3 : -.3) + (std::fmod(ph2, 1.0) < .5 ? .25 : -.25)) * expEnv(t, len); break;
                case Bass: ph1 += twoPi * f0 * dt; y = std::asin(std::sin(ph1)) * expEnv(t, len); break;
                case Stab: ph1 += f0 * dt; ph2 += f0 * 1.26 * dt; ph3 += f0 * 1.5 * dt;
                    y = ((std::fmod(ph1, 1.0) - .5) + (std::fmod(ph2, 1.0) - .5) + (std::fmod(ph3, 1.0) - .5)) * .45 * expEnv(t, len); break;
                case Zap: ph1 += sweep(f0, f1) * dt; y = (std::fmod(ph1, 1.0) < .5 ? .5 : -.5) * expEnv(t, len); break;
                case None: case Smp: case Other: default: ph1 += twoPi * sweep(f0, f1) * dt; y = std::asin(std::sin(ph1)) * .5 * expEnv(t, len) + n * .3 * expEnv(t, .05); break;
            }
            if (t >= len) { kind = None; return 0; }
        }
        t += dt;
        return (float) (y * gain);
    }
};

class Engine {
public:
    struct Host { bool playing = false; bool hasPpq = false; double ppq = 0; double bpm = 120; };
    struct Pos { bool playing = false; int mode = 0, seg = 0, tick = 0, idx = -1; };   // mode 1 segment, 2 song

    void prepare(double sampleRate) {
        sr = sampleRate; for (auto& v : voices) v.kind = Voice::None; pendingOff.clear(); internal = false; trigNote = -1;
        pendingOff.reserve(512); trig.reserve(2048); auditions.reserve(64);
    }
    void setProject(const Project* p) {
        proj = p;
        seq = p ? songSequence(*p, p->curSong) : std::vector<SeqEntry>{};
        if (internal && imode == 2) iseq = p ? songSequence(*p, isong) : std::vector<SeqEntry>{};
    }

    // from the editor (called under the processor's lock)
    void play(int mode, int seg, int song, int fromTick) {
        if (!proj) return;
        imode = mode; iseg = clampv(seg, 0, NSEG - 1); isong = clampv(song, 0, NSONG - 1); ili = 0; itick = std::max(0, fromTick); iacc = 0;
        if (imode == 2) { iseq = songSequence(*proj, isong); if (iseq.empty()) return; }
        else if (!proj->seg[iseg].used) return;
        internal = true;
    }
    void stop() { internal = false; trigNote = -1; }
    void audition(int s, int l, int u) { auditions.push_back({s, l, u}); }
    Pos position() const { return pos; }

    // midiIn: note messages from the DAW (for notes mode); midiOut gets every note sent to the SP
    void process(float* L, float* R, int n, const Host& host, const std::vector<MidiMsg>& midiIn, std::vector<MidiMsg>& midiOut) {
        trig.clear();
        if (!proj) { std::fill(L, L + n, 0.f); std::fill(R, R + n, 0.f); return; }
        const Project& p = *proj;
        for (auto& a : auditions) fire(a.s, a.l, a.u, p.curMix, 0, midiOut);
        auditions.clear();

        const bool hostDrives = host.playing && host.hasPpq && (p.hostMode == "seg" || p.hostMode == "song");
        if (hostDrives) internal = false;

        // notes mode: a note on the trigger channel starts that segment, note off stops it
        if (p.hostMode == "notes") {
            for (auto& m : midiIn) {
                const int st = m.b[0] & 0xF0, ch = (m.b[0] & 15) + 1;
                if (ch != p.trigCh || m.b[1] >= NSEG) continue;
                if (st == 0x90 && m.b[2] > 0) { trigNote = m.b[1]; trigAt = m.offset; trigStarted = false; }
                else if ((st == 0x80 || (st == 0x90 && m.b[2] == 0)) && m.b[1] == trigNote) { trigNote = -1; trigOffAt = m.offset; }
            }
        }

        pos = {};
        if (hostDrives) runHost(p, n, host, midiOut);
        else if (p.hostMode == "notes" && trigNote >= 0) runTrigger(p, n, host, midiOut);
        else if (internal) runInternal(p, n, midiOut);

        // note offs
        for (auto it = pendingOff.begin(); it != pendingOff.end();) {
            if (it->left < n) { midiOut.push_back({std::max(0, it->left), {(uint8_t) (0x80 | it->ch), (uint8_t) it->note, 0}}); it = pendingOff.erase(it); }
            else { it->left -= n; ++it; }
        }

        // audio: start voices at their sample, mix
        std::sort(trig.begin(), trig.end(), [](const Trig& a, const Trig& b) { return a.offset < b.offset; });
        size_t ti = 0;
        for (int i = 0; i < n; ++i) {
            while (ti < trig.size() && trig[ti].offset <= i) start(p, trig[ti++]);
            float y = 0;
            for (auto& v : voices) y += v.render();
            y = std::tanh(y * .9f);
            L[i] = y; R[i] = y;
        }
    }

private:
    struct Trig { int offset, s, l, u, mix; };
    struct Aud { int s, l, u; };
    struct Off { int left, ch, note; };

    void fire(int s, int l, int u, int mix, int offset, std::vector<MidiMsg>& midiOut) {
        const Project& p = *proj;
        const int ch = clampv(p.ch - 1, 0, 15), note = p.noteOf(s), vel = clampv((int) std::lround(l / 31.0 * 126) + 1, 1, 127);
        for (auto it = pendingOff.begin(); it != pendingOff.end(); ++it)
            if (it->ch == ch && it->note == note) { midiOut.push_back({offset, {(uint8_t) (0x80 | ch), (uint8_t) note, 0}}); pendingOff.erase(it); break; }
        midiOut.push_back({offset, {(uint8_t) (0x90 | ch), (uint8_t) note, (uint8_t) vel}});
        pendingOff.push_back({offset + (int) (sr * .03), ch, note});
        if (p.audio) trig.push_back({offset, s, l, u, mix});
    }
    void fireTick(const Project& p, int seg, int tick, int mix, int offset, std::vector<MidiMsg>& midiOut) {
        const Segment& g = p.seg[seg];
        if (!g.used || tick < 0 || tick >= (int) g.at.size()) return;
        for (int i : g.at[(size_t) tick]) { const Event& e = g.events[(size_t) i]; fire(e.s, e.l, e.u, mix, offset, midiOut); }
    }

    void runHost(const Project& p, int n, const Host& h, std::vector<MidiMsg>& midiOut) {
        const double ppqPerSample = h.bpm / 60.0 / sr, t0 = h.ppq * PPQ, t1 = (h.ppq + n * ppqPerSample) * PPQ;
        const bool song = p.hostMode == "song";
        for (long k = (long) std::ceil(t0 - 1e-9); k < t1; ++k) {
            if (k < 0) continue;
            const int off = clampv((int) ((k / (double) PPQ - h.ppq) / ppqPerSample), 0, n - 1);
            if (!song) {
                const Segment& g = p.seg[p.curSeg]; if (!g.used || g.ticks() <= 0) break;
                const int tk = (int) (k % g.ticks());
                fireTick(p, p.curSeg, tk, p.curMix, off, midiOut);
            } else {
                const SeqEntry* e = locate(seq, k); if (!e) continue;
                fireTick(p, e->seg, (int) (k - e->start), e->mix, off, midiOut);
            }
        }
        const long now = std::max(0L, (long) std::floor(t0));
        if (!song) { const Segment& g = p.seg[p.curSeg]; if (g.used && g.ticks() > 0) pos = {true, 1, p.curSeg, (int) (now % g.ticks()), -1}; }
        else if (const SeqEntry* e = locate(seq, now)) pos = {true, 2, e->seg, (int) (now - e->start), e->idx};
    }
    static const SeqEntry* locate(const std::vector<SeqEntry>& s, long k) {
        auto it = std::upper_bound(s.begin(), s.end(), k, [](long v, const SeqEntry& e) { return v < e.start; });
        if (it == s.begin()) return nullptr;
        --it;
        return it == s.end() ? nullptr : &*it;
    }

    // own clock: one tick every 60 / (tempo * 24) seconds
    void runInternal(const Project& p, int n, std::vector<MidiMsg>& midiOut) {
        int i = 0;
        while (internal && i < n) {
            int seg; double tempo; int mix, idx = -1;
            if (imode == 2) { if (ili >= (int) iseq.size()) { internal = false; break; } const auto& e = iseq[(size_t) ili]; seg = e.seg; tempo = e.tempo; mix = e.mix; idx = e.idx; }
            else { seg = iseg; tempo = p.seg[seg].tempo; mix = p.curMix; }
            const Segment& g = p.seg[seg];
            if (!g.used || g.ticks() <= 0) { internal = false; break; }
            if (iacc <= 0) {
                fireTick(p, seg, itick, mix, i, midiOut);
                pos = {true, imode, seg, itick, idx};
                iacc += sr * 60.0 / (tempo * PPQ);
                if (++itick >= g.ticks()) { itick = 0; if (imode == 2) ++ili; }
            }
            const int step = std::min(n - i, (int) std::ceil(iacc));
            iacc -= step; i += std::max(1, step);
        }
        if (internal && !pos.playing) {
            const int seg = imode == 2 && ili < (int) iseq.size() ? iseq[(size_t) ili].seg : iseg;
            pos = {true, imode, seg, std::max(0, itick - 1), imode == 2 && ili < (int) iseq.size() ? iseq[(size_t) ili].idx : -1};
        }
    }

    void runTrigger(const Project& p, int n, const Host& h, std::vector<MidiMsg>& midiOut) {
        const int seg = trigNote;
        const Segment& g = p.seg[seg];
        if (!g.used || g.ticks() <= 0) return;
        int i = 0;
        if (!trigStarted) { trigStarted = true; ttick = 0; tacc = 0; i = clampv(trigAt, 0, n - 1); }
        const double tempo = h.bpm > 0 ? h.bpm : g.tempo;
        while (i < n) {
            if (tacc <= 0) {
                fireTick(p, seg, ttick, p.curMix, i, midiOut);
                tacc += sr * 60.0 / (tempo * PPQ);
                if (++ttick >= g.ticks()) ttick = 0;
            }
            const int step = std::min(n - i, (int) std::ceil(tacc));
            tacc -= step; i += std::max(1, step);
        }
        pos = {true, 1, seg, std::max(0, ttick - 1), -1};
    }

    static Voice::Kind kindFor(const std::string& nm, double& f0, double& f1, double& len) {
        auto has = [&](const char* w) { return nm.find(w) != std::string::npos; };
        f1 = 0;
        if (has("kick")) { f0 = 140; f1 = 45; len = .45; return Voice::Kick; }
        if (has("snare")) { f0 = 220; f1 = 160; len = .2; return Voice::Snare; }
        if (has("clap") || has("snap")) { len = .18; return Voice::Clap; }
        if (has("hat op") || has("ride") || has("crash")) { len = has("crash") ? 1.2 : has("ride") ? .9 : .4; return Voice::Cymbal; }
        if (has("hat") || has("shaker") || has("tamb")) { len = has("shaker") ? .09 : .06; return Voice::Hat; }
        if (has("tom") || has("conga")) { const double f = has("lo") ? 95 : has("hi") ? 180 : has("conga") ? 260 : 130; f0 = f * 1.4; f1 = f; len = .35; return Voice::Tom; }
        if (has("rim") || has("clave") || has("wood")) { f0 = has("clave") ? 2500 : has("wood") ? 900 : 1700; len = .05; return Voice::Click; }
        if (has("cowbell")) { f0 = 560; len = .3; return Voice::Cowbell; }
        if (has("bass")) { f0 = 55; len = .6; return Voice::Bass; }
        if (has("stab") || has("hit")) { f0 = 220; len = .3; return Voice::Stab; }
        if (has("zap")) { f0 = 2000; f1 = 80; len = .2; return Voice::Zap; }
        f0 = 400; f1 = 300; len = .15; return Voice::Other;
    }

    void start(const Project& p, const Trig& tr) {
        const Sound& s = p.snd[tr.s];
        const double mixL = p.mix[tr.mix][tr.s] / 31.0, gain = std::pow(tr.l / 31.0, 1.5) * mixL * .8;
        if (gain <= .0005) return;
        const int tune = s.decayMode ? s.tune : s.tune + tr.u;
        const double decay = clampv(s.decayMode ? s.decay + tr.u * 2 : s.decay, 0, 31) / 31.0;
        const double rate = (SP_TUNE[clampv(tune, -16, 15) + 16] + 1) / 128.0;
        // the SP-1200 cuts a sound when it plays again
        Voice* v = nullptr;
        for (auto& x : voices) if (x.kind != Voice::None && x.sound == tr.s) { v = &x; break; }
        if (!v) for (auto& x : voices) if (x.kind == Voice::None) { v = &x; break; }
        if (!v) { v = &voices[0]; for (auto& x : voices) if (x.t > v->t) v = &x; }
        *v = Voice{};
        v->sound = tr.s; v->dt = 1.0 / sr; v->gain = gain; v->seed = 12345u + (uint32_t) tr.s * 7919u;
        if (s.sample && !s.sample->data.empty()) {
            const int N = (int) s.sample->data.size();
            v->kind = Voice::Smp; v->smp = s.sample;
            v->st = clampv(s.start, 0, N - 1); v->en = clampv(s.end > 0 ? s.end : N, v->st + 1, N);
            v->lo = s.loop >= v->st && s.loop < v->en - 1 ? s.loop : -1;
            v->pos = v->st; v->inc = s.sample->rate / sr * rate;
            double dur = (v->en - v->st) / s.sample->rate / rate;
            if (v->lo >= 0) dur = std::max(dur, 2.0);
            v->len = dur * (.06 + .94 * decay * decay);
            v->hold = decay < 1 || v->lo >= 0 ? v->len * .55 : v->len;
            return;
        }
        std::string nm = s.name; for (auto& c : nm) c = (char) std::tolower((unsigned char) c);
        double f0 = 100, f1 = 0, len = .2;
        v->kind = kindFor(nm, f0, f1, len);
        v->f0 = f0 * rate; v->f1 = (f1 > 0 ? f1 : f0) * rate; v->len = len * (.15 + .85 * decay);
    }

    double sr = 44100;
    const Project* proj = nullptr;
    std::vector<SeqEntry> seq, iseq;
    Voice voices[16];
    std::vector<Off> pendingOff;
    std::vector<Trig> trig;
    std::vector<Aud> auditions;
    Pos pos;
    bool internal = false; int imode = 1, iseg = 1, isong = 1, ili = 0, itick = 0; double iacc = 0;
    int trigNote = -1, trigAt = 0, trigOffAt = 0, ttick = 0; bool trigStarted = false; double tacc = 0;
};

} // namespace spq
