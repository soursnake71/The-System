#include "SolinaPlugin.h"
#include "SolinaEditor.h"

namespace solina {

juce::String fmtVal(int id, double v) {
    const ParamInfo& p = PARAMS[id];
    if (!p.choices.empty()) { const int i = juce::jlimit(0, (int) p.choices.size() - 1, (int) std::lround(v)); return juce::String(juce::CharPointer_UTF8(p.choices[(size_t) i])); }
    if (p.step == 1 && p.max == 1 && juce::String(p.unit).isEmpty()) return v >= .5 ? "ON" : "OFF";
    if (juce::String(p.id).endsWith("Pan")) return std::abs(v) < .02 ? "C" : juce::String(v < 0 ? "L" : "R") + juce::String((int) std::lround(std::abs(v) * 100));
    const juce::String u(p.unit);
    if (u == "%") return juce::String((int) std::lround(v * 100)) + "%";
    if (u == "Hz") return juce::String(v, 2) + " Hz";
    if (u == "ms") return juce::String((int) std::lround(v)) + " ms";
    if (u == "s") return juce::String(v, 1) + " s";
    if (u == "st" || u == "ct") return (v > 0 ? "+" : "") + juce::String((int) std::lround(v)) + " " + u;
    if (u == "dB") return (v > 0 ? "+" : "") + juce::String(v, 1) + " dB";
    return juce::String(v, 2);
}

// a random sound with the Solina's character (as randomPatch in engine.js)
Params randomPatch(juce::Random& R) {
    auto rnd = [&] { return (double) R.nextFloat(); };
    auto r = [&](double a, double b) { return a + (b - a) * rnd(); };
    Params o = defaults();
    auto S = [&](int id, double v) { o[(size_t) id] = v; };
    for (int t : CH_TAB) S(t, rnd() < .45 ? 1 : 0);
    if (o[pid::violin] + o[pid::viola] + o[pid::trumpet] + o[pid::horn] == 0) S(CH_TAB[R.nextInt(4)], 1);
    S(pid::cresc, std::pow(rnd(), 1.5)); S(pid::sus, rnd() < .8); S(pid::susLen, rnd()); S(pid::bassVol, r(.3, 1)); S(pid::ens, rnd() < .9);
    S(pid::chorusHz, r(.3, 1.6)); S(pid::tremHz, r(4.5, 8)); S(pid::depth, r(.15, .8)); S(pid::trem, r(0, .6)); S(pid::wide, rnd() < .5); S(pid::outSel, rnd() < .15);
    static const int PIT[4] = {-12, 12, 7, -5};
    for (int c = 0; c < 6; c++) { const int b = CH_BASE[c];
        S(b + oLvl, r(.55, .95)); S(b + oPan, rnd() < .5 ? 0 : r(-.7, .7)); S(b + oPit, rnd() < .85 ? 0 : PIT[R.nextInt(4)]);
        S(b + oAtk, rnd() < .5 ? 0 : std::pow(rnd(), 2) * .8); S(b + oDec, r(.3, .8)); S(b + oTon, rnd() < .5 ? 1 : r(.5, 1));
        S(b + oEns, rnd() < .85); S(b + oEch, rnd() < .5 ? 0 : r(0, .6)); S(b + oRev, r(0, .7)); }
    static const int SY[4] = {1, 2, 3, 5};
    S(pid::phOn, rnd() < .35); S(pid::phRate, std::exp(r(std::log(.05), std::log(2.0)))); S(pid::phDepth, r(.4, 1)); S(pid::phFb, r(0, .8)); S(pid::phMix, r(.35, .55));
    S(pid::dlOn, rnd() < .5); S(pid::dlSync, rnd() < .6 ? SY[R.nextInt(4)] : 0); S(pid::dlTime, r(120, 700)); S(pid::dlFb, r(.15, .7)); S(pid::dlTone, r(.3, .9)); S(pid::dlPing, rnd() < .5); S(pid::dlRet, r(.3, .8));
    S(pid::rvOn, rnd() < .85); S(pid::rvSize, rnd()); S(pid::rvDecay, r(.8, 7)); S(pid::rvDamp, r(.2, .8)); S(pid::rvPre, r(0, 60)); S(pid::rvRet, r(.3, .8));
    S(pid::eqLow, std::round(r(-4, 5))); S(pid::eqMid, std::round(r(-4, 3))); S(pid::eqHigh, std::round(r(-5, 4))); S(pid::drive, rnd() < .2 ? r(0, .5) : 0);
    return o;
}

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    for (int i = 0; i < pid::count; i++) {
        const ParamInfo& p = PARAMS[i];
        juce::NormalisableRange<float> rg((float) p.min, (float) p.max, (float) p.step);
        L.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{p.id, 1}, p.name, rg, (float) p.def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction([i](float v, int) { return fmtVal(i, v); })));
    }
    return L;
}

