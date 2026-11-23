// Omega Wheel of Steel engine tests: plain C++17, no JUCE. Builds synthetic tracks, drives the decks / FX / mixer
// like the processor would and checks the behaviour. Prints PASS/FAIL per check; exits nonzero on any failure.
//   wheel_engine_test [wavDir]   -- with a directory, also writes scratch / keylock / fx renders there for listening
#include "../Engine.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace wheel;

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kHost = 48000;
int failures = 0, passes = 0;

void check(bool ok, const char* name, const std::string& detail = {}) {
    std::printf("%s  %-52s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    (ok ? passes : failures)++;
}
std::string fmt(const char* f, double a, double b = 0, double c = 0) { char buf[256]; std::snprintf(buf, sizeof buf, f, a, b, c); return buf; }

// ---- synthetic tracks ----
std::shared_ptr<Track> sineTrack(double freq = 440, double secs = 30, double bpm = 120, double first = 0.1, double sr = 44100) {
    auto t = std::make_shared<Track>();
    t->sr = sr; t->bpm = bpm; t->firstBeat = first; t->title = "sine";
    const size_t n = (size_t) (secs * sr);
    t->L.resize(n); t->R.resize(n);
    for (size_t i = 0; i < n; ++i) t->L[i] = t->R[i] = (float) (0.5 * std::sin(2 * kPi * freq * (double) i / sr));
    return t;
}
// clicks on the beat + a tone (the brief's test track)
std::shared_ptr<Track> clickTrack(double bpm = 120, double secs = 30, double first = 0.1, double sr = 44100) {
    auto t = std::make_shared<Track>();
    t->sr = sr; t->bpm = bpm; t->firstBeat = first; t->title = "clicks";
    const size_t n = (size_t) (secs * sr);
    t->L.resize(n); t->R.resize(n);
    const double bl = 60.0 / bpm;
    for (size_t i = 0; i < n; ++i) {
        const double ts = (double) i / sr;
        double v = 0.2 * std::sin(2 * kPi * 330 * ts);
        const double ph = std::fmod(ts - first + 100 * bl, bl);
        if (ph < 0.01) v += 0.6 * std::exp(-ph * 400) * std::sin(2 * kPi * 1500 * ph);
        t->L[i] = t->R[i] = (float) v;
    }
    return t;
}
// something musical for the listening renders: kick, hats, bass, a chord stab, a vocal-ish sweep
std::shared_ptr<Track> musicTrack(double bpm = 120, double secs = 30, double sr = 44100) {
    auto t = std::make_shared<Track>();
    t->sr = sr; t->bpm = bpm; t->firstBeat = 0; t->title = "music";
    const size_t n = (size_t) (secs * sr);
    t->L.resize(n); t->R.resize(n);
    std::mt19937 rng(1); std::uniform_real_distribution<float> u(-1, 1);
    const double bl = 60.0 / bpm;
    const double chord[3] = { 220.0, 261.63, 329.63 };
    double kp = 0, hp = 0;
    for (size_t i = 0; i < n; ++i) {
        const double ts = (double) i / sr, ph = std::fmod(ts, bl), beat = std::floor(ts / bl);
        const double kf = 50 + 120 * std::exp(-ph * 30);
        kp += 2 * kPi * kf / sr;
        double v = 0.7 * std::exp(-ph * 9) * std::sin(kp);
        const double hph = std::fmod(ts + bl / 2, bl);
        const float nz = u(rng); hp = nz - hp * 0.0; v += 0.12 * std::exp(-hph * 60) * hp * (hph < 0.08 ? 1 : 0);
        const double bassF = ((int) beat % 8 < 4) ? 55.0 : 65.41;
        v += 0.25 * std::sin(2 * kPi * bassF * ts) * (0.6 + 0.4 * std::exp(-ph * 4));
        double c = 0;
        for (double f : chord) c += std::sin(2 * kPi * f * ts) + 0.3 * std::sin(2 * kPi * 2 * f * ts);
        v += 0.05 * c * ((int) beat % 2 == 1 ? std::exp(-ph * 5) : 0.2);
        const double vf = 600 + 300 * std::sin(2 * kPi * 0.25 * ts);
        v += 0.06 * std::sin(2 * kPi * vf * ts + 3 * std::sin(2 * kPi * 5 * ts));
        t->L[i] = (float) (v * 0.8); t->R[i] = (float) (v * 0.8 + 0.02 * nz);
    }
    return t;
}

// ---- rendering helpers ----
struct Rec { std::vector<float> L, R; };
// renders 'secs' in blocks of 'block', calling 'each' before every block (optional), appending to rec (optional)
void run(Deck& d, double secs, Rec* rec = nullptr, int block = 64, const std::function<void(int)>& each = {}) {
    const int total = (int) std::lround(secs * kHost);
    std::vector<float> l((size_t) block), r((size_t) block);
    int k = 0;
    for (int done = 0; done < total; done += block, ++k) {
        const int n = std::min(block, total - done);
        if (each) each(k);
        d.render(l.data(), r.data(), n);
        if (rec) { rec->L.insert(rec->L.end(), l.begin(), l.begin() + n); rec->R.insert(rec->R.end(), r.begin(), r.begin() + n); }
    }
}
double maxDelta(const std::vector<float>& x, size_t from = 1, size_t to = SIZE_MAX) {
    double m = 0;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, x.size()); ++i) m = std::max(m, (double) std::abs(x[i] - x[i - 1]));
    return m;
}
double zcFreq(const std::vector<float>& x, size_t from, size_t to) {
    double first = -1, last = -1; int cnt = 0;
    for (size_t i = from + 1; i < to && i < x.size(); ++i) {
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double t = (double) (i - 1) + x[i - 1] / (double) (x[i - 1] - x[i]);
            if (first < 0) first = t; else ++cnt;
            last = t;
        }
    }
    return cnt > 0 ? cnt * kHost / (last - first) : 0;
}
double rms(const float* x, size_t n) { double s = 0; for (size_t i = 0; i < n; ++i) s += (double) x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, n)); }

// ---- WAV writer (16-bit stereo) ----
void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("  (could not write %s)\n", path.c_str()); return; }
    const uint32_t n = (uint32_t) std::min(L.size(), R.size()), data = n * 4;
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32((uint32_t) sr); u32((uint32_t) sr * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(data);
    for (uint32_t i = 0; i < n; ++i) {
        const int16_t a = (int16_t) std::lround(std::max(-1.0f, std::min(1.0f, L[i])) * 32767);
        const int16_t b = (int16_t) std::lround(std::max(-1.0f, std::min(1.0f, R[i])) * 32767);
        std::fwrite(&a, 2, 1, f); std::fwrite(&b, 2, 1, f);
    }
    std::fclose(f);
    std::printf("  wrote %s (%.1f s)\n", path.c_str(), n / (double) sr);
}

void fresh(Deck& d, std::shared_ptr<const Track> t) {
    d.prepare(kHost, 512);
    d.set = DeckSettings {};
    d.load(std::move(t));
}
}  // namespace

