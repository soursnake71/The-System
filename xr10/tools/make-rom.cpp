// Builds xr10/rom.bin, the sound ROM built into the app and the plugin, from your own samples.
//   g++ -O2 -std=c++17 xr10/tools/make-rom.cpp -o make-rom
//   ./make-rom xr10/samples xr10/rom.bin        a folder of WAV / AIFF files named after the sounds ("KICK1.wav", "07 KICK7.wav", "Hat CL.wav")
//   ./make-rom my-memory.xr10 xr10/rom.bin      or the samples you loaded in the app, from its "Save all to a file"
// Sounds without a file stay synthesized. Files are mixed to mono, resampled to the machine's rate (31.25 kHz) and stored as 16-bit.
#include "../core/xr10.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
namespace fs = std::filesystem;
using namespace xr;

static std::vector<unsigned char> readFile(const fs::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static u32 le(const unsigned char* p, int n) { u32 v = 0; for (int i = n - 1; i >= 0; i--) v = (v << 8) | p[i]; return v; }
static u32 be(const unsigned char* p, int n) { u32 v = 0; for (int i = 0; i < n; i++) v = (v << 8) | p[i]; return v; }
static double ext80(const unsigned char* p) {   // AIFF sample rate
    int e = ((p[0] & 0x7f) << 8) | p[1]; unsigned long long m = 0; for (int i = 0; i < 8; i++) m = (m << 8) | p[2 + i];
    return std::ldexp((double) m, e - 16383 - 63);
}
// decodes WAV or AIFF to mono float; returns false if it can't
static bool decode(const std::vector<unsigned char>& d, std::vector<float>& out, double& rate) {
    if (d.size() < 12) return false;
    bool wav = !memcmp(d.data(), "RIFF", 4) && !memcmp(d.data() + 8, "WAVE", 4);
    bool aiff = !memcmp(d.data(), "FORM", 4) && (!memcmp(d.data() + 8, "AIFF", 4) || !memcmp(d.data() + 8, "AIFC", 4));
    if (!wav && !aiff) return false;
    int fmt = 1, ch = 0, bits = 0; const unsigned char* data = nullptr; size_t dataLen = 0; bool bigEndian = aiff;
    size_t at = 12;
    while (at + 8 <= d.size()) {
        const unsigned char* c = d.data() + at;
        u32 len = wav ? le(c + 4, 4) : be(c + 4, 4);
        const unsigned char* b = c + 8;
        size_t avail = std::min<size_t>(len, d.size() - at - 8);
        if (wav && !memcmp(c, "fmt ", 4)) { fmt = le(b, 2); ch = le(b + 2, 2); rate = le(b + 4, 4); bits = le(b + 14, 2); if (fmt == 0xfffe && avail >= 26) fmt = le(b + 24, 2); }
        if (wav && !memcmp(c, "data", 4)) { data = b; dataLen = avail; }
        if (aiff && !memcmp(c, "COMM", 4)) { ch = be(b, 2); bits = be(b + 6, 2); rate = ext80(b + 8); if (!memcmp(d.data() + 8, "AIFC", 4) && avail >= 22) { if (!memcmp(b + 18, "sowt", 4)) bigEndian = false; if (!memcmp(b + 18, "fl32", 4) || !memcmp(b + 18, "FL32", 4)) fmt = 3; } }
        if (aiff && !memcmp(c, "SSND", 4)) { u32 off = be(b, 4); data = b + 8 + off; dataLen = avail - 8 - off; }
        at += 8 + len + (len & 1);
    }
    if (!data || ch <= 0 || bits <= 0 || rate <= 0) return false;
    const int bps = (bits + 7) / 8;
    const size_t frames = dataLen / (size_t) (bps * ch);
    out.assign(frames, 0.f);
    for (size_t i = 0; i < frames; i++) {
        double sum = 0;
        for (int c = 0; c < ch; c++) {
            const unsigned char* p = data + (i * ch + c) * bps;
            double v;
            if (fmt == 3 && bits == 32) { u32 u = bigEndian ? be(p, 4) : le(p, 4); float f; memcpy(&f, &u, 4); v = f; }
            else if (fmt == 3 && bits == 64) { unsigned long long u = 0; for (int k = 0; k < 8; k++) u |= (unsigned long long) p[bigEndian ? 7 - k : k] << (8 * k); double f; memcpy(&f, &u, 8); v = f; }
            else if (bits == 8 && wav) v = (p[0] - 128) / 128.0;
            else {
                u32 u = bigEndian ? be(p, bps) : le(p, bps);
                int shift = 32 - bps * 8; int s = (int) (u << shift) >> shift;
                v = s / (double) (1u << (bps * 8 - 1));
            }
            sum += v;
        }
        out[i] = (float) (sum / ch);
    }
    return true;
}
// band-limited resampling to the machine's rate (the same resampler the app uses), 16-bit, samples kept as they are
static std::vector<short> toRom(const std::vector<float>& in, double rate) {
    const double ratio = rate / ROM_RATE, cut = ratio > 1 ? 0.96 / ratio : 1.0;
    size_t n = (size_t) (in.size() / ratio);
    std::vector<short> o(n);
    for (size_t i = 0; i < n; i++) {
        double y = Machine::resampleAt(in.data(), (int) in.size(), i * ratio, cut);
        o[i] = (short) std::lrint(std::max(-1.0, std::min(1.0, y)) * 32767);
    }
    while (o.size() > 64 && std::abs(o.back()) < 8) o.pop_back();   // trailing silence
    return o;
}
int main(int argc, char** argv) {
    if (argc < 3) { std::puts("usage: make-rom <sample folder | memory.xr10> <rom.bin>"); return 2; }
    fs::path in = argv[1];
    std::map<int, std::vector<short>> got;
    std::map<int, std::string> from;
    if (fs::is_directory(in)) {
        std::vector<fs::path> files;
        for (auto& e : fs::recursive_directory_iterator(in)) if (e.is_regular_file()) files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (auto& p : files) {
            std::string name = p.filename().string();
            int s = Machine::matchSoundName(name.c_str());
            std::vector<float> mono; double rate = 0;
            if (s < 0) { std::printf("  skipped %s (not a sound name)\n", name.c_str()); continue; }
            if (!decode(readFile(p), mono, rate)) { std::printf("  skipped %s (only WAV and AIFF are read: convert it first)\n", name.c_str()); continue; }
            if (got.count(s)) std::printf("  note: %s replaces %s for %s\n", name.c_str(), from[s].c_str(), SOUND_NAMES[s]);
            got[s] = toRom(mono, rate); from[s] = name;
        }
    } else {
        auto mem = readFile(in);
        static Machine m; m.init(44100);
        if (!m.load(mem.data(), (u32) mem.size())) { std::puts("not an XR10 memory file"); return 1; }
        for (int s = 0; s < NSOUNDS; s++) if (m.user[s].len) { got[s].assign(m.userPool + m.user[s].start, m.userPool + m.user[s].start + m.user[s].len); from[s] = "memory file"; }
    }
    std::vector<unsigned char> o;
    auto put = [&](u32 v) { for (int i = 0; i < 4; i++) o.push_back((unsigned char) (v >> (8 * i))); };
    size_t total = 0;
    // the sounds without a sample go in pre-rendered (flagged), so the app doesn't spend seconds synthesizing them at start
    std::map<int, std::vector<short>> synth;
    static float buf[44100 * 3];
    for (int s = 0; s < NSOUNDS; s++) if (!got.count(s)) {
        int n = renderSound(s, buf, 44100 * 3);
        auto& v = synth[s];
        for (int k = 0; k < n; k++) { double x = buf[k] * 32767.0; v.push_back((short) (x > 32767 ? 32767 : (x < -32768 ? -32768 : (x < 0 ? x - 0.5 : x + 0.5)))); }
    }
    o.clear(); put(0x3250525a); put(64); put((u32) ROM_RATE);   // "XRP2", count, rate
    for (int s = 0; s < NSOUNDS; s++) {
        const bool mine = got.count(s) > 0;
        auto& d = mine ? got[s] : synth[s];
        put((u32) s | (mine ? 0u : 0x10000u)); put((u32) d.size());
        for (short v : d) { o.push_back((unsigned char) (v & 0xff)); o.push_back((unsigned char) ((unsigned short) v >> 8)); }
        if (!mine) continue;
        for (short v : d) { o.push_back((unsigned char) (v & 0xff)); o.push_back((unsigned char) ((unsigned short) v >> 8)); }
        total += d.size();
        std::printf("  %2d %-9s <- %s (%.2f s)\n", s + 1, SOUND_NAMES[s], from[s].c_str(), d.size() / (double) ROM_RATE);
    }
    if (total > (size_t) ROM_POOL) std::printf("warning: %.1f s of samples, the ROM holds %.1f s: the last ones will be cut\n", total / (double) ROM_RATE, ROM_POOL / (double) ROM_RATE);
    std::ofstream(argv[2], std::ios::binary).write((const char*) o.data(), (std::streamsize) o.size());
    std::printf("%d of 64 sounds from your samples, %d synthesized. Wrote %s (%zu bytes)\n", (int) got.size(), 64 - (int) got.size(), argv[2], o.size());
    for (int s = 0; s < NSOUNDS; s++) if (!got.count(s)) std::printf("  still synthesized: %s\n", SOUND_NAMES[s]);
    return 0;
}