SolinaProcessor::SolinaProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SOLINA", makeLayout()) {
    seq.song = &song;
    resetCC();
    startTimerHz(30);
}

void SolinaProcessor::resetCC() { ccMap.clear(); for (int i = 0; i < pid::count; i++) if (PARAMS[i].cc >= 0) ccMap[PARAMS[i].cc] = i; setCC(-1, -1); }
void SolinaProcessor::setCC(int cc, int param) {
    if (cc >= 0) { for (auto it = ccMap.begin(); it != ccMap.end();) { if (it->second == param) it = ccMap.erase(it); else ++it; } ccMap[cc] = param; }
    const juce::SpinLock::ScopedLockType l(ccLock); ccLive.fill(-1); for (auto& [c, p] : ccMap) if (c >= 0 && c < 128) ccLive[(size_t) c] = p;
}

void SolinaProcessor::prepareToPlay(double sampleRate, int) {
    fs = sampleRate; engine = std::make_unique<Engine>(sampleRate);
    for (int i = 0; i < pid::count; i++) last[(size_t) i] = getParam(i);
    engine->setAll(last);
}

void SolinaProcessor::setParam(int id, double v) {
    if (auto* p = apvts.getParameter(PARAMS[id].id)) p->setValueNotifyingHost(p->convertTo0to1((float) v));
}
void SolinaProcessor::applyValues(const Params& v, const juce::String& name) {
    for (int i = 0; i < pid::count; i++) if (juce::String(PARAMS[i].group) != "play") setParam(i, v[(size_t) i]);
    presetName = name;
}
void SolinaProcessor::setCurrentProgram(int i) {
    if (i < 0 || i >= kPresets) return; program = i;
    Params v; for (int k = 0; k < pid::count; k++) v[(size_t) k] = PRESETS[i].v[k];
    applyValues(v, juce::String(juce::CharPointer_UTF8(PRESETS[i].name)));
}
void SolinaProcessor::randomize() { juce::Random r; applyValues(randomPatch(r), "Random"); }

