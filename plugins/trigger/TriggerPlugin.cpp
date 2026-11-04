#include "TriggerPlugin.h"
#include "TriggerEditor.h"

namespace trig {

using APVTS = juce::AudioProcessorValueTreeState;

static juce::NormalisableRange<float> logRange(float lo, float hi) {
    return { lo, hi, [](float a, float b, float v) { return a * std::pow(b / a, v); },
             [](float a, float b, float x) { return std::log(x / a) / std::log(b / a); },
             [](float, float, float x) { return (float) std::round(x); } };
}

static APVTS::ParameterLayout layout() {
    using namespace juce;
    APVTS::ParameterLayout l;
    auto db = [](float v, int) { return String(v, 1) + " dB"; };
    auto ms = [](float v, int) { return String(v, v < 10 ? 1 : 0) + " ms"; };
    auto hz = [](float v, int) { return v >= 1000 ? String(v / 1000, 2) + " kHz" : String((int) v) + " Hz"; };
    auto pct = [](float v, int) { return String((int) std::round(v)) + " %"; };
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("thresh", 1), "Threshold", NormalisableRange<float>(-70, 0, 0.1f), -30.f, AudioParameterFloatAttributes().withStringFromValueFunction(db)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("sens", 1), "Sensitivity", NormalisableRange<float>(2, 24, 0.1f), 9.f, AudioParameterFloatAttributes().withStringFromValueFunction(db)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("retrig", 1), "Retrigger", NormalisableRange<float>(5, 250, 0.5f, 0.5f), 22.f, AudioParameterFloatAttributes().withStringFromValueFunction(ms)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("hp", 1), "High-pass", logRange(20, 5000), 600.f, AudioParameterFloatAttributes().withStringFromValueFunction(hz)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("lp", 1), "Low-pass", logRange(300, 20000), 12000.f, AudioParameterFloatAttributes().withStringFromValueFunction(hz)));
    l.add(std::make_unique<AudioParameterInt>(ParameterID("note", 1), "Note", 0, 127, 36, AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return MidiMessage::getMidiNoteName(v, true, true, 3) + " (" + String(v) + ")"; })));
    l.add(std::make_unique<AudioParameterInt>(ParameterID("chan", 1), "Channel", 1, 16, 10));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("len", 1), "Note length", NormalisableRange<float>(5, 500, 1, 0.5f), 50.f, AudioParameterFloatAttributes().withStringFromValueFunction(ms)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("velceil", 1), "Velocity ceiling", NormalisableRange<float>(-40, 0, 0.1f), -6.f, AudioParameterFloatAttributes().withStringFromValueFunction(db)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("velrange", 1), "Velocity range", NormalisableRange<float>(6, 60, 0.1f), 24.f, AudioParameterFloatAttributes().withStringFromValueFunction(db)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("dyn", 1), "Dynamics", NormalisableRange<float>(0, 100, 1), 100.f, AudioParameterFloatAttributes().withStringFromValueFunction(pct)));
    l.add(std::make_unique<AudioParameterBool>(ParameterID("side", 1), "Sidechain bleed reject", false));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("sideratio", 1), "Bleed level", NormalisableRange<float>(-40, 0, 0.1f), -12.f, AudioParameterFloatAttributes().withStringFromValueFunction(db)));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID("monitor", 1), "Monitor", StringArray { "Input", "Detection band", "Trigger clicks", "Mute" }, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID("nudge", 1), "Recorder nudge", NormalisableRange<float>(-50, 50, 0.1f), 0.f, AudioParameterFloatAttributes().withStringFromValueFunction(ms)));
    return l;
}

// background file analysis
class FnThread : public juce::Thread {
public:
    FnThread(std::function<void(juce::Thread&)> f) : juce::Thread("Omega Trigger analysis"), fn(std::move(f)) {}
    void run() override { fn(*this); }
private:
    std::function<void(juce::Thread&)> fn;
};

TriggerProcessor::TriggerProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)
          .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "TRIGGER", layout()) {
    startTimerHz(30);
}

TriggerProcessor::~TriggerProcessor() {
    stopTimer();
    if (anThread) anThread->stopThread(10000);
}

