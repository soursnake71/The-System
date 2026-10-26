#include "PluginProcessor.h"
#include "PluginEditor.h"

const LaneInfo laneInfo[6] = {
    { "arp",   "Circling",    "Arpeggio",     0xffff5ec8, sky::eObxa,  1,  0.8f,  0.25f, 0.35f },
    { "rise",  "Climbing",    "Rising line",  0xffffd23f, sky::eObxa,  2,  0.7f,  0.35f, 0.25f },
    { "fall",  "Descending",  "Falling line", 0xff4fc3ff, sky::eObxa,   3,  0.65f, 0.45f, 0.3f },
    { "drone", "Cruising",    "Pad",          0xff9c8cff, sky::eObxa,    4,  0.75f, 0.6f,  0.1f },
    { "grid",  "Sky grid",    "Sequence",     0xff4dffa6, sky::eObxa,   5,  0.75f, 0.1f,  0.15f },
    { "hit",   "Low & close", "Percussion",   0xffff8a3d, sky::eObxa,   10, 0.7f,  0.15f, 0.1f },
};
const int windowMinutes[7] = { 1, 2, 5, 10, 15, 30, 60 };

juce::StringArray scaleNames() { return { "minor", "major", "dorian", "lydian", "phrygian", "pentatonic minor", "pentatonic major", "harmonic minor", "whole tone" }; }
const std::vector<int>& scaleSteps (int i)
{
    static const std::vector<std::vector<int>> s = { { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 6, 7, 9, 11 },
                                                     { 0, 1, 3, 5, 7, 8, 10 }, { 0, 3, 5, 7, 10 }, { 0, 2, 4, 7, 9 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 4, 6, 8, 10 } };
    return s[(size_t) juce::jlimit (0, (int) s.size() - 1, i)];
}

// mixer and rhythm defaults per lane (same as DEF_LANES in sky/presets.js)
static const float lanePanDef[6] = { 0.3f, -0.35f, 0.45f, -0.2f, 0, 0.1f }, laneGtdDef[6] = { 0, 0, 0, 0, 0, 0.4f };
static const float laneEchoDef[6] = { 0.4f, 0.3f, 0.3f, 0.1f, 0.1f, 0.3f }, laneGateDef[6] = { 0.6f, 0.45f, 0.5f, 0.5f, 0.6f, 0.5f };
static const int laneRhythmDef[6] = { 0, 0, 1, 6, 0, 0 };

