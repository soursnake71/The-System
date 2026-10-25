#include "PluginProcessor.h"
#include "PluginEditor.h"

using farp::Val;

FarpProcessor::FarpProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Voice", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (auto& l : live) l = { -1, -1 };
    local.reserve (512); graveyard.reserve (512); inbox.reserve (512); toFree.reserve (512);
    outbox.msgs.reserve (64); outbox.keys.reserve (256); outbox.state.reserve (2048);
    auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("FARP ROMs");
    romDir = docs;
    samplesDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("FARP Samples");
}

bool FarpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

// ---------------------------------------------------------------------------------------------
// the replay cache: the latest of each kind of setting message, in the order they first arrived
std::string FarpProcessor::keyOf (const Val& m)
{
    const std::string& t = m["t"].str();
    if (t == "mix" || t == "p5" || t == "e2" || t == "e2z" || t == "voc" || t == "route" || t == "vin" || t == "vcfg") return t;
    if (t == "vphr") return "vphr:" + std::to_string ((int) m["id"].num());
    if (t == "pads" || t == "rack") return t;
    if (t == "padbuf") return "padbuf:" + std::to_string ((int) m["i"].num());
    if (t == "eng")
    {
        const std::string& op = m["op"].str();
        const std::string& i = m["i"].str();
        if (op == "load" || op == "voice" || op == "tone" || op == "zones" || op == "params" || op == "level" || op == "run") return "eng:" + i + ":" + op;
        if (op == "pot") { const Val& k = m["k"]; return "eng:" + i + ":pot:" + (k.isStr() ? k.str() : std::to_string ((int) k.num())); }
    }
    return {};
}

void FarpProcessor::fromPage (const juce::String& json)
{
    auto text = json.toStdString();
    auto v = std::make_shared<Val> (Val::parse (text));
    const std::string& t = (*v)["t"].str();
    if (t.empty()) return;

    bool forward = true;
    {
        std::lock_guard<std::mutex> g (cacheMutex);
        if (t == "all") { params.clear(); for (auto& kv : (*v)["p"].o) params[kv.first] = kv.second; }
        else if (t == "p") params[(*v)["k"].str()] = (*v)["v"];
        else if (t == "vreset")
            cache.erase (std::remove_if (cache.begin(), cache.end(), [] (const Cached& c) { return c.key.rfind ("vphr:", 0) == 0; }), cache.end());
        else if (t == "eng" && (*v)["op"].str() == "unload")
        {
            const std::string pre = "eng:" + (*v)["i"].str() + ":load";
            cache.erase (std::remove_if (cache.begin(), cache.end(), [&] (const Cached& c) { return c.key == pre; }), cache.end());
        }
        else
        {
            auto key = keyOf (*v);
            if (! key.empty())
            {
                auto it = std::find_if (cache.begin(), cache.end(), [&] (const Cached& c) { return c.key == key; });
                if (it != cache.end())
                {
                    // the page re-sends its ROM sounds whenever the editor opens: skip what the engine already has
                    const bool heavy = key == "e2z" || key.rfind ("padbuf:", 0) == 0 || key.find (":load") != std::string::npos || key.find (":zones") != std::string::npos;
                    if (heavy && it->raw == text)
                    {
                        forward = false;
                        if (key.find (":load") != std::string::npos)
                        {
                            farp::JsonOut j; j.raw ("{\"t\":\"eng\""); j.key ("i"); j.str ((*v)["i"].str()); j.key ("loaded"); j.raw ("true"); j.raw ("}");
                            std::lock_guard<std::mutex> o (outbox.m); outbox.msgs.push_back (std::move (j.s));
                        }
                    }
                    else { it->raw = std::move (text); it->val = v; }
                }
                else cache.push_back ({ key, std::move (text), v });
            }
        }
    }
    if (! forward) return;
    std::lock_guard<std::mutex> g (inMutex);
    inbox.push_back (std::move (v));
    toFree.clear();
}

