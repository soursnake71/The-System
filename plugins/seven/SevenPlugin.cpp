#include "SevenPlugin.h"
#include "SevenEditor.h"

namespace seven {
#include "Voices.inc"

static juce::String paramName(int id) {
    const auto& p = vparams()[(size_t) id];
    return p.op ? "OP" + juce::String(p.op) + " " + juce::String(p.name) : juce::String(p.name);
}

SevenProcessor::SevenProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    for (int i = 0; i < 32; ++i) std::memcpy(internal[(size_t) i].data(), FACTORY_VOICES[i], 156);
    edit = internal[0];
    syn.loadVoice(edit);
    sq.h = this;
    vp.assign(156, nullptr);
    for (int id = 0; id < 156; ++id) {
        if (id >= 145 && id <= 154) continue;
        const auto& p = vparams()[(size_t) id];
        auto* ap = new juce::AudioParameterInt(juce::ParameterID {"v" + juce::String(id), 1}, paramName(id), p.min, p.max, edit[(size_t) id]);
        vp[(size_t) id] = ap; addParameter(ap); lastV[(size_t) id] = edit[(size_t) id];
    }
    for (int k = 0; k < FCOUNT; ++k) {
        auto* ap = new juce::AudioParameterInt(juce::ParameterID {juce::String("f_") + FUNC_DEF[k].key, 1}, FUNC_DEF[k].name, 0, FUNC_DEF[k].max, syn.func[k]);
        fp.push_back(ap); addParameter(ap); lastF[(size_t) k] = syn.func[k];
    }
    startTimerHz(30);
}
SevenProcessor::~SevenProcessor() { stopTimer(); }

bool SevenProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo();
}
void SevenProcessor::prepareToPlay(double sampleRate, int maxBlock) {
    const juce::ScopedLock l(lock);
    fs = sampleRate; syn.setRate(sampleRate); toTick = 0; stereoScratch.assign((size_t) std::max(1, maxBlock) * 2, 0.0f);
}

/* ---------------- edits from the panel (message thread) ---------------- */
void SevenProcessor::setParam(int id, int v) {
    const juce::ScopedLock l(lock);
    if (id < 0 || id > 155) return;
    edit[(size_t) id] = (uint8_t) v; syn.setParam(id, v); voiceVer++;
    if (sq.rec && sq.recordParam(id, v)) seqDirty = true;
    if (syn.func[F::sysInfo]) { const auto m = paramChange(syn.func[F::txCh], 0, id, v); pendingMsgs.push_back(m); }
    pushParams = true;
}
void SevenProcessor::setVoice(const Voice156& v) {
    const juce::ScopedLock l(lock);
    edit = v; clampVoice(edit); syn.loadVoice(edit); voiceVer++; pushParams = true;
}
void SevenProcessor::setFunc(int k, int v) {
    const juce::ScopedLock l(lock);
    syn.setFunc(k, v); pushParams = true;
    // a full-REV7 program: the time / high / delay / diffusion controls take its factory values
    if (k == F::rvProg && syn.func[k] >= REV7_FULL) { const auto c = rev7Controls(syn.func[k]); const int ks[4] = {F::revSize, F::rvHigh, F::rvDelay, F::rvDiff}; for (int i = 0; i < 4; ++i) syn.setFunc(ks[i], c[(size_t) i]); }
    if (FUNC_DEF[k].num >= 0 && syn.func[F::sysInfo]) pendingMsgs.push_back(paramChange(syn.func[F::txCh], 2, FUNC_DEF[k].num, syn.func[k]));
}
void SevenProcessor::store(bool c, int i, const Voice156& v) { const juce::ScopedLock l(lock); if (c) { if (cart) (*cart)[(size_t) i] = v; } else internal[(size_t) i] = v; updateHostDisplay(); }
void SevenProcessor::setBank(bool c, const Bank& b) { const juce::ScopedLock l(lock); if (c) cart = b; else internal = b; updateHostDisplay(); }
void SevenProcessor::factoryReset() { const juce::ScopedLock l(lock); for (int i = 0; i < 32; ++i) std::memcpy(internal[(size_t) i].data(), FACTORY_VOICES[i], 156); updateHostDisplay(); }
void SevenProcessor::seqCmd(const std::string& cmd, int a) {
    const juce::ScopedLock l(lock);
    if (cmd == "pattern") sq.s.pat = std::max(0, std::min(PATTERNS - 1, a));
    else if (cmd == "arp") sq.setArp(a != 0);
    else if (cmd == "play") { sq.start(); toTick = 0; }
    else if (cmd == "stop") { stopSeq(); sq.rec = false; }
    else if (cmd == "rec") sq.rec = a != 0;
    else if (cmd == "cursor") sq.stepCursor = a;
}
void SevenProcessor::stopSeq() { sq.stop(); syn.loadVoice(edit); }
void SevenProcessor::noteFromUi(int n, int v, bool on) {
    { const juce::ScopedLock l(lock); if (panel.stepNote(n, v, on)) return; }
    if (on) keys.noteOn(1, n, (float) v / 127.0f); else keys.noteOff(1, n, 0.0f);
}
void SevenProcessor::panic() {
    const juce::ScopedLock l(lock);
    sq.stop(); sq.setArp(false); player.stop(); syn.allNotesOff(true); keys.allNotesOff(0);
}

