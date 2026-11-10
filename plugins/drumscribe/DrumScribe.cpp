#include "DrumScribe.h"

namespace scribe {

// ======================================================================================================== client
juce::File Client::cacheDir()
{
    auto d = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Drum Scribe");
    d.createDirectory();
    return d;
}

void Client::set(const juce::String& state, const juce::String& msg, double p)
{
    const juce::ScopedLock l(lock);
    st.state = state;
    st.message = msg;
    if (state == "error") st.error = msg;
    if (p >= 0) st.progress = p;
    ++ver;
}

juce::String Client::request(const juce::String& method, const juce::String& path, const juce::MemoryBlock* body,
                             const juce::String& contentType, juce::MemoryBlock* out, int timeoutMs)
{
    auto base = server.trim();
    if (! base.startsWithIgnoreCase("http")) base = "http://" + base;
    while (base.endsWithChar('/')) base = base.dropLastCharacters(1);
    juce::URL url(base + path);
    juce::String headers;
    if (token.isNotEmpty()) headers << "X-Token: " << token << "\r\n";
    auto opts = juce::URL::InputStreamOptions(body ? juce::URL::ParameterHandling::inPostData : juce::URL::ParameterHandling::inAddress);
    if (body != nullptr) {
        url = url.withPOSTData(*body);
        headers << "Content-Type: " << contentType << "\r\n";
    }
    int code = 0;
    opts = opts.withExtraHeaders(headers).withConnectionTimeoutMs(timeoutMs).withStatusCode(&code).withHttpRequestCmd(method);
    auto in = url.createInputStream(opts);
    if (in == nullptr) return "can't reach the Drum Scribe engine at " + base + " (is it running?)";
    juce::MemoryBlock data;
    in->readIntoMemoryBlock(data);
    if (code >= 400) {
        auto v = juce::JSON::parse(data.toString());
        auto e = v.getProperty("error", juce::var()).toString();
        return e.isNotEmpty() ? e : "engine error " + juce::String(code);
    }
    if (out) *out = std::move(data);
    return {};
}

juce::String Client::health()
{
    juce::MemoryBlock m;
    auto e = request("GET", "/api/health", nullptr, {}, &m, 3000);
    if (e.isNotEmpty()) return e;
    return juce::JSON::parse(m.toString()).getProperty("ok", false) ? juce::String() : "not a Drum Scribe engine";
}

void Client::startLink(const juce::String& l, bool f, double t)
{
    stopThread(2000);
    link = l; fast = f; tempo = t; op = Op::link;
    set("running", "Sending the link", 0);
    startThread();
}

void Client::startFile(const juce::File& fl, bool f, double t)
{
    stopThread(2000);
    file = fl; fast = f; tempo = t; op = Op::file;
    set("running", "Sending " + fl.getFileName(), 0);
    startThread();
}

void Client::resume(const juce::String& id)
{
    stopThread(2000);
    { const juce::ScopedLock l(lock); job = id; }
    op = Op::resume;
    set("running", "Loading the last result", 0);
    startThread();
}

void Client::retempo(double t)
{
    if (jobId().isEmpty()) return;
    stopThread(2000);
    tempo = t; op = Op::retempo;
    set("running", "Rewriting at " + juce::String(t, 2) + " BPM", 1);
    startThread();
}

bool Client::fetchResult()
{
    const auto id = jobId();
    juce::MemoryBlock m;
    auto e = request("GET", "/api/jobs/" + id + "?hits=1", nullptr, {}, &m);
    if (e.isNotEmpty()) { set("error", e); return false; }
    auto v = juce::JSON::parse(m.toString());
    auto r = v.getProperty("result", juce::var());
    if (! r.isObject()) { set("error", "the engine sent no result"); return false; }
    auto res2 = std::make_shared<Result>();
    res2->title = r.getProperty("title", "song").toString();
    res2->tempo = (double) r.getProperty("tempo", 120.0);
    res2->duration = (double) r.getProperty("duration", 0.0);
    res2->midiName = r.getProperty("midi", "drums.mid").toString();
    res2->splitName = r.getProperty("midi_split", "drums split.mid").toString();
    if (auto* hs = r.getProperty("hits", juce::var()).getArray())
        for (auto& h : *hs)
            if (auto* a = h.getArray(); a && a->size() >= 3)
                res2->hits.push_back({ (double) (*a)[0], (int) (*a)[1], (int) (*a)[2] });
    if (auto* ps = r.getProperty("pieces", juce::var()).getArray())
        for (auto& p : *ps)
            res2->pieces.push_back({ p.getProperty("label", "").toString(), (int) p.getProperty("note", 0), (int) p.getProperty("count", 0) });
    // local copies of both MIDI files, for dragging into the DAW
    auto dir = cacheDir().getChildFile(id);
    dir.createDirectory();
    for (int split = 0; split < 2; ++split) {
        juce::MemoryBlock mid;
        auto err = request("GET", "/api/jobs/" + id + "/midi" + (split ? "?split=1" : ""), nullptr, {}, &mid, 60000);
        if (err.isNotEmpty()) { set("error", err); return false; }
        auto f = dir.getChildFile(juce::File::createLegalFileName(split ? res2->splitName : res2->midiName));
        f.replaceWithData(mid.getData(), mid.getSize());
        (split ? res2->splitFile : res2->midiFile) = f;
    }
    {
        const juce::ScopedLock l(lock);
        res = res2;
    }
    set("done", juce::String((int) res2->hits.size()) + " hits, " + juce::String(res2->tempo, 2) + " BPM", 1);
    return true;
}

void Client::run()
{
    juce::MemoryBlock m;
    juce::String e;
    if (op == Op::link) {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty("link", link);
        o->setProperty("quality", fast ? "fast" : "best");
        if (tempo > 0) o->setProperty("tempo", tempo);
        auto body = juce::JSON::toString(juce::var(o.release()), true);
        juce::MemoryBlock b(body.toRawUTF8(), body.getNumBytesAsUTF8());
        e = request("POST", "/api/jobs", &b, "application/json", &m);
    } else if (op == Op::file) {
        juce::MemoryBlock b;
        if (! file.loadFileAsData(b)) { set("error", "can't read " + file.getFullPathName()); return; }
        auto q = "/api/upload?name=" + juce::URL::addEscapeChars(file.getFileName(), true) + "&quality=" + (fast ? "fast" : "best");
        if (tempo > 0) q << "&tempo=" << juce::String(tempo, 3);
        e = request("POST", q, &b, "application/octet-stream", &m, 120000);
    } else if (op == Op::retempo) {
        auto body = "{\"tempo\": " + juce::String(tempo, 4) + "}";
        juce::MemoryBlock b(body.toRawUTF8(), body.getNumBytesAsUTF8());
        e = request("POST", "/api/jobs/" + jobId() + "/tempo", &b, "application/json", &m, 120000);
        if (e.isEmpty()) fetchResult(); else set("error", e);
        return;
    }
    if (e.isNotEmpty()) { set("error", e); return; }
    if (op != Op::resume) {
        auto id = juce::JSON::parse(m.toString()).getProperty("id", "").toString();
        if (id.isEmpty()) { set("error", "the engine didn't start a job"); return; }
        const juce::ScopedLock l(lock);
        job = id;
    }
    int fails = 0;
    while (! threadShouldExit()) {
        e = request("GET", "/api/jobs/" + jobId(), nullptr, {}, &m);
        if (e.isNotEmpty()) {
            if (++fails > 20) { set("error", e); return; }
            set("running", "Waiting for the engine: " + e);
        } else {
            fails = 0;
            auto v = juce::JSON::parse(m.toString());
            auto state = v.getProperty("state", "").toString();
            auto msg = v.getProperty("message", "").toString();
            if (state == "error") { set("error", msg.isNotEmpty() ? msg : v.getProperty("error", "failed").toString()); return; }
            if (state == "done") { fetchResult(); return; }
            set("running", msg, (double) v.getProperty("progress", 0.0));
        }
        wait(1000);
    }
}

// ======================================================================================================== processor
Processor::Processor()
    : juce::AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                            .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == l.getMainInputChannelSet() && ! l.getMainOutputChannelSet().isDisabled();
}

