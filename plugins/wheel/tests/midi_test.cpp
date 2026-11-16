// wheel_midi_test: the processor's MIDI out of the box: the MPC's fader (touch strip / pitch bend, or CC 1) is the
// crossfader, the crossfader is the first parameter (hosts that auto-assign knobs take them in order), learning
// another control for it replaces the default, and saved mappings come back as saved.
#include "../WheelPlugin.h"
#include <cstdio>
#include <thread>
using namespace wheel;
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
extern "C" int omega_wheel_screen(uint32_t* pixels, int width, int height, int strideBytes);

static int fails = 0;
static void check(bool ok, const char* what, double got) { std::printf("%s  %-52s %.3f\n", ok ? "PASS" : "FAIL", what, got); if (!ok) ++fails; }
static void block(WheelProcessor& p, juce::MidiBuffer m) { juce::AudioBuffer<float> b(2, 512); p.processBlock(b, m); }
static void send(WheelProcessor& p, const juce::MidiMessage& msg) { juce::MidiBuffer m; m.addEvent(msg, 0); block(p, m); juce::Thread::sleep(80); block(p, {}); }
static float xf(WheelProcessor& p) { return p.apvts.getRawParameterValue("xfader")->load(); }

static void test() {
    std::unique_ptr<WheelProcessor> p((WheelProcessor*) createPluginFilter());
    p->prepareToPlay(48000, 512);
    const auto& params = p->getParameters();
    auto idOf = [&](int i) { auto* r = dynamic_cast<juce::RangedAudioParameter*>(params[i]); return r ? r->getParameterID() : juce::String(); };
    check(idOf(0) == "xfader", "parameter 1 is the crossfader", 0);
    check(idOf(1) == "a_fader" && idOf(2) == "b_fader", "parameters 2, 3 are the channel faders", 0);
    check(idOf(3) == "a_platter" && idOf(4) == "b_platter", "parameters 4, 5 scratch the platters", 0);

    send(*p, juce::MidiMessage::pitchWheel(1, 0));      check(xf(*p) < -.99f, "touch strip / pitch bend down: full A", xf(*p));
    send(*p, juce::MidiMessage::pitchWheel(5, 16383));  check(xf(*p) > .99f, "pitch bend up (any channel): full B", xf(*p));
    send(*p, juce::MidiMessage::pitchWheel(1, 8192));   check(std::abs(xf(*p)) < .01f, "pitch bend centre: centre", xf(*p));
    send(*p, juce::MidiMessage::controllerEvent(1, 1, 0));   check(xf(*p) < -.99f, "CC 1 = 0: full A", xf(*p));
    send(*p, juce::MidiMessage::controllerEvent(3, 1, 127)); check(xf(*p) > .99f, "CC 1 = 127 (channel 3): full B", xf(*p));

    // learn the crossfader on CC 20: CC 1 lets go of it
    p->learn("xfader");
    send(*p, juce::MidiMessage::controllerEvent(1, 20, 0));
    send(*p, juce::MidiMessage::controllerEvent(1, 20, 127)); check(xf(*p) > .99f, "learned CC 20 moves the crossfader", xf(*p));
    send(*p, juce::MidiMessage::controllerEvent(1, 1, 0));    check(xf(*p) > .99f, "CC 1 no longer does", xf(*p));
    send(*p, juce::MidiMessage::pitchWheel(1, 0));            check(xf(*p) < -.99f, "pitch bend still does", xf(*p));

    juce::MemoryBlock mb; p->getStateInformation(mb);
    std::unique_ptr<WheelProcessor> q((WheelProcessor*) createPluginFilter());
    q->prepareToPlay(48000, 512); q->setStateInformation(mb.getData(), (int) mb.getSize());
    send(*q, juce::MidiMessage::controllerEvent(1, 20, 127)); check(xf(*q) > .99f, "restored: CC 20 is the crossfader", xf(*q));
    send(*q, juce::MidiMessage::controllerEvent(1, 1, 0));    check(xf(*q) > .99f, "restored: CC 1 stays unmapped", xf(*q));
    q->clearMappings();
    send(*q, juce::MidiMessage::controllerEvent(1, 1, 0));    check(xf(*q) < -.99f, "clear mappings: CC 1 is the crossfader again", xf(*q));
    q.reset(); p.reset();

    // ---- the MPC Sample's own control surface (Auto recognises it)
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wheel_midi_test_card");
    dir.deleteRecursively(); dir.getChildFile("Crate One").createDirectory();
    juce::File wav = dir.getChildFile("Crate One").getChildFile("Test Artist - Test Beat.wav");
    { juce::WavAudioFormat fmt; auto os = wav.createOutputStream();
      std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.release(), 44100, 2, 16, {}, 0));
      juce::AudioBuffer<float> b(2, 44100 * 12);
      for (int i = 0; i < b.getNumSamples(); ++i) { float v = (i % 22050) < 1500 ? .8f * std::sin((float) i * .08f) : .05f * std::sin((float) i * .03f); b.setSample(0, i, v); b.setSample(1, i, v); }
      w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
    std::unique_ptr<WheelProcessor> s((WheelProcessor*) createPluginFilter());
    s->prepareToPlay(44100, 128);
    s->lastRoot = dir;
    for (int i = 0; i < 100 && s->library.cratesCopy().isEmpty(); ++i) { block(*s, {}); juce::Thread::sleep(20); }
    check(!s->library.cratesCopy().isEmpty(), "SD card root found, crates listed", s->library.cratesCopy().size());
    check(!s->mpcSampleActive(), "not in MPC Sample mode before its MIDI", 0);
    send(*s, juce::MidiMessage::controllerEvent(1, 100, 1));
    check(s->mpcSampleActive(), "encoder (CC 100 ch 1) switches to MPC Sample mode", 1);
    send(*s, juce::MidiMessage::controllerEvent(1, 20, 0));    check(xf(*s) < -.99f, "MPC Sample fader (CC 20) down: crossfader A", xf(*s));
    send(*s, juce::MidiMessage::controllerEvent(1, 20, 127));  check(xf(*s) > .99f, "MPC Sample fader up: crossfader B", xf(*s));
    // browse with the hardware: SAMPLE, encoder, press, press (loads on the focused deck A)
    send(*s, juce::MidiMessage::noteOn(1, 6, (juce::uint8) 127)); send(*s, juce::MidiMessage::noteOff(1, 6));
    check(s->hwBrowser().mode == 1, "SAMPLE opens the crate browser", s->hwBrowser().mode);
    int crate = -1; auto hb = s->hwBrowser();
    for (int i = 0; i < hb.rows.size(); ++i) if (hb.rows[i].contains("Crate One")) crate = i;
    for (int i = 0; i < crate; ++i) send(*s, juce::MidiMessage::controllerEvent(1, 100, 1));
    check(s->hwBrowser().cursor == crate && crate >= 0, "encoder moves to the crate", s->hwBrowser().cursor);
    send(*s, juce::MidiMessage::noteOn(1, 111, (juce::uint8) 127));
    for (int i = 0; i < 100 && s->hwBrowser().busy; ++i) { block(*s, {}); juce::Thread::sleep(20); }
    check(s->hwBrowser().mode == 2 && s->hwBrowser().rows.size() == 1, "encoder press opens the crate", s->hwBrowser().rows.size());
    send(*s, juce::MidiMessage::noteOn(1, 111, (juce::uint8) 127));
    for (int i = 0; i < 300 && (s->loading[0].load() || !s->deckTrack(0)); ++i) { block(*s, {}); juce::Thread::sleep(20); }
    check(s->deckTrack(0) != nullptr && s->hwBrowser().mode == 0, "second press loads it on deck A and closes", s->deckTrack(0) ? 1 : 0);
    // PLAY plays the focused deck; B2 moves focus to B
    send(*s, juce::MidiMessage::noteOn(1, 82, (juce::uint8) 127)); send(*s, juce::MidiMessage::noteOff(1, 82));
    for (int i = 0; i < 40; ++i) block(*s, {});
    check(s->status(0).playing.load(), "PLAY starts deck A", s->status(0).pos.load());
    send(*s, juce::MidiMessage::noteOn(1, 13, (juce::uint8) 127));
    check(s->focusDeck() == 1, "B2 focuses deck B", s->focusDeck());
    send(*s, juce::MidiMessage::noteOn(1, 12, (juce::uint8) 127));
    // K1 = focused deck's filter; SHIFT + K1 = its high EQ
    send(*s, juce::MidiMessage::controllerEvent(1, 16, 0));
    check(s->apvts.getRawParameterValue("a_filter")->load() < -.99f, "K1 turns deck A's filter", s->apvts.getRawParameterValue("a_filter")->load());
    send(*s, juce::MidiMessage::noteOn(1, 49, (juce::uint8) 127));
    send(*s, juce::MidiMessage::controllerEvent(1, 16, 0));
    check(s->apvts.getRawParameterValue("a_hi")->load() < -25.f, "SHIFT + K1 kills deck A's highs", s->apvts.getRawParameterValue("a_hi")->load());
    send(*s, juce::MidiMessage::noteOff(1, 49));
    // pads: MPC Sample pad 2 (note 36, channel 10) = deck A hot cue 2; the SHIFT button (note 49 ch 1) never hits a pad
    send(*s, juce::MidiMessage::noteOn(10, 36, (juce::uint8) 100)); send(*s, juce::MidiMessage::noteOff(10, 36));
    check(s->cuesOf(0)[1].set(), "pad 2 sets deck A hot cue 2", s->cuesOf(0)[1].t);
    check(!s->cuesOf(0)[6].set(), "SHIFT didn't set a cue (pad 13 slot)", 0);
    // the encoder is the focused deck's platter: turning it puts a hand on the record
    { juce::MidiBuffer m; m.addEvent(juce::MidiMessage::controllerEvent(1, 100, 127), 0); block(*s, m); }
    check(s->status(0).touched.load(), "encoder turn scratches deck A", s->status(0).pos.load());
    // the screen for an OS that draws it: XRGB8888
    std::vector<uint32_t> px(320 * 240, 0);
    int okScreen = 0;
    juce::MessageManager::getInstance()->callFunctionOnMessageThread([](void* a) -> void* {
        auto* v = static_cast<std::vector<uint32_t>*>(a); return (void*) (intptr_t) omega_wheel_screen(v->data(), 320, 240, 320 * 4); }, &px) ? okScreen = 1 : okScreen = 0;
    int lit = 0; for (auto v : px) if ((v & 0xffffff) != 0) ++lit;
    check(okScreen == 1 && lit > 3000, "omega_wheel_screen draws the 320 x 240 screen", lit);
    if (auto out = juce::File::getCurrentWorkingDirectory().getChildFile("wheel_screen_export.png"); true) {
        juce::Image img(juce::Image::RGB, 320, 240, true);
        for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) img.setPixelAt(x, y, juce::Colour(px[(size_t) (y * 320 + x)]));
        out.deleteFile(); if (auto os = out.createOutputStream()) juce::PNGImageFormat().writeImageToStream(img, *os);
    }
    s.reset();
    dir.deleteRecursively();
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    std::thread t([] { test(); juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    t.join();
    std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
