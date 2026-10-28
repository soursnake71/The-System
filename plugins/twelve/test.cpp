// Renders a test signal through Twelve.h for twelve/tools/compare.mjs, which renders it with engine.js and compares.
// usage: twelve_test in.f32 params.txt fs out.f32   (in: [n, sr, channels...]; params: one value per line in PARAMS order)
#include "Twelve.h"
#include <cstdio>
#include <fstream>
using namespace twelve;
int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: twelve_test in.f32 params.txt fs out.f32\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    float hdr[3]; in.read((char*) hdr, sizeof hdr);
    const size_t n = (size_t) hdr[0]; const double sr = hdr[1]; const int nc = (int) hdr[2];
    Chans chs(nc, Buf(n)); for (auto& c : chs) in.read((char*) c.data(), (std::streamsize) (n * sizeof(float)));
    Params p; std::ifstream pf(argv[2]); for (int i = 0; i < pid::count; ++i) pf >> p[i];
    double match[10]; for (double& m : match) pf >> m;
    const double fs = std::atof(argv[3]);
    const Chans pre = renderPre(prepareSource(chs, p, sr), sr, p, fs);
    const Chans out = renderMaster(pre, p, match, fs);
    std::ofstream o(argv[4], std::ios::binary); const float len = (float) out[0].size(); o.write((const char*) &len, sizeof len);
    for (auto& c : out) o.write((const char*) c.data(), (std::streamsize) (c.size() * sizeof(float)));
    std::printf("tempo %.1f key %s lufs %.2f\n", detectTempo(chs, sr), detectKey(chs, sr).name().c_str(), loudness(out, fs));
    return 0;
}
