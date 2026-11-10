// Standard MIDI File (format 1, 960 PPQ) writer, from scratch.
//
// Timing: every note is placed at its original (unquantized) time. Seconds -> ticks always goes THROUGH THE TEMPO MAP
// THAT IS WRITTEN (piecewise integration of the integer microsecond tempi), so a DAW that honours the file's tempo puts
// each note within half a tick of the played time, in both modes:
//   tempo_map = true   a set_tempo at every beat; beat i sits exactly on tick off + i * TPB (TPB = 960, or 1440 for
//                      6/8 where the beat is a dotted quarter). The integer tempi are chosen by error diffusion so the
//                      written beat times never drift from the tracked ones (|error| < 1 us). The stretch before the
//                      first beat is one pre-roll beat; beats before the first downbeat make a pickup bar. Bars whose
//                      length differs get their own time signature.
//   tempo_map = false  one tempo (fixed_bpm, else the grid's mean), notes converted with that same integer tempo.
// Conductor: track name, copyright + text "Drum Scribe", GM System On SysEx, time signature(s), tempo(s), a marker
// "Bar N" at each bar, the groove summary as text events. Drums: channel 10, lengths from DrumHit.end (>= 10 ms,
// clipped at the same note's next hit), optionally one track per kit piece. Bass: channel 2, program 38 (Synth Bass 1),
// RPN 0 = +-12 semitones, 14-bit pitch bends (held cents + BassNote.bends, relative to the note), reset to centre after
// each note, slides: the note overlaps the next by 5 ms (legato) inside CC65 portamento on / CC5 time, off after.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include "ds/kit.h"
#include "ds/modules.h"
#include "midi_writer.h"