juce::AudioProcessorValueTreeState::ParameterLayout SkyProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 9));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Play", StringArray { "With the DAW transport", "Always (own tempo when the DAW is stopped)" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Own tempo", NormalisableRange<float> (40, 220, 1), 96));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "thresh", 1 }, "Grid threshold", NormalisableRange<float> (0.05f, 0.9f, 0.01f), 0.3f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "window", 1 }, "Grid window", StringArray { "1 min", "2 min", "5 min", "10 min", "15 min", "30 min", "60 min" }, 2));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", NormalisableRange<float> (0, 1.5f, 0.01f), 0.8f));
    for (auto& ln : laneInfo)
    {
        const String id (ln.id), nm (ln.name);
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id + "_on", 1 }, nm + " on", true));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "_sound", 1 }, nm + " sound", sky::soundNames(), 0));
        l.add (std::make_unique<AudioParameterInt> (ParameterID { id + "_ch", 1 }, nm + " MIDI channel", 1, 16, ln.channel));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_vol", 1 }, nm + " volume", NormalisableRange<float> (0, 1.5f, 0.01f), ln.vol));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_rev", 1 }, nm + " reverb", NormalisableRange<float> (0, 1, 0.01f), ln.rev));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_dly", 1 }, nm + " delay", NormalisableRange<float> (0, 1, 0.01f), ln.dly));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "_drum", 1 }, nm + " drum loop", sky::PresetLibrary::get().patternNames(), 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_dv", 1 }, nm + " drum level", NormalisableRange<float> (0, 1.2f, 0.01f), 0.8f));
        const int k = (int) (&ln - laneInfo);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_pan", 1 }, nm + " pan", NormalisableRange<float> (-1, 1, 0.01f), lanePanDef[k]));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id + "_mute", 1 }, nm + " mute", false));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id + "_solo", 1 }, nm + " solo", false));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_gtd", 1 }, nm + " gated reverb", NormalisableRange<float> (0, 1, 0.01f), laneGtdDef[k]));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_echo", 1 }, nm + " echoes", NormalisableRange<float> (0, 1, 0.01f), laneEchoDef[k]));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_gate", 1 }, nm + " note length", NormalisableRange<float> (0.1f, 1, 0.01f), laneGateDef[k]));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "_rhythm", 1 }, nm + " rhythm", sky::PresetLibrary::get().rhythmNames(), laneRhythmDef[k]));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id + "_auto", 1 }, nm + " auto-vary", true));
    }
    // drum channel on the mixer
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drm_vol", 1 }, "Drums volume", NormalisableRange<float> (0, 1.5f, 0.01f), 0.9f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drm_pan", 1 }, "Drums pan", NormalisableRange<float> (-1, 1, 0.01f), 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "drm_mute", 1 }, "Drums mute", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "drm_solo", 1 }, "Drums solo", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drm_rev", 1 }, "Drums reverb", NormalisableRange<float> (0, 1, 0.01f), 0.12f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drm_dly", 1 }, "Drums delay", NormalisableRange<float> (0, 1, 0.01f), 0.05f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drm_gtd", 1 }, "Drums gated reverb", NormalisableRange<float> (0, 1, 0.01f), 0.4f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drm_echo", 1 }, "Drums echoes", NormalisableRange<float> (0, 1, 0.01f), 0));
    // gated reverb, random echoes, auto-vary, main beat
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gtime", 1 }, "Gate time", NormalisableRange<float> (0.08f, 0.7f, 0.01f), 0.3f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gmix", 1 }, "Gated reverb level", NormalisableRange<float> (0, 1, 0.01f), 0.7f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "erate", 1 }, "Echo rate", NormalisableRange<float> (0, 1, 0.01f), 0.25f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "emix", 1 }, "Echo level", NormalisableRange<float> (0, 1, 0.01f), 0.6f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "autobars", 1 }, "Auto-vary", StringArray { "Off", "Every 4 bars", "Every 8 bars", "Every 16 bars" }, 2));
    { auto names = sky::PresetLibrary::get().patternNames(); names.set (0, "None");
      l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mainbeat", 1 }, "Main beat", names, 0)); }
    // ranges for patch and kit numbers; log ones are skewed so the middle of the knob is the geometric middle
    auto range = [] (const sky::ParamSpec& s) {
        NormalisableRange<float> r (s.lo, s.hi, s.integer ? 1.0f : 0.0f);
        if (s.log) r.setSkewForCentre (std::sqrt (s.lo * s.hi));
        return r;
    };
    for (auto& ln : laneInfo)
        for (auto& s : sky::patchSpecs())
        {
            const auto id = String (ln.id) + "_" + s.id, nm = String (ln.name) + " OB-Xa " + s.label;
            if (s.integer) l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, nm, sky::choiceLabels (s.id), (int) s.def));
            else l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, range (s), s.def));
        }
    for (auto& s : sky::kitSpecs()) l.add (std::make_unique<AudioParameterFloat> (ParameterID { String ("kit_") + s.id, 1 }, String ("Kit ") + s.label, range (s), s.def));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "dmode", 1 }, "Delay mode", StringArray { "Simple (dotted eighth)", "Edge (U2 style)" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dmix", 1 }, "Delay level", NormalisableRange<float> (0, 1, 0.01f), 0.5f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dfb", 1 }, "Delay feedback", NormalisableRange<float> (0, 0.9f, 0.01f), 0.38f));
    { NormalisableRange<float> r (800, 9000); r.setSkewForCentre (2700); l.add (std::make_unique<AudioParameterFloat> (ParameterID { "dtone", 1 }, "Delay tone", r, 2800)); }
    return l;
}

