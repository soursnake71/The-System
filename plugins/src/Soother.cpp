#include "../common/Spec.h"
#include "../dsp/Inserts.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s;
    s.name = "Omega Resonance Tamer"; s.sub = "Dynamic cuts on harsh resonances, then an oversampled clipper"; s.code = "Osth"; s.accent = 0xff8fd3ff;
    s.toggle("on", "Tame resonances", true);
    s.knob("depth", "Depth", 0, 1, .01, .5, Fmt::pct);
    s.knob("sharp", "Sharpness", 0, 1, .01, .5, Fmt::pct);
    s.knob("speed", "Speed", 0, 1, .01, .5, Fmt::pct);
    s.toggle("clip", "Clipper", false);
    s.knob("drive", "Drive", 0, 12, .5, 0, Fmt::db1);
    s.knob("ceil", "Ceiling", -6, 0, .1, -.3, Fmt::db1);
    s.knob("hard", "Hardness", 0, 1, .01, .5, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Soother(sp)); };
    return s;
}
}  // namespace omega
