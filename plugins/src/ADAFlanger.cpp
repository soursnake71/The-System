#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_ad(s);
    s.name = "Omega " + s.name; s.code = "Oada"; s.accent = 0xff9cd65a;
    s.knob("omix", "Dry/Wet", 0, 1, .01, 1.0, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::ADAFlanger(sp)); };
    return s;
}
}  // namespace omega
