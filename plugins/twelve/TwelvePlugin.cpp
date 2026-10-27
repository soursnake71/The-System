#include "TwelvePlugin.h"
#include "TwelveEditor.h"

namespace twelve {

juce::String fmtVal(int id, double v) {
    const auto& p = PARAMS[id];
    if (!p.choices.empty()) return juce::CharPointer_UTF8(p.choices[(size_t) juce::jlimit(0, (int) p.choices.size() - 1, (int) std::lround(v))]);
    if (p.toggle) return v > .5 ? "On" : "Off";
    const std::string u = p.unit;
    if (u == "Hz") return v >= 1000 ? juce::String(v / 1000, v >= 10000 ? 1 : 2) + "k" : juce::String((int) std::lround(v));
    if (u == "%") return juce::String((int) std::lround(v * 100)) + "%";
    if (u == "dB") return (v > 0 ? "+" : "") + juce::String(v, 1);
    if (u == "st") return (v > 0 ? "+" : "") + juce::String((int) std::lround(v)) + " st";
    if (u == "ct") return (v > 0 ? "+" : "") + juce::String((int) std::lround(v)) + " ct";
    if (u == "x") return juce::String(v, 2) + "x";
    if (u == "ms") return juce::String(v, v < 10 ? 1 : 0) + " ms";
    if (u == "bit") return juce::String((int) std::lround(v));
    if (u == ":1") return juce::String(v, 1) + ":1";
    return juce::String(v, std::fabs(v) < 10 ? 2 : 1) + juce::String(p.unit);
}

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < pid::count; ++i) {
        const auto& p = PARAMS[i];
        const juce::ParameterID id { p.id, 1 };
        if (!p.choices.empty()) {
            juce::StringArray c; for (auto* s : p.choices) c.add(juce::CharPointer_UTF8(s));
            l.add(std::make_unique<juce::AudioParameterChoice>(id, p.name, c, (int) p.def));
        } else if (p.toggle) {
            l.add(std::make_unique<juce::AudioParameterBool>(id, p.name, p.def > .5));
        } else {
            const double mn = p.min, mx = p.max, st = p.step; const bool lg = p.log;
            juce::NormalisableRange<float> r((float) mn, (float) mx,
                [=](float, float, float n) { return (float) (lg ? mn * std::pow(mx / mn, (double) n) : mn + n * (mx - mn)); },
                [=](float, float, float v) { return (float) (lg ? std::log(v / mn) / std::log(mx / mn) : (v - mn) / (mx - mn)); },
                [=](float, float, float v) { double x = juce::jlimit(mn, mx, (double) v); if (st > 0) x = std::round(x / st) * st; return (float) x; });
            l.add(std::make_unique<juce::AudioParameterFloat>(id, p.name, r, (float) p.def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction([i](float v, int) { return fmtVal(i, v); })));
        }
    }
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "live", 1 }, "Live input", true));
    return l;
}

TwelveProcessor::TwelveProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), TWELVE_INSTRUMENT == 0).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("Twelve render"), apvts(*this, nullptr, "STATE", makeLayout()) {
    for (int i = 0; i < pid::count; ++i) raw[(size_t) i] = apvts.getRawParameterValue(PARAMS[i].id);
    liveRaw = apvts.getRawParameterValue("live");
    for (auto& m : matchG) m = 0;
    formats.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg Vorbis, MP3; plus CoreAudio (Mac) / Windows Media formats
#if TWELVE_INSTRUMENT
    apvts.getParameter("live")->setValueNotifyingHost(0);
#endif
    resetCCMap();
    for (auto& c : ccVal) c = 0;
    ccDirty[0] = 0; ccDirty[1] = 0;
    startThread();
    startTimerHz(30);
}

// sensible defaults so a controller works before anything is learned (General MIDI sound-controller numbers)
void TwelveProcessor::resetCCMap() {
    for (auto& c : ccMap) c = -1;
    ccMap[1] = pid::cutoff; ccMap[74] = pid::cutoff; ccMap[71] = pid::res; ccMap[73] = pid::fenv; ccMap[72] = pid::drive;
    ccMap[7] = pid::output; ccMap[91] = pid::crackle; ccMap[93] = pid::wow; ccMap[94] = pid::hiss; ccMap[95] = pid::bits;
}
void TwelveProcessor::timerCallback() {
    const int prog = pendingProgram.exchange(-1);
    if (prog >= 0) setCurrentProgram(prog % kPresets);
    for (int w = 0; w < 2; ++w) {
        uint64_t bits = ccDirty[w].exchange(0);
        for (int b = 0; b < 64 && bits; ++b) {
            if (!(bits & (1ull << b))) continue;
            bits &= ~(1ull << b);
            const int cc = w * 64 + b, id = ccMap[(size_t) cc].load();
            if (id >= 0) if (auto* prm = apvts.getParameter(PARAMS[id].id)) prm->setValueNotifyingHost(ccVal[(size_t) cc].load());
        }
    }
}

