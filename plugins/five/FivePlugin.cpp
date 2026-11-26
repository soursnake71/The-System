// RX5 plugin: the processor (see FivePlugin.h).
#include "FivePlugin.h"
#include "FiveEditor.h"
#include <BinaryData.h>
#include <cctype>
#include <set>

namespace five {

/* ================================================================================================ helpers */
static juce::String chName(int ch) { return juce::String(CHANNEL_NAMES[ch]); }

// mono float data from any file JUCE can read (WAV, AIFF, FLAC, Ogg, MP3), channels averaged
static bool readAudio(std::unique_ptr<juce::InputStream> in, std::vector<float>& out, double& rate, juce::String& err) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(std::move(in)));
    if (!r) { err = "not an audio file"; return false; }
    const int n = (int) juce::jmin<juce::int64>(r->lengthInSamples, 25000 * 600), chs = (int) juce::jmax(1u, r->numChannels);
    if (n < 2) { err = "empty audio file"; return false; }
    juce::AudioBuffer<float> b(chs, n); r->read(&b, 0, n, 0, true, true);
    out.assign((size_t) n, 0.f);
    for (int c = 0; c < chs; c++) { const float* d = b.getReadPointer(c); for (int i = 0; i < n; i++) out[(size_t) i] += d[i] / (float) chs; }
    rate = r->sampleRate > 0 ? r->sampleRate : 44100;
    return true;
}
static bool readAudioFile(const juce::File& f, std::vector<float>& out, double& rate, juce::String& err) {
    auto in = f.createInputStream(); if (!in) { err = "can't open " + f.getFileName(); return false; }
    return readAudio(std::move(in), out, rate, err);
}
// kits.js trimSilence: leading / trailing samples under thr, keeping 0.5 ms before the first sound
static std::vector<float> trimSilence(const std::vector<float>& x, double rate, double thr = .0005, double preMs = .5) {
    size_t a = 0, b = x.size();
    while (a < b && std::fabs(x[a]) < thr) a++;
    while (b > a && std::fabs(x[b - 1]) < thr) b--;
    const size_t pre = (size_t) jsround(preMs * rate / 1000); a = a > pre ? a - pre : 0;
    if (a == 0 && b == x.size()) return x;
    return std::vector<float>(x.begin() + (std::ptrdiff_t) a, x.begin() + (std::ptrdiff_t) std::max(a + 2, std::min(b, x.size())));
}

// kits.js name matching: normName, the RX5 internal / WRC-01 names, generic drum aliases
static std::string normName(const juce::String& file) {
    juce::String s = file.fromLastOccurrenceOf("/", false, false).fromLastOccurrenceOf("\\", false, false);
    if (s.containsChar('.')) { const auto ext = s.fromLastOccurrenceOf(".", false, false); if (ext.length() >= 2 && ext.length() <= 5) s = s.upToLastOccurrenceOf(".", false, false); }
    std::string t = s.toLowerCase().toStdString(), o;
    // remove [\s_\-.]*rx[\s_\-]*5 (not followed by a digit)
    for (size_t i = 0; i < t.size();) {
        size_t j = i; while (j < t.size() && (t[j] == ' ' || t[j] == '_' || t[j] == '-' || t[j] == '.')) j++;
        if (j + 1 < t.size() && t[j] == 'r' && t[j + 1] == 'x') {
            size_t k = j + 2; while (k < t.size() && (t[k] == ' ' || t[k] == '_' || t[k] == '-')) k++;
            if (k < t.size() && t[k] == '5' && !(k + 1 < t.size() && std::isdigit((unsigned char) t[k + 1]))) { i = k + 1; continue; }
        }
        o += t[i]; i++;
    }
    std::string r; for (char c : o) if (std::isalnum((unsigned char) c)) r += c;
    return r;
}
static const std::vector<std::pair<int, std::vector<const char*>>>& intKeys() {
    static const std::vector<std::pair<int, std::vector<const char*>>> k = {
        {0, {"bdrum1", "bd1", "bassdrum1"}}, {12, {"bdrum2", "bd2", "bassdrum2"}}, {1, {"sdrum1", "sd1", "snaredrum1", "snare1"}}, {13, {"sdrum2", "sd2", "snaredrum2", "snare2"}},
        {2, {"rim1", "rimshot1"}}, {14, {"rim2", "rimshot2"}}, {3, {"tom1", "tom1a"}}, {4, {"tom2", "tom2a"}}, {5, {"tom3", "tom3a"}}, {6, {"tom4", "tom4a"}},
        {15, {"etom1", "tom1b"}}, {16, {"etom2", "tom2b"}}, {17, {"etom3", "tom3b"}}, {18, {"etom4", "tom4b"}},
        {7, {"chat", "hhclos", "hhclosed", "hhcls", "closedhat", "closedhihat"}}, {19, {"ohat", "hhopen", "hhopn", "openhat", "openhihat"}},
        {8, {"rideedge", "edge", "ride"}}, {20, {"ridebell", "ridecup", "cup", "ridecp"}}, {9, {"crash"}}, {21, {"china", "chinese", "chinesecymbal"}},
        {22, {"clap", "claps", "handclap"}}, {10, {"tamb", "tambrn", "tambourine"}}, {11, {"shaker", "shakr"}}, {23, {"cowbell", "cowbel"}} };
    return k;
}
static const char* const CART_NAMES[28] = { "SD3", "BD3", "CgaHMT", "CgaHOP", "CgaLO", "BgoHI", "BgoLO", "TimblH", "TimblL", "AgoHI", "AgoLO", "Cuica", "Cstnt", "Whstl",
    "Timpn", "GlsCsh", "Gun", "FMprc1", "FMprc2", "FMprc3", "EbassH", "EbassL", "DXorch", "DXmrmb", "DXclav", "Hey", "Wao", "Ooo" };
static const char* const INT_NAMES[NV] = { "BD 1", "SD 1", "RIM 1", "Tom 1", "Tom 2", "Tom 3", "Tom 4", "HHclos", "Edge", "Crash", "Tambrn", "Shaker",
    "BD 2", "SD 2", "RIM 2", "ETom 1", "ETom 2", "ETom 3", "ETom 4", "HHopen", "Cup", "China", "Claps", "Cowbel" };
