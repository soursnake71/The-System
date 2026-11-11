// Audio in/out: decoding goes through ffmpeg (any format, also video), writing is plain 32-bit float WAV.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "ds/modules.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace ds {

static std::string quote(const std::string& s)
{
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string q = "'";
    for (char c : s) q += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    return q + "'";
#endif
}

Stereo load_audio(const std::string& path)
{
    std::ifstream probe(path, std::ios::binary);
    if (!probe) throw std::runtime_error("can't open " + path);
    const std::string cmd = "ffmpeg -nostdin -loglevel error -i " + quote(path) + " -vn -f f32le -ac 2 -ar 44100 -";
#ifdef _WIN32
    FILE* f = popen(cmd.c_str(), "rb");
#else
    FILE* f = popen(cmd.c_str(), "r");
#endif
    if (!f) throw std::runtime_error("can't run ffmpeg (is it installed and on the PATH?)");
    Stereo out;
    std::vector<float> buf(2 * 65536);
    size_t n;
    while ((n = fread(buf.data(), sizeof(float), buf.size(), f)) > 0)
        for (size_t i = 0; i + 1 < n; i += 2) { out.l.push_back(buf[i]); out.r.push_back(buf[i + 1]); }
    const int rc = pclose(f);
    if (out.l.empty()) throw std::runtime_error("ffmpeg couldn't decode " + path + (rc ? " (is ffmpeg installed?)" : ""));
    return out;
}

Signal to_mono(const Stereo& s)
{
    Signal m(s.l.size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (s.l[i] + (i < s.r.size() ? s.r[i] : s.l[i]));
    return m;
}

Stereo load_wav(const std::string& path, int* sr_out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("can't open " + path);
    auto u32 = [&](uint32_t& v) { return (bool) in.read(reinterpret_cast<char*>(&v), 4); };
    char id[4];
    uint32_t len;
    if (!in.read(id, 4) || std::memcmp(id, "RIFF", 4) || !u32(len) || !in.read(id, 4) || std::memcmp(id, "WAVE", 4))
        throw std::runtime_error("not a WAV file: " + path);
    uint16_t fmt = 0, nch = 0, bits = 0;
    uint32_t sr = 0;
    bool have_fmt = false;
    while (in.read(id, 4) && u32(len)) {
        if (!std::memcmp(id, "fmt ", 4)) {
            std::vector<char> f(len);
            in.read(f.data(), len);
            if (len < 16) break;
            std::memcpy(&fmt, &f[0], 2);
            std::memcpy(&nch, &f[2], 2);
            std::memcpy(&sr, &f[4], 4);
            std::memcpy(&bits, &f[14], 2);
            if (fmt == 0xFFFE && len >= 26) std::memcpy(&fmt, &f[24], 2);  // WAVE_FORMAT_EXTENSIBLE: the sub-format
            have_fmt = true;
        } else if (!std::memcmp(id, "data", 4)) {
            if (!have_fmt || nch == 0 || !((fmt == 3 && bits == 32) || (fmt == 1 && (bits == 16 || bits == 24 || bits == 32))))
                throw std::runtime_error("unsupported WAV format: " + path);
            const size_t bps = bits / 8, frames = len / (bps * nch);
            std::vector<char> raw(frames * bps * nch);
            in.read(raw.data(), (std::streamsize) raw.size());
            const size_t got = (size_t) in.gcount() / (bps * nch);
            Stereo s;
            s.l.resize(got);
            s.r.resize(got);
            auto sample = [&](size_t i, int c) -> float {
                const char* p = &raw[(i * nch + c) * bps];
                if (fmt == 3) { float v; std::memcpy(&v, p, 4); return v; }
                if (bits == 16) { int16_t v; std::memcpy(&v, p, 2); return v / 32768.f; }
                if (bits == 24) {
                    int32_t v = (int32_t) ((uint32_t) (uint8_t) p[0] << 8 | (uint32_t) (uint8_t) p[1] << 16 | (uint32_t) (uint8_t) p[2] << 24);
                    return (float) (v / 2147483648.0);
                }
                int32_t v; std::memcpy(&v, p, 4); return (float) (v / 2147483648.0);
            };
            for (size_t i = 0; i < got; ++i) {
                s.l[i] = sample(i, 0);
                s.r[i] = sample(i, nch > 1 ? 1 : 0);
            }
            if (sr_out) *sr_out = (int) sr;
            return s;
        } else {
            in.seekg(len + (len & 1), std::ios::cur);
        }
    }
    throw std::runtime_error("no audio in " + path);
}

static void put32(std::ofstream& o, uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); }
static void put16(std::ofstream& o, uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); }

static void write_wav(const std::string& path, const float* const* ch, int nch, size_t frames, int sr)
{
    std::ofstream o(path, std::ios::binary);
    if (!o) throw std::runtime_error("can't write " + path);
    const uint32_t bytes = (uint32_t) (frames * nch * 4);
    o.write("RIFF", 4); put32(o, 36 + bytes); o.write("WAVE", 4);
    o.write("fmt ", 4); put32(o, 16); put16(o, 3 /* IEEE float */); put16(o, (uint16_t) nch);
    put32(o, (uint32_t) sr); put32(o, (uint32_t) (sr * nch * 4)); put16(o, (uint16_t) (nch * 4)); put16(o, 32);
    o.write("data", 4); put32(o, bytes);
    std::vector<float> inter(4096 * nch);
    for (size_t a = 0; a < frames; a += 4096) {
        const size_t b = std::min(frames, a + 4096);
        for (size_t i = a; i < b; ++i)
            for (int c = 0; c < nch; ++c) inter[(i - a) * nch + c] = ch[c][i];
        o.write(reinterpret_cast<const char*>(inter.data()), (std::streamsize) ((b - a) * nch * 4));
    }
}

void save_wav(const std::string& path, const Signal& mono, int sr)
{
    const float* c[1] = {mono.data()};
    write_wav(path, c, 1, mono.size(), sr);
}

void save_wav(const std::string& path, const Stereo& st, int sr)
{
    const float* c[2] = {st.l.data(), st.r.data()};
    write_wav(path, c, 2, st.l.size(), sr);
}

}  // namespace ds
