// track_beats against exact ground truth: synthetic bands (tests/groove_synth.h) at constant tempo, with gradual drift,
// a sudden section tempo change, ritardando, swing, 16th funk, human jitter, 3/4, 6/8 and a forced fixed tempo.
// Metrics: beat F-measure (+-70 ms), mean / max beat error (ms), downbeat accuracy, meter, mean-tempo error (BPM).
#include <cmath>
#include <cstdio>
#include <string>
#include "ds/modules.h"
#include "groove_synth.h"

using namespace gsynth;

struct Case {
    std::string name;
    Opts o;
    double fixed = 0;
    double max_mean_err_ms;
};

int main()
{
    std::vector<Case> cases;
    auto add = [&](std::string n, Style st, std::function<double(int)> bpm, double jit, double err, double s8 = 0.5,
                   double s16 = 0.5, unsigned seed = 1) {
        Case c;
        c.name = n; c.o.style = st; c.o.bpm = bpm; c.o.jitter_ms = jit; c.o.swing8 = s8; c.o.swing16 = s16;
        c.o.seed = seed; c.o.vel_jitter = 6; c.max_mean_err_ms = err;
        cases.push_back(c);
    };
    add("rock 120 exact", Style::rock, [](int) { return 120.0; }, 0, 0.5);
    add("rock 96 jitter 7ms", Style::rock, [](int) { return 96.0; }, 7, 5.0, 0.5, 0.5, 2);
    add("drift 118->124 jit 5", Style::rock, [](int i) { return 118 + 6.0 * std::min(i, 128) / 128; }, 5, 4.0, 0.5, 0.5, 3);
    add("sudden 120->140 jit 5", Style::rock, [](int i) { return i < 64 ? 120.0 : 140.0; }, 5, 5.0, 0.5, 0.5, 4);
    add("ritardando 126->100", Style::rock, [](int i) { return i < 96 ? 126.0 : 126 - 26.0 * std::min(i - 96, 32) / 32; }, 4, 5.0, 0.5, 0.5, 5);
    add("shuffle 100 swing 64%", Style::shuffle, [](int) { return 100.0; }, 6, 5.0, 0.64, 0.5, 6);
    add("funk16 92 swing16 55%", Style::funk16, [](int) { return 92.0; }, 5, 4.0, 0.5, 0.55, 7);
    add("waltz 3/4 140 jit 6", Style::waltz, [](int) { return 140.0; }, 6, 5.0, 0.5, 0.5, 8);
    add("6/8 at 66 (dotted) jit 5", Style::sixeight, [](int) { return 66.0; }, 5, 5.0, 0.5, 0.5, 9);
    add("rock 174 jit 5", Style::rock, [](int) { return 174.0; }, 5, 4.0, 0.5, 0.5, 10);
    add("rock 78 jit 8", Style::rock, [](int) { return 78.0; }, 8, 6.0, 0.5, 0.5, 11);
    {
        Case c;
        c.name = "fixed_bpm=120, t0=0.8137";
        c.o.bpm = [](int) { return 120.0; }; c.o.t0 = 0.8137; c.o.jitter_ms = 5; c.o.seed = 12;
        c.fixed = 120; c.max_mean_err_ms = 2.5;
        cases.push_back(c);
    }

    int fails = 0;
    std::printf("%-26s %6s %8s %8s %6s %5s %7s %7s\n", "case", "F", "mean ms", "max ms", "downb", "meter", "bpm", "err");
    for (auto& c : cases) {
        const Song s = make_song(c.o);
        const ds::BeatGrid g = ds::track_beats(s.drums, s.bass, s.duration, c.fixed);
        const BeatScore sc = score_beats(s.beats, s.downbeats, g.beats, g.downbeats, 0, s.duration);
        const double true_bpm = s.grid().mean_bpm, terr = std::abs(g.mean_bpm - true_bpm);
        const bool ok = sc.f >= 0.98 && sc.mean_err_ms <= c.max_mean_err_ms && sc.downbeat_acc >= 0.95 &&
                        g.beats_per_bar == s.bpb && terr <= 0.5 && !g.beats.empty() && g.beats.front() >= 0 &&
                        g.beats.back() >= s.duration && g.bpm_at_beat.size() == g.beats.size();
        std::printf("%-26s %6.3f %8.2f %8.2f %6.3f %3d/%d %7.2f %7.3f %s\n", c.name.c_str(), sc.f, sc.mean_err_ms,
                    sc.max_err_ms, sc.downbeat_acc, g.beats_per_bar, s.bpb, g.mean_bpm, terr, ok ? "ok" : "FAIL");
        if (!ok) ++fails;
    }
    std::printf("%s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
