/* The 500-series rack. Every module is a class behind one interface and lives in a registry keyed by the type id
   used in spec.js. To add a module or a new revision of one: describe its controls in spec.js (new type id, e.g.
   "eeq2"), regenerate Spec.generated.h, write the class here and add one line to registerModules() in Modules.cpp.
   Sessions store the type id, so an old revision keeps loading exactly as it was. */
#pragma once
#include "Dsp.h"
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace xl {

// what a module sees of its controls: normalised values by name (steps come back as an index)
struct Controls
{
    const float* base = nullptr;            // this location's 16 generic parameters
    const xlspec::ModuleDef* def = nullptr;
    int index (const char* n) const { for (int j = 0; j < def->nParams; ++j) if (std::string (def->params[j].name) == n) return j; return -1; }
    float operator() (int j) const
    {
        if (j < 0) return 0;
        const auto& p = def->params[j];
        const float v = base[j];
        return p.kind == 's' ? (float) std::lround (v * (p.steps - 1)) : v;
    }
};

struct Module
{
    virtual ~Module() = default;
    virtual void prepare (double fs) = 0;
    virtual void bind (const Controls& c) = 0;     // look up control indices once
    virtual void update (const Controls& c) = 0;   // once per 128-sample block
    virtual void run (float* b, int N) { (void) b; (void) N; }
    virtual void runStereo (float* l, float* r, int N) { (void) l; (void) r; (void) N; }
    // meters: compressor / gate activity in dB, signal peak (linear), needle (dB GR)
    float grMax = 0, gMax = 0, pk = 0, needle = 0;
    float noise = 0;
    bool hasGr = false, hasPeak = false, stereo = false, pre = false;
};

using Factory = std::function<std::unique_ptr<Module>()>;
const std::map<std::string, Factory>& registry();
std::unique_ptr<Module> makeModule (const std::string& type, double fs);
const xlspec::ModuleDef* moduleDef (const std::string& type);

} // namespace xl
