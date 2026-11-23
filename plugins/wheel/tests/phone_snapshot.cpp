// Omega Wheel of Steel: renders the phone-style performance screen (PhoneDeckView) to PNGs, headless (xvfb-run -a).
// First the empty screen at 2000 x 933 (the user's phone screenshot, scaled: no tracks loaded), then two synthetic
// tracks loaded and playing (cues, a loop, pitch, key lock, FX) at 2000 x 933 and 1280 x 600, the SETUP sheet opened
// by a simulated click on SETUP, and a scratch (a finger on platter A) with the hot cue pads in LOOP mode.
// The references are the phone (android/wheel WheelView.java) and the Robolectric shots of LaunchTest.java.
//   wheel_phone_snapshot <out dir>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <cstdio>
#include "../WheelPlugin.h"
#include "../PhoneUI.h"

using namespace juce;

namespace {

// a four-on-the-floor groove: kick, clap on 2 and 4, hats, a bassline and a pad chord in the breakdowns
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
        const double t = i / sr, b = t / beat;
        const double inBeat = (b - std::floor(b)) * beat;
        const int bi = (int) std::floor(b);
        float s = 0;
        if (bi % 32 < 30 || bi < 16)
        {
            const double fq = 50 + 110 * std::exp(-inBeat * 30);
            s += (float) (std::sin(MathConstants<double>::twoPi * fq * inBeat) * std::exp(-inBeat * 7) * .9);
        }
        if (bi % 2 == 1) s += (rng.nextFloat() * 2 - 1) * (float) std::exp(-inBeat * 22) * .45f;
        const double h8 = std::fmod(b * 2, 1.0) * beat * .5;
        if (((int) std::floor(b * 2)) % 2 == 1) s += (rng.nextFloat() * 2 - 1) * (float) std::exp(-h8 * 60) * .22f * tone;
        const double bf = bassNotes[(bi / 4) % 4];
        const double bp = std::fmod(b * 2, 1.0);
        s += (float) (std::sin(MathConstants<double>::twoPi * bf * t) * (bp < .7 ? .35 : .05)) * (bi >= 16 ? 1.0f : .3f);
        if (bi % 64 >= 48)
            for (double m : { 1.0, 1.26, 1.5 }) s += (float) (std::sin(MathConstants<double>::twoPi * 220 * m * t) * .08);
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

struct Rig
{
    wheel::WheelProcessor& proc;
    AudioBuffer<float> block { 4, 512 };
    MidiBuffer mb;
    void run(double seconds)
    {
        const int blocks = (int) (seconds * 48000 / 512);
        for (int i = 0; i < blocks; ++i) { block.clear(); proc.processBlock(block, mb); }
    }
    static void pump(int ms) { MessageManager::getInstance()->runDispatchLoopUntil(ms); }
    void frames(int n) { for (int i = 0; i < n; ++i) { run(1.0 / 60); pump(16); } }
    void send(wheel::Cmd::Type t, int deck, double v = 0, bool on = false, int i = 0)
    {
        wheel::Cmd c; c.type = t; c.deck = deck; c.v = v; c.on = on; c.i = i; proc.send(c); run(.02);
    }
    void set(const String& id, float v)
    {
        if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    }
};

// where the phone layout puts the SETUP button (WheelView.layout, the mixer's PFL A | SETUP | PFL B row)
Point<float> setupButton(float W, float H)
{
    const auto cl = [](float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); };
    const float dp = cl(std::min(W / 882.f, H / 411.f), .6f, 6.f), g = 3 * dp;
    const float ts = dp * cl(std::min(W / (800 * dp), H / (360 * dp)), .85f, 2.2f);
    const float topH = cl(H * .34f, 110 * dp, 330 * dp);
    const float mixW = cl(W * .27f, 196 * dp, 440 * dp), deckW = (W - mixW) / 2 - g;
    const float mixTop = topH + g, mixH = H - mixTop;
    const float mx0 = deckW + 2 * g, mx1 = deckW + g + mixW - g;
    const float statusTop = H - 12 * ts - g;
    const float xfTop = statusTop - cl(mixH * .2f, 44 * dp, 96 * dp);
    const float rowH = cl(mixH * .1f, 22 * dp, 40 * dp), rowY = xfTop - g - rowH;
    return { (mx0 + mx1) / 2, rowY + rowH / 2 };
}
Point<float> platterA(float W, float H)
{
    const auto cl = [](float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); };
    const float dp = cl(std::min(W / 882.f, H / 411.f), .6f, 6.f), g = 3 * dp;
    const float topH = cl(H * .34f, 110 * dp, 330 * dp), botH = cl(H * .29f, 96 * dp, 290 * dp);
    const float midTop = topH + g, midH = H - topH - botH - 2 * g;
    const float mixW = cl(W * .27f, 196 * dp, 440 * dp), deckW = (W - mixW) / 2 - g;
    const float rad = std::min(midH - 2 * g, deckW * .5f) / 2;
    return { g + rad + 2 * dp, midTop + midH / 2 };
}

MouseEvent mouse(Component& c, Point<float> p, bool down)
{
    auto src = Desktop::getInstance().getMainMouseSource();
    const auto now = Time::getCurrentTime();
    return MouseEvent(src, p, down ? ModifierKeys(ModifierKeys::leftButtonModifier) : ModifierKeys(), 1.0f, 0, 0, 0, 0, &c, &c, now, p, now, 1, false);
}

