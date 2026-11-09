// Drives the Omega CMI processor directly (no host): plays the keyboard and Page R, saves and restores state,
// loads a sample into a user voice, and writes a picture of each page of the screen. cmi_snapshot <outdir>
#include "../CmiPlugin.h"
#include "../CmiEditor.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out(juce::String::fromUTF8(argc > 1 ? argv[1] : "."));
    out.createDirectory();
    cmi::CmiProcessor p;
    p.setPlayConfigDetails(0, 2, 48000, 512);
    p.prepareToPlay(48000, 512);
    bool ok = true;
    auto run = [&](int blocks, std::function<void(int, juce::MidiBuffer&)> ev) {
        double pk = 0;
        for (int b = 0; b < blocks; ++b) {
            juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer m;
            if (ev) ev(b, m);
            p.processBlock(buf, m);
            pk = std::max(pk, (double) buf.getMagnitude(0, 512));
        }
        return pk;
    };
    // keyboard
    const double k = run(200, [](int b, juce::MidiBuffer& m) { if (b == 0) for (int n : { 60, 64, 67 }) m.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8) 100), 0); if (b == 120) m.addEvent(juce::MidiMessage::allNotesOff(1), 0); });
    std::printf("keyboard chord peak %.3f\n", k); ok &= k < 1.0 && (p.catalog.voices.empty() ? k < 0.001 : k > 0.05);   // no library: no voices, silence
    // Page R on its own clock
    p.set("sync", 0); p.seqPlay(true);
    const double r = run(300, {});
    std::printf("Page R peak %.3f, step %d\n", r, p.engine.seqStep.load()); ok &= p.catalog.voices.empty() ? r < 0.001 : r > 0.05;
    // a user sample
    {
        juce::File wav = out.getChildFile("tone.wav"); wav.deleteFile();
        juce::WavAudioFormat fmt; std::unique_ptr<juce::OutputStream> os(wav.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), 44100, 1, 16, {}, 0));
        if (w) { os.release(); juce::AudioBuffer<float> b(1, 44100); for (int i = 0; i < 44100; ++i) b.setSample(0, i, 0.5f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / 44100.0)); w->writeFromAudioSampleBuffer(b, 0, 44100); }
        w.reset();
        const auto err = p.loadSample(wav, 0, 57, true);
        std::printf("sample: %s\n", err.isEmpty() ? "loaded" : err.toRawUTF8()); ok &= err.isEmpty();
    }
    // a folder of sounds: three WAVs, one with a root key and a loop in its smpl chunk
    {
        juce::File dir = out.getChildFile("import"); dir.deleteRecursively(); dir.createDirectory();
        for (int k = 0; k < 3; ++k) {
            juce::File wav = dir.getChildFile(juce::String("SND") + juce::String(k + 1) + ".wav");
            juce::StringPairArray md;
            if (k == 1) { md.set("MidiUnityNote", "45"); md.set("NumSampleLoops", "1"); md.set("Loop0Type", "0"); md.set("Loop0Start", "5120"); md.set("Loop0End", "12799"); }
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::OutputStream> os(wav.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), 25000, 1, 16, md, 0));
            if (!w) { ok = false; break; }
            os.release();
            juce::AudioBuffer<float> b(1, 14000); for (int i = 0; i < 14000; ++i) b.setSample(0, i, 0.4f * (float) std::sin(i * 0.05 * (k + 1)));
            w->writeFromAudioSampleBuffer(b, 0, 14000);
        }
        int n = 0;
        const auto msg = p.importFiles({ dir }, 60, false, n);
        const cmi::Sound* s2 = nullptr;
        for (int k = 0; k < cmi::Library::USER_SLOTS; ++k) if (p.kit.userLoaded(k) && p.kit.user(k)->name == "SND2") s2 = p.kit.user(k);
        std::printf("folder import: %s; SND2 root %d loop %d-%d rate %.0f\n", msg.toRawUTF8(), s2 ? s2->root : -1, s2 ? s2->loopStart : -1, s2 ? s2->loopEnd : -1, s2 ? s2->rate : 0);
        ok &= n == 3 && s2 && s2->name == "SND2" && s2->root == 45 && s2->loopStart == 40 && s2->loopEnd == 99 && s2->rate == 25000;
    }
    // a song folder (CMI_SONGS): scanned, and a song loaded from it
    if (const char* sd = std::getenv("CMI_SONGS")) {
        p.setSongFolder(juce::File(juce::String::fromUTF8(sd)));
        const auto list = p.songFiles();
        const int before = p.songs.count();
        if (!list.empty()) p.importMidi(list.front());
        std::printf("song folder: %d songs; loading the first: %d -> %d loaded, found again: %d\n", (int) list.size(), before, p.songs.count(), list.empty() ? -1 : p.loadedSongFor(list.front()));
        ok &= !list.empty() && p.loadedSongFor(list.front()) >= 0;
    }
    // a library folder (CMI_LIBRARY): its catalogue, and songs connected to it
    if (const char* lib = std::getenv("CMI_LIBRARY")) {
        p.setLibraryFolder(juce::File(juce::String::fromUTF8(lib)));
        std::printf("library: %d files, catalogue %zu voices on %d disks\n", (int) p.libraryFiles().size(), p.catalog.voices.size(), p.catalog.disks);
        ok &= !p.catalog.voices.empty();
        // no built-in voices: Page R's tracks and the keyboard now play CMI voices from the library
        p.set("sync", 0); p.seqPlay(true);
        const double rl = run(300, {});
        const cmi::Sound* t0 = p.kit.bank.at(p.engine.seq.trk[0].sound);
        std::printf("Page R on library voices: track 1 %s, peak %.3f\n", t0 ? t0->name.c_str() : "?", rl);
        ok &= rl > 0.05 && !cmi::isBlank(p.kit, p.engine.seq.trk[0].sound);
        // factory presets: found in the library by name and disk, one loads onto the keyboard and plays
        int found = 0; for (int c : p.presetCat) found += c >= 0;
        int orch = -1; for (int i = 0; i < (int) cmi::factoryPresets().size(); ++i) if (std::string(cmi::factoryPresets()[(size_t) i].name) == "ORCH5") orch = i;
        const bool loaded = p.loadPreset(orch);
        p.seqPlay(false); run(100, {});
        const double kp = run(100, [](int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 110), 0); if (b == 60) m.addEvent(juce::MidiMessage::allNotesOff(1), 0); });
        const cmi::Sound* ks = p.kit.bank.at(juce::roundToInt(p.get("sound")));
        std::printf("factory presets: %d of %zu in the library; preset %d ORCH5 -> %s, peak %.3f\n", found, cmi::factoryPresets().size(), orch + 1, ks ? (ks->name + " from " + ks->category).c_str() : "?", kp);
        ok &= found > 600 && loaded && ks && ks->name == "ORCH5" && kp > 0.05;
    }
    // optional: real CMI disk images or .VC files given on the command line (cmi_snapshot <outdir> <disk.imd> ...)
    for (int i = 2; i < argc; ++i) {
        int n = 0;
        const auto msg = p.importFiles({ juce::File(juce::String::fromUTF8(argv[i])) }, 60, false, n);
        std::printf("import %s: %s\n", argv[i], msg.toRawUTF8());
        if (const cmi::SongEntry* e = p.songs.at(p.currentSong())) for (int t = 0; t < cmi::SONG_TRK; ++t) if (e->song->used[t]) std::printf("    %-20s %s\n", e->song->trackName[t].substr(0, 20).c_str(), e->why[t].c_str());
        ok &= msg.contains("IMPORTED") && !msg.contains("UNREADABLE");
    }
    // a zip with songs in it
    {
        juce::File zf = out.getChildFile("songs.zip"); zf.deleteFile();
        juce::ZipFile::Builder zb; int mids = 0;
        for (int i = 2; i < argc; ++i) { juce::File f(juce::String::fromUTF8(argv[i])); if (f.hasFileExtension("mid")) { zb.addFile(f, 9, "inside/" + f.getFileName()); ++mids; } }
        if (mids > 0) {
            { juce::FileOutputStream os(zf); zb.writeToStream(os, nullptr); }
            const int before = p.songs.count(); int n = 0;
            const auto msg = p.importFiles({ zf }, 60, false, n);
            std::printf("zip import: %s (%d songs before, %d after)\n", msg.toRawUTF8(), before, p.songs.count());
            ok &= p.songs.count() > before || msg.contains("DUPLICATES SKIPPED");
        }
    }
    // state round trip keeps Page R and the user sample
    juce::MemoryBlock st; p.getStateInformation(st);
    cmi::CmiProcessor q; q.setStateInformation(st.getData(), (int) st.getSize());
    const bool same = q.songs.count() == p.songs.count() && q.currentSong() == p.currentSong() && std::memcmp(&q.engine.seq.pat[0], &p.engine.seq.pat[0], sizeof(p.engine.seq.pat)) == 0 && q.kit.userLoaded(0) && q.kit.loadedCount() == p.kit.loadedCount() && q.kit.user(2)->mode == p.kit.user(2)->mode && q.kit.user(2)->ram == p.kit.user(2)->ram;
    std::printf("state %d bytes, Page R and user voice restored: %s\n", (int) st.getSize(), same ? "yes" : "NO"); ok &= same;
    // pictures
    std::string second;   // program 2: the second voice loaded
    { int n = 0; for (int i = 0; i < p.kit.bank.count.load(); ++i) if (!cmi::isBlank(p.kit, i) && n++ == 1) { second = p.kit.bank.at(i)->name; break; } }
    if (p.presetCat.size() > 1 && p.presetCat[1] >= 0) second = cmi::factoryPresets()[1].name;   // with a library: program 2 is factory preset 2
    if (p.engine.song.load()) {   // a song was given: play some of it and check it sounds
        p.set("sync", 0); p.engine.songRun = true;
        const double pk = run(1200, {});
        std::printf("song %s: beat %.1f, peak %.3f\n", p.engine.song.load()->name.c_str(), p.engine.songBeat.load(), pk);
        ok &= pk > 0.05;
    }
    // the light pen: a drawing goes straight to the PEN voice, which MIDI plays
    {
        p.pen.shape(0, 1); p.pen.shape(1, 2); p.penChanged();
        const cmi::Sound* ps = p.kit.bank.at(juce::roundToInt(p.get("sound")));
        const double pk = run(60, [](int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8) 110), 0); if (b == 40) m.addEvent(juce::MidiMessage::noteOff(1, 57), 0); });
        std::printf("light pen: keyboard voice %s, MIDI note peak %.3f\n", ps ? ps->name.c_str() : "?", pk);
        ok &= ps && ps->name == "PEN" && pk > 0.05;
    }
    // MIDI control: CC 74 moves the filter (a default), MIDI learn puts the attack on CC 21, program change picks a voice
    {
        auto cc = [&](int c, int v) { run(2, [c, v](int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent(juce::MidiMessage::controllerEvent(1, c, v), 0); }); juce::MessageManager::getInstance()->runDispatchLoopUntil(80); };
        cc(74, 0);
        const float f = p.get("filter");
        p.learnParam = cmi::paramIndex("attack");
        cc(21, 127); cc(21, 64);
        const float at = p.get("attack");
        run(2, [](int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent(juce::MidiMessage::programChange(1, 1), 0); }); juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
        const int snd = juce::roundToInt(p.get("sound"));
        std::printf("MIDI control: CC74 0 -> filter %.0f; learned CC21 -> attack %.1f; program 2 -> %s\n", f, at, p.kit.bank.at(snd)->name.c_str());
        ok &= f < 1 && at > 14 && at < 17 && p.kit.bank.at(snd)->name == second;
    }
    // exports: the song as WAV and as MIDI
    if (p.engine.song.load()) {
        juce::File wav = out.getChildFile("song.wav"), mid = out.getChildFile("song.mid"); wav.deleteFile(); mid.deleteFile();
        juce::String got;
        p.exportAudio(wav, false, [&](juce::String m) { got = m; });
        for (int i = 0; i < 600 && got.isEmpty(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        p.exportMidi(mid, [&](juce::String m) { got += " / " + m; });
        std::printf("export: %s (wav %lld bytes, mid %lld bytes)\n", got.toRawUTF8(), (long long) wav.getSize(), (long long) mid.getSize());
        ok &= wav.getSize() > 1000000 && mid.getSize() > 100;
    }
    // the drum pads: a folder of drum samples onto the pads, a pad played, kept with the project
    {
        juce::File dir = out.getChildFile("kit"); dir.deleteRecursively(); dir.createDirectory();
        const char* names[] = { "Kick.wav", "Snare.wav", "CH.wav", "OH.wav", "Crash.wav" };
        for (int k = 0; k < 5; ++k) {
            juce::File wav = dir.getChildFile(names[k]); juce::WavAudioFormat fmt;
            std::unique_ptr<juce::OutputStream> os(wav.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), 44100, 1, 16, {}, 0));
            if (!w) { ok = false; break; }
            os.release();
            const int n = k == 4 ? 88200 : 15000;
            juce::AudioBuffer<float> b(1, n); for (int i = 0; i < n; ++i) b.setSample(0, i, 0.6f * (float) std::sin(i * (0.01 + 0.05 * k)) * std::exp(-i / 4000.f));
            w->writeFromAudioSampleBuffer(b, 0, n);
        }
        const auto msg = p.padImport({ dir }, true);
        const double hitPk = run(20, [&](int b, juce::MidiBuffer&) { if (b == 0) p.hitPad(38, 120); });
        const double ch10 = run(20, [&](int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8) 120), 0); });
        juce::MemoryBlock st; p.getStateInformation(st);
        cmi::CmiProcessor q; q.setStateInformation(st.getData(), (int) st.getSize());
        const bool kept = q.pads().p[38].voice == p.pads().p[38].voice && !q.pads().p[38].voice.empty() && q.padKitName == p.padKitName;
        std::printf("pads: %s; snare pad peak %.3f, channel 10 kick %.3f; kept with the project %s (%s)\n", msg.toRawUTF8(), hitPk, ch10, kept ? "yes" : "NO", q.pads().p[38].voice.c_str());
        ok &= hitPk > 0.02 && ch10 > 0.02 && kept;
        p.padSel = 1;
        // REC DRUMS: the pads into a new song's drum track
        p.chooseSong(-1);
        const auto r0 = p.recordPads();
        run(400, [&](int b, juce::MidiBuffer&) { if (b > 200 && b % 25 == 0) p.hitPad(b % 50 ? 36 : 38, 110); if (b > 200 && b % 25 == 3) p.hitPad(b % 50 ? 36 : 38, 0); if (b % 10 == 0) p.pollRecording(); });
        const auto r1 = p.recordPads();
        int notes = 0; if (auto* e = p.songs.at(p.currentSong())) for (const auto& ev : e->song->ev) notes += (ev.status & 0xF0) == 0x90 && ev.d2 > 0 && e->voice[ev.track] == cmi::GM_KIT;
        std::printf("rec drums: %s / %s; %d drum notes in the song\n", r0.toRawUTF8(), r1.toRawUTF8(), notes);
        ok &= notes >= 4;
    }
    std::printf("sample editor on pad 38: %s\n", p.editPad(38) ? "open" : "NOT OPEN");
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto* ce = dynamic_cast<cmi::CmiEditor*>(ed.get());
    for (char pg : { 'R', 'S', '2', '7', 'D', '8', 'M', '5', 'F', 'E', 'P', 'W' }) {
        run(20, [](int b, juce::MidiBuffer& m) { if (b == 0) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0); });
        ce->showPage(pg);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File f = out.getChildFile(juce::String("page_") + pg + ".png"); f.deleteFile();
        juce::FileOutputStream fo(f); juce::PNGImageFormat png; png.writeImageToStream(img, fo);
        std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
    if (!p.songFiles().empty()) {   // the song browser with a song picked
        ce->pickSong(p.songFiles().back()); ce->showPage('F');
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File f = out.getChildFile("page_F_songs.png"); f.deleteFile();
        juce::FileOutputStream fo(f); juce::PNGImageFormat png; png.writeImageToStream(img, fo);
    }
    {
        ce->showFxMixer();
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File f = out.getChildFile("page_E_mixer.png"); f.deleteFile();
        juce::FileOutputStream fo(f); juce::PNGImageFormat png; png.writeImageToStream(img, fo);
        ce->showPage('E');
    }
    if (p.engine.song.load()) {
        ce->showPage('M');
        { auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f); juce::File f = out.getChildFile("page_M_song.png"); f.deleteFile(); juce::FileOutputStream fo(f); juce::PNGImageFormat png; png.writeImageToStream(img, fo); }
        ce->showPage('K');
        { auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f); juce::File f = out.getChildFile("page_K_kit.png"); f.deleteFile(); juce::FileOutputStream fo(f); juce::PNGImageFormat png; png.writeImageToStream(img, fo); }
        ce->showPage('S'); ce->showSongGrid(true, 1);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File f = out.getChildFile("page_S_grid.png"); f.deleteFile();
        juce::FileOutputStream fo(f); juce::PNGImageFormat png; png.writeImageToStream(img, fo);
    }
    ed.reset();
    p.padEd.close(); p.resetAll(false);
    std::printf("clear all: master %.2f, pads %s\n", p.get("master"), p.padKitName.toRawUTF8()); ok &= std::fabs(p.get("master") - 1.f) < 1e-3;
    if (const char* nf = std::getenv("CMI_STATE_FUZZ")) {   // crash test: damaged project data, then every page drawn
        juce::MemoryBlock good; p.getStateInformation(good);
        std::unique_ptr<juce::XmlElement> x(juce::AudioProcessor::getXmlFromBinary(good.getData(), (int) good.getSize()));
        const juce::String xml = x ? x->toString() : juce::String();
        juce::Random rnd(77); const int runs = std::atoi(nf);
        for (int i = 0; i < runs; ++i) {
            juce::String m = xml;
            for (int k = 0, nm = 1 + rnd.nextInt(6); k < nm && m.isNotEmpty(); ++k) {
                const int at = rnd.nextInt(m.length());
                switch (rnd.nextInt(5)) {
                    case 0: m = m.substring(0, at) + juce::String(rnd.nextInt(2000000) - 1000000) + m.substring(at + 1); break;
                    case 1: m = m.substring(0, at); break;
                    case 2: m = m.substring(0, at) + "\"-1e30\" " + m.substring(at); break;
                    case 3: m = m.replace("slot=\"", "slot=\"9" + juce::String(rnd.nextInt(99999))); break;
                    default: m = m.substring(0, at) + m.substring(at + rnd.nextInt(200)); break;
                }
            }
            juce::MemoryBlock mb;   // as copyXmlToBinary stores it: a magic number, the length, the text
            { juce::MemoryOutputStream o(mb, false); o.writeInt(0x21324356); o.writeInt((int) m.getNumBytesAsUTF8() + 1); o.write(m.toRawUTF8(), m.getNumBytesAsUTF8() + 1); }
            p.setStateInformation(mb.getData(), (int) mb.getSize());
            run(4, [](int b, juce::MidiBuffer& mm) { if (b == 0) mm.addEvent(juce::MidiMessage::noteOn(10, 38, (juce::uint8) 100), 0); });
            if (i % 25 == 0) { std::unique_ptr<juce::AudioProcessorEditor> e2(p.createEditor()); auto* c2 = dynamic_cast<cmi::CmiEditor*>(e2.get());
                for (char pg : { 'R', 'S', '2', '7', 'D', '8', 'M', '5', 'F', 'E', 'P', 'W', 'K' }) { c2->showPage(pg); (void) e2->createComponentSnapshot(e2->getLocalBounds(), true, 1.0f); } }
        }
        std::printf("project data damaged %d ways, every page drawn: no crash\n", runs);
        p.setStateInformation(good.getData(), (int) good.getSize());
    }
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
