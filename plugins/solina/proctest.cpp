// Drives the Omega Solina processor directly: sequencer -> MIDI out, MIDI learn / CC map, MIDI file export and import,
// state save and restore, and a picture of every page of the window.  solina_proc_test <outdir>
#include "SolinaPlugin.h"
#include "SolinaEditor.h"
#include <cstdio>
using namespace solina;
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    SolinaProcessor p; const double sr = 48000; const int bl = 256; p.prepareToPlay(sr, bl);
    auto block = [&](juce::MidiBuffer& m) { juce::AudioBuffer<float> b(2, bl); p.processBlock(b, m); return b; };
    // sequencer: a random progression, played for 2 seconds, comes out as MIDI and as sound
    { std::mt19937 rng(3); const juce::SpinLock::ScopedLockType l(p.songLock); p.song.pats[0] = randomPattern(rng); p.song.bpm = 120; }
    p.hostSync = false; p.playRequest = true;
    int ons = 0, offs = 0; double rms = 0;
    for (int i = 0; i < (int) (2 * sr / bl); i++) { juce::MidiBuffer m; auto b = block(m); for (const auto e : m) { if (e.getMessage().isNoteOn()) ons++; if (e.getMessage().isNoteOff()) offs++; } rms += b.getRMSLevel(0, 0, bl); }
    std::printf("     sequencer: %d note-ons, %d note-offs out, rms %.4f\n", ons, offs, rms / (2 * sr / bl));
    check(ons > 3 && rms > 1e-3, "sequencer plays the pattern and sends MIDI out");
    p.stopRequest = true; { juce::MidiBuffer m; block(m); }
    check(!p.playing, "stop");
    // incoming notes come through to the output (THRU) and sound
    { juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0); block(m); bool thru = false; for (const auto e : m) if (e.getMessage().isNoteOn() && e.getMessage().getNoteNumber() == 60) thru = true; check(thru, "MIDI thru"); }
    // a mapped controller moves its parameter (CC 20 = violin level)
    { juce::MidiBuffer m; m.addEvent(juce::MidiMessage::controllerEvent(1, 20, 0), 0); block(m); juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
      check(p.getParam(pid::vnLvl) < .01, "CC 20 sets the violin level"); }
    // MIDI learn: pick a control, move CC 70
    { p.learn = true; p.learnTarget = pid::depth; juce::MidiBuffer m; m.addEvent(juce::MidiMessage::controllerEvent(1, 30, 64), 0); block(m); juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
      p.learn = false; check(p.ccMap.count(30) && p.ccMap[30] == pid::depth, "MIDI learn maps CC 30 to ensemble depth");
      juce::MidiBuffer m2; m2.addEvent(juce::MidiMessage::controllerEvent(1, 30, 127), 0); block(m2); juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
      check(p.getParam(pid::depth) > .99, "learned CC moves the control"); }
    // program change picks a preset
    { juce::MidiBuffer m; m.addEvent(juce::MidiMessage::programChange(1, 31), 0); block(m); juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
      check(p.getCurrentProgram() == 31 && p.getParam(pid::phOn) > .5, "program change 32 loads a phaser preset"); }
    // MIDI file out and back in
    const auto mid = out.getChildFile("solina-test.mid");
    { const juce::SpinLock::ScopedLockType l(p.songLock); p.song.mode = 1; p.song.chain = {0, 0}; }
    check(p.exportMidi(mid, true).isEmpty() && mid.existsAsFile(), "export .mid");
    size_t before; { const juce::SpinLock::ScopedLockType l(p.songLock); before = p.song.pats[0].notes.size(); }
    check(p.importMidi(mid, false, true).isEmpty(), "import .mid");
    { const juce::SpinLock::ScopedLockType l(p.songLock); std::printf("     %zu notes in, %zu + %zu back, chain %zu\n", before, p.song.pats[0].notes.size(), p.song.pats[1].notes.size(), p.song.chain.size());
      check(p.song.pats[0].notes.size() + p.song.pats[1].notes.size() == before * 2, "round trip keeps every note"); }
    // state
    juce::MemoryBlock st; p.getStateInformation(st);
    SolinaProcessor q; q.setStateInformation(st.getData(), (int) st.getSize());
    { const juce::SpinLock::ScopedLockType l(q.songLock); check(q.song.pats[0].notes.size() > 0 && q.song.mode == 1, "song saved with the project"); }
    check(q.ccMap.count(30) && std::abs(q.getParam(pid::phOn) - 1) < 1e-6, "CC map and parameters saved with the project");
    // pictures of the window
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); auto* se = dynamic_cast<SolinaEditor*>(ed.get());
    const char* names[5] = {"panel", "mixer", "fx", "seq", "midi"};
    for (int i = 0; i < 5; i++) { se->show(i); auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os(out.getChildFile(juce::String("solina-") + names[i] + ".png")); os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os); }
    ed.reset();
    std::printf(fails ? "FAILED %d\n" : "PASS\n", fails); return fails ? 1 : 0;
}
