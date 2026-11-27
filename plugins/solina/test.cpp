// Renders a script with Solina.h for solina/tools/compare.mjs: test <script> <out.f64>
// script lines: "fs 48000", "set <id> <value>", "on <note> <vel>", "off <note>", "pedal 0|1", "bend <v>", "mod <v>", "expr <v>", "bpm <v>", "render <samples>"
#include "Solina.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: solina_test script out\n"); return 2; }
    std::ifstream in(argv[1]); std::FILE* out = std::fopen(argv[2], "wb"); std::string line;
    std::unique_ptr<solina::Engine> e;
    std::vector<double> L, R;
    while (std::getline(in, line)) {
        std::istringstream s(line); std::string cmd; s >> cmd;
        if (cmd == "fs") { double f; s >> f; e = std::make_unique<solina::Engine>(f); }
        else if (cmd == "set") { std::string id; double v; s >> id >> v; for (int i = 0; i < solina::pid::count; i++) if (id == solina::PARAMS[i].id) e->set(i, v); }
        else if (cmd == "on") { int n; double v; s >> n >> v; e->noteOn(n, v); }
        else if (cmd == "off") { int n; s >> n; e->noteOff(n); }
        else if (cmd == "pedal") { int v; s >> v; e->pedal(v != 0); }
        else if (cmd == "bend") { double v; s >> v; e->bend(v); }
        else if (cmd == "mod") { double v; s >> v; e->modWheel(v); }
        else if (cmd == "expr") { double v; s >> v; e->expression(v); }
        else if (cmd == "bpm") { double v; s >> v; e->bpm = v; }
        else if (cmd == "render") { int n; s >> n; L.assign((size_t) n, 0); R.assign((size_t) n, 0);
            for (int o = 0; o < n; o += 128) e->process(L.data(), R.data(), o, std::min(128, n - o));
            for (int i = 0; i < n; i++) { std::fwrite(&L[(size_t) i], 8, 1, out); std::fwrite(&R[(size_t) i], 8, 1, out); } }
    }
    std::fclose(out); return 0;
}
