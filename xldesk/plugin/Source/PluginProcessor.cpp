#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace xl;

/* ---------- the rear panel as plugin buses ---------- */
struct BusDef { const char* name; int ch; int first; };
// inputs map onto contiguous console sources (Engine.h Src); bus 0 is the main input
static const BusDef IN_BUSES[] = { { "DAW 1-16", 16, IN_DAW1 }, { "Mic 1-8 (VHD)", 8, IN_MIC1 }, { "Line 9-16", 8, IN_LINE9 }, { "Stereo In 1-4", 8, IN_ST1L },
                                   { "Returns 1-2 + Ext", 6, IN_RET1L }, { "iJack + Talk + Listen", 4, IN_IJL }, { "500 Slot In 1-16", 16, IN_SLOTIN1 } };
static const BusDef OUT_BUSES[] = { { "Mix A", 2, OUT_MIXA }, { "Mix B", 2, OUT_MIXB }, { "Mix C", 2, OUT_MIXC }, { "Mix D", 2, OUT_MIXD }, { "Monitor Main", 2, OUT_MAIN }, { "Mini 1", 2, OUT_MINI1 },
                                    { "Mini 2", 2, OUT_MINI2 }, { "Sub", 1, OUT_SUB }, { "Headphones", 2, OUT_HP }, { "Foldback A", 2, OUT_FBA }, { "Foldback B", 2, OUT_FBB }, { "Cue", 2, OUT_CUE },
                                    { "Aux 1-2", 2, OUT_AUX1 }, { "Channel outs 1-16", 16, OUT_CHOUT1 }, { "Stereo outs 1-4", 8, OUT_STOUT1 }, { "Return outs 1-2", 4, OUT_RETOUT1 },
                                    { "500 Slot outs 1-16", 16, OUT_SLOTOUT1 }, { "Meter L/R + Listen out", 3, OUT_METER } };
static juce::AudioChannelSet setFor (int n) { return n == 1 ? juce::AudioChannelSet::mono() : n == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::discreteChannels (n); }
juce::AudioProcessor::BusesProperties XLDeskProcessor::makeBuses()
{
    BusesProperties b;
    bool first = true;
    for (auto& d : IN_BUSES) { b = b.withInput (d.name, first ? juce::AudioChannelSet::stereo() : setFor (d.ch), first); first = false; }
    first = true;
    for (auto& d : OUT_BUSES) { b = b.withOutput (d.name, setFor (d.ch), first); first = false; }
    return b;
}

// friendly host-facing names: "Ch 3 Trim", "Ret 1 Return level", "Slot 5 control 2"
static juce::String friendlyName (const xlspec::ParamDef& p)
{
    const juce::String id (p.id), lb (p.label);
    if (p.kind == 'm')
    {
        const juce::String w = id.upToFirstOccurrenceOf (".", false, false); const int j = id.fromFirstOccurrenceOf (".", false, false).getIntValue() + 1;
        if (w.startsWith ("slot")) return "Slot " + w.substring (4) + " control " + juce::String (j);
        if (w.startsWith ("ins")) return "Ch " + w.substring (3) + " INS control " + juce::String (j);
        return "Mix " + w.substring (4) + " INS control " + juce::String (j);
    }
    for (auto [pre, nm] : { std::pair { "m", "Ch " }, std::pair { "s", "St " }, std::pair { "r", "Ret " } })
        if (id.startsWith (pre) && juce::CharacterFunctions::isDigit (id[1])) return nm + id.substring (1).upToFirstOccurrenceOf (".", false, false) + " " + lb;
    return lb;
}
static juce::String fmtValue (const xlspec::Law& law, const char* unit, float v)
{
    const juce::String u (unit); const double x = xlspec::law (law, v);
    if (u == "pan") return std::abs (x) < .02 ? juce::String ("C") : juce::String (x < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (x) * 100));
    if (u == "dB" || u == "dBu") return std::isinf (x) ? juce::String ("-inf ") + u : (x > 0 ? "+" : "") + juce::String (x, 1) + " " + u;
    if (u == "Hz") return x >= 1000 ? juce::String (x / 1000, 2) + " kHz" : juce::String (juce::roundToInt (x)) + " Hz";
    if (u == "s") return x < 1 ? juce::String (juce::roundToInt (x * 1000)) + " ms" : juce::String (x, 2) + " s";
    if (u == ":1") return std::isinf (x) ? juce::String ("limit") : juce::String (x, 1) + ":1";
    if (u == "h") return x < .05 ? juce::String ("2nd") : x > .95 ? juce::String ("3rd") : juce::String (juce::roundToInt ((1 - x) * 100)) + "% 2nd";
    return juce::String (x, 2);
}