bool TriggerProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != out) return false;
    if (l.inputBuses.size() > 1) {
        const auto sc = l.getChannelSet(true, 1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void TriggerProcessor::readParams(Settings& s) const {
    s.thresholdDb = apvts.getRawParameterValue("thresh")->load();
    s.sensitivityDb = apvts.getRawParameterValue("sens")->load();
    s.retriggerMs = apvts.getRawParameterValue("retrig")->load();
    s.hpHz = apvts.getRawParameterValue("hp")->load();
    s.lpHz = apvts.getRawParameterValue("lp")->load();
    s.sideOn = apvts.getRawParameterValue("side")->load() > 0.5f;
    s.sideRatioDb = apvts.getRawParameterValue("sideratio")->load();
}

Settings TriggerProcessor::currentSettings() const { Settings s; readParams(s); return s; }

void TriggerProcessor::prepareToPlay(double sampleRate, int maxBlock) {
    sr = sampleRate;
    Settings s; readParams(s);
    detector.update(s); detector.prepare(sr);
    Settings ls = s; ls.thresholdDb = -70; ls.sideOn = false;
    learner.update(ls); learner.prepare(sr);
    lastSettings = s; settingsValid = true;
    monitorBand.set(s.hpHz, s.lpHz, sr); monitorBand.reset();
    latency = detector.latency();
    setLatencySamples(latency);
    delayBuf.setSize(3, std::max(1, latency)); delayBuf.clear(); delayPos = 0;   // read-then-write: a delay of exactly the latency
    mono.assign((size_t) std::max(1, maxBlock), 0.f); sideMono = mono; bandMon = mono;
    nPending = 0; noteHeld = false; clickEnv = 0;
    colLen = std::max(1, (int) std::lround(sr / kColumnsPerSecond)); colCount = 0;
}

static bool differs(const Settings& a, const Settings& b) {
    return a.thresholdDb != b.thresholdDb || a.sensitivityDb != b.sensitivityDb || a.retriggerMs != b.retriggerMs
        || a.hpHz != b.hpHz || a.lpHz != b.lpHz || a.sideOn != b.sideOn || a.sideRatioDb != b.sideRatioDb;
}

void TriggerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    midi.clear();
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if ((int) mono.size() < n) { mono.resize((size_t) n); sideMono.resize((size_t) n); bandMon.resize((size_t) n); }   // host broke its promise

    auto mainIn = getBusBuffer(buffer, true, 0);
    const int nin = std::min(mainIn.getNumChannels(), buffer.getNumChannels());
    for (int i = 0; i < n; ++i) {
        float v = 0; for (int c = 0; c < nin; ++c) v += mainIn.getSample(c, i);
        mono[(size_t) i] = nin ? v / (float) nin : 0.f;
    }
    bool haveSide = false;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled()
        && getChannelIndexInProcessBlockBuffer(true, 1, 0) + getBus(true, 1)->getNumberOfChannels() <= buffer.getNumChannels()) {
        auto sc = getBusBuffer(buffer, true, 1);
        const int ns = sc.getNumChannels();
        if (ns > 0) {
            haveSide = true;
            for (int i = 0; i < n; ++i) { float v = 0; for (int c = 0; c < ns; ++c) v += sc.getSample(c, i); sideMono[(size_t) i] = v / (float) ns; }
        }
    }

    Settings s; readParams(s);
    if (!haveSide) s.sideOn = false;
    if (!settingsValid || differs(s, lastSettings)) {
        detector.update(s);
        Settings ls = s; ls.thresholdDb = -70; ls.sideOn = false; learner.update(ls);
        if (s.hpHz != lastSettings.hpHz || s.lpHz != lastSettings.lpHz) monitorBand.set(s.hpHz, s.lpHz, sr);
        lastSettings = s; settingsValid = true;
    }
    const int note = (int) apvts.getRawParameterValue("note")->load();
    const int chan = (int) apvts.getRawParameterValue("chan")->load();
    const int64_t lenS = std::max<int64_t>(1, (int64_t) (apvts.getRawParameterValue("len")->load() * sr / 1000));
    const float velCeil = apvts.getRawParameterValue("velceil")->load(), velRange = apvts.getRawParameterValue("velrange")->load();
    const float dyn = apvts.getRawParameterValue("dyn")->load() / 100.f;
    const double nudge = apvts.getRawParameterValue("nudge")->load() / 1000.0;
    const int monitor = (int) apvts.getRawParameterValue("monitor")->load();

    // where the song is
    bool playing = false, hasPpq = false; double ppq0 = 0, sec0 = 0, bpm = 120;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        playing = pos->getIsPlaying();
        if (auto b = pos->getBpm()) { bpm = *b; lastBpm.store(bpm); }
        if (auto p = pos->getPpqPosition()) { ppq0 = *p; hasPpq = true; }
        if (auto t = pos->getTimeInSeconds()) sec0 = *t;
        else if (auto ts = pos->getTimeInSamples()) sec0 = (double) *ts / sr;
        else if (hasPpq) sec0 = ppq0 * 60.0 / bpm;
    }
    const bool recNow = playing && recording.load();
    const int64_t blockStart = detector.position();

    auto queue = [&](const Pending& p) {
        if (nPending < (int) pending.size()) pending[(size_t) nPending++] = p;
    };
    auto onHit = [&](const Hit& h) {
        HitInfo info; info.onset = h.onset; info.peakDb = 20 * std::log10(std::max(h.peak, 1e-9f)); info.rejected = h.rejected;
        info.velocity = Detector::velocity(h.level, velCeil, velRange, dyn);
        { int s1, n1, s2, n2; hitFifo.prepareToWrite(1, s1, n1, s2, n2); if (n1) hitData[(size_t) s1] = info; hitFifo.finishedWrite(n1 + n2); }
        if (h.rejected) return;
        const int64_t on = h.onset + latency;
        // a note still sounding is cut just before the new one
        for (int i = 0; i < nPending; ++i)
            if (!pending[(size_t) i].on && pending[(size_t) i].note == note && pending[(size_t) i].channel == chan && pending[(size_t) i].at > on)
                pending[(size_t) i].at = on;
        queue({ on, note, chan, info.velocity, true });
        queue({ on + lenS, note, chan, 0, false });
        if (recNow) {
            const double off = (double) (h.onset - blockStart) / sr + nudge;   // the hit's place relative to this block
            RecItem r { ppq0 + off * bpm / 60.0, sec0 + off, info.velocity, hasPpq, bpm };
            int s1, n1, s2, n2; recFifo.prepareToWrite(1, s1, n1, s2, n2); if (n1) recData[(size_t) s1] = r; recFifo.finishedWrite(n1 + n2);
        }
    };
    auto onCand = [&](const Hit& h) {
        int s1, n1, s2, n2; candFifo.prepareToWrite(1, s1, n1, s2, n2);
        if (n1) candData[(size_t) s1] = 20 * std::log10(std::max(h.peak, 1e-9f));
        candFifo.finishedWrite(n1 + n2);
    };

    // run the detector in pieces that end on display columns
    for (int i = 0; i < n;) {
        const int m = std::min(n - i, colLen - colCount);
        detector.process(mono.data() + i, haveSide ? sideMono.data() + i : nullptr, m, onHit);
        learner.process(mono.data() + i, nullptr, m, onCand);
        colCount += m; i += m;
        if (colCount >= colLen) {
            colCount = 0;
            int s1, n1, s2, n2; colFifo.prepareToWrite(1, s1, n1, s2, n2);
            if (n1) colData[(size_t) s1] = detector.takeMeter();
            colFifo.finishedWrite(n1 + n2);
            colEnd.store(detector.position());
            learner.takeMeter();
        }
    }

    // send what's due in this block: note-offs before note-ons at the same sample
    std::sort(pending.begin(), pending.begin() + nPending, [](const Pending& a, const Pending& b) { return a.at != b.at ? a.at < b.at : (!a.on && b.on); });
    int keep = 0;
    std::array<int, 64> clickAt {}; std::array<float, 64> clickVel {}; int nClicks = 0;
    for (int i = 0; i < nPending; ++i) {
        const auto& p = pending[(size_t) i];
        if (p.at < blockStart + n) {
            const int off = (int) juce::jlimit<int64_t>(0, n - 1, p.at - blockStart);
            if (p.on) {
                midi.addEvent(juce::MidiMessage::noteOn(p.channel, p.note, (juce::uint8) p.vel), off);
                if (nClicks < (int) clickAt.size()) { clickAt[(size_t) nClicks] = off; clickVel[(size_t) nClicks++] = p.vel / 127.f; }
            } else midi.addEvent(juce::MidiMessage::noteOff(p.channel, p.note), off);
        } else pending[(size_t) keep++] = p;
    }
    nPending = keep;

    // audio out: the input delayed by the latency, the detection band, clicks on the hits, or nothing
    for (int i = 0; i < n; ++i) { float b = monitorBand.run(mono[(size_t) i]); bandMon[(size_t) i] = b; }
    const int nout = buffer.getNumChannels(), dlen = delayBuf.getNumSamples();
    int ci = 0;
    for (int i = 0; i < n; ++i) {
        float clk = 0;
        while (ci < nClicks && clickAt[(size_t) ci] == i) { clickEnv = 0.5f * (0.25f + 0.75f * clickVel[(size_t) ci]); clickPhase = 0; ++ci; }
        if (clickEnv > 1e-4f) { clk = clickEnv * std::sin(clickPhase); clickPhase += (float) (2 * 3.14159265 * 1800 / sr); clickEnv *= (float) std::exp(-1.0 / (0.004 * sr)); }
        const int rd = delayPos;
        for (int c = 0; c < std::min(nout, 2); ++c) {
            const float x = c < nin ? mainIn.getSample(c, i) : mono[(size_t) i];
            const float y = delayBuf.getSample(c, rd);
            delayBuf.setSample(c, rd, x);
            const float yb = delayBuf.getSample(2, rd);
            buffer.setSample(c, i, monitor == 0 ? y : monitor == 1 ? yb : monitor == 2 ? clk : 0.f);
        }
        delayBuf.setSample(2, rd, bandMon[(size_t) i]);
        delayPos = (delayPos + 1) % dlen;
    }
    for (int c = 2; c < nout; ++c) buffer.clear(c, 0, n);
}

