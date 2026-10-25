#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_h9(s);
    s.name = "Omega " + s.name; s.code = "Oh91"; s.accent = 0xff60a8ff;
    s.knob("omix", "Dry/Wet", 0, 1, .01, 0.5, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::H910(sp)); };
    return s;
}
}  // namespace omega
