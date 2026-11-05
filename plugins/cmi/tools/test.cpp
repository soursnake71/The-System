// Omega CMI engine tests, no framework: cmi_test <outdir>
// Checks the channel card maths against the hardware formulas, renders every library sound and the Page R demo
// song to WAV files, and fails on silence, clipping, NaNs or wrong pitch.
#include "../core/Disk.h"
#include "../core/Song.h"
#include "../core/TestVoices.h"
#include <cstdio>
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