void FarpProcessor::setLs (const juce::String& key, const juce::String& rawJson)
{
    std::lock_guard<std::mutex> g (cacheMutex);
    ls[key] = rawJson;
}

juce::String FarpProcessor::stateScript() const
{
    std::lock_guard<std::mutex> g (cacheMutex);
    juce::String s = "window.__FARP_STATE={sr:" + juce::String (sr > 0 ? sr : 48000.0) + ",ls:{";
    bool first = true;
    for (auto& kv : ls)
    {
        if (! first) s << ",";
        first = false;
        s << juce::JSON::toString (juce::var (kv.first)) << ":" << juce::JSON::toString (juce::var (kv.second));
    }
    s << "},romDir:" << juce::JSON::toString (juce::var (romDir.getFullPathName())) << "};";
    return s;
}

juce::String FarpProcessor::romListJson() const
{
    juce::Array<juce::var> arr;
    if (romDir.isDirectory())
        for (auto& f : romDir.findChildFiles (juce::File::findFiles, false, "*.zip"))
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("n", f.getFileName());
            o->setProperty ("s", (juce::int64) f.getSize());
            arr.add (juce::var (o));
        }
    return juce::JSON::toString (juce::var (arr), true);
}

juce::String FarpProcessor::sampleListJson() const
{
    juce::Array<juce::var> arr;
    if (samplesDir.isDirectory())
    {
        int n = 0;
        for (const auto& e : juce::RangedDirectoryIterator (samplesDir, true, "*.wav;*.wave;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a", juce::File::findFiles))
        {
            const auto rel = e.getFile().getRelativePathFrom (samplesDir).replaceCharacter ('\\', '/');
            if (rel.startsWithChar ('.') || rel.contains ("/.")) continue;
            arr.add (rel);
            if (++n >= 20000) break;
        }
    }
    return juce::JSON::toString (juce::var (arr), true);
}

void FarpProcessor::pageReady()
{
    // the page keeps no copy of the user's own pad samples: send back what the project holds
    std::lock_guard<std::mutex> g (cacheMutex);
    std::lock_guard<std::mutex> o (outbox.m);
    for (auto& c : cache)
        if (c.key.rfind ("padbuf:", 0) == 0 && (*c.val)["user"].truthy())
            outbox.msgs.push_back (c.raw);
}

// ---------------------------------------------------------------------------------------------
void FarpProcessor::rebuildEngine()
{
    dsp = std::make_unique<farp::FarpDSP> (sr);
    pads = std::make_unique<farp::PadSampler> (sr);
    rack = std::make_unique<farp::rack::Rack> (sr);
    rtL.assign (RT, 0.f); rtR.assign (RT, 0.f); rtW = 0;
    dsp->post = [this] (std::string&& s) { postOut (std::move (s)); };
    dsp->onScope = [this] (const float* sc, const float* pk)
    {
        std::unique_lock<std::mutex> o (outbox.m, std::try_to_lock);
        if (! o.owns_lock()) return;
        std::copy (sc, sc + 1024, outbox.scope);
        std::copy (pk, pk + 12, outbox.peaks);
        outbox.state = dsp->stateJson;
        outbox.scopeNew = true;
    };
    for (auto& l : live) l = { -1, -1 };
    std::fill (std::begin (liveOn), std::end (liveOn), false);
    heldMidi = 0; play.on = false; phrases.clear();

    std::lock_guard<std::mutex> g (cacheMutex);
    if (! params.empty())
    {
        Val all; all.type = Val::Obj; Val t; t.type = Val::Str; t.s = "all"; all.o["t"] = t;
        Val p; p.type = Val::Obj; p.o = std::map<std::string, Val> (params.begin(), params.end()); all.o["p"] = p;
        dsp->msg (all);
    }
    for (auto& c : cache)
    {
        applyOnAudio (*c.val);
    }
}

void FarpProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    voiceBuf.assign ((size_t) juce::jmax (samplesPerBlock, 32) * 2 + 64, 0.f);
    if (dsp == nullptr || sampleRate != sr) { sr = sampleRate; rebuildEngine(); }
}

void FarpProcessor::postOut (std::string&& s)
{
    std::unique_lock<std::mutex> o (outbox.m, std::try_to_lock);
    if (o.owns_lock() && outbox.msgs.size() < 256) outbox.msgs.push_back (std::move (s));
}

// messages the engine doesn't know: MIDI routing and the vocoder's spoken phrases
void FarpProcessor::applyOnAudio (const Val& m)
{
    const std::string& t = m["t"].str();
    if (t == "route")
    {
        route.keys = m["keys"].str();
        const Val& sp = m["split"];
        route.splitOn = sp["on"].truthy(); route.point = (int) sp["point"].num (60);
        auto zone = [] (const Val& z, Route::Z& d)
        {
            d.str = z["str"].truthy(); d.syn = z["syn"].truthy(); d.bas = z["bas"].truthy(); d.oct = (int) z["oct"].num();
            const Val& i = z["inst"]; d.keysInst = i.isNull() || (i.isStr() && i.str() == "keys"); d.inst = i.isStr() ? i.str() : std::string();
        };
        zone (sp["lo"], route.lo); zone (sp["hi"], route.hi);
        const Val& vr = m["vr"]; route.vr = vr.isObj(); route.vrM = (int) vr["m"].num (7); route.vrI = vr["i"].str();
        return;
    }
    if (t == "vphr")
    {
        Phrase p; p.fs = m["fs"].num (22050);
        if (m["data"].type == Val::F32) p.data = m["data"].f; else p.data = std::make_shared<std::vector<float>>();
        phrases[(int) m["id"].num()] = p;
        return;
    }
    if (t == "vsay") { say ((int) m["id"].num(), m["loop"].truthy(), m["at"].num()); return; }
    if (t == "vreset") { phrases.clear(); return; }
    if (t == "vcfg")
    {
        vcfg.on = m["on"].truthy(); vcfg.trig = m["trig"].isStr() ? m["trig"].str() : std::string ("say"); vcfg.li = (int) m["li"].num();
        vcfg.ids.clear(); for (size_t i = 0; i < m["ids"].size(); i++) vcfg.ids.push_back ((int) m["ids"][i].num());
        return;
    }
    if (t == "vstop") { play.stopAt = m["at"].num(); return; }
    if (t == "vfree") { phrases.erase ((int) m["id"].num()); return; }
    if (t == "pads") { pads->setPads (m); return; }
    if (t == "padbuf") { pads->setBuf ((int) m["i"].num(), m); return; }
    if (t == "padtrig") { pads->trigger ((int) m["i"].num(), m["v"].num (1), m["at"].num(), farp::PadSampler::xFrom (m["x"]), dsp->time()); return; }
    if (t == "rack") { rack->set (m); return; }
    if (t == "vin") { useInput = m["on"].truthy(); return; }
    dsp->msg (m);
}

void FarpProcessor::say (int id, bool loop, double at)
{
    auto it = phrases.find (id);
    if (it == phrases.end() || ! it->second.data) return;
    play.ph = it->second; play.pos = 0; play.step = it->second.fs / sr; play.loop = loop;
    play.startAt = at; play.stopAt = -1; play.on = true; sayDone = true;
}

// chords played from the host speak the vocoder's lines, the way the on-screen keyboard does
void FarpProcessor::vocTrigger()
{
    if (! vcfg.on || vcfg.ids.empty()) return;
    const int n = (int) vcfg.ids.size();
    if (vcfg.li < 0 || vcfg.li >= n) vcfg.li = 0;
    const double at = dsp->time();
    if (vcfg.trig == "keys")
    {
        if (sayDone && n > 1)
        {
            vcfg.li = (vcfg.li + 1) % n;
            farp::JsonOut j; j.raw ("{\"t\":\"vli\""); j.key ("li"); j.num (vcfg.li); j.raw ("}"); postOut (std::move (j.s));
        }
        say (vcfg.ids[(size_t) vcfg.li], false, at);
    }
    else if (! play.on) say (vcfg.ids[(size_t) vcfg.li], vcfg.trig == "loop", at);
}

