#include "../common/Spec.h"
#include "../dsp/Vocoder.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s;
    s.name = "Omega Speech"; s.sub = "The talking machine: type words, each MIDI note speaks them";
    s.code = "Ospk"; s.synth = true; s.hasText = true; s.accent = 0xff79d68c; s.panel = 0xff15201a;
    s.defaultText = "Ahhhh, this stuff is really fresh.";
    s.textHint = "Type the words. Each MIDI note speaks them (at that pitch when Keys set the pitch is on).";
    addSpeech(s, true);
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Speech(sp)); };
    return s;
}
}  // namespace omega
