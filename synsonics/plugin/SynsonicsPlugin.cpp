// Omega Synsonics as a VST3 / AU instrument and a standalone app (JUCE). The sound is core/Engine.h and the panel
// is core/Panel.h, the same code the Android app runs; this file only connects them to the host: parameters for
// automation, MIDI notes as the trigger inputs, host tempo, saved state (with the sample pad), file dialogs for import
// and export, and a window the panel draws into.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../core/Panel.h"
#include "../core/Files.h"
#include <mutex>

namespace {

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout l;
  for (int i = 0; i < syn::NumParams; ++i) {
    const auto& p = syn::paramInfo()[i];
    if (p.toggle)
      l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{p.id, 1}, p.name, p.def > 0.5f));
    else
      l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{p.id, 1}, p.name, juce::NormalisableRange<float>(0.f, 1.f), p.def));
  }
  return l;
}

class SynsonicsProcessor : public juce::AudioProcessor {
 public:
  SynsonicsProcessor()
      : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        apvts(*this, nullptr, "SYNSONICS", makeLayout()) {
    for (int i = 0; i < syn::NumParams; ++i) raw[i] = apvts.getRawParameterValue(syn::paramInfo()[i].id);
  }

  syn::Engine engine;
  juce::AudioProcessorValueTreeState apvts;
  std::atomic<float>* raw[syn::NumParams];

  // file jobs run here, off the message thread; their result reaches the panel through the status
  juce::ThreadPool jobs{1};
  std::mutex statusMutex;
  std::string status;
  std::atomic<int> statusVersion{0};
  std::atomic<bool> busy{false};
  void setStatus(const std::string& st) { std::lock_guard<std::mutex> lk(statusMutex); status = st; ++statusVersion; }
  ~SynsonicsProcessor() override { jobs.removeAllJobs(true, 60000); }

  void exportTo(int op, syn::ExportOptions o, juce::File file) {
    if (busy.exchange(true)) { setStatus("Still working on the last file."); return; }
    setStatus("Exporting " + file.getFileName().toStdString() + "...");
    jobs.addJob([this, op, o, file] {
      std::vector<uint8_t> bytes;
      std::string err = syn::exportFile(engine, o, op, bytes);
      if (err.empty()) err = file.replaceWithData(bytes.data(), bytes.size()) ? "" : "Couldn't write " + file.getFullPathName().toStdString();
      setStatus(err.empty() ? "Saved " + file.getFullPathName().toStdString() + " (" + std::to_string(bytes.size() / 1024) + " KB)." : err);
      busy = false;
    });
  }
  void importFrom(int op, juce::File file, int pattern) {
    if (busy.exchange(true)) { setStatus("Still working on the last file."); return; }
    jobs.addJob([this, op, file, pattern] {
      juce::MemoryBlock mb;
      std::string info, err;
      if (!file.loadFileAsData(mb)) err = "Couldn't read " + file.getFullPathName().toStdString();
      else {
        std::vector<uint8_t> data(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
        err = syn::importFile(engine, op, data, file.getFileName().toStdString(), pattern, info);
      }
      setStatus(err.empty() ? "Loaded " + info + "." : err);
      busy = false;
    });
  }

  juce::RangedAudioParameter* param(int i) { return apvts.getParameter(syn::paramInfo()[i].id); }

  void prepareToPlay(double sr, int) override { engine.setSampleRate(sr); }
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& l) const override {
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
  }

