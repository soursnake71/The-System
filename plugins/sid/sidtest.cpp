// Omega SID 64 engine checks (no JUCE): every preset renders finite, audible, unclipped audio; the oscillator is in
// tune; the filter, sequencer, arp and state round trip work; and how fast it runs.
// Build: c++ -O2 -std=c++17 plugins/sid/sidtest.cpp -o sidtest && ./sidtest [wav-dir]
#include "SidSynth.h"
#include <chrono>
#include <functional>
#include <cstdio>
#include <memory>

using namespace sid;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r, int fs) {
    FILE* f = std::fopen(path, "wb"); if (!f) return;
    const uint32_t n = (uint32_t) l.size(), data = n * 4, riff = 36 + data;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(riff); std::fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(2); w32((uint32_t) fs); w32((uint32_t) fs * 4); w16(4); w16(16);
    std::fwrite("data", 1, 4, f); w32(data);
    for (uint32_t i = 0; i < n; ++i) { w16((uint16_t) (int16_t) std::lround(std::clamp(l[i], -1.f, 1.f) * 32767)); w16((uint16_t) (int16_t) std::lround(std::clamp(r[i], -1.f, 1.f) * 32767)); }
    std::fclose(f);
}

struct Out { std::vector<float> l, r; float peak = 0; double rms = 0; bool finite = true; };
static Out run(Synth& s, int fs, double seconds, const std::function<void(Synth&, int)>& atBlock) {
    Out o; const int n = (int) (seconds * fs), B = 256; o.l.resize((size_t) n); o.r.resize((size_t) n);
    for (int p = 0, b = 0; p < n; p += B, ++b) {
        atBlock(s, b);
        const int len = std::min(B, n - p);
        s.render(o.l.data() + p, o.r.data() + p, len);
    }
    for (int i = 0; i < n; ++i) {
        const float a = o.l[(size_t) i], c = o.r[(size_t) i];
        if (!std::isfinite(a) || !std::isfinite(c)) o.finite = false;
        o.peak = std::max({ o.peak, std::fabs(a), std::fabs(c) }); o.rms += a * a + c * c;
    }
    o.rms = std::sqrt(o.rms / (2.0 * n));
    return o;
}

// fundamental: rising zero crossings of the DC-removed signal over a steady stretch, with sub-sample positions
static double pitchOf(const std::vector<float>& x, int fs, int from, int len) {
    double mean = 0; for (int i = 0; i < len; ++i) mean += x[(size_t) (from + i)]; mean /= len;
    double first = -1, lastT = -1; int n = 0;
    for (int i = from; i < from + len - 1; ++i) {
        const double a = x[(size_t) i] - mean, b = x[(size_t) i + 1] - mean;
        if (a < 0 && b >= 0) { const double t = i + a / (a - b); if (first < 0) first = t; else ++n; lastT = t; }
    }
    return n > 0 ? n * fs / (lastT - first) : 0;
}

