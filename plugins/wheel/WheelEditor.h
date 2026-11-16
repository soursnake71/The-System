// Omega Wheel of Steel editor: Serato-style. Top: the two scrolling 3-band waveforms (red lows, green mids, blue highs)
// with beatgrid, cues and loops, stacked, plus the overviews. Middle: two SL-1200 decks (spinning platter with a
// strobe and a sticker, touch to scratch, start/stop, 33/45, pitch fader, cue, sync, key lock) around the mixer
// (trim, hi/mid/low, filter, channel faders, meters, crossfader). Below each deck: 8 performance pads (hot cues /
// loops / rolls / transport) and its FX unit. Bottom: the library: crate list and track list from the SD card.
#pragma once
#include "WheelPlugin.h"

namespace wheel {

class WheelEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit WheelEditor(WheelProcessor&);
    ~WheelEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();   // take in the processor's state now (off-screen rendering, no timer)
private:
    void timerCallback() override;   // 60 Hz: waveforms, platters, meters
    WheelProcessor& proc;
    struct Impl; std::unique_ptr<Impl> d;
};

}  // namespace wheel
