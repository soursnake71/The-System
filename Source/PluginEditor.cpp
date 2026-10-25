#include "PluginEditor.h"
#include "BinaryData.h"

FarpEditor::FarpEditor (FarpProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    seenGeneration = proc.stateGeneration.load();
#if JUCE_WEB_BROWSER
    using WBC = juce::WebBrowserComponent;
    auto opts = WBC::Options{}
                    .withBackend (WBC::Options::Backend::webview2)
                    .withWinWebView2Options (WBC::Options::WinWebView2{}
                                                 .withUserDataFolder (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FARP").getChildFile ("WebView"))
                                                 .withBackgroundColour (juce::Colour (0xff15161a))
                                                 .withStatusBarDisabled())
                    .withNativeIntegrationEnabled()
                    .withKeepPageLoadedWhenBrowserIsHidden()
                    .withResourceProvider ([this] (const juce::String& path) { return resource (path); })
                    .withEventListener ("farp", [this] (juce::var v) { proc.fromPage (v.toString()); })
                    .withEventListener ("farpLs", [this] (juce::var v) { proc.setLs (v["k"].toString(), v["v"].toString()); })
                    .withEventListener ("farpReady", [this] (juce::var) { pageReady = true; proc.pageReady(); })
                    .withNativeFunction ("romList", [this] (const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) { done (proc.romListJson()); })
                    .withNativeFunction ("romFolder", [this] (const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) { done (proc.romDir.getFullPathName()); })
                    .withNativeFunction ("pickRomFolder", [this] (const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) { pickRomFolder(); done (juce::var()); })
                    .withNativeFunction ("sampleList", [this] (const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) { done (proc.sampleListJson()); })
                    .withNativeFunction ("sampleFolder", [this] (const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) { done (proc.samplesDir.getFullPathName()); })
                    .withNativeFunction ("pickSampleFolder", [this] (const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) { pickSampleFolder(); done (juce::var()); });
    web = std::make_unique<WBC> (opts);
    addAndMakeVisible (*web);
    web->goToURL (WBC::getResourceProviderRoot());
#else
    fallback.setText ("This build has no web view.", juce::dontSendNotification);
    addAndMakeVisible (fallback);
#endif
    setResizable (true, true);
    setResizeLimits (360, 600, 2400, 1600);
    setSize (1120, 740);
    startTimerHz (30);
}

FarpEditor::~FarpEditor() { stopTimer(); }

void FarpEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff15161a)); }

void FarpEditor::resized()
{
#if JUCE_WEB_BROWSER
    if (web) web->setBounds (getLocalBounds());
#endif
    fallback.setBounds (getLocalBounds());
}

#if JUCE_WEB_BROWSER
std::optional<juce::WebBrowserComponent::Resource> FarpEditor::resource (const juce::String& path)
{
    auto bytes = [] (const void* d, size_t n, const char* mime)
    {
        juce::WebBrowserComponent::Resource r;
        r.data.resize (n);
        if (n) std::memcpy (r.data.data(), d, n);
        r.mimeType = mime;
        return r;
    };
    const auto p = path.upToFirstOccurrenceOf ("?", false, false);
    if (p == "/" || p == "/index.html")
        return bytes (BinaryData::index_html, (size_t) BinaryData::index_htmlSize, "text/html");
    if (p == "/state.js")
    {
        auto s = proc.stateScript().toStdString();
        return bytes (s.data(), s.size(), "text/javascript");
    }
    if (p.startsWith ("/samples/"))
    {
        // a sound inside the samples folder (sub-folders allowed, never above it)
        auto rel = juce::URL::removeEscapeChars (p.fromFirstOccurrenceOf ("/samples/", false, false));
        if (rel.contains ("..") || rel.containsChar (':') || rel.startsWithChar ('/') || rel.startsWithChar ('\\')) return std::nullopt;
        auto f = proc.samplesDir.getChildFile (rel);
        if (! f.isAChildOf (proc.samplesDir)) return std::nullopt;
        juce::MemoryBlock mb;
        if (! f.existsAsFile() || f.getSize() > 200 * 1024 * 1024 || ! f.loadFileAsData (mb)) return std::nullopt;
        return bytes (mb.getData(), mb.getSize(), "application/octet-stream");
    }
    if (p.startsWith ("/roms/"))
    {
        auto name = juce::URL::removeEscapeChars (p.fromFirstOccurrenceOf ("/roms/", false, false));
        if (name.containsAnyOf ("/\\:") || name.contains ("..")) return std::nullopt;
        auto f = proc.romDir.getChildFile (name);
        juce::MemoryBlock mb;
        if (! f.existsAsFile() || ! f.loadFileAsData (mb)) return std::nullopt;
        return bytes (mb.getData(), mb.getSize(), "application/zip");
    }
    return std::nullopt;
}
#endif

