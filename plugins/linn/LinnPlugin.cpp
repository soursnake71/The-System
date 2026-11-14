#include "LinnPlugin.h"
#include "LinnEditor.h"
#include "BinaryData.h"
#include <map>
#include <set>

namespace linn {

static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const auto ps = params();
    for (int i = 0; i < NP; ++i) {
        const auto& p = ps[(size_t) i];
        auto ends = [&](const char* e) { const std::string x(e); return p.id.size() >= x.size() && p.id.compare(p.id.size() - x.size(), x.size(), x) == 0; };
        const bool toggle = ends("mute") || ends("rev") || p.id == "poly" || p.id == "dlPing" || p.id == "bassChip" || p.id == "kit";
        const bool stepped = toggle || p.id == "hatMode" || p.id == "rvProg" || p.id == "odMode" || p.id == "dlSync" || ends("out") || p.id == "tempo";
        juce::NormalisableRange<float> r((float) p.min, (float) p.max, stepped ? 1.0f : 0.0f);
        const bool logish = p.id == "rvTime" || p.id == "rvCut" || ends("F") || ends("Q") || ends("Cut") || ends("cut") || p.id == "dlTime" || p.id == "dlModRate"
            || ends("dcy") || ends("rel") || ends("dec");
        if (logish && p.min > 0) r.setSkewForCentre((float) std::sqrt(p.min * p.max));
        if (ends("atk")) r.setSkewForCentre(0.08f);
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(p.id, 1), p.name, r, (float) p.def));
    }
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("follow", 1), "Follow DAW Transport", true));
    return l;
}

LinnProcessor::LinnProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Mix", juce::AudioChannelSet::stereo(), true)
          .withOutput("OUT 1 Bass", juce::AudioChannelSet::stereo(), false)
          .withOutput("OUT 2 Snare", juce::AudioChannelSet::stereo(), false)
          .withOutput("OUT 3 Sidestick", juce::AudioChannelSet::stereo(), false)
          .withOutput("OUT 4 Hihat", juce::AudioChannelSet::stereo(), false)
          .withOutput("OUT 5 Toms", juce::AudioChannelSet::stereo(), false)
          .withOutput("OUT 6 Cymbals", juce::AudioChannelSet::stereo(), false)
          .withOutput("OUT 7 Congas", juce::AudioChannelSet::stereo(), false)
          .withOutput("OUT 8 Percussion", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "LINN", layout()) {
    const auto ps = params();
    for (int i = 0; i < NP; ++i) raw[(size_t) i] = apvts.getRawParameterValue(ps[(size_t) i].id);
    follow = apvts.getRawParameterValue("follow");
    cur = defaults();
    factoryRom = parseRom((const uint8_t*) BinaryData::linn_rom, (size_t) BinaryData::linn_romSize);
    rom = factoryRom;
    for (auto& m : meters) m = 0;
    engine = std::make_unique<Engine>(48000.0);
    machine = std::make_unique<Machine>(*engine, 48000.0);
    for (int g = 0; g < NG_ROM; ++g) engine->setGroup(g, rom[(size_t) g]);
}