  void processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override {
    juce::ScopedNoDenormals nd;
    for (int i = 0; i < syn::NumParams; ++i) engine.params[i] = raw[i]->load();
    syn::HostTime ht;
    if (auto* ph = getPlayHead())
      if (auto pos = ph->getPosition()) {
        ht.valid = pos->getBpm().hasValue() && pos->getPpqPosition().hasValue();
        ht.playing = pos->getIsPlaying();
        ht.bpm = pos->getBpm().orFallback(120.0);
        ht.ppq = pos->getPpqPosition().orFallback(0.0);
      }
    int n = buf.getNumSamples();
    tmpL.resize(size_t(n)); tmpR.resize(size_t(n));
    int done = 0;
    double ppqPerSample = ht.bpm / 60.0 / getSampleRate();
    auto renderTo = [&](int end) {
      if (end <= done) return;
      float* o[2] = {tmpL.data() + done, tmpR.data() + done};
      syn::HostTime h = ht;
      h.ppq = ht.ppq + done * ppqPerSample;
      engine.process(o, end - done, h);
      done = end;
    };
    for (const auto m : midi) {  // the trigger inputs, sample accurate
      renderTo(std::clamp(m.samplePosition, 0, n));
      auto msg = m.getMessage();
      if (msg.isNoteOn()) engine.midiNote(msg.getNoteNumber(), true);
      else if (msg.isNoteOff()) engine.midiNote(msg.getNoteNumber(), false);
    }
    renderTo(n);
    midi.clear();
    if (buf.getNumChannels() >= 2) {
      buf.copyFrom(0, 0, tmpL.data(), n);
      buf.copyFrom(1, 0, tmpR.data(), n);
      for (int c = 2; c < buf.getNumChannels(); ++c) buf.clear(c, 0, n);
    } else if (buf.getNumChannels() == 1) {
      auto* d = buf.getWritePointer(0);
      for (int i = 0; i < n; ++i) d[i] = 0.5f * (tmpL[size_t(i)] + tmpR[size_t(i)]);
    }
  }

  void getStateInformation(juce::MemoryBlock& dest) override {
    auto st = apvts.copyState();
    auto blob = engine.saveState();
    st.setProperty("engine", juce::Base64::toBase64(blob.data(), blob.size()), nullptr);
    // the sample pad travels with the project (as a 24-bit WAV), up to about five minutes of stereo
    if (auto smp = engine.sample(); smp && smp->frames() < size_t(48000) * 300) {
      auto wav = syn::encodeWav(smp->l, smp->r.empty() ? smp->l : smp->r, int(smp->rate), 24);
      st.setProperty("sample", juce::Base64::toBase64(wav.data(), wav.size()), nullptr);
      st.setProperty("sampleName", juce::String(smp->name), nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
  }
  void setStateInformation(const void* data, int size) override {
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    auto st = juce::ValueTree::fromXml(*xml);
    if (!st.isValid()) return;
    juce::MemoryOutputStream mo;
    if (juce::Base64::convertFromBase64(mo, st.getProperty("engine").toString()))
      engine.loadState(static_cast<const uint8_t*>(mo.getData()), mo.getDataSize());
    st.removeProperty("engine", nullptr);
    juce::MemoryOutputStream ms;
    if (st.hasProperty("sample") && juce::Base64::convertFromBase64(ms, st.getProperty("sample").toString())) {
      std::vector<uint8_t> wav(static_cast<const uint8_t*>(ms.getData()), static_cast<const uint8_t*>(ms.getData()) + ms.getDataSize());
      std::string err;
      if (auto smp = syn::decodeAudio(wav, err)) { smp->name = st.getProperty("sampleName").toString().toStdString(); engine.setSample(smp); }
    } else {
      engine.setSample(nullptr);
    }
    st.removeProperty("sample", nullptr);
    st.removeProperty("sampleName", nullptr);
    apvts.replaceState(st);
  }

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }
  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 2.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return "Default"; }
  void changeProgramName(int, const juce::String&) override {}

 private:
  std::vector<float> tmpL, tmpR;
};

// The editor: the shared panel draws into an image the size of the window in physical pixels.
class SynsonicsEditor : public juce::AudioProcessorEditor, private juce::Timer, private syn::PanelHost {
 public:
  explicit SynsonicsEditor(SynsonicsProcessor& p) : AudioProcessorEditor(p), proc(p), panel(p.engine, *this) {
    setOpaque(true);
    setResizable(true, true);
    lastBent = proc.raw[syn::pMode]->load() > 0.5f;
    applyAspect(true);
    startTimerHz(40);
  }

 private:
  SynsonicsProcessor& proc;
  syn::Panel panel;
  juce::Image img;
  float pixScale = 1;
  bool lastBent = false;