// -> voice 0..23, or 100 + cartridge index, or -1
static int rx5Name(const std::string& n) {
    for (const auto& e : intKeys()) for (const char* k : e.second) if (n == k) return e.first;
    static const std::map<std::string, int> extra = { {"sdrum3", 0}, {"snaredrum3", 0}, {"bdrum3", 1}, {"bassdrum3", 1}, {"castanet", 12}, {"castanets", 12},
        {"whistle", 13}, {"timpani", 14}, {"dxmrrmb", 23}, {"dxmarimba", 23}, {"glasscrash", 15} };
    for (int i = 0; i < 28; i++) if (n == juce::String(CART_NAMES[i]).toLowerCase().toStdString()) return 100 + i;
    const auto it = extra.find(n); return it != extra.end() ? 100 + it->second : -1;
}
static std::vector<int> aliasVoices(const juce::String& file) {
    const auto stem = juce::String(normName(file)), j = stem;
    auto has = [&](const char* s) { return j.contains(s); };
    int num = 0; for (auto c : j) if (c >= '1' && c <= '4') { num = c - '0'; break; }
    if (has("hihat") || has("hat") || j.startsWith("hh") || j == "ch" || j == "oh") return has("open") || j.startsWith("oh") || j.contains("hho") ? std::vector<int> { 19, 7 } : std::vector<int> { 7, 19 };
    if (has("ride")) return has("bell") || has("cup") ? std::vector<int> { 20, 8 } : std::vector<int> { 8, 20 };
    if (has("china")) return { 21, 9 };
    if (has("crash") || has("splash") || has("cym")) return { 9, 21 };
    if (has("clap")) return { 22 };
    if (has("cow")) return { 23 };
    if (has("tamb")) return { 10 };
    if (has("shak") || has("maraca") || has("cabasa")) return { 11 };
    if (has("rim") || has("stick")) return num == 2 ? std::vector<int> { 14, 2 } : std::vector<int> { 2, 14 };
    if (has("tom") || has("floor")) {
        const bool B = j.startsWith("etom") || has("elec") || has("syn") || has("simmons");
        const int i = num >= 1 && num <= 4 ? num - 1 : has("hi") ? 0 : has("mid") ? 1 : has("lo") ? 2 : has("floor") ? 3 : -1;
        const int A4[4] = { 3, 4, 5, 6 }, B4[4] = { 15, 16, 17, 18 }; const int* P = B ? B4 : A4; const int* Q = B ? A4 : B4;
        if (i >= 0) return { P[i], Q[i] };
        return { P[0], P[1], P[2], P[3], Q[0], Q[1], Q[2], Q[3] };
    }
    if (has("kick") || has("kik") || has("bassdrum") || j.startsWith("bd")) return num == 2 ? std::vector<int> { 12, 0 } : std::vector<int> { 0, 12 };
    if (has("snare") || has("snr") || j.startsWith("sd")) return num == 2 ? std::vector<int> { 13, 1 } : std::vector<int> { 1, 13 };
    return {};
}
juce::String FiveProcessor::chooseKitName(const juce::String& file) {
    juce::String s = file.fromLastOccurrenceOf("/", false, false).upToLastOccurrenceOf(".", false, false);
    return s.replace("-RX5", "").replace("_RX5", "").substring(0, 6);
}

/* ================================================================================================ parameters */
static juce::String pid(const char* base, int i) { return juce::String(base) + juce::String(i + 1); }
void FiveProcessor::createParams(juce::AudioProcessorValueTreeState::ParameterLayout& L) {
    using F = juce::AudioParameterFloat; using B = juce::AudioParameterBool; using R = juce::NormalisableRange<float>;
    L.add(std::make_unique<F>(juce::ParameterID { "volume", 1 }, "Volume", R(0, 1), 1.f));
    L.add(std::make_unique<F>(juce::ParameterID { "click", 1 }, "Click level", R(0, 1), .5f));
    L.add(std::make_unique<F>(juce::ParameterID { "tempo", 1 }, "Tempo", R(40, 250, .1f), 120.f));
    L.add(std::make_unique<B>(juce::ParameterID { "hostSync", 1 }, "Host sync", true));
    for (int c = 0; c < NCH; c++) {
        const auto n = chName(c);
        L.add(std::make_unique<F>(juce::ParameterID { pid("fader", c), 1 }, n + " fader", R(0, 1), 1.f));
        L.add(std::make_unique<F>(juce::ParameterID { pid("pan", c), 1 }, n + " pan", R(-1, 1), (float) channelDefault(c).pan));
        L.add(std::make_unique<F>(juce::ParameterID { pid("rev", c), 1 }, n + " reverb send", R(0, 1), 0.f));
        L.add(std::make_unique<F>(juce::ParameterID { pid("dly", c), 1 }, n + " delay send", R(0, 1), 0.f));
        L.add(std::make_unique<B>(juce::ParameterID { pid("mute", c), 1 }, n + " mute", false));
    }
    for (int v = 0; v < NV; v++) {
        const juce::String n(VOICE_NAMES[v]);
        L.add(std::make_unique<F>(juce::ParameterID { pid("pitch", v), 1 }, n + " pitch", R(-3600, 2400, 10), 0.f));
        L.add(std::make_unique<F>(juce::ParameterID { pid("level", v), 1 }, n + " level", R(0, 31, 1), 25.f));
    }
    L.add(std::make_unique<F>(juce::ParameterID { "revProgram", 1 }, "Reverb program", R(0, 10, 1), 0.f));
    L.add(std::make_unique<F>(juce::ParameterID { "revTime", 1 }, "Reverb time", R(.3f, 30, 0, .4f), 2.6f));
    L.add(std::make_unique<F>(juce::ParameterID { "revLevel", 1 }, "Reverb level", R(0, 1), 1.f));
    L.add(std::make_unique<B>(juce::ParameterID { "dlyOn", 1 }, "Delay on", true));
    L.add(std::make_unique<F>(juce::ParameterID { "dlyFeedback", 1 }, "Delay feedback", R(0, 120), 35.f));
    L.add(std::make_unique<F>(juce::ParameterID { "dlyMix", 1 }, "Delay mix", R(0, 1), .6f));
}
static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(FiveProcessor* p, void (FiveProcessor::*f)(juce::AudioProcessorValueTreeState::ParameterLayout&)) {
    juce::AudioProcessorValueTreeState::ParameterLayout L; (p->*f)(L); return L;
}

FiveProcessor::FiveProcessor()
    : AudioProcessor(buses()), apvts(*this, nullptr, "PARAMS", makeLayout(this, &FiveProcessor::createParams)) {
    st = std::make_unique<State>();
    player.setState(st.get());
    for (auto& c : chLevel) c = 0;
    auto add = [&](int target, int index, int key, const juce::String& id) {
        Auto a; a.target = target; a.index = index; a.key = key; a.p = apvts.getParameter(id); a.raw = apvts.getRawParameterValue(id); jassert(a.p && a.raw); a.last = a.raw->load(); autos.push_back(a);
    };
    add(4, 0, 0, "volume"); add(4, 1, 0, "click"); add(4, 2, 0, "tempo");
    for (int c = 0; c < NCH; c++) { add(1, c, cFader, pid("fader", c)); add(1, c, cPan, pid("pan", c)); add(1, c, cSendRev, pid("rev", c)); add(1, c, cSendDly, pid("dly", c)); add(1, c, cMute, pid("mute", c)); }
    for (int v = 0; v < NV; v++) { add(0, v, vPitch, pid("pitch", v)); add(0, v, vLevel, pid("level", v)); }
    add(2, 0, rProgram, "revProgram"); add(2, 0, rTime, "revTime"); add(2, 0, rLevel, "revLevel");
    add(3, 0, dOn, "dlyOn"); add(3, 0, dFeedback, "dlyFeedback"); add(3, 0, dMix, "dlyMix");
    syncRaw = apvts.getRawParameterValue("hostSync");
    loadFactory();
    buildEngine(48000);
    startTimerHz(30);
}
FiveProcessor::~FiveProcessor() { stopTimer(); }

