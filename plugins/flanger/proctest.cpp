// Drives the Omega MXR Flanger processor and editor without a host: every preset through audio, mono and stereo
// layouts, state round trip, and pictures of the panel at a few knob settings.
// usage: flanger_proc_test <outdir>
#include "FlangerPlugin.h"
#include "FlangerEditor.h"
#include <cstdio>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::puts("usage: flanger_proc_test outdir"); return 2; }
    const juce::File out(juce::String::fromUTF8(argv[1]));
    out.createDirectory();
    bool ok = true;
    flanger::Processor p;
    const double sr = 48000; const int block = 480;
    auto run = [&](int chans, double secs) {
        const int n = (int) (secs * sr); double rms = 0, diff = 0; bool nan = false;
        for (int pos = 0; pos < n; pos += block) {
            juce::AudioBuffer<float> b(chans, block); juce::MidiBuffer m;
            for (int c = 0; c < chans; ++c) for (int i = 0; i < block; ++i) {
                const double t = (pos + i) / sr, ph = std::fmod(t, .25);
                b.setSample(c, i, (float) (std::sin(6.283185307 * 110 * t) * std::exp(-ph * 6) * .3 + std::sin(6.283185307 * 2310 * t) * .05));
            }
            juce::AudioBuffer<float> dry(b);
            p.processBlock(b, m);
            for (int c = 0; c < chans; ++c) for (int i = 0; i < block; ++i) {
                const float x = b.getSample(c, i); if (!std::isfinite(x)) nan = true;
                rms += x * x; diff += std::abs(x - dry.getSample(c, i));
            }
        }
        rms = std::sqrt(rms / (n * chans));
        return std::make_tuple(rms, diff / (n * chans), nan);
    };
    for (int chans : {2, 1}) {
        juce::AudioProcessor::BusesLayout l;
        const auto set = chans == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono();
        l.inputBuses.add(set); l.outputBuses.add(set);
        if (!p.setBusesLayout(l)) { std::printf("FAIL layout %d\n", chans); ok = false; continue; }
        p.prepareToPlay(sr, block);
        for (int i = 0; i < p.getNumPrograms(); ++i) {
            p.setCurrentProgram(i);
            auto [rms, diff, nan] = run(chans, 1.0);
            const bool good = !nan && rms > 1e-3 && rms < 2 && diff > 1e-3;
            ok &= good;
            if (chans == 2 || !good) std::printf("%s %dch %-24s rms %.4f changed %.4f\n", good ? "ok  " : "FAIL", chans, p.getProgramName(i).toRawUTF8(), rms, diff);
        }
        std::printf("%d-channel layout: all %d presets run\n", chans, p.getNumPrograms());
    }
    // bypass passes the input through untouched once the fade is done
    p.param(m117::bypass)->setValueNotifyingHost(1.f);
    run(1, .5);
    { auto [rms, diff, nan] = run(1, .5); std::printf("%s bypass changed %.6f\n", diff < 1e-5 && !nan ? "ok  " : "FAIL", diff); ok &= diff < 1e-5; }
    p.param(m117::bypass)->setValueNotifyingHost(0.f);
    // state round trip
    p.setCurrentProgram(7);
    juce::MemoryBlock st; p.getStateInformation(st);
    p.setCurrentProgram(0);
    p.setStateInformation(st.getData(), (int) st.getSize());
    const bool stOk = p.getCurrentProgram() == 7 && std::abs(p.param(m117::regen)->convertFrom0to1(p.param(m117::regen)->getValue()) - 9.2f) < 0.01f;
    std::printf("%s state round trip (program %d)\n", stOk ? "ok  " : "FAIL", p.getCurrentProgram()); ok &= stOk;
    // pictures of the panel
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto* e = dynamic_cast<flanger::Editor*>(ed.get());
    struct Shot { const char* name; int program; float m, w, s, r; bool bypass; };
    const Shot shots[] = {
        {"photo", 1, -1, -1, -1, -1, false},      // "As Found (Photo)": the knobs should match the photo
        {"zero", -1, 0, 0, 0, 0, false},
        {"ten", -1, 10, 10, 10, 10, true},
        {"noon", 0, -1, -1, -1, -1, false},
        {"mixed", -1, 2.5f, 7.5f, 5, 10, false},
    };
    for (const auto& sh : shots) {
        if (sh.program >= 0) p.setCurrentProgram(sh.program);
        const float v[4] = {sh.m, sh.w, sh.s, sh.r};
        for (int k = 0; k < 4; ++k) if (v[k] >= 0) p.param(k)->setValueNotifyingHost(p.param(k)->convertTo0to1(v[k]));
        p.param(m117::bypass)->setValueNotifyingHost(sh.bypass ? 1.f : 0.f);
        e->snapshot(out.getChildFile(juce::String("flanger-") + sh.name + ".png"), sh.program < 0 && sh.r == 10 && !sh.bypass ? 3 : -1);
    }
    ed->setSize(480, 414); e->snapshot(out.getChildFile("flanger-1x.png"));
    std::printf("editor snapshots in %s\n", out.getFullPathName().toRawUTF8());
    ed.reset();
    std::puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
