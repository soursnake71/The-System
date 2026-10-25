// What a plugin is: its name, parameters, presets and the engine that makes the sound.
// The DSP engines are plain C++ (no JUCE) so they can be tested on their own.
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace omega {

// how a parameter's value is shown (mirrors the formatters in the web app)
enum class Fmt { plain, pct, spct, ms, msv, hz, hzv, sec, db, negdb, n1, k10, time, st, ct, cents, cutoff, res10, pw, p5lfo, wmmix,
                 u7in, u7out, adaman, adarange, adaspeed, semis, db1, toggle, choice };

struct Choice { std::string label; double value; };

struct ParamDef {
    std::string id, name;
    float min = 0, max = 1, step = 0, def = 0;
    Fmt fmt = Fmt::plain;
    std::vector<Choice> choices;   // non-empty: a menu
    bool toggle = false;
    int defChoice = 0;
};

struct Preset { std::string name; std::vector<std::pair<std::string, double>> values; std::string text; };

struct Transport { double bpm = 120, ppq = 0, timeSec = 0; bool playing = false; };

class Engine;

struct Spec {
    std::string name, sub, maker = "Omega", code;   // code: 4 characters, unique per plugin
    bool synth = false, midiIn = false, sidechain = false, hasText = false;
    uint32_t accent = 0xffff9a3c, panel = 0xff1d2027;
    std::string defaultText, textHint = "Type the words.";
    std::vector<ParamDef> params;
    std::vector<Preset> presets;
    std::function<std::unique_ptr<Engine>(const Spec&)> make;

    void knob(const std::string& id, const std::string& name, double mn, double mx, double step, double def, Fmt f) {
        ParamDef p; p.id = id; p.name = name; p.min = (float) mn; p.max = (float) mx; p.step = (float) step; p.def = (float) def; p.fmt = f; params.push_back(p);
    }
    void toggle(const std::string& id, const std::string& name, bool def) {
        ParamDef p; p.id = id; p.name = name; p.min = 0; p.max = 1; p.step = 1; p.def = def ? 1.f : 0.f; p.fmt = Fmt::toggle; p.toggle = true; params.push_back(p);
    }
    void choice(const std::string& id, const std::string& name, std::vector<Choice> opts, int def) {
        ParamDef p; p.id = id; p.name = name; p.choices = std::move(opts); p.defChoice = def; p.fmt = Fmt::choice;
        p.min = 0; p.max = (float) (p.choices.size() - 1); p.step = 1; p.def = (float) def; params.push_back(p);
    }
    void preset(const std::string& name, std::vector<std::pair<std::string, double>> v, const std::string& text = {}) { presets.push_back({name, std::move(v), text}); }
    int index(const std::string& id) const { for (size_t i = 0; i < params.size(); ++i) if (params[i].id == id) return (int) i; return -1; }
    void remove(const std::string& id) { int i = index(id); if (i >= 0) params.erase(params.begin() + i); }
    ParamDef* find(const std::string& id) { int i = index(id); return i >= 0 ? &params[(size_t) i] : nullptr; }
};

// The sound engine. Parameter values arrive in their real units (a menu gives its option's value).
class Engine {
public:
    explicit Engine(const Spec& s) : spec(s), vals(s.params.size(), 0.0) {
        for (size_t i = 0; i < s.params.size(); ++i) {
            idx[s.params[i].id] = (int) i;
            const auto& p = s.params[i];
            vals[i] = p.choices.empty() ? p.def : p.choices[(size_t) p.defChoice].value;
        }
    }
    virtual ~Engine() = default;
    virtual void prepare(double sampleRate, int maxBlock) = 0;
    virtual void reset() {}
    // called at the start of every block (and before prepare) with the current values
    void setValues(const std::vector<double>& v) { vals = v; changed(); }
    virtual void changed() {}
    virtual void noteOn(int /*note*/, float /*velocity*/) {}
    virtual void noteOff(int /*note*/) {}
    virtual void allNotesOff() {}
    virtual void pitchBend(double /*semitones*/) {}
    virtual void modWheel(double /*v*/) {}
    virtual void setText(const std::string& /*t*/) {}
    // in*: main input (may be null for a synth), sc*: sidechain (may be null); out must be filled
    virtual void process(const float* inL, const float* inR, const float* scL, const float* scR, float* outL, float* outR, int n, const Transport& t) = 0;

protected:
    double v(const char* id) const { auto it = idx.find(id); return it == idx.end() ? 0.0 : vals[(size_t) it->second]; }
    bool has(const char* id) const { return idx.count(id) != 0; }
    const Spec& spec;
    std::vector<double> vals;
    std::unordered_map<std::string, int> idx;
};

}  // namespace omega
