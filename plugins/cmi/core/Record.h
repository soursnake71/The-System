// Recording: what the engine logged while recording (notes on and off with their beats) becomes notes on a track.
#pragma once
#include "Song.h"
#include <map>

namespace cmi {

struct RecordSettings {
    int track = 0;
    double quant = 0;        // 0: as played; else the grid to pull note starts to (beats)
    bool replace = false;    // replace: the track's notes in the recorded span go
    bool countIn = true;     // a bar of clicks before
};

// the logged events into notes; returns how many notes were added. from / to: the span recorded (beats)
inline int mergeRecording(std::vector<SongNote>& notes, const std::vector<Engine::RecEv>& evs, const RecordSettings& rs, double from, double to) {
    std::map<int, std::pair<double, int>> open;   // note -> start, velocity
    std::vector<SongNote> add;
    auto close = [&](int note, double end) {
        auto it = open.find(note); if (it == open.end()) return;
        double st = std::max(from, it->second.first), len = std::max(1.0 / 64, end - it->second.first);
        if (rs.quant > 0) { st = std::max(0.0, std::round(st / rs.quant) * rs.quant); len = std::max(rs.quant * 0.5, len); }
        add.push_back({ rs.track, st, len, note, std::clamp(it->second.second, 1, 127) });
        open.erase(it);
    };
    for (const auto& e : evs) {
        if (e.vel > 0) { close(e.note, e.beat); open[e.note] = { e.beat, e.vel }; }
        else close(e.note, e.beat);
    }
    std::vector<int> still; for (auto& kv : open) still.push_back(kv.first);
    for (int n : still) close(n, to);   // held to the end
    if (add.empty()) return 0;
    if (rs.replace) notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const SongNote& n) { return n.track == rs.track && n.start >= from - 1e-9 && n.start < to; }), notes.end());
    notes.insert(notes.end(), add.begin(), add.end());
    return (int) add.size();
}

// a song to record into when there's none: empty, at a tempo
inline MidiSong recordSong(const std::string& name, double bpm) {
    MidiSong s = emptySong(name, std::clamp(bpm, 30.0, 300.0));
    s.raw = writeSmf(s);
    return s;
}

// a recording session, as the plugin, the app and the headless player run it
struct Recorder {
    RecordSettings rs; bool countIn = true; std::atomic<bool> on { false }; double from = 0;
    std::vector<Engine::RecEv> evs;
    // no song: a new one (selected); returns its index
    static int newSong(SongList& sl, Engine& eng, const Bank& bank, double bpm) {
        const int i = sl.add(recordSong("NEW SONG " + std::to_string(sl.count() + 1), bpm), bank);
        selectSong(eng, sl.at(i)); return i;
    }
    // records the keyboard (and MIDI in) onto track, from fromBeat, after a bar's count-in if set
    std::string start(Engine& eng, SongEntry* e, int track, double fromBeat) {
        if (!e || !eng.song.load()) return "NO SONG";
        rs.track = std::clamp(track, 0, SONG_TRK - 1); from = std::max(0.0, fromBeat);
        if (!e->song->used[rs.track]) { auto ns = std::make_unique<MidiSong>(*e->song); ns->used[rs.track] = true; e->song = std::move(ns); eng.song = e->song.get(); }
        evs.clear(); Engine::RecEv ev; while (eng.recPop(ev)) {}
        const double barBeats = e->song->num * 4.0 / std::max(1, e->song->den);
        eng.extSync = false;
        eng.recTrack = rs.track; eng.recFrom = from; eng.recOn = true; on = true;
        eng.songStartReq = from - (countIn ? barBeats : 0.0); eng.songRun = true;
        return "RECORDING TRACK " + std::to_string(rs.track + 1) + (countIn ? " (COUNT-IN)" : "");
    }
    void poll(Engine& eng) { Engine::RecEv ev; while (eng.recPop(ev)) evs.push_back(ev); }
    std::string stop(Engine& eng, SongEntry* e) {
        if (!on.exchange(false)) return {};
        const double to = std::max(from, eng.songBeat.load());
        eng.songRun = false; eng.recOn = false;
        poll(eng);
        if (!e) return "NO SONG";
        auto notes = notesOf(*e->song);
        const int n = mergeRecording(notes, evs, rs, from, to);
        evs.clear();
        if (n == 0) return "NOTHING RECORDED";
        editSong(eng, e, notes);
        return std::to_string(n) + " NOTES RECORDED ON TRACK " + std::to_string(rs.track + 1);
    }
};

}  // namespace cmi
