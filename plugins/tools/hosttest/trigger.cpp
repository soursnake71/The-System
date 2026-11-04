// Loads the built Omega Drum Trigger .vst3 like a DAW, plays synthesized metal drum tracks through it (fixed and
// random block sizes, as hosts do) and checks the MIDI that comes out of the VST3: every hit, nothing else, at the
// right sample once the reported latency is taken off (what the DAW's delay compensation does).
// usage: trigger_host_test <OmegaTrigger.vst3> <outdir>      (saves a picture of the panel in outdir)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#define TRIGGER_TEST_NO_MAIN
#include "../../trigger/test.cpp"

static void setParam(juce::AudioPluginInstance& p, const juce::String& name, float value) {
    for (auto* q : p.getParameters())
        if (q->getName(64) == name) {
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(q)) { r->setValueNotifyingHost(r->convertTo0to1(value)); return; }
            // a hosted VST3 parameter: find the normalized value that reads back as this
            float lo = 0, hi = 1;
            for (int i = 0; i < 40; ++i) {
                const float mid = (lo + hi) / 2;
                q->setValue(mid);
                const auto t = q->getText(mid, 32);
                if (t.getFloatValue() * (t.contains("kHz") ? 1000.f : 1.f) < value) lo = mid; else hi = mid;
            }
            q->setValueNotifyingHost((lo + hi) / 2);
            return;
        }
    std::printf("  (no parameter %s)\n", name.toRawUTF8());
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::puts("usage: trigger_host_test OmegaTrigger.vst3 outdir"); return 2; }
    const juce::File file(juce::String::fromUTF8(argv[1])), out(juce::String::fromUTF8(argv[2]));
    juce::AudioPluginFormatManager fm; fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3; vst3.findAllTypesForFile(found, file.getFullPathName());
    if (found.isEmpty()) { std::printf("FAIL no plugin in %s\n", argv[1]); return 1; }
    bool ok = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 }) {
        for (int scen = 0; scen < 3; ++scen) {
            Scenario sc = scen == 0 ? doubleKick(sr, 77, 250, 16, -6, "double kick 16ths 250 bpm")
                        : scen == 1 ? doubleKick(sr, 78, 240, 32, -8, "double kick 32nds 240 bpm")
                                    : blast(sr, 79, 280, false, "blast 280 bpm, kick mic");
            juce::String err;
            auto inst = fm.createPluginInstance(*found[0], sr, 512, err);
            if (!inst) { std::printf("FAIL load: %s\n", err.toRawUTF8()); return 1; }
            inst->enableAllBuses();
            inst->prepareToPlay(sr, 2048);
            const Settings s = presetSettings(Preset::kick);
            setParam(*inst, "Threshold", -20); setParam(*inst, "Sensitivity", s.sensitivityDb); setParam(*inst, "Retrigger", s.retriggerMs);
            setParam(*inst, "High-pass", s.hpHz); setParam(*inst, "Low-pass", s.lpHz);
            if (sr == 44100.0 && scen == 0) { std::printf("  params:"); for (auto* q : inst->getParameters()) std::printf(" %s=%s |", q->getName(32).toRawUTF8(), q->getCurrentValueAsText().toRawUTF8()); std::printf("\n"); }
            const int lat = inst->getLatencySamples();
            const int ch = juce::jmax(inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels());
            std::mt19937 rng(5); std::uniform_int_distribution<int> bs(32, 2048);
            std::vector<Hit> hits; std::vector<int> vels;
            const int64_t N = (int64_t) sc.trk.x.size() + lat + 4096;
            for (int64_t pos = 0; pos < N;) {
                const int len = (int) std::min<int64_t>(sr == 48000.0 ? 512 : bs(rng), N - pos);
                juce::AudioBuffer<float> b(ch, len); b.clear(); juce::MidiBuffer midi;
                for (int c = 0; c < juce::jmin(2, inst->getTotalNumInputChannels()); ++c)
                    for (int i = 0; i < len; ++i) b.setSample(c, i, pos + i < (int64_t) sc.trk.x.size() ? sc.trk.x[(size_t) (pos + i)] : 0.f);
                inst->processBlock(b, midi);
                for (const auto m : midi) {
                    const auto msg = m.getMessage();
                    if (msg.isNoteOn()) { Hit h; h.onset = pos + m.samplePosition - lat; h.peak = 1; h.level = 1; hits.push_back(h); vels.push_back(msg.getVelocity()); }
                }
                pos += len;
            }
            Result r = check(sc, hits, sr, 1.0);
            int vlo = 127, vhi = 0; for (int v : vels) { vlo = std::min(vlo, v); vhi = std::max(vhi, v); }
            const bool pass = r.missed == 0 && r.extra == 0;
            ok &= pass;
            std::printf("  %-4s %5.0f Hz  %-28s latency %d  notes %3zu/%3zu  missed %d extra %d  timing max %.3f ms  velocity %d-%d\n",
                        pass ? "ok" : "FAIL", sr, sc.name.c_str(), lat, hits.size(), sc.trk.truth.size(), r.missed, r.extra, r.maxErrMs, vlo, vhi);
            if (sr == 48000.0 && scen == 0) {
                juce::MemoryBlock st; inst->getStateInformation(st); inst->setStateInformation(st.getData(), (int) st.getSize());
                std::printf("  state %d bytes restored\n", (int) st.getSize());
                if (auto* ed = inst->createEditorIfNeeded()) {
                    ed->setVisible(true);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);   // let the panel pull the display
                    const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
                    out.createDirectory();
                    juce::FileOutputStream os(out.getChildFile("trigger-panel.png"));
                    if (os.openedOk()) { os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os); }
                    std::printf("  editor %dx%d -> %s\n", ed->getWidth(), ed->getHeight(), out.getChildFile("trigger-panel.png").getFullPathName().toRawUTF8());
                    delete ed;
                }
            }
            inst->releaseResources();
        }
    }
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
