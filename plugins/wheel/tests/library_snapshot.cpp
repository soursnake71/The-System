// Omega Wheel of Steel: renders the phone-style library screen (PhoneLibraryView) to PNGs, headless (xvfb-run -a).
// Writes a small music tree (WAVs with ID3 tags, Serato crates in _Serato_/Subcrates incl. a nested one, an .m3u8),
// loads two tracks on the decks (the A / B marks), sets the root, waits for the listing, opens the House crate,
// selects its second track and snapshots the view at 2000 x 933 and 1280 x 600 (the phone's 800 x 360 dp shape).
// The reference is the Android screen: android/wheel/src/test/.../LibraryShotTest.java (same crates and tracks).
//   wheel_library_snapshot <out dir>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include "../WheelPlugin.h"
#include "../PhoneUI.h"

using namespace juce;

namespace {

void put32(MemoryOutputStream& m, uint32_t v) { m.writeIntBigEndian((int) v); }

void crateRecord(MemoryOutputStream& m, const char* tag, const MemoryBlock& payload)
{
    m.write(tag, 4);
    put32(m, (uint32_t) payload.getSize());
    m.write(payload.getData(), payload.getSize());
}

MemoryBlock utf16be(const String& s)
{
    MemoryOutputStream m;
    for (auto p = s.toUTF16(); ! p.isEmpty(); ++p) m.writeShortBigEndian((short) *p);
    return m.getMemoryBlock();
}

MemoryBlock makeCrate(const StringArray& paths)
{
    MemoryOutputStream m;
    crateRecord(m, "vrsn", utf16be("1.0/Serato ScratchLive Crate"));
    for (const auto& p : paths) {
        MemoryOutputStream t;
        crateRecord(t, "ptrk", utf16be(p));
        crateRecord(m, "otrk", t.getMemoryBlock());
    }
    return m.getMemoryBlock();
}

void id3Frame(MemoryOutputStream& m, const char* id, const String& text)
{
    MemoryOutputStream b;
    b.writeByte(0);
    b.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
    m.write(id, 4);
    put32(m, (uint32_t) b.getDataSize());
    m.writeShort(0);
    m.write(b.getData(), b.getDataSize());
}

MemoryBlock id3(const String& title, const String& artist, const String& bpm, const String& key)
{
    MemoryOutputStream frames;
    id3Frame(frames, "TIT2", title);
    id3Frame(frames, "TPE1", artist);
    if (bpm.isNotEmpty()) id3Frame(frames, "TBPM", bpm);
    if (key.isNotEmpty()) id3Frame(frames, "TKEY", key);
    MemoryOutputStream m;
    m.write("ID3", 3); m.writeByte(3); m.writeByte(0); m.writeByte(0);
    const uint32_t n = (uint32_t) frames.getDataSize();
    m.writeByte((char) ((n >> 21) & 0x7f)); m.writeByte((char) ((n >> 14) & 0x7f)); m.writeByte((char) ((n >> 7) & 0x7f)); m.writeByte((char) (n & 0x7f));
    m.write(frames.getData(), frames.getDataSize());
    return m.getMemoryBlock();
}

// a quiet 8 kHz mono WAV of the given length (a soft tone, so analysis has something), with an "id3 " chunk
void writeTrack(const File& f, double seconds, const MemoryBlock& tag)
{
    if (f.existsAsFile()) return;
    f.getParentDirectory().createDirectory();
    const double sr = 8000;
    const int n = (int) (seconds * sr);
    AudioBuffer<float> buf(1, n);
    for (int i = 0; i < n; ++i) buf.setSample(0, i, .1f * (float) std::sin(MathConstants<double>::twoPi * 110.0 * i / sr));
    {
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os = std::make_unique<FileOutputStream>(f);
        std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor(os.get(), sr, 1, 16, {}, 0) };
        if (w == nullptr) return;
        os.release();
        w->writeFromAudioSampleBuffer(buf, 0, n);
    }
    if (tag.isEmpty()) return;
    MemoryBlock d;
    if (! f.loadFileAsData(d)) return;
    MemoryOutputStream m;
    m.write(d.getData(), d.getSize());
    m.write("id3 ", 4);
    m.writeInt((int) tag.getSize());
    m.write(tag.getData(), tag.getSize());
    if (tag.getSize() & 1) m.writeByte(0);
    MemoryBlock out = m.getMemoryBlock();
    const uint32_t riff = (uint32_t) out.getSize() - 8;
    auto* b = (uint8_t*) out.getData();
    b[4] = (uint8_t) riff; b[5] = (uint8_t) (riff >> 8); b[6] = (uint8_t) (riff >> 16); b[7] = (uint8_t) (riff >> 24);
    f.replaceWithData(out.getData(), out.getSize());
}

