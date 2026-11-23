// Omega Wheel of Steel tags: Serato / ID3 / RIFF INFO / Vorbis / MP4 tags and Serato crates, plus the key helpers.
// Plain C++17 (no JUCE): shared by the desktop library and the Android engine.
#include "SeratoTags.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#if ! defined(_WIN32)
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace wheel {

//==============================================================================================================
// byte sources

size_t MemorySource::read(uint64_t off, void* dst, size_t n)
{
    if (off >= len) return 0;
    const size_t k = (size_t) std::min<uint64_t>(n, len - off);
    std::memcpy(dst, data + off, k);
    return k;
}

#if ! defined(_WIN32)
uint64_t FdSource::size()
{
    struct stat s {};
    if (fd < 0 || fstat(fd, &s) != 0 || s.st_size < 0) return 0;
    return (uint64_t) s.st_size;
}

size_t FdSource::read(uint64_t off, void* dst, size_t n)
{
    if (fd < 0) return 0;
    size_t got = 0;
    while (got < n) {
        const ssize_t r = pread(fd, (uint8_t*) dst + got, n - got, (off_t) (off + got));
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        got += (size_t) r;
    }
    return got;
}
#endif

//==============================================================================================================
// text

namespace {

using Bytes = std::vector<uint8_t>;

void putUtf8(std::string& s, uint32_t c)
{
    if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
    if (c < 0x80) s += (char) c;
    else if (c < 0x800) { s += (char) (0xC0 | (c >> 6)); s += (char) (0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { s += (char) (0xE0 | (c >> 12)); s += (char) (0x80 | ((c >> 6) & 0x3F)); s += (char) (0x80 | (c & 0x3F)); }
    else if (c < 0x110000) { s += (char) (0xF0 | (c >> 18)); s += (char) (0x80 | ((c >> 12) & 0x3F)); s += (char) (0x80 | ((c >> 6) & 0x3F)); s += (char) (0x80 | (c & 0x3F)); }
    else s += "\xEF\xBF\xBD";
}

std::string latin1(const uint8_t* p, size_t n)
{
    std::string s;
    s.reserve(n + 1);
    for (size_t i = 0; i < n && p[i]; ++i) putUtf8(s, p[i]);
    return s;
}

// UTF-8 up to the first NUL; malformed sequences become U+FFFD, so the result is always valid UTF-8
std::string utf8(const uint8_t* p, size_t n)
{
    size_t e = 0;
    while (e < n && p[e]) ++e;
    std::string s;
    s.reserve(e);
    for (size_t i = 0; i < e;) {
        const uint8_t c = p[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (len == 1) { s += (char) c; ++i; continue; }
        bool ok = len > 0 && i + (size_t) len <= e;
        uint32_t cp = len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (int k = 1; ok && k < len; ++k) {
            if ((p[i + (size_t) k] & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (p[i + (size_t) k] & 0x3Fu);
        }
        if (ok && ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)))) ok = false;
        if (! ok) { s += "\xEF\xBF\xBD"; ++i; continue; }
        putUtf8(s, cp);
        i += (size_t) len;
    }
    return s;
}

std::string utf16(const uint8_t* p, size_t n, bool bigEndian)
{
    std::string s;
    s.reserve(n / 2 + 1);
    for (size_t i = 0; i + 1 < n; i += 2) {
        const auto c = (uint32_t) (bigEndian ? (p[i] << 8) | p[i + 1] : (p[i + 1] << 8) | p[i]);
        if (c == 0) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < n) {
            const auto d = (uint32_t) (bigEndian ? (p[i + 2] << 8) | p[i + 3] : (p[i + 3] << 8) | p[i + 2]);
            if (d >= 0xDC00 && d < 0xE000) { putUtf8(s, 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00)); i += 2; continue; }
        }
        putUtf8(s, c);
    }
    return s;
}

// ID3 text with its encoding byte (0 latin1, 1 UTF-16 with BOM, 2 UTF-16BE, 3 UTF-8)
std::string id3Text(const uint8_t* p, size_t n, uint8_t enc)
{
    if (enc == 1 || enc == 2) {
        bool be = enc == 2;
        if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { be = true; p += 2; n -= 2; }
        else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { be = false; p += 2; n -= 2; }
        else if (enc == 1) be = false;
        return utf16(p, n, be);
    }
    return enc == 3 ? utf8(p, n) : latin1(p, n);
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) ++a;
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string upper(std::string s) { for (auto& c : s) if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A'); return s; }
std::string lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a'); return s; }

// ID3 TCON: "(17)Rock" / "(17)" / "Rock"; a bare ID3v1 number keeps only the commonest DJ genres' names
std::string id3Genre(const std::string& s)
{
    std::string g = s;
    size_t k = 0;
    if (! g.empty() && g[0] == '(') {
        k = 1;
        while (k < g.size() && g[k] >= '0' && g[k] <= '9') ++k;
        if (k > 1 && k < g.size() && g[k] == ')') {
            const int n = std::atoi(g.substr(1, k - 1).c_str());
            g = trim(g.substr(k + 1));
            if (! g.empty()) return g;
            g = std::to_string(n);
        } else return g;
    }
    if (g.empty() || g.find_first_not_of("0123456789") != std::string::npos) return g;
    static const std::pair<int, const char*> names[] = { { 0, "Blues" }, { 2, "Country" }, { 3, "Dance" }, { 5, "Funk" }, { 7, "Hip-Hop" },
        { 8, "Jazz" }, { 13, "Pop" }, { 14, "R&B" }, { 15, "Rap" }, { 16, "Reggae" }, { 17, "Rock" }, { 18, "Techno" }, { 26, "Ambient" },
        { 31, "Trance" }, { 35, "House" }, { 42, "Soul" }, { 52, "Electronic" } };
    const int n = std::atoi(g.c_str());
    for (const auto& x : names) if (x.first == n) return x.second;
    return g;
}
bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// the leading number of a string ("126", " 93.50 ", "1.2e2"); 0 when there is none. Locale-independent.
double toDouble(const std::string& text)
{
    const std::string s = trim(text);
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    double v = 0;
    bool any = false;
    while (i < s.size() && isDigit(s[i])) { v = v * 10 + (s[i++] - '0'); any = true; }
    if (i < s.size() && s[i] == '.') {
        ++i;
        double f = 0.1;
        while (i < s.size() && isDigit(s[i])) { v += f * (s[i++] - '0'); f *= 0.1; any = true; }
    }
    if (! any) return 0;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        bool eneg = false;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) eneg = s[j++] == '-';
        int e = 0;
        bool ed = false;
        while (j < s.size() && isDigit(s[j]) && e < 400) { e = e * 10 + (s[j++] - '0'); ed = true; }
        if (ed) v *= std::pow(10.0, eneg ? -e : e);
    }
    return neg ? -v : v;
}

uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }
uint32_t le32(const uint8_t* p) { return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[0]); }
uint32_t syncsafe(const uint8_t* p) { return (uint32_t(p[0] & 0x7f) << 21) | (uint32_t(p[1] & 0x7f) << 14) | (uint32_t(p[2] & 0x7f) << 7) | uint32_t(p[3] & 0x7f); }
float beFloat(const uint8_t* p) { const uint32_t v = be32(p); float f; std::memcpy(&f, &v, 4); return f; }

