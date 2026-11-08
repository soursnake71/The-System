#include "SidPlugin.h"
#include "SidEditor.h"
#include <cstring>

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

// an audio input to sample from: on in the standalone app (the microphone / interface), optional in a DAW
static bool loadedStandalone() { return juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone; }

SidProcessor::SidProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                      .withInput("Sample In", juce::AudioChannelSet::stereo(), loadedStandalone())),
      apvts(*this, nullptr, "STATE", makeLayout()) {
    for (int i = 0; i < NP; ++i) raw[(size_t) i] = apvts.getRawParameterValue(params()[i].key);
    formats.registerBasicFormats();
    setCurrentProgram(0);
    engine.pollSample();
    startTimerHz(8);
}

void SidProcessor::loadSampleFile() {
    chooser = std::make_unique<juce::FileChooser>("Sample into the C64", juce::File(), formats.getWildcardForAllFormats());
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) loadSampleFrom(f);
    });
}
juce::String SidProcessor::loadSampleFrom(const juce::File& f) {
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
    if (!r) return "can't read " + f.getFileName();
    const int n = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 60));
    juce::AudioBuffer<float> b((int) r->numChannels, n);
    r->read(&b, 0, n, 0, true, true);
    std::vector<float> mono((size_t) n, 0.f);
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < n; ++i) mono[(size_t) i] += b.getSample(c, i) / (float) b.getNumChannels();
    engine.setSource(std::move(mono), r->sampleRate, f.getFileNameWithoutExtension().toUpperCase().substring(0, 13).toStdString());
    if (get(P::dmode) < .5f) set(P::dmode, DIGI_VOL4);
    engine.pollSample();
    return {};
}

bool SidProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto o = l.getMainOutputChannelSet(), in = l.getMainInputChannelSet();
    const bool outOk = o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
    const bool inOk = in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
    return outOk && inOk;
}

void SidProcessor::prepareToPlay(double sampleRate, int maxBlock) {
    for (int i = 0; i < NP; ++i) engine.set(i, raw[(size_t) i]->load());
    engine.prepare(sampleRate);
    for (auto& v : lastHost) v = std::numeric_limits<float>::quiet_NaN();   // everything is copied on the first block
    tmp.setSize(2, std::max(1, maxBlock));
}

void SidProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    // parameters: only the ones the host changed (so a value set from MIDI holds until the host's copy catches up)
    for (int i = 0; i < NP; ++i) {
        const float v = raw[(size_t) i]->load(std::memory_order_relaxed);
        if (std::memcmp(&v, &lastHost[(size_t) i], sizeof v) != 0) { engine.set(i, v); lastHost[(size_t) i] = v; }
    }
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            const double bpm = pos->getBpm().orFallback(0.0);
            engine.hostTransport(pos->getIsPlaying(), pos->getPpqPosition().orFallback(0.0), bpm);
        }
    const int n = buf.getNumSamples();
    // recording: the input comes in on the same buffer, before the synth writes its output over it
    if (getTotalNumInputChannels() > 0 && engine.recOn.load(std::memory_order_relaxed))
        engine.recordInput(buf.getReadPointer(0), getTotalNumInputChannels() > 1 ? buf.getReadPointer(1) : nullptr, n);
    if (tmp.getNumSamples() < n) tmp.setSize(2, n, false, false, true);
    float* L = tmp.getWritePointer(0); float* R = tmp.getWritePointer(1);
    // split the block at each MIDI event so notes land on time
    int pos = 0;
    juce::MidiBuffer out;
    // the sequencer's notes (and clock) go out to the host, at the samples they happened
    auto renderTo = [&](int upto) {
        if (upto <= pos) return;
        engine.render(L + pos, R + pos, upto - pos);
        for (int i = 0; i < engine.midiOutCount; ++i) {
            const auto& e = engine.midiOutBuf[(size_t) i];
            out.addEvent(e.n == 1 ? juce::MidiMessage(e.b[0]) : juce::MidiMessage(e.b[0], e.b[1], e.b[2]), juce::jlimit(0, n - 1, pos + e.offset));
        }
        pos = upto;
    };
    // everything that comes in (notes, controllers, bend, aftertouch, programs, clock) goes to the engine's parser
    for (const auto meta : midi) {
        renderTo(juce::jlimit(0, n, meta.samplePosition));
        engine.midiIn(meta.data, meta.numBytes);
    }
    renderTo(n);
    midi.swapWith(out);
    if (buf.getNumChannels() >= 2) { buf.copyFrom(0, 0, L, n); buf.copyFrom(1, 0, R, n); }
    else { buf.copyFrom(0, 0, L, n); buf.addFrom(0, 0, R, n); buf.applyGain(0.5f); }
    for (int c = 2; c < buf.getNumChannels(); ++c) buf.clear(c, 0, n);
}

void SidProcessor::handleAsyncUpdate() {}

// message thread: program changes from MIDI, settings moved by MIDI controllers (into the host's automation), the sample
void SidProcessor::timerCallback() {
    engine.pollSample();
    const int prog = engine.pendingProgram.exchange(-1);
    if (prog >= 0) setCurrentProgram(prog);
    int id; float v;
    while (engine.popParamChange(id, v))
        if (auto* p = apvts.getParameter(params()[id].key)) { lastHost[(size_t) id] = v; p->setValueNotifyingHost(p->convertTo0to1(v)); }
}

void SidProcessor::saveMidiFile(const std::string& bytes, const std::string& name) {
    chooser = std::make_unique<juce::FileChooser>("Save the pattern as a MIDI file", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name), "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [bytes](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return;
            if (!f.hasFileExtension("mid")) f = f.withFileExtension("mid");
            f.replaceWithData(bytes.data(), bytes.size());
        });
}
void SidProcessor::loadMidiFile() {
    chooser = std::make_unique<juce::FileChooser>("Load a MIDI file into the pattern", juce::File(), "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult(); if (!f.existsAsFile()) return;
        juce::MemoryBlock mb; f.loadFileAsData(mb);
        if (midiFileLoaded) midiFileLoaded(std::string((const char*) mb.getData(), mb.getSize()));
    });
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
    if (const int b = Synth::presetBuiltin(i); b >= 0) engine.loadBuiltin(b);
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
