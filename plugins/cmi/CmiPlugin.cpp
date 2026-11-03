#include "CmiPlugin.h"
#include "CmiEditor.h"

namespace cmi {

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (const auto& d : paramDefs()) {
        const juce::String id(d.id);
        if (d.step == 1 && d.min == 0 && d.max == 1)
            l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(id, 1), d.name, d.def > 0.5f));
        else if (d.step == 1)
            l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID(id, 1), d.name, (int) d.min, (int) d.max, (int) d.def));
        else
            l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1), d.name, juce::NormalisableRange<float>(d.min, d.max), d.def));
    }
    return l;
}

CmiProcessor::CmiProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "CMI", makeLayout()) {
    formats.registerBasicFormats();
    for (const auto& d : paramDefs()) raw.push_back(apvts.getRawParameterValue(d.id));
    engine.bank = &kit.bank;
    Library::factorySeq(engine.seq, Library::get());
    loadSound(Library::get().find("ORCHHIT"));
    startTimerHz(20);
}

bool CmiProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void CmiProcessor::prepareToPlay(double sr, int maxBlock) {
    engine.prepare(sr);
    bufL.assign((size_t) std::max(maxBlock, 512), 0.f); bufR = bufL;
    setLatencySamples(BL_HALF);
}

Controls CmiProcessor::controlsNow() const {
    Controls c;
    c.level = get("level"); c.filter = get("filter"); c.attack = get("attack"); c.damp = get("damp"); c.sustain = get("sustain");
    c.vibDepth = get("vib"); c.vibRate = get("vibRate"); c.vibDelay = get("vibDelay"); c.porta = get("porta");
    c.velVol = get("velVol"); c.velFilt = get("velFilt"); c.transpose = get("transpose"); c.fine = get("fine");
    c.loop = get("loop"); c.segTime = get("seg");
    return c;
}

void CmiProcessor::set(const char* id, float v) {
    if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
}

void CmiProcessor::loadSound(int index) {
    const Sound* s = kit.bank.at(index);
    if (!s) return;
    const Controls& c = s->ctl;
    set("sound", (float) index);
    set("level", c.level); set("filter", c.filter); set("attack", c.attack); set("damp", c.damp); set("sustain", c.sustain);
    set("vib", c.vibDepth); set("vibRate", c.vibRate); set("vibDelay", c.vibDelay); set("porta", c.porta);
    set("velVol", c.velVol); set("velFilt", c.velFilt); set("transpose", c.transpose); set("fine", c.fine);
    set("loop", c.loop); set("seg", c.segTime);
    if (index < Library::get().builtIn) program = index;
}

void CmiProcessor::setCurrentProgram(int i) { if (i >= 0 && i < Library::get().builtIn) loadSound(i); }

const juce::String CmiProcessor::getProgramName(int i) {
    const Sound* s = Library::get().sound(i);
    return s ? juce::String(s->name) + " - " + juce::String(s->title) : juce::String();
}

juce::String CmiProcessor::loadSample(const juce::File& f, int slot, int root, bool loop) {
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
    if (!r) return "Can't read " + f.getFileName();
    const int64_t maxLen = (int64_t) (r->sampleRate * 8);   // more than 16 KB can hold at the lowest rate
    const int n = (int) std::min<int64_t>(r->lengthInSamples, maxLen);
    if (n <= 0) return "Empty file";
    juce::AudioBuffer<float> b((int) r->numChannels, n);
    r->read(&b, 0, n, 0, true, true);
    std::vector<float> mono((size_t) n, 0.f);
    for (int c = 0; c < b.getNumChannels(); ++c) { auto* p = b.getReadPointer(c); for (int i = 0; i < n; ++i) mono[(size_t) i] += p[i] / (float) b.getNumChannels(); }
    Sound s = sampleToCmi(mono, r->sampleRate, f.getFileNameWithoutExtension().toStdString(), root, loop);
    kit.setUser(slot, std::move(s));
    loadSound(kit.userSlot(slot));
    return {};
}