/* ---------------- files ---------------- */
void SevenProcessor::request(const std::string& kind, int arg) {
    auto say = [this](const juce::String& s) { if (toast) toast(s); };
    auto pick = [this](const juce::String& title, const juce::String& pat, std::function<void(juce::File)> then) {
        chooser = std::make_unique<juce::FileChooser>(title, juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), pat);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [then](const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.existsAsFile()) then(f); });
    };
    if (kind == "loadFile") pick("Load a voice bank (.syx) or MIDI file", "*.syx;*.SYX;*.bin;*.mid;*.midi", [this](juce::File f) { openFile(f, "cart"); });
    else if (kind == "loadInt") pick("Load a bank into internal memory", "*.syx;*.SYX;*.bin", [this](juce::File f) { openFile(f, "int"); });
    else if (kind == "saveBank") {
        const juce::ScopedLock l(lock);
        if (arg && !cart) { panel.flash("CARTRIDGE", "NOT READY"); return; }
        saveBytes(bankToSysex(arg ? *cart : internal, syn.func[F::txCh]), arg ? (cartName.isEmpty() ? "cartridge.syx" : cartName.upToLastOccurrenceOf(".", false, false) + ".syx") : "omega-seven-internal.syx", "*.syx");
    } else if (kind == "saveVoice") { const juce::ScopedLock l(lock); saveBytes(voiceToSysex(edit, syn.func[F::txCh]), juce::String(voiceName(edit)).trim().replaceCharacters(" /\\:", "____") + ".syx", "*.syx"); }
    else if (kind == "sendVoice") { const juce::ScopedLock l(lock); pendingMsgs.push_back(voiceToSysex(edit, syn.func[F::txCh])); panel.flash("MIDI TRANSMIT", "VOICE SENT"); }
    else if (kind == "sendBank") { const juce::ScopedLock l(lock); pendingMsgs.push_back(bankToSysex(internal, syn.func[F::txCh])); panel.flash("MIDI TRANSMIT", "BANK SENT"); }
    else if (kind == "requestBank") { const juce::ScopedLock l(lock); pendingMsgs.push_back({0xF0, 0x43, (uint8_t) (0x20 | syn.func[F::txCh]), 0x09, 0xF7}); panel.flash("BANK REQUEST", "SENT"); }
    else if (kind == "requestVoice") { const juce::ScopedLock l(lock); pendingMsgs.push_back({0xF0, 0x43, (uint8_t) (0x20 | syn.func[F::txCh]), 0x00, 0xF7}); panel.flash("VOICE REQUEST", "SENT"); }
    else if (kind == "midiPlay") pick("Play a MIDI file", "*.mid;*.midi", [this](juce::File f) { openFile(f, "play"); });
    else if (kind == "midiStop") { const juce::ScopedLock l(lock); player.stop(); syn.allNotesOff(false); }
    else if (kind == "midiLoop") { const juce::ScopedLock l(lock); player.loop = arg != 0; }
    else if (kind == "midiImport") pick("Import a MIDI file into the pattern", "*.mid;*.midi", [this](juce::File f) { openFile(f, "pattern"); });
    else if (kind == "midiExport") {
        const juce::ScopedLock l(lock);
        const auto& p = sq.pat(); bool any = false; for (int i = 0; i < p.len; ++i) if (!p.steps[(size_t) i].notes.empty()) any = true;
        if (!any) { panel.flash("PATTERN " + std::to_string(sq.s.pat + 1), "IS EMPTY"); return; }
        saveBytes(patternToMidi(p, &edit, sq.s.tempo, syn.func[F::txCh], 1), "omega-seven-pattern-" + juce::String(sq.s.pat + 1) + ".mid", "*.mid");
    } else if (kind == "recStart") { const juce::ScopedLock l(lock); recEvents.clear(); recT0 = (double) frames; recOn = true; panel.flash("RECORDING", "NO=STOP & SAVE"); }
    else if (kind == "recStop") {
        std::vector<RecEvent> evs; Voice156 v;
        { const juce::ScopedLock l(lock); recOn = false; evs.swap(recEvents); v = edit; }
        if (evs.empty()) { say("Nothing was recorded"); return; }
        saveBytes(recordingToMidi(evs, &v, syn.func[F::txCh]), "omega-seven-performance.mid", "*.mid");
    }
}
void SevenProcessor::openFile(const juce::File& f, const juce::String& kind) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) { if (toast) toast("Can't read " + f.getFileName()); return; }
    const auto err = loadBytes(mb, f.getFileName(), kind);
    if (toast) toast(err);
}
// "" or a message for the user
juce::String SevenProcessor::loadBytes(const juce::MemoryBlock& mb, const juce::String& name, const juce::String& kind) {
    const auto* d = (const uint8_t*) mb.getData(); const size_t n = mb.getSize();
    const bool isMidi = n > 4 && std::memcmp(d, "MThd", 4) == 0;
    const juce::ScopedLock l(lock);
    if (kind == "play" || (kind == "cart" && isMidi)) {
        if (!isMidi) return "Not a MIDI file";
        const auto mf = parseMidiFile(d, n); if (!mf.ok) return juce::String(mf.error);
        player.load(mf, name.toStdString()); player.start();
        return "Playing " + name;
    }
    if (kind == "pattern") {
        if (!isMidi) return "Not a MIDI file";
        const auto mf = parseMidiFile(d, n); if (!mf.ok) return juce::String(mf.error);
        double tempo = 120; int count = 0;
        sq.pat() = patternFromMidi(mf, sq.pat().rate, tempo, count);
        sq.s.tempo = std::max(30.0, std::min(300.0, jsRound(tempo)));
        seqDirty = true;
        return juce::String(count) + " notes into pattern " + juce::String(sq.s.pat + 1);
    }
    const auto r = parseSysexFile(d, n);
    juce::String extra = r.bad ? " (" + juce::String(r.bad) + " checksum error(s): loaded anyway)" : "";
    if (r.hasBank) {
        if (kind == "int") { if (panel.protect[0]) return "Turn MEMORY PROTECT INTERNAL off first"; internal = r.bank; updateHostDisplay(); return "Bank loaded into internal memory" + extra; }
        cart = r.bank; cartName = name; panel.cart = true; panel.mode = Panel::play;
        return "Cartridge: " + name + extra;
    }
    if (!r.voices.empty()) { edit = r.voices[0]; syn.loadVoice(edit); panel.edited = true; voiceVer++; pushParams = true; return "Voice " + juce::String(voiceName(edit)).trim() + " in the edit buffer (STORE to keep it)" + extra; }
    if (!r.params.empty()) { for (auto& pv : r.params) { edit[(size_t) pv.first] = (uint8_t) pv.second; } syn.loadVoice(edit); voiceVer++; pushParams = true; return juce::String((int) r.params.size()) + " parameter changes applied"; }
    return "No voices found in " + name;
}
void SevenProcessor::saveBytes(const std::vector<uint8_t>& data, const juce::String& name, const juce::String& pattern) {
    chooser = std::make_unique<juce::FileChooser>("Save", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name), pattern);
    auto bytes = std::make_shared<std::vector<uint8_t>>(data);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, bytes](const juce::FileChooser& fc) {
            const auto f = fc.getResult(); if (f == juce::File()) return;
            const bool ok = f.replaceWithData(bytes->data(), bytes->size());
            if (toast) toast(ok ? "Saved " + f.getFileName() : "Couldn't write " + f.getFullPathName());
        });
}

