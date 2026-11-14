#include "DmxPlugin.h"
#include "DmxEditor.h"
#include "BinaryData.h"

namespace dmx {

static juce::String fid(int g) { return "fader" + juce::String(g); }

juce::AudioProcessorValueTreeState::ParameterLayout DmxProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const float def[kGroups] = {.8f, .8f, .8f, .8f, .8f, .8f, .8f, 0.f, .85f};
    const char* const names[kGroups] = {"Bass", "Snare", "Hi-Hat", "Toms", "Cymbal", "Perc 1", "Perc 2", "Metronome", "Volume"};
    for (int g = 0; g < kGroups; ++g)
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(fid(g), 1), names[g], juce::NormalisableRange<float>(0.f, 1.f), def[g]));
    return l;
}

DmxProcessor::DmxProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "params", layout()) {
    for (int g = 0; g < kGroups; ++g) raw[(size_t) g] = apvts.getRawParameterValue(fid(g));
    // the kits are embedded as "<kit>__<file>.wav" (see CMakeLists.txt)
    for (int k = 0; k < kKits; ++k) {
        engine.samples[(size_t) k].resize((size_t) kKitDefs[k].voices);
        for (int v = 0; v < kKitDefs[k].voices; ++v) {
            const juce::String want = juce::String(kKitDefs[k].id) + "__" + kKitDefs[k].v[v].file + ".wav";
            for (int r = 0; r < BinaryData::namedResourceListSize; ++r)
                if (want == BinaryData::originalFilenames[r]) {
                    int size = 0; const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[r], size);
                    readWav(data, (size_t) size, engine.samples[(size_t) k][(size_t) v]);
                    break;
                }
        }
    }
}

bool DmxProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void DmxProcessor::setFader(int g, float v) {
    if (auto* p = apvts.getParameter(fid(g))) p->setValueNotifyingHost(juce::jlimit(0.f, 1.f, v));
}

void DmxProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    for (int g = 0; g < kGroups; ++g) engine.fader[g] = raw[(size_t) g]->load();
    Engine::Host h;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        h.playing = pos->getIsPlaying();
        if (auto bpm = pos->getBpm()) h.bpm = *bpm;
        if (auto ppq = pos->getPpqPosition()) h.ppq = *ppq; else h.playing = false;
    }
    for (const auto m : midi) {
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) engine.note(msg.getNoteNumber(), msg.getVelocity(), m.samplePosition);
        else if (msg.isProgramChange()) setCurrentProgram(msg.getProgramChangeNumber() % kPatterns);
    }
    midi.clear();
    const int n = buf.getNumSamples();
    float* L = buf.getWritePointer(0); float* R = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
    engine.process(L, R, n, h);
    for (int c = 2; c < buf.getNumChannels(); ++c) buf.clear(c, 0, n);
}

// ---- state: the faders (parameters) plus every kit's patterns, song and voice names ----
void DmxProcessor::getStateInformation(juce::MemoryBlock& out) {
    auto st = apvts.copyState();
    juce::XmlElement x("OmegaDMX");
    x.addChildElement(st.createXml().release());
    Locked l(engine.lock);
    x.setAttribute("kit", engine.kit); x.setAttribute("tempo", engine.tempo); x.setAttribute("swing", engine.swing);
    for (int k = 0; k < kKits; ++k) {
        auto* kx = x.createNewChildElement("Kit");
        kx->setAttribute("id", kKitDefs[k].id); kx->setAttribute("pattern", engine.cur[(size_t) k]); kx->setAttribute("circuit", engine.circuitState[(size_t) k]);
        juce::StringArray tn; for (int v = 0; v < kKitDefs[k].voices; ++v) tn.add(juce::String(engine.tune[(size_t) k][(size_t) v])); kx->setAttribute("tune", tn.joinIntoString(",")); kx->setAttribute("useSong", engine.useSong[(size_t) k]);
        juce::StringArray sg; for (int p : engine.song[(size_t) k]) sg.add(juce::String(p)); kx->setAttribute("song", sg.joinIntoString(","));
        for (int v = 0; v < kKitDefs[k].voices; ++v) if (!engine.names[(size_t) k][(size_t) v].empty()) kx->setAttribute("name" + juce::String(v), juce::String(engine.names[(size_t) k][(size_t) v]));
        for (int p = 0; p < kPatterns; ++p) {
            auto* px = kx->createNewChildElement("Pattern"); const auto& P = engine.pats[(size_t) k][(size_t) p];
            px->setAttribute("name", juce::String(P.name));
            juce::StringArray rows; for (int v = 0; v < kKitDefs[k].voices; ++v) rows.add(Engine::rowString(P, v));
            px->setAttribute("rows", rows.joinIntoString("|"));
        }
    }
    copyXmlToBinary(x, out);
}

void DmxProcessor::setStateInformation(const void* data, int size) {
    auto x = getXmlFromBinary(data, size);
    if (!x || !x->hasTagName("OmegaDMX")) return;
    if (auto* p = x->getChildByName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*p));
    {
    Locked l(engine.lock);
    engine.kit = juce::jlimit(0, kKits - 1, x->getIntAttribute("kit", 0)); engine.tempo = x->getDoubleAttribute("tempo", 112);
    engine.swing = x->getIntAttribute("swing", 50);
    for (auto* kx : x->getChildWithTagNameIterator("Kit")) {
        int k = -1; for (int i = 0; i < kKits; ++i) if (kx->getStringAttribute("id") == kKitDefs[i].id) k = i;
        if (k < 0) continue;
        engine.cur[(size_t) k] = juce::jlimit(0, kPatterns - 1, kx->getIntAttribute("pattern")); engine.useSong[(size_t) k] = kx->getBoolAttribute("useSong");
        engine.circuitState[(size_t) k] = juce::jlimit(-1, 1, kx->getIntAttribute("circuit", -1));
        { auto tn = juce::StringArray::fromTokens(kx->getStringAttribute("tune"), ",", ""); for (int v = 0; v < kKitDefs[k].voices; ++v) engine.tune[(size_t) k][(size_t) v] = v < tn.size() && tn[v].isNotEmpty() ? juce::jlimit(0, 100, tn[v].getIntValue()) : 50; }
        engine.song[(size_t) k].clear();
        for (auto& s : juce::StringArray::fromTokens(kx->getStringAttribute("song"), ",", "")) if (s.isNotEmpty()) engine.song[(size_t) k].push_back(juce::jlimit(0, kPatterns - 1, s.getIntValue()));
        for (int v = 0; v < kKitDefs[k].voices; ++v) engine.names[(size_t) k][(size_t) v] = kx->getStringAttribute("name" + juce::String(v)).toStdString();
        int p = 0;
        for (auto* px : kx->getChildWithTagNameIterator("Pattern")) {
            if (p >= kPatterns) break;
            auto& P = engine.pats[(size_t) k][(size_t) p++]; P = Pattern(); P.name = px->getStringAttribute("name").toStdString();
            auto rows = juce::StringArray::fromTokens(px->getStringAttribute("rows"), "|", "");
            for (int v = 0; v < kKitDefs[k].voices && v < rows.size(); ++v) Engine::setRow(P, v, rows[v].toStdString());
        }
    }
    }
    engine.refresh();   // the restored kit's cards with its tuning
}

juce::AudioProcessorEditor* DmxProcessor::createEditor() { return new DmxEditor(*this); }

}  // namespace dmx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dmx::DmxProcessor(); }