void SolinaProcessor::timerCallback() {
    // controller moves and program changes from the audio thread become parameter changes here
    int s1, n1, s2, n2;
    fifo.prepareToRead(fifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; i++) setParam(pend[(size_t) (s1 + i)].id, pend[(size_t) (s1 + i)].v);
    for (int i = 0; i < n2; i++) setParam(pend[(size_t) (s2 + i)].id, pend[(size_t) (s2 + i)].v);
    fifo.finishedRead(n1 + n2);
    const int pc = pcPending.exchange(-1); if (pc >= 0) setCurrentProgram(pc);
    const int lc = learnedCC.exchange(-1); if (lc >= 0 && learnTarget >= 0) { setCC(lc, learnTarget); learnTarget = -1; learnChanged = true; }
    logFifo.prepareToRead(logFifo.getNumReady(), s1, n1, s2, n2);
    auto add = [&](juce::uint32 m) {
        const int st = (int) (m >> 16), a = (int) (m >> 8 & 255), b = (int) (m & 255), hi = st & 0xF0;
        const char* nm = hi == 0x90 ? "on" : hi == 0x80 ? "off" : hi == 0xB0 ? "CC" : hi == 0xC0 ? "program" : hi == 0xE0 ? "bend" : hi == 0xD0 ? "AT" : "?";
        const juce::SpinLock::ScopedLockType l(logLock); log.add("ch" + juce::String((st & 15) + 1) + " " + nm + " " + juce::String(a) + " " + juce::String(b)); if (log.size() > 40) log.remove(0);
    };
    for (int i = 0; i < n1; i++) add(logQ[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; i++) add(logQ[(size_t) (s2 + i)]);
    logFifo.finishedRead(n1 + n2);
}

void SolinaProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples();
    buf.clear();
    if (!engine) return;
    // parameters
    bool changed = false;
    for (int i = 0; i < pid::count; i++) { const double v = getParam(i); if (v != last[(size_t) i]) { last[(size_t) i] = v; changed = true; } }
    if (changed) engine->setAll(last);
    // on-screen keyboard into the incoming MIDI
    keyboard.processNextMidiBuffer(midi, 0, n, true);
    // host transport
    double bpm = 0; bool hostPlaying = false; double ppq = 0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        if (auto b = pos->getBpm()) bpm = *b;
        hostPlaying = pos->getIsPlaying();
        if (auto q = pos->getPpqPosition()) ppq = *q;
    }
    hostBpm = bpm;
    juce::MidiBuffer out;
    const int och = juce::jlimit(1, 16, outChannel.load());
    auto emitOut = [&](int off, int st, int nn, int v) {
        if (st == 0x90) out.addEvent(juce::MidiMessage::noteOn(och, nn, (juce::uint8) v), off);
        else if (st == 0x80) out.addEvent(juce::MidiMessage::noteOff(och, nn), off);
        else if (st == 0xF8 && clockOut) out.addEvent(juce::MidiMessage::midiClock(), off);
    };
    const juce::SpinLock::ScopedLockType songL(songLock);
    if (hostSync && bpm > 0) song.bpm = bpm;
    engine->bpm = song.bpm;
    seq.rec = recording; seq.quant = quantize;
    struct Ev { int off, st, n, v; };
    std::vector<Ev> evs; evs.reserve(64);
    auto stopSeq = [&] { seq.stop([&](int, int, int nn, int) { evs.push_back({0, 0x80, nn, 0}); emitOut(0, 0x80, nn, 0); }); if (clockOut) out.addEvent(juce::MidiMessage::midiStop(), 0); };
    if (hostSync && hostPlaying && !hostWasPlaying) { double len = 0; if (song.mode) for (int p : song.chain) len += patLen(song.pats[p]); else len = patLen(song.pats[song.cur]);
        seq.start(len > 0 ? std::fmod(ppq * PPQ, len) : 0); if (clockOut) out.addEvent(juce::MidiMessage::midiStart(), 0); }
    if (hostSync && !hostPlaying && hostWasPlaying && seq.playing) stopSeq();
    hostWasPlaying = hostPlaying;
    if (playRequest.exchange(false)) { seq.start(0); if (clockOut) out.addEvent(juce::MidiMessage::midiStart(), 0); }
    if (stopRequest.exchange(false) && seq.playing) stopSeq();
    if (panicRequest.exchange(false)) { engine->allOff(); for (int ch = 1; ch <= 16; ch++) out.addEvent(juce::MidiMessage::allNotesOff(ch), 0); }
    seq.run(n, fs, [&](int off, int st, int nn, int v) { if (st != 0xF8) evs.push_back({off, st, nn, v}); emitOut(off, st, nn, v); });
    // incoming MIDI
    for (const auto m : midi) {
        const auto msg = m.getMessage(); const int off = juce::jlimit(0, n - 1, m.samplePosition);
        if (msg.isMidiClock() || msg.isMidiStart() || msg.isMidiStop() || msg.isMidiContinue() || msg.isSysEx()) continue;
        const int ch = msg.getChannel(); if (inChannel > 0 && ch != inChannel) continue;
        const juce::uint8* d = msg.getRawData();
        if (msg.getRawDataSize() >= 2) { auto w = logFifo.write(1); if (w.blockSize1 > 0) logQ[(size_t) w.startIndex1] = (juce::uint32) (d[0] << 16 | d[1] << 8 | (msg.getRawDataSize() > 2 ? d[2] : 0)); }
        if (msg.isNoteOn()) { evs.push_back({off, 0x90, msg.getNoteNumber(), msg.getVelocity()}); seq.recOn(msg.getNoteNumber(), msg.getFloatVelocity()); if (thru) out.addEvent(juce::MidiMessage::noteOn(och, msg.getNoteNumber(), msg.getVelocity()), off); }
        else if (msg.isNoteOff()) { evs.push_back({off, 0x80, msg.getNoteNumber(), 0}); int pat; Note nt; if (seq.recOff(msg.getNoteNumber(), pat, nt)) { song.pats[pat].notes.push_back(nt); songVersion++; }
            if (thru) out.addEvent(juce::MidiMessage::noteOff(och, msg.getNoteNumber()), off); }
        else if (msg.isPitchWheel()) { evs.push_back({off, 0xE0, msg.getPitchWheelValue(), 0}); if (thru) out.addEvent(juce::MidiMessage::pitchWheel(och, msg.getPitchWheelValue()), off); }
        else if (msg.isChannelPressure()) evs.push_back({off, 0xB0, 1, msg.getChannelPressureValue()});
        else if (msg.isProgramChange()) { if (pcChangesPreset && msg.getProgramChangeNumber() < kPresets) pcPending = msg.getProgramChangeNumber(); }
        else if (msg.isController()) {
            const int cc = msg.getControllerNumber(), v = msg.getControllerValue();
            if (thru) out.addEvent(juce::MidiMessage::controllerEvent(och, cc, v), off);
            if (cc == 1 || cc == 11 || cc == 64 || cc == 120 || cc == 121 || cc == 123) evs.push_back({off, 0xB0, cc, v});
            else if (learn && learnTarget >= 0) { if (cc != 0 && cc != 32) learnedCC = cc; }
            else { int id = -1; { const juce::SpinLock::ScopedLockType l(ccLock); id = ccLive[(size_t) cc]; }
                if (id >= 0) { const ParamInfo& p = PARAMS[id]; const double val = p.step == 1 && p.max - p.min == 1 ? (v >= 64 ? 1 : 0) : p.min + v / 127.0 * (p.max - p.min);
                    auto w = fifo.write(1); if (w.blockSize1 > 0) pend[(size_t) w.startIndex1] = {id, val}; } }
        }
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.off < b.off; });
    // render between events
    float* L = buf.getWritePointer(0); float* R = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : L;
    int at = 0;
    for (const auto& e : evs) {
        if (e.off > at) { engine->process(L, R, at, e.off - at); at = e.off; }
        switch (e.st) {
            case 0x90: engine->noteOn(e.n, e.v / 127.0); break;
            case 0x80: engine->noteOff(e.n); break;
            case 0xE0: engine->bend((e.n - 8192) / 8192.0); break;
            case 0xB0:
                if (e.n == 1) engine->modWheel(e.v / 127.0);
                else if (e.n == 11) engine->expression(e.v / 127.0);
                else if (e.n == 64) engine->pedal(e.v >= 64);
                else if (e.n == 120 || e.n == 123) engine->allOff();
                else if (e.n == 121) { engine->bend(0); engine->modWheel(0); engine->expression(1); engine->pedal(false); }
                break;
            case 0xF8: break;
        }
    }
    if (at < n) engine->process(L, R, at, n - at);
    // MIDI out: the sequencer's notes, the clock, and what came in (with THRU)
    midi.swapWith(out);
    playing = seq.playing; playPat = seq.pat; playTick = seq.tick;
}

