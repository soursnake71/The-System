// ds-sep-check: run the C++ separation stages on a WAV, time them, write the stems, and compare stems with the
// PyTorch reference (tools/sep_reference.py) as SNR in dB. Needs the models (tools/get_models.sh); not a ctest.
//
//   ds-sep-check demucs  <model.bin> <song.wav> <out_prefix> [shift]      -> <out_prefix>_drums.wav, _bass.wav
//   ds-sep-check drumsep <drumsep.onnx> <drums.wav> <out_prefix> <overlap> -> <out_prefix>_<kick..crash>.wav (stereo)
//   ds-sep-check full    <song.wav> <out_dir> fast|best [model_dir]       -> separate() end to end, timing + progress
//   ds-sep-check compare <reference.wav> <test.wav> [...pairs]            -> SNR (dB) of test vs reference
#include <algorithm>
#include <chrono>
#include <memory>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "separate_impl.h"

using namespace ds;

// ---- tiny WAV I/O (PCM 16/24/32, float 32; mono or stereo)
static Stereo read_wav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::vector<char> d((std::istreambuf_iterator<char>(f)), {});
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4))
        throw std::runtime_error(path + ": not a WAV file");
    int fmt = 0, nch = 0, bits = 0, sr = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size()) {
        uint32_t sz;
        std::memcpy(&sz, d.data() + pos + 4, 4);
        const char* id = d.data() + pos;
        const char* body = d.data() + pos + 8;
        if (!std::memcmp(id, "fmt ", 4)) {
            uint16_t v;
            std::memcpy(&v, body, 2), fmt = v;
            std::memcpy(&v, body + 2, 2), nch = v;
            std::memcpy(&sr, body + 4, 4);
            std::memcpy(&v, body + 14, 2), bits = v;
            if (fmt == 0xFFFE) std::memcpy(&v, body + 24, 2), fmt = v;  // WAVE_FORMAT_EXTENSIBLE subformat
        } else if (!std::memcmp(id, "data", 4)) {
            if (nch < 1 || nch > 2) throw std::runtime_error(path + ": only mono/stereo");
            if (sr != kSR) std::cerr << "warning: " << path << " is " << sr << " Hz, expected 44100\n";
            const int bps = bits / 8;
            size_t n = std::min<size_t>(sz, d.size() - pos - 8) / (bps * nch);
            Stereo s;
            s.l.resize(n), s.r.resize(n);
            for (size_t i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c) {
                    const char* q = body + (i * nch + std::min(c, nch - 1)) * bps;
                    float v;
                    if (fmt == 3 && bits == 32) std::memcpy(&v, q, 4);
                    else if (fmt == 1 && bits == 16) { int16_t x; std::memcpy(&x, q, 2); v = x / 32768.f; }
                    else if (fmt == 1 && bits == 24) {
                        int32_t x = (uint8_t)q[0] | ((uint8_t)q[1] << 8) | ((int8_t)q[2] << 16);
                        v = x / 8388608.f;
                    } else if (fmt == 1 && bits == 32) { int32_t x; std::memcpy(&x, q, 4); v = x / 2147483648.f; }
                    else throw std::runtime_error(path + ": unsupported sample format");
                    (c ? s.r : s.l)[i] = v;
                }
            return s;
        }
        pos += 8 + sz + (sz & 1);
    }
    throw std::runtime_error(path + ": no data chunk");
}

static void write_wav(const std::string& path, const Stereo& s) {
    const bool st = !s.r.empty();
    const uint16_t nch = st ? 2 : 1;
    const uint32_t n = (uint32_t)s.l.size(), data = n * nch * 4, sr = kSR, br = kSR * nch * 4;
    std::ofstream f(path, std::ios::binary);
    auto w32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
    auto w16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
    f.write("RIFF", 4), w32(36 + data), f.write("WAVEfmt ", 8), w32(16), w16(3), w16(nch), w32(sr), w32(br);
    w16(nch * 4), w16(32), f.write("data", 4), w32(data);
    for (uint32_t i = 0; i < n; ++i) {
        f.write((const char*)&s.l[i], 4);
        if (st) f.write((const char*)&s.r[i], 4);
    }
}

