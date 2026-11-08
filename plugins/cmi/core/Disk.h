// Fairlight CMI Series II / IIx disks and voice files, read as the CMI itself stores them.
//
//  Disk images: ImageDisk (.IMD) or raw sector dumps (.IMG / .BIN / .RAW / .DSK). An 8" CMI disk is 77 cylinders x
//  2 heads x 26 sectors of 128 bytes (512 512 bytes), FM. Sectors are numbered linearly: cylinder, then head, then
//  sector. Single-sided images (256 256 bytes) work too.
//  QDOS (the CMI's Motorola MDOS-style filesystem): sector 0 is the disk ID, 1 the cluster allocation table, 2 the
//  lockout table, and the directory starts at sector 3: 16-byte entries (8 name, 2 type, the file's first sector as a
//  big-endian word, attributes...), 0x00 / 0xFF first byte = empty. A file starts with a retrieval information block
//  (RIB): big-endian segment words, (clusters - 1) << 10 | first cluster, 4 sectors per cluster, until a word with
//  the top bit set, whose low 15 bits are the number of data sectors - 1. The data follows the RIB sector.
//  .VC voice files (21 888 bytes): a 5504-byte header, then the 16 384 bytes of channel card RAM, byte for byte
//  (8-bit offset binary, 128 pages of 128 samples). Header: 0x00 mode (4 = sampled, 128 segments; 1 = waveform,
//  32 segments), 0xA0 the linked control (.CO) file's name, 0x1332 / 0x1333 loop start / end segment (zero-based,
//  end inclusive), 0x133B loop on (1) / off (0).
//  Pitch: the CMI plays every voice with its channel clock at 128 x the key's frequency (its Page 8 sampling rates
//  are exactly that: A = 14 080 Hz = 128 x 110 Hz), so a voice sounds at the pitch it was sampled at on the key whose
//  128 x frequency is its sampling rate. Measured on the factory library: 375 of 426 periodic voices are in tune with
//  the keys this way (the rest are effects, chords and detuned voices).
//  Not read yet: the .CO control files (Page 7 settings), so voices start from neutral controls.
#pragma once
#include "Library.h"

