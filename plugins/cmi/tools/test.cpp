// Omega CMI engine tests, no framework: cmi_test <outdir>
// Checks the channel card maths against the hardware formulas, renders every library sound and the Page R demo
// song to WAV files, and fails on silence, clipping, NaNs or wrong pitch.
#include "../core/Disk.h"
#include "../core/Song.h"
#include "../core/TestVoices.h"
#include "../core/Catalog.h"
#include "../core/GridEdit.h"
#include "../core/Record.h"
#include "../core/Pen.h"
#include "../core/Pads.h"
#include "../core/SampleEdit.h"
#include <cstdio>
#include <set>
#include <chrono>

using namespace cmi;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++fails; } } while (0)

static void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int fs) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return;
    const uint32_t n = (uint32_t) L.size(), bytes = n * 4;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(2); u32((uint32_t) fs); u32((uint32_t) fs * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (uint32_t i = 0; i < n; ++i) { int16_t a = (int16_t) std::lround(clampd(L[i], -1, 1) * 32767), b = (int16_t) std::lround(clampd(R[i], -1, 1) * 32767); std::fwrite(&a, 2, 1, f); std::fwrite(&b, 2, 1, f); }
    std::fclose(f);
}

// frequency by counting rising zero crossings of a low-passed signal
static double freqOf(const std::vector<float>& x, int fs, size_t from, size_t to) {
    int first = -1, last = -1, n = 0; double y = 0, prev = 0;
    for (size_t i = from; i < to; ++i) { y += (x[i] - y) * 0.2; if (prev < 0 && y >= 0) { if (first < 0) first = (int) i; last = (int) i; ++n; } prev = y; }
    return n > 1 ? (n - 1) * (double) fs / (last - first) : 0;
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const int fs = 48000;
    auto t0 = std::chrono::steady_clock::now();
    TestKit lib;   // synthesised test voices: the app itself has none
    auto t1 = std::chrono::steady_clock::now();
    std::printf("library: %d sounds built in %.0f ms\n", lib.size(), std::chrono::duration<double, std::milli>(t1 - t0).count());

    // ---- hardware formulas ----
    const double mf = masterFreq(0x80);
    CHECK(std::fabs(mf - 3968.0 * MOSC / 2 / 4096) < 1e-6, "master freq");
    for (double want : { 2500.0, 8000.0, 16000.0, 22050.0, 30000.0, 56320.0, 120000.0 }) {
        PitchRegs r = pitchRegsFor(want, mf);
        const double err = 1200 * std::log2(r.clock / want);
        CHECK(std::fabs(err) < 1.0, "pitch regs for %.0f Hz: oct %d pitch %d -> %.2f Hz (%.2f cents)", want, r.oct, r.pitch, r.clock, err);
        CHECK(std::fabs(channelClock(r.oct, r.pitch, mf) - r.clock) < 1e-9, "channel clock");
    }
    CHECK(std::fabs(filterCutoff(256) - 6410) < 1e-6, "filter at fval 256");
    CHECK(std::fabs(filterCutoff(256 + 32) / filterCutoff(256) - 1.98) < 0.02, "filter tracks one octave per 32 steps");
    const double su = envSweepClocks(true), sd = envSweepClocks(false);
    std::printf("envelope sweep: %.0f ECLKs up, %.0f down\n", su, sd);
    CHECK(su > 255 && sd > 255, "envelope sweeps");

    // ---- every sound ----
    Engine eng; eng.bank = &lib.bank; eng.prepare(fs); eng.setOutput(1, 0, 0.3);
    std::vector<float> L, R;
    for (int i = 0; i < lib.size(); ++i) {
        const Sound* s = lib.sound(i);
        CHECK(s && !s->ram.empty() && s->ram.size() <= (size_t) RAM_SIZE && s->ram.size() % PAGE == 0, "sound %d memory", i);
        if (!s) continue;
        eng.allOff(true);
        { std::vector<float> a(4096), b(4096); for (int k = 0; k < 20; ++k) eng.render(a.data(), b.data(), 4096); }
        eng.setKeyboard(i, s->ctl);
        const int len = fs * 2;
        L.assign((size_t) len, 0); R.assign((size_t) len, 0);
        const int notes[3] = { s->root, s->root + 7, s->root + 12 };
        int pos = 0; const int seg = len / 3;
        for (int k = 0; k < 3; ++k) {
            eng.keyOn(notes[k], 0.9f);
            const int hold = (int) (seg * 0.7);
            eng.render(L.data() + pos, R.data() + pos, hold); pos += hold;
            eng.keyOff(notes[k]);
            eng.render(L.data() + pos, R.data() + pos, seg - hold); pos += seg - hold;
        }
        double pk = 0, rms = 0; bool bad = false;
        for (int k = 0; k < pos; ++k) { if (!std::isfinite(L[(size_t) k])) bad = true; pk = std::max(pk, (double) std::fabs(L[(size_t) k])); rms += L[(size_t) k] * L[(size_t) k]; }
        rms = std::sqrt(rms / pos);
        CHECK(!bad, "%s: NaN", s->name.c_str());
        CHECK(pk > 0.02, "%s: silent (peak %.4f)", s->name.c_str(), pk);
        CHECK(pk < 0.999, "%s: clips (peak %.3f)", s->name.c_str(), pk);
        std::printf("%-9s %-10s %5.0f Hz root %3d pages %3d loop %3d-%3d  peak %.3f rms %.3f\n", s->name.c_str(), s->category.c_str(), s->rate, s->root,
                    s->pages(), s->loopStart, s->loopEnd, pk, rms);
        L.resize((size_t) pos); R.resize((size_t) pos);
        writeWav(dir + "/" + s->name + ".wav", L, R, fs);
    }

    // ---- pitch: a waveform-mode sawtooth on A4 must be 440 Hz (within the card's pitch steps) ----
    {
        const int saw = lib.find("SAWTOOTH");
        Controls c = lib.sound(saw)->ctl; c.vibDepth = 0; c.filter = 255;
        eng.allOff(true); { std::vector<float> a(48000), b(48000); eng.render(a.data(), b.data(), 48000); }
        eng.setKeyboard(saw, c); eng.setOutput(1, 0, 0);
        for (int note : { 45, 57, 69, 81 }) {
            std::vector<float> a((size_t) fs), b((size_t) fs);
            eng.keyOn(note, 1); eng.render(a.data(), b.data(), fs); eng.keyOff(note);
            { std::vector<float> x(48000), y(48000); eng.render(x.data(), y.data(), 48000); }
            const double f = freqOf(a, fs, fs / 4, fs), want = 440 * std::pow(2.0, (note - 69) / 12.0);
            const double c2 = 1200 * std::log2(f / want);
            std::printf("note %d: %.2f Hz (%.1f cents)\n", note, f, c2);
            CHECK(std::fabs(c2) < 4, "pitch of note %d: %.2f Hz, want %.2f", note, f, want);
        }
    }

    // ---- envelope: attack 0 reaches full level fast, damp time roughly as set ----
    // ---- Page R: the demo song, 8 bars at 120 BPM, internal clock ----
    {
        Engine e2; e2.bank = &lib.bank; e2.prepare(fs); e2.setOutput(1, 0, 0.3);
        Library::factorySeq(e2.seq, lib.bank);
        e2.extSync = false; e2.songMode = true; e2.tempo = 120; e2.seqRun = true;
        const int len = fs * 17;
        L.assign((size_t) len, 0); R.assign((size_t) len, 0);
        for (int p = 0; p < len; p += 512) e2.render(L.data() + p, R.data() + p, std::min(512, len - p));
        double pk = 0; for (float v : L) pk = std::max(pk, (double) std::fabs(v));
        std::printf("Page R song: peak %.3f\n", pk);
        CHECK(pk > 0.1 && pk < 0.999, "Page R song level %.3f", pk);
        writeWav(dir + "/PAGE_R_SONG.wav", L, R, fs);
        // host sync: ppq drives the steps
        Engine e3; e3.bank = &lib.bank; e3.prepare(fs); Library::factorySeq(e3.seq, lib.bank);
        e3.extSync = true; e3.curPattern = 0;
        Transport tp; tp.valid = true; tp.playing = true; tp.bpm = 120; tp.ppq = 0;
        int steps = 0, last = -1;
        std::vector<float> a(256), b(256);
        for (int p = 0; p < fs * 2; p += 256) { e3.render(a.data(), b.data(), 256, &tp); tp.ppq += 256.0 / fs * 2.0; if (e3.seqStep.load() != last) { last = e3.seqStep.load(); ++steps; } }
        std::printf("host sync: %d steps in 2 s at 120 BPM\n", steps);
        CHECK(steps >= 15 && steps <= 17, "host sync step count %d", steps);
    }

    // ---- loop blend: a loop whose seam jumps (phase and level) is smoothed by the crossfade, and is left as the
    //      card played it at 0. Loops with room before them and loops from the very start (the loop shrinks). ----
    for (int from : { 40, 0 }) {
        Sound s; s.name = "SEAM"; s.mode = Mode::Sample; s.rate = 128 * 440.0; s.root = 69; s.loopStart = from; s.loopEnd = 99;
        s.ram.resize(100 * PAGE);
        for (int i = 0; i < 100 * PAGE; ++i) s.ram[(size_t) i] = (uint8_t) std::lround(128 + 100 * std::sin(2 * PI * i / 37.3) * (1.0 - 0.5 * i / (100.0 * PAGE)));
        Controls c; c.loop = 1; c.sustain = 1; c.attack = 0; c.filter = 255; c.vibDepth = 0; c.damp = 10; s.ctl = c;
        double worst[3] {};
        const float blends[3] = { 0, 0.5f, 1 };
        for (int k = 0; k < 3; ++k) {
            Kit kl; kl.setUser(0, s);
            Engine el; el.bank = &kl.bank; el.prepare(fs); el.setOutput(1, 0, 0); el.loopBlend = blends[k];
            el.setKeyboard(kl.userSlot(0), c); el.keyOn(69, 1);
            const int len = fs * 3; std::vector<float> a((size_t) len), b2((size_t) len);
            for (int p = 0; p < len; p += 256) el.render(a.data() + p, b2.data() + p, std::min(256, len - p));
            // the worst corner in the sustain (second difference), against the sine's own
            double w = 0; for (int i = fs / 2; i < len; ++i) w = std::max(w, (double) std::fabs(a[(size_t) i] - 2 * a[(size_t) i - 1] + a[(size_t) i - 2]));
            worst[k] = w;
        }
        std::printf("loop from page %d: worst corner, blend 0 %.3f, 0.5 %.3f, 1 %.3f\n", from, worst[0], worst[1], worst[2]);
        CHECK(worst[1] < worst[0] * 0.8 && worst[2] < worst[0] * 0.8, "loop blend smooths the seam (loop from page %d)", from);
    }

    // ---- the effects section: the REV7 on the send rings on after the note; off (or no send): silence ----
    {
        double tails[3] {};
        for (int k = 0; k < 3; ++k) {
            TestKit kf; Engine ef; ef.bank = &kf.bank; ef.prepare(fs); ef.setOutput(1, 0, 0);
            ef.fxOn = k != 1; for (auto& sd : ef.fxSend) sd = k == 2 ? 0.f : 0.5f;
            ef.fx.preset(0);   // 1 LARGE HALL
            const int sn = kf.find("ORCHHIT"); ef.setKeyboard(sn, kf.sound(sn)->ctl);
            std::vector<float> a(fs * 4), b2(fs * 4);
            ef.keyOn(60, 1); ef.render(a.data(), b2.data(), fs / 4); ef.keyOff(60);
            ef.render(a.data() + fs / 4, b2.data() + fs / 4, fs * 4 - fs / 4);
            for (int i = fs * 2; i < fs * 3; ++i) tails[k] = std::max(tails[k], (double) std::fabs(a[(size_t) i]));
        }
        std::printf("REV7 send: tail at 2-3 s %.4f (fx off %.4f, no send %.4f); %d presets\n", tails[0], tails[1], tails[2], (int) Rev7Fx::spec().presets.size());
        CHECK(tails[0] > 0.002 && tails[1] < 0.0005 && tails[2] < 0.0005, "REV7 on the aux send");
        CHECK(Rev7Fx::spec().presets.size() == 244, "244 REV7 presets (%zu)", Rev7Fx::spec().presets.size());
        // the dice: 200 rolls with audio running, every one a valid preset or program, the output stays sane
        TestKit kd; Engine ed; ed.bank = &kd.bank; ed.prepare(fs); ed.setOutput(1, 0, 0);
        for (auto& sd : ed.fxSend) sd = 0.5f;
        const int sn = kd.find("ORCHHIT"); ed.setKeyboard(sn, kd.sound(sn)->ctl);
        std::vector<float> a(2048), b2(2048); double worst = 0; bool named = true; int stuck = 0, last = -2;
        for (int k = 0; k < 200; ++k) {
            last = ed.fx.curPreset.load();
            const std::string what = k % 2 ? ed.fx.rollSettings() : ed.fx.rollPreset();
            named &= !what.empty();
            if (k % 2 == 0) stuck += ed.fx.curPreset.load() == last;   // never the preset already on
            if (k % 10 == 0) ed.keyOn(60, 1);
            ed.render(a.data(), b2.data(), 2048);
            if (k % 10 == 5) ed.keyOff(60);
            for (float x : a) { if (!std::isfinite(x)) worst = 99; worst = std::max(worst, (double) std::fabs(x)); }
        }
        std::printf("REV7 dice: 200 rolls, peak %.3f\n", worst);
        CHECK(named && stuck == 0 && worst < 1.5, "dice rolls (peak %.3f, repeats %d)", worst, stuck);
    }

    // ---- the grid's edits ----
    {
        GridEdit ge; ge.setNotes({ { 0, 0, 1, 60, 100 }, { 0, 1, 1, 62, 100 }, { 0, 2, 0.5, 64, 100 }, { 1, 0, 4, 40, 90 } });
        ge.selectRect(0, 0.5, 2.2, 61, 70, false);
        CHECK(ge.selected() == 2 && ge.sel[1] && ge.sel[2], "box select: %d", ge.selected());
        ge.transpose(0, 12); ge.move(0, 0.25);
        CHECK(ge.notes[1].pitch == 74 && ge.notes[2].start == 2.25 && ge.notes[0].pitch == 60, "move and transpose the selection");
        ge.duplicate(0.25);
        CHECK(ge.notes.size() == 6 && ge.selected() == 2 && std::fabs(ge.notes[4].start - 2.75) < 1e-9, "duplicate after itself (%.2f)", ge.notes[4].start);
        ge.copy(); ge.paste(1, 8);
        CHECK(ge.notes.size() == 8 && ge.notes[6].track == 1 && std::fabs(ge.notes[6].start - 8) < 1e-9, "paste on another track");
        ge.clear(); ge.legato(0);
        CHECK(std::fabs(ge.notes[0].len - 1.25) < 1e-9, "legato (%.2f)", ge.notes[0].len);
        ge.selectAll(1); ge.erase();
        CHECK(ge.notes.size() == 5 && ge.at(0, 0.5, 60, 0.1) == 0, "erase the selection");
    }

    // ---- the drum kit: a piece's sound, level, mute, tune; and its text ----
    {
        TestKit kk; SongList sk;
        MidiSong ds = emptySong("KIT", 120, 2);
        for (int t = 0; t < SONG_TRK; ++t) ds.used[t] = false;
        ds.used[4] = true; ds.program[4] = -1;
        std::vector<SongNote> dn; for (int b = 0; b < 8; ++b) dn.push_back({ 4, (double) b, 0.5, b % 2 ? 38 : 36, 110 });
        setNotes(ds, dn); ds.raw = writeSmf(ds);
        const int di = sk.add(ds, kk.bank); SongEntry* de = sk.at(di); de->voice[4] = GM_KIT;
        auto level = [&](const DrumKit& k) {
            de->kit = k; Engine e; e.bank = &kk.bank; e.kitVoice = kitLookup(kk.bank); e.prepare(fs); e.setOutput(1, 0, 0); e.fxOn = false; e.extSync = false;
            selectSong(e, de); e.songRun = true;
            std::vector<float> a(fs * 4), b2(fs * 4); e.render(a.data(), b2.data(), fs * 4);
            double s2 = 0; for (float x : a) s2 += x * x; return std::sqrt(s2 / a.size());
        };
        DrumKit k0; const double base = level(k0);
        DrumKit km; km.p[36].mute = true; km.p[38].mute = true; const double muted = level(km);
        DrumKit kl; kl.p[36].level = 0.25f; kl.p[38].level = 0.25f; const double quiet = level(kl);
        DrumKit kv; kv.p[36].voice = "ORCHHIT"; kv.p[38].voice = "ORCHHIT"; const double other = level(kv);
        std::printf("kit: rms %.4f, muted %.5f, level 1/4 %.4f, other voice %.4f\n", base, muted, quiet, other);
        CHECK(base > 0.005 && muted < base * 0.02 && quiet < base * 0.6 && std::fabs(other - base) > base * 0.05, "drum kit pieces");
        DrumKit kt; kt.p[42].voice = "HIHAT1@DRUMS"; kt.p[42].tune = -2; kt.p[36].solo = true; kt.p[38].pan = 0.25f;
        DrumKit kr; kr.load(kt.save());
        CHECK(kr.save() == kt.save() && kr.p[42].voice == "HIHAT1@DRUMS" && kr.p[42].tune == -2 && kr.p[36].solo, "kit text: %s", kt.save().c_str());
    }

    // ---- room for the library: 1023 voices, NO_VOICE kept at 255; library voices kept by name and put back ----
    {
        CHECK(Library::USER_SLOTS == 1023 && Library::slotOf(254) == 254 && Library::slotOf(255) == 256 && Library::userOf(256) == 255 && Library::userOf(NO_VOICE) == -1,
              "voice numbers: NO_VOICE in its old place");
        Kit big; int put = 0; for (int k = 0; k < 700; ++k) { Sound x; x.name = "V" + std::to_string(k); x.category = "DRUMS"; x.ram.assign(PAGE, 0x90); x.fromLibrary = k % 2 == 0; big.setUser(k, x); ++put; }
        CHECK(big.loadedCount() == 700 && big.libraryCount() == 350 && big.ownCount() == 350 && big.firstFree() == 700 && big.bank.at(NO_VOICE)->name == "--", "700 voices loaded (%d free after)", Library::USER_SLOTS - big.loadedCount());
        // a library voice's place kept, filled when the library is read
        auto fakeVc = [](int pages, int start) { std::vector<uint8_t> v(0x1580 + (size_t) pages * PAGE, 0x80); v[0] = 4; for (size_t i = 0x1580 + (size_t) start; i < v.size(); ++i) v[i] = (uint8_t) (128 + (int) std::lround(100 * std::sin(i * 0.2) * std::exp(-(double) (i - 0x1580 - start) / 3000.0))); return v; };
        Catalog cat;
        auto addVoice = [&](const char* nm, const char* dk, int start = 0) { CatVoice c; c.name = nm; c.disk = dk; c.vc = fakeVc(start ? 120 : 40, start); c.hash = cat.voices.size() + 1; cat.voices.push_back(c); };
        addVoice("KICK1", "DRUMS1"); addVoice("SNARE1", "DRUMS1"); addVoice("LMSNARE", "DRUMS"); addVoice("HIHAT2", "CYMBALS"); addVoice("HHOPEN1", "CYMBALS");
        addVoice("TTOM", "DRUMS"); addVoice("CYMSPLS1", "CYMBALS"); addVoice("CROSSTIX", "DRUMS"); addVoice("CLAP", "PERCUSN1"); addVoice("BDRUM", "DRUMS", 13000);
        addVoice("STRNGENS", "STRINGS"); addVoice("COWBELL1", "PERCUSN1"); addVoice("CYMSTICK", "CYMBALS");
        { const auto bytes = cat.save(); Catalog back; const bool ok2 = back.load(bytes.data(), bytes.size());
          CHECK(ok2 && back.voices.size() == cat.voices.size() && back.voices[3].vc == cat.voices[3].vc && back.voices[3].name == "HIHAT2" && back.voices[3].hash == cat.voices[3].hash && back.gen != cat.gen,
                "the catalogue saved and read back (%zu bytes)", bytes.size()); }
        Kit kp; kp.keepPlace(300, "STRNGENS", "STRINGS");
        CHECK(kp.user(300)->waiting && !isBlank(kp, kp.userSlot(300)) && findVoiceAt(kp.bank, "STRNGENS@STRINGS") == kp.userSlot(300), "a library voice's place kept");
        const int missing = fillLibraryPlaces(kp, cat);
        CHECK(missing == 0 && !kp.user(300)->waiting && kp.user(300)->fromLibrary && kp.user(300)->ram.size() == 40 * PAGE, "the library voice back in its place");
        const int lv = loadCatalogVoice(kp, cat, 0);
        CHECK(lv >= 0 && kp.bank.at(lv)->fromLibrary && kp.libraryCount() == 2 && kp.ownCount() == 0, "library voices marked as such");
        SongList none; SeqData sq; for (auto& t : sq.trk) t.sound = NO_VOICE;
        const int gone = unloadUnusedLibrary(kp, voicesInUse(kp, lv, sq, none, nullptr));
        CHECK(gone == 1 && kp.userLoaded(300) == false && kp.bank.at(lv)->name == "KICK1", "unused library voices unloaded, the keyboard's kept (%d)", gone);

        // ---- the drum pads ----
        struct NameCase { const char* n; Part p; };
        const NameCase names[] = { { "Kick 808 (hard).wav", Part::Kick }, { "BD01.wav", Part::Kick }, { "SD_02.wav", Part::Snare }, { "snare-tight.aif", Part::Snare },
            { "HiHat Closed.wav", Part::ClosedHat }, { "CH.wav", Part::ClosedHat }, { "OH 1.wav", Part::OpenHat }, { "hihat open.wav", Part::OpenHat }, { "Pedal Hat.wav", Part::PedalHat },
            { "Clap.wav", Part::Clap }, { "RIMSHOT.wav", Part::Rim }, { "Tom Lo.wav", Part::Tom }, { "LT.wav", Part::Tom }, { "Crash 16.wav", Part::Crash }, { "Ride Bell.wav", Part::RideBell },
            { "ride.wav", Part::Ride }, { "Cowbell.wav", Part::Cowbell }, { "Conga Hi.wav", Part::Conga }, { "shaker.wav", Part::Shaker }, { "Tamb.wav", Part::Tamb },
            { "KICK1", Part::Kick }, { "LMSNARE", Part::Snare }, { "HHOPNCL1", Part::OpenHat }, { "HIHAT2", Part::ClosedHat }, { "CROSSTIX", Part::Rim }, { "CYMSPLS1", Part::Crash },
            { "CYMBELL1", Part::RideBell }, { "TTOM", Part::Tom }, { "SYNDRUM1", Part::Tom }, { "CCONGHI", Part::Conga }, { "BONGLO", Part::Bongo }, { "GOGO", Part::Agogo },
            { "STRNGENS", Part::None }, { "ORCH5", Part::None }, { "BASSGT", Part::None }, { "custom pad.wav", Part::None } };
        int wrong = 0; for (const auto& nc : names) if (partOfName(nc.n).part != nc.p) { ++wrong; std::printf("  %s -> %s (want %s)\n", nc.n, partName(partOfName(nc.n).part), partName(nc.p)); }
        CHECK(wrong == 0, "drum names: %d wrong", wrong);
        CHECK(partOfName("Tom Lo.wav").pitch < 0 && partOfName("Tom Hi.wav").pitch > 0, "tom pitch from the name");
        // GM lines up: the kick on 36, snare 38, hats 42/46, toms across 41-50 low to high
        std::array<PartChoice, PARTS> ch;
        ch[(size_t) Part::Kick].voices = { "K@X" }; ch[(size_t) Part::Snare].voices = { "S@X" }; ch[(size_t) Part::ClosedHat].voices = { "CH@X" }; ch[(size_t) Part::OpenHat].voices = { "OH@X" };
        ch[(size_t) Part::Tom].voices = { "TL@X", "TH@X" }; ch[(size_t) Part::Crash].voices = { "CR@X" };
        const DrumKit gk = kitFromParts(ch);
        CHECK(gk.p[36].voice == "K@X" && gk.p[35].voice == "K@X" && gk.p[38].voice == "S@X" && gk.p[40].voice == "S@X" && gk.p[42].voice == "CH@X" && gk.p[44].voice == "CH@X" && gk.p[46].voice == "OH@X" &&
              gk.p[41].voice == "TL@X" && gk.p[45].voice == "TL@X" && gk.p[48].voice == "TH@X" && gk.p[50].voice == "TH@X" && gk.p[41].tune < gk.p[45].tune && gk.p[48].tune < gk.p[50].tune &&
              gk.p[49].voice == "CR@X" && gk.p[57].voice == "CR@X" && gk.p[55].voice == "CR@X", "a kit in General MIDI order");
        int padsOk = 1; std::vector<int> seen; for (int b = 0; b < PAD_BANKS; ++b) for (int i = 0; i < 16; ++i) { const int n = padNote(b, i); if (std::find(seen.begin(), seen.end(), n) != seen.end() || n < 0 || n >= KIT_SLOTS || (b < 3 && (n < 35 || n > 82))) padsOk = 0; seen.push_back(n); }
        CHECK(padsOk && seen.size() == 320 && padNote(0, 0) == 36 && padNote(0, 1) == 38 && padNote(0, 2) == 42 && padNote(0, 3) == 46, "320 pads on 20 banks, each its own place, the GM drums on A-C");
        // presets: 100, named, each with a kick, snare and hat
        const auto& kps = kitPresets(); int full = 0; std::vector<std::string> pn;
        for (const auto& kp2 : kps) { full += !kp2.kit.p[36].voice.empty() && !kp2.kit.p[38].voice.empty() && !kp2.kit.p[42].voice.empty() && !kp2.kit.p[46].voice.empty(); pn.push_back(kp2.name); }
        std::sort(pn.begin(), pn.end());
        CHECK(kps.size() == 100 && full == 100 && std::unique(pn.begin(), pn.end()) == pn.end(), "100 kit presets (%zu, %d complete)", kps.size(), full);
        // a preset fitted to a library that lacks some of its voices: the same kind from what's there
        DrumKit fk = kps[10].kit; const int repl = fitKitToLibrary(fk, cat);
        CHECK(repl > 0 && cat.find(fk.p[36].voice.substr(0, fk.p[36].voice.find('@'))) >= 0 && partOfName(fk.p[38].voice).part == Part::Snare && partOfName(fk.p[36].voice).part == Part::Kick, "a preset on a small library: %d replaced, kick %s, snare %s", repl, fk.p[36].voice.c_str(), fk.p[38].voice.c_str());
        { Sound sv; cat.sound(cat.find("STRNGENS"), sv); CHECK(sv.octaveFix == -24, "a voice with a 31-sample cycle: songs play it two octaves down (%d)", sv.octaveFix); }
        { Sound bd; cat.sound(cat.find("BDRUM"), bd); CHECK(bd.startPage == 13000 / PAGE, "silence ahead of a voice skipped: starts at page %d", bd.startPage); }
        {   // the dice fills banks A-C and gives the snare a round robin; the engine cycles it
            const DrumKit rk2 = randomKit(cat, 11); int empty = 0; for (int b = 0; b < 3; ++b) for (int i = 0; i < 16; ++i) empty += rk2.p[(size_t) padNote(b, i)].voice.empty();
            Kit kr; DrumKit rk3 = rk2; rk3.rrOn = true; loadKitVoices(rk3, kr, cat); Engine er; er.bank = &kr.bank; er.kitVoice = kitLookup(kr.bank); applyPads(er, rk3);
            std::set<const Sound*> heard; for (int h = 0; h < 6; ++h) heard.insert(er.nextKitSound(38));
            CHECK(empty == 0 && er.kitRRn[38].load() >= 1 && heard.size() >= 2, "dice: banks A-C full (%d empty), snare round robin %d voices, %zu heard", empty, er.kitRRn[38].load() + 1, heard.size());
        }
        // the dice: a kit from this library only
        const DrumKit rk = randomKit(cat, 7); int fromLib = 1;
        for (const auto& p : rk.p) if (!p.voice.empty() && cat.find(p.voice.substr(0, p.voice.find('@'))) < 0) fromLib = 0;
        CHECK(fromLib && !rk.p[36].voice.empty() && !rk.p[38].voice.empty() && rk.p[1].voice.empty(), "random kit from the library: %s", kitSummary(rk).c_str());

        {   // a new install: empty pads become preset 1 from the library, and every pad sounds, a quick tap too
            Kit kn; DrumKit np; const bool set = padsReady(np, kn, cat);
            Engine en; en.bank = &kn.bank; en.kitVoice = kitLookup(kn.bank); en.prepare(fs); en.setOutput(1, 0, 0); en.fxOn = false; en.extSync = false; applyPads(en, np);
            int quiet = 0;
            for (int n : { 36, 38, 42, 46, 49, 37, 39, 41 }) {
                en.padPush(n, 110); std::vector<float> a(fs / 50), b2(fs / 50); en.render(a.data(), b2.data(), fs / 50); en.padPush(n, 0);   // a 20 ms tap
                std::vector<float> c(fs / 4), d(fs / 4); en.render(c.data(), d.data(), fs / 4);
                float m = 0; for (float x : c) m = std::max(m, std::fabs(x)); quiet += m < 0.01f;
                en.allOff(true); en.render(c.data(), d.data(), fs / 4);
            }
            CHECK(set && quiet == 0 && !np.p[36].voice.empty(), "new pads: preset 1, every pad sounds after a quick tap (%d silent)", quiet);
        }
        {   // ADSR on the kit: a held, looped drum decays to its sustain level; the text keeps it
            Kit ka; Sound lp; lp.name = "LOOPY"; lp.category = "User"; lp.mode = Mode::Sample; lp.rate = 30200; lp.root = 60; lp.ram.resize(100 * PAGE);
            for (size_t i = 0; i < lp.ram.size(); ++i) lp.ram[i] = (uint8_t) (128 + (int) std::lround(100 * std::sin(i * 0.2)));
            lp.loopStart = 50; lp.loopEnd = 99; lp.ctl.attack = 0; lp.ctl.sustain = 1; lp.ctl.loop = 1; ka.setUser(0, lp);
            auto tail = [&](const DrumKit& dk) {
                Engine ea; ea.bank = &ka.bank; ea.prepare(fs); ea.setOutput(1, 0, 0); ea.fxOn = false; ea.extSync = false; applyPads(ea, dk);
                ea.padPush(38, 120); std::vector<float> x(fs), y(fs); ea.render(x.data(), y.data(), fs);
                double e0 = 0, e1 = 0; for (int i = fs / 20; i < fs / 10; ++i) e0 += x[(size_t) i] * x[(size_t) i]; for (int i = fs * 8 / 10; i < fs; ++i) e1 += x[(size_t) i] * x[(size_t) i];
                (void) e0; return std::sqrt(e1 / (fs / 5.0));
            };
            DrumKit k1; k1.p[38].voice = "LOOPY@User"; const double flat = tail(k1);
            DrumKit k2 = k1; k2.env[1] = 4; k2.env[2] = 0.25f; const double kitEnv = tail(k2);
            DrumKit k3 = k2; k3.p[38].s = 1.f; const double padOwn = tail(k3);
            DrumKit k4; k4.load(k2.save());
            CHECK(flat > 0.01 && kitEnv < flat * 0.5 && padOwn > flat * 0.9 && k4.env[1] == 4 && std::fabs(k4.env[2] - 0.25f) < 1e-6, "ADSR: held level %.2f, kit sustain 25%% %.2f, the pad's own 100%% %.2f", flat, kitEnv, padOwn);
        }
        {   // the sample editor: trim, reverse, fades, a loop on the selection; whole pages; undo; on the pad while editing
            Kit ke; DrumKit pk2; padsReady(pk2, ke, cat); Engine ee; ee.bank = &ke.bank; ee.kitVoice = kitLookup(ke.bank); applyPads(ee, pk2);
            PadEditor pe; const bool opened = pe.begin(ke, ee, 38);
            const int before = pe.ed.len(); pe.ed.select(PAGE * 2, PAGE * 10 + 5); pe.ed.trim(); const int afterTrim = pe.ed.len();
            pe.ed.selectAll(); pe.ed.fadeOut(); const int lastAbs = std::abs(pe.ed.at(pe.ed.len() - 1)); pe.ed.reverse(); pe.ed.select(PAGE * 2, PAGE * 4); pe.ed.loopSelection();
            pe.commit(ke, ee);
            const Sound* onPad = ke.bank.at(ee.kitSound[38].load());
            const bool undone = pe.ed.undoLast() && pe.ed.s.loopStart < 0;
            {   // chopping: four hits found, on bank D's first four pads, each its own voice
                PadEditor pc; Sound hits; hits.name = "BEAT"; hits.category = "User"; hits.mode = Mode::Sample; hits.rate = 30200; hits.root = 60; hits.ram.assign(100 * PAGE, 0x80);
                for (int h = 0; h < 4; ++h) for (int i = 0; i < 2000; ++i) hits.ram[(size_t) (h * 3000 + 200 + i)] = (uint8_t) (128 + (int) std::lround(110 * std::sin(i * 0.3) * std::exp(-i / 400.0)));
                const int hs = ke.firstFree(); ke.setUser(hs, hits); DrumKit hk = ee.padKit; hk.p[60].voice = "BEAT@User"; applyPads(ee, hk);
                int made = 0; const bool op = pc.begin(ke, ee, 60); const int bank = pc.chop(ke, ee, 0, &made);
                const Sound* s0 = ke.bank.at(findVoiceAt(ke.bank, ee.padKit.p[(size_t) padNote(3, 0)].voice));
                int eq = 0; pc.chop(ke, ee, 8, &eq);
                CHECK(op && bank == 3 && made == 4 && s0 && s0->ram.size() <= 3000 + PAGE && eq == 8, "chop: %d hits onto bank %c, 8 equal slices onto the next (%d)", made, padBankLetter(bank < 0 ? 0 : bank), eq);
                pc.close();
            }
            pe.revert(ke, ee);
            CHECK(opened && before > afterTrim && afterTrim == 9 * PAGE && lastAbs <= 1 && onPad && onPad->category == "User" && onPad->loopStart == 2 && onPad->loopEnd == 3 && undone &&
                  ee.padKit.p[38].voice == pk2.p[38].voice, "sample editor: %d -> %d bytes, loop on the pad %d-%d, reverted", before, afterTrim, onPad ? onPad->loopStart : -9, onPad ? onPad->loopEnd : -9);
        }
        // the pads play the kit with no song; MIDI channel 10 too; a song's kit goes on top
        Kit kk; DrumKit pk = randomKit(cat, 3); loadKitVoices(pk, kk, cat);
        Engine e; e.bank = &kk.bank; e.kitVoice = kitLookup(kk.bank); e.prepare(fs); e.setOutput(1, 0, 0); e.fxOn = false; e.extSync = false;
        applyPads(e, pk);
        auto hit = [&](std::function<void()> f) { f(); std::vector<float> a(fs / 4), b2(fs / 4); e.render(a.data(), b2.data(), fs / 4); float m = 0; for (float x : a) m = std::max(m, std::fabs(x)); for (int i = 0; i < 4; ++i) e.render(a.data(), b2.data(), fs / 4); e.allOff(true); return m; };
        const float viaPad = hit([&] { e.padPush(38, 120); });
        const uint8_t on10[3] = { 0x99, 36, 120 }; const float viaMidi = hit([&] { e.midi(on10, 3); });
        DrumKit mk = pk; mk.p[38].mute = true; applyPads(e, mk); const float muted = hit([&] { e.padPush(38, 120); }); applyPads(e, pk);
        CHECK(viaPad > 0.02f && viaMidi > 0.02f && muted < 0.001f && e.kitSound[38].load() == findVoiceAt(kk.bank, pk.p[38].voice), "pads: %.3f, channel 10: %.3f, muted %.4f", viaPad, viaMidi, muted);
        DrumKit sk; sk.p[38].tune = 2; sk.p[42].voice = "CYMSTICK@CYMBALS"; loadKitVoices(sk, kk, cat); applyKit(e, sk);
        CHECK(e.kitTune[38].load() == pk.p[38].tune + 2 && e.kitSound[38].load() == findVoiceAt(kk.bank, pk.p[38].voice) && e.kitSound[42].load() == findVoiceAt(kk.bank, "CYMSTICK") && e.kitSound[36].load() == findVoiceAt(kk.bank, pk.p[36].voice),
              "a song's kit over the pads' kit");
        // your samples onto the pads, as CMI voices: 8-bit, at most 30.2 kHz, a long one at a lower rate
        std::vector<KitSample> smp;
        auto tone = [&](const char* file, double secs, double hz) { KitSample k2; k2.file = file; k2.rate = 44100; k2.mono.resize((size_t) (secs * 44100)); for (size_t i = 0; i < k2.mono.size(); ++i) k2.mono[i] = (float) (0.5 * std::sin(i * 2 * PI * hz / 44100) * std::exp(-(double) i / (secs * 44100 / 4))); smp.push_back(k2); };
        tone("Kick.wav", 0.4, 60); tone("Snare 1.wav", 0.3, 200); tone("CH.wav", 0.1, 3000); tone("Open Hat.wav", 0.8, 3000); tone("Crash.wav", 2.5, 2500); tone("Tom Lo.wav", 0.5, 90); tone("Tom Hi.wav", 0.5, 160); tone("weird noise.wav", 0.2, 500);
        Kit ks; DrumKit sp; const std::string msg = importKitSamples(smp, ks, sp, true);
        const Sound* kick = ks.bank.at(findVoiceAt(ks.bank, sp.p[36].voice)); const Sound* crash = ks.bank.at(findVoiceAt(ks.bank, sp.p[49].voice));
        int unnamed = 0; for (int i = 0; i < 16; ++i) unnamed += sp.p[(size_t) padNote(3, i)].voice.rfind("WEIRDNOI", 0) == 0;
        CHECK(kick && kick->name == "KICK" && kick->rate <= 30200.5 && kick->category == "User" && !kick->fromLibrary && crash && crash->rate < 8000.5 && crash->ram.size() <= (size_t) RAM_SIZE &&
              sp.p[38].voice.rfind("SNARE1", 0) == 0 && sp.p[42].voice.rfind("CH", 0) == 0 && sp.p[46].voice.rfind("OPENHAT", 0) == 0 && sp.p[41].voice.rfind("TOMLO", 0) == 0 && sp.p[50].voice.rfind("TOMHI", 0) == 0 && unnamed == 1,
              "samples onto the pads: %s (kick %.0f Hz, crash %.0f Hz)", msg.c_str(), kick ? kick->rate : 0, crash ? crash->rate : 0);
        std::printf("pads: %s; preset 11 on a small library: %s; random: %s\n", msg.c_str(), kitSummary(fk).c_str(), kitSummary(rk).c_str());
    }

    // ---- a CMI disk voice in the drum kit plays at its own speed: the Series II's 30.2 kHz ----
    {
        Sound dr; dr.name = "SNARE1"; dr.category = "DRUMS1"; dr.mode = Mode::Sample; dr.rate = 128 * 440.0; dr.root = 69;
        dr.ram.resize(60 * PAGE); for (size_t i = 0; i < dr.ram.size(); ++i) dr.ram[i] = (uint8_t) (128 + (int) std::lround(90 * std::sin(i * 0.3)));
        Controls dc; dc.attack = 0; dc.damp = 8; dc.sustain = 1; dc.loop = 1; dc.filter = 255; dr.ctl = dc;
        TestKit kd; kd.setUser(200, dr);
        SongList sd; MidiSong ds = emptySong("D", 120, 2); for (int t = 0; t < SONG_TRK; ++t) ds.used[t] = false; ds.used[4] = true; ds.program[4] = -1;
        setNotes(ds, { { 4, 0, 0.5, 38, 110 } }); ds.raw = writeSmf(ds);
        SongEntry* de = sd.at(sd.add(ds, kd.bank)); de->voice[4] = GM_KIT; de->kit.p[38].voice = "SNARE1@DRUMS1";
        Engine e; e.bank = &kd.bank; e.prepare(fs); e.setOutput(1, 0, 0); e.fxOn = false; e.extSync = false; selectSong(e, de); e.songRun = true;
        std::vector<float> a(fs), b2(fs); e.render(a.data(), b2.data(), fs);
        int first = -1, last = -1; for (int i = 0; i < fs; ++i) if (std::fabs(a[(size_t) i]) > 0.01f) { if (first < 0) first = i; last = i; }
        const double secs = (last - first) / (double) fs, want = 60.0 * PAGE / 30200.0;
        std::printf("kit voice from a CMI disk: %.1f ms long (at 30.2 kHz %.1f ms); natural key %.2f\n", secs * 1000, want * 1000, dr.naturalKey());
        CHECK(std::fabs(secs - want) < 0.006, "CMI drums at their own speed");
    }

    // ---- mono tracks: a new note cuts the one before; parts whose notes never overlap start mono ----
    {
        TestKit km; SongList sm;
        MidiSong ms2 = emptySong("MONO", 120, 2);
        std::vector<SongNote> nn = { { 0, 0, 2, 48, 100 }, { 0, 0.5, 2, 52, 100 } };   // two long notes overlapping on track 1
        for (int b = 0; b < 8; ++b) nn.push_back({ 2, b * 0.5, 0.4, 36 + b, 100 });      // a line on track 3
        setNotes(ms2, nn); ms2.raw = writeSmf(ms2);
        SongEntry* me = sm.at(sm.add(ms2, km.bank));
        const bool autoLine = me->mono[2], autoChords = me->mono[0];
        int voicesNow[2] {};
        for (int m = 0; m < 2; ++m) {
            me->voice[0] = km.find("STRINGS"); me->mono[0] = m == 1;
            Engine e; e.bank = &km.bank; e.prepare(fs); e.extSync = false; e.fxOn = false; selectSong(e, me); e.songRun = true;
            std::vector<float> a(fs), b2(fs); e.render(a.data(), b2.data(), fs / 2 + fs / 50);   // 0.52 beats... 0.27 s in: both notes asked
            e.render(a.data(), b2.data(), fs / 10);
            voicesNow[m] = 0; for (int t = 0; t < 1; ++t) {} voicesNow[m] = e.activeVoices();
        }
        std::printf("mono: poly %d cards sounding, mono %d; auto: the line %s, the chords %s\n", voicesNow[0], voicesNow[1], autoLine ? "mono" : "poly", autoChords ? "mono" : "poly");
        CHECK(voicesNow[1] < voicesNow[0] && autoLine && !autoChords, "mono tracks");
    }

    // ---- recording: keyboard notes onto a track, with a count-in and the click ----
    {
        TestKit kr; SongList sr; const int ri = sr.add(recordSong("REC", 120), kr.bank); SongEntry* re = sr.at(ri);
        Engine e; e.bank = &kr.bank; e.prepare(fs); e.setOutput(1, 0, 0); e.fxOn = false; e.extSync = false;
        selectSong(e, re);
        RecordSettings rs; rs.track = 2; rs.quant = 0.25;
        e.recTrack = rs.track; e.recFrom = 0; e.recOn = true; e.clickOn = true;
        e.songStartReq = -4.0; e.songRun = true;   // a bar's count-in
        std::vector<float> a(fs * 6), b2(fs * 6);
        auto run = [&](int from, int to) { e.render(a.data() + from, b2.data() + from, to - from); };
        const int beat = fs / 2;   // 120 BPM
        run(0, 4 * beat + beat / 20);                 // the count-in, then just past beat 0
        const uint8_t on1[3] = { 0x90, 60, 100 }, off1[3] = { 0x80, 60, 0 }, on2[3] = { 0x90, 64, 90 }, off2[3] = { 0x80, 64, 0 };
        e.midi(on1, 3); run(4 * beat + beat / 20, 5 * beat); e.midi(off1, 3);
        run(5 * beat, 6 * beat - beat / 30); e.midi(on2, 3); run(6 * beat - beat / 30, 7 * beat); e.midi(off2, 3);
        run(7 * beat, fs * 6);
        e.recOn = false;
        std::vector<Engine::RecEv> evs; Engine::RecEv ev; while (e.recPop(ev)) evs.push_back(ev);
        auto notes = notesOf(*re->song);
        const int added = mergeRecording(notes, evs, rs, 0, e.songBeat.load());
        editSong(e, re, notes);
        const auto got = notesOf(*re->song);
        int on2n = 0; double st1 = -1, st2 = -1; for (const auto& n : got) if (n.track == 2) { ++on2n; if (n.pitch == 60) st1 = n.start; if (n.pitch == 64) st2 = n.start; }
        // the click: blips in the count-in, before any note
        double clickPk = 0; for (int i = 0; i < beat / 10; ++i) clickPk = std::max(clickPk, (double) std::fabs(a[(size_t) i]));
        double gapPk = 0; for (int i = beat / 2; i < beat * 9 / 10; ++i) gapPk = std::max(gapPk, (double) std::fabs(a[(size_t) i]));
        std::printf("record: %d events, %d notes (60 at %.3f, 64 at %.3f), click %.3f vs between %.4f\n", (int) evs.size(), added, st1, st2, clickPk, gapPk);
        CHECK(added == 2 && on2n == 2 && std::fabs(st1 - 0) < 1e-9 && std::fabs(st2 - 2) < 1e-9, "recorded and quantised onto track 3");
        CHECK(clickPk > 0.05 && gapPk < clickPk * 0.1, "the click in the count-in");
    }

    // ---- the light pen: shapes, a voice, back from the voice, as text, as a WAV with its loop ----
    {
        PenDrawing pd; pd.shape(0, 0); pd.shape(1, 1); pd.smooth(1);
        const Sound v = pd.voice("PEN");
        PenDrawing back; back.fromSound(v);
        double err = 0; for (int i = 0; i < PAGE; ++i) err = std::max(err, (double) std::fabs(back.c[0][i] - pd.c[0][i]));
        PenDrawing t; t.load(pd.save());
        const auto w = voiceWav(v); WavData wd = parseWav(w.data(), w.size());
        CHECK(v.mode == Mode::Waveform && err < 0.03 && std::fabs(t.c[1][5] - pd.c[1][5]) < 1e-3, "pen voice and back (%.3f)", err);
        CHECK(wd.err.empty() && (int) wd.mono.size() == RAM_SIZE && wd.root == 60 && wd.loopStart == 96 * PAGE, "pen as a WAV: %s root %d loop %ld", wd.err.c_str(), wd.root, wd.loopStart);
    }

    // ---- a loaded sample goes through Page 8 ----
    {
        std::vector<float> s(44100 * 2); for (size_t i = 0; i < s.size(); ++i) s[i] = (float) std::sin(2 * PI * 261.63 * i / 44100.0) * 0.5f;
        Sound u = sampleToCmi(s, 44100, "test tone.wav", 60, true);
        CHECK(u.ram.size() <= (size_t) RAM_SIZE && u.rate >= 4000 && u.rate <= 32000, "sampled: %zu bytes at %.0f Hz", u.ram.size(), u.rate);
        Kit k0; k0.setUser(0, u);
        CHECK(k0.user(0)->name == "TESTTONE" && k0.userLoaded(0), "user slot name %s", k0.user(0)->name.c_str());
    }

    // ---- importing real CMI sounds: a 16-bit WAV at 25 kHz with a smpl chunk (root A3, a loop on pages 40-99) ----
    {
        const int sr = 25000, n = 128 * 110;
        std::vector<int16_t> pcm((size_t) n);
        for (int i = 0; i < n; ++i) pcm[(size_t) i] = (int16_t) std::lround(20000 * std::sin(2 * PI * 220.0 * i / sr) * std::exp(-i / 20000.0));
        std::vector<uint8_t> f;
        auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) f.push_back((uint8_t) (v >> (8 * k))); };
        auto u16 = [&](uint16_t v) { f.push_back((uint8_t) v); f.push_back((uint8_t) (v >> 8)); };
        auto tag = [&](const char* t) { f.insert(f.end(), t, t + 4); };
        tag("RIFF"); u32(0); tag("WAVE");
        tag("fmt "); u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
        tag("smpl"); u32(36 + 24); u32(0); u32(0); u32(1000000000u / sr); u32(57); u32(0); u32(0); u32(0); u32(1); u32(0);
        u32(0); u32(0); u32(40 * 128); u32(100 * 128 - 1); u32(0); u32(0);
        tag("data"); u32((uint32_t) n * 2); for (int16_t v : pcm) u16((uint16_t) v);
        WavData w = parseWav(f.data(), f.size());
        CHECK(w.err.empty() && w.rate == sr && (int) w.mono.size() == n, "wav parse: %s rate %.0f frames %zu", w.err.c_str(), w.rate, w.mono.size());
        CHECK(w.root == 57 && w.loopStart == 40 * 128 && w.loopEnd == 100 * 128 - 1, "smpl chunk: root %d loop %ld-%ld", w.root, w.loopStart, w.loopEnd);
        Sound u = sampleToCmi(w.mono, w.rate, "ORCH5", w.root, false, w.loopStart, w.loopEnd);
        CHECK(u.rate == sr, "kept at its own rate (%.0f)", u.rate);
        CHECK(u.loopStart == 40 && u.loopEnd == 99, "loop kept on pages %d-%d", u.loopStart, u.loopEnd);
        CHECK(u.root == 57 && u.name == "ORCH5", "root %d name %s", u.root, u.name.c_str());
        // sample for sample (to 8 bits): the loop seam is not crossfaded
        int worst = 0;
        double pk = 0; for (int16_t v : pcm) pk = std::max(pk, std::fabs(v / 32768.0));
        for (int i = 0; i < n; i += 7) worst = std::max(worst, std::abs((int) u.ram[(size_t) i] - (int) std::lround(pcm[(size_t) i] / 32768.0 / pk * 126.0) - 128));
        CHECK(worst <= 1, "kept sample for sample (worst %d LSB)", worst);
        // a long 44.1 kHz file is resampled to fit
        std::vector<float> longf(44100 * 3, 0.1f);
        Sound l = sampleToCmi(longf, 44100, "long", 60, false);
        CHECK(l.ram.size() <= (size_t) RAM_SIZE && l.rate < 6000, "long file resampled to %.0f Hz", l.rate);
        // slots
        Kit kit;
        CHECK(kit.firstFree() == 0 && kit.loadedCount() == 0, "kit starts empty");
        for (int k = 0; k < 3; ++k) kit.setUser(kit.firstFree(), u);
        CHECK(kit.loadedCount() == 3 && kit.firstFree() == 3, "three imported");
        kit.clearUser(1);
        CHECK(kit.firstFree() == 1 && kit.loadedCount() == 2, "clear a slot");
        CHECK(Library::USER_SLOTS == MAX_SOUNDS - 1 && Library::get().size() == MAX_SOUNDS && Library::get().builtIn == 0, "no built-in voices, %d user voices", Library::USER_SLOTS);
    }

    // ---- CMI disks: a QDOS image built here (raw and as ImageDisk), with a sampled (mode 4) and a waveform (mode 1) voice ----
    {
        const size_t SECB = 128, NSEC = 77 * 2 * 26;
        std::vector<uint8_t> img(NSEC * SECB, 0xE5);
        std::memcpy(img.data(), "TESTDISK", 8);
        for (size_t i = 3 * SECB; i < 23 * SECB; ++i) img[i] = 0;   // empty directory
        auto makeVc = [](int mode, int ls, int le, bool loop, uint8_t seed) {
            std::vector<uint8_t> v(21888, 0);
            v[0] = (uint8_t) mode; std::memcpy(&v[0xA0], "TESTCO  ", 8);
            v[0x1332] = (uint8_t) ls; v[0x1333] = (uint8_t) le; v[0x133B] = loop ? 1 : 0;
            for (size_t i = 0x1580; i < v.size(); ++i) v[i] = (uint8_t) (128 + 100 * std::sin((i - 0x1580) * 2 * PI / 128.0 + seed));
            return v;
        };
        auto put = [&](int entry, const char* name, const char* type, const std::vector<uint8_t>& data, int firstCluster) {
            const size_t o = 3 * SECB + (size_t) entry * 16;
            std::memcpy(&img[o], name, 8); std::memcpy(&img[o + 8], type, 2);
            const int rib = firstCluster * 4; img[o + 10] = (uint8_t) (rib >> 8); img[o + 11] = (uint8_t) rib; img[o + 12] = 0x10;
            const size_t dataSecs = (data.size() + 127) / 128, secs = dataSecs + 1;
            // two segments, to exercise the retrieval block: half the clusters here, the rest 40 clusters on
            const size_t clusters = (secs + 3) / 4, c1 = clusters / 2, c2 = clusters - c1;
            const uint16_t w1 = (uint16_t) ((c1 - 1) << 10 | firstCluster), w2 = (uint16_t) ((c2 - 1) << 10 | (firstCluster + 40)), w3 = (uint16_t) (0x8000 | (dataSecs - 1));
            const uint16_t ws[3] = { w1, w2, w3 };
            for (int k = 0; k < 3; ++k) { img[(size_t) rib * SECB + 2 * k] = (uint8_t) (ws[k] >> 8); img[(size_t) rib * SECB + 2 * k + 1] = (uint8_t) ws[k]; }
            std::vector<size_t> list;
            for (size_t c = 0; c < c1 * 4; ++c) list.push_back((size_t) rib + c);
            for (size_t c = 0; c < c2 * 4; ++c) list.push_back((size_t) (firstCluster + 40) * 4 + c);
            for (size_t sct = 0; sct < dataSecs; ++sct) for (size_t b = 0; b < 128; ++b) { const size_t i = sct * 128 + b; img[list[sct + 1] * SECB + b] = i < data.size() ? data[i] : 0; }
        };
        const auto vc4 = makeVc(4, 17, 118, true, 1), vc1 = makeVc(1, 0, 0, false, 2);
        put(0, "SARTEST ", "VC", vc4, 6);
        put(1, "WAVTEST ", "VC", vc1, 100);
        Disk d = readDiskImage(img.data(), img.size());
        CHECK(d.err.empty() && d.label == "TESTDISK" && d.files.size() == 2, "raw disk: %s, label %s, %zu files", d.err.c_str(), d.label.c_str(), d.files.size());
        if (d.files.size() == 2) {
            CHECK(d.files[0].name == "SARTEST" && d.files[0].type == "VC" && d.files[0].data.size() >= vc4.size() &&
                  std::equal(vc4.begin(), vc4.end(), d.files[0].data.begin()), "voice read through its retrieval block");
            Sound s4, s1; std::string err;
            CHECK(vcToSound(d.files[0].data, d.files[0].name, d.label, s4, err), "vc mode 4: %s", err.c_str());
            CHECK(s4.mode == Mode::Sample && s4.pages() == 128 && s4.loopStart == 17 && s4.loopEnd == 118 && s4.rate == 128 * 440.0 && s4.root == 69,
                  "mode 4: pages %d loop %d-%d", s4.pages(), s4.loopStart, s4.loopEnd);
            CHECK(std::equal(s4.ram.begin(), s4.ram.end(), vc4.begin() + 0x1580), "channel card RAM is the file's 16 KB, untouched");
            CHECK(vcToSound(d.files[1].data, d.files[1].name, d.label, s1, err) && s1.mode == Mode::Waveform && s1.pages() == 32, "mode 1: %d segments", s1.pages());
            // the CMI's pitch rule: a 128-sample cycle sounds at the key's frequency
            Kit kd; kd.setUser(0, s1);
            Engine ed; ed.bank = &kd.bank; ed.prepare(fs); ed.setOutput(1, 0, 0);
            Controls c = s1.ctl; ed.setKeyboard(kd.userSlot(0), c);
            std::vector<float> a((size_t) fs), b2((size_t) fs);
            ed.keyOn(57, 1); ed.render(a.data(), b2.data(), fs);
            const double f = freqOf(a, fs, fs / 4, fs);
            CHECK(std::fabs(1200 * std::log2(f / 220.0)) < 4, "mode 1 voice on A3 plays %.2f Hz", f);
        }
        // the same disk as an ImageDisk file: tracks out of order, interleaved sectors, compressed sectors, a head map
        std::vector<uint8_t> imd;
        const char* hdr = "IMD 1.18: 01/01/2000 0:00:00\r\ntest\x1a";
        imd.insert(imd.end(), hdr, hdr + std::strlen(hdr));
        for (int t = 2 * 77 - 1; t >= 0; --t) {
            const int cyl = t / 2, head = t % 2;
            imd.push_back(0); imd.push_back((uint8_t) cyl); imd.push_back((uint8_t) (head | (head ? 0x40 : 0))); imd.push_back(26); imd.push_back(0);
            int order[26]; for (int k = 0; k < 26; ++k) order[k] = (k * 7) % 26 + 1;   // interleave
            for (int k = 0; k < 26; ++k) imd.push_back((uint8_t) order[k]);
            if (head) for (int k = 0; k < 26; ++k) imd.push_back(0);   // physical head 1 recorded as 0
            for (int k = 0; k < 26; ++k) {
                const size_t off = ((size_t) t * 26 + (size_t) (order[k] - 1)) * 128;
                bool same = true; for (int b = 1; b < 128; ++b) same &= img[off + (size_t) b] == img[off];
                if (same) { imd.push_back(2); imd.push_back(img[off]); }
                else { imd.push_back(1); imd.insert(imd.end(), img.begin() + (long) off, img.begin() + (long) off + 128); }
            }
        }
        Disk d2 = readDiskImage(imd.data(), imd.size());
        CHECK(d2.err.empty() && d2.files.size() == 2 && d2.files[0].data == d.files[0].data && d2.files[1].data == d.files[1].data, "IMD reads the same: %s", d2.err.c_str());
        // as an HFE (the HxC emulator's raw track bits): FM at twice the cell rate, LSB first, sides interleaved in 256-byte
        // blocks, a skewed sector order, head 0 written in the IDs of both sides (as CMI disks do), a sector across the index
        {
            auto crc = [](const std::vector<uint8_t>& v) { return disk::crcCcitt(v.data(), v.size()); };
            std::vector<uint8_t> hfe(512, 0xFF); std::memcpy(hfe.data(), "HXCPICFE", 8);
            hfe[8] = 0; hfe[9] = 77; hfe[10] = 2; hfe[11] = 2; hfe[12] = 0xF4; hfe[13] = 1; hfe[14] = 0; hfe[15] = 0; hfe[16] = 7; hfe[17] = 1; hfe[18] = 1; hfe[19] = 0;
            hfe.resize(1024, 0xFF);
            std::vector<std::vector<uint8_t>> sideBits[1];
            size_t trackBlock = 2;
            for (int cyl = 0; cyl < 77; ++cyl) {
                std::vector<uint8_t> bits[2];
                for (int head = 0; head < 2; ++head) {
                    auto& bv = bits[head];
                    auto cellByte = [&](uint8_t data, uint8_t clock) { for (int i = 7; i >= 0; --i) { for (int r = 0; r < 2; ++r) bv.push_back(r ? (clock >> i) & 1 : 0); for (int r = 0; r < 2; ++r) bv.push_back(r ? (data >> i) & 1 : 0); } };
                    auto put = [&](uint8_t v) { cellByte(v, 0xFF); };
                    for (int k = 0; k < 40; ++k) put(0xFF);
                    for (int k = 0; k < 26; ++k) {
                        const int sn = (k * 7 + cyl * 3 + head) % 26;
                        for (int g = 0; g < 6; ++g) put(0x00);
                        std::vector<uint8_t> id = { 0xFE, (uint8_t) cyl, 0, (uint8_t) (sn + 1), 0 };
                        cellByte(0xFE, 0xC7); for (int i = 1; i < 5; ++i) put(id[(size_t) i]); const uint16_t c1 = crc(id); put((uint8_t) (c1 >> 8)); put((uint8_t) c1);
                        for (int g = 0; g < 11; ++g) put(0xFF); for (int g = 0; g < 6; ++g) put(0x00);
                        std::vector<uint8_t> dt = { 0xFB }; const size_t off = ((size_t) (cyl * 2 + head) * 26 + (size_t) sn) * 128;
                        dt.insert(dt.end(), img.begin() + (long) off, img.begin() + (long) off + 128);
                        cellByte(0xFB, 0xC7); for (size_t i = 1; i < dt.size(); ++i) put(dt[i]); const uint16_t c2 = crc(dt); put((uint8_t) (c2 >> 8)); put((uint8_t) c2);
                        for (int g = 0; g < 27; ++g) put(0xFF);
                    }
                    std::rotate(bv.begin(), bv.begin() + (long) (bv.size() - 4000), bv.end());   // the index falls inside a sector
                }
                const size_t nb = bits[0].size() / 8, blocks = (nb + 255) / 256;   // the sides' bytes; the track length exact
                hfe[512 + (size_t) cyl * 4] = (uint8_t) trackBlock; hfe[513 + (size_t) cyl * 4] = (uint8_t) (trackBlock >> 8);
                const size_t len = nb * 2; hfe[514 + (size_t) cyl * 4] = (uint8_t) len; hfe[515 + (size_t) cyl * 4] = (uint8_t) (len >> 8);
                hfe.resize(trackBlock * 512 + blocks * 512, 0xAA);
                for (int head = 0; head < 2; ++head)
                    for (size_t i = 0; i < bits[head].size(); ++i) {
                        const size_t byte = i / 8, blk = byte / 256, at = trackBlock * 512 + blk * 512 + (size_t) head * 256 + byte % 256;
                        if (i % 8 == 0) hfe[at] = 0;
                        hfe[at] = (uint8_t) (hfe[at] | (bits[head][i] << (i % 8)));
                    }
                trackBlock += blocks;
            }
            std::vector<uint8_t> back; std::string herr; int hbad = -1, hfound = 0;
            const bool hok = disk::unpackHfe(hfe.data(), hfe.size(), back, herr, &hbad, &hfound);
            Disk d3 = readDiskImage(hfe.data(), hfe.size());
            CHECK(hok && hfound == 4004 && hbad == 0 && back == img, "HFE decodes to the same sectors (%d found, %d bad) %s", hfound, hbad, herr.c_str());
            CHECK(d3.err.empty() && d3.files.size() == 2 && d3.files[1].data == d.files[1].data, "HFE reads the same voices: %s", d3.err.c_str());
            // an HFE at the cell rate (one bit a cell, not two)
            {
                std::vector<uint8_t> one = hfe;
                for (int cyl = 0; cyl < 77; ++cyl) {
                    const size_t off = (size_t) (one[512 + cyl * 4] | one[513 + cyl * 4] << 8) * 512, len = (size_t) (one[514 + cyl * 4] | one[515 + cyl * 4] << 8);
                    for (int side = 0; side < 2; ++side) {
                        std::vector<uint8_t> bits; const size_t sl = len / 2;
                        for (size_t j = 0; j < sl; ++j) { const uint8_t by = hfe[off + (j / 256) * 512 + (size_t) side * 256 + j % 256]; for (int k = 1; k < 8; k += 2) bits.push_back((by >> k) & 1); }
                        std::vector<uint8_t> by((bits.size() + 7) / 8, 0); for (size_t i = 0; i < bits.size(); ++i) by[i / 8] = (uint8_t) (by[i / 8] | bits[i] << (i % 8));
                        for (size_t j = 0; j < sl; ++j) one[off + (j / 256) * 512 + (size_t) side * 256 + j % 256] = j < by.size() ? by[j] : 0xFF;
                        if (side == 1) { const size_t nl = by.size() * 2; one[514 + cyl * 4] = (uint8_t) nl; one[515 + cyl * 4] = (uint8_t) (nl >> 8); }   // the track is half as long
                    }
                }
                std::vector<uint8_t> ob; std::string oe; int obad = -1, ofound = 0;
                const bool ook = disk::unpackHfe(one.data(), one.size(), ob, oe, &obad, &ofound);
                CHECK(ook && ofound == 4004 && obad == 0 && ob == img, "an HFE at the cell rate: %d found, %d bad", ofound, obad);
            }
            // a damaged HFE is set aside, and the disk's IMG copy fills in
            {
                std::vector<uint8_t> dmg = hfe;
                for (size_t i = 0; i < 400; ++i) dmg[512 * 40 + 600 + i] ^= 0x55;   // a few sectors of track ~1
                Catalog cat; cat.add("TESTDISK.hfe", dmg.data(), dmg.size()); cat.add("TESTDISK.img", img.data(), img.size());
                Catalog c2; c2.add("TESTDISK.hfe", hfe.data(), hfe.size()); c2.add("TESTDISK.img", img.data(), img.size());
                CHECK(cat.hfeSetAside == 1 && cat.hfeDisks == 0 && cat.voices.size() == 2, "damaged HFE set aside, IMG used (%zu voices)", cat.voices.size());
                CHECK(c2.hfeDisks == 1 && c2.disks == 1 && c2.voices.size() == 2, "a good HFE: its IMG copy skipped (%d disks, %zu voices)", c2.disks, c2.voices.size());
            }
        }
        Disk bad = readDiskImage((const uint8_t*) "hello", 5);
        CHECK(!bad.err.empty(), "junk is refused");
    }

    // ---- MIDI songs: a two-channel file made here, played on its own clock and with the host ----
    {
        std::vector<uint8_t> f;
        auto u32 = [&](uint32_t v) { for (int k = 3; k >= 0; --k) f.push_back((uint8_t) (v >> (8 * k))); };
        auto u16 = [&](uint16_t v) { f.push_back((uint8_t) (v >> 8)); f.push_back((uint8_t) v); };
        auto tag = [&](const char* t) { f.insert(f.end(), t, t + 4); };
        tag("MThd"); u32(6); u16(1); u16(2); u16(96);
        std::vector<uint8_t> t0 = { 0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20, 0, 0xFF, 0x2F, 0 };   // 120 BPM
        tag("MTrk"); u32((uint32_t) t0.size()); f.insert(f.end(), t0.begin(), t0.end());
        // channel 1: strings (program 48), four quarter notes and a volume change; channel 3: choir (52) on beat 2, running status
        std::vector<uint8_t> t1 = { 0, 0xFF, 0x03, 4, 'V', 'L', 'N', 'S', 0, 0xC0, 48, 0, 0xC2, 52, 0, 0xB0, 7, 100,
                                    0, 0x90, 60, 100, 96, 0x80, 60, 0, 0, 0x90, 62, 100, 96, 0x80, 62, 0, 0, 0xB0, 7, 40,
                                    0, 0x90, 64, 100, 96, 64, 0, 0, 65, 100, 96, 65, 0,
                                    0, 0x92, 57, 90, 0x81, 0x40, 0x82, 57, 0, 0, 0xFF, 0x2F, 0 };
        tag("MTrk"); u32((uint32_t) t1.size()); f.insert(f.end(), t1.begin(), t1.end());
        MidiSong ms; std::string err;
        const bool ok = parseSmf(f.data(), f.size(), "TEST", ms, err);
        CHECK(ok, "parse: %s", err.c_str());
        if (ok) {
            CHECK(std::fabs(ms.bpmAt(0) - 120) < 0.01 && std::fabs(ms.length - 6) < 1e-6, "tempo %.2f length %.2f beats", ms.bpmAt(0), ms.length);
            CHECK(ms.used[0] && ms.program[0] == 48 && ms.trackName[0] == "VLNS" && ms.used[1] && ms.program[1] == 52 && !ms.used[2], "tracks: %d %d '%s'", ms.program[0], ms.program[1], ms.trackName[0].c_str());
            TestKit ks; SongList sl; const int si = sl.add(ms, ks.bank);
            CHECK(ks.bank.at(sl.at(si)->voice[0])->name == "STRINGS" && ks.bank.at(sl.at(si)->voice[1])->name == "CHOIRAAH", "GM voices");
            Engine es; es.bank = &ks.bank; es.prepare(fs); es.setOutput(1, 0, 0); es.extSync = false;
            selectSong(es, sl.at(si)); es.songRun = true;
            const int len = fs * 4;
            std::vector<float> a((size_t) len), b2((size_t) len);
            for (int p = 0; p < len; p += 256) es.render(a.data() + p, b2.data() + p, std::min(256, len - p));
            double pk1 = 0, pk2 = 0, late = 0;
            for (int i = 0; i < fs / 2; ++i) pk1 = std::max(pk1, (double) std::fabs(a[(size_t) i]));
            for (int i = fs; i < fs * 3 / 2; ++i) pk2 = std::max(pk2, (double) std::fabs(a[(size_t) i]));
            for (int i = fs * 7 / 2; i < len; ++i) late = std::max(late, (double) std::fabs(a[(size_t) i]));
            std::printf("song: beat-1 peak %.3f, after CC7 40 %.3f, after the end %.4f, running %d\n", pk1, pk2, late, (int) es.songRun.load());
            CHECK(pk1 > 0.05 && pk2 > 0.01 && pk2 < pk1, "song plays and follows CC7");
            CHECK(!es.songRun.load() && late < 0.05, "song stops at its end");
            // with the host: the beat comes from the playhead
            Engine eh; eh.bank = &ks.bank; eh.prepare(fs); eh.extSync = true; selectSong(eh, sl.at(si));
            Transport tp; tp.valid = true; tp.playing = true; tp.bpm = 120; tp.ppq = 1.0;   // start on beat 2
            std::vector<float> x(256), y(256); double pk = 0;
            for (int p = 0; p < fs / 4; p += 256) { eh.render(x.data(), y.data(), 256, &tp); tp.ppq += 256.0 / fs * 2.0; for (float v : x) pk = std::max(pk, (double) std::fabs(v)); }
            CHECK(pk > 0.01 && eh.songBeat.load() > 1.0, "host sync from beat 2 (beat %.2f)", eh.songBeat.load());
        }
        CHECK(!parseSmf((const uint8_t*) "junk", 4, "x", ms, err), "junk MIDI refused");
        // duplicates: other versions of the same title, the fuller one stays
        {
            CHECK(songKey("1984 - Frankie Goes To Hollywood - Relax (alt)") == songKey("Frankie Goes To Hollywood - Relax") &&
                  songKey("1984   FRANKIE GOES TO HOLLYWOOD   RELAX") == songKey("Frankie Goes To Hollywood - Relax") &&
                  songKey("Jan Hammer - Miami Vice Theme (alt 2)") == songKey("Jan Hammer - Miami Vice Theme") &&
                  songKey("Kate Bush - Running Up That Hill (Online Sequencer)") == songKey("Kate Bush - Running Up That Hill") &&
                  songKey("Duran Duran - Rio") != songKey("Duran Duran - A View to a Kill"), "song keys");
            if (ok) {
                Kit kd2; SongList sl2; int dup = -1;
                MidiSong a = ms; a.name = "Song A"; MidiSong b = ms; b.name = "SONG A (alt)"; MidiSong c = ms; c.name = "Song B";
                auto nb = notesOf(b); nb.push_back({ 0, 5, 1, 70, 100 }); setNotes(b, nb); b.raw = writeSmf(b);
                sl2.addUnique(a, kd2.bank, dup); const int d0 = dup;
                sl2.addUnique(a, kd2.bank, dup); const int d1 = dup;    // the same file again
                sl2.addUnique(b, kd2.bank, dup); const int d2 = dup;    // a fuller version
                sl2.addUnique(c, kd2.bank, dup); const int d3 = dup;
                CHECK(d0 == 0 && d1 == 1 && d2 == 2 && d3 == 0 && sl2.count() == 2, "dedupe on import: %d %d %d %d, %d songs", d0, d1, d2, d3, sl2.count());
                SongList sl3; sl3.add(a, kd2.bank); sl3.add(b, kd2.bank); sl3.add(c, kd2.bank); sl3.add(a, kd2.bank);
                const int gone = sl3.removeDuplicates();
                CHECK(gone == 2 && sl3.count() == 2 && noteCount(*sl3.at(sl3.nth(0))->song) == 6, "remove duplicates: %d gone", gone);
            }
        }
        // auto matching: by name, preferring a loaded CMI disk voice, then GM
        {
            MidiSong m2 = ms; m2.used[2] = m2.used[3] = m2.used[4] = true;
            m2.trackName[0] = "SynthVox"; m2.trackName[1] = "Timpani"; m2.trackName[2] = "Bassoon"; m2.trackName[3] = "Str Ensem"; m2.trackName[4] = "Shadowrun for SNES";
            m2.program[2] = 70; m2.program[3] = 48; m2.program[4] = 30;
            TestKit k2; int v[SONG_TRK]; std::string why[SONG_TRK];
            autoSongVoices(m2, k2.bank, v, why);
            auto nm = [&](int i) { return i == GM_KIT ? std::string("KIT") : k2.bank.at(i)->name; };
            CHECK(nm(v[0]) == "CHOIRAAH" && nm(v[1]) == "TIMPANI" && nm(v[2]) == "SAX" && nm(v[3]) == "STRINGS" && nm(v[4]) == "GTRLEAD",
                  "auto voices: %s %s %s %s %s", nm(v[0]).c_str(), nm(v[1]).c_str(), nm(v[2]).c_str(), nm(v[3]).c_str(), nm(v[4]).c_str());
            // the library matcher's roles: track names first, then the GM program; drum parts on any channel
            {
                auto role = [&](const char* name, int prog) { MidiSong r = m2; r.trackName[0] = name; r.program[0] = prog; r.used[1] = r.used[2] = r.used[3] = r.used[4] = false;
                                                              const int i = roleOf(r, 0); return i < 0 ? std::string("drums") : std::string(roles()[(size_t) i].name); };
                CHECK(role("Violin", 40) == "violin" && role("Ragtime Piano", 3) == "honky tonk" && role("FR.HORN", 60) == "french horn" && role("VIBRAPHONE", 11) == "vibes" &&
                      role("TRACK 4", 46) == "harp" && role("Rhodes", 4) == "electric piano" && role("", 33) == "bass" && role("", 123) == "birds",
                      "roles: %s %s %s", role("Violin", 40).c_str(), role("Ragtime Piano", 3).c_str(), role("TRACK 4", 46).c_str());
                CHECK(libraryRole("BASSES.HFE", true) == 1 && libraryRole("BASSES.img", true) == 2 && libraryRole("BASS1.VC", true) == 3 &&
                      libraryRole("BASSES.hfe", false) == 2 && libraryRole("x.wav", true) == 0, "library: HFE images first");
                CHECK(std::string(drumVoices(36)).rfind("KICK1", 0) == 0 && std::string(drumVoices(56)).find("COWBELL1") != std::string::npos, "CMI drum map");
            }
            Sound arr = *k2.bank.at(lib.find("CHOIRAAH")); arr.name = "ARR1"; arr.category = "CHORAL"; k2.setUser(0, arr);
            autoSongVoices(m2, k2.bank, v, why);
            CHECK(nm(v[0]) == "ARR1" && v[0] == k2.userSlot(0), "a loaded disk voice wins (%s, %s)", nm(v[0]).c_str(), why[0].c_str());
        }
        // editing: notes out, moved, back in; written as a MIDI file and read again; the player carries on
        if (ok) {
            TestKit ke; SongList se; const int ei = se.add(ms, ke.bank);
            auto notes = notesOf(*se.at(ei)->song);
            CHECK(notes.size() == 5 && std::fabs(notes[0].len - 1) < 1e-9 && notes[0].pitch == 60, "%zu notes", notes.size());
            Engine ee; ee.bank = &ke.bank; ee.prepare(fs); ee.extSync = false; selectSong(ee, se.at(ei)); ee.songRun = true;
            std::vector<float> x(4800), y(4800); ee.render(x.data(), y.data(), 4800);   // 0.1 s in
            notes[0].pitch = 72; notes.push_back({ 0, 4.5, 0.5, 67, 110 });
            editSong(ee, se.at(ei), notes);
            ee.render(x.data(), y.data(), 4800);
            CHECK(ee.songRun.load() && ee.songBeat.load() > 0.3, "plays on through an edit (beat %.2f)", ee.songBeat.load());
            const auto n2 = notesOf(*se.at(ei)->song);
            CHECK(n2.size() == 6 && n2[0].pitch == 72 && std::fabs(se.at(ei)->song->length - 6) < 1e-9, "edited: %zu notes", n2.size());
            MidiSong back; std::string e2;
            CHECK(parseSmf(se.at(ei)->song->raw.data(), se.at(ei)->song->raw.size(), "B", back, e2) && notesOf(back).size() == 6 && notesOf(back)[0].pitch == 72 &&
                  std::fabs(back.bpmAt(0) - 120) < 0.01, "MIDI file written and read back: %s", e2.c_str());
            CHECK(undoSong(ee, se.at(ei)) && notesOf(*se.at(ei)->song)[0].pitch == 60, "undo");
            // render the whole song, and one track, to WAV
            Rendered rr = renderSong(ke.bank, *se.at(ei), 44100);
            Rendered r1 = renderSong(ke.bank, *se.at(ei), 44100, 1);
            double pk = 0; for (float q : rr.L) pk = std::max(pk, (double) std::fabs(q));
            double pk1 = 0; for (size_t q = 0; q < r1.L.size() && q < (size_t) 44100 * 2; ++q) pk1 = std::max(pk1, (double) std::fabs(r1.L[q]));
            CHECK(rr.L.size() > 44100 * 3 && rr.L.size() < 44100 * 7 && pk > 0.05, "render: %.2f s, peak %.3f", rr.L.size() / 44100.0, pk);
            CHECK(pk1 < 0.01, "stem of track 2 is silent before its note (%.4f)", pk1);
            const auto wb = wavBytes(rr, 24);
            CHECK(wb.size() == 44 + rr.L.size() * 6 && !std::memcmp(wb.data(), "RIFF", 4), "24-bit WAV");
            writeWav(dir + "/SONG_RENDER.wav", rr.L, rr.R, 44100);
        }
    }

    // ---- the mixer: faders, mute, solo, meters ----
    {
        Engine em; em.bank = &lib.bank; em.prepare(fs); em.setOutput(1, 0, 0); em.setKeyboard(lib.find("STRINGS"), lib.sound(lib.find("STRINGS"))->ctl);
        auto level = [&]() { std::vector<float> a(4800), b(4800); for (int k = 0; k < 10; ++k) em.render(a.data(), b.data(), 4800); double p = 0; for (float v : a) p = std::max(p, (double) std::fabs(v)); return p; };
        em.keyOn(60, 1);
        const double full = level();
        em.fader[0] = 0.5f; const double half = level();
        em.mute[0] = true; const double muted = level();
        em.mute[0] = false; em.solo[3] = true; const double soloed = level();
        em.solo[3] = false; em.fader[0] = 1; level();
        std::printf("mixer: full %.3f, fader 0.5 %.3f, muted %.4f, another strip soloed %.4f, meter %.3f\n", full, half, muted, soloed, em.meter[0].load());
        CHECK(half < full * 0.6 && half > full * 0.4 && muted < 0.002 && soloed < 0.002 && em.meter[0].load() > 0.05, "mixer strips");
    }
    // ---- a drawn voice ----
    {
        float a[PAGE], b2[PAGE]; for (int i = 0; i < PAGE; ++i) { a[i] = (float) std::sin(2 * PI * i / PAGE); b2[i] = i < PAGE / 2 ? 1.f : -1.f; }
        Sound d = drawnVoice("DRAW1", a, b2);
        CHECK(d.mode == Mode::Waveform && d.ram.size() == (size_t) RAM_SIZE && std::abs((int) d.ram[32] - 254) < 3 && std::abs((int) d.ram[127 * PAGE + 32] - 254) < 3 &&
              std::abs((int) d.ram[127 * PAGE + 96] - 2) < 3, "drawn voice: sine morphing to square");
    }

    // ---- speed: 16 voices for one second ----
    {
        Engine e4; e4.bank = &lib.bank; e4.prepare(fs); e4.setKeyboard(lib.find("STRINGS"), lib.sound(lib.find("STRINGS"))->ctl);
        for (int k = 0; k < 8; ++k) e4.keyOn(48 + k * 3, 1);
        Library::factorySeq(e4.seq, lib.bank); e4.extSync = false; e4.seqRun = true; e4.songMode = true;
        std::vector<float> a(256), b(256);
        auto s0 = std::chrono::steady_clock::now();
        for (int p = 0; p < fs * 4; p += 256) e4.render(a.data(), b.data(), 256);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count();
        std::printf("4 s of audio (8 held + Page R) in %.1f ms (%.1f%% of one core)\n", ms, ms / 40.0);
    }

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
