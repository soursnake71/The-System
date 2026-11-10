// write_midi against exact ground truth: files are written and parsed back with an independent reader (below), then
// every note's time is converted through the FILE's tempo map and compared with the input time (must be within half a
// tick), in tempo-map and constant-tempo mode, merged and split drums, 4/4 with tempo drift + a section change and 6/8.
// Also: lengths, channels, GM SysEx, tempo event count / values, time signatures, bar markers, the RPN pitch-bend
// range, program, bend values and resets, slide overlaps with portamento, accents. Writes build-groove/sample.mid.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include "ds/kit.h"
#include "ds/modules.h"
#include "groove_synth.h"

using namespace gsynth;
static int fails = 0, checks = 0;
static void check(const std::string& what, bool ok, const std::string& detail = "")
{
    ++checks;
    if (!ok) ++fails;
    std::printf("  %-58s %s %s\n", what.c_str(), ok ? "ok" : "FAIL", detail.c_str());
}
static std::string f2(const char* f, double a, double b = 0) { char x[200]; std::snprintf(x, sizeof x, f, a, b); return x; }

// ---------------------------------------------------------------- an independent SMF reader
struct MEv { long tick; uint8_t st; std::vector<uint8_t> d; };  // st: status byte (0xFF meta: d[0] = type)
struct MFile { int format = -1, ppq = 0; std::vector<std::vector<MEv>> tracks; };

