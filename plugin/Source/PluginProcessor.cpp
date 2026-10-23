#include "PluginProcessor.h"
#include "PluginEditor.h"

const LaneInfo laneInfo[6] = {
    { "arp",   "Circling",    "Arpeggio",     0xffff5ec8, sky::ePluck, 1,  0.8f,  0.25f, 0.35f },
    { "rise",  "Climbing",    "Rising line",  0xffffd23f, sky::eGlass, 2,  0.7f,  0.35f, 0.25f },
    { "fall",  "Descending",  "Falling line", 0xff4fc3ff, sky::eBell,  3,  0.65f, 0.45f, 0.3f },
    { "drone", "Cruising",    "Pad",          0xff9c8cff, sky::ePad,   4,  0.75f, 0.6f,  0.1f },
    { "grid",  "Sky grid",    "Sequence",     0xff4dffa6, sky::eBass,  5,  0.75f, 0.1f,  0.15f },
    { "hit",   "Low & close", "Percussion",   0xffff8a3d, sky::ePerc,  10, 0.7f,  0.15f, 0.1f },
};
const int windowMinutes[7] = { 1, 2, 5, 10, 15, 30, 60 };

juce::StringArray scaleNames() { return { "minor", "major", "dorian", "lydian", "phrygian", "pentatonic minor", "pentatonic major", "harmonic minor", "whole tone" }; }
const std::vector<int>& scaleSteps (int i)
{
    static const std::vector<std::vector<int>> s = { { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 6, 7, 9, 11 },
                                                     { 0, 1, 3, 5, 7, 8, 10 }, { 0, 3, 5, 7, 10 }, { 0, 2, 4, 7, 9 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 4, 6, 8, 10 } };
    return s[(size_t) juce::jlimit (0, (int) s.size() - 1, i)];
}

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
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id + "_engine", 1 }, nm + " sound", sky::engineNames(), ln.engine));
        l.add (std::make_unique<AudioParameterInt> (ParameterID { id + "_ch", 1 }, nm + " MIDI channel", 1, 16, ln.channel));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_vol", 1 }, nm + " volume", NormalisableRange<float> (0, 1.5f, 0.01f), ln.vol));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_rev", 1 }, nm + " reverb", NormalisableRange<float> (0, 1, 0.01f), ln.rev));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id + "_dly", 1 }, nm + " delay", NormalisableRange<float> (0, 1, 0.01f), ln.dly));
    }
    return l;
}

SkyProcessor::SkyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "SkyParams", layout())
{
    for (int i = 0; i < numLanes; ++i)
    {
        const juce::String id (laneInfo[i].id);
        pOn[(size_t) i] = params.getRawParameterValue (id + "_on"); pEngine[(size_t) i] = params.getRawParameterValue (id + "_engine");
        pCh[(size_t) i] = params.getRawParameterValue (id + "_ch"); pVol[(size_t) i] = params.getRawParameterValue (id + "_vol");
        pRev[(size_t) i] = params.getRawParameterValue (id + "_rev"); pDly[(size_t) i] = params.getRawParameterValue (id + "_dly");
    }
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
    pushSettings();
    feed.startThread();

    // the grid window can be automated, so it's picked up here on the message thread rather than in processBlock
    poller.fn = [this] { pushSettings(); };
    poller.startTimer (500);

    // first run: find the listener from their internet connection (they can search or type a place any time)
    lookups.addJob ([this] {
        sky::Place pl; juce::String err;
        if (! sky::locateByIp (pl, err)) return;
        juce::MessageManager::callAsync ([this, pl, alive = std::weak_ptr<bool> (aliveFlag)] {
            if (alive.expired() || placeName().isNotEmpty()) return;   // a saved or chosen place wins
            setLocation (pl.lat, pl.lon, pl.name + " (from your connection)");
        });
    });
}