/* ---------------- the sequencer's notes ---------------- */
void SevenProcessor::seqNoteOn(int n, int v) { syn.noteOn(n, v); pendingOut.addEvent(juce::MidiMessage::noteOn(syn.func[F::txCh] + 1, n, (juce::uint8) v), curPos); if (recOn) recEvents.push_back({(frames + curPos - recT0) / fs, {(uint8_t) 0x90, (uint8_t) n, (uint8_t) v}}); }
void SevenProcessor::seqNoteOff(int n) { syn.noteOff(n); pendingOut.addEvent(juce::MidiMessage::noteOff(syn.func[F::txCh] + 1, n), curPos); if (recOn) recEvents.push_back({(frames + curPos - recT0) / fs, {(uint8_t) 0x80, (uint8_t) n, 0}}); }
void SevenProcessor::seqParam(int id, int v) { if (id >= 200) { if (id - 200 < FCOUNT) syn.setFunc(id - 200, v); } else syn.setParam(id, v); }

/* ---------------- audio ---------------- */
void SevenProcessor::applyHostParams() {
    bool changed = false;
    for (int id = 0; id < 156; ++id) {
        auto* p = vp[(size_t) id]; if (!p) continue;
        const int v = p->get();
        if (v != lastV[(size_t) id]) { lastV[(size_t) id] = v; if (edit[(size_t) id] != v) { edit[(size_t) id] = (uint8_t) v; syn.setParam(id, v); changed = true; } }
    }
    for (int k = 0; k < FCOUNT; ++k) {
        const int v = fp[(size_t) k]->get();
        if (v != lastF[(size_t) k]) { lastF[(size_t) k] = v; if (syn.func[k] != v) syn.setFunc(k, v); }
    }
    if (changed) { voiceVer++; panel.edited = true; }
}
void SevenProcessor::syncParamsToHost() {
    for (int id = 0; id < 156; ++id) {
        auto* p = vp[(size_t) id]; if (!p) continue;
        const int v = edit[(size_t) id];
        if (p->get() != v) { lastV[(size_t) id] = v; p->setValueNotifyingHost(p->convertTo0to1((float) v)); }
    }
    for (int k = 0; k < FCOUNT; ++k) {
        auto* p = fp[(size_t) k]; const int v = syn.func[k];
        if (p->get() != v) { lastF[(size_t) k] = v; p->setValueNotifyingHost(p->convertTo0to1((float) v)); }
    }
}
void SevenProcessor::key(bool on, int n, int v) {
    if (panel.mode == Panel::func && panel.stepEdit) { panel.stepNote(n, v, on); seqDirty = true; return; }
    if (on) { if (!sq.keyOn(n, v)) syn.noteOn(n, v); }
    else if (!sq.keyOff(n)) syn.noteOff(n);
    if (sq.rec) seqDirty = true;
}
void SevenProcessor::handleMidi(const juce::MidiMessage& m, juce::MidiBuffer& out, int pos) {
    curPos = pos;
    if (m.isSysEx()) {
        std::vector<uint8_t> d; d.push_back(0xF0); const auto* s = m.getSysExData(); d.insert(d.end(), s, s + m.getSysExDataSize()); d.push_back(0xF7);
        if (!syn.func[F::sysInfo]) return;
        const auto r = parseSysexMessage(d.data(), d.size());
        switch (r.kind) {
            case SysexMsg::param: edit[(size_t) r.n] = (uint8_t) r.v; syn.setParam(r.n, r.v); panel.edited = true; voiceVer++; pushParams = true; break;
            case SysexMsg::fparam: { const int k = funcByNum(r.n); if (k >= 0) { syn.setFunc(k, r.v); pushParams = true; } break; }
            case SysexMsg::voice: edit = r.voiceData; syn.loadVoice(edit); panel.edited = true; voiceVer++; pushParams = true; panel.flash("MIDI RECEIVED", "VOICE"); break;
            case SysexMsg::bank: if (!panel.protect[0]) { internal = r.bankData; panel.flash("MIDI RECEIVED", "BANK > INTERNAL"); } else { cart = r.bankData; cartName = "MIDI bank"; panel.flash("MIDI RECEIVED", "BANK > CARTRIDGE"); } break;
            case SysexMsg::reqBank: { const auto b = bankToSysex(internal, syn.func[F::txCh]); out.addEvent(juce::MidiMessage::createSysExMessage(b.data() + 1, (int) b.size() - 2), pos); break; }
            case SysexMsg::reqVoice: { const auto b = voiceToSysex(edit, syn.func[F::txCh]); out.addEvent(juce::MidiMessage::createSysExMessage(b.data() + 1, (int) b.size() - 2), pos); break; }
            default: break;
        }
        return;
    }
    const auto* raw = m.getRawData(); const int len = m.getRawDataSize();
    if (len < 1) return;
    const int st = raw[0];
    if (st == 0xF8) { if (sq.s.ext && !lastHostPlaying) for (int i = 0; i < 4; ++i) sq.doTick(); return; }
    if (st == 0xFA) { if (sq.s.ext) sq.start(); return; }
    if (st == 0xFB) { if (sq.s.ext) sq.playing = true; return; }
    if (st == 0xFC) { if (sq.s.ext) stopSeq(); return; }
    if (st >= 0xF0) return;
    const int ty = st & 0xF0, rx = syn.func[F::rxCh];
    if (rx > 0 && (st & 15) != rx - 1) return;
    if (recOn) recEvents.push_back({(frames + pos - recT0) / fs, std::vector<uint8_t>(raw, raw + len)});
    if (ty == 0x90 && len > 2 && raw[2] > 0) { key(true, raw[1], raw[2]); return; }
    if (ty == 0x80 || ty == 0x90) { key(false, raw[1], 0); return; }
    if (ty == 0xC0) { pendingProgram = raw[1] & 31; return; }
    const auto r = syn.midi(raw, (size_t) len);
    if (r.kind == MidiResult::func) pushParams = true;
}
void SevenProcessor::renderTo(float* L, float* R, int from, int to) {
    const double spt = fs * 60.0 / (std::max(20.0, sq.s.tempo) * PPQ);
    syn.fx.tempo = sq.s.tempo;   // the tempo-synced delay
    int p = from;
    while (p < to) {
        const bool clocked = sq.playing || (sq.s.arp.on && sq.arpRun);
        int chunk = to - p;
        curPos = p;
        if (clocked) {
            if (toTick <= 0) { sq.doTick(); toTick += spt; if (sq.rec) seqDirty = true; }
            chunk = std::min(chunk, std::max(1, (int) std::ceil(toTick)));
        } else toTick = 0;
        if (player.playing) {
            juce::MidiBuffer dummy;
            player.run(chunk / fs, [&](const std::vector<uint8_t>& d) {
                if (d.empty()) return;
                if (d[0] == 0xF0) { if (d.size() > 2) handleMidi(juce::MidiMessage::createSysExMessage(d.data() + 1, (int) d.size() - 2), dummy, p); }
                else handleMidi(juce::MidiMessage(d.data(), (int) d.size()), dummy, p);
            });
        }
        syn.renderStereo(L + p, R + p, chunk);
        if (clocked) toTick -= chunk;
        p += chunk;
    }
}
void SevenProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    keys.processNextMidiBuffer(midi, 0, n, true);
    const juce::ScopedLock l(lock);
    applyHostParams();
    if (ctlDirty.exchange(false)) { syn.setBend(bendUi.load()); syn.setCtl(syn.ctl.wheel, wheelUi.load()); }
    // the host's transport: the sequencer follows it when the clock is set to MIDI / host
    bool hostPlaying = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        hostPlaying = pos->getIsPlaying();
        if (sq.s.ext) {
            if (auto bpm = pos->getBpm()) sq.s.tempo = *bpm;
            if (hostPlaying && !lastHostPlaying) { sq.start(); }
            if (!hostPlaying && lastHostPlaying && sq.playing) stopSeq();
            if (hostPlaying) if (auto ppq = pos->getPpqPosition()) {
                const double t = *ppq * PPQ, spt = fs * 60.0 / (std::max(20.0, sq.s.tempo) * PPQ);
                toTick = (std::ceil(t) - t) * spt;
            }
        }
    }
    lastHostPlaying = hostPlaying && sq.s.ext;
    juce::MidiBuffer out;
    for (auto& m : pendingMsgs) if (!m.empty() && m[0] == 0xF0 && m.size() > 2) out.addEvent(juce::MidiMessage::createSysExMessage(m.data() + 1, (int) m.size() - 2), 0);
    pendingMsgs.clear();
    pendingOut.clear();
    float* L = buffer.getWritePointer(0);
    if (stereoScratch.size() < (size_t) n) stereoScratch.resize((size_t) n);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : stereoScratch.data();
    int pos = 0;
    for (const auto meta : midi) {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        renderTo(L, R, pos, at); pos = at;
        handleMidi(meta.getMessage(), out, at);
    }
    renderTo(L, R, pos, n);
    if (buffer.getNumChannels() == 1) for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
    out.addEvents(pendingOut, 0, n, 0);
    midi.swapWith(out);
    frames += n;
    peak = std::max(peak.load(), (float) syn.peak); syn.peak = 0;
    stepShown = sq.playing ? sq.step : -1;
}