namespace ds {
namespace midi {

long TempoMap::ticks(double t) const
{
    if (segs.empty()) return 0;
    if (t <= 0) return 0;
    size_t i = std::upper_bound(segs.begin(), segs.end(), t, [](double v, const Seg& s) { return v < s.time; }) - segs.begin();
    if (i == 0) i = 1;
    const Seg& s = segs[i - 1];
    return s.tick + (long) std::llround((t - s.time) * 1e6 / s.us * ppq);
}

double TempoMap::time(long tick) const
{
    if (segs.empty()) return 0;
    size_t i = std::upper_bound(segs.begin(), segs.end(), tick, [](long v, const Seg& s) { return v < s.tick; }) - segs.begin();
    if (i == 0) i = 1;
    const Seg& s = segs[i - 1];
    return s.time + (double) (tick - s.tick) * s.us / ppq * 1e-6;
}

void TempoMap::add(long tick, long us)
{
    us = std::max(1L, std::min(0xFFFFFFL, us));
    if (!segs.empty()) {
        const Seg& p = segs.back();
        num += (int64_t) (tick - p.tick) * p.us;  // written time so far, in us * ticks-per-quarter units (exact)
    }
    segs.push_back({tick, (double) num / ppq * 1e-6, us});
}

}  // namespace midi

namespace {
using midi::TempoMap;

struct Ev {
    long tick;
    int prio;  // order at equal ticks: 0 note-off, 1 resets after notes, 2 setup before notes, 3 note-on, 4 in-note
    long seq;
    std::vector<uint8_t> data;  // complete event bytes (status included; meta = FF type len data)
};

struct Track {
    std::vector<Ev> ev;
    long seq = 0;
    void add(long tick, int prio, std::vector<uint8_t> d) { ev.push_back({std::max(0L, tick), prio, seq++, std::move(d)}); }
    void meta(long tick, int prio, uint8_t type, const std::string& s)
    {
        std::vector<uint8_t> d = {0xFF, type};
        midi::vlq(d, (uint32_t) s.size());
        d.insert(d.end(), s.begin(), s.end());
        add(tick, prio, d);
    }
};

std::vector<uint8_t> chunk(Track& t)
{
    std::stable_sort(t.ev.begin(), t.ev.end(), [](const Ev& a, const Ev& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.prio != b.prio ? a.prio < b.prio : a.seq < b.seq;
    });
    std::vector<uint8_t> body;
    long now = 0;
    for (auto& e : t.ev) {
        const long d = e.tick - now;
        if (d < 0) throw std::logic_error("midi: negative delta");
        midi::vlq(body, (uint32_t) d);
        body.insert(body.end(), e.data.begin(), e.data.end());
        now = e.tick;
    }
    midi::vlq(body, 0);
    body.insert(body.end(), {0xFF, 0x2F, 0x00});
    std::vector<uint8_t> c = {'M', 'T', 'r', 'k'};
    const uint32_t n = (uint32_t) body.size();
    c.insert(c.end(), {(uint8_t) (n >> 24), (uint8_t) (n >> 16), (uint8_t) (n >> 8), (uint8_t) n});
    c.insert(c.end(), body.begin(), body.end());
    return c;
}

int bend14(double semis) { return std::max(0, std::min(16383, (int) std::lround(8192 + semis / 12.0 * 8192))); }

void time_sig(Track& t, long tick, int beats, bool compound)
{
    // numerator, log2(denominator), MIDI clocks per metronome click, 32nds per quarter
    if (compound) t.add(tick, 2, {0xFF, 0x58, 4, (uint8_t) (3 * beats), 3, 36, 8});
    else t.add(tick, 2, {0xFF, 0x58, 4, (uint8_t) beats, 2, 24, 8});
}

void tempo_ev(Track& t, long tick, long us)
{
    t.add(tick, 2, {0xFF, 0x51, 3, (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us});
}

}  // namespace

void write_midi(const std::string& path, const std::vector<DrumHit>& drums, const std::vector<BassNote>& bass,
                const BeatGrid& grid, const nlohmann::json& groove, const MidiOptions& o)
{
    using midi::kPPQ;
    const int bpb = std::max(1, grid.beats_per_bar);
    bool compound = bpb == 2;
    if (groove.is_object() && groove.contains("meter") && groove["meter"].contains("compound") &&
        groove["meter"]["compound"].is_boolean())
        compound = groove["meter"]["compound"].get<bool>() && bpb == 2;
    const long TPB = compound ? kPPQ * 3 / 2 : kPPQ;  // ticks per beat
    const double q_per_beat = (double) TPB / kPPQ;

    TempoMap tm;
    Track cond;
    cond.meta(0, 0, 0x03, o.title);
    cond.meta(0, 0, 0x02, "Drum Scribe");
    cond.meta(0, 0, 0x01, "Drum Scribe: unquantized drum + bass transcription (notes at their played times)");
    cond.add(0, 1, {0xF0, 0x05, 0x7E, 0x7F, 0x09, 0x01, 0xF7});  // GM System On
    if (groove.is_object() && !groove.empty()) {
        std::istringstream ss(groove_text(groove));
        std::string line;
        while (std::getline(ss, line))
            if (!line.empty()) cond.meta(0, 1, 0x01, line);
    }

    const auto& B = grid.beats;
    const bool use_map = o.tempo_map && B.size() >= 2;
    if (use_map) {
        // [0, first beat): when it is at least half a beat long it becomes a pre-roll beat of its own (tempo within
        // 1x..2x of the band's); a shorter gap is absorbed by stretching beat 0 back to time 0 (tick 0 = beat 0), so
        // no absurd tempo is ever written. Then a tempo per beat with error diffusion.
        long off = 0;
        if (B[0] >= 0.5 * (B[1] - B[0])) {
            off = TPB;
            tm.add(0, std::lround(B[0] * 1e6 / q_per_beat));
        }
        for (size_t i = 0; i + 1 < B.size(); ++i) {
            const long tick = off + (long) i * TPB;
            // written time at this tick (us * ppq), then the tempo that best reaches the next beat
            int64_t num = tm.num;
            if (!tm.segs.empty()) num += (int64_t) (tick - tm.segs.back().tick) * tm.segs.back().us;
            const double target = B[i + 1] * 1e6 * kPPQ;  // us * ppq
            const long us = std::lround((target - (double) num) / TPB);
            tm.add(tick, us);
        }
        for (auto& s : tm.segs) tempo_ev(cond, s.tick, s.us);
        // bars: pickup, then one per downbeat, a time signature wherever the bar length changes
        std::vector<int> db = grid.downbeats;
        if (db.empty()) for (int i = 0; i < (int) B.size(); i += bpb) db.push_back(i);
        const int pickup = db[0] + (off ? 1 : 0);
        int cur = -1;
        if (pickup > 0) { time_sig(cond, 0, pickup, compound); cur = pickup; }
        for (size_t j = 0; j < db.size(); ++j) {
            const int len = j + 1 < db.size() ? db[j + 1] - db[j] : bpb;
            const long tick = off + (long) db[j] * TPB;
            if (len != cur) { time_sig(cond, tick, len, compound); cur = len; }
            cond.meta(tick, 3, 0x06, "Bar " + std::to_string(j + 1));
        }
    } else {
        double bpm = o.fixed_bpm > 0 ? o.fixed_bpm : grid.mean_bpm > 0 ? grid.mean_bpm : 120;
        const long us = std::lround(60e6 / bpm / q_per_beat);
        tm.add(0, us);
        tempo_ev(cond, 0, us);
        time_sig(cond, 0, bpb, compound);
        for (size_t j = 0; j < grid.downbeats.size(); ++j) {
            const int i = grid.downbeats[j];
            if (i >= 0 && i < (int) B.size()) cond.meta(tm.ticks(B[i]), 3, 0x06, "Bar " + std::to_string(j + 1));
        }
    }

    std::vector<Track> tracks;
    tracks.push_back(std::move(cond));

    // ---------------------------------------------------------------- drums
    if (o.include_drums && !drums.empty()) {
        struct N { double t, end; int note, vel; };
        std::vector<N> notes;
        for (auto& h : drums) {
            const int n = h.note > 0 ? h.note : note_of(h.piece);
            if (n <= 0 || n > 127) continue;
            notes.push_back({h.time, h.end, n, std::max(1, std::min(127, h.velocity > 0 ? h.velocity : 90))});
        }
        std::stable_sort(notes.begin(), notes.end(), [](const N& a, const N& b) { return a.t < b.t; });
        std::vector<int> order;  // track per note number
        for (auto& kp : kit_pieces())
            for (auto& n : notes) if (n.note == kp.note) { order.push_back(kp.note); break; }
        for (auto& n : notes)
            if (std::find(order.begin(), order.end(), n.note) == order.end()) order.push_back(n.note);
        std::map<int, size_t> track_of;
        if (o.split_drums) {
            for (int n : order) {
                track_of[n] = tracks.size();
                Track t;
                const std::string lab = label_of(n);
                t.meta(0, 0, 0x03, lab == "?" ? "Drum note " + std::to_string(n) : lab);
                tracks.push_back(std::move(t));
            }
        } else {
            Track t;
            t.meta(0, 0, 0x03, "Drums");
            for (int n : order) track_of[n] = tracks.size();
            tracks.push_back(std::move(t));
        }
        for (int nn : order) {
            std::vector<N*> same;
            for (auto& n : notes) if (n.note == nn) same.push_back(&n);
            Track& t = tracks[track_of[nn]];
            for (size_t k = 0; k < same.size(); ++k) {
                const N& n = *same[k];
                const long on = tm.ticks(n.t);
                const long next_on = k + 1 < same.size() ? tm.ticks(same[k + 1]->t) : -1;
                if (next_on == on) {  // the same note twice on one tick: keep the louder one
                    same[k + 1]->vel = std::max(same[k + 1]->vel, n.vel);
                    continue;
                }
                double end = std::max(n.end, n.t + 0.010);
                if (k + 1 < same.size()) end = std::min(end, same[k + 1]->t);
                long off = std::max(on + 1, tm.ticks(end));
                if (next_on >= 0) off = std::min(off, next_on);
                t.add(on, 3, {0x99, (uint8_t) nn, (uint8_t) n.vel});
                t.add(off, 0, {0x89, (uint8_t) nn, 0});
            }
        }
    }

    // ---------------------------------------------------------------- bass
    if (o.include_bass && !bass.empty()) {
        Track t;
        const uint8_t ch = 1;
        t.meta(0, 0, 0x03, "Bass");
        t.add(0, 1, {(uint8_t) (0xC0 | ch), midi::kBassProgram});
        for (auto cc : std::vector<std::pair<int, int>>{{101, 0}, {100, 0}, {6, 12}, {38, 0}, {101, 127}, {100, 127}})
            t.add(0, 1, {(uint8_t) (0xB0 | ch), (uint8_t) cc.first, (uint8_t) cc.second});
        auto bend = [&](long tick, int prio, int v) {
            t.add(tick, prio, {(uint8_t) (0xE0 | ch), (uint8_t) (v & 0x7F), (uint8_t) ((v >> 7) & 0x7F)});
        };
        auto cc = [&](long tick, int prio, int c, int v) { t.add(tick, prio, {(uint8_t) (0xB0 | ch), (uint8_t) c, (uint8_t) v}); };
        std::vector<BassNote> ns = bass;
        std::stable_sort(ns.begin(), ns.end(), [](const BassNote& a, const BassNote& b) { return a.time < b.time; });
        int state = 8192;
        bool porta = false;
        for (size_t i = 0; i < ns.size(); ++i) {
            const BassNote& n = ns[i];
            if (n.note < 0 || n.note > 127) continue;
            const BassNote* nx = i + 1 < ns.size() ? &ns[i + 1] : nullptr;
            const int vel = std::max(1, std::min(127, (n.velocity > 0 ? n.velocity : 90) + (n.accent ? midi::kAccentBoost : 0)));
            double end = std::max(n.end > n.time ? n.end : n.time + 0.1, n.time + 0.010);
            // a slide into the same pitch cannot overlap (the next note-on would be cut by this note-off): it is
            // written as a tie, ending exactly where the next note starts
            const bool slide = n.slide_to_next && nx && nx->time > n.time && nx->note != n.note;
            if (slide) end = nx->time + midi::kSlideOverlap;
            else if (n.slide_to_next && nx) end = std::max(nx->time, n.time + 1e-4);  // tie into the same pitch
            else if (nx) end = std::min(end, std::max(nx->time, n.time + 1e-4));
            const long on = tm.ticks(n.time);
            long off = std::max(on + 1, tm.ticks(end));
            if (!slide && nx) off = std::min(off, std::max(on + 1, tm.ticks(nx->time)));
            // the held pitch: cents off the note, set just before the note-on
            const int v0 = bend14(n.cents / 100.0);
            if (v0 != state) { bend(on, 2, v0); state = v0; }
            if ((slide || (n.slide_to_next && nx)) && !porta) { cc(on, 2, 5, midi::kPortaTime); cc(on, 2, 65, 127); porta = true; }
            t.add(on, 3, {(uint8_t) (0x90 | ch), (uint8_t) n.note, (uint8_t) vel});
            for (auto& pb : n.bends) {
                const double bt = pb.time < n.time - 1e-6 ? n.time + pb.time : pb.time;  // absolute, or relative to the onset
                if (bt > end) continue;
                const int v = bend14(n.cents / 100.0 + pb.semitones);  // bends are relative to the held pitch
                if (v == state) continue;
                bend(std::max(on, tm.ticks(bt)), 4, v);
                state = v;
            }
            t.add(off, 0, {(uint8_t) (0x80 | ch), (uint8_t) n.note, 0});
            if (!n.slide_to_next || !nx) {
                if (state != 8192) { bend(off, 1, 8192); state = 8192; }
                if (porta) { cc(off, 1, 65, 0); porta = false; }
            }
        }
        if (porta) cc(t.ev.empty() ? 0 : t.ev.back().tick, 1, 65, 0);
        tracks.push_back(std::move(t));
    }

    // ---------------------------------------------------------------- file
    std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1};
    const uint16_t nt = (uint16_t) tracks.size();
    f.insert(f.end(), {(uint8_t) (nt >> 8), (uint8_t) nt, (uint8_t) (kPPQ >> 8), (uint8_t) (kPPQ & 0xFF)});
    for (auto& t : tracks) {
        const auto c = chunk(t);
        f.insert(f.end(), c.begin(), c.end());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path);
    out.write((const char*) f.data(), (std::streamsize) f.size());
    if (!out) throw std::runtime_error("write failed: " + path);
}

}  // namespace ds
