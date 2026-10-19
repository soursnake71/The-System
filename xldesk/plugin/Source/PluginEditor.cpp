#include "PluginEditor.h"
#include "BinaryData.h"

std::optional<juce::WebBrowserComponent::Resource> XLDeskEditor::resource (const juce::String& url)
{
    auto file = url.fromLastOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
    if (file.isEmpty()) file = "index.html";
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (file == BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i]))
        {
            int size = 0; const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
            const auto ext = file.fromLastOccurrenceOf (".", false, false);
            const juce::String mime = ext == "html" ? "text/html; charset=utf-8" : ext == "js" ? "text/javascript; charset=utf-8" : ext == "jpg" ? "image/jpeg" : ext == "svg" ? "image/svg+xml" : ext == "png" ? "image/png" : "application/octet-stream";
            std::vector<std::byte> bytes;
            if (ext == "html" || ext == "js")
            {
                // serve text as plain ASCII (entities in HTML, \u escapes in JS): some WebViews ignore the charset
                const auto text = juce::String::fromUTF8 (data, size); juce::String out; out.preallocateBytes ((size_t) size + 1024);
                for (auto t = text.getCharPointer(); ! t.isEmpty();)
                {
                    const auto c = t.getAndAdvance();
                    if (c < 128) { out << juce::String::charToString (c); continue; }
                    if (ext == "html") { out << "&#x" << juce::String::toHexString ((int) c) << ";"; continue; }
                    auto u16 = [&] (juce::uint32 u) { out << "\\u" << juce::String::toHexString ((int) u).paddedLeft ('0', 4); };
                    if (c > 0xFFFF) { const auto v = (juce::uint32) c - 0x10000; u16 (0xD800 + (v >> 10)); u16 (0xDC00 + (v & 0x3FF)); } else u16 ((juce::uint32) c);
                }
                const auto* raw = out.toRawUTF8(); bytes.resize (out.getNumBytesAsUTF8()); std::memcpy (bytes.data(), raw, bytes.size());
            }
            else { bytes.resize ((size_t) size); std::memcpy (bytes.data(), data, (size_t) size); }
            return juce::WebBrowserComponent::Resource { std::move (bytes), mime };
        }
    return std::nullopt;
}

XLDeskEditor::XLDeskEditor (XLDeskProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    sent.assign (xlspec::NPARAMS, -1.f); uiTouched.assign (xlspec::NPARAMS, 0);
    auto opts = juce::WebBrowserComponent::Options {}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}.withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("XLDeskWebView")))
        .withNativeIntegrationEnabled()
        .withResourceProvider ([this] (const juce::String& u) { return resource (u); })
        .withEventListener ("xl", [this] (const juce::var& m) { fromUi (m); });
    browser = std::make_unique<juce::WebBrowserComponent> (opts);
    addAndMakeVisible (*browser);
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable (true, true); setResizeLimits (640, 420, 3840, 2400); setSize (1280, 820);
    startTimerHz (30);
}
XLDeskEditor::~XLDeskEditor() { stopTimer(); }
void XLDeskEditor::resized() { browser->setBounds (getLocalBounds()); }

