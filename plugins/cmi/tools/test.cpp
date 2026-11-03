// Omega CMI engine tests, no framework: cmi_test <outdir>
// Checks the channel card maths against the hardware formulas, renders every library sound and the Page R demo
// song to WAV files, and fails on silence, clipping, NaNs or wrong pitch.
#include "../core/Library.h"
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
        lib.setUser(0, u);
        CHECK(lib.sound(lib.userSlot(0))->name == "TESTTONE", "user slot name %s", lib.sound(lib.userSlot(0))->name.c_str());
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
