#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_pc(s);
    s.name = "Omega " + s.name; s.code = "Opcs"; s.accent = 0xffe04848;
    s.knob("omix", "Dry/Wet", 0, 1, .01, 0.6, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::PCSim(sp)); };
    return s;
}
}  // namespace omega