// permissive base64 (Serato leaves out padding and wraps lines)
Bytes base64(const uint8_t* p, size_t n)
{
    Bytes out;
    out.reserve(n * 3 / 4 + 3);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c = p[i];
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == 0 || c == '=') break;
        else continue;
        acc = (acc << 6) | (uint32_t) v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t) ((acc >> bits) & 0xff)); }
    }
    return out;
}

const char* const kKeyNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
const char* const kCamMaj[12] = { "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B" };
const char* const kCamMin[12] = { "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A" };

}  // namespace

//==============================================================================================================
// keys

std::string keyName(int key)
{
    if (key < 0 || key > 23) return {};
    return std::string(kKeyNames[key % 12]) + (key >= 12 ? "m" : "");
}

std::string camelot(int key)
{
    if (key < 0 || key > 23) return {};
    return key >= 12 ? kCamMin[key - 12] : kCamMaj[key];
}

int parseKey(const std::string& text)
{
    const std::string s = trim(text);
    if (s.empty()) return -1;
    // Camelot "8A" / "08A" / "8 A", Open Key "1m" / "1d"
    {
        size_t p = 0;
        int num = 0, digits = 0;
        while (p < s.size() && isDigit(s[p]) && digits < 3) { num = num * 10 + (s[p] - '0'); ++p; ++digits; }
        if (digits > 0 && num >= 1 && num <= 12) {
            const std::string rest = upper(trim(s.substr(p)));
            if (rest == "A" || rest == "B") {
                const std::string cam = std::to_string(num) + rest;
                for (int r = 0; r < 12; ++r) {
                    if (rest == "A" && cam == kCamMin[r]) return 12 + r;
                    if (rest == "B" && cam == kCamMaj[r]) return r;
                }
            }
            if (rest == "M" || rest == "D") {   // Open Key: 1m = Am (8A), 1d = C (8B)
                const int c = ((num + 6) % 12) + 1;
                return parseKey(std::to_string(c) + (rest == "M" ? "A" : "B"));
            }
            return -1;
        }
    }
    const char l = upper(s.substr(0, 1))[0];
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A..G
    if (l < 'A' || l > 'G') return -1;
    int r = base[l - 'A'];
    size_t p = 1;
    if (p < s.size()) {
        if (s[p] == '#') { ++r; ++p; }
        else if (s.compare(p, 3, "\xE2\x99\xAF") == 0) { ++r; p += 3; }   // U+266F sharp
        else if (s[p] == 'b') { --r; ++p; }
        else if (s.compare(p, 3, "\xE2\x99\xAD") == 0) { --r; p += 3; }   // U+266D flat
    }
    r = (r + 12) % 12;
    const std::string rest = lower(trim(s.substr(p)));
    bool minor = false;
    if (startsWith(rest, "maj")) minor = false;
    else if (startsWith(rest, "m")) minor = true;          // "m", "min", "minor"
    else if (! rest.empty() && ! startsWith(rest, "major")) {
        if (! (startsWith(rest, "(") || startsWith(rest, "/"))) return -1;
    }
    return minor ? 12 + r : r;
}

