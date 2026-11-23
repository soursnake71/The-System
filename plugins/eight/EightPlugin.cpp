#include "EightPlugin.h"
#include "EightEditor.h"
#include "Presets.inc"

namespace eight {

juce::String paramId(const juce::String& key) { return key.replaceCharacter('.', '_'); }

// General MIDI drum notes -> voices (the app's midi.js default map; congas, claves and maracas play their own voices)
static const char* noteVoice(int n) {
    switch (n) {
        case 35: case 36: return "BD";  case 38: case 40: return "SD";  case 41: case 43: return "LT";
        case 45: case 47: return "MT";  case 48: case 50: return "HT";  case 64: return "LC"; case 63: return "MC";
        case 62: return "HC";  case 37: return "RS";  case 75: return "CL";  case 39: return "CP";  case 70: return "MA";
        case 56: return "CB";  case 49: case 57: return "CY";  case 46: return "OH";  case 42: case 44: return "CH";
        case 76: return "TRIG";
        default: return nullptr;
    }
}
static int voiceNote(const std::string& v) {
    static const std::map<std::string, int> N = {{"BD", 36}, {"SD", 38}, {"LT", 41}, {"MT", 45}, {"HT", 48}, {"LC", 64}, {"MC", 63},
        {"HC", 62}, {"RS", 37}, {"CL", 75}, {"CP", 39}, {"MA", 70}, {"CB", 56}, {"CY", 49}, {"OH", 46}, {"CH", 42}, {"TRIG", 76}};
    auto i = N.find(v); return i == N.end() ? -1 : i->second;
}
static constexpr int kTrigNote = 76, kOutCh = 10;

juce::AudioProcessorValueTreeState::ParameterLayout EightProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (auto& p : params()) {
        const juce::String id = paramId(p.k), name = juce::String(p.group) + " " + p.label;
        if (!p.opts.empty()) {
            juce::StringArray o; for (auto& s : p.opts) o.add(s);
            l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, o, juce::jlimit(0, o.size() - 1, (int) std::lround(p.def - p.min))));
        } else {
            juce::NormalisableRange<float> r((float) p.min, (float) p.max, p.step > 0 ? (float) p.step : 0.0f);
            l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 1}, name, r, (float) p.def));
        }
    }
    // transport and pattern select (not engine params)
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"syncHost", 1}, "Sync to host", true));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"run", 1}, "Start/Stop", false));
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"tempo", 1}, "Tempo", juce::NormalisableRange<float>(40.f, 300.f, 0.1f), 120.f));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"variation", 1}, "Basic variation", juce::StringArray{"A", "AB", "B"}, 0));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"ifVariation", 1}, "I/F variation", juce::StringArray{"A", "B"}, 0));
    return l;
}

EightProcessor::EightProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Omega808", layout()) {
    for (auto& p : params()) raw.push_back({p.k, apvts.getRawParameterValue(paramId(p.k))});
    last.assign(raw.size(), -1e9f);
    pSync = apvts.getRawParameterValue("syncHost"); pRun = apvts.getRawParameterValue("run"); pTempo = apvts.getRawParameterValue("tempo");
    pVar = apvts.getRawParameterValue("variation"); pIfVar = apvts.getRawParameterValue("ifVariation");
    formats.registerBasicFormats();
    offs.reserve(256);
    // start on the first bank, as the app does
    if (kPresets > 0) applyPresetJson(juce::CharPointer_UTF8(PRESETS[0].json));
}

bool EightProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void EightProcessor::prepareToPlay(double rate, int) {
    sr = rate;
    m = std::make_unique<Machine>(rate);
    std::fill(last.begin(), last.end(), -1e9f);
    pushParams(true);
    m->seq.setBank(bank);
    lastVar = lastIfVar = lastBasic = -1;
    applySelect();
    {
        const juce::ScopedLock l(sampleLock);
        for (auto& s : samples) m->eng.loadSample(s.first.toStdString(), s.second.data.data(), (int) s.second.data.size(), s.second.sr);
    }
    hostWasPlaying = false; lastPpq = -1; lastBpm = 0;
}

