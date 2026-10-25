#include "../common/Spec.h"
#include "../dsp/Inserts.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s;
    s.name = "Omega Bus Compressor"; s.sub = "G-series style quad bus compressor: glue for the mix bus"; s.code = "Obus"; s.accent = 0xffd8d8d0; s.panel = 0xff23262c;
    s.knob("thr", "Threshold", -30, 0, .5, -12, Fmt::db1);
    s.choice("ratio", "Ratio", {{"2:1", 2}, {"4:1", 4}, {"10:1", 10}}, 1);
    s.choice("atk", "Attack", {{"0.1 ms", 0}, {"0.3 ms", 1}, {"1 ms", 2}, {"3 ms", 3}, {"10 ms", 4}, {"30 ms", 5}}, 3);
    s.choice("rel", "Release", {{"0.1 s", 0}, {"0.3 s", 1}, {"0.6 s", 2}, {"1.2 s", 3}, {"Auto", 4}}, 4);
    s.knob("makeup", "Make-up", 0, 15, .5, 0, Fmt::db1);
    s.choice("hp", "S/C HPF", {{"Off", 0}, {"30 Hz", 30}, {"60 Hz", 60}, {"90 Hz", 90}, {"120 Hz", 120}, {"150 Hz", 150}}, 0);
    s.knob("mix", "Mix", 0, 1, .05, 1, Fmt::pct);
    s.preset("Mix bus glue (4:1, 10 ms, Auto)", {{"thr", -14}, {"ratio", 4}, {"atk", 4}, {"rel", 4}, {"makeup", 2}, {"hp", 0}, {"mix", 1}});
    s.preset("Gentle glue (2:1, 30 ms, Auto)", {{"thr", -10}, {"ratio", 2}, {"atk", 5}, {"rel", 4}, {"makeup", 1}, {"hp", 0}, {"mix", 1}});
    s.preset("Hip-hop bus (4:1, 30 ms, 0.1 s)", {{"thr", -16}, {"ratio", 4}, {"atk", 5}, {"rel", 0}, {"makeup", 3}, {"hp", 60}, {"mix", 1}});
    s.preset("Drum smash (10:1, 1 ms, 0.1 s)", {{"thr", -24}, {"ratio", 10}, {"atk", 2}, {"rel", 0}, {"makeup", 8}, {"hp", 0}, {"mix", .5}});
    s.preset("Punch (4:1, 30 ms, 0.3 s)", {{"thr", -18}, {"ratio", 4}, {"atk", 5}, {"rel", 1}, {"makeup", 4}, {"hp", 90}, {"mix", 1}});
    s.preset("Master safety (2:1, 3 ms, Auto)", {{"thr", -6}, {"ratio", 2}, {"atk", 3}, {"rel", 4}, {"makeup", 0}, {"hp", 0}, {"mix", 1}});
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::SSLBus(sp)); };
    return s;
}
}  // namespace omega
