#include "../common/Spec.h"
#include "../dsp/Prophet5.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s; gen::prophet5(s);
    s.name = "Omega Prophet-5"; s.sub = "Sequential Circuits Rev 3 voice model, five voices"; s.code = "Op5a"; s.synth = true; s.accent = 0xffd9a05b; s.panel = 0xff201a14;
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Prophet5(sp)); };
    return s;
}
}  // namespace omega
