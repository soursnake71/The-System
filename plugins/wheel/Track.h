// Omega Wheel of Steel: one decoded, analysed track, shared read-only between threads once built (shared_ptr<const Track>).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wheel {

constexpr int kCues = 8;

struct HotCue { double t = -1; uint32_t rgb = 0xCC0000; std::string name; bool set() const { return t >= 0; } };
struct SavedLoop { double a = -1, b = -1; bool set() const { return a >= 0 && b > a; } };

// waveform summary: one column per 1/kWaveRate s; band peaks 0..255 (low < 250 Hz, mid 250-2.5k, high > 2.5k)
constexpr int kWaveRate = 150;
struct Wave { std::vector<uint8_t> low, mid, high, peak; };

struct Track {
    std::vector<float> L, R;   // decoded at the file's own rate
    double sr = 44100;
    std::string path, title, artist, album;
    double bpm = 0;            // 0 = unknown
    double firstBeat = 0;      // seconds of the first downbeat (beatgrid anchor)
    int key = -1;              // 0..11 major C..B, 12..23 minor Cm..Bm, -1 unknown
    bool fromSerato = false;   // cues / grid / key came from Serato tags
    std::array<HotCue, kCues> cues;
    std::array<SavedLoop, kCues> loops;
    Wave wave;
    double length() const { return sr > 0 ? (double) L.size() / sr : 0; }
};

// key helpers (implemented in Library.cpp)
std::string keyName(int key);      // "Am", "F#"
std::string camelot(int key);      // "8A"
int parseKey(const std::string&);  // "8A", "Am", "A minor", "F#m", "Gb" -> 0..23 or -1

}  // namespace wheel
