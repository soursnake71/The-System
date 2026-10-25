#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_mx(s);
    s.name = "Omega " + s.name; s.code = "Omx1"; s.accent = 0xffff8a3d;
    s.knob("omix", "Dry/Wet", 0, 1, .01, 0.35, Fmt::pct);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::MXR113(sp)); };
    return s;
}
}  // namespace omega