namespace cmi {

struct DiskFile { std::string name, type; std::vector<uint8_t> data; };

struct Disk {
    std::string label, err;
    std::vector<DiskFile> files;
    int sectorsFound = -1, sectorsBad = 0;   // an HFE: the sectors its tracks gave, and how many failed their CRC
};

namespace disk {

constexpr int SEC = 128, SPT = 26, CYL = 77;

// ImageDisk: an ASCII header ending in 0x1A, then per track: mode, cylinder, head (+ flags), sector count, size code,
// the sector numbering map, optional cylinder / head maps, then each sector as a type byte and its data.
inline bool unpackImd(const uint8_t* d, size_t n, std::vector<uint8_t>& out, std::string& err) {
    size_t p = 0;
    while (p < n && d[p] != 0x1A) ++p;
    if (p >= n) { err = "BAD IMD HEADER"; return false; }
    ++p;
    out.assign((size_t) CYL * 2 * SPT * SEC, 0xE5);
    int maxTrack = 0;
    while (p + 5 <= n) {
        const int cyl = d[p + 1], headRaw = d[p + 2], count = d[p + 3], sizeCode = d[p + 4];
        p += 5;
        const int size = 128 << sizeCode;
        if (count <= 0 || count > 64 || p + (size_t) count > n) { err = "BAD IMD TRACK"; return false; }
        const uint8_t* smap = d + p; p += (size_t) count;
        if (headRaw & 0x80) p += (size_t) count;      // cylinder map
        if (headRaw & 0x40) p += (size_t) count;      // head map
        const int head = headRaw & 1;
        for (int s = 0; s < count; ++s) {
            if (p >= n) { err = "TRUNCATED IMD"; return false; }
            const int type = d[p++];
            std::vector<uint8_t> buf((size_t) size, 0);
            if (type == 0) {}                                            // unavailable
            else if (type % 2 == 1) { if (p + (size_t) size > n) { err = "TRUNCATED IMD"; return false; } std::memcpy(buf.data(), d + p, (size_t) size); p += (size_t) size; }
            else { if (p >= n) { err = "TRUNCATED IMD"; return false; } std::fill(buf.begin(), buf.end(), d[p]); ++p; }   // compressed
            const int sn = smap[s] - 1;
            if (size != SEC || cyl >= CYL || sn < 0 || sn >= SPT) continue;   // not a CMI sector
            const size_t off = ((size_t) (cyl * 2 + head) * SPT + (size_t) sn) * SEC;
            std::memcpy(out.data() + off, buf.data(), SEC);
            maxTrack = std::max(maxTrack, cyl * 2 + head + 1);
        }
    }
    if (!maxTrack) { err = "NO CMI SECTORS IN IMD"; return false; }
    return true;
}

// HFE (the HxC floppy emulator's image): not sectors but each track's raw bit cells as the drive head reads them,
// both sides interleaved in 256-byte blocks, bits in time order from bit 0. The CMI's 8" disks are single density
// (FM): every data bit is a clock cell then a data cell; an ID mark (data FE, clock C7) precedes each sector's
// cylinder, head, sector and size, a data mark (FB / F8, clock C7) its 128 bytes; each field ends in a CRC-CCITT.
// Decoded here into the same sector image an IMG holds. badCrc: sectors read with a CRC error (kept, counted).
inline uint16_t crcCcitt(const uint8_t* p, size_t n, uint16_t crc = 0xFFFF) {
    for (size_t i = 0; i < n; ++i) { crc ^= (uint16_t) (p[i] << 8); for (int k = 0; k < 8; ++k) crc = (uint16_t) (crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1); }
    return crc;
}
inline uint16_t fmCells(uint8_t data, uint8_t clock) {   // 16 cells, the first in time at bit 15
    uint16_t v = 0; for (int i = 7; i >= 0; --i) v = (uint16_t) ((v << 2) | (((clock >> i) & 1) << 1) | ((data >> i) & 1)); return v;
}
inline bool unpackHfe(const uint8_t* d, size_t n, std::vector<uint8_t>& out, std::string& err, int* badCrc = nullptr, int* found = nullptr) {
    const bool v3 = n >= 8 && !std::memcmp(d, "HXCHFEV3", 8);
    if (n < 512 || (std::memcmp(d, "HXCPICFE", 8) && !v3)) { err = "NOT AN HFE IMAGE"; return false; }
    const int tracks = d[9], sides = d[10], enc = d[11];
    const size_t tl = (size_t) (d[18] | d[19] << 8) * 512;
    if (enc != 2 && enc != 3 && enc != 0xFF) { err = enc == 0 ? "HFE IS MFM (DOUBLE DENSITY): NOT A CMI DISK" : "HFE ENCODING NOT FM"; return false; }
    out.assign((size_t) CYL * 2 * SPT * SEC, 0xE5);
    int good = 0, bad = 0;
    const uint16_t idam = fmCells(0xFE, 0xC7), dam = fmCells(0xFB, 0xC7), ddam = fmCells(0xF8, 0xC7);
    for (int t = 0; t < tracks && t < CYL; ++t) {
        if (tl + (size_t) t * 4 + 4 > n) break;
        const size_t off = (size_t) (d[tl + t * 4] | d[tl + t * 4 + 1] << 8) * 512, len = (size_t) (d[tl + t * 4 + 2] | d[tl + t * 4 + 3] << 8);
        for (int side = 0; side < sides && side < 2; ++side) {
            // this side's bits, in time order
            std::vector<uint8_t> bits;
            const size_t sideLen = len / 2;   // each side has half the track's bytes, 256 to a block (the last block split evenly too)
            int skip = 0;
            for (size_t j = 0; j < sideLen; ++j) {
                const size_t at = off + (j / 256) * 512 + (size_t) side * 256 + j % 256;
                if (at >= n) break;
                const uint8_t by = d[at];
                if (v3 && (by & 0xF0) == 0xF0) {   // HFE v3 opcodes: NOP, index, bit rate (+1), skip bits (+1), random
                    if (by == 0xF2) ++j; else if (by == 0xF3 && j + 1 < sideLen) { ++j; skip = d[off + (j / 256) * 512 + (size_t) side * 256 + j % 256] & 7; }
                    else if (by == 0xF4) for (int k = 0; k < 8; ++k) bits.push_back(0);
                    continue;
                }
                for (int k = skip; k < 8; ++k) bits.push_back((by >> k) & 1);
                skip = 0;
            }
            // the FM cells. An image made from a real floppy drifts against HxC's fixed bit clock, so the cells come from the
            // gaps between flux transitions (a 1 bit), each rounded to whole cells of 1 or (as HxC stores 8" FM at twice
            // the rate) 2 bits: a slip of a bit here and there doesn't lose the place. Whichever reading finds marks.
            auto marks = [&](const std::vector<uint8_t>& cv) { int m = 0; uint16_t r = 0; for (size_t c = 0; c < cv.size(); ++c) { r = (uint16_t) ((r << 1) | cv[c]); m += r == idam || r == dam; } return m; };
            // the track goes round: its start again after its end, so a sector across the index reads whole
            const size_t nBits = bits.size();
            for (size_t i = 0; i < std::min<size_t>(nBits, 16 * 400 * 2); ++i) bits.push_back(bits[i]);
            auto fromGaps = [&](double cellBits, size_t& onceOut) {
                std::vector<uint8_t> cv; cv.reserve((size_t) (bits.size() / cellBits) + 8); long last = -1; onceOut = 0;
                for (size_t i = 0; i < bits.size(); ++i) {
                    if (i == nBits) onceOut = cv.size();
                    if (!bits[i]) continue;
                    if (last >= 0) { const int k = std::max(1, (int) std::floor((double) ((long) i - last) / cellBits + 0.49)); for (int z = 1; z < k; ++z) cv.push_back(0); }
                    cv.push_back(1); last = (long) i;
                }
                if (!onceOut) onceOut = cv.size();
                return cv;
            };
            size_t once = 0, o2 = 0;
            std::vector<uint8_t> cells = fromGaps(2.0, once); int bestMarks = marks(cells);
            { auto cv = fromGaps(1.0, o2); const int m = marks(cv); if (m > bestMarks) { bestMarks = m; cells = std::move(cv); once = o2; } }
            for (int ph = 0; ph < 2; ++ph) {   // a clean image: every other bit, as stored
                std::vector<uint8_t> cv; cv.reserve(bits.size() / 2 + 1);
                for (size_t i = (size_t) ph; i < bits.size(); i += 2) cv.push_back(bits[i]);
                const int m = marks(cv); if (m > bestMarks) { bestMarks = m; cells = std::move(cv); once = (nBits - (size_t) ph + 1) / 2; }
            }
            auto byteAt = [&](size_t c) { uint8_t v = 0; for (int i = 0; i < 8; ++i) v = (uint8_t) ((v << 1) | cells[c + (size_t) i * 2 + 1]); return v; };
            int state[SPT] {};   // 0 not found, 1 found with a CRC error, 2 good
            uint16_t reg = 0;
            for (size_t c = 0; c < cells.size(); ++c) {
                reg = (uint16_t) ((reg << 1) | cells[c]);
                if (c < 15 || reg != idam || c - 15 >= once) continue;
                size_t p = c + 1;
                if (p + 16 * 6 > cells.size()) break;
                uint8_t id[7] = { 0xFE }; for (int k = 0; k < 6; ++k) id[1 + k] = byteAt(p + (size_t) k * 16);
                if (crcCcitt(id, 5) != (uint16_t) (id[5] << 8 | id[6])) continue;   // a broken ID: not a sector we can place
                const int cyl = id[1], head = side, sn = id[3] - 1, size = 128 << (id[4] & 3);   // the side it was read on (CMI disks write head 0 on both)
                if (size != SEC || cyl >= CYL || sn < 0 || sn >= SPT || state[sn] == 2) continue;
                // its data mark, within the gap after the ID
                p += 16 * 6; uint16_t r2 = 0; size_t q = 0;
                for (size_t k = p; k < std::min(cells.size(), p + 16 * 60); ++k) { r2 = (uint16_t) ((r2 << 1) | cells[k]); if (k >= p + 15 && (r2 == dam || r2 == ddam)) { q = k + 1; break; } }
                if (!q || q + 16 * (SEC + 2) > cells.size()) continue;
                uint8_t buf[1 + SEC + 2]; buf[0] = (uint8_t) (r2 == dam ? 0xFB : 0xF8);
                for (int k = 0; k < SEC + 2; ++k) buf[1 + k] = byteAt(q + (size_t) k * 16);
                const bool ok = crcCcitt(buf, 1 + SEC) == (uint16_t) (buf[1 + SEC] << 8 | buf[2 + SEC]);
                if (!ok && state[sn] == 1) continue;   // keep the first bad copy
                std::memcpy(out.data() + ((size_t) (cyl * 2 + head) * SPT + (size_t) sn) * SEC, buf + 1, SEC);
                state[sn] = ok ? 2 : 1;
            }
            for (int k = 0; k < SPT; ++k) { if (state[k] == 2) ++good; else if (state[k] == 1) ++bad; }
        }
    }
    if (badCrc) *badCrc = bad;
    if (found) *found = good + bad;
    if (good + bad == 0) { err = "NO CMI SECTORS IN THE HFE"; return false; }
    return true;
}

inline uint16_t be16(const std::vector<uint8_t>& v, size_t o) { return o + 1 < v.size() ? (uint16_t) (v[o] << 8 | v[o + 1]) : 0; }

inline std::string trimmed(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) { const char c = (char) p[i]; s += (c >= 32 && c < 127) ? c : ' '; }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// a QDOS file system in a raw image
inline Disk readQdos(const std::vector<uint8_t>& img) {
    Disk dk;
    const size_t nsec = img.size() / SEC;
    if (nsec < 24) { dk.err = "IMAGE TOO SMALL"; return dk; }
    dk.label = trimmed(img.data(), 8);
    for (int e = 0; e < 160; ++e) {
        const size_t o = 3 * SEC + (size_t) e * 16;
        if (o + 16 > img.size()) break;
        const uint8_t first = img[o];
        if (first == 0 || first == 0xFF || first == 0xE5) continue;
        DiskFile f; f.name = trimmed(&img[o], 8); f.type = trimmed(&img[o + 8], 2);
        if (f.name.empty()) continue;
        const size_t rib = be16(img, o + 10);
        if (rib >= nsec) continue;
        std::vector<size_t> secs; long total = -1;
        for (int k = 0; k < 64; ++k) {
            const uint16_t w = be16(img, rib * SEC + (size_t) k * 2);
            if (w & 0x8000) { total = (w & 0x7FFF) + 1; break; }
            const size_t clusters = (size_t) (w >> 10) + 1, start = (size_t) (w & 0x3FF) * 4;
            for (size_t s = 0; s < clusters * 4; ++s) secs.push_back(start + s);
        }
        if (total <= 0 || secs.size() < 2) continue;
        for (long s = 1; s <= total && s < (long) secs.size(); ++s) {
            const size_t sec = secs[(size_t) s];
            if (sec >= nsec) break;
            f.data.insert(f.data.end(), img.begin() + (long) (sec * SEC), img.begin() + (long) (sec * SEC + SEC));
        }
        if (!f.data.empty()) dk.files.push_back(std::move(f));
    }
    if (dk.files.empty()) dk.err = "NO QDOS FILES ON THIS DISK";
    return dk;
}

}  // namespace disk

// any supported disk image, from its bytes
inline Disk readDiskImage(const uint8_t* d, size_t n) {
    std::vector<uint8_t> raw; std::string err;
    if (n >= 4 && !std::memcmp(d, "IMD ", 4)) {
        if (!disk::unpackImd(d, n, raw, err)) { Disk dk; dk.err = err; return dk; }
    } else if (n >= 8 && (!std::memcmp(d, "HXCPICFE", 8) || !std::memcmp(d, "HXCHFEV3", 8))) {
        int bad = 0, found = 0;
        if (!disk::unpackHfe(d, n, raw, err, &bad, &found)) { Disk dk; dk.err = err; return dk; }
        Disk dk = disk::readQdos(raw); dk.sectorsFound = found; dk.sectorsBad = bad; return dk;
    } else if (n % disk::SEC == 0 && n >= (size_t) disk::SPT * disk::SEC * 4) {
        raw.assign(d, d + n);
    } else { Disk dk; dk.err = "NOT A CMI DISK IMAGE"; return dk; }
    return disk::readQdos(raw);
}

// a .VC file as a channel card voice: the RAM image untouched, the CMI's own loop, the CMI's pitch rule
inline bool vcToSound(const std::vector<uint8_t>& vc, const std::string& name, const std::string& from, Sound& out, std::string& err) {
    constexpr size_t HDR = 0x1580;
    if (vc.size() < HDR + 4096) { err = "NOT A CMI VOICE FILE"; return false; }
    const int mode = vc[0];
    if (mode != 1 && mode != 4) { err = "UNKNOWN VOICE MODE " + std::to_string(mode); return false; }
    Sound s;
    s.name = name.substr(0, 8); for (auto& c : s.name) c = (char) std::toupper((unsigned char) c);
    s.title = s.name + (from.empty() ? std::string(" (CMI voice file)") : " from " + from);
    s.category = from.empty() ? "CMI" : from.substr(0, 10);
    const size_t len = std::min<size_t>((size_t) RAM_SIZE, vc.size() - HDR);
    const int pages = mode == 1 ? 32 : (int) (len / PAGE);
    s.ram.assign(vc.begin() + (long) HDR, vc.begin() + (long) (HDR + (size_t) pages * PAGE));
    s.mode = mode == 1 ? Mode::Waveform : Mode::Sample;
    s.rate = 128.0 * 440.0; s.root = 69;       // the channel clock is 128 x the key's frequency
    const int ls = vc.size() > 0x133B ? vc[0x1332] : 0, le = vc.size() > 0x133B ? vc[0x1333] : 0;
    const bool loopOn = vc.size() > 0x133B && vc[0x133B] == 1;
    if (mode == 4) {
        if (loopOn && le >= ls && le < pages) { s.loopStart = ls; s.loopEnd = le; }
    } else {
        // waveform voices hold their last segment, or cycle the loop segments if the voice has one
        if (loopOn && le >= ls && le < pages) { s.loopStart = ls; s.loopEnd = le; } else { s.loopStart = pages - 1; s.loopEnd = pages - 1; }
    }
    Controls c; c.attack = 0; c.damp = 8; c.sustain = 1; c.loop = 1; c.filter = 255; c.velVol = 0.5f; c.velFilt = 0.2f; c.segTime = 0.04f;
    s.ctl = c;
    if (mode == 4) {   // the voice's cycle (normalised autocorrelation over its loop, or its middle): 64 samples is an octave up
        const int n = (int) s.ram.size(); int a0, len;
        if (s.loopStart >= 0) { a0 = s.loopStart * PAGE; len = std::min(4096, (s.loopEnd + 1) * PAGE - a0); } else { a0 = n / 5; len = std::min(4096, n / 2); }
        if (len >= 512) {
            std::vector<double> x((size_t) len); double m = 0; for (int i = 0; i < len; ++i) { x[(size_t) i] = (int) s.ram[(size_t) (a0 + i)] - 128; m += x[(size_t) i]; } m /= len; for (auto& v : x) v -= m;
            double e0 = 0; for (int i = 0; i < len / 2; ++i) e0 += x[(size_t) i] * x[(size_t) i];
            if (e0 > 1) {
                std::vector<double> r(1026, 0.0); double best = 0;
                for (int L = 8; L <= 1024 && L < len / 2; ++L) { double c = 0, e1 = 0; for (int i = 0; i < len / 2; ++i) { c += x[(size_t) i] * x[(size_t) (i + L)]; e1 += x[(size_t) (i + L)] * x[(size_t) (i + L)]; } r[(size_t) L] = c / std::sqrt(e0 * e1 + 1e-9); best = std::max(best, r[(size_t) L]); }
                int P = 0; for (int L = 9; L < 1024 && L < len / 2 - 1; ++L) if (r[(size_t) L] >= best * 0.9 && r[(size_t) L] >= r[(size_t) L - 1] && r[(size_t) L] >= r[(size_t) L + 1]) { P = L; break; }
                if (P > 0 && best >= 0.85) { const double off = 12 * std::log2(128.0 / P); if (std::fabs(off) >= 9) s.octaveFix = -12 * (int) std::lround(off / 12); }
            }
        }
    }
    if (mode == 4) {   // silence ahead of the sound (BDRUM, PERCSTK: 0.4 s, the sampling trigger set early): start where it begins
        size_t at = 0; while (at < s.ram.size() && std::abs((int) s.ram[at] - 128) <= 8) ++at;
        if (at > 3000 && at < s.ram.size()) s.startPage = (int) (at / PAGE);
    }
    out = std::move(s);
    return true;
}

}  // namespace cmi