TwelveProcessor::~TwelveProcessor() { stopTimer(); stopThread(4000); pool.removeAllJobs(true, 4000); }

bool TwelveProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto in = l.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

Params TwelveProcessor::snapshot() const { Params p; for (int i = 0; i < pid::count; ++i) p[i] = raw[(size_t) i]->load(); return p; }

void TwelveProcessor::setParam(int id, double v) {
    auto* prm = apvts.getParameter(PARAMS[id].id);
    prm->setValueNotifyingHost(prm->convertTo0to1((float) v));
}
void TwelveProcessor::applyValues(const Params& p, bool soundOnly) {
    for (int i = 0; i < pid::count; ++i) if (!soundOnly || !isMaster(i)) setParam(i, p[i]);
}
void TwelveProcessor::setCurrentProgram(int i) {
    if (i < 0 || i >= kPresets) return;
    program = i; Params p = snapshot(); applyPreset(p, i); applyValues(p, true);
    setPresetName(juce::CharPointer_UTF8(PRESETS[i].name));
}
void TwelveProcessor::randomize() {
    Params p = snapshot();
    const auto name = randomPatch(p, (uint32_t) juce::Random::getSystemRandom().nextInt() ^ juce::Time::getMillisecondCounter());
    applyValues(p, true); setPresetName(juce::CharPointer_UTF8(name.c_str()));
}

void TwelveProcessor::prepareToPlay(double sampleRate, int maxBlock) {
    hostFs = sampleRate;
    for (int c = 0; c < 2; ++c) { conv[c] = Converter(sampleRate); filt[c] = OutFilter(sampleRate); dc[c] = 0; }
    lofi = Lofi(sampleRate, 4321); master = Master(sampleRate);
    for (auto& s2 : spl) s2 = sp::SPLive(sampleRate);
    primed = false; lbuf.assign((size_t) juce::jmax(512, maxBlock), 0.f); rbuf = lbuf;
    setLatencySamples(master.latency() + sp::SPLive(sampleRate).latency());
    for (auto& v : voices) v = Voice();
}

