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
// one block; returns what the processor sent to the MIDI out
static juce::MidiBuffer out(WheelProcessor& p, const juce::MidiBuffer& in = {}) { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m(in); p.processBlock(b, m); return m; }
// blocks for ~100 ms (past the LED rate limit), collecting the MIDI out
static juce::MidiBuffer outFor(WheelProcessor& p, const juce::MidiBuffer& in = {}, int blocks = 10) {
    juce::MidiBuffer all = out(p, in);
    for (int i = 1; i < blocks; ++i) all.addEvents(out(p), 0, -1, 0);
    return all;
}
static int ledCC(const juce::MidiBuffer& m, int note) {   // the last button LED value for that button, -1 none
    int v = -1;
    for (const auto meta : m) { const auto x = meta.getMessage(); if (x.isController() && x.getChannel() == 1 && x.getControllerNumber() == note) v = x.getControllerValue(); }
    return v;
}
static int sysexCount(const juce::MidiBuffer& m, int pad = -1, int* rgb = nullptr) {   // pad colour SysEx (for a pad, or all)
    int n = 0;
    for (const auto meta : m) {
        const auto x = meta.getMessage();
        if (!x.isSysEx() || x.getRawDataSize() != 12) continue;
        const auto* d = x.getRawData();
        if (d[1] != 0x47 || d[2] != 0x7F || d[3] != 0x5C || d[4] != 0x65 || d[5] != 0 || d[6] != 4 || d[11] != 0xF7) continue;
        if (pad >= 0 && d[7] != pad) continue;
        ++n;
        if (rgb) { rgb[0] = d[8]; rgb[1] = d[9]; rgb[2] = d[10]; }
    }
    return n;
}
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
    check(s->producesMidi(), "the plugin has a MIDI out", 1);
    { auto o = outFor(*s); check(o.isEmpty(), "no LED messages outside MPC Sample mode", o.getNumEvents()); }
    {   // the first MPC Sample message: every LED goes out once
        juce::MidiBuffer in; in.addEvent(juce::MidiMessage::controllerEvent(1, 100, 1), 0);
        auto o = outFor(*s, in);
        check(s->mpcSampleActive(), "encoder (CC 100 ch 1) switches to MPC Sample mode", 1);
        check(sysexCount(o) == 16, "entering the mode sends all 16 pad colours (SysEx)", sysexCount(o));
        check(ledCC(o, 82) == 1 && ledCC(o, 12) == 127 && ledCC(o, 13) == 1 && ledCC(o, 49) == 1,
              "and the buttons: PLAY dim, B1 lit (focus A), B2 dim, SHIFT dim", ledCC(o, 12));
        int n = 0; for (const auto m : o) { juce::ignoreUnused(m); ++n; }
        check(n == 16 + 10, "exactly one message per LED", n);
        auto again = outFor(*s);
        check(again.isEmpty(), "nothing changes: nothing is sent again", again.getNumEvents());
    }
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
    outFor(*s);   // flush the LEDs the load changed
    {
        juce::MidiBuffer in; in.addEvent(juce::MidiMessage::noteOn(1, 82, (juce::uint8) 127), 0); in.addEvent(juce::MidiMessage::noteOff(1, 82), 1);
        auto o = outFor(*s, in);
        check(s->status(0).playing.load(), "PLAY starts deck A", s->status(0).pos.load());
        check(ledCC(o, 82) == 127, "PLAY's LED lights (B0 82 127)", ledCC(o, 82));
        int plays = 0; for (const auto m : o) if (m.getMessage().isController() && m.getMessage().getControllerNumber() == 82) ++plays;
        check(plays == 1, "sent once, not every block", plays);
    }
    {
        juce::MidiBuffer in; in.addEvent(juce::MidiMessage::noteOn(1, 13, (juce::uint8) 127), 0);
        auto o = outFor(*s, in);
        check(s->focusDeck() == 1, "B2 focuses deck B", s->focusDeck());
        check(ledCC(o, 13) == 127 && ledCC(o, 12) == 1 && ledCC(o, 82) == 1, "focus B: B2 lit, B1 dim, PLAY dim (deck B stopped)", ledCC(o, 13));
        check(sysexCount(o) == 0, "focus change: no pad SysEx (pads didn't change)", sysexCount(o));
    }
    {
        juce::MidiBuffer in; in.addEvent(juce::MidiMessage::noteOn(1, 12, (juce::uint8) 127), 0);
        auto o = outFor(*s, in);
        check(ledCC(o, 12) == 127 && ledCC(o, 82) == 127, "back to A: B1 lit, PLAY lit again", ledCC(o, 82));
    }
    // K1 = focused deck's filter; SHIFT + K1 = its high EQ
    send(*s, juce::MidiMessage::controllerEvent(1, 16, 0));
    check(s->apvts.getRawParameterValue("a_filter")->load() < -.99f, "K1 turns deck A's filter", s->apvts.getRawParameterValue("a_filter")->load());
    send(*s, juce::MidiMessage::noteOn(1, 49, (juce::uint8) 127));
    send(*s, juce::MidiMessage::controllerEvent(1, 16, 0));
    check(s->apvts.getRawParameterValue("a_hi")->load() < -25.f, "SHIFT + K1 kills deck A's highs", s->apvts.getRawParameterValue("a_hi")->load());
    send(*s, juce::MidiMessage::noteOff(1, 49));
    // pads: MPC Sample pad 2 (note 36, channel 10) = deck A hot cue 2; the SHIFT button (note 49 ch 1) never hits a pad
    {
        juce::MidiBuffer in; in.addEvent(juce::MidiMessage::noteOn(10, 36, (juce::uint8) 100), 0);
        auto o = outFor(*s, in);
        int rgb[3] {};
        check(sysexCount(o, 1, rgb) == 1 && rgb[0] == 127 && rgb[1] == 127 && rgb[2] == 127, "pad 2 held: its LED is white", rgb[0]);
        check(sysexCount(o) == 1, "only that pad's colour is sent", sysexCount(o));
        juce::MidiBuffer up; up.addEvent(juce::MidiMessage::noteOff(10, 36), 0);
        o = outFor(*s, up);
        const auto cue = s->cuesOf(0)[1];
        const int want[3] = { (int) std::lround(((cue.rgb >> 16) & 255) * 127. / 255), (int) std::lround(((cue.rgb >> 8) & 255) * 127. / 255), (int) std::lround((cue.rgb & 255) * 127. / 255) };
        check(sysexCount(o, 1, rgb) == 1 && rgb[0] == want[0] && rgb[1] == want[1] && rgb[2] == want[2], "released: pad 2 shows hot cue 2's colour (SysEx)", rgb[0]);
        int other[3] {};
        auto o2 = outFor(*s);
        check(sysexCount(o2) == 0 && sysexCount(o, 0, other) == 0, "no duplicates, no other pads", sysexCount(o2));
    }
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
    for (int i = 0; i < 20; ++i) block(*s, {});   // let go of the record
    // ---- the project keeps the loop, sync, the focused deck and the browser's crate
    {
        send(*s, juce::MidiMessage::noteOn(1, 74, (juce::uint8) 127)); send(*s, juce::MidiMessage::noteOff(1, 74));   // LOOP: 4 beats on A
        send(*s, juce::MidiMessage::noteOn(1, 14, (juce::uint8) 127)); send(*s, juce::MidiMessage::noteOff(1, 14));   // B3: sync A
        send(*s, juce::MidiMessage::noteOn(1, 13, (juce::uint8) 127));                                                 // B2: focus B
        for (int i = 0; i < 4; ++i) block(*s, {});
        const double la = s->status(0).loopA.load(), lb = s->status(0).loopB.load();
        check(s->status(0).looping.load() && lb > la && s->status(0).synced.load(), "deck A looping and synced before saving", lb - la);
        juce::MemoryBlock st; s->getStateInformation(st);
        std::unique_ptr<WheelProcessor> r((WheelProcessor*) createPluginFilter());
        r->prepareToPlay(44100, 128);
        r->setStateInformation(st.getData(), (int) st.getSize());
        for (int i = 0; i < 300 && (r->loading[0].load() || !r->deckTrack(0)); ++i) { block(*r, {}); juce::Thread::sleep(20); }
        for (int i = 0; i < 10; ++i) block(*r, {});
        check(r->focusDeck() == 1, "restored: deck B has the focus", r->focusDeck());
        check(r->status(0).looping.load() && std::abs(r->status(0).loopA.load() - la) < 1e-3 && std::abs(r->status(0).loopB.load() - lb) < 1e-3,
              "restored: deck A's loop (region and on)", r->status(0).loopB.load() - r->status(0).loopA.load());
        check(r->status(0).synced.load(), "restored: deck A's sync", 0);
        for (int i = 0; i < 100 && r->library.cratesCopy().isEmpty(); ++i) { block(*r, {}); juce::Thread::sleep(20); }
        r->hwBrowse(WheelProcessor::browseToggle, 0);
        juce::Thread::sleep(100);
        const auto hb2 = r->hwBrowser();
        check(hb2.mode == 1 && hb2.cursor == crate, "restored: the hardware browser opens on the last crate", hb2.cursor);
        r.reset();
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