static MFile read_midi(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    MFile m;
    size_t p = 0;
    auto u32 = [&](size_t q) { return (uint32_t) b[q] << 24 | (uint32_t) b[q + 1] << 16 | (uint32_t) b[q + 2] << 8 | b[q + 3]; };
    if (b.size() < 14 || std::string(b.begin(), b.begin() + 4) != "MThd") return m;
    m.format = b[8] << 8 | b[9];
    const int nt = b[10] << 8 | b[11];
    m.ppq = b[12] << 8 | b[13];
    p = 8 + u32(4);
    for (int t = 0; t < nt && p + 8 <= b.size(); ++t) {
        if (std::string(b.begin() + p, b.begin() + p + 4) != "MTrk") break;
        const size_t end = p + 8 + u32(p + 4);
        p += 8;
        std::vector<MEv> ev;
        long tick = 0;
        uint8_t run = 0;
        auto vlq = [&]() { uint32_t v = 0; uint8_t c; do { c = b[p++]; v = v << 7 | (c & 0x7F); } while (c & 0x80); return v; };
        while (p < end) {
            tick += vlq();
            uint8_t st = b[p];
            if (st & 0x80) ++p; else st = run;
            MEv e{tick, st, {}};
            if (st == 0xFF) {
                const uint8_t type = b[p++];
                const uint32_t len = vlq();
                e.d.push_back(type);
                e.d.insert(e.d.end(), b.begin() + p, b.begin() + p + len);
                p += len;
            } else if (st == 0xF0 || st == 0xF7) {
                const uint32_t len = vlq();
                e.d.insert(e.d.end(), b.begin() + p, b.begin() + p + len);
                p += len;
            } else {
                run = st;
                const int n = (st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0 ? 1 : 2;
                e.d.insert(e.d.end(), b.begin() + p, b.begin() + p + n);
                p += n;
            }
            ev.push_back(std::move(e));
        }
        p = end;
        m.tracks.push_back(std::move(ev));
    }
    return m;
}

struct FileTempo {
    std::vector<std::pair<long, long>> t;  // tick, us per quarter
    int ppq;
    long double time(long tick) const
    {
        long double s = 0;
        for (size_t i = 0; i < t.size(); ++i) {
            const long a = t[i].first, b = i + 1 < t.size() ? std::min(t[i + 1].first, tick) : tick;
            if (b <= a) break;
            s += (long double) (b - a) * t[i].second / ppq * 1e-6L;
        }
        return s;
    }
    double half_tick(long tick) const
    {
        long us = t.empty() ? 500000 : t[0].second;
        for (auto& x : t) if (x.first <= tick) us = x.second;
        return 0.5 * us / ppq * 1e-6;
    }
};

static std::string meta_text(const MEv& e) { return std::string(e.d.begin() + 1, e.d.end()); }

struct Note { long on, off; int ch, note, vel; size_t track; };
static std::vector<Note> notes_of(const MFile& m, bool& overlap_same_note)
{
    std::vector<Note> out;
    overlap_same_note = false;
    for (size_t t = 0; t < m.tracks.size(); ++t) {
        std::map<int, size_t> open;  // ch*128+note -> index
        for (auto& e : m.tracks[t]) {
            const int ty = e.st & 0xF0, ch = e.st & 0x0F;
            if (ty != 0x80 && ty != 0x90) continue;
            const int key = ch * 128 + e.d[0];
            if (ty == 0x90 && e.d[1] > 0) {
                if (open.count(key)) overlap_same_note = true;
                open[key] = out.size();
                out.push_back({e.tick, -1, ch, e.d[0], e.d[1], t});
            } else if (open.count(key)) {
                out[open[key]].off = e.tick;
                open.erase(key);
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.on < b.on; });
    return out;
}

// ---------------------------------------------------------------- one file
static void verify(const std::string& name, const std::string& path, const Song& s, const ds::BeatGrid& g,
                   const std::vector<ds::BassNote>& bass, const nlohmann::json& groove, ds::MidiOptions o)
{
    std::printf("%s\n", name.c_str());
    ds::write_midi(path, s.drums, bass, g, groove, o);
    const MFile m = read_midi(path);
    check("header: format 1, 960 PPQ", m.format == 1 && m.ppq == 960);
    std::map<int, int> pieces;
    for (auto& h : s.drums) pieces[h.note]++;
    const size_t want_tracks = 1 + (o.split_drums ? pieces.size() : 1) + (bass.empty() ? 0 : 1);
    check("track count", m.tracks.size() == want_tracks, f2("%.0f vs %.0f", (double) m.tracks.size(), (double) want_tracks));
    if (m.tracks.size() != want_tracks) return;

    // conductor
    FileTempo ft;
    ft.ppq = m.ppq;
    int sysex = 0, markers = 0, texts = 0, copyright = 0;
    std::vector<std::pair<long, std::vector<uint8_t>>> sigs;
    std::vector<long> marker_ticks;
    bool markers_named = true;
    for (auto& e : m.tracks[0]) {
        if (e.st == 0xF0 && e.d == std::vector<uint8_t>{0x7E, 0x7F, 0x09, 0x01, 0xF7}) ++sysex;
        if (e.st != 0xFF) continue;
        if (e.d[0] == 0x51) ft.t.push_back({e.tick, (long) e.d[1] << 16 | (long) e.d[2] << 8 | e.d[3]});
        if (e.d[0] == 0x58) sigs.push_back({e.tick, std::vector<uint8_t>(e.d.begin() + 1, e.d.end())});
        if (e.d[0] == 0x06) { ++markers; marker_ticks.push_back(e.tick); markers_named &= meta_text(e) == "Bar " + std::to_string(markers); }
        if (e.d[0] == 0x01) ++texts;
        if (e.d[0] == 0x02 && meta_text(e) == "Drum Scribe") ++copyright;
    }
    check("GM System On SysEx F0 7E 7F 09 01 F7", sysex == 1);
    check("copyright 'Drum Scribe' + text events", copyright == 1 && texts >= (groove.empty() ? 1 : 5), f2("%.0f texts", texts));
    const bool pre = g.beats[0] >= 0.5 * (g.beats[1] - g.beats[0]);  // pre-roll beat (else beat 0 stretched to t = 0)
    const size_t want_tempi = o.tempo_map ? g.beats.size() - 1 + (pre ? 1 : 0) : 1;
    check("tempo events count", ft.t.size() == want_tempi, f2("%.0f vs %.0f", (double) ft.t.size(), (double) want_tempi));
    check("bar markers 'Bar N', one per downbeat", markers == (int) g.downbeats.size() && markers_named,
          f2("%.0f vs %.0f", markers, (double) g.downbeats.size()));
    const bool compound = g.beats_per_bar == 2;
    if (o.tempo_map) {
        // beats must sit on whole beats of the file, at their exact times; tempo values must be the beat lengths
        const long TPB = compound ? 1440 : 960, off = pre ? TPB : 0;
        double worst = 0, worst_tempo = 0;
        for (size_t i = pre ? 0 : 1; i < g.beats.size(); ++i) worst = std::max(worst, (double) std::abs(ft.time(off + (long) i * TPB) - (long double) g.beats[i]));
        for (size_t i = pre ? 0 : 1; i + 1 < g.beats.size(); ++i) {
            const double want_us = (g.beats[i + 1] - g.beats[i]) * 1e6 * 960 / TPB;
            worst_tempo = std::max(worst_tempo, std::abs(ft.t[i + (pre ? 1 : 0)].second - want_us));
        }
        check(pre ? "pre-roll beat for [0, first beat)" : "beat 0 stretched back to t=0 (gap < half a beat)",
              pre ? ft.t[0].first == 0 && std::abs(ft.t[0].second - g.beats[0] * 1e6 * 960 / TPB) <= 0.5
                  : std::abs((double) ft.time(TPB) - g.beats[1]) < 1e-6);
        check("tempo map: every beat at its tracked time (< 1 us)", worst < 1e-6, f2("worst %.3f us", worst * 1e6));
        check("tempo values = beat lengths (+-1 us)", worst_tempo <= 1.0, f2("worst %.3f us", worst_tempo));
        bool mk = true;
        for (size_t j = 0; j < marker_ticks.size(); ++j) mk &= marker_ticks[j] == off + (long) g.downbeats[j] * TPB;
        check("markers exactly on the downbeats", mk);
        const int num = compound ? 6 : g.beats_per_bar * 1, den = compound ? 3 : 2;
        bool sig = false;
        for (auto& [tk, d] : sigs) if (tk == off + (long) g.downbeats[0] * TPB && d[0] == num && d[1] == den) sig = true;
        check(compound ? "time signature 6/8 at bar 1" : "time signature at bar 1", sig);
    } else {
        check("one time signature", sigs.size() == 1);
        const double bpm = o.fixed_bpm > 0 ? o.fixed_bpm : g.mean_bpm;
        check("constant tempo value", ft.t.size() == 1 && std::abs(ft.t[0].second - 60e6 / bpm / (compound ? 1.5 : 1)) <= 0.5);
    }

    // drums
    bool overlap = false;
    const std::vector<Note> fn = notes_of(m, overlap);
    std::vector<Note> fd, fb;
    for (auto& n : fn) (n.ch == 9 ? fd : fb).push_back(n);
    std::vector<ds::DrumHit> hd = s.drums;
    std::stable_sort(hd.begin(), hd.end(), [](const ds::DrumHit& a, const ds::DrumHit& b) { return a.time < b.time; });
    check("drum note count, all on channel 10", fd.size() == hd.size() && fd.size() + fb.size() == fn.size(),
          f2("%.0f vs %.0f", (double) fd.size(), (double) hd.size()));
    check("no same-note overlap (note-offs before note-ons)", !overlap);
    if (fd.size() == hd.size()) {
        std::vector<bool> used(fd.size(), false);
        double worst = 0, worst_len = 0;
        int bad = 0, unmatched = 0, badvel = 0, badtrack = 0;
        for (auto& h : hd) {
            // the file note of this pitch nearest in time
            long double bd = 1e9;
            int bi = -1;
            for (size_t j = 0; j < fd.size(); ++j) {
                if (used[j] || fd[j].note != h.note) continue;
                const long double d = std::abs(ft.time(fd[j].on) - (long double) h.time);
                if (d < bd) { bd = d; bi = (int) j; }
                if (ft.time(fd[j].on) > h.time + 1) break;
            }
            if (bi < 0) { ++unmatched; continue; }
            used[bi] = true;
            const Note& n = fd[bi];
            const double ht = ft.half_tick(n.on);
            worst = std::max(worst, (double) bd / ht);
            if (bd > ht + 1e-9) ++bad;
            if (n.vel != h.velocity) ++badvel;
            // length: max(end, t + 10 ms), clipped at the next hit of the same note
            double end = std::max(h.end, h.time + 0.010);
            for (auto& x : hd) if (x.note == h.note && x.time > h.time) { end = std::min(end, x.time); break; }
            const double got = (double) ft.time(n.off);
            worst_len = std::max(worst_len, std::abs(got - end) / (2 * ft.half_tick(n.off)));
            if (o.split_drums && m.tracks[n.track].size() && meta_text(m.tracks[n.track][0]) != ds::label_of(h.note)) ++badtrack;
        }
        check("every drum note within half a tick of its time", bad == 0 && unmatched == 0,
              f2("worst %.3f of half a tick", worst) + f2(", %.0f unmatched", unmatched));
        check("drum note lengths (end within half a tick)", worst_len <= 0.5 + 1e-9, f2("worst %.3f of a tick", worst_len));
        check("drum velocities", badvel == 0);
        if (o.split_drums) check("split: one named track per kit piece", badtrack == 0);
    }

    // bass
    if (!bass.empty()) {
        const auto& bt = m.tracks.back();
        bool name = !bt.empty() && bt[0].st == 0xFF && meta_text(bt[0]) == "Bass";
        int prog = -1;
        std::vector<std::pair<int, int>> ccs0;
        for (auto& e : bt) {
            if (e.tick != 0) break;
            if (e.st == 0xC1) prog = e.d[0];
            if (e.st == 0xB1) ccs0.push_back({e.d[0], e.d[1]});
        }
        check("bass track name, program 38 on channel 2", name && prog == 38);
        const std::vector<std::pair<int, int>> rpn = {{101, 0}, {100, 0}, {6, 12}, {38, 0}, {101, 127}, {100, 127}};
        check("RPN 0 pitch-bend range = 12 semitones (then RPN null)", ccs0.size() >= 6 && std::equal(rpn.begin(), rpn.end(), ccs0.begin()));
        check("bass note count", fb.size() == bass.size(), f2("%.0f vs %.0f", (double) fb.size(), (double) bass.size()));
        if (fb.size() == bass.size()) {
            int bad = 0, badvel = 0, badslide = 0, badch = 0, slides = 0, ties = 0;
            double worst = 0;
            for (size_t i = 0; i < bass.size(); ++i) {
                const auto& n = bass[i];
                const Note& f = fb[i];
                const double ht = ft.half_tick(f.on), d = std::abs((double) ft.time(f.on) - n.time);
                worst = std::max(worst, d / ht);
                if (d > ht + 1e-9 || f.note != n.note) ++bad;
                if (f.ch != 1) ++badch;
                if (f.vel != std::min(127, n.velocity + (n.accent ? 20 : 0))) ++badvel;
                if (n.slide_to_next && i + 1 < bass.size()) {
                    const double ov = (double) (ft.time(f.off) - ft.time(fb[i + 1].on));
                    if (bass[i + 1].note == n.note) { if (f.off != fb[i + 1].on) ++badslide; ++ties; }  // same pitch: tie
                    else if (!(f.off > fb[i + 1].on) || std::abs(ov - 0.005) > 2 * ht + 1e-9) {
                        ++badslide;
                        std::printf("    slide %zu: off %ld next on %ld overlap %.3f ms\n", i, f.off, fb[i + 1].on, ov * 1000);
                    }
                    ++slides;
                }
            }
            check("bass notes within half a tick, right pitch, channel 2", bad == 0 && badch == 0, f2("worst %.3f of half a tick", worst));
            check("bass accents = velocity + 20", badvel == 0);
            check("slides overlap the next note by 5 ms (same pitch: tie)", badslide == 0, f2("%.0f slides, %.0f ties", slides, ties));
        }
        // bends: replay the bend / CC65 stream and check each note's start value, its in-note bends and the resets
        int state = 8192, porta = 0, badbend = 0, badporta = 0, bends = 0, resets_ok = 1;
        size_t ni = 0;
        for (auto& e : bt) {
            if (e.st == 0xE1) { state = e.d[0] | e.d[1] << 7; ++bends; }
            if (e.st == 0xB1 && e.d[0] == 65) porta = e.d[1];
            if (e.st == 0x91 && e.d[1] > 0 && ni < bass.size()) {
                const auto& n = bass[ni];
                const int want = std::max(0, std::min(16383, (int) std::lround(8192 + n.cents / 100.0 / 12 * 8192)));
                if (state != want) ++badbend;
                const bool sliding_in = ni > 0 && bass[ni - 1].slide_to_next;
                if ((n.slide_to_next || sliding_in) != (porta == 127)) ++badporta;
                ++ni;
            }
        }
        // in-note bend values: every BassNote bend must appear with its 14-bit value at its tick
        int missing = 0;
        for (size_t i = 0; i < bass.size(); ++i)
            for (auto& pb : bass[i].bends) {
                const int v = std::max(0, std::min(16383, (int) std::lround(8192 + pb.semitones / 12 * 8192)));
                bool found = false;
                for (auto& e : bt)
                    if (e.st == 0xE1 && (e.d[0] | e.d[1] << 7) == v &&
                        std::abs((double) ft.time(e.tick) - pb.time) <= ft.half_tick(e.tick) + 1e-9) found = true;
                missing += !found;
            }
        // every non-slide note with a bend ends with a reset to centre at its note-off tick
        for (size_t i = 0; i < bass.size() && i < fb.size(); ++i) {
            if ((bass[i].slide_to_next && i + 1 < bass.size()) || (bass[i].bends.empty() && bass[i].cents == 0)) continue;
            bool ok = false;
            for (auto& e : bt) if (e.st == 0xE1 && e.tick == fb[i].off && (e.d[0] | e.d[1] << 7) == 8192) ok = true;
            if (!ok) std::printf("    no reset after bass note %zu (off tick %ld, cents %.0f, %zu bends)\n", i, fb[i].off, bass[i].cents, bass[i].bends.size());
            resets_ok &= ok;
        }
        check("bend at each note-on = held cents (14-bit)", badbend == 0, f2("%.0f bend events", bends));
        check("in-note bends at their times with their values", missing == 0, f2("%.0f missing", missing));
        check("bend reset to centre after each bent note", resets_ok == 1);
        check("CC65 portamento on exactly around slides", badporta == 0 && porta == 0);
    }
}

int main()
{
    // a 4/4 band with drift then a sudden section change, human jitter, tracked grid
    Opts a;
    a.bpm = [](int i) { return i < 64 ? 118 + 4.0 * i / 64 : 136.0; };
    a.jitter_ms = 6; a.swing8 = 0.56; a.lag_ms = {{"snare", 6.0}}; a.bass_kick_ms = 4; a.seed = 31; a.t0 = 0.613;
    const Song s = make_song(a);
    const ds::BeatGrid g = ds::track_beats(s.drums, s.bass, s.duration);
    const nlohmann::json groove = ds::analyze_groove(s.drums, s.bass, g, s.duration);
    // a bass line with held cents, bends (absolute times), accents and slide chains
    std::vector<ds::BassNote> bass = s.bass;
    for (size_t i = 0; i < bass.size(); ++i) {
        auto& n = bass[i];
        if (i % 5 == 1) n.cents = -23;
        if (i % 7 == 2) n.accent = true;
        if (i % 9 == 3 || i % 9 == 4) n.slide_to_next = true;  // two-note slide chains
        if (i % 6 == 5) {
            const double d = n.end - n.time;
            n.bends = {{n.time + 0.25 * d, 0.5}, {n.time + 0.5 * d, 2.0}, {n.time + 0.75 * d, -1.25}};
        }
    }
    bass.back().slide_to_next = false;

    const std::string dir = "/home/user/The-System/build-groove/";
    for (int mode = 0; mode < 4; ++mode) {
        ds::MidiOptions o;
        o.tempo_map = mode < 2;
        o.split_drums = mode % 2 == 1;
        if (mode == 3) o.fixed_bpm = 127.5;
        verify(std::string("4/4 drift + change, tempo_map=") + (o.tempo_map ? "on" : "off") + ", split=" + (o.split_drums ? "on" : "off") +
                   (o.fixed_bpm > 0 ? ", fixed 127.5" : ""),
               dir + "test_midi_" + std::to_string(mode) + ".mid", s, g, bass, groove, o);
    }
    // 6/8 (two dotted-quarter beats per bar), true grid
    Opts c;
    c.style = Style::sixeight; c.bpm = [](int) { return 64.0; }; c.jitter_ms = 4; c.seed = 32;
    const Song s68 = make_song(c);
    for (int mode = 0; mode < 2; ++mode) {
        ds::MidiOptions o;
        o.tempo_map = mode == 0;
        verify(std::string("6/8, tempo_map=") + (o.tempo_map ? "on" : "off"), dir + "test_midi_68_" + std::to_string(mode) + ".mid",
               s68, s68.grid(), s68.bass, nlohmann::json::object(), o);
    }
    // a meter change: bar 9 is cut to 2 beats -> 2/4 at bar 9, back to 4/4 at bar 10
    {
        std::printf("meter change (a 2-beat bar)\n");
        ds::BeatGrid g2 = g;
        for (size_t j = 9; j < g2.downbeats.size(); ++j) g2.downbeats[j] -= 2;
        const std::string p = dir + "test_midi_meter.mid";
        ds::MidiOptions o;
        ds::write_midi(p, s.drums, {}, g2, nlohmann::json::object(), o);
        const MFile m = read_midi(p);
        const long off = g2.beats[0] >= 0.5 * (g2.beats[1] - g2.beats[0]) ? 960 : 0;
        std::vector<std::pair<long, int>> sigs;
        for (auto& e : m.tracks[0]) if (e.st == 0xFF && e.d[0] == 0x58) sigs.push_back({e.tick, e.d[1]});
        bool ok = false;
        for (size_t k = 0; k + 1 < sigs.size(); ++k)
            if (sigs[k].first == off + 960L * g2.downbeats[8] && sigs[k].second == 2 &&
                sigs[k + 1].first == off + 960L * g2.downbeats[9] && sigs[k + 1].second == 4) ok = true;
        check("time signature 2/4 at bar 9, 4/4 again at bar 10", ok, f2("%.0f time signatures", (double) sigs.size()));
    }
    // the sample file: tempo map, split drums, groove summary
    ds::MidiOptions o;
    o.split_drums = true;
    o.title = "Drum Scribe sample";
    ds::write_midi(dir + "sample.mid", s.drums, bass, g, groove, o);
    std::printf("wrote %ssample.mid\n%d/%d checks passed\n", dir.c_str(), checks - fails, checks);
    return fails ? 1 : 0;
}
