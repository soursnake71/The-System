// Renders a list of hits through DR110.h for dr110/tools/compare.mjs, which renders the same with engine.js and compares.
// usage: dr110_test params.txt hits.txt sr seconds out.f32   (params: one value per line; hits: "sample voice accent" lines)
#include "DR110.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
using namespace dr110;
int main(int argc, char** argv) {
    if (argc < 6) { std::fprintf(stderr, "usage: dr110_test params.txt hits.txt sr seconds out.f32\n"); return 2; }
    Params p{}; std::ifstream pf(argv[1]); for (auto& v : p) pf >> v;
    std::vector<std::array<long, 3>> hits; std::ifstream hf(argv[2]); long a, b, c; while (hf >> a >> b >> c) hits.push_back({a, b, c});
    const double sr = std::atof(argv[3]); const long n = (long) (sr * std::atof(argv[4]));
    Engine e(sr); e.setAll(p);
    std::vector<float> L((size_t) n), R((size_t) n); std::vector<std::vector<float>> O(8, std::vector<float>((size_t) n));
    long pos = 0; size_t h = 0;
    while (pos < n) {
        while (h < hits.size() && hits[h][0] <= pos) { if (hits[h][1] == 7) e.sweep(); else if (hits[h][1] >= 8) e.release((int) hits[h][1] - 8); else e.trigger((int) hits[h][1], hits[h][2] != 0); ++h; }
        long end = h < hits.size() ? std::min(n, hits[h][0]) : n;
        end = std::min(end, pos + 128);
        float* op[8]; for (int k = 0; k < 8; ++k) op[k] = O[(size_t) k].data() + pos;
        e.process(L.data() + pos, R.data() + pos, (int) (end - pos), op);
        pos = end;
    }
    std::ofstream o(argv[5], std::ios::binary);
    o.write((const char*) L.data(), (std::streamsize) (n * 4)); o.write((const char*) R.data(), (std::streamsize) (n * 4));
    for (auto& ch : O) o.write((const char*) ch.data(), (std::streamsize) (n * 4));
    return 0;
}
