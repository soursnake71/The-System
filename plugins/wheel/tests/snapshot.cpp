// Omega Wheel of Steel: renders the editor to a PNG, deterministic and headless-friendly (run under xvfb-run on a
// machine without a display). Makes two synthetic tracks and a little library, loads them on the decks, sets cues,
// a loop and some pitch, runs the engine for a few seconds, then snapshots the editor.
//   wheel_snapshot <out.png> [width height] [--midi]
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include "../WheelPlugin.h"
#include "../WheelEditor.h"

using namespace juce;

namespace {

// a simple four-on-the-floor groove: kick, snare/clap on 2 and 4, hats, a bassline and a pad chord
void writeGroove(const File& f, double bpm, double seconds, int seed, float tone)
{
    const double sr = 44100;
    const int n = (int) (seconds * sr);
    AudioBuffer<float> buf(2, n);
    buf.clear();
    Random rng(seed);
    const double beat = 60.0 / bpm;
    const double bassNotes[] = { 55.0, 55.0, 65.41, 49.0 };
    for (int i = 0; i < n; ++i)
    {
        double t = i / sr, b = t / beat;
        double inBeat = (b - std::floor(b)) * beat;
        int bi = (int) std::floor(b);
        float s = 0;
        // kick
        if (bi % 32 < 30 || bi < 16)
        {
            double fq = 50 + 110 * std::exp(-inBeat * 30);
            s += (float) (std::sin(MathConstants<double>::twoPi * fq * inBeat) * std::exp(-inBeat * 7) * .9);
        }
        // clap on 2 and 4
        if (bi % 2 == 1) s += (rng.nextFloat() * 2 - 1) * (float) std::exp(-inBeat * 22) * .45f;
        // hats on the off-beat 8ths
        double h8 = std::fmod(b * 2, 1.0) * beat * .5;
        if (((int) std::floor(b * 2)) % 2 == 1) s += (rng.nextFloat() * 2 - 1) * (float) std::exp(-h8 * 60) * .22f * tone;
        // bass
        int bar = bi / 4;
        double bf = bassNotes[bar % 4];
        double bp = std::fmod(b * 2, 1.0);
        s += (float) (std::sin(MathConstants<double>::twoPi * bf * t) * (bp < .7 ? .35 : .05)) * (bi >= 16 ? 1.0f : .3f);
        // pad (breakdown every 64 beats)
        if (bi % 64 >= 48)
        {
            for (double m : { 1.0, 1.26, 1.5 }) s += (float) (std::sin(MathConstants<double>::twoPi * 220 * m * t) * .08);
        }
        buf.setSample(0, i, s * .6f);
        buf.setSample(1, i, s * .6f);
    }
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os(f.createOutputStream());
    if (auto* w = wav.createWriterFor(os.get(), sr, 2, 16, {}, 0))
    {
        os.release();
        std::unique_ptr<AudioFormatWriter> writer(w);
        writer->writeFromAudioSampleBuffer(buf, 0, n);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    File out = File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "screenshot.png");
    int W = argc > 3 ? atoi(argv[2]) : 1400, H = argc > 3 ? atoi(argv[3]) : 900;
    bool midi = false;
    for (int i = 1; i < argc; ++i) if (String(argv[i]) == "--midi") midi = true;

    auto root = File::getSpecialLocation(File::tempDirectory).getChildFile("wheel_snapshot_lib");
    auto music = root.getChildFile("House");
    music.createDirectory();
    root.getChildFile("Hip Hop").createDirectory();
    auto f1 = music.getChildFile("Omega Steel - Wheel Of Steel (Club Mix).wav");
    auto f2 = root.getChildFile("Hip Hop").getChildFile("Snake Device - Boom Bap Theory.wav");
    if (!f1.existsAsFile()) writeGroove(f1, 124, 150, 1, 1.0f);
    if (!f2.existsAsFile()) writeGroove(f2, 96, 130, 2, .6f);
    auto f3 = music.getChildFile("Night Shift - Deep Cut.wav");
    if (!f3.existsAsFile()) writeGroove(f3, 122, 40, 3, .8f);

    wheel::WheelProcessor proc;
    proc.setRateAndBufferSizeDetails(48000, 512);
    proc.prepareToPlay(48000, 512);
    AudioBuffer<float> block(4, 512);
    MidiBuffer mb;
    auto run = [&](double seconds) {
        int blocks = (int) (seconds * 48000 / 512);
        for (int i = 0; i < blocks; ++i) { block.clear(); proc.processBlock(block, mb); }
    };
    auto pump = [&](int ms) { MessageManager::getInstance()->runDispatchLoopUntil(ms); };
    auto send = [&](wheel::Cmd::Type t, int deck, double v = 0, bool on = false, int i = 0) {
        wheel::Cmd c; c.type = t; c.deck = deck; c.v = v; c.on = on; c.i = i; proc.send(c); run(.02);
    };

    proc.lastRoot = root;
    proc.library.setRoot(root);
    proc.loadTrack(0, f1);
    proc.loadTrack(1, f2);
    for (int i = 0; i < 600 && (proc.loading[0].load() || proc.loading[1].load() || !proc.deckTrack(0) || !proc.deckTrack(1)); ++i) { pump(50); run(.01); }
    pump(100); run(.05); pump(100);
    if (!proc.deckTrack(0) || !proc.deckTrack(1)) std::printf("warning: tracks did not load (%s / %s)\n", proc.loadError[0].toRawUTF8(), proc.loadError[1].toRawUTF8());

    // deck A: cues at a few phrases, then play from bar 17 with +2.1 % pitch
    double cues[] = { 7.74, 15.48, 30.97, 46.45, 61.94 };
    for (int i = 0; i < 5; ++i) { send(wheel::Cmd::seek, 0, cues[i]); send(wheel::Cmd::hotCue, 0, 0, true, i); send(wheel::Cmd::hotCue, 0, 0, false, i); }
    send(wheel::Cmd::seek, 0, 31.2);
    if (auto* p = proc.apvts.getParameter(wheel::deckParam(0, "pitch"))) p->setValueNotifyingHost(p->convertTo0to1(.26f));
    if (auto* p = proc.apvts.getParameter(wheel::deckParam(1, "keylock"))) p->setValueNotifyingHost(1);
    if (auto* p = proc.apvts.getParameter(wheel::deckParam(0, "fxtype"))) p->setValueNotifyingHost(p->convertTo0to1(1));
    if (auto* p = proc.apvts.getParameter("xfader")) p->setValueNotifyingHost(p->convertTo0to1(-.35f));
    if (auto* p = proc.apvts.getParameter(wheel::deckParam(1, "fader"))) p->setValueNotifyingHost(.72f);
    if (auto* p = proc.apvts.getParameter(wheel::deckParam(0, "low"))) p->setValueNotifyingHost(.3f);
    if (auto* p = proc.apvts.getParameter(wheel::deckParam(1, "filter"))) p->setValueNotifyingHost(.68f);
    send(wheel::Cmd::playPause, 0);
    // deck B: two cues and a 4-beat loop, playing
    for (int i = 0; i < 3; ++i) { send(wheel::Cmd::seek, 1, 5.0 + i * 10); send(wheel::Cmd::hotCue, 1, 0, true, i); send(wheel::Cmd::hotCue, 1, 0, false, i); }
    send(wheel::Cmd::seek, 1, 40.0);
    send(wheel::Cmd::playPause, 1);
    run(1.2);
    send(wheel::Cmd::autoLoop, 1, 4);
    run(2.0);
    pump(100);

    std::unique_ptr<AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(W, H);
    // let the editor's timers run with the engine going (platters, meters, library listing)
    for (int i = 0; i < 40; ++i) { run(1.0 / 30); pump(33); }
    if (midi)
    {
        // open the MIDI / MPC panel through its button
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<TextButton*>(c); b != nullptr && b->getButtonText() == "MIDI / MPC") b->triggerClick();
        pump(200);
    }
    for (int i = 0; i < 6; ++i) { run(1.0 / 30); pump(33); }

    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    PNGImageFormat png;
    std::unique_ptr<OutputStream> os(out.createOutputStream());
    bool ok = os != nullptr && png.writeImageToStream(img, *os);
    std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    proc.releaseResources();
    return ok ? 0 : 1;
}
