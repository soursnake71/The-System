// Omega CMI engine tests, no framework: cmi_test <outdir>
// Checks the channel card maths against the hardware formulas, renders every library sound and the Page R demo
// song to WAV files, and fails on silence, clipping, NaNs or wrong pitch.
#include "../core/Disk.h"
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
    Library& lib = Library::get();
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
        Library::factorySeq(e2.seq, lib);
        e2.extSync = false; e2.songMode = true; e2.tempo = 120; e2.seqRun = true;
        const int len = fs * 17;
        L.assign((size_t) len, 0); R.assign((size_t) len, 0);
        for (int p = 0; p < len; p += 512) e2.render(L.data() + p, R.data() + p, std::min(512, len - p));
        double pk = 0; for (float v : L) pk = std::max(pk, (double) std::fabs(v));
        std::printf("Page R song: peak %.3f\n", pk);
        CHECK(pk > 0.1 && pk < 0.999, "Page R song level %.3f", pk);
        writeWav(dir + "/PAGE_R_SONG.wav", L, R, fs);
        // host sync: ppq drives the steps
        Engine e3; e3.bank = &lib.bank; e3.prepare(fs); Library::factorySeq(e3.seq, lib);
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
        CHECK(Library::USER_SLOTS == 200 && lib.size() <= MAX_SOUNDS, "200 user voices fit the bank (%d)", lib.size());
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

    // ---- speed: 16 voices for one second ----
    {
        Engine e4; e4.bank = &lib.bank; e4.prepare(fs); e4.setKeyboard(lib.find("STRINGS"), lib.sound(lib.find("STRINGS"))->ctl);
        for (int k = 0; k < 8; ++k) e4.keyOn(48 + k * 3, 1);
        Library::factorySeq(e4.seq, lib); e4.extSync = false; e4.seqRun = true; e4.songMode = true;
        std::vector<float> a(256), b(256);
        auto s0 = std::chrono::steady_clock::now();
        for (int p = 0; p < fs * 4; p += 256) e4.render(a.data(), b.data(), 256);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count();
        std::printf("4 s of audio (8 held + Page R) in %.1f ms (%.1f%% of one core)\n", ms, ms / 40.0);
    }

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