//==============================================================================================================
// tags

namespace {

void parseGeob(const std::string& desc, const uint8_t* d, size_t n, Tags& out)
{
    if (desc == "Serato Markers2" && n > 2) {
        size_t e = 2;
        while (e < n && d[e]) ++e;
        const Bytes raw = base64(d + 2, e - 2);
        if (raw.size() < 2) return;
        out.serato = true;
        size_t o = 2;
        while (o < raw.size()) {
            size_t te = o;
            while (te < raw.size() && raw[te]) ++te;
            if (te == o || te + 5 > raw.size()) break;
            const std::string type = latin1(raw.data() + o, te - o);
            const size_t len = be32(raw.data() + te + 1), p = te + 5;
            if (p + len > raw.size()) break;
            const uint8_t* r = raw.data() + p;
            if (type == "CUE" && len >= 13) {
                const int k = r[1];
                if (k >= 0 && k < kCues) {
                    auto& c = out.cues[(size_t) k];
                    c.t = be32(r + 2) / 1000.0;
                    c.rgb = (uint32_t(r[7]) << 16) | (uint32_t(r[8]) << 8) | r[9];
                    c.name = utf8(r + 12, len - 12);
                }
            } else if (type == "LOOP" && len >= 20) {
                const int k = r[1];
                const double a = be32(r + 2) / 1000.0, b = be32(r + 6) / 1000.0;
                if (k >= 0 && k < kCues && b > a) out.loops[(size_t) k] = { a, b };
            }
            o = p + len;
        }
    } else if (desc == "Serato BeatGrid" && n >= 14) {
        const uint32_t m = be32(d + 2);
        if (m < 1 || 6 + (size_t) m * 8 > n) return;
        const double first = beFloat(d + 6), bpm = beFloat(d + 6 + (size_t) (m - 1) * 8 + 4);
        if (bpm > 20 && bpm < 400) {
            out.hasGrid = true;
            out.serato = true;
            out.gridBpm = std::round(bpm * 100) / 100;
            const double bar = 4 * 60 / out.gridBpm;
            out.gridFirst = std::fmod(std::fmod(first, bar) + bar, bar);
        }
    } else if (desc == "Serato Autotags" && n > 2) {
        const double v = toDouble(latin1(d + 2, n - 2));
        if (v > 20 && v < 400) { out.autoBpm = v; out.serato = true; }
    }
}

void parseId3(const uint8_t* u, size_t n, Tags& out)
{
    if (n < 10 || u[0] != 'I' || u[1] != 'D' || u[2] != '3') return;
    const int ver = u[3];
    if (ver < 3 || ver > 4) return;
    const size_t end = std::min(n, (size_t) 10 + syncsafe(u + 6));
    size_t p = 10;
    if (u[5] & 0x40) {
        if (p + 4 > end) return;
        p += ver == 4 ? syncsafe(u + p) : be32(u + p) + 4;
    }
    while (p + 10 <= end) {
        const uint8_t* h = u + p;
        bool ok = true;
        for (int i = 0; i < 4; ++i) ok = ok && ((h[i] >= 'A' && h[i] <= 'Z') || (h[i] >= '0' && h[i] <= '9'));
        if (! ok) break;
        const std::string id((const char*) h, 4);
        const size_t sz = ver == 4 ? syncsafe(h + 4) : be32(h + 4);
        size_t fs = p + 10, fe = std::min(end, p + 10 + sz);
        p += 10 + sz;
        if (ver == 4 && (h[9] & 0x01) && fs + 4 <= fe) fs += 4;   // data length indicator
        if (fe <= fs) continue;
        const uint8_t* f = u + fs;
        const size_t fn = fe - fs;
        const uint8_t enc = f[0];
        if (id == "TIT2") out.title = trim(id3Text(f + 1, fn - 1, enc));
        else if (id == "TPE1") out.artist = trim(id3Text(f + 1, fn - 1, enc));
        else if (id == "TALB") out.album = trim(id3Text(f + 1, fn - 1, enc));
        else if (id == "TCON") out.genre = id3Genre(trim(id3Text(f + 1, fn - 1, enc)));
        else if (id == "TKEY") out.keyText = trim(id3Text(f + 1, fn - 1, enc));
        else if (id == "TBPM") { const double v = toDouble(id3Text(f + 1, fn - 1, enc)); if (v > 20 && v < 400) out.tagBpm = v; }
        else if (id == "GEOB") {
            const bool wide = enc == 1 || enc == 2;
            size_t q = 1;
            while (q < fn && f[q]) ++q;   // mime (always latin1)
            ++q;
            auto zt = [&]() -> size_t {   // skip a terminated string, return its end
                if (wide) {
                    while (q + 1 < fn && (f[q] || f[q + 1])) q += 2;
                    const size_t s0 = q;
                    q += 2;
                    return s0;
                }
                while (q < fn && f[q]) ++q;
                return q++;
            };
            zt();   // file name
            const size_t ds = q, de = zt();
            if (ds > fn || de > fn || q > fn) continue;
            parseGeob(id3Text(f + ds, de - ds, enc), f + q, fn - q, out);
        }
    }
}

void vorbisComment(const std::string& c, Tags& out)
{
    const size_t eq = c.find('=');
    if (eq == std::string::npos || eq == 0) return;
    const std::string key = upper(c.substr(0, eq)), val = c.substr(eq + 1);
    if (key == "TITLE") out.title = trim(val);
    else if (key == "ARTIST") out.artist = trim(val);
    else if (key == "ALBUM") out.album = trim(val);
    else if (key == "GENRE") out.genre = trim(val);
    else if (key == "BPM" || key == "TEMPO") { const double v = toDouble(val); if (v > 20 && v < 400) out.tagBpm = v; }
    else if (key == "INITIALKEY" || key == "KEY") out.keyText = trim(val);
    else if (startsWith(key, "SERATO_MARKERS") || key == "SERATO_BEATGRID") {
        const Bytes raw = base64((const uint8_t*) val.c_str(), std::strlen(val.c_str()));
        if (raw.empty()) return;
        const std::string nm = key == "SERATO_BEATGRID" ? "Serato BeatGrid" : "Serato Markers2";
        if (startsWith(key, "SERATO_MARKERS") && key.find('2') == std::string::npos) return;   // the old Markers_ format: skip
        const std::string pat = nm + std::string(1, '\0');
        auto it = std::search(raw.begin(), raw.end(), pat.begin(), pat.end());
        if (it != raw.end()) {
            const size_t at = (size_t) (it - raw.begin()) + pat.size();
            parseGeob(nm, raw.data() + at, raw.size() - at, out);
        } else {
            parseGeob(nm, raw.data(), raw.size(), out);
        }
    }
}

void parseVorbisComments(const uint8_t* b, size_t n, Tags& out)
{
    if (n < 8) return;
    size_t q = 4 + (size_t) le32(b);
    if (q + 4 > n) return;
    const uint32_t count = le32(b + q);
    q += 4;
    for (uint32_t k = 0; k < count && q + 4 <= n; ++k) {
        const size_t l = le32(b + q);
        if (q + 4 + l > n) break;
        vorbisComment(utf8(b + q + 4, l), out);
        q += 4 + l;
    }
}

constexpr size_t kMaxTag = 64u << 20;

Bytes readAt(ByteSource& in, uint64_t pos, size_t n)
{
    Bytes b;
    const uint64_t len = in.size();
    if (pos >= len) return b;
    b.resize((size_t) std::min<uint64_t>(n, len - pos));
    b.resize(in.read(pos, b.data(), b.size()));
    return b;
}

void readId3At(ByteSource& in, uint64_t pos, Tags& out, uint64_t* tagEnd = nullptr)
{
    const Bytes h = readAt(in, pos, 10);
    if (h.size() < 10 || h[0] != 'I' || h[1] != 'D' || h[2] != '3') return;
    const size_t total = std::min(kMaxTag, (size_t) 10 + syncsafe(h.data() + 6) + ((h[5] & 0x10) ? 10 : 0));
    if (tagEnd) *tagEnd = pos + total;
    const Bytes t = readAt(in, pos, total);
    parseId3(t.data(), t.size(), out);
}

void readId3v1(ByteSource& in, Tags& out)
{
    const uint64_t len = in.size();
    if (len < 128) return;
    const Bytes b = readAt(in, len - 128, 128);
    if (b.size() < 128 || b[0] != 'T' || b[1] != 'A' || b[2] != 'G') return;
    if (out.title.empty()) out.title = trim(latin1(b.data() + 3, 30));
    if (out.artist.empty()) out.artist = trim(latin1(b.data() + 33, 30));
    if (out.album.empty()) out.album = trim(latin1(b.data() + 63, 30));
}

void readFlac(ByteSource& in, uint64_t p, Tags& out)
{
    const uint64_t len = in.size();
    p += 4;
    for (int guard = 0; guard < 256 && p + 4 <= len; ++guard) {
        const Bytes h = readAt(in, p, 4);
        if (h.size() < 4) break;
        const bool last = h[0] & 0x80;
        const int type = h[0] & 0x7f;
        const size_t blen = (size_t(h[1]) << 16) | (size_t(h[2]) << 8) | h[3];
        if (type == 0 && blen >= 18) {   // STREAMINFO: length
            const Bytes s = readAt(in, p + 4, 18);
            if (s.size() >= 18) {
                const uint32_t rate = (uint32_t(s[10]) << 12) | (uint32_t(s[11]) << 4) | (uint32_t(s[12]) >> 4);
                const uint64_t total = (uint64_t(s[13] & 0x0f) << 32) | be32(s.data() + 14);
                if (rate > 0) out.length = (double) total / rate;
            }
        } else if (type == 4) {
            const Bytes b = readAt(in, p + 4, std::min(blen, kMaxTag));
            parseVorbisComments(b.data(), b.size(), out);
        }
        p += 4 + (uint64_t) blen;
        if (last) break;
    }
}

void readOgg(ByteSource& in, Tags& out)
{
    const Bytes b = readAt(in, 0, 256 * 1024);
    static const uint8_t sig[7] = { 3, 'v', 'o', 'r', 'b', 'i', 's' };
    auto it = std::search(b.begin(), b.end(), sig, sig + 7);
    if (it == b.end()) return;
    const size_t at = (size_t) (it - b.begin()) + 7;
    parseVorbisComments(b.data() + at, b.size() - at, out);
}

void readRiff(ByteSource& in, bool aiff, Tags& out)
{
    const uint64_t len = in.size();
    uint64_t p = 12;
    for (int guard = 0; guard < 4096 && p + 8 <= len; ++guard) {
        const Bytes h = readAt(in, p, 8);
        if (h.size() < 8) break;
        const size_t clen = aiff ? be32(h.data() + 4) : le32(h.data() + 4);
        const std::string id((const char*) h.data(), 4);
        if (id == "ID3 " || id == "id3 ") readId3At(in, p + 8, out);
        else if (! aiff && id == "LIST" && clen >= 4 && clen < (1u << 20)) {
            const Bytes l = readAt(in, p + 8, clen);
            if (l.size() >= 4 && std::memcmp(l.data(), "INFO", 4) == 0) {
                size_t q = 4;
                while (q + 8 <= l.size()) {
                    const std::string sid((const char*) l.data() + q, 4);
                    const size_t sl = le32(l.data() + q + 4);
                    if (q + 8 + sl > l.size()) break;
                    const std::string v = trim(utf8(l.data() + q + 8, sl));
                    if (sid == "INAM" && out.title.empty()) out.title = v;
                    else if (sid == "IART" && out.artist.empty()) out.artist = v;
                    else if (sid == "IPRD" && out.album.empty()) out.album = v;
                    else if (sid == "IGNR" && out.genre.empty()) out.genre = v;
                    q += 8 + sl + (sl & 1);
                }
            }
        }
        p += 8 + (uint64_t) clen + (uint64_t) (clen & 1);
    }
}

// MP4 / M4A: moov/mvhd for the length, moov/udta/meta/ilst for tags (incl. Serato's "----" freeform items)
void mp4Atoms(const uint8_t* b, size_t n, Tags& out, int depth)
{
    size_t p = 0;
    while (p + 8 <= n && depth < 8) {
        uint64_t sz = be32(b + p);
        size_t hdr = 8;
        if (sz == 1 && p + 16 <= n) { sz = (uint64_t(be32(b + p + 8)) << 32) | be32(b + p + 12); hdr = 16; }
        else if (sz == 0) sz = n - p;
        if (sz < hdr || p + sz > n) break;
        const uint8_t* ty = b + p + 4;
        const auto is = [ty](const char* name) { return std::memcmp(ty, name, 4) == 0; };
        const uint8_t* c = b + p + hdr;
        const size_t cn = (size_t) sz - hdr;
        if (is("moov") || is("udta") || is("ilst")) mp4Atoms(c, cn, out, depth + 1);
        else if (is("meta") && cn > 4) mp4Atoms(c + 4, cn - 4, out, depth + 1);
        else if (is("mvhd") && cn >= 20) {
            const bool v1 = c[0] == 1;
            const double scale = v1 ? be32(c + 20) : be32(c + 12);
            const double dur = v1 ? (double) ((uint64_t(be32(c + 24)) << 32) | be32(c + 28)) : be32(c + 16);
            if (scale > 0 && (! v1 || cn >= 32)) out.length = dur / scale;
        } else if (depth >= 3) {
            // an ilst item: children "mean", "name", "data"
            std::string meanName;
            const uint8_t* data = nullptr;
            size_t dataLen = 0;
            uint32_t dataType = 0;
            size_t q = 0;
            while (q + 8 <= cn) {
                const size_t s2 = be32(c + q);
                if (s2 < 8 || q + s2 > cn) break;
                if (std::memcmp(c + q + 4, "name", 4) == 0 && s2 > 12) meanName = utf8(c + q + 12, s2 - 12);
                else if (std::memcmp(c + q + 4, "data", 4) == 0 && s2 >= 16 && ! data) { dataType = be32(c + q + 8) & 0xffffff; data = c + q + 16; dataLen = s2 - 16; }
                q += s2;
            }
            if (data) {
                const std::string s = utf8(data, dataLen);
                if (is("\xA9nam")) out.title = trim(s);
                else if (is("\xA9" "ART")) out.artist = trim(s);
                else if (is("\xA9" "alb")) out.album = trim(s);
                else if (is("\xA9" "gen")) out.genre = trim(s);
                else if (is("tmpo") && dataLen >= 2 && dataType != 1) { const double v = (data[0] << 8) | data[1]; if (v > 20 && v < 400) out.tagBpm = v; }
                else if (is("----")) {
                    const std::string nm = lower(meanName);
                    if (nm == "initialkey" || nm == "key") out.keyText = trim(s);
                    else if (nm == "bpm") { const double v = toDouble(s); if (v > 20 && v < 400) out.tagBpm = v; }
                    else if (nm == "markersv2") vorbisComment("SERATO_MARKERS_V2=" + s, out);
                    else if (nm == "beatgrid") vorbisComment("SERATO_BEATGRID=" + s, out);
                    else if (nm == "autotags") vorbisComment("SERATO_AUTOTAGS=" + s, out);
                }
            }
        }
        p += (size_t) sz;
    }
}

void readMp4(ByteSource& in, Tags& out)
{
    const uint64_t len = in.size();
    uint64_t p = 0;
    for (int guard = 0; guard < 64 && p + 8 <= len; ++guard) {
        const Bytes h = readAt(in, p, 16);
        if (h.size() < 8) break;
        uint64_t sz = be32(h.data());
        if (sz == 1 && h.size() >= 16) sz = (uint64_t(be32(h.data() + 8)) << 32) | be32(h.data() + 12);
        else if (sz == 0) sz = len - p;
        if (sz < 8) break;
        if (std::memcmp(h.data() + 4, "moov", 4) == 0 && sz < kMaxTag) {
            const Bytes m = readAt(in, p, (size_t) sz);
            mp4Atoms(m.data(), m.size(), out, 0);
            break;
        }
        p += sz;
    }
}

}  // namespace