bool LinnProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int b = 1; b < l.outputBuses.size(); ++b) {
        const auto& s = l.outputBuses.getReference(b);
        if (!s.isDisabled() && s != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void LinnProcessor::prepareToPlay(double sampleRate, int maxBlock) {
    const juce::ScopedLock sl(memLock);
    auto e2 = std::make_unique<Engine>(sampleRate);
    for (int g = 0; g < NG_ROM; ++g) e2->setGroup(g, rom[(size_t) g]);
    auto m2 = std::make_unique<Machine>(*e2, sampleRate);
    m2->mem = std::move(machine->mem);
    m2->mode = machine->mode; m2->pat = machine->pat; m2->ppat = machine->pat; m2->song = machine->song; m2->cursor = machine->cursor;
    m2->trig = machine->trig; m2->perc = machine->perc; m2->ext = machine->ext;
    machine = std::move(m2); engine = std::move(e2);
    readParams(); engine->setAll(cur);
    for (auto& s : scratch) s.assign((size_t) std::max(1, maxBlock), 0.0f);
    hostWasPlaying = false;
}

void LinnProcessor::readParams() {
    Params p = cur;
    for (int i = 0; i < NP; ++i) if (raw[(size_t) i]) p[(size_t) i] = raw[(size_t) i]->load();
    if (p != cur) { cur = p; engine->setAll(cur); }
}

// General MIDI drum notes -> sounds (linn/io.js noteToSound): velocity picks the dynamic
int LinnProcessor::noteToSound(int n, int vel) {
    auto lv2 = [vel](int a, int b) { return vel < 80 ? a : b; };
    switch (n) {
        case 35: case 36: return lv2(0, 1);
        case 38: case 40: return vel < 50 ? 2 : vel < 100 ? 3 : 4;
        case 37: case 31: return 5;
        case 42: return lv2(6, 7); case 44: return 6; case 46: return 8;
        case 48: case 50: return 9; case 45: case 47: return 10; case 41: case 43: return 11;
        case 51: case 53: case 59: return lv2(12, 13);
        case 49: case 52: case 55: case 57: return 14;
        case 69: return lv2(15, 16); case 70: return 15;
        case 54: return lv2(17, 18);
        case 60: case 62: case 63: return 19; case 61: case 64: return 20;
        case 56: return 21; case 39: return 22;
        default: return -1;
    }
}

void LinnProcessor::handleMidi(const juce::MidiMessage& m) {
    if (m.isNoteOn()) { const int s = noteToSound(m.getNoteNumber(), m.getVelocity()); if (s < 0) return; machine->inst(s); selected = SOUNDS[s].ch; }
    else if (m.isNoteOff()) { const int s = noteToSound(m.getNoteNumber(), 100); if (s >= 0) engine->release(SOUNDS[s].ch); }
    else if (m.isMidiClock()) machine->clockTick();
}

void LinnProcessor::publish() {
    const Display d = machine->display();
    shownPat = machine->running ? machine->ppat : machine->pat;
    const juce::SpinLock::ScopedLockType l(dispLock); disp = d;
}

void LinnProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    const juce::ScopedLock sl(memLock);
    if (romDirty.exchange(false)) for (int g = 0; g < NG_ROM; ++g) engine->setGroup(g, rom[(size_t) g]);
    double ppq = -1, bpm = 0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        if (auto b = pos->getBpm()) bpm = *b;
        if (auto p = pos->getPpqPosition()) ppq = *p;
        playing = pos->getIsPlaying();
    }
    readParams();
    const bool fol = follow->load() >= 0.5f;
    machine->bpm = fol && bpm > 0 ? juce::jlimit(20.0, 400.0, bpm) : cur[(size_t) pid::tempo];
    machine->metroAlways = metroAlways.load();
    for (const int w = qW.load(); qR != w; ++qR) {
        const int e = q[(size_t) (qR & 255)];
        machine->key(e & 0xff, (e & 0x100) != 0);
        if ((e & 0x100) && (e & 0xff) >= K::DRUM && (e & 0xff) != K::DRUM + PERC_KEY) selected = machine->sel;
    }
    if (fol) {
        if (playing && !hostWasPlaying) { if (!machine->running) machine->start(false, 0); if (ppq >= 0) machine->locate(ppq * PPQ); }
        else if (playing && machine->running && ppq >= 0) {
            const double want = ppq * PPQ, have = machine->position();
            double d = want - have;
            if (machine->mode == 0) { const double len = machine->mem.pats[(size_t) machine->ppat].len; d = std::fmod(std::fmod(d, len) + len * 1.5, len) - len * 0.5; }
            if (std::abs(d) > 3) machine->locate(want);
        }
        else if (!playing && hostWasPlaying && machine->running) machine->stop();
    }
    hostWasPlaying = playing;
    float* outs[16];
    for (int b = 0; b < 8; ++b) for (int c = 0; c < 2; ++c) {
        auto* bus = getBus(false, b + 1);
        if (bus && bus->isEnabled() && bus->getNumberOfChannels() == 2) outs[b * 2 + c] = getBusBuffer(buf, false, b + 1).getWritePointer(c);
        else { if ((int) scratch[b * 2 + c].size() < n) scratch[b * 2 + c].assign((size_t) n, 0.0f); outs[b * 2 + c] = scratch[b * 2 + c].data(); }
    }
    auto main = getBusBuffer(buf, false, 0);
    float* L = main.getWritePointer(0); float* R = main.getWritePointer(1);
    int pos = 0;
    auto run = [&](int upto) {
        if (upto <= pos) return;
        float* o[16]; for (int k = 0; k < 16; ++k) o[k] = outs[k] + pos;
        machine->render(L + pos, R + pos, upto - pos, o);
        pos = upto;
    };
    for (const auto meta : midi) { run(juce::jlimit(0, n, meta.samplePosition)); handleMidi(meta.getMessage()); }
    run(n);
    for (int v = 0; v < NV; ++v) { meters[(size_t) v] = std::max(meters[(size_t) v].load() * 0.85f, (float) engine->meter[v]); engine->meter[v] = 0; }
    publish();
}