SkyProcessor::SkyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "SkyParams", layout())
{
    for (int i = 0; i < numLanes; ++i)
    {
        const juce::String id (laneInfo[i].id);
        pOn[(size_t) i] = params.getRawParameterValue (id + "_on"); pEngine[(size_t) i] = params.getRawParameterValue (id + "_sound");
        pCh[(size_t) i] = params.getRawParameterValue (id + "_ch"); pVol[(size_t) i] = params.getRawParameterValue (id + "_vol");
        pRev[(size_t) i] = params.getRawParameterValue (id + "_rev"); pDly[(size_t) i] = params.getRawParameterValue (id + "_dly");
        pDrum[(size_t) i] = params.getRawParameterValue (id + "_drum"); pDv[(size_t) i] = params.getRawParameterValue (id + "_dv");
        for (int k = 0; k < 21; ++k) pPatch[(size_t) i][(size_t) k] = params.getRawParameterValue (patchParamId (i, k));
        pPan[(size_t) i] = params.getRawParameterValue (id + "_pan"); pMute[(size_t) i] = params.getRawParameterValue (id + "_mute");
        pSolo[(size_t) i] = params.getRawParameterValue (id + "_solo"); pGtd[(size_t) i] = params.getRawParameterValue (id + "_gtd");
        pEcho[(size_t) i] = params.getRawParameterValue (id + "_echo"); pGate[(size_t) i] = params.getRawParameterValue (id + "_gate");
        pRhythm[(size_t) i] = params.getRawParameterValue (id + "_rhythm"); pAuto[(size_t) i] = params.getRawParameterValue (id + "_auto");
        lastFire[(size_t) i] = -1000;
    }
    for (int k = 0; k < 24; ++k) pKit[(size_t) k] = params.getRawParameterValue (juce::String ("kit_") + sky::kitSpecs()[(size_t) k].id);
    dVol = params.getRawParameterValue ("drm_vol"); dPan = params.getRawParameterValue ("drm_pan"); dMute = params.getRawParameterValue ("drm_mute"); dSolo = params.getRawParameterValue ("drm_solo");
    dRev = params.getRawParameterValue ("drm_rev"); dDly = params.getRawParameterValue ("drm_dly"); dGtd = params.getRawParameterValue ("drm_gtd"); dEcho = params.getRawParameterValue ("drm_echo");
    pGTime = params.getRawParameterValue ("gtime"); pGMix = params.getRawParameterValue ("gmix"); pERate = params.getRawParameterValue ("erate"); pEMix = params.getRawParameterValue ("emix");
    pAutoBars = params.getRawParameterValue ("autobars"); pMainBeat = params.getRawParameterValue ("mainbeat");
    pDMode = params.getRawParameterValue ("dmode"); pDMix = params.getRawParameterValue ("dmix"); pDFb = params.getRawParameterValue ("dfb"); pDTone = params.getRawParameterValue ("dtone");
    state.setProperty ("preset", "Init", nullptr);
    pKey = params.getRawParameterValue ("key"); pScale = params.getRawParameterValue ("scale"); pBpm = params.getRawParameterValue ("bpm");
    pMode = params.getRawParameterValue ("mode"); pThresh = params.getRawParameterValue ("thresh"); pWindow = params.getRawParameterValue ("window");
    pMaster = params.getRawParameterValue ("master");

    state.setProperty ("lat", 51.47, nullptr); state.setProperty ("lon", -0.4543, nullptr); state.setProperty ("place", "", nullptr);
    state.setProperty ("source", (int) sky::srcAdsbLol, nullptr); state.setProperty ("radius", 45.0, nullptr);
    state.setProperty ("localUrl", "http://192.168.1.50:8080/data/aircraft.json", nullptr);

    feed.onSnapshot = [this] (const sky::Snapshot& s) {
        const juce::SpinLock::ScopedLockType sl (snapLock);
        pending = s; pendingFresh = true;
    };
    // a new instance starts with a random OB-Xa patch on every lane, a random kit and a main beat (a saved project replaces this)
    applyPreset (sky::generatePreset (rng, "Random start"));
    feed.requests = &requests;
    pushSettings();
    feed.startThread();

    // the grid window can be automated, so it's picked up here on the message thread rather than in processBlock
    poller.fn = [this] { pushSettings(); if (autoPending.exchange (false)) autoVary(); };
    poller.startTimer (100);

    // first run: find the listener from their internet connection (they can search or type a place any time)
    lookups.addJob ([this] {
        sky::Place pl; juce::String err;
        if (! sky::locateByIp (pl, err, &requests)) return;
        juce::MessageManager::callAsync ([this, pl, alive = std::weak_ptr<bool> (aliveFlag)] {
            if (alive.expired() || placeName().isNotEmpty()) return;   // a saved or chosen place wins
            setLocation (pl.lat, pl.lon, pl.name + " (from your connection)");
        });
    });
}

SkyProcessor::~SkyProcessor()
{
    aliveFlag.reset();
    requests.cancelAll();   // so the threads below stop at once instead of waiting for a slow server
    poller.stopTimer();
    lookups.removeAllJobs (true, 12000);
    feed.stopThread (8000);
}

sky::FeedSettings SkyProcessor::feedSettings() const
{
    sky::FeedSettings f;
    f.source = juce::jlimit (0, sky::numSources - 1, (int) state.getProperty ("source"));
    f.lat = state.getProperty ("lat"); f.lon = state.getProperty ("lon"); f.radius = state.getProperty ("radius");
    f.localUrl = state.getProperty ("localUrl").toString();
    f.windowMin = windowMinutes[juce::jlimit (0, 6, (int) pWindow->load())];
    return f;
}
void SkyProcessor::pushSettings() { feed.setSettings (feedSettings()); }
void SkyProcessor::setLocation (double lat, double lon, const juce::String& name)
{
    state.setProperty ("lat", juce::jlimit (-90.0, 90.0, lat), nullptr); state.setProperty ("lon", juce::jlimit (-180.0, 180.0, lon), nullptr);
    state.setProperty ("place", name.isEmpty() ? juce::String (lat, 4) + ", " + juce::String (lon, 4) : name, nullptr);
    pushSettings();
}
void SkyProcessor::setFeed (int source, double radius, const juce::String& url)
{
    state.setProperty ("source", source, nullptr); state.setProperty ("radius", juce::jlimit (5.0, 150.0, radius), nullptr); state.setProperty ("localUrl", url, nullptr);
    pushSettings();
}