// changed parameters go to the engine at the start of each block
void EightProcessor::pushParams(bool all) {
    for (size_t i = 0; i < raw.size(); i++) {
        const float v = raw[i].second->load();
        if (all || v != last[i]) {
            last[i] = v;
            const auto& p = params()[i];
            m->eng.set(raw[i].first, p.opts.empty() ? (double) v : p.min + v);   // a choice's raw value is its index
        }
    }
}

void EightProcessor::applySelect() {
    const int var = (int) pVar->load(), ifv = (int) pIfVar->load(), b = basic.load();
    if (var == lastVar && ifv == lastIfVar && b == lastBasic) return;
    lastVar = var; lastIfVar = ifv; lastBasic = b;
    static const char* V[] = {"A", "AB", "B"};
    m->seq.select(b, 0, V[juce::jlimit(0, 2, var)], ifv ? "B" : "A");
}

void EightProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    buf.clear();
    if (!m) return;
    pushParams(false);
    applySelect();

    // transport: the host's, or our own START/STOP and TEMPO
    double bpm = pTempo->load();
    bool play = pRun->load() > 0.5f;
    if (pSync->load() > 0.5f) {
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) bpm = *b;
            play = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) {
                const bool jumped = lastPpq >= 0 && (*ppq < lastPpq - 1e-6 || *ppq > lastPpq + 1.0);   // loop or locate
                if (play && (!hostWasPlaying || jumped)) m->seq.setPositionBeats(*ppq);
                lastPpq = *ppq + n / sr * bpm / 60.0;
            }
        }
        hostWasPlaying = play;
    }
    if (bpm != lastBpm) { lastBpm = bpm; m->seq.setTempo(bpm); m->eng.set("tempo", bpm); tempoShown = bpm; }
    if (play != m->seq.playing()) { m->seq.play(play); if (!play) runStep = -1; }
    running = play;

    // render in pieces split at incoming MIDI, so hits land on their sample
    juce::MidiBuffer out;
    float* L = buf.getWritePointer(0);
    float* R = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
    std::vector<float> scratch; if (!R) { scratch.assign((size_t) n, 0.f); R = scratch.data(); }

    int pos = 0;
    auto render = [&](int upTo) {
        if (upTo <= pos) return;
        const int base = pos;
        m->process(L + pos, R + pos, upTo - pos, [&](int off, const StepInfo& s) {
            runStep = s.step; runRhythm = s.rhythm;
            const int at = juce::jlimit(0, n - 1, base + off), len = (int) (sr * 0.05);
            for (auto& h : s.hits) {
                const int note = voiceNote(h); if (note < 0) continue;
                out.addEvent(juce::MidiMessage::noteOn(kOutCh, note, (juce::uint8) (s.acc ? 127 : 90)), at);
                if (offs.size() < 250) offs.push_back({at + len, note});
            }
            if (s.trig) {
                out.addEvent(juce::MidiMessage::noteOn(kOutCh, kTrigNote, (juce::uint8) 127), at);
                if (offs.size() < 250) offs.push_back({at + (int) (sr * 0.02), kTrigNote});   // 20 ms, like the jack's pulse
            }
        });
        pos = upTo;
    };
    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        render(juce::jlimit(0, n, meta.samplePosition));
        if (msg.isNoteOn()) {
            if (auto* v = noteVoice(msg.getNoteNumber())) m->eng.trigger(v, msg.getVelocity() >= 100 ? 1 : 0, msg.getVelocity() / 127.0);
        } else if (msg.isProgramChange()) {
            basic = msg.getProgramChangeNumber() % 12; applySelect();
        } else if (msg.isController() && msg.getControllerNumber() == 120) {
            // all sound off: nothing sustains long on an 808, so nothing to cut
        }
    }
    render(n);
    // note-offs due in this block (positions are relative to the block start; later ones carry over)
    for (auto& o : offs) if (o.left < n) out.addEvent(juce::MidiMessage::noteOff(kOutCh, o.note), o.left);
    offs.erase(std::remove_if(offs.begin(), offs.end(), [n](const Off& o) { return o.left < n; }), offs.end());
    for (auto& o : offs) o.left -= n;
    if (buf.getNumChannels() == 1) for (int i = 0; i < n; i++) L[i] = 0.5f * (L[i] + R[i]);
    midi.swapWith(out);
}