void click(Component& c, Point<float> p, int clicks = 1)
{
    auto src = Desktop::getInstance().getMainMouseSource();
    auto now = Time::getCurrentTime();
    MouseEvent down(src, p, ModifierKeys(ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, &c, &c, now, p, now, clicks, false);
    MouseEvent up(src, p, ModifierKeys(), 1.0f, 0, 0, 0, 0, &c, &c, now, p, now, clicks, false);
    c.mouseDown(down);
    c.mouseUp(up);
}

bool save(Component& c, const File& out)
{
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    out.deleteFile();
    PNGImageFormat png;
    std::unique_ptr<OutputStream> os(out.createOutputStream());
    const bool ok = os != nullptr && png.writeImageToStream(img, *os);
    std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const File outDir = File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "library-shots");
    outDir.createDirectory();

    const File root = File::getSpecialLocation(File::tempDirectory).getChildFile("wheel_library_snapshot").getChildFile("MPC CARD");
    const File music = root.getChildFile("Music");
    const File house = music.getChildFile("House/DJ Test - Warehouse Jack.wav"), night = music.getChildFile("House/Late Shift - Deep Into The Night.wav"),
               boom = music.getChildFile("Hip Hop/DJ Test - Boom Bap 90.wav"), gold = music.getChildFile("Hip Hop/Crate Digger - Golden Era Loop.wav"),
               disco = music.getChildFile("Disco/The Edits - Disco Strut.wav"), chords = music.getChildFile("Keys/chords_am.wav");
    writeTrack(house, 372, id3("Warehouse Jack", "DJ Test", "126", "8A"));
    writeTrack(night, 401, id3("Deep Into The Night", "Late Shift", "122", "5A"));
    writeTrack(boom, 214, id3("Boom Bap 90", "DJ Test", "90", "4A"));
    writeTrack(gold, 188, id3("Golden Era Loop", "Crate Digger", "93.5", "11B"));
    writeTrack(disco, 330, id3("Disco Strut", "The Edits", "118", "2B"));
    writeTrack(chords, 40, {});
    const File sub = root.getChildFile("_Serato_/Subcrates");
    sub.createDirectory();
    auto crate = [&](const String& name, const StringArray& paths) { const auto mb = makeCrate(paths); sub.getChildFile(name + ".crate").replaceWithData(mb.getData(), mb.getSize()); };
    crate("House", { "Music/House/DJ Test - Warehouse Jack.wav", "Music/House/Late Shift - Deep Into The Night.wav", "Music/Keys/chords_am.wav", "Music/Disco/The Edits - Disco Strut.wav" });
    crate("Hip Hop", { "Music/Hip Hop/DJ Test - Boom Bap 90.wav", "Music/Hip Hop/Crate Digger - Golden Era Loop.wav" });
    crate("Hip Hop%%Boom Bap", { "Music/Hip Hop/DJ Test - Boom Bap 90.wav" });
    root.getChildFile("Playlists").createDirectory();
    root.getChildFile("Playlists/set.m3u8").replaceWithText("#EXTM3U\n../Music/House/DJ Test - Warehouse Jack.wav\n../Music/Keys/chords_am.wav\n");

    wheel::WheelProcessor proc;
    proc.setRateAndBufferSizeDetails(48000, 512);
    proc.prepareToPlay(48000, 512);
    AudioBuffer<float> block(4, 512);
    MidiBuffer mb;
    auto run = [&](double seconds) { for (int i = 0; i < (int) (seconds * 48000 / 512); ++i) { block.clear(); proc.processBlock(block, mb); } };
    auto pump = [&](int ms) { MessageManager::getInstance()->runDispatchLoopUntil(ms); };

    // the A / B marks: something on each deck
    proc.lastRoot = root;
    proc.library.setRoot(root);
    for (const auto& c : proc.library.cratesCopy()) std::printf("crate: %s (%d)\n", c.name.toRawUTF8(), c.depth);
    proc.loadTrack(0, house);
    proc.loadTrack(1, boom);
    for (int i = 0; i < 1200 && (proc.loading[0].load() || proc.loading[1].load() || ! proc.deckTrack(0) || ! proc.deckTrack(1)); ++i) { pump(50); run(.01); }
    if (! proc.deckTrack(0) || ! proc.deckTrack(1)) std::printf("warning: tracks did not load (%s / %s)\n", proc.loadError[0].toRawUTF8(), proc.loadError[1].toRawUTF8());

    std::printf("root after loading: %s\n", proc.library.root().getFullPathName().toRawUTF8());
    wheel::PhoneHost host;
    bool closed = false;
    host.closeLibrary = [&] { closed = true; };
    // the root first (the processor otherwise looks for an SD card under /media, /mnt... on its own)
    proc.lastRoot = root;
    proc.library.setRoot(root);
    wheel::PhoneLibraryView view(proc, host);
    view.setSize(2000, 933);
    view.setVisible(true);
    for (int i = 0; i < 100; ++i) pump(30);   // root, listing, crate counts

    // the crates and tracks lists are the last two children; open "House" (4th row) and pick the 2nd track
    auto* crates = view.getChildComponent(view.getNumChildComponents() - 2);
    auto* tracks = view.getChildComponent(view.getNumChildComponents() - 1);
    const float crateRow = 42 * 2.5f, trackRow = 44 * 2.5f;
    click(*crates, { 40.0f, crateRow * 3.5f });
    for (int i = 0; i < 30; ++i) pump(30);
    click(*tracks, { 300.0f, trackRow * 1.5f });
    pump(100);
    std::printf("selected: %s\n", view.selectedTitle().toRawUTF8());

    bool ok = save(view, outDir.getChildFile("library-2000x933.png"));
    view.setSize(1280, 600);
    pump(100);
    ok = save(view, outDir.getChildFile("library-1280x600.png")) && ok;
    // focus back on the crates (the phone's second shot)
    click(*crates, { 40.0f, 42 * 1.6f * 3.5f });
    for (int i = 0; i < 30; ++i) pump(30);
    ok = save(view, outDir.getChildFile("library-1280x600-crates.png")) && ok;
    // search: "disco" in All tracks
    click(*crates, { 40.0f, 42 * 1.6f * .5f });
    for (int i = 0; i < 20; ++i) pump(30);
    for (auto* c : view.getChildren())
        if (auto* te = dynamic_cast<TextEditor*>(c)) te->setText("disco 2b", true);
    for (int i = 0; i < 20; ++i) pump(30);
    ok = save(view, outDir.getChildFile("library-1280x600-search.png")) && ok;
    if (closed) std::printf("note: the view asked to close\n");
    proc.releaseResources();
    return ok ? 0 : 1;
}
