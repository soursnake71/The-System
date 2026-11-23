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

// ---- the Numark MixTrack Go (controllers/MixtrackGo.md)
static juce::MidiMessage on(int ch, int note) { return juce::MidiMessage::noteOn(ch, note, (juce::uint8) 127); }
static juce::MidiMessage offN(int ch, int note) { return juce::MidiMessage::noteOff(ch, note); }
static juce::MidiMessage cc(int ch, int n, int v) { return juce::MidiMessage::controllerEvent(ch, n, v); }
static void press(WheelProcessor& p, int ch, int note) { send(p, on(ch, note)); send(p, offN(ch, note)); }
static int ledNote(const juce::MidiBuffer& m, int ch, int note) {   // last LED velocity sent for (ch, note), -1 none
    int v = -1;
    for (const auto meta : m) { const auto x = meta.getMessage(); if (x.isNoteOn() && x.getChannel() == ch && x.getNoteNumber() == note) v = x.getVelocity(); }
    return v;
}
static float prm(WheelProcessor& p, const char* id) { return p.apvts.getRawParameterValue(id)->load(); }
static void blocks(WheelProcessor& p, int n, const juce::MidiMessage* each = nullptr) {
    for (int i = 0; i < n; ++i) { juce::MidiBuffer m; if (each) m.addEvent(*each, 0); block(p, m); }
}

