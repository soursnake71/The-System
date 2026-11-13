#include "TraksPlugin.h"
#include "TraksEditor.h"
#include "BinaryData.h"
#include <map>

namespace traks {

static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const auto ps = params();
    for (int i = 0; i < NP; ++i) {
        const auto& p = ps[(size_t) i];
        auto ends = [&](const char* e) { const std::string x(e); return p.id.size() >= x.size() && p.id.compare(p.id.size() - x.size(), x.size(), x) == 0; };
        const bool toggle = ends("mute") || ends("rev") || p.id == "interp" || p.id == "poly" || p.id == "filt" || p.id == "dlPing";
        const bool stepped = toggle || p.id == "dac" || p.id == "rvProg" || p.id == "odMode" || p.id == "dlSync" || ends("out") || p.id == "tempo";
        juce::NormalisableRange<float> r((float) p.min, (float) p.max, stepped ? 1.0f : 0.0f);
        const bool logish = p.id == "rvTime" || p.id == "rvCut" || ends("F") || ends("Q") || ends("Cut") || ends("cut") || p.id == "dlTime" || p.id == "dlModRate"
            || ends("dcy") || ends("rel") || ends("dec");
        if (logish && p.min > 0) r.setSkewForCentre((float) std::sqrt(p.min * p.max));
        if (ends("atk")) r.setSkewForCentre(0.08f);
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(p.id, 1), p.name, r, (float) p.def));
    }
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("accVel", 1), "Accent Velocity", juce::NormalisableRange<float>(1, 127, 1), 110.0f));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("follow", 1), "Follow DAW Transport", true));
    return l;
}

TraksProcessor::TraksProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Mix", juce::AudioChannelSet::stereo(), true)
          .withOutput("CH1 Bass", juce::AudioChannelSet::stereo(), false)
          .withOutput("CH2 Snare/Rim", juce::AudioChannelSet::stereo(), false)
          .withOutput("CH3 Toms", juce::AudioChannelSet::stereo(), false)
          .withOutput("CH4 Cymbal", juce::AudioChannelSet::stereo(), false)
          .withOutput("CH5 Hi Hat", juce::AudioChannelSet::stereo(), false)
          .withOutput("CH6 Percussion", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "TRAKS", layout()) {
    const auto ps = params();
    for (int i = 0; i < NP; ++i) raw[(size_t) i] = apvts.getRawParameterValue(ps[(size_t) i].id);
    accVel = apvts.getRawParameterValue("accVel"); follow = apvts.getRawParameterValue("follow");
    cur = defaults();
    factoryRom = parseRom((const uint8_t*) BinaryData::traks_rom, (size_t) BinaryData::traks_romSize);
    factoryRom.resize(NV);
    rom = factoryRom;
    for (int v = 0; v < NV; ++v) romNames[(size_t) v] = factoryName(v);
    for (auto& m : meters) m = 0;
    engine = std::make_unique<Engine>(48000.0);
    machine = std::make_unique<Machine>(*engine, 48000.0);
    for (int v = 0; v < NV; ++v) engine->setRom(v, rom[(size_t) v]);
}