/* ================================================================================================ factory kit */
void FiveProcessor::loadFactory() {
    int size = 0; const char* json = FiveKitData::getNamedResource("kit_json", size);
    juce::var meta; if (json) juce::JSON::parse(juce::String::fromUTF8(json, size), meta);
    std::map<juce::String, const char*> byFile; std::map<juce::String, int> sizes;
    for (int i = 0; i < FiveKitData::namedResourceListSize; i++) {
        int n = 0; const char* d = FiveKitData::getNamedResource(FiveKitData::namedResourceList[i], n);
        const juce::String f(FiveKitData::getNamedResourceOriginalFilename(FiveKitData::namedResourceList[i]));
        byFile[f] = d; sizes[f] = n;
    }
    if (auto* lib = meta["library"].getArray()) for (const auto& e : *lib) {
        const juce::String file = e["file"].toString(); if (!byFile.count(file)) continue;
        std::vector<float> data; double rate = 25000; juce::String err;
        if (!readAudio(std::make_unique<juce::MemoryInputStream>(byFile[file], (size_t) sizes[file], false), data, rate, err)) continue;
        const int start = (int) e["start"], len = (int) e["length"];
        Wave w; w.id = (int) e["id"]; w.name = e["name"].toString().toStdString(); w.kind = e["kind"].toString().toStdString(); w.file = file.toStdString(); w.sr = rate;
        if (start >= 0 && len > 1 && start + len <= (int) data.size()) w.data.assign(data.begin() + start, data.begin() + start + len); else w.data = std::move(data);
        factoryWaves.push_back(std::move(w));
    }
    std::sort(factoryWaves.begin(), factoryWaves.end(), [](const Wave& a, const Wave& b) { return a.id < b.id; });
}

std::vector<FiveProcessor::WaveRef> FiveProcessor::waveLib() const {
    std::vector<WaveRef> out; std::set<int> ids;
    for (const auto& w : st->library) { out.push_back({ w.id, juce::String(w.name), true }); ids.insert(w.id); }
    for (const auto& w : factoryWaves) if (!ids.count(w.id)) out.push_back({ w.id, juce::String(w.name), false });
    return out;
}
const Wave* FiveProcessor::findWave(int id) const {
    if (id < 0) return nullptr;
    for (const auto& w : factoryWaves) if (w.id == id) { bool shadowed = false; for (const auto& l : st->library) if (l.id == id) shadowed = true; if (!shadowed) return &w; }
    return nullptr;
}
juce::String FiveProcessor::waveName(int v) const {
    const auto& s = st->samples[(size_t) v];
    if (!s.data.empty()) return ("USR " + juce::String(s.name).substring(0, 6));
    const int id = st->wave[(size_t) v];
    for (const auto& l : st->library) if (l.id == id && id >= 0) return juce::String(l.name).substring(0, 10);
    if (auto* w = findWave(id)) return juce::String(w->name).substring(0, 10);
    return ("INT " + juce::String(VOICE_NAMES[v])).substring(0, 10);
}
void FiveProcessor::setWave(int v, int id) {
    { const Edit l(lock); st->wave[(size_t) v] = id; }
    applyVoiceSample(v);
}

/* ================================================================================================ samples */
void FiveProcessor::applyVoiceSample(int v) {
    if (v < 0 || v >= NV || !engine) return;
    const float* d = nullptr; size_t n = 0; double rate = 25000; bool rec = false; int bits = 12; bool fl = false;
    {
        const Edit l(lock);
        const auto& s = st->samples[(size_t) v]; bits = (int) st->voice[(size_t) v][vBits]; fl = engine->dacFloat;
        if (!s.data.empty()) { d = s.data.data(); n = s.data.size(); rate = s.sr; rec = false; }
        else {
            const int id = st->wave[(size_t) v];
            const LibWave* lw = nullptr; for (const auto& w : st->library) if (w.id == id && id >= 0) lw = &w;
            if (lw) { d = lw->data.data(); n = lw->data.size(); rate = lw->sr; rec = lw->kind != "user"; }
            else if (auto* w = findWave(id >= 0 ? id : v)) { d = w->data.data(); n = w->data.size(); rate = w->sr; rec = w->kind != "user"; }
        }
    }
    // only the message thread replaces samples / library, so the data stays valid while we resample it
    auto p = Engine::prepare(d, n, rate, rec, bits, fl);
    { const Edit l(lock); engine->install(v, p); st->voice[(size_t) v].rec = rec; }
    trashSamples.push_back(std::move(p));
}
void FiveProcessor::applyAllSamples() { for (int v = 0; v < NV; v++) applyVoiceSample(v); trashSamples.clear(); }

/* ================================================================================================ engine */
static void loadRevProgram(State& s, int prog) {
    const int i = (int) clampd(prog, 0, NUM_REV_PROGRAMS - 1); s.rev[rProgram] = i;
    for (int k = rTime; k <= rGateTime; k++) s.rev[(size_t) k] = programValue(REV_PROGRAMS[i], k);
}
void FiveProcessor::buildEngine(double rate) {
    auto e = std::make_unique<Engine>(rate);
    { const Edit l(lock); engine.swap(e); sr = rate; applied.valid = false; }
    e.reset();
    applyAllSamples();
}
void FiveProcessor::prepareToPlay(double rate, int block) {
    maxBlock = block;
    if (!engine || std::fabs(rate - sr) > .5) buildEngine(rate);
    for (int c = 0; c < NCH; c++) engine->direct[c] = 0;
}
bool FiveProcessor::isBusesLayoutSupported(const BusesLayout& L) const {
    if (L.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < L.outputBuses.size(); i++) { const auto& s = L.outputBuses.getReference(i); if (!s.isDisabled() && s != juce::AudioChannelSet::mono()) return false; }
    for (const auto& s : L.inputBuses) if (!s.isDisabled()) return false;
    return true;
}

FiveProcessor::Auto* FiveProcessor::findAuto(int target, int index, int key) {
    for (auto& a : autos) if (a.target == target && a.index == index && a.key == key) return &a;
    return nullptr;
}
void FiveProcessor::setAuto(int target, int index, int key, double x) {
    if (auto* a = findAuto(target, index, key)) a->p->setValueNotifyingHost(a->p->convertTo0to1((float) x));
}
void FiveProcessor::pushParamsFromState() {
    for (auto& a : autos) {
        double x = 0; const State& s = *st;
        switch (a.target) {
            case 0: x = s.voice[(size_t) a.index][a.key]; break;
            case 1: { const auto& c = s.channel[(size_t) a.index]; x = a.key == cFader ? c.fader : a.key == cPan ? c.pan : a.key == cSendRev ? c.sendRev : a.key == cSendDly ? c.sendDly : (c.mute ? 1 : 0); break; }
            case 2: x = s.rev[(size_t) a.key]; break;
            case 3: x = s.dly[(size_t) a.key]; break;
            default: x = a.index == 0 ? s.volume : a.index == 1 ? s.clickLevel : s.tempo; break;
        }
        a.p->setValueNotifyingHost(a.p->convertTo0to1((float) x));
    }
}

