#include "../common/Spec.h"
#include "../dsp/Vocoder.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s;
    s.name = "Omega Vocoder 350"; s.sub = "SVC-350 style. Your track is the voice; play MIDI for the built-in carrier, or use the sidechain";
    s.code = "Ovoc"; s.midiIn = true; s.sidechain = true; s.accent = 0xffff7a2e; s.panel = 0xff1b1c20;
    s.choice("carrier", "Carrier: Carrier", {{"Built-in Prophet-5 (MIDI)", 0}, {"Sidechain input", 1}}, 0);
    s.choice("drone", "Carrier: Hold a note", {{"Off (MIDI only)", 0}, {"C1", 36}, {"C2", 48}, {"C3", 60}, {"C4", 72}}, 2);
    s.knob("ccut", "Carrier: Brightness", 0, 1, 0, .95, Fmt::cutoff);
    s.knob("cnoise", "Carrier: Noise", 0, 1, 0, .03, Fmt::k10);
    addVocoder(s);
    for (const auto& p : vocPresets()) s.preset(p.name, vocPresetValues(p, false));
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::SVC350Fx(sp)); };
    return s;
}
}  // namespace omega
