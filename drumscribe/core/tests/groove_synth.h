// A synthetic band with exact ground truth for tests/test_beats.cpp, test_groove.cpp and test_midi.cpp: no audio, only
// the transcribed events (DrumHit / BassNote) the analysis modules consume. Every event is placed on a known beat grid
// (constant tempo, drift, sudden tempo change), at a known swung position, with a known per-instrument lag, known
// velocities and seeded Gaussian human jitter, so the true beats, downbeats, swing, lags and accents are exact.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>
#include "ds/kit.h"
#include "ds/types.h"

namespace gsynth {

struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned s) : g(s) {}
    // portable: the standard distributions give different numbers on libstdc++, libc++ and MSVC; mt19937's raw
    // output is the same everywhere, so the tests see the same drummer on every platform
    double u01() { return ((double) g() + 0.5) / 4294967296.0; }
    double uni(double a, double b) { return a + (b - a) * u01(); }
    double norm(double m, double s) { return s > 0 ? m + s * std::sqrt(-2 * std::log(u01())) * std::cos(6.283185307179586 * u01()) : m; }
};

enum class Style { rock, funk16, waltz, shuffle, sixeight };

struct Opts {
    Style style = Style::rock;
    int bars = 32;
    double t0 = 0.8;                      // first downbeat (s)
    std::function<double(int)> bpm;       // tempo of beat i -> i+1
    double swing8 = 0.5, swing16 = 0.5;   // off-beat 8th / 16th position as a fraction (0.5 straight, 0.667 triplet)
    double jitter_ms = 0;                 // Gaussian timing jitter (sd) on every event
    std::map<std::string, double> lag_ms; // per piece ("snare", "hh_closed", ...) systematic offset; "bass" for the bass
    double bass_kick_ms = 0;              // bass onset relative to the kick it doubles
    int hat_on = 105, hat_off = 70;       // hat accent pattern: on-beat 8ths / off-beat 8ths (16ths: e/a = hat_off - 15)
    double vel_jitter = 0;                // velocity sd
    bool bass = true;
    bool crashes = true;
    unsigned seed = 1;
};

struct Song {
    std::vector<double> beats;  // true beats (s), cover [0, duration]
    std::vector<int> downbeats; // indices into beats
    int bpb = 4;
    double duration = 0;
    std::vector<ds::DrumHit> drums;
    std::vector<ds::BassNote> bass;
    ds::BeatGrid grid() const
    {
        ds::BeatGrid g;
        g.beats = beats; g.downbeats = downbeats; g.beats_per_bar = bpb;
        for (size_t i = 0; i + 1 < beats.size(); ++i) g.bpm_at_beat.push_back(60.0 / (beats[i + 1] - beats[i]));
        g.bpm_at_beat.push_back(g.bpm_at_beat.empty() ? 120.0 : g.bpm_at_beat.back());
        g.mean_bpm = 60.0 * (beats.size() - 1) / (beats.back() - beats.front());
        return g;
    }
};

// position of 16th step k (0..3) inside a beat, with 8th swing s8 and 16th swing s16
inline double step_pos(int k, double s8, double s16)
{
    switch (k) {
    case 0: return 0;
    case 1: return s16 * s8;
    case 2: return s8;
    default: return s8 + s16 * (1 - s8);
    }
}

