// wheel_lib_test: crates, Serato tags, decoding and analysis on generated files (plus any real audio found here).
#include "../Library.h"
#include "../SeratoTags.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdio>
#include <random>

using namespace wheel;

namespace {

int failures = 0;
void check(bool ok, const juce::String& what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

constexpr double kSr = 44100;
constexpr float kPi = 3.14159265358979f;

// ---- synthetic drums ------------------------------------------------------------------------------------
std::mt19937 rng(1234);
float noise() { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng); }

void addKick(std::vector<float>& x, double t, float g)
{
    const long s0 = (long) (t * kSr);
    double ph = 0;
    for (long i = 0; i < (long) (0.35 * kSr); ++i) {
        const long j = s0 + i;
        if (j < 0 || j >= (long) x.size()) continue;
        const double tt = (double) i / kSr, f = 45 + 110 * std::exp(-tt * 30);
        ph += 2 * 3.14159265358979323846 * f / kSr;
        x[(size_t) j] += g * (float) (std::sin(ph) * std::exp(-tt * 9));
    }
}

void addSnare(std::vector<float>& x, double t, float g)
{
    const long s0 = (long) (t * kSr);
    float lp = 0;
    for (long i = 0; i < (long) (0.2 * kSr); ++i) {
        const long j = s0 + i;
        if (j < 0 || j >= (long) x.size()) continue;
        const float tt = (float) i / (float) kSr, n = noise();
        lp += 0.5f * (n - lp);
        x[(size_t) j] += g * (0.6f * (n - lp) * std::exp(-tt * 22) + 0.5f * std::sin(2 * kPi * 190 * tt) * std::exp(-tt * 30));
    }
}

void addHat(std::vector<float>& x, double t, float g, float decay)
{
    const long s0 = (long) (t * kSr);
    float prev = 0;
    for (long i = 0; i < (long) (0.15 * kSr); ++i) {
        const long j = s0 + i;
        if (j < 0 || j >= (long) x.size()) continue;
        const float n = noise(), hp = n - prev;
        prev = n;
        x[(size_t) j] += g * 0.35f * hp * std::exp(-(float) i / (float) kSr * decay);
    }
}

void addTone(std::vector<float>& x, double t, double dur, double hz, float g)
{
    const long s0 = (long) (t * kSr), len = (long) (dur * kSr);
    for (long i = 0; i < len; ++i) {
        const long j = s0 + i;
        if (j < 0 || j >= (long) x.size()) continue;
        const double tt = (double) i / kSr;
        const float env = (float) std::min(1.0, std::min(tt * 50, (dur - tt) * 20));
        float v = 0;
        for (int h = 1; h <= 6; ++h) v += (float) std::sin(2 * 3.14159265358979323846 * hz * h * tt) / (float) h;   // saw-ish
        x[(size_t) j] += g * env * v;
    }
}

double midiHz(int m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

// boom bap at 90: kick 1, "a" of 2, "and" of 3; snare 2 and 4; swung 8th hats; bass on the kicks
std::vector<float> hipHop(double bpm, double start, double seconds)
{
    std::vector<float> x((size_t) (seconds * kSr), 0.0f);
    const double step = 60.0 / bpm / 4;
    for (int bar = 0;; ++bar) {
        const double b0 = start + bar * 16 * step;
        if (b0 > seconds) break;
        for (int s = 0; s < 16; ++s) {
            const double t = b0 + s * step + ((s & 1) ? step * 0.12 : 0.0);
            if (s == 0 || s == 7 || s == 10) { addKick(x, t, s == 0 ? 0.9f : 0.7f); addTone(x, t, step * 2.5, midiHz(s == 0 ? 33 : 36), 0.15f); }
            if (s == 4 || s == 12) addSnare(x, t, 0.7f);
            if (s % 2 == 0 || s == 15) addHat(x, t, s % 4 == 2 ? 0.5f : 0.3f, 40);
        }
    }
    return x;
}

// house at 126: four to the floor, claps on 2 and 4, open hats off the beat, 16th closed hats
std::vector<float> house(double bpm, double start, double seconds)
{
    std::vector<float> x((size_t) (seconds * kSr), 0.0f);
    const double step = 60.0 / bpm / 4;
    for (int bar = 0;; ++bar) {
        const double b0 = start + bar * 16 * step;
        if (b0 > seconds) break;
        for (int s = 0; s < 16; ++s) {
            const double t = b0 + s * step;
            if (s % 4 == 0) addKick(x, t, 0.9f);
            if (s == 4 || s == 12) addSnare(x, t, 0.5f);
            if (s % 4 == 2) { addHat(x, t, 0.6f, 12); addTone(x, t, step * 1.5, midiHz(bar % 2 ? 41 : 43), 0.12f); }
            else addHat(x, t, 0.2f, 60);
        }
    }
    return x;
}

// A minor: Am - Dm - E - Am (harmonic minor so it can't be C major), 2 s per chord, with a bass note
std::vector<float> chordsAm(double seconds)
{
    std::vector<float> x((size_t) (seconds * kSr), 0.0f);
    const int chords[4][3] = { { 57, 60, 64 }, { 62, 65, 69 }, { 64, 68, 71 }, { 57, 60, 64 } };
    const int bass[4] = { 45, 50, 40, 45 };
    for (int i = 0; i * 2.0 < seconds; ++i) {
        const auto& c = chords[i % 4];
        for (int k = 0; k < 3; ++k) addTone(x, i * 2.0, 2.0, midiHz(c[k]), 0.12f);
        addTone(x, i * 2.0, 2.0, midiHz(bass[i % 4]), 0.15f);
        addTone(x, i * 2.0 + 1.0, 1.0, midiHz(c[i % 3] + 12), 0.08f);   // a little melody on top
    }
    return x;
}

// a groove for the BPM sweep: 16 steps per bar, bitmasks for kick / snare / hats, swing, humanised timing and level
struct Groove { const char* name; uint16_t kick, snare, hat, openHat; float swing, hatGain; bool bass; };
std::vector<float> groove(const Groove& g, double bpm, double start, double seconds)
{
    std::vector<float> x((size_t) (seconds * kSr), 0.0f);
    std::normal_distribution<double> jit(0.0, 0.003);
    std::uniform_real_distribution<float> vel(0.75f, 1.0f);
    const double step = 60.0 / bpm / 4;
    for (int bar = 0;; ++bar) {
        const double b0 = start + bar * 16 * step;
        if (b0 > seconds) break;
        for (int s = 0; s < 16; ++s) {
            const double t = b0 + s * step + ((s & 1) ? step * g.swing : 0.0) + jit(rng);
            const uint16_t m = (uint16_t) (1u << s);
            if (g.kick & m) { addKick(x, t, 0.9f * vel(rng)); if (g.bass) addTone(x, t, step * 1.8, midiHz(31 + (bar % 4) * 2), 0.15f); }
            if (g.snare & m) addSnare(x, t, 0.7f * vel(rng));
            if (g.hat & m) addHat(x, t, g.hatGain * vel(rng), 45);
            if (g.openHat & m) addHat(x, t, g.hatGain * 1.3f * vel(rng), 10);
        }
    }
    // a quiet pad so it isn't pure drums
    for (int c = 0; c * 4.0 < seconds; ++c) addTone(x, c * 4.0, 4.0, midiHz(57 + (c % 3) * 2), 0.03f);
    return x;
}

void normalise(std::vector<float>& x)
{
    float m = 0;
    for (float v : x) m = std::max(m, std::abs(v));
    if (m > 0) for (auto& v : x) v *= 0.9f / m;
}

bool writeWav(const juce::File& f, const std::vector<float>& mono, bool stereo = true)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(f);
    std::unique_ptr<juce::AudioFormatWriter> w { wav.createWriterFor(os.get(), kSr, stereo ? 2u : 1u, 16, {}, 0) };
    if (! w) return false;
    os.release();
    const float* ch[2] = { mono.data(), mono.data() };
    return w->writeFromFloatArrays(ch, stereo ? 2 : 1, (int) mono.size());
}

// ---- Serato / ID3 builders ------------------------------------------------------------------------------
void put32(juce::MemoryOutputStream& m, uint32_t v) { m.writeIntBigEndian((int) v); }

void crateRecord(juce::MemoryOutputStream& m, const char* tag, const juce::MemoryBlock& payload)
{
    m.write(tag, 4);
    put32(m, (uint32_t) payload.getSize());
    m.write(payload.getData(), payload.getSize());
}

juce::MemoryBlock utf16be(const juce::String& s)
{
    juce::MemoryOutputStream m;
    for (auto p = s.toUTF16(); ! p.isEmpty(); ++p) m.writeShortBigEndian((short) *p);
    return m.getMemoryBlock();
}

juce::MemoryBlock makeCrate(const juce::StringArray& paths)
{
    juce::MemoryOutputStream m;
    crateRecord(m, "vrsn", utf16be("1.0/Serato ScratchLive Crate"));
    {   // a column record, as Serato writes (osrt / ovct), to check they're skipped
        juce::MemoryOutputStream c;
        crateRecord(c, "tvcn", utf16be("song"));
        crateRecord(m, "ovct", c.getMemoryBlock());
    }
    for (const auto& p : paths) {
        juce::MemoryOutputStream t;
        crateRecord(t, "ptrk", utf16be(p));
        crateRecord(m, "otrk", t.getMemoryBlock());
    }
    return m.getMemoryBlock();
}

// a Serato smart crate (.scrate), laid out as community tools (serato-tools) read and write them
struct SmartRuleSpec { int field; const char* op; juce::String text; int num = -1; };
juce::MemoryBlock makeSmartCrate(bool matchAll, const std::vector<SmartRuleSpec>& rules, const juce::StringArray& paths = {})
{
    auto u32 = [](uint32_t v) { juce::MemoryOutputStream m; put32(m, v); return m.getMemoryBlock(); };
    auto flag = [](bool on) { juce::MemoryOutputStream b; const char v = on ? 1 : 0; crateRecord(b, "brut", juce::MemoryBlock(&v, 1)); return b.getMemoryBlock(); };
    juce::MemoryOutputStream m;
    crateRecord(m, "vrsn", utf16be("1.0/Serato ScratchLive Smart Crate"));
    crateRecord(m, "rlut", flag(true));
    crateRecord(m, "rart", flag(matchAll));
    for (const auto& r : rules) {
        juce::MemoryOutputStream x;
        crateRecord(x, "trft", utf16be(r.op));
        crateRecord(x, "urkt", u32((uint32_t) r.field));
        if (r.num >= 0) crateRecord(x, "urpt", u32((uint32_t) r.num));
        else crateRecord(x, "trpt", utf16be(r.text));
        crateRecord(m, "rurt", x.getMemoryBlock());
    }
    for (const auto& p : paths) {
        juce::MemoryOutputStream t;
        crateRecord(t, "ptrk", utf16be(p));
        crateRecord(m, "otrk", t.getMemoryBlock());
    }
    return m.getMemoryBlock();
}

void id3Frame(juce::MemoryOutputStream& m, const char* id, const juce::MemoryBlock& body)
{
    m.write(id, 4);
    put32(m, (uint32_t) body.getSize());
    m.writeShort(0);
    m.write(body.getData(), body.getSize());
}

juce::MemoryBlock textFrame(const juce::String& s)
{
    juce::MemoryOutputStream m;
    m.writeByte(0);
    m.write(s.toRawUTF8(), s.getNumBytesAsUTF8());
    return m.getMemoryBlock();
}

juce::MemoryBlock geob(const juce::String& desc, const juce::MemoryBlock& data)
{
    juce::MemoryOutputStream m;
    m.writeByte(0);
    m.writeString("application/octet-stream");
    m.writeByte(0);   // file name
    m.writeString(desc);
    m.write(data.getData(), data.getSize());
    return m.getMemoryBlock();
}

juce::MemoryBlock markers2()
{
    juce::MemoryOutputStream raw;
    raw.writeByte(1); raw.writeByte(1);
    auto entry = [&raw](const char* type, const juce::MemoryBlock& b) { raw.writeString(type); put32(raw, (uint32_t) b.getSize()); raw.write(b.getData(), b.getSize()); };
    { juce::MemoryOutputStream b; b.writeByte(0); b.writeByte((char) 0xff); b.writeByte((char) 0xff); b.writeByte((char) 0xff); entry("COLOR", b.getMemoryBlock()); }
    auto cue = [&](int idx, uint32_t ms, uint32_t rgb, const char* name) {
        juce::MemoryOutputStream b;
        b.writeByte(0); b.writeByte((char) idx); put32(b, ms); b.writeByte(0);
        b.writeByte((char) (rgb >> 16)); b.writeByte((char) (rgb >> 8)); b.writeByte((char) rgb);
        b.writeByte(0); b.writeByte(0); b.writeString(name);
        entry("CUE", b.getMemoryBlock());
    };
    cue(0, 1500, 0xCC0000, "Drop");
    cue(3, 10250, 0x00CC00, "Verse");
    {
        juce::MemoryOutputStream b;
        b.writeByte(0); b.writeByte(1); put32(b, 4000); put32(b, 8000);
        for (int i = 0; i < 4; ++i) b.writeByte((char) 0xff);
        b.writeByte(0); b.writeByte(0x27); b.writeByte((char) 0xaa); b.writeByte((char) 0xe1); b.writeByte(0); b.writeByte(0);
        b.writeString("");
        entry("LOOP", b.getMemoryBlock());
    }
    { juce::MemoryOutputStream b; b.writeByte(0); entry("BPMLOCK", b.getMemoryBlock()); }
    raw.writeByte(0);
    juce::String b64 = juce::Base64::toBase64(raw.getData(), raw.getDataSize()).removeCharacters("=");
    juce::String wrapped;   // Serato wraps the base64 at 72 characters
    for (int i = 0; i < b64.length(); i += 72) wrapped << b64.substring(i, i + 72) << "\n";
    juce::MemoryOutputStream m;
    m.writeByte(1); m.writeByte(1);
    m.write(wrapped.toRawUTF8(), wrapped.getNumBytesAsUTF8());
    m.writeByte(0);
    return m.getMemoryBlock();
}

juce::MemoryBlock beatGrid(float firstBeat, float bpm)
{
    juce::MemoryOutputStream m;
    m.writeByte(1); m.writeByte(0);
    put32(m, 1);
    m.writeFloatBigEndian(firstBeat);
    m.writeFloatBigEndian(bpm);
    m.writeByte(0);
    return m.getMemoryBlock();
}

juce::MemoryBlock id3Tag()
{
    juce::MemoryOutputStream frames;
    id3Frame(frames, "TIT2", textFrame("Cue Test"));
    id3Frame(frames, "TPE1", textFrame("Serato Tester"));
    id3Frame(frames, "TKEY", textFrame("8A"));
    id3Frame(frames, "TCON", textFrame("(35)"));   // ID3v1 genre 35 = House
    id3Frame(frames, "GEOB", geob("Serato Markers2", markers2()));
    id3Frame(frames, "GEOB", geob("Serato BeatGrid", beatGrid(0.1f, 128.0f)));
    juce::MemoryOutputStream m;
    m.write("ID3", 3); m.writeByte(3); m.writeByte(0); m.writeByte(0);
    const uint32_t n = (uint32_t) frames.getDataSize();
    m.writeByte((char) ((n >> 21) & 0x7f)); m.writeByte((char) ((n >> 14) & 0x7f)); m.writeByte((char) ((n >> 7) & 0x7f)); m.writeByte((char) (n & 0x7f));
    m.write(frames.getData(), frames.getDataSize());
    return m.getMemoryBlock();
}

// append an "id3 " chunk to a WAV and fix the RIFF size
bool addId3Chunk(const juce::File& wavFile, const juce::MemoryBlock& tag)
{
    juce::MemoryBlock d;
    if (! wavFile.loadFileAsData(d)) return false;
    juce::MemoryOutputStream m;
    m.write(d.getData(), d.getSize());
    m.write("id3 ", 4);
    m.writeInt((int) tag.getSize());
    m.write(tag.getData(), tag.getSize());
    if (tag.getSize() & 1) m.writeByte(0);
    juce::MemoryBlock out = m.getMemoryBlock();
    const uint32_t riff = (uint32_t) out.getSize() - 8;
    auto* b = (uint8_t*) out.getData();
    b[4] = (uint8_t) riff; b[5] = (uint8_t) (riff >> 8); b[6] = (uint8_t) (riff >> 16); b[7] = (uint8_t) (riff >> 24);
    return wavFile.replaceWithData(out.getData(), out.getSize());
}

const Crate* findCrate(const Library& lib, const juce::String& name)
{
    for (const auto& c : lib.crates()) if (c.name == name) return &c;
    return nullptr;
}

double beatPhaseErr(double got, double expect, double bpm)
{
    const double P = 60.0 / bpm;
    double d = std::fmod(got - expect, P);
    if (d < 0) d += P;
    return std::min(d, P - d);
}

}  // namespace