void CmiProcessor::timerCallback() {
    const int p = pendingProgram.exchange(-1);
    if (p >= 0) setCurrentProgram(p);
}

void CmiProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    keyboard.processNextMidiBuffer(midi, 0, n, true);

    engine.setKeyboard(juce::roundToInt(get("sound")), controlsNow());
    engine.setPoly(juce::roundToInt(get("poly")));
    engine.setBendRange(get("bend"));
    engine.setOutput(get("master"), get("hiss"), get("spread"));
    engine.setMultiTimbral(get("multi") > 0.5f);
    engine.extSync = get("sync") > 0.5f;
    engine.tempo = get("tempo");
    engine.seq.swing = get("swing");
    engine.songMode = get("songMode") > 0.5f;

    const int pv = previewReq.exchange(-1);
    if (pv >= 0) engine.trackNote(pv, 0, 0.9f);

    Transport tp;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            tp.valid = true; tp.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) tp.ppq = *q;
            if (auto b = pos->getBpm()) tp.bpm = *b;
        }
    }

    if ((int) bufL.size() < n) { bufL.resize((size_t) n); bufR.resize((size_t) n); }
    int done = 0;
    const double spq = tp.bpm / 60.0 / getSampleRate();
    auto renderTo = [&](int upTo) {
        if (upTo <= done) return;
        Transport t2 = tp; t2.ppq = tp.ppq + done * spq;
        engine.render(bufL.data() + done, bufR.data() + done, upTo - done, &t2);
        done = upTo;
    };
    for (const auto m : midi) {
        renderTo(juce::jlimit(0, n, m.samplePosition));
        const auto msg = m.getMessage();
        if (msg.isProgramChange()) { pendingProgram = msg.getProgramChangeNumber(); continue; }
        engine.midi(msg.getRawData(), msg.getRawDataSize());
    }
    renderTo(n);
    midi.clear();

    float pk = 0;
    if (buffer.getNumChannels() >= 2) {
        buffer.copyFrom(0, 0, bufL.data(), n); buffer.copyFrom(1, 0, bufR.data(), n);
        for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
    } else if (buffer.getNumChannels() == 1) {
        auto* o = buffer.getWritePointer(0); for (int i = 0; i < n; ++i) o[i] = 0.5f * (bufL[(size_t) i] + bufR[(size_t) i]);
    }
    for (int i = 0; i < n; ++i) pk = std::max(pk, std::abs(bufL[(size_t) i]));
    meter = std::max(pk, meter.load() * 0.9f);
}

// ---- state: parameters, Page R, user samples ----
void CmiProcessor::writeSeq(juce::XmlElement& x) const {
    auto* s = x.createNewChildElement("PageR");
    s->setAttribute("songLen", engine.seq.songLen);
    juce::String song; for (int i = 0; i < engine.seq.songLen; ++i) song << engine.seq.song[i] << " ";
    s->setAttribute("song", song.trim());
    s->setAttribute("pattern", engine.curPattern.load());
    for (int t = 0; t < TRACKS; ++t) {
        auto* tr = s->createNewChildElement("Track");
        const Sound* snd = kit.bank.at(engine.seq.trk[t].sound);
        tr->setAttribute("sound", snd ? juce::String(snd->name) : juce::String());
        tr->setAttribute("index", engine.seq.trk[t].sound);
        tr->setAttribute("level", engine.seq.trk[t].level);
        tr->setAttribute("audible", engine.seq.trk[t].audible);
        tr->setAttribute("pan", engine.seq.trk[t].pan);
    }
    juce::MemoryBlock mb(&engine.seq.pat[0], sizeof(engine.seq.pat));
    s->setAttribute("patterns", mb.toBase64Encoding());
}

