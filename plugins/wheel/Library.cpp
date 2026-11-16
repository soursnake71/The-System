// Omega Wheel of Steel library: crates (Serato, playlists, folders), Serato / ID3 / Vorbis / MP4 tags, decoding and
// analysis (BPM + first downbeat, key, waveform).
#include "Library.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace wheel {

//==============================================================================================================
// keys
namespace {
const char* const kKeyNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
const char* const kCamMaj[12] = { "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B" };
const char* const kCamMin[12] = { "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A" };
}  // namespace

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
    juce::String s = juce::String::fromUTF8(text.c_str()).trim();
    if (s.isEmpty()) return -1;
    // Camelot "8A" / "08A" / "8 A", Open Key "1m" / "1d"
    {
        int p = 0, num = 0, digits = 0;
        while (p < s.length() && juce::CharacterFunctions::isDigit(s[p]) && digits < 3) { num = num * 10 + (s[p] - '0'); ++p; ++digits; }
        if (digits > 0 && num >= 1 && num <= 12) {
            juce::String rest = s.substring(p).trim().toUpperCase();
            if (rest == "A" || rest == "B") {
                const juce::String cam = juce::String(num) + rest;
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
    const juce::juce_wchar l = juce::CharacterFunctions::toUpperCase(s[0]);
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A..G
    if (l < 'A' || l > 'G') return -1;
    int r = base[l - 'A'], p = 1;
    if (p < s.length()) {
        const juce::juce_wchar a = s[p];
        if (a == '#' || a == 0x266F) { ++r; ++p; }
        else if (a == 'b' || a == 0x266D) { --r; ++p; }
    }
    r = (r + 12) % 12;
    juce::String rest = s.substring(p).trim().toLowerCase();
    bool minor = false;
    if (rest.startsWith("maj")) minor = false;
    else if (rest.startsWith("m")) minor = true;          // "m", "min", "minor"
    else if (rest.isNotEmpty() && ! rest.startsWith("major")) {
        if (! (rest.startsWith("(") || rest.startsWith("/"))) return -1;
    }
    return minor ? 12 + r : r;
}

//==============================================================================================================
// small helpers
namespace {

using Bytes = std::vector<uint8_t>;

uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }
uint32_t le32(const uint8_t* p) { return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[0]); }
uint32_t syncsafe(const uint8_t* p) { return (uint32_t(p[0] & 0x7f) << 21) | (uint32_t(p[1] & 0x7f) << 14) | (uint32_t(p[2] & 0x7f) << 7) | uint32_t(p[3] & 0x7f); }
float beFloat(const uint8_t* p) { const uint32_t v = be32(p); float f; std::memcpy(&f, &v, 4); return f; }

juce::String latin1(const uint8_t* p, size_t n)
{
    juce::String s;
    s.preallocateBytes(n + 1);
    for (size_t i = 0; i < n && p[i]; ++i) s += (juce::juce_wchar) p[i];
    return s;
}

juce::String utf8(const uint8_t* p, size_t n)
{
    size_t e = 0;
    while (e < n && p[e]) ++e;
    return juce::String::fromUTF8((const char*) p, (int) e);
}

juce::String utf16(const uint8_t* p, size_t n, bool bigEndian)
{
    std::vector<juce::CharPointer_UTF16::CharType> w;
    w.reserve(n / 2 + 1);
    for (size_t i = 0; i + 1 < n; i += 2) {
        const auto c = (uint16_t) (bigEndian ? (p[i] << 8) | p[i + 1] : (p[i + 1] << 8) | p[i]);
        if (c == 0) break;
        w.push_back((juce::CharPointer_UTF16::CharType) c);
    }
    w.push_back(0);
    return juce::String(juce::CharPointer_UTF16(w.data()));
}

// ID3 text with its encoding byte (0 latin1, 1 UTF-16 with BOM, 2 UTF-16BE, 3 UTF-8)
juce::String id3Text(const uint8_t* p, size_t n, uint8_t enc)
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

double toDouble(const juce::String& s) { return s.trim().getDoubleValue(); }

// everything the tags say
struct Tags {
    juce::String title, artist, album, keyText;
    double tagBpm = 0, autoBpm = 0, gridBpm = 0, gridFirst = 0, length = 0;
    bool hasGrid = false, serato = false;
    std::array<HotCue, kCues> cues;
    std::array<SavedLoop, kCues> loops;
};

void parseGeob(const juce::String& desc, const uint8_t* d, size_t n, Tags& out)
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
            const juce::String type = latin1(raw.data() + o, te - o);
            const size_t len = be32(raw.data() + te + 1), p = te + 5;
            if (p + len > raw.size()) break;
            const uint8_t* r = raw.data() + p;
            if (type == "CUE" && len >= 13) {
                const int k = r[1];
                if (k >= 0 && k < kCues) {
                    auto& c = out.cues[(size_t) k];
                    c.t = be32(r + 2) / 1000.0;
                    c.rgb = (uint32_t(r[7]) << 16) | (uint32_t(r[8]) << 8) | r[9];
                    c.name = utf8(r + 12, len - 12).toStdString();
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
        const juce::String id = latin1(h, 4);
        const size_t sz = ver == 4 ? syncsafe(h + 4) : be32(h + 4);
        size_t fs = p + 10, fe = std::min(end, p + 10 + sz);
        p += 10 + sz;
        if (ver == 4 && (h[9] & 0x01) && fs + 4 <= fe) fs += 4;   // data length indicator
        if (fe <= fs) continue;
        const uint8_t* f = u + fs;
        const size_t fn = fe - fs;
        const uint8_t enc = f[0];
        if (id == "TIT2") out.title = id3Text(f + 1, fn - 1, enc).trim();
        else if (id == "TPE1") out.artist = id3Text(f + 1, fn - 1, enc).trim();
        else if (id == "TALB") out.album = id3Text(f + 1, fn - 1, enc).trim();
        else if (id == "TKEY") out.keyText = id3Text(f + 1, fn - 1, enc).trim();
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

void vorbisComment(const juce::String& c, Tags& out)
{
    const int eq = c.indexOfChar('=');
    if (eq <= 0) return;
    const juce::String key = c.substring(0, eq).toUpperCase(), val = c.substring(eq + 1);
    if (key == "TITLE") out.title = val.trim();
    else if (key == "ARTIST") out.artist = val.trim();
    else if (key == "ALBUM") out.album = val.trim();
    else if (key == "BPM" || key == "TEMPO") { const double v = toDouble(val); if (v > 20 && v < 400) out.tagBpm = v; }
    else if (key == "INITIALKEY" || key == "KEY") out.keyText = val.trim();
    else if (key.startsWith("SERATO_MARKERS") || key == "SERATO_BEATGRID") {
        const auto u8 = val.toRawUTF8();
        const Bytes raw = base64((const uint8_t*) u8, std::strlen(u8));
        if (raw.empty()) return;
        const juce::String nm = key == "SERATO_BEATGRID" ? "Serato BeatGrid" : "Serato Markers2";
        if (key.startsWith("SERATO_MARKERS") && ! key.contains("2")) return;   // the old Markers_ format: skip
        const std::string pat = nm.toStdString() + std::string(1, '\0');
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
        vorbisComment(juce::String::fromUTF8((const char*) b + q + 4, (int) l), out);
        q += 4 + l;
    }
}

Bytes readAt(juce::FileInputStream& in, juce::int64 pos, size_t n)
{
    Bytes b;
    if (! in.setPosition(pos)) return b;
    b.resize(n);
    const int got = in.read(b.data(), (int) n);
    b.resize((size_t) std::max(0, got));
    return b;
}

constexpr size_t kMaxTag = 64u << 20;

void readId3At(juce::FileInputStream& in, juce::int64 pos, Tags& out, juce::int64* tagEnd = nullptr)
{
    const Bytes h = readAt(in, pos, 10);
    if (h.size() < 10 || h[0] != 'I' || h[1] != 'D' || h[2] != '3') return;
    const size_t total = std::min(kMaxTag, (size_t) 10 + syncsafe(h.data() + 6) + ((h[5] & 0x10) ? 10 : 0));
    if (tagEnd) *tagEnd = pos + (juce::int64) total;
    const Bytes t = readAt(in, pos, total);
    parseId3(t.data(), t.size(), out);
}

void readId3v1(juce::FileInputStream& in, Tags& out)
{
    const juce::int64 len = in.getTotalLength();
    if (len < 128) return;
    const Bytes b = readAt(in, len - 128, 128);
    if (b.size() < 128 || b[0] != 'T' || b[1] != 'A' || b[2] != 'G') return;
    if (out.title.isEmpty()) out.title = latin1(b.data() + 3, 30).trim();
    if (out.artist.isEmpty()) out.artist = latin1(b.data() + 33, 30).trim();
    if (out.album.isEmpty()) out.album = latin1(b.data() + 63, 30).trim();
}

void readFlac(juce::FileInputStream& in, juce::int64 p, Tags& out)
{
    const juce::int64 len = in.getTotalLength();
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
        p += 4 + (juce::int64) blen;
        if (last) break;
    }
}

void readOgg(juce::FileInputStream& in, Tags& out)
{
    const Bytes b = readAt(in, 0, 256 * 1024);
    static const uint8_t sig[7] = { 3, 'v', 'o', 'r', 'b', 'i', 's' };
    auto it = std::search(b.begin(), b.end(), sig, sig + 7);
    if (it == b.end()) return;
    const size_t at = (size_t) (it - b.begin()) + 7;
    parseVorbisComments(b.data() + at, b.size() - at, out);
}

void readRiff(juce::FileInputStream& in, bool aiff, Tags& out)
{
    const juce::int64 len = in.getTotalLength();
    juce::int64 p = 12;
    for (int guard = 0; guard < 4096 && p + 8 <= len; ++guard) {
        const Bytes h = readAt(in, p, 8);
        if (h.size() < 8) break;
        const size_t clen = aiff ? be32(h.data() + 4) : le32(h.data() + 4);
        const juce::String id = latin1(h.data(), 4);
        if (id == "ID3 " || id == "id3 ") readId3At(in, p + 8, out);
        else if (! aiff && id == "LIST" && clen >= 4 && clen < (1u << 20)) {
            const Bytes l = readAt(in, p + 8, clen);
            if (l.size() >= 4 && std::memcmp(l.data(), "INFO", 4) == 0) {
                size_t q = 4;
                while (q + 8 <= l.size()) {
                    const juce::String sid = latin1(l.data() + q, 4);
                    const size_t sl = le32(l.data() + q + 4);
                    if (q + 8 + sl > l.size()) break;
                    const juce::String v = utf8(l.data() + q + 8, sl).trim();
                    if (sid == "INAM" && out.title.isEmpty()) out.title = v;
                    else if (sid == "IART" && out.artist.isEmpty()) out.artist = v;
                    else if (sid == "IPRD" && out.album.isEmpty()) out.album = v;
                    q += 8 + sl + (sl & 1);
                }
            }
        }
        p += 8 + (juce::int64) clen + (juce::int64) (clen & 1);
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
            juce::String meanName;
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
                const juce::String s = utf8(data, dataLen);
                if (is("\xA9nam")) out.title = s.trim();
                else if (is("\xA9" "ART")) out.artist = s.trim();
                else if (is("\xA9" "alb")) out.album = s.trim();
                else if (is("tmpo") && dataLen >= 2 && dataType != 1) { const double v = (data[0] << 8) | data[1]; if (v > 20 && v < 400) out.tagBpm = v; }
                else if (is("----")) {
                    const juce::String nm = meanName.toLowerCase();
                    if (nm == "initialkey" || nm == "key") out.keyText = s.trim();
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

void readMp4(juce::FileInputStream& in, Tags& out)
{
    const juce::int64 len = in.getTotalLength();
    juce::int64 p = 0;
    for (int guard = 0; guard < 64 && p + 8 <= len; ++guard) {
        const Bytes h = readAt(in, p, 16);
        if (h.size() < 8) break;
        uint64_t sz = be32(h.data());
        if (sz == 1 && h.size() >= 16) sz = (uint64_t(be32(h.data() + 8)) << 32) | be32(h.data() + 12);
        else if (sz == 0) sz = (uint64_t) (len - p);
        if (sz < 8) break;
        if (std::memcmp(h.data() + 4, "moov", 4) == 0 && sz < kMaxTag) {
            const Bytes m = readAt(in, p, (size_t) sz);
            mp4Atoms(m.data(), m.size(), out, 0);
            break;
        }
        p += (juce::int64) sz;
    }
}

Tags readTags(const juce::File& f)
{
    Tags t;
    juce::FileInputStream in(f);
    if (! in.openedOk()) return t;
    const Bytes h = readAt(in, 0, 12);
    if (h.size() < 4) return t;
    const auto is = [&](size_t o, const char* s) { return h.size() >= o + 4 && std::memcmp(h.data() + o, s, 4) == 0; };
    if (is(0, "fLaC")) readFlac(in, 0, t);
    else if (is(0, "RIFF") || is(0, "RF64")) readRiff(in, false, t);
    else if (is(0, "FORM")) readRiff(in, true, t);
    else if (is(0, "OggS")) readOgg(in, t);
    else if (is(4, "ftyp")) readMp4(in, t);
    else if (h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
        juce::int64 end = 0;
        readId3At(in, 0, t, &end);
        const Bytes after = readAt(in, end, 4);
        if (after.size() == 4 && std::memcmp(after.data(), "fLaC", 4) == 0) readFlac(in, end, t);
        else if (t.title.isEmpty() || t.artist.isEmpty()) readId3v1(in, t);
    } else readId3v1(in, t);
    return t;
}

// "Artist - Title.mp3", "01 - Artist - Title.mp3", "01. Title.mp3"
void nameFromFile(const juce::File& f, juce::String& title, juce::String& artist)
{
    juce::String s = f.getFileNameWithoutExtension().replaceCharacter('_', ' ').trim();
    int dash = s.indexOf(" - ");
    if (dash > 0 && s.substring(0, dash).trim().containsOnly("0123456789.")) { s = s.substring(dash + 3).trim(); dash = s.indexOf(" - "); }
    else {
        int k = 0;
        while (k < s.length() && juce::CharacterFunctions::isDigit(s[k])) ++k;
        if (k > 0 && k <= 3 && k < s.length() && (s[k] == '.' || s[k] == ' ') && s.length() > k + 2) { s = s.substring(k + 1).trim(); dash = s.indexOf(" - "); }
    }
    if (dash > 0) {
        if (artist.isEmpty()) artist = s.substring(0, dash).trim();
        if (title.isEmpty()) title = s.substring(dash + 3).trim();
    } else if (title.isEmpty()) title = s;
}

//==============================================================================================================
// caches shared by all Library instances (tags per file, the file index per root)

struct CachedEntry { juce::int64 mtime = 0, size = 0; Entry e; };
std::mutex entryMutex;
std::unordered_map<std::string, CachedEntry> entryCache;

// fold a path component for matching across OSes: lower-case ASCII letters and digits only; an accented letter (NFC or
// NFD) drops out entirely so "Beyoncé" matches whichever normalisation the other OS used
std::string fold(const juce::String& s)
{
    std::string o;
    o.reserve((size_t) s.length());
    for (auto p = s.getCharPointer(); ! p.isEmpty();) {
        const juce::juce_wchar c = p.getAndAdvance();
        if (c >= 0x300 && c <= 0x36f) { if (! o.empty()) o.pop_back(); continue; }
        if (c < 128 && juce::CharacterFunctions::isLetterOrDigit(c)) o += (char) juce::CharacterFunctions::toLowerCase(c);
    }
    return o;
}

juce::StringArray components(juce::String path)
{
    path = path.replaceCharacter('\\', '/');
    juce::StringArray a;
    a.addTokens(path, "/", "");
    a.removeEmptyStrings();
    return a;
}

struct RootIndex {
    juce::Array<juce::File> audio;                                  // all audio under the root (capped)
    std::unordered_map<std::string, std::vector<int>> byName;       // folded file name -> indices
    juce::Array<Crate> folders, playlists;
};
std::mutex indexMutex;
std::map<std::string, std::shared_ptr<RootIndex>> indexes;

std::shared_ptr<RootIndex> indexFor(const juce::File& root)
{
    std::lock_guard<std::mutex> g(indexMutex);
    auto it = indexes.find(root.getFullPathName().toStdString());
    return it == indexes.end() ? nullptr : it->second;
}

constexpr int kMaxAudio = 20000, kMaxDirs = 6000, kMaxDepth = 8, kMaxFolderCrates = 2000;

bool skipDir(const juce::File& d)
{
    const juce::String n = d.getFileName();
    if (n.isEmpty() || n.startsWithChar('.') || n.startsWithChar('$') || d.isHidden() || d.isSymbolicLink()) return true;
    static const char* const skip[] = { "_Serato_", "_Serato_Backup", "System Volume Information", "LOST.DIR", "lost+found",
                                        "RECYCLER", "Recycled", "Autosave", "Expansions", "node_modules", "Library", "AppData",
                                        "Applications", "Program Files", "Program Files (x86)", "Windows", "proc", "sys", "dev" };
    for (auto* s : skip) if (n.equalsIgnoreCase(s)) return true;
    return n.containsIgnoreCase("[ProjectData]") || n.endsWithIgnoreCase("_[ProjectData]");
}

struct Walker {
    explicit Walker(RootIndex& i) : idx(i) {}
    RootIndex& idx;
    int dirs = 0;
    juce::uint32 startMs = juce::Time::getMillisecondCounter();
    bool outOfTime() const { return juce::Time::getMillisecondCounter() - startMs > 5000; }

    // returns whether the folder (or anything below it) holds audio
    bool walk(const juce::File& dir, int depth)
    {
        if (++dirs > kMaxDirs || outOfTime()) return false;
        juce::Array<juce::File> files, subs;
        for (const auto& e : juce::RangedDirectoryIterator(dir, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles)) {
            const auto& f = e.getFile();
            if (e.isDirectory()) { if (! skipDir(f)) subs.add(f); }
            else if (Library::isAudio(f)) files.add(f);
            else if (depth <= 2 && (f.hasFileExtension("m3u") || f.hasFileExtension("m3u8")))
                idx.playlists.add({ f.getFileNameWithoutExtension(), 0, Crate::playlist, f });
        }
        files.sort();
        subs.sort();
        for (const auto& f : files) {
            if (idx.audio.size() >= kMaxAudio) break;
            idx.byName[fold(f.getFileName())].push_back(idx.audio.size());
            idx.audio.add(f);
        }
        bool any = ! files.isEmpty();
        if (depth >= kMaxDepth) return any;
        for (const auto& s : subs) {
            const int at = idx.folders.size();
            if (walk(s, depth + 1)) {
                any = true;
                if (idx.folders.size() < kMaxFolderCrates) idx.folders.insert(at, { s.getFileName(), depth, Crate::folder, s });
            }
        }
        return any;
    }
};

void loadM3u(const juce::File& f, juce::StringArray& out)
{
    juce::StringArray lines;
    lines.addLines(f.loadFileAsString());
    for (auto l : lines) {
        l = l.trim();
        if (l.isEmpty() || l.startsWithChar('#')) continue;
        if (l.startsWithIgnoreCase("file://")) l = juce::URL::removeEscapeChars(l.substring(7));
        out.add(l);
    }
}

}  // namespace

//==============================================================================================================
// crates

juce::StringArray parseCrate(const juce::MemoryBlock& data)
{
    juce::StringArray paths;
    const auto* u = (const uint8_t*) data.getData();
    std::function<void(size_t, size_t, int)> walk = [&](size_t s, size_t e, int depth) {
        size_t p = s;
        while (p + 8 <= e) {
            const size_t len = be32(u + p + 4), b = p + 8, end = std::min(e, b + len);
            if (std::memcmp(u + p, "otrk", 4) == 0 && depth < 4) walk(b, end, depth + 1);
            else if (std::memcmp(u + p, "ptrk", 4) == 0) {
                const juce::String path = utf16(u + b, end - b, true);
                if (path.isNotEmpty()) paths.add(path);
            }
            if (b + len > e) break;
            p = b + len;
        }
    };
    walk(0, data.getSize(), 0);
    return paths;
}

Library::Library() { formats.registerBasicFormats(); }

bool Library::isAudio(const juce::File& f)
{
    if (f.getFileName().startsWithChar('.')) return false;   // "._x.mp3" AppleDouble files
    return f.hasFileExtension("mp3;wav;wave;aif;aiff;aifc;flac;ogg;m4a;mp4;aac;alac;caf;wma");
}

void Library::setRoot(const juce::File& root)
{
    const juce::ScopedLock scoped(scanLock);
    rootDir = root;
    seratoDir = juce::File();
    crateList.clearQuick();
    if (! root.isDirectory()) return;

    // the _Serato_ folder: in the root, in <root>/Music, or in a parent (up to the volume root)
    for (juce::File d = root;; d = d.getParentDirectory()) {
        for (auto c : { d.getChildFile("_Serato_"), d.getChildFile("Music").getChildFile("_Serato_") })
            if (c.isDirectory()) { seratoDir = c; break; }
        if (seratoDir.exists() || d.getParentDirectory() == d) break;
    }

    crateList.add({ "All tracks", 0, Crate::all, root });

    if (seratoDir.isDirectory()) {
        struct C { juce::StringArray parts; juce::File f; };
        std::vector<C> cs;
        for (const auto& e : juce::RangedDirectoryIterator(seratoDir.getChildFile("Subcrates"), false, "*.crate")) {
            juce::StringArray parts;
            juce::String nm = e.getFile().getFileNameWithoutExtension();
            for (int at; (at = nm.indexOf("%%")) >= 0; nm = nm.substring(at + 2)) parts.add(nm.substring(0, at));
            parts.add(nm);
            parts.removeEmptyStrings();
            if (! parts.isEmpty()) cs.push_back({ parts, e.getFile() });
        }
        std::sort(cs.begin(), cs.end(), [](const C& a, const C& b) {
            for (int i = 0; i < std::min(a.parts.size(), b.parts.size()); ++i) {
                const int r = a.parts[i].compareNatural(b.parts[i]);
                if (r != 0) return r < 0;
            }
            return a.parts.size() < b.parts.size();
        });
        for (const auto& c : cs) crateList.add({ c.parts.joinIntoString(" / "), c.parts.size() - 1, Crate::serato, c.f });
    }

    // one walk: every audio file (for "All tracks" and matching crate paths), playlists, folders with audio
    auto idx = std::make_shared<RootIndex>();
    Walker w(*idx);
    w.walk(root, 0);
    for (const auto& p : idx->playlists) crateList.add(p);
    for (const auto& f : idx->folders) crateList.add(f);
    std::lock_guard<std::mutex> g(indexMutex);
    indexes[root.getFullPathName().toStdString()] = idx;
}

namespace {
// a path from a crate or playlist, made on whatever OS, to a file here
juce::File resolvePath(const juce::String& rawPath, const juce::File& rootDir, const juce::File& seratoDir,
                       const juce::File& relativeTo, const RootIndex* idx, juce::File& lastBase)
{
    juce::String p = rawPath.trim();
    if (p.isEmpty()) return {};
    if (juce::File::isAbsolutePath(p)) {
        const juce::File f(p);
        if (f.existsAsFile()) return f;
    }
    juce::String rel = p.replaceCharacter('\\', '/');
    if (rel.length() >= 2 && rel[1] == ':') rel = rel.substring(2);   // "D:/Music/..."
    while (rel.startsWithChar('/')) rel = rel.substring(1);
    juce::StringArray tries { rel };
    if (rel.startsWithIgnoreCase("Volumes/")) {   // "Volumes/<card>/Music/x.mp3" -> "Music/x.mp3"
        const int s = rel.indexOfChar(8, '/');
        if (s > 0) tries.add(rel.substring(s + 1));
    }
    if (rel.startsWithIgnoreCase("media/") || rel.startsWithIgnoreCase("mnt/")) {   // Linux mounts
        const juce::StringArray c = components(rel);
        for (int k = 2; k < c.size() - 1 && k <= 3; ++k) {
            juce::StringArray tail;
            for (int i = k; i < c.size(); ++i) tail.add(c[i]);
            tries.add(tail.joinIntoString("/"));
        }
    }

    if (lastBase.isDirectory())
        for (const auto& t : tries) { const juce::File f = lastBase.getChildFile(t); if (f.existsAsFile()) return f; }

    juce::Array<juce::File> bases;
    if (relativeTo.isDirectory()) bases.add(relativeTo);
    for (juce::File d : { seratoDir.exists() ? seratoDir.getParentDirectory() : juce::File(), rootDir })
        for (int guard = 0; d.exists() && guard < 32; ++guard) {
            bases.addIfNotAlreadyThere(d);
            if (d.getParentDirectory() == d) break;
            d = d.getParentDirectory();
        }
    for (const auto& b : bases)
        for (const auto& t : tries) {
            const juce::File f = b.getChildFile(t);
            if (f.existsAsFile()) { lastBase = b; return f; }
        }

    // match by path suffix against the files under the root
    if (idx == nullptr) return {};
    const juce::StringArray want = components(rel);
    if (want.isEmpty()) return {};
    auto it = idx->byName.find(fold(want[want.size() - 1]));
    if (it == idx->byName.end()) return {};
    int best = -1, bestScore = 0;
    for (int i : it->second) {
        const juce::File& f = idx->audio.getReference(i);
        const juce::StringArray have = components(f.getRelativePathFrom(rootDir));
        int score = 0;
        while (score < want.size() && score < have.size()
               && fold(want[want.size() - 1 - score]) == fold(have[have.size() - 1 - score])) ++score;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    return best >= 0 ? idx->audio[best] : juce::File();
}
}  // namespace

juce::Array<Entry> Library::list(const Crate& c)
{
    const juce::ScopedLock scoped(scanLock);
    juce::Array<Entry> out;
    const auto idx = indexFor(rootDir);
    if (c.kind == Crate::all) {
        if (idx) for (const auto& f : idx->audio) out.add(readEntry(f));
        return out;
    }
    if (c.kind == Crate::folder) {
        int n = 0;
        if (idx) {   // from the index (already sorted, recursive)
            const juce::String base = c.source.getFullPathName() + juce::File::getSeparatorString();
            for (const auto& f : idx->audio)
                if (f.getFullPathName().startsWith(base) && ++n <= kMaxAudio) out.add(readEntry(f));
            if (n > 0) return out;
        }
        for (const auto& e : juce::RangedDirectoryIterator(c.source, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles)) {
            if (isAudio(e.getFile())) out.add(readEntry(e.getFile()));
            if (out.size() >= kMaxAudio) break;
        }
        return out;
    }
    juce::StringArray paths;
    juce::File relTo;
    if (c.kind == Crate::serato) {
        juce::MemoryBlock mb;
        if (c.source.loadFileAsData(mb)) paths = parseCrate(mb);
    } else {
        loadM3u(c.source, paths);
        relTo = c.source.getParentDirectory();
    }
    juce::File lastBase;
    juce::Array<juce::File> seen;
    for (const auto& p : paths) {
        const juce::File f = resolvePath(p, rootDir, seratoDir, relTo, idx.get(), lastBase);
        if (f.existsAsFile() && ! seen.contains(f)) { seen.add(f); out.add(readEntry(f)); }
    }
    return out;
}

juce::Array<Entry> Library::search(const juce::String& text, const Crate& c)
{
    const juce::ScopedLock scoped(scanLock);
    juce::StringArray words;
    words.addTokens(text, " \t", "\"");
    words.trim();
    words.removeEmptyStrings();
    for (auto& w : words) w = w.unquoted();
    juce::Array<Entry> all = list(c), out;
    for (const auto& e : all) {
        const juce::String hay = e.title + " " + e.artist + " " + e.album + " " + e.file.getFileName();
        bool ok = true;
        for (const auto& w : words) if (! hay.containsIgnoreCase(w)) { ok = false; break; }
        if (ok) out.add(e);
    }
    return out;
}

namespace {
void applyTags(const juce::File& f, Tags g, Track& t, Entry* e)
{
    nameFromFile(f, g.title, g.artist);
    t.title = g.title.toStdString();
    t.artist = g.artist.toStdString();
    t.album = g.album.toStdString();
    const int key = parseKey(g.keyText.toStdString());
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
    if (e) {
        e->title = g.title;
        e->artist = g.artist;
        e->album = g.album;
        e->keyText = g.keyText;
        e->bpm = t.bpm;
        e->serato = g.serato;
        if (g.length > 0 && e->length <= 0) e->length = g.length;
    }
}
}  // namespace

void readSeratoTags(const juce::File& f, Track& t, Entry* e) { applyTags(f, readTags(f), t, e); }

Entry Library::readEntry(const juce::File& f)
{
    const std::string key = f.getFullPathName().toStdString();
    const juce::int64 mtime = f.getLastModificationTime().toMilliseconds(), size = f.getSize();
    {
        std::lock_guard<std::mutex> g(entryMutex);
        auto it = entryCache.find(key);
        if (it != entryCache.end() && it->second.mtime == mtime && it->second.size == size) return it->second.e;
    }
    Entry e;
    e.file = f;
    Track tmp;
    readSeratoTags(f, tmp, &e);
    if (e.length <= 0) {
        if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor(f) })
            if (r->sampleRate > 0) e.length = (double) r->lengthInSamples / r->sampleRate;
    }
    std::lock_guard<std::mutex> g(entryMutex);
    if (entryCache.size() > 200000) entryCache.clear();
    entryCache[key] = { mtime, size, e };
    return e;
}

//==============================================================================================================
// analysis
namespace {

// low-pass (windowed sinc) and keep every factor-th sample
std::vector<float> decimate(const float* x, size_t n, int factor)
{
    if (factor <= 1) return std::vector<float>(x, x + n);
    const int H = 6 * factor;
    std::vector<float> h((size_t) (2 * H + 1));
    const double fc = 0.45 / factor;
    double sum = 0;
    for (int k = -H; k <= H; ++k) {
        const double s = k == 0 ? 2 * fc : std::sin(2 * juce::MathConstants<double>::pi * fc * k) / (juce::MathConstants<double>::pi * k);
        const double w = 0.42 + 0.5 * std::cos(juce::MathConstants<double>::pi * k / (H + 1)) + 0.08 * std::cos(2 * juce::MathConstants<double>::pi * k / (H + 1));
        h[(size_t) (k + H)] = (float) (s * w);
        sum += s * w;
    }
    for (auto& v : h) v = (float) (v / sum);
    const size_t m = n / (size_t) factor;
    std::vector<float> y(m);
    for (size_t i = 0; i < m; ++i) {
        const long c = (long) (i * (size_t) factor);
        float acc = 0;
        if (c - H >= 0 && c + H < (long) n) {
            const float* xp = x + c - H;
            for (size_t k = 0; k < h.size(); ++k) acc += h[k] * xp[k];
        } else {
            for (int k = -H; k <= H; ++k) {
                const long j = c + k;
                if (j >= 0 && j < (long) n) acc += h[(size_t) (k + H)] * x[j];
            }
        }
        y[i] = acc;
    }
    return y;
}

struct Onsets {
    std::vector<float> env, low, hi;   // combined onset strength, low-band (kick), mid + high (snare, hats)
    double fps = 0, sr = 0;
    int hop = 0, size = 0;
};

Onsets onsets(const std::vector<float>& x, double sr)
{
    Onsets o;
    o.sr = sr;
    o.size = sr > 16000 ? 1024 : 512;
    o.hop = o.size / 4;
    o.fps = sr / o.hop;
    const int order = o.size == 1024 ? 10 : 9, N = o.size, half = N / 2;
    juce::dsp::FFT fft(order);
    std::vector<float> win((size_t) N), buf((size_t) (2 * N)), prev((size_t) half + 1, 0.0f), cur((size_t) half + 1);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
    const double binHz = sr / N;
    const int lowEnd = std::max(2, (int) std::lround(150 / binHz)), midEnd = (int) std::lround(2000 / binHz), top = std::min(half, (int) std::lround(6000 / binHz));
    const long frames = (long) x.size() / o.hop + 1;
    std::vector<float> fl((size_t) frames), fm((size_t) frames), fh((size_t) frames);
    const float norm = 4.0f / (float) N;
    for (long i = 0; i < frames; ++i) {
        const long s0 = i * o.hop - half;   // frame i is centred on sample i * hop
        for (int k = 0; k < N; ++k) {
            const long j = s0 + k;
            buf[(size_t) k] = (j >= 0 && j < (long) x.size()) ? x[(size_t) j] * win[(size_t) k] : 0.0f;
        }
        std::fill(buf.begin() + N, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(buf.data(), true);
        float a = 0, b = 0, c = 0;
        for (int k = 1; k <= top; ++k) {
            cur[(size_t) k] = std::log1p(100.0f * buf[(size_t) k] * norm);
            const float d = cur[(size_t) k] - prev[(size_t) k];
            if (d > 0) (k < lowEnd ? a : k < midEnd ? b : c) += d;
        }
        std::swap(cur, prev);
        fl[(size_t) i] = a;
        fm[(size_t) i] = b;
        fh[(size_t) i] = c;
    }
    auto meanOf = [](const std::vector<float>& v) { double s = 0; for (float f : v) s += f; return (float) (s / (double) std::max<size_t>(1, v.size()) + 1e-6); };
    const float ml = meanOf(fl), mm = meanOf(fm), mh = meanOf(fh);
    o.env.resize((size_t) frames);
    o.low.resize((size_t) frames);
    o.hi.resize((size_t) frames);
    for (size_t i = 0; i < (size_t) frames; ++i) {
        o.low[i] = fl[i] / ml;
        o.hi[i] = fm[i] / mm + fh[i] / mh;
        o.env[i] = fl[i] / ml + fm[i] / mm + 0.5f * fh[i] / mh;
    }
    // subtract the local mean (+-0.25 s), keep the rise
    const int W = std::max(2, (int) (o.fps * 0.25));
    std::vector<double> cs((size_t) frames + 1, 0.0);
    for (size_t i = 0; i < (size_t) frames; ++i) cs[i + 1] = cs[i] + o.env[i];
    std::vector<float> e2((size_t) frames);
    for (long i = 0; i < frames; ++i) {
        const long a = std::max(0L, i - W), b = std::min(frames, i + W + 1);
        const float m = (float) ((cs[(size_t) b] - cs[(size_t) a]) / (double) (b - a));
        e2[(size_t) i] = std::max(0.0f, o.env[(size_t) i] - m);
    }
    o.env.swap(e2);
    return o;
}

float interp(const std::vector<float>& v, double x)
{
    if (x < 0) return 0;
    const size_t i = (size_t) x;
    if (i + 1 >= v.size()) return i < v.size() ? v[i] : 0.0f;
    const float f = (float) (x - (double) i);
    return v[i] * (1 - f) + v[i + 1] * f;
}

// the strongest point of v within [c - r, c + r], to sub-frame precision; returns the strength
float peakNear(const std::vector<float>& v, double c, double r, double& at)
{
    const long a = std::max(1L, (long) std::ceil(c - r)), b = std::min((long) v.size() - 2, (long) std::floor(c + r));
    float best = -1;
    long bi = -1;
    for (long i = a; i <= b; ++i) if (v[(size_t) i] > best) { best = v[(size_t) i]; bi = i; }
    if (bi < 0) { at = c; return 0; }
    const double y0 = v[(size_t) bi - 1], y1 = v[(size_t) bi], y2 = v[(size_t) bi + 1], den = y0 - 2 * y1 + y2;
    at = (double) bi + (std::abs(den) > 1e-9 ? std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0);
    return best;
}

// onset envelope frames lag the attacks they mark by about this much (measured on synthetic drums)
constexpr double kOnsetLagSeconds = 0.005;

double bpmFromOnsets(const Onsets& o, double& firstBeat, double hint)
{
    firstBeat = 0;
    const auto& env = o.env;
    const size_t n = env.size();
    if (n < (size_t) (o.fps * 4)) return 0;

    // smoothed copy for the autocorrelation
    std::vector<float> sm(n);
    for (size_t i = 0; i < n; ++i) sm[i] = 0.5f * env[i] + 0.25f * (i > 0 ? env[i - 1] : 0.0f) + 0.25f * (i + 1 < n ? env[i + 1] : 0.0f);
    double mean = 0;
    for (float v : sm) mean += v;
    mean /= (double) n;
    int order = 1;
    while ((size_t) (1 << order) < 2 * n) ++order;
    const size_t F = (size_t) 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> buf(2 * F, 0.0f);
    for (size_t i = 0; i < n; ++i) buf[i] = (float) (sm[i] - mean);
    fft.performRealOnlyForwardTransform(buf.data(), true);
    for (size_t k = 0; k <= F / 2; ++k) {
        const float re = buf[2 * k], im = buf[2 * k + 1];
        buf[2 * k] = re * re + im * im;
        buf[2 * k + 1] = 0;
    }
    fft.performRealOnlyInverseTransform(buf.data());
    const size_t maxLag = std::min(n - 1, (size_t) (o.fps * 60.0 / 30.0 * 4.5));
    std::vector<float> ac(maxLag + 1);
    const float ac0 = std::max(1e-9f, buf[0]);
    for (size_t l = 0; l <= maxLag; ++l) ac[l] = buf[l] / ac0 * (float) n / (float) (n - l);

    auto acAt = [&](double lag) { return lag < (double) maxLag ? std::max(0.0f, interp(ac, lag)) : 0.0f; };
    auto score = [&](double bpm) {
        const double L = o.fps * 60.0 / bpm;
        return acAt(L) + 0.5f * acAt(2 * L) + 0.25f * acAt(4 * L) + 0.25f * acAt(3 * L);
    };
    // mild preference for the 85..135 range (hip-hop .. house) over its double / half
    auto prior = [](double bpm) { const double d = std::log2(bpm / 112.0); return std::exp(-0.5 * d * d / (0.6 * 0.6)); };
    double best = 0, bestV = -1;
    for (double b = 70; b <= 180.0001; b += 0.05) {
        const double v = score(b) * prior(b);
        if (v > bestV) { bestV = v; best = b; }
    }
    if (best <= 0) return 0;
    if (hint > 0) {   // a tagged BPM settles the octave
        for (double m : { 0.5, 2.0, 2.0 / 3.0, 1.5 })
            if (std::abs(best * m - hint) < std::abs(best - hint) && std::abs(best * m - hint) < hint * 0.03) best *= m;
    }

    // refine: follow the beats outward from the middle and fit a straight line (period, phase) through the onsets
    double P = o.fps * 60.0 / best;
    const double centre = (double) n * 0.5;
    double phase = 0, bestPh = -1;
    for (double ph = 0; ph < P; ph += 0.25) {   // best phase over +-16 beats around the centre
        double s = 0;
        for (int k = -16; k <= 16; ++k) s += interp(env, centre + ph + k * P);
        if (s > bestPh) { bestPh = s; phase = ph; }
    }
    double a = centre + phase;   // position of beat k = a + k * P
    struct Pt { double k, x, w; };
    std::vector<Pt> pts;
    const int kMin = -(int) std::floor(a / P), kMax = (int) std::floor(((double) n - 1 - a) / P);
    for (int span = 16;; span *= 2) {
        const int k0 = std::max(kMin, -span), k1 = std::min(kMax, span);
        for (int pass = 0; pass < 2; ++pass) {
            pts.clear();
            for (int k = k0; k <= k1; ++k) {
                double at;
                const float w = peakNear(env, a + k * P, P * (pass == 0 ? 0.12 : 0.07), at);
                if (w > 0) pts.push_back({ (double) k, at, w });
            }
            if (pts.size() < 8) break;
            // weighted least squares, then once more without outliers
            for (int it = 0; it < 2; ++it) {
                double sw = 0, sk = 0, sx = 0, skk = 0, skx = 0;
                for (const auto& p : pts) { sw += p.w; sk += p.w * p.k; sx += p.w * p.x; skk += p.w * p.k * p.k; skx += p.w * p.k * p.x; }
                const double den = sw * skk - sk * sk;
                if (sw <= 0 || std::abs(den) < 1e-12) break;
                const double slope = (sw * skx - sk * sx) / den, icpt = (sx - slope * sk) / sw;
                if (slope > P * 0.97 && slope < P * 1.03) { P = slope; a = icpt; }
                std::vector<Pt> keep;
                for (const auto& p : pts) if (std::abs(p.x - (a + p.k * P)) < P * 0.05) keep.push_back(p);
                if (keep.size() < 8) break;
                pts.swap(keep);
            }
        }
        if (k0 == kMin && k1 == kMax) break;
    }
    double bpm = o.fps * 60.0 / P;
    if (std::abs(bpm - std::round(bpm)) < 0.05) bpm = std::round(bpm);
    else bpm = std::round(bpm * 100) / 100;
    P = o.fps * 60.0 / bpm;
    // phase for the final period: weighted mean over the inlier beats
    {
        double sw = 0, s = 0;
        for (const auto& p : pts) { sw += p.w; s += p.w * (p.x - p.k * P); }
        if (sw > 0) a = s / sw;
    }
    // where the music starts: first frame with a real attack
    float emax = 0;
    for (float v : env) emax = std::max(emax, v);
    double start = 0;
    for (size_t i = 0; i < n; ++i) if (env[i] > emax * 0.15f) { start = (double) i; break; }
    // downbeat: the beat (of 4) where the kick stands out most against snare / hats (beat 1 in hip-hop: kick, no
    // snare); when two or more beats look alike (four to the floor), the one the music starts on
    double lowSum[4] = { 0, 0, 0, 0 }, hiSum[4] = { 0, 0, 0, 0 };
    for (int k = kMin; k <= kMax; ++k) {
        double at;
        const int j = ((k % 4) + 4) % 4;
        lowSum[j] += peakNear(o.low, a + k * P, 2.0, at);
        hiSum[j] += peakNear(o.hi, a + k * P, 2.0, at);
    }
    double r[4];
    int db = 0;
    for (int j = 0; j < 4; ++j) {
        r[j] = lowSum[j] / (lowSum[j] + 0.5 * hiSum[j] + 1e-9);
        if (r[j] > r[db]) db = j;
    }
    {
        const int sj = (((int) std::lround((start - a) / P)) % 4 + 4) % 4;
        if (sj != db && r[sj] > r[db] - 0.03) db = sj;
    }
    const double bar = 4 * P;
    double d = a + db * P;
    d -= std::floor((d - (start - 0.3 * P)) / bar) * bar;   // first downbeat at or after the start
    firstBeat = d * o.hop / o.sr + kOnsetLagSeconds;
    if (firstBeat < 0) firstBeat += bar * o.hop / o.sr;
    return bpm;
}

int keyFromSignal(const std::vector<float>& x, double sr)
{
    // x at ~5.5 kHz (or a bit higher); FFT 8192 there is ~1.5 s with ~0.7 Hz bins
    const int order = sr > 8000 ? 14 : 13, N = 1 << order, hop = N / 2;
    if ((int) x.size() < N) return -1;
    juce::dsp::FFT fft(order);
    std::vector<float> win((size_t) N), buf((size_t) (2 * N));
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
    const double binHz = sr / N;
    const int k0 = (int) std::ceil(55.0 / binHz), k1 = std::min(N / 2 - 2, (int) std::floor(2000.0 / binHz));
    std::vector<std::vector<float>> mags;
    for (size_t s = 0; s + (size_t) N <= x.size(); s += (size_t) hop) {
        for (int i = 0; i < N; ++i) buf[(size_t) i] = x[s + (size_t) i] * win[(size_t) i];
        std::fill(buf.begin() + N, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(buf.data(), true);
        mags.emplace_back(buf.begin() + k0, buf.begin() + k1 + 1);
    }
    // tuning: where the spectral peaks sit relative to the 440 Hz grid
    double cx = 0, cy = 0;
    for (const auto& m : mags)
        for (size_t i = 1; i + 1 < m.size(); ++i)
            if (m[i] > m[i - 1] && m[i] >= m[i + 1] && m[i] > 1e-3f) {
                const double den = m[i - 1] - 2 * m[i] + m[i + 1];
                const double off = std::abs(den) > 1e-12 ? 0.5 * (m[i - 1] - m[i + 1]) / den : 0.0;
                const double f = ((double) (i + (size_t) k0) + off) * binHz, midi = 69 + 12 * std::log2(f / 440.0);
                const double ang = 2 * juce::MathConstants<double>::pi * (midi - std::round(midi));
                cx += m[i] * std::cos(ang);
                cy += m[i] * std::sin(ang);
            }
    const double tune = std::atan2(cy, cx) / (2 * juce::MathConstants<double>::pi);   // semitones, -0.5..0.5
    std::vector<int> pcOf((size_t) (k1 - k0 + 1));
    std::vector<float> wOf((size_t) (k1 - k0 + 1));
    for (int k = k0; k <= k1; ++k) {
        const double midi = 69 + 12 * std::log2(k * binHz / 440.0) - tune, r = std::round(midi), d = midi - r;
        pcOf[(size_t) (k - k0)] = ((int) r % 12 + 12) % 12;
        // only bins close to a semitone count; weaker high up (harmonics)
        const double c = std::cos(juce::MathConstants<double>::pi * d);
        wOf[(size_t) (k - k0)] = (float) (std::abs(d) < 0.35 ? c * c : 0.0) * (float) (k * binHz < 400 ? 1.0 : 400.0 / (k * binHz));
    }
    std::array<double, 12> chroma {};
    for (const auto& m : mags) {
        std::array<double, 12> c {};
        double tot = 0;
        for (size_t i = 1; i + 1 < m.size(); ++i) {
            if (m[i] < m[i - 1] || m[i] < m[i + 1]) continue;   // peaks only
            const double v = std::sqrt((double) m[i]) * wOf[i];
            c[(size_t) pcOf[i]] += v;
            tot += v;
        }
        if (tot <= 0) continue;
        for (int i = 0; i < 12; ++i) chroma[(size_t) i] += c[(size_t) i];
    }
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&](const double* prof, int root) {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) ((i + root) % 12)]; my += prof[i]; }
        mx /= 12;
        my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) {
            const double a = chroma[(size_t) ((i + root) % 12)] - mx, b = prof[i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return sxx > 0 ? sxy / std::sqrt(sxx * syy) : 0.0;
    };
    int best = -1;
    double bv = -2;
    for (int r = 0; r < 12; ++r) {
        const double a = corr(maj, r), b = corr(mnr, r);
        if (a > bv) { bv = a; best = r; }
        if (b > bv) { bv = b; best = 12 + r; }
    }
    return bv > 0.1 ? best : -1;
}

int factorFor(double sr, double target) { return std::max(1, (int) std::lround(sr / target)); }

}  // namespace

double detectBpm(const std::vector<float>& mono, double sr, double& firstBeat)
{
    firstBeat = 0;
    if (mono.empty() || sr <= 0) return 0;
    const int f = factorFor(sr, 11025);
    const auto ds = decimate(mono.data(), mono.size(), f);
    return bpmFromOnsets(onsets(ds, sr / f), firstBeat, 0);
}

int detectKey(const std::vector<float>& mono, double sr)
{
    if (mono.empty() || sr <= 0) return -1;
    const int f = factorFor(sr, 5512.5);
    return keyFromSignal(decimate(mono.data(), mono.size(), f), sr / f);
}

void buildWave(const Track& t, Wave& w)
{
    const size_t n = t.L.size();
    const size_t cols = (size_t) std::ceil((double) n * kWaveRate / t.sr);
    w.low.assign(cols, 0);
    w.mid.assign(cols, 0);
    w.high.assign(cols, 0);
    w.peak.assign(cols, 0);
    if (n == 0 || cols == 0) return;
    // RBJ biquads: low = LP 250 x2, mid = HP 250 + LP 2500, high = HP 2500 x2
    struct BQ {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set(double sr, double f, bool high)
        {
            const double w0 = 2 * juce::MathConstants<double>::pi * f / sr, c = std::cos(w0), al = std::sin(w0) / (2 * 0.7071), a0 = 1 + al;
            if (high) { b0 = (float) ((1 + c) / 2 / a0); b1 = (float) (-(1 + c) / a0); }
            else { b0 = (float) ((1 - c) / 2 / a0); b1 = (float) ((1 - c) / a0); }
            b2 = b0;
            a1 = (float) (-2 * c / a0);
            a2 = (float) ((1 - al) / a0);
        }
        float operator()(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    BQ l1, l2, m1, m2, h1, h2;
    l1.set(t.sr, 250, false); l2.set(t.sr, 250, false);
    m1.set(t.sr, 250, true); m2.set(t.sr, 2500, false);
    h1.set(t.sr, 2500, true); h2.set(t.sr, 2500, true);
    std::vector<float> fl(cols), fm(cols), fh(cols), fp(cols);
    const bool st = t.R.size() == n;
    for (size_t c = 0; c < cols; ++c) {
        const size_t s0 = (size_t) ((double) c * t.sr / kWaveRate), s1 = std::min(n, (size_t) ((double) (c + 1) * t.sr / kWaveRate));
        float a = 0, b = 0, d = 0, p = 0;
        for (size_t i = s0; i < s1; ++i) {
            const float l = t.L[i], r = st ? t.R[i] : l, x = 0.5f * (l + r);
            a = std::max(a, std::abs(l2(l1(x))));
            b = std::max(b, std::abs(m2(m1(x))));
            d = std::max(d, std::abs(h2(h1(x))));
            p = std::max(p, std::max(std::abs(l), std::abs(r)));
        }
        fl[c] = a; fm[c] = b; fh[c] = d; fp[c] = p;
    }
    // scale so the loud part of the track reaches the top (99th percentile -> 245)
    auto pct = [](std::vector<float> v) {
        if (v.empty()) return 0.0f;
        const size_t k = (size_t) ((double) (v.size() - 1) * 0.99);
        std::nth_element(v.begin(), v.begin() + (long) k, v.end());
        return v[k];
    };
    const float pp = std::max(1e-4f, pct(fp)), sp = 245.0f / pp;
    auto fill = [&](const std::vector<float>& src, std::vector<uint8_t>& dst, float scale) {
        for (size_t c = 0; c < cols; ++c) dst[c] = (uint8_t) std::clamp((int) std::lround(src[c] * scale), 0, 255);
    };
    auto bandScale = [&](const std::vector<float>& v) { return std::min(245.0f / std::max(1e-6f, pct(v)), sp * 4.0f); };
    fill(fp, w.peak, sp);
    fill(fl, w.low, bandScale(fl));
    fill(fm, w.mid, bandScale(fm));
    fill(fh, w.high, bandScale(fh));
}

//==============================================================================================================
// load

std::shared_ptr<Track> Library::load(const juce::File& f, juce::String& err, std::function<bool()> cancelled)
{
    auto stop = [&] { return cancelled && cancelled(); };
    if (! f.existsAsFile()) { err = "File not found: " + f.getFileName(); return nullptr; }
    std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor(f) };
    if (! r || r->sampleRate <= 0 || r->numChannels < 1) { err = "Can't decode " + f.getFileName(); return nullptr; }
    auto t = std::make_shared<Track>();
    t->sr = r->sampleRate;
    t->path = f.getFullPathName().toStdString();
    const juce::int64 cap = (juce::int64) (20 * 60 * r->sampleRate);
    const juce::int64 total = std::min(r->lengthInSamples, cap);
    if (total <= 0) { err = "Empty file: " + f.getFileName(); return nullptr; }
    t->L.resize((size_t) total);
    t->R.resize((size_t) total);
    const int chunk = 1 << 16;
    juce::AudioBuffer<float> buf((int) std::min<juce::uint32>(2, r->numChannels), chunk);
    for (juce::int64 pos = 0; pos < total; pos += chunk) {
        if (stop()) { err = "Cancelled"; return nullptr; }
        const int num = (int) std::min<juce::int64>(chunk, total - pos);
        if (! r->read(&buf, 0, num, pos, true, true)) { if (pos == 0) { err = "Read error: " + f.getFileName(); return nullptr; } t->L.resize((size_t) pos); t->R.resize((size_t) pos); break; }
        std::memcpy(t->L.data() + pos, buf.getReadPointer(0), sizeof(float) * (size_t) num);
        std::memcpy(t->R.data() + pos, buf.getReadPointer(buf.getNumChannels() > 1 ? 1 : 0), sizeof(float) * (size_t) num);
    }
    r.reset();

    const Tags tg = readTags(f);
    const double tagBpm = tg.autoBpm > 0 ? tg.autoBpm : tg.tagBpm;
    applyTags(f, tg, *t, nullptr);
    const bool haveGrid = tg.hasGrid;
    if (stop()) { err = "Cancelled"; return nullptr; }

    std::thread waveThread([&t] { buildWave(*t, t->wave); });
    {
        std::vector<float> mono(t->L.size());
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (t->L[i] + t->R[i]);
        const int fac = factorFor(t->sr, 11025);
        std::vector<float> ds = decimate(mono.data(), mono.size(), fac);
        mono = {};
        const double dsr = t->sr / fac;
        if (! haveGrid && ! stop()) {
            double fb = 0;
            double bpm = bpmFromOnsets(onsets(ds, dsr), fb, tagBpm);
            if (bpm > 0) {
                if (tagBpm > 0 && std::abs(bpm - tagBpm) < 0.1) bpm = tagBpm;
                t->bpm = bpm;
                t->firstBeat = fb;
            }
        }
        if (t->key < 0 && ! stop()) t->key = keyFromSignal(decimate(ds.data(), ds.size(), 2), dsr / 2);
    }
    waveThread.join();
    if (stop()) { err = "Cancelled"; return nullptr; }
    return t;
}

}  // namespace wheel