bool shot(Component& c, const File& f)
{
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    PNGImageFormat png;
    std::unique_ptr<OutputStream> os(f.createOutputStream());
    const bool ok = os != nullptr && png.writeImageToStream(img, *os);
    std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const File out = File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "phone-shots");
    out.createDirectory();
    bool ok = true;

    wheel::PhoneHost host;
    host.midiStatus = [] { return String("MIDI: plug in an MPC or controller"); };
    host.selectedTitle = [] { return String(); };
    host.loadSelected = [](int) { return false; };
    host.openLibrary = [] {};
    host.closeLibrary = [] {};

    // ---- the empty screen, as on the phone screenshot (no tracks)
    {
        wheel::WheelProcessor proc;
        proc.setRateAndBufferSizeDetails(48000, 512);
        proc.prepareToPlay(48000, 512);
        Rig rig { proc };
        // the phone starts with echo picked so FX ON does something (Ctl.init); the plugin's default is FX OFF
        for (int d = 0; d < 2; ++d) rig.set(wheel::deckParam(d, "fxtype"), 1);
        wheel::PhoneDeckView view(proc, host);
        view.setSize(2000, 933);
        view.setVisible(true);
        rig.frames(10);
        ok &= shot(view, out.getChildFile("phone-empty-2000x933.png"));
    }

    // ---- two tracks playing
    auto music = File::getSpecialLocation(File::tempDirectory).getChildFile("wheel_phone_snapshot");
    music.createDirectory();
    auto f1 = music.getChildFile("Omega Steel - Wheel Of Steel (Club Mix).wav");
    auto f2 = music.getChildFile("Snake Device - Boom Bap Theory.wav");
    if (!f1.existsAsFile()) writeGroove(f1, 124, 150, 1, 1.0f);
    if (!f2.existsAsFile()) writeGroove(f2, 96, 130, 2, .6f);

    wheel::WheelProcessor proc;
    proc.setRateAndBufferSizeDetails(48000, 512);
    proc.prepareToPlay(48000, 512);
    Rig rig { proc };
    proc.loadTrack(0, f1);
    proc.loadTrack(1, f2);
    for (int i = 0; i < 600 && (proc.loading[0].load() || proc.loading[1].load() || !proc.deckTrack(0) || !proc.deckTrack(1)); ++i) { Rig::pump(50); rig.run(.01); }
    Rig::pump(100); rig.run(.05); Rig::pump(100);
    if (!proc.deckTrack(0) || !proc.deckTrack(1)) std::printf("warning: tracks did not load (%s / %s)\n", proc.loadError[0].toRawUTF8(), proc.loadError[1].toRawUTF8());

    // deck A: cues at a few phrases, then play from bar 17 with +2.1 % pitch, echo on
    const double cues[] = { 7.74, 15.48, 30.97, 46.45, 61.94 };
    for (int i = 0; i < 5; ++i) { rig.send(wheel::Cmd::seek, 0, cues[i]); rig.send(wheel::Cmd::hotCue, 0, 0, true, i); rig.send(wheel::Cmd::hotCue, 0, 0, false, i); }
    rig.send(wheel::Cmd::seek, 0, 31.2);
    rig.set(wheel::deckParam(0, "pitch"), .26f);
    rig.set(wheel::deckParam(0, "fxtype"), 1);
    rig.set(wheel::deckParam(0, "fxon"), 1);
    rig.set(wheel::deckParam(1, "fxtype"), 4);
    rig.set(wheel::deckParam(1, "keylock"), 1);
    rig.set("xfader", -.35f);
    rig.set(wheel::deckParam(1, "fader"), .72f);
    rig.set(wheel::deckParam(0, "low"), -26.f);
    rig.set(wheel::deckParam(1, "filter"), .36f);
    rig.set(wheel::deckParam(1, "pfl"), 1);
    rig.send(wheel::Cmd::playPause, 0);
    // deck B: three cues and a 4-beat loop, playing (it ends 20 s from the end: the time blinks red)
    for (int i = 0; i < 3; ++i) { rig.send(wheel::Cmd::seek, 1, 5.0 + i * 10); rig.send(wheel::Cmd::hotCue, 1, 0, true, i); rig.send(wheel::Cmd::hotCue, 1, 0, false, i); }
    rig.send(wheel::Cmd::seek, 1, 106.0);
    rig.send(wheel::Cmd::playPause, 1);
    rig.run(1.2);
    rig.send(wheel::Cmd::autoLoop, 1, 4);
    rig.run(1.5);
    Rig::pump(100);

    {
        wheel::PhoneDeckView view(proc, host);
        view.setVisible(true);
        for (auto [W, H] : { std::pair<int, int> { 2000, 933 }, std::pair<int, int> { 1280, 600 } })
        {
            view.setSize(W, H);
            rig.frames(30);
            ok &= shot(view, out.getChildFile("phone-" + String(W) + "x" + String(H) + ".png"));
        }
        // the SETUP sheet, opened by a click on SETUP
        view.setSize(2000, 933);
        rig.frames(2);
        const auto sp = setupButton(2000, 933);
        view.mouseDown(mouse(view, sp, true));
        view.mouseUp(mouse(view, sp, false));
        rig.frames(6);
        ok &= shot(view, out.getChildFile("phone-setup-2000x933.png"));
        // close it with Esc, switch the pads to LOOP mode and hold platter A (a scratch: the rim lights)
        view.keyPressed(KeyPress(KeyPress::escapeKey));
        rig.set("padmode", 1);
        const auto pc = platterA(2000, 933);
        const Point<float> p0 = pc + Point<float>(0, -60), p1 = pc + Point<float>(40, -50);
        view.mouseDown(mouse(view, p0, true));
        rig.frames(3);
        view.mouseDrag(mouse(view, p1, true));
        rig.frames(3);
        ok &= shot(view, out.getChildFile("phone-scratch-2000x933.png"));
        view.mouseUp(mouse(view, p1, false));
        rig.frames(2);
    }
    proc.releaseResources();
    return ok ? 0 : 1;
}
