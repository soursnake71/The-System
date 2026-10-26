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
            for (int d = 0; d < numDrumVoices; ++d)
            {
                const auto row = pv[drumKeys[d]].toString();
                for (int s = 0; s < 16 && s < row.length(); ++s) p.hits[(size_t) d][(size_t) s] = row[s] == 'x' ? 2 : row[s] == 'o' ? 1 : 0;
            }
            patterns.push_back (p);
        }
    if (auto* a = j["presets"].getArray()) presets = *a;
    if (auto* a = j["rhythms"].getArray())
        for (auto& r : *a)
        {
            Rhythm rh; rh.name = r[0].toString();
            const auto m = r[1].toString();
            for (int k = 0; k < 16 && k < m.length(); ++k) rh.on[(size_t) k] = m[k] == 'x';
            rhythms.push_back (rh);
        }
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
    set ("int2", g.p (0.6) ? 0 : g.pick ({ 1, 2, 3, 4 })); set ("poles", g.p (0.75) ? 1 : 0); set ("res", rd (g.u (0.05, 0.65)));
    set ("lfo", rd (g.lg (0.2, 7))); set ("vib", g.p (0.3) ? rd (g.u (2, 12), 1) : 0.0); set ("pwm", (w1 || w2) && g.p (0.5) ? rd (g.u (0.05, 0.35)) : 0.0);
    // only pads hold on; everything else is short and plucky or medium
    if (role == "arp" || role == "hit")
    { set ("cut", rd (g.lg (250, 3000), 0)); set ("env", rd (g.u (2, 5), 2)); set ("fa", 0.002); set ("fd", rd (g.lg (0.05, 0.35))); set ("fs", rd (g.u (0, 0.2))); set ("fr", rd (g.lg (0.05, 0.25)));
      set ("aa", 0.002); set ("ad", rd (g.lg (0.1, 0.5))); set ("as", rd (g.u (0, 0.25))); set ("ar", rd (g.lg (0.05, 0.25))); }
    else if (role == "drone")
    { set ("cut", rd (g.lg (400, 3000), 0)); set ("env", rd (g.u (0.5, 2.5), 2)); set ("fa", rd (g.lg (0.1, 1))); set ("fd", rd (g.lg (0.4, 2))); set ("fs", rd (g.u (0.3, 0.8))); set ("fr", rd (g.lg (0.4, 1.5)));
      set ("aa", rd (g.lg (0.05, 0.8))); set ("ad", rd (g.lg (0.4, 1.5))); set ("as", rd (g.u (0.5, 0.9))); set ("ar", rd (g.lg (0.3, 1.5))); set ("det", rd (g.u (6, 20), 1)); set ("vib", rd (g.u (0, 6), 1)); }
    else if (role == "grid")
    { set ("cut", rd (g.lg (90, 700), 0)); set ("env", rd (g.u (2, 5.5), 2)); set ("res", rd (g.u (0.2, 0.75))); set ("poles", 1); set ("fa", 0.002); set ("fd", rd (g.lg (0.06, 0.3))); set ("fs", rd (g.u (0, 0.3))); set ("fr", 0.06);
      set ("aa", 0.002); set ("ad", rd (g.lg (0.12, 0.45))); set ("as", rd (g.u (0.2, 0.7))); set ("ar", 0.05); set ("int2", g.pick ({ 0, 2, 3 })); set ("vib", 0); }
    else
    { set ("cut", rd (g.lg (600, 5000), 0)); set ("env", rd (g.u (1, 3.5), 2)); set ("fa", rd (g.lg (0.003, 0.06))); set ("fd", rd (g.lg (0.1, 0.6))); set ("fs", rd (g.u (0.2, 0.6))); set ("fr", rd (g.lg (0.05, 0.35)));
      set ("aa", rd (g.lg (0.003, 0.04))); set ("ad", rd (g.lg (0.15, 0.7))); set ("as", rd (g.u (0.3, 0.7))); set ("ar", rd (g.lg (0.06, 0.35))); set ("vib", rd (g.u (3, 15), 1)); }
    return juce::var (o);
}