/* ---------------- audio ---------------- */
void TwelveProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples(), nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    const double fs = hostFs;
    const Params p = snapshot();
    const bool live = liveRaw->load() > .5;

    // set up whatever changed (compared group by group, no allocation)
    bool chLive = !primed, chLofi = !primed, chMaster = !primed;
    for (int i = 0; i < pid::count; ++i) if (p[i] != lastP[i]) { const char g = PARAMS[i].group[0]; if (g == 'm') chMaster = true; else if (g == 'l') chLofi = true; else chLive = true; }
    lastP = p; primed = true;
    if (chLive) {
        for (int c = 0; c < 2; ++c) { conv[c].setup(p); filt[c].setup(p); }
        // SP-1200 / SP-12: live input through the circuit-level path (SP1200.h)
        spMode = MACHINES[(int) clampd(p[pid::machine], 0, 6)].pitch == 0;
        if (spMode) {
            sp::SPLive::Settings s2; const int ft = (int) p[pid::ftype];
            s2.clock = p[pid::rate]; s2.level = driveGain(p); s2.bits = p[pid::bits]; s2.rpm = p[pid::rpm]; s2.rest = p[pid::cutoff]; s2.res = p[pid::res];
            s2.depth = p[pid::fenv]; s2.output = ft == 0 ? 1 : ft <= 4 ? ft + 2 : 7; s2.measured = p[pid::calib] > .5; s2.sp12 = (int) p[pid::machine] == 1;
            for (auto& x : spl) x.setup(s2);
        }
    }
    if (chLofi) lofi.setup(p);
    std::array<float, 10> m; double md[10];
    for (int i = 0; i < 10; ++i) { m[(size_t) i] = matchG[(size_t) i].load(); md[i] = m[(size_t) i]; }
    if (chMaster || m != lastMatch) { master.setup(p, md); lastMatch = m; }

    if (stopRequest.exchange(false)) {
        for (auto& v : voices) v.active = false;
        for (auto& q : pend) q.used = false;
        master.reset(); for (int c = 0; c < 2; ++c) { filt[c].reset(); conv[c].reset(); }
        bend = 0;
        mainPlaying = false;
    }
    if (playRequest.exchange(false)) {
        for (auto& v : voices) if (v.main) v.active = false;
        if (auto b = noteBuffer(0, false)) {
            for (auto& v : voices) if (!v.active) { v = Voice(); v.buf = b; v.main = true; v.active = true; v.pos = playFrom.load() * (double) b->at(0).size(); break; }
        }
    }
    // MIDI: middle C plays the sample at its own pitch
    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) {
            const int off = msg.getNoteNumber() - 60;
            if (off < -24 || off > 24) continue;
            for (auto& q : pend) if (!q.used) { q.used = true; q.note = msg.getNoteNumber(); q.off = off; q.vel = msg.getFloatVelocity(); q.age = 0; break; }
            if (spMode) for (auto& x : spl) x.trigger();   // a note fires the SSM2044 envelope on live input, like a pad hit
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff() || (msg.isController() && (msg.getControllerNumber() == 120 || msg.getControllerNumber() == 123))) {
            for (auto& v : voices) v.active = false;
            for (auto& q : pend) q.used = false;
        } else if (msg.isPitchWheel()) {
            bend = (msg.getPitchWheelValue() - 8192) / 8192.0 * 2;   // +-2 semitones
        } else if (msg.isProgramChange()) {
            pendingProgram = msg.getProgramChangeNumber() + 128 * (bankMsb.load() & 3);
        } else if (msg.isController()) {
            const int cc = msg.getControllerNumber(), learn = learnParam.load();
            if (cc == 0) { bankMsb = msg.getControllerValue(); continue; }
            lastCC = cc;
            if (learn >= 0) { for (auto& c : ccMap) if (c.load() == learn) c = -1; ccMap[(size_t) cc] = learn; learnParam = -1; }
            ccVal[(size_t) cc] = msg.getControllerValue() / 127.f; ccDirty[cc >> 6].fetch_or(1ull << (cc & 63));
        }
    }
    for (auto& q : pend) {
        if (!q.used) continue;
        if (auto b = noteBuffer(q.off, false)) {
            // one voice per note: a new hit cuts the old one, like the hardware's channels
            for (auto& v : voices) if (v.active && v.note == q.note && !v.main) v.active = false;
            for (auto& v : voices) if (!v.active) { v = Voice(); v.buf = b; v.note = q.note; v.gain = q.vel; v.active = true; break; }
            q.used = false;
        } else { requestNote(q.off); q.age += n / fs; if (q.age > 3) q.used = false; }
    }

    const float* inL = nIn > 0 ? buf.getReadPointer(0) : nullptr;
    const float* inR = nIn > 1 ? buf.getReadPointer(1) : inL;
    if ((int) lbuf.size() < n) { lbuf.resize((size_t) n); rbuf.resize((size_t) n); }
    float* l = lbuf.data(); float* r = rbuf.data();
    const double inGain = dbToGain(p[pid::input]), outGain = dbToGain(p[pid::output]), mix = p[pid::mix], dv = 1 + p[pid::drive] * 3;
    const bool mono = p[pid::mono] > .5;
    for (int i = 0; i < n; ++i) {
        double yl = 0, yr = 0;
        if (live && inL) {
            const double xl = mono ? (inL[i] + inR[i]) * .5 : inL[i], xr = mono ? xl : inR[i];
            double a, b;
            if (spMode) {
                a = spl[0].process((float) xl); dc[0] += (a - dc[0]) * (6 / fs); a = (a - dc[0]) / dv;
                if (mono) b = a; else { b = spl[1].process((float) xr); dc[1] += (b - dc[1]) * (6 / fs); b = (b - dc[1]) / dv; }
            } else {
                a = conv[0].process(xl); dc[0] += (a - dc[0]) * (6 / fs); a = filt[0].process(a - dc[0]) / dv;
                if (mono) b = a; else { b = conv[1].process(xr); dc[1] += (b - dc[1]) * (6 / fs); b = filt[1].process(b - dc[1]) / dv; }
            }
            yl = a * mix + xl * inGain * (1 - mix); yr = b * mix + xr * inGain * (1 - mix);
            lofi.process(yl, yr); yl *= outGain; yr *= outGain;
        }
        l[(size_t) i] = (float) yl; r[(size_t) i] = (float) yr;
    }
    // sampler voices (their renders already include the filter, lo-fi and output level)
    bool anyMain = false;
    for (auto& v : voices) {
        if (!v.active) continue;
        const auto& b = *v.buf; const double len = (double) b[0].size(), rate = v.main ? 1.0 : std::pow(2.0, bend.load() / 12);
        for (int i = 0; i < n; ++i) {
            if (v.pos >= len - 1) { if (v.main && loop) v.pos = 0; else { v.active = false; break; } }
            const size_t k = (size_t) v.pos; const float f = (float) (v.pos - (double) k);
            l[(size_t) i] += (b[0][k] + (b[0][k + 1] - b[0][k]) * f) * (float) v.gain; r[(size_t) i] += (b[1][k] + (b[1][k + 1] - b[1][k]) * f) * (float) v.gain;
            v.pos += rate;
        }
        if (v.main && v.active) { anyMain = true; mainFrac = v.pos / len; }
    }
    mainPlaying = anyMain;
    for (int i = 0; i < n; ++i) {
        double a = l[(size_t) i], b = r[(size_t) i];
        if (master.enabled()) master.process(a, b);
        l[(size_t) i] = (float) a; r[(size_t) i] = (float) b;
    }
    if (nOut > 0) buf.copyFrom(0, 0, l, n);
    if (nOut > 1) buf.copyFrom(1, 0, r, n);
    else if (nOut == 1) { auto* o = buf.getWritePointer(0); for (int i = 0; i < n; ++i) o[i] = (l[(size_t) i] + r[(size_t) i]) * .5f; }
    for (int c = 2; c < nOut; ++c) buf.clear(c, 0, n);
    midi.clear();
}