Tags readTags(ByteSource& in)
{
    Tags t;
    const Bytes h = readAt(in, 0, 12);
    if (h.size() < 4) return t;
    const auto is = [&](size_t o, const char* s) { return h.size() >= o + 4 && std::memcmp(h.data() + o, s, 4) == 0; };
    if (is(0, "fLaC")) readFlac(in, 0, t);
    else if (is(0, "RIFF") || is(0, "RF64")) readRiff(in, false, t);
    else if (is(0, "FORM")) readRiff(in, true, t);
    else if (is(0, "OggS")) readOgg(in, t);
    else if (is(4, "ftyp")) readMp4(in, t);
    else if (h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
        uint64_t end = 0;
        readId3At(in, 0, t, &end);
        const Bytes after = readAt(in, end, 4);
        if (after.size() == 4 && std::memcmp(after.data(), "fLaC", 4) == 0) readFlac(in, end, t);
        else if (t.title.empty() || t.artist.empty()) readId3v1(in, t);
    } else readId3v1(in, t);
    return t;
}

void applyTags(const Tags& g, Track& t)
{
    t.title = g.title;
    t.artist = g.artist;
    t.album = g.album;
    const int key = parseKey(g.keyText);
    if (key >= 0) t.key = key;
    bool anyCue = false;
    for (int i = 0; i < kCues; ++i) {
        if (g.cues[(size_t) i].set()) { t.cues[(size_t) i] = g.cues[(size_t) i]; anyCue = true; }
        if (g.loops[(size_t) i].set()) { t.loops[(size_t) i] = g.loops[(size_t) i]; anyCue = true; }
    }
    if (g.hasGrid) { t.bpm = g.gridBpm; t.firstBeat = g.gridFirst; }
    else if (g.autoBpm > 0) t.bpm = g.autoBpm;
    else if (g.tagBpm > 0) t.bpm = g.tagBpm;
    t.fromSerato = g.hasGrid || anyCue;
}