// nudge a patch part of the way toward a fresh random one (auto-vary)
juce::var mutatePatch (const juce::var& patch, const juce::String& role, float amt, juce::Random& rr)
{
    const auto t = randomPatch (role, rr);
    auto* o = new juce::DynamicObject();
    for (auto& s : patchSpecs())
    {
        const double a = patch.hasProperty (s.id) ? (double) patch[s.id] : s.def, b = t[s.id];
        if (s.integer) o->setProperty (s.id, rr.nextFloat() < amt * 0.5f ? b : a);
        else if (s.log) o->setProperty (s.id, std::exp (std::log (juce::jmax ((double) s.lo, a)) * (1 - amt) + std::log (juce::jmax ((double) s.lo, b)) * amt));
        else o->setProperty (s.id, a * (1 - amt) + b * amt);
    }
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
    static const float echos[6] = { 0.4f, 0.3f, 0.3f, 0.1f, 0.1f, 0.3f }, gates[6] = { 0.6f, 0.45f, 0.5f, 0.5f, 0.6f, 0.5f };
    auto obj = [] { return new juce::DynamicObject(); };
    auto* pre = obj();
    pre->setProperty ("name", name); pre->setProperty ("key", g.i (0, 11));
    pre->setProperty ("scale", juce::String (g.pick ({ "minor", "minor", "dorian", "pentatonic minor", "major", "lydian", "phrygian", "harmonic minor", "pentatonic major" })));
    pre->setProperty ("bpm", g.i (78, 132));
    auto* d = obj();
    d->setProperty ("mode", g.p (0.55) ? 1 : 0); d->setProperty ("mix", rd (g.u (0.3, 0.65))); d->setProperty ("fb", rd (g.u (0.25, 0.55))); d->setProperty ("tone", rd (g.lg (1800, 6000), 0));
    pre->setProperty ("delay", juce::var (d));
    pre->setProperty ("kit", randomKit (rr));
    auto* fx = obj();
    fx->setProperty ("gtime", rd (g.u (0.18, 0.45))); fx->setProperty ("gmix", rd (g.u (0.5, 0.9))); fx->setProperty ("erate", rd (g.u (0.15, 0.5))); fx->setProperty ("emix", rd (g.u (0.4, 0.8)));
    pre->setProperty ("fx", juce::var (fx));
    // the main beat plays all the time (most presets have one)
    juce::StringArray grooves;
    for (auto& p : PresetLibrary::get().patterns) if (p.id != "none" && p.id != "toms" && p.id != "perc" && p.id != "sparse") grooves.add (p.id);
    const auto pickGroove = [&] { return grooves.isEmpty() ? juce::String ("four") : grooves[g.i (0, grooves.size() - 1)]; };
    const juce::String mainBeat = g.p (0.75) ? pickGroove() : juce::String ("none");
    pre->setProperty ("mainBeat", mainBeat);
    pre->setProperty ("autoBars", g.pick ({ 4, 8, 8, 16 }));
    auto* dr = obj();
    dr->setProperty ("vol", rd (g.u (0.75, 1))); dr->setProperty ("pan", 0); dr->setProperty ("rev", rd (g.u (0.05, 0.2))); dr->setProperty ("dly", rd (g.u (0, 0.1)));
    dr->setProperty ("gtd", rd (g.u (0.25, 0.7))); dr->setProperty ("echo", rd (g.u (0, 0.25)));
    pre->setProperty ("drums", juce::var (dr));
    juce::StringArray used;
    static const float pans[5] = { -0.6f, -0.3f, 0, 0.3f, 0.6f };
    auto* ls = obj();
    for (int k = 0; k < 6; ++k)
    {
        const juce::String id (lanes[k]);
        // prefer a sound no other lane has; the OB-Xa can repeat because each lane gets its own patch
        std::vector<juce::String> opts;
        for (auto& e : roleEngines (id)) if (e == "obxa" || ! used.contains (e)) opts.push_back (e);
        if (opts.empty()) opts = roleEngines (id);
        const auto eng = opts[(size_t) g.i (0, (int) opts.size() - 1)];
        used.add (eng);
        auto* l = obj();
        l->setProperty ("on", true); l->setProperty ("engine", eng); l->setProperty ("ch", chans[k]);
        l->setProperty ("vol", rd (vols[k] * g.u (0.8, 1.15))); l->setProperty ("rev", rd (juce::jmin (1.0, revs[k] * g.u (0.6, 1.6)))); l->setProperty ("dly", rd (juce::jmin (1.0, dlys[k] * g.u (0.5, 1.8))));
        l->setProperty ("pan", id == "grid" ? 0.0 : rd (pans[g.i (0, 4)] + g.u (-0.1, 0.1), 2));
        l->setProperty ("mute", false); l->setProperty ("solo", false);
        l->setProperty ("gtd", id == "hit" ? rd (g.u (0.2, 0.6)) : g.p (0.2) ? rd (g.u (0.05, 0.3)) : 0.0);
        l->setProperty ("echo", rd (juce::jmin (1.0, echos[k] * g.u (0.5, 2)))); l->setProperty ("gate", rd (juce::jmin (1.0, gates[k] * g.u (0.6, 1.4))));
        const auto rh = roleRhythms (id);
        l->setProperty ("rhythm", rh.empty() ? 0 : rh[(size_t) g.i (0, (int) rh.size() - 1)]);
        l->setProperty ("auto", true); l->setProperty ("drum", "none"); l->setProperty ("dv", rd (g.u (0.6, 0.95)));
        l->setProperty ("patch", randomPatch (id, rr));
        ls->setProperty (id, juce::var (l));
    }
    // one lane may add a second groove or fills on top of the main beat
    if (g.p (0.6)) if (auto* l = ls->getProperty (lanes[g.pick ({ 4, 3, 0, 5, 1 })]).getDynamicObject()) l->setProperty ("drum", juce::String (g.pick ({ "perc", "toms", "sparse", "tribal", "latin", "afrobeat", "dub" })));
    if (mainBeat == "none") if (auto* l = ls->getProperty (lanes[g.pick ({ 4, 3 })]).getDynamicObject()) l->setProperty ("drum", pickGroove());
    pre->setProperty ("lanes", juce::var (ls));
    return juce::var (pre);
}
} // namespace sky