XLDeskProcessor::XLDeskProcessor() : AudioProcessor (makeBuses())
{
    hwCb.p = this;
    values.resize (xlspec::NPARAMS); lastFb.assign (xlspec::NPARAMS, -1.f); fromMidi.assign (xlspec::NPARAMS, false);
    for (int i = 0; i < xlspec::NPARAMS; ++i)
    {
        const auto& p = xlspec::PARAMS[i];
        juce::RangedAudioParameter* rp;
        if (p.kind == 'b')
            rp = new juce::AudioParameterBool (juce::ParameterID { p.id, 1 }, friendlyName (p), p.def > .5f);
        else if (p.kind == 'p')
            rp = new juce::AudioParameterFloat (juce::ParameterID { p.id, 1 }, friendlyName (p), juce::NormalisableRange<float> (0, 1), p.def,
                                                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([law = p.law, u = p.unit] (float v, int) { return fmtValue (law, u, v); }));
        else
        {
            // generic module control: its meaning follows whatever module is fitted at that location
            const juce::String id (p.id); const int w = Engine::whereIndex (id.upToFirstOccurrenceOf (".", false, false).toStdString()), j = id.fromFirstOccurrenceOf (".", false, false).getIntValue();
            rp = new juce::AudioParameterFloat (juce::ParameterID { p.id, 1 }, friendlyName (p), juce::NormalisableRange<float> (0, 1), p.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([this, w, j] (float v, int) -> juce::String {
                    const auto* d = moduleDef (engine.type (w)); if (! d || j >= d->nParams) return "-";
                    const auto& q = d->params[j];
                    if (q.kind == 'b') return v > .5f ? "on" : "off";
                    if (q.kind == 's') return q.stepText[juce::jlimit (0, q.steps - 1, juce::roundToInt (v * (q.steps - 1)))];
                    if (juce::String (q.unit) == "eqdB") return juce::String ((v - .5) * 30, 1) + " dB";
                    return fmtValue (q.law, q.unit, v); }));
        }
        addParameter (rp); params.push_back (rp); index[p.id] = i;
    }
    for (int i = 0; i < xlspec::NMIDIMAP; ++i) defaultMap[juce::String (xlspec::MIDIMAP[i].ch) + ":" + juce::String (xlspec::MIDIMAP[i].cc)] = xlspec::MIDIMAP[i].target;
    src.assign (IN_NSRC, nullptr); sigOut.assign (OUT_NSIG, nullptr);
    for (int i = 0; i < xlspec::NPARAMS; ++i) values[(size_t) i] = params[(size_t) i]->getValue();
    engine.setParams (values.data());
    startTimerHz (100);
}
XLDeskProcessor::~XLDeskProcessor() { stopTimer(); if (hwIn) hwIn->stop(); }

bool XLDeskProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    for (int b = 0; b < l.inputBuses.size(); ++b)
    {
        const int n = l.inputBuses[b].size();
        if (b == 0 ? (n < 1 || n > 16) : (n != 0 && n != IN_BUSES[b].ch)) return false;   // DAW returns: any 1-16 channels, so a stereo track works too
    }
    for (int b = 0; b < l.outputBuses.size(); ++b)
    {
        const int n = l.outputBuses[b].size();
        if (b == 0 ? n != 2 : (n != 0 && n != OUT_BUSES[b].ch)) return false;
    }
    return true;
}