std::vector<std::string> parseCrate(const uint8_t* u, size_t n)
{
    std::vector<std::string> paths;
    if (u == nullptr) return paths;
    std::function<void(size_t, size_t, int)> walk = [&](size_t s, size_t e, int depth) {
        size_t p = s;
        while (p + 8 <= e) {
            const size_t len = be32(u + p + 4), b = p + 8, end = std::min(e, b + len);
            if (std::memcmp(u + p, "otrk", 4) == 0 && depth < 4) walk(b, end, depth + 1);
            else if (std::memcmp(u + p, "ptrk", 4) == 0) {
                std::string path = utf16(u + b, end - b, true);
                if (! path.empty()) paths.push_back(std::move(path));
            }
            if (b + len > e) break;
            p = b + len;
        }
    };
    walk(0, n, 0);
    return paths;
}

// a Serato smart crate (.scrate): the same tag / length / payload records as a .crate. Layout from community reverse
// engineering (serato-tools): "rart" / "rlut" hold a "brut" byte (match all / live update); each "rurt" rule holds
// "trft" (comparison, UTF-16BE), "urkt" (field id, u32) and one value: "trpt" (text), "urpt" (u32) or "trtt" (time).
SmartCrate parseSmartCrate(const uint8_t* u, size_t n)
{
    SmartCrate sc;
    if (u == nullptr) return sc;
    sc.paths = parseCrate(u, n);
    // the first "brut" byte inside a record (or the record's own byte when it is just one)
    const auto flag = [u](size_t b, size_t e, bool def) {
        if (e - b == 1) return u[b] != 0;
        for (size_t p = b; p + 8 <= e;) {
            const size_t len = be32(u + p + 4), q = p + 8;
            if (q + len > e) break;
            if (std::memcmp(u + p, "brut", 4) == 0 && len >= 1) return u[q] != 0;
            p = q + len;
        }
        return def;
    };
    for (size_t p = 0; p + 8 <= n;) {
        const size_t len = be32(u + p + 4), b = p + 8, e = std::min(n, b + len);
        if (std::memcmp(u + p, "rart", 4) == 0) sc.matchAll = flag(b, e, true);
        else if (std::memcmp(u + p, "rlut", 4) == 0) sc.liveUpdate = flag(b, e, false);
        else if (std::memcmp(u + p, "rurt", 4) == 0) {
            SmartRule r;
            for (size_t q = b; q + 8 <= e;) {
                const size_t l2 = be32(u + q + 4), c = q + 8;
                if (c + l2 > e) break;
                const auto tag = [&](const char* t) { return std::memcmp(u + q, t, 4) == 0; };
                if (tag("trft")) r.op = utf16(u + c, l2, true);
                else if (tag("urkt") && l2 >= 4) r.field = (int) be32(u + c);
                else if (tag("trpt")) r.text = utf16(u + c, l2, true);
                else if ((tag("urpt") || tag("trtt")) && l2 >= 4) { r.num = be32(u + c); r.hasNum = true; }
                q = c + l2;
            }
            sc.rules.push_back(std::move(r));
        }
        if (b + len > n) break;
        p = b + len;
    }
    return sc;
}

