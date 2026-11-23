// Omega Wheel of Steel: the phone app's screen (android/wheel WheelView.java + LibraryView.java) as the plugin's
// main view, drawn with JUCE in the same layout, colours and gestures. PhoneDeckView is the performance screen
// (waveforms, two platters, transport, mixer, pads, loops, FX, the SETUP sheet); PhoneLibraryView is the crate /
// track browser that covers it when LIBRARY is pressed. Both talk to the processor only through its public API
// (apvts parameters, send(Cmd), status(), cuesOf(), deckTrack(), loadTrack(), library, pad()).
#pragma once
#include "WheelPlugin.h"
#include <functional>

namespace wheel {

// what the two views ask of the editor that holds them
struct PhoneHost {
    std::function<void()> openLibrary, closeLibrary;
    std::function<bool(int deck)> loadSelected;     // load the library's selected track on a deck; false if none
    std::function<juce::String()> selectedTitle;     // the selected track's title ("" if none)
    std::function<juce::String()> midiStatus;        // e.g. "MIDI: MPC Sample" (the processor knows what it saw)
    std::function<void()> showMpcScreen;             // switch the editor to the 320 x 240 MPC screen
};

class PhoneDeckView : public juce::Component, private juce::Timer {
public:
    PhoneDeckView(WheelProcessor&, PhoneHost&);
    ~PhoneDeckView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // all pointer handling goes through these (multitouch: e.source.getIndex() tells fingers apart)
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify(const juce::MouseEvent&, float scale) override;
    bool keyPressed(const juce::KeyPress&) override;   // Esc closes the SETUP sheet
private:
    void timerCallback() override;                      // 60 Hz
    struct Impl; std::unique_ptr<Impl> d;
};

class PhoneLibraryView : public juce::Component {
public:
    PhoneLibraryView(WheelProcessor&, PhoneHost&);
    ~PhoneLibraryView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    juce::File selectedFile() const;
    juce::String selectedTitle() const;
    void refresh();                                     // re-read crates (after the root changed elsewhere)
private:
    struct Impl; std::unique_ptr<Impl> d;
};

}  // namespace wheel