// audio thread, under lock: automation -> state, then state -> engine (only what changed)
void FiveProcessor::syncEngine() {
    State& s = *st; Engine& e = *engine;
    for (auto& a : autos) {
        const float x = a.raw->load(); if (x == a.last) continue;
        a.last = x;
        switch (a.target) {
            case 0: s.voice[(size_t) a.index][a.key] = clampd(x, VOICE_RANGES[a.key][0], VOICE_RANGES[a.key][1]); break;
            case 1: { auto& c = s.channel[(size_t) a.index]; if (a.key == cFader) c.fader = x; else if (a.key == cPan) c.pan = x; else if (a.key == cSendRev) c.sendRev = x; else if (a.key == cSendDly) c.sendDly = x; else c.mute = x >= .5f; break; }
            case 2: if (a.key == rProgram) loadRevProgram(s, (int) jsround(x)); else s.rev[(size_t) a.key] = clampd(x, REV_RANGES[a.key][0], REV_RANGES[a.key][1]); break;
            case 3: s.dly[(size_t) a.key] = clampd(x, DLY_RANGES[a.key][0], DLY_RANGES[a.key][1]); break;
            default: if (a.index == 0) s.volume = x; else if (a.index == 1) s.clickLevel = x; else s.tempo = clampd(x, 40, 250); break;
        }
    }
    const bool all = !applied.valid; applied.valid = true;
    for (int v = 0; v < NV; v++) {
        auto& A = applied.voice[(size_t) v]; const auto& S = s.voice[(size_t) v];
        for (int k = 0; k < vCount; k++) if (all || A[k] != S[k]) { e.setVoiceParam(v, k, S[k]); A[k] = S[k]; }
        if (S.rec != e.voices[v].rec) e.setPath(v, S.rec);
    }
    for (int c = 0; c < NCH; c++) {
        auto& A = applied.channel[(size_t) c]; const auto& S = s.channel[(size_t) c];
        if (all || A.fader != S.fader || A.pan != S.pan || A.sendRev != S.sendRev || A.sendDly != S.sendDly || A.mute != S.mute) {
            e.channels[c] = S; e.mixCh(c); A = S;
        }
    }
    if (all || applied.rev[rProgram] != s.rev[rProgram]) {
        e.revFx.setProgram(s.rev[rProgram]);
        for (int k = rTime; k < rCount; k++) e.revFx.set(k, s.rev[(size_t) k]);
        applied.rev = s.rev;
    } else for (int k = rTime; k < rCount; k++) if (applied.rev[(size_t) k] != s.rev[(size_t) k]) { e.revFx.set(k, s.rev[(size_t) k]); applied.rev[(size_t) k] = s.rev[(size_t) k]; }
    for (int k = 0; k < dCount; k++) if (all || applied.dly[(size_t) k] != s.dly[(size_t) k]) { if (k != dTempo) e.dlyFx.set(k, s.dly[(size_t) k]); applied.dly[(size_t) k] = s.dly[(size_t) k]; }
    if (all || applied.volume != s.volume) { e.setVolume(s.volume); applied.volume = s.volume; }
    if (all || applied.click != s.clickLevel) { e.setClick(s.clickLevel); applied.click = s.clickLevel; }
    const double bpm = player.bpm();
    if (all || applied.tempo != bpm) { e.setTempo(bpm); applied.tempo = bpm; }
    for (int v = 0; v < NV; v++) outCh[(size_t) v] = outChannel(v);
    player.clockTicks = s.midi.clockOut;
    player.setExternal(s.sync == "midi" || s.midi.clockIn);
}
int FiveProcessor::outChannel(int v) const {
    const auto& su = st->setups[(size_t) clampInt(st->setup, 0, NUM_SETUPS - 1, 0)];
    for (const auto& k : su.keys) if (k.voice == v) return k.out >= 0 && k.out < NCH ? k.out : v % NCH;
    return v % NCH;
}

/* ================================================================================================ audio */
void FiveProcessor::addOut(const juce::MidiMessage& m, int offset) { if (out) out->addEvent(m, juce::jlimit(0, juce::jmax(0, blockN - 1), offset)); }
void FiveProcessor::emit(const PlayEvent& ev, int) {
    const State& s = *st;
    if (ev.v == -2) { addOut(juce::MidiMessage::midiClock(), ev.offset); return; }
    if (ev.v == -1) { engine->clickTick(ev.click == 2, ev.offset); return; }
    engine->trigger(ev.v, ev.vel, ev.acc, ev.offset, 0, outCh[(size_t) ev.v]);
    firedMask.fetch_or(1 << ev.v);
    if (s.midi.notesOut && s.midi.channelMsg) {
        const int ch = txChannel(s, ev.v), note = s.noteMap[(size_t) ev.v];
        addOut(juce::MidiMessage::noteOn(ch, note, (juce::uint8) juce::jlimit(1, 127, accentVel(ev.vel, ev.acc))), ev.offset);
        if (nOffs < (int) offs.size()) offs[(size_t) nOffs++] = { ch, note, ev.offset + clampd(s.midi.gateMs, 10, 1000) * sr / 1000 };
    }
}

void FiveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    auto mainBus = getBusBuffer(buffer, false, 0);
    float* L = mainBus.getWritePointer(0); float* R = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer(1) : nullptr;
    std::array<float*, NCH> aux {}; bool anyAux = false;
    for (int c = 0; c < NCH && c + 1 < getBusCount(false); c++) {
        auto* bus = getBus(false, c + 1);
        if (bus && bus->isEnabled()) { auto b = getBusBuffer(buffer, false, c + 1); if (b.getNumChannels() > 0) { aux[(size_t) c] = b.getWritePointer(0); anyAux = true; } }
    }
    juce::MidiBuffer outBuf; out = &outBuf; blockN = n;
    {
        const Edit l(lock);
        if (!engine || !R) { buffer.clear(); midi.clear(); out = nullptr; return; }
        State& s = *st; Engine& e = *engine;
        for (int c = 0; c < NCH; c++) e.direct[c] = aux[(size_t) c] ? 1 : 0;
        syncEngine();
        auto em = [&](const PlayEvent& ev) { emit(ev, n); };
        // transport requests from the panel
        if (const int t = transportReq.exchange(0)) {
            if (t == 1 || (t == 3 && (player.mode != (Player::Mode) reqMode.load() || player.n != reqN.load()))) {
                player.setMode((Player::Mode) reqMode.load(), reqN.load()); player.start();
                if (s.midi.clockOut) addOut(juce::MidiMessage::midiStart(), 0);
            } else if (t == 2) { player.stop(); if (s.midi.clockOut) addOut(juce::MidiMessage::midiStop(), 0); }
            else { player.cont(); if (s.midi.clockOut) addOut(juce::MidiMessage::midiContinue(), 0); }
        }
        if (dampReq.exchange(false)) for (int v = 0; v < NV; v++) e.cut(v);
        if (const int c = reqCue.exchange(-1); c >= 0 && player.mode == Player::PatternMode) { if (player.running) player.cue = c; else { player.n = c; player.pat = c; } }
        // pads from the panel
        for (int r = hitR.load(), w = hitW.load(); r != w; r = (r + 1) % (int) hitQ.size()) {
            const Hit& h = hitQ[(size_t) r];
            if (h.rel) e.release(h.v);
            else {
                e.trigger(h.v, h.vel, h.acc, 0, h.pitch, h.ch >= 0 ? h.ch : outCh[(size_t) h.v]); firedMask.fetch_or(1 << h.v);
                if (ui.rtw && player.running && player.mode == Player::PatternMode) player.record(h.v, h.vel, h.acc);
                if (s.midi.notesOut && s.midi.channelMsg) {
                    const int ch = txChannel(s, h.v), note = s.noteMap[(size_t) h.v];
                    addOut(juce::MidiMessage::noteOn(ch, note, (juce::uint8) juce::jlimit(1, 127, accentVel(h.vel, h.acc))), 0);
                    if (nOffs < (int) offs.size()) offs[(size_t) nOffs++] = { ch, note, clampd(s.midi.gateMs, 10, 1000) * sr / 1000 };
                }
            }
            hitR.store((r + 1) % (int) hitQ.size());
        }
        // MIDI in
        const bool synced = s.sync == "midi" || s.midi.clockIn;
        for (const auto meta : midi) {
            const auto m = meta.getMessage(); const int off = juce::jlimit(0, n - 1, meta.samplePosition);
            if (m.isSysEx()) {
                Bytes b; b.reserve((size_t) m.getSysExDataSize() + 2); b.push_back(0xF0);
                b.insert(b.end(), m.getSysExData(), m.getSysExData() + m.getSysExDataSize()); b.push_back(0xF7);
                const juce::SpinLock::ScopedLockType il(inLock); if (sysexIn.size() < 256) sysexIn.push_back(std::move(b));
            } else if (m.isNoteOn() || m.isNoteOff()) {
                if (!s.midi.channelMsg || m.getChannel() != s.midi.rxCh) continue;
                int vs[NV]; double ps[NV]; const int k = noteVoices(s, m.getNoteNumber(), vs, ps);
                for (int i = 0; i < k; i++) {
                    if (m.isNoteOn()) {
                        const int vel = m.getVelocity();
                        e.trigger(vs[i], vel, 0, off, ps[i], outCh[(size_t) vs[i]]); firedMask.fetch_or(1 << vs[i]);
                        if (ui.rtw && player.running && player.mode == Player::PatternMode) player.record(vs[i], vel, 0, player.rawPosAt(off));
                    } else if (s.voice[(size_t) vs[i]][vLoop] >= .5) e.release(vs[i], off);
                }
            } else if (synced && m.isMidiClock()) player.clock24(off, em);
            else if (synced && m.isMidiStart()) { player.setMode((Player::Mode) reqMode.load(), reqN.load()); player.start(); }
            else if (synced && m.isMidiContinue()) player.cont();
            else if (synced && m.isMidiStop()) player.stop();
            else if (synced && m.isSongPositionPointer()) player.spp(m.getSongPositionPointerMidiBeat());
            else if (m.getRawDataSize() >= 2 && m.getRawData()[0] == 0xF3 && s.midi.songSelect) { ui.song = juce::jlimit(0, NUM_SONGS - 1, (int) m.getRawData()[1]); needUi = true; }
            else if (m.isProgramChange() && s.midi.tunable) s.midi.tunableVoice = m.getProgramChangeNumber() % NV;
        }
        midi.clear();
        // the sequencer: the host transport (HOST SYNC, while the host plays) or free running
        bool hostPlaying = false; double ppq = 0, hostBpm = 120;
        if (!synced && syncRaw && syncRaw->load() >= .5f)
            if (auto* ph = getPlayHead()) if (const auto pos = ph->getPosition()) {
                hostPlaying = pos->getIsPlaying(); ppq = pos->getPpqPosition().orFallback(0.0); hostBpm = pos->getBpm().orFallback(120.0);
            }
        if (hostPlaying) {
            if (!wasHostPlaying) { player.setMode((Player::Mode) reqMode.load(), reqN.load()); player.start(); }
            player.hostBlock(ppq, hostBpm, n, sr, em);
        } else {
            if (wasHostPlaying) { player.hostStopped(); player.setTempo(0); }
            player.advance(n, sr, 0, em);
        }
        wasHostPlaying = hostPlaying;
        if (wasRunning && !player.running && player.ended && s.midi.clockOut) addOut(juce::MidiMessage::midiStop(), n - 1);
        wasRunning = player.running; isRunning = player.running;
        // the voices
        e.process(L, R, n, anyAux ? aux.data() : nullptr);
        for (int c = 0; c < NCH; c++) chLevel[c] = (float) e.voiceActive(c);
        meterL = juce::jmax(meterL.load(), (float) e.mL); meterR = juce::jmax(meterR.load(), (float) e.mR); e.mL = e.mR = 0;
    }
    // note offs, then whatever the message thread queued
    for (int i = 0; i < nOffs;) {
        Off& o = offs[(size_t) i];
        if (o.left < n) { addOut(juce::MidiMessage::noteOff(o.ch, o.note, (juce::uint8) 64), (int) o.left); offs[(size_t) i] = offs[(size_t) --nOffs]; }
        else { o.left -= n; i++; }
    }
    { const juce::SpinLock::ScopedTryLockType ol(outLock); if (ol.isLocked() && !outQueue.isEmpty()) { for (const auto meta : outQueue) outBuf.addEvent(meta.getMessage(), 0); outQueue.clear(); } }
    midi.swapWith(outBuf); out = nullptr;
}
void FiveProcessor::queueOut(const juce::MidiMessage& m) { const juce::SpinLock::ScopedLockType l(outLock); outQueue.addEvent(m, 0); }

/* ================================================================================================ message thread: SysEx in, flashes */
void FiveProcessor::timerCallback() {
    std::vector<Bytes> in;
    { const juce::SpinLock::ScopedLockType l(inLock); in.swap(sysexIn); }
    for (const auto& m : in) {
        auto copy = copyState();
        const auto r = handleSysex(*copy, m, receiver, juce::Time::getMillisecondCounterHiRes());
        for (const auto& rep : r.reply) if (rep.size() >= 2) queueOut(juce::MidiMessage::createSysExMessage(rep.data() + 1, (int) rep.size() - 2));
        if (r.changed) replaceState(std::move(copy));
        const juce::String msg = juce::String(r.message.empty() ? r.kind : r.message);
        setMessage(msg);
        if (onFlash && r.kind != "ignored" && r.kind != "other") onFlash("SYSEX " + juce::String(r.kind).toUpperCase().substring(0, 10), msg.substring(0, 16));
    }
    trash.clear(); trashSamples.clear();
}