juce::AudioProcessorEditor* SolinaProcessor::createEditor() { return new SolinaEditor(*this); }

// ---- state: parameters, song, settings, CC map ----
static juce::var songToVar(const Song& s) {
    auto* o = new juce::DynamicObject();
    o->setProperty("bpm", s.bpm); o->setProperty("swing", s.swing); o->setProperty("mode", s.mode); o->setProperty("cur", s.cur);
    juce::Array<juce::var> ch; for (int c : s.chain) ch.add(c); o->setProperty("chain", ch);
    juce::Array<juce::var> ps;
    for (const auto& p : s.pats) { auto* po = new juce::DynamicObject(); po->setProperty("bars", p.bars); juce::Array<juce::var> ns;
        for (const auto& n : p.notes) { auto* no = new juce::DynamicObject(); no->setProperty("t", n.t); no->setProperty("n", n.n); no->setProperty("d", n.d); no->setProperty("v", n.v); ns.add(juce::var(no)); }
        po->setProperty("notes", ns); ps.add(juce::var(po)); }
    o->setProperty("pats", ps);
    return juce::var(o);
}
static void varToSong(const juce::var& v, Song& s) {
    if (!v.isObject()) return;
    s = Song{};
    s.bpm = (double) v.getProperty("bpm", 120); s.swing = (double) v.getProperty("swing", 0); s.mode = (int) v.getProperty("mode", 0); s.cur = juce::jlimit(0, NPAT - 1, (int) v.getProperty("cur", 0));
    s.chain.clear(); if (auto* a = v.getProperty("chain", {}).getArray()) for (auto& c : *a) s.chain.push_back(juce::jlimit(0, NPAT - 1, (int) c));
    if (auto* a = v.getProperty("pats", {}).getArray()) for (int i = 0; i < juce::jmin(NPAT, a->size()); i++) {
        const auto& p = a->getReference(i); s.pats[i].bars = juce::jlimit(1, 4, (int) p.getProperty("bars", 1));
        if (auto* ns = p.getProperty("notes", {}).getArray()) for (auto& n : *ns) s.pats[i].notes.push_back({(int) n.getProperty("t", 0), (int) n.getProperty("n", 60), (int) n.getProperty("d", 96), (int) n.getProperty("v", 100)});
    }
}
void SolinaProcessor::getStateInformation(juce::MemoryBlock& mb) {
    auto st = apvts.copyState(); std::unique_ptr<juce::XmlElement> x(st.createXml());
    x->setAttribute("preset", presetName);
    { const juce::SpinLock::ScopedLockType l(songLock); x->setAttribute("song", juce::JSON::toString(songToVar(song), true)); }
    juce::String cc; for (auto& [c, p] : ccMap) cc << c << ":" << PARAMS[p].id << " "; x->setAttribute("cc", cc.trim());
    x->setAttribute("hostSync", hostSync.load()); x->setAttribute("thru", thru.load()); x->setAttribute("clockOut", clockOut.load());
    x->setAttribute("pc", pcChangesPreset.load()); x->setAttribute("inCh", inChannel.load()); x->setAttribute("outCh", outChannel.load()); x->setAttribute("quant", quantize.load());
    copyXmlToBinary(*x, mb);
}
void SolinaProcessor::setStateInformation(const void* d, int size) {
    std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(d, size)); if (!x) return;
    apvts.replaceState(juce::ValueTree::fromXml(*x));
    presetName = x->getStringAttribute("preset", presetName);
    { Song s; varToSong(juce::JSON::parse(x->getStringAttribute("song")), s); const juce::SpinLock::ScopedLockType l(songLock); song = s; }
    songVersion++;
    if (x->hasAttribute("cc")) { ccMap.clear(); for (auto& t : juce::StringArray::fromTokens(x->getStringAttribute("cc"), " ", "")) {
        const int c = t.upToFirstOccurrenceOf(":", false, false).getIntValue(); const auto id = t.fromFirstOccurrenceOf(":", false, false);
        for (int i = 0; i < pid::count; i++) if (id == PARAMS[i].id) ccMap[c] = i; } setCC(-1, -1); }
    hostSync = x->getBoolAttribute("hostSync", true); thru = x->getBoolAttribute("thru", true); clockOut = x->getBoolAttribute("clockOut", false);
    pcChangesPreset = x->getBoolAttribute("pc", true); inChannel = x->getIntAttribute("inCh", 0); outChannel = x->getIntAttribute("outCh", 1); quantize = x->getBoolAttribute("quant", true);
}

