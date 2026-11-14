// Renders a scripted performance through Seven.h for seven/tools/compare.mjs, which renders it with engine.js too.
// usage: seven_test script.txt fs frames out.f32   (writes left then right)
// script lines: <frame> <cmd> <args...>  cmd: voice <156 numbers> | rnd seed | on n v | off n | p id v | f index v | bend v | wheel v | at v | sus 0/1
#include "Seven.h"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace seven;
int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: seven_test script.txt fs frames out.f32\n"); return 2; }
    const double fs = std::atof(argv[2]); const long frames = std::atol(argv[3]);
    Synth syn(fs);
    struct Ev { long f; std::string line; };
    std::vector<Ev> evs; std::ifstream in(argv[1]); std::string line;
    while (std::getline(in, line)) { if (line.empty()) continue; std::istringstream s(line); long f; s >> f; evs.push_back({f, line}); }
    std::vector<float> out((size_t) frames), outR((size_t) frames);
    long pos = 0; size_t ei = 0;
    while (pos < frames) {
        while (ei < evs.size() && evs[ei].f <= pos) {
            std::istringstream s(evs[ei].line); long f; std::string cmd; s >> f >> cmd;
            if (cmd == "voice") { Voice156 v {}; for (auto& b : v) { int x; s >> x; b = (uint8_t) x; } syn.loadVoice(v); }
            else if (cmd == "rnd") { uint32_t sd; s >> sd; syn.loadVoice(randomVoice(sd)); }
            else if (cmd == "on") { int n, v; s >> n >> v; syn.noteOn(n, v); }
            else if (cmd == "off") { int n; s >> n; syn.noteOff(n); }
            else if (cmd == "p") { int id, v; s >> id >> v; syn.setParam(id, v); }
            else if (cmd == "f") { int k, v; s >> k >> v; syn.setFunc(k, v); }
            else if (cmd == "bend") { int v; s >> v; syn.setBend(v); }
            else if (cmd == "wheel") { int v; s >> v; syn.setCtl(syn.ctl.wheel, v); }
            else if (cmd == "at") { int v; s >> v; syn.setCtl(syn.ctl.at, v); }
            else if (cmd == "sus") { int v; s >> v; syn.sustain(v != 0); }
            ++ei;
        }
        const long next = ei < evs.size() ? std::min(frames, evs[ei].f) : frames;
        const long n = std::max(1L, next - pos);
        syn.renderStereo(out.data() + pos, outR.data() + pos, (int) std::min(n, frames - pos));
        pos += n;
    }
    std::ofstream o(argv[4], std::ios::binary); o.write((const char*) out.data(), (std::streamsize) (out.size() * sizeof(float))); o.write((const char*) outR.data(), (std::streamsize) (outR.size() * sizeof(float)));
    std::printf("voices %d\n", syn.activeVoices());
    return 0;
}