std::shared_ptr<const Chans> TwelveProcessor::noteBuffer(int off, bool blocking) {
    std::shared_ptr<const Chans> b;
    if (blocking) { const juce::SpinLock::ScopedLockType l(notesLock); auto it = notes.find(off); if (it != notes.end()) b = it->second; }
    else { const juce::SpinLock::ScopedTryLockType l(notesLock); if (l.isLocked()) { auto it = notes.find(off); if (it != notes.end()) b = it->second; } }
    return b;
}

/* ---------------- background renders ---------------- */
std::vector<double> TwelveProcessor::soundKey(const Params& p) {
    std::vector<double> k;
    for (int i = 0; i < pid::count; ++i) if (!isMaster(i)) k.push_back(p[i]);
    const juce::ScopedLock l(srcLock);
    k.push_back((double) selA); k.push_back((double) selB); k.push_back(version.load()); k.push_back(hostFs.load());
    return k;
}
std::shared_ptr<const Chans> TwelveProcessor::prepared(const Params& p, Chans&, double& sr) {
    std::vector<double> k { p[pid::mono], p[pid::stretch], p[pid::grain] };
    Chans sel;
    {
        const juce::ScopedLock l(srcLock);
        if (src.empty()) return nullptr;
        k.push_back((double) selA); k.push_back((double) selB); k.push_back(version.load());
        sr = srcSr;
        if (prep && k == prepKey) return prep;
        for (auto& c : src) sel.emplace_back(c.begin() + selA, c.begin() + selB);
    }
    prep = std::make_shared<const Chans>(prepareSource(sel, p, sr)); prepKey = k; prepSr = sr;
    return prep;
}
void TwelveProcessor::renderOffset(const Params& p, int off, bool mainRender) {
    Chans dummy; double sr = 44100;
    auto ps = prepared(p, dummy, sr);
    if (!ps) return;
    const double fs = hostFs;
    auto out = std::make_shared<const Chans>(renderPre(*ps, sr, p, fs, off));
    if (mainRender) {
        Params mp = snapshot(); double md[10]; for (int i = 0; i < 10; ++i) md[i] = matchG[(size_t) i].load();
        const auto mo = renderMaster(*out, mp, md, fs);
        const juce::ScopedLock l(infoLock);
        outText = juce::String(loudness(mo, fs), 1) + " LUFS  peak " + juce::String(peakDb(mo), 1) + " dB";
    }
    const juce::SpinLock::ScopedLockType l(notesLock);
    if (mainRender) notes.clear();
    notes[off] = out;
}
void TwelveProcessor::run() {
    while (!threadShouldExit()) {
        wait(25);
        if (!hasSource()) continue;
        const Params p = snapshot();
        const auto k = soundKey(p);
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (k != renderedKey) {
            if (k != pendingKey) { pendingKey = k; pendingSince = now; continue; }
            if (now - pendingSince < 70) continue;
            rendering = true; renderOffset(p, 0, true); renderedKey = k; rendering = false;
            continue;
        }
        const uint64_t bits = noteBits.exchange(0);
        for (int i = 0; i < 49 && !threadShouldExit(); ++i) if (bits & (1ull << i)) {
            if (!noteBuffer(i - 24, true)) { rendering = true; renderOffset(p, i - 24, false); rendering = false; }
            if (soundKey(snapshot()) != renderedKey) break;
        }
    }
}

