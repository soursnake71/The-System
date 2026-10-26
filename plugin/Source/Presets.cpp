#include "Presets.h"
#include "BinaryData.h"

namespace sky
{
PresetLibrary::PresetLibrary()
{
    patterns.push_back ({ "none", "No drum loop", {} });
    const auto j = juce::JSON::parse (juce::String::fromUTF8 (BinaryData::presets_json, BinaryData::presets_jsonSize));
    if (auto* obj = j["patterns"].getDynamicObject())
        for (auto& [id, pv] : obj->getProperties())
        {
            Pattern p; p.id = id.toString(); p.name = pv["name"].toString();
            for (int d = 0; d < 8; ++d)
            {
                const auto row = pv[drumKeys[d]].toString();
                for (int s = 0; s < 16 && s < row.length(); ++s) p.hits[(size_t) d][(size_t) s] = row[s] == 'x' ? 2 : row[s] == 'o' ? 1 : 0;
            }
            patterns.push_back (p);
        }
    if (auto* a = j["presets"].getArray()) presets = *a;
}

namespace
{
struct G
{
    juce::Random& r;
    double u (double a, double b) { return a + r.nextDouble() * (b - a); }
    double lg (double a, double b) { return a * std::pow (b / a, r.nextDouble()); }
    int i (int a, int b) { return a + r.nextInt (b - a + 1); }
    bool p (double x) { return r.nextDouble() < x; }
    template <typename T> T pick (std::initializer_list<T> l) { return *(l.begin() + r.nextInt ((int) l.size())); }
};
double rd (double x, int d = 3) { const double m = std::pow (10.0, d); return std::round (x * m) / m; }
}

juce::var randomPatch (const juce::String& role, juce::Random& rr)
{
    G g { rr };
    auto* o = new juce::DynamicObject();
    for (auto& s : patchSpecs()) o->setProperty (s.id, s.def);
    auto set = [o] (const char* k, double v) { o->setProperty (k, v); };
    const int w1 = g.p (0.7) ? 0 : 1, w2 = g.p (0.5) ? 0 : 1;
    set ("w1", w1); set ("w2", w2); set ("pw", rd (g.u (0.12, 0.5))); set ("mix2", rd (g.u (0.4, 1))); set ("det", rd (g.u (2, 16), 1));
    set ("int2", g.p (0.65) ? 0 : g.pick ({ 1, 2, 3, 4 })); set ("poles", g.p (0.75) ? 1 : 0); set ("res", rd (g.u (0.05, 0.6)));
    set ("lfo", rd (g.lg (0.2, 7))); set ("vib", g.p (0.3) ? rd (g.u (2, 12), 1) : 0.0); set ("pwm", (w1 || w2) && g.p (0.5) ? rd (g.u (0.05, 0.35)) : 0.0);
    if (role == "arp" || role == "hit")
    { set ("cut", rd (g.lg (250, 2500), 0)); set ("env", rd (g.u (2, 5), 2)); set ("fa", 0.002); set ("fd", rd (g.lg (0.08, 0.5))); set ("fs", rd (g.u (0, 0.25))); set ("fr", rd (g.lg (0.08, 0.4)));
      set ("aa", 0.002); set ("ad", rd (g.lg (0.15, 0.8))); set ("as", rd (g.u (0, 0.4))); set ("ar", rd (g.lg (0.08, 0.5))); }
    else if (role == "drone")
    { set ("cut", rd (g.lg (400, 3000), 0)); set ("env", rd (g.u (0.5, 2.5), 2)); set ("fa", rd (g.lg (0.2, 1.5))); set ("fd", rd (g.lg (0.5, 2.5))); set ("fs", rd (g.u (0.3, 0.8))); set ("fr", rd (g.lg (0.8, 3)));
      set ("aa", rd (g.lg (0.2, 1.6))); set ("ad", rd (g.lg (0.5, 2))); set ("as", rd (g.u (0.6, 1))); set ("ar", rd (g.lg (0.8, 3.5))); set ("det", rd (g.u (6, 20), 1)); set ("vib", rd (g.u (0, 6), 1)); }
    else if (role == "grid")
    { set ("cut", rd (g.lg (90, 700), 0)); set ("env", rd (g.u (2, 5.5), 2)); set ("res", rd (g.u (0.2, 0.75))); set ("poles", 1); set ("fa", 0.002); set ("fd", rd (g.lg (0.08, 0.35))); set ("fs", rd (g.u (0, 0.3))); set ("fr", 0.08);
      set ("aa", 0.002); set ("ad", rd (g.lg (0.2, 0.6))); set ("as", rd (g.u (0.4, 0.9))); set ("ar", 0.06); set ("int2", g.pick ({ 0, 2, 3 })); set ("vib", 0); }
    else
    { set ("cut", rd (g.lg (600, 5000), 0)); set ("env", rd (g.u (1, 3.5), 2)); set ("fa", rd (g.lg (0.005, 0.1))); set ("fd", rd (g.lg (0.2, 1))); set ("fs", rd (g.u (0.3, 0.7))); set ("fr", rd (g.lg (0.1, 0.8)));
      set ("aa", rd (g.lg (0.005, 0.08))); set ("ad", rd (g.lg (0.2, 1))); set ("as", rd (g.u (0.5, 0.9))); set ("ar", rd (g.lg (0.1, 0.8))); set ("vib", rd (g.u (3, 15), 1)); }
    return juce::var (o);
}

juce::var randomKit (juce::Random& rr)
{
    G g { rr };
    auto* o = new juce::DynamicObject();
    for (auto& s : kitSpecs()) o->setProperty (s.id, rd (s.log ? g.lg (s.lo, s.hi) : g.u (s.lo, s.hi)));
    o->setProperty ("kp", juce::jmax ((double) o->getProperty ("kp"), (double) o->getProperty ("kf") * 2));
    o->setProperty ("lvl", rd (g.u (0.75, 1.05)));
    return juce::var (o);
}

juce::var generatePreset (juce::Random& rr, const juce::String& name)
{
    G g { rr };
    static const char* lanes[6] = { "arp", "rise", "fall", "drone", "grid", "hit" };
    static const int chans[6] = { 1, 2, 3, 4, 5, 10 };
    static const float vols[6] = { 0.8f, 0.7f, 0.65f, 0.75f, 0.75f, 0.7f }, revs[6] = { 0.25f, 0.35f, 0.45f, 0.6f, 0.1f, 0.15f }, dlys[6] = { 0.35f, 0.25f, 0.3f, 0.1f, 0.15f, 0.1f };
    auto* pre = new juce::DynamicObject();
    pre->setProperty ("name", name); pre->setProperty ("key", g.i (0, 11));
    pre->setProperty ("scale", juce::String (g.pick ({ "minor", "minor", "dorian", "pentatonic minor", "major", "lydian", "phrygian", "harmonic minor", "pentatonic major" })));
    pre->setProperty ("bpm", g.i (78, 132));
    auto* d = new juce::DynamicObject();
    d->setProperty ("mode", g.p (0.55) ? 1 : 0); d->setProperty ("mix", rd (g.u (0.3, 0.7))); d->setProperty ("fb", rd (g.u (0.25, 0.6))); d->setProperty ("tone", rd (g.lg (1800, 6000), 0));
    pre->setProperty ("delay", juce::var (d));
    pre->setProperty ("kit", randomKit (rr));
    const juce::String groove = g.pick ({ "grid", "grid", "drone", "arp" });
    const juce::String grooveId = g.pick ({ "four", "house", "breakbeat", "electro", "halftime", "dnb", "boombap", "trap", "shuffle", "motorik", "tribal" });
    const juce::String extra = g.p (0.5) ? juce::String (g.pick ({ "hit", "rise", "fall" })) : juce::String();
    auto* ls = new juce::DynamicObject();
    for (int k = 0; k < 6; ++k)
    {
        const juce::String id (lanes[k]);
        juce::String eng;
        if (id == "arp") eng = g.pick ({ "obxa", "obxa", "pluck", "glass", "bell" });
        else if (id == "rise") eng = g.pick ({ "obxa", "glass", "lead", "bell" });
        else if (id == "fall") eng = g.pick ({ "obxa", "bell", "glass", "lead" });
        else if (id == "drone") eng = g.pick ({ "obxa", "obxa", "pad" });
        else if (id == "grid") eng = g.pick ({ "obxa", "obxa", "bass", "pluck" });
        else eng = g.pick ({ "perc", "perc", "obxa", "bell" });
        auto* l = new juce::DynamicObject();
        l->setProperty ("on", true); l->setProperty ("engine", eng); l->setProperty ("ch", chans[k]);
        l->setProperty ("vol", rd (vols[k] * g.u (0.8, 1.15))); l->setProperty ("rev", rd (juce::jmin (1.0, revs[k] * g.u (0.6, 1.6)))); l->setProperty ("dly", rd (juce::jmin (1.0, dlys[k] * g.u (0.5, 1.8))));
        l->setProperty ("drum", id == groove ? grooveId : id == extra ? juce::String (g.pick ({ "perc", "toms", "sparse", "tribal" })) : juce::String ("none"));
        l->setProperty ("dv", rd (g.u (0.6, 0.95)));
        l->setProperty ("patch", randomPatch (id, rr));
        ls->setProperty (id, juce::var (l));
    }
    pre->setProperty ("lanes", juce::var (ls));
    return juce::var (pre);
}
} // namespace sky