bool TraksProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int b = 1; b < l.outputBuses.size(); ++b) {
        const auto& s = l.outputBuses.getReference(b);
        if (!s.isDisabled() && s != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void TraksProcessor::prepareToPlay(double sampleRate, int maxBlock) {
    const juce::ScopedLock sl(memLock);
    auto e2 = std::make_unique<Engine>(sampleRate);
    for (int v = 0; v < NV; ++v) e2->setRom(v, rom[(size_t) v]);
    auto m2 = std::make_unique<Machine>(*e2, sampleRate);
    // carry the memory and the panel's state over
    m2->mem = std::move(machine->mem);
    m2->group = machine->group; m2->patFn = machine->patFn; m2->songFn = machine->songFn; m2->pat = machine->pat; m2->ppat = machine->pat;
    m2->song = machine->song; m2->quant = machine->quant; m2->bstep = machine->bstep;
    machine = std::move(m2); engine = std::move(e2);
    readParams(); engine->setAll(cur);
    for (auto& s : scratch) s.assign((size_t) std::max(1, maxBlock), 0.0f);
    hostWasPlaying = false;
}

void TraksProcessor::readParams() {
    Params p = cur;
    for (int i = 0; i < NP; ++i) if (raw[(size_t) i]) p[(size_t) i] = raw[(size_t) i]->load();
    if (p != cur) { cur = p; engine->setAll(cur); }
}

static int noteToVoice(int n) {
    switch (n) {
        case 35: case 36: return 0; case 38: case 40: return 1; case 37: return 2;
        case 48: case 50: case 47: return 3; case 45: case 43: case 41: return 4;
        case 49: case 57: case 52: case 55: return 5; case 51: case 59: case 53: return 6;
        case 42: case 44: return 7; case 46: return 8; case 39: return 9; case 54: return 10;
        case 56: return 11; case 69: case 70: return 12; default: return -1;
    }
}

void TraksProcessor::handleMidi(const juce::MidiMessage& m) {
    if (m.isNoteOn()) {
        const int v = noteToVoice(m.getNoteNumber()); if (v < 0) return;
        const bool prev = machine->accHeld;
        machine->accHeld = prev || m.getVelocity() >= (int) accVel->load();
        machine->inst(v, m.getVelocity() / 127.0);
        machine->held &= ~(1 << v);
        machine->accHeld = prev;
        selected = v;
    } else if (m.isNoteOff()) {
        const int v = noteToVoice(m.getNoteNumber()); if (v >= 0) engine->release(v);
    }
}

void TraksProcessor::publish() {
    const Display d = machine->display();
    const juce::SpinLock::ScopedLockType l(dispLock); disp = d;
}

void TraksProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    const juce::ScopedLock sl(memLock);
    if (romDirty.exchange(false)) for (int v = 0; v < NV; ++v) engine->setRom(v, rom[(size_t) v]);
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
    // keys pressed on the editor
    for (const int w = qW.load(); qR != w; ++qR) {
        const int e = q[(size_t) (qR & 255)];
        if ((e & 0xff) == 200) machine->knobMoved();
        else { machine->key(e & 0xff, (e & 0x100) != 0); if ((e & 0xff) >= K::INST && (e & 0x100)) selected = (e & 0xff) - K::INST; }
    }
    // follow the DAW: its play starts the machine where the DAW is, its stop stops it
    if (fol) {
        if (playing && !hostWasPlaying) { if (!machine->running) machine->start(); if (ppq >= 0) machine->locate(ppq * PPQ); }
        else if (playing && machine->running && ppq >= 0) {
            const double want = ppq * PPQ, have = machine->position();
            double d = want - have;
            if (machine->group == 0) { const double len = patLen(machine->mem.pats[(size_t) machine->ppat]); d = std::fmod(std::fmod(d, len) + len * 1.5, len) - len * 0.5; }
            if (std::abs(d) > 3) machine->locate(want);
        }
        else if (!playing && hostWasPlaying && machine->running) machine->stop();
    }
    hostWasPlaying = playing;
    float* outs[12];
    for (int b = 0; b < 6; ++b) for (int c = 0; c < 2; ++c) {
        auto* bus = getBus(false, b + 1);
        if (bus && bus->isEnabled() && bus->getNumberOfChannels() == 2) outs[b * 2 + c] = getBusBuffer(buf, false, b + 1).getWritePointer(c);
        else { if ((int) scratch[b * 2 + c].size() < n) scratch[b * 2 + c].assign((size_t) n, 0.0f); outs[b * 2 + c] = scratch[b * 2 + c].data(); }
    }
    auto main = getBusBuffer(buf, false, 0);
    float* L = main.getWritePointer(0); float* R = main.getWritePointer(1);
    int pos = 0;
    auto run = [&](int upto) {
        if (upto <= pos) return;
        float* o[12]; for (int k = 0; k < 12; ++k) o[k] = outs[k] + pos;
        machine->render(L + pos, R + pos, upto - pos, o);
        pos = upto;
    };
    for (const auto meta : midi) { run(juce::jlimit(0, n, meta.samplePosition)); handleMidi(meta.getMessage()); }
    run(n);
    for (int v = 0; v < NV; ++v) { meters[(size_t) v] = std::max(meters[(size_t) v].load() * 0.85f, (float) engine->meter[v]); engine->meter[v] = 0; }
    publish();
}

void TraksProcessor::setValue(int index, double v) {
    if (auto* prm = apvts.getParameter(params()[(size_t) index].id)) prm->setValueNotifyingHost(prm->convertTo0to1((float) v));
}

static bool kept(int i) { return i == pid::tempo || i == pid::volume || i == pid::metro; }
void TraksProcessor::setCurrentProgram(int i) {
    if (i < 0 || i >= kPresets) return;
    program = i;
    for (int k = 0; k < NP; ++k) if (!kept(k)) setValue(k, PRESETS[i].p[k]);
    if (onProgram) onProgram();
}

// the dice (traks/presets.js dice(), the same rules from Presets.inc)
void TraksProcessor::dice(int part) {
    const auto ps = params();
    std::map<std::string, int> index; for (int i = 0; i < NP; ++i) index[ps[(size_t) i].id] = i;
    uint32_t x = (uint32_t) juce::Random::getSystemRandom().nextInt() | 1u;
    auto r = [&x]() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x / 4294967296.0; };
    auto expand = [&](const std::string& id) {
        std::vector<int> out;
        if (id.find('*') != std::string::npos) { for (int v = 0; v < NV; ++v) { std::string k = id; k.replace(k.find('*'), 1, VOICES[v]); out.push_back(index.at(k)); } }
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

/* ---- memory as JSON (the same backup format as the app: traks/app.js backup()) */
juce::var TraksProcessor::memToVar() {
    juce::Array<juce::var> pats, songs;
    for (const auto& pt : machine->mem.pats) {
        auto* o = new juce::DynamicObject(); o->setProperty("num", pt.num); o->setProperty("den", pt.den); o->setProperty("meas", pt.meas); o->setProperty("swing", pt.swing);
        juce::Array<juce::var> ev;
        for (const auto& x : pt.ev) { auto* e = new juce::DynamicObject(); e->setProperty("t", x.t); e->setProperty("v", x.v); e->setProperty("a", x.a); e->setProperty("vel", x.vel); e->setProperty("lv", x.lv); e->setProperty("tu", x.tu); ev.add(juce::var(e)); }
        o->setProperty("ev", ev); pats.add(juce::var(o));
    }
    for (const auto& s : machine->mem.songs) {
        auto* o = new juce::DynamicObject(); juce::Array<juce::var> st;
        for (const auto& x : s.steps) { auto* e = new juce::DynamicObject(); e->setProperty("p", x.p); e->setProperty("vol", x.vol); e->setProperty("tempo", x.tempo); st.add(juce::var(e)); }
        o->setProperty("steps", st); songs.add(juce::var(o));
    }
    auto* m = new juce::DynamicObject(); m->setProperty("pats", pats); m->setProperty("songs", songs);
    return juce::var(m);
}
void TraksProcessor::varToMem(const juce::var& m) {
    auto* pa = m["pats"].getArray(); auto* sa = m["songs"].getArray();
    if (!pa || !sa) return;
    Memory mem; mem.pats.resize(100); mem.songs.resize(100);
    for (int i = 0; i < 100 && i < pa->size(); ++i) {
        const auto& o = (*pa)[i]; Pattern& pt = mem.pats[(size_t) i];
        pt.num = juce::jlimit(1, 16, (int) o["num"]); pt.den = (int) o["den"]; if (pt.den != 2 && pt.den != 4 && pt.den != 8 && pt.den != 16) pt.den = 4;
        pt.meas = juce::jlimit(1, 99, (int) o["meas"]); pt.swing = juce::jlimit(50, 75, (int) o["swing"]);
        if (auto* ev = o["ev"].getArray()) for (const auto& x : *ev) {
            Ev e; e.t = (int) x["t"]; e.v = juce::jlimit(0, NV - 1, (int) x["v"]); e.a = (int) x["a"];
            e.vel = x.hasProperty("vel") ? (double) x["vel"] : 1.0; e.lv = x.hasProperty("lv") ? (double) x["lv"] : -1.0; e.tu = x.hasProperty("tu") ? (double) x["tu"] : -99.0;
            if (e.t >= 0 && e.t < patLen(pt) && (int) pt.ev.size() < MAX_EV) pt.ev.push_back(e);
        }
        std::sort(pt.ev.begin(), pt.ev.end(), evLess);
    }
    for (int i = 0; i < 100 && i < sa->size(); ++i) if (auto* st = (*sa)[i]["steps"].getArray())
        for (const auto& x : *st) if (mem.songs[(size_t) i].steps.size() < 255) { Step s; s.p = juce::jlimit(0, 99, (int) x["p"]); s.vol = (int) x["vol"]; s.tempo = (int) x["tempo"]; mem.songs[(size_t) i].steps.push_back(s); }
    machine->mem = std::move(mem);
    machine->changed();
}

juce::String TraksProcessor::backupJson() {
    auto* o = new juce::DynamicObject();
    o->setProperty("format", "omega-traks"); o->setProperty("version", 1);
    {
        const juce::ScopedLock sl(memLock);
        o->setProperty("mem", memToVar());
        o->setProperty("pat", machine->pat); o->setProperty("song", machine->song); o->setProperty("quant", machine->quant);
    }
    juce::Array<juce::var> ps; for (int i = 0; i < NP; ++i) ps.add(value(i));
    o->setProperty("params", ps);
    auto* roms = new juce::DynamicObject();
    for (int v = 0; v < NV; ++v) if (romNames[(size_t) v] != factoryName(v)) {
        auto* r = new juce::DynamicObject(); r->setProperty("rate", rom[(size_t) v].rate); r->setProperty("name", romNames[(size_t) v]);
        r->setProperty("codes", juce::Base64::toBase64(rom[(size_t) v].codes.data(), rom[(size_t) v].codes.size()));
        roms->setProperty(juce::String(v), juce::var(r));
    }
    o->setProperty("roms", juce::var(roms));
    return juce::JSON::toString(juce::var(o), true);
}
bool TraksProcessor::restoreJson(const juce::String& s) {
    const auto j = juce::JSON::parse(s);
    if (j["format"].toString() != "omega-traks") return false;
    {
        const juce::ScopedLock sl(memLock);
        if (j.hasProperty("mem")) varToMem(j["mem"]);
        if (j.hasProperty("pat")) { machine->pat = machine->ppat = juce::jlimit(0, 99, (int) j["pat"]); }
        if (j.hasProperty("song")) machine->song = juce::jlimit(0, 99, (int) j["song"]);
        if (j.hasProperty("quant")) machine->quant = (int) j["quant"];
        rom = factoryRom; for (int v = 0; v < NV; ++v) romNames[(size_t) v] = factoryName(v);
        if (auto* ro = j["roms"].getDynamicObject()) for (const auto& pr : ro->getProperties()) {
            const int v = pr.name.toString().getIntValue(); if (v < 0 || v >= NV) continue;
            juce::MemoryOutputStream mo; if (!juce::Base64::convertFromBase64(mo, pr.value["codes"].toString())) continue;
            Rom r; r.rate = (double) pr.value["rate"]; const auto* d = (const uint8_t*) mo.getData(); r.codes.assign(d, d + mo.getDataSize());
            if (r.codes.size() > 1 && r.rate > 1000) { rom[(size_t) v] = r; romNames[(size_t) v] = pr.value["name"].toString(); }
        }
        romDirty = true;
    }
    if (auto* ps = j["params"].getArray()) for (int i = 0; i < NP && i < ps->size(); ++i) setValue(i, (double) (*ps)[i]);
    return true;
}

bool TraksProcessor::loadSound(int v, const juce::File& f) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (!r || r->lengthInSamples < 2) return false;
    const int len = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 8));
    juce::AudioBuffer<float> b((int) r->numChannels, len); r->read(&b, 0, len, 0, true, true);
    std::vector<double> mono((size_t) len, 0.0);
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < len; ++i) mono[(size_t) i] += b.getSample(c, i) / b.getNumChannels();
    // to the ROM's 25 kHz clock (linear), normalised, companded to 8 bits
    const double rate = 25000, step = r->sampleRate / rate; const size_t n = (size_t) std::floor((len - 1) / step);
    std::vector<double> rs(n); double pk = 1e-9;
    for (size_t i = 0; i < n; ++i) { const double p = (double) i * step; const size_t k = (size_t) p; const double fr = p - (double) k; rs[i] = mono[k] + (mono[std::min(k + 1, mono.size() - 1)] - mono[k]) * fr; pk = std::max(pk, std::abs(rs[i])); }
    Rom ro; ro.rate = rate; ro.codes.resize(n); for (size_t i = 0; i < n; ++i) ro.codes[i] = muEncode(rs[i] / pk * 0.97);
    if (ro.codes.size() < 2) return false;
    { const juce::ScopedLock sl(memLock); rom[(size_t) v] = ro; romNames[(size_t) v] = f.getFileNameWithoutExtension(); romDirty = true; }
    return true;
}
void TraksProcessor::resetSound(int v) { const juce::ScopedLock sl(memLock); rom[(size_t) v] = factoryRom[(size_t) v]; romNames[(size_t) v] = factoryName(v); romDirty = true; }