/* ---------------- editor helpers ---------------- */
void EightProcessor::setParam(const juce::String& key, float value) {
    if (auto* p = apvts.getParameter(paramId(key))) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture();
    }
}
float EightProcessor::getParam(const juce::String& key) const {
    if (auto* v = apvts.getRawParameterValue(paramId(key))) return v->load();
    return 0.f;
}

static Pattern& editPat(Bank& b, int basicIdx, int var) { return var == 2 ? b.basic[basicIdx].B : b.basic[basicIdx].A; }

int EightProcessor::editLength() {
    const juce::ScopedLock l(getCallbackLock());
    auto& p = editPat(bank, basic.load(), (int) pVar->load());
    return juce::jlimit(1, 32, p.len * p.parts);
}
bool EightProcessor::getStep(int t, int s) {
    const juce::ScopedLock l(getCallbackLock());
    auto& p = editPat(bank, basic.load(), (int) pVar->load());
    auto it = p.steps.find(TRACKS[t]);
    const int off = mode.load() == 1 ? p.len : 0;
    return it != p.steps.end() && s + off < (int) it->second.size() && it->second[(size_t) (s + off)] == 'x';
}
void EightProcessor::toggleStep(int t, int s) {
    const juce::ScopedLock l(getCallbackLock());
    auto& p = editPat(bank, basic.load(), (int) pVar->load());
    if (mode.load() == 1 && p.parts < 2) p.parts = 2;
    const int total = p.len * p.parts, i = s + (mode.load() == 1 ? p.len : 0);
    if (i >= total) return;
    auto& str = p.steps[TRACKS[t]];
    if ((int) str.size() < total) str.resize((size_t) total, '.');
    str[(size_t) i] = str[(size_t) i] == 'x' ? '.' : 'x';
    if (m) m->seq.setBank(bank);
}
void EightProcessor::selectRhythm(int b) { basic = juce::jlimit(0, 11, b); }
void EightProcessor::hit(const juce::String& v, bool acc) {
    const juce::ScopedLock l(getCallbackLock());
    if (m) m->eng.trigger(v.toStdString(), acc ? 1 : 0, acc ? 1.0 : 0.8);
}
juce::String EightProcessor::lcdText() const {
    static const char* M[] = {"WRITE 1ST", "WRITE 2ND", "", "PLAY"};
    const int md = mode.load();
    juce::String s = juce::String(M[juce::jlimit(0, 3, md)]) + "  R" + juce::String(basic.load() + 1).paddedLeft('0', 2)
        + juce::String(" ABB").substring((int) pVar->load() + 1, (int) pVar->load() + 2);
    if (md <= 1) s << "  " << TRACKS[track.load()];
    s << "\n" << juce::String(tempoShown.load(), 1) << " BPM  " << (running ? "RUN" : "STOP") << (pSync->load() > 0.5f ? "  HOST" : "");
    return s;
}

/* ---------------- samples ---------------- */
juce::String EightProcessor::loadSample(const juce::String& voice, const juce::File& f) {
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
    if (!r) return "Can't read " + f.getFileName();
    const int len = (int) juce::jmin<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 30));
    juce::AudioBuffer<float> b((int) r->numChannels, len);
    r->read(&b, 0, len, 0, true, true);
    Sample s; s.name = f.getFileName(); s.sr = r->sampleRate; s.data.assign((size_t) len, 0.f);
    for (int c = 0; c < b.getNumChannels(); c++) for (int i = 0; i < len; i++) s.data[(size_t) i] += b.getSample(c, i) / b.getNumChannels();
    {
        const juce::ScopedLock l(getCallbackLock());
        if (m) m->eng.loadSample(voice.toStdString(), s.data.data(), len, s.sr);
    }
    const juce::ScopedLock l(sampleLock);
    samples[voice] = std::move(s);
    return {};
}
void EightProcessor::clearSample(const juce::String& voice) {
    { const juce::ScopedLock l(getCallbackLock()); if (m) m->eng.clearSample(voice.toStdString()); }
    const juce::ScopedLock l(sampleLock); samples.erase(voice);
}
juce::String EightProcessor::sampleName(const juce::String& voice) const {
    const juce::ScopedLock l(sampleLock); auto i = samples.find(voice); return i == samples.end() ? juce::String() : i->second.name;
}