void XLDeskProcessor::prepareToPlay (double sr, int block)
{
    engine.setMicRef (micRef); engine.setConvRef (convRef);
    engine.prepare (sr, block);
    scratch.assign (OUT_NSIG, std::vector<float> ((size_t) juce::jmax (block, 16)));
}

void XLDeskProcessor::pushIn (juce::AbstractFifo& f, std::array<Raw, 1024>& buf, const juce::uint8* d, int n)
{
    if (n <= 0 || n > 64) return;
    const auto w = f.write (1); if (w.blockSize1 < 1) return;
    auto& r = buf[(size_t) w.startIndex1]; std::memcpy (r.d, d, (size_t) n); r.n = n;
}

void XLDeskProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    for (const auto m : midi) pushIn (inFifo, inBuf, m.data, m.numBytes);
    midi.clear();
    { const auto r = outFifo.read (outFifo.getNumReady()); for (int s : { 0, 1 }) { const int st = s ? r.startIndex2 : r.startIndex1, sz = s ? r.blockSize2 : r.blockSize1; for (int k = 0; k < sz; ++k) midi.addEvent (outBuf[(size_t) (st + k)].d, outBuf[(size_t) (st + k)].n, 0); } }
    for (int i = 0; i < xlspec::NPARAMS; ++i) values[(size_t) i] = params[(size_t) i]->getValue();
    if ((int) scratch[0].size() < n) return;   // host broke its block-size promise; skip rather than allocate here
    // inputs: each bus feeds a run of console sources; a missing or narrower bus leaves those inputs unplugged
    std::fill (src.begin(), src.end(), nullptr);
    for (int b = 0; b < getBusCount (true); ++b)
    {
        auto* bus = getBus (true, b); if (! bus || ! bus->isEnabled()) continue;
        const int off = getChannelIndexInProcessBlockBuffer (true, b, 0), nch = bus->getNumberOfChannels();
        for (int c = 0; c < nch && c < IN_BUSES[b].ch; ++c) src[(size_t) (IN_BUSES[b].first + c)] = buffer.getReadPointer (off + c);
    }
    // the engine writes its outputs to scratch first (inputs and outputs share the host buffer)
    for (int s = 0; s < OUT_NSIG; ++s) sigOut[(size_t) s] = scratch[(size_t) s].data();
    engine.process (n, src.data(), sigOut.data());
    buffer.clear();
    for (int b = 0; b < getBusCount (false); ++b)
    {
        auto* bus = getBus (false, b); if (! bus || ! bus->isEnabled()) continue;
        const int off = getChannelIndexInProcessBlockBuffer (false, b, 0), nch = bus->getNumberOfChannels();
        for (int c = 0; c < nch && c < OUT_BUSES[b].ch; ++c) buffer.copyFrom (off + c, 0, scratch[(size_t) (OUT_BUSES[b].first + c)].data(), n);
    }
}

/* ---------- parameters from the UI ---------- */
void XLDeskProcessor::setFromUi (int i, float v)
{
    if (i < 0) return;
    auto* p = params[(size_t) i];
    p->beginChangeGesture(); p->setValueNotifyingHost (v); p->endChangeGesture();
}
void XLDeskProcessor::setType (const juce::String& where, const juce::String& type)
{
    const int w = Engine::whereIndex (where.toStdString()); if (w < 0) return;
    engine.setType (w, type.toStdString());
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));   // generic control names/values now describe the new module
}
void XLDeskProcessor::setCfg (double mic, double conv) { micRef = mic; convRef = conv; engine.setMicRef (mic); engine.setConvRef (conv); }