void LinnProcessor::setValue(int index, double v) {
    if (auto* prm = apvts.getParameter(params()[(size_t) index].id)) prm->setValueNotifyingHost(prm->convertTo0to1((float) v));
}

static bool kept(int i) { return i == pid::tempo || i == pid::volume || i == pid::clickLv || i == pid::clickPan || i == pid::kit; }
void LinnProcessor::setCurrentProgram(int i) {
    if (i < 0 || i >= kPresets) return;
    program = i;
    for (int k = 0; k < NP; ++k) if (!kept(k)) setValue(k, PRESETS[i].p[k]);
    if (onProgram) onProgram();
}

// the dice (linn/presets.js dice(), the same rules from Presets.inc)
void LinnProcessor::dice(int part) {
    const auto ps = params();
    std::map<std::string, int> index; for (int i = 0; i < NP; ++i) index[ps[(size_t) i].id] = i;
    uint32_t x = (uint32_t) juce::Random::getSystemRandom().nextInt() | 1u;
    auto r = [&x]() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x / 4294967296.0; };
    auto expand = [&](const std::string& id) {
        std::vector<int> out;
        if (id.find('*') != std::string::npos) { for (int v = 0; v < NV; ++v) { std::string k = id; k.replace(k.find('*'), 1, CHS[v]); out.push_back(index.at(k)); } }
        else { size_t a = 0; while (a <= id.size()) { const size_t b = std::min(id.find(',', a), id.size()); out.push_back(index.at(id.substr(a, b - a))); a = b + 1; } }
        return out;
    };
    Params p{}; for (int i = 0; i < NP; ++i) p[(size_t) i] = value(i);
    const Params d = defaults();
    for (int pt = 1; pt < 6; ++pt) {
        if (part != 0 && part != pt) continue;
        for (const auto& rule : DICE_RULES) if (rule.part == pt) for (int i : expand(rule.id)) p[(size_t) i] = d[(size_t) i];
        for (const auto& rule : DICE_RULES) if (rule.part == pt) for (int i : expand(rule.id)) {
            if (r() >= rule.chance) continue;
            const double u = r(); double v = p[(size_t) i];
            switch (rule.kind) {
                case 0: v = rule.a + (rule.b - rule.a) * u; break;
                case 1: v = rule.a * std::pow(rule.b / rule.a, u); break;
                case 2: v = std::floor(rule.a + (rule.b - rule.a + 1) * u); break;
                case 3: v = rule.a; break;
                case 4: v = v + rule.a + (rule.b - rule.a) * u; break;
                default: v = v * (rule.a + (rule.b - rule.a) * u); break;
            }
            p[(size_t) i] = std::max(ps[(size_t) i].min, std::min(ps[(size_t) i].max, std::round(v * 1000) / 1000));
        }
    }
    for (int k = 0; k < NP; ++k) if (!kept(k)) setValue(k, p[(size_t) k]);
}

