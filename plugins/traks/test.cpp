// Runs a script of front-panel key presses through Machine.h + Traks.h for traks/tools/compare.mjs, which runs the same
// script through machine.js + engine.js and compares the audio, the displays and the memory.
// usage: traks_test rom params.txt script.txt sr seconds out.f32 out.txt
// script lines: "sample key down" (down: 1 press, 0 release; key 100+v: a hit straight to the engine, accented)
#include "Machine.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
using namespace traks;
int main(int argc, char** argv) {
    if (argc < 8) { std::fprintf(stderr, "usage: traks_test rom params.txt script.txt sr seconds out.f32 out.txt\n"); return 2; }
    std::ifstream rf(argv[1], std::ios::binary); std::vector<uint8_t> rb((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
    const auto rom = parseRom(rb.data(), rb.size());
    Params p{}; std::ifstream pf(argv[2]); for (auto& v : p) pf >> v;
    std::vector<std::array<long, 3>> sc; std::ifstream sf(argv[3]); long a, b, c; while (sf >> a >> b >> c) sc.push_back({a, b, c});
    const double sr = std::atof(argv[4]); const long n = (long) (sr * std::atof(argv[5]));
    Engine e(sr); for (int v = 0; v < NV && v < (int) rom.size(); ++v) e.setRom(v, rom[(size_t) v]); e.setAll(p);
    Machine m(e, sr); m.bpm = p[pid::tempo];
    std::vector<float> L((size_t) n), R((size_t) n); std::vector<std::vector<float>> O(12, std::vector<float>((size_t) n));
    std::ofstream t(argv[7]);
    long pos = 0; size_t h = 0;
    while (pos < n) {
        while (h < sc.size() && sc[h][0] <= pos) {
            if (sc[h][1] >= 100) e.trigger((int) sc[h][1] - 100, true); else m.key((int) sc[h][1], sc[h][2] != 0);
            const auto d = m.display(); t << d.l << "|" << d.r << "|" << d.lp << "|" << d.ls << "\n"; ++h;
        }
        long end = h < sc.size() ? std::min(n, sc[h][0]) : n;
        end = std::min(end, pos + 128);
        float* op[12]; for (int k = 0; k < 12; ++k) op[k] = O[(size_t) k].data() + pos;
        m.render(L.data() + pos, R.data() + pos, (int) (end - pos), op);
        pos = end;
    }
    for (int i = 0; i < 100; ++i) { t << "P" << i << ":" << m.mem.pats[(size_t) i].meas << "," << m.mem.pats[(size_t) i].num << "/" << m.mem.pats[(size_t) i].den << "," << m.mem.pats[(size_t) i].swing;
        for (const auto& x : m.mem.pats[(size_t) i].ev) t << " " << x.t << ":" << x.v << ":" << x.a; t << "\n"; }
    for (int i = 0; i < 100; ++i) { t << "S" << i << ":"; for (const auto& s : m.mem.songs[(size_t) i].steps) t << " " << s.p << "/" << s.vol << "/" << s.tempo; t << "\n"; }
    std::ofstream o(argv[6], std::ios::binary);
    o.write((const char*) L.data(), (std::streamsize) (n * 4)); o.write((const char*) R.data(), (std::streamsize) (n * 4));
    for (auto& ch : O) o.write((const char*) ch.data(), (std::streamsize) (n * 4));
    return 0;
}
