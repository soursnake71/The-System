// analyze_groove against exact ground truth: grooves synthesized (tests/groove_synth.h) with a KNOWN swing, snare lag,
// hat lag, hat accent pattern, ghost notes, bass-kick offset, tempo drift / section change and jitter. Each number the
// analysis reports is checked against the value the band was built with, on the true grid and on the tracked grid.
#include <cmath>
#include <cstdio>
#include <string>
#include "ds/modules.h"
#include "groove_synth.h"

using namespace gsynth;
using json = nlohmann::json;
static int fails = 0, checks = 0;

static double num(const json& j) { return j.is_number() ? j.get<double>() : NAN; }
static void check(const char* what, double got, double want, double tol)
{
    ++checks;
    const bool ok = std::abs(got - want) <= tol;
    if (!ok) ++fails;
    std::printf("  %-44s got %9.3f  want %9.3f  +-%-6g %s\n", what, got, want, tol, ok ? "ok" : "FAIL");
}

int main()
{
    // ---- A: rock 110 BPM, 8ths swung 60 %, snare +8 ms, hats -3 ms, bass +6 ms after the kick, jitter 3 ms
    Opts a;
    a.style = Style::rock; a.bpm = [](int) { return 110.0; }; a.swing8 = 0.60; a.jitter_ms = 3; a.bars = 48;
    a.lag_ms = {{"snare", 8.0}, {"hh_closed", -3.0}}; a.bass_kick_ms = 6; a.hat_on = 105; a.hat_off = 70; a.seed = 21;
    const Song sa = make_song(a);
    for (int pass = 0; pass < 2; ++pass) {
        const ds::BeatGrid g = pass == 0 ? sa.grid() : ds::track_beats(sa.drums, sa.bass, sa.duration);
        const json r = ds::analyze_groove(sa.drums, sa.bass, g, sa.duration);
        const double tol = pass == 0 ? 1.0 : 1.5;
        std::printf("A rock 110, swing 60%%, snare +8, hats -3, bass +6 (%s grid)\n", pass == 0 ? "true" : "tracked");
        check("tempo mean_bpm", num(r["tempo"]["mean_bpm"]), 110, pass == 0 ? 0.01 : 0.05);
        check("tempo sections", (double) r["tempo"]["sections"].size(), 1, 0);
        check("swing hihat eighths_pct", num(r["swing"]["per_instrument"]["hihat"]["eighths_pct"]), 60, 0.6);
        check("swing eighths_pct (all)", num(r["swing"]["eighths_pct"]), 60, 0.8);
        check("snare - kick (ms)", num(r["push_pull"]["snare_vs_kick_ms"]), 8, tol);
        check("hihat - kick (ms)", num(r["push_pull"]["hihat_vs_kick_ms"]), -3, tol);
        if (pass == 0) {
            check("snare microtiming mean_ms", num(r["microtiming"]["snare"]["mean_ms"]), 8, 1.0);
            check("kick microtiming mean_ms", num(r["microtiming"]["kick"]["mean_ms"]), 0, 1.0);
            check("hihat microtiming mean_ms", num(r["microtiming"]["hihat"]["mean_ms"]), -3, 0.8);
            check("snare sd_ms (jitter 3)", num(r["microtiming"]["snare"]["sd_ms"]), 3, 0.8);
        }
        check("bass-kick mean_offset_ms", num(r["bass_kick_lock"]["mean_offset_ms"]), 6, 0.6);
        check("bass-kick share_within_20ms", num(r["bass_kick_lock"]["share_within_20ms"]), 1, 0);
        check("hihat velocity step 2 (off 8th)", num(r["dynamics"]["hihat"]["per_step"][2]), 70, 0.01);
        check("hihat velocity step 4 (beat 2)", num(r["dynamics"]["hihat"]["per_step"][4]), 105, 0.01);
        check("hihat pattern == X.x.X.x.X.x.X.x.", r["dynamics"]["hihat"]["pattern"] == "X.x.X.x.X.x.X.x." ? 1 : 0, 1, 0);
        check("kick per bar", num(r["density"]["per_bar"]["kick"]), 3, 0.01);
        check("snare per bar", num(r["density"]["per_bar"]["snare"]), 2, 0.01);
        check("syncopation index (rock = 0)", num(r["syncopation"]["index"]), 0, 0.01);
        check("template steps", num(r["template"]["steps"]), 16, 0);
        // template offset at step 2 (straight grid) = swing displacement (0.60-0.5) beat - 3 ms hat lag
        if (pass == 0)
            check("template hihat step 2 offset_ms", num(r["template"]["per_instrument"]["hihat"]["offset_ms"][2]),
                  0.10 * 60000 / 110 - 3, 1.0);
        // relative to the hats' own beat: swing displacement 0.1 beat (grid-independent)
        check("template hihat step 6 - step 4 (ms)", num(r["template"]["per_instrument"]["hihat"]["offset_ms"][6]) -
              num(r["template"]["per_instrument"]["hihat"]["offset_ms"][4]), 0.10 * 60000 / 110, 1.5);
        check("humanization timing_sd_ms (jitter 3)", num(r["humanization"]["timing_sd_ms"]), 3, 0.8);
        if (pass == 1) {
            const std::string txt = ds::groove_text(r);
            int lines = 0;
            for (char c : txt) lines += c == '\n';
            std::printf("%s", txt.c_str());
            check("groove_text lines < 40", lines < 40 ? 1 : 0, 1, 0);
        }
    }

    // ---- B: funk 16ths at 96 BPM, 16ths swung 57 %, ghost notes, exact timing
    Opts b;
    b.style = Style::funk16; b.bpm = [](int) { return 96.0; }; b.swing16 = 0.57; b.jitter_ms = 0; b.seed = 22;
    const Song sb = make_song(b);
    {
        const json r = ds::analyze_groove(sb.drums, sb.bass, sb.grid(), sb.duration);
        std::printf("B funk16 96, 16ths swung 57%%, snare ghosts, no jitter\n");
        check("swing sixteenths_pct", num(r["swing"]["sixteenths_pct"]), 57, 0.05);
        check("swing eighths_pct", num(r["swing"]["eighths_pct"]), 50, 0.05);
        check("snare ghost_share", num(r["dynamics"]["snare"]["ghost_share"]), 0.5, 0.001);
        check("syncopation index (LHL = 3/bar)", num(r["syncopation"]["index"]), 3, 0.001);
        check("hihat per bar", num(r["density"]["per_bar"]["hihat"]), 16 - 0.25, 0.01);
        check("kick microtiming sd (exact)", num(r["microtiming"]["kick"]["sd_ms"]), 0, 0.01);
        check("humanization timing_sd_ms (exact)", num(r["humanization"]["timing_sd_ms"]), 0, 0.01);
        check("humanization score (machine)", num(r["humanization"]["score"]), 0, 15);
    }

    // ---- C: drift 118 -> 124 over the song (tracked grid): one section, +6 BPM change
    Opts c;
    c.bpm = [](int i) { return 118 + 6.0 * std::min(i, 128) / 128; }; c.jitter_ms = 4; c.seed = 23;
    const Song sc = make_song(c);
    {
        const json r = ds::analyze_groove(sc.drums, sc.bass, ds::track_beats(sc.drums, sc.bass, sc.duration), sc.duration);
        std::printf("C drift 118 -> 124 (tracked grid)\n");
        check("tempo change_bpm", num(r["tempo"]["change_bpm"]), 6, 0.6);
        check("tempo sections (no split)", (double) r["tempo"]["sections"].size(), 1, 0);
        check("tempo min_bpm", num(r["tempo"]["min_bpm"]), 118.1, 1.0);
        check("tempo max_bpm", num(r["tempo"]["max_bpm"]), 123.9, 1.0);
    }
    // ---- D: sudden 120 -> 140 at bar 17 (tracked grid)
    Opts d;
    d.bpm = [](int i) { return i < 64 ? 120.0 : 140.0; }; d.jitter_ms = 4; d.seed = 24;
    const Song sd = make_song(d);
    {
        const json r = ds::analyze_groove(sd.drums, sd.bass, ds::track_beats(sd.drums, sd.bass, sd.duration), sd.duration);
        std::printf("D sudden 120 -> 140 at bar 17 (tracked grid)\n");
        const auto& s = r["tempo"]["sections"];
        check("tempo sections", (double) s.size(), 2, 0);
        if (s.size() == 2) {
            check("section 1 bpm", num(s[0]["bpm"]), 120, 0.3);
            check("section 2 bpm", num(s[1]["bpm"]), 140, 0.3);
            check("section 2 start_bar", num(s[1]["start_bar"]), 17, 0);
        }
        for (auto& f : r["findings"]) std::printf("    finding: %s\n", f.get<std::string>().c_str());
    }
    std::printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
