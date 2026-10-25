#include "../common/Spec.h"
#include "../dsp/Inserts.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s;
    s.name = "Omega Gate Stepper"; s.sub = "16-step volume gate locked to the tempo (Don't You Want Me, Pure Energy chops)"; s.code = "Ogat"; s.accent = 0xffff5ea8;
    s.choice("rate", "Step: Rate", {{"1/8", 8}, {"1/16", 16}, {"1/16 triplet", 24}, {"1/32", 32}}, 1);
    s.knob("duty", "Step: Length", .05, 1, .01, .75, Fmt::pct);
    s.knob("depth", "Step: Depth", 0, 1, .01, 1, Fmt::pct);
    s.knob("atk", "Step: Attack", .5, 30, .5, 2, Fmt::msv);
    s.knob("rel", "Step: Release", 1, 200, 1, 25, Fmt::msv);
    s.knob("swing", "Step: Swing", 0, 1, .01, 0, Fmt::pct);
    s.knob("mix", "Step: Mix", 0, 1, .01, 1, Fmt::pct);
    const double d[16] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
    for (int k = 0; k < 16; ++k) s.choice("s" + std::to_string(k + 1), "Steps: " + std::to_string(k + 1), {{"Off", 0}, {"Soft", .4}, {"Mid", .6}, {"Full", 1}}, d[k] > 0 ? 3 : 0);
    struct P { const char* n; double st[16]; double rate, duty, atk, rel, swing; };
    const P ps[] = {
        {"Synth-pop 8ths (Don't You Want Me)", {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0}, 16, .75, .002, .025, 0},
        {"Pure Energy gated guitar", {1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, .6, 1, 0}, 16, .55, .001, .012, 0},
        {"Straight 16ths chop", {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 16, .5, .001, .01, 0},
        {"Trance gate", {1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1}, 16, .65, .002, .02, 0},
        {"Off-beat pump", {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0}, 16, .95, .01, .06, 0},
        {"Accented 16ths", {1, .4, .6, .4, 1, .4, .6, .4, 1, .4, .6, .4, 1, .4, .6, 1}, 16, .6, .001, .015, 0},
        {"Triplet chop", {1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1}, 24, .6, .001, .012, 0},
        {"Stutter 32nds", {1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0}, 32, .5, .001, .006, 0},
        {"Swing 16ths", {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 16, .55, .002, .015, .5}};
    for (const auto& p : ps) {
        std::vector<std::pair<std::string, double>> v = {{"rate", p.rate}, {"duty", p.duty}, {"atk", p.atk * 1000}, {"rel", p.rel * 1000}, {"swing", p.swing}};
        for (int k = 0; k < 16; ++k) v.push_back({"s" + std::to_string(k + 1), p.st[k]});
        s.preset(p.n, v);
    }
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::GateStep(sp)); };
    return s;
}
}  // namespace omega
