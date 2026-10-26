#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

/* Preset data shared with the web app: the OB-Xa patch and drum-kit parameter tables (same as sky/presets.js),
   the drum patterns and factory presets from sky/presets.json (bundled as binary data), and the generators. */
namespace sky
{
struct ParamSpec { const char* id; const char* label; float lo, hi, def; bool log, integer; };

inline const std::vector<ParamSpec>& patchSpecs()
{
    static const std::vector<ParamSpec> s = {
        { "w1", "Osc 1 wave", 0, 1, 0, false, true }, { "w2", "Osc 2 wave", 0, 1, 1, false, true }, { "pw", "Pulse width", 0.05f, 0.5f, 0.3f, false, false },
        { "mix2", "Osc 2 level", 0, 1, 0.7f, false, false }, { "det", "Detune", 0, 30, 7, false, false }, { "int2", "Osc 2 interval", 0, 4, 0, false, true },
        { "cut", "Cutoff", 60, 12000, 1800, true, false }, { "res", "Resonance", 0, 1, 0.3f, false, false }, { "poles", "Filter slope", 0, 1, 1, false, true },
        { "env", "Filter env", 0, 6, 2.5f, false, false }, { "fa", "Filter attack", 0.001f, 2, 0.005f, true, false }, { "fd", "Filter decay", 0.01f, 3, 0.4f, true, false },
        { "fs", "Filter sustain", 0, 1, 0.3f, false, false }, { "fr", "Filter release", 0.01f, 4, 0.3f, true, false }, { "aa", "Amp attack", 0.001f, 3, 0.005f, true, false },
        { "ad", "Amp decay", 0.01f, 3, 0.5f, true, false }, { "as", "Amp sustain", 0, 1, 0.7f, false, false }, { "ar", "Amp release", 0.01f, 5, 0.3f, true, false },
        { "lfo", "LFO rate", 0.05f, 12, 5, true, false }, { "vib", "Vibrato", 0, 50, 0, false, false }, { "pwm", "LFO to PW", 0, 0.45f, 0, false, false } };
    return s;
}
inline juce::StringArray choiceLabels (const juce::String& id)
{
    if (id == "w1" || id == "w2") return { "Saw", "Pulse" };
    if (id == "poles") return { "12 dB", "24 dB" };
    if (id == "int2") return { "Unison", "+5th", "+Octave", "-Octave", "+4th" };
    return {};
}
inline const std::vector<ParamSpec>& kitSpecs()
{
    static const std::vector<ParamSpec> s = {
        { "kp", "Kick start pitch", 80, 300, 180, true, false }, { "kf", "Kick pitch", 35, 75, 48, false, false }, { "kd", "Kick decay", 0.15f, 1.2f, 0.45f, true, false },
        { "kc", "Kick click", 0, 1, 0.4f, false, false }, { "sf", "Snare tone", 140, 300, 190, false, false }, { "sd", "Snare decay", 0.06f, 0.4f, 0.18f, true, false },
        { "ss", "Snare snap", 0.3f, 1, 0.7f, false, false }, { "hd", "Closed hat decay", 0.02f, 0.15f, 0.05f, true, false }, { "ho", "Open hat decay", 0.15f, 0.9f, 0.4f, true, false },
        { "ht", "Hat tone", 4000, 11000, 7000, true, false }, { "cd", "Clap decay", 0.08f, 0.45f, 0.2f, true, false }, { "tf", "Tom pitch", 70, 240, 120, true, false },
        { "td", "Tom decay", 0.12f, 0.6f, 0.3f, true, false }, { "rf", "Rim pitch", 350, 1900, 900, true, false }, { "pf", "Perc pitch", 300, 1300, 560, true, false },
        { "pr", "Perc ratio", 1.2f, 2.6f, 1.48f, false, false }, { "pd", "Perc decay", 0.04f, 0.45f, 0.15f, true, false }, { "lvl", "Kit level", 0, 1.5f, 0.9f, false, false } };
    return s;
}
inline const char* drumKeys[8] = { "k", "s", "ch", "oh", "cl", "t", "r", "p" };
inline const int drumNotes[8] = { 36, 38, 42, 46, 39, 45, 37, 56 };   // General MIDI, channel 10

struct Pattern { juce::String id, name; std::array<std::array<juce::uint8, 16>, 8> hits {}; };   // 0 rest, 1 soft, 2 accent

class PresetLibrary
{
public:
    static PresetLibrary& get() { static PresetLibrary lib; return lib; }
    std::vector<Pattern> patterns;      // index 0 is "No drum loop"
    juce::Array<juce::var> presets;
    juce::StringArray patternNames() const { juce::StringArray s; for (auto& p : patterns) s.add (p.name); return s; }
    int patternIndex (const juce::String& id) const { for (int i = 0; i < (int) patterns.size(); ++i) if (patterns[(size_t) i].id == id) return i; return 0; }
private:
    PresetLibrary();
};

// generators: same rules as sky/presets.js (randomPatch, randomKit, generatePreset)
juce::var randomPatch (const juce::String& role, juce::Random&);
juce::var randomKit (juce::Random&);
juce::var generatePreset (juce::Random&, const juce::String& name);
} // namespace sky
