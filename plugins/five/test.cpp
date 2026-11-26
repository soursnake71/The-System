// Renders the scenarios of five/tools/render.mjs through Five.h for five/tools/compare.mjs (raw float32, interleaved L/R).
// Every engine first loads the factory kit (kitDir/FACTORY_MAP[v]) as 'rec' voices.
//   kit (default): Engine at 48 kHz, every voice 0..23 once 0.25 s apart, mixed velocities and accents, REV HALL1 on a few
//     channels, TC delay on, 7 s.   voices: every voice's loaded sample (before REC_EQ) with 0.1 s gaps at 25 kHz, L = R.
// usage: five_test <outfile> [kit|voices] [kitDir (default five/kit)]
#include "Five.h"
#include <cstdio>
#include <cstring>
#include <iterator>
#include <fstream>
#include <string>
#include <vector>
using namespace five;

// 16-bit / 24-bit / float PCM WAV -> mono floats (channel 0), like the app's WAV reader
static bool readWav(const std::string& path, std::vector<float>& out, double& sr) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() < 12 || std::string((char*) b.data(), 4) != "RIFF") return false;
    auto u16 = [&](size_t o) { return (int) (b[o] | b[o + 1] << 8); };
    auto u32 = [&](size_t o) { return (uint32_t) (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t) b[o + 3] << 24); };
    int fmt = 1, ch = 1, bits = 16; size_t p = 12;
    while (p + 8 <= b.size()) {
        const std::string id((char*) &b[p], 4); const uint32_t len = u32(p + 4); const size_t d = p + 8;
        if (id == "fmt ") { fmt = u16(d); ch = u16(d + 2); sr = u32(d + 4); bits = u16(d + 14); if (fmt == 0xFFFE) fmt = u16(d + 24); }
        else if (id == "data") {
            const int bps = bits / 8; const size_t n = std::min<size_t>(len, b.size() - d) / (size_t) (bps * ch); out.resize(n);
            for (size_t i = 0; i < n; i++) {
                const size_t o = d + i * (size_t) (bps * ch);
                if (fmt == 3 && bits == 32) { float x; std::memcpy(&x, &b[o], 4); out[i] = x; }
                else if (bits == 16) out[i] = (float) ((int16_t) u16(o) / 32768.0);
                else if (bits == 24) out[i] = (float) (((int32_t) (b[o] << 8 | b[o + 1] << 16 | (uint32_t) b[o + 2] << 24) >> 8) / 8388608.0);
                else if (bits == 8) out[i] = (float) ((b[o] - 128) / 128.0);
            }
            return true;
        }
        p = d + len + (len & 1);
    }
    return false;
}

static bool loadKit(Engine& e, const std::string& kit) {
    for (int v = 0; v < NV; v++) {
        std::vector<float> d; double wsr = 25000;
        if (!readWav(kit + "/" + FACTORY_MAP[v], d, wsr)) { std::fprintf(stderr, "can't read %s/%s\n", kit.c_str(), FACTORY_MAP[v]); return false; }
        e.setUserSample(v, d.data(), d.size(), wsr, true);
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: five_test <outfile> [kit|voices] [kitDir]\n"); return 2; }
    const std::string scenario = argc > 2 ? argv[2] : "kit", kit = argc > 3 ? argv[3] : "five/kit";
    std::vector<float> L, R; double sr;
    if (scenario == "voices") {
        sr = ROM_SR; Engine e(sr); if (!loadKit(e, kit)) return 1;
        const size_t gap = (size_t) jsround(.1 * ROM_SR);
        for (int v = 0; v < NV; v++) { L.insert(L.end(), e.user[v].begin(), e.user[v].end()); L.insert(L.end(), gap, 0.f); }
        R = L;
    } else {
        sr = 48000; const double secs = 7; const int block = 128, n = (int) jsround(secs * sr);
        Engine e(sr); if (!loadKit(e, kit)) return 1;
        e.revFx.setProgram(std::string("HALL1")); e.setDly(dOn, 1);
        e.setChannelParam(1, cSendRev, .4); e.setChannelParam(1, cSendDly, .2); e.setChannelParam(2, cSendRev, .3); e.setChannelParam(9, cSendRev, .25);
        e.setChannelParam(10, cSendRev, .3); e.setChannelParam(10, cSendDly, .35);
        struct Hit { int at, v, vel, acc; }; std::vector<Hit> hits;
        for (int v = 0; v < 24; v++) hits.push_back({ (int) jsround(v * .25 * sr), v, 127 - (v * 17) % 60, v % 3 });
        L.assign((size_t) n, 0); R.assign((size_t) n, 0); std::vector<float> bl((size_t) block), br((size_t) block);
        for (int s = 0; s < n; s += block) {
            const int m = std::min(block, n - s);
            for (auto& h : hits) if (h.at >= s && h.at < s + m) e.trigger(h.v, h.vel, h.acc, h.at - s);
            e.process(bl.data(), br.data(), m);
            std::copy(bl.begin(), bl.begin() + m, L.begin() + s); std::copy(br.begin(), br.begin() + m, R.begin() + s);
        }
    }
    std::vector<float> inter(L.size() * 2);
    for (size_t i = 0; i < L.size(); i++) { inter[2 * i] = L[i]; inter[2 * i + 1] = R[i]; }
    std::ofstream o(argv[1], std::ios::binary); o.write((const char*) inter.data(), (std::streamsize) (inter.size() * sizeof(float)));
    std::printf("%s: %zu frames @ %g Hz -> %s\n", scenario.c_str(), L.size(), sr, argv[1]);
    return 0;
}
