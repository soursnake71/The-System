// Builds xr10/rom.bin, the sound ROM built into the app and the plugin, from your own samples.
//   g++ -O2 -std=c++17 xr10/tools/make-rom.cpp -o make-rom
//   ./make-rom xr10/samples xr10/rom.bin        a folder of WAV / AIFF files named after the sounds ("KICK1.wav", "07 KICK7.wav", "Hat CL.wav")
//   ./make-rom my-memory.xr10 xr10/rom.bin      or the samples you loaded in the app, from its "Save all to a file"
// A map.txt in the folder says which file is which sound ("N path", N = 1-64 as on the panel list); without one the
// files are matched by name. "source outputs" in map.txt means the files were recorded from the XR10's audio outputs:
// the machine's output circuit (DAC hold, 12 kHz LC filters, output amp, output network: core/analog.h) is then taken
// back out (inverse filter, boost limited to MAX_BOOST_DB), the result band-limited to the machine's Nyquist, so that
// playing it through the circuit model gives back the recording. Lead-in silence and the tail hiss are trimmed.
// Sounds without a file stay synthesized. Files are mixed to mono, resampled to the machine's rate (33.33 kHz) and stored as 16-bit.
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
#include <complex>
#include <sstream>
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
// ---- taking the output circuit back out of a recording ----
static const double MAX_BOOST_DB = 24;
static void fft(std::vector<std::complex<double>>& a, bool inv) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * M_PI / (double) len * (inv ? 1 : -1);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t k = 0; k < len / 2; k++) { auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
    if (inv) for (auto& x : a) x /= (double) n;
}
static std::vector<float> trimRecording(const std::vector<float>& in) {
    float pk = 0; for (float v : in) pk = std::max(pk, std::fabs(v));
    size_t a = 0; while (a < in.size() && std::fabs(in[a]) < pk * 0.01f) a++;   // onset: -40 dB
    a = a > 24 ? a - 24 : 0;
    size_t b = in.size(); while (b > a + 64 && std::fabs(in[b - 1]) < pk * 0.0005f) b--;   // the tail down to -66 dB
    std::vector<float> o(in.begin() + (long) a, in.begin() + (long) b);
    const size_t fade = std::min<size_t>(o.size() / 4, 220);   // 5 ms fade into the end
    for (size_t i = 0; i < fade; i++) o[o.size() - 1 - i] *= (float) i / (float) fade;
    return o;
}
static std::vector<float> removeCircuit(const std::vector<float>& x, double rate) {
    const double L = LCLowpass::solveL(), nyq = MACHINE_RATE / 2 * 0.995, floorG = std::pow(10.0, -MAX_BOOST_DB / 20);
    size_t n = 1; while (n < x.size() + 4096) n <<= 1;
    std::vector<std::complex<double>> X(n);
    for (size_t i = 0; i < x.size(); i++) X[i] = x[i];
    fft(X, false);
    const double g0 = std::abs(std::complex<double>(stageResponse(1000, MACHINE_RATE, L).r, stageResponse(1000, MACHINE_RATE, L).i));
    for (size_t k = 0; k <= n / 2; k++) {
        const double f = (double) k * rate / (double) n;
        std::complex<double> inv = 0;
        if (f > 5 && f < nyq) {
            Cx h = stageResponse(f, MACHINE_RATE, L);
            std::complex<double> H(h.r / g0, h.i / g0);   // relative to 1 kHz: the level is set by normalizing after
            const double m = std::abs(H);
            inv = std::conj(H) / std::max(m * m, floorG * floorG);   // limited boost where the circuit cuts hardest
            if (f > nyq * 0.97) inv *= 0.5 * (1 + std::cos(M_PI * (f - nyq * 0.97) / (nyq * 0.03)));   // smooth edge
        }
        X[k] *= inv;
        if (k > 0 && k < n / 2) X[n - k] = std::conj(X[k]);
    }
    fft(X, true);
    std::vector<float> o(x.size());
    for (size_t i = 0; i < o.size(); i++) o[i] = (float) X[i].real();
    float pk = 0; for (float v : o) pk = std::max(pk, std::fabs(v));
    if (pk > 0) for (float& v : o) v *= 0.95f / pk;   // ROM samples sit just under full scale
    return o;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::puts("usage: make-rom <sample folder | memory.xr10> <rom.bin>"); return 2; }
    fs::path in = argv[1];
    std::map<int, std::vector<short>> got;
    std::map<int, std::string> from;
    if (fs::is_directory(in) && fs::exists(in / "map.txt")) {
        std::ifstream mf(in / "map.txt");
        std::string line;
        bool outputs = false;
        while (std::getline(mf, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line.rfind("source outputs", 0) == 0) { outputs = true; std::puts("recordings from the XR10's outputs: taking the output circuit back out"); continue; }
            std::istringstream ls(line); int num = 0; ls >> num; std::string path; std::getline(ls, path);
            while (!path.empty() && path[0] == ' ') path.erase(0, 1);
            const int s = num - 1;
            if (s < 0 || s >= NSOUNDS || path.empty()) continue;
            std::vector<float> mono; double rate = 0;
            if (!decode(readFile(in / path), mono, rate)) { std::printf("  can't read %s\n", path.c_str()); continue; }
            if (outputs) mono = removeCircuit(trimRecording(mono), rate);
            got[s] = toRom(mono, rate); from[s] = path;
        }
    } else if (fs::is_directory(in)) {
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
    o.clear(); put(0x3250525a); put(64); put((u32) ROM_HZ);   // "XRP2", count, rate
    for (int s = 0; s < NSOUNDS; s++) {
        const bool mine = got.count(s) > 0;
        auto& d = mine ? got[s] : synth[s];
        put((u32) s | (mine ? 0u : 0x10000u)); put((u32) d.size());
        for (short v : d) { o.push_back((unsigned char) (v & 0xff)); o.push_back((unsigned char) ((unsigned short) v >> 8)); }
        if (!mine) continue;
        total += d.size();
        std::printf("  %2d %-9s <- %s (%.2f s)\n", s + 1, SOUND_NAMES[s], from[s].c_str(), d.size() / (double) ROM_RATE);
    }
    if (total > (size_t) ROM_POOL) std::printf("warning: %.1f s of samples, the ROM holds %.1f s: the last ones will be cut\n", total / (double) ROM_RATE, ROM_POOL / (double) ROM_RATE);
    std::ofstream(argv[2], std::ios::binary).write((const char*) o.data(), (std::streamsize) o.size());
    std::printf("%d of 64 sounds from your samples, %d synthesized. Wrote %s (%zu bytes)\n", (int) got.size(), 64 - (int) got.size(), argv[2], o.size());
    for (int s = 0; s < NSOUNDS; s++) if (!got.count(s)) std::printf("  still synthesized: %s\n", SOUND_NAMES[s]);
    return 0;
}