/* ================================================================================================ transport, pads */
void FiveProcessor::start() {
    if (ui.mode == 1) { reqMode = ui.chainPlay ? Player::ChainMode : Player::SongMode; reqN = ui.chainPlay ? ui.chain : ui.song; }
    else { reqMode = Player::PatternMode; reqN = ui.ptn; }
    if (ui.rtw) setRtw(true);
    transportReq = 1;
}
void FiveProcessor::stop() { transportReq = 2; }
void FiveProcessor::selectPattern(int n) { ui.ptn = juce::jlimit(0, NUM_PATTERNS - 1, n); reqCue = ui.ptn; }
void FiveProcessor::cont() {
    if (ui.mode == 1) { reqMode = ui.chainPlay ? Player::ChainMode : Player::SongMode; reqN = ui.chainPlay ? ui.chain : ui.song; }
    else { reqMode = Player::PatternMode; reqN = ui.ptn; }
    transportReq = 3;
}
void FiveProcessor::playVoice(int v, int vel, int acc, double pitchOff, int ch) {
    const int w = hitW.load(), nx = (w + 1) % (int) hitQ.size(); if (nx == hitR.load()) return;
    hitQ[(size_t) w] = { v, juce::jlimit(1, 127, vel), acc, ch, 0, pitchOff }; hitW.store(nx);
}
void FiveProcessor::releaseVoice(int v) {
    const int w = hitW.load(), nx = (w + 1) % (int) hitQ.size(); if (nx == hitR.load()) return;
    hitQ[(size_t) w] = { v, 0, 0, -1, 1, 0 }; hitW.store(nx);
}
void FiveProcessor::hitPad(int pad, int vel, int acc) {
    SetupKey k; { const Edit l(lock); k = st->setups[(size_t) clampInt(st->setup, 0, NUM_SETUPS - 1, 0)].keys[(size_t) juce::jlimit(0, NV - 1, pad)]; }
    playVoice(k.voice, vel, acc, k.pitch, k.out);
}
void FiveProcessor::damp() { dampReq = true; }
void FiveProcessor::setRtw(bool on) {
    ui.rtw = on;
    if (on) { const Edit l(lock); for (auto& p : st->patterns) p.events.reserve(p.events.size() + 2048); }
}

/* ================================================================================================ parameters (message thread) */
void FiveProcessor::setVoiceParam(int v, int k, double x) {
    if (v < 0 || v >= NV || k < 0 || k >= vCount) return;
    x = clampd(x, VOICE_RANGES[k][0], VOICE_RANGES[k][1]); if (k == vGate && x > 0) x = std::max(100.0, x); if (k == vBits) x = x < 10 ? 8 : 12;
    { const Edit l(lock); st->voice[(size_t) v][k] = x; }
    setAuto(0, v, k, x);
}
void FiveProcessor::setChannelParam(int ch, int k, double x) {
    if (ch < 0 || ch >= NCH || k < 0 || k >= cCount) return;
    x = clampd(x, CHANNEL_RANGES[k][0], CHANNEL_RANGES[k][1]);
    { const Edit l(lock); auto& c = st->channel[(size_t) ch]; if (k == cFader) c.fader = x; else if (k == cPan) c.pan = x; else if (k == cSendRev) c.sendRev = x; else if (k == cSendDly) c.sendDly = x; else c.mute = x >= .5; }
    setAuto(1, ch, k, x);
}
void FiveProcessor::setRevParam(int k, double x) {
    if (k < 0 || k >= rCount) return;
    { const Edit l(lock); if (k == rProgram) loadRevProgram(*st, (int) jsround(x)); else st->rev[(size_t) k] = clampd(x, REV_RANGES[k][0], REV_RANGES[k][1]); }
    if (k == rProgram) { setAuto(2, 0, rProgram, st->rev[rProgram]); setAuto(2, 0, rTime, st->rev[rTime]); setAuto(2, 0, rLevel, st->rev[rLevel]); }
    else setAuto(2, 0, k, st->rev[(size_t) k]);
}
void FiveProcessor::setDlyParam(int k, double x) {
    if (k < 0 || k >= dCount) return;
    { const Edit l(lock); st->dly[(size_t) k] = clampd(x, DLY_RANGES[k][0], DLY_RANGES[k][1]); if (k == dSync) st->dly[dSync] = jsround(st->dly[dSync]); }
    setAuto(3, 0, k, st->dly[(size_t) k]);
}
void FiveProcessor::setTempo(double bpm) { bpm = clampd(bpm, 40, 250); { const Edit l(lock); st->tempo = bpm; } setAuto(4, 2, 0, bpm); }
void FiveProcessor::setVolume(double x) { x = clampd(x, 0, 1); { const Edit l(lock); st->volume = x; } setAuto(4, 0, 0, x); }
void FiveProcessor::setClickLevel(double x) { x = clampd(x, 0, 1); { const Edit l(lock); st->clickLevel = x; } setAuto(4, 1, 0, x); }
bool FiveProcessor::hostSync() const { return syncRaw && syncRaw->load() >= .5f; }
void FiveProcessor::setHostSync(bool on) { if (auto* p = apvts.getParameter("hostSync")) p->setValueNotifyingHost(on ? 1.f : 0.f); }
void FiveProcessor::setSync(const std::string& s) { const Edit l(lock); st->sync = s == "midi" ? "midi" : "int"; }

/* ================================================================================================ whole memory */
std::unique_ptr<State> FiveProcessor::copyState() const { const Edit l(lock); return std::make_unique<State>(*st); }
void FiveProcessor::rebindPlayer(State* s) {
    // keep playing across the swap (like the app's worklet): same mode, the position after the last tick
    const bool run = player.running; const auto mode = player.mode; const int num = player.n, cur = player.cur, pat = player.pat;
    player.setState(s);
    if (run) { player.setMode(mode, num); if (mode == Player::PatternMode) player.pat = pat; player.locate(std::max(0, cur + 1)); player.running = true; }
}
void FiveProcessor::replaceState(std::unique_ptr<State> s) {
    if (!s) return;
    if (ui.rtw) for (auto& p : s->patterns) p.events.reserve(p.events.size() + 2048);
    { const Edit l(lock); st.swap(s); rebindPlayer(st.get()); applied.valid = false; }
    trash.push_back(std::move(s));
    pushParamsFromState();
    applyAllSamples();
    needUi = true;
}
juce::String FiveProcessor::randomizeAll(const std::string& scope) {
    auto s = copyState();
    const auto g = randomize(*s, (uint32_t) juce::Random::getSystemRandom().nextInt(), scope, ui.ptn, 0, ui.song);
    replaceState(std::move(s));
    return juce::String(g).toUpperCase();
}
void FiveProcessor::initAll() { replaceState(std::make_unique<State>()); }

