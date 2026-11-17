// Omega Wheel of Steel analysis: BPM + first downbeat, musical key and the 3-band waveform. Plain C++17, no JUCE, so it
// builds into the Android library and the tests too (Library.cpp uses it on the desktop).
#pragma once
#include <functional>
#include <vector>
#include "Track.h"

namespace wheel {

double detectBpm(const std::vector<float>& mono, double sr, double& firstBeat);   // 60..190 folded to 70..180-ish
int detectKey(const std::vector<float>& mono, double sr);              // Krumhansl-Schmuckler on a chromagram
void buildWave(const Track& t, Wave& w);

// everything a load does after decoding and reading the tags: the waveform (always, on a second thread), BPM + first
// beat unless the Serato grid gave them (tagBpm, from the tags, settles half / double time), the key when t.key < 0.
// stop (optional) is polled between the steps.
void analyseTrack(Track& t, bool haveGrid, double tagBpm, const std::function<bool()>& stop = {});

}  // namespace wheel
