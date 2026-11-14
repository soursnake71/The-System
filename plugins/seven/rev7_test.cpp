// File-driven runner for Rev7Full.h (to compare against the JavaScript port).
//   rev7_test <prog 1-30> <hostRate> <in.f32> <out.f32> [key=value ...]
// in.f32: raw float32 mono samples. out.f32: float32, all of L then all of R. key=value pairs set parameters by
// their keys (prog rt hi lo diff idly fdly flev live mode size dlyL dlyR dlev fb hdamp mfreq mdep mdly amdep eq eqlf
// eqlg eqmf eqmg eqhf eqhg circ) after the factory program is loaded. Processed in blocks of 512 (the engine's sleep
// logic is counted per block).
// Build: g++ -std=c++17 -O2 -ffp-contract=off -o rev7_test rev7_test.cpp
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "Rev7Full.h"

int main(int argc, char** argv) {
    using namespace seven::rev7;
    if (argc < 5) { std::fprintf(stderr, "usage: %s <prog 1-30> <hostRate> <in.f32> <out.f32> [key=value ...]\n", argv[0]); return 2; }
    const int prog = std::atoi(argv[1]); const double rate = std::atof(argv[2]);
    if (prog < 1 || prog > 30 || rate <= 0) { std::fprintf(stderr, "bad prog or rate\n"); return 2; }
    FILE* f = std::fopen(argv[3], "rb"); if (!f) { std::perror(argv[3]); return 1; }
    std::vector<float> in; float buf[4096]; size_t got;
    while ((got = std::fread(buf, sizeof(float), 4096, f)) > 0) in.insert(in.end(), buf, buf + got);
    std::fclose(f);

    Rev7Full r; r.loadFactory(prog);
    for (int a = 5; a < argc; ++a) {
        const char* eq = std::strchr(argv[a], '=');
        if (!eq) { std::fprintf(stderr, "bad arg %s\n", argv[a]); return 2; }
        const std::string key(argv[a], (size_t) (eq - argv[a])); int k = 0;
        while (k < PCOUNT && key != PARAMS[k].key) ++k;
        if (k == PCOUNT) { std::fprintf(stderr, "unknown key %s\n", key.c_str()); return 2; }
        r.set(k, std::atof(eq + 1));
    }
    r.prepare(rate);

    const int n = (int) in.size(); std::vector<float> L((size_t) n), R((size_t) n);
    for (int i = 0; i < n; i += 512) r.process(in.data() + i, L.data() + i, R.data() + i, std::min(512, n - i));

    FILE* o = std::fopen(argv[4], "wb"); if (!o) { std::perror(argv[4]); return 1; }
    std::fwrite(L.data(), sizeof(float), L.size(), o); std::fwrite(R.data(), sizeof(float), R.size(), o);
    std::fclose(o);
    return 0;
}
