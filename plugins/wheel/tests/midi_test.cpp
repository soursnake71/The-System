// wheel_midi_test: the processor's MIDI out of the box: the MPC's fader (touch strip / pitch bend, or CC 1) is the
// crossfader, the crossfader is the first parameter (hosts that auto-assign knobs take them in order), learning
// another control for it replaces the default, and saved mappings come back as saved.
#include "../WheelPlugin.h"
#include <cstdio>
#include <thread>
using namespace wheel;
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

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
}

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    std::thread t([] { test(); juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    t.join();
    std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
