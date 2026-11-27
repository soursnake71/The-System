// Omega Solina sequencer: the same as solina/seq.js (16 patterns of 1-4 bars, song chain, swing, real-time recording,
// 96 ticks per quarter), plus MIDI file import / export through juce::MidiFile in the processor.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <random>
#include <vector>

namespace solina {

static constexpr int PPQ = 96, STEP = 24, BAR = 384, NPAT = 16;
struct Note { int t = 0, n = 60, d = 96, v = 100; };
struct Pattern { int bars = 1; std::vector<Note> notes; };
struct Song {
    double bpm = 120, swing = 0; int mode = 0, cur = 0;
    std::vector<int> chain{0};
    Pattern pats[NPAT];
};
inline int patLen(const Pattern& p) { return std::max(1, std::min(4, p.bars)) * BAR; }

class SeqPlayer {
public:
    Song* song = nullptr;
    bool playing = false, rec = false, quant = true;
    double tick = 0; int pos = 0, pat = 0;
    struct Held { int n; double off; };
    std::vector<Held> held;
    struct RecHeld { double t; double v; int pat; };
    std::map<int, RecHeld> recHeld;

    void start(double fromTick) {
        Song& s = *song; playing = true; pos = 0; pat = s.mode ? first() : s.cur; tick = fromTick;
        if (s.mode && tick > 0 && !s.chain.empty()) {
            double t = tick; pos = 0; int guard = 0;
            while (t >= patLen(s.pats[s.chain[(size_t) pos]]) && guard++ < 10000) { t -= patLen(s.pats[s.chain[(size_t) pos]]); pos = (pos + 1) % (int) s.chain.size(); }
            pat = s.chain[(size_t) pos]; tick = t;
        }
    }
    template <typename Emit> void stop(Emit emit) { playing = false; for (auto& h : held) emit(0, 0x80, h.n, 0); held.clear(); recHeld.clear(); }
    double swingT(int t) const { return ((t / STEP) & 1) ? t + song->swing * 12 : t; }

    // emit(sampleOffset, status, note, vel); status 0x90 / 0x80, or 0xF8 for MIDI clock
    template <typename Emit> void run(int n, double fs, Emit emit) {
        if (!playing) return;
        Song& s = *song; const double tps = s.bpm * PPQ / (60 * fs);
        int done = 0, guard = 0;
        while (done < n && guard++ < 64) {
            if (pat < 0 || pat >= NPAT) pat = 0;
            Pattern& p = s.pats[pat]; const int len = patLen(p); const double t0 = tick; const int left = n - done;
            double t1 = t0 + left * tps; bool wrap = false;
            if (t1 >= len) { t1 = len; wrap = true; }
            auto off = [&](double t) { return done + std::max(0, std::min(left - 1, (int) std::floor((t - t0) / tps))); };
            for (double c = std::ceil(t0 / 4) * 4; c < t1; c += 4) emit(off(c), 0xF8, 0, 0);
            for (size_t i = 0; i < held.size(); i++) if (held[i].off < t1) { emit(off(held[i].off), 0x80, held[i].n, 0); held.erase(held.begin() + (long) i); i--; }
            for (auto& nt : p.notes) {
                const double ts = swingT(nt.t);
                if (ts >= t0 && ts < t1 && nt.t < len) {
                    for (size_t i = 0; i < held.size(); i++) if (held[i].n == nt.n) { emit(off(ts), 0x80, nt.n, 0); held.erase(held.begin() + (long) i); i--; }
                    emit(off(ts), 0x90, nt.n, std::max(1, std::min(127, nt.v)));
                    held.push_back({nt.n, ts + std::max(1, nt.d)});
                }
            }
            done += wrap ? std::max(1, (int) std::ceil((t1 - t0) / tps)) : left;
            if (wrap) {
                for (auto& h : held) h.off -= len;
                tick = 0;
                if (s.mode && !s.chain.empty()) { pos = (pos + 1) % (int) s.chain.size(); pat = s.chain[(size_t) pos]; } else pat = s.cur;
            } else tick = t1;
        }
    }
    void recOn(int n, double v) { if (!playing || !rec) return; recHeld[n] = {tick, v, pat}; }
    // a finished note to add to pattern `outPat`, when the key was being recorded
    bool recOff(int n, int& outPat, Note& out) {
        auto it = recHeld.find(n); if (it == recHeld.end()) return false;
        RecHeld h = it->second; recHeld.erase(it);
        const int len = patLen(song->pats[h.pat]);
        double t = h.t, d = pat == h.pat ? tick - h.t : len - h.t + tick;
        if (quant) { t = std::round(t / STEP) * STEP; d = std::max((double) STEP, std::round(d / STEP) * STEP); if (t >= len) t -= len; }
        outPat = h.pat; out = {std::max(0, (int) std::lround(t)), n, std::max(1, (int) std::lround(std::min(d, (double) BAR * 4))), (int) std::lround(h.v * 127)};
        return true;
    }
private:
    int first() const { return song->chain.empty() ? song->cur : song->chain[0]; }
};

// a chord progression voiced for a string ensemble, as randomPattern in seq.js
inline Pattern randomPattern(std::mt19937& rng) {
    std::uniform_real_distribution<double> U(0, 1); auto rnd = [&] { return U(rng); };
    static const int SC[3][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}};
    static const int PROGS[7][4] = {{0, 5, 3, 4}, {0, 3, 4, 0}, {0, 5, 1, 4}, {0, 6, 5, 4}, {5, 3, 0, 4}, {0, 4, 5, 3}, {0, 2, 3, 4}};
    const int root = (int) (rnd() * 12), si = (int) (rnd() * 3), pi = (int) (rnd() * 7), style = (int) (rnd() * 4);
    auto deg = [&](int d, int o) { const int i = ((d % 7) + 7) % 7; return 48 + root + SC[si][i] + 12 * ((int) std::floor(d / 7.0) + o); };
    Pattern P; P.bars = 4;
    for (int b = 0; b < 4; b++) {
        const int d = PROGS[pi][b]; std::vector<int> chord{deg(d, 0), deg(d + 2, 0), deg(d + 4, 0)};
        if (rnd() < .5) chord.push_back(deg(d + 6, 0));
        const int t0 = b * BAR, bass = deg(d, -1);
        if (style == 0) { for (int n : chord) P.notes.push_back({t0, n + 12, BAR - 6, 100}); P.notes.push_back({t0, bass - 12 < 36 ? bass : bass - 12, BAR - 6, 100}); }
        else if (style == 1) { for (int h : {0, 192}) for (int n : chord) P.notes.push_back({t0 + h, n + 12, 186, 96}); P.notes.push_back({t0, bass, BAR - 6, 100}); }
        else if (style == 2) { auto ar = chord; ar.push_back(chord[0] + 12); for (int s = 0; s < 16; s++) P.notes.push_back({t0 + s * STEP, ar[(size_t) s % ar.size()] + 12, STEP - 2, s % 4 ? 80 : 110}); P.notes.push_back({t0, bass, BAR - 6, 100}); }
        else { for (int n : chord) P.notes.push_back({t0, n + 12, BAR - 6, 100}); for (int s = 0; s < 4; s++) P.notes.push_back({t0 + s * 96 + (s % 2 ? 48 : 0), bass, 40, 110}); }
    }
    return P;
}

} // namespace solina