  // PanelHost: knobs and switches go through the automatable parameters
  float getParam(int id) override { return proc.raw[id]->load(); }
  void setParam(int id, float v) override { if (auto* p = proc.param(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); }
  void beginEdit(int id) override { if (auto* p = proc.param(id)) p->beginChangeGesture(); }
  void endEdit(int id) override { if (auto* p = proc.param(id)) p->endChangeGesture(); }

  // FILE page: a save or open dialog, then the job runs on the processor's thread
  std::unique_ptr<juce::FileChooser> chooser;
  int seenStatus = -1;
  void requestFile(int op) override {
    bool exp = syn::isExport(op);
    syn::ExportOptions o = panel.exportOptions();
    int pattern = panel.editPattern();
    juce::String pattern_ = op == syn::OpImportMidi ? "*.mid;*.midi" : op == syn::OpImportAudio ? "*.wav;*.mp3" : juce::String("*.") + syn::opExtension(op);
    juce::File start = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    if (exp) start = start.getChildFile(syn::exportName(proc.engine, o, op));
    chooser = std::make_unique<juce::FileChooser>(exp ? "Export" : "Import", start, pattern_);
    int dlgFlags = exp ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting)
                    : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
    juce::Component::SafePointer<SynsonicsEditor> self(this);
    chooser->launchAsync(dlgFlags, [self, op, o, pattern, exp](const juce::FileChooser& fc) {
      if (!self) return;
      juce::File f = fc.getResult();
      if (f == juce::File()) return;
      if (exp) {
        if (!f.hasFileExtension(syn::opExtension(op))) f = f.withFileExtension(syn::opExtension(op));
        self->proc.exportTo(op, o, f);
      } else {
        self->proc.importFrom(op, f, pattern);
      }
    });
  }

  void applyAspect(bool initial) {
    float aspect = panel.aspect();
    int w = initial ? 560 : getWidth();
    int h = int(std::round(w / aspect));
    if (auto* c = getConstrainer()) {
      c->setFixedAspectRatio(aspect);
      c->setSizeLimits(300, int(300 / aspect), 1600, int(1600 / aspect));
    }
    setSize(w, h);
  }

  bool redraw(bool force) {
    int pw = std::max(1, int(std::round(getWidth() * pixScale))), ph = std::max(1, int(std::round(getHeight() * pixScale)));
    if (!img.isValid() || img.getWidth() != pw || img.getHeight() != ph) { img = juce::Image(juce::Image::ARGB, pw, ph, false); force = true; }
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
    syn::Canvas c;
    c.attach(reinterpret_cast<uint32_t*>(bd.data), pw, ph, bd.lineStride / 4, false);
    return panel.render(c, force);
  }

  void timerCallback() override {
    if (proc.statusVersion != seenStatus) {
      seenStatus = proc.statusVersion;
      std::lock_guard<std::mutex> lk(proc.statusMutex);
      if (!proc.status.empty()) panel.setStatus(proc.status);
    }
    bool b = proc.raw[syn::pMode]->load() > 0.5f;
    if (b != lastBent) { lastBent = b; applyAspect(false); }
    if (redraw(false)) repaint();
  }
  void resized() override { redraw(true); }
  void paint(juce::Graphics& g) override {
    float s = float(g.getInternalContext().getPhysicalPixelScaleFactor());
    if (std::fabs(s - pixScale) > 0.01f) { pixScale = s; redraw(true); }
    g.drawImage(img, getLocalBounds().toFloat());
  }

  juce::Point<float> px(const juce::MouseEvent& e) const { return e.position * pixScale; }
  void mouseDown(const juce::MouseEvent& e) override { auto p = px(e); panel.pointerDown(e.source.getIndex(), p.x, p.y); kick(); }
  void mouseDrag(const juce::MouseEvent& e) override { auto p = px(e); panel.pointerMove(e.source.getIndex(), p.x, p.y); kick(); }
  void mouseUp(const juce::MouseEvent& e) override { panel.pointerUp(e.source.getIndex()); kick(); }
  void mouseDoubleClick(const juce::MouseEvent& e) override { auto p = px(e); panel.pointerDoubleTap(p.x, p.y); kick(); }
  void kick() { if (redraw(false)) repaint(); }
};

juce::AudioProcessorEditor* SynsonicsProcessor::createEditor() { return new SynsonicsEditor(*this); }

}  // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SynsonicsProcessor(); }
