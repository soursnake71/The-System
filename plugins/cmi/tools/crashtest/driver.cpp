// a mutation fuzzer: every seed, then mutations of them (flips, sets, cuts, inserts, splices, interesting values) for a time
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>
#include <chrono>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n);
int main(int argc, char** argv) {
    const std::string dir = argv[1]; const double secs = argc > 2 ? atof(argv[2]) : 60; const int only = argc > 3 ? atoi(argv[3]) : -1;
    std::vector<std::vector<uint8_t>> seeds;
    DIR* dd = opendir(dir.c_str()); while (auto* e = readdir(dd)) { if (e->d_name[0] == '.') continue; std::ifstream f(dir + "/" + e->d_name, std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {}); if (!b.empty() && (only < 0 || b[0] % 9 == only)) seeds.push_back(b); } closedir(dd);
    for (auto& s : seeds) LLVMFuzzerTestOneInput(s.data(), s.size());
    std::mt19937 r(12345); long it = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
        std::vector<uint8_t> x = seeds[r() % seeds.size()];
        const int muts = 1 + r() % 8;
        for (int m = 0; m < muts && x.size() > 1; ++m) {
            const size_t p = 1 + r() % (x.size() - 1);
            switch (r() % 8) {
                case 0: x[p] ^= (uint8_t) (1 << (r() % 8)); break;
                case 1: x[p] = (uint8_t) r(); break;
                case 2: { static const uint8_t v[] = { 0, 1, 0x7f, 0x80, 0xff, 0xfe, 0x40, 0x10 }; x[p] = v[r() % 8]; break; }
                case 3: x.resize(1 + r() % x.size()); break;
                case 4: { const size_t l = 1 + r() % 64; x.insert(x.begin() + (long) p, l, (uint8_t) r()); break; }
                case 5: { const size_t l = std::min<size_t>(1 + r() % 256, x.size() - p); x.erase(x.begin() + (long) p, x.begin() + (long) (p + l)); break; }
                case 6: { const auto& o = seeds[r() % seeds.size()]; if (o.size() > 2) { const size_t a = 1 + r() % (o.size() - 1), l = std::min<size_t>(1 + r() % 512, o.size() - a); x.insert(x.begin() + (long) p, o.begin() + (long) a, o.begin() + (long) (a + l)); } break; }
                case 7: { if (p + 4 <= x.size()) { const uint32_t v[] = { 0, 0xffffffffu, 0x7fffffffu, 0x80000000u, 65536, 16384, 255, 1u << 20 }; const uint32_t w = v[r() % 8]; std::memcpy(&x[p], &w, 4); } break; }
            }
        }
        LLVMFuzzerTestOneInput(x.data(), x.size()); ++it;
    }
    std::printf("%ld runs over %zu seeds in %.0f s: no crash\n", it, seeds.size(), secs);
}