void XLDeskEditor::fromUi (const juce::var& m)
{
    const auto t = m["t"].toString();
    if (t == "hello")
    {
        // everything the page needs: values, fitted modules, its saved settings, and that it is inside a plugin
        juce::DynamicObject::Ptr P = new juce::DynamicObject(), T = new juce::DynamicObject(), o = new juce::DynamicObject();
        for (int i = 0; i < xlspec::NPARAMS; ++i) { const float v = proc.param (i)->getValue(); P->setProperty (xlspec::PARAMS[i].id, v); sent[(size_t) i] = v; }
        for (int w = 0; w < xlspec::NWHERE; ++w) T->setProperty (xlspec::WHERE[w].id, proc.typeAt (w));
        o->setProperty ("t", "init"); o->setProperty ("P", P.get()); o->setProperty ("types", T.get()); o->setProperty ("ui", proc.getUi());
        o->setProperty ("micRef", proc.micRef); o->setProperty ("convRef", proc.convRef); o->setProperty ("wrapper", juce::AudioProcessor::getWrapperTypeDescription (proc.wrapperType));
        send (o.get()); ready = true;
    }
    else if (t == "p")
    {
        const int i = proc.paramIndex (m["id"].toString()); if (i < 0) return;
        const float v = (float) m["v"]; sent[(size_t) i] = v; uiTouched[(size_t) i] = juce::Time::getMillisecondCounter(); proc.setFromUi (i, v);
    }
    else if (t == "type") proc.setType (m["w"].toString(), m["type"].toString());
    else if (t == "ui") proc.setUi (m["ui"].toString());
    else if (t == "cfg") proc.setCfg ((double) m["cfg"]["micRef"], (double) m["cfg"]["convRef"]);
    else if (t == "learn") proc.armLearn (m["id"].toString());
    else if (t == "midiDevices") send (proc.midiDevices());
    else if (t == "midiout")
    {
        juce::MemoryBlock b; for (auto& x : *m["bytes"].getArray()) { const auto c = (juce::uint8) (int) x; b.append (&c, 1); }
        if (b.getSize()) proc.sendMidiOut (juce::MidiMessage (b.getData(), (int) b.getSize()));
    }
}

void XLDeskEditor::timerCallback()
{
    if (! ready) return;
    auto& M = proc.meters();
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    auto arr = [] (auto& a) { juce::Array<juce::var> r; for (auto& x : a) { r.add (xl::toDbu (x.load())); x = 0; } return r; };
    o->setProperty ("t", "m"); o->setProperty ("m", arr (M.mono)); o->setProperty ("s", arr (M.stereo)); o->setProperty ("b", arr (M.bus));
    o->setProperty ("gc", M.gcomp.load()); o->setProperty ("pos", 0); o->setProperty ("play", false); o->setProperty ("rec", false);
    juce::DynamicObject::Ptr mod = new juce::DynamicObject();
    for (int w = 0; w < xlspec::NWHERE; ++w)
    {
        const auto* d = xl::moduleDef (proc.typeAt (w).toStdString()); if (! d) continue;
        auto& v = M.mod[(size_t) w];
        if (d->stereo) mod->setProperty (xlspec::WHERE[w].id, v[0].load());
        else { juce::Array<juce::var> a { v[0].load(), v[1].load() }; mod->setProperty (xlspec::WHERE[w].id, a); }
        v[0] = 0; v[1] = 0;
    }
    o->setProperty ("mod", mod.get());
    send (o.get());
    // automation, MIDI and snapshot changes reach the page; the page's own moves are not echoed back for half a second
    // one message per frame however many controls moved (a snapshot or a busy automation lane moves hundreds)
    const auto now = juce::Time::getMillisecondCounter();
    juce::DynamicObject::Ptr changed = new juce::DynamicObject(); int nChanged = 0;
    for (int i = 0; i < xlspec::NPARAMS; ++i)
    {
        const float v = proc.param (i)->getValue();
        if (std::abs (v - sent[(size_t) i]) < 1e-7f || now - uiTouched[(size_t) i] < 500) continue;
        sent[(size_t) i] = v; changed->setProperty (xlspec::PARAMS[i].id, v); ++nChanged;
    }
    if (nChanged) { juce::DynamicObject::Ptr p = new juce::DynamicObject(); p->setProperty ("t", "pp"); p->setProperty ("P", changed.get()); send (p.get()); }
    for (int w = 0; w < xlspec::NWHERE; ++w)
    {
        const auto ty = proc.typeAt (w);
        if (sentTypes.size() <= w) sentTypes.add (ty);
        else if (sentTypes[w] != ty) { sentTypes.set (w, ty); juce::DynamicObject::Ptr p = new juce::DynamicObject(); p->setProperty ("t", "type"); p->setProperty ("w", xlspec::WHERE[w].id); p->setProperty ("type", ty); send (p.get()); }
    }
    for (auto& n : proc.takeNotices()) send (n);
}
