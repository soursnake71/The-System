// Drives the Omega Seven processor directly (no plugin wrapper): sysex banks and parameter changes in, the panel,
// the arpeggiator and sequencer on the host transport, MIDI files, and the state. usage: seven_proc_test <dir>
#include "SevenPlugin.h"
#include "SevenEditor.h"
using namespace seven;
struct Transport : juce::AudioPlayHead {
    double ppq = 0, bpm = 120; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying(playing); p.setBpm(bpm); p.setPpqPosition(ppq); return p; }
};
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir(argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    SevenProcessor p; Transport tp; p.setPlayHead(&tp);
    const double sr = 48000; const int block = 256; p.prepareToPlay(sr, block);
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); if (!ok) fails++; };
    auto run = [&](double secs, std::function<void(int, juce::MidiBuffer&)> ev, int* notesOut = nullptr) {
        double rms = 0; const int n = (int) (secs * sr);
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(2, block); b.clear(); juce::MidiBuffer m; if (ev) ev(pos, m);
            if (tp.playing) tp.ppq += block / sr * tp.bpm / 60;
            p.processBlock(b, m);
            for (const auto meta : m) if (meta.getMessage().isNoteOn() && notesOut) (*notesOut)++;
            for (int i = 0; i < block; ++i) rms += b.getSample(0, i) * b.getSample(0, i);
        }
        return std::sqrt(rms / n);
    };
    auto sysex = [](const std::vector<uint8_t>& d) { return juce::MidiMessage::createSysExMessage(d.data() + 1, (int) d.size() - 2); };
    // bank dump in: protected internal memory -> cartridge
    Bank bank {}; for (int i = 0; i < 32; ++i) { bank[(size_t) i] = initVoice(); setName(bank[(size_t) i], "SYX " + std::to_string(i + 1)); }
    run(0.05, [&](int pos, juce::MidiBuffer& m) { if (!pos) m.addEvent(sysex(bankToSysex(bank, 0)), 0); });
    check(p.bank(true) && voiceName((*p.bank(true))[4]) == "SYX 5     ", "bank dump over MIDI went to the cartridge");
    p.panel.protect[0] = false;
    bank[0][134] = 9; setName(bank[0], "TO INT");
    run(0.05, [&](int pos, juce::MidiBuffer& m) { if (!pos) m.addEvent(sysex(bankToSysex(bank, 0)), 0); });
    check(voiceName((*p.bank(false))[0]) == "TO INT    ", "with protect off, a bank dump replaces internal memory");
    run(0.05, [&](int pos, juce::MidiBuffer& m) { if (!pos) m.addEvent(sysex(paramChange(0, 0, 135, 6)), 0); });
    check(p.voice()[135] == 6, "parameter change (feedback = 6) reached the edit buffer");
    run(0.05, [&](int pos, juce::MidiBuffer& m) { if (!pos) m.addEvent(sysex(paramChange(0, 2, 65, 7)), 0); });
    check(p.func()[F::pbRange] == 7, "function parameter change (bend range = 7)");
    // the panel: store with protect, voice name, op on/off
    { const juce::ScopedLock l(p.lock); p.panel.memSelect(false); p.panel.button(3); p.panel.editBtn(); p.panel.button(31); p.panel.button(8); p.panel.button(9); p.panel.button(31);
      p.panel.storeBtn(); p.panel.button(32); }
    check(voiceName((*p.bank(false))[31]).rfind("HI", 0) == 0, "named a voice HI on the panel and stored it to 32 (" + juce::String(voiceName((*p.bank(false))[31])) + ")");
    // arpeggiator on its own clock (120 BPM, 1/16, up/down 2 octaves)
    { const juce::ScopedLock l(p.lock); p.panel.funcBtn(); p.panel.button(25); p.panel.yes(); }
    check(p.seq().arp.on, "FUNCTION 25 YES switched the arpeggiator on");
    int notes = 0;
    const double rms = run(2.0, [](int pos, juce::MidiBuffer& m) { if (!pos) for (int k : {60, 64, 67}) m.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8) 100), 0); }, &notes);
    check(notes >= 15 && notes <= 17 && rms > 1e-3, "arpeggiator: " + juce::String(notes) + " notes out in 2 s at 1/16, 120 BPM");
    run(0.1, [](int pos, juce::MidiBuffer& m) { if (!pos) for (int k : {60, 64, 67}) m.addEvent(juce::MidiMessage::noteOff(1, k), 0); });
    { const juce::ScopedLock l(p.lock); p.panel.no(); }
    // sequencer: step write from MIDI notes with FUNCTION 28, then play locked to the host
    { const juce::ScopedLock l(p.lock); p.panel.button(28); }
    run(0.1, [](int pos, juce::MidiBuffer& m) { if (!pos) { int t = 0; for (int k : {36, 43, 48, 43}) { m.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8) 100), t++); m.addEvent(juce::MidiMessage::noteOff(1, k), t++); } } });
    check(p.seq().pats[0].steps[3].notes.size() == 1 && p.seq().pats[0].steps[3].notes[0].first == 43, "step write: four notes into steps 1-4");
    { const juce::ScopedLock l(p.lock); p.seq().ext = true; p.seq().pats[0].len = 4; p.panel.memSelect(false); }
    tp.playing = true; tp.ppq = 0; notes = 0;
    run(2.0, nullptr, &notes);
    check(notes >= 15 && notes <= 17, "sequencer on the host transport: " + juce::String(notes) + " notes in 2 s (4-step loop of 16ths)");
    tp.playing = false; run(0.1, nullptr);
    check(!p.playing(), "host stop stops the sequencer");
    // MIDI file export / import of the pattern
    const auto mid = patternToMidi(p.seq().pats[0], &p.voice(), 120, 0, 1);
    juce::MemoryBlock mb(mid.data(), mid.size());
    { const juce::ScopedLock l(p.lock); p.seq().pat = 5; }
    const auto msg = p.loadBytes(mb, "pattern.mid", "pattern");
    check(p.seq().pats[5].steps[2].notes.size() == 1 && p.seq().pats[5].steps[2].notes[0].first == 48, "pattern -> .mid -> pattern 6 (" + msg + ")");
    // .syx file into the cartridge, single voice into the edit buffer
    const auto vs = voiceToSysex(bank[7], 0); juce::MemoryBlock vb(vs.data(), vs.size());
    p.loadBytes(vb, "one.syx", "cart");
    check(voiceName(p.voice()) == voiceName(bank[7]), "single-voice .syx into the edit buffer");
    // state
    juce::MemoryBlock st; p.getStateInformation(st);
    SevenProcessor q; q.setStateInformation(st.getData(), (int) st.getSize());
    check(q.bank(true) != nullptr && voiceName((*q.bank(false))[31]) == voiceName((*p.bank(false))[31]) && q.seq().pats[5].steps[2].notes.size() == 1, "state round trip (banks, cartridge, patterns)");
    // MIDI file playback through the synth
    { std::vector<TickMsg> ev; for (int i = 0; i < 8; ++i) { ev.push_back({(int64_t) i * 48, {0x90, (uint8_t) (60 + i), 100}}); ev.push_back({(int64_t) i * 48 + 40, {0x80, (uint8_t) (60 + i), 0}}); }
      const auto f = writeMidiFile(ev, 96, 120, "t"); juce::MemoryBlock fb(f.data(), f.size()); p.loadBytes(fb, "t.mid", "play"); }
    check(run(1.0, nullptr) > 1e-3 && p.playerPlaying(), "MIDI file player sounds");
    // pictures of the window: play mode, and EDIT with the sequencer page open
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1880, 800);
        auto shot = [&](const char* name) { const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f); juce::FileOutputStream fo(dir.getChildFile(name)); fo.setPosition(0); fo.truncate(); juce::PNGImageFormat().writeImageToStream(img, fo); };
        { const juce::ScopedLock l(p.lock); p.panel.memSelect(false); p.panel.button(2); }
        shot("seven-editor.png");
        { const juce::ScopedLock l(p.lock); p.panel.editBtn(); p.panel.button(20); p.panel.opSelect(); }
        if (auto* se = dynamic_cast<SevenEditor*>(ed.get())) se->showSequencer(true);
        shot("seven-editor-seq.png");
        std::puts("pictures: seven-editor.png, seven-editor-seq.png");
    }
    std::puts(fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