/* ---------------- message thread housekeeping ---------------- */
void SevenProcessor::timerCallback() {
    const int pr = pendingProgram.exchange(-1);
    if (pr >= 0) { const juce::ScopedLock l(lock); panel.mode = Panel::play; panel.selectVoice(pr); }
    if (pushParams.exchange(false)) syncParamsToHost();
}
void SevenProcessor::setCurrentProgram(int i) { if (i >= 0 && i < 32) pendingProgram = i; }
const juce::String SevenProcessor::getProgramName(int i) { return i >= 0 && i < 32 ? juce::String(voiceName(internal[(size_t) i])).trim() : juce::String(); }

/* ---------------- state ---------------- */
juce::var SevenProcessor::seqToVar() const {
    auto* o = new juce::DynamicObject(); const auto& s = sq.s;
    o->setProperty("tempo", s.tempo); o->setProperty("ext", s.ext); o->setProperty("pat", s.pat); o->setProperty("keyTrans", s.keyTrans); o->setProperty("transBase", s.transBase);
    juce::Array<juce::var> ch; for (int c : s.chain) ch.add(c); o->setProperty("chain", ch);
    auto* a = new juce::DynamicObject();
    a->setProperty("on", s.arp.on); a->setProperty("mode", s.arp.mode); a->setProperty("oct", s.arp.oct); a->setProperty("rate", s.arp.rate); a->setProperty("gate", s.arp.gate);
    a->setProperty("latch", s.arp.latch); a->setProperty("swing", s.arp.swing); o->setProperty("arp", juce::var(a));
    juce::Array<juce::var> pats;
    for (auto& p : s.pats) {
        auto* po = new juce::DynamicObject(); po->setProperty("len", p.len); po->setProperty("rate", p.rate); po->setProperty("swing", p.swing);
        juce::Array<juce::var> steps;
        for (auto& st : p.steps) { auto* so = new juce::DynamicObject(); so->setProperty("on", st.on); so->setProperty("gate", st.gate);
            juce::Array<juce::var> ns; for (auto& nv : st.notes) ns.add(juce::Array<juce::var> {nv.first, nv.second}); so->setProperty("notes", ns); steps.add(juce::var(so)); }
        po->setProperty("steps", steps);
        juce::Array<juce::var> lanes;
        for (auto& l : p.lanes) { auto* lo = new juce::DynamicObject(); lo->setProperty("param", l.param); lo->setProperty("smooth", l.smooth);
            juce::Array<juce::var> vs; for (int v : l.vals) vs.add(v); lo->setProperty("vals", vs); lanes.add(juce::var(lo)); }
        po->setProperty("lanes", lanes);
        pats.add(juce::var(po));
    }
    o->setProperty("pats", pats);
    return juce::var(o);
}
void SevenProcessor::seqFromVar(const juce::var& v) {
    if (!v.isObject()) return;
    SeqState s;
    s.tempo = (double) v.getProperty("tempo", 120); s.ext = (bool) v.getProperty("ext", false); s.pat = juce::jlimit(0, PATTERNS - 1, (int) v.getProperty("pat", 0));
    s.keyTrans = (bool) v.getProperty("keyTrans", true); s.transBase = (int) v.getProperty("transBase", 60);
    if (auto* ch = v.getProperty("chain", {}).getArray()) for (auto& c : *ch) s.chain.push_back(juce::jlimit(0, 15, (int) c));
    const auto a = v.getProperty("arp", {});
    s.arp.on = (bool) a.getProperty("on", false); s.arp.mode = juce::jlimit(0, 7, (int) a.getProperty("mode", 0)); s.arp.oct = juce::jlimit(1, 4, (int) a.getProperty("oct", 1));
    s.arp.rate = juce::jlimit(0, 5, (int) a.getProperty("rate", 3)); s.arp.gate = juce::jlimit(5, 100, (int) a.getProperty("gate", 50)); s.arp.latch = (bool) a.getProperty("latch", false);
    s.arp.swing = juce::jlimit(50, 75, (int) a.getProperty("swing", 50));
    if (auto* pats = v.getProperty("pats", {}).getArray())
        for (int i = 0; i < std::min(PATTERNS, pats->size()); ++i) {
            const auto& po = (*pats)[i]; auto& p = s.pats[(size_t) i];
            p.len = juce::jlimit(1, MAX_STEPS, (int) po.getProperty("len", 16)); p.rate = juce::jlimit(0, 5, (int) po.getProperty("rate", 3)); p.swing = juce::jlimit(50, 75, (int) po.getProperty("swing", 50));
            if (auto* steps = po.getProperty("steps", {}).getArray())
                for (int k = 0; k < std::min(MAX_STEPS, steps->size()); ++k) {
                    const auto& so = (*steps)[k]; auto& st = p.steps[(size_t) k];
                    st.on = (bool) so.getProperty("on", true); st.gate = juce::jlimit(5, TIE, (int) so.getProperty("gate", 75));
                    if (auto* ns = so.getProperty("notes", {}).getArray()) for (auto& nv : *ns) if (auto* pr = nv.getArray()) if (pr->size() >= 2) st.notes.push_back({(int) (*pr)[0], (int) (*pr)[1]});
                }
            if (auto* lanes = po.getProperty("lanes", {}).getArray())
                for (int k = 0; k < std::min(LANES, lanes->size()); ++k) {
                    const auto& lo = (*lanes)[k]; auto& l = p.lanes[(size_t) k];
                    l.param = (int) lo.getProperty("param", -1); l.smooth = (bool) lo.getProperty("smooth", false);
                    if (auto* vs = lo.getProperty("vals", {}).getArray()) for (int j = 0; j < std::min(MAX_STEPS, vs->size()); ++j) l.vals[(size_t) j] = (int) (*vs)[j];
                }
        }
    sq.s = s;
}
void SevenProcessor::getStateInformation(juce::MemoryBlock& dest) {
    const juce::ScopedLock l(lock);
    juce::XmlElement x("OmegaSeven");
    auto b64 = [](const std::vector<uint8_t>& d) { return juce::Base64::toBase64(d.data(), d.size()); };
    x.setAttribute("internal", b64(bankToSysex(internal, 0)));
    if (cart) { x.setAttribute("cart", b64(bankToSysex(*cart, 0))); x.setAttribute("cartName", cartName); }
    x.setAttribute("edit", juce::Base64::toBase64(edit.data(), edit.size()));
    for (int k = 0; k < FCOUNT; ++k) x.setAttribute(juce::String("f_") + FUNC_DEF[k].key, syn.func[k]);
    x.setAttribute("cartSel", panel.cart); x.setAttribute("num", panel.num); x.setAttribute("protI", panel.protect[0]); x.setAttribute("protC", panel.protect[1]);
    x.setAttribute("op", panel.op); x.setAttribute("edited", panel.edited);
    x.setAttribute("seq", juce::JSON::toString(seqToVar(), true));
    copyXmlToBinary(x, dest);
}
void SevenProcessor::setStateInformation(const void* data, int size) {
    auto x = getXmlFromBinary(data, size);
    if (!x || !x->hasTagName("OmegaSeven")) return;
    const juce::ScopedLock l(lock);
    auto unb64 = [](const juce::String& s) { juce::MemoryOutputStream o; juce::Base64::convertFromBase64(o, s); return o.getMemoryBlock(); };
    { const auto mb = unb64(x->getStringAttribute("internal")); const auto r = parseSysexFile((const uint8_t*) mb.getData(), mb.getSize()); if (r.hasBank) internal = r.bank; }
    cart.reset(); cartName.clear();
    if (x->hasAttribute("cart")) { const auto mb = unb64(x->getStringAttribute("cart")); const auto r = parseSysexFile((const uint8_t*) mb.getData(), mb.getSize()); if (r.hasBank) { cart = r.bank; cartName = x->getStringAttribute("cartName"); } }
    { const auto mb = unb64(x->getStringAttribute("edit")); if (mb.getSize() == 156) { std::memcpy(edit.data(), mb.getData(), 156); clampVoice(edit); } }
    for (int k = 0; k < FCOUNT; ++k) syn.setFunc(k, x->getIntAttribute(juce::String("f_") + FUNC_DEF[k].key, FUNC_DEF[k].def));
    panel.cart = x->getBoolAttribute("cartSel") && cart.has_value(); panel.num = juce::jlimit(0, 31, x->getIntAttribute("num")); panel.protect[0] = x->getBoolAttribute("protI", true);
    panel.protect[1] = x->getBoolAttribute("protC", true); panel.op = juce::jlimit(1, 6, x->getIntAttribute("op", 1)); panel.edited = x->getBoolAttribute("edited");
    seqFromVar(juce::JSON::parse(x->getStringAttribute("seq")));
    syn.loadVoice(edit); voiceVer++; pushParams = true;
}

juce::AudioProcessorEditor* SevenProcessor::createEditor() { return new SevenEditor(*this); }

}  // namespace seven

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new seven::SevenProcessor(); }
