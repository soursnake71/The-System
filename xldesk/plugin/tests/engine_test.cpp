// Checks the C++ engine against the same reference levels as the web engine (xldesk/dsp/console.js).
// Build: g++ -std=c++17 -O2 -I../Source engine_test.cpp ../Source/Engine.cpp ../Source/Modules.cpp
#include "Engine.h"
#include <cstdio>
#include <vector>
#include <string>
using namespace xl;

struct Rig
{
    Engine e; std::vector<float> P; std::vector<std::vector<float>> in; std::vector<const float*> src; std::vector<std::vector<float>> out; std::vector<float*> outp;
    Rig()
    {
        P.resize (xlspec::NPARAMS); for (int i = 0; i < xlspec::NPARAMS; ++i) P[(size_t) i] = xlspec::PARAMS[i].def;
        e.setParams (P.data()); e.prepare (48000, 512);
        in.assign (IN_NSRC, std::vector<float> (512)); src.assign (IN_NSRC, nullptr); out.assign (OUT_NSIG, std::vector<float> (512)); outp.resize (OUT_NSIG); for (int i = 0; i < OUT_NSIG; ++i) outp[(size_t) i] = out[(size_t) i].data();
    }
    void set (const std::string& id, float v) { const int i = Engine::paramIndex (id); if (i < 0) { std::printf ("no param %s\n", id.c_str()); return; } P[(size_t) i] = v; }
    double freq = 1000, amp = .1; std::vector<float> polarity = std::vector<float> (IN_NSRC, 1.f);
    void tone (int s) { src[(size_t) s] = in[(size_t) s].data(); }
    // fit a module and give it its defaults, as the UI does
    void setType (const char* w, const char* t)
    {
        e.setType (Engine::whereIndex (w), t); const auto* d = moduleDef (t);
        for (int j = 0; d && j < d->nParams; ++j) set (std::string (w) + "." + std::to_string (j), d->params[j].def);
        e.process (16, src.data(), outp.data());   // the swap lands on the next block
    }
    void mp (const char* w, const char* type, const char* name, float v) { const auto* d = moduleDef (type); for (int j = 0; j < d->nParams; ++j) if (std::string (d->params[j].name) == name) set (std::string (w) + "." + std::to_string (j), v); }
    double run (int sigIdx, int blocks = 200)
    {
        double rms = 0; long n = 0; long ph = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int s = 0; s < IN_NSRC; ++s) if (src[(size_t) s]) for (int i = 0; i < 512; ++i) in[(size_t) s][(size_t) i] = polarity[(size_t) s] * (float) amp * (float) std::sin (2 * PI * freq * (ph + i) / 48000.0);
            ph += 512; e.process (512, src.data(), outp.data());
            if (b > blocks / 2) for (int i = 0; i < 512; ++i) { const double v = out[(size_t) sigIdx][(size_t) i]; rms += v * v; ++n; }
        }
        return 10 * std::log10 (rms / n * 2 + 1e-30) + 24;
    }
};
static int fails = 0;
static void check (const char* what, double got, double want, double tol = .1)
{
    const bool ok = std::abs (got - want) <= tol; if (! ok) ++fails;
    std::printf ("%-44s %8.2f  (want %6.2f) %s\n", what, got, want, ok ? "ok" : "FAIL");
}
// silence: at or below the noise floor
static void checkSilent (const char* what, double got) { const bool ok = got <= -60; if (! ok) ++fails; std::printf ("%-44s %8.2f  (want <= -60) %s\n", what, got, ok ? "ok" : "FAIL"); }
static float modParamIndex (const char* type, const char* name) { const auto* d = moduleDef (type); for (int j = 0; j < d->nParams; ++j) if (std::string (d->params[j].name) == name) return (float) j; return -1; }
int main()
{
    { Rig r; r.tone (IN_LINE9); check ("line 9 -> Mix A L", r.run (OUT_MIXA), 1.0); }
    { Rig r; r.tone (IN_MIC1); r.set ("m1.gain", (44 - 15) / 60.f); check ("mic 1, 44 dB, 150 ohm into 1.2k", r.run (OUT_MIXA), -0.02); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.fader", .374f); check ("channel fader at the -10 mark", r.run (OUT_MIXA), -9.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("A.fader", .572f); check ("Mix A fader at the -20 mark", r.run (OUT_MIXA), -19.0); }
    { Rig r; r.tone (IN_SLOTIN1 + 8); r.set ("m9.s500", 1); r.mp ("slot9", "vhdpre", "gain", (44 - 20) / 55.f); check ("VHD Pre in slot 9 via 500 SLOT IN, 44 dB", r.run (OUT_MIXA), -0.02); }
    { Rig r; r.tone (IN_SLOTIN1 + 8); r.set ("slot9." + std::to_string ((int) modParamIndex ("vhdpre", "gain")), (44 - 20) / 55.f); check ("500 SLOT OUT 9", r.run (OUT_SLOTOUT1 + 8), 2.98); }
    {
        Rig r; r.e.setType (Engine::whereIndex ("slot9"), "eeq"); r.tone (IN_LINE9);
        const auto* d = moduleDef ("eeq"); for (int j = 0; j < d->nParams; ++j) r.set ("slot9." + std::to_string (j), d->params[j].def);
        r.set ("m9.s500", 1); r.set ("slot9.ext", 0); r.set ("slot9.3", 1); r.set ("slot9.4", (float) (std::log (1000.0 / 600) / std::log (7000.0 / 600)));
        check ("E-EQ in slot 9, HMF +15 dB at 1 kHz", r.run (OUT_MIXA), 16.0);
    }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.ins", 1); r.set ("ins9.0", 1); r.set ("ins9.2", 1); check ("E-DYN limiter on INS 9", r.run (OUT_MIXA, 400), 0.95); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.trim", 1); r.set ("A.comp", 1); r.run (OUT_MIXA, 400); check ("G-COMP gain reduction (needle)", r.e.meters.gcomp.load(), 13.9, .3); }
    { Rig r; r.tone (IN_DAW1); r.set ("m1.daw", 1); r.set ("m1.B", 1); r.set ("m1.A", 0); r.set ("B.toA", 1); check ("DAW 1 -> Mix B -> TO A", r.run (OUT_MIXA), 1.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.solo", 1); r.tone (IN_LINE9 + 1); check ("solo-in-place cuts the others", r.run (OUT_MIXA), 1.0); }

    std::printf ("\n-- published specs (Owner's Manual, Appendix E) --\n");
    { Rig r; r.tone (IN_LINE9); r.freq = 20; check ("frequency response 20 Hz (+/-0.2 dB)", r.run (OUT_CHOUT1 + 8, 400), 4.0, .2); }
    { Rig r; r.tone (IN_LINE9); r.freq = 20000; check ("frequency response 20 kHz (+/-0.2 dB)", r.run (OUT_CHOUT1 + 8), 4.0, .2); }
    { Rig r; r.tone (IN_LINE9); r.amp = 1e-9; const double n = r.run (OUT_CHOUT1 + 8); check ("noise, line in to channel out (spec < -86 dBu)", n, -86, .5); }
    { Rig r; r.amp = 0; for (int i = 1; i <= 16; ++i) r.set ("m" + std::to_string (i) + ".A", 0); for (int k = 1; k <= 4; ++k) r.set ("s" + std::to_string (k) + ".A", 0); r.set ("m1.A", 1); const double n = r.run (OUT_MIXA); check ("noise, single fader to mix bus (spec < -83 dBu)", n, -84.2, .5); }
    { Rig r; r.tone (IN_LINE9); r.amp = 1.25; check ("headroom: +26 dBu passes clean (> +24 dBu)", r.run (OUT_CHOUT1 + 8), 25.94, .1); }

    std::printf ("\n-- channel strip --\n");
    { Rig r; r.tone (IN_LINE9); r.set ("m9.trim", 1); check ("trim +20 dB", r.run (OUT_CHOUT1 + 8), 24.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.trim", 0); check ("trim -20 dB", r.run (OUT_CHOUT1 + 8), -16.0); }
    { Rig r; r.tone (IN_LINE9); r.tone (IN_LINE9 + 1); r.set ("m10.phase", 1); checkSilent ("polarity: ch 9 + inverted ch 10 cancel", r.run (OUT_MIXA)); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.fader", .374f); check ("CHOP POST off: direct out pre-fader", r.run (OUT_CHOUT1 + 8), 4.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.fader", .374f); r.set ("m9.chop", 1); check ("CHOP POST on: direct out post-fader (-10)", r.run (OUT_CHOUT1 + 8), -6.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.pan", 0); check ("pan hard left: full level on L", r.run (OUT_MIXA), 4.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.pan", 0); checkSilent ("pan hard left: nothing on R", r.run (OUT_MIXA + 1)); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.cut", 1); checkSilent ("CUT", r.run (OUT_MIXA)); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.A", 0); r.set ("m9.D", 1); check ("route to Mix D only", r.run (OUT_MIXD), 1.0); }
    { Rig r; r.tone (IN_LINE9); r.tone (IN_DAW1 + 8); r.amp = .1; r.set ("m9.daw", 1); r.set ("m9.alt", 1); r.set ("m9.cue", .75f); r.set ("m9.A", 0); r.polarity[(size_t) IN_LINE9] = 0; checkSilent ("ALT: cue takes the input not selected", r.run (OUT_CUE)); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.daw", 1); r.set ("m9.alt", 1); r.set ("m9.cue", .75f); check ("ALT: line on cue while DAW is the main input", r.run (OUT_CUE), 1.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.cue", .75f); r.set ("m9.fader", 0); check ("cue pre-fader (default)", r.run (OUT_CUE), 1.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.cue", .75f); r.set ("m9.fader", 0); r.set ("m9.cuepost", 1); checkSilent ("cue POST follows the fader", r.run (OUT_CUE)); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.aux1", .75f); check ("aux 1 post-fader (default)", r.run (OUT_AUX1), 4.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.aux1", .75f); r.set ("m9.fader", 0); r.set ("aux1.pre", 1); check ("aux 1 PRE (global)", r.run (OUT_AUX1), 4.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.aux2", 1); check ("aux send fully up: +6 dB", r.run (OUT_AUX2), 10.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.aux1", .75f); r.set ("aux1.level", 1); check ("aux master +10 dB", r.run (OUT_AUX1), 14.0); }

    std::printf ("\n-- VHD preamps --\n");
    { Rig r; r.tone (IN_MIC1); r.set ("m1.gain", (44 - 15) / 60.f); r.set ("m1.hiz", 1); check ("HI-Z: 150 ohm into 10k loses 0.13 dB", r.run (OUT_MIXA), .87); }
    { Rig r; r.tone (IN_MIC1); r.set ("m1.gain", (64 - 15) / 60.f); r.set ("m1.pad", 1); check ("PAD -20 dB", r.run (OUT_MIXA), -.02); }
    { Rig r; r.tone (IN_MIC1); r.set ("m1.gain", (44 - 15) / 60.f); r.set ("m1.vhd", 1); check ("VHD in at +4 dBu: level holds (within 1.5 dB)", r.run (OUT_MIXA), -.02, 1.5); }
    { Rig r; r.tone (IN_MIC1); r.set ("m1.gain", 1); r.set ("m1.vhd", 1); r.set ("m1.trim", 0); check ("VHD +75 dB on a -40 dBu mic saturates (linear: +15)", r.run (OUT_CHOUT1), 3.5, 4); }

    std::printf ("\n-- 500 rack --\n");
    { Rig r; r.tone (IN_LINE9); r.set ("m9.s500", 1); r.set ("slot9.ext", 0); r.setType ("slot9", "empty"); checkSilent ("empty slot: 500 returns silence", r.run (OUT_MIXA)); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.s500", 1); r.set ("slot9.ext", 0); r.set ("rear.unlink2", 1); checkSilent ("links removed: 500 returns silence", r.run (OUT_MIXA)); }
    { Rig r; r.tone (IN_LINE9 + 2); r.set ("m11.s500", 1); r.set ("B.s500", 1); checkSilent ("Mix B steals slots 11-12: ch 11's 500 is silent", r.run (OUT_MIXA)); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.A", 0); r.set ("m9.B", 1); r.set ("B.s500", 1); r.setType ("slot11", "eeq"); r.setType ("slot12", "eeq"); r.mp ("slot11", "eeq", "hmfG", 1); r.mp ("slot11", "eeq", "hmfF", (float) (std::log (1000.0 / 600) / std::log (7000.0 / 600))); check ("Mix B through slot 11 (L): HMF +15", r.run (OUT_MIXB), 16.0); }
    { Rig r; r.tone (IN_SLOTIN1 + 8); r.mp ("slot9", "vhdpre", "gain", (44 - 20) / 55.f); r.mp ("slot9", "vhdpre", "hpfIn", 1); r.mp ("slot9", "vhdpre", "hpf", (float) (std::log (100.0 / 15) / std::log (500.0 / 15))); r.freq = 50; check ("VHD Pre HPF 100 Hz, 18 dB/oct: -18 dB at 50 Hz", r.run (OUT_SLOTOUT1 + 8, 400), 2.98 - 18.1, .6); }
    { Rig r; r.tone (IN_SLOTIN1 + 8); r.mp ("slot9", "vhdpre", "gain", (44 - 20) / 55.f); r.mp ("slot9", "vhdpre", "trim", 0); check ("VHD Pre trim -20", r.run (OUT_SLOTOUT1 + 8), 2.98 - 20); }
    { Rig r; r.tone (IN_LINE9); r.amp = .01; r.set ("m9.ins", 1); r.mp ("ins9", "edyn", "range", 1); r.mp ("ins9", "edyn", "gthr", 1); check ("E-DYN gate closed: 40 dB range", r.run (OUT_CHOUT1 + 8, 400), -16 - 40, 1); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.ins", 1); r.setType ("ins9", "none"); checkSilent ("INS with nothing patched: silent", r.run (OUT_MIXA)); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.ins", 1); r.setType ("ins9", "loop"); check ("INS with a patch cable: unity", r.run (OUT_MIXA), 1.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("A.sum", 1); check ("Mix A SUM with a cable on its INS: +6 dB", r.run (OUT_MIXA), 7.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.trim", 1); r.run (OUT_MIXA, 100); check ("bus comp needle moves with COMP out", r.e.meters.gcomp.load(), 13.9, .5); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.trim", 1); check ("COMP out: Mix A untouched", r.run (OUT_MIXA), 21.0); }

    std::printf ("\n-- buses, solo, returns --\n");
    { Rig r; r.tone (IN_LINE9); r.set ("m9.A", 0); r.set ("m9.B", 1); r.set ("B.level", 1); check ("Mix B level +10", r.run (OUT_MIXB), 11.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.cue", .75f); r.set ("m9.A", 0); r.set ("cue.toA", 1); check ("St Cue TO A", r.run (OUT_MIXA), 1.0); }
    { Rig r; r.tone (IN_LINE9); r.tone (IN_LINE9 + 1); r.set ("m10.solo", 1); r.set ("m9.safe", 1); check ("solo safe: ch 9 survives ch 10's solo", r.run (OUT_MIXA), 7.0, .2); }
    { Rig r; r.tone (IN_LINE9); r.tone (IN_LINE9 + 1); r.set ("solo.afl", 1); r.set ("m10.solo", 1); check ("AFL mode: nothing cut on Mix A", r.run (OUT_MIXA), 7.0, .2); }
    { Rig r; r.tone (IN_LINE9); r.tone (IN_LINE9 + 1); r.set ("solo.afl", 1); r.set ("m10.solo", 1); r.set ("mon.level", 1); check ("AFL mode: monitors hear only the solo", r.run (OUT_MAIN), 1.0); }
    { Rig r; r.tone (IN_RET1L); r.tone (IN_RET1L + 1); check ("stereo return 1 to Mix A", r.run (OUT_MIXA), 4.0); }
    { Rig r; r.tone (IN_RET1L); r.tone (IN_RET1L + 1); r.set ("r1.afl", 1); r.set ("mon.level", 1); check ("return AFL to the monitors", r.run (OUT_MAIN), 4.0); }
    { Rig r; r.tone (IN_ST1L); r.set ("s1.mono", 1); check ("stereo ch MONO (L-MONO), panned centre", r.run (OUT_MIXA + 1), 1.0); }

    std::printf ("\n-- monitoring and foldback --\n");
    { Rig r; r.tone (IN_LINE9); r.set ("mon.level", 1); check ("monitor level full = Mix A", r.run (OUT_MAIN), 1.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("mon.level", 1); r.set ("mon.dim", 1); check ("DIM at -20", r.run (OUT_MAIN), -19.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("mon.level", 1); r.set ("mon.mini1", 1); r.set ("mon.off1", .5f + 3.f / 40); check ("MINI 1 with +3 dB trim", r.run (OUT_MINI1), 4.0); }
    { Rig r; r.tone (IN_LINE9); r.freq = 40; r.set ("mon.level", 1); r.set ("mon.subMain", 1); check ("SUB: 40 Hz goes to the sub", r.run (OUT_SUB, 400), 7.0, .5); }
    { Rig r; r.tone (IN_LINE9); r.freq = 40; r.set ("mon.level", 1); r.set ("mon.subMain", 1); check ("SUB: speakers high-passed (-12 dB at 40 Hz)", r.run (OUT_MAIN, 400), 1.0 - 12.3, .6); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.cue", .75f); check ("Foldback A = St Cue", r.run (OUT_FBA), 1.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("fbA.mon", 1); check ("Foldback A MON = monitor source", r.run (OUT_FBA), 1.0); }
    { Rig r; r.tone (IN_TALK); r.amp = .1; r.set ("fbB.talk", 1); r.set ("talk.level", 2.f / 3); check ("TALK B into Foldback B (-40 dBu mic, +40 dB)", r.run (OUT_FBB), 0.0, .2); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.cue", .75f); r.set ("hp.fbA", 1); r.set ("hp.level", 1); check ("headphones on F/B A", r.run (OUT_HP), 1.0); }

    std::printf (fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
