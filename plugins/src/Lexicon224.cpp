#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_lx(s);
    s.name = "Omega " + s.name; s.code = "Olx2"; s.accent = 0xff7fb2ff;
    s.knob("omix", "Dry/Wet", 0, 1, .01, 0.3, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Verb224(sp)); };
    return s;
}
}  // namespace omega
