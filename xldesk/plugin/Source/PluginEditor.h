// The plugin's face is the same console page as the web version (index.html, app.js...), shown in a WebView.
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class XLDeskEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit XLDeskEditor (XLDeskProcessor&);
    ~XLDeskEditor() override;
    void resized() override;

private:
    void timerCallback() override;
    void fromUi (const juce::var& m);
    void send (const juce::var& v) { browser->emitEventIfBrowserIsVisible ("xl", v); }
    std::optional<juce::WebBrowserComponent::Resource> resource (const juce::String& url);
    XLDeskProcessor& proc;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::vector<float> sent;
    std::vector<juce::uint32> uiTouched;   // ms time a control was last moved in the UI, so its echo isn't sent back
    juce::StringArray sentTypes;
    bool ready = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XLDeskEditor)
};