int FarpProcessor::padOfNote (int n)
{
    switch (n)
    {
        case 35: case 36: return 0; case 38: case 40: return 1; case 39: return 2; case 42: case 44: return 3; case 46: return 4;
        case 41: case 43: return 5; case 45: case 47: return 6; case 48: case 50: return 7; case 37: return 8; case 56: return 9;
        case 49: case 57: return 10; case 51: case 59: return 11; case 54: case 69: return 12; case 70: return 15;
        case 62: case 63: case 64: return 13; case 67: case 68: return 14;
        default: return (((n - 36) % 16) + 16) % 16;
    }
}

void FarpProcessor::routeKey (int k, int& n, int& m, int& inst) const
{
    auto insOf = [] (const std::string& s) { return s.empty() ? -1 : farp::FarpDSP::insOf (s); };
    if (route.vr && ! route.splitOn) { n = k; m = route.vrM; inst = insOf (route.vrI); return; }
    if (! route.splitOn) { n = k; m = 7; inst = insOf (route.keys); return; }
    const auto& z = k < route.point ? route.lo : route.hi;
    n = juce::jlimit (0, 127, k + 12 * z.oct);
    m = (z.str ? 1 : 0) | (z.syn ? 2 : 0) | (z.bas ? 4 : 0) | 8;
    inst = insOf (z.keysInst ? route.keys : z.inst);
}

void FarpProcessor::midiNote (int k, double vel, bool on)
{
    if (k < 0 || k > 127) return;
    if (on)
    {
        if (liveOn[k]) dsp->noteOffMsg (live[k].n, live[k].inst);
        int n, m, inst; routeKey (k, n, m, inst);
        live[k] = { n, inst }; liveOn[k] = true;
        dsp->noteOnMsg (n, vel, m, inst);
        if (heldMidi++ == 0) vocTrigger();
    }
    else
    {
        if (! liveOn[k]) return;
        liveOn[k] = false; if (heldMidi > 0) heldMidi--;
        dsp->noteOffMsg (live[k].n, live[k].inst);
    }
    std::unique_lock<std::mutex> o (outbox.m, std::try_to_lock);
    if (o.owns_lock() && outbox.keys.size() < outbox.keys.capacity()) outbox.keys.push_back (on ? k + 1 : -(k + 1));
}

void FarpProcessor::renderChunk (float* L, float* R, int n, const float* inL, const float* inR, int numIn)
{
    const double t0 = dsp->time();
    float v[64];
    for (int i = 0; i < n; i++)
    {
        float x = 0;
        if (useInput && numIn > 0) x = numIn > 1 ? 0.5f * (inL[i] + inR[i]) : inL[i];
        if (play.on && play.ph.data)
        {
            const double now = t0 + i / sr;
            if (play.stopAt >= 0 && now >= play.stopAt) play.on = false;
            else if (now >= play.startAt)
            {
                const auto& d = *play.ph.data; const size_t len = d.size();
                if (len < 2) play.on = false;
                else
                {
                    if (play.pos >= (double) (len - 1)) { if (play.loop) play.pos = std::fmod (play.pos, (double) (len - 1)); else play.on = false; }
                    if (play.on)
                    {
                        const size_t a = (size_t) play.pos; const double f = play.pos - (double) a;
                        x += (float) (d[a] + (d[a + 1] - d[a]) * f);
                        play.pos += play.step;
                    }
                }
            }
        }
        v[i] = x;
    }
    std::fill (padL, padL + n, 0.f); std::fill (padR, padR + n, 0.f);
    pads->render (padL, padR, n, t0);
    for (int i = 0; i < n; i++) { const int r = (rtW - RT_LAT + i + RT) % RT; retL[i] = rtL[(size_t) r]; retR[i] = rtR[(size_t) r]; }
    dsp->process (L, R, n, t0, v, padL, padR, sendBuf, retL, retR);
    std::fill (rackL, rackL + n, 0.f); std::fill (rackR, rackR + n, 0.f);
    if (! rack->empty()) rack->process (sendBuf, rackL, rackR, n);
    for (int i = 0; i < n; i++) { rtL[(size_t) rtW] = rackL[i]; rtR[(size_t) rtW] = rackR[i]; rtW = (rtW + 1) % RT; }
}

void FarpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int N = buffer.getNumSamples();
    if (dsp == nullptr) { buffer.clear(); return; }

    // the page's messages
    {
        std::unique_lock<std::mutex> g (inMutex, std::try_to_lock);
        if (g.owns_lock())
        {
            local.swap (inbox);
            for (auto& x : graveyard) toFree.push_back (std::move (x));
            graveyard.clear();
        }
    }
    for (auto& m : local) { applyOnAudio (*m); graveyard.push_back (std::move (m)); }
    local.clear();

    const int numIn = getTotalNumInputChannels();
    const float* inL = numIn > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;
    // keep a mono copy of the voice input: the output overwrites the buffer
    if ((int) voiceBuf.size() < N) voiceBuf.resize ((size_t) N);
    float* inCopy = voiceBuf.data();
    for (int i = 0; i < N; i++) inCopy[i] = numIn > 0 ? (numIn > 1 ? 0.5f * (inL[i] + inR[i]) : inL[i]) : 0.f;

    const int outCh = buffer.getNumChannels();
    float* L = buffer.getWritePointer (0);
    float* R = outCh > 1 ? buffer.getWritePointer (1) : nullptr;
    float tmpR[64];

    int pos = 0;
    auto renderTo = [&] (int end)
    {
        while (pos < end)
        {
            const int n = juce::jmin (end - pos, 64);
            float* r = R ? R + pos : tmpR;
            renderChunk (L + pos, r, n, inCopy + pos, inCopy + pos, numIn > 0 ? 1 : 0);
            if (! R) for (int i = 0; i < n; i++) L[pos + i] = 0.5f * (L[pos + i] + r[i]);
            pos += n;
        }
    };

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        renderTo (juce::jlimit (0, N, meta.samplePosition));
        if (msg.getChannel() == 10 && (msg.isNoteOn() || msg.isNoteOff()))
        {
            if (msg.isNoteOn())
            {
                const int k = padOfNote (msg.getNoteNumber());
                const int shown = pads->hit (k, msg.getFloatVelocity(), dsp->time(), dsp->time());
                farp::JsonOut j; j.raw ("{\"t\":\"padflash\""); j.key ("i"); j.num (shown); j.raw ("}"); postOut (std::move (j.s));
            }
            continue;
        }
        if (msg.isNoteOn()) midiNote (msg.getNoteNumber(), msg.getFloatVelocity(), true);
        else if (msg.isNoteOff()) midiNote (msg.getNoteNumber(), 0, false);
        else if (msg.isPitchWheel()) dsp->setBend ((msg.getPitchWheelValue() - 8192) / 8192.0 * 2.0);
        else if (msg.isChannelPressure()) dsp->setMod (msg.getChannelPressureValue() / 127.0);
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber(), v = msg.getControllerValue();
            if (cc == 1) dsp->setMod (v / 127.0);
            else if (cc == 64) dsp->setSus (v >= 64 ? 1 : 0);
            else if (cc == 120 || cc == 123) { for (int k = 0; k < 128; k++) if (liveOn[k]) midiNote (k, 0, false); if (cc == 120) { dsp->panic(); pads->allOff(); } }
        }
    }
    renderTo (N);
    for (int c = 2; c < outCh; c++) buffer.clear (c, 0, N);

    double bpm = 0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
    std::unique_lock<std::mutex> o (outbox.m, std::try_to_lock);
    if (o.owns_lock()) { outbox.time = dsp->time(); if (bpm > 0) outbox.bpm = bpm; }
}

