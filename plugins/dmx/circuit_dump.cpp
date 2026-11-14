// Renders every kit voice through the C++ circuit model (Circuit.h) for dmx/tools/compare.mjs.
// usage: dmx_circuit_dump <repo>/dmx/kits <outdir> [fs]   writes <kit>_<voice>_<tune>.f32 (float32, mono)
#include "DmxEngine.h"
#include "Circuit.h"
#include <cstdio>
#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string dir = argv[1], out = argv[2]; const double fs = argc > 3 ? std::atof(argv[3]) : 48000;
    int files = 0;
    for (int k = 0; k < dmx::kKits; ++k) {
        const auto& K = dmx::kKitDefs[k]; std::vector<const char*> f, nm; std::vector<int> g;
        for (int v = 0; v < K.voices; ++v) { f.push_back(K.v[v].file); nm.push_back(K.v[v].name); g.push_back(K.v[v].group); }
        for (int v = 0; v < K.voices; ++v) {
            std::ifstream in(dir + "/" + K.id + "/" + K.v[v].file + ".wav", std::ios::binary); std::vector<char> b((std::istreambuf_iterator<char>(in)), {});
            dmx::Sample s; if (!dmx::readWav(b.data(), b.size(), s)) { std::printf("cannot read %s/%s\n", K.id, K.v[v].file); return 1; }
            const auto pl = dmx::circuit::cardFor(f.data(), nm.data(), g.data(), K.voices, v); const auto& card = dmx::circuit::CARDS[pl.card];
            const auto rom = dmx::circuit::makeRom(std::vector<double>(s.d.begin(), s.d.end()), s.sr, card);
            for (int tune : {50, 20, 85}) {
                const auto y = dmx::circuit::render(rom, card, pl.mode, tune, fs);
                std::ofstream o(out + "/" + K.id + "_" + std::to_string(v) + "_" + std::to_string(tune) + ".f32", std::ios::binary);
                o.write(reinterpret_cast<const char*>(y.data()), (std::streamsize) (y.size() * sizeof(float))); ++files;
            }
        }
    }
    std::printf("wrote %d renders\n", files);
    return 0;
}
