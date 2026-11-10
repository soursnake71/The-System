// Audio in/out: decoding goes through ffmpeg (any format, also video), writing is plain 32-bit float WAV.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
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
