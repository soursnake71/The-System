// Parameter sets shared by several plugins.
#pragma once
#include "../common/Spec.h"
#include "../gen/tables.inc"

namespace omega {

inline void addVocoder(Spec& s, bool withOnSwitch = true) {
    static const char* F[10] = {"160", "230", "330", "500", "730", "1.1k", "1.6k", "2.3k", "3.3k", "5k"};
    if (withOnSwitch) s.toggle("von", "Vocoder: Vocoder on", true);
    s.knob("bal", "Vocoder: Balance (mic)", 0, 1, .01, 0, Fmt::pct);
    s.knob("vvol", "Vocoder: Master volume", 0, 1.5, .01, 1, Fmt::pct);
    s.knob("harm", "Vocoder: Harmonics", 0, 1, .01, .5, Fmt::pct);
    s.knob("growl", "Vocoder: Growl", 0, 1, .01, 0, Fmt::pct);
    s.knob("sib", "Vocoder: S/T noise", 0, 1, .01, .7, Fmt::pct);
    s.knob("cons", "Vocoder: HF consonants", 0, 1.5, .01, .8, Fmt::pct);
    s.toggle("ens", "Vocoder: Ensemble", false);
    s.toggle("hold", "Vocoder: Hold", false);
    s.choice("clev", "Vocoder: Inst level", {{"-30 dB", -30}, {"-15 dB", -15}, {"0 dB", 0}}, 2);
    for (int k = 0; k < 10; ++k) s.knob("b" + std::to_string(k + 1), std::string("Bands: ") + F[k] + " Hz", 0, 1.5, .01, 1, Fmt::pct);
}

// the vocoder setups from the app (VOC_PRESETS)
struct VocPreset { const char* name; double growl, harm, cons, sib; bool ens; double bands[10]; const char* text; int voice; double semi, speed; };
inline const std::vector<VocPreset>& vocPresets() {
    static const std::vector<VocPreset> P = {
        {"Clean SVC", 0, .5, .8, .7, false, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, "", 0, 0, 1},
        {"Jonzun Crew (Space Is the Place)", .35, .85, 1, .85, true, {1.1, 1.1, 1, 1, 1, 1, 1.05, 1.15, 1.2, 1.25}, "Space is the place.", 0, -3, .8},
        {"Newcleus (Jam On It)", .7, .8, 1, .9, false, {1.35, 1.3, 1.2, 1.1, 1, 1, 1, 1.1, 1.15, 1.2}, "Jam on it.", 0, 0, 1},
        {"Egyptian Lover", .6, .7, 1.1, .85, false, {1.4, 1.3, 1.2, 1, 1, .9, .9, 1, 1.1, 1.2}, "Egypt, Egypt.", 0, -2, .9},
        {"Starscream", .25, 1, 1.2, 1, true, {.6, .7, .8, 1, 1.1, 1.2, 1.3, 1.3, 1.4, 1.4}, "I am Starscream.", 3, 0, 1.1},
        {"Mr. Roboto", .1, .6, .9, .7, false, {1, 1, 1, 1.05, 1.1, 1.1, 1.05, 1, 1, 1}, "I am a robot.", 0, 0, .9},
        {"Cylon Warrior", .9, .5, .6, .5, true, {1.5, 1.5, 1.35, 1.15, 1, .9, .8, .8, .75, .7}, "By your command.", 1, -5, .7},
        {"Fresh (Change the Beat)", .45, .75, 1, .85, true, {1.3, 1.2, 1.1, 1, 1, 1, 1, 1.1, 1.1, 1.2}, "Ahhhh, this stuff is really fresh.", 1, -2, .75},
        {"Herbie Hancock (Rockit)", .2, .95, 1.1, .9, false, {.85, .9, 1, 1.1, 1.2, 1.2, 1.15, 1.1, 1.1, 1.15}, "Rock it.", 2, 2, 1},
    };
    return P;
}
inline std::vector<std::pair<std::string, double>> vocPresetValues(const VocPreset& p, bool speech) {
    std::vector<std::pair<std::string, double>> v = {{"growl", p.growl}, {"harm", p.harm}, {"cons", p.cons}, {"sib", p.sib}, {"ens", p.ens ? 1 : 0}};
    for (int k = 0; k < 10; ++k) v.push_back({"b" + std::to_string(k + 1), p.bands[k]});
    if (speech) { v.push_back({"voice", p.voice}); v.push_back({"semi", p.semi}); v.push_back({"speed", p.speed}); }
    return v;
}

inline void addSpeech(Spec& s, bool keys) {
    s.choice("voice", "Speech: Voice", {{"Robot (monotone)", 0}, {"Deep", 1}, {"Normal", 2}, {"High", 3}, {"Female", 4}, {"Chipmunk", 5}}, 0);
    s.knob("semi", "Speech: Pitch", -12, 12, 1, 0, Fmt::semis);
    s.knob("speed", "Speech: Speed", .5, 1.5, .05, 1, Fmt::pct);
    s.choice("trig", "Speech: Play", {{"Once per note", 0}, {"Loop while held", 1}, {"Always loop", 2}}, keys ? 0 : 1);
    s.knob("sgain", "Speech: Level", 0, 1.5, .01, 1, Fmt::pct);
    if (keys) s.toggle("keys", "Speech: Keys set the pitch", true);
}

}  // namespace omega
