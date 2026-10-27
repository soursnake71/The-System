// Headless check of SPQuencer1200.vst3: load it like a DAW, give it a project, run the DAW transport and check that
// the notes come out of its MIDI output on the right samples (swing included), in segment, song and notes modes.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

struct Head : juce::AudioPlayHead {
    double sr = 48000, bpm = 120; juce::int64 pos = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(playing); p.setBpm(bpm); p.setTimeInSamples(pos);
        p.setPpqPosition(pos / sr * bpm / 60.0); return p;
    }
};

// segment 1: 1 bar 4/4, 63 % swing (15 ticks): kick at 0, hat at 12 (plays at 15), snare at 24. Segment 2: kick at 0.
// song 1: repeat 2 x segment 2, then segment 1. Sound 1-3 plays on its own note 42.
static const char* PROJECT = R"({"v":1,"name":"t","model":"Rossum SP-1200","midi":{"ch":10,"base":36},"host":{"mode":"seg","trigCh":16,"audio":true},
"cur":{"seg":1,"song":1,"mix":0},
"segments":[null,{"len":1,"num":4,"den":4,"tempo":90,"swing":15,"events":[{"t":0,"s":0,"l":31,"u":0},{"t":12,"s":2,"l":20,"u":0},{"t":24,"s":1,"l":31,"u":0}]},
{"len":1,"num":4,"den":4,"tempo":90,"swing":12,"events":[{"t":0,"s":0,"l":31,"u":0}]}],
"songs":[null,{"name":"S","tempo":90,"steps":[{"k":"rep","v":2},{"k":"seg","v":2},{"k":"rend"},{"k":"seg","v":1},{"k":"end"}]}],
"mixes":[[24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24]],
"sounds":[{"name":"Kick","mode":"tune","tune":0,"decay":31,"note":null,"sample":null,"start":0,"end":0,"loop":-1},
{"name":"Snare","mode":"tune","tune":0,"decay":31,"note":null,"sample":null,"start":0,"end":0,"loop":-1},
{"name":"Hat cl","mode":"tune","tune":0,"decay":31,"note":42,"sample":null,"start":0,"end":0,"loop":-1}]})";

