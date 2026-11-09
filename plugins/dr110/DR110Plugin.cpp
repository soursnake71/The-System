#include "DR110Plugin.h"
#include "DR110Editor.h"
#include "BinaryData.h"
#include <map>

namespace dr110 {

static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const auto ps = params();
    for (int i = 0; i < NP; ++i) {
        const auto& p = ps[(size_t) i];
        if (p.id == "tempo") continue;                 // the DAW sets the tempo
        auto ends = [&](const char* e) { const std::string x(e); return p.id.size() >= x.size() && p.id.compare(p.id.size() - x.size(), x.size(), x) == 0; };
        const bool toggle = p.id == "src" || ends("mute") || p.id.find("Boost") != std::string::npos
            || p.id == "bdBass" || p.id == "swTarget" || p.id == "supply" || p.id == "bdEnv" || p.id == "dlPing";
        const float step = toggle || p.id == "rvProg" || p.id == "odMode" || p.id == "dlSync" ? 1.0f : 0.0f;
        juce::NormalisableRange<float> r((float) p.min, (float) p.max, step);
        // log-like knobs for frequencies, times and Q (centre = the geometric middle)
        const bool logish = p.id == "rvTime" || p.id == "rvCut" || ends("dec") || ends("F") || ends("Q") || ends("Cut") || p.id == "dlTime" || p.id == "dlModRate"
            || ends("dcy") || ends("rel");
        if (logish && p.min > 0) r.setSkewForCentre((float) std::sqrt(p.min * p.max));
        if (ends("atk")) r.setSkewForCentre(0.08f);
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(p.id, 1), p.name, r, (float) p.def));
    }
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("accVel", 1), "Accent Velocity", juce::NormalisableRange<float>(1, 127, 1), 100.0f));
    return l;
}

static void loadWav(const void* data, int size, std::vector<float>& out, double& sr) {
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatReader> r(fmt.createReaderFor(new juce::MemoryInputStream(data, (size_t) size, false), true));
    if (!r) return;
    juce::AudioBuffer<float> b((int) r->numChannels, (int) r->lengthInSamples);
    r->read(&b, 0, (int) r->lengthInSamples, 0, true, true);
    out.assign((size_t) b.getNumSamples(), 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) out[(size_t) i] += b.getSample(c, i) / (float) b.getNumChannels();
    sr = r->sampleRate;
}

DR110Processor::DR110Processor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Mix", juce::AudioChannelSet::stereo(), true)
          .withOutput("BD", juce::AudioChannelSet::stereo(), false)
          .withOutput("SD", juce::AudioChannelSet::stereo(), false)
          .withOutput("CP", juce::AudioChannelSet::stereo(), false)
          .withOutput("HH/CY", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "DR110", layout()) {
    const auto ps = params();
    for (int i = 0; i < NP; ++i) raw[(size_t) i] = apvts.getRawParameterValue(ps[(size_t) i].id);
    accVel = apvts.getRawParameterValue("accVel");
    cur = defaults();
    // the recordings (Source: Recordings), built into the plugin
    struct W { int v; const char* d; int n; bool acc; };
    const W ws[] = {{0, BinaryData::bd_wav, BinaryData::bd_wavSize, false}, {1, BinaryData::sd_wav, BinaryData::sd_wavSize, false},
        {2, BinaryData::oh_wav, BinaryData::oh_wavSize, false}, {2, BinaryData::ohlong_wav, BinaryData::ohlong_wavSize, true},
        {3, BinaryData::ch_wav, BinaryData::ch_wavSize, false}, {3, BinaryData::chlong_wav, BinaryData::chlong_wavSize, true},
        {4, BinaryData::cy_wav, BinaryData::cy_wavSize, false}, {5, BinaryData::hc_wav, BinaryData::hc_wavSize, false}};
    for (const auto& w : ws) { double sr = 44100; loadWav(w.d, w.n, w.acc ? samples[(size_t) w.v].acc : samples[(size_t) w.v].d, sr); samples[(size_t) w.v].sr = sr; }
    for (auto& g : grid) g = 0;
    for (auto& m : meters) m = 0;
}