// ---- MIDI files ----
juce::String SolinaProcessor::importMidi(const juce::File& f, bool quant, bool skipDrums) {
    juce::FileInputStream in(f); if (!in.openedOk()) return "Could not open the file";
    juce::MidiFile mf; if (!mf.readFrom(in)) return "Not a MIDI file";
    const int tpq = mf.getTimeFormat(); if (tpq <= 0) return "SMPTE-timed MIDI files are not supported";
    double tempo = 120; juce::MidiMessageSequence tempos; mf.findAllTempoEvents(tempos);
    if (tempos.getNumEvents() > 0) tempo = 60.0 / tempos.getEventPointer(0)->message.getTempoSecondsPerQuarterNote();
    struct N { double t, d; int n, v; }; std::vector<N> ns;
    for (int t = 0; t < mf.getNumTracks(); t++) {
        juce::MidiMessageSequence sq(*mf.getTrack(t)); sq.updateMatchedPairs();
        for (int i = 0; i < sq.getNumEvents(); i++) { auto* e = sq.getEventPointer(i); const auto& m = e->message;
            if (!m.isNoteOn()) continue; if (skipDrums && m.getChannel() == 10) continue;
            const double off = e->noteOffObject ? e->noteOffObject->message.getTimeStamp() : m.getTimeStamp() + tpq;
            ns.push_back({m.getTimeStamp(), std::max(1.0, off - m.getTimeStamp()), m.getNoteNumber(), m.getVelocity()}); }
    }
    if (ns.empty()) return "No notes in the file";
    std::sort(ns.begin(), ns.end(), [](const N& a, const N& b) { return a.t < b.t || (a.t == b.t && a.n < b.n); });
    const double sc = (double) PPQ / tpq, first = std::floor(ns[0].t * sc / BAR) * BAR;
    double lastT = 0; for (auto& x : ns) lastT = std::max(lastT, x.t * sc - first + 1);
    const int bars = std::min(NPAT * 4, (int) std::ceil(lastT / BAR)), npat = (bars + 3) / 4;
    Song s; s.bpm = std::round(juce::jlimit(30.0, 300.0, tempo) * 10) / 10; s.mode = 1; s.chain.clear();
    for (int i = 0; i < npat; i++) { s.pats[i].bars = std::min(4, bars - i * 4); s.chain.push_back(i); }
    for (auto& x : ns) {
        double t = x.t * sc - first, d = std::max(1.0, x.d * sc);
        if (quant) { t = std::round(t / STEP) * STEP; d = std::max((double) STEP, std::round(d / STEP) * STEP); }
        const int ti = (int) std::lround(t), di = (int) std::lround(d), pi = ti / (BAR * 4); if (pi >= npat) continue;
        s.pats[pi].notes.push_back({ti - pi * BAR * 4, x.n, std::min(di, BAR * 4 * NPAT), x.v});
    }
    { const juce::SpinLock::ScopedLockType l(songLock); song = s; }
    songVersion++;
    return {};
}
juce::String SolinaProcessor::exportMidi(const juce::File& f, bool wholeSong) {
    Song s; { const juce::SpinLock::ScopedLockType l(songLock); s = song; }
    std::vector<int> list; if (wholeSong && s.mode) list = s.chain; else list = {s.cur};
    juce::MidiMessageSequence t0, t1; const int ch = juce::jlimit(1, 16, outChannel.load());
    t0.addEvent(juce::MidiMessage::textMetaEvent(3, "Omega Solina"), 0);
    t0.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::lround(60e6 / s.bpm)), 0);
    t0.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    t1.addEvent(juce::MidiMessage::textMetaEvent(3, "Strings"), 0);
    double base = 0;
    for (int pi : list) { const auto& p = s.pats[pi]; const int len = patLen(p);
        for (auto& n : p.notes) { if (n.t >= len) continue; const double t = base + std::round(n.t + (((n.t / STEP) & 1) ? s.swing * 12 : 0));
            t1.addEvent(juce::MidiMessage::noteOn(ch, n.n, (juce::uint8) juce::jlimit(1, 127, n.v)), t);
            t1.addEvent(juce::MidiMessage::noteOff(ch, n.n), t + std::max(1, n.d)); }
        base += len; }
    t1.sort(); t1.updateMatchedPairs();
    t0.addEvent(juce::MidiMessage::endOfTrack(), base); t1.addEvent(juce::MidiMessage::endOfTrack(), base);
    juce::MidiFile mf; mf.setTicksPerQuarterNote(PPQ); mf.addTrack(t0); mf.addTrack(t1);
    f.deleteFile(); juce::FileOutputStream out(f); if (!out.openedOk()) return "Could not write the file";
    mf.writeTo(out, 1); return {};
}

} // namespace solina

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new solina::SolinaProcessor(); }
