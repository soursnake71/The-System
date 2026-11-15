#include "DmxPlugin.h"
#include "DmxEditor.h"
#include "BinaryData.h"

namespace dmx {

static juce::String fid(int g) { return "fader" + juce::String(g); }

// a sound parameter's value as the app's knobs show it (dmx/app.js fmtParam)
juce::String fmtParam(int i, double v) {
    static const char* const RV[] = {"HALL", "ROOM", "VOCAL", "PLATE", "EARLY REF"};
    static const char* const DIV[] = {"OFF", "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/1"};
    static const char* const OD[] = {"OVERDRIVE", "HARD CLIP", "TUBE"}, * const DM[] = {"STOCK", "ON", "OFF"}, * const ST[] = {"STOCK", "EARLY", "LATE"},
        * const FL[] = {"STOCK", "LOW 10K", "HIGH 16K", "BYPASS"}, * const DA[] = {"AM6070", "LINEAR 8", "LINEAR 6"};
    const juce::String id(kParamDefs[i].id), f = i >= kNG ? id.substring(2) : id; const int n = (int) std::floor(v + .5);
    auto pick = [&](const char* const* a, int count) { return juce::String(a[juce::jlimit(0, count - 1, n)]); };
    if (f == "rvProg") return pick(RV, 5);
    if (f == "dlSync") return pick(DIV, 14);
    if (f == "odMode") return pick(OD, 3);
    if (f == "dmod") return pick(DM, 3);
    if (f == "early") return pick(ST, 3);
    if (f == "filt") return pick(FL, 4);
    if (f == "dac") return pick(DA, 3);
    if (f == "rev" || f == "mute" || f == "dlPing") return v >= .5 ? "ON" : "OFF";
    if (f == "cut" && v >= 19999) return "OFF";
    if (f.endsWith("F") || f == "cut" || f == "rvCut" || f == "dlHiCut" || f == "dlLoCut") return v >= 1000 ? juce::String(v / 1000, v >= 10000 ? 0 : 1) + "k" : juce::String((int) std::floor(v + .5));
    if (f.endsWith("G") || f == "odOut") return (v > 0 ? "+" : "") + juce::String(v, 1);
    if (f == "dec") return v >= 2.999 ? juce::String("OFF") : juce::String(v, 2) + "s";
    if (f == "rel") return v >= 3.999 ? juce::String("HOLD") : juce::String(v, 2) + "s";
    if (f == "tune") return (v > 0 ? "+" : "") + juce::String(n);
    if (f == "pan") return std::abs(v) < .01 ? juce::String("C") : juce::String(v < 0 ? "L" : "R") + juce::String((int) std::floor(std::abs(v) * 100 + .5));
    if (f == "cap") return juce::String::fromUTF8("\xc3\x97") + juce::String(v, 2);
    if (f == "dlTime" || f == "rvDelay") return juce::String((int) std::floor(v + .5)) + "ms";
    if (f == "rvTime" || f == "atk" || f == "dcy") return juce::String(v, 2) + "s";
    return std::abs(v) >= 100 ? juce::String((int) std::floor(v + .5)) : juce::String(v, 2);
}

juce::AudioProcessorValueTreeState::ParameterLayout DmxProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const float def[kGroups] = {.8f, .8f, .8f, .8f, .8f, .8f, .8f, 0.f, .85f};
    const char* const names[kGroups] = {"Bass", "Snare", "Hi-Hat", "Toms", "Cymbal", "Perc 1", "Perc 2", "Metronome", "Volume"};
    for (int g = 0; g < kGroups; ++g)
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(fid(g), 1), names[g], juce::NormalisableRange<float>(0.f, 1.f), def[g]));
    // the sound parameters: switches and modes step, wide ranges (frequencies, times) sit on a log taper as in the app
    static const char* const stepped[] = {"rvProg", "dlSync", "odMode", "dlPing", "dmod", "early", "filt", "dac", "rev", "mute"};
    for (int i = 0; i < kParams; ++i) {
        const auto& d = kParamDefs[i]; const juce::String id(d.id), fld = i >= kNG ? id.substring(2) : id;
        bool step = false; for (auto* x : stepped) if (fld == x) step = true;
        juce::NormalisableRange<float> r((float) d.min, (float) d.max, step ? 1.f : 0.f);
        if (!step && d.min > 0 && d.max / d.min >= 20) r.setSkewForCentre((float) std::sqrt(d.min * d.max));
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1), d.name, r, (float) d.def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([i](float v, int) { return fmtParam(i, v); })));
    }
    return l;
}

DmxProcessor::DmxProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "params", layout()) {
    for (int g = 0; g < kGroups; ++g) raw[(size_t) g] = apvts.getRawParameterValue(fid(g));
    for (int i = 0; i < kParams; ++i) sraw[(size_t) i] = apvts.getRawParameterValue(pid(i));
    startTimerHz(30);
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

void DmxProcessor::syncParams() {
    std::array<double, kParams> v {}; bool diff = false;
    for (int i = 0; i < kParams; ++i) { v[(size_t) i] = sraw[(size_t) i]->load(); { const double a = v[(size_t) i], b = (double) (float) engine.sp[(size_t) i]; if (a < b || a > b) diff = true; } }
    if (!diff) return;
    engine.setSoundParams(v.data());
    if (!loading) edited = true;
}
void DmxProcessor::setSound(int i, double v) {
    if (auto* p = apvts.getParameter(pid(i))) p->setValueNotifyingHost(p->convertTo0to1((float) v));
}
void DmxProcessor::loadPreset(int i) {
    if (i < 0 || i >= kPresets) return;
    loading = true; for (int k = 0; k < kParams; ++k) setSound(k, kPresetValues[i][k]); syncParams(); loading = false;
    presetName = juce::CharPointer_UTF8(kPresetNames[i]); edited = false;
}
void DmxProcessor::dice(int part) {
    std::array<double, kParams> cur {}; for (int i = 0; i < kParams; ++i) cur[(size_t) i] = sraw[(size_t) i]->load();
    const auto v = presets::dice(cur.data(), part, (uint32_t) juce::Random::getSystemRandom().nextInt());
    loading = true; for (int k = 0; k < kParams; ++k) setSound(k, v[(size_t) k]); syncParams(); loading = false;
    presetName = juce::String("Dice: ") + kDicePartNames[part]; edited = false;
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
        else if (msg.isNoteOff()) engine.release(msg.getNoteNumber());   // the pad's ADSR releases
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
    x.setAttribute("preset", presetName); x.setAttribute("edited", edited);
    copyXmlToBinary(x, out);
}

void DmxProcessor::setStateInformation(const void* data, int size) {
    auto x = getXmlFromBinary(data, size);
    if (!x || !x->hasTagName("OmegaDMX")) return;
    if (auto* p = x->getChildByName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*p));
    presetName = x->getStringAttribute("preset", presetName); edited = x->getBoolAttribute("edited");
    loading = true; syncParams(); loading = false;
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