// ================================================================================================================
static void testMotor() {
    Deck d; fresh(d, sineTrack());
    d.set.startTime = 0.7f; d.set.brakeTime = 0.35f;
    d.playPause();
    double tUp = -1, t = 0;
    run(d, 2.0, nullptr, 64, [&](int k) { t = k * 64 / kHost; if (tUp < 0 && d.st.rate.load() >= 0.99) tUp = t; });
    check(tUp > 0.5 && tUp < 0.95, "motor start reaches speed in ~startTime", fmt("%.3f s (startTime 0.70)", tUp));
    check(std::abs(d.st.rate.load() - 1.0) < 1e-3, "settles at exactly x1.00", fmt("rate %.5f", d.st.rate.load()));
    d.playPause();
    double tDown = -1;
    run(d, 1.0, nullptr, 64, [&](int k) { t = k * 64 / kHost; if (tDown < 0 && d.st.rate.load() <= 0.01) tDown = t; });
    check(tDown > 0.25 && tDown < 0.5, "brake stops in ~brakeTime", fmt("%.3f s (brakeTime 0.35)", tDown));
    // 45 rpm and platter angle
    d.set.rpm45 = true; d.set.startTime = 0.05f; d.playPause();
    run(d, 0.5);
    check(std::abs(d.st.rate.load() - 1.35) < 2e-3, "45 rpm plays x1.35", fmt("rate %.4f", d.st.rate.load()));
    const double a0 = d.st.angle.load();
    run(d, 1.0);
    double da = d.st.angle.load() - a0; if (da < 0) da += 1;
    check(std::abs(da - 0.75) < 0.01, "platter angle 0.75 rev/s at 45", fmt("%.4f rev", da));
    d.set.rpm45 = false;
    d.set.pitch = 1; d.set.pitchRange = 0.08f;
    run(d, 0.5);
    check(std::abs(d.st.rate.load() - 1.08) < 2e-3, "pitch fader +8 %", fmt("rate %.4f", d.st.rate.load()));
    d.nudge(1); run(d, 0.2);
    check(std::abs(d.st.rate.load() - 1.08 * 1.04) < 3e-3, "nudge +4 %", fmt("rate %.4f", d.st.rate.load()));
    d.nudge(0); run(d, 0.2);
    const double p0 = d.st.pos.load();
    d.bend(0.25); run(d, 0.5);
    const double adv = d.st.pos.load() - p0;
    check(std::abs(adv - (0.5 * 1.08 + 0.25)) < 0.01, "bend pushes the record by secDelta", fmt("advanced %.4f s (want %.4f)", adv, 0.5 * 1.08 + 0.25));
}

static void testCues() {
    Deck d; fresh(d, sineTrack());
    run(d, 0.01);
    check(std::abs(d.st.pos.load() - 0.1) < 1e-3, "load: cue at the first beat", fmt("pos %.4f", d.st.pos.load()));
    d.seek(5.3); run(d, 0.01);
    d.cue(true); run(d, 0.01);
    check(std::abs(d.st.cuePoint.load() - 5.1) < 1e-6, "cue while stopped sets a quantized cue", fmt("cue %.4f", d.st.cuePoint.load()));
    check(d.st.playing.load(), "cue held while stopped previews");
    run(d, 0.5);
    check(std::abs(d.st.pos.load() - 5.61) < 0.002, "preview plays from the cue", fmt("pos %.4f", d.st.pos.load()));
    d.cue(false); run(d, 0.01);
    check(!d.st.playing.load() && std::abs(d.st.pos.load() - 5.1) < 1e-3, "cue release: back to cue, stopped", fmt("pos %.4f", d.st.pos.load()));
    // play, then cue: jump back and stop
    d.playPause(); run(d, 2.0);
    d.cue(true); d.cue(false); run(d, 0.05);
    check(!d.st.playing.load() && std::abs(d.st.pos.load() - 5.1) < 1e-3, "cue while playing: back & stop", fmt("pos %.4f", d.st.pos.load()));
    // play pressed while previewing keeps playing
    d.cue(true); run(d, 0.2); d.playPause(); d.cue(false); run(d, 0.2);
    check(d.st.playing.load() && d.st.pos.load() > 5.4, "play during cue preview latches", fmt("pos %.4f", d.st.pos.load()));

    // hot cues
    d.play(false); run(d, 1.0);
    d.seek(10.02); run(d, 0.01);
    d.hotCue(0, true); d.hotCue(0, false);
    check(std::abs(d.cues[0].t - 10.1) < 1e-6, "hot cue set (quantized)", fmt("t %.4f", d.cues[0].t));
    d.seek(3.0); run(d, 0.01);
    d.hotCue(0, true); run(d, 0.5);
    check(d.st.playing.load() && std::abs(d.st.pos.load() - 10.6) < 0.01, "hot cue held while stopped previews", fmt("pos %.4f", d.st.pos.load()));
    d.hotCue(0, false); run(d, 0.01);
    check(!d.st.playing.load() && std::abs(d.st.pos.load() - 10.1) < 1e-3, "hot cue release: back & stop", fmt("pos %.4f", d.st.pos.load()));
    d.seek(3.0); d.set.startTime = 0.05f; d.playPause(); run(d, 0.8);
    const double ph0 = d.beatPhase();
    d.hotCue(0, true); d.hotCue(0, false);
    const double ph1 = d.beatPhase(); run(d, 0.1);
    double dph = std::abs(ph1 - ph0); dph = std::min(dph, 1 - dph);
    check(d.st.playing.load() && std::abs(d.st.pos.load() - 10.2) < 0.26 && dph < 1e-3, "hot cue while playing jumps, keeps the beat phase",
          fmt("pos %.4f, phase %.4f -> %.4f", d.st.pos.load(), ph0, ph1));
    d.clearCue(0);
    check(!d.cues[0].set(), "clear cue");
}

static void testLoops() {
    Deck d; fresh(d, clickTrack());
    d.set.startTime = 0.05f;
    d.seek(2.04); d.playPause(); run(d, 0.3);
    d.autoLoop(4); run(d, 64 / kHost);
    const double A = d.st.loopA.load(), B = d.st.loopB.load();
    check(std::abs(A - 2.1) < 1e-6 && std::abs(B - 4.1) < 1e-6, "auto loop 4 beats on the nearest beat", fmt("%.4f .. %.4f", A, B));
    double lo = 1e9, hi = -1;
    run(d, 5.0, nullptr, 64, [&](int) { const double p = d.st.pos.load(); lo = std::min(lo, p); hi = std::max(hi, p); });
    check(d.st.looping.load() && lo >= A - 1e-9 && hi <= B + 1e-9, "loop holds playback in [A, B]", fmt("seen %.3f .. %.3f", lo, hi));
    d.loopHalve();
    run(d, 0.01);
    check(std::abs(d.st.loopB.load() - 3.1) < 1e-6, "loop halve", fmt("B %.4f", d.st.loopB.load()));
    d.loopDouble(); run(d, 0.01);
    check(std::abs(d.st.loopB.load() - 4.1) < 1e-6, "loop double", fmt("B %.4f", d.st.loopB.load()));
    d.loopMove(1); run(d, 0.01);
    check(std::abs(d.st.loopA.load() - 2.6) < 1e-6 && d.st.pos.load() >= 2.6, "loop move +1 beat", fmt("A %.4f pos %.4f", d.st.loopA.load(), d.st.pos.load()));
    d.loopExit(); run(d, 3.0);
    check(!d.st.looping.load() && d.st.pos.load() > 4.6, "loop exit lets it run on", fmt("pos %.4f", d.st.pos.load()));
    d.reloop(); run(d, 0.01);
    check(d.st.looping.load() && std::abs(d.st.pos.load() - 2.6) < 0.01, "reloop jumps to A", fmt("pos %.4f", d.st.pos.load()));
    d.autoLoop(4); run(d, 0.01);   // same size again: toggles off
    check(!d.st.looping.load(), "auto loop same size toggles off");
    // manual loop
    d.seek(8.0); run(d, 0.05);
    d.loopIn(); run(d, 0.75); d.loopOut(); run(d, 64 / kHost);
    const double mA = d.st.loopA.load(), mB = d.st.loopB.load();
    run(d, 2.0, nullptr, 64, [&](int) {});
    check(d.st.looping.load() && std::abs(mA - 8.1) < 1e-6 && std::abs(mB - 8.85) < 1e-6 && d.st.pos.load() >= mA && d.st.pos.load() <= mB,
          "manual loop in / out (1/4-beat quantized)", fmt("%.4f .. %.4f pos %.4f", mA, mB, d.st.pos.load()));
    d.loopExit();
    // roll with slip return
    run(d, 0.3);
    const double p0 = d.st.pos.load();
    d.roll(0.25, true);
    lo = 1e9; hi = -1;
    run(d, 1.0, nullptr, 64, [&](int) { const double p = d.st.pos.load(); lo = std::min(lo, p); hi = std::max(hi, p); });
    const double rollLen = d.st.loopB.load() - d.st.loopA.load();
    check(d.st.rolling.load() && hi - lo < 0.13 && std::abs(rollLen - 0.125) < 1e-6, "loop roll 1/4 beat repeats", fmt("span %.4f len %.4f", hi - lo, rollLen));
    d.roll(0.25, false); run(d, 64 / kHost);
    const double want = p0 + 1.0 + 64 / kHost;
    check(!d.st.rolling.load() && !d.st.looping.load() && std::abs(d.st.pos.load() - want) < 0.005, "roll release slips back to the timeline",
          fmt("pos %.4f want %.4f", d.st.pos.load(), want));
    // saved loop from the track
    auto t2 = clickTrack(); t2->loops[1] = SavedLoop { 12.1, 14.1 };
    d.load(t2); d.savedLoop(1); run(d, 0.01);
    check(d.st.looping.load() && std::abs(d.st.pos.load() - 12.1) < 1e-3, "saved loop", fmt("pos %.4f", d.st.pos.load()));
}