void XLDeskProcessor::setUi (const juce::String& json)
{
    uiJson = json;
    const auto v = juce::JSON::parse (json);
    const auto m = v["midi"];
    if (m.isObject())
    {
        mc.inId = m["inId"].toString(); mc.outId = m["outId"].toString(); mc.fb = (bool) m["fb"]; mc.pb = (bool) m["pb"]; mc.follow = m["btn"].toString() == "follow";
        mc.mmcIn = (bool) m["mmcIn"]; mc.mmcOut = (bool) m["mmcOut"]; mc.mmcId = m["mmcId"].isVoid() ? 127 : (int) m["mmcId"];
    }
    learnMap.clear();
    if (auto* o = v["learn"].getDynamicObject()) for (auto& p : o->getProperties()) learnMap[p.name.toString()] = p.value.toString();
    snaps = v["snaps"];
    openMidiPorts();
}

/* ---------- MIDI: CC map, learn, pitch-bend faders, program-change snapshots, MMC ---------- */
juce::var XLDeskProcessor::midiDevices() const
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    juce::Array<juce::var> ins, outs;
    for (auto& d : juce::MidiInput::getAvailableDevices()) { juce::DynamicObject::Ptr x = new juce::DynamicObject(); x->setProperty ("id", d.identifier); x->setProperty ("name", d.name); ins.add (x.get()); }
    for (auto& d : juce::MidiOutput::getAvailableDevices()) { juce::DynamicObject::Ptr x = new juce::DynamicObject(); x->setProperty ("id", d.identifier); x->setProperty ("name", d.name); outs.add (x.get()); }
    o->setProperty ("t", "midiDevices"); o->setProperty ("ins", ins); o->setProperty ("outs", outs);
    return o.get();
}
void XLDeskProcessor::openMidiPorts()
{
    const juce::ScopedLock l (hwLock);
    if (mc.inId != openIn)
    {
        if (hwIn) { hwIn->stop(); hwIn.reset(); }
        openIn = mc.inId;
        if (mc.inId.isNotEmpty()) { hwIn = juce::MidiInput::openDevice (mc.inId, &hwCb); if (hwIn) hwIn->start(); }
    }
    if (mc.outId != openOut) { hwOut.reset(); openOut = mc.outId; if (mc.outId.isNotEmpty()) hwOut = juce::MidiOutput::openDevice (mc.outId); }
}
void XLDeskProcessor::sendMidiOut (const juce::MidiMessage& m)
{
    { const juce::ScopedLock l (hwLock); if (hwOut) hwOut->sendMessageNow (m); }
    if (m.getRawDataSize() <= 64) { const auto w = outFifo.write (1); if (w.blockSize1 > 0) { auto& r = outBuf[(size_t) w.startIndex1]; std::memcpy (r.d, m.getRawData(), (size_t) m.getRawDataSize()); r.n = m.getRawDataSize(); } }
}
void XLDeskProcessor::mmc (int cmd)
{
    if (! mc.mmcOut) return;
    const juce::uint8 b[] = { 0xF0, 0x7F, (juce::uint8) (mc.mmcId & 127), 0x06, (juce::uint8) cmd, 0xF7 };
    sendMidiOut (juce::MidiMessage (b, 6));
}
void XLDeskProcessor::action (const juce::String& id, int v)
{
    if (! v && id != "@rew" && id != "@ff") return;
    if (id == "@play") mmc (0x02); else if (id == "@stop") mmc (0x01); else if (id == "@rec") mmc (0x06);
    else if (id == "@rtz") { if (mc.mmcOut) { const juce::uint8 b[] = { 0xF0, 0x7F, (juce::uint8) (mc.mmcId & 127), 0x06, 0x44, 0x06, 0x01, 0x60, 0, 0, 0, 0, 0xF7 }; sendMidiOut (juce::MidiMessage (b, 13)); } }
    else if (id == "@rew") mmc (v ? 0x05 : 0x01); else if (id == "@ff") mmc (v ? 0x04 : 0x01);
    else if (id == "@soloclear") { for (int i = 0; i < xlspec::NPARAMS; ++i) if (juce::String (xlspec::PARAMS[i].id).endsWith (".solo")) setFromUi (i, 0); }
    else if (id == "@sub")
    {
        const juce::String set = params[(size_t) paramIndex ("mon.mini1")]->getValue() > .5f ? "Mini1" : params[(size_t) paramIndex ("mon.mini2")]->getValue() > .5f ? "Mini2" : "Main";
        const int i = paramIndex ("mon.sub" + set); setFromUi (i, params[(size_t) i]->getValue() > .5f ? 0.f : 1.f);
    }
}
void XLDeskProcessor::applyMidi (const juce::String& target, int v7, int raw14)
{
    if (target.startsWith ("@")) { action (target, v7); return; }
    const int i = paramIndex (target); if (i < 0) return;
    const auto& p = xlspec::PARAMS[i];
    float v;
    if (raw14 >= 0) v = raw14 / 16383.f;
    else if (p.kind == 'b') { if (mc.follow) v = v7 >= 64 ? 1.f : 0.f; else if (v7 > 0) v = params[(size_t) i]->getValue() > .5f ? 0.f : 1.f; else return; }
    else v = (juce::String (p.unit) == "pan" && v7 == 64) ? .5f : v7 / 127.f;
    fromMidi[(size_t) i] = true; setFromUi (i, v);
}
void XLDeskProcessor::recallSnap (const juce::String& n)
{
    const auto s = snaps[juce::Identifier (n)]; if (! s.isObject()) return;
    if (auto* t = s["types"].getDynamicObject()) for (auto& p : t->getProperties()) setType (p.name.toString(), p.value.toString());
    if (auto* pv = s["P"].getDynamicObject()) for (auto& p : pv->getProperties()) setFromUi (paramIndex (p.name.toString()), (float) p.value);
    juce::DynamicObject::Ptr o = new juce::DynamicObject(); o->setProperty ("t", "recalled"); o->setProperty ("n", n); notice (o.get());
}
void XLDeskProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isSysEx())
    {
        const auto* d = m.getSysExData(); const int sz = m.getSysExDataSize();
        if (! mc.mmcIn || sz < 4 || d[0] != 0x7F || d[2] != 0x06) return;
        const int dev = d[1]; if (dev != 0x7F && mc.mmcId != 0x7F && dev != mc.mmcId) return;
        // the plugin has no tape of its own: MMC from a controller passes through to the DAW
        if (mc.mmcOut) sendMidiOut (m);
        juce::DynamicObject::Ptr o = new juce::DynamicObject(); o->setProperty ("t", "mmc"); o->setProperty ("cmd", (int) d[3]); notice (o.get());
        return;
    }
    const int ch = m.getChannel() - 1;
    if (m.isController())
    {
        const juce::String key = juce::String (ch) + ":" + juce::String (m.getControllerNumber());
        if (learnId.isNotEmpty())
        {
            for (auto it = learnMap.begin(); it != learnMap.end();) it = it->second == learnId ? learnMap.erase (it) : std::next (it);
            learnMap[key] = learnId;
            juce::DynamicObject::Ptr o = new juce::DynamicObject(); o->setProperty ("t", "learned"); o->setProperty ("key", key); o->setProperty ("id", learnId); notice (o.get());
            learnId = {}; return;
        }
        auto it = learnMap.find (key); if (it == learnMap.end()) it = defaultMap.find (key); if (it == defaultMap.end()) return;
        applyMidi (it->second, m.getControllerValue(), -1);
    }
    else if (m.isPitchWheel() && mc.pb) applyMidi ("m" + juce::String (ch + 1) + ".fader", 0, m.getPitchWheelValue());
    else if (m.isProgramChange()) recallSnap (juce::String (m.getProgramChangeNumber()));
}
void XLDeskProcessor::timerCallback()
{
    for (auto [f, buf] : { std::pair { &inFifo, &inBuf }, std::pair { &hwFifo, &hwBuf } })
    {
        const auto r = f->read (f->getNumReady());
        for (int s : { 0, 1 }) { const int st = s ? r.startIndex2 : r.startIndex1, sz = s ? r.blockSize2 : r.blockSize1; for (int k = 0; k < sz; ++k) handleMidi (juce::MidiMessage ((*buf)[(size_t) (st + k)].d, (*buf)[(size_t) (st + k)].n)); }
    }
    // feedback: every change (UI, automation, snapshots) goes back out as CC / pitch bend, except what MIDI itself just set
    if (! mc.fb) return;
    std::map<juce::String, juce::String> rev;
    for (int i = 0; i < xlspec::NPARAMS; ++i)
    {
        const float v = params[(size_t) i]->getValue();
        if (std::abs (v - lastFb[(size_t) i]) < 1e-7f) continue;
        const bool first = lastFb[(size_t) i] < 0; lastFb[(size_t) i] = v;
        if (first || fromMidi[(size_t) i]) { fromMidi[(size_t) i] = false; continue; }
        if (rev.empty()) { for (auto& [k, id] : defaultMap) rev.emplace (id, k); for (auto& [k, id] : learnMap) rev[id] = k; }
        const juce::String id (xlspec::PARAMS[i].id);
        auto it = rev.find (id);
        if (it != rev.end()) sendMidiOut (juce::MidiMessage::controllerEvent (it->second.upToFirstOccurrenceOf (":", false, false).getIntValue() + 1, it->second.fromFirstOccurrenceOf (":", false, false).getIntValue(), juce::roundToInt (v * 127)));
        if (mc.pb && id.startsWith ("m") && id.endsWith (".fader")) sendMidiOut (juce::MidiMessage::pitchWheel (id.substring (1).getIntValue(), juce::roundToInt (v * 16383)));
    }
}