juce::String FiveProcessor::loadCartFile(const juce::File& f) {
    juce::MemoryBlock mb; if (!f.loadFileAsData(mb)) return "can't read file";
    try { replaceState(std::make_unique<State>(loadCart(mb.getData(), mb.getSize()))); }
    catch (const std::exception& e) { return juce::String(e.what()).substring(0, 40); }
    return {};
}
juce::String FiveProcessor::saveCartFile(const juce::File& f) {
    auto s = copyState(); const auto mb = saveCart(*s, true);
    return f.replaceWithData(mb.getData(), mb.getSize()) ? juce::String() : juce::String("can't write file");
}
juce::String FiveProcessor::importMidiFile(const juce::File& f, int startPattern, int songSlot) {
    juce::MemoryBlock mb; if (!f.loadFileAsData(mb)) return "can't read file";
    auto s = copyState(); const auto r = importSMF(*s, (const uint8_t*) mb.getData(), mb.getSize(), 0, startPattern, songSlot);
    if (!r.ok) return juce::String(r.error);
    replaceState(std::move(s)); ui.song = r.song; ui.mode = 1;
    return {};
}
juce::String FiveProcessor::exportMidiFile(const juce::File& f, bool song, const std::string& map) {
    auto s = copyState(); const auto b = exportSMF(*s, song ? "song" : "pattern", song ? ui.song : ui.ptn, map);
    return f.replaceWithData(b.data(), b.size()) ? juce::String() : juce::String("can't write file");
}
juce::String FiveProcessor::loadWavIntoVoice(const juce::File& f, int v) {
    std::vector<float> d; double rate = 44100; juce::String err;
    if (!readAudioFile(f, d, rate, err)) return err;
    { const Edit l(lock); auto& s = st->samples[(size_t) v]; s.name = f.getFileNameWithoutExtension().substring(0, 16).toStdString(); s.sr = rate; s.data = std::move(d); s.lib = -1; }
    applyVoiceSample(v);
    return {};
}
juce::String FiveProcessor::loadSyxFile(const juce::File& f) {
    juce::MemoryBlock mb; if (!f.loadFileAsData(mb)) return "can't read file";
    auto s = copyState(); const auto r = loadSyx(*s, (const uint8_t*) mb.getData(), mb.getSize());
    if (!r.errors.empty()) return juce::String(r.errors.front());
    if (!r.omega && !r.yamaha) return "no RX5 or RX5 data";
    replaceState(std::move(s));
    return {};
}
juce::String FiveProcessor::saveSyxFile(const juce::File& f, int what) {
    auto s = copyState(); std::vector<Bytes> ms;
    if (what == 0) ms.push_back(omegaFull(*s));
    else if (what == 1) { ms = rxBulkMessages(*s); if (ms.empty()) return "no RX5 bulk data stored"; }
    else if (what == 2) ms.push_back(omegaPattern(*s, ui.ptn));
    else ms.push_back(omegaSong(*s, ui.song));
    const auto b = writeSyx(ms);
    return f.replaceWithData(b.data(), b.size()) ? juce::String() : juce::String("can't write file");
}
juce::String FiveProcessor::transmitBulk(int what) {
    auto s = copyState();
    std::vector<Bytes> ms = what == 0 ? std::vector<Bytes> { omegaFull(*s) } : five::transmitBulk(*s);
    if (ms.empty()) return "no bulk data";
    for (const auto& m : ms) queueOut(juce::MidiMessage::createSysExMessage(m.data() + 1, (int) m.size() - 2));
    return {};
}
juce::String FiveProcessor::loadBankFile(const juce::File& f) {
    juce::MemoryBlock mb; if (!f.loadFileAsData(mb)) return "can't read file";
    const auto p = parseBank((const uint8_t*) mb.getData(), mb.getSize());
    if (!p.ok) return juce::String(p.error);
    auto s = copyState(); s->library.clear(); int id = 24;
    for (const auto& v : p.voices) {
        if (id >= 24 + 30) break;
        LibWave w; w.id = id++; w.name = v.name; w.kind = "crt"; w.sr = ROM_SR; w.data = v.pcm; w.file = f.getFileName().toStdString(); w.source = "RX5 bank";
        s->library.push_back(std::move(w));
    }
    replaceState(std::move(s));
    return {};
}
juce::String FiveProcessor::saveBankFile(const juce::File& f) {
    std::vector<BankVoice> vs;
    {
        const Edit l(lock);
        for (int v = 0; v < NV; v++) {
            BankVoice b; b.name = juce::String(VOICE_NAMES[v]).removeCharacters(" ").substring(0, 6).toStdString(); b.params = st->voice[(size_t) v];
            b.bits = (int) st->voice[(size_t) v][vBits]; b.out = outChannel(v); b.pcm = engine->user[v];
            if (b.pcm.size() >= 2) vs.push_back(std::move(b));
        }
    }
    // a bank holds 128 KB: drop voices from the end until the rest fit
    while (!vs.empty()) {
        try { const auto bank = buildBank(vs); if (!f.replaceWithData(bank.data(), bank.size())) return "can't write file"; return vs.size() < NV ? juce::String(vs.size()) + " voices fit" : juce::String(); }
        catch (const std::exception&) { vs.pop_back(); }
    }
    return "no voices to save";
}
juce::String FiveProcessor::loadKit(const juce::File& src) {
    struct In { juce::String name; std::vector<float> data; double sr; };
    std::vector<In> items; juce::String err;
    auto isAudio = [](const juce::String& n) { const auto e = n.fromLastOccurrenceOf(".", false, false).toLowerCase(); return e == "wav" || e == "wave" || e == "aif" || e == "aiff"; };
    if (src.isDirectory()) {
        for (const auto& f : src.findChildFiles(juce::File::findFiles, true, "*")) {
            if (!isAudio(f.getFileName()) || f.getFileName().startsWith(".")) continue;
            In it; it.name = f.getFileName(); if (readAudioFile(f, it.data, it.sr, err)) items.push_back(std::move(it));
        }
    } else if (src.hasFileExtension("zip")) {
        juce::ZipFile z(src);
        for (int i = 0; i < z.getNumEntries(); i++) {
            const auto* e = z.getEntry(i); const juce::String path = e->filename, base = path.fromLastOccurrenceOf("/", false, false);
            if (!isAudio(base) || base.startsWith(".") || path.contains("__MACOSX")) continue;
            In it; it.name = base;
            if (readAudio(std::unique_ptr<juce::InputStream>(z.createStreamForEntry(i)), it.data, it.sr, err)) items.push_back(std::move(it));
        }
    } else if (isAudio(src.getFileName())) { In it; it.name = src.getFileName(); if (readAudioFile(src, it.data, it.sr, err)) items.push_back(std::move(it)); }
    else return "pick a folder, .zip or WAV/AIFF";
    if (items.empty()) return err.isNotEmpty() ? err : juce::String("no WAV/AIFF files");
    std::sort(items.begin(), items.end(), [](const In& a, const In& b) { return a.name.compareNatural(b.name) < 0; });
    // assignment: exact RX5 names, then generic aliases; library ids: voice, WRC-01 slot, else free slots 24..53
    std::array<int, NV> taken; taken.fill(-1); std::vector<int> voiceOf(items.size(), -1), id(items.size(), -1), exact(items.size(), -1);
    for (size_t i = 0; i < items.size(); i++) exact[i] = rx5Name(normName(items[i].name));
    for (size_t i = 0; i < items.size(); i++) if (exact[i] >= 0 && exact[i] < NV && taken[(size_t) exact[i]] < 0) { taken[(size_t) exact[i]] = (int) i; voiceOf[i] = exact[i]; }
    for (size_t i = 0; i < items.size(); i++) if (voiceOf[i] < 0 && exact[i] < 0) for (int v : aliasVoices(items[i].name)) if (taken[(size_t) v] < 0) { taken[(size_t) v] = (int) i; voiceOf[i] = v; break; }
    std::set<int> used;
    for (size_t i = 0; i < items.size(); i++) if (voiceOf[i] >= 0) { id[i] = voiceOf[i]; used.insert(id[i]); }
    for (size_t i = 0; i < items.size(); i++) if (voiceOf[i] < 0 && exact[i] >= 100 && !used.count(24 + exact[i] - 100)) { id[i] = 24 + exact[i] - 100; used.insert(id[i]); }
    int next = 24;
    for (size_t i = 0; i < items.size(); i++) if (id[i] < 0) { while (next < 54 && used.count(next)) next++; if (next >= 54) break; id[i] = next; used.insert(next); }
    auto s = copyState(); s->library.clear(); int assigned = 0;
    for (size_t i = 0; i < items.size(); i++) {
        if (id[i] < 0) continue;
        LibWave w; w.id = id[i]; w.sr = items[i].sr; w.data = trimSilence(items[i].data, items[i].sr); w.file = items[i].name.toStdString(); w.source = src.getFileName().toStdString();
        w.kind = voiceOf[i] >= 0 ? "int" : exact[i] >= 100 ? "crt" : "user";
        w.name = voiceOf[i] >= 0 && exact[i] == voiceOf[i] ? INT_NAMES[voiceOf[i]] : exact[i] >= 100 ? CART_NAMES[exact[i] - 100] : chooseKitName(items[i].name).toStdString();
        if (voiceOf[i] >= 0) { auto& u = s->samples[(size_t) voiceOf[i]]; u.name = w.name; u.sr = w.sr; u.data = w.data; u.lib = w.id; assigned++; }
        s->library.push_back(std::move(w));
    }
    std::sort(s->library.begin(), s->library.end(), [](const LibWave& a, const LibWave& b) { return a.id < b.id; });
    replaceState(std::move(s));
    setMessage("KIT " + juce::String(assigned) + " VOICES");
    return {};
}