// the plugin's own state ("SPQ1" + gzipped project), wrapped the way a VST3 host stores a component's state
static juce::MemoryBlock state(const juce::String& json) {
    juce::MemoryOutputStream out; out.writeString("SPQ1");
    { juce::GZIPCompressorOutputStream z(out); z.writeString(json); }
    juce::MemoryBlock comp(out.getData(), out.getDataSize()), mb;
    juce::XmlElement x("VST3PluginState"); x.createNewChildElement("IComponent")->addTextElement(comp.toBase64Encoding());
    juce::AudioProcessor::copyXmlToBinary(x, mb);
    return mb;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::puts("usage: spq_test SPQuencer1200.vst3"); return 2; }
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found; juce::VST3PluginFormat vst3; vst3.findAllTypesForFile(found, argv[1]);
    if (found.isEmpty()) { std::puts("FAIL no plugin"); return 1; }
    juce::String err; const double sr = 48000; const int block = 480;
    auto inst = fm.createPluginInstance(*found[0], sr, block, err);
    if (!inst) { std::printf("FAIL load: %s\n", err.toRawUTF8()); return 1; }
    std::printf("%s | instrument %d | midi in %d out %d | out ch %d\n", found[0]->name.toRawUTF8(), (int) found[0]->isInstrument, (int) inst->acceptsMidi(), (int) inst->producesMidi(), inst->getTotalNumOutputChannels());
    inst->enableAllBuses(); inst->prepareToPlay(sr, block);
    Head head; head.sr = sr; inst->setPlayHead(&head);
    bool ok = true;
    auto run = [&](const juce::String& json, double secs, std::function<void(juce::MidiBuffer&, int)> feed, std::vector<std::pair<int, int>>& notes, double& rms) {
        auto st = state(json); inst->setStateInformation(st.getData(), (int) st.getSize());
        head.pos = 0; notes.clear(); rms = 0; const int n = (int) (secs * sr);
        for (int p = 0; p < n; p += block) {
            juce::AudioBuffer<float> b(2, block); b.clear(); juce::MidiBuffer m; if (feed) feed(m, p);
            inst->processBlock(b, m);
            for (const auto e : m) if (e.getMessage().isNoteOn()) notes.push_back({p + e.samplePosition, e.getMessage().getNoteNumber() + 1000 * e.getMessage().getChannel()});
            for (int i = 0; i < block; ++i) rms += b.getSample(0, i) * b.getSample(0, i);
            head.pos += block;
        }
        rms = std::sqrt(rms / n);
    };
    auto expect = [&](const char* what, const std::vector<std::pair<int, int>>& got, const std::vector<std::pair<int, int>>& want) {
        bool good = got.size() >= want.size();
        for (size_t i = 0; good && i < want.size(); ++i) good = std::abs(got[i].first - want[i].first) <= 1 && got[i].second == want[i].second;
        std::printf("  %-44s %s (", what, good ? "ok" : "WRONG");
        for (size_t i = 0; i < std::min<size_t>(got.size(), 8); ++i) std::printf("%d@%d ", got[i].second % 1000, got[i].first);
        std::puts(")"); ok &= good;
    };
    // 120 BPM: one tick = 60 / (120 * 24) s = 1000 samples at 48 kHz; a 4/4 bar = 96000 samples
    std::vector<std::pair<int, int>> notes; double rms;
    run(PROJECT, 2.5, nullptr, notes, rms);
    expect("segment mode, swing, own note, channel 10", notes, {{0, 10036}, {15000, 10042}, {24000, 10037}, {96000, 10036}, {111000, 10042}});
    std::printf("  preview audio rms %.4f %s\n", rms, rms > 1e-3 ? "ok" : "SILENT"); ok &= rms > 1e-3;

    juce::String song = juce::String(PROJECT).replace("\"mode\":\"seg\"", "\"mode\":\"song\"");
    run(song, 6.5, nullptr, notes, rms);
    expect("song mode: 2 x seg 2, then seg 1", notes, {{0, 10036}, {96000, 10036}, {192000, 10036}, {207000, 10042}, {216000, 10037}});
    const size_t before = notes.size(); (void) before;

    juce::String trig = juce::String(PROJECT).replace("\"mode\":\"seg\"", "\"mode\":\"notes\"");
    run(trig, 1.5, [](juce::MidiBuffer& m, int p) { if (p == 4800) m.addEvent(juce::MidiMessage::noteOn(16, 1, (juce::uint8) 100), 100); if (p == 48000) m.addEvent(juce::MidiMessage::noteOff(16, 1), 0); }, notes, rms);
    expect("notes mode: note 1 on ch 16 starts seg 1", notes, {{4900, 10036}, {19900, 10042}, {28900, 10037}});
    std::printf("  notes mode stops on note off: %s\n", notes.size() == 3 ? "ok" : "WRONG"); ok &= notes.size() == 3;

    juce::MemoryBlock st; inst->getStateInformation(st);
    std::printf("  state saved %d bytes\n", (int) st.getSize());
    // with a second argument: open the editor (needs a display), let the page load the project and send it back
    if (argc > 2) {
        auto st0 = state(PROJECT); inst->setStateInformation(st0.getData(), (int) st0.getSize());
        if (auto* ed = inst->createEditorIfNeeded()) {
            juce::DocumentWindow w("SPQuencer1200", juce::Colours::black, 0);
            w.setContentNonOwned(ed, true); w.setVisible(true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(juce::String(argv[2]).getIntValue());
            juce::MemoryBlock s2; inst->getStateInformation(s2);
            // the state is wrapped by the host; dig out the plugin's own block and count the sounds the page sent back
            int sounds = -1;
            if (auto x = juce::AudioProcessor::getXmlFromBinary(s2.getData(), (int) s2.getSize()))
                if (auto* c = x->getChildByName("IComponent")) {
                    juce::MemoryBlock comp; comp.fromBase64Encoding(c->getAllSubText());
                    juce::MemoryInputStream in(comp, false); in.readString();
                    juce::GZIPDecompressorInputStream z(in);
                    auto v = juce::JSON::parse(z.readString());
                    if (auto* a = v["sounds"].getArray()) sounds = a->size();
                    std::printf("  editor %dx%d, page sent the project back: %d sounds, machine %s\n", ed->getWidth(), ed->getHeight(), sounds, v["model"].toString().toRawUTF8());
                }
            ok &= sounds == 32;
            w.setContentNonOwned(nullptr, false);
            delete ed;
        }
    }
    inst->releaseResources(); inst.reset();
    std::puts(ok ? "PASS" : "FAIL"); return ok ? 0 : 1;
}