/* ---------------- source editing ---------------- */
void TwelveProcessor::pushUndo() { if (!src.empty()) { undos.push_back({src, srcSr, srcName, selA, selB}); if (undos.size() > 12) undos.erase(undos.begin()); } }
void TwelveProcessor::afterSourceChange(bool selectAll) {
    if (selectAll || selB > (int64_t) src[0].size() || selB <= selA) { selA = 0; selB = (int64_t) src[0].size(); }
    version++;
}
juce::String TwelveProcessor::loadFile(const juce::File& f) {
    std::unique_ptr<juce::AudioFormatReader> rd(formats.createReaderFor(f));
    if (!rd) return "Can't read " + f.getFileName() + " (try WAV, AIFF, FLAC, OGG or MP3)";
    if (rd->lengthInSamples <= 0 || rd->lengthInSamples > (int64_t) rd->sampleRate * 60 * 30) return "That file is empty or longer than 30 minutes";
    const int nc = (int) juce::jmin<unsigned>(2, rd->numChannels), n = (int) rd->lengthInSamples;
    juce::AudioBuffer<float> b(nc, n);
    rd->read(&b, 0, n, 0, true, nc > 1);
    Chans c; for (int i = 0; i < nc; ++i) c.emplace_back(b.getReadPointer(i), b.getReadPointer(i) + n);
    const juce::ScopedLock l(srcLock);
    pushUndo(); src = std::move(c); srcSr = rd->sampleRate; srcName = f.getFileName(); afterSourceChange(true);
    tempo = 0; key = Key();
    return {};
}
void TwelveProcessor::getPeaks(std::vector<float>& mn, std::vector<float>& mx, int64_t a, int64_t b, int w) const {
    const juce::ScopedLock l(srcLock);
    mn.assign((size_t) w, 0.f); mx.assign((size_t) w, 0.f);
    if (src.empty() || b <= a) return;
    const auto& d0 = src[0]; const auto& d1 = src.size() > 1 ? src[1] : src[0];
    for (int x = 0; x < w; ++x) {
        const int64_t s = a + (b - a) * x / w, e = juce::jmax(s + 1, a + (b - a) * (x + 1) / w), step = juce::jmax<int64_t>(1, (e - s) / 64);
        float lo = 0, hi = 0;
        for (int64_t i = s; i < e && i < (int64_t) d0.size(); i += step) { const float v = (d0[(size_t) i] + d1[(size_t) i]) * .5f; lo = juce::jmin(lo, v); hi = juce::jmax(hi, v); }
        mn[(size_t) x] = lo; mx[(size_t) x] = hi;
    }
}
void TwelveProcessor::setSelection(int64_t a, int64_t b) {
    const juce::ScopedLock l(srcLock);
    if (src.empty()) return;
    const int64_t n = (int64_t) src[0].size(); a = juce::jlimit<int64_t>(0, n - 1, a); b = juce::jlimit<int64_t>(a + 1, n, b);
    selA = a; selB = b;
}
juce::String TwelveProcessor::truncate() {
    const juce::ScopedLock l(srcLock);
    if (src.empty()) return "Load a sample first";
    if (selA == 0 && selB == (int64_t) src[0].size()) return "Select part of the sample first (drag on the waveform)";
    pushUndo(); for (auto& c : src) c = Buf(c.begin() + selA, c.begin() + selB);
    afterSourceChange(true);
    return "Truncated to " + juce::String((double) src[0].size() / srcSr, 3) + " s";
}
juce::String TwelveProcessor::normalize() {
    const juce::ScopedLock l(srcLock);
    if (src.empty()) return "Load a sample first";
    float pk = 1e-9f; for (auto& c : src) for (int64_t i = selA; i < selB; ++i) pk = juce::jmax(pk, std::fabs(c[(size_t) i]));
    pushUndo(); const float g = .989f / pk; for (auto& c : src) for (int64_t i = selA; i < selB; ++i) c[(size_t) i] *= g;
    afterSourceChange(false);
    return "Normalized (" + juce::String(20 * std::log10(g), 1) + " dB)";
}
juce::String TwelveProcessor::reverse() {
    const juce::ScopedLock l(srcLock);
    if (src.empty()) return "Load a sample first";
    pushUndo(); for (auto& c : src) std::reverse(c.begin() + selA, c.begin() + selB);
    afterSourceChange(false);
    return "Reversed the selection";
}
juce::String TwelveProcessor::undo() {
    const juce::ScopedLock l(srcLock);
    if (undos.empty()) return "Nothing to undo";
    auto u = std::move(undos.back()); undos.pop_back();
    src = std::move(u.src); srcSr = u.sr; srcName = u.name; selA = u.a; selB = u.b; afterSourceChange(false);
    return "Undone";
}
juce::String TwelveProcessor::resample() {
    auto b = noteBuffer(0, true);
    if (!b) return "Nothing rendered yet";
    Params p = snapshot(); double md[10]; for (int i = 0; i < 10; ++i) md[i] = matchG[(size_t) i].load();
    Chans out = renderMaster(*b, p, md, hostFs);
    {
        const juce::ScopedLock l(srcLock);
        pushUndo(); src = std::move(out); srcSr = hostFs; srcName = srcName.upToLastOccurrenceOf(" (resampled)", false, false) + " (resampled)"; afterSourceChange(true);
    }
    // the sound is in the sample now: reset the effects so nothing is applied twice
    Params q; setMachine(q, 6); q[pid::mono] = 0; q[pid::fenv] = 0; q[pid::retrig] = 0; q[pid::drive] = 0; q[pid::cutoff] = 20000; q[pid::res] = 0;
    applyValues(q, true); setParam(pid::mOn, 0); setPresetName("Resampled");
    return "Resampled. Effects reset to Clean and the master switched off so nothing is applied twice. Undo brings the old sample back.";
}
void TwelveProcessor::play(double from) { playFrom = juce::jlimit(0.0, .999, from); playRequest = true; }

