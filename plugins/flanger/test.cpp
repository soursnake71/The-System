// Renders a test signal through M117.h for flanger/tools/compare.mjs, which renders it with engine.js and compares.
// usage: flanger_test in.f32 params.txt fs out.f32
//   in.f32: [n, channels(2), L..., R...] floats; params.txt: one value per line in m117::Param order
//   out.f32: [n, L..., R...]
#include "M117.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: flanger_test in.f32 params.txt fs out.f32\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    float hdr[2]; in.read((char*) hdr, sizeof hdr);
    const size_t n = (size_t) hdr[0];
    std::vector<float> L(n), R(n), oL(n), oR(n);
    in.read((char*) L.data(), (std::streamsize) (n * 4)); in.read((char*) R.data(), (std::streamsize) (n * 4));
    m117::Engine e;
    std::ifstream pf(argv[2]); for (int i = 0; i < m117::kNumParams; ++i) { double v; pf >> v; e.set(i, v); }
    e.prepare(std::atof(argv[3]));
    // blocks of varying size, like a host
    size_t pos = 0; int b = 0;
    const int sizes[] = {512, 64, 1, 300, 128, 7, 1024};
    while (pos < n) {
        int len = sizes[b++ % 7]; if ((size_t) len > n - pos) len = (int) (n - pos);
        e.process(L.data() + pos, R.data() + pos, oL.data() + pos, oR.data() + pos, len);
        pos += (size_t) len;
    }
    std::ofstream o(argv[4], std::ios::binary); const float len = (float) n; o.write((const char*) &len, 4);
    o.write((const char*) oL.data(), (std::streamsize) (n * 4)); o.write((const char*) oR.data(), (std::streamsize) (n * 4));
    return 0;
}