/* ---------------- bank <-> JSON (the app's format) ---------------- */
static juce::var patToVar(const Pattern& p) {
    auto* o = new juce::DynamicObject(); o->setProperty("scale", p.scale); o->setProperty("len", p.len); o->setProperty("parts", p.parts);
    auto* st = new juce::DynamicObject(); for (auto& kv : p.steps) st->setProperty(juce::Identifier(kv.first), juce::String(kv.second));
    o->setProperty("steps", juce::var(st)); return juce::var(o);
}
static Pattern patFromVar(const juce::var& v) {
    Pattern p;
    if (!v.isObject()) return p;
    p.scale = juce::jlimit(0, 3, (int) v.getProperty("scale", 0)); p.len = juce::jlimit(1, 16, (int) v.getProperty("len", 16));
    p.parts = juce::jlimit(1, 2, (int) v.getProperty("parts", 1));
    if (auto* st = v.getProperty("steps", {}).getDynamicObject())
        for (auto& kv : st->getProperties()) p.steps[kv.name.toString().toStdString()] = kv.value.toString().toStdString();
    return p;
}
juce::var EightProcessor::bankToVar() const {
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> bs, fs;
    for (auto& r : bank.basic) { auto* x = new juce::DynamicObject(); x->setProperty("A", patToVar(r.A)); x->setProperty("B", patToVar(r.B)); bs.add(juce::var(x)); }
    for (auto& r : bank.fill) { auto* x = new juce::DynamicObject(); x->setProperty("A", patToVar(r.A)); x->setProperty("B", patToVar(r.B)); fs.add(juce::var(x)); }
    o->setProperty("basic", bs); o->setProperty("fill", fs); o->setProperty("tempo", bank.tempo); o->setProperty("swing", bank.swing); o->setProperty("fillEvery", bank.fillEvery);
    return juce::var(o);
}
void EightProcessor::bankFromVar(const juce::var& v) {
    if (!v.isObject()) return;
    Bank b;
    if (auto* a = v.getProperty("basic", {}).getArray()) for (int i = 0; i < juce::jmin(12, a->size()); i++) {
        const auto& r = (*a)[i]; b.basic[i].A = patFromVar(r.getProperty("A", {})); b.basic[i].B = patFromVar(r.getProperty("B", r.getProperty("A", {})));
    }
    if (auto* a = v.getProperty("fill", {}).getArray()) for (int i = 0; i < juce::jmin(4, a->size()); i++) {
        const auto& r = (*a)[i]; b.fill[i].A = patFromVar(r.getProperty("A", {})); b.fill[i].B = patFromVar(r.getProperty("B", r.getProperty("A", {})));
    }
    b.tempo = (double) v.getProperty("tempo", 120.0); b.swing = (double) v.getProperty("swing", 0.0); b.fillEvery = (int) v.getProperty("fillEvery", 0);
    const juce::ScopedLock l(getCallbackLock());
    bank = b;
    if (m) m->seq.setBank(bank);
}

