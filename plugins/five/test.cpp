// Renders the scenarios of five/tools/render.mjs through Five.h for five/tools/compare.mjs (raw float32, interleaved L/R).
// usage: five_test <outfile> [kit|voices]
#include "Five.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
using namespace five;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: five_test <outfile> [kit|voices]\n"); return 2; }
    const std::string scenario = argc > 2 ? argv[2] : "kit";
    std::vector<float> L, R; double sr;
    if (scenario == "voices") {
        sr = ROM_SR; const size_t gap = (size_t) jsround(.1 * ROM_SR);
        for (int v = 0; v < NV; v++) { const auto x = makeVoiceSample(v, ROM_SR); L.insert(L.end(), x.begin(), x.end()); L.insert(L.end(), gap, 0.f); }
        R = L;
    } else {
        sr = 48000; const double secs = 7; const int block = 128; const int n = (int) jsround(secs * sr);
        Engine e(sr);
        e.selectModels();
        e.revFx.setProgram(std::string("HALL1")); e.setDly(dOn, 1);
        e.setChannelParam(1, cSendRev, .4); e.setChannelParam(1, cSendDly, .2); e.setChannelParam(2, cSendRev, .3); e.setChannelParam(9, cSendRev, .25);
        e.setChannelParam(10, cSendRev, .3); e.setChannelParam(10, cSendDly, .35);
        struct Hit { int at, v, vel, acc; }; std::vector<Hit> hits;
        for (int v = 0; v < 24; v++) hits.push_back({ (int) jsround(v * .25 * sr), v, 127 - (v * 17) % 60, v % 3 });
        L.assign((size_t) n, 0); R.assign((size_t) n, 0);
        std::vector<float> bl((size_t) block), br((size_t) block);
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