namespace {
bool strOp(const std::string& op) { return op == "cond_con_str" || op == "cond_dnc_str" || op == "cond_is_str" || op == "cond_isn_str"; }
bool numOp(const std::string& op) { return op == "cond_greq_uint" || op == "cond_lseq_uint"; }
const std::string* textField(int field, const SmartFields& f)
{
    switch (field) {
        case smartField::fileName: return &f.fileName;
        case smartField::title: return &f.title;
        case smartField::artist: return &f.artist;
        case smartField::album: return &f.album;
        case smartField::genre: return &f.genre;
        default: return nullptr;
    }
}
}  // namespace

bool smartRuleSupported(const SmartRule& r)
{
    if (r.field == smartField::bpm) return numOp(r.op);
    if (r.field == smartField::key) return strOp(r.op);
    SmartFields none;
    return textField(r.field, none) != nullptr && strOp(r.op);
}

int smartRuleMatch(const SmartRule& r, const SmartFields& f)
{
    if (! smartRuleSupported(r)) return -1;
    if (r.field == smartField::bpm) {
        if (f.bpm <= 0) return 0;
        const double want = r.hasNum ? (double) r.num : std::atof(r.text.c_str());
        const double have = std::round(f.bpm);   // Serato shows (and compares) whole BPM
        return r.op == "cond_greq_uint" ? have >= want : have <= want;
    }
    const std::string want = lower(trim(r.text));
    bool is = false, has = false;
    if (r.field == smartField::key) {
        const int a = parseKey(f.keyText), b = parseKey(r.text);
        if (a >= 0 && b >= 0) is = a == b;
        else is = lower(trim(f.keyText)) == want;
        has = is || lower(f.keyText).find(want) != std::string::npos
              || (a >= 0 && (lower(keyName(a)).find(want) != std::string::npos || lower(camelot(a)).find(want) != std::string::npos));
    } else {
        const std::string have = lower(*textField(r.field, f));
        is = trim(have) == want;
        has = have.find(want) != std::string::npos;
    }
    if (r.op == "cond_con_str") return has;
    if (r.op == "cond_dnc_str") return ! has;
    if (r.op == "cond_is_str") return is;
    return ! is;   // cond_isn_str
}