void Processor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    // audio passes through; only the host's tempo is of interest
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
}

void Processor::getStateInformation(juce::MemoryBlock& out)
{
    juce::ValueTree t("DrumScribe");
    t.setProperty("server", client.server, nullptr);
    t.setProperty("token", client.token, nullptr);
    t.setProperty("link", lastLink, nullptr);
    t.setProperty("job", client.jobId(), nullptr);
    t.setProperty("fast", fast, nullptr);
    t.setProperty("hostTempo", useHostTempo, nullptr);
    t.setProperty("split", splitTracks, nullptr);
    juce::MemoryOutputStream s(out, false);
    t.writeToStream(s);
}

void Processor::setStateInformation(const void* data, int size)
{
    auto t = juce::ValueTree::readFromData(data, (size_t) size);
    if (! t.isValid()) return;
    client.server = t.getProperty("server", client.server).toString();
    client.token = t.getProperty("token", "").toString();
    lastLink = t.getProperty("link", "").toString();
    fast = t.getProperty("fast", false);
    useHostTempo = t.getProperty("hostTempo", true);
    splitTracks = t.getProperty("split", false);
    auto job = t.getProperty("job", "").toString();
    if (job.isNotEmpty() && ! client.busy()) client.resume(job);
}

// ======================================================================================================== editor
namespace {
const juce::Colour BG(0xff111317), PANEL(0xff1b1e24), LINE(0xff2c313a), TEXT(0xffe8eaee), DIM(0xff9aa1ad),
                   ACCENT(0xfff2a33a), BAD(0xffe5615b);

struct Look : juce::LookAndFeel_V4 {
    Look()
    {
        setColour(juce::ResizableWindow::backgroundColourId, BG);
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d0f12));
        setColour(juce::TextEditor::outlineColourId, LINE);
        setColour(juce::TextEditor::focusedOutlineColourId, ACCENT);
        setColour(juce::TextEditor::textColourId, TEXT);
        setColour(juce::TextButton::buttonColourId, PANEL);
        setColour(juce::TextButton::textColourOffId, TEXT);
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0d0f12));
        setColour(juce::ComboBox::outlineColourId, LINE);
        setColour(juce::ComboBox::textColourId, TEXT);
        setColour(juce::PopupMenu::backgroundColourId, PANEL);
        setColour(juce::ToggleButton::textColourId, TEXT);
        setColour(juce::ToggleButton::tickColourId, ACCENT);
        setColour(juce::Label::textColourId, DIM);
    }
};