void FarpEditor::send (const juce::String& js)
{
#if JUCE_WEB_BROWSER
    if (web) web->evaluateJavascript (js);
#else
    juce::ignoreUnused (js);
#endif
}

void FarpEditor::pickRomFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the folder with your ROM zips", proc.romDir, "*");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto dir = fc.getResult();
                              if (! dir.isDirectory()) return;
                              proc.romDir = dir;
                              send ("window.onRomFolder&&window.onRomFolder(" + juce::JSON::toString (juce::var (dir.getFullPathName())) + ")");
                          });
}

void FarpEditor::pickSampleFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose your samples folder", proc.samplesDir, "*");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto dir = fc.getResult();
                              if (! dir.isDirectory()) return;
                              proc.samplesDir = dir;
                              send ("window.onSampleFolder&&window.onSampleFolder(" + juce::JSON::toString (juce::var (dir.getFullPathName())) + ")");
                          });
}

static void appendNum (juce::String& s, float v)
{
    if (! std::isfinite (v)) { s << "0"; return; }
    char b[24];
    std::snprintf (b, sizeof b, "%.4g", (double) v);
    s << b;
}

void FarpEditor::timerCallback()
{
    const int gen = proc.stateGeneration.load();
    if (gen != seenGeneration)
    {
        // the host restored a saved state: reload the page so it shows those settings
        seenGeneration = gen;
        pageReady = false;
#if JUCE_WEB_BROWSER
        if (web) web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
#endif
        return;
    }
    if (! pageReady) return;

    std::vector<std::string> msgs; std::vector<int> keys; float scope[1024], peaks[12]; std::string state; bool haveScope = false; double t = 0, bpm = 0;
    {
        std::lock_guard<std::mutex> g (proc.outbox.m);
        msgs.swap (proc.outbox.msgs);
        keys.swap (proc.outbox.keys);
        proc.outbox.keys.reserve (256);
        if (proc.outbox.scopeNew)
        {
            std::copy (proc.outbox.scope, proc.outbox.scope + 1024, scope);
            std::copy (proc.outbox.peaks, proc.outbox.peaks + 12, peaks);
            state = proc.outbox.state;
            proc.outbox.scopeNew = false; haveScope = true;
        }
        t = proc.outbox.time; bpm = proc.outbox.bpm;
    }
    juce::String js;
    js.preallocateBytes (12000);
    js << "window.__farpRx&&window.__farpRx({clock:" << juce::String (t, 6) << ",bpm:" << juce::String (bpm, 3) << ",msgs:[";
    for (size_t i = 0; i < msgs.size(); i++) { if (i) js << ","; js << juce::String (msgs[i]); }
    js << "],keys:[";
    for (size_t i = 0; i < keys.size(); i++) { if (i) js << ","; js << keys[i]; }
    js << "]";
    if (haveScope)
    {
        js << ",scope:{t:\"scope\",d:[";
        for (int i = 0; i < 1024; i++) { if (i) js << ","; appendNum (js, scope[i]); }
        js << "],m:[";
        for (int i = 0; i < 12; i++) { if (i) js << ","; appendNum (js, peaks[i]); }
        js << "],st:" << (state.empty() ? juce::String ("null") : juce::String (state)) << "}";
    }
    js << "})";
    send (js);
}