/* ---------------- programs: the app's banks and kits ---------------- */
int EightProcessor::getNumPrograms() { return juce::jmax(1, kPresets); }
const juce::String EightProcessor::getProgramName(int i) { return i >= 0 && i < kPresets ? juce::String(juce::CharPointer_UTF8(PRESETS[i].name)) : juce::String(); }
void EightProcessor::setCurrentProgram(int i) {
    if (i < 0 || i >= kPresets) return;
    program = i;
    applyPresetJson(juce::CharPointer_UTF8(PRESETS[i].json));
}
void EightProcessor::applyPresetJson(const juce::String& json) {
    const auto v = juce::JSON::parse(json);
    if (auto* ps = v.getProperty("params", {}).getDynamicObject())
        for (auto& kv : ps->getProperties()) setParam(kv.name.toString(), (float) (double) kv.value);
    if (v.hasProperty("bank")) {
        bankFromVar(v["bank"]);
        const double t = (double) v.getProperty("tempo", 0.0);
        if (t > 0) setParam("tempo", (float) t);   // the "tempo" id has no dot, so paramId leaves it as it is
        else if (bank.tempo > 0) setParam("tempo", (float) bank.tempo);
    }
}

/* ---------------- state ---------------- */
void EightProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto* o = new juce::DynamicObject();
    auto* ps = new juce::DynamicObject();
    for (auto* p : getParameters()) if (auto* wp = dynamic_cast<juce::RangedAudioParameter*>(p))
        ps->setProperty(wp->getParameterID(), wp->convertFrom0to1(wp->getValue()));
    o->setProperty("params", juce::var(ps));
    { const juce::ScopedLock l(getCallbackLock()); o->setProperty("bank", bankToVar()); }
    o->setProperty("basic", basic.load()); o->setProperty("track", track.load()); o->setProperty("mode", mode.load()); o->setProperty("program", program);
    auto* ss = new juce::DynamicObject();
    {
        const juce::ScopedLock l(sampleLock);
        for (auto& s : samples) {
            auto* x = new juce::DynamicObject(); x->setProperty("name", s.second.name); x->setProperty("sr", s.second.sr);
            juce::MemoryBlock mb(s.second.data.data(), s.second.data.size() * sizeof(float));
            x->setProperty("data", mb.toBase64Encoding()); ss->setProperty(s.first, juce::var(x));
        }
    }
    o->setProperty("samples", juce::var(ss));
    const auto text = juce::JSON::toString(juce::var(o), true);
    dest.replaceAll(text.toRawUTF8(), text.getNumBytesAsUTF8());
}
void EightProcessor::setStateInformation(const void* data, int size) {
    const auto v = juce::JSON::parse(juce::String::fromUTF8((const char*) data, size));
    if (!v.isObject()) return;
    if (auto* ps = v.getProperty("params", {}).getDynamicObject())
        for (auto& kv : ps->getProperties())
            if (auto* p = apvts.getParameter(kv.name.toString())) p->setValueNotifyingHost(p->convertTo0to1((float) (double) kv.value));
    bankFromVar(v.getProperty("bank", {}));
    basic = juce::jlimit(0, 11, (int) v.getProperty("basic", 0)); track = juce::jlimit(0, kTracks - 1, (int) v.getProperty("track", 1));
    mode = (int) v.getProperty("mode", 3); program = (int) v.getProperty("program", 0);
    if (auto* ss = v.getProperty("samples", {}).getDynamicObject()) {
        for (auto& kv : ss->getProperties()) {
            Sample s; s.name = kv.value.getProperty("name", "").toString(); s.sr = (double) kv.value.getProperty("sr", 44100.0);
            juce::MemoryBlock mb; mb.fromBase64Encoding(kv.value.getProperty("data", "").toString());
            s.data.assign((const float*) mb.getData(), (const float*) mb.getData() + mb.getSize() / sizeof(float));
            const auto voice = kv.name.toString();
            { const juce::ScopedLock l(getCallbackLock()); if (m) m->eng.loadSample(voice.toStdString(), s.data.data(), (int) s.data.size(), s.sr); }
            const juce::ScopedLock l(sampleLock); samples[voice] = std::move(s);
        }
    }
    lastBasic = -1;
}

juce::AudioProcessorEditor* EightProcessor::createEditor() { return new EightEditor(*this); }

}  // namespace eight

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new eight::EightProcessor(); }
