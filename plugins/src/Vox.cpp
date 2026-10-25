#include "../common/Spec.h"
#include "../dsp/Vocoder.h"
#include "Specs.h"

namespace omega {
Spec makeSpec() {
    Spec s;
    s.name = "Omega Vox"; s.sub = "Prophet-5 + vocoder + talking machine. Type words, play keys: the synth sings them";
    s.code = "Ovox"; s.synth = true; s.sidechain = true; s.hasText = true; s.accent = 0xffff9a3c; s.panel = 0xff1f1712;
    s.defaultText = "I am the Omega Vox.";
    s.textHint = "Type the words, then hold keys: the Prophet-5 sings them through the vocoder.";
    addSpeech(s, false);
    s.choice("modsrc", "Speech: Voice source", {{"Talking machine", 0}, {"Sidechain (mic)", 1}, {"Both", 2}}, 0);
    addVocoder(s);
    s.find("vvol")->def = .35f;
    Spec p5; gen::prophet5(p5);
    for (auto& d : p5.params) { d.name = "Prophet-5 " + d.name; s.params.push_back(d); }
    // the carrier starts on the Prophet-5 "Vocoder carrier" program
    for (const auto& pr : p5.presets) if (pr.name == "Vocoder carrier") for (auto& [k, x] : pr.values) if (auto* d = s.find(k)) d->def = (float) x;
    for (const auto& p : vocPresets()) s.preset(p.name, vocPresetValues(p, true), p.text[0] ? p.text : "I am the Omega Vox.");
    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Vox(sp)); };
    return s;
}
}  // namespace omega