bool smartMatch(const SmartCrate& c, const SmartFields& f)
{
    if (c.rules.empty()) return false;
    for (const auto& r : c.rules) {
        const bool ok = smartRuleMatch(r, f) == 1;   // a rule this can't evaluate never matches
        if (c.matchAll && ! ok) return false;
        if (! c.matchAll && ok) return true;
    }
    return c.matchAll;
}

void nameFromFileName(const std::string& fileName, std::string& title, std::string& artist)
{
    std::string s = fileName;
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const size_t dot = s.rfind('.');
    if (dot != std::string::npos && dot > 0) s = s.substr(0, dot);
    std::replace(s.begin(), s.end(), '_', ' ');
    s = trim(s);
    size_t dash = s.find(" - ");
    auto onlyDigits = [](const std::string& x) { return x.find_first_not_of("0123456789.") == std::string::npos; };
    if (dash != std::string::npos && dash > 0 && onlyDigits(trim(s.substr(0, dash)))) { s = trim(s.substr(dash + 3)); dash = s.find(" - "); }
    else {
        size_t k = 0;
        while (k < s.size() && isDigit(s[k])) ++k;
        if (k > 0 && k <= 3 && k < s.size() && (s[k] == '.' || s[k] == ' ') && s.size() > k + 2) { s = trim(s.substr(k + 1)); dash = s.find(" - "); }
    }
    if (dash != std::string::npos && dash > 0) {
        if (artist.empty()) artist = trim(s.substr(0, dash));
        if (title.empty()) title = trim(s.substr(dash + 3));
    } else if (title.empty()) title = s;
}

}  // namespace wheel
