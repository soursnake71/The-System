#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_mv(s);
    s.name = "Omega " + s.name; s.code = "Omvb"; s.accent = 0xffb0b6c4;
    s.knob("omix", "Dry/Wet", 0, 1, .01, 0.3, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Microverb(sp)); };
    return s;
}
}  // namespace omega