// every hit, one lane per kit piece; brightness = velocity
struct Lanes : juce::Component {
    std::shared_ptr<const Result> r;
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0d0f12));
        if (! r || r->pieces.empty()) {
            g.setColour(DIM);
            g.drawText("The hits show here.", getLocalBounds(), juce::Justification::centred);
            return;
        }
        const int n = (int) r->pieces.size();
        const float lane = (float) getHeight() / (float) n, w = (float) getWidth();
        std::map<int, int> idx;
        for (int i = 0; i < n; ++i) {
            idx[r->pieces[(size_t) i].note] = i;
            g.setColour(i % 2 ? juce::Colour(0xff14171c) : juce::Colour(0xff101317));
            g.fillRect(0.f, (float) i * lane, w, lane);
        }
        const double dur = juce::jmax(1.0, r->duration);
        for (auto& h : r->hits) {
            auto it = idx.find(h.note);
            if (it == idx.end()) continue;
            g.setColour(juce::Colour::fromHSV((30.f + (float) it->second * 23.f) / 360.f, 0.8f, 0.35f + 0.65f * (float) h.vel / 127.f, 1.f));
            g.fillRect((float) (h.time / dur) * w, ((float) it->second + 0.15f) * lane, 1.2f, lane * 0.7f);
        }
        g.setFont(juce::FontOptions(juce::jmin(12.f, lane * 0.7f)));
        g.setColour(DIM);
        for (int i = 0; i < n; ++i)
            g.drawText(r->pieces[(size_t) i].label + "  " + juce::String(r->pieces[(size_t) i].count), 6, (int) ((float) i * lane), 300, (int) lane, juce::Justification::centredLeft);
    }
};

// drag this onto a MIDI track
struct DragTile : juce::Component {
    std::function<juce::File()> file;
    bool armed = false;
    void paint(juce::Graphics& g) override
    {
        auto f = file ? file() : juce::File();
        auto b = getLocalBounds().toFloat().reduced(1);
        g.setColour(f.existsAsFile() ? ACCENT : LINE);
        g.drawRoundedRectangle(b, 8, 2);
        g.setColour(f.existsAsFile() ? TEXT : DIM);
        g.setFont(juce::FontOptions(14.f, juce::Font::bold));
        g.drawFittedText(f.existsAsFile() ? "Drag the MIDI onto a track\n" + f.getFileName() : "The MIDI to drag shows up here",
                         getLocalBounds().reduced(10), juce::Justification::centred, 2);
    }
    void mouseDown(const juce::MouseEvent&) override { armed = true; }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        auto f = file ? file() : juce::File();
        if (armed && f.existsAsFile() && e.getDistanceFromDragStart() > 6) {
            armed = false;
            juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
        }
    }
};

class Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor& p) : AudioProcessorEditor(p), proc(p)
    {
        setLookAndFeel(&look);
        auto label = [this](juce::Label& l, const juce::String& s) { l.setText(s, juce::dontSendNotification); l.setFont(juce::FontOptions(13.f)); addAndMakeVisible(l); };
        label(linkLabel, "YouTube, Spotify or SoundCloud link");
        label(serverLabel, "Engine");
        label(tempoLabel, "Tempo");
        addAndMakeVisible(link);
        link.setTextToShowWhenEmpty("https://www.youtube.com/watch?v=...", DIM.withAlpha(0.6f));
        link.setText(p.lastLink, false);
        link.onReturnKey = [this] { analyze(); };
        addAndMakeVisible(server);
        server.setText(p.client.server, false);
        server.onTextChange = [this] { proc.client.server = server.getText(); };
        addAndMakeVisible(quality);
        quality.addItem("Best separation", 1);
        quality.addItem("Fast separation", 2);
        quality.setSelectedId(p.fast ? 2 : 1, juce::dontSendNotification);
        quality.onChange = [this] { proc.fast = quality.getSelectedId() == 2; };
        addAndMakeVisible(hostTempo);
        hostTempo.setButtonText("Use the project's tempo");
        hostTempo.setToggleState(p.useHostTempo, juce::dontSendNotification);
        hostTempo.onClick = [this] { proc.useHostTempo = hostTempo.getToggleState(); updateTempoBox(); };
        addAndMakeVisible(tempo);
        tempo.setInputRestrictions(7, "0123456789.");
        tempo.setTextToShowWhenEmpty("detect", DIM.withAlpha(0.6f));
        addAndMakeVisible(go);
        go.setButtonText("Make drum MIDI");
        go.setColour(juce::TextButton::buttonColourId, ACCENT);
        go.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff1a1205));
        go.onClick = [this] { analyze(); };
        addAndMakeVisible(pick);
        pick.setButtonText("Audio file...");
        pick.onClick = [this] { pickFile(); };
        addAndMakeVisible(rewrite);
        rewrite.setButtonText("Rewrite at this tempo");
        rewrite.onClick = [this] { if (auto t = tempoValue(); t > 0) proc.client.retempo(t); };
        addAndMakeVisible(split);
        split.setButtonText("One track per kit piece");
        split.setToggleState(p.splitTracks, juce::dontSendNotification);
        split.onClick = [this] { proc.splitTracks = split.getToggleState(); repaint(); drag.repaint(); };
        addAndMakeVisible(save);
        save.setButtonText("Save MIDI...");
        save.onClick = [this] { saveMidi(); };
        addAndMakeVisible(lanes);
        addAndMakeVisible(drag);
        drag.file = [this]() -> juce::File {
            auto r = proc.client.result();
            return r ? (proc.splitTracks ? r->splitFile : r->midiFile) : juce::File();
        };
        addAndMakeVisible(bar);
        addAndMakeVisible(status);
        status.setFont(juce::FontOptions(13.f));
        status.setJustificationType(juce::Justification::centredLeft);
        updateTempoBox();
        setResizable(true, true);
        setResizeLimits(560, 480, 1600, 1200);
        setSize(760, 600);
        startTimerHz(10);
        timerCallback();
    }
    ~Editor() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(BG);
        g.setColour(TEXT);
        g.setFont(juce::FontOptions(20.f, juce::Font::bold));
        g.drawText("DRUM SCRIBE", 16, 10, 300, 28, juce::Justification::centredLeft);
        g.setColour(DIM);
        g.setFont(juce::FontOptions(13.f));
        g.drawText("song link in, every drum hit out: unquantized, with velocities, every kit piece", 170, 10, getWidth() - 186, 28,
                   juce::Justification::centredLeft);
        if (auto r = proc.client.result()) {
            g.setColour(TEXT);
            g.setFont(juce::FontOptions(15.f, juce::Font::bold));
            g.drawText(r->title + "   " + juce::String(r->tempo, 2) + " BPM   " + juce::String((int) r->hits.size()) + " hits",
                       resultLine, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(16);
        b.removeFromTop(34);
        linkLabel.setBounds(b.removeFromTop(20));
        auto r1 = b.removeFromTop(32);
        pick.setBounds(r1.removeFromRight(120));
        r1.removeFromRight(8);
        link.setBounds(r1);
        b.removeFromTop(8);
        auto r2 = b.removeFromTop(30);
        quality.setBounds(r2.removeFromLeft(170));
        r2.removeFromLeft(10);
        hostTempo.setBounds(r2.removeFromLeft(190));
        tempoLabel.setBounds(r2.removeFromLeft(50));
        tempo.setBounds(r2.removeFromLeft(80));
        r2.removeFromLeft(10);
        go.setBounds(r2);
        b.removeFromTop(8);
        bar.setBounds(b.removeFromTop(10));
        b.removeFromTop(4);
        status.setBounds(b.removeFromTop(22));
        b.removeFromTop(6);
        resultLine = b.removeFromTop(24);
        auto bottom = b.removeFromBottom(30);
        serverLabel.setBounds(bottom.removeFromLeft(52));
        server.setBounds(bottom.removeFromLeft(260));
        b.removeFromBottom(8);
        auto r3 = b.removeFromBottom(64);
        drag.setBounds(r3.removeFromLeft(r3.getWidth() / 2));
        r3.removeFromLeft(10);
        auto c = r3.removeFromTop(30);
        save.setBounds(c.removeFromLeft(130));
        c.removeFromLeft(8);
        rewrite.setBounds(c);
        r3.removeFromTop(4);
        split.setBounds(r3);
        b.removeFromBottom(8);
        lanes.setBounds(b);
    }

private:
    double tempoValue() const
    {
        if (proc.useHostTempo && proc.hostBpm() > 0) return proc.hostBpm();
        return tempo.getText().getDoubleValue();
    }

    void updateTempoBox()
    {
        tempo.setEnabled(! proc.useHostTempo);
        if (proc.useHostTempo) tempo.setText(proc.hostBpm() > 0 ? juce::String(proc.hostBpm(), 2) : "", false);
    }

    void analyze()
    {
        auto l = link.getText().trim();
        if (l.isEmpty()) { localError = "Paste a link first."; return; }
        localError.clear();
        proc.lastLink = l;
        proc.client.startLink(l, proc.fast, tempoValue());
    }

    void pickFile()
    {
        chooser = std::make_unique<juce::FileChooser>("Audio file", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.aac;*.ogg;*.opus;*.mp4;*.webm");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
            if (c.getResult().existsAsFile()) proc.client.startFile(c.getResult(), proc.fast, tempoValue());
        });
    }

    void saveMidi()
    {
        auto f = drag.file();
        if (! f.existsAsFile()) return;
        chooser = std::make_unique<juce::FileChooser>("Save MIDI", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(f.getFileName()), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [f](const juce::FileChooser& c) {
            auto dst = c.getResult();
            if (dst != juce::File()) f.copyFileTo(dst.withFileExtension(".mid"));
        });
    }

    void timerCallback() override
    {
        if (proc.useHostTempo && ! tempo.hasKeyboardFocus(true)) updateTempoBox();
        const int v = proc.client.version();
        if (v == seen && localError == shownError) return;
        seen = v;
        shownError = localError;
        auto s = proc.client.status();
        progress = s.progress;
        const bool running = proc.client.busy() && s.state == "running";
        go.setEnabled(! running);
        pick.setEnabled(! running);
        status.setColour(juce::Label::textColourId, (s.state == "error" || localError.isNotEmpty()) ? BAD : DIM);
        auto text = localError.isNotEmpty() ? localError : s.message;
        if (running) text << "  (" << juce::String(juce::roundToInt(s.progress * 100)) << "%)";
        if (s.state == "error" && s.message.startsWith("can't reach"))
            text << " - start it with: drumscribe serve";
        status.setText(text, juce::dontSendNotification);
        lanes.r = proc.client.result();
        lanes.repaint();
        drag.repaint();
        repaint();
    }

    Processor& proc;
    Look look;
    juce::Label linkLabel, serverLabel, tempoLabel, status;
    juce::TextEditor link, server, tempo;
    juce::ComboBox quality;
    juce::ToggleButton hostTempo, split;
    juce::TextButton go, pick, rewrite, save;
    double progress = 0;
    juce::ProgressBar bar { progress };
    Lanes lanes;
    DragTile drag;
    juce::Rectangle<int> resultLine;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String localError, shownError;
    int seen = -1;
};
}  // namespace

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

}  // namespace scribe

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new scribe::Processor(); }
