#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// The editor shows the F.A.R.P. page in a web view. Its screens talk to the C++ engine through the processor.
class FarpEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FarpEditor (FarpProcessor&);
    ~FarpEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void pickRomFolder();
    void pickSampleFolder();
    void send (const juce::String& js);

    FarpProcessor& proc;
    int seenGeneration = 0;
    bool pageReady = false;
    std::unique_ptr<juce::FileChooser> chooser;
#if JUCE_WEB_BROWSER
    std::unique_ptr<juce::WebBrowserComponent> web;
    std::optional<juce::WebBrowserComponent::Resource> resource (const juce::String& path);
#endif
    juce::Label fallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FarpEditor)
};
