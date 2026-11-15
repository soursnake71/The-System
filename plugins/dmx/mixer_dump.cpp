// Plays a fixed set of hits through the C++ engine (Mixer.h, with the kits through Circuit.h) for dmx/tools/compare.mjs.
// usage: dmx_mixer_dump <repo>/dmx/kits <outdir>   writes mix_<case>.f32 (interleaved stereo float32)
#include "DmxEngine.h"
#include "Mixer.h"
#include "Presets.h"
#include <cstdio>
#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string dir = argv[1], out = argv[2]; const double fs = 48000; const int B = 128;
    // the cases: kit, circuit on, preset (-1: defaults)
    const int cases[][3] = {{1, 1, -1}, {1, 1, 0}, {1, 1, 37}, {1, 1, 75}, {1, 1, 85}, {0, 0, 95}, {2, 1, 33}};
    int ci = 0;
    for (const auto& cs : cases) {
        const int k = cs[0]; const bool circ = cs[1]; const int pre = cs[2];
        const auto& K = dmx::kKitDefs[k]; std::vector<const char*> f, nm; std::vector<int> g;
        for (int v = 0; v < K.voices; ++v) { f.push_back(K.v[v].file); nm.push_back(K.v[v].name); g.push_back(K.v[v].group); }
        dmx::Mixer M(fs);
        std::vector<double> P(dmx::kParams); for (int i = 0; i < dmx::kParams; ++i) P[(size_t) i] = pre < 0 ? dmx::kParamDefs[i].def : dmx::kPresetValues[pre][i];
        M.setAll(P.data()); M.setTempo(120); M.setCircuit(circ); M.setFaders({.8, .8, .8, .8, .8, .8, .8, .5, .85});
        for (int v = 0; v < K.voices; ++v) {
            std::ifstream in(dir + "/" + K.id + "/" + K.v[v].file + ".wav", std::ios::binary); std::vector<char> b((std::istreambuf_iterator<char>(in)), {});
            dmx::Sample s; dmx::readWav(b.data(), b.size(), s);
            const auto pl = dmx::circuit::cardFor(f.data(), nm.data(), g.data(), K.voices, v); const auto& card = dmx::circuit::CARDS[pl.card];
            const int c = (int) pl.card;   // engine.js CARDS is in Circuit.h CardId order
            auto q = [&](int fld) { return P[(size_t) (dmx::kNG + c * dmx::kNF + fld)]; };
            dmx::circuit::Mods m; m.cap = q(0); m.dmod = (int) std::floor(q(1) + .5); m.early = (int) std::floor(q(2) + .5); m.filt = (int) std::floor(q(3) + .5); m.dac = (int) std::floor(q(4) + .5); m.rev = q(5) >= .5;
            const double tune = std::min(100.0, std::max(0.0, 50 + q(6)));
            dmx::Mixer::Sound S; S.card = c; S.group = K.v[v].group; S.open = K.v[v].open;
            if (circ) { const auto rom = dmx::circuit::makeRom(std::vector<double>(s.d.begin(), s.d.end()), s.sr, card);
                S.d = std::make_shared<std::vector<float>>(dmx::circuit::render(rom, card, pl.mode, tune, fs, m)); S.rate = fs; const auto pn = dmx::circuit::pan(card); S.panL = pn.first; S.panR = pn.second; }
            else { auto d = std::make_shared<std::vector<float>>(s.d); if (m.rev) std::reverse(d->begin(), d->end()); S.d = d; S.rate = s.sr;
                S.ratio = dmx::circuit::rate(card, pl.mode, tune) / dmx::circuit::rate(card, pl.mode, 50); }
            M.setSound(v, S);
        }
        // the hits: [frame, voice, velocity x 1000, gate frames or -1]; voice 99 = click, 98 = release voice 0
        const long hits[][4] = {{0, 0, 1000, 6000}, {0, 7, 720, 6000}, {3000, 8, 1000, 6000}, {6000, 7, 720, 6000}, {12000, 2, 1000, 6000}, {12000, 99, 1, 0},
            {18000, 4, 720, 6000}, {24000, 0, 720, -1}, {30000, 98, 0, 0}, {36000, 9, 1000, 6000}, {42000, 14, 1000, 6000}, {48000, 5, 1000, 3000}, {60000, 12, 1000, 6000}, {66001, 3, 720, 6000}};
        const int N = (int) (fs * 3); std::vector<float> o((size_t) N * 2); std::vector<double> L(B), R(B); size_t h = 0;
        for (int f0 = 0; f0 < N; f0 += B) {
            const int n = std::min(B, N - f0);
            while (h < sizeof hits / sizeof hits[0] && hits[h][0] < f0 + n) { const auto& x = hits[h]; const int off = (int) std::max(0L, x[0] - f0);
                if (x[1] == 99) M.click(true, off); else if (x[1] == 98) M.release(0); else if (x[1] < K.voices) M.trigger((int) x[1], x[2] / 1000.0, off, x[3]); ++h; }
            M.process(L.data(), R.data(), n);
            for (int i = 0; i < n; ++i) { o[(size_t) (f0 + i) * 2] = (float) L[(size_t) i]; o[(size_t) (f0 + i) * 2 + 1] = (float) R[(size_t) i]; }
        }
        std::ofstream w(out + "/mix_" + std::to_string(ci++) + ".f32", std::ios::binary); w.write(reinterpret_cast<const char*>(o.data()), (std::streamsize) (o.size() * sizeof(float)));
    }
    // the dice: every part, two seeds, from preset 12
    for (int part = 0; part < dmx::kDiceParts; ++part) for (uint32_t seed : {12345u, 4000000001u}) {
        const auto d = dmx::presets::dice(dmx::kPresetValues[12], part, seed);
        std::ofstream w(out + "/dice_" + std::to_string(part) + "_" + std::to_string(seed) + ".f64", std::ios::binary); w.write(reinterpret_cast<const char*>(d.data()), (std::streamsize) (d.size() * sizeof(double)));
    }
    std::printf("wrote %d mixes and the dice\n", ci);
    return 0;
}