static void testScratchSlip(const std::string& wavDir) {
    Deck d; fresh(d, sineTrack());
    d.set.startTime = 0.05f;
    d.seek(10); d.playPause(); run(d, 0.5);
    d.touch(true); run(d, 0.05);
    check(std::abs(d.st.rate.load()) < 1e-3, "hand stops the record dead", fmt("rate %.5f", d.st.rate.load()));
    const double p0 = d.st.pos.load();
    // push forward 0.25 s in 10 steps of a 512-sample block each (jerky controller), then pull back 0.5 s
    double minRate = 0;
    run(d, 10 * 512 / kHost, nullptr, 512, [&](int k) { if (k < 10) d.move(0.025); });
    run(d, 0.1, nullptr, 512);
    check(std::abs(d.st.pos.load() - (p0 + 0.25)) < 2e-3, "scratch forward lands where the hand put it", fmt("moved %.4f (want 0.25)", d.st.pos.load() - p0));
    run(d, 20 * 512 / kHost, nullptr, 512, [&](int k) { if (k < 20) d.move(-0.025); minRate = std::min(minRate, d.st.rate.load()); });
    run(d, 0.1, nullptr, 512);
    check(std::abs(d.st.pos.load() - (p0 - 0.25)) < 2e-3 && minRate < -0.5, "scratch back plays in reverse", fmt("pos %.4f rate %.3f", d.st.pos.load() - p0, minRate));
    // smoothness: a steady 1x drag from a 100 Hz jerky controller should give a steady rate (small ripple)
    std::vector<double> rates;
    // irregular controller: updates every 5 or 9 blocks of 64 (6.7 / 12 ms), the hand moving at 1x
    int nextMove = 0, lastMove = 0;
    run(d, 1.5, nullptr, 64, [&](int k) {
        if (k == nextMove) { d.move((k - lastMove) * 64 / kHost); lastMove = k; nextMove = k + ((k / 7) % 2 ? 5 : 9); }
        if (k > 400) rates.push_back(d.st.rate.load());
    });
    double rmin = 1e9, rmax = -1e9; for (double r : rates) { rmin = std::min(rmin, r); rmax = std::max(rmax, r); }
    check(rmin > 0.7 && rmax < 1.3, "jerky controller drags give smooth platter speed", fmt("rate %.3f .. %.3f", rmin, rmax));
    d.touch(false); run(d, 0.3);
    d.touch(true); run(d, 0.1);
    d.touch(false);
    double tBack = -1;
    run(d, 0.5, nullptr, 64, [&](int k) { if (tBack < 0 && std::abs(d.st.rate.load() - 1) < 0.01) tBack = k * 64 / kHost; });
    check(tBack > 0 && tBack < 0.12, "release: motor picks the record back up", fmt("%.3f s", tBack));

    // slip scratch: release returns to where the record would have been
    d.set.slip = true;
    run(d, 0.2);
    const double s0 = d.st.pos.load();
    d.touch(true);
    run(d, 1.0, nullptr, 256, [&](int k) { d.move((k / 12) % 2 ? -0.02 : 0.02); });
    d.touch(false); run(d, 64 / kHost);
    const double want = s0 + 1.0 + 64 / kHost;
    check(std::abs(d.st.pos.load() - want) < 0.005, "slip mode: scratch release jumps to the shadow", fmt("pos %.4f want %.4f", d.st.pos.load(), want));
    // censor: reverse while held, then back on the timeline
    const double c0 = d.st.pos.load();
    d.censor(true);
    double minR = 9;
    run(d, 0.5, nullptr, 64, [&](int) { minR = std::min(minR, d.st.rate.load()); });
    const double during = d.st.pos.load();
    d.censor(false); run(d, 64 / kHost);
    check(minR < -0.99 && during < c0 && std::abs(d.st.pos.load() - (c0 + 0.5 + 64 / kHost)) < 0.005, "censor plays reverse then drops back in",
          fmt("rate %.2f, pos %.4f want %.4f", minR, d.st.pos.load(), c0 + 0.5 + 64 / kHost));
    d.set.slip = false;

    if (!wavDir.empty()) {
        Deck m; fresh(m, musicTrack());
        m.set.startTime = 0.4f; m.seek(4.0);
        Rec rec;
        m.playPause(); run(m, 2.0, &rec);
        // baby scratches then forward/back chirps from a jerky 'controller' (moves every 512-sample block), then release
        auto pattern = [&](double secs, double hz, double amp) {
            const int blocks = (int) (secs * kHost / 512);
            double ph = 0, last = 0;
            run(m, blocks * 512 / kHost, &rec, 512, [&](int) {
                ph += hz * 512 / kHost;
                const double x = amp * (0.5 - 0.5 * std::cos(2 * kPi * ph));
                m.move(x - last); last = x;
            });
        };
        m.touch(true); pattern(2.0, 2.0, 0.25); m.touch(false); run(m, 1.0, &rec);
        m.touch(true); pattern(2.0, 4.0, 0.15); m.touch(false); run(m, 1.0, &rec);
        m.touch(true); run(m, 0.3, &rec); m.touch(false); run(m, 1.0, &rec);   // stop dead, let go
        m.playPause(); run(m, 1.0, &rec);                                       // brake
        m.playPause(); run(m, 1.5, &rec);                                       // start
        writeWav(wavDir + "/wheel_scratch.wav", rec.L, rec.R, (int) kHost);
    }
}

