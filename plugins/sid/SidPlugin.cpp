#include "SidPlugin.h"
#include "SidEditor.h"

namespace sid {

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < NP; ++i) {
        const ParamInfo& p = params()[i];
        const juce::ParameterID id { p.key, 1 };
        const juce::String name = juce::String(p.section) + " " + p.name;
        if (p.kind == CHOICE) {
            juce::StringArray c; for (auto& s : choiceList(i)) c.add(s);
            l.add(std::make_unique<juce::AudioParameterChoice>(id, name, c, (int) p.def));
        } else if (p.kind == TOGGLE) {
            l.add(std::make_unique<juce::AudioParameterBool>(id, name, p.def > .5f));
        } else {
            juce::NormalisableRange<float> r(p.min, p.max,
                [i](float, float, float n) { return fromNorm(i, n); },
                [i](float, float, float v) { return toNorm(i, v); },
                [i](float, float, float v) { const ParamInfo& q = params()[i]; v = juce::jlimit(q.min, q.max, v); return q.kind == INT ? std::round(v) : v; });
            l.add(std::make_unique<juce::AudioParameterFloat>(id, name, r, p.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction([i](float v, int) { return juce::String(formatValue(i, v)); })));
        }
    }
    return l;
}

SidProcessor::SidProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", makeLayout()) {
    for (int i = 0; i < NP; ++i) raw[(size_t) i] = apvts.getRawParameterValue(params()[i].key);
    setCurrentProgram(0);
}

bool SidProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void SidProcessor::prepareToPlay(double sampleRate, int maxBlock) {
    for (int i = 0; i < NP; ++i) engine.set(i, raw[(size_t) i]->load());
    engine.prepare(sampleRate);
    tmp.setSize(2, std::max(1, maxBlock));
}

void SidProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    for (int i = 0; i < NP; ++i) engine.set(i, raw[(size_t) i]->load(std::memory_order_relaxed));
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            const double bpm = pos->getBpm().orFallback(0.0);
            engine.hostTransport(pos->getIsPlaying(), pos->getPpqPosition().orFallback(0.0), bpm);
        }
    const int n = buf.getNumSamples();
    if (tmp.getNumSamples() < n) tmp.setSize(2, n, false, false, true);
    float* L = tmp.getWritePointer(0); float* R = tmp.getWritePointer(1);
    // split the block at each MIDI event so notes land on time
    int pos = 0;
    auto renderTo = [&](int upto) { if (upto > pos) { engine.render(L + pos, R + pos, upto - pos); pos = upto; } };
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        renderTo(juce::jlimit(0, n, meta.samplePosition));
        if (m.isNoteOn()) engine.noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine.noteOff(m.getNoteNumber());
        else if (m.isPitchWheel()) engine.setBend((m.getPitchWheelValue() - 8192) / 8192.0);
        else if (m.isSustainPedalOn()) engine.setSustain(true);
        else if (m.isSustainPedalOff()) engine.setSustain(false);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
        else if (m.isController() && m.getControllerNumber() == 1) engine.setModWheel(m.getControllerValue() / 127.0);
        else if (m.isController() && m.getControllerNumber() == 74) engine.setCutoffCC((m.getControllerValue() - 64) / 64.0);
        else if (m.isProgramChange()) { pendingProgram = m.getProgramChangeNumber(); triggerAsyncUpdate(); }
    }
    renderTo(n);
    midi.clear();
    if (buf.getNumChannels() >= 2) { buf.copyFrom(0, 0, L, n); buf.copyFrom(1, 0, R, n); }
    else { buf.copyFrom(0, 0, L, n); buf.addFrom(0, 0, R, n); buf.applyGain(0.5f); }
    for (int c = 2; c < buf.getNumChannels(); ++c) buf.clear(c, 0, n);
}

void SidProcessor::handleAsyncUpdate() {
    const int p = pendingProgram.exchange(-1);
    if (p >= 0 && p < presetCount()) setCurrentProgram(p);
}

void SidProcessor::set(int id, float v) {
    if (auto* p = apvts.getParameter(params()[id].key)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(v));
        p->endChangeGesture();
    }
}

// the preset is worked out on a scratch engine, then sent to the parameters (so the host records it)
void SidProcessor::applyValues(const std::array<float, NP>& v) {
    for (int i = 0; i < NP; ++i) if (params()[i].preset && std::fabs(v[(size_t) i] - get(i)) > 1e-6f) set(i, v[(size_t) i]);
    ++uiPresetVersion;
}
void SidProcessor::setCurrentProgram(int i) {
    if (i < 0 || i >= presetCount()) return;
    auto s = std::make_unique<Synth>();
    for (int k = 0; k < NP; ++k) s->set(k, get(k));
    s->applyPreset(i);
    std::array<float, NP> v {}; for (int k = 0; k < NP; ++k) v[(size_t) k] = s->get(k);
    engine.program = i; engine.presetName = PRESETS[i].name;
    applyValues(v);
}
const juce::String SidProcessor::getProgramName(int i) {
    return i >= 0 && i < presetCount() ? juce::String(PRESETS[i].cat) + ": " + PRESETS[i].name : juce::String();
}
void SidProcessor::initPatch() {
    std::array<float, NP> v {}; for (int k = 0; k < NP; ++k) v[(size_t) k] = params()[k].preset ? params()[k].def : get(k);
    engine.presetName = "INIT"; applyValues(v);
}
void SidProcessor::randomize() {
    auto s = std::make_unique<Synth>();
    for (int k = 0; k < NP; ++k) s->set(k, get(k));
    s->randomize((uint32_t) juce::Random::getSystemRandom().nextInt());
    std::array<float, NP> v {}; for (int k = 0; k < NP; ++k) v[(size_t) k] = s->get(k);
    engine.presetName = "RANDOM"; applyValues(v);
}

void SidProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = apvts.copyState();
    juce::ValueTree x("SID");
    x.setProperty("engine", juce::String(engine.saveState()), nullptr);   // patterns, preset name, program
    st.removeChild(st.getChildWithName("SID"), nullptr);
    st.appendChild(x, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
}
void SidProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size); if (!xml) return;
    auto st = juce::ValueTree::fromXml(*xml); if (!st.isValid()) return;
    auto x = st.getChildWithName("SID");
    if (x.isValid()) {
        engine.loadState(x.getProperty("engine").toString().toStdString());
        st.removeChild(x, nullptr);
    }
    apvts.replaceState(st);
    ++uiPresetVersion;
}

juce::AudioProcessorEditor* SidProcessor::createEditor() { return new SidEditor(*this); }

}  // namespace sid

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new sid::SidProcessor(); }
