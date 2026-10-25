#include "../common/Spec.h"
#include "../dsp/Rack.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::rack_u7(s);
    s.name = "Omega " + s.name; s.code = "O176"; s.accent = 0xffc9c9c9;
    s.remove("ret");
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::UREI1176(sp)); };
    return s;
}
}  // namespace omega
