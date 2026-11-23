#include "FlangerPlugin.h"
#include "FlangerEditor.h"

namespace flanger {

const Preset kPresets[] = {
#include "Presets.inc"
};
const int kNumPresets = (int) (sizeof kPresets / sizeof kPresets[0]);

juce::String Processor::formatValue(int i, float v) {
    switch (i) {
        case m117::bypass: return v >= .5f ? "Bypassed" : "On";
        case m117::mix: return juce::String(juce::roundToInt(v)) + "%";
        case m117::spread: return juce::String(juce::roundToInt(v)) + juce::String::fromUTF8("\xc2\xb0");
        case m117::level: return (v > 0 ? "+" : "") + juce::String(v, 1) + " dB";
        default: return juce::String(v, 1);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    for (int i = 0; i < m117::kNumParams; ++i) {
        const auto& p = m117::kParams[i];
        const juce::ParameterID id { p.id, 1 };
        if (i == m117::bypass) { L.add(std::make_unique<juce::AudioParameterBool>(id, p.name, false)); continue; }
        auto attrs = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([i](float v, int) { return formatValue(i, v); })
            .withLabel(p.unit);
        L.add(std::make_unique<juce::AudioParameterFloat>(id, p.name, juce::NormalisableRange<float>((float) p.min, (float) p.max), (float) p.def, attrs));
    }
    return L;
}

Processor::Processor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", layout()) {
    for (int i = 0; i < m117::kNumParams; ++i) {
        params.push_back(apvts.getParameter(m117::kParams[i].id));
        raw.push_back(apvts.getRawParameterValue(m117::kParams[i].id));
    }
}

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const auto ok = [](const juce::AudioChannelSet& s) { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    return ok(in) && ok(out);
}

void Processor::prepareToPlay(double sampleRate, int maxBlock) {
    scratch.setSize(1, juce::jmax(1, maxBlock));
    for (int i = 0; i < m117::kNumParams; ++i) engine.set(i, raw[(size_t) i]->load());
    engine.prepare(sampleRate);
}

void Processor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    midi.clear();
    for (int i = 0; i < m117::kNumParams; ++i) engine.set(i, raw[(size_t) i]->load());
    const int n = buf.getNumSamples(), ins = getTotalNumInputChannels(), outs = getTotalNumOutputChannels();
    if (ins == 0 || outs == 0 || n == 0) return;
    float* l = buf.getWritePointer(0);
    float* r = outs > 1 ? buf.getWritePointer(1) : nullptr;
    // mono in, stereo out: both sides start from the one input
    if (r && ins == 1) juce::FloatVectorOperations::copy(r, l, n);
    const float* inR = ins > 1 ? buf.getReadPointer(1) : l;
    for (int pos = 0; pos < n;) {
        const int len = juce::jmin(n - pos, scratch.getNumSamples());
        float* outR = r ? r + pos : scratch.getWritePointer(0);
        engine.process(l + pos, (ins > 1 ? inR : (r ? r : l)) + pos, l + pos, outR, len);
        pos += len;
    }
    for (int c = outs; c < buf.getNumChannels(); ++c) buf.clear(c, 0, n);
}

void Processor::setCurrentProgram(int i) {
    if (i < 0 || i >= kNumPresets) return;
    program = i;
    // the footswitch stays where it is: presets set the knobs
    for (int k = 0; k < m117::kNumParams; ++k) {
        if (k == m117::bypass) continue;
        auto* p = params[(size_t) k];
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1((float) kPresets[i].v[k]));
        p->endChangeGesture();
    }
}

const juce::String Processor::getProgramName(int i) {
    return i >= 0 && i < kNumPresets ? juce::String::fromUTF8(kPresets[i].name) : juce::String();
}

void Processor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = apvts.copyState();
    st.setProperty("program", program, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
}

void Processor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        auto st = juce::ValueTree::fromXml(*xml);
        if (!st.isValid() || !st.hasType(apvts.state.getType())) return;
        program = juce::jlimit(0, kNumPresets - 1, (int) st.getProperty("program", 0));
        apvts.replaceState(st);
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

}  // namespace flanger

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new flanger::Processor(); }
