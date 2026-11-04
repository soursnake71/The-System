// Omega Drum Trigger test bench: synthesized close-mic metal drum tracks with known hit positions, run through the
// detector at several sample rates, in the plugin's streaming mode (random block sizes) and in the offline file
// analysis, which must agree exactly. Pass: no missed and no false notes in any scenario, no note more than 2.5 ms
// from the real start, and no more than 1% of notes more than 1 ms off. Exit code 0 = pass. Build: part of plugins/CMakeLists.txt (trigger_test), or
//   g++ -O2 -std=c++17 plugins/trigger/test.cpp -o trigger_test
#include "Detector.h"
#include "Presets.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

using namespace trig;
static const double PI = 3.14159265358979323846;

struct Track {
    std::vector<float> x;
    std::vector<int64_t> truth;     // onset sample of each hit that must trigger
    std::vector<float> gainDb;      // its level
};

struct Synth {
    double sr; std::mt19937 rng;
    explicit Synth(double s, unsigned seed) : sr(s), rng(seed) {}
    float uni(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
    float noise() { return uni(-1, 1); }

    // close-mic metal kick: pitch-dropping body + beater click; tight (gated/damped) like a metal kick
    void kick(std::vector<float>& y, int64_t at, float g) {
        const float f0 = 105 * uni(0.95f, 1.05f), f1 = 52 * uni(0.95f, 1.05f), click = 0.7f * uni(0.75f, 1.25f);
        const float tau = 0.040f * uni(0.9f, 1.1f);
        double ph = 0; float hpPrev = 0, nPrev = 0;
        const int64_t len = (int64_t) (0.25 * sr);
        for (int64_t i = 0; i < len && at + i < (int64_t) y.size(); ++i) {
            const double t = i / sr;
            const double f = f1 + (f0 - f1) * std::exp(-t / 0.018);
            ph += 2 * PI * f / sr;
            const double atk = std::min(1.0, t / 0.0004);
            const double body = std::sin(ph) * std::exp(-t / tau) * atk;
            const float n = noise(); const float hp = n - nPrev; nPrev = n;      // bright noise for the beater
            hpPrev = 0.6f * hpPrev + 0.4f * hp;
            const double cl = (hpPrev * 0.8 + 0.5 * std::sin(2 * PI * 3200 * t)) * std::exp(-t / 0.0015) * click;
            y[(size_t) (at + i)] += (float) (g * (0.8 * body + cl));
        }
    }
    // snare: two shell modes + wire noise
    void snare(std::vector<float>& y, int64_t at, float g) {
        const float tau = 0.07f * uni(0.9f, 1.1f), wires = uni(0.8f, 1.2f);
        float lp = 0, prev = 0;
        const int64_t len = (int64_t) (0.4 * sr);
        for (int64_t i = 0; i < len && at + i < (int64_t) y.size(); ++i) {
            const double t = i / sr;
            const double atk = std::min(1.0, t / 0.0003);
            const double shell = (0.6 * std::sin(2 * PI * 195 * t) + 0.35 * std::sin(2 * PI * 340 * t)) * std::exp(-t / 0.045);
            const float n = noise(); lp = 0.5f * lp + 0.5f * (n - prev); prev = n;
            const double w = lp * 1.4 * wires * std::exp(-t / tau);
            const double crack = noise() * std::exp(-t / 0.002) * 0.6;
            y[(size_t) (at + i)] += (float) (g * atk * (shell + w + crack));
        }
    }
    // hi-hat / cymbal: bright noise
    void hat(std::vector<float>& y, int64_t at, float g, float tau) {
        float prev = 0, p2 = 0;
        const int64_t len = (int64_t) (tau * 6 * sr);
        for (int64_t i = 0; i < len && at + i < (int64_t) y.size(); ++i) {
            const double t = i / sr;
            const float n = noise(); const float d = n - prev; prev = n; const float dd = d - p2; p2 = d;
            y[(size_t) (at + i)] += (float) (g * 0.5 * dd * std::exp(-t / tau));
        }
    }
    void floorNoise(std::vector<float>& y, float g) { for (auto& v : y) v += g * noise(); }
};

static float db(float d) { return std::pow(10.f, d / 20.f); }

struct Scenario {
    std::string name; Track trk; std::vector<float> side; Settings s;
    bool learn = true;   // set the threshold the way the Learn button does
};

// ---- the scenarios ----
// every hit is placed on a grid with a little human timing; levels vary like a real drummer's

static Scenario doubleKick(double sr, unsigned seed, double bpm, int div, float velLo, const char* name, float snBleed = -26) {
    Synth sy(sr, seed); Scenario sc; sc.name = name;
    const double len = 6.0; sc.trk.x.assign((size_t) (len * sr), 0.f);
    std::vector<float> sn((size_t) (len * sr), 0.f), cym((size_t) (len * sr), 0.f);
    const double step = 60.0 / bpm / (div / 4.0);
    for (double t = 0.2; t < len - 0.4; t += step) {
        const int64_t at = (int64_t) ((t + sy.uni(-0.002f, 0.002f)) * sr);
        const float gd = sy.uni(velLo, 0);
        sy.kick(sc.trk.x, at, db(gd));
        sc.trk.truth.push_back(at); sc.trk.gainDb.push_back(gd);
    }
    // snare on 2 and 4 and cymbals on every 8th, bleeding into the kick mic
    for (double t = 0.2 + 60 / bpm; t < len - 0.4; t += 120 / bpm) sy.snare(sn, (int64_t) (t * sr), 1.f);
    for (double t = 0.2; t < len - 0.4; t += 30 / bpm) sy.hat(cym, (int64_t) (t * sr), 0.8f, 0.25f);
    for (size_t i = 0; i < sc.trk.x.size(); ++i) sc.trk.x[i] += db(snBleed) * sn[i] + db(-32) * cym[i];
    sy.floorNoise(sc.trk.x, db(-72));
    sc.side = sn; for (size_t i = 0; i < sc.side.size(); ++i) sc.side[i] += db(-14) * sc.trk.x[i];
    sc.s = presetSettings(Preset::kick);
    return sc;
}

static Scenario blast(double sr, unsigned seed, double bpm, bool snareTrack, const char* name, float snBleed = -26) {
    Synth sy(sr, seed); Scenario sc; sc.name = name;
    const double len = 6.0; const size_t N = (size_t) (len * sr);
    std::vector<float> k(N, 0.f), s(N, 0.f), h(N, 0.f);
    std::vector<int64_t> kt, st; std::vector<float> kg, sg;
    const double step = 60.0 / bpm / 4;   // 16ths, kick and snare alternating (traditional blast)
    int i = 0;
    for (double t = 0.2; t < len - 0.5; t += step, ++i) {
        const int64_t at = (int64_t) ((t + sy.uni(-0.0015f, 0.0015f)) * sr);
        const float gd = sy.uni(-7, 0);
        if (i % 2 == 0) { sy.kick(k, at, db(gd)); kt.push_back(at); kg.push_back(gd); }
        else { sy.snare(s, at, db(gd)); st.push_back(at); sg.push_back(gd); }
        sy.hat(h, at, 0.9f, 0.12f);   // ride / china on every 16th
    }
    sc.trk.x.assign(N, 0.f);
    if (snareTrack) {
        for (size_t j = 0; j < N; ++j) sc.trk.x[j] = s[j] + db(-16) * k[j] + db(-22) * h[j];
        sc.trk.truth = st; sc.trk.gainDb = sg; sc.side = h;
        sc.s = presetSettings(Preset::snare);
    } else {
        for (size_t j = 0; j < N; ++j) sc.trk.x[j] = k[j] + db(snBleed) * s[j] + db(-32) * h[j];
        sc.trk.truth = kt; sc.trk.gainDb = kg; sc.side = s;
        sc.s = presetSettings(Preset::kick);
    }
    sy.floorNoise(sc.trk.x, db(-72));
    return sc;
}

// hammer blast: kick and snare struck together on every 8th, kick mic with the snare in it at -22 dB;
// the snare mic is the sidechain and hears the kick 20 dB down
static Scenario hammer(double sr, unsigned seed, double bpm, const char* name) {
    Synth sy(sr, seed); Scenario sc; sc.name = name;
    const double len = 6.0; const size_t N = (size_t) (len * sr);
    std::vector<float> k(N, 0.f), s(N, 0.f), h(N, 0.f);
    for (double t = 0.2; t < len - 0.5; t += 30.0 / bpm) {
        const int64_t at = (int64_t) ((t + sy.uni(-0.0015f, 0.0015f)) * sr);
        const int64_t as = (int64_t) ((t + sy.uni(-0.0015f, 0.0015f)) * sr);
        const float gk = sy.uni(-8, 0), gs = sy.uni(-4, 0);
        sy.kick(k, at, db(gk)); sc.trk.truth.push_back(at); sc.trk.gainDb.push_back(gk);
        sy.snare(s, as, db(gs));
        sy.hat(h, at, 0.9f, 0.12f);
    }
    sc.trk.x.assign(N, 0.f); sc.side.assign(N, 0.f);
    for (size_t j = 0; j < N; ++j) { sc.trk.x[j] = k[j] + db(-22) * s[j] + db(-32) * h[j]; sc.side[j] = s[j] + db(-20) * k[j] + db(-22) * h[j]; }
    sy.floorNoise(sc.trk.x, db(-72));
    sc.s = presetSettings(Preset::kick);
    return sc;
}

// snare track with constant 16th hats bleeding in hard, snare on the backbeat with a wide dynamic range.
// hatDb: the hat bleed level. The sidechain is the hat mic: hats at 0 dB, the snare 14 dB down, the kick 20 down.
static Scenario snareHats(double sr, unsigned seed, const char* name, float hatDb = -16) {
    Synth sy(sr, seed); Scenario sc; sc.name = name;
    const double len = 6.0, bpm = 200; const size_t N = (size_t) (len * sr);
    std::vector<float> s(N, 0.f), h(N, 0.f), k(N, 0.f);
    for (double t = 0.2; t < len - 0.5; t += 15 / bpm) sy.hat(h, (int64_t) ((t + sy.uni(-0.002f, 0.002f)) * sr), sy.uni(0.6f, 1.f), 0.05f);
    for (double t = 0.2; t < len - 0.5; t += 30 / bpm) sy.kick(k, (int64_t) (t * sr), 1.f);
    for (double t = 0.2 + 60 / bpm; t < len - 0.5; t += 60 / bpm) {
        const int64_t at = (int64_t) ((t + sy.uni(-0.002f, 0.002f)) * sr);
        const float gd = sy.uni(-10, 0);
        sy.snare(s, at, db(gd)); sc.trk.truth.push_back(at); sc.trk.gainDb.push_back(gd);
    }
    sc.trk.x.assign(N, 0.f); sc.side.assign(N, 0.f);
    for (size_t j = 0; j < N; ++j) {
        sc.trk.x[j] = s[j] + db(hatDb) * h[j] + db(-18) * k[j];
        sc.side[j] = h[j] + db(-20) * k[j] + db(-14) * s[j];
    }
    sy.floorNoise(sc.trk.x, db(-72));
    sc.s = presetSettings(Preset::snare);
    return sc;
}

// ---- checking ----
// A detected hit within 5 ms of a played one is that hit (5 ms is well under the 25 ms between the fastest real
// strokes); then: missed = a played hit with no note, extra = a note with no played hit, and the timing error of
// every matched note. "off" counts notes placed more than tolMs from the real start.
struct Result { int missed = 0, extra = 0, off = 0, matched = 0; double maxErrMs = 0, meanErrMs = 0, velSpreadDb = 0; };

static Result check(const Scenario& sc, const std::vector<Hit>& hits, double sr, double tolMs, bool verbose = true) {
    Result r; std::vector<bool> used(hits.size(), false);
    std::vector<double> dv;
    double sum = 0;
    for (size_t i = 0; i < sc.trk.truth.size(); ++i) {
        const int64_t t = sc.trk.truth[i];
        int best = -1; int64_t bd = INT64_MAX;
        for (size_t j = 0; j < hits.size(); ++j) {
            if (used[j]) continue;
            const int64_t d = std::llabs(hits[j].onset - t);
            if (d < bd) { bd = d; best = (int) j; }
        }
        const double ms = 1000.0 * (double) bd / sr;
        if (best < 0 || ms > 5.0) { ++r.missed; if (verbose) std::printf("    MISSED hit at %.4f s (%.1f dB)\n", t / sr, sc.trk.gainDb[i]); continue; }
        used[(size_t) best] = true; ++r.matched;
        if (ms > tolMs) { ++r.off; if (verbose) std::printf("    hit at %.4f s (%.1f dB) placed %+.2f ms off\n", t / sr, sc.trk.gainDb[i], 1000.0 * (double) (hits[(size_t) best].onset - t) / sr); }
        r.maxErrMs = std::max(r.maxErrMs, ms); sum += ms;
        dv.push_back(20 * std::log10(hits[(size_t) best].level) - sc.trk.gainDb[i]);
    }
    for (size_t j = 0; j < hits.size(); ++j) if (!used[j]) { ++r.extra; if (verbose) std::printf("    EXTRA note at %.4f s (peak %.1f dB)\n", hits[j].onset / sr, 20 * std::log10(hits[j].peak)); }
    r.meanErrMs = r.matched ? sum / r.matched : 0;
    // velocity tracking: how consistently the measured level follows the played level (spread of the difference)
    if (!dv.empty()) { auto mm = std::minmax_element(dv.begin(), dv.end()); r.velSpreadDb = *mm.second - *mm.first; }
    return r;
}

static int lateDecisions = 0;   // a note decided after its sending time would go out late: must stay 0

// the plugin's way: random block sizes, hits reported as they're decided
static std::vector<Hit> streaming(const Scenario& sc, double sr, unsigned seed) {
    Detector d; d.update(sc.s); d.prepare(sr);
    std::mt19937 rng(seed); std::uniform_int_distribution<int> bs(1, 2048);
    std::vector<Hit> out; const int64_t N = (int64_t) sc.trk.x.size();
    const bool useSide = sc.s.sideOn;
    for (int64_t i = 0; i < N;) {
        const int m = (int) std::min<int64_t>(bs(rng), N - i);
        d.process(sc.trk.x.data() + i, useSide ? sc.side.data() + i : nullptr, m, [&](const Hit& h) {
            // the note must be sendable: decided no later than onset + latency
            if (d.position() > h.onset + d.latency()) { std::printf("    LATE decision: %lld > %lld\n", (long long) d.position(), (long long) (h.onset + d.latency())); ++lateDecisions; }
            if (!h.rejected) out.push_back(h);
        });
        i += m;
    }
    return out;
}

#ifndef TRIGGER_TEST_NO_MAIN
int main() {
    int fails = 0, total = 0, played = 0, missed = 0, extra = 0, off = 0; double worst = 0;
    const double tolMs = 1.0, hardMs = 2.5;
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        std::vector<Scenario> all;
        // TRIGGER_SEED picks another set of random performances (CI runs the default and a couple more)
        unsigned seed = 1 + (std::getenv("TRIGGER_SEED") ? (unsigned) std::atoi(std::getenv("TRIGGER_SEED")) * 1000u : 0u);
        for (double bpm : {180.0, 220.0, 250.0, 280.0}) {
            char nm[64]; std::snprintf(nm, sizeof nm, "kick 16ths %d bpm", (int) bpm);
            all.push_back(doubleKick(sr, seed++, bpm, 16, -6, nm));
        }
        all.push_back(doubleKick(sr, seed++, 200, 32, -8, "kick 32nds 200 bpm (37.5 ms)"));
        all.push_back(doubleKick(sr, seed++, 240, 32, -8, "kick 32nds 240 bpm (31 ms)"));
        all.push_back(doubleKick(sr, seed++, 160, 16, -14, "kick 16ths 160 bpm, 14 dB dynamics"));
        all.push_back(blast(sr, seed++, 250, false, "blast 250 bpm, kick mic"));
        all.push_back(blast(sr, seed++, 250, true, "blast 250 bpm, snare mic"));
        all.push_back(blast(sr, seed++, 300, false, "blast 300 bpm, kick mic"));
        all.push_back(blast(sr, seed++, 300, true, "blast 300 bpm, snare mic"));
        all.push_back(snareHats(sr, seed++, "snare under loud 16th hats"));
        {   // snare bleed in the kick mic as loud as the softest kicks: only the snare mic on the sidechain can sort it
            Scenario sc = blast(sr, seed++, 250, false, "blast 250, brutal snare bleed + sidechain", -16);
            sc.s.sideOn = true; sc.s.sideRatioDb = -12; sc.s.thresholdDb = -45; sc.learn = false;
            all.push_back(sc);
        }
        {   // hammer blast: kick and snare together on every 8th, snare mic on the kick's sidechain. The sidechain jumps
            // hard on every kick too; the kicks must all stay
            Scenario sc = hammer(sr, seed++, 220, "hammer blast 220, snare sidechain");
            sc.s.sideOn = true; sc.s.sideRatioDb = -17; sc.s.thresholdDb = -45; sc.learn = false;   // bleed level just over the real -22
            all.push_back(sc);
        }
        {   // hats nearly as loud in the snare mic as the snare's soft hits, and on every beat with it: the hat mic on
            // the sidechain sorts them (hats jump far more there, the snare far more in the snare mic)
            Scenario sc = snareHats(sr, seed++, "snare, hats at -6 dB + hat-mic sidechain", -6);
            sc.s.thresholdDb = -17; sc.s.sideOn = true; sc.s.sideRatioDb = -3; sc.learn = false;   // above kick bleed + a hat together (-18.6)
            all.push_back(sc);
        }
        std::printf("== %.0f Hz\n", sr);
        for (auto& sc : all) {
            ++total;
            // what the Learn button does: threshold from the track itself (unless the scenario sets one by hand)
            float thr = sc.s.thresholdDb;
            if (sc.learn) {
                auto c = candidatePeaksDb(sc.trk.x.data(), nullptr, (int64_t) sc.trk.x.size(), sr, sc.s);
                if (!autoThreshold(c, thr)) thr = sc.s.thresholdDb;
                sc.s.thresholdDb = thr;
            }
            auto hs = streaming(sc, sr, 99);
            auto ho = analyze(sc.trk.x.data(), sc.s.sideOn ? sc.side.data() : nullptr, (int64_t) sc.trk.x.size(), sr, sc.s);
            Result r = check(sc, hs, sr, tolMs);
            bool same = ho.size() == hs.size();
            for (size_t i = 0; same && i < ho.size(); ++i) same = ho[i].onset == hs[i].onset && ho[i].peak == hs[i].peak;
            const bool ok = r.missed == 0 && r.extra == 0 && r.maxErrMs <= hardMs && same;
            if (!ok) ++fails;
            played += (int) sc.trk.truth.size(); missed += r.missed; extra += r.extra; off += r.off; worst = std::max(worst, r.maxErrMs);
            std::printf("  %-4s %-44s thr %5.1f  notes %3zu/%3zu  missed %d extra %d  >%.0fms %d  timing max %.2f mean %.3f ms  level spread %.1f dB%s\n",
                ok ? "ok" : "FAIL", sc.name.c_str(), thr, hs.size(), sc.trk.truth.size(), r.missed, r.extra, tolMs, r.off, r.maxErrMs, r.meanErrMs,
                r.velSpreadDb, same ? "" : "  (offline != streaming)");
        }
    }
    const double offPct = 100.0 * off / std::max(1, played);
    std::printf("\n%d hits played: %d missed, %d false, %d placed more than %.0f ms off (%.2f%%), worst %.2f ms\n",
                played, missed, extra, off, tolMs, offPct, worst);
    std::printf("%d/%d scenarios passed\n", total - fails, total);
    // pass: no missed and no false notes anywhere, nothing past hardMs, and at most 1% of notes past tolMs
    if (lateDecisions) std::printf("%d notes decided too late to send on time\n", lateDecisions);
    return fails || offPct > 1.0 || lateDecisions ? 1 : 0;
}
#endif
