// Starting points for the detector. Shared by the plugin's preset buttons and the test bench.
#pragma once
#include "Detector.h"

namespace trig {

enum class Preset { kick, snare, tom };

inline Settings presetSettings(Preset p) {
    Settings s;
    switch (p) {
        case Preset::kick:    // beater attack: fast double kick is told apart by the click, not the boom
            s.thresholdDb = -30; s.sensitivityDb = 9; s.retriggerMs = 22; s.hpHz = 600; s.lpHz = 12000; break;
        case Preset::snare:   // crack and wires above the shell, below most of the cymbal wash
            s.thresholdDb = -24; s.sensitivityDb = 4.5; s.retriggerMs = 30; s.hpHz = 150; s.lpHz = 6000; break;
        case Preset::tom:
            s.thresholdDb = -24; s.sensitivityDb = 10; s.retriggerMs = 40; s.hpHz = 70; s.lpHz = 5000; break;
    }
    return s;
}

inline int presetNote(Preset p) { return p == Preset::kick ? 36 : p == Preset::snare ? 38 : 45; }

} // namespace trig
