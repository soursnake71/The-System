#include "DR110Plugin.h"
#include "DR110Editor.h"
#include "BinaryData.h"

namespace dr110 {

static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const auto ps = params();
    for (int i = 0; i < NP; ++i) {
        const auto& p = ps[(size_t) i];
        if (p.id == "tempo") continue;                 // the DAW sets the tempo
        const bool toggle = p.id == "src" || p.id.find("mute") != std::string::npos || p.id.find("Boost") != std::string::npos
            || p.id == "bdBass" || p.id == "swTarget" || p.id == "supply";
        const float step = toggle || p.id == "rvProg" ? 1.0f : 0.0f;
        juce::NormalisableRange<float> r((float) p.min, (float) p.max, step);
        if (p.id == "rvTime") r.setSkewForCentre(2.0f);
        if (p.id == "rvCut") r.setSkewForCentre(4000.0f);
        if (p.id.size() > 3 && p.id.compare(p.id.size() - 3, 3, "dec") == 0) r.setSkewForCentre(1.0f);
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
    } else if (m.isNoteOff() && m.getNoteNumber() == accNote) accHeld = false;
    else if (m.isAllNotesOff() || m.isAllSoundOff()) accHeld = false;
}

void DR110Processor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    if (!engine) { buf.clear(); return; }
    readParams();
    // where the DAW is: the LCD shows its bar and step
    double ppq0 = -1, bpm = 120; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        if (auto b = pos->getBpm()) bpm = *b;
        if (auto p = pos->getPpqPosition()) ppq0 = *p;
        playing = pos->getIsPlaying();
    }
    hostBpm = (float) bpm; hostPlaying = playing;
    const double ppqPerSample = bpm / 60.0 / getSampleRate();
    if (ppq0 >= 0) {
        const int bar = (int) std::floor(ppq0 / 4.0);
        if (bar != lastBar) { lastBar = bar; for (auto& g : grid) g = 0; }
        hostBar = bar + 1; hostStep = playing ? (int) std::floor(std::fmod(ppq0, 4.0) * 4.0) % 16 : -1;
    }
    // keys pressed on the editor
    for (const int w = qW.load(); qR != w; ++qR) {
        const int e = q[(size_t) (qR & 63)];
        if ((e & 15) == 7) engine->sweep(); else { engine->trigger(e & 15, (e & 16) != 0); markGrid(e & 15, (e & 16) != 0, ppq0); }
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

void DR110Processor::getStateInformation(juce::MemoryBlock& mb) {
    auto st = apvts.copyState();
    std::unique_ptr<juce::XmlElement> x(st.createXml());
    copyXmlToBinary(*x, mb);
}
void DR110Processor::setStateInformation(const void* d, int n) {
    std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(d, n));
    if (x && x->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*x));
}

juce::AudioProcessorEditor* DR110Processor::createEditor() { return new DR110Editor(*this); }

} // namespace dr110

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dr110::DR110Processor(); }