/* ---------------- jobs ---------------- */
void TwelveProcessor::runJob(std::function<juce::String()> job, std::function<void(juce::String)> done) {
    pool.addJob([job, done] { auto r = job(); juce::MessageManager::callAsync([done, r] { done(r); }); });
}
static Chans selectionOf(const Chans& src, int64_t a, int64_t b) { Chans c; for (auto& x : src) c.emplace_back(x.begin() + a, x.begin() + b); return c; }
juce::String TwelveProcessor::analyze() {
    Chans c; double sr;
    { const juce::ScopedLock l(srcLock); if (src.empty()) return "Load a sample first"; c = selectionOf(src, selA, selB); sr = srcSr; }
    tempo = detectTempo(c, sr); key = detectKey(c, sr);
    return (tempo > 0 ? juce::String(tempo, 1) + " BPM" : juce::String("Too short for a tempo")) + " · " + juce::String(key.name());
}
juce::String TwelveProcessor::warp(double from, double to, bool stretchMode) {
    if (!(from > 0) || !(to > 0)) return "Set the sample BPM (Detect) and a target BPM";
    if (stretchMode) setParam(pid::stretch, clampd(from / to, .25, 4));
    else {
        const double c = 1200 * std::log2(to / from), st = std::round(c / 100);
        setParam(pid::semis, clampd(st, -24, 24)); setParam(pid::cents, clampd(std::round(c - st * 100), -100, 100)); setParam(pid::stretch, 1);
    }
    return "Warped " + juce::String(from, 1) + " -> " + juce::String(to, 1) + " BPM (" + (stretchMode ? "pitch kept)" : "pitch moves with the speed)");
}
juce::String TwelveProcessor::transposeTo(int root) {
    if (key.root < 0) return "Detect the key first";
    int d = (root - key.root + 12) % 12; if (d > 6) d -= 12;
    setParam(pid::semis, d);
    return "Transposed " + juce::String(d >= 0 ? "+" : "") + juce::String(d) + " semitones to " + NOTE_NAMES[root] + (key.minor ? " minor" : " major");
}
juce::String TwelveProcessor::loadReference(const juce::File& f) {
    std::unique_ptr<juce::AudioFormatReader> rd(formats.createReaderFor(f));
    if (!rd) return "Can't read " + f.getFileName();
    const int nc = (int) juce::jmin<unsigned>(2, rd->numChannels), n = (int) juce::jmin<int64_t>(rd->lengthInSamples, (int64_t) (rd->sampleRate * 600));
    juce::AudioBuffer<float> b(nc, n); rd->read(&b, 0, n, 0, true, nc > 1);
    Chans c; for (int i = 0; i < nc; ++i) c.emplace_back(b.getReadPointer(i), b.getReadPointer(i) + n);
    refBands = bandSpectrum(c, rd->sampleRate); hasRef = true; refName = f.getFileName();
    return "Reference loaded: " + refName;
}
juce::String TwelveProcessor::matchEq() {
    auto b = noteBuffer(0, true); if (!b) return "Load a sample first";
    std::array<double, 10> ref {}; const auto cur = bandSpectrum(*b, hostFs);
    if (hasRef) ref = refBands; else for (int i = 0; i < 10; ++i) ref[(size_t) i] = TARGET_CURVE[i];
    const auto g = matchGains(ref, cur);
    for (int i = 0; i < 10; ++i) matchG[(size_t) i] = (float) g[(size_t) i];
    if (raw[pid::match]->load() <= 0) setParam(pid::match, .8);
    setParam(pid::mOn, 1);
    return hasRef ? "EQ matched to " + refName : juce::String("EQ matched to a typical finished mix (load a reference to match a track)");
}
juce::String TwelveProcessor::autoMasterNow(double target) {
    auto b = noteBuffer(0, true); if (!b) return "Load a sample first";
    const auto r = autoMaster(*b, hostFs, hasRef ? &refBands : nullptr, target);
    for (int i = 0; i < pid::count; ++i) if (isMaster(i)) setParam(i, r.params[i]);
    for (int i = 0; i < 10; ++i) matchG[(size_t) i] = (float) r.match[(size_t) i];
    return "Auto master: compressor at " + juce::String(r.params[pid::cThr], 1) + " dB, gain " + juce::String(r.params[pid::mGain], 1) + " dB, ceiling -1 dB";
}
void TwelveProcessor::clearMatch() { for (auto& m : matchG) m = 0; setParam(pid::match, 0); }
void TwelveProcessor::resetMaster() { Params d; for (int i = 0; i < pid::count; ++i) if (isMaster(i)) setParam(i, d[i]); for (auto& m : matchG) m = 0; }