static void testClickFree() {
    // every jump crossfades: on a 440 Hz sine the sample-to-sample step stays near the sine's own slope
    const double natural = 0.5 * 2 * kPi * 440 / kHost;   // 0.0288
    const double limit = 0.05;
    Deck d; fresh(d, sineTrack());
    d.set.startTime = 0.05f;
    Rec rec;
    d.seek(5.0); d.hotCue(0, true); d.hotCue(0, false);
    d.seek(1.37);
    d.playPause(); run(d, 0.3, &rec);
    for (int i = 0; i < 6; ++i) { d.hotCue(0, true); run(d, 0.05, &rec); d.hotCue(0, false); d.seek(1.0 + 1.337 * i); run(d, 0.07, &rec); }
    d.autoLoop(1.0 / 8); run(d, 0.6, &rec);           // 62.5 ms loop: wraps every 3000 samples
    d.loopExit(); d.roll(1.0 / 16, true); run(d, 0.3, &rec); d.roll(1.0 / 16, false); run(d, 0.1, &rec);
    d.cue(true); run(d, 0.1, &rec); d.cue(false); run(d, 0.1, &rec);
    d.cue(true); run(d, 0.1, &rec); d.cue(false); run(d, 0.1, &rec);   // preview & instant stop
    double m = maxDelta(rec.L);
    check(m < limit, "jumps / loops / rolls / cue stops are click-free", fmt("max step %.4f (sine slope %.4f)", m, natural));
    // keylock engage at +8 %, jumps under keylock, disengage by touching
    Rec k;
    d.set.keyLock = true; d.set.pitch = 1; d.set.pitchRange = 0.08f;
    d.play(true); run(d, 0.5, &k);
    d.seek(8.123); run(d, 0.2, &k); d.hotCue(0, true); d.hotCue(0, false); run(d, 0.3, &k);
    d.autoLoop(2); run(d, 1.5, &k); d.loopExit();
    d.touch(true); run(d, 0.2, &k); d.touch(false); run(d, 0.5, &k);
    m = maxDelta(k.L);
    check(m < limit * 1.08, "keylock engage / jumps / scratch-in are click-free", fmt("max step %.4f", m));
    // end of track: stops, no click
    Rec e;
    d.set.keyLock = false; d.seek(29.5); run(d, 1.0, &e);
    check(!d.st.playing.load() && d.st.pos.load() > 29.99, "end of track: stop", fmt("pos %.3f", d.st.pos.load()));
    check(maxDelta(e.L) < limit * 1.08, "end of track is click-free", fmt("max step %.4f", maxDelta(e.L)));
}

static void testKeylock(const std::string& wavDir) {
    Deck d; fresh(d, sineTrack(440));
    d.set.startTime = 0.05f; d.set.pitchRange = 0.08f; d.set.pitch = 1; d.set.keyLock = true;
    d.seek(2); d.playPause(); run(d, 0.5);
    const double p0 = d.st.pos.load();
    Rec r; run(d, 2.0, &r);
    const double adv = d.st.pos.load() - p0;
    const double f = zcFreq(r.L, 0, r.L.size());
    check(std::abs(f - 440) < 2.0, "keylock at +8 % keeps 440 Hz", fmt("%.2f Hz", f));
    check(std::abs(adv - 2.16) < 0.005, "keylock at +8 % still runs 8 % faster", fmt("%.4f s in 2 s", adv));
    // amplitude steadiness (no phasing / warble): 10 ms RMS windows
    double lo = 1e9, hi = 0;
    for (size_t i = 0; i + 480 <= r.L.size(); i += 480) { const double v = rms(&r.L[i], 480); lo = std::min(lo, v); hi = std::max(hi, v); }
    check(20 * std::log10(hi / lo) < 0.5, "keylock is steady (no warble)", fmt("RMS spread %.3f dB", 20 * std::log10(hi / lo)));
    // small pitch change: -1.3 %
    d.set.pitch = -0.1625f; run(d, 0.5);
    Rec r2; run(d, 2.0, &r2);
    const double f2 = zcFreq(r2.L, 0, r2.L.size());
    lo = 1e9; hi = 0;
    for (size_t i = 0; i + 480 <= r2.L.size(); i += 480) { const double v = rms(&r2.L[i], 480); lo = std::min(lo, v); hi = std::max(hi, v); }
    check(std::abs(f2 - 440) < 2.0 && 20 * std::log10(hi / lo) < 0.5, "keylock at -1.3 % steady", fmt("%.2f Hz, spread %.3f dB", f2, 20 * std::log10(hi / lo)));
    d.set.keyLock = false; d.set.pitch = 1; run(d, 0.5);
    Rec r3; run(d, 1.0, &r3);
    const double f3 = zcFreq(r3.L, 0, r3.L.size());
    check(std::abs(f3 - 475.2) < 2.0, "without keylock +8 % = 475 Hz", fmt("%.2f Hz", f3));

    // keylock on a 44.1k click track: beats keep coming at the faster tempo
    Deck c; fresh(c, clickTrack()); c.set.startTime = 0.05f; c.set.keyLock = true; c.set.pitch = 1; c.set.pitchRange = 0.08f;
    c.playPause(); run(c, 1.0);
    check(std::abs(c.currentBpm() - 129.6) < 0.05, "keylocked bpm reads 129.6", fmt("%.3f", c.currentBpm()));

    if (!wavDir.empty()) {
        Deck m; fresh(m, musicTrack());
        m.set.startTime = 0.05f; m.set.pitchRange = 0.08f; m.set.pitch = 1;
        Rec rec;
        m.playPause();
        run(m, 4.0, &rec);                                // +8 % vinyl
        m.set.keyLock = true; run(m, 6.0, &rec);          // +8 % keylock
        m.set.pitch = -0.25f; run(m, 4.0, &rec);          // -2 % keylock
        m.set.pitchRange = 0.16f; m.set.pitch = -1; run(m, 4.0, &rec);   // -16 % keylock
        writeWav(wavDir + "/wheel_keylock.wav", rec.L, rec.R, (int) kHost);
    }
}

static void testSync() {
    Deck a, b; fresh(a, clickTrack(120)); fresh(b, clickTrack(126, 30, 0.23));
    a.set.startTime = b.set.startTime = 0.05f;
    a.set.pitch = 0.375f; a.set.pitchRange = 0.08f;   // +3 % = 123.6
    a.playPause(); b.playPause();
    run(a, 0.5); run(b, 0.5);
    b.syncTo(a.currentBpm(), a.beatPhase(), true);
    run(a, 0.5); run(b, 0.5);
    check(std::abs(b.currentBpm() - 123.6) < 0.05, "sync matches tempo", fmt("A %.3f B %.3f", a.currentBpm(), b.currentBpm()));
    b.setSync(true);
    std::vector<float> l(64), r(64);
    for (int k = 0; k < (int) (2.0 * kHost / 64); ++k) {
        b.syncTo(a.currentBpm(), a.beatPhase(), true);
        a.render(l.data(), r.data(), 64); b.render(l.data(), r.data(), 64);
    }
    double e = std::abs(a.beatPhase() - b.beatPhase()); e = std::min(e, 1 - e);
    check(e < 0.01 && b.st.synced.load(), "sync aligns the beat phase", fmt("phase error %.4f beats", e));
    // a 62 bpm track syncs at half time... and a pitch-fader move takes over after a one-shot sync
    b.setSync(false);
    b.set.pitch = 0.5f; run(b, 0.5);
    check(!b.st.synced.load() && std::abs(b.currentBpm() - 126 * 1.04) < 0.05, "pitch fader takes over after sync off", fmt("%.3f", b.currentBpm()));
    Deck h; fresh(h, clickTrack(62)); h.playPause(); h.syncTo(123.6, 0, false); run(h, 0.5);
    check(std::abs(h.currentBpm() - 61.8) < 0.05, "sync picks half time for a 62 bpm track", fmt("%.3f", h.currentBpm()));
    h.gridBpm(124); run(h, 0.01);
    check(std::abs(h.beatLength() - 60.0 / 124) < 1e-9, "grid bpm override", fmt("%.4f", h.beatLength()));
}