static void testMixtrack() {
    // no false detection: MPC software pads (notes 36-55 on the track channel), Q-Links, the MPC Sample's surface
    {
        std::unique_ptr<WheelProcessor> q((WheelProcessor*) createPluginFilter());
        q->prepareToPlay(44100, 128);
        for (int ch : { 1, 2, 5, 6, 16 }) for (int n = 36; n < 56; ++n) { send(*q, on(ch, n)); send(*q, offN(ch, n)); }
        for (int ch : { 1, 5, 16 }) for (int c : { 1, 7, 16, 17, 18, 19, 20, 74 }) send(*q, cc(ch, c, 90));
        check(!q->mixtrackActive() && !q->mpcSampleActive(), "MPC pads / Q-Links: no controller detected", 0);
        check(q->controllerName() == "MPC / MIDI", "controllerName: MPC / MIDI", 0);
        send(*q, on(10, 37)); send(*q, offN(10, 37));
        for (int n : { 0, 1, 12, 13, 49, 82 }) press(*q, 1, n);
        for (int c : { 16, 17, 18, 19, 20, 100 }) send(*q, cc(1, c, 1));
        check(q->mpcSampleActive() && !q->mixtrackActive(), "MPC Sample traffic: MPC Sample, not MixTrack", 0);
        check(q->controllerName() == "MPC Sample", "controllerName: MPC Sample", 0);
        // and the MixTrack's deck-channel notes / jog alone don't switch away (MPC Sample uses ch 1 notes)
        press(*q, 1, 0); send(*q, cc(1, 6, 1));
        check(q->mpcSampleActive(), "MixTrack ch 1 notes / jog alone don't steal MPC Sample mode", 0);
    }

    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wheel_midi_test_mixtrack");
    dir.deleteRecursively(); dir.getChildFile("Crate One").createDirectory();
    juce::File wav = dir.getChildFile("Crate One").getChildFile("Test Artist - Test Beat.wav");
    { juce::WavAudioFormat fmt; auto os = wav.createOutputStream();
      std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.release(), 44100, 2, 16, {}, 0));
      juce::AudioBuffer<float> b(2, 44100 * 30);
      for (int i = 0; i < b.getNumSamples(); ++i) { float v = (i % 22050) < 1500 ? .8f * std::sin((float) i * .08f) : .05f * std::sin((float) i * .03f); b.setSample(0, i, v); b.setSample(1, i, v); }
      w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
    std::unique_ptr<WheelProcessor> p((WheelProcessor*) createPluginFilter());
    auto& P = *p;
    P.prepareToPlay(44100, 128);
    P.lastRoot = dir;
    for (int i = 0; i < 100 && P.library.cratesCopy().isEmpty(); ++i) { block(P, {}); juce::Thread::sleep(20); }
    check(!P.mixtrackActive(), "MixTrack: not active before its MIDI", 0);
    {   // detection by the crossfader (CC 8 ch 16), every LED sent once
        juce::MidiBuffer in; in.addEvent(cc(16, 8, 0), 0);
        auto o = outFor(P, in);
        check(P.mixtrackActive() && P.controllerName() == "Numark MixTrack Go", "crossfader CC 8 ch 16 detects the MixTrack Go", 1);
        juce::Thread::sleep(80); block(P, {});
        check(xf(P) < -.99f, "crossfader 0: full A", xf(P));
        check(o.getNumEvents() == 26, "entering the mode sends all 26 LEDs", o.getNumEvents());
        check(ledNote(o, 1, 80) == 127 && ledNote(o, 2, 80) == 127, "VINYL lit on both decks (default on)", ledNote(o, 1, 80));
        check(ledNote(o, 1, 0) == 1 && ledNote(o, 2, 2) == 1 && ledNote(o, 1, 27) == 1, "PLAY / SYNC / PFL dim", ledNote(o, 1, 0));
        check(ledNote(o, 5, 1) == 127 && ledNote(o, 5, 2) == 1 && ledNote(o, 6, 1) == 127, "pad mode LED: hot cue lit", ledNote(o, 5, 1));
        check(ledNote(o, 5, 20) == 1 && ledNote(o, 6, 23) == 1, "pads dim (no cues)", ledNote(o, 5, 20));
        auto again = outFor(P);
        check(again.isEmpty(), "nothing changes: no LEDs sent", again.getNumEvents());
    }
    send(P, cc(16, 8, 127)); check(xf(P) > .99f, "crossfader 127: full B", xf(P));
    send(P, cc(16, 8, 64));  check(std::abs(xf(P)) < .01f, "crossfader 64: centre", xf(P));
    {   // with a pitch-pair detection too (fresh processor)
        std::unique_ptr<WheelProcessor> q((WheelProcessor*) createPluginFilter());
        q->prepareToPlay(44100, 128);
        send(*q, cc(2, 9, 0)); send(*q, cc(2, 41, 0));
        check(q->mixtrackActive(), "pitch fader CC 9 + CC 41 detects it", 0);
        check(prm(*q, "b_pitch") < -.99f, "and that move reaches deck B's pitch", prm(*q, "b_pitch"));
        std::unique_ptr<WheelProcessor> r((WheelProcessor*) createPluginFilter());
        r->prepareToPlay(44100, 128);
        press(*r, 5, 20);
        check(r->mixtrackActive(), "a pad (note 20 ch 5) detects it", 0);
    }

    // browse + load: the first turn opens the browser, turns move, push opens the crate, LOAD A / B load
    send(P, cc(16, 0, 1));
    check(P.hwBrowser().mode == 1, "browse knob: first turn opens the crate browser", P.hwBrowser().mode);
    int crate = -1;
    { auto hb = P.hwBrowser(); for (int i = 0; i < hb.rows.size(); ++i) if (hb.rows[i].contains("Crate One")) crate = i; }
    send(P, cc(16, 0, 127 - 20)); check(P.hwBrowser().cursor == 0, "browse back past the top: first row", P.hwBrowser().cursor);
    for (int i = 0; i < crate; ++i) send(P, cc(16, 0, 1));
    check(crate >= 0 && P.hwBrowser().cursor == crate, "browse knob moves to the crate", P.hwBrowser().cursor);
    press(P, 16, 7);
    for (int i = 0; i < 100 && P.hwBrowser().busy; ++i) { block(P, {}); juce::Thread::sleep(20); }
    check(P.hwBrowser().mode == 2 && P.hwBrowser().rows.size() == 1, "browse push opens the crate", P.hwBrowser().rows.size());
    press(P, 16, 2);
    for (int i = 0; i < 300 && (P.loading[0].load() || !P.deckTrack(0)); ++i) { block(P, {}); juce::Thread::sleep(20); }
    check(P.deckTrack(0) != nullptr && P.hwBrowser().mode == 2, "LOAD A loads deck A, the list stays open", P.deckTrack(0) ? 1 : 0);
    press(P, 16, 3);
    for (int i = 0; i < 300 && (P.loading[1].load() || !P.deckTrack(1)); ++i) { block(P, {}); juce::Thread::sleep(20); }
    check(P.deckTrack(1) != nullptr && P.focusDeck() == 1, "LOAD B loads deck B", P.deckTrack(1) ? 1 : 0);
    for (int i = 0; i < 10; ++i) block(P, {});
    outFor(P);   // flush

    {   // PLAY: deck A plays, its LED lights once
        juce::MidiBuffer in; in.addEvent(on(1, 0), 0); in.addEvent(offN(1, 0), 1);
        auto o = outFor(P, in);
        check(P.status(0).playing.load(), "PLAY (note 0 ch 1) starts deck A", 1);
        check(ledNote(o, 1, 0) == 127, "PLAY LED lights (90 00 7F)", ledNote(o, 1, 0));
        int n = 0; for (const auto m : o) if (m.getMessage().getChannel() == 1 && m.getMessage().getNoteNumber() == 0) ++n;
        check(n == 1, "sent once", n);
    }
    for (int i = 0; i < 100; ++i) block(P, {});   // up to speed
    {   // jog (vinyl): scratch backwards while playing
        const double p0 = P.status(0).pos.load();
        const auto back = cc(1, 6, 128 - 20);
        bool touched = false;
        for (int i = 0; i < 10; ++i) { juce::MidiBuffer m; m.addEvent(back, 0); block(P, m); touched = touched || P.status(0).touched.load(); }
        blocks(P, 4);
        check(touched, "jog (CC 6) in vinyl mode puts a hand on deck A", 1);
        check(P.status(0).pos.load() < p0 - .05, "and drags it back (200 ticks = 0.2 turn)", P.status(0).pos.load() - p0);
        blocks(P, 60);
        check(!P.status(0).touched.load() && P.status(0).playing.load(), "the hand lifts after the jog stops; still playing", 0);
    }
    {   // VINYL off: the jog nudges (bend), no hand
        auto o = outFor(P, [] { juce::MidiBuffer m; m.addEvent(on(1, 80), 0); m.addEvent(offN(1, 80), 1); return m; }());
        check(!P.vinylMode(0) && P.vinylMode(1), "VINYL (note 80) turns deck A's vinyl mode off", 0);
        check(ledNote(o, 1, 80) == 1, "VINYL LED dims", ledNote(o, 1, 80));
        blocks(P, 20);
        const double b0 = P.status(0).pos.load();
        blocks(P, 60);
        const double el = P.status(0).pos.load() - b0;   // 60 blocks untouched
        const double p0 = P.status(0).pos.load();
        const auto fwd = cc(1, 6, 30);
        bool touched = false;
        for (int i = 0; i < 10; ++i) { juce::MidiBuffer m; m.addEvent(fwd, 0); block(P, m); touched = touched || P.status(0).touched.load(); }
        blocks(P, 50);
        const double got = P.status(0).pos.load() - p0;
        std::printf("      60 blocks: %.3f s untouched, %.3f s with the jog\n", el, got);
        check(!touched && got - el > .12 && got - el < .5, "jog nudges deck A ahead (300 ticks ~ +0.27 s), no scratch", got - el);
    }
    press(P, 16, 73); check(P.vinylMode(0) && P.vinylMode(1), "ch 16 note 73: vinyl on both decks", 0);
    press(P, 16, 73); check(!P.vinylMode(0) && !P.vinylMode(1), "again: off on both", 0);
    press(P, 16, 73);
    // sync
    press(P, 2, 2); blocks(P, 4); check(P.status(1).synced.load(), "SYNC (note 2 ch 2) syncs deck B", 0);
    // SHIFT + SYNC: sync off (the tempo stays latched until the pitch fader moves, as in Serato)
    press(P, 2, 3); send(P, cc(2, 9, 80)); blocks(P, 4);
    check(!P.status(1).synced.load(), "SHIFT + SYNC (note 3) turns it off", 0);
    press(P, 2, 84);
    // shift layer: censor (hold), jump to start, shift + jog = seek
    send(P, on(1, 4)); check(P.status(0).censoring.load(), "SHIFT + PLAY (note 4): censor while held", 0);
    send(P, offN(1, 4)); check(!P.status(0).censoring.load(), "released: censor off", 0);
    blocks(P, 20);
    press(P, 1, 5); check(P.status(0).pos.load() < .2, "SHIFT + CUE (note 5): back to the start", P.status(0).pos.load());
    {
        const double p0 = P.status(0).pos.load();
        send(P, cc(1, 43, 20));
        check(P.status(0).pos.load() > p0 + 1.5, "SHIFT + jog (CC 43): fast seek (20 ticks ~ 2 s)", P.status(0).pos.load() - p0);
        send(P, on(1, 32));
        check(P.shiftHeld(), "SHIFT (note 32 ch 1) held", 0);
        const double p1 = P.status(0).pos.load();
        send(P, cc(1, 6, 10));
        check(P.status(0).pos.load() > p1 + .7, "SHIFT held + jog CC 6: seeks too", P.status(0).pos.load() - p1);
        send(P, offN(1, 32));
        send(P, on(16, 50)); check(P.shiftHeld(), "SHIFT (note 50 ch 16) held", 0);
        send(P, offN(16, 50)); check(!P.shiftHeld(), "released", 0);
    }
    // cue on a playing deck: back to the cue point and stop
    press(P, 1, 1); blocks(P, 4);
    check(!P.status(0).playing.load(), "CUE (note 1) on a playing deck stops it at the cue point", P.status(0).pos.load());
    // 14-bit pitch: top = slower
    send(P, cc(1, 9, 0)); send(P, cc(1, 41, 0));       check(prm(P, "a_pitch") < -.99f, "pitch fader top (0): -100% of range", prm(P, "a_pitch"));
    send(P, cc(1, 9, 64)); send(P, cc(1, 41, 0));      check(std::abs(prm(P, "a_pitch")) < 1e-3f, "pitch fader centre (8192): 0", prm(P, "a_pitch"));
    send(P, cc(1, 9, 127)); send(P, cc(1, 41, 127));   check(prm(P, "a_pitch") > .999f, "pitch fader bottom (16383): +", prm(P, "a_pitch"));
    send(P, cc(1, 9, 64)); send(P, cc(1, 41, 64));     check(prm(P, "a_pitch") > 0 && prm(P, "a_pitch") < .01f, "the LSB refines it (8256)", prm(P, "a_pitch"));
    send(P, cc(1, 9, 96));                             check(std::abs(prm(P, "a_pitch") - .5f) < .001f, "MSB alone works (96 -> +50%)", prm(P, "a_pitch"));
    press(P, 1, 84);                                   check(std::abs(prm(P, "a_pitch")) < 1e-4f, "note 84: pitch reset", prm(P, "a_pitch"));
    // channel level, filter / low
    send(P, cc(2, 22, 0));   check(prm(P, "b_fader") < .01f, "channel level CC 22 ch 2: deck B fader down", prm(P, "b_fader"));
    send(P, cc(2, 22, 127)); check(prm(P, "b_fader") > .99f, "and up", prm(P, "b_fader"));
    send(P, cc(1, 26, 0));   check(prm(P, "a_filter") < -.99f, "filter knob CC 26: deck A filter (LP)", prm(P, "a_filter"));
    send(P, cc(1, 26, 64));  check(std::abs(prm(P, "a_filter")) < 1e-4f, "64: filter off", prm(P, "a_filter"));
    press(P, 16, 72);        check(P.filterLowMode(), "ch 16 note 72: FILTER / LOW toggle to low", 0);
    send(P, cc(1, 26, 0));   check(prm(P, "a_low") < -25.f && std::abs(prm(P, "a_filter")) < 1e-4f, "now CC 26 kills deck A's lows, filter untouched", prm(P, "a_low"));
    send(P, cc(1, 26, 64));  check(std::abs(prm(P, "a_low")) < .01f, "64: low EQ 0 dB", prm(P, "a_low"));
    press(P, 16, 72);        check(!P.filterLowMode(), "toggle back to filter", 0);
    {   // PFL
        auto o = outFor(P, [] { juce::MidiBuffer m; m.addEvent(on(1, 27), 0); m.addEvent(offN(1, 27), 1); return m; }());
        juce::Thread::sleep(80); block(P, {});
        check(prm(P, "a_pfl") > .5f, "PFL (note 27): headphone cue on", prm(P, "a_pfl"));
        check(ledNote(o, 1, 27) == 127, "PFL LED lit", ledNote(o, 1, 27));
        press(P, 1, 27); check(prm(P, "a_pfl") < .5f, "again: off", prm(P, "a_pfl"));
    }
    press(P, 1, 70); check(prm(P, "a_fxon") > .5f, "note 70: FX on", 0);
    press(P, 1, 70); check(prm(P, "a_fxon") < .5f, "again: off", 0);
    press(P, 1, 72); check(prm(P, "a_slip") > .5f, "note 72: slip on", 0);
    press(P, 1, 72);
    press(P, 16, 70);
    check(prm(P, "a_fxon") > .5f && prm(P, "b_fxon") > .5f && (int) prm(P, "a_fxtype") == 1 && (int) prm(P, "b_fxtype") == 1, "ch 16 note 70: echo on both decks", prm(P, "a_fxtype"));
    press(P, 16, 71); check((int) prm(P, "a_fxtype") == 2 && (int) prm(P, "b_fxtype") == 2, "ch 16 note 71: next FX type on both", prm(P, "a_fxtype"));
    press(P, 16, 70); check((int) prm(P, "a_fxtype") == 1 && prm(P, "a_fxon") > .5f, "note 70 when not all echo: echo", prm(P, "a_fxtype"));
    press(P, 16, 70); check(prm(P, "a_fxon") < .5f && prm(P, "b_fxon") < .5f, "note 70 again: echo off on both", 0);
    // pads in cue mode: set, shift + pad clears; LEDs follow
    check(P.padMode() == PadMode::cues, "pad mode: hot cues", 0);
    outFor(P);
    {
        auto o = outFor(P, [] { juce::MidiBuffer m; m.addEvent(on(5, 20), 0); m.addEvent(offN(5, 20), 1); return m; }());
        check(P.cuesOf(0)[0].set(), "pad 1 (note 20 ch 5) sets deck A cue 1", P.cuesOf(0)[0].t);
        check(ledNote(o, 5, 20) == 127, "its LED lights", ledNote(o, 5, 20));
        press(P, 6, 23); check(P.cuesOf(1)[3].set(), "note 23 ch 6 sets deck B cue 4", P.cuesOf(1)[3].t);
        o = outFor(P, [] { juce::MidiBuffer m; m.addEvent(on(5, 28), 0); m.addEvent(offN(5, 28), 1); return m; }());
        check(!P.cuesOf(0)[0].set(), "SHIFT + pad 1 (note 28) clears cue 1", P.cuesOf(0)[0].t);
        check(ledNote(o, 5, 20) == 1, "its LED dims", ledNote(o, 5, 20));
    }
    {   // pad mode button
        auto o = outFor(P, [] { juce::MidiBuffer m; m.addEvent(on(6, 0), 0); m.addEvent(offN(6, 0), 1); return m; }());
        check(P.padMode() == PadMode::loops, "pad mode button (note 0 ch 6): next mode, loops", (int) P.padMode());
        check(ledNote(o, 5, 2) == 127 && ledNote(o, 5, 1) == 1 && ledNote(o, 6, 2) == 127, "mode LEDs: loop lit, hot cue dim", ledNote(o, 5, 2));
        press(P, 5, 22); blocks(P, 4);
        check(P.status(0).looping.load(), "loop mode: pad 3 (note 22) = 1-beat auto loop", P.status(0).loopB.load() - P.status(0).loopA.load());
        press(P, 5, 0); press(P, 5, 0); press(P, 5, 0);
        check(P.padMode() == PadMode::cues, "four presses: back to hot cues", (int) P.padMode());
    }
    p.reset();
    dir.deleteRecursively();
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    std::thread t([] { test(); testMixtrack(); juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    t.join();
    std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
