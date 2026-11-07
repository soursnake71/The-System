// The effects section: a Yamaha REV7 (the Omega REV7 model, plugins/dsp/Rev7.h) on an aux send, the way a CMI was
// usually run in the studio. Each mixer strip sends to it (post fader and pan); its return comes back into the mix
// before the CMI's output stage. The REV7 always runs 100% wet here (its MIXING knob is the send's job); its OUTPUT
// knob is the return level.
#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <sstream>
#include "../../dsp/Rev7Spec.h"

namespace cmi {

class Rev7Fx {
public:
    static const omega::Spec& spec() { static const omega::Spec s = omega::rev7Spec(); return s; }
    // parameters the effects page shows (MIXING, MUTE and BYPASS belong to the send and the ON switch here)
    static bool shown(const omega::ParamDef& p) { return p.id != "omix" && p.id != "mute" && p.id != "bypass"; }
    static double defaultOf(const omega::ParamDef& p) { return p.choices.empty() ? p.def : p.choices[(size_t) p.defChoice].value; }

    Rev7Fx() : eng(spec().make(spec())) {
        const auto& ps = spec().params;
        pending.resize(ps.size()); for (size_t i = 0; i < ps.size(); ++i) pending[i] = defaultOf(ps[i]);
        force(pending); live = pending; dirty = true;
    }
    Rev7Fx(const Rev7Fx&) = delete;

    void prepare(double fs, int maxBlock) {
        std::lock_guard<std::mutex> g(lock);
        live = pending; dirty = false;
        eng->setValues(live); eng->prepare(fs, maxBlock); eng->setValues(live);
    }
    // UI thread
    int count() const { return (int) spec().params.size(); }
    double get(int i) const { std::lock_guard<std::mutex> g(lock); return i >= 0 && i < (int) pending.size() ? pending[(size_t) i] : 0; }
    double get(const std::string& id) const { return get(spec().index(id)); }
    void set(int i, double v) {
        if (i < 0 || i >= count()) return;
        const auto& p = spec().params[(size_t) i];
        v = std::min<double>(p.max, std::max<double>(p.min, v));
        if (p.step > 0) v = p.min + std::round((v - p.min) / p.step) * p.step;
        std::lock_guard<std::mutex> g(lock); pending[(size_t) i] = v; force(pending); dirty = true;
    }
    void set(const std::string& id, double v) { set(spec().index(id), v); }
    // a preset: the defaults, then its values (as the REV7 plugin loads one)
    void preset(int k) {
        const auto& pr = spec().presets; if (k < 0 || k >= (int) pr.size()) return;
        std::lock_guard<std::mutex> g(lock);
        const auto& ps = spec().params;
        for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id != "ret" && ps[i].id != "in") pending[i] = defaultOf(ps[i]);   // keep the levels
        for (const auto& kv : pr[(size_t) k].values) { const int i = spec().index(kv.first); if (i >= 0 && kv.first != "omix") pending[(size_t) i] = kv.second; }
        force(pending); dirty = true; curPreset = k;
    }
    std::atomic<int> curPreset { 0 };

    // the dice (as on the Omega REV7's panel): a random preset from all of them, or a random program with its
    // settings rolled within useful ranges for its type. Returns what it rolled, for the screen.
    std::string rollPreset() {
        const int n = (int) spec().presets.size(); if (n <= 0) return {};
        int k = (int) (rng() % (unsigned) n); if (n > 1) while (k == curPreset.load()) k = (int) (rng() % (unsigned) n);
        preset(k);
        return spec().presets[(size_t) k].name;
    }
    std::string rollSettings() {
        std::uniform_real_distribution<double> u(0, 1);
        auto r = [&](double a, double b) { return a + (b - a) * u(rng); };
        auto rl = [&](double a, double b) { return a * std::pow(b / a, u(rng)); };   // log-spread
        const int prog = 1 + (int) (rng() % 30u);
        preset(prog - 1);   // the factory program first (presets 1-30 are the ROM programs), then roll what matters
        using K = omega::dsp::Rev7::Kind;
        switch (omega::dsp::Rev7::prog(prog).kind) {
            case K::REV: set("rt", rl(.5, 5)); set("hi", r(.2, .8)); set("lo", r(.6, 1.8)); set("diff", std::round(r(3, 10))); set("idly", rl(1, 70)); break;
            case K::ER: set("live", std::round(r(1, 9))); set("mode", 1 + (int) (rng() % 6u)); set("size", rl(.6, 5)); set("diff", std::round(r(2, 9))); set("idly", rl(1, 35)); break;
            case K::DLY: set("dlyL", rl(40, 600)); set("dlyR", rl(40, 600)); set("dlev", r(.4, .9)); break;
            case K::ECHO: { const double d = rl(80, 450); set("fb", r(.15, .65)); set("dlyL", d); set("dlyR", std::min(450.0, d * r(.7, 1.4))); set("hdamp", std::round(r(2, 9))); break; }
            case K::FLANGE: set("fb", r(.1, .8)); set("mfreq", rl(.1, 3)); set("mdep", r(.3, 1)); set("mdly", rl(.5, 4)); break;
            case K::RFLANGE: set("rt", rl(1, 4)); set("mfreq", rl(.1, 2)); set("mdep", r(.3, 1)); set("mdly", rl(.5, 4)); break;
            case K::CHORUS: set("mfreq", rl(.1, 2)); set("mdep", r(.2, .9)); set("amdep", r(0, .5)); break;
            case K::PHASE: set("mfreq", rl(.1, 3)); set("mdep", r(.3, 1)); set("mdly", rl(.5, 6)); break;
            case K::TREM: set("mfreq", r(2, 8)); set("mdep", r(.2, .7)); break;
            case K::SYMPH: set("mfreq", rl(.2, 1.5)); set("mdep", r(.3, .9)); break;
        }
        if (u(rng) < .3) { set("fdly", rl(5, 60)); set("flev", r(.2, .6)); }
        curPreset = -1;
        return std::string(omega::rev7ProgName(prog)) + " *";
    }
    // "id=value;..." for saving
    std::string save() const {
        std::lock_guard<std::mutex> g(lock); std::ostringstream o; o.precision(9);
        for (size_t i = 0; i < pending.size(); ++i) o << spec().params[i].id << "=" << pending[i] << ";";
        o << "preset=" << curPreset.load() << ";";
        return o.str();
    }
    void load(const std::string& s) {
        std::istringstream in(s); std::string tok;
        while (std::getline(in, tok, ';')) {
            const size_t eq = tok.find('='); if (eq == std::string::npos) continue;
            const std::string id = tok.substr(0, eq); const double v = std::atof(tok.substr(eq + 1).c_str());
            if (id == "preset") curPreset = (int) v; else set(id, v);
        }
    }
    float level() const { return eng->level(); }

