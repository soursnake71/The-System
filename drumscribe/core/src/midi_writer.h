// Private helpers of src/midi.cpp: the written tempo map (seconds <-> ticks through integer microsecond tempi) and
// the Standard MIDI File variable-length quantity.
#pragma once
#include <cstdint>
#include <map>
#include <vector>

namespace ds {
namespace midi {

constexpr int kPPQ = 960;
constexpr uint8_t kBassProgram = 38;  // GM Synth Bass 1 (0-based program number on the wire)
constexpr int kPortaTime = 24;        // CC5 value while a slide is under way
constexpr int kAccentBoost = 20;      // velocity added to accented bass notes
constexpr double kSlideOverlap = 0.005;  // s: a slide's note keeps sounding this far into the next note (legato)

struct TempoMap {
    struct Seg { long tick; double time; long us; };  // from tick (at time s) on, us microseconds per quarter
    std::vector<Seg> segs;
    int64_t num = 0;  // written time of the last segment start, us * ppq (exact integer)
    int ppq = kPPQ;
    void add(long tick, long us);
    long ticks(double t) const;   // nearest tick of time t (s)
    double time(long tick) const;
};

inline void vlq(std::vector<uint8_t>& o, uint32_t v)
{
    uint8_t b[5];
    int n = 0;
    b[n++] = v & 0x7F;
    while (v >>= 7) b[n++] = (uint8_t) (0x80 | (v & 0x7F));
    while (n) o.push_back(b[--n]);
}

}  // namespace midi
}  // namespace ds
