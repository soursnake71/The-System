// Omega REV7: the Yamaha REV7 digital reverberator (1985). Engine: dsp/Rev7.h. Panel: src/Rev7Editor.cpp.
//
// Factory presets 1-30 carry the values in the REV7 service manual's "Memory (ROM) contents" chart. Where the
// chart leaves a value out (no REV TIME on presets 16-18, a few unreadable cells), the value here is an estimate.
// The artist presets are settings made for this plugin from what these musicians and engineers have said about
// using a REV7 (sources in the README); they are not the artists' own saved programs.
#include "../common/Spec.h"
#include "../dsp/Rev7.h"

namespace omega {

static const char* kNames[30] = {
    "LARGE HALL", "SMALL HALL", "VOCAL PLATE", "PERCUSSION PLATE", "EARLY REF. 1", "EARLY REF. 2", "DELAY L,R", "STEREO ECHO",
    "STEREO FLANGE", "REVERB FLANGE", "CHORUS A", "CHORUS B", "STEREO PHASING", "TREMOLO", "SYMPHONIC", "REVERB ROOM",
    "ECHO ROOM", "STRINGS", "ELECTRIC BASS A", "ELECTRIC BASS B", "KICK", "SNARE", "GATE REVERB", "REVERSE GATE",
    "PIANO", "ORGAN", "BRASS", "GUITAR", "HANDCLAPS", "LIVE REFERENCE"};

const char* rev7Name(int n) { return n >= 1 && n <= 30 ? kNames[n - 1] : ""; }

Spec makeSpec() {
    Spec s;
    s.name = "Omega REV7"; s.sub = "Digital reverberator, 31.25 kHz / 16-bit"; s.code = "Orv7"; s.accent = 0xff6fd08c; s.panel = 0xff17181b;
    std::vector<Choice> progs; for (int n = 1; n <= 30; ++n) progs.push_back({std::to_string(n) + " " + kNames[n - 1], (double) n});
    s.choice("prog", "Program", progs, 0);
    // the edit keys (what each shows depends on the program's type, as on the unit)
    s.knob("rt", "Rev time", .3, 10, .1, 2.6, Fmt::sec);
    s.knob("hi", "High RT ratio", .1, 1, .05, .3, Fmt::n1);
    s.knob("lo", "Low RT ratio", .1, 2.4, .05, 1.2, Fmt::n1);
    s.knob("diff", "Diffusion", 0, 10, 1, 5, Fmt::n1);
    s.knob("idly", "Initial delay", .1, 100, .1, 30, Fmt::ms);
    s.knob("fdly", "1st ref delay", .1, 100, .1, 10, Fmt::ms);
    s.knob("flev", "1st ref level", 0, 1, .01, 0, Fmt::pct);
    s.knob("live", "Liveness", 0, 10, 1, 5, Fmt::n1);
    s.choice("mode", "E/R mode", {{"1 Small hall", 1}, {"2 Large hall", 2}, {"3 Random", 3}, {"4 Reverse", 4}, {"5 Plate", 5}, {"6 Spring", 6}}, 0);
    s.knob("size", "Room size", .1, 20, .1, 2, Fmt::n1);
    s.knob("dlyL", "L delay", .1, 900, .1, 100, Fmt::ms);
    s.knob("dlyR", "R delay", .1, 900, .1, 200, Fmt::ms);
    s.knob("dlev", "Delay level", 0, 1, .01, 1, Fmt::pct);
    s.knob("fb", "Feedback gain", 0, .99, .01, .6, Fmt::pct);
    s.knob("hdamp", "High damp", 0, 10, 1, 5, Fmt::n1);
    s.knob("mfreq", "Mod freq", .1, 20, .1, 1.1, Fmt::hzv);
    s.knob("mdep", "Mod depth", 0, 1, .01, .5, Fmt::pct);
    s.knob("mdly", "Mod delay", .1, 100, .1, 1.2, Fmt::ms);
    s.knob("amdep", "AM depth", 0, 1, .01, .4, Fmt::pct);
    // the knobs and switches along the bottom of the panel
    s.knob("in", "Panel: Input level", 0, 10, .1, 8, Fmt::n1);
    s.toggle("mono", "Panel: Mono", false);
    s.toggle("eq", "Panel: EQ on", false);
    s.knob("eqlf", "EQ: Lo freq", 50, 700, 1, 100, Fmt::hz);
    s.knob("eqlg", "EQ: Lo level", -15, 15, .5, 0, Fmt::db1);
    s.knob("eqmf", "EQ: Mid freq", 350, 7000, 1, 1000, Fmt::hz);
    s.knob("eqmg", "EQ: Mid level", -15, 15, .5, 0, Fmt::db1);
    s.knob("eqhf", "EQ: Hi freq", 2000, 20000, 10, 8000, Fmt::hz);
    s.knob("eqhg", "EQ: Hi level", -15, 15, .5, 0, Fmt::db1);
    s.knob("omix", "Panel: Mixing", 0, 1, .01, .35, Fmt::pct);
    s.toggle("mute", "Panel: Mute", false);
    s.toggle("phase", "Panel: Out phase", false);
    s.toggle("bypass", "Panel: Bypass", false);
    s.toggle("circ", "Panel: Vintage converters", true);
    s.knob("ret", "Panel: Output", 0, 1.5, .01, 1, Fmt::pct);

    using V = std::vector<std::pair<std::string, double>>;
    auto P = [&](int n, V v) { v.insert(v.begin(), {"prog", (double) n}); s.preset(std::to_string(n) + " " + kNames[n - 1], v); };
    // REV: rt, idly, fdly, flev, hi, lo, diff
    auto rev = [](double rt, double id, double hi, double lo, double df, double fd = .1, double fl = 0) {
        return V {{"rt", rt}, {"idly", id}, {"fdly", fd}, {"flev", fl}, {"hi", hi}, {"lo", lo}, {"diff", df}}; };
    // E/R: live, idly, flev, mode, size, diff
    auto er = [](double live, double id, double fl, int mode, double size, double df) {
        return V {{"live", live}, {"idly", id}, {"fdly", .1}, {"flev", fl}, {"mode", (double) mode}, {"size", size}, {"diff", df}}; };
    P(1, rev(2.6, 30, .3, 1.2, 5, 10));
    P(2, rev(2.0, 20, .4, 1.0, 5, 10));
    P(3, rev(2.4, 45, .3, 1.0, 5, 10));
    P(4, rev(2.0, 10, .5, 1.2, 5, 10));
    P(5, er(5, 10, 0, 1, 2.0, 5));
    P(6, er(5, 10, 0, 1, 2.0, 5));
    P(7, {{"idly", .1}, {"fdly", .1}, {"dlyL", 100}, {"dlyR", 200}, {"dlev", 1}});
    P(8, {{"fb", .6}, {"idly", .1}, {"dlyL", 170}, {"dlyR", 178}, {"hdamp", 9}});
    P(9, {{"fb", .35}, {"idly", .1}, {"mfreq", 2.5}, {"mdep", .5}, {"mdly", 1.2}});
    P(10, {{"rt", 2.5}, {"idly", .1}, {"mfreq", 1.1}, {"mdep", .8}, {"mdly", 1.2}});
    P(11, {{"idly", .1}, {"mfreq", .2}, {"mdep", .5}, {"amdep", .4}});
    P(12, {{"idly", .1}, {"mfreq", .6}, {"mdep", .5}, {"amdep", .1}});
    P(13, {{"idly", .1}, {"mfreq", 1.1}, {"mdep", .5}, {"mdly", 1.0}});
    P(14, {{"idly", .1}, {"mfreq", 5.0}, {"mdep", .5}});
    P(15, {{"idly", .1}, {"mfreq", .6}, {"mdep", .5}});
    P(16, rev(1.6, 25, .2, 1.0, 5));
    P(17, rev(1.8, 16, .3, 1.0, 5));
    P(18, rev(2.2, 13, .3, 1.0, 5));
    P(19, er(5, 12, .65, 5, 2.0, 5));
    P(20, er(3, 12, .68, 5, 2.0, 5));
    P(21, er(1, 12, .87, 5, 2.0, 5));
    P(22, rev(1.2, 10, .8, 1.0, 5));
    P(23, er(5, 20, 0, 3, 1.6, 5));
    P(24, er(5, 25, 0, 4, 2.4, 5));
    P(25, er(3, 12, .72, 6, 2.0, 5));
    P(26, er(4, 10, .61, 1, 2.0, 7));
    P(27, er(4, 12, .61, 3, .9, 5));
    P(28, er(5, 5, .69, 6, 1.5, 5));
    P(29, rev(.4, .1, .1, 2.0, 5));
    P(30, er(5, 20, 0, 2, 2.5, 5));

    // artists who have talked about using a REV7 (see README for where); settings are this plugin's interpretation
    auto A = [&](const std::string& name, int n, V v) { v.insert(v.begin(), {"prog", (double) n}); s.preset(name, v); };
    A("Jean-Michel Jarre - synth hall", 1, {{"rt", 5.5}, {"idly", 40}, {"hi", .45}, {"lo", 1.4}, {"diff", 8}, {"omix", .45}});
    A("Rick Wakeman - cathedral keys", 1, {{"rt", 8.5}, {"idly", 60}, {"hi", .35}, {"lo", 1.6}, {"diff", 9}, {"omix", .45}});
    A("Tony Banks - reverb flange pads", 10, {{"rt", 3.2}, {"mfreq", .4}, {"mdep", .6}, {"mdly", 2.5}, {"omix", .4}});
    A("Tony Banks - symphonic strings", 15, {{"mfreq", .45}, {"mdep", .65}, {"omix", .5}});
    A("The Prodigy - live snare verb", 22, {{"rt", 1.4}, {"idly", 8}, {"hi", .7}, {"eq", 1}, {"eqhf", 6000}, {"eqhg", 4}, {"eqlf", 200}, {"eqlg", -6}, {"omix", .3}});
    A("The Prodigy - gated snare", 23, {{"live", 6}, {"idly", 6}, {"size", 1.4}, {"diff", 8}, {"omix", .35}});
    A("Graham Massey (808 State) - short pre-delay", 2, {{"rt", 1.1}, {"idly", 2}, {"hi", .5}, {"diff", 6}, {"omix", .3}});
    A("Graham Massey (808 State) - slap", 7, {{"idly", .1}, {"dlyL", 28}, {"dlyR", 41}, {"dlev", .8}, {"omix", .3}});
    A("Charlie Burchill (Simple Minds) - guitar echo", 8, {{"fb", .45}, {"dlyL", 340}, {"dlyR", 345}, {"hdamp", 6}, {"omix", .3}});
    A("Charlie Burchill (Simple Minds) - big guitar", 1, {{"rt", 3.4}, {"idly", 35}, {"hi", .4}, {"diff", 7}, {"omix", .35}});
    A("Martyn Ware - synth chorus", 11, {{"mfreq", .35}, {"mdep", .6}, {"amdep", .2}, {"omix", .5}});
    A("Daniel Miller - electronic perc plate", 4, {{"rt", 1.2}, {"idly", 5}, {"hi", .6}, {"lo", .8}, {"diff", 6}, {"omix", .3}});
    A("Paul Hardcastle - vocal plate", 3, {{"rt", 1.8}, {"idly", 30}, {"hi", .45}, {"diff", 6}, {"omix", .3}});
    A("Minoru Mukaiya (Casiopea) - fusion keys", 18, {{"rt", 2.0}, {"idly", 18}, {"hi", .4}, {"omix", .3}});

    s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Rev7(sp)); };
    return s;
}

}  // namespace omega
