// The C++ side of five/tools/interop.mjs: FiveSeq.h / FiveSysex.h / FiveCart.h against seq.js, rxsysex.js and rxcart.js.
// interop.mjs writes the js-* inputs into <dir>, runs this, and compares the cpp-* outputs with what the JS makes of
// the same inputs.
//   cpp-cart.json    js-cart.json loaded and saved again (plain JSON)
//   cpp-rand.json    a fresh state randomized with seed 4242 (scope all), then the pattern scope with seed 77 into pattern 20
//   cpp-ptn.mid      exportSMF(js-cart state, pattern 0, gm)        cpp-song.mid  exportSMF(js-cart state, song 0, rx5)
//   cpp-import.json  importSMF(js-song.mid) into a fresh state (start pattern 50, song slot 1)
//   cpp-syx.json     loadSyx(js-omega.syx) into a fresh state     cpp-omega.syx omega pattern 0, song 0, voices, fx from js-cart
//   cpp-bank.bin     parseBank(js-bank.bin) -> buildBank(voices, its id)
//   cpp-play.txt     the Player on js-cart: song 0, then pattern 10 with the click, then chain 0; 48 kHz, 256-frame blocks
//   cpp-rx.txt       handleSysex replies for the messages in js-rx.syx (identity, dump requests, device filter)
// usage: five_seq_test <dir>
#include "FiveSysex.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

using namespace five;

static std::vector<uint8_t> readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {}); }
static void writeFile(const std::string& p, const std::vector<uint8_t>& b) { std::ofstream f(p, std::ios::binary); f.write((const char*) b.data(), (std::streamsize) b.size()); }
static void writeText(const std::string& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f << s; }
static std::string hex(const Bytes& m) { std::string s; char b[4]; for (auto x : m) { std::snprintf(b, sizeof b, "%02X", x); s += b; } return s; }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: five_seq_test <dir>\n"); return 2; }
    const std::string dir = argv[1], P = dir + "/";
    try {
        // cartridge round trip
        const auto cartBytes = readFile(P + "js-cart.json");
        State st = loadCart(cartBytes.data(), cartBytes.size());
        { const auto mb = saveCart(st, false); writeFile(P + "cpp-cart.json", std::vector<uint8_t>((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize())); }
        // randomize
        { State r; const auto g = randomize(r, 4242, "all"); randomize(r, 77, "pattern", 20); const auto mb = saveCart(r, false);
          writeFile(P + "cpp-rand.json", std::vector<uint8_t>((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize())); std::printf("randomize: %s\n", g.c_str()); }
        // SMF
        writeFile(P + "cpp-ptn.mid", exportSMF(st, "pattern", 0, "gm"));
        writeFile(P + "cpp-song.mid", exportSMF(st, "song", 0, "rx5"));
        { const auto mid = readFile(P + "js-song.mid"); State s; const auto r = importSMF(s, mid.data(), mid.size(), 0, 50, 1);
          if (!r.ok) throw std::runtime_error("importSMF: " + r.error);
          const auto mb = saveCart(s, false); writeFile(P + "cpp-import.json", std::vector<uint8_t>((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize()));
          std::printf("importSMF: %s\n", r.report.c_str()); }
        // Omega SysEx both ways
        { const auto syx = readFile(P + "js-omega.syx"); State s; const auto r = loadSyx(s, syx.data(), syx.size());
          for (const auto& e : r.errors) std::printf("loadSyx error: %s\n", e.c_str());
          const auto mb = saveCart(s, false); writeFile(P + "cpp-syx.json", std::vector<uint8_t>((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize()));
          std::printf("loadSyx: %d omega, %d yamaha, %d other\n", r.omega, r.yamaha, r.other); }
        writeFile(P + "cpp-omega.syx", writeSyx({ omegaPattern(st, 0), omegaSong(st, 0), omegaVoices(st), omegaFx(st), omegaParam(2, 0, "time", 3.5), omegaParam(0, 5, "pitch", -1200) }));
        // RX5 bank
        { const auto b = readFile(P + "js-bank.bin"); const auto p = parseBank(b.data(), b.size());
          if (!p.ok) throw std::runtime_error("parseBank: " + p.error);
          writeFile(P + "cpp-bank.bin", buildBank(p.voices, p.id)); std::printf("bank: %zu voices, checksum %s\n", p.voices.size(), p.checksumOk ? "ok" : "BAD"); }
        // the Player
        {
            std::ostringstream o; Player pl(&st); const double sr = 48000; const int block = 256;
            auto run = [&](Player::Mode mode, int n, int blocks, const char* tag) {
                pl.setMode(mode, n); pl.start(); long long at = 0;
                for (int b = 0; b < blocks && pl.running; b++, at += block)
                    pl.advance(block, sr, 0, [&](const PlayEvent& e) { o << tag << ' ' << (at + e.offset) << ' ' << e.v << ' ' << e.vel << ' ' << e.acc << ' ' << e.click << '\n'; });
                o << tag << " end " << (pl.running ? 1 : 0) << ' ' << (pl.ended ? 1 : 0) << '\n';
            };
            run(Player::SongMode, 0, 3000, "song");
            const bool ck = st.clickOn; st.clickOn = true; run(Player::PatternMode, 10, 400, "ptn"); st.clickOn = ck;
            run(Player::ChainMode, 0, 4000, "chain");
            writeText(P + "cpp-play.txt", o.str());
        }
        // SysEx receive: replies
        {
            const auto syx = readFile(P + "js-rx.syx"); std::ostringstream o;
            for (int dev : { 1, 0, 3 }) {
                State s = st; s.midi.deviceNo = dev; BulkReceiver rcv;
                for (const auto& m : parseSyx(syx.data(), syx.size())) {
                    const auto r = handleSysex(s, m, rcv, 0);
                    o << dev << ' ' << (r.kind.empty() ? "-" : r.kind) << ' ' << r.reply.size();
                    for (const auto& x : r.reply) o << ' ' << (isOmega(x) ? std::string("omega") : hex(x).substr(0, 40));
                    o << '\n';
                }
            }
            writeText(P + "cpp-rx.txt", o.str());
        }
    } catch (const std::exception& e) { std::fprintf(stderr, "five_seq_test: %s\n", e.what()); return 1; }
    std::printf("five_seq_test: wrote %s/cpp-*\n", dir.c_str());
    return 0;
}