static void testMixer() {
    Mixer mx; mx.prepare(kHost, 512);
    const int n = 512, blocks = 200;
    std::vector<float> aL(n), aR(n), bL(n), bR(n), oL(n), oR(n), cL(n), cR(n);
    auto measure = [&](MixerSettings s, std::function<float(double)> sigA, std::function<float(double)> sigB, double* outRms, double* inRms) {
        Mixer m; m.prepare(kHost, n);
        double so = 0, si = 0; size_t cnt = 0;
        for (int k = 0; k < blocks; ++k) {
            for (int i = 0; i < n; ++i) { const double t = (k * n + i) / kHost; aL[i] = aR[i] = sigA(t); bL[i] = bR[i] = sigB(t); }
            m.process(aL.data(), aR.data(), bL.data(), bR.data(), oL.data(), oR.data(), cL.data(), cR.data(), n, s);
            if (k > blocks / 2) for (int i = 0; i < n; ++i) { so += (double) oL[i] * oL[i]; si += (double) (aL[i] + bL[i]) * (aL[i] + bL[i]); ++cnt; }
        }
        *outRms = std::sqrt(so / cnt); *inRms = std::sqrt(si / cnt);
    };
    auto zero = [](double) { return 0.0f; };
    MixerSettings flat; flat.ch[0].xfAssign = flat.ch[1].xfAssign = 1;
    double o, i;
    auto multi = [](double t) { return (float) (0.08 * (std::sin(2 * kPi * 50 * t) + std::sin(2 * kPi * 300 * t) + std::sin(2 * kPi * 1000 * t) + std::sin(2 * kPi * 4000 * t) + std::sin(2 * kPi * 12000 * t))); };
    measure(flat, multi, zero, &o, &i);
    check(std::abs(20 * std::log10(o / i)) < 0.2, "flat EQ / filter centre is transparent", fmt("%.3f dB", 20 * std::log10(o / i)));
    struct K { const char* name; double f; int band; } kills[] = { { "low kill at 80 Hz", 80, 0 }, { "mid kill at 1 kHz", 1000, 1 }, { "high kill at 12 kHz", 12000, 2 } };
    for (auto& k : kills) {
        MixerSettings s = flat;
        (k.band == 0 ? s.ch[0].low : k.band == 1 ? s.ch[0].mid : s.ch[0].hi) = -26;
        const double f = k.f;
        measure(s, [f](double t) { return (float) (0.3 * std::sin(2 * kPi * f * t)); }, zero, &o, &i);
        check(20 * std::log10(o / i) < -30, k.name, fmt("%.1f dB", 20 * std::log10(o / i)));
    }
    {   // filter knob: full left kills 5 kHz, full right kills 100 Hz
        MixerSettings s = flat; s.ch[0].filter = -1;
        measure(s, [](double t) { return (float) (0.3 * std::sin(2 * kPi * 5000 * t)); }, zero, &o, &i);
        const double lp = 20 * std::log10(o / i);
        s.ch[0].filter = 1;
        measure(s, [](double t) { return (float) (0.3 * std::sin(2 * kPi * 100 * t)); }, zero, &o, &i);
        const double hp = 20 * std::log10(o / i);
        check(lp < -40 && hp < -30, "filter knob LP / HP sweep ends", fmt("LP %.1f dB, HP %.1f dB", lp, hp));
    }
    // crossfader: sharp cut curve
    auto sinA = [](double t) { return (float) (0.3 * std::sin(2 * kPi * 500 * t)); };
    MixerSettings x; x.ch[0].xfAssign = 0; x.ch[1].xfAssign = 2; x.xfCurve = 1;
    x.xfader = -1; measure(x, zero, sinA, &o, &i);
    const double bClosed = o / i;
    x.xfader = -1 + 0.04f; measure(x, zero, sinA, &o, &i);
    const double bOpen = o / i;
    x.xfader = 0; measure(x, sinA, zero, &o, &i);
    const double aMid = o / i;
    check(bClosed < 1e-4 && bOpen > 0.97 && aMid > 0.97, "crossfader scratch cut: a hair opens fully", fmt("closed %.5f, +2 %% %.3f, A at centre %.3f", bClosed, bOpen, aMid));
    x.xfCurve = 0; x.xfader = 0; measure(x, sinA, zero, &o, &i);
    check(std::abs(o / i - 0.7071) < 0.01, "crossfader blend: constant power at centre", fmt("%.4f", o / i));
    x.hamster = true; x.xfCurve = 1; x.xfader = -1; measure(x, zero, sinA, &o, &i);
    check(o / i > 0.97, "hamster reverses the crossfader", fmt("%.3f", o / i));
    {   // channel fader curve
        MixerSettings c = flat; c.ch[0].fader = 0.1f; c.faderCurve = 1; measure(c, sinA, zero, &o, &i);
        const double sharp = o / i;
        c.faderCurve = 0; measure(c, sinA, zero, &o, &i);
        check(sharp > 0.9 && o / i < 0.01, "channel fader curve (sharp vs slow)", fmt("at 10 %%: sharp %.3f, slow %.4f", sharp, o / i));
    }
    {   // no zipper: slam the fader and the low kill on a 200 Hz sine
        Mixer m; m.prepare(kHost, n);
        MixerSettings s = flat;
        std::vector<float> out;
        for (int k = 0; k < 60; ++k) {
            for (int j = 0; j < n; ++j) { const double t = (k * n + j) / kHost; aL[j] = aR[j] = (float) (0.4 * std::sin(2 * kPi * 200 * t)); bL[j] = bR[j] = 0; }
            if (k == 20) s.ch[0].fader = 0;
            if (k == 30) s.ch[0].fader = 1;
            if (k == 40) s.ch[0].low = -26;
            if (k == 50) s.ch[0].low = 6;
            m.process(aL.data(), aR.data(), bL.data(), bR.data(), oL.data(), oR.data(), nullptr, nullptr, n, s);
            out.insert(out.end(), oL.begin(), oL.end());
        }
        const double md = maxDelta(out, 10 * n);
        check(md < 0.4 * 2 * kPi * 200 / kHost * 1.6 + 0.01, "gain changes are smoothed (no zipper / clicks)", fmt("max step %.4f", md));
    }
    {   // limiter
        MixerSettings s = flat; s.master = 12;
        measure(s, [](double t) { return (float) (0.9 * std::sin(2 * kPi * 100 * t)); }, [](double t) { return (float) (0.9 * std::sin(2 * kPi * 150 * t)); }, &o, &i);
        float pk = 0; for (float v : oL) pk = std::max(pk, std::abs(v));
        check(pk <= 1.0f && o > 0.3, "soft limiter holds the master under 0 dBFS", fmt("peak %.3f rms %.3f", pk, o));
    }
    {   // cue bus
        Mixer m; m.prepare(kHost, n);
        MixerSettings s = flat; s.ch[1].pfl = true; s.ch[1].fader = 0;
        for (int k = 0; k < 40; ++k) {
            for (int j = 0; j < n; ++j) { const double t = (k * n + j) / kHost; aL[j] = aR[j] = 0; bL[j] = bR[j] = (float) (0.3 * std::sin(2 * kPi * 440 * t)); }
            m.process(aL.data(), aR.data(), bL.data(), bR.data(), oL.data(), oR.data(), cL.data(), cR.data(), n, s);
        }
        check(rms(cL.data(), n) > 0.2 && rms(oL.data(), n) < 1e-4, "cue bus hears PFL pre-fader", fmt("cue %.3f master %.5f", rms(cL.data(), n), rms(oL.data(), n)));
        check(m.meter[1].load() < 1e-3 && m.masterL.load() < 1e-3, "meters", "");
    }
}

static void testFx(const std::string& wavDir) {
    const int n = 256;
    const char* names[] = { "off", "echo", "pingpong", "reverb", "flanger", "phaser", "crush", "gate" };
    auto src = clickTrack();
    Rec all;
    for (int ty = 1; ty < (int) FxType::count; ++ty) {
        Fx fx; fx.prepare(kHost, n);
        FxSettings s; s.type = (FxType) ty; s.on = true; s.beats = 0.5f; s.depth = 0.6f; s.param = 0.6f;
        std::vector<float> L(n), R(n), out;
        double tailE = 0, maxAbs = 0; bool finite = true;
        const int total = (int) (4.0 * kHost / n);
        for (int k = 0; k < total; ++k) {
            const bool on = k < (int) (1.5 * kHost / n);
            s.on = on;
            for (int j = 0; j < n; ++j) { const size_t idx = ((size_t) (k * n + j) * 441 / 480) % src->L.size(); L[j] = R[j] = on ? src->L[idx] : 0.0f; }
            fx.process(L.data(), R.data(), n, s, 120);
            for (int j = 0; j < n; ++j) {
                if (!std::isfinite(L[j]) || !std::isfinite(R[j])) finite = false;
                maxAbs = std::max(maxAbs, (double) std::abs(L[j]));
                if (!on && k < (int) (2.0 * kHost / n)) tailE += (double) L[j] * L[j];
                all.L.push_back(L[j]); all.R.push_back(R[j]);
            }
        }
        const bool tails = s.type == FxType::echo || s.type == FxType::pingpong || s.type == FxType::reverb;
        const bool ok = finite && maxAbs < 3 && (!tails || tailE > 1e-3);
        check(ok, (std::string("fx ") + names[ty] + (tails ? " (tail rings out)" : "")).c_str(), fmt("peak %.3f tail %.4f", maxAbs, tailE));
    }
    if (!wavDir.empty()) writeWav(wavDir + "/wheel_fx.wav", all.L, all.R, (int) kHost);
}

