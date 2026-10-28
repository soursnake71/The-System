// SPQuencer1200 as a VST3 / AU instrument: the editor page (spquencer/index.html + app.js) runs in a JUCE web view,
// the Engine plays its segments and songs with the DAW, sends every note to the MIDI output (for an E-mu or
// Rossum SP-1200 on a MIDI track) and plays preview sounds on the audio output. The project is saved with the DAW set.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "BinaryData.h"
#include "Engine.h"

namespace {

struct SampleSlot { juce::String b64; double rate = 0; const spq::Sample* data = nullptr; };

class Processor : public juce::AudioProcessor {
public:
    Processor() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        project = std::make_unique<spq::Project>();
        engine.setProject(project.get());
    }

    void prepareToPlay(double sampleRate, int maxBlock) override {
        const juce::SpinLock::ScopedLockType l(lock);
        engine.prepare(sampleRate);
        midiIn.reserve(1024); midiOut.reserve(4096);
        scratchBuf.resize((size_t) juce::jmax(maxBlock, 4096));   // mono hosts: sized here, not on the audio thread
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    }

    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals nd;
        const int n = buf.getNumSamples();
        spq::Engine::Host h;
        if (auto* ph = getPlayHead())
            if (auto p = ph->getPosition()) {
                h.playing = p->getIsPlaying();
                if (auto q = p->getPpqPosition()) { h.hasPpq = true; h.ppq = *q; }
                if (auto b = p->getBpm()) h.bpm = *b;
            }
        midiIn.clear();
        for (const auto m : midi) {
            const auto msg = m.getMessage();
            if (msg.isNoteOnOrOff()) midiIn.push_back({m.samplePosition, {msg.getRawData()[0], msg.getRawData()[1], msg.getRawData()[2]}});
        }
        midi.clear();
        midiOut.clear();
        float* L = buf.getWritePointer(0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : L;
        {
            const juce::SpinLock::ScopedLockType l(lock);
            engine.process(L, R == L ? scratch(n) : R, n, h, midiIn, midiOut);
            const auto ps = engine.position();
            posPlaying = ps.playing; posMode = ps.mode; posSeg = ps.seg; posTick = ps.tick; posIdx = ps.idx;
        }
        for (auto& m : midiOut) midi.addEvent(m.b, 3, juce::jlimit(0, n - 1, m.offset));
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SPQuencer1200"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    // ---- project: the page sends everything but sample data as JSON, and each sample on its own ----
    // meta and slots are shared by the editor (message thread) and the host saving or loading state (any thread)
    void setMeta(const juce::String& json) { const juce::ScopedLock sl(stateLock); meta = json; rebuild(); }
    void setSample(int i, double rate, const juce::String& b64) { const juce::ScopedLock sl(stateLock); decode(i, rate, b64); rebuild(); }
    void decode(int i, double rate, const juce::String& b64) {
        if (i < 0 || i >= spq::NSND) return;
        auto& s = slots[(size_t) i];
        s.b64 = b64; s.rate = rate; s.data = nullptr;
        if (rate > 0 && b64.isNotEmpty()) {
            juce::MemoryOutputStream raw;
            if (juce::Base64::convertFromBase64(raw, b64)) {
                auto smp = std::make_unique<spq::Sample>();
                smp->rate = rate;
                const auto* p = static_cast<const int16_t*>(raw.getData());
                const size_t count = raw.getDataSize() / 2;
                smp->data.resize(count);
                for (size_t k = 0; k < count; ++k) smp->data[k] = p[k] / 32768.0f;
                s.data = smp.get();
                samples.push_back(std::move(smp));   // kept until the plugin closes, so a playing voice never loses it
            }
        }
    }
    // the whole project as the page saves it (samples included)
    juce::String fullJson() const {
        const juce::ScopedLock sl(stateLock);
        if (meta.isEmpty()) return {};
        auto v = juce::JSON::parse(meta);
        if (auto* sounds = v["sounds"].getArray())
            for (int i = 0; i < sounds->size() && i < spq::NSND; ++i)
                if (auto* o = (*sounds)[i].getDynamicObject()) {
                    const auto& s = slots[(size_t) i];
                    if (s.rate > 0 && s.b64.isNotEmpty()) {
                        auto* smp = new juce::DynamicObject(); smp->setProperty("rate", s.rate); smp->setProperty("b64", s.b64);
                        o->setProperty("sample", juce::var(smp));
                    } else o->setProperty("sample", juce::var());
                }
        return juce::JSON::toString(v, true);
    }

    void transport(const juce::var& d) {
        const auto mode = d["play"].toString();
        const juce::SpinLock::ScopedLockType l(lock);
        if (mode == "seg" || mode == "song") engine.play(mode == "song" ? 2 : 1, (int) d["seg"], (int) d["song"], (int) d["from"]);
        else engine.stop();
    }
    void audition(int s, int lv, int u) { const juce::SpinLock::ScopedLockType l(lock); engine.audition(juce::jlimit(0, spq::NSND - 1, s), juce::jlimit(0, 31, lv), juce::jlimit(-8, 7, u)); }
    // for the playhead display only, so the fields need not change together
    spq::Engine::Pos position() const { return {posPlaying.load(), posMode.load(), posSeg.load(), posTick.load(), posIdx.load()}; }

    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::MemoryOutputStream out(dest, false);
        out.writeString("SPQ1");
        juce::GZIPCompressorOutputStream z(out);
        z.writeString(fullJson());
    }
    void setStateInformation(const void* data, int size) override {
        juce::MemoryInputStream in(data, (size_t) size, false);
        if (in.readString() != "SPQ1") return;
        juce::GZIPDecompressorInputStream z(in);
        loadFull(z.readString());
    }
    void loadFull(const juce::String& json) {
        auto v = juce::JSON::parse(json);
        if (!v.isObject()) return;
        const juce::ScopedLock sl(stateLock);
        if (auto* sounds = v["sounds"].getArray())
            for (int i = 0; i < sounds->size() && i < spq::NSND; ++i) {
                const auto smp = (*sounds)[i]["sample"];
                const double rate = smp.isObject() ? (double) smp["rate"] : 0.0;
                const auto b64 = smp.isObject() ? smp["b64"].toString() : juce::String();
                decode(i, rate, b64);
                if (auto* o = (*sounds)[i].getDynamicObject()) o->setProperty("sample", juce::var());
            }
        meta = juce::JSON::toString(v, true);
        rebuild();
    }

private:
    float* scratch(int n) { if ((int) scratchBuf.size() < n) scratchBuf.resize((size_t) n); return scratchBuf.data(); }

