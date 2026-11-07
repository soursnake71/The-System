// The piano roll's editing: a song's notes and a selection (one flag per note), the operations on the selection.
// The plugin's grid and the Android app's grid both work through these.
#pragma once
#include "Song.h"
#include <algorithm>
#include <cmath>

namespace cmi {

struct GridEdit {
    std::vector<SongNote> notes;
    std::vector<char> sel;   // one per note

    void setNotes(std::vector<SongNote> n) { notes = std::move(n); sel.assign(notes.size(), 0); }
    void fit() { sel.resize(notes.size(), 0); }
    int selected() const { int n = 0; for (char c : sel) n += c != 0; return n; }
    bool any() const { return selected() > 0; }
    void clear() { std::fill(sel.begin(), sel.end(), 0); }

    // the note under a point of a track's lane (-1 none)
    int at(int track, double beat, int pitch, double minLen) const {
        int best = -1;
        for (size_t i = 0; i < notes.size(); ++i) { const auto& n = notes[i]; if (n.track == track && n.pitch == pitch && beat >= n.start && beat <= n.start + std::max(n.len, minLen)) best = (int) i; }
        return best;
    }
    void selectAll(int track) { fit(); for (size_t i = 0; i < notes.size(); ++i) sel[i] = notes[i].track == track; }
    void selectRect(int track, double b0, double b1, int p0, int p1, bool add) {
        fit(); if (b0 > b1) std::swap(b0, b1); if (p0 > p1) std::swap(p0, p1);
        for (size_t i = 0; i < notes.size(); ++i) {
            const auto& n = notes[i];
            const bool in = n.track == track && n.pitch >= p0 && n.pitch <= p1 && n.start < b1 && n.start + n.len > b0;
            sel[i] = add ? (sel[i] || in) : in;
        }
    }
    // what an operation works on: the selection, else every note of the track
    template <class F> void each(int track, F f) {
        fit(); const bool s = any();
        for (size_t i = 0; i < notes.size(); ++i) if (s ? sel[i] != 0 : notes[i].track == track) f(notes[i]);
    }
    void erase() {
        fit(); std::vector<SongNote> n; std::vector<char> s;
        for (size_t i = 0; i < notes.size(); ++i) if (!sel[i]) { n.push_back(notes[i]); s.push_back(0); }
        notes = std::move(n); sel = std::move(s);
    }
    void transpose(int track, int semis) { each(track, [&](SongNote& n) { n.pitch = std::clamp(n.pitch + semis, 0, 127); }); }
    void move(int track, double beats) { each(track, [&](SongNote& n) { n.start = std::max(0.0, n.start + beats); }); }
    void quantize(int track, double snap) { if (snap <= 0) return; each(track, [&](SongNote& n) { n.start = std::max(0.0, std::round(n.start / snap) * snap); }); }
    void stretch(int track, double factor) { each(track, [&](SongNote& n) { n.len = std::max(1.0 / 64, n.len * factor); }); }
    void velocity(int track, int delta) { each(track, [&](SongNote& n) { n.vel = std::clamp(n.vel + delta, 1, 127); }); }
    void setVelocity(int track, int v) { each(track, [&](SongNote& n) { n.vel = std::clamp(v, 1, 127); }); }
    // legato: each note runs to the next one in the track
    void legato(int track) {
        fit(); std::vector<size_t> ix; const bool s = any();
        for (size_t i = 0; i < notes.size(); ++i) if (s ? sel[i] != 0 : notes[i].track == track) ix.push_back(i);
        std::sort(ix.begin(), ix.end(), [&](size_t a, size_t b) { return notes[a].start < notes[b].start; });
        for (size_t k = 0; k + 1 < ix.size(); ++k) { const double nx = notes[ix[k + 1]].start; if (nx > notes[ix[k]].start + 1e-6) notes[ix[k]].len = nx - notes[ix[k]].start; }
    }
    // the selection copied right after itself (by its length, rounded up to the snap); the copy is selected
    void duplicate(double snap) {
        fit(); if (!any()) return;
        double lo = 1e18, hi = 0; for (size_t i = 0; i < notes.size(); ++i) if (sel[i]) { lo = std::min(lo, notes[i].start); hi = std::max(hi, notes[i].start + notes[i].len); }
        double span = hi - lo; if (snap > 0) span = std::max(snap, std::ceil(span / snap - 1e-9) * snap);
        std::vector<SongNote> add; for (size_t i = 0; i < notes.size(); ++i) if (sel[i]) { SongNote n = notes[i]; n.start += span; add.push_back(n); }
        clear(); for (auto& n : add) { notes.push_back(n); sel.push_back(1); }
    }
    // a clipboard shared by every grid: copy, then paste at a beat on a track (the pasted notes selected)
    static std::vector<SongNote>& clipboard() { static std::vector<SongNote> c; return c; }
    void copy() {
        fit(); auto& c = clipboard(); c.clear(); double lo = 1e18;
        for (size_t i = 0; i < notes.size(); ++i) if (sel[i]) { c.push_back(notes[i]); lo = std::min(lo, notes[i].start); }
        for (auto& n : c) n.start -= lo;
    }
    void paste(int track, double at) {
        fit(); clear();
        for (auto n : clipboard()) { n.track = track; n.start += std::max(0.0, at); notes.push_back(n); sel.push_back(1); }
    }
    int add(int track, double start, double len, int pitch, int vel) {
        fit(); notes.push_back({ track, std::max(0.0, start), std::max(1.0 / 64, len), std::clamp(pitch, 0, 127), std::clamp(vel, 1, 127) }); sel.push_back(0);
        return (int) notes.size() - 1;
    }
};

}  // namespace cmi
