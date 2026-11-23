// Omega Wheel of Steel tags: Serato data (hot cues with colours and names, saved loops, beatgrid, autotags), ID3v2.3 /
// 2.4 (MP3, and inside WAV / AIFF chunks), RIFF INFO, FLAC and Ogg Vorbis comments, MP4 ilst atoms, and Serato .crate
// files. Plain C++17, no JUCE: reads through a random-access ByteSource (a file descriptor with pread, a JUCE stream on
// the desktop, or memory). Text comes out as UTF-8.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Track.h"

namespace wheel {

struct ByteSource {
    virtual ~ByteSource() = default;
    virtual uint64_t size() = 0;
    // reads up to n bytes at off; returns how many were read (0 at / past the end or on error)
    virtual size_t read(uint64_t off, void* dst, size_t n) = 0;
};

struct MemorySource : ByteSource {
    MemorySource(const void* d, size_t n) : data((const uint8_t*) d), len(n) {}
    uint64_t size() override { return len; }
    size_t read(uint64_t off, void* dst, size_t n) override;
    const uint8_t* data; size_t len;
};

#if ! defined(_WIN32)
// a POSIX file descriptor, read with pread (the position is left alone; the descriptor is not closed)
struct FdSource : ByteSource {
    explicit FdSource(int f) : fd(f) {}
    uint64_t size() override;
    size_t read(uint64_t off, void* dst, size_t n) override;
    int fd;
};
#endif

// everything the tags say
struct Tags {
    std::string title, artist, album, genre, keyText;   // keyText as tagged ("8A", "Am")
    double tagBpm = 0, autoBpm = 0, gridBpm = 0, gridFirst = 0, length = 0;
    bool hasGrid = false, serato = false;
    std::array<HotCue, kCues> cues;
    std::array<SavedLoop, kCues> loops;
};

Tags readTags(ByteSource& in);
// the tags into a track: title / artist / album, key, cues and saved loops, BPM (the Serato grid wins, then Serato's
// autotag, then the BPM tag) and the first beat from the grid; fromSerato. Text left empty stays empty.
void applyTags(const Tags& g, Track& t);
// the BPM a load hands the analysis to settle half / double time
inline double hintBpm(const Tags& g) { return g.autoBpm > 0 ? g.autoBpm : g.tagBpm; }

// the track paths in a Serato .crate ("otrk" records holding UTF-16BE "ptrk" paths), in order
std::vector<std::string> parseCrate(const uint8_t* data, size_t n);

// Serato smart crates (_Serato_/SmartCrates/*.scrate). The layout is from community reverse engineering, not a spec:
// rules are (field id, comparison, value). Evaluated here: file name, title, artist, album, genre and key with
// contains / does not contain / is / is not, and BPM >= / <=. Other fields (comment, year, date added, plays...) are
// kept but never match.
namespace smartField { enum : int { fileName = 4, title = 6, artist = 7, album = 8, genre = 9, bpm = 15, comment = 17, grouping = 19,
                                    remixer = 20, label = 21, composer = 22, year = 23, added = 25, key = 51, plays = 79 }; }
struct SmartRule { int field = 0; std::string op /* "cond_con_str"... */, text; uint32_t num = 0; bool hasNum = false; };
struct SmartCrate {
    bool matchAll = true, liveUpdate = false;
    std::vector<SmartRule> rules;
    std::vector<std::string> paths;   // any track list stored in the file (as in a .crate)
};
SmartCrate parseSmartCrate(const uint8_t* data, size_t n);
struct SmartFields { std::string fileName, title, artist, album, genre, keyText; double bpm = 0; };
bool smartRuleSupported(const SmartRule& r);
int smartRuleMatch(const SmartRule& r, const SmartFields& f);   // 1 / 0, -1 = a rule this can't evaluate
bool smartMatch(const SmartCrate& c, const SmartFields& f);      // all / any of the rules; no rules: false

// "Artist - Title.mp3", "01 - Artist - Title.mp3", "01. Title.mp3" -> fills title / artist where empty
void nameFromFileName(const std::string& fileName, std::string& title, std::string& artist);

}  // namespace wheel