inline Song make_song(const Opts& o)
{
    Song s;
    Rng r(o.seed);
    const int bpb = o.style == Style::waltz ? 3 : o.style == Style::sixeight ? 2 : 4;
    s.bpb = bpb;
    auto bpm = o.bpm ? o.bpm : [](int) { return 120.0; };
    // beats: pickup before t0 back to >= 0, then the bars, then one more bar of silence
    const int nbeats = o.bars * bpb;
    std::vector<double> after{o.t0};
    for (int i = 0; i < nbeats + bpb; ++i) after.push_back(after.back() + 60.0 / bpm(i));
    std::vector<double> before;
    for (double t = o.t0 - 60.0 / bpm(0); t >= 0; t -= 60.0 / bpm(0)) before.push_back(t);
    std::reverse(before.begin(), before.end());
    const int first = (int) before.size();
    s.beats = before;
    s.beats.insert(s.beats.end(), after.begin(), after.end());
    s.duration = after[nbeats] + 0.5 * 60.0 / bpm(nbeats);
    while (s.beats.size() > 1 && s.beats[s.beats.size() - 2] >= s.duration) s.beats.pop_back();  // last beat >= duration
    for (int i = first % bpb; i < (int) s.beats.size(); i += bpb) s.downbeats.push_back(i);

    auto lag = [&](const std::string& p) { auto it = o.lag_ms.find(p); return it == o.lag_ms.end() ? 0.0 : it->second; };
    auto at = [&](int beat, double pos, const std::string& piece) {
        const int b = first + beat;
        const double t = s.beats[b] + pos * (s.beats[b + 1] - s.beats[b]);
        return t + (lag(piece) + r.norm(0, o.jitter_ms)) / 1000.0;
    };
    auto hit = [&](int beat, int step, const std::string& piece, int vel, double len = 0.15) {
        double pos;
        if (o.style == Style::sixeight) pos = step / 3.0;  // dotted-quarter beats split into three 8ths
        else pos = step_pos(step, o.swing8, o.swing16);
        ds::DrumHit h;
        h.time = at(beat, pos, piece);
        h.end = h.time + len;
        h.piece = piece;
        h.note = ds::note_of(piece);
        h.velocity = std::max(1, std::min(127, (int) std::lround(vel + r.norm(0, o.vel_jitter))));
        s.drums.push_back(h);
        return h.time;
    };
    std::vector<double> kicks;
    for (int bar = 0; bar < o.bars; ++bar) {
        const int b0 = bar * bpb;
        const bool crash = o.crashes && bar % 4 == 0;
        switch (o.style) {
        case Style::rock:
        case Style::shuffle:
            for (int b = 0; b < 4; ++b) {
                if (b == 0 && crash) hit(b0, 0, "crash1", 115, 1.5);
                else hit(b0 + b, 0, "hh_closed", o.hat_on, 0.06);
                hit(b0 + b, 2, "hh_closed", o.hat_off, 0.06);
            }
            kicks.push_back(hit(b0, 0, "kick", 112));
            kicks.push_back(hit(b0 + 2, 0, "kick", 100));
            kicks.push_back(hit(b0 + 2, 2, "kick", 92));
            hit(b0 + 1, 0, "snare", 115, 0.25);
            hit(b0 + 3, 0, "snare", 118, 0.25);
            break;
        case Style::funk16:
            for (int b = 0; b < 4; ++b)
                for (int k = 0; k < 4; ++k) {
                    if (b == 0 && k == 0 && crash) { hit(b0, 0, "crash1", 115, 1.5); continue; }
                    hit(b0 + b, k, "hh_closed", k == 0 ? o.hat_on : k == 2 ? o.hat_off : o.hat_off - 15, 0.05);
                }
            kicks.push_back(hit(b0, 0, "kick", 110));
            kicks.push_back(hit(b0, 3, "kick", 90));
            kicks.push_back(hit(b0 + 2, 2, "kick", 100));
            hit(b0 + 1, 0, "snare", 115, 0.25);
            hit(b0 + 1, 3, "snare", 34, 0.1);  // ghost
            hit(b0 + 3, 0, "snare", 118, 0.25);
            hit(b0 + 3, 3, "snare", 30, 0.1);  // ghost
            break;
        case Style::waltz:
            for (int b = 0; b < 3; ++b) {
                if (b == 0 && crash) hit(b0, 0, "crash1", 115, 1.5);
                else hit(b0 + b, 0, "hh_closed", o.hat_on, 0.06);
                hit(b0 + b, 2, "hh_closed", o.hat_off, 0.06);
            }
            kicks.push_back(hit(b0, 0, "kick", 112));
            hit(b0 + 1, 0, "snare", 100, 0.2);
            hit(b0 + 2, 0, "snare", 96, 0.2);
            break;
        case Style::sixeight:
            for (int b = 0; b < 2; ++b)
                for (int k = 0; k < 3; ++k) {
                    if (b == 0 && k == 0 && crash) { hit(b0, 0, "crash1", 115, 1.5); continue; }
                    hit(b0 + b, k, "hh_closed", k == 0 ? o.hat_on : o.hat_off, 0.06);
                }
            kicks.push_back(hit(b0, 0, "kick", 112));
            kicks.push_back(hit(b0, 2, "kick", 85));
            hit(b0 + 1, 0, "snare", 115, 0.25);
            break;
        }
    }
    std::sort(s.drums.begin(), s.drums.end(), [](const ds::DrumHit& a, const ds::DrumHit& b) { return a.time < b.time; });
    if (o.bass) {
        std::sort(kicks.begin(), kicks.end());
        const int pitches[] = {40, 40, 43, 45, 38, 40, 47, 45};
        for (size_t i = 0; i < kicks.size(); ++i) {
            ds::BassNote n;
            n.time = kicks[i] + o.bass_kick_ms / 1000.0 + r.norm(0, o.jitter_ms * 0.5) / 1000.0;
            n.end = i + 1 < kicks.size() ? kicks[i + 1] - 0.03 : n.time + 0.3;
            n.note = pitches[i % 8];
            n.velocity = 96;
            s.bass.push_back(n);
        }
    }
    return s;
}

// ---- beat-tracking metrics
struct BeatScore { double f = 0, mean_err_ms = 0, max_err_ms = 0, downbeat_acc = 0; int matched = 0; };
inline BeatScore score_beats(const std::vector<double>& truth, const std::vector<int>& true_db,
                             const std::vector<double>& est, const std::vector<int>& est_db, double t_lo, double t_hi,
                             double tol = 0.07)
{
    std::vector<double> T, E;
    for (double t : truth) if (t >= t_lo && t <= t_hi) T.push_back(t);
    for (double t : est) if (t >= t_lo && t <= t_hi) E.push_back(t);
    BeatScore s;
    std::vector<bool> used(E.size(), false);
    double err = 0;
    for (double t : T) {
        int best = -1;
        double bd = tol;
        for (size_t j = 0; j < E.size(); ++j)
            if (!used[j] && std::abs(E[j] - t) <= bd) { bd = std::abs(E[j] - t); best = (int) j; }
        if (best >= 0) { used[best] = true; ++s.matched; err += bd; s.max_err_ms = std::max(s.max_err_ms, bd * 1000); }
    }
    const double p = E.empty() ? 0 : (double) s.matched / E.size(), rcl = T.empty() ? 0 : (double) s.matched / T.size();
    s.f = p + rcl > 0 ? 2 * p * rcl / (p + rcl) : 0;
    s.mean_err_ms = s.matched ? err / s.matched * 1000 : 0;
    int dn = 0, dm = 0;
    for (int i : true_db) {
        if (i >= (int) truth.size() || truth[i] < t_lo || truth[i] > t_hi) continue;
        ++dn;
        for (int j : est_db)
            if (j < (int) est.size() && std::abs(est[j] - truth[i]) <= tol) { ++dm; break; }
    }
    s.downbeat_acc = dn ? (double) dm / dn : 0;
    return s;
}

}  // namespace gsynth