/* ---- memory as JSON (the same backup format as the app: linn/app.js backup()) */
juce::var LinnProcessor::memToVar() {
    juce::Array<juce::var> pats, songs;
    for (const auto& pt : machine->mem.pats) {
        auto* o = new juce::DynamicObject(); o->setProperty("len", pt.len); o->setProperty("ec", pt.ec); o->setProperty("tim", pt.tim); o->setProperty("bef", pt.bef);
        juce::Array<juce::var> ev;
        for (const auto& x : pt.ev) { auto* e = new juce::DynamicObject(); e->setProperty("t", x.t); e->setProperty("s", x.s); ev.add(juce::var(e)); }
        o->setProperty("ev", ev); pats.add(juce::var(o));
    }
    for (const auto& s : machine->mem.songs) { auto* o = new juce::DynamicObject(); juce::Array<juce::var> st; for (int x : s.steps) st.add(x); o->setProperty("steps", st); songs.add(juce::var(o)); }
    auto* m = new juce::DynamicObject(); m->setProperty("pats", pats); m->setProperty("songs", songs); m->setProperty("repeat", machine->mem.repeat);
    return juce::var(m);
}
void LinnProcessor::varToMem(const juce::var& m) {
    auto* pa = m["pats"].getArray(); auto* sa = m["songs"].getArray();
    if (!pa || !sa || pa->size() != NPAT) return;
    Memory mem; mem.pats.resize(NPAT); mem.songs.resize(NPAT); mem.repeat = m.hasProperty("repeat") ? (bool) m["repeat"] : true;
    for (int i = 0; i < NPAT; ++i) {
        const auto& o = (*pa)[i]; Pattern& pt = mem.pats[(size_t) i];
        pt.len = juce::jlimit(1, 99, (int) o["len"] / PPQ) * PPQ; pt.ec = juce::jlimit(0, 6, (int) o["ec"]); pt.tim = juce::jlimit(0, 5, (int) o["tim"]); pt.bef = juce::jlimit(0, 5, (int) o["bef"]);
        if (auto* ev = o["ev"].getArray()) for (const auto& x : *ev) {
            Ev e; e.t = (int) x["t"]; e.s = juce::jlimit(0, NS - 1, (int) x["s"]);
            if (e.t >= 0 && e.t < pt.len && (int) pt.ev.size() < MAX_EV) pt.ev.push_back(e);
        }
        std::sort(pt.ev.begin(), pt.ev.end(), evLess);
    }
    for (int i = 0; i < NPAT && i < sa->size(); ++i) if (auto* st = (*sa)[i]["steps"].getArray())
        for (const auto& x : *st) if (mem.songs[(size_t) i].steps.size() < 99) mem.songs[(size_t) i].steps.push_back(juce::jlimit(0, NPAT - 1, (int) x));
    machine->mem = std::move(mem);
    machine->changed();
}
static juce::var layerToVar(const Layer& L) {
    std::vector<int16_t> i16(L.data.size()); for (size_t i = 0; i < i16.size(); ++i) i16[i] = (int16_t) juce::jlimit(-32768, 32767, (int) jsRound(L.data[i] * 32767));
    auto* o = new juce::DynamicObject(); o->setProperty("rate", L.rate); o->setProperty("clk", L.clk); o->setProperty("dyn", L.dyn);
    o->setProperty("data", juce::Base64::toBase64(i16.data(), i16.size() * 2)); return juce::var(o);
}
static bool varToLayer(const juce::var& o, Layer& L) {
    juce::MemoryOutputStream mo; if (!juce::Base64::convertFromBase64(mo, o["data"].toString())) return false;
    const auto* d = (const int16_t*) mo.getData(); const size_t n = mo.getDataSize() / 2;
    L.rate = (double) o["rate"]; L.clk = (double) o["clk"]; L.dyn = o.hasProperty("dyn") ? (int) o["dyn"] : -1; L.kit = 0;
    L.data.resize(n); for (size_t i = 0; i < n; ++i) L.data[i] = d[i] / 32768.0;
    return n > 16 && L.rate > 1000 && L.clk > 100;
}
juce::String LinnProcessor::romName(int g) const {
    if (userRom[(size_t) g]) return romNames[(size_t) g];
    const auto& f = factoryRom[(size_t) g]; const int kit = value(pid::kit) >= 0.5 ? 2 : 1;
    int mine = 0, dyn = 0; for (const auto& L : f) if (L.kit == kit) { mine++; if (L.dyn >= 0) dyn++; }
    if (f.empty()) return "EMPTY (no recording yet)";
    juce::String s = "LinnDrum, set " + juce::String(mine ? kit : f[0].kit) + (mine ? "" : " (this set has none)");
    if (dyn) s << ", " << dyn << " dynamics"; else if ((mine ? mine : (int) f.size()) > 1) s << ", " << (mine ? mine : (int) f.size()) << " tunings";
    return s;
}
juce::String LinnProcessor::backupJson() {
    auto* o = new juce::DynamicObject();
    o->setProperty("format", "omega-linn"); o->setProperty("version", 1);
    {
        const juce::ScopedLock sl(memLock);
        o->setProperty("mem", memToVar());
        o->setProperty("pat", machine->pat); o->setProperty("song", machine->song); o->setProperty("mode", machine->mode);
    }
    juce::Array<juce::var> ps; for (int i = 0; i < NP; ++i) ps.add(value(i));
    o->setProperty("params", ps);
    auto* roms = new juce::DynamicObject();
    for (int g = 0; g < NG_ROM; ++g) if (userRom[(size_t) g]) {
        auto* r = new juce::DynamicObject(); r->setProperty("name", romNames[(size_t) g]);
        juce::Array<juce::var> ls; for (const auto& L : rom[(size_t) g]) ls.add(layerToVar(L));
        r->setProperty("layers", ls); roms->setProperty(juce::String(g), juce::var(r));
    }
    o->setProperty("roms", juce::var(roms));
    return juce::JSON::toString(juce::var(o), true);
}
bool LinnProcessor::restoreJson(const juce::String& s) {
    const auto j = juce::JSON::parse(s);
    if (j["format"].toString() != "omega-linn") return false;
    {
        const juce::ScopedLock sl(memLock);
        if (j.hasProperty("mem")) varToMem(j["mem"]);
        if (j.hasProperty("pat")) { machine->pat = machine->ppat = juce::jlimit(0, NPAT - 1, (int) j["pat"]); }
        if (j.hasProperty("song")) machine->song = juce::jlimit(0, NPAT - 1, (int) j["song"]);
        if (j.hasProperty("mode") && !machine->running) machine->mode = juce::jlimit(0, 1, (int) j["mode"]);
        rom = factoryRom; userRom.fill(false);
        if (auto* ro = j["roms"].getDynamicObject()) for (const auto& pr : ro->getProperties()) {
            const int g = pr.name.toString().getIntValue(); if (g < 0 || g >= NG_ROM) continue;
            std::vector<Layer> ls; if (auto* la = pr.value["layers"].getArray()) for (const auto& lv : *la) { Layer L; if (varToLayer(lv, L)) ls.push_back(std::move(L)); }
            if (!ls.empty()) { rom[(size_t) g] = std::move(ls); romNames[(size_t) g] = pr.value["name"].toString(); userRom[(size_t) g] = true; }
        }
        romDirty = true;
    }
    if (auto* ps = j["params"].getArray()) for (int i = 0; i < NP && i < ps->size(); ++i) setValue(i, (double) (*ps)[i]);
    return true;
}