bool DR110Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int b = 1; b < l.outputBuses.size(); ++b) {
        const auto& s = l.outputBuses.getReference(b);
        if (!s.isDisabled() && s != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void DR110Processor::prepareToPlay(double sampleRate, int maxBlock) {
    engine = std::make_unique<Engine>(sampleRate);
    for (int v = 0; v < NV; ++v) engine->setSample(v, samples[(size_t) v].d.empty() ? nullptr : &samples[(size_t) v]);
    readParams(); engine->setAll(cur);
    for (auto& s : scratch) s.assign((size_t) std::max(1, maxBlock), 0.0f);
}

void DR110Processor::readParams() {
    Params p = cur;
    for (int i = 0; i < NP; ++i) if (raw[(size_t) i]) p[(size_t) i] = raw[(size_t) i]->load();
    p[pid::tempo] = juce::jlimit(20.0, 999.0, (double) hostBpm.load());     // the DAW's tempo (the delay's sync)
    if (p != cur && engine) { cur = p; engine->setAll(cur); }
    else cur = p;
}

void DR110Processor::markGrid(int v, bool acc, double ppq) {
    if (ppq < 0) return;
    const int step = (int) std::floor(std::fmod(ppq, 4.0) * 4.0 + 0.5) % 16;
    grid[(size_t) step] = grid[(size_t) step].load() | (1 << v) | (acc ? 128 : 0);
}

void DR110Processor::handleMidi(const juce::MidiMessage& m, double ppq) {
    if (m.isNoteOn()) {
        const int n = m.getNoteNumber();
        if (n == accNote) { accHeld = true; engine->sweep(); return; }
        if (n == sweepNote) { engine->sweep(); return; }
        int v = -1;
        for (int i = 0; i < NV; ++i) if (notes[(size_t) i] == n) v = i;
        if (v < 0) {   // other drum maps
            switch (n) { case 35: v = 0; break; case 40: v = 1; break; case 44: v = 3; break; case 51: case 52: case 55: case 57: v = 4; break; case 75: case 76: case 77: v = 6; break; default: break; }
        }
        if (v < 0) return;
        const bool acc = m.getVelocity() >= (int) accVel->load() || accHeld;
        engine->trigger(v, acc); markGrid(v, acc, ppq);
    } else if (m.isNoteOff()) {
        // the end of the note: the pad's ADSR goes to its release
        if (m.getNoteNumber() == accNote) accHeld = false;
        for (int i = 0; i < NV; ++i) if (notes[(size_t) i] == m.getNoteNumber()) engine->release(i);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) accHeld = false;
}

void DR110Processor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    if (!engine) { buf.clear(); return; }
    // where the DAW is: the LCD shows its bar and step
    double ppq0 = -1, bpm = 120; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        if (auto b = pos->getBpm()) bpm = *b;
        if (auto p = pos->getPpqPosition()) ppq0 = *p;
        playing = pos->getIsPlaying();
    }
    hostBpm = (float) bpm; hostPlaying = playing;
    readParams();
    const double ppqPerSample = bpm / 60.0 / getSampleRate();
    if (ppq0 >= 0) {
        const int bar = (int) std::floor(ppq0 / 4.0);
        if (bar != lastBar) { lastBar = bar; for (auto& g : grid) g = 0; }
        hostBar = bar + 1; hostStep = playing ? (int) std::floor(std::fmod(ppq0, 4.0) * 4.0) % 16 : -1;
    }
    // keys pressed on the editor
    for (const int w = qW.load(); qR != w; ++qR) {
        const int e = q[(size_t) (qR & 63)];
        if (e & 32) engine->release(e & 15);
        else if ((e & 15) == 7) engine->sweep(); else { engine->trigger(e & 15, (e & 16) != 0); markGrid(e & 15, (e & 16) != 0, ppq0); }
    }
    // separate outputs (or scratch when a bus is off)
    float* outs[8];
    for (int b = 0; b < 4; ++b) for (int c = 0; c < 2; ++c) {
        auto* bus = getBus(false, b + 1);
        if (bus && bus->isEnabled() && bus->getNumberOfChannels() == 2) outs[b * 2 + c] = getBusBuffer(buf, false, b + 1).getWritePointer(c);
        else { if ((int) scratch[b * 2 + c].size() < n) scratch[b * 2 + c].assign((size_t) n, 0.0f); outs[b * 2 + c] = scratch[b * 2 + c].data(); }
    }
    auto main = getBusBuffer(buf, false, 0);
    float* L = main.getWritePointer(0); float* R = main.getWritePointer(1);
    int pos = 0;
    auto run = [&](int upto) {
        while (pos < upto) {
            const int len = upto - pos;
            float* o[8]; for (int k = 0; k < 8; ++k) o[k] = outs[k] + pos;
            engine->process(L + pos, R + pos, len, o);
            pos += len;
        }
    };
    for (const auto meta : midi) {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        run(at);
        handleMidi(meta.getMessage(), ppq0 >= 0 ? ppq0 + at * ppqPerSample : -1);
    }
    run(n);
    for (int v = 0; v < NV; ++v) { meters[(size_t) v] = std::max(meters[(size_t) v].load() * 0.85f, (float) engine->meter[v]); engine->meter[v] = 0; }
}

void DR110Processor::setValue(int index, double v) {
    if (auto* prm = apvts.getParameter(params()[(size_t) index].id)) prm->setValueNotifyingHost(prm->convertTo0to1((float) v));
}

// a preset sets every sound parameter but TEMPO, VOLUME and SOURCE
static bool kept(int i) { return i == pid::tempo || i == pid::volume || i == pid::src; }
void DR110Processor::setCurrentProgram(int i) {
    if (i < 0 || i >= kPresets) return;
    program = i;
    for (int k = 0; k < NP; ++k) if (!kept(k)) setValue(k, PRESETS[i].p[k]);
    if (onProgram) onProgram();
}

// the dice (dr110/presets.js dice(), the same rules from Presets.inc)
void DR110Processor::dice(int part) {
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

void DR110Processor::getStateInformation(juce::MemoryBlock& mb) {
    auto st = apvts.copyState();
    st.setProperty("program", program, nullptr);
    std::unique_ptr<juce::XmlElement> x(st.createXml());
    copyXmlToBinary(*x, mb);
}
void DR110Processor::setStateInformation(const void* d, int n) {
    std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(d, n));
    if (x && x->hasTagName(apvts.state.getType())) { auto t = juce::ValueTree::fromXml(*x); program = (int) t.getProperty("program", 0); apvts.replaceState(t); }
}

juce::AudioProcessorEditor* DR110Processor::createEditor() { return new DR110Editor(*this); }

} // namespace dr110

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dr110::DR110Processor(); }