// ---------------------------------------------------------------------------------------------
// state: the page's saved settings, the ROM folder and the engine's settings (gzipped JSON)
void FarpProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::String s;
    {
        std::lock_guard<std::mutex> g (cacheMutex);
        s << "{\"v\":1,\"romDir\":" << juce::JSON::toString (juce::var (romDir.getFullPathName())) << ",\"samplesDir\":" << juce::JSON::toString (juce::var (samplesDir.getFullPathName())) << ",\"ls\":{";
        bool first = true;
        for (auto& kv : ls) { if (! first) s << ","; first = false; s << juce::JSON::toString (juce::var (kv.first)) << ":" << juce::JSON::toString (juce::var (kv.second)); }
        s << "},\"params\":{";
        first = true;
        for (auto& kv : params)
        {
            if (! first) s << ",";
            first = false;
            s << juce::JSON::toString (juce::var (juce::String (kv.first))) << ":";
            const auto& v = kv.second;
            if (v.type == Val::Num || v.type == Val::Bool) s << juce::String (v.num(), 10);
            else if (v.type == Val::Str) s << juce::JSON::toString (juce::var (juce::String (v.s)));
            else s << "null";
        }
        s << "},\"cache\":[";
        first = true;
        for (auto& c : cache) { if (! first) s << ","; first = false; s << juce::String (c.raw); }
        s << "]}";
    }
    juce::MemoryOutputStream out (dest, false);
    out.writeString ("FARP1");
    juce::GZIPCompressorOutputStream gz (out, 6);
    gz.writeString (s);
    gz.flush();
}

void FarpProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream in (data, (size_t) size, false);
    if (in.readString() != "FARP1") return;
    juce::GZIPDecompressorInputStream gz (in);
    const auto text = gz.readString().toStdString();
    const Val st = Val::parse (text);
    {
        std::lock_guard<std::mutex> g (cacheMutex);
        ls.clear(); params.clear(); cache.clear();
        if (st["romDir"].isStr() && st["romDir"].str().size()) romDir = juce::File (juce::String (st["romDir"].str()));
        if (st["samplesDir"].isStr() && st["samplesDir"].str().size()) samplesDir = juce::File (juce::String (st["samplesDir"].str()));
        for (auto& kv : st["ls"].o) if (kv.second.isStr()) ls[juce::String (kv.first)] = juce::String (kv.second.str());
        for (auto& kv : st["params"].o) params[kv.first] = kv.second;
    }
    {
        // the cached messages are stored verbatim: walk the array and keep each one's text with its parse
        std::lock_guard<std::mutex> g (cacheMutex);
        const auto at = text.find ("\"cache\":[");
        if (at != std::string::npos)
        {
            size_t i = at + 9;
            while (i < text.size() && text[i] != ']')
            {
                if (text[i] == ',' || text[i] == ' ') { i++; continue; }
                const size_t start = i; int depth = 0; bool inStr = false;
                for (; i < text.size(); i++)
                {
                    const char ch = text[i];
                    if (inStr) { if (ch == '\\') i++; else if (ch == '"') inStr = false; continue; }
                    if (ch == '"') inStr = true;
                    else if (ch == '{' || ch == '[') depth++;
                    else if (ch == '}' || ch == ']') { depth--; if (depth == 0) { i++; break; } }
                }
                std::string raw = text.substr (start, i - start);
                auto v = std::make_shared<Val> (Val::parse (raw));
                auto key = keyOf (*v);
                if (! key.empty()) cache.push_back ({ key, std::move (raw), v });
            }
        }
    }
    if (sr > 0)
    {
        suspendProcessing (true);
        rebuildEngine();
        suspendProcessing (false);
    }
    ++stateGeneration;
}

juce::AudioProcessorEditor* FarpProcessor::createEditor() { return new FarpEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FarpProcessor(); }
