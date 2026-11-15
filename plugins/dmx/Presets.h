// Omega DMX presets and dice in C++: the 100 factory presets and the dice rules come from dmx/presets.js (generated into
// Params.inc by dmx/tools/gen.mjs); dice() draws exactly as presets.js dice() does for the same seed (dmx/tools/compare.mjs).
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include "Params.inc"

namespace dmx::presets {

struct Rng { uint32_t x; explicit Rng(uint32_t seed) : x(seed ? seed : 1) {}
    double operator()() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x / 4294967296.0; } };

// part: 0 everything, then kDicePartNames[1..]: voices, mods, eq, env, fx, master
inline std::vector<double> dice(const double* p, int part, uint32_t seed) {
    std::vector<double> out(p, p + kParams); Rng r(seed);
    for (int set = 0; set < kDiceParts - 1; ++set) {
        if (part != 0 && set != part - 1) continue;
        const DiceRule* rules = kDiceRules[set]; const int n = kDiceCounts[set];
        for (int i = 0; i < n; ++i) out[(size_t) rules[i].param] = kParamDefs[rules[i].param].def;
        for (int i = 0; i < n; ++i) {
            const DiceRule& R = rules[i];
            if (r() >= R.chance) continue;
            const double u = r(); const ParamDef& q = kParamDefs[R.param]; double v = out[(size_t) R.param];
            switch (R.kind) {
                case 0: v = R.a + (R.b - R.a) * u; break;
                case 1: v = R.a * std::pow(R.b / R.a, u); break;
                case 2: v = std::floor(R.a + (R.b - R.a + 1) * u); break;
                case 3: v = R.a; break;
                case 4: v = v + R.a + (R.b - R.a) * u; break;
                case 5: v = v * (R.a + (R.b - R.a) * u); break;
            }
            out[(size_t) R.param] = std::max(q.min, std::min(q.max, std::floor(v * 1000 + 0.5) / 1000));
        }
    }
    return out;
}

}  // namespace dmx::presets
