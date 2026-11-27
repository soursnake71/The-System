// Plays each sound from rom.bin through the whole circuit model (voice, digital mix, DAC, output stage) and compares
// it with the recording it came from (xr10/samples/map.txt), band by band. If the circuit was taken out properly, the
// model gives the recording back.   g++ -O2 -std=c++17 xr10/tools/check-samples.cpp -o check && ./check
#include "../core/xr10.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <complex>
using namespace xr;
static std::vector<float> wavMono(const std::string& p, double& sr) {
    std::ifstream f(p, std::ios::binary); std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), {});
    size_t at = 12; int ch = 2; std::vector<float> o;
    while (at + 8 <= d.size()) {
        unsigned len = d[at + 4] | d[at + 5] << 8 | d[at + 6] << 16 | (unsigned) d[at + 7] << 24;
        if (!memcmp(&d[at], "fmt ", 4)) { ch = d[at + 10]; sr = d[at + 12] | d[at + 13] << 8 | d[at + 14] << 16; }
        if (!memcmp(&d[at], "data", 4)) for (size_t i = at + 8; i + 2 * ch <= at + 8 + len && i + 2 * ch <= d.size(); i += 2 * ch) { float s = 0; for (int c = 0; c < ch; c++) s += (short) (d[i + 2 * c] | d[i + 2 * c + 1] << 8) / 32768.f; o.push_back(s / ch); }
        at += 8 + len + (len & 1);
    }
    return o;
}
// energy in third-octave-ish bands
static std::vector<double> bands(const std::vector<float>& x, double sr, const std::vector<double>& edges) {
    size_t n = 1; while (n < x.size()) n <<= 1;
    std::vector<std::complex<double>> X(n); for (size_t i = 0; i < x.size(); i++) X[i] = x[i];
    for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(X[i], X[j]); }
    for (size_t len = 2; len <= n; len <<= 1) { double a = -2 * M_PI / len; std::complex<double> wl(cos(a), sin(a)); for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t k = 0; k < len / 2; k++) { auto u = X[i + k], v = X[i + k + len / 2] * w; X[i + k] = u + v; X[i + k + len / 2] = u - v; w *= wl; } } }
    std::vector<double> e(edges.size() - 1, 1e-20);
    for (size_t k = 1; k < n / 2; k++) { double f = k * sr / n; for (size_t b = 0; b + 1 < edges.size(); b++) if (f >= edges[b] && f < edges[b + 1]) e[b] += std::norm(X[k]); }
    return e;
}
int main() {
    static Machine m;
    std::ifstream rf("xr10/rom.bin", std::ios::binary); std::vector<u8> rom((std::istreambuf_iterator<char>(rf)), {});
    m.init(44100, rom.data(), (u32) rom.size());
    m.set[K_VOLUME] = 100;
    const std::vector<double> edges = {60, 125, 250, 500, 1000, 2000, 4000, 6000, 8000, 10000, 12000, 14000, 16000};
    std::ifstream mf("xr10/samples/map.txt"); std::string line;
    double worst = 0, sum = 0; int cnt = 0;
    std::printf("%-30s", "sound (dB, model vs recording)"); for (size_t b = 0; b + 1 < edges.size(); b++) std::printf("%6.0fk", edges[b] / 1000); std::printf("\n");
    while (std::getline(mf, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("source", 0) == 0) continue;
        std::istringstream ls(line); int num; ls >> num; std::string path; std::getline(ls, path); while (!path.empty() && path[0] == ' ') path.erase(0, 1);
        const int s = num - 1;
        double sr = 44100; auto rec = wavMono("xr10/samples/" + path, sr);
        // the model: the sound alone, plain voice settings
        for (auto& v : m.v) v.active = 0;
        s16 keep[NVP]; for (int p = 0; p < NVP; p++) keep[p] = m.vp[s][p];
        m.defaultVoice(s); m.vp[s][V_LEVEL] = 31; m.vp[s][V_PAN] = 0; m.vp[s][V_SEND] = 0; m.vp[s][V_TUNE] = 0; m.vp[s][V_FINE] = 0; m.vp[s][V_VELFEEL] = 0;
        std::vector<float> L(2048), R(2048); for (int k = 0; k < 20; k++) m.process(L.data(), R.data(), nullptr, nullptr, 2048);   // settle
        m.trigger(s, 127);
        std::vector<float> out;
        for (size_t got = 0; got < rec.size() + 4096; got += 2048) { m.process(L.data(), R.data(), nullptr, nullptr, 2048); for (int i = 0; i < 2048; i++) out.push_back((L[i] + R[i]) * .5f); }
        for (int p = 0; p < NVP; p++) m.vp[s][p] = keep[p];
        auto a = bands(out, 44100, edges), b = bands(rec, sr, edges);
        double ta = 0, tb = 0; for (size_t i = 0; i < a.size(); i++) { ta += a[i]; tb += b[i]; }
        std::printf("%2d %-27s", num, path.substr(path.find('/') + 1).c_str());
        for (size_t i = 0; i < a.size(); i++) {
            const double share = b[i] / tb;   // only judge bands that hold a real part of the sound
            const double d = 10 * std::log10((a[i] / ta) / (b[i] / tb));
            if (share > 0.002) { std::printf("%+7.1f", d); worst = std::max(worst, std::fabs(d)); sum += std::fabs(d); cnt++; } else std::printf("      .");
        }
        std::printf("\n");
    }
    std::printf("mean band error %.2f dB, worst %.1f dB (bands holding at least 0.2%% of the sound's energy)\n", sum / cnt, worst);
    return 0;
}