void TraksProcessor::getStateInformation(juce::MemoryBlock& mb) {
    auto st = apvts.copyState();
    st.setProperty("program", program, nullptr);
    st.setProperty("backup", backupJson(), nullptr);
    st.setProperty("metroAlways", metroAlways.load(), nullptr);
    std::unique_ptr<juce::XmlElement> x(st.createXml());
    copyXmlToBinary(*x, mb);
}
void TraksProcessor::setStateInformation(const void* d, int n) {
    std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(d, n));
    if (!x || !x->hasTagName(apvts.state.getType())) return;
    auto t = juce::ValueTree::fromXml(*x);
    program = (int) t.getProperty("program", 0);
    metroAlways = (bool) t.getProperty("metroAlways", false);
    const juce::String b = t.getProperty("backup", "").toString();
    t.removeProperty("backup", nullptr);
    apvts.replaceState(t);
    if (b.isNotEmpty()) {   // the memory and ROMs (its params are the same as the state's)
        auto j = juce::JSON::parse(b);
        if (auto* o = j.getDynamicObject()) o->removeProperty("params");
        restoreJson(juce::JSON::toString(j));
    }
}

juce::AudioProcessorEditor* TraksProcessor::createEditor() { return new TraksEditor(*this); }

} // namespace traks

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new traks::TraksProcessor(); }