void SkyProcessor::prepareToPlay (double sampleRate, int block)
{
    sr = sampleRate;
    synth.prepare (sr, juce::jmax (block, 4096));
    numOffs = 0; lastStep = -1; wasPlaying = false; freePos = 0;
}

bool SkyProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

int SkyProcessor::degToMidi (int deg, int base) const
{
    const auto& sc = scaleSteps ((int) pScale->load());
    const int L = (int) sc.size(), o = (int) std::floor ((double) deg / L), i = ((deg % L) + L) % L;
    return base + (int) pKey->load() + (snap.emergency ? 5 : 0) + sc[(size_t) i] + 12 * o;
}

sky::Patch SkyProcessor::patchFor (int lane) const
{
    static float sky::Patch::* const fields[21] = { &sky::Patch::w1, &sky::Patch::w2, &sky::Patch::pw, &sky::Patch::mix2, &sky::Patch::det, &sky::Patch::int2, &sky::Patch::cut,
        &sky::Patch::res, &sky::Patch::poles, &sky::Patch::env, &sky::Patch::fa, &sky::Patch::fd, &sky::Patch::fs, &sky::Patch::fr, &sky::Patch::aa, &sky::Patch::ad,
        &sky::Patch::as, &sky::Patch::ar, &sky::Patch::lfo, &sky::Patch::vib, &sky::Patch::pwm };
    sky::Patch p;
    for (int k = 0; k < 21; ++k) p.*fields[k] = pPatch[(size_t) lane][(size_t) k]->load();
    return p;
}

bool SkyProcessor::anySolo() const
{
    for (int i = 0; i < numLanes; ++i) if (pSolo[(size_t) i]->load() > 0.5f) return true;
    return dSolo->load() > 0.5f;
}
bool SkyProcessor::muted (int lane) const { return pMute[(size_t) lane]->load() > 0.5f || (anySolo() && pSolo[(size_t) lane]->load() < 0.5f); }

// random echoes: a channel's Echoes knob (with the global rate) sets how often its notes get thrown, and how loud
void SkyProcessor::echoFor (float amount, float& amt, int& line)
{
    amt = 0; line = 0;
    if (amount > 0 && audioRng.nextFloat() < pERate->load() * amount) { amt = amount * (0.6f + 0.4f * audioRng.nextFloat()); line = audioRng.nextInt (3); }
}

void SkyProcessor::play (int lane, int note, float vel, double dur, float pan, int offset, juce::MidiBuffer& midi, juce::uint32 hash, int cell)
{
    if (pOn[(size_t) lane]->load() < 0.5f || muted (lane)) return;
    lastFire[(size_t) lane] = stepNow;
    note = juce::jlimit (0, 127, note);
    dur *= 0.8 + 0.4 * audioRng.nextDouble();   // note lengths vary a little
    pan = juce::jlimit (-1.0f, 1.0f, pPan[(size_t) lane]->load() + pan * 0.5f);
    float ea; int el; echoFor (pEcho[(size_t) lane]->load(), ea, el);
    const int engine = pEngine[(size_t) lane]->load() < 0.5f ? sky::eObxa : sky::eMidiOnly;
    const auto patch = patchFor (lane);
    synth.noteOn (engine, lane, note, vel, dur, pan, offset, engine == sky::eObxa ? &patch : nullptr, ea, el);
    const int ch = juce::jlimit (1, 16, (int) pCh[(size_t) lane]->load());
    midi.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) juce::jlimit (1, 127, (int) std::round (vel * 127))), offset);
    if (numOffs < (int) offs.size()) offs[(size_t) numOffs++] = { sampleClock + offset + juce::jmax ((juce::int64) 1, (juce::int64) (dur * sr)), ch, note };
    int s1, n1, s2, n2;
    flashFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) flashes[(size_t) s1] = { hash, lane, cell };
    flashFifo.finishedWrite (n1);
}

static int pmod (juce::int64 a, int m) { return (int) (((a % m) + m) % m); }

