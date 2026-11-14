// Omega DMX engine test (no JUCE): loads the kits from dmx/kits, plays factory patterns and checks timing, accents,
// swing, levels, the open-hat choke, the keypad, song mode and live recording against what dmx/app.js does.
// usage: dmx_test <repo>/dmx/kits
#include "DmxEngine.h"
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace dmx;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void load(Engine& e, const std::string& dir) {
    for (int k = 0; k < kKits; ++k) {
        e.samples[(size_t) k].resize((size_t) kKitDefs[k].voices);
        for (int v = 0; v < kKitDefs[k].voices; ++v) {
            std::ifstream f(dir + "/" + kKitDefs[k].id + "/" + kKitDefs[k].v[v].file + ".wav", std::ios::binary);
            std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
            CHECK(!b.empty() && readWav(b.data(), b.size(), e.samples[(size_t) k][(size_t) v]), "load %s/%s", kKitDefs[k].id, kKitDefs[k].v[v].file);
        }
    }
}
// render `secs` with the engine's own clock, in blocks of 256
static std::vector<float> render(Engine& e, double secs, double sr, Engine::Host h = {}) {
    std::vector<float> out((size_t) (secs * sr)); const int B = 256;
    for (size_t i = 0; i < out.size(); i += B) { const int n = (int) std::min<size_t>(B, out.size() - i); Engine::Host hh = h; if (h.playing) hh.ppq = h.ppq + (double) i / sr * h.bpm / 60; e.process(out.data() + i, nullptr, n, hh); }
    return out;
}
static std::vector<size_t> onsets(const std::vector<float>& x, float thr) {
    // a hit = the first sample over thr after 3000 samples without one; the onset is where it rose out of silence
    std::vector<size_t> o; long last = -100000;
    for (size_t i = 0; i < x.size(); ++i) if (std::fabs(x[i]) > thr) { if ((long) i - last > 3000) { size_t j = i; while (j > 0 && i - j < 600 && std::fabs(x[j - 1]) > 1e-4f) --j; o.push_back(j); } last = (long) i; }
    return o;
}
static float peak(const std::vector<float>& x, size_t a, size_t b) { float p = 0; for (size_t i = a; i < std::min(b, x.size()); ++i) p = std::max(p, std::fabs(x[i])); return p; }

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "dmx/kits";
    const double sr = 48000;
    {   // Boom Bap on the DMX kit, kick only: hits on steps 0 (accent), 7, 10 at 120 BPM = 6000 samples a step
        Engine e; load(e, dir); e.prepare(sr); e.tempo = 120;
        for (int g = 1; g < kVol; ++g) e.fader[g] = 0;
        e.internalPlay = true; auto x = render(e, 4.0, sr);
        auto o = onsets(x, .02f);
        std::vector<size_t> want = {0, 42000, 60000, 96000, 138000, 156000};
        CHECK(o.size() == want.size(), "kick onsets: %zu", o.size());
        for (size_t i = 0; i < std::min(o.size(), want.size()); ++i) CHECK(o[i] + 64 >= want[i] && o[i] <= want[i] + 64, "kick %zu at %zu, want %zu", i, o[i], want[i]);
        // levels: accent 1.0, hit .72; through the Bass fader (.8) and Volume (.85) with the app's curve
        const float raw = peak(e.samples[0][0].d, 0, e.samples[0][0].d.size()), g = fgain(.8f) * fgain(.85f);
        CHECK(std::fabs(peak(x, 0, 4000) - raw * g) < .02f, "accent level %f want %f", peak(x, 0, 4000), raw * g);
        CHECK(std::fabs(peak(x, 42000, 46000) - raw * g * .72f) < .02f, "hit level %f want %f", peak(x, 42000, 46000), raw * g * .72f);
    }
    {   // swing 66%: odd steps late by a third of a step (2000 samples at 120 BPM)
        Engine e; load(e, dir); e.prepare(sr); e.tempo = 120; e.swing = 66;
        for (int g = 1; g < kVol; ++g) e.fader[g] = 0;
        e.internalPlay = true; auto x = render(e, 1.0, sr); auto o = onsets(x, .02f);
        CHECK(o.size() >= 2 && o[1] + 64 >= 42000 + 1920 && o[1] <= 42000 + 1920 + 64, "swung step 7 at %zu", o.size() > 1 ? o[1] : 0);
    }
    {   // the choke: an open hat stops when a closed hat plays
        Engine e; load(e, dir); e.prepare(sr); for (auto& P : e.pats[0]) std::memset(P.rows, 0, sizeof P.rows);
        e.pattern(0).rows[6][0] = 2; e.internalPlay = true; auto a = render(e, .3, sr);
        Engine f; load(f, dir); f.prepare(sr); for (auto& P : f.pats[0]) std::memset(P.rows, 0, sizeof P.rows);
        f.pattern(0).rows[6][0] = 2; f.fader[2] = .8f; f.internalPlay = true; f.tempo = 120;
        f.pattern(0).rows[4][1] = 1; auto b = render(f, .5, sr);
        // after the closed hat (6000) plus its own length, the open hat is gone in b but still rings in a
        const float ringA = peak(a, 9000, 12000), ringB = peak(b, 9000, 12000);
        CHECK(ringA > .01f && ringB < ringA * .5f, "choke: open %f, after closed %f", ringA, ringB);
    }
    {   // keypad and song: pattern 3 picked while stopped; a song 1 2 plays pattern 1 then 2 (bars of 96000 samples)
        Engine e; load(e, dir); e.prepare(sr); e.tempo = 120;
        e.keypad('3'); CHECK(e.current() == 2, "keypad 3 -> %d", e.current());
        CHECK(e.led() == "03 120", "led '%s'", e.led().c_str());
        e.fn("song"); e.keypad('1'); e.keypad('2'); e.fn("song");
        CHECK(e.songList().size() == 2 && e.useSong[0], "song");
        e.internalPlay = true; render(e, .5, sr); CHECK(e.playingPattern() == 0, "song bar 1 plays %d", e.playingPattern());
        render(e, 2.5, sr); CHECK(e.playingPattern() == 1, "song bar 2 plays %d", e.playingPattern());
        e.fn("tempo"); e.keypad('1'); e.keypad('3'); e.keypad('0'); CHECK(e.tempo == 130, "tempo typed %f", e.tempo);
    }
    {   // live record: a snare played on beat 2 (step 4) lands on step 4 of the playing pattern
        Engine e; load(e, dir); e.prepare(sr); e.tempo = 120; for (auto& P : e.pats[0]) std::memset(P.rows, 0, sizeof P.rows);
        e.fn("rec"); e.internalPlay = true; render(e, .49, sr);
        e.hit(1, 1, 100); render(e, .1, sr);
        CHECK(e.pattern(0).rows[1][4] == 1, "recorded snare at step 4: %d %d %d", e.pattern(0).rows[1][3], e.pattern(0).rows[1][4], e.pattern(0).rows[1][5]);
    }
    {   // follows the host: at bar 2 beat 1 (ppq 4) the kick plays at once
        Engine e; load(e, dir); e.prepare(sr); for (int g = 1; g < kVol; ++g) e.fader[g] = 0;
        auto x = render(e, .1, sr, {true, 4.0, 90}); CHECK(peak(x, 0, 2000) > .1f, "host sync kick %f", peak(x, 0, 2000));
        CHECK(e.tempo == 90, "tempo from host %f", e.tempo);
    }
    {   // every kit loads and every factory pattern makes sound on every kit
        Engine e; load(e, dir); e.prepare(sr);
        for (int k = 0; k < kKits; ++k) for (int p = 0; p < kPatterns; ++p) {
            Engine f; f.samples = e.samples; f.prepare(sr); f.setKit(k); f.keypad(p == 9 ? '0' : (char) ('1' + p)); f.internalPlay = true;
            auto x = render(f, .6, sr); CHECK(peak(x, 0, x.size()) > .05f, "kit %s pattern %d silent", kKitDefs[k].id, p + 1);
        }
    }
    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
