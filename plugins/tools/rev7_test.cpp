// Renders every Omega REV7 preset offline (no JUCE) and checks it: sound comes out, no NaN, no runaway level,
// and the tail dies away. Usage: rev7_test [outdir]  (writes a WAV per preset when outdir is given)
#include <cstdio>
#include <fstream>
#include "../src/Rev7.cpp"

using namespace omega;

static void wav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
    std::ofstream f(path, std::ios::binary); const uint32_t n = (uint32_t) L.size(), bytes = n * 4;
    auto u32 = [&](uint32_t v) { f.write((const char*) &v, 4); }; auto u16 = [&](uint16_t v) { f.write((const char*) &v, 2); };
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32((uint32_t) sr); u32((uint32_t) sr * 4); u16(4); u16(16); f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < n; ++i) for (float x : {L[i], R[i]}) { const int16_t s = (int16_t) std::lround(std::max(-1.f, std::min(1.f, x)) * 32767); f.write((const char*) &s, 2); }
}

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "";
    Spec s = makeSpec(); int fails = 0;
    for (int sr : {44100, 48000, 96000}) for (size_t pi = 0; pi < s.presets.size(); ++pi) {
        const auto& pr = s.presets[pi];
        std::vector<double> vals; for (const auto& p : s.params) vals.push_back(p.choices.empty() ? p.def : p.choices[(size_t) p.defChoice].value);
        for (const auto& [id, x] : pr.values) vals[(size_t) s.index(id)] = x;
        vals[(size_t) s.index("omix")] = 1;   // wet only
        auto e = s.make(s); e->setValues(vals); e->prepare(sr, 256);
        const int n = sr * 8, block = 256; std::vector<float> inL((size_t) n), L((size_t) n), R((size_t) n);
        for (int i = 0; i < n; ++i) { const double t = (double) i / sr, ph = std::fmod(t, .5);
            inL[(size_t) i] = t < 2 ? (float) (std::sin(2 * dsp::PI * 110 * t) * std::exp(-ph * 8) * .4 + std::sin(2 * dsp::PI * 1830 * t) * std::exp(-ph * 30) * .3) : 0.f; }
        Transport tr;
        for (int p = 0; p < n; p += block) e->process(&inL[(size_t) p], &inL[(size_t) p], nullptr, nullptr, &L[(size_t) p], &R[(size_t) p], std::min(block, n - p), tr);
        double peak = 0, rms = 0, tail = 0; bool nan = false;
        for (int i = 0; i < n; ++i) { const double a = std::max(std::fabs(L[(size_t) i]), std::fabs(R[(size_t) i])); if (!std::isfinite(a)) nan = true; peak = std::max(peak, a); if (i < 2.5 * sr) rms += a * a; if (i > 7.5 * sr) tail = std::max(tail, a); }
        rms = std::sqrt(rms / (2.5 * sr));
        const bool longTail = pr.name.find("cathedral") != std::string::npos || pr.name.find("synth hall") != std::string::npos;
        const bool bad = nan || peak > 2.5 || rms < 1e-3 || (!longTail && tail > .02);
        if (bad || sr == 48000) std::printf("%s %6d  %-48s peak %.3f rms %.4f tail %.5f\n", bad ? "FAIL" : "ok  ", sr, pr.name.c_str(), peak, rms, tail);
        if (bad) fails++;
        if (!out.empty() && sr == 48000) { std::string f = pr.name; for (char& c : f) if (!std::isalnum((unsigned char) c)) c = '_'; wav(out + "/rev7_" + f + ".wav", L, R, sr); }
    }
    std::printf(fails ? "%d failed\n" : "all good\n", fails);
    return fails ? 1 : 0;
}
