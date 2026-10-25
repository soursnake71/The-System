#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_pt(s);
    s.name = "Omega " + s.name; s.code = "Opt9"; s.accent = 0xffff6b4a;
    s.knob("omix", "Dry/Wet", 0, 1, .01, 0.3, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::PrimeTime(sp)); };
    return s;
}
}  // namespace omega
