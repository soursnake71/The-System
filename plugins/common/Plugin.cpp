#include "Plugin.h"
#include "Editor.h"

namespace omega {

juce::AudioProcessor::BusesProperties Processor::busesFor(const Spec& s) {
    BusesProperties b;
    if (!s.synth) b = b.withInput("Input", juce::AudioChannelSet::stereo(), true);
    b = b.withOutput("Output", juce::AudioChannelSet::stereo(), true);
    if (s.sidechain) b = b.withInput("Sidechain", juce::AudioChannelSet::stereo(), false);
    return b;
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::layoutFor(const Spec& s) {
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    for (const auto& p : s.params) {
        const juce::ParameterID id { p.id, 1 };
        if (!p.choices.empty()) {
            juce::StringArray names; for (const auto& c : p.choices) names.add(c.label);
            L.add(std::make_unique<juce::AudioParameterChoice>(id, p.name, names, p.defChoice));
        } else if (p.toggle) {
            L.add(std::make_unique<juce::AudioParameterBool>(id, p.name, p.def >= .5f));
        } else {
            const ParamDef def = p;
            auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([def](float v, int) { return juce::String(formatValue(def, v)); });
            L.add(std::make_unique<juce::AudioParameterFloat>(id, p.name, juce::NormalisableRange<float>(p.min, p.max, p.step), p.def, attrs));
        }
    }
    return L;
}

Processor::Processor()
    : AudioProcessor(busesFor(makeSpec())), sp(makeSpec()), apvts(*this, nullptr, "PARAMS", layoutFor(sp)) {
    for (const auto& p : sp.params) { plist.push_back(apvts.getParameter(p.id)); apvts.addParameterListener(p.id, this); }
    vals.assign(sp.params.size(), 0.0);
    text = sp.defaultText;
    engine = sp.make(sp);
    pushValues();
    if (sp.hasText) engine->setText(text.toStdString());
}

Processor::~Processor() { for (const auto& p : sp.params) apvts.removeParameterListener(p.id, this); }

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (!sp.synth) { const auto in = l.getMainInputChannelSet(); if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false; }
    return true;
}

void Processor::pushValues() {
    for (size_t i = 0; i < sp.params.size(); ++i) {
        const auto& p = sp.params[i];
        const float raw = plist[i]->convertFrom0to1(plist[i]->getValue());
        vals[i] = p.choices.empty() ? (double) raw : p.choices[(size_t) juce::jlimit(0, (int) p.choices.size() - 1, (int) std::lround(raw))].value;
    }
    engine->setValues(vals);
}

void Processor::prepareToPlay(double sampleRate, int maxBlock) {
    inBuf.setSize(2, maxBlock); scBuf.setSize(2, maxBlock); outBuf.setSize(2, maxBlock);
    engine->prepare(sampleRate, maxBlock);
    pushValues();
}

void Processor::setText(const juce::String& t) {
    { const juce::ScopedLock l(textLock); text = t; }
    engine->setText(t.toStdString());   // engines render off the audio thread and swap in safely
}

void Processor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    if (dirty.exchange(false)) pushValues();
    Transport tr;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        if (auto b = pos->getBpm()) tr.bpm = *b;
        if (auto q = pos->getPpqPosition()) tr.ppq = *q;
        if (auto s = pos->getTimeInSeconds()) tr.timeSec = *s;
        tr.playing = pos->getIsPlaying();
    }
    const int n = buf.getNumSamples();
    auto mainIn = getBusBuffer(buf, true, 0);
    auto mainOut = getBusBuffer(buf, false, 0);
    // copy inputs first: in and out share channels
    auto& in = inBuf; auto& sc = scBuf; auto& out = outBuf;
    in.setSize(2, n, false, false, true); sc.setSize(2, n, false, false, true); out.setSize(2, n, false, false, true);
    bool haveIn = false, haveSc = false;
    if (!sp.synth && getBusCount(true) > 0 && mainIn.getNumChannels() > 0) {
        in.copyFrom(0, 0, mainIn, 0, 0, n); in.copyFrom(1, 0, mainIn, juce::jmin(1, mainIn.getNumChannels() - 1), 0, n); haveIn = true;
    }
    if (sp.sidechain && getBusCount(true) > (sp.synth ? 0 : 1)) {
        auto s = getBusBuffer(buf, true, sp.synth ? 0 : 1);
        if (s.getNumChannels() > 0 && getBus(true, sp.synth ? 0 : 1)->isEnabled()) {
            sc.copyFrom(0, 0, s, 0, 0, n); sc.copyFrom(1, 0, s, juce::jmin(1, s.getNumChannels() - 1), 0, n); haveSc = true;
        }
    }
    out.clear();
    int pos = 0;
    auto run = [&](int upto) {
        if (upto <= pos) return;
        const int len = upto - pos;
        engine->process(haveIn ? in.getReadPointer(0, pos) : nullptr, haveIn ? in.getReadPointer(1, pos) : nullptr,
                        haveSc ? sc.getReadPointer(0, pos) : nullptr, haveSc ? sc.getReadPointer(1, pos) : nullptr,
                        out.getWritePointer(0, pos), out.getWritePointer(1, pos), len, tr);
        tr.timeSec += len / getSampleRate(); tr.ppq += len / getSampleRate() * tr.bpm / 60.0;
        pos = upto;
    };
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        run(juce::jlimit(0, n, meta.samplePosition));
        if (m.isNoteOn()) engine->noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine->noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine->allNotesOff();
        else if (m.isPitchWheel()) engine->pitchBend((m.getPitchWheelValue() - 8192) / 8192.0 * 2.0);
        else if (m.isController() && m.getControllerNumber() == 1) engine->modWheel(m.getControllerValue() / 127.0);
    }
    run(n);
    for (int c = 0; c < mainOut.getNumChannels(); ++c) mainOut.copyFrom(c, 0, out, juce::jmin(c, 1), 0, n);
    for (int c = getTotalNumOutputChannels(); c < buf.getNumChannels(); ++c) buf.clear(c, 0, n);
}

void Processor::setCurrentProgram(int i) {
    if (i < 0 || i >= (int) sp.presets.size()) return;
    program = i;
    // a preset sets its own values over the defaults, like the app does
    for (size_t k = 0; k < sp.params.size(); ++k) {
        const auto& p = sp.params[k]; double v = p.choices.empty() ? p.def : p.defChoice;
        for (const auto& [id, x] : sp.presets[(size_t) i].values) if (id == p.id) {
            if (p.choices.empty()) v = x;
            else { v = p.defChoice; for (size_t c = 0; c < p.choices.size(); ++c) if (std::fabs(p.choices[c].value - x) < 1e-9) v = (double) c; }
        }
        plist[k]->setValueNotifyingHost(plist[k]->convertTo0to1((float) v));
    }
    if (sp.hasText && !sp.presets[(size_t) i].text.empty()) setText(sp.presets[(size_t) i].text);
    dirty = true;
}

void Processor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = apvts.copyState();
    st.setProperty("text", getText(), nullptr);
    st.setProperty("program", program, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
}

void Processor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        auto st = juce::ValueTree::fromXml(*xml);
        if (!st.isValid()) return;
        if (st.hasProperty("text")) setText(st["text"].toString());
        program = (int) st.getProperty("program", 0);
        apvts.replaceState(st);
        dirty = true;
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

}  // namespace omega

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new omega::Processor(); }