static bool readAudio(const juce::File& f, kit::Rec& out) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (!r || r->lengthInSamples < 16) return false;
    const int len = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 6));
    juce::AudioBuffer<float> b((int) r->numChannels, len); r->read(&b, 0, len, 0, true, true);
    out.mono.assign((size_t) len, 0.0); out.rate = r->sampleRate;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < len; ++i) out.mono[(size_t) i] += b.getSample(c, i) / b.getNumChannels();
    return true;
}
bool LinnProcessor::loadSound(int g, const juce::File& f) {
    kit::Rec rec; if (!readAudio(f, rec)) return false;
    Layer L = kit::makeLayer(g, rec.mono, rec.rate, g == G::HAT && kit::isOpen(f.getFileName().toStdString()));
    { const juce::ScopedLock sl(memLock); rom[(size_t) g] = {L}; romNames[(size_t) g] = f.getFileNameWithoutExtension(); userRom[(size_t) g] = true; romDirty = true; }
    return true;
}
juce::String LinnProcessor::loadFolder(const juce::File& dir) {
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    files.sort();
    std::vector<std::string> names; for (const auto& f : files) names.push_back(f.getFileName().toStdString());
    const auto c = kit::classify(names);
    std::set<int> use; for (const auto& l : c.groups) for (int i : l) use.insert(i);
    std::vector<kit::Rec> recs(names.size());
    for (int i : use) readAudio(files[i], recs[(size_t) i]);
    const auto groups = kit::buildKit(names, recs);
    juce::StringArray done;
    {
        const juce::ScopedLock sl(memLock);
        for (int g = 0; g < NG_ROM; ++g) if (!groups[(size_t) g].empty()) {
            rom[(size_t) g] = groups[(size_t) g]; userRom[(size_t) g] = true;
            juce::StringArray nm; for (int i : c.groups[(size_t) g]) nm.add(files[i].getFileNameWithoutExtension());
            romNames[(size_t) g] = nm.joinIntoString(", ").substring(0, 60); done.add(GROUPS[g]);
        }
        romDirty = true;
    }
    juce::String s = done.isEmpty() ? juce::String("No drum sounds found in that folder (names like kick, snare, ohh, tom, conga...).") : "Loaded: " + done.joinIntoString(", ");
    if (!c.left.empty()) { juce::StringArray l; for (int i : c.left) l.add(files[i].getFileName()); s << "\nNot used: " << l.joinIntoString(", "); }
    return s;
}
void LinnProcessor::resetSound(int g) { const juce::ScopedLock sl(memLock); rom[(size_t) g] = factoryRom[(size_t) g]; userRom[(size_t) g] = false; romDirty = true; }
void LinnProcessor::resetSounds() { const juce::ScopedLock sl(memLock); rom = factoryRom; userRom.fill(false); romDirty = true; }

