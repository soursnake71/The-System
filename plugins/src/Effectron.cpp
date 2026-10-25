#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_ef(s);
    s.name = "Omega " + s.name; s.code = "Oefx"; s.accent = 0xffffc040;
    s.knob("omix", "Dry/Wet", 0, 1, .01, 0.5, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Effectron(sp)); };
    return s;
}
}  // namespace omega