SkyProcessor::~SkyProcessor()
{
    aliveFlag.reset();
    poller.stopTimer();
    lookups.removeAllJobs (true, 12000);
    feed.stopThread (4000);
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

void SkyProcessor::play (int lane, int note, float vel, double dur, float pan, int offset, juce::MidiBuffer& midi, juce::uint32 hash, int cell)
{
    if (pOn[(size_t) lane]->load() < 0.5f) return;
    note = juce::jlimit (0, 127, note);
    synth.noteOn ((int) pEngine[(size_t) lane]->load(), lane, note, vel, dur, pan, offset);
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
    auto loud = [this] (const MusicPlane& p) { return 0.45f + 0.55f * juce::jmax (0.0f, 1.0f - p.dist / (float) snap.radius); };
    auto each = [this] (int kind, int k, auto fn) { int c = 0; for (int i = 0; i < snap.numPlanes && c < k; ++i) if (snap.planes[(size_t) i].kind == kind) { fn (snap.planes[(size_t) i]); ++c; } };

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
        const int octave = p.alt < 3000 ? 48 : p.alt < 12000 ? 60 : 72;
        play (lArp, degToMidi (chord[sector] + idx * 2, octave), loud (p) * (i == 0 ? 1.0f : 0.8f), sd * every * 0.85, p.x, off, midi, p.hash);
    });
    // climbing / descending: pitch follows altitude
    each (kRise, 4, [&] (const MusicPlane& p) { if (pmod (n + p.phase, 4) == 0) play (lRise, degToMidi (juce::jmin (21, (int) (p.alt / 1500)), 55), juce::jmin (1.0f, 0.45f + p.vr / 6000), sd * 2.5, p.x, off, midi, p.hash); });
    each (kFall, 4, [&] (const MusicPlane& p) { if (pmod (n + p.phase, 4) == 2) play (lFall, degToMidi (juce::jmin (21, (int) (p.alt / 1200)), 50), juce::jmin (1.0f, 0.45f - p.vr / 6000), sd * 3, p.x, off, midi, p.hash); });
    // cruising: one sustained chord tone per plane per bar
    if (s16 == 0)
    {
        static const int tones[6] = { 0, 2, 4, 6, 7, 9 };
        each (kDrone, 3, [&] (const MusicPlane& p) { play (lDrone, degToMidi (tones[(int) (p.alt / 3000) % 6], 48), loud (p) * 0.9f, sd * 16, p.x, off, midi, p.hash); });
    }
    // heat grid column -> up to three notes
    float mx = 0; for (auto h : snap.heat) mx = juce::jmax (mx, h);
    if (mx > 0)
    {
        std::array<std::pair<int, float>, gridRows> hits; int nh = 0;
        for (int r = 0; r < gridRows; ++r) { const float v = snap.heat[(size_t) (r * gridCols + s16)] / mx; if (v >= pThresh->load()) hits[(size_t) nh++] = { r, v }; }
        std::sort (hits.begin(), hits.begin() + nh, [] (auto& a, auto& b) { return a.second > b.second; });
        for (int k = 0; k < juce::jmin (3, nh); ++k)
            play (lGrid, degToMidi (hits[(size_t) k].first, 36), 0.35f + 0.65f * hits[(size_t) k].second, sd * 0.9, ((s16 + 0.5f) / gridCols) * 2 - 1, off, midi, 0, hits[(size_t) k].first * gridCols + s16);
    }
    // low & close: a hit every beat; arrivals: one hit, pitched by the direction they came from
    each (kHit, 3, [&] (const MusicPlane& p) { if (pmod (n + p.phase, 4) == 0) play (lHit, 38 + (int) (p.typeHash % 14), loud (p), sd, p.x, off, midi, p.hash); });
    float ax, ab;
    if (s16 % 2 == 0 && model.popArrival (ax, ab)) play (lHit, 45 + (int) (ab / 30) * 2, 0.6f, sd, ax, off, midi, 0);
    // emergency squawk: an alarm figure on the bell (audio only)
    if (snap.emergency && s16 % 8 == 0) synth.noteOn (eBell, 6, degToMidi (s16 ? 4 : 7, 72), 0.6f, sd * 2, 0, off);
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

juce::AudioProcessorEditor* SkyProcessor::createEditor() { return new SkyEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkyProcessor(); }