    static int num(const juce::var& v, int def) { return v.isVoid() || v.isUndefined() ? def : (int) v; }

    void rebuild() {
        if (meta.isEmpty()) return;
        const auto v = juce::JSON::parse(meta);
        if (!v.isObject()) return;
        auto p = std::make_unique<spq::Project>();
        if (auto* segs = v["segments"].getArray())
            for (int i = 0; i < segs->size() && i < spq::NSEG; ++i) {
                const auto& s = (*segs)[i];
                if (!s.isObject()) continue;
                auto& g = p->seg[i];
                g.used = true;
                g.len = juce::jlimit(1, 99, num(s["len"], 2)); g.num = juce::jlimit(1, 16, num(s["num"], 4));
                g.den = num(s["den"], 4); if (g.den != 4 && g.den != 8 && g.den != 16) g.den = 4;
                g.tempo = juce::jlimit(30.0, 250.0, s["tempo"].isVoid() ? 90.0 : (double) s["tempo"]);
                g.swing = juce::jlimit(12, 17, num(s["swing"], 12));
                if (auto* ev = s["events"].getArray())
                    for (const auto& e : *ev)
                        g.events.push_back({num(e["t"], 0), juce::jlimit(0, spq::NSND - 1, num(e["s"], 0)), juce::jlimit(0, 31, num(e["l"], 31)), juce::jlimit(-8, 7, num(e["u"], 0))});
                g.index();
            }
        if (auto* songs = v["songs"].getArray())
            for (int i = 0; i < songs->size() && i < spq::NSONG; ++i) {
                const auto& s = (*songs)[i];
                if (!s.isObject()) continue;
                auto& song = p->song[i];
                song.used = true; song.tempo = s["tempo"].isVoid() ? 90.0 : (double) s["tempo"];
                if (auto* st = s["steps"].getArray())
                    for (const auto& x : *st) song.steps.push_back({x["k"].toString().toStdString(), x["v"].isVoid() ? 0.0 : (double) x["v"]});
            }
        if (auto* mixes = v["mixes"].getArray())
            for (int m = 0; m < mixes->size() && m < spq::NMIX; ++m)
                if (auto* lv = (*mixes)[m].getArray())
                    for (int i = 0; i < lv->size() && i < spq::NSND; ++i) p->mix[m][i] = juce::jlimit(0, 31, (int) (*lv)[i]);
        if (auto* sounds = v["sounds"].getArray())
            for (int i = 0; i < sounds->size() && i < spq::NSND; ++i) {
                const auto& s = (*sounds)[i];
                auto& d = p->snd[i];
                d.name = s["name"].toString().toStdString();
                d.decayMode = s["mode"].toString() == "decay";
                d.tune = juce::jlimit(-8, 7, num(s["tune"], 0)); d.decay = juce::jlimit(0, 31, num(s["decay"], 31));
                d.note = s["note"].isVoid() || s["note"].isUndefined() || s["note"].toString().isEmpty() ? -1 : juce::jlimit(0, 127, (int) s["note"]);
                d.start = num(s["start"], 0); d.end = num(s["end"], 0); d.loop = num(s["loop"], -1);
                d.sample = slots[(size_t) i].data;
            }
        const auto midiV = v["midi"], host = v["host"], cur = v["cur"];
        p->ch = juce::jlimit(1, 16, num(midiV["ch"], 1)); p->base = juce::jlimit(0, 96, num(midiV["base"], 36));
        p->hostMode = host["mode"].isVoid() ? "seg" : host["mode"].toString().toStdString();
        p->trigCh = juce::jlimit(1, 16, num(host["trigCh"], 16));
        p->audio = host["audio"].isVoid() ? true : (bool) host["audio"];
        p->curSeg = juce::jlimit(0, spq::NSEG - 1, num(cur["seg"], 1)); p->curSong = juce::jlimit(0, spq::NSONG - 1, num(cur["song"], 1));
        p->curMix = juce::jlimit(0, spq::NMIX - 1, num(cur["mix"], 0));

        std::unique_ptr<spq::Project> old;
        {
            const juce::SpinLock::ScopedLockType l(lock);
            old = std::move(project);
            project = std::move(p);
            engine.setProject(project.get());
        }
        // the old project is freed here, on the message thread
    }