/* ---------- state ---------- */
void XLDeskProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement x ("XLDESK"); x.setAttribute ("version", 1); x.setAttribute ("micRef", micRef); x.setAttribute ("convRef", convRef);
    auto* t = x.createNewChildElement ("types"); for (int w = 0; w < xlspec::NWHERE; ++w) t->setAttribute (xlspec::WHERE[w].id, juce::String (engine.type (w)));
    auto* p = x.createNewChildElement ("params"); for (int i = 0; i < xlspec::NPARAMS; ++i) { auto* e = p->createNewChildElement ("p"); e->setAttribute ("id", xlspec::PARAMS[i].id); e->setAttribute ("v", params[(size_t) i]->getValue()); }
    x.createNewChildElement ("ui")->addTextElement (uiJson);
    copyXmlToBinary (x, dest);
}
void XLDeskProcessor::setStateInformation (const void* data, int size)
{
    auto x = getXmlFromBinary (data, size); if (! x || ! x->hasTagName ("XLDESK")) return;
    setCfg (x->getDoubleAttribute ("micRef", -20), x->getDoubleAttribute ("convRef", 24));
    if (auto* t = x->getChildByName ("types")) for (int w = 0; w < xlspec::NWHERE; ++w) if (t->hasAttribute (xlspec::WHERE[w].id)) setType (xlspec::WHERE[w].id, t->getStringAttribute (xlspec::WHERE[w].id));
    if (auto* p = x->getChildByName ("params")) for (auto* e : p->getChildIterator()) { const int i = paramIndex (e->getStringAttribute ("id")); if (i >= 0) params[(size_t) i]->setValueNotifyingHost ((float) e->getDoubleAttribute ("v")); }
    if (auto* u = x->getChildByName ("ui")) setUi (u->getAllSubText());
}

juce::AudioProcessorEditor* XLDeskProcessor::createEditor() { return new XLDeskEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new XLDeskProcessor(); }
