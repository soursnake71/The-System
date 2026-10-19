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
    void tone (int s) { src[(size_t) s] = in[(size_t) s].data(); }
    double run (int sigIdx, int blocks = 200)
    {
        double rms = 0; long n = 0; long ph = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int s = 0; s < IN_NSRC; ++s) if (src[(size_t) s]) for (int i = 0; i < 512; ++i) in[(size_t) s][(size_t) i] = 0.1f * (float) std::sin (2 * PI * 1000 * (ph + i) / 48000.0);
            ph += 512; e.process (512, src.data(), outp.data());
            if (b > blocks / 2) for (int i = 0; i < 512; ++i) { const double v = out[(size_t) sigIdx][(size_t) i]; rms += v * v; ++n; }
        }
        return 10 * std::log10 (rms / n * 2) + 24;
    }
};
static int fails = 0;
static void check (const char* what, double got, double want, double tol = .1)
{
    const bool ok = std::abs (got - want) <= tol; if (! ok) ++fails;
    std::printf ("%-44s %8.2f  (want %6.2f) %s\n", what, got, want, ok ? "ok" : "FAIL");
}
static float modParamIndex (const char* type, const char* name) { const auto* d = moduleDef (type); for (int j = 0; j < d->nParams; ++j) if (std::string (d->params[j].name) == name) return (float) j; return -1; }
int main()
{
    { Rig r; r.tone (IN_LINE9); check ("line 9 -> Mix A L", r.run (OUT_MIXA), 1.0); }
    { Rig r; r.tone (IN_MIC1); r.set ("m1.gain", (44 - 15) / 60.f); check ("mic 1, 44 dB, 150 ohm into 1.2k", r.run (OUT_MIXA), -0.02); }
    { Rig r; r.tone (IN_LINE9); r.set ("m9.fader", .374f); check ("channel fader at the -10 mark", r.run (OUT_MIXA), -9.0); }
    { Rig r; r.tone (IN_LINE9); r.set ("A.fader", .572f); check ("Mix A fader at the -20 mark", r.run (OUT_MIXA), -19.0); }
    { Rig r; r.tone (IN_SLOTIN1 + 8); r.set ("m9.s500", 1); r.set ("slot9." + std::to_string ((int) modParamIndex ("vhdpre", "gain")), (44 - 20) / 55.f); check ("VHD Pre in slot 9 via 500 SLOT IN, 44 dB", r.run (OUT_MIXA), -0.02); }
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
    std::printf (fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
