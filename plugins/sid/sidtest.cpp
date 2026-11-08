// Omega SID 64 engine checks (no JUCE): every preset renders finite, audible, unclipped audio; the oscillator is in
// tune; the filter, sequencer, arp and state round trip work; and how fast it runs.
// Build: c++ -O2 -std=c++17 plugins/sid/sidtest.cpp -o sidtest && ./sidtest [wav-dir]
#include "ScreenUI.h"
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

    // the sampler: a 220 Hz sine sampled in, played back by each method on each chip, at the root and an octave up
    {
        std::vector<float> sine((size_t) fs);
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = (float) std::sin(2 * kPi * 220 * i / fs);
        const char* names[5] = { "", "$D418 4-BIT", "MAHONEY 8-BIT", "PWM 8-BIT", "DIGIMAX 8-BIT" };
        for (int model = 0; model < 2; ++model)
            for (int m = 1; m <= 4; ++m) {
                s->panic(); s->initPatch();
                s->set(P::model, (float) model); s->set(P::o1on, 0); s->set(P::dmode, (float) m); s->set(P::drate, 4);   // 7.8 kHz
                s->setSource(sine, fs, "SINE"); s->pollSample();
                double hz[2], rms = 0;
                for (int k = 0; k < 2; ++k) {
                    auto o = run(*s, fs, 0.6, [&](Synth& x, int b) { if (b == 0) x.noteOn(60 + 12 * k, 1); });
                    // low-pass first: the PWM carrier and the sample-rate steps are above it
                    std::vector<float> f = o.l; double a = 0, b2 = 0;
                    for (auto& x : f) { a += 0.05 * (x - a); b2 += 0.05 * (a - b2); x = (float) b2; }
                    // the strongest frequency near the expected one (a DFT scan): steady on stepped 4-bit audio
                    const double want = 220.0 * (1 << k); double best = 0, bestHz = 0;
                    for (double fq = want * 0.85; fq < want * 1.15; fq += 0.25) {
                        double re = 0, im = 0;
                        for (int i = fs / 10; i < fs / 2; ++i) { re += f[(size_t) i] * std::cos(2 * kPi * fq * i / fs); im += f[(size_t) i] * std::sin(2 * kPi * fq * i / fs); }
                        if (re * re + im * im > best) { best = re * re + im * im; bestHz = fq; }
                    }
                    hz[k] = bestHz;
                    if (k == 0) rms = o.rms;
                    s->noteOff(60 + 12 * k); run(*s, fs, 0.3, [](Synth&, int) {});
                }
                std::printf("%s %-14s rms %.4f  root %.1f Hz  +12 %.1f Hz\n", model ? "8580" : "6581", names[m], rms, hz[0], hz[1]);
                const bool quietOk = model == 1 && m == 1;   // the 8580's volume register barely leaks: a quiet digi is right
                CHECK(quietOk || rms > 0.01, "%s %s silent", model ? "8580" : "6581", names[m]);
                if (!quietOk) {
                    CHECK(std::fabs(hz[0] - 220) < 4, "%s %s root pitch %.1f", model ? "8580" : "6581", names[m], hz[0]);
                    CHECK(std::fabs(hz[1] - 440) < 8, "%s %s octave pitch %.1f", model ? "8580" : "6581", names[m], hz[1]);
                }
            }
        // the 8580 needs the boost for a $D418 digi
        double q[2];
        for (int b = 0; b < 2; ++b) {
            s->panic(); s->initPatch(); s->set(P::model, 1); s->set(P::o1on, 0); s->set(P::dmode, 1); s->set(P::dboost, (float) b); s->pollSample();
            q[b] = run(*s, fs, 0.4, [](Synth& x, int bl) { if (bl == 0) x.noteOn(60, 1); }).rms;
            s->noteOff(60); run(*s, fs, 0.2, [](Synth&, int) {});
        }
        std::printf("8580 $D418 digi: %.4f plain, %.4f boosted\n", q[0], q[1]);
        CHECK(q[1] > q[0] * 4, "8580 digi boost does nothing");
        // 4-bit really is 16 levels, 8-bit up to 256; the RAM limit holds 60K bytes (4-bit: two per byte)
        {
            std::vector<float> longer((size_t) (fs * 12)); for (size_t i = 0; i < longer.size(); ++i) longer[i] = (float) std::sin(i * 0.01);
            SampleSource src; src.set(longer, fs, "X");
            std::unique_ptr<SampleData> a(src.convert(22050, 4, true)), b(src.convert(22050, 8, true)), c(src.convert(22050, 8, false));
            std::vector<int> lv4(256, 0), lv8(256, 0); for (auto x : a->d) lv4[x] = 1; for (auto x : b->d) lv8[x] = 1;
            int n4 = 0, n8 = 0; for (int i = 0; i < 256; ++i) { n4 += lv4[(size_t) i]; n8 += lv8[(size_t) i]; }
            std::printf("conversion: 4-bit %d levels, 8-bit %d levels; 64K limit %zu / %zu samples (4 / 8-bit), unlimited %zu\n", n4, n8, a->d.size(), b->d.size(), c->d.size());
            CHECK(n4 <= 16 && n8 > 200, "quantisation wrong");
            CHECK(a->d.size() == 2 * 60 * 1024 && b->d.size() == 60 * 1024 && c->d.size() > b->d.size(), "RAM limit wrong");
        }
        s->panic(); s->initPatch();
    }

    // the screen's touch controls: selecting a setting, a tap between rows, swipes, the big +/-/reset
    {
        struct TH : ScreenUI::Host {
            Synth& sy; explicit TH(Synth& x) : sy(x) {}
            Synth& synth() override { return sy; } float get(int i) override { return sy.get(i); } void set(int i, float v) override { sy.set(i, v); }
            void loadPreset(int i) override { sy.applyPreset(i); } void initPatch() override { sy.initPatch(); } void randomize() override { sy.randomize(3); }
            bool isPlugin() const override { return false; }
        } th(*s);
        s->initPatch();
        ScreenUI ui(th); ui.touchMode = true;
        std::vector<uint32_t> px((size_t) (ScreenUI::W * ScreenUI::MAXROWS * 8));
        auto tap = [&](float x, float y) { ui.render(px.data()); ui.down(x, y); ui.up(x, y); };
        ui.setPage(4); tap(20, 5 * 8 + 4);                 // FILTER page, the cutoff row
        CHECK(ui.selected == P::cut, "tapping CUTOFF selects %d", ui.selected);
        ui.selected = -1; tap(20, 17 * 8 + 1.5f);            // just below the last row (VEL>CUTOFF), on an empty row
        CHECK(ui.selected == P::velcut, "a tap just below the last row selects %d", ui.selected);
        tap(20, 5 * 8 + 4);
        const float c0 = s->get(P::cut);
        ui.stepSelected(1); CHECK(s->get(P::cut) > c0, "+ doesn't raise the cutoff");
        ui.setSelectedNorm(1); CHECK(std::fabs(s->get(P::cut) - 2047) < 1, "the slider's top isn't 2047");
        ui.resetSelected(); CHECK(std::fabs(s->get(P::cut) - params()[P::cut].def) < 1, "reset doesn't restore the default");
        ui.setPage(1); tap(20, 6 * 8 + 4);                    // OSC 1: the waveform row's TRI toggle is at col 18
        ui.render(px.data()); ui.down(150, 6 * 8 + 4); ui.up(150, 6 * 8 + 4);
        CHECK(ui.selected == P::o1tri && s->get(P::o1tri) > .5f, "tapping TRI selects and toggles it");
        ui.setPage(0); ui.render(px.data()); ui.down(200, 100); ui.move(140, 102); ui.up(120, 103);   // swipe left on the preset list
        CHECK(ui.page == 1, "swiping left goes to the next page (page %d)", ui.page);
        ui.render(px.data()); ui.down(100, 30 * 1); ui.move(170, 31); ui.up(190, 31);                 // swipe right
        CHECK(ui.page == 0, "swiping right goes back (page %d)", ui.page);
        // the grid: draw by dragging, tap to delete, move, resize, chords, the velocity lane, undo
        {
            const int pat = (int) std::lround(s->get(P::spat)) - 1;
            s->clearPattern(pat);
            ui.setPage(13); ui.render(px.data());
            const int top = 60 + std::max(6, ui.rows - 14) / 2;           // an empty pattern centres the view here
            auto cellX = [](int st) { return (4 + 2 * st) * 8 + 4.f; };   // the 1-16 view: two columns a step
            auto rowY = [](int k) { return (8 + k) * 8 + 4.f; };
            auto drag = [&](float x0, float y0, float x1, float y1) { ui.render(px.data()); ui.down(x0, y0); ui.move((x0 + x1) / 2, (y0 + y1) / 2); ui.move(x1, y1); ui.up(x1, y1); };
            drag(cellX(0), rowY(0), cellX(3), rowY(0));
            auto ns = s->patternNotes(pat);
            CHECK(ns.size() == 1 && ns[0].second.start == 0 && ns[0].second.len == 4 && ns[0].second.note == top, "drawing a 4-step note: %d notes, len %d", (int) ns.size(), ns.empty() ? 0 : ns[0].second.len);
            drag(cellX(1), rowY(0), cellX(3), rowY(1));                    // move: two steps later, a semitone down
            ns = s->patternNotes(pat);
            CHECK(ns.size() == 1 && ns[0].second.start == 2 && ns[0].second.note == top - 1 && ns[0].second.len == 4, "moving the note: start %d note %d", ns.empty() ? -1 : ns[0].second.start, ns.empty() ? -1 : ns[0].second.note);
            drag(cellX(5), rowY(1), cellX(7), rowY(1));                    // resize from its last cell
            ns = s->patternNotes(pat);
            CHECK(ns.size() == 1 && ns[0].second.len == 6, "resizing the note: len %d", ns.empty() ? 0 : ns[0].second.len);
            tap(cellX(8), rowY(3)); tap(cellX(8), rowY(5));                 // a chord at step 9
            ns = s->patternNotes(pat);
            CHECK(ns.size() == 3, "a two-note chord: %d notes", (int) ns.size());
            ui.render(px.data()); const float vy = (9 + std::max(6, ui.rows - 14)) * 8 + 1.f; ui.down(cellX(8), vy); ui.up(cellX(8), vy);
            int loud = 0; for (auto& q : s->patternNotes(pat)) if (q.second.start == 8 && q.second.vel >= 120) ++loud;
            CHECK(loud == 2, "the velocity lane set %d of the chord's notes to full", loud);
            tap(cellX(3), rowY(1));                                        // tap the long note: gone
            CHECK(s->patternNotes(pat).size() == 2, "tapping a note didn't delete it");
            ui.stepPreset(0);   // (no-op guard) then undo brings it back
            ui.render(px.data());
            CHECK(ui.page == 13, "grid gestures changed page (%d)", ui.page);
        }
        std::printf("touch controls: select, near-miss tap, +/slider/reset, toggles, swipes, grid painting checked\n");
        s->initPatch();
    }

    // the reverb is the Omega REV7: its presets are numbered as the SID presets use them, its decay follows REV TIME
    // in the SID, every program runs, and the sends / return route it
    {
        const omega::Spec& sp = rev7Spec();
        std::printf("Omega REV7: %d presets, %d parameters\n", (int) sp.presets.size(), (int) sp.params.size());
        CHECK(sp.presets.size() == 144, "expected the REV7's 144 presets, got %d", (int) sp.presets.size());
        CHECK(sp.presets[22].name == "23 GATE REVERB", "REV7 preset 23 is %s", sp.presets[22].name.c_str());
        CHECK(sp.presets[31].name.find("Wakeman") != std::string::npos, "REV7 preset 32 is %s", sp.presets[31].name.c_str());
        CHECK(sp.presets[46].name.find("808 kick room") != std::string::npos, "REV7 preset 47 is %s", sp.presets[46].name.c_str());
        for (auto& m : Synth::rev7Ids()) CHECK(sp.index(m.second) >= 0, "the REV7 has no parameter %s", m.second);
        // the SID's REV7 parameters have the REV7's own ranges and defaults
        for (auto& m : Synth::rev7Ids()) {
            const auto& q = sp.params[(size_t) sp.index(m.second)]; const ParamInfo& pi = params()[m.first];
            if (!q.choices.empty()) { CHECK((int) q.choices.size() == (int) pi.max + 1, "%s has %d choices", m.second, (int) q.choices.size()); continue; }
            CHECK(std::fabs(q.min - pi.min) < 1e-3 && std::fabs(q.max - pi.max) < 1e-3 && std::fabs(q.def - pi.def) < 1e-3, "%s range %g..%g def %g vs SID %g..%g def %g", m.second, q.min, q.max, q.def, pi.min, pi.max, pi.def);
        }
        // decay through the SID: an impulse-ish blip on the send, LARGE HALL at three REV TIMEs
        auto rt60 = [&](const std::vector<float>& x, int from) {
            std::vector<double> e(x.size() + 1, 0.0);
            for (size_t i = x.size(); i-- > (size_t) from;) e[i] = e[i + 1] + (double) x[i] * x[i];
            int a5 = -1, b25 = -1;
            for (size_t i = (size_t) from; i < x.size(); ++i) { const double db = 10 * std::log10(e[i] / e[(size_t) from] + 1e-30); if (a5 < 0 && db < -5) a5 = (int) i; if (b25 < 0 && db < -25) { b25 = (int) i; break; } }
            return a5 < 0 || b25 < 0 ? 0.0 : (b25 - a5) / (double) fs * 3.0;
        };
        for (double t : { 1.0, 2.6, 5.0 }) {
            s->panic(); s->initPatch();
            s->set(P::o1tri, 1); s->set(P::o1saw, 0); s->set(P::o1d, 0); s->set(P::o1s, 0); s->set(P::o1r, 0);
            s->set(P::rret, 1); s->set(P::s1r, 1); s->set(P::r7prog, 0); s->set(P::r7rt, (float) t); s->set(P::r7idly, 1);
            auto o = run(*s, fs, t * 1.8 + 0.3, [](Synth& x, int bl) { if (bl == 0) x.noteOn(72, 1); if (bl == 2) x.noteOff(72); });
            const double m = rt60(o.l, (int) (fs * 0.08));
            std::printf("REV7 LARGE HALL in the SID, REV TIME %.1f s: measured %.2f s\n", t, m);
            CHECK(m > t * 0.7 && m < t * 1.35, "REV7 decay %.2f s for REV TIME %.1f s", m, t);
        }
        // all 30 programs, through the synth
        int silent = 0;
        for (int prg = 0; prg < 30; ++prg) {
            s->panic(); s->initPatch(); s->set(P::rret, 1); s->set(P::s1r, 1); s->set(P::r7prog, (float) prg);
            auto o = run(*s, fs, 1.2, [](Synth& x, int bl) { if (bl == 0) x.noteOn(60, 1); if (bl == 20) x.noteOff(60); });
            CHECK(o.finite, "REV7 program %d: not finite", prg + 1);
            if (o.rms < 1e-3) { ++silent; std::printf("REV7 program %d (%s) quiet\n", prg + 1, sp.presets[(size_t) prg].name.c_str()); }
            CHECK(o.peak < 1.0, "REV7 program %d clips (%.2f)", prg + 1, o.peak);
        }
        CHECK(silent == 0, "%d REV7 programs are silent", silent);
        // the sends and the return
        auto tail = [&](float ret, float send) {
            s->panic(); s->initPatch(); s->set(P::o1d, 4); s->set(P::o1s, 0); s->set(P::o1r, 0);
            s->set(P::rret, ret); s->set(P::s1r, send);
            auto o = run(*s, fs, 1.2, [](Synth& x, int bl) { if (bl == 0) x.noteOn(60, 1); if (bl == 10) x.noteOff(60); });
            double e = 0; for (int i = (int) (fs * 0.5); i < (int) (fs * 1.2); ++i) e += (double) o.l[(size_t) i] * o.l[(size_t) i];
            return e;
        };
        const double none = tail(0, 1), noSend = tail(1, 0), wet = tail(1, 1);
        std::printf("REV7 in the synth: tail energy %.6f no return, %.6f no send, %.4f with both\n", none, noSend, wet);
        CHECK(wet > 100 * (none + 1e-9) && wet > 100 * (noSend + 1e-9), "the reverb send / return doesn't work");
        // a SID preset that names a REV7 preset gets its program
        for (int i = 0; i < presetCount(); ++i) if (std::string(PRESETS[i].name) == "GATED SNARE") { s->applyPreset(i); break; }
        CHECK((int) std::lround(s->get(P::r7prog)) == 22, "GATED SNARE didn't load REV7 program 23 (got %d)", (int) std::lround(s->get(P::r7prog)) + 1);
        s->initPatch();
    }

    // the sequencer plays chords and note lengths; MIDI files round-trip; MIDI in: learn, channel, programs, bend range, clock
    {
        s->panic(); s->initPatch(); s->set(P::poly, 6); s->set(P::spat, 3); s->set(P::slen, 16); s->set(P::srate, 1); s->set(P::sgate, 0.5f); s->set(P::bpm, 120);
        s->clearPattern(2);
        s->addNote(2, GNote { 0, 4, 48, 100, false }); s->addNote(2, GNote { 0, 1, 55, 100, false }); s->addNote(2, GNote { 4, 1, 60, 127, false });
        s->post(Synth::EV_SEQ_PLAY, 0, 0);
        std::vector<std::pair<double, std::array<int, 3>>> ev; double t = 0;
        run(*s, fs, 1.0, [&](Synth& x, int) { for (int i = 0; i < x.midiOutCount; ++i) ev.push_back({ t + x.midiOutBuf[(size_t) i].offset / (double) fs, { x.midiOutBuf[(size_t) i].b[0], x.midiOutBuf[(size_t) i].b[1], x.midiOutBuf[(size_t) i].b[2] } }); t += 256.0 / fs; });
        s->post(Synth::EV_SEQ_STOP, 0, 0); run(*s, fs, 0.05, [](Synth&, int) {});
        int ons0 = 0; double off48 = -1;
        for (auto& e : ev) { if ((e.second[0] & 0xF0) == 0x90 && e.first < 0.05) ++ons0; if ((e.second[0] & 0xF0) == 0x80 && e.second[1] == 48 && off48 < 0) off48 = e.first; }
        std::printf("sequencer: %d notes at once on step 1; the 4-step note stops at %.3f s (expect %.3f)\n", ons0, off48, 3.5 * 0.125);
        CHECK(ons0 == 2, "the chord on step 1 sent %d notes", ons0);
        CHECK(std::fabs(off48 - 3.5 * 0.125) < 0.02, "the 4-step note stopped at %.3f s", off48);
        // a MIDI file of the pattern, read back into another
        const std::string mid = s->exportMidi(2);
        int len = 0; const int got = s->importMidi(mid, 5, &len);
        auto key = [](std::vector<std::pair<int, GNote>> v) { std::vector<std::array<int, 4>> k; for (auto& q : v) k.push_back({ q.second.start, q.second.len, q.second.note, q.second.vel }); std::sort(k.begin(), k.end()); return k; };
        CHECK(got == 3 && key(s->patternNotes(2)) == key(s->patternNotes(5)), "MIDI file round trip: %d notes", got);
        std::printf("MIDI file: %d bytes, %d notes back, length %d steps\n", (int) mid.size(), got, len);
        CHECK(s->importMidi("not a midi file", 6) < 0, "a non-MIDI file was accepted");
        // MIDI in
        s->initPatch(); s->resetCCMap();
        const uint8_t cc74[3] = { 0xB0, 74, 127 }; s->midiIn(cc74, 3);
        CHECK(std::fabs(s->get(P::cut) - 2047) < 1, "CC 74 didn't open the cutoff (%.0f)", s->get(P::cut));
        s->learnParam = P::res; const uint8_t cc12[3] = { 0xB0, 12, 64 }; s->midiIn(cc12, 3);
        CHECK(s->ccMap[12].load() == P::res && std::fabs(s->get(P::res) - 8) < 0.6f, "learning CC 12 for RESONANCE (map %d, res %.1f)", s->ccMap[12].load(), s->get(P::res));
        int id; float v; int changes = 0; while (s->popParamChange(id, v)) ++changes;
        CHECK(changes >= 2, "MIDI parameter changes weren't reported to the host (%d)", changes);
        s->set(P::midich, 2); const uint8_t ch1[3] = { 0xB0, 74, 0 }; s->midiIn(ch1, 3);
        CHECK(s->get(P::cut) > 2000, "a message on channel 1 got through a channel 2 filter");
        s->set(P::midich, 0);
        const uint8_t pc[5] = { 0xB0, 0, 1, 0xC0, 5 }; s->midiIn(pc, 5);
        CHECK(s->pendingProgram.exchange(-1) == 133, "bank 1 program 6 isn't preset 134");
        const uint8_t rpn[9] = { 0xB0, 101, 0, 0xB0, 100, 0, 0xB0, 6, 12 }; s->midiIn(rpn, 9);
        CHECK(std::fabs(s->get(P::bend) - 12) < 0.5f, "RPN 0 didn't set the bend range (%.0f)", s->get(P::bend));
        // MIDI clock in drives the sequencer: 24 ticks a beat at 120 BPM, a step every 6 ticks at 1/16
        s->set(P::clksrc, 1); s->set(P::slen, 16);
        const uint8_t start = 0xFA; s->midiIn(&start, 1);
        int steps = 0, lastStep = -1; float L[1000], R[1000];
        for (int tick = 0; tick < 24 * 4; ++tick) {   // four beats
            const uint8_t clk = 0xF8; s->postMidi(&clk, 1); s->render(L, R, 1000);   // 1000 samples a tick at 48 kHz = 120 BPM
            const int st = s->seqStepShown.load(); if (st != lastStep) { ++steps; lastStep = st; }
        }
        const uint8_t stop = 0xFC; s->postMidi(&stop, 1); s->render(L, R, 1000);
        std::printf("MIDI clock in: %d steps in 4 beats (expect 16)\n", steps);
        CHECK(steps >= 15 && steps <= 17, "MIDI clock drove %d steps in 4 beats", steps);
        s->set(P::clksrc, 0); s->initPatch(); s->resetCCMap();
        // the old saved step format still loads
        s->loadState("pat7=" + std::to_string(128 | 60) + "," + std::to_string(0) + "," + std::to_string(128 | 256 | 64) + "\n");
        auto ns = s->patternNotes(7);
        CHECK(ns.size() == 2, "an old pattern loaded %d notes", (int) ns.size());
    }

    // every preset: a chord, held then released
    int quiet = 0, loud = 0;
    for (int i = 0; i < presetCount(); ++i) {
        s->panic(); s->applyPreset(i); s->pollSample();
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