void SkyProcessor::playStep (juce::int64 n, int off, juce::MidiBuffer& midi, double sd)
{
    using namespace sky;
    const int s16 = pmod (n, 16);
    currentStep = s16;
    stepNow = n;
    // auto-vary on the bar line every few bars (the change itself happens on the message thread)
    static const int autoEvery[4] = { 0, 4, 8, 16 };
    const int ab = autoEvery[juce::jlimit (0, 3, (int) pAutoBars->load())];
    if (s16 == 0 && n > 0 && ab > 0 && pmod (n / 16, ab) == 0) autoPending = true;

    auto loud = [this] (const MusicPlane& p) { return 0.45f + 0.55f * juce::jmax (0.0f, 1.0f - p.dist / (float) snap.radius); };
    auto each = [this] (int kind, int k, auto fn) { int c = 0; for (int i = 0; i < snap.numPlanes && c < k; ++i) if (snap.planes[(size_t) i].kind == kind) { fn (snap.planes[(size_t) i]); ++c; } };
    // a lane's rhythm: which steps it plays on; each plane's copy is shifted so several planes interlock
    const auto& rhythms = PresetLibrary::get().rhythms;
    auto mask = [&] (int lane) -> const std::array<bool, 16>& { static const std::array<bool, 16> quarters { true, false, false, false, true, false, false, false, true, false, false, false, true, false, false, false };
                                                               const int r = (int) pRhythm[(size_t) lane]->load(); return r >= 0 && r < (int) rhythms.size() ? rhythms[(size_t) r].on : quarters; };
    auto onBeat = [&] (int lane, const MusicPlane& p) { return mask (lane)[(size_t) ((s16 + (p.phase % 4) * 2) % 16)]; };
    auto lenOf = [&] (int lane, const MusicPlane& p) { const auto& m = mask (lane); const int st = (s16 + (p.phase % 4) * 2) % 16; int gap = 16;
                                                       for (int k = 1; k <= 16; ++k) if (m[(size_t) ((st + k) % 16)]) { gap = k; break; }
                                                       return gap * sd * pGate[(size_t) lane]->load(); };

    // circling -> arpeggio: turn rate sets speed, direction sets up/down, orbit size sets the span, altitude the octave
    static const int chord[8] = { 0, 3, 4, 5, 2, 6, 1, 4 };   // chord degree by the compass sector the plane circles in
    each (kArp, 4, [&] (const MusicPlane& p) {
        const float rate = std::abs (p.turnRate);
        const int every = rate >= 2.5f ? 1 : rate >= 1.2f ? 2 : 4;
        if (pmod (n + p.phase, every)) return;
        auto& slot = arp[p.hash % arp.size()];
        if (slot.hash != p.hash) slot = { p.hash, 0 };
        const int span = p.orbitR < 3 ? 4 : p.orbitR < 7 ? 6 : 8, i = slot.i++ % span, idx = p.turnRate >= 0 ? i : span - 1 - i;
        const int sector = juce::jlimit (0, 7, (int) (p.bearing / 45.0f));
        const int octave = p.alt < 3000 ? 60 : p.alt < 12000 ? 72 : 84;
        play (lArp, degToMidi (chord[sector] + idx * 2, octave), loud (p) * (i == 0 ? 1.0f : 0.8f), sd * every * pGate[lArp]->load() * 1.4, p.x, off, midi, p.hash);
    });
    // climbing / descending: pitch follows altitude, on the lane's rhythm
    each (kRise, 4, [&] (const MusicPlane& p) { if (onBeat (lRise, p)) play (lRise, degToMidi (juce::jmin (21, (int) (p.alt / 1500)), 62), juce::jmin (1.0f, 0.45f + p.vr / 6000), lenOf (lRise, p), p.x, off, midi, p.hash); });
    each (kFall, 4, [&] (const MusicPlane& p) { if (onBeat (lFall, p)) play (lFall, degToMidi (juce::jmin (21, (int) (p.alt / 1200)), 55), juce::jmin (1.0f, 0.45f - p.vr / 6000), lenOf (lFall, p), p.x, off, midi, p.hash); });
    // cruising: chord tones from flight level, low and wide, on the lane's rhythm
    {
        static const int tones[6] = { 0, 2, 4, 6, 7, 9 };
        each (kDrone, 3, [&] (const MusicPlane& p) { if (onBeat (lDrone, p)) play (lDrone, degToMidi (tones[(int) (p.alt / 3000) % 6], 43), loud (p) * 0.9f, lenOf (lDrone, p), p.x, off, midi, p.hash); });
    }
    // heat grid column -> up to two bass notes
    float mx = 0; for (auto h : snap.heat) mx = juce::jmax (mx, h);
    if (mx > 0)
    {
        std::array<std::pair<int, float>, gridRows> hits; int nh = 0;
        for (int r = 0; r < gridRows; ++r) { const float v = snap.heat[(size_t) (r * gridCols + s16)] / mx; if (v >= pThresh->load()) hits[(size_t) nh++] = { r, v }; }
        std::sort (hits.begin(), hits.begin() + nh, [] (auto& a, auto& b) { return a.second > b.second; });
        for (int k = 0; k < juce::jmin (2, nh); ++k)
            play (lGrid, degToMidi (hits[(size_t) k].first, 31), 0.35f + 0.65f * hits[(size_t) k].second, sd * pGate[lGrid]->load() * 1.5, ((s16 + 0.5f) / gridCols) * 2 - 1, off, midi, 0, hits[(size_t) k].first * gridCols + s16);
    }
    // low & close: hits on the lane's rhythm; arrivals: one hit, pitched by the direction they came from
    each (kHit, 3, [&] (const MusicPlane& p) { if (onBeat (lHit, p)) play (lHit, 50 + (int) (p.typeHash % 14), loud (p), lenOf (lHit, p), p.x, off, midi, p.hash); });
    float ax, ab2;
    if (s16 % 2 == 0 && model.popArrival (ax, ab2)) play (lHit, 57 + (int) (ab2 / 30) * 2, 0.6f, sd * pGate[lHit]->load(), ax, off, midi, 0);
    // emergency squawk: an alarm figure on the bell (audio only)
    if (snap.emergency && s16 % 8 == 0)
    {
        static const Patch alarm = [] { Patch p; p.w1 = 1; p.w2 = 1; p.pw = 0.2f; p.cut = 2500; p.env = 2; p.as = 0.6f; p.ar = 0.1f; p.vib = 20; p.lfo = 6; return p; }();
        synth.noteOn (eObxa, 6, degToMidi (s16 ? 4 : 7, 72), 0.6f, sd * 2, 0, off, &alarm);
    }
    drumStep (n, off, midi, sd);
}

