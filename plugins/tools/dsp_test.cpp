// Runs every engine offline (no plugin host): checks for silence, NaN and runaway levels, and writes WAVs.
// Usage: omega_dsp_test [outdir] [only]
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include "../dsp/Prophet5.h"
#include "../dsp/Rack.h"
#include "../dsp/Vocoder.h"
#include "../src/Specs.h"

using namespace omega;

static void wav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
    std::ofstream f(path, std::ios::binary); const uint32_t n = (uint32_t) L.size(), bytes = n * 4;
    auto u32 = [&](uint32_t v) { f.write((const char*) &v, 4); }; auto u16 = [&](uint16_t v) { f.write((const char*) &v, 2); };
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32((uint32_t) sr); u32((uint32_t) sr * 4); u16(4); u16(16); f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < n; ++i) for (float x : {L[i], R[i]}) { const int16_t s = (int16_t) std::lround(std::max(-1.f, std::min(1.f, x)) * 32767); f.write((const char*) &s, 2); }
}

struct Case { std::string name; std::function<Spec()> spec; bool synth; std::string text; };

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "."; const std::string only = argc > 2 ? argv[2] : "";
    const int sr = 48000, block = 256;
    auto rack = [](void (*f)(Spec&), std::function<std::unique_ptr<Engine>(const Spec&)> mk, bool mix = true) {
        return [=] { Spec s; f(s); if (mix) s.knob("omix", "Dry/Wet", 0, 1, .01, 1, Fmt::pct); s.make = mk; return s; }; };
#define R(K, C) rack(gen::rack_##K, [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::C(sp)); })
    std::vector<Case> cases = {
        {"Prophet5", [] { Spec s; gen::prophet5(s); s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Prophet5(sp)); }; return s; }, true, ""},
        {"Speech", [] { Spec s; addSpeech(s, true); s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Speech(sp)); }; return s; }, true, "Ahhhh, this stuff is really fresh."},
        {"Vox", [] { Spec s; addSpeech(s, false); s.choice("modsrc", "m", {{"a", 0}, {"b", 1}, {"c", 2}}, 0); addVocoder(s); Spec p; gen::prophet5(p); for (auto& d : p.params) s.params.push_back(d);
                     for (const auto& pr : p.presets) if (pr.name == "Vocoder carrier") for (auto& [k, x] : pr.values) if (auto* d = s.find(k)) d->def = (float) x;
                     s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::Vox(sp)); }; return s; }, true, "I am the Omega Vox."},
        {"Vocoder", [] { Spec s; s.choice("carrier", "c", {{"a", 0}, {"b", 1}}, 0); s.choice("drone", "d", {{"off", 0}, {"C2", 48}}, 1); s.knob("ccut", "", 0, 1, 0, .95, Fmt::pct); s.knob("cnoise", "", 0, 1, 0, .03, Fmt::pct); addVocoder(s);
                         s.make = [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::SVC350Fx(sp)); }; return s; }, false, ""},
        {"Lexicon224", R(lx, Verb224), false, ""}, {"PrimeTime", R(pt, PrimeTime), false, ""}, {"Effectron", R(ef, Effectron), false, ""}, {"DP4", R(dp, DP4), false, ""},
        {"Microverb", R(mv, Microverb), false, ""}, {"ADAFlanger", R(ad, ADAFlanger), false, ""}, {"UREI1176", rack(gen::rack_u7, [](const Spec& sp) { return std::unique_ptr<Engine>(new dsp::UREI1176(sp)); }, false), false, ""},
        {"PhilCollins", R(pc, PCSim), false, ""}, {"H910", R(h9, H910), false, ""}, {"MXR113", R(mx, MXR113), false, ""},
    };
    int fails = 0;
    for (auto& c : cases) {
        if (!only.empty() && c.name != only) continue;
        Spec s = c.spec(); auto e = s.make(s);
        if (!c.text.empty()) e->setText(c.text);
        e->prepare(sr, block);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));   // let the speech render
        const int secs = 4, n = sr * secs; std::vector<float> L((size_t) n), Rr((size_t) n), inL((size_t) n), inR((size_t) n);
        // test input: drum-like hits plus a voice-ish buzz (for the vocoder) in the first two seconds
        dsp::Rand rnd;
        for (int i = 0; i < n; ++i) {
            const double t = (double) i / sr; double x = 0;
            const double ph = std::fmod(t, .5); if (t < 2) x = std::sin(2 * dsp::PI * 110 * t) * std::exp(-ph * 8) * .4 + std::sin(2 * dsp::PI * 1830 * t) * std::exp(-ph * 30) * .3;
            if (t < 2 && c.name == "Vocoder") { const double f0 = 120, saw = 2 * std::fmod(t * f0, 1.0) - 1; x = saw * .3 * (.5 + .5 * std::sin(2 * dsp::PI * 3 * t)); }
            inL[(size_t) i] = inR[(size_t) i] = (float) x;
        }
        Transport tr; tr.playing = true;
        for (int pos = 0; pos < n; pos += block) {
            const int len = std::min(block, n - pos);
            if (c.synth || c.name == "Vocoder") {
                if (pos == 0) { e->noteOn(60, .9f); e->noteOn(64, .9f); e->noteOn(67, .9f); }
                if (pos <= sr * 2 && pos + len > sr * 2) { e->noteOff(60); e->noteOff(64); e->noteOff(67); }
            }
            e->process(c.synth ? nullptr : inL.data() + pos, c.synth ? nullptr : inR.data() + pos, nullptr, nullptr, L.data() + pos, Rr.data() + pos, len, tr);
        }
        double rms = 0, pk = 0; bool nan = false;
        for (int i = 0; i < n; ++i) { for (float x : {L[(size_t) i], Rr[(size_t) i]}) { if (!std::isfinite(x)) nan = true; rms += x * x; pk = std::max(pk, (double) std::fabs(x)); } }
        rms = std::sqrt(rms / (2.0 * n));
        const bool bad = nan || rms < 1e-4 || pk > 4;
        fails += bad;
        std::printf("%-12s rms %.4f  peak %.3f %s\n", c.name.c_str(), rms, pk, nan ? "NaN!" : bad ? "BAD" : "ok");
        wav(out + "/" + c.name + ".wav", L, Rr, sr);
    }
    return fails ? 1 : 0;
}