// ---- sound quality: key lock transients, anti-aliasing, rapid jumps, half / double time phase ----
namespace {
// a dry drum loop: kick on the beat (sweep + click), snare on 2 and 4, a closed hat on every off-beat 8th, a quiet
// bass. Every hit is short and 250 ms from the next, so each one can be looked at on its own.
std::shared_ptr<Track> drumTrack(double sr = 48000, double secs = 20, double bpm = 120, double first = 0.1) {
    auto t = std::make_shared<Track>();
    t->sr = sr; t->bpm = bpm; t->firstBeat = first; t->title = "drums";
    const size_t n = (size_t) (secs * sr);
    t->L.assign(n, 0.0f); t->R.assign(n, 0.0f);
    std::mt19937 rng(7); std::uniform_real_distribution<float> u(-1, 1);
    const double bl = 60.0 / bpm, e8 = bl / 2;
    double kp = 0; float pn = 0, ph1 = 0;
    for (size_t i = 0; i < n; ++i) {
        const double ts = (double) i / sr - first;
        double v = 0.08 * std::sin(2 * kPi * 55 * (double) i / sr);
        if (ts >= 0) {
            const int k8 = (int) std::floor(ts / e8); const double ph = ts - k8 * e8;
            const float nz = u(rng), d1 = nz - pn, hf = 0.5f * (d1 + ph1); pn = nz; ph1 = d1;   // top end, peaking at fs / 4
            if (k8 % 2 == 0) {   // beat: kick, plus snare on 2 and 4
                const double kf = 50 + 110 * std::exp(-ph * 35);
                kp += 2 * kPi * kf / sr;
                v += 0.6 * std::exp(-ph * 14) * std::sin(kp) + 0.25 * std::exp(-ph * 900) * hf;
                if ((k8 / 2) % 2 == 1) v += 0.3 * std::exp(-ph * 60) * nz + 0.2 * std::exp(-ph * 30) * std::sin(2 * kPi * 190 * ph);
            } else {
                kp = 0;
                v += 0.35 * std::exp(-ph * 250) * hf;   // hat: 4 ms decay, all top end
            }
        }
        t->L[i] = t->R[i] = (float) v;
    }
    return t;
}
// high-frequency energy envelope: first difference, 'win'-sample windows every 12 samples (0.25 ms)
constexpr int kHop = 12;
std::vector<double> hfEnv(const std::vector<float>& x, int win) {
    std::vector<double> c(x.size() + 1, 0.0);
    for (size_t i = 0; i < x.size(); ++i) {
        const double d = i < 1 ? 0.0 : (double) x[i] - x[i - 1];
        c[i + 1] = c[i] + d * d;
    }
    std::vector<double> e;
    for (size_t i = 0; i + (size_t) win <= x.size(); i += kHop) e.push_back(c[i + (size_t) win] - c[i]);
    return e;
}
struct Hit { int k8 = 0; double peak = 0; std::vector<double> shape; };
// Every 8th-note hit of the drum loop in a keylocked render, found around where the timeline says it should be: its
// HF envelope peak (1 ms windows) and its envelope shape (2 ms windows, +-40 ms around the peak, relative to it).
constexpr long kShapeR = 40 * 48 / kHop;
std::vector<Hit> findHits(const Track& src, const std::vector<float>& out, const std::vector<double>& posAt, int block) {
    const std::vector<double> e1 = hfEnv(out, 48), e2 = hfEnv(out, 96);
    const double e8 = 30.0 / src.bpm;
    std::vector<Hit> hs;
    for (int k8 = 0; ; ++k8) {
        const double te = src.firstBeat + k8 * e8;
        if (te < posAt.front() + 0.06) continue;
        if (te > posAt.back() - 0.1) break;
        size_t b = 0; while (b + 1 < posAt.size() && posAt[b + 1] < te) ++b;   // output block where the timeline gets there
        const double oi = ((double) b + (te - posAt[b]) / std::max(1e-9, posAt[b + 1] - posAt[b])) * block;
        const long c = (long) (oi / kHop);
        long pk = -1; double pv = 0;
        for (long f = c - kShapeR; f <= c + kShapeR; ++f) if (f >= 0 && f < (long) e1.size() && e1[(size_t) f] > pv) { pv = e1[(size_t) f]; pk = f; }
        if (pk < kShapeR + 8 || pv <= 0 || pk + kShapeR + 8 >= (long) e2.size()) continue;
        long p2 = pk - 2; double p2v = 0;   // the 2 ms envelope's own peak next to the 1 ms one
        for (long f = pk - 6; f <= pk + 2; ++f) if (e2[(size_t) f] > p2v) { p2v = e2[(size_t) f]; p2 = f; }
        Hit h; h.k8 = k8; h.peak = pv;
        for (long f = p2 - kShapeR; f <= p2 + kShapeR; ++f) h.shape.push_back(e2[(size_t) f] / p2v);
        hs.push_back(h);
    }
    return hs;
}
struct HitStats { double worstEcho = -99, meanEcho = 0, worstPeak = 99, meanPeak = 0; int hits = 0, doubled = 0; };
// The hits of a render against the same hits in a reference render (key lock at pitch 0: the source itself).
// echo: the most envelope energy, 3..40 ms before or after the peak, beyond what the reference hit has there,
// relative to the peak: a doubled / flammed attack or a pre-echo. peak: the attack's level (smearing loses it).
HitStats hitStats(const std::vector<Hit>& out, const std::vector<Hit>& ref) {
    HitStats h; double sumE = 0, sumP = 0;
    const long g = 3 * 48 / kHop;
    for (const Hit& o : out) {
        const Hit* r = nullptr;
        for (const Hit& x : ref) if (x.k8 == o.k8) r = &x;
        if (!r) continue;
        double ex = 1e-6; long at = 0;
        for (long k = 0; k < (long) o.shape.size(); ++k) {
            if (std::abs(k - kShapeR) < g) continue;
            double rv = 0;   // the reference within +-0.5 ms (peak alignment is only as good as a hop or two)
            for (long j = std::max(0L, k - 2); j <= std::min((long) r->shape.size() - 1, k + 2); ++j) rv = std::max(rv, r->shape[(size_t) j]);
            const double v = o.shape[(size_t) k] - rv;
            if (v > ex) { ex = v; at = k - kShapeR; }
        }
        const double echoDb = 10 * std::log10(ex), peakDb = 10 * std::log10(o.peak / r->peak);
        if (std::getenv("WHEEL_DEBUG") && (echoDb > -15 || peakDb < -1))
            std::printf("    hit %d (%s): echo %.1f dB at %+.1f ms, peak %.2f dB\n", o.k8, o.k8 % 2 ? "hat" : (o.k8 / 2) % 2 ? "snare" : "kick", echoDb, at * kHop / 48.0, peakDb);
        h.worstEcho = std::max(h.worstEcho, echoDb); h.worstPeak = std::min(h.worstPeak, peakDb);
        if (echoDb > -10) ++h.doubled;
        sumE += echoDb; sumP += peakDb; ++h.hits;
    }
    if (h.hits) { h.meanEcho = sumE / h.hits; h.meanPeak = sumP / h.hits; }
    return h;
}
// keylocked render of the drum loop at a pitch; returns the audio and the timeline per block
void keylockRender(std::shared_ptr<const Track> t, double pitchPct, double secs, Rec& rec, std::vector<double>& posAt) {
    Deck d; fresh(d, std::move(t));
    d.set.startTime = 0.05f; d.set.pitchRange = 0.16f; d.set.pitch = (float) (pitchPct / 16.0); d.set.keyLock = true;
    d.seek(1.0); d.playPause(); run(d, 0.5);
    posAt.clear(); posAt.push_back(d.st.pos.load());
    run(d, secs, &rec, 64, [&](int k) { if (k > 0) posAt.push_back(d.st.pos.load()); });
    posAt.push_back(d.st.pos.load());
}
// a sweep (log, lo..hi Hz) at 0.5
std::shared_ptr<Track> sweepTrack(double lo, double hi, double secs = 12, double sr = 44100) {
    auto t = std::make_shared<Track>();
    t->sr = sr; t->bpm = 120; t->firstBeat = 0; t->title = "sweep";
    const size_t n = (size_t) (secs * sr);
    t->L.resize(n); t->R.resize(n);
    double ph = 0;
    for (size_t i = 0; i < n; ++i) {
        const double f = lo * std::pow(hi / lo, (double) i / (double) n);
        ph += 2 * kPi * f / sr;
        t->L[i] = t->R[i] = (float) (0.5 * std::sin(ph));
    }
    return t;
}
// hand-drag the record at a steady 'rate' for 'secs' (64-sample controller steps); returns the output RMS after
// the first 0.15 s
double dragRms(std::shared_ptr<const Track> t, double rate, double secs, Rec* keep = nullptr) {
    Deck d; fresh(d, std::move(t));
    d.seek(0.5); d.touch(true); run(d, 0.05);
    Rec r; run(d, secs, &r, 64, [&](int) { d.move(rate * 64 / kHost); });
    if (keep) { keep->L.insert(keep->L.end(), r.L.begin(), r.L.end()); keep->R.insert(keep->R.end(), r.R.begin(), r.R.end()); }
    const size_t s0 = (size_t) (0.15 * kHost);
    return rms(r.L.data() + s0, r.L.size() - s0);
}
}  // namespace

