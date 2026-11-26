// Headless check of RX5: load the .vst3 like a DAW, play its voices from MIDI (RX5 note map, channel 10), run the
// sequencer from a host transport (after a random pattern is sent in as an Omega SysEx dump), check the individual
// outputs, round-trip its state and open its window. The processor itself is tested by plugins/five/proctest.cpp.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

struct Head : juce::AudioPlayHead {
    bool playing = false; double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying(playing); p.setPpqPosition(ppq); p.setBpm(bpm); return p; }
};

// usage: five_host_test <RX5.vst3>
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) return 2;
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found; juce::VST3PluginFormat vst3; vst3.findAllTypesForFile(found, juce::String::fromUTF8(argv[1]));
    if (found.isEmpty()) { std::puts("FAIL no plugin"); return 1; }
    juce::String err; const double sr = 48000; const int block = 256;
    auto inst = fm.createPluginInstance(*found[0], sr, block, err);
    if (!inst) { std::printf("FAIL load: %s\n", err.toRawUTF8()); return 1; }
    bool ok = true;
    auto check = [&](bool c, const juce::String& m) { std::printf("  %s %s\n", c ? "ok  " : "FAIL", m.toRawUTF8()); ok &= c; };
    std::printf("%s | %s | params %d | out buses %d | midi in %d out %d\n", found[0]->name.toRawUTF8(), found[0]->isInstrument ? "instrument" : "effect",
                (int) inst->getParameters().size(), inst->getBusCount(false), (int) inst->acceptsMidi(), (int) inst->producesMidi());
    check(found[0]->isInstrument && inst->acceptsMidi() && inst->getBusCount(false) == 13, "an instrument with MIDI and 1 + 12 output buses");
    inst->prepareToPlay(sr, block);
    Head head; inst->setPlayHead(&head);
    auto run = [&](double secs, std::function<void(int, juce::MidiBuffer&)> in) {
        const int n = (int) (secs * sr), ch = juce::jmax(2, inst->getTotalNumOutputChannels()); double rms = 0, pk = 0; bool nan = false;
        for (int pos = 0; pos < n; pos += block) {
            head.ppq = pos / sr * head.bpm / 60;
            juce::AudioBuffer<float> b(ch, block); b.clear(); juce::MidiBuffer midi; if (in) in(pos, midi);
            inst->processBlock(b, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const float x = b.getSample(c, i); nan |= !std::isfinite(x); rms += x * x; pk = juce::jmax(pk, (double) std::abs(x)); }
            juce::Thread::sleep(1);
        }
        return std::make_tuple(std::sqrt(rms / (2.0 * n)), pk, nan);
    };
    {
        auto [rms, pk, nan] = run(1, [](int pos, juce::MidiBuffer& m) { if (pos == 0) for (int k : { 45, 52, 57, 60 }) m.addEvent(juce::MidiMessage::noteOn(10, k, (juce::uint8) 110), 0); });
        check(!nan && rms > 1e-3 && pk < 1.5, "MIDI notes on channel 10 play the factory kit: rms " + juce::String(rms, 4) + " peak " + juce::String(pk, 3));
    }
    {
        // an Omega pattern dump (F0 7D 4F 35 02 n <packed JSON> cs F7) with a four-on-the-floor bass drum into pattern 00
        juce::String json = "{\"name\":\"HOST\",\"beats\":4,\"unit\":4,\"bars\":1,\"swing\":50,\"events\":[";
        for (int t = 0; t < 96; t += 24) json << (t ? "," : "") << "{\"t\":" << t << ",\"v\":0,\"vel\":120,\"acc\":0}";
        json << "]}";
        std::vector<uint8_t> raw(json.toRawUTF8(), json.toRawUTF8() + json.getNumBytesAsUTF8()), body { 0x02, 0x00 };
        for (size_t i = 0; i < raw.size(); i += 7) { uint8_t msb = 0; const size_t n = std::min<size_t>(7, raw.size() - i); for (size_t k = 0; k < n; k++) msb |= (uint8_t) ((raw[i + k] >> 7) << k); body.push_back(msb); for (size_t k = 0; k < n; k++) body.push_back(raw[i + k] & 0x7F); }
        int s = 0; for (auto x : body) s += x; std::vector<uint8_t> msg { 0x7D, 0x4F, 0x35 }; msg.insert(msg.end(), body.begin(), body.end()); msg.push_back((uint8_t) ((-s) & 0x7F));
        run(.05, [&](int pos, juce::MidiBuffer& m) { if (pos == 0) m.addEvent(juce::MidiMessage::createSysExMessage(msg.data(), (int) msg.size()), 0); });
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        run(.3, nullptr);
        head.playing = true; head.bpm = 120;
        auto [rms, pk, nan] = run(2, nullptr);
        head.playing = false; auto [r2, p2, n2] = run(1.5, nullptr); (void) p2; (void) n2;
        check(!nan && rms > 1e-3 && r2 < rms, "host transport plays the pattern sent by SysEx: rms " + juce::String(rms, 4) + ", after stop " + juce::String(r2, 5));
    }
    {
        // individual output 1 (BD) on: the bass drum leaves the main mix
        auto layout = inst->getBusesLayout(); layout.outputBuses.getReference(1) = juce::AudioChannelSet::mono();
        inst->releaseResources(); const bool set = inst->setBusesLayout(layout); inst->prepareToPlay(sr, block);   // a VST3 changes buses only while inactive
        double aux = 0, main = 0; const int ch = inst->getTotalNumOutputChannels();
        for (int pos = 0; pos < (int) (.5 * sr); pos += block) {
            juce::AudioBuffer<float> b(ch, block); b.clear(); juce::MidiBuffer midi; if (pos == 0) midi.addEvent(juce::MidiMessage::noteOn(10, 45, (juce::uint8) 120), 0);
            inst->processBlock(b, midi);
            for (int i = 0; i < block; i++) { main += b.getSample(0, i) * b.getSample(0, i); if (ch > 2) aux += b.getSample(2, i) * b.getSample(2, i); }
        }
        std::printf("  layout set %d, %d output channels, bus 1 %s\n", (int) set, ch, inst->getBus(false, 1) ? inst->getBus(false, 1)->getCurrentLayout().getDescription().toRawUTF8() : "-");
        check(set && ch == 3 && aux > 1e-3 && main < aux * 1e-2, "individual output 1 carries the BD (" + juce::String(aux, 3) + " / main " + juce::String(main, 5) + ")");
    }
    juce::MemoryBlock st; inst->getStateInformation(st); inst->setStateInformation(st.getData(), (int) st.getSize());
    check(st.getSize() > 500, "state " + juce::String((int) st.getSize()) + " bytes restored");
    if (auto* ed = inst->createEditorIfNeeded()) { check(ed->getWidth() > 500, "editor " + juce::String(ed->getWidth()) + "x" + juce::String(ed->getHeight()) + " opens"); delete ed; }
    inst->setPlayHead(nullptr); inst->releaseResources(); inst.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