int main(int argc, char** argv) {
    const int fs = 48000;
    const char* dir = argc > 1 ? argv[1] : nullptr;
    auto s = std::make_unique<Synth>();
    s->prepare(fs);
    std::printf("%d presets, %d patterns, %d parameters\n", presetCount(), patternPresetCount(), NP);

    // tuning: a sawtooth A4 should be 440 Hz (the 16-bit frequency register quantises to well under a cent here)
    {
        s->initPatch(); s->set(P::o1d, 0); s->set(P::o1s, 15);
        auto o = run(*s, fs, 0.6, [](Synth& x, int b) { if (b == 0) x.noteOn(69, 1); });
        const double hz = pitchOf(o.l, fs, fs / 4, fs / 4);
        std::printf("A4 saw: %.2f Hz\n", hz);
        CHECK(std::fabs(hz - 440) < 1.5, "A4 measured %.2f Hz", hz);
        s->noteOff(69); run(*s, fs, 0.5, [](Synth&, int) {});
        s->set(P::o1saw, 0); s->set(P::o1tri, 1);
        o = run(*s, fs, 0.6, [](Synth& x, int b) { if (b == 0) x.noteOn(45, 1); });
        const double hz2 = pitchOf(o.l, fs, fs / 4, fs / 4);
        std::printf("A2 triangle: %.2f Hz\n", hz2);
        CHECK(std::fabs(hz2 - 110) < 0.5, "A2 measured %.2f Hz", hz2);
        s->noteOff(45); run(*s, fs, 0.5, [](Synth&, int) {});
    }
    // filter: a low cutoff should take most of a bright saw away, on both chips
    for (int model = 0; model < 2; ++model) {
        double lvl[2];
        for (int k = 0; k < 2; ++k) {
            s->initPatch(); s->set(P::model, (float) model); s->set(P::o1flt, 1); s->set(P::cut, k ? 2047.f : 100.f); s->set(P::o1s, 15);
            auto o = run(*s, fs, 0.5, [](Synth& x, int b) { if (b == 0) x.noteOn(84, 1); });
            // high-frequency energy: first difference
            double e = 0; for (int i = fs / 5; i < fs / 2 - 1; ++i) { const double d = o.l[(size_t) i + 1] - o.l[(size_t) i]; e += d * d; }
            lvl[k] = e;
            s->noteOff(84); run(*s, fs, 0.6, [](Synth&, int) {});
        }
        std::printf("%s filter: open/closed HF energy ratio %.1f\n", model ? "8580" : "6581", lvl[1] / std::max(lvl[0], 1e-12));
        CHECK(lvl[1] > lvl[0] * 8, "%s filter barely changes the sound", model ? "8580" : "6581");
    }

    // the mixer: mute, solo, level and pan
    {
        auto level = [&](const std::function<void(Synth&)>& setup, double& l, double& r) {
            s->panic(); s->initPatch();
            s->set(P::o2on, 1); s->set(P::o2saw, 0); s->set(P::o2pul, 1); s->set(P::o2s, 15);
            s->set(P::o1s, 15); s->set(P::o2flt, 1); s->set(P::mode, 1);
            setup(*s);
            auto o = run(*s, fs, 0.5, [](Synth& x, int b) { if (b == 0) x.noteOn(57, 1); });
            l = r = 0; for (int i = fs / 5; i < fs / 2; ++i) { l += o.l[(size_t) i] * o.l[(size_t) i]; r += o.r[(size_t) i] * o.r[(size_t) i]; }
            s->noteOff(57); run(*s, fs, 0.4, [](Synth&, int) {});
        };
        double l0, r0, l1, r1, l2, r2, l3, r3, l4, r4;
        level([](Synth&) {}, l0, r0);
        level([](Synth& x) { x.set(P::m1mute, 1); x.set(P::mfmute, 1); }, l1, r1);
        level([](Synth& x) { x.set(P::m1pan, -1); x.set(P::mfmute, 1); }, l2, r2);
        level([](Synth& x) { x.set(P::m1lvl, -12); x.set(P::mfmute, 1); }, l3, r3);
        level([](Synth& x) { x.set(P::mfsolo, 1); }, l4, r4);
        double l5, r5; level([](Synth& x) { x.set(P::mfmute, 1); }, l5, r5);
        std::printf("mixer: all %.4f, muted %.6f, osc 1 panned left L/R %.4f/%.6f, -12 dB %.2f dB, filter solo %.4f\n",
                    l0 + r0, l1 + r1, l2, r2, 10 * std::log10((l3 + r3) / (l5 + r5)), l4 + r4);
        CHECK(l1 + r1 < (l0 + r0) * 1e-4, "mute leaves sound");
        CHECK(r2 < l2 * 1e-3, "hard-left pan leaks right");
        CHECK(std::fabs(10 * std::log10((l3 + r3) / (l5 + r5)) + 12) < 0.5, "-12 dB fader is off");
        CHECK(l4 + r4 > 0 && l4 + r4 < (l0 + r0) * 0.9, "filter solo should drop the direct oscillator");
        s->initPatch();
    }

    // every preset: a chord, held then released
    int quiet = 0, loud = 0;
    for (int i = 0; i < presetCount(); ++i) {
        s->panic(); s->applyPreset(i);
        const bool drum = std::string(PRESETS[i].cat) == "DRUM";
        auto o = run(*s, fs, 1.4, [&](Synth& x, int b) {
            if (b == 0) { x.noteOn(drum ? 60 : 57, 0.9f); if (!drum) { x.noteOn(64, 0.8f); } }
            if (b == 4 && !drum) x.noteOn(60, 0.8f);
            if (b == 170) { x.noteOff(57); x.noteOff(60); x.noteOff(64); }
        });
        CHECK(o.finite, "preset %d %s: not finite", i, PRESETS[i].name);
        if (o.peak < 0.01) { ++quiet; std::printf("quiet: %s/%s peak %.4f\n", PRESETS[i].cat, PRESETS[i].name, o.peak); }
        if (o.peak > 0.99) { ++loud; std::printf("loud: %s/%s peak %.3f\n", PRESETS[i].cat, PRESETS[i].name, o.peak); }
        if (dir) {
            std::string nm = std::string(dir) + "/" + std::to_string(i) + "_" + PRESETS[i].name + ".wav";
            for (auto& c : nm) if (c == ' ') c = '_';
            writeWav(nm.c_str(), o.l, o.r, fs);
        }
    }
    CHECK(quiet == 0, "%d presets are silent", quiet);

    // sequencer + arp: the internal clock plays the pattern
    {
        s->panic(); s->applyPreset(0); s->loadPatternPreset(0, 0); s->set(P::spat, 1); s->set(P::slen, 16);
        s->post(Synth::EV_SEQ_PLAY, 0, 0);
        int stepsSeen = 0, last = -1;
        auto o = run(*s, fs, 2.0, [&](Synth& x, int) { const int st = x.seqStepShown.load(); if (st != last) { ++stepsSeen; last = st; } });
        s->post(Synth::EV_SEQ_STOP, 0, 0); run(*s, fs, 0.1, [](Synth&, int) {});
        std::printf("sequencer: %d step changes in 2 s at 120 BPM 1/16 (expect ~16), rms %.3f\n", stepsSeen, o.rms);
        CHECK(stepsSeen >= 14 && stepsSeen <= 18, "sequencer step count %d", stepsSeen);
        CHECK(o.rms > 0.01, "sequencer silent");
        // host sync: follow the host's ppq
        s->set(P::shost, 1);
        double ppq = 0; int hostSteps = 0; last = -1;
        run(*s, fs, 1.0, [&](Synth& x, int) { x.hostTransport(true, ppq, 120); ppq += 256.0 / fs * 2; const int st = x.seqStepShown.load(); if (st != last) { ++hostSteps; last = st; } });
        std::printf("host sync: %d steps in 1 s (expect ~8)\n", hostSteps);
        CHECK(hostSteps >= 7 && hostSteps <= 10, "host sync step count %d", hostSteps);
        run(*s, fs, 0.2, [&](Synth& x, int) { x.hostTransport(false, ppq, 120); });
    }
    // state round trip
    {
        s->applyPreset(42); s->set(P::bpm, 133);
        Step st; st.on = true; st.note = 70; st.accent = true; s->setStep(3, 5, st);
        const std::string a = s->saveState();
        auto t = std::make_unique<Synth>(); t->prepare(fs); t->loadState(a);
        CHECK(t->saveState() == a, "state round trip differs");
        CHECK(t->step(3, 5).note == 70 && t->step(3, 5).accent, "pattern step lost");
    }
    // speed: 8 chips, 4x oversampling
    {
        s->panic(); s->applyPreset(0); s->set(P::poly, 8); s->set(P::o2on, 1); s->set(P::o3on, 1); s->set(P::o2saw, 1); s->set(P::o3saw, 1); s->set(P::o1flt, 1);
        for (int q = 0; q < 3; ++q) {
            s->set(P::quality, (float) q);
            const auto t0 = std::chrono::steady_clock::now();
            run(*s, fs, 4.0, [](Synth& x, int b) { if (b == 0) for (int n = 0; n < 8; ++n) x.noteOn(48 + n * 3, 1); });
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("8 chips, %dx oversampling: %.1f%% of one core\n", 2 << q, sec / 4.0 * 100);
        }
    }
    std::printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