static void testKeylockTransients(const std::string& wavDir) {
    auto t48 = drumTrack(48000), t44 = drumTrack(44100);
    for (double pct : { -16.0, -8.0, 8.0, 16.0 }) {
        for (auto* t : { &t48, &t44 }) {
            Rec r, r0; std::vector<double> posAt, pos0;
            keylockRender(*t, pct, 8.0, r, posAt);
            keylockRender(*t, 0.0, 10.0, r0, pos0);
            const HitStats h = hitStats(findHits(**t, r.L, posAt, 64), findHits(**t, r0.L, pos0, 64));
            const std::string nm = fmt("keylock %+.0f %% drums (%.1f kHz): one sharp attack per hit", pct, (*t)->sr / 1000);
            check(h.hits > 20 && h.doubled == 0 && h.meanEcho < -20 && h.worstPeak > -1.5 && h.meanPeak > -0.75, nm.c_str(),
                  fmt("%.0f hits, %.0f doubled; echo worst %+.1f", (double) h.hits, (double) h.doubled, h.worstEcho) + fmt(" / mean %+.1f dB; peak worst %.2f / mean %.2f dB", h.meanEcho, h.worstPeak, h.meanPeak));
        }
    }
    {   // pitch 0: the key lock is transparent, onsets and all: the grains continue each other sample for sample, so
        // the output is the source (read at the grains' fractional offset, through the deck's 5 Hz DC blocker)
        auto t = drumTrack(48000);
        Deck d; fresh(d, t);
        d.set.startTime = 0.05f; d.set.keyLock = true;
        d.seek(1.0); d.playPause(); run(d, 0.5);
        const double p0 = std::floor(d.st.pos.load() * 48000);
        Rec r; run(d, 3.0, &r);
        const float* x = t->L.data();
        auto herm = [x](double ps) {
            const long i = (long) std::floor(ps); const float u = (float) (ps - (double) i); const float* q = x + i - 1;
            const float c = (q[2] - q[0]) * 0.5f, v = q[1] - q[2], w = c + v, a = w + v + (q[3] - q[1]) * 0.5f, b = w + a;
            return ((a * u - b) * u + c) * u + q[1];
        };
        long bo = 0; double bc = -1e30;   // where the grain stream sits relative to the timeline (within +-12 ms)
        for (long off = -600; off <= 600; ++off) {
            double c = 0; for (size_t k = 0; k < 30000; k += 2) c += (double) r.L[k] * x[(size_t) ((long) p0 + off + (long) k)];
            if (c > bc) { bc = c; bo = off; }
        }
        const float R = (float) (1.0 - 2 * kPi * 5.0 / kHost);
        auto resid = [&](double fr) {
            float x1 = 0, y1 = 0; double se = 0, ss = 0;
            for (long k = -48000; k < (long) r.L.size(); ++k) {
                const float v = herm(p0 + (double) bo + fr + (double) k), y = v - x1 + R * y1; x1 = v; y1 = y;
                if (k >= 4800) { const double e = (double) r.L[(size_t) k] - y; se += e * e; ss += (double) y * y; }
            }
            return 10 * std::log10(se / ss + 1e-30);
        };
        double best = 1e9, bf = 0;
        for (double fr = -1; fr <= 1; fr += 0.01) { const double v = resid(fr); if (v < best) { best = v; bf = fr; } }
        for (double fr = bf - 0.01; fr <= bf + 0.01; fr += 0.0002) best = std::min(best, resid(fr));
        check(best < -80, "keylock at pitch 0 is transparent (drums, onsets)", fmt("residual %.1f dB", best));
    }
    if (!wavDir.empty()) {
        Rec all;
        for (double pct : { 8.0, 16.0, -8.0, -16.0 }) {
            Rec r; std::vector<double> posAt;
            keylockRender(t44, pct, 4.0, r, posAt);
            all.L.insert(all.L.end(), r.L.begin(), r.L.end()); all.R.insert(all.R.end(), r.R.begin(), r.R.end());
            all.L.insert(all.L.end(), 24000, 0.0f); all.R.insert(all.R.end(), 24000, 0.0f);
        }
        writeWav(wavDir + "/wheel_keylock_drums.wav", all.L, all.R, (int) kHost);
        Rec m; std::vector<double> posAt;
        keylockRender(musicTrack(), 12.0, 8.0, m, posAt);
        writeWav(wavDir + "/wheel_keylock_music_+12.wav", m.L, m.R, (int) kHost);
    }
}