    // a value as the REV7's LCD would show it
    static std::string format(const omega::ParamDef& p, double v) {
        char b[48];
        using F = omega::Fmt;
        if (!p.choices.empty()) { for (const auto& c : p.choices) if (std::fabs(c.value - v) < 1e-6) return c.label; return std::to_string((int) v); }
        switch (p.fmt) {
            case F::toggle: return v >= .5 ? "ON" : "OFF";
            case F::sec: std::snprintf(b, sizeof b, "%.1fs", v); break;
            case F::ms: std::snprintf(b, sizeof b, v < 10 ? "%.1fms" : "%.0fms", v); break;
            case F::pct: std::snprintf(b, sizeof b, "%.0f%%", v * 100); break;
            case F::hz: std::snprintf(b, sizeof b, v >= 1000 ? "%.1fkHz" : "%.0fHz", v >= 1000 ? v / 1000 : v); break;
            case F::hzv: std::snprintf(b, sizeof b, "%.1fHz", v); break;
            case F::db1: std::snprintf(b, sizeof b, "%+.1fdB", v); break;
            default: std::snprintf(b, sizeof b, "%.2g", v); if (std::fabs(v) >= 1) std::snprintf(b, sizeof b, "%.1f", v); break;
        }
        return b;
    }
    // whether the current program uses a parameter (the REV7 shows a different edit list per program type)
    bool relevant(const std::string& id) const {
        static const char* panel[] = { "in", "mono", "eq", "eqlf", "eqlg", "eqmf", "eqmg", "eqhf", "eqhg", "phase", "circ", "ret" };
        for (const char* q : panel) if (id == q) return true;
        using K = omega::dsp::Rev7::Kind;
        const K k = omega::dsp::Rev7::prog((int) get("prog")).kind;
        auto in = [&](std::initializer_list<const char*> l) { for (const char* q : l) if (id == q) return true; return false; };
        switch (k) {
            case K::REV: return in({ "rt", "hi", "lo", "diff", "idly", "fdly", "flev" });
            case K::ER: return in({ "live", "idly", "fdly", "flev", "mode", "size", "diff" });
            case K::DLY: return in({ "idly", "fdly", "dlyL", "dlyR", "dlev" });
            case K::ECHO: return in({ "fb", "idly", "dlyL", "dlyR", "hdamp" });
            case K::FLANGE: return in({ "fb", "idly", "mfreq", "mdep", "mdly" });
            case K::RFLANGE: return in({ "rt", "idly", "mfreq", "mdep", "mdly" });
            case K::CHORUS: return in({ "idly", "mfreq", "mdep", "amdep" });
            case K::PHASE: return in({ "idly", "mfreq", "mdep", "mdly" });
            default: return in({ "idly", "mfreq", "mdep" });
        }
    }

    // audio thread: the send in, the return out (both stereo, n samples)
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        if (dirty.load() && lock.try_lock()) { live = pending; dirty = false; lock.unlock(); eng->setValues(live); }
        omega::Transport t;
        eng->process(inL, inR, nullptr, nullptr, outL, outR, n, t);
    }

private:
    static void force(std::vector<double>& v) {   // always 100% wet, never muted or bypassed
        const auto& s = spec();
        const int m = s.index("omix"), mu = s.index("mute"), by = s.index("bypass");
        if (m >= 0) v[(size_t) m] = 1;
        if (mu >= 0) v[(size_t) mu] = 0;
        if (by >= 0) v[(size_t) by] = 0;
    }
    std::unique_ptr<omega::Engine> eng;
    std::vector<double> pending, live;
    std::atomic<bool> dirty { false };
    mutable std::mutex lock;
    std::mt19937 rng { std::random_device {}() };
};

}  // namespace cmi