/* ---- a MIDI file in (linn/io.js importMidi) */
juce::String LinnProcessor::importMidi(const juce::File& file, bool asSong, int bars, int ec, bool takeTempo) {
    juce::FileInputStream in(file); juce::MidiFile mf;
    if (!in.openedOk() || !mf.readFrom(in)) return "Not a MIDI file";
    const int div = mf.getTimeFormat(); if (div <= 0) return "SMPTE-timed MIDI files are not supported";
    struct N { int t, s; }; std::vector<N> all10, all; double tempo = 0; int num = 4, den = 4;
    for (int t = 0; t < mf.getNumTracks(); ++t) for (const auto* h : *mf.getTrack(t)) {
        const auto& m = h->message;
        if (m.isTempoMetaEvent() && tempo == 0) tempo = 60.0 / m.getTempoSecondsPerQuarterNote();
        if (m.isTimeSignatureMetaEvent() && m.getTimeStamp() == 0) m.getTimeSignatureInfo(num, den);
        if (m.isNoteOn()) { const int s = noteToSound(m.getNoteNumber(), m.getVelocity()); if (s < 0) continue;
            const N x{(int) jsRound(m.getTimeStamp() * PPQ / div), s}; all.push_back(x); if (m.getChannel() == 10) all10.push_back(x); }
    }
    auto notes = all10.empty() ? all : all10;
    if (notes.empty()) return "No drum notes in that file";
    if (ec >= 0) for (auto& x : notes) x.t = (int) jsRound((double) x.t / EC_GRID[ec]) * EC_GRID[ec];
    const int barTicks = (int) jsRound(num * 4.0 / den * PPQ);
    int last = 0; for (const auto& x : notes) last = std::max(last, x.t);
    const int totalBars = std::max(1, (last + barTicks) / barTicks);
    auto cut = [&](int from, int len) { std::vector<Ev> ev; std::set<std::pair<int, int>> seen;
        for (const auto& x : notes) if (x.t >= from && x.t < from + len && seen.insert({x.t - from, x.s}).second) { Ev e; e.t = x.t - from; e.s = x.s; ev.push_back(e); }
        std::sort(ev.begin(), ev.end(), evLess); return ev; };
    juce::String msg;
    {
        const juce::ScopedLock sl(memLock);
        auto& mem = machine->mem;
        if (!asSong) {
            const int p = machine->running ? machine->ppat : machine->pat;
            const int beats = juce::jlimit(1, 99, (int) jsRound((double) totalBars * barTicks / PPQ));
            auto& pt = mem.pats[(size_t) p]; pt.len = beats * PPQ; pt.ev = cut(0, pt.len); if ((int) pt.ev.size() > MAX_EV) pt.ev.resize(MAX_EV); if (ec >= 0) pt.ec = ec;
            msg << file.getFileName() << " is pattern " << pnum(p) << " (" << beats << " beats, " << (int) pt.ev.size() << " notes)";
        } else {
            const int len = std::max(1, bars) * barTicks;
            std::vector<int> fr; for (int i = 35; i < NPAT; ++i) if (mem.pats[(size_t) i].ev.empty()) fr.push_back(i); for (int i = 0; i < 35; ++i) if (mem.pats[(size_t) i].ev.empty()) fr.push_back(i);
            std::map<std::vector<std::pair<int, int>>, int> made; std::vector<int> steps;
            for (int b = 0; b < totalBars; b += std::max(1, bars)) {
                const auto ev = cut(b * barTicks, len); std::vector<std::pair<int, int>> key; for (const auto& x : ev) key.push_back({x.t, x.s});
                auto it = made.find(key);
                if (it == made.end()) {
                    if (fr.empty()) { msg << "Not enough empty patterns: the song stops at step " << (int) steps.size() << ". "; break; }
                    const int i = fr.front(); fr.erase(fr.begin());
                    Pattern pt; pt.len = std::min(99 * PPQ, len); pt.ec = ec >= 0 ? ec : 6; pt.ev = ev; mem.pats[(size_t) i] = pt; it = made.emplace(key, i).first;
                }
                if (steps.size() < 99) steps.push_back(it->second);
            }
            mem.songs[(size_t) machine->song].steps = steps;
            msg << file.getFileName() << " is song " << pnum(machine->song) << ": " << (int) steps.size() << " steps on " << (int) made.size() << " patterns";
        }
        machine->changed();
    }
    if (takeTempo && tempo > 0) setValue(pid::tempo, juce::jlimit(40.0, 250.0, std::round(tempo)));
    return msg;
}

void LinnProcessor::getStateInformation(juce::MemoryBlock& mb) {
    auto st = apvts.copyState();
    st.setProperty("program", program, nullptr);
    st.setProperty("backup", backupJson(), nullptr);
    st.setProperty("metroAlways", metroAlways.load(), nullptr);
    std::unique_ptr<juce::XmlElement> x(st.createXml());
    copyXmlToBinary(*x, mb);
}
void LinnProcessor::setStateInformation(const void* d, int n) {
    std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(d, n));
    if (!x || !x->hasTagName(apvts.state.getType())) return;
    auto t = juce::ValueTree::fromXml(*x);
    program = (int) t.getProperty("program", 0);
    metroAlways = (bool) t.getProperty("metroAlways", false);
    const juce::String b = t.getProperty("backup", "").toString();
    t.removeProperty("backup", nullptr);
    apvts.replaceState(t);
    if (b.isNotEmpty()) {
        auto j = juce::JSON::parse(b);
        if (auto* o = j.getDynamicObject()) o->removeProperty("params");
        restoreJson(juce::JSON::toString(j));
    }
}

juce::AudioProcessorEditor* LinnProcessor::createEditor() { return new LinnEditor(*this); }

} // namespace linn

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new linn::LinnProcessor(); }