static void testAntiAlias(const std::string& wavDir) {
    // x3 hand drag of a 8.5 .. 20 kHz sweep (44.1 kHz): all of it lands above the output Nyquist (25.5 .. 60 kHz),
    // so a clean resampler outputs (almost) nothing; a plain interpolator folds it back as audible whistles.
    const double ref = 0.5 / std::sqrt(2.0);
    const double a3 = 20 * std::log10(dragRms(sweepTrack(8500, 20000), 3.0, 1.5) / ref);
    check(a3 < -50, "x3 scratch: aliasing of an 8.5-20 kHz sweep", fmt("%.1f dB re the input", a3));
    // a tone inside the band still comes through at x3 (2 kHz -> 6 kHz), and a x1 drag is unchanged (Hermite path)
    const double in3 = 20 * std::log10(dragRms(sineTrack(2000, 6, 120, 0, 44100), 3.0, 1.0) / ref);
    const double in1 = 20 * std::log10(dragRms(sineTrack(6000, 6, 120, 0, 44100), 1.0, 1.0) / ref);
    check(std::abs(in3) < 0.5 && std::abs(in1) < 1.5, "x3 scratch keeps the passband (2 kHz -> 6 kHz)", fmt("x3 %.2f dB, x1 %.2f dB", in3, in1));
    // 45 rpm + 50 % pitch (x2.025): a 12-20 kHz sweep (-> 24.3-40.5 kHz) on the motor, no hand
    {
        Deck d; fresh(d, sweepTrack(12000, 20000));
        d.set.rpm45 = true; d.set.pitchRange = 0.5f; d.set.pitch = 1; d.set.startTime = 0.05f;
        d.seek(0.5); d.playPause(); run(d, 0.3);
        Rec r; run(d, 1.5, &r);
        const double a2 = 20 * std::log10(rms(r.L.data(), r.L.size()) / ref);
        check(a2 < -50, "45 rpm +50 %: aliasing of a 12-20 kHz sweep", fmt("%.1f dB re the input", a2));
    }
    if (!wavDir.empty()) {   // a x3 baby scratch on the music: forward-back at up to x3
        Deck m; fresh(m, musicTrack());
        m.set.startTime = 0.05f; m.seek(4.0); m.playPause();
        Rec rec; run(m, 1.0, &rec);
        m.touch(true);
        double ph = 0, last = 0;
        run(m, 3.0, &rec, 64, [&](int) {
            ph += 3.0 * 64 / kHost;
            const double x = 0.2 * (0.5 - 0.5 * std::cos(2 * kPi * ph)) / (kPi * 0.5) * 3.0;   // peak speed ~ x3
            m.move(x - last); last = x;
        });
        m.touch(false); run(m, 1.0, &rec);
        Rec sw; dragRms(sweepTrack(8500, 20000), 3.0, 2.0, &sw);
        rec.L.insert(rec.L.end(), sw.L.begin(), sw.L.end()); rec.R.insert(rec.R.end(), sw.R.begin(), sw.R.end());
        writeWav(wavDir + "/wheel_scratch_x3.wav", rec.L, rec.R, (int) kHost);
    }
}

static void testRapidJumps() {
    // jumps closer together than the 4 ms crossfade (hot-cue drumming, a cue right before a loop wrap) stay smooth
    const double limit = 0.05;   // as testClickFree: a 440 Hz sine at 0.5 has steps of 0.029
    Deck d; fresh(d, sineTrack());
    d.set.startTime = 0.05f;
    d.seek(3.0); d.playPause(); run(d, 0.3);
    Rec rec;
    const int gaps[] = { 16, 48, 64, 96, 128, 160, 200 };
    int j = 0;
    for (int g : gaps) {
        for (int rep = 0; rep < 3; ++rep, ++j) {
            d.seek(2.0 + 0.3713 * j); run(d, g / kHost, &rec, g);
            d.seek(9.0 + 0.2911 * j); run(d, g / kHost, &rec, g);
            d.seek(15.0 + 0.1777 * j); run(d, 0.03, &rec);
        }
    }
    const double m = maxDelta(rec.L);
    check(m < limit, "rapid double / triple jumps (0.3-4 ms apart) are click-free", fmt("max step %.4f", m));
    // the same under key lock
    d.set.keyLock = true; d.set.pitch = 0.5f; run(d, 0.5);
    Rec k;
    for (int g : gaps) {
        for (int rep = 0; rep < 2; ++rep, ++j) {
            d.seek(2.0 + 0.3713 * j); run(d, g / kHost, &k, g);
            d.seek(9.0 + 0.2911 * j); run(d, g / kHost, &k, g);
            d.seek(15.0 + 0.1777 * j); run(d, 0.06, &k);
        }
    }
    const double mk = maxDelta(k.L);
    check(mk < limit * 1.08, "rapid jumps under key lock are click-free", fmt("max step %.4f", mk));
}

static void testHalfDoubleSync() {
    auto lockErr = [](Deck& master, Deck& slave, int mode) {
        slave.setSync(true);
        std::vector<float> l(64), r(64);
        for (int k = 0; k < (int) (3.0 * kHost / 64); ++k) {
            slave.syncTo(master.currentBpm(), master.beatPhase(), true);
            master.render(l.data(), r.data(), 64); slave.render(l.data(), r.data(), 64);
        }
        const double pm = master.beatPhase(), ps = slave.beatPhase();
        auto fr = [](double x) { return x - std::floor(x); };
        double e = mode == 1 ? fr(2 * ps) - pm : fr(2 * pm) - ps;
        e -= std::round(e);
        return std::abs(e);
    };
    {   // 62 bpm against 123.6: half time, the slave's half beats on the master's beats
        Deck a, b; fresh(a, clickTrack(120)); fresh(b, clickTrack(62, 30, 0.37));
        a.set.startTime = b.set.startTime = 0.05f; a.set.pitch = 0.375f;
        a.playPause(); b.playPause(); run(a, 0.5); run(b, 0.7);
        const double e = lockErr(a, b, 1);
        check(e < 0.01 && std::abs(b.currentBpm() - 61.8) < 0.05, "half-time sync locks the phase", fmt("error %.4f beats, bpm %.3f", e, b.currentBpm()));
    }
    {   // 128 bpm against 62: double time, two slave beats per master beat
        Deck a, b; fresh(a, clickTrack(62)); fresh(b, clickTrack(128, 30, 0.21));
        a.set.startTime = b.set.startTime = 0.05f;
        a.playPause(); b.playPause(); run(a, 0.5); run(b, 0.9);
        const double e = lockErr(a, b, 2);
        check(e < 0.01 && std::abs(b.currentBpm() - 124) < 0.05, "double-time sync locks the phase", fmt("error %.4f beats, bpm %.3f", e, b.currentBpm()));
    }
}

static void testPerformance() {
    Deck a, b; Fx fa, fb; Mixer m;
    fresh(a, musicTrack(120, 30)); fresh(b, musicTrack(124, 30));
    fa.prepare(kHost, 256); fb.prepare(kHost, 256); m.prepare(kHost, 256);
    a.set.keyLock = b.set.keyLock = true; a.set.pitch = 0.6f; b.set.pitch = -0.4f; a.set.startTime = b.set.startTime = 0.05f;
    a.playPause(); b.playPause();
    FxSettings fs; fs.type = FxType::echo; fs.on = true;
    FxSettings fr; fr.type = FxType::reverb; fr.on = true;
    MixerSettings ms; ms.ch[0].filter = -0.4f;
    const int n = 256; const double secs = 25;
    std::vector<float> aL(n), aR(n), bL(n), bR(n), oL(n), oR(n), cL(n), cR(n);
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < (int) (secs * kHost / n); ++k) {
        a.render(aL.data(), aR.data(), n); b.render(bL.data(), bR.data(), n);
        fa.process(aL.data(), aR.data(), n, fs, a.currentBpm()); fb.process(bL.data(), bR.data(), n, fr, b.currentBpm());
        m.process(aL.data(), aR.data(), bL.data(), bR.data(), oL.data(), oR.data(), cL.data(), cR.data(), n, ms);
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check(secs / el > 20, "realtime: 2 keylocked decks + echo + reverb + mixer", fmt("%.0fx realtime (%.2f%% of a core)", secs / el, 100 * el / secs));
}

int main(int argc, char** argv) {
    const std::string wavDir = argc > 1 ? argv[1] : "";
    testMotor();
    testCues();
    testLoops();
    testScratchSlip(wavDir);
    testClickFree();
    testKeylock(wavDir);
    testSync();
    testMixer();
    testFx(wavDir);
    testKeylockTransients(wavDir);
    testAntiAlias(wavDir);
    testRapidJumps();
    testHalfDoubleSync();
    testPerformance();
    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