int main(int argc, char** argv)
{
    const juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("wheel_lib_test_card");
    root.deleteRecursively();
    root.createDirectory();
    std::printf("test card: %s\n", root.getFullPathName().toRawUTF8());

    // ---- the card ----
    {
        auto hh = hipHop(90, 0.30, 40); normalise(hh);
        writeWav(root.getChildFile("Music/Hip Hop/DJ Test - Boom Bap 90.wav"), hh);
        auto hs = house(126, 0.12, 40); normalise(hs);
        writeWav(root.getChildFile("Music/House/house126.wav"), hs);
        auto ch = chordsAm(24); normalise(ch);
        writeWav(root.getChildFile("Music/Keys/chords_am.wav"), ch, false);
        auto cu = house(124, 0.1, 15); normalise(cu);
        const juce::File cued = root.getChildFile("Music/Cues/cued.wav");
        writeWav(cued, cu);
        check(addId3Chunk(cued, id3Tag()), "wrote WAV with an ID3 chunk");
        writeWav(root.getChildFile(".Trashes/junk.wav"), std::vector<float>(1000, 0.0f));
        root.getChildFile("Autosave/session.xpj").create();
        root.getChildFile("MPC Projects/empty/readme.txt").create();
        root.getChildFile("Playlists").createDirectory();
        root.getChildFile("Playlists/set.m3u8").replaceWithText("#EXTM3U\n#EXTINF:40,House\n../Music/House/house126.wav\nE:\\Music\\Keys\\chords_am.wav\n");

        const auto sub = root.getChildFile("_Serato_/Subcrates");
        sub.createDirectory();
        root.getChildFile("_Serato_/SmartCrates").createDirectory();
        auto save = [&](const juce::String& name, const juce::StringArray& paths) {
            const auto mb = makeCrate(paths);
            sub.getChildFile(name + ".crate").replaceWithData(mb.getData(), mb.getSize());
        };
        save("House", { "Volumes/MyCard/Music/House/house126.wav", "E:\\Music\\Keys\\chords_am.wav", "Music/nothere.mp3" });
        save("Hip Hop", { "Music/Hip Hop/DJ Test - Boom Bap 90.wav" });
        save("Hip Hop%%Boom Bap", { "Users/someone/Music/Hip Hop/DJ Test - Boom Bap 90.wav", "Music/Cues/cued.wav" });
        save("100% Bangers", { "Music/House/house126.wav" });
        const auto smart = root.getChildFile("_Serato_/SmartCrates");
        auto saveSmart = [&](const juce::String& name, const juce::MemoryBlock& mb) { smart.getChildFile(name + ".scrate").replaceWithData(mb.getData(), mb.getSize()); };
        saveSmart("Bangers 128", makeSmartCrate(true, { { 15, "cond_greq_uint", {}, 125 }, { 7, "cond_con_str", "SERATO" } }));
        saveSmart("Boom or Am", makeSmartCrate(false, { { 6, "cond_con_str", "boom" }, { 51, "cond_is_str", "A minor" } }));
        saveSmart("Genre House", makeSmartCrate(true, { { 9, "cond_is_str", "house" }, { 15, "cond_lseq_uint", {}, 130 } }));
        saveSmart("Played", makeSmartCrate(true, { { 79, "cond_greq_uint", {}, 3 } }, { "Music/House/house126.wav" }));
        saveSmart("Sets%%Warmup", makeSmartCrate(true, { { 4, "cond_con_str", "chords" }, { 7, "cond_dnc_str", "tester" } }));
    }

    // ---- parseCrate round trip ----
    {
        const auto mb = makeCrate({ "Music/a.mp3", juce::String::fromUTF8("Volumes/X/b \xc3\xa9.mp3") });
        const auto p = parseCrate(mb);
        check(p.size() == 2 && p[0] == "Music/a.mp3" && p[1] == juce::String::fromUTF8("Volumes/X/b \xc3\xa9.mp3"), "parseCrate reads ptrk paths (UTF-16BE)");
    }

    // ---- key helpers ----
    check(keyName(21) == "Am" && camelot(21) == "8A" && camelot(0) == "8B" && keyName(6) == "F#", "keyName / camelot");
    check(parseKey("8A") == 21 && parseKey("08A") == 21 && parseKey("Am") == 21 && parseKey("A minor") == 21 && parseKey("F#m") == 18
              && parseKey("Gb") == 6 && parseKey("C") == 0 && parseKey("Bbm") == 22 && parseKey("1m") == 21 && parseKey("xyz") == -1,
          "parseKey (Camelot, Open Key, names)");

    Library lib;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    lib.setRoot(root.getChildFile("Music"));   // a subfolder: _Serato_ found in the parent
    std::printf("setRoot: %.1f ms, crates:\n", juce::Time::getMillisecondCounterHiRes() - t0);
    for (const auto& c : lib.crates())
        std::printf("   %*s%s  [%s]\n", c.depth * 2, "", c.name.toRawUTF8(),
                    c.kind == Crate::all ? "all" : c.kind == Crate::serato ? "serato" : c.kind == Crate::folder ? "folder" : c.kind == Crate::smart ? "smart" : "playlist");
    lib.setRoot(root);
    std::printf("crates from the card root:\n");
    for (const auto& c : lib.crates())
        std::printf("   %*s%s  [%s]\n", c.depth * 2, "", c.name.toRawUTF8(),
                    c.kind == Crate::all ? "all" : c.kind == Crate::serato ? "serato" : c.kind == Crate::folder ? "folder" : c.kind == Crate::smart ? "smart" : "playlist");
    const auto& cr = lib.crates();
    check(! cr.isEmpty() && cr[0].kind == Crate::all && cr[0].name == "All tracks", "All tracks first");
    const Crate* hip = findCrate(lib, "Hip Hop");
    const Crate* boom = findCrate(lib, "Hip Hop / Boom Bap");
    const Crate* hou = findCrate(lib, "House");
    check(hip && hip->kind == Crate::serato && hip->depth == 0, "Serato crate 'Hip Hop'");
    check(boom && boom->kind == Crate::serato && boom->depth == 1 && boom == hip + 1, "nested crate 'Hip Hop / Boom Bap' right after its parent");
    check(findCrate(lib, "100% Bangers") != nullptr, "crate name with a % sign");
    check(hou && hou->kind == Crate::serato, "Serato crate 'House'");
    const Crate* pl = findCrate(lib, "set");
    check(pl && pl->kind == Crate::playlist, "m3u8 playlist found");
    {   // Serato smart crates: a group, then each crate with its rules evaluated over the card
        const Crate* grp = findCrate(lib, "Smart crates");
        check(grp && grp->kind == Crate::smart && grp->depth == 0, "'Smart crates' group listed");
        const Crate* bang = findCrate(lib, "Smart crates / Bangers 128");
        const Crate* warm = findCrate(lib, "Smart crates / Sets / Warmup");
        check(bang && bang->kind == Crate::smart && bang->depth == 1 && warm && warm->depth == 2, "smart crates under it (nested names too)");
        auto names = [&](const Crate* c) {
            juce::StringArray n;
            if (c) for (const auto& e : lib.list(*c)) n.add(e.file.getFileName());
            n.sort(true);
            return n.joinIntoString(",");
        };
        check(names(bang) == "cued.wav", "match all: BPM >= 125 and artist contains: " + names(bang));
        check(names(findCrate(lib, "Smart crates / Boom or Am")) == "cued.wav,DJ Test - Boom Bap 90.wav", "match any: title contains / key is (8A = A minor): " + names(findCrate(lib, "Smart crates / Boom or Am")));
        check(names(findCrate(lib, "Smart crates / Genre House")) == "cued.wav", "genre is (ID3 '(35)' = House) and BPM <= 130: " + names(findCrate(lib, "Smart crates / Genre House")));
        check(names(findCrate(lib, "Smart crates / Played")) == "house126.wav", "a rule it can't evaluate (plays): the stored track list: " + names(findCrate(lib, "Smart crates / Played")));
        check(names(warm) == "chords_am.wav", "file name contains / artist does not contain: " + names(warm));
        check(names(grp) == "chords_am.wav,cued.wav,DJ Test - Boom Bap 90.wav,house126.wav", "the group lists every smart crate's tracks once: " + names(grp));
        const auto sc = parseSmartCrate((const uint8_t*) makeSmartCrate(false, { { 15, "cond_greq_uint", {}, 90 } }).getData(), 0);
        check(sc.rules.empty(), "parseSmartCrate: empty input");
        const auto mb = makeSmartCrate(false, { { 15, "cond_lseq_uint", {}, 90 }, { 21, "cond_con_str", "YT" } }, { "a.mp3" });
        const auto sc2 = parseSmartCrate((const uint8_t*) mb.getData(), mb.getSize());
        check(! sc2.matchAll && sc2.liveUpdate && sc2.rules.size() == 2 && sc2.rules[0].field == 15 && sc2.rules[0].hasNum && sc2.rules[0].num == 90
                  && sc2.rules[1].op == "cond_con_str" && sc2.rules[1].text == "YT" && ! smartRuleSupported(sc2.rules[1]) && sc2.paths.size() == 1,
              "parseSmartCrate: match any, live update, rules (u32 / text values), stored tracks");
    }
    const Crate* fMusic = findCrate(lib, "Music");
    bool folderHouse = false, junk = false;
    for (const auto& c : cr) {
        if (c.kind == Crate::folder && c.name == "House" && c.depth == 1) folderHouse = true;
        if (c.name.containsIgnoreCase("Trashes") || c.name == "_Serato_" || c.name == "Autosave" || c.name == "MPC Projects" || c.name == "empty") junk = true;
    }
    check(fMusic && fMusic->kind == Crate::folder && fMusic->depth == 0 && folderHouse, "folder crates with depth");
    check(! junk, "hidden / system / audio-less folders skipped");

    const auto all = lib.list(cr[0]);
    check(all.size() == 4, "All tracks lists 4 files (got " + juce::String(all.size()) + ")");
    if (hou) {
        const auto e = lib.list(*hou);
        check(e.size() == 2 && e[0].file.getFileName() == "house126.wav" && e[1].file.getFileName() == "chords_am.wav",
              "crate paths resolved (Mac /Volumes and Windows D:\\ paths)");
    }
    if (boom) {
        const auto e = lib.list(*boom);
        check(e.size() == 2 && e[0].file.getFileName() == "DJ Test - Boom Bap 90.wav", "crate path matched by suffix (Users/someone/...)");
    }
    if (pl) {
        const auto e = lib.list(*pl);
        check(e.size() == 2, "playlist entries resolved");
    }
    {
        const auto hits = lib.search("boom  BAP", cr[0]);
        check(hits.size() == 1 && hits[0].artist == "DJ Test" && hits[0].title == "Boom Bap 90", "search + Artist - Title from file name");
        check(lib.search("boom house", cr[0]).isEmpty(), "search needs all words");
        const auto cuedE = lib.search("serato tester", cr[0]);
        check(cuedE.size() == 1 && cuedE[0].serato && std::abs(cuedE[0].bpm - 128) < 0.01 && cuedE[0].keyText == "8A" && cuedE[0].length > 14.9,
              "entry: ID3-in-WAV tags (title, artist, bpm, key, length)");
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i) lib.list(cr[0]);
        std::printf("re-listing All tracks x20 (cached): %.2f ms\n", juce::Time::getMillisecondCounterHiRes() - t1);
    }

    // ---- load + analysis ----
    auto loadTimed = [&](const juce::File& f) {
        juce::String err;
        const auto a = juce::Time::getMillisecondCounterHiRes();
        auto t = lib.load(f, err);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - a;
        if (! t) { std::printf("load %s failed: %s\n", f.getFileName().toRawUTF8(), err.toRawUTF8()); return t; }
        std::printf("load %-28s %6.0f ms  len %.2f s  bpm %.2f  first beat %.3f  key %s (%s)  '%s' by '%s'\n", f.getFileName().toRawUTF8(), ms,
                    t->length(), t->bpm, t->firstBeat, keyName(t->key).c_str(), camelot(t->key).c_str(), t->title.c_str(), t->artist.c_str());
        return t;
    };
    if (auto t = loadTimed(root.getChildFile("Music/Hip Hop/DJ Test - Boom Bap 90.wav"))) {
        check(std::abs(t->bpm - 90) < 0.5, "hip-hop BPM 90 (got " + juce::String(t->bpm, 2) + ")");
        check(std::abs(t->firstBeat - 0.30) < 0.03, "hip-hop first downbeat at 0.30 s (got " + juce::String(t->firstBeat, 3) + ")");
        const size_t cols = (size_t) std::ceil(t->length() * kWaveRate);
        check(t->wave.peak.size() == cols && t->wave.low.size() == cols && t->wave.mid.size() == cols && t->wave.high.size() == cols, "waveform sizes");
        const auto mx = *std::max_element(t->wave.peak.begin(), t->wave.peak.end());
        const auto ml = *std::max_element(t->wave.low.begin(), t->wave.low.end());
        const auto mh = *std::max_element(t->wave.high.begin(), t->wave.high.end());
        check(mx >= 240 && ml > 150 && mh > 150, "waveform normalised (peak " + juce::String(mx) + ", low " + juce::String(ml) + ", high " + juce::String(mh) + ")");
    }
    if (auto t = loadTimed(root.getChildFile("Music/House/house126.wav"))) {
        check(std::abs(t->bpm - 126) < 0.5, "house BPM 126 (got " + juce::String(t->bpm, 2) + ")");
        check(std::abs(t->firstBeat - 0.12) < 0.03, "house first downbeat at 0.12 s (got " + juce::String(t->firstBeat, 3) + ")");
    }
    if (auto t = loadTimed(root.getChildFile("Music/Keys/chords_am.wav"))) {
        check(t->key == 21, "key A minor (got " + juce::String(keyName(t->key)) + ")");
        check(t->L.size() == t->R.size() && t->L == t->R, "mono file duplicated to L/R");
    }
    if (auto t = loadTimed(root.getChildFile("Music/Cues/cued.wav"))) {
        check(t->fromSerato && std::abs(t->bpm - 128) < 0.01 && std::abs(t->firstBeat - 0.1) < 0.001, "Serato BeatGrid: bpm 128, first beat 0.1");
        check(t->cues[0].set() && std::abs(t->cues[0].t - 1.5) < 1e-6 && t->cues[0].rgb == 0xCC0000 && t->cues[0].name == "Drop", "cue 1: 1.5 s, red, 'Drop'");
        check(t->cues[3].set() && std::abs(t->cues[3].t - 10.25) < 1e-6 && t->cues[3].rgb == 0x00CC00 && t->cues[3].name == "Verse", "cue 4: 10.25 s, green, 'Verse'");
        check(! t->cues[1].set() && ! t->cues[2].set(), "unset cues stay unset");
        check(t->loops[1].set() && std::abs(t->loops[1].a - 4) < 1e-6 && std::abs(t->loops[1].b - 8) < 1e-6, "saved loop 2: 4..8 s");
        check(t->key == 21 && t->title == "Cue Test" && t->artist == "Serato Tester", "TKEY / TIT2 / TPE1");
    }
    {   // cancel
        juce::String err;
        auto t = lib.load(root.getChildFile("Music/House/house126.wav"), err, [] { return true; });
        check(t == nullptr && err.isNotEmpty(), "load honours cancelled()");
    }
    {   // a 5 minute track for timing and BPM precision
        auto hs = house(124, 0.05, 300);
        normalise(hs);
        const juce::File big = root.getChildFile("Long/long124.wav");
        writeWav(big, hs);
        if (auto t = loadTimed(big)) check(std::abs(t->bpm - 124) < 0.02, "5 min track BPM 124 to 0.02 (got " + juce::String(t->bpm, 3) + ")");
        auto hh = hipHop(93.5, 1.0, 180);
        normalise(hh);
        const juce::File hhf = root.getChildFile("Long/hh935.wav");
        writeWav(hhf, hh);
        if (auto t = loadTimed(hhf)) {
            check(std::abs(t->bpm - 93.5) < 0.05, "3 min hip-hop BPM 93.5 (got " + juce::String(t->bpm, 3) + ")");
            check(std::abs(t->firstBeat - 1.0) < 0.03, "hip-hop downbeat after 1 s of silence (got " + juce::String(t->firstBeat, 3) + ")");
        }
    }

    // ---- BPM sweep over grooves and tempos (30 s clips, straight into detectBpm) ----
    {
        const uint16_t steps8 = 0x5555, all16 = 0xffff, beats = 0x1111;
        const Groove grooves[] = {
            { "boom bap",          0x0481, 0x1010, steps8, 0,       0.12f, 0.35f, true },
            { "boom bap 16th hats",0x0481, 0x1010, all16,  0,       0.08f, 0.55f, true },
            { "lazy swing",        0x2401, 0x1010, steps8, 0x8000,  0.22f, 0.40f, true },
            { "kick heavy",        0x0525, 0x1010, steps8, 0,       0.10f, 0.30f, true },
            { "four to the floor", beats,  0x1010, all16,  0x4444,  0.0f,  0.30f, true },
            { "house no clap",     beats,  0,      0,      0x4444,  0.0f,  0.45f, true },
            { "garage swing",      0x0101, 0x1010, all16,  0x4444,  0.14f, 0.35f, true },
        };
        struct Case { int g; double bpm; };
        const Case cases[] = { { 0, 85 }, { 0, 88 }, { 0, 92 }, { 0, 96 }, { 0, 100 }, { 1, 86 }, { 1, 90 }, { 1, 95 }, { 2, 84 }, { 2, 89 },
                               { 2, 97 }, { 3, 91 }, { 3, 99 }, { 4, 118 }, { 4, 120 }, { 4, 122 }, { 4, 124 }, { 4, 125 }, { 4, 127 },
                               { 4, 128 }, { 4, 130 }, { 5, 123 }, { 5, 126 }, { 6, 132 }, { 6, 128 }, { 0, 76 }, { 4, 105 }, { 4, 140 } };
        int good = 0, total = 0;
        double worstPhase = 0;
        for (const auto& c : cases) {
            const auto& g = grooves[c.g];
            const double start = 0.2 + 0.1 * (total % 5);
            auto x = groove(g, c.bpm, start, 30);
            normalise(x);
            double fb = 0;
            const auto a = juce::Time::getMillisecondCounterHiRes();
            const double got = detectBpm(x, kSr, fb);
            const double ms = juce::Time::getMillisecondCounterHiRes() - a;
            const bool ok = std::abs(got - c.bpm) < 0.5;
            const double ph = beatPhaseErr(fb, start, c.bpm / 4);   // the downbeat: phase within the bar
            worstPhase = std::max(worstPhase, ph);
            good += ok ? 1 : 0;
            ++total;
            std::printf("   %-20s %6.1f -> %7.2f  first beat %.3f (phase err %4.1f ms)  %4.0f ms  %s\n", g.name, c.bpm, got, fb, ph * 1000, ms, ok ? "ok" : "WRONG");
        }
        check(good == total, "BPM sweep: " + juce::String(good) + "/" + juce::String(total) + " within 0.5 BPM");
        check(worstPhase < 0.03, "BPM sweep: downbeat within 30 ms (worst " + juce::String(worstPhase * 1000, 1) + " ms)");
    }

    // ---- real audio on this machine, if any ----
    juce::Array<juce::File> real;
    for (int i = 1; i < argc; ++i) real.add(juce::File::getCurrentWorkingDirectory().getChildFile(argv[i]));
    for (auto p : { "/home/user/The-System/damn-son.mp3" }) if (juce::File(p).existsAsFile()) real.add(juce::File(p));
    for (const auto& f : real) {
        std::printf("real file: %s\n", f.getFullPathName().toRawUTF8());
        const auto e = lib.readEntry(f);
        std::printf("   entry: '%s' / '%s'  bpm %.2f  key '%s'  length %.2f s\n", e.title.toRawUTF8(), e.artist.toRawUTF8(), e.bpm, e.keyText.toRawUTF8(), e.length);
        loadTimed(f);
    }

    root.deleteRecursively();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