juce::String TwelveProcessor::exportTo(const juce::File& f, const juce::String& fmt, double fs, bool whole) {
    Chans c; double sr;
    {
        const juce::ScopedLock l(srcLock); if (src.empty()) return "Load a sample first";
        c = whole ? src : selectionOf(src, selA, selB); sr = srcSr;
    }
    const Params p = snapshot(); double md[10]; for (int i = 0; i < 10; ++i) md[i] = matchG[(size_t) i].load();
    const Chans out = renderMaster(renderPre(prepareSource(c, p, sr), sr, p, fs), p, md, fs);
    const auto kind = fmt.upToFirstOccurrenceOf(":", false, false); const int q = fmt.fromFirstOccurrenceOf(":", false, false).getIntValue();
    std::unique_ptr<juce::AudioFormat> af;
    if (kind == "wav") af = std::make_unique<juce::WavAudioFormat>();
    else if (kind == "aiff") af = std::make_unique<juce::AiffAudioFormat>();
    else if (kind == "flac") af = std::make_unique<juce::FlacAudioFormat>();
    else af = std::make_unique<juce::OggVorbisAudioFormat>();
    const int bits = kind == "ogg" ? 16 : q;
    const int quality = kind == "ogg" ? juce::jlimit(0, af->getQualityOptions().size() - 1, q) : 0;
    f.deleteFile();
    auto os = f.createOutputStream();
    if (!os) return "Can't write " + f.getFullPathName();
    std::unique_ptr<juce::AudioFormatWriter> w(af->createWriterFor(os.get(), fs, 2, bits, {}, quality));
    if (!w) return "This format can't be written at " + juce::String(fs, 0) + " Hz / " + juce::String(bits) + "-bit";
    os.release();
    const float* ch[2] = { out[0].data(), out[1].data() };
    w->writeFromFloatArrays(ch, 2, (int) out[0].size());
    w.reset();
    return "Saved " + f.getFileName() + " (" + juce::String((double) f.getSize() / 1048576.0, 2) + " MB)";
}

