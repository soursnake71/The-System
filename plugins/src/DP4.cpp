#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_dp(s);
    s.name = "Omega " + s.name; s.code = "Odp4"; s.accent = 0xff52d0c8;
    s.knob("omix", "Dry/Wet", 0, 1, .01, 0.4, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::DP4(sp)); };
    return s;
}
}  // namespace omega
