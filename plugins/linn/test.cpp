// Runs a script of front-panel key presses through Machine.h + Linn.h for linn/tools/compare.mjs, which runs the same
// script through machine.js + engine.js and compares the audio (the mix and OUT 1-8), the displays and the memory.
// usage: linn_test rom params.txt script.txt sr seconds out.f32 out.txt
// script lines: "sample key down" (down: 1 press, 0 release; key 100+s: sound s straight to the engine)
#include "Machine.h"
#include "Kit.h"
#include <cstring>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
using namespace linn;
int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--kit") == 0) {    // the kit name rules: names in, classification out
        std::ifstream f(argv[2]); std::vector<std::string> names; std::string ln; while (std::getline(f, ln)) if (!ln.empty()) names.push_back(ln);
        const auto c = kit::classify(names);
        for (int g = 0; g < NG_ROM; ++g) { std::printf("%d:", g); for (int i : c.groups[(size_t) g]) std::printf(" %d", i); std::printf("\n"); }
        std::printf("dyn:"); for (int d : c.dyn) std::printf(" %d", d); std::printf("\nleft:"); for (int i : c.left) std::printf(" %d", i); std::printf("\n");
        return 0;
    }
    if (argc < 8) { std::fprintf(stderr, "usage: linn_test rom params.txt script.txt sr seconds out.f32 out.txt\n"); return 2; }
    std::ifstream rf(argv[1], std::ios::binary); std::vector<uint8_t> rb((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
    const auto rom = parseRom(rb.data(), rb.size());
    Params p{}; std::ifstream pf(argv[2]); for (auto& v : p) pf >> v;
    std::vector<std::array<long, 3>> sc; std::ifstream sf(argv[3]); long a, b, c; while (sf >> a >> b >> c) sc.push_back({a, b, c});
    const double sr = std::atof(argv[4]); const long n = (long) (sr * std::atof(argv[5]));
    Engine e(sr); for (int g = 0; g < NG_ROM; ++g) e.setGroup(g, rom[(size_t) g]); e.setAll(p);
    Machine m(e, sr); m.bpm = p[pid::tempo];
    std::vector<float> L((size_t) n), R((size_t) n); std::vector<std::vector<float>> O(16, std::vector<float>((size_t) n));
    std::ofstream t(argv[7]);
    long pos = 0; size_t h = 0;
    while (pos < n) {
        while (h < sc.size() && sc[h][0] <= pos) {
            if (sc[h][1] >= 100) e.trigger((int) sc[h][1] - 100); else m.key((int) sc[h][1], sc[h][2] != 0);
            const auto d = m.display(); t << d.l << "|" << d.r << "|" << d.song << d.tim << d.ec << d.rec << d.ext << d.perc << "\n"; ++h;
        }
        long end = h < sc.size() ? std::min(n, sc[h][0]) : n;
        end = std::min(end, pos + 128);
        float* op[16]; for (int k = 0; k < 16; ++k) op[k] = O[(size_t) k].data() + pos;
        m.render(L.data() + pos, R.data() + pos, (int) (end - pos), op);
        pos = end;
    }
    for (int i = 0; i < NPAT; ++i) { const auto& pt = m.mem.pats[(size_t) i]; t << "P" << i << ":" << pt.len << "," << pt.ec << "," << pt.tim << "," << pt.bef;
        for (const auto& x : pt.ev) t << " " << x.t << ":" << x.s; t << "\n"; }
    for (int i = 0; i < NPAT; ++i) { t << "S" << i << ":"; for (int s : m.mem.songs[(size_t) i].steps) t << " " << s; t << "\n"; }
    t << "R" << (m.mem.repeat ? 1 : 0) << "\n";
    std::ofstream o(argv[6], std::ios::binary);
    o.write((const char*) L.data(), (std::streamsize) (n * 4)); o.write((const char*) R.data(), (std::streamsize) (n * 4));
    for (auto& ch : O) o.write((const char*) ch.data(), (std::streamsize) (n * 4));
    return 0;
}