void CmiProcessor::readSeq(const juce::XmlElement& x) {
    const auto* s = x.getChildByName("PageR");
    if (!s) return;
    auto& q = engine.seq;
    q.songLen = juce::jlimit(1, SONG_MAX, s->getIntAttribute("songLen", 1));
    auto toks = juce::StringArray::fromTokens(s->getStringAttribute("song"), " ", "");
    for (int i = 0; i < q.songLen; ++i) q.song[i] = juce::jlimit(0, NPAT - 1, i < toks.size() ? toks[i].getIntValue() : 0);
    engine.curPattern = juce::jlimit(0, NPAT - 1, s->getIntAttribute("pattern", 0));
    int t = 0;
    for (auto* tr : s->getChildWithTagNameIterator("Track")) {
        if (t >= TRACKS) break;
        int idx = Library::get().find(tr->getStringAttribute("sound").toStdString());
        if (idx < 0) idx = juce::jlimit(0, kit.bank.count.load() - 1, tr->getIntAttribute("index", 0));
        q.trk[t].sound = idx;
        q.trk[t].level = (float) tr->getDoubleAttribute("level", 0.7);
        q.trk[t].audible = tr->getBoolAttribute("audible", true);
        q.trk[t].pan = (float) tr->getDoubleAttribute("pan", 0.5);
        ++t;
    }
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding(s->getStringAttribute("patterns")) && mb.getSize() == sizeof(q.pat)) std::memcpy(&q.pat[0], mb.getData(), sizeof(q.pat));
}

void CmiProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = apvts.copyState();
    std::unique_ptr<juce::XmlElement> x(st.createXml());
    x->setAttribute("program", program);
    const Sound* cur = kit.bank.at(juce::roundToInt(get("sound")));
    if (cur) x->setAttribute("soundName", juce::String(cur->name));
    writeSeq(*x);
    for (int k = 0; k < Library::USER_SLOTS; ++k) {
        if (!kit.userLoaded(k)) continue;
        const Sound* s = kit.user(k);
        auto* u = x->createNewChildElement("User");
        u->setAttribute("slot", k); u->setAttribute("name", juce::String(s->name)); u->setAttribute("title", juce::String(s->title));
        u->setAttribute("rate", s->rate); u->setAttribute("root", s->root);
        u->setAttribute("loopStart", s->loopStart); u->setAttribute("loopEnd", s->loopEnd);
        u->setAttribute("ram", juce::MemoryBlock(s->ram.data(), s->ram.size()).toBase64Encoding());
    }
    copyXmlToBinary(*x, dest);
}

void CmiProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(data, size));
    if (!x) return;
    for (auto* u : x->getChildWithTagNameIterator("User")) {
        Sound s; s.name = u->getStringAttribute("name").toStdString(); s.title = u->getStringAttribute("title").toStdString(); s.category = "User";
        s.rate = u->getDoubleAttribute("rate", 20000); s.root = u->getIntAttribute("root", 60);
        s.loopStart = u->getIntAttribute("loopStart", -1); s.loopEnd = u->getIntAttribute("loopEnd", -1);
        juce::MemoryBlock mb; mb.fromBase64Encoding(u->getStringAttribute("ram"));
        const size_t n = std::min<size_t>((size_t) RAM_SIZE, mb.getSize() / PAGE * PAGE);
        if (n == 0) continue;
        s.ram.assign((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + n);
        kit.setUser(u->getIntAttribute("slot"), std::move(s));
    }
    readSeq(*x);
    program = x->getIntAttribute("program", 0);
    const juce::String sn = x->getStringAttribute("soundName");
    if (x->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*x));
    // the library may have grown: find the sound by name
    if (sn.isNotEmpty()) {
        int idx = -1;
        for (int i = 0; i < kit.bank.count.load(); ++i) if (const Sound* s = kit.bank.at(i)) if (juce::String(s->name) == sn) { idx = i; break; }
        if (idx >= 0) set("sound", (float) idx);
    }
}

}  // namespace cmi

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new cmi::CmiProcessor(); }