/* ---------------- state ---------------- */
void TwelveProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = apvts.copyState();
    juce::ValueTree x("TWELVE");
    x.setProperty("name", presetName(), nullptr); x.setProperty("program", program, nullptr); x.setProperty("loop", loop, nullptr);
    juce::String m; for (auto& g : matchG) m << juce::String(g.load(), 3) << " "; x.setProperty("match", m.trim(), nullptr);
    juce::String cm; for (int c = 0; c < 128; ++c) if (ccMap[(size_t) c].load() >= 0) cm << c << ":" << PARAMS[ccMap[(size_t) c].load()].id << " ";
    x.setProperty("ccmap", cm.trim(), nullptr);
    {
        const juce::ScopedLock l(srcLock);
        // the sample itself goes into the project (FLAC, up to 10 minutes) so it comes back when the song is opened
        if (!src.empty() && (double) src[0].size() / srcSr <= 600) {
            // the writer owns the stream and finishes the file when it's deleted, so write into a block we keep
            juce::MemoryBlock mb; juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(new juce::MemoryOutputStream(mb, false), srcSr, (unsigned) src.size(), 24, {}, 0));
            if (w) {
                std::vector<const float*> ch; for (auto& c : src) ch.push_back(c.data());
                w->writeFromFloatArrays(ch.data(), (int) ch.size(), (int) src[0].size()); w.reset();
                x.setProperty("sample", mb.toBase64Encoding(), nullptr);
                x.setProperty("sampleName", srcName, nullptr); x.setProperty("selA", (juce::int64) selA, nullptr); x.setProperty("selB", (juce::int64) selB, nullptr);
            }
        }
    }
    st.appendChild(x, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
}
void TwelveProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size); if (!xml) return;
    auto st = juce::ValueTree::fromXml(*xml); if (!st.isValid()) return;
    auto x = st.getChildWithName("TWELVE");
    if (x.isValid()) st.removeChild(x, nullptr);
    apvts.replaceState(st);
    if (!x.isValid()) return;
    setPresetName(x.getProperty("name", "Init").toString()); program = x.getProperty("program", 0); loop = x.getProperty("loop", false);
    juce::StringArray m; m.addTokens(x.getProperty("match").toString(), " ", ""); for (int i = 0; i < 10 && i < m.size(); ++i) matchG[(size_t) i] = m[i].getFloatValue();
    if (x.hasProperty("ccmap")) {
        for (auto& c : ccMap) c = -1;
        juce::StringArray cm; cm.addTokens(x.getProperty("ccmap").toString(), " ", "");
        for (auto& e : cm) { const int cc = e.upToFirstOccurrenceOf(":", false, false).getIntValue(); const auto id = e.fromFirstOccurrenceOf(":", false, false);
            for (int i = 0; i < pid::count; ++i) if (id == PARAMS[i].id && cc >= 0 && cc < 128) ccMap[(size_t) cc] = i; }
    }
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding(x.getProperty("sample").toString()) && mb.getSize() > 0) {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> rd(flac.createReaderFor(new juce::MemoryInputStream(mb, true), true));
        if (rd) {
            const int nc = (int) rd->numChannels, n = (int) rd->lengthInSamples;
            juce::AudioBuffer<float> b(nc, n); rd->read(&b, 0, n, 0, true, true);
            Chans c; for (int i = 0; i < nc; ++i) c.emplace_back(b.getReadPointer(i), b.getReadPointer(i) + n);
            const juce::ScopedLock l(srcLock);
            src = std::move(c); srcSr = rd->sampleRate; srcName = x.getProperty("sampleName").toString();
            afterSourceChange(true); setSelection((juce::int64) x.getProperty("selA", 0), (juce::int64) x.getProperty("selB", (juce::int64) n));
        }
    }
}

juce::AudioProcessorEditor* TwelveProcessor::createEditor() { return new TwelveEditor(*this); }

}  // namespace twelve

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new twelve::TwelveProcessor(); }
