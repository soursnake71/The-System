#include "XR10Plugin.h"
#include "XR10Editor.h"

namespace xr10 {

Processor::Processor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Main", juce::AudioChannelSet::stereo(), true)
                         .withOutput("FX Send", juce::AudioChannelSet::stereo(), false)) {
    m = std::make_unique<xr::Machine>();
    m->init(44100);
    formats.registerBasicFormats();
}

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (l.outputBuses.size() > 1) {
        const auto& aux = l.outputBuses[1];
        if (!aux.isDisabled() && aux != juce::AudioChannelSet::stereo()) return false;
    }
    return l.inputBuses.isEmpty() || l.getMainInputChannelSet().isDisabled();
}

void Processor::prepareToPlay(double sampleRate, int) {
    rate = sampleRate;
    with([&](xr::Machine& mm) { mm.setRate(sampleRate); });
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const juce::SpinLock::ScopedLockType l(lock);
    const int n = buffer.getNumSamples();
    // host transport
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            const bool playing = pos->getIsPlaying();
            const double ppq = pos->getPpqPosition().orFallback(0.0);
            const double bpm = pos->getBpm().orFallback(0.0);
            m->host(playing ? 1 : 0, ppq, bpm);
        }
    }
    auto main = getBusBuffer(buffer, false, 0);
    float* L = main.getWritePointer(0);
    float* R = main.getNumChannels() > 1 ? main.getWritePointer(1) : nullptr;
    float* aL = nullptr; float* aR = nullptr;
    if (getBusCount(false) > 1 && getBus(false, 1)->isEnabled()) {
        auto aux = getBusBuffer(buffer, false, 1);
        if (aux.getNumChannels() >= 2) { aL = aux.getWritePointer(0); aR = aux.getWritePointer(1); }
    }
    std::vector<float> tmpR;
    if (!R) { tmpR.resize((size_t) n); R = tmpR.data(); }
    // MIDI with sample accuracy: render up to each event
    int at = 0;
    for (const auto meta : midi) {
        const int pos = juce::jlimit(0, n, meta.samplePosition);
        if (pos > at) { m->process(L + at, R + at, aL ? aL + at : nullptr, aR ? aR + at : nullptr, pos - at); at = pos; }
        const auto msg = meta.getMessage();
        const auto* d = msg.getRawData();
        m->midi(d[0], msg.getRawDataSize() > 1 ? d[1] : 0, msg.getRawDataSize() > 2 ? d[2] : 0);
    }
    if (n > at) m->process(L + at, R + at, aL ? aL + at : nullptr, aR ? aR + at : nullptr, n - at);
    midi.clear();
}

int Processor::getCurrentProgram() { int p = 0; with([&](xr::Machine& mm) { p = mm.curPat; }); return p; }
void Processor::setCurrentProgram(int i) { with([&](xr::Machine& mm) { mm.selectPattern(i); }); }
const juce::String Processor::getProgramName(int i) {
    juce::String s;
    with([&](xr::Machine& mm) { s = juce::String(i + 1).paddedLeft('0', 2) + " " + juce::String(mm.pat[juce::jlimit(0, xr::NPAT - 1, i)].name); });
    return s;
}

juce::MemoryBlock Processor::saveMemory() {
    juce::MemoryBlock b;
    with([&](xr::Machine& mm) {
        xr::Machine::Writer w {nullptr, 0, 0};
        mm.save(w);
        b.setSize(w.n);
        xr::Machine::Writer w2 {(u8*) b.getData(), 0, w.n};
        mm.save(w2);
    });
    return b;
}
bool Processor::loadMemory(const juce::MemoryBlock& b) {
    bool ok = false;
    with([&](xr::Machine& mm) { ok = mm.load((const u8*) b.getData(), (u32) b.getSize()); });
    return ok;
}
void Processor::getStateInformation(juce::MemoryBlock& dest) {
    // the engine's memory (the same format as the app's "Save all to a file"), then the editor's size and tab
    auto b = saveMemory();
    juce::MemoryOutputStream o(dest, false);
    o.writeInt(0x58523130);   // "XR10"
    o.writeInt(1);
    o.writeFloat(editorScale);
    o.writeInt(editorTab);
    o.writeInt((int) b.getSize());
    o.write(b.getData(), b.getSize());
}
void Processor::setStateInformation(const void* data, int size) {
    juce::MemoryInputStream in(data, (size_t) size, false);
    if (in.readInt() != 0x58523130) return;
    in.readInt();
    editorScale = juce::jlimit(0.4f, 2.0f, in.readFloat());
    editorTab = in.readInt();
    const int len = in.readInt();
    if (len <= 0 || len > size) return;
    juce::MemoryBlock b;
    in.readIntoMemoryBlock(b, len);
    loadMemory(b);
}

void Processor::snapshot(Snapshot& s, int gridBar) {
    with([&](xr::Machine& mm) {
        s.st = mm.status();
        for (int k = 0; k < xr::NKEYS; k++) s.keys[k] = mm.get(k);
        for (int p = 0; p < xr::NVP; p++) s.params[p] = mm.vp[s.st.sound][p];
        for (int i = 0; i < xr::NSOUNDS; i++) s.user[i] = mm.user[i].len > 0;
        mm.grid(gridBar, s.grid);
        s.steps = mm.gridSteps();
        const auto& sg = mm.songs[s.st.song];
        s.songLen = sg.len; s.songTempo = sg.tempo;
        for (int i = 0; i < sg.len; i++) s.song[i] = sg.steps[i];
        std::memcpy(s.patName, mm.pat[mm.curPat].name, 10);
    });
}
void Processor::patternNames(std::vector<juce::String>& out) {
    out.clear();
    with([&](xr::Machine& mm) { for (int i = 0; i < xr::NPAT; i++) out.push_back(mm.pat[i].name); });
}
void Processor::sampleWave(int sound, std::vector<short>& out) {
    with([&](xr::Machine& mm) { int len = 0; const short* d = mm.sampleOf(sound, len); out.assign(d, d + len); });
}
juce::String Processor::loadSample(int sound, const juce::File& f) {
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
    if (!r) return "Can't read " + f.getFileName();
    const int n = (int) juce::jmin<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 10));
    juce::AudioBuffer<float> b((int) r->numChannels, n);
    r->read(&b, 0, n, 0, true, true);
    std::vector<float> mono((size_t) n, 0.f);
    for (int c = 0; c < b.getNumChannels(); c++) {
        const float* d = b.getReadPointer(c);
        for (int i = 0; i < n; i++) mono[(size_t) i] += d[i] / (float) b.getNumChannels();
    }
    int s0 = 0;
    while (s0 < n - 1 && std::abs(mono[(size_t) s0]) < 0.003f) s0++;
    bool ok = false;
    with([&](xr::Machine& mm) { ok = mm.loadSample(sound, mono.data() + s0, n - s0, r->sampleRate); });
    return ok ? juce::String() : juce::String("Sample memory is full");
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

}  // namespace xr10

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new xr10::Processor(); }