void SkyProcessor::hitPattern (const sky::Pattern& pat, int s16, float level, int off, juce::MidiBuffer& midi, double sd)
{
    for (int d = 0; d < sky::numDrumVoices; ++d)
    {
        const int h = pat.hits[(size_t) d][(size_t) s16];
        if (h == 0) continue;
        const float v = (h == 2 ? 1.0f : 0.6f) * level;
        float ea; int el; echoFor (dEcho->load(), ea, el);
        synth.drumOn (d, v, juce::jlimit (-1.0f, 1.0f, dPan->load() + sky::drumPans[d]), off, ea, el);
        midi.addEvent (juce::MidiMessage::noteOn (10, sky::drumNotes[d], (juce::uint8) juce::jlimit (1, 127, (int) (v * 127))), off);
        if (numOffs < (int) offs.size()) offs[(size_t) numOffs++] = { sampleClock + off + (juce::int64) (sd * 0.5 * sr), 10, sky::drumNotes[d] };
    }
}

/* drums: the main beat plays all the time; a lane's drum loop starts on the next bar line after the lane plays and keeps
   going while it's still playing (it stops after two quiet bars). Hits use the step's sample offset, so they land on the grid. */
void SkyProcessor::drumStep (juce::int64 n, int off, juce::MidiBuffer& midi, double sd)
{
    const auto& lib = sky::PresetLibrary::get();
    const int s16 = pmod (n, 16);
    if (dMute->load() > 0.5f || (anySolo() && dSolo->load() < 0.5f)) return;
    const int mb = (int) pMainBeat->load();
    if (mb > 0 && mb < (int) lib.patterns.size()) hitPattern (lib.patterns[(size_t) mb], s16, 0.9f, off, midi, sd);
    for (int l = 0; l < numLanes; ++l)
    {
        const int pi = (int) pDrum[(size_t) l]->load();
        if (pi <= 0 || pi >= (int) lib.patterns.size() || pOn[(size_t) l]->load() < 0.5f) { loopOn[(size_t) l] = false; continue; }
        if (s16 == 0) loopOn[(size_t) l] = n - lastFire[(size_t) l] <= 32;
        if (loopOn[(size_t) l]) hitPattern (lib.patterns[(size_t) pi], s16, pDv[(size_t) l]->load(), off, midi, sd);
    }
}

void SkyProcessor::flushNoteOffs (juce::MidiBuffer& midi, int n)
{
    for (int i = 0; i < numOffs;)
    {
        if (offs[(size_t) i].at < sampleClock + n)
        {
            midi.addEvent (juce::MidiMessage::noteOff (offs[(size_t) i].ch, offs[(size_t) i].note), (int) juce::jmax ((juce::int64) 0, offs[(size_t) i].at - sampleClock));
            offs[(size_t) i] = offs[(size_t) --numOffs];
        }
        else ++i;
    }
}
void SkyProcessor::allNotesOff (juce::MidiBuffer& midi, int offset)
{
    for (int i = 0; i < numOffs; ++i) midi.addEvent (juce::MidiMessage::noteOff (offs[(size_t) i].ch, offs[(size_t) i].note), offset);
    numOffs = 0;
    for (int c = 1; c <= 16; ++c) midi.addEvent (juce::MidiMessage::allNotesOff (c), offset);
    synth.allOff();
}

void SkyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    midi.clear();
    {
        const juce::SpinLock::ScopedTryLockType tl (snapLock);
        if (tl.isLocked() && pendingFresh) { snap = pending; pendingFresh = false; }
    }
    emergency = snap.emergency;
    for (int i = 0; i < numLanes; ++i) { synth.laneVol[(size_t) i] = pVol[(size_t) i]->load(); synth.laneRev[(size_t) i] = pRev[(size_t) i]->load(); synth.laneDly[(size_t) i] = pDly[(size_t) i]->load(); }
    synth.laneVol[6] = 0.5f; synth.laneRev[6] = 0.5f; synth.laneDly[6] = 0.4f;
    {
        static float sky::Kit::* const kf[24] = { &sky::Kit::kp, &sky::Kit::kf, &sky::Kit::kd, &sky::Kit::kc, &sky::Kit::sf, &sky::Kit::sd, &sky::Kit::ss, &sky::Kit::hd, &sky::Kit::ho,
            &sky::Kit::ht, &sky::Kit::cd, &sky::Kit::tf, &sky::Kit::td, &sky::Kit::rf, &sky::Kit::pf, &sky::Kit::pr, &sky::Kit::pd, &sky::Kit::cy, &sky::Kit::ct, &sky::Kit::shd,
            &sky::Kit::cgf, &sky::Kit::cgd, &sky::Kit::tbd, &sky::Kit::lvl };
        for (int k = 0; k < 24; ++k) synth.kit.*kf[k] = pKit[(size_t) k]->load();
        synth.fx = { (int) pDMode->load(), pDMix->load(), pDFb->load(), pDTone->load() };
        synth.space = { pGTime->load(), pGMix->load(), pEMix->load() };
        // mixer: mute and solo silence a channel straight away (ringing notes too)
        for (int i = 0; i < numLanes; ++i) { if (muted (i)) synth.laneVol[(size_t) i] = 0; synth.laneGtd[(size_t) i] = pGtd[(size_t) i]->load(); }
        const bool drumsMuted = dMute->load() > 0.5f || (anySolo() && dSolo->load() < 0.5f);
        synth.drumVol = drumsMuted ? 0.0f : dVol->load(); synth.drumRev = dRev->load(); synth.drumDly = dDly->load(); synth.drumGtd = dGtd->load();
    }

    // timing: lock to the DAW's beat grid while it plays; otherwise run on our own tempo if asked to
    juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
    if (auto* ph = getPlayHead()) pos = ph->getPosition();
    const bool hostPlaying = pos && pos->getIsPlaying() && pos->getPpqPosition().hasValue();
    const bool always = pMode->load() > 0.5f || wrapperType == wrapperType_Standalone;
    const double bpm = hostPlaying && pos->getBpm() ? *pos->getBpm() : (pos && pos->getBpm() && ! always ? *pos->getBpm() : (double) pBpm->load());
    currentBpm = bpm;
    synth.setBpm (bpm);
    const double stepSec = 60.0 / bpm / 4.0, stepsPerSample = 1.0 / (stepSec * sr);
    const bool run = hostPlaying || always;

    if (! run && wasPlaying) { allNotesOff (midi, 0); lastStep = -1; currentStep = -1; }
    wasPlaying = run; running = run;

    if (run)
    {
        const double startStep = hostPlaying ? *pos->getPpqPosition() * 4.0 : freePos;
        auto s = (juce::int64) std::ceil (startStep - 1e-9);
        for (;; ++s)
        {
            const int off = (int) std::round ((s - startStep) / stepsPerSample);
            if (off >= n) break;
            if (s == lastStep) continue;
            playStep (s, juce::jlimit (0, n - 1, off), midi, stepSec);
            lastStep = s;
        }
        if (! hostPlaying) freePos += n * stepsPerSample; else freePos = startStep + n * stepsPerSample;
    }
    flushNoteOffs (midi, n);

    synth.render (buffer, n);
    buffer.applyGain (0, n, pMaster->load());
    masterPeak = juce::jmax (masterPeak.load(), buffer.getMagnitude (0, n));
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    sampleClock += n;
}

void SkyProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = params.copyState();
    if (auto old = st.getChildWithName ("SkyLocation"); old.isValid()) st.removeChild (old, nullptr);
    st.appendChild (state.createCopy(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}
void SkyProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto st = juce::ValueTree::fromXml (*xml);
    if (! st.isValid()) return;
    if (auto loc = st.getChildWithName ("SkyLocation"); loc.isValid()) { state.copyPropertiesFrom (loc, nullptr); st.removeChild (loc, nullptr); }
    params.replaceState (st);
    pushSettings();
}

