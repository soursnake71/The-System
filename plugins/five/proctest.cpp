// Drives the RX5 processor and editor directly (linked with the plugin's code): the built-in factory kit, pads,
// MIDI in / out, the sequencer free running and following a host transport, SysEx replies, individual outputs, automation,
// every file job (cartridge, MIDI, WAV, SysEx, RX5 bank, kit folder), the plugin state round trip, and the panel
// (keys, menus, LCD) with a picture of it.
// usage: five_proc_test <scratchDir> [shotsDir]   (shotsDir: also writes plugin.png there)
#include "FivePlugin.h"
#include "FiveEditor.h"
#include <cstdio>
using namespace five;

static bool ok = true;
static void check(bool c, const juce::String& m) { std::printf("%s %s\n", c ? "ok  " : "FAIL", m.toRawUTF8()); std::fflush(stdout); ok &= c; }

struct FakeHead : juce::AudioPlayHead {
    bool playing = false; double ppq = 0, bpm = 100;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying(playing); p.setPpqPosition(ppq); p.setBpm(bpm); return p; }
};

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir(argc > 1 ? juce::String::fromUTF8(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const juce::File shots = argc > 2 ? juce::File(juce::String::fromUTF8(argv[2])) : juce::File();
    dir.createDirectory();
    const double sr = 48000; const int block = 256;
    auto proc = std::make_unique<FiveProcessor>();
    proc->prepareToPlay(sr, block);
    auto pump = [](int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };
    struct Run { double rms = 0, peak = 0; bool nan = false; std::vector<juce::MidiMessage> out; };
    auto run = [&](double secs, std::function<void(int, juce::MidiBuffer&)> in = nullptr, juce::AudioBuffer<float>* keep = nullptr) {
        Run r; const int n = (int) (secs * sr); const int chs = proc->getTotalNumOutputChannels();
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(juce::jmax(2, chs), block); b.clear(); juce::MidiBuffer m; if (in) in(pos, m);
            proc->processBlock(b, m);
            for (const auto meta : m) r.out.push_back(meta.getMessage());
            for (int c = 0; c < 2; c++) for (int i = 0; i < block; i++) { const float x = b.getSample(c, i); r.nan |= !std::isfinite(x); r.rms += x * x; r.peak = juce::jmax(r.peak, (double) std::fabs(x)); }
            if (keep) for (int c = 0; c < juce::jmin(keep->getNumChannels(), b.getNumChannels()); c++) keep->addFrom(c, 0, b, c, 0, block);
        }
        r.rms = std::sqrt(r.rms / (2.0 * n)); return r;
    };

    // ---- the factory kit is built in
    check(proc->factory().size() == 51, "factory library: " + juce::String((int) proc->factory().size()) + " recordings");
    check(proc->waveName(0) == "BD 1" || proc->waveName(0).startsWith("INT"), "voice 0 plays " + proc->waveName(0));
    { auto r = run(.3); check(r.rms < 1e-6 && !r.nan, "silent at rest"); }
    for (int v : { 0, 1, 7, 9, 22 }) { proc->playVoice(v, 120, 0); auto r = run(.4); check(r.rms > 1e-3 && r.peak < 1.5 && !r.nan, juce::String("voice ") + VOICE_NAMES[v] + ": rms " + juce::String(r.rms, 4) + " peak " + juce::String(r.peak, 3)); }
    proc->damp(); run(.2);

    // ---- MIDI in (RX5 note map, receive channel 10) and MIDI out (notes out)
    {
        auto r = run(.4, [](int pos, juce::MidiBuffer& m) { if (pos == 0) m.addEvent(juce::MidiMessage::noteOn(10, 45, (juce::uint8) 110), 10); });
        check(r.rms > 1e-3, "MIDI note 45 on ch 10 plays BD 1: rms " + juce::String(r.rms, 4));
        auto q = run(.3, [](int pos, juce::MidiBuffer& m) { if (pos == 0) m.addEvent(juce::MidiMessage::noteOn(3, 45, (juce::uint8) 110), 10); });
        check(q.rms < r.rms * .2, "other channels are ignored");
        { const FiveProcessor::Edit l(proc->lock); proc->state().midi.notesOut = true; proc->state().midi.txCh = 4; }
        proc->playVoice(1, 100, 1); auto o = run(.2); int on = 0, off = 0;
        for (const auto& m : o.out) { if (m.isNoteOn() && m.getChannel() == 4 && m.getNoteNumber() == 52) on++; if (m.isNoteOff() && m.getNoteNumber() == 52) off++; }
        check(on == 1 && off == 1, "a pad sends note on / off on the transmit channel (" + juce::String(on) + "/" + juce::String(off) + ")");
        { const FiveProcessor::Edit l(proc->lock); proc->state().midi.notesOut = false; }
    }

    // ---- the sequencer: a randomized pattern, free running
    const auto genre = proc->randomizeAll("all"); pump();
    check(genre.isNotEmpty(), "randomize all: " + genre);
    proc->ui.mode = 0; proc->ui.ptn = 0; proc->start();
    { auto r = run(2); check(proc->running() && r.rms > 1e-3 && !r.nan && r.peak < 2, "pattern 00 plays: rms " + juce::String(r.rms, 4) + ", " + juce::String(proc->position().bar) + ":" + juce::String(proc->position().beat)); }
    proc->stop(); run(.05);
    check(!proc->running(), "STOP");
    // real-time write: pads land in the pattern while it loops
    {
        { const FiveProcessor::Edit l(proc->lock); clearPattern(proc->state(), 5); proc->state().quantize = Q_1_16; }
        proc->selectPattern(5); proc->setRtw(true); proc->start(); run(.1);
        for (int k = 0; k < 4; k++) { proc->playVoice(13, 100, 0); run(.25); }
        proc->stop(); proc->setRtw(false); run(.05);
        const auto n = proc->state().patterns[5].events.size();
        check(n >= 3, "REAL TIME WRITE recorded " + juce::String((int) n) + " hits into pattern 05");
        proc->selectPattern(0); run(.02);
    }
    // song mode, MIDI clock out
    {
        { const FiveProcessor::Edit l(proc->lock); proc->state().midi.clockOut = true; }
        proc->ui.mode = 1; proc->ui.song = 0; proc->start();
        auto r = run(1); int clocks = 0, starts = 0; for (const auto& m : r.out) { clocks += m.isMidiClock(); starts += m.isMidiStart(); }
        const double bpm = proc->position().bpm, want = 24 * bpm / 60;
        check(starts == 1 && std::abs(clocks - want) <= 2, "song plays with MIDI clock out: " + juce::String(clocks) + " clocks in 1 s at " + juce::String(bpm, 1) + " BPM");
        proc->stop(); run(.05);
        { const FiveProcessor::Edit l(proc->lock); proc->state().midi.clockOut = false; }
        proc->ui.mode = 0;
    }
    // following the host transport
    {
        FakeHead head; proc->setPlayHead(&head); proc->setHostSync(true); proc->ui.mode = 0; proc->ui.ptn = 0;
        head.playing = true; head.bpm = 100;
        double fired = 0; int pos0 = 0;
        for (int k = 0; k < 200; k++) {
            head.ppq = pos0 / sr * head.bpm / 60; juce::AudioBuffer<float> b(2, block); b.clear(); juce::MidiBuffer m; proc->processBlock(b, m); pos0 += block;
            for (int i = 0; i < block; i++) fired += b.getSample(0, i) * b.getSample(0, i);
        }
        const auto p = proc->position();
        check(proc->running() && fired > 0 && std::abs(p.bpm - 100) < 1e-6, "host sync: plays at the host's 100 BPM, at " + juce::String(p.bar) + ":" + juce::String(p.beat));
        head.playing = false; run(.05);
        check(!proc->running(), "host stop stops the sequencer");
        proc->setPlayHead(nullptr); proc->setHostSync(false);
    }

    // ---- SysEx: identity request answered (after the message thread has seen it)
    {
        const uint8_t idr[] = { 0x7E, 0x7F, 0x06, 0x01 };
        run(.02, [&](int pos, juce::MidiBuffer& m) { if (pos == 0) m.addEvent(juce::MidiMessage::createSysExMessage(idr, 4), 0); });
        pump(120);
        auto r = run(.02); bool reply = false; for (const auto& m : r.out) if (m.isSysEx() && m.getSysExDataSize() > 4 && m.getSysExData()[3] == 0x02) reply = true;
        check(reply, "identity request -> identity reply");
        // an Omega parameter message changes the state
        const auto pm = omegaParam(0, 3, "pitch", -500);
        run(.02, [&](int pos, juce::MidiBuffer& m) { if (pos == 0) m.addEvent(juce::MidiMessage::createSysExMessage(pm.data() + 1, (int) pm.size() - 2), 0); });
        pump(120); run(.02);
        check(std::abs(proc->state().voice[3][vPitch] + 500) < 1e-9, "Omega param SysEx: voice 3 pitch " + juce::String(proc->state().voice[3][vPitch]));
    }

    // ---- automation: a channel fader parameter reaches the state, and an edit reaches the parameter
    {
        auto* p = proc->apvts.getParameter("fader2"); p->setValueNotifyingHost(.25f); run(.02);
        check(std::abs(proc->state().channel[1].fader - .25) < 1e-3, "automation: SD fader -> state " + juce::String(proc->state().channel[1].fader, 3));
        proc->setChannelParam(1, cFader, .8); check(std::abs(p->convertFrom0to1(p->getValue()) - .8f) < 1e-3, "panel edit -> SD fader parameter");
    }

    // ---- individual outputs: an enabled output takes its channel out of the main mix
    {
        auto layout = proc->getBusesLayout(); layout.outputBuses.getReference(1) = juce::AudioChannelSet::mono();
        check(proc->setBusesLayout(layout), "enable individual out 1 (BD)");
        proc->prepareToPlay(sr, block);
        proc->setChannelParam(0, cSendRev, 0); proc->setChannelParam(0, cSendDly, 0); proc->setDlyParam(dOn, 0); proc->setRevParam(rLevel, 0); run(.3);   // no effect tails from the tests before
        juce::AudioBuffer<float> keep(proc->getTotalNumOutputChannels(), block);
        proc->playVoice(0, 120, 0); double aux = 0, main = 0;
        for (int k = 0; k < 20; k++) { keep.clear(); run((double) block / sr, nullptr, &keep); for (int i = 0; i < block; i++) { aux += keep.getSample(2, i) * keep.getSample(2, i); main += keep.getSample(0, i) * keep.getSample(0, i); } }
        check(aux > 1e-3 && main < aux * 1e-3, "BD on its own output (" + juce::String(aux, 3) + "), not in the main mix (" + juce::String(main, 6) + ")");
        layout.outputBuses.getReference(1) = juce::AudioChannelSet::disabled(); proc->setBusesLayout(layout); proc->prepareToPlay(sr, block);
    }

    // ---- files
    {
        const auto cart = dir.getChildFile("five-test.o5cart");
        const bool saved = proc->saveCartFile(cart).isEmpty(); check(saved && cart.getSize() > 1000, "save cartridge (" + juce::String(cart.getSize()) + " bytes)");
        const auto name = proc->state().patterns[0].name;
        proc->initAll(); check(proc->state().patterns[0].events.empty(), "INIT ALL");
        check(proc->loadCartFile(cart).isEmpty() && proc->state().patterns[0].name == name, "load cartridge: pattern 00 is " + juce::String(name));
        const auto mid = dir.getChildFile("five-test.mid"); proc->ui.song = 0;
        const bool em = proc->exportMidiFile(mid, true).isEmpty(); check(em && mid.getSize() > 100, "export song as MIDI (" + juce::String(mid.getSize()) + " bytes)");
        check(proc->importMidiFile(mid, 40, 5).isEmpty() && !proc->state().songs[5].steps.empty(), "import it into song F from pattern 40");
        const auto wav = dir.getChildFile("five-test.wav"); proc->ui.ptn = 0;
        const bool ew = proc->exportWav(wav, false).isEmpty(); check(ew && wav.getSize() > 44100 * 6, "export pattern 00 as WAV (" + juce::String(wav.getSize()) + " bytes)");
        const auto syx = dir.getChildFile("five-test.syx");
        const bool sx = proc->saveSyxFile(syx, 0).isEmpty() && proc->loadSyxFile(syx).isEmpty(); check(sx, "SysEx file save / load (" + juce::String(syx.getSize()) + " bytes)");
        const auto bin = dir.getChildFile("five-test.bin"); const auto br = proc->saveBankFile(bin);
        check(bin.getSize() == BANK_SIZE, "save RX5 bank: " + (br.isEmpty() ? juce::String("all voices") : br));
        const bool lb = proc->loadBankFile(bin).isEmpty(); check(lb && !proc->state().library.empty(), "load RX5 bank: " + juce::String((int) proc->state().library.size()) + " voices in the library");
        // a kit folder: copies of a few factory files under generic names
        const auto kd = dir.getChildFile("kit"); kd.deleteRecursively(); kd.createDirectory();
        const juce::File kitSrc = juce::File::getCurrentWorkingDirectory().getChildFile("five/kit");
        if (kitSrc.isDirectory()) {
            kitSrc.getChildFile("BDrum2-RX5.wav").copyFileTo(kd.getChildFile("Kick 01.wav"));
            kitSrc.getChildFile("Clap-RX5.wav").copyFileTo(kd.getChildFile("handclap.wav"));
            kitSrc.getChildFile("Cuica-RX5.wav").copyFileTo(kd.getChildFile("Cuica-RX5.wav"));
            kitSrc.getChildFile("Gun-RX5.wav").copyFileTo(kd.getChildFile("laser zap.wav"));
            const auto kr = proc->loadKit(kd); pump();
            check(kr.isEmpty() && proc->state().samples[0].lib == 0 && proc->state().samples[22].lib == 22 && proc->state().library.size() == 4,
                  "load kit folder: " + proc->lastMidiMessage() + ", library " + juce::String((int) proc->state().library.size()));
            int cuica = -1; for (const auto& w : proc->state().library) if (w.name == "Cuica") cuica = w.id;
            check(cuica == 35, "a WRC-01 name keeps its cartridge slot (Cuica -> " + juce::String(cuica) + ")");
        } else check(false, "five/kit not found from " + juce::File::getCurrentWorkingDirectory().getFullPathName());
        proc->setWave(3, 33); pump(); check(proc->waveName(3) == "AgoHI", "EDIT VOICE WAVEFORM: voice 3 plays " + proc->waveName(3));
        proc->playVoice(3, 120, 0); auto r = run(.3); check(r.rms > 1e-3, "and sounds: rms " + juce::String(r.rms, 4));
    }

    // ---- the plugin state round trip
    {
        juce::MemoryBlock mb; proc->getStateInformation(mb);
        auto p2 = std::make_unique<FiveProcessor>(); p2->setStateInformation(mb.getData(), (int) mb.getSize()); p2->prepareToPlay(sr, block);
        bool same = true; for (int i = 0; i < NUM_PATTERNS; i++) same &= p2->state().patterns[(size_t) i].events == proc->state().patterns[(size_t) i].events;
        check(same && p2->state().wave[3] == 33 && p2->state().library.size() == proc->state().library.size() && p2->state().samples[0].data.size() == proc->state().samples[0].data.size(),
              "state round trip (" + juce::String((int) mb.getSize()) + " bytes): patterns, waveform picks, library, samples");
    }

    // ---- the panel
    if (auto* ed = dynamic_cast<FiveEditor*>(proc->createEditor())) {
        auto& P = ed->panel(); proc->ui = Ui(); proc->initAll(); pump();
        check(P.lcdText().startsWith("PLAY PTN00"), "LCD: " + P.lcdText().replace("\n", " | "));
        P.key("EDIT VOICE"); check(P.lcdText().startsWith("01 SELECT VOICE"), "EDIT VOICE: " + P.lcdText().replace("\n", " | "));
        P.key("ACC2"); P.key("ACC2"); P.key("YES");
        check(P.lcdText().startsWith("02 PITCH") && std::abs(proc->state().voice[0][vPitch] - 100) < 1e-9, "ACCENT 2 x2 -> PITCH, YES -> +100 cent: " + P.lcdText().replace("\n", " | "));
        P.key("LEGEND:rev:1"); P.key("YES"); check(proc->state().rev[rProgram] == 1, "legend REV 01, YES -> program HALL2");
        P.key("M:PTN"); P.key("N1"); P.key("N2"); check(proc->ui.ptn == 12, "keypad 1 2 selects pattern 12");
        P.key("STEP WRITE"); P.key("PAD0"); P.key("YES"); P.key("YES"); P.key("PAD0");
        check(proc->state().patterns[12].events.size() == 2, "STEP WRITE: two steps entered (" + juce::String((int) proc->state().patterns[12].events.size()) + ")");
        P.key("JOB"); P.key("N0"); P.key("N4"); check(P.lcdText().startsWith("04 RANDOMIZE"), "JOB 04: " + P.lcdText().replace("\n", " | "));
        P.key("START"); run(.5); check(proc->running(), "START from the panel"); P.key("STOP"); run(.05); check(!proc->running(), "STOP from the panel");
        P.key("M:PTN"); pump(100);
        for (int k = 0; k < 4; k++) { ed->setSize(1300, 973); pump(30); }
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        const auto f = (shots.isDirectory() ? shots : dir).getChildFile("plugin.png"); f.deleteFile();
        { juce::FileOutputStream fo(f); juce::PNGImageFormat().writeImageToStream(img, fo); }
        check(img.getWidth() == 1300 && f.getSize() > 10000, "editor picture " + f.getFullPathName());
        ed->setSize(650, 487); check(std::abs(ed->getHeight() * 1300.0 / 973 - ed->getWidth()) < 3, "fixed aspect when resized: " + juce::String(ed->getWidth()) + "x" + juce::String(ed->getHeight()));
        delete ed;
    } else check(false, "editor");
    proc.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