static double snr_db(const Stereo& ref, const Stereo& x) {
    double e = 0, r = 0;
    size_t n = std::min(ref.size(), x.size());
    for (size_t i = 0; i < n; ++i) {
        r += (double)ref.l[i] * ref.l[i] + (double)ref.r[i] * ref.r[i];
        double a = ref.l[i] - x.l[i], b = ref.r[i] - x.r[i];
        e += a * a + b * b;
    }
    if (ref.size() != x.size()) std::cerr << "warning: lengths differ " << ref.size() << " vs " << x.size() << "\n";
    return 10 * std::log10(r / std::max(e, 1e-30));
}

static double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static Progress printer() {
    auto last = std::make_shared<int>(-1);
    return [last](double f, const std::string& m) {
        int pc = (int)(f * 100);
        if (pc / 5 != *last / 5) std::cerr << "  " << pc << "% " << m << "\n", *last = pc;
    };
}

int main(int argc, char** argv) {
    try {
        std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "demucs" && argc >= 5) {
            Stereo song = read_wav(argv[3]);
            int shift = argc > 5 ? std::atoi(argv[5]) : -1;
            double t = now();
            auto r = sep::demucs_run(argv[2], song, {0, 1}, shift, printer());
            double dt = now() - t;
            write_wav(std::string(argv[4]) + "_drums.wav", r[0]);
            write_wav(std::string(argv[4]) + "_bass.wav", r[1]);
            std::printf("demucs %s: %.1f s audio in %.1f s (incl. model load), shift %d, %d threads\n", argv[2],
                        song.size() / (double)kSR, dt, shift < 0 ? sep::demucs_default_shift() : shift,
                        sep::num_threads());
        } else if (cmd == "drumsep" && argc >= 6) {
            Stereo d = read_wav(argv[3]);
            float peak = 0;
            for (size_t i = 0; i < d.size(); ++i) peak = std::max({peak, std::fabs(d.l[i]), std::fabs(d.r[i])});
            const float g = peak > 0 ? 0.5f / peak : 1.f;
            for (size_t i = 0; i < d.size(); ++i) d.l[i] *= g, d.r[i] *= g;
            double t = now();
            auto r = sep::drumsep_run(argv[2], d, std::atoi(argv[5]), printer());
            double dt = now() - t;
            for (int s = 0; s < 6; ++s) {
                for (size_t i = 0; i < r[s].size(); ++i) r[s].l[i] /= g, r[s].r[i] /= g;
                write_wav(std::string(argv[4]) + "_" + sep::kKitStems[s] + ".wav", r[s]);
            }
            std::printf("drumsep overlap %s: %.1f s audio in %.1f s (incl. model load), %d threads\n", argv[5],
                        d.size() / (double)kSR, dt, sep::num_threads());
        } else if (cmd == "full" && argc >= 5) {
            Stereo song = read_wav(argv[2]);
            std::string q = argv[4];
            double t = now();
            Stems st = separate(song, q == "best" ? Quality::best : Quality::fast, argc > 5 ? argv[5] : "", printer());
            double dt = now() - t;
            std::filesystem::create_directories(argv[3]);
            std::string o = argv[3];
            write_wav(o + "/drums.wav", st.drums);
            write_wav(o + "/bass.wav", st.bass);
            for (auto& [k, v] : st.kit) write_wav(o + "/" + k + ".wav", Stereo{v, {}});
            std::printf("separate(%s): %.1f s audio in %.1f s\n", q.c_str(), song.size() / (double)kSR, dt);
            t = now();
            separate(song, q == "best" ? Quality::best : Quality::fast, argc > 5 ? argv[5] : "", {});
            std::printf("separate(%s) again (models cached): %.1f s\n", q.c_str(), now() - t);
        } else if (cmd == "compare" && argc >= 4 && argc % 2 == 0) {
            for (int i = 2; i + 1 < argc; i += 2)
                std::printf("SNR %7.2f dB  %s vs %s\n", snr_db(read_wav(argv[i]), read_wav(argv[i + 1])),
                            std::filesystem::path(argv[i + 1]).filename().string().c_str(),
                            std::filesystem::path(argv[i]).filename().string().c_str());
        } else {
            std::fprintf(stderr,
                         "usage:\n  %s demucs <model.bin> <song.wav> <out_prefix> [shift]\n"
                         "  %s drumsep <drumsep.onnx> <drums.wav> <out_prefix> <overlap>\n"
                         "  %s full <song.wav> <out_dir> fast|best [model_dir]\n"
                         "  %s compare <ref.wav> <test.wav> [<ref.wav> <test.wav> ...]\n",
                         argv[0], argv[0], argv[0], argv[0]);
            return 2;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