/* ---- presets ---- */
void SkyProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (params.getParameter (id)))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void SkyProcessor::applyPreset (const juce::var& pre)
{
    if (! pre.isObject()) return;
    setParam ("key", (float) (int) pre["key"]);
    setParam ("scale", (float) juce::jmax (0, scaleNames().indexOf (pre["scale"].toString())));
    setParam ("bpm", (float) pre["bpm"]);
    const auto& d = pre["delay"];
    setParam ("dmode", (float) (int) d["mode"]); setParam ("dmix", (float) d["mix"]); setParam ("dfb", (float) d["fb"]); setParam ("dtone", (float) d["tone"]);
    for (auto& s : sky::kitSpecs()) if (pre["kit"].hasProperty (s.id)) setParam (juce::String ("kit_") + s.id, (float) pre["kit"][s.id]);
    const auto& lib = sky::PresetLibrary::get();
    for (int l = 0; l < numLanes; ++l)
    {
        const auto& L = pre["lanes"][laneInfo[l].id];
        if (! L.isObject()) continue;
        const juce::String id (laneInfo[l].id);
        setParam (id + "_on", (bool) L["on"] ? 1.0f : 0.0f);
        setParam (id + "_sound", L["engine"].toString() == "none" ? 1.0f : 0.0f);
        setParam (id + "_ch", (float) (int) L["ch"]);
        setParam (id + "_vol", (float) L["vol"]); setParam (id + "_rev", (float) L["rev"]); setParam (id + "_dly", (float) L["dly"]);
        setParam (id + "_drum", (float) lib.patternIndex (L["drum"].toString()));
        setParam (id + "_dv", (float) L["dv"]);
        for (int k = 0; k < 21; ++k) { const auto* key = sky::patchSpecs()[(size_t) k].id; if (L["patch"].hasProperty (key)) setParam (patchParamId (l, k), (float) L["patch"][key]); }
        for (auto key : { "pan", "gtd", "echo", "gate" }) if (L.hasProperty (key)) setParam (id + "_" + key, (float) L[key]);
        for (auto key : { "mute", "solo", "auto" }) if (L.hasProperty (key)) setParam (id + "_" + key, (bool) L[key] ? 1.0f : 0.0f);
        if (L.hasProperty ("rhythm")) setParam (id + "_rhythm", (float) (int) L["rhythm"]);
    }
    if (const auto& D = pre["drums"]; D.isObject())
    {
        for (auto key : { "vol", "pan", "rev", "dly", "gtd", "echo" }) if (D.hasProperty (key)) setParam (juce::String ("drm_") + key, (float) D[key]);
        for (auto key : { "mute", "solo" }) setParam (juce::String ("drm_") + key, D.hasProperty (key) && (bool) D[key] ? 1.0f : 0.0f);
    }
    if (const auto& F = pre["fx"]; F.isObject()) for (auto key : { "gtime", "gmix", "erate", "emix" }) if (F.hasProperty (key)) setParam (key, (float) F[key]);
    if (pre.hasProperty ("mainBeat")) setParam ("mainbeat", (float) lib.patternIndex (pre["mainBeat"].toString()));
    if (pre.hasProperty ("autoBars")) { const int b = pre["autoBars"]; setParam ("autobars", b >= 16 ? 3.0f : b >= 8 ? 2.0f : b >= 4 ? 1.0f : 0.0f); }
    state.setProperty ("preset", pre["name"].toString(), nullptr);
}
void SkyProcessor::loadPreset (int i)
{
    const auto& p = sky::PresetLibrary::get().presets;
    if (i >= 0 && i < p.size()) applyPreset (p[i]);
}
void SkyProcessor::generatePreset() { applyPreset (sky::generatePreset (rng, "Generated " + juce::Time::getCurrentTime().toString (false, true, false))); }
void SkyProcessor::randomKit()
{
    const auto k = sky::randomKit (rng);
    for (auto& s : sky::kitSpecs()) setParam (juce::String ("kit_") + s.id, (float) k[s.id]);
}
/* auto-vary: lanes that allow it drift to a new rhythm, their note length changes, their OB-Xa patch moves part of the
   way to a fresh one, and now and then they swap to a sound no other lane is using */
void SkyProcessor::autoVary()
{
    for (int l = 0; l < numLanes; ++l)
    {
        if (pAuto[(size_t) l]->load() < 0.5f) continue;
        const juce::String id (laneInfo[l].id);
        const auto rh = sky::roleRhythms (id);
        if (! rh.empty() && rng.nextFloat() < 0.6f) setParam (id + "_rhythm", (float) rh[(size_t) rng.nextInt ((int) rh.size())]);
        setParam (id + "_gate", juce::jlimit (0.15f, 1.0f, pGate[(size_t) l]->load() * (0.75f + rng.nextFloat() * 0.55f)));
        auto* cur = new juce::DynamicObject();
        for (int k = 0; k < 21; ++k) cur->setProperty (sky::patchSpecs()[(size_t) k].id, pPatch[(size_t) l][(size_t) k]->load());
        const auto next = sky::mutatePatch (juce::var (cur), id, 0.35f, rng);
        for (int k = 0; k < 21; ++k) setParam (patchParamId (l, k), (float) next[sky::patchSpecs()[(size_t) k].id]);
    }
}

void SkyProcessor::randomPatch (int lane)
{
    const auto p = sky::randomPatch (laneInfo[lane].id, rng);
    for (int k = 0; k < 21; ++k) setParam (patchParamId (lane, k), (float) p[sky::patchSpecs()[(size_t) k].id]);
}

juce::AudioProcessorEditor* SkyProcessor::createEditor() { return new SkyEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkyProcessor(); }
