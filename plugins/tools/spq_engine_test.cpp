// SPQuencer1200 engine checks that need no plugin host: the editor's own play button (internal clock), songs with
// sub-songs, links, repeats and tempo steps, notes mode released mid-block, per-pad notes and the same-pad cut.
// Build: g++ -std=c++17 -O2 -I../spquencer spq_engine_test.cpp -o spq_engine_test && ./spq_engine_test
#include "Engine.h"
#include <cstdio>

using namespace spq;
static bool ok = true;
static void check(const char* what, bool good) { std::printf("  %-58s %s\n", what, good ? "ok" : "WRONG"); ok &= good; }

struct Rig {
    Engine e; Project p; double sr = 48000; int block = 480; long t = 0;
    std::vector<std::pair<long, int>> on;   // absolute sample, note
    Rig() { e.prepare(sr); }
    void commit() { for (auto& g : p.seg) if (g.used) g.index(); e.setProject(&p); }
    void run(double secs, Engine::Host h = {}, std::vector<std::pair<long, MidiMsg>> in = {}) {
        std::vector<float> L((size_t) block), R((size_t) block); std::vector<MidiMsg> out;
        for (long end = t + (long) (secs * sr); t < end; t += block) {
            std::vector<MidiMsg> mi;
            for (auto& m : in) if (m.first >= t && m.first < t + block) { MidiMsg x = m.second; x.offset = (int) (m.first - t); mi.push_back(x); }
            out.clear();
            if (h.playing) h.ppq = t / sr * h.bpm / 60.0;
            e.process(L.data(), R.data(), block, h, mi, out);
            for (auto& m : out) if ((m.b[0] & 0xF0) == 0x90) on.push_back({t + m.offset, m.b[1]});
        }
    }
};

static Segment seg(int bars, double tempo, std::vector<Event> ev) { Segment g; g.used = true; g.len = bars; g.tempo = tempo; g.events = ev; return g; }

int main() {
    std::puts("SPQuencer1200 engine");
    {   // internal clock: segment at its own tempo (120 BPM: a tick is 1000 samples), from a cursor tick
        Rig r; r.p.hostMode = "off"; r.p.seg[3] = seg(1, 120, {{0, 0}, {24, 1}, {48, 0}});
        r.commit(); r.e.play(1, 3, 0, 24); r.run(2.2);
        check("play button: starts at the cursor (tick 24)", r.on.size() >= 2 && r.on[0] == std::make_pair(0L, 37) && r.on[1] == std::make_pair(24000L, 36));
        check("play button: loops the segment (tick 0 after 96)", r.on.size() >= 3 && r.on[2] == std::make_pair(72000L, 36));
        r.e.stop(); const size_t n = r.on.size(); r.run(1);
        check("stop: no more notes", r.on.size() == n);
    }
    {   // internal song: repeat 2 x seg 1, tempo 60 step, sub-song 2 (seg 2), link to song 3 (seg 3), steps after a link never play
        Rig r; r.p.hostMode = "off";
        r.p.seg[1] = seg(1, 90, {{0, 0}}); r.p.seg[2] = seg(1, 90, {{0, 1}}); r.p.seg[3] = seg(1, 90, {{0, 2}});
        r.p.song[1] = {true, 120, {{"rep", 2}, {"seg", 1}, {"rend", 0}, {"tempo", 60}, {"sub", 2}, {"link", 3}, {"seg", 1}}};
        r.p.song[2] = {true, 90, {{"seg", 2}}}; r.p.song[3] = {true, 90, {{"seg", 3}}};
        r.commit();
        auto sq = songSequence(r.p, 1);
        check("song: rep x2, sub, link, nothing after the link", sq.size() == 4 && sq[0].seg == 1 && sq[1].seg == 1 && sq[2].seg == 2 && sq[3].seg == 3);
        check("song: tempo step carries into sub-song and link", sq.size() == 4 && sq[0].tempo == 120 && sq[2].tempo == 60 && sq[3].tempo == 60);
        r.e.play(2, 0, 1, 0); r.run(12);
        // 2 bars at 120 (96000 each), then bars at 60 BPM (192000 each)
        check("song: notes at bar starts, tempo change applied", r.on.size() == 4 && r.on[1].first == 96000 && r.on[2].first == 192000 && r.on[3].first == 384000);
        check("song: stops at the end", r.on.size() == 4);
    }
    {   // notes mode: note on at 1000 starts seg 5, note off at 13200 (same block as tick 12) still plays tick 12 (at 13000), nothing after
        Rig r; r.p.hostMode = "notes"; r.p.trigCh = 16; r.p.seg[5] = seg(1, 90, {{0, 0}, {12, 1}, {24, 2}});
        r.commit();
        MidiMsg onm{0, {0x9F, 5, 100}}, offm{0, {0x8F, 5, 0}};
        Engine::Host h; h.bpm = 120;
        r.run(1, h, {{1000, onm}, {13200, offm}});
        check("notes mode: release mid-block keeps notes before it", r.on.size() == 2 && r.on[0].first == 1000 && r.on[1].first == 13000);
    }
    {   // per-pad note, channel and velocity from level
        Rig r; r.p.hostMode = "off"; r.p.ch = 10; r.p.base = 36; r.p.snd[2].note = 42;
        r.p.seg[1] = seg(1, 120, {{0, 2, 31}}); r.commit();
        std::vector<float> L(480), R(480); std::vector<MidiMsg> out; r.e.play(1, 1, 0, 0);
        r.e.process(L.data(), R.data(), 480, {}, {}, out);
        check("own note 42 on channel 10, level 31 = velocity 127", !out.empty() && out[0].b[0] == 0x99 && out[0].b[1] == 42 && out[0].b[2] == 127);
        float pk = 0; for (float x : L) pk = std::max(pk, std::abs(x));
        check("preview sound plays", pk > .01f);
    }
    {   // the same pad again cuts its note: note off sent before the new note on
        Rig r; r.p.hostMode = "off"; r.p.seg[1] = seg(1, 250, {{0, 0}, {1, 0}}); r.commit();
        std::vector<float> L(4800), R(4800); std::vector<MidiMsg> out; r.e.play(1, 1, 0, 0);
        r.e.process(L.data(), R.data(), 4800, {}, {}, out);
        int ons = 0, offsBefore2nd = 0;
        for (auto& m : out) { if ((m.b[0] & 0xF0) == 0x90) ++ons; else if (ons == 1) ++offsBefore2nd; }
        check("retrigger: note off before the second note on", ons == 2 && offsBefore2nd == 1);
    }
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