// offline render of the current pattern (one pass) or song, plus a 2 s tail, as a 24-bit stereo WAV
juce::String FiveProcessor::exportWav(const juce::File& f, bool song, double rate) {
    auto s = copyState(); Engine e(rate);
    for (int v = 0; v < NV; v++) {
        std::vector<float> d; { const Edit l(lock); d = engine->user[v]; }
        auto p = Engine::prepare(d.data(), d.size(), ROM_SR, s->voice[(size_t) v].rec, (int) s->voice[(size_t) v][vBits], false); e.install(v, p);
    }
    for (int v = 0; v < NV; v++) { for (int k = 0; k < vCount; k++) e.setVoiceParam(v, k, s->voice[(size_t) v][k]); e.setPath(v, s->voice[(size_t) v].rec); }
    for (int c = 0; c < NCH; c++) { e.channels[c] = s->channel[(size_t) c]; e.mixCh(c); }
    e.revFx.setProgram(s->rev[rProgram]); for (int k = rTime; k < rCount; k++) e.revFx.set(k, s->rev[(size_t) k]);
    for (int k = 0; k < dCount; k++) if (k != dTempo) e.dlyFx.set(k, s->dly[(size_t) k]);
    e.setVolume(s->volume); e.setClick(0);
    std::array<int, NV> oc; for (int v = 0; v < NV; v++) oc[(size_t) v] = outChannel(v);
    Player p(s.get()); p.setMode(song ? Player::SongMode : Player::PatternMode, song ? ui.song : ui.ptn); p.start();
    if (!p.running) return "the song is empty";
    const int block = 512; std::vector<float> L, R; std::vector<float> bl(block), br(block); long long tail = -1; const long long maxLen = (long long) (rate * 60 * 20);
    for (long long at = 0; at < maxLen; at += block) {
        p.advance(block, rate, 0, [&](const PlayEvent& ev) { if (ev.v >= 0) e.trigger(ev.v, ev.vel, ev.acc, ev.offset, 0, oc[(size_t) ev.v]); });
        e.setTempo(p.bpm());
        e.process(bl.data(), br.data(), block);
        L.insert(L.end(), bl.begin(), bl.end()); R.insert(R.end(), br.begin(), br.end());
        if (tail < 0 && (!p.running || (!song && p.pass >= 1))) { p.stop(); tail = (long long) (2 * rate); }
        if (tail >= 0 && (tail -= block) <= 0) break;
    }
    juce::AudioBuffer<float> b(2, (int) L.size()); b.copyFrom(0, 0, L.data(), (int) L.size()); b.copyFrom(1, 0, R.data(), (int) R.size());
    f.deleteFile(); juce::WavAudioFormat wf; auto os = f.createOutputStream(); if (!os) return "can't write file";
    std::unique_ptr<juce::AudioFormatWriter> w(wf.createWriterFor(os.get(), rate, 2, 24, {}, 0)); if (!w) return "can't write WAV"; os.release();
    w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    return {};
}

/* ================================================================================================ plugin state */
void FiveProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto s = copyState(); auto v = cartToVar(*s);
    auto pl = obj(); put(pl, "mode", ui.mode); put(pl, "ptn", ui.ptn); put(pl, "song", ui.song); put(pl, "chain", ui.chain); put(pl, "voice", ui.voice);
    put(pl, "hostSync", hostSync()); put(v, "plugin", pl);
    const auto json = juce::JSON::toString(v, true);
    juce::MemoryOutputStream mo(dest, false); juce::GZIPCompressorOutputStream gz(mo, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
    gz.write(json.toRawUTF8(), json.getNumBytesAsUTF8()); gz.flush();
}
void FiveProcessor::setStateInformation(const void* data, int size) {
    if (size <= 0) return;
    juce::String text; const auto* u8 = (const uint8_t*) data;
    if (size >= 2 && u8[0] == 0x1f && u8[1] == 0x8b) {
        juce::MemoryInputStream mi(data, (size_t) size, false); juce::GZIPDecompressorInputStream gz(&mi, false, juce::GZIPDecompressorInputStream::gzipFormat);
        text = gz.readEntireStreamAsString();
    } else text = juce::String::fromUTF8((const char*) data, size);
    juce::var v; if (juce::JSON::parse(text, v).failed()) return;
    try { replaceState(std::make_unique<State>(cartFromVar(v))); } catch (const std::exception&) { return; }
    const auto pl = v["plugin"];
    if (pl.isObject()) {
        ui.mode = (int) getD(pl, "mode", 0) ? 1 : 0; ui.ptn = clampInt(getD(pl, "ptn", 0), 0, NUM_PATTERNS - 1, 0); ui.song = clampInt(getD(pl, "song", 0), 0, NUM_SONGS - 1, 0);
        ui.chain = clampInt(getD(pl, "chain", 0), 0, NUM_CHAINS - 1, 0); ui.voice = clampInt(getD(pl, "voice", 0), 0, NV - 1, 0);
        if (has(pl, "hostSync")) setHostSync((bool) pl["hostSync"]);
    }
}

juce::AudioProcessorEditor* FiveProcessor::createEditor() { return new FiveEditor(*this); }

}  // namespace five

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new five::FiveProcessor(); }