void TriggerProcessor::timerCallback() {
    // learn candidates
    {
        int s1, n1, s2, n2; candFifo.prepareToRead(candFifo.getNumReady(), s1, n1, s2, n2);
        const std::lock_guard<std::mutex> l(candMutex);
        for (int i = 0; i < n1; ++i) candidates.push_back(candData[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) candidates.push_back(candData[(size_t) (s2 + i)]);
        candFifo.finishedRead(n1 + n2);
        if (candidates.size() > 3000) candidates.erase(candidates.begin(), candidates.begin() + (long) (candidates.size() - 3000));
    }
    // recorded hits: kept in song order; a hit within 5 ms of one already there replaces it (playing a section
    // again re-records it instead of doubling it)
    {
        int s1, n1, s2, n2; recFifo.prepareToRead(recFifo.getNumReady(), s1, n1, s2, n2);
        const std::lock_guard<std::mutex> l(recMutex);
        auto add = [&](const RecItem& r) {
            SongHit h { r.hasPpq ? r.ppq : r.seconds * 2.0, r.seconds, r.vel };
            recBpm.store(r.bpm);
            auto it = std::lower_bound(rec.begin(), rec.end(), h.seconds, [](const SongHit& a, double t) { return a.seconds < t; });
            if (it != rec.end() && it->seconds - h.seconds < 0.005) { *it = h; return; }
            if (it != rec.begin() && h.seconds - std::prev(it)->seconds < 0.005) { *std::prev(it) = h; return; }
            rec.insert(it, h);
        };
        for (int i = 0; i < n1; ++i) add(recData[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) add(recData[(size_t) (s2 + i)]);
        recFifo.finishedRead(n1 + n2);
    }
}

void TriggerProcessor::pullDisplay(std::vector<float>& columns, std::vector<HitInfo>& hits, int64_t& lastColumnEnd, int& columnLen) {
    {
        int s1, n1, s2, n2; colFifo.prepareToRead(colFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) columns.push_back(colData[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) columns.push_back(colData[(size_t) (s2 + i)]);
        colFifo.finishedRead(n1 + n2);
    }
    {
        int s1, n1, s2, n2; hitFifo.prepareToRead(hitFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) hits.push_back(hitData[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) hits.push_back(hitData[(size_t) (s2 + i)]);
        hitFifo.finishedRead(n1 + n2);
    }
    lastColumnEnd = colEnd.load(); columnLen = colLen;
}

bool TriggerProcessor::learnThreshold(float& db) {
    std::vector<float> c;
    { const std::lock_guard<std::mutex> l(candMutex); c = candidates; }
    return autoThreshold(c, db);
}

void TriggerProcessor::applyPreset(Preset p) {
    const Settings s = presetSettings(p);
    auto set = [&](const char* id, float v) { if (auto* q = apvts.getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(v)); };
    set("thresh", s.thresholdDb); set("sens", s.sensitivityDb); set("retrig", s.retriggerMs);
    set("hp", s.hpHz); set("lp", s.lpHz); set("note", (float) presetNote(p));
}

void TriggerProcessor::analyzeFile(const juce::File& file, bool learnFirst) {
    if (anRunning.load()) return;
    if (anThread) anThread->stopThread(1000);
    anRunning = true;
    { const std::lock_guard<std::mutex> l(anMutex); anStatus = "Reading " + file.getFileName() + "..."; anHits.clear(); }
    const Settings base = currentSettings();
    anThread = std::make_unique<FnThread>([this, file, learnFirst, base](juce::Thread& th) {
        auto status = [this](const juce::String& s) { const std::lock_guard<std::mutex> l(anMutex); anStatus = s; };
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(file));
        if (!rd) { status("Can't read " + file.getFileName()); anRunning = false; return; }
        const int64_t len = rd->lengthInSamples; const int ch = (int) rd->numChannels;
        const double fsr = rd->sampleRate;
        if (len <= 0 || len > (int64_t) 1 << 31) { status("File is empty or too long"); anRunning = false; return; }
        std::vector<float> x((size_t) len, 0.f);
        juce::AudioBuffer<float> chunk(ch, 65536);
        for (int64_t pos = 0; pos < len && !th.threadShouldExit(); pos += 65536) {
            const int m = (int) std::min<int64_t>(65536, len - pos);
            rd->read(&chunk, 0, m, pos, true, true);
            for (int c = 0; c < ch; ++c) { const float* p = chunk.getReadPointer(c); for (int i = 0; i < m; ++i) x[(size_t) (pos + i)] += p[i] / (float) ch; }
        }
        if (th.threadShouldExit()) { anRunning = false; return; }
        Settings s = base; s.sideOn = false;
        juce::String note;
        if (learnFirst) {
            status("Learning the threshold...");
            float t;
            if (autoThreshold(candidatePeaksDb(x.data(), nullptr, len, fsr, s), t)) {
                s.thresholdDb = t;
                note = "threshold learned " + juce::String(t, 1) + " dB. ";
                juce::MessageManager::callAsync([this, t] { if (auto* q = apvts.getParameter("thresh")) q->setValueNotifyingHost(q->convertTo0to1(t)); });
            } else note = "threshold kept at " + juce::String(s.thresholdDb, 1) + " dB (no clear gap to bleed). ";
        }
        status("Finding hits...");
        const auto hits = analyze(x.data(), nullptr, len, fsr, s);
        const float velCeil = apvts.getRawParameterValue("velceil")->load(), velRange = apvts.getRawParameterValue("velrange")->load();
        const float dyn = apvts.getRawParameterValue("dyn")->load() / 100.f;
        std::vector<SongHit> out;
        for (const auto& h : hits) out.push_back({ 0.0, (double) h.onset / fsr, Detector::velocity(h.level, velCeil, velRange, dyn) });
        {
            const std::lock_guard<std::mutex> l(anMutex);
            anHits = out;
            anStatus = juce::String((int) out.size()) + " hits in " + file.getFileName() + ", " + note + "MIDI starts at the file's start.";
        }
        anRunning = false;
    });
    anThread->startThread();
}

juce::MidiFile TriggerProcessor::makeMidi(const std::vector<SongHit>& hits, bool usePpq, double bpm) const {
    const int note = (int) apvts.getRawParameterValue("note")->load();
    const int chan = (int) apvts.getRawParameterValue("chan")->load();
    const double lenMs = apvts.getRawParameterValue("len")->load();
    const double tpq = 960;
    if (bpm <= 0) bpm = 120;
    std::vector<double> ticks;
    for (const auto& h : hits) ticks.push_back(std::max(0.0, (usePpq ? h.ppq : h.seconds * bpm / 60.0) * tpq));
    juce::MidiMessageSequence seq;
    auto tn = juce::MidiMessage::textMetaEvent(3, "Omega Drum Trigger"); tn.setTimeStamp(0); seq.addEvent(tn);
    auto tempo = juce::MidiMessage::tempoMetaEvent((int) std::lround(60e6 / bpm)); tempo.setTimeStamp(0); seq.addEvent(tempo);
    auto sig = juce::MidiMessage::timeSignatureMetaEvent(4, 4); sig.setTimeStamp(0); seq.addEvent(sig);
    const double lenT = lenMs / 1000.0 * bpm / 60.0 * tpq;
    for (size_t i = 0; i < hits.size(); ++i) {
        const double on = std::round(ticks[i]);
        double off = on + std::max(1.0, std::round(lenT));
        if (i + 1 < hits.size()) off = std::min(off, std::max(on + 1, std::round(ticks[i + 1]) - 1));
        auto a = juce::MidiMessage::noteOn(chan, note, (juce::uint8) juce::jlimit(1, 127, hits[i].velocity)); a.setTimeStamp(on);
        auto b = juce::MidiMessage::noteOff(chan, note); b.setTimeStamp(off);
        seq.addEvent(a); seq.addEvent(b);
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote((int) tpq); mf.addTrack(seq);
    return mf;
}

juce::File TriggerProcessor::writeTempMidi(const std::vector<SongHit>& hits, bool usePpq, double bpm, const juce::String& name) const {
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Omega Drum Trigger").getChildFile(name + ".mid");
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FileOutputStream os(f);
    if (os.openedOk()) { makeMidi(hits, usePpq, bpm).writeTo(os); os.flush(); }
    return f;
}

void TriggerProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = apvts.copyState();
    juce::String r;
    { const std::lock_guard<std::mutex> l(recMutex); for (auto& h : rec) r << juce::String(h.ppq, 6) << "," << juce::String(h.seconds, 6) << "," << h.velocity << ";"; }
    st.setProperty("recorded", r, nullptr);
    st.setProperty("recbpm", recBpm.load(), nullptr);
    st.setProperty("recording", recording.load(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
}

void TriggerProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        auto st = juce::ValueTree::fromXml(*xml);
        if (!st.isValid()) return;
        const juce::String r = st.getProperty("recorded").toString();
        recBpm.store((double) st.getProperty("recbpm", 120.0));
        recording.store((bool) st.getProperty("recording", true));
        st.removeProperty("recorded", nullptr); st.removeProperty("recbpm", nullptr); st.removeProperty("recording", nullptr);
        apvts.replaceState(st);
        std::vector<SongHit> v;
        for (auto& item : juce::StringArray::fromTokens(r, ";", ""))
            if (item.isNotEmpty()) {
                auto f = juce::StringArray::fromTokens(item, ",", "");
                if (f.size() == 3) v.push_back({ f[0].getDoubleValue(), f[1].getDoubleValue(), f[2].getIntValue() });
            }
        const std::lock_guard<std::mutex> l(recMutex); rec = v;
    }
}

juce::AudioProcessorEditor* TriggerProcessor::createEditor() { return new TriggerEditor(*this); }

} // namespace trig

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new trig::TriggerProcessor(); }