    juce::SpinLock lock;
    juce::CriticalSection stateLock;
    spq::Engine engine;
    std::unique_ptr<spq::Project> project;
    std::array<SampleSlot, spq::NSND> slots;
    std::vector<std::unique_ptr<spq::Sample>> samples;
    std::vector<spq::MidiMsg> midiIn, midiOut;
    std::vector<float> scratchBuf;
    juce::String meta;
    std::atomic<bool> posPlaying { false };
    std::atomic<int> posMode { 0 }, posSeg { 0 }, posTick { 0 }, posIdx { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

// ---- editor: the web page, served from the plugin's own binary data ----
std::optional<juce::WebBrowserComponent::Resource> serve(const juce::String& url) {
    auto path = url.fromFirstOccurrenceOf("/", false, false).upToFirstOccurrenceOf("?", false, false);
    if (path.isEmpty()) path = "index.html";
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (path == BinaryData::originalFilenames[i]) {
            int size = 0;
            const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
            // some web views read the page as Latin-1 whatever the charset says, so send pure ASCII:
            // HTML gets character references, JavaScript gets \u escapes (only used in strings and comments there)
            const bool html = path.endsWith(".html"), js = path.endsWith(".js");
            juce::String text = juce::String::fromUTF8(data, size), ascii;
            if (html || js) {
                juce::MemoryOutputStream o;
                for (auto p = text.getCharPointer(); ! p.isEmpty();) {
                    const auto ch = (juce::uint32) p.getAndAdvance();
                    if (ch < 128) o.writeByte((char) ch);
                    else if (html) o << "&#" << juce::String((int) ch) << ";";
                    else if (ch < 0x10000) o << "\\u" << juce::String::toHexString((int) ch).paddedLeft('0', 4);
                    else o << "\\u{" << juce::String::toHexString((int) ch) << "}";
                }
                ascii = o.toString();
            }
            const auto* src = html || js ? ascii.toRawUTF8() : data;
            const size_t len = html || js ? (size_t) ascii.getNumBytesAsUTF8() : (size_t) size;
            std::vector<std::byte> bytes(len);
            std::memcpy(bytes.data(), src, len);
            const auto mime = path.endsWith(".html") ? "text/html; charset=utf-8" : path.endsWith(".js") ? "text/javascript; charset=utf-8" : path.endsWith(".svg") ? "image/svg+xml" : "application/octet-stream";
            return juce::WebBrowserComponent::Resource { std::move(bytes), mime };
        }
    return std::nullopt;
}

class Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor& p) : AudioProcessorEditor(p), proc(p), web(options()) {
        addAndMakeVisible(web);
        setResizable(true, true);
        setResizeLimits(720, 480, 2400, 1600);
        setSize(1200, 780);
        web.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
        startTimerHz(30);
    }
    void resized() override { web.setBounds(getLocalBounds()); }

private:
    juce::WebBrowserComponent::Options options() {
        using O = juce::WebBrowserComponent::Options;
        return O{}
            .withBackend(O::Backend::webview2)
            .withWinWebView2Options(O::WinWebView2{}.withUserDataFolder(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("SPQuencer1200")))
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withResourceProvider(serve)
            .withEventListener("spqReady", [this](const juce::var&) {
                auto* o = new juce::DynamicObject(); o->setProperty("json", proc.fullJson());
                web.emitEventIfBrowserIsVisible("spqLoad", juce::var(o));
            })
            .withEventListener("spqProject", [this](const juce::var& d) { proc.setMeta(d["json"].toString()); })
            .withEventListener("spqSample", [this](const juce::var& d) { proc.setSample((int) d["i"], (double) d["rate"], d["b64"].toString()); })
            .withEventListener("spqTransport", [this](const juce::var& d) { proc.transport(d); })
            .withEventListener("spqAudition", [this](const juce::var& d) { proc.audition((int) d["s"], (int) d["l"], (int) d["u"]); });
    }
    void timerCallback() override {
        const auto p = proc.position();
        if (p.playing == last.playing && p.seg == last.seg && p.tick == last.tick && p.idx == last.idx && p.mode == last.mode) return;
        last = p;
        auto* o = new juce::DynamicObject();
        o->setProperty("playing", p.playing); o->setProperty("mode", p.mode == 2 ? "song" : "seg");
        o->setProperty("seg", p.seg); o->setProperty("tick", p.tick); o->setProperty("idx", p.idx);
        web.emitEventIfBrowserIsVisible("spqPos", juce::var(o));
    }

    Processor& proc;
    juce::WebBrowserComponent web;
    spq::Engine::Pos last {};
};

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
