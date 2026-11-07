#include "CmiPlugin.h"
#include "CmiEditor.h"
#include "core/Disk.h"

namespace cmi {

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (const auto& d : paramDefs()) {
        const juce::String id(d.id);
        if (d.step == 1 && d.min == 0 && d.max == 1)
            l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(id, 1), d.name, d.def > 0.5f));
        else if (d.step == 1)
            l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID(id, 1), d.name, (int) d.min, (int) d.max, (int) d.def));
        else
            l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1), d.name, juce::NormalisableRange<float>(d.min, d.max), d.def));
    }
    return l;
}

CmiProcessor::CmiProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "CMI", makeLayout()) {
    formats.registerBasicFormats();
    for (const auto& d : paramDefs()) raw.push_back(apvts.getRawParameterValue(d.id));
    engine.bank = &kit.bank;
    engine.kitVoice = cmiKitLookup(kit.bank);
    Library::factorySeq(engine.seq, kit.bank);   // its voices come from the library folder (connectDefaults)
    resetCCMap();
    {   // settings every instance shares: the library folder
        juce::PropertiesFile::Options o; o.applicationName = "Omega CMI"; o.filenameSuffix = ".settings"; o.folderName = "Omega CMI"; o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile>(o);
        const juce::File sf(settings->getValue("songFolder"));
        if (sf.isDirectory()) { songDir = sf; rescanSongs(); }
        for (auto& tok : juce::StringArray::fromTokens(settings->getValue("presets.choice"), ";", ""))
            if (tok.contains("=")) presetChoice[tok.upToFirstOccurrenceOf("=", false, false).getIntValue()] = tok.fromFirstOccurrenceOf("=", false, false).toStdString();
        const juce::File f(settings->getValue("libraryFolder"));
        if (f.isDirectory()) { libFolder = f; rescanLibrary(); }
    }
    for (auto& v : ccVal) v = 0;
    ccDirty[0] = ccDirty[1] = 0;
    startTimerHz(40);
}

bool CmiProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void CmiProcessor::prepareToPlay(double sr, int maxBlock) {
    engine.prepare(sr);
    bufL.assign((size_t) std::max(maxBlock, 512), 0.f); bufR = bufL;
    setLatencySamples(BL_HALF);
}

Controls CmiProcessor::controlsNow() const {
    Controls c;
    c.level = get("level"); c.filter = get("filter"); c.attack = get("attack"); c.damp = get("damp"); c.sustain = get("sustain");
    c.vibDepth = get("vib"); c.vibRate = get("vibRate"); c.vibDelay = get("vibDelay"); c.porta = get("porta");
    c.velVol = get("velVol"); c.velFilt = get("velFilt"); c.transpose = get("transpose"); c.fine = get("fine");
    c.loop = get("loop"); c.segTime = get("seg");
    return c;
}

void CmiProcessor::set(const char* id, float v) {
    if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
}

void CmiProcessor::loadSound(int index) {
    const Sound* s = kit.bank.at(index);
    if (!s) return;
    const Controls& c = s->ctl;
    set("sound", (float) index); curPreset = -1;
    set("level", c.level); set("filter", c.filter); set("attack", c.attack); set("damp", c.damp); set("sustain", c.sustain);
    set("vib", c.vibDepth); set("vibRate", c.vibRate); set("vibDelay", c.vibDelay); set("porta", c.porta);
    set("velVol", c.velVol); set("velFilt", c.velFilt); set("transpose", c.transpose); set("fine", c.fine);
    set("loop", c.loop); set("seg", c.segTime);
}

void CmiProcessor::setCurrentProgram(int i) { loadPreset(i); }

const juce::String CmiProcessor::getProgramName(int i) {
    const auto& ps = factoryPresets();
    if (i < 0 || i >= (int) ps.size()) return {};
    return juce::String(i + 1).paddedLeft('0', 3) + " " + ps[(size_t) i].name + " (" + ps[(size_t) i].file + ")";
}

void CmiProcessor::rebuildPresets() {
    const juce::ScopedLock l(catLock);
    const int n = (int) factoryPresets().size();
    std::vector<int> m((size_t) n, -1);
    if (!catalog.voices.empty())
        for (int i = 0; i < n; ++i) { auto it = presetChoice.find(i); m[(size_t) i] = findFactory(catalog, i, it == presetChoice.end() ? "" : it->second); }
    presetCat = std::move(m);
}
bool CmiProcessor::loadPreset(int n) {
    int v = -1;
    {
        const juce::ScopedLock l(catLock);
        if (n < 0 || n >= (int) presetCat.size() || presetCat[(size_t) n] < 0) return false;
        v = cmi::loadCatalogVoice(kit, catalog, presetCat[(size_t) n]);
    }
    if (v < 0) return false;
    loadSound(v); curPreset = n; program = n;
    return true;
}
void CmiProcessor::choosePresetVoice(int n, int ci) {
    {
        const juce::ScopedLock l(catLock);
        if (ci >= 0 && ci < (int) catalog.voices.size()) presetChoice[n] = catalog.voices[(size_t) ci].name + "@" + catalog.voices[(size_t) ci].disk;
        else presetChoice.erase(n);
    }
    if (settings) { juce::String v; for (auto& kv : presetChoice) v << kv.first << "=" << juce::String(kv.second) << ";"; settings->setValue("presets.choice", v); settings->saveIfNeeded(); }
    rebuildPresets();
}

// Page R's tracks and the keyboard get their CMI voices from the library when they have none
void CmiProcessor::connectDefaults() {
    const juce::ScopedLock l(catLock);
    connectPageR(engine.seq, kit, catalog);
    if (isBlank(kit, juce::roundToInt(get("sound")))) {
        int v = -1;
        for (const auto& w : splitBar(defaultKeyboardVoices())) if ((v = findVoice(kit.bank, w)) >= 0) break;
        if (v < 0) v = loadCatalogNamed(kit, catalog, defaultKeyboardVoices());
        if (v >= 0) loadSound(v);
    }
}

int CmiProcessor::loadCatalogVoice(int ci) {
    const juce::ScopedLock l(catLock);
    return cmi::loadCatalogVoice(kit, catalog, ci);
}

// one file as a CMI voice: its own root key and loop when the file has them (WAV smpl chunk)
juce::String CmiProcessor::makeSound(const juce::File& f, int root, bool loop, Sound& out) {
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
    if (!r) return "Can't read " + f.getFileName();
    const int64_t maxLen = (int64_t) (r->sampleRate * 12);
    const int n = (int) std::min<int64_t>(r->lengthInSamples, maxLen);
    if (n <= 0) return "Empty file " + f.getFileName();
    juce::AudioBuffer<float> b((int) r->numChannels, n);
    r->read(&b, 0, n, 0, true, true);
    std::vector<float> mono((size_t) n, 0.f);
    for (int c = 0; c < b.getNumChannels(); ++c) { auto* p = b.getReadPointer(c); for (int i = 0; i < n; ++i) mono[(size_t) i] += p[i] / (float) b.getNumChannels(); }
    const auto& md = r->metadataValues;
    const int unity = md.getValue("MidiUnityNote", "-1").getIntValue();
    if (unity > 0 && unity < 128) root = unity;
    long ls = -1, le = -1;
    if (md.getValue("NumSampleLoops", "0").getIntValue() > 0) { ls = md.getValue("Loop0Start", "-1").getLargeIntValue(); le = md.getValue("Loop0End", "-1").getLargeIntValue(); }
    out = sampleToCmi(mono, r->sampleRate, f.getFileNameWithoutExtension().toStdString(), root, loop, ls, le);
    return {};
}

static bool isDiskImage(const juce::File& f) { return f.hasFileExtension("imd;img;bin;raw;dsk;hfe"); }

// every sound in a file: a CMI disk image (all its voices), a .VC voice file, or any audio file
juce::String CmiProcessor::soundsFrom(const juce::File& f, int root, bool loop, std::vector<Sound>& out) {
    if (isDiskImage(f) || f.hasFileExtension("vc")) {
        juce::MemoryBlock mb;
        if (!f.loadFileAsData(mb)) return "Can't read " + f.getFileName();
        const auto* d = (const uint8_t*) mb.getData();
        if (f.hasFileExtension("vc")) {
            Sound s; std::string err;
            if (!vcToSound(std::vector<uint8_t>(d, d + mb.getSize()), f.getFileNameWithoutExtension().toStdString(), f.getParentDirectory().getFileName().toStdString(), s, err))
                return juce::String(err);
            out.push_back(std::move(s));
            return {};
        }
        Disk dk = readDiskImage(d, mb.getSize());
        if (!dk.err.empty()) return f.getFileName() + ": " + juce::String(dk.err);
        const std::string label = dk.label.empty() ? f.getFileNameWithoutExtension().toStdString() : dk.label;
        for (const auto& df : dk.files) {
            if (df.type != "VC") continue;
            Sound s; std::string err;
            if (vcToSound(df.data, df.name, label, s, err)) out.push_back(std::move(s));
        }
        return out.empty() ? f.getFileName() + ": no voices on this disk" : juce::String();
    }
    Sound s;
    const auto err = makeSound(f, root, loop, s);
    if (err.isEmpty()) out.push_back(std::move(s));
    return err;
}

juce::String CmiProcessor::loadSample(const juce::File& f, int slot, int root, bool loop) {
    std::vector<Sound> got;
    const auto err = soundsFrom(f, root, loop, got);
    if (got.empty()) return err.isEmpty() ? "Nothing to load" : err;
    // a disk fills this slot and the free ones after it
    int k = slot, first = slot;
    for (auto& s : got) {
        if (k < 0 || k >= Library::USER_SLOTS) break;
        kit.setUser(k, std::move(s));
        k = kit.firstFree(k + 1);
    }
    loadSound(kit.userSlot(first));
    return {};
}

void CmiProcessor::setSongFolder(const juce::File& f) {
    { const juce::ScopedLock l(infoLock); songDir = f; }
    if (settings) { settings->setValue("songFolder", f.getFullPathName()); settings->saveIfNeeded(); }
    rescanSongs();
}
void CmiProcessor::rescanSongs() {
    std::vector<juce::File> found;
    const juce::File dir = songFolder();
    if (dir.isDirectory())
        for (const auto& e : juce::RangedDirectoryIterator(dir, true, "*.mid;*.midi;*.kar;*.smf", juce::File::findFiles)) { found.push_back(e.getFile()); if (found.size() > 10000) break; }
    std::sort(found.begin(), found.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
    const juce::ScopedLock l(infoLock); songList = std::move(found);
}
int CmiProcessor::loadedSongFor(const juce::File& f) const {
    const std::string k = songKey(f.getFileNameWithoutExtension().toUpperCase().replaceCharacters("_-", "  ").toStdString());
    for (int i = 0; i < songs.size(); ++i) if (const SongEntry* e = songs.at(i)) if (songKey(e->song->name) == k) return i;
    return -1;
}

void CmiProcessor::setLibraryFolder(const juce::File& f) {
    { const juce::ScopedLock l(infoLock); libFolder = f; }
    if (settings) { settings->setValue("libraryFolder", f.getFullPathName()); settings->saveIfNeeded(); }
    rescanLibrary();
}

void CmiProcessor::rescanLibrary() {
    std::vector<juce::File> found;
    const juce::File dir = libraryFolder();
    if (dir.isDirectory())
        for (const auto& e : juce::RangedDirectoryIterator(dir, true, "*.mid;*.midi;*.zip;*.imd;*.img;*.bin;*.raw;*.dsk;*.vc;*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg", juce::File::findFiles)) {
            found.push_back(e.getFile());
            if (found.size() > 5000) break;
        }
    std::sort(found.begin(), found.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
    // the catalogue: every CMI voice on the disks (and .VC files) there
    Catalog cat;
    for (int pass = 0; pass < 2; ++pass)   // disk images first: their labels name the voices' disks
        for (const auto& f : found) {
            if (!f.hasFileExtension(pass == 0 ? "imd;img;bin;raw;dsk" : "vc")) continue;
            juce::MemoryBlock mb; if (!f.loadFileAsData(mb)) continue;
            juce::File dirF = f.getParentDirectory(); if (dirF.getFileName().equalsIgnoreCase("VCs")) dirF = dirF.getParentDirectory();
            cat.add(f.getFileName().toStdString(), (const uint8_t*) mb.getData(), mb.getSize(), dirF.getFileName().toUpperCase().toStdString());
        }
    { const juce::ScopedLock l(catLock); catalog = std::move(cat); }
    { const juce::ScopedLock l(infoLock); libFiles = std::move(found); }
    rebuildPresets();
    connectDefaults();
}

// ---- the drum kit ----
std::vector<int> CmiProcessor::kitNotes(bool all) const {
    if (all) { std::vector<int> o; for (int n = 35; n <= 81; ++n) o.push_back(n); return o; }
    const SongEntry* e = songs.at(currentSong()); if (!e) return {};
    return kitNotesOf(*e);
}
KitPiece CmiProcessor::kitPiece(int note) const { const SongEntry* e = songs.at(currentSong()); return e && note >= 0 && note < 128 ? e->kit.p[(size_t) note] : KitPiece(); }
void CmiProcessor::setKitPiece(int note, const KitPiece& p) {
    SongEntry* e = songs.at(currentSong()); if (!e || note < 0 || note > 127) return;
    e->kit.p[(size_t) note] = p;
    applyKit(engine, e->kit);
    remember(*e->song, -1, e->kit.save());
}
void CmiProcessor::setKitVoice(int note, int v) {
    KitPiece p = kitPiece(note);
    const Sound* s = v >= 0 && !isBlank(kit, v) ? kit.bank.at(v) : nullptr;
    p.voice = s ? s->name + "@" + s->category : std::string();
    setKitPiece(note, p); auditionKit(note);
}
void CmiProcessor::setKitCatalogVoice(int note, int ci) {
    int v; { const juce::ScopedLock l(catLock); v = cmi::loadCatalogVoice(kit, catalog, ci); }
    if (v >= 0) setKitVoice(note, v);
}
void CmiProcessor::auditionKit(int note) {
    for (int t = 0; t < SONG_TRK; ++t) if (engine.songVoice[t] == GM_KIT) { audition(t, note); return; }
}
void CmiProcessor::resetKit() { SongEntry* e = songs.at(currentSong()); if (!e) return; e->kit = DrumKit(); applyKit(engine, e->kit); remember(*e->song, -1, ""); }

// ---- recording: the keyboard (and MIDI in) onto a song track, with a count-in and the click ----
juce::String CmiProcessor::startRecording(int track) {
    if (currentSong() < 0) Recorder::newSong(songs, engine, kit.bank, get("tempo"));
    set("sync", 0.f);
    return recorder.start(engine, songs.at(currentSong()), track, recFromBeat);
}
void CmiProcessor::pollRecording() { recorder.poll(engine); }
juce::String CmiProcessor::stopRecording() { return recorder.stop(engine, songs.at(currentSong())); }

// ---- the light pen ----
void CmiProcessor::penChanged() {
    if (!penLive) return;
    penSlot = penToSlot(kit, pen, penSlot);
    if (penSlot >= 0) loadSound(kit.userSlot(penSlot));
}
juce::String CmiProcessor::penSaveAs() {
    std::string name; const int slot = cmi::penSaveAs(kit, pen, name);
    return slot < 0 ? juce::String("USER VOICES FULL") : juce::String(name) + " SAVED (VOICE " + juce::String(slot + 1) + ")";
}
juce::String CmiProcessor::penExport(const juce::File& f) {
    const auto b = voiceWav(pen.voice(f.getFileNameWithoutExtension().toUpperCase().toStdString()));
    return f.replaceWithData(b.data(), b.size()) ? "EXPORTED " + f.getFileName() : "CAN'T WRITE " + f.getFileName();
}
bool CmiProcessor::penFrom(int v) { const Sound* s = kit.bank.at(v); if (!s || isBlank(kit, v)) return false; pen.fromSound(*s); return true; }

juce::String CmiProcessor::saveDrawnVoice(const float* start, const float* end, bool asNew) {
    int slot = asNew || lastDrawn < 0 || !kit.userLoaded(lastDrawn) ? kit.firstFree() : lastDrawn;
    if (slot < 0) return "USER VOICES FULL";
    int n = 1; for (int k = 0; k < Library::USER_SLOTS; ++k) if (kit.userLoaded(k) && kit.user(k)->name.rfind("DRAW", 0) == 0) ++n;
    const std::string name = slot == lastDrawn && kit.userLoaded(slot) ? kit.user(slot)->name : "DRAW" + std::to_string(n);
    kit.setUser(slot, drawnVoice(name, start, end));
    lastDrawn = slot;
    loadSound(kit.userSlot(slot));
    return {};
}

juce::String CmiProcessor::importMidi(const juce::File& f) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) return "Can't read " + f.getFileName();
    MidiSong ms; std::string err;
    juce::String name = f.getFileNameWithoutExtension().toUpperCase().replaceCharacters("_-", "  ");
    if (!parseSmf((const uint8_t*) mb.getData(), mb.getSize(), name.toStdString(), ms, err)) return f.getFileName() + ": " + juce::String(err);
    int dup = 0;
    const int i = songs.addUnique(std::move(ms), kit.bank, dup);
    if (dup == 1) { ++dupSkipped; return {}; }
    connectToLibrary(i);   // the original CMI voices, when a library is set
    chooseSong(i);
    return {};
}

void CmiProcessor::chooseSong(int i) { selectSong(engine, songs.at(i)); }

// what you chose for each track of a song, kept with the settings every project shares: songKey -> "t=NAME@DISK;..."
std::map<int, std::string> CmiProcessor::rememberedFor(const MidiSong& s) const {
    std::map<int, std::string> m;
    if (!settings) return m;
    for (auto& tok : juce::StringArray::fromTokens(settings->getValue("voices." + juce::String(songKey(s.name))), ";", ""))
        if (tok.contains("=")) m[tok.upToFirstOccurrenceOf("=", false, false).getIntValue()] = tok.fromFirstOccurrenceOf("=", false, false).toStdString();
    return m;
}
void CmiProcessor::remember(const MidiSong& s, int track, const std::string& what) {
    if (!settings) return;
    auto m = rememberedFor(s); m[track] = what;
    juce::String v; for (auto& kv : m) v << kv.first << "=" << juce::String(kv.second) << ";";
    settings->setValue("voices." + juce::String(songKey(s.name)), v); settings->saveIfNeeded();
}
void CmiProcessor::setSongVoice(int track, int voice) {
    SongEntry* e = songs.at(currentSong()); if (!e) return;
    e->voice[track] = voice; engine.songVoice[track] = voice;
    const Sound* snd = kit.bank.at(voice);
    if (voice == GM_KIT) remember(*e->song, track, "KIT");
    else if (snd && !isBlank(kit, voice)) remember(*e->song, track, snd->name + "@" + snd->category);
}
int CmiProcessor::connectToLibrary(int si) {
    SongEntry* e = songs.at(si); if (!e) return 0;
    const juce::ScopedLock l(catLock);
    if (catalog.voices.empty()) return 0;
    const auto rem = rememberedFor(*e->song);
    const int n = connectSong(*e, kit, catalog, rem);
    if (auto it = rem.find(-1); it != rem.end()) { e->kit.load(it->second); loadKitVoices(e->kit, kit, catalog); }   // the kit as you left it
    if (si == currentSong()) chooseSong(si);
    return n;
}
void CmiProcessor::useCatalogVoice(int track, int ci) {
    SongEntry* e = songs.at(currentSong()); if (!e) return;
    const juce::ScopedLock l(catLock);
    if (ci < 0 || ci >= (int) catalog.voices.size()) return;
    const CatVoice& cv = catalog.voices[(size_t) ci];
    const int v = cmi::loadCatalogVoice(kit, catalog, ci);
    if (v < 0) return;
    e->voice[track] = v; engine.songVoice[track] = v; e->why[track] = cv.name + " (" + cv.disk + "): your choice";
    remember(*e->song, track, cv.name + "@" + cv.disk.substr(0, 10));
}

// files and folders (searched inside) into the free user voices, in name order
juce::String CmiProcessor::importFiles(const juce::Array<juce::File>& items, int root, bool loop, int& imported) {
    juce::Array<juce::File> files;
    const juce::String exts = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.vc;*.imd;*.img;*.bin;*.raw;*.dsk;*.hfe;*.mid;*.midi;*.zip";
    // zip files: unpacked into a temporary folder, then everything in them comes in like a folder
    std::vector<std::unique_ptr<juce::TemporaryFile>> unzipped;
    std::function<void(const juce::File&)> addItem = [&](const juce::File& it) {
        if (it.isDirectory()) { for (const auto& e : juce::RangedDirectoryIterator(it, true, exts, juce::File::findFiles)) addItem(e.getFile()); return; }
        if (!it.existsAsFile()) return;
        if (it.hasFileExtension("zip")) {
            juce::ZipFile z(it);
            auto tmp = std::make_unique<juce::TemporaryFile>(juce::String(), juce::TemporaryFile::useHiddenFile);
            const juce::File dir = tmp->getFile(); dir.createDirectory();
            if (z.uncompressTo(dir, true).wasOk()) for (const auto& e : juce::RangedDirectoryIterator(dir, true, exts, juce::File::findFiles)) if (!e.getFile().hasFileExtension("zip")) files.add(e.getFile());
            unzipped.push_back(std::move(tmp));
            return;
        }
        files.add(it);
    };
    for (const auto& it : items) addItem(it);
    struct ByName { static int compareElements(const juce::File& a, const juce::File& b) { return a.getFullPathName().compareNatural(b.getFullPathName()); } } cmp;
    files.sort(cmp);
    imported = 0; int failed = 0, first = -1, disks = 0, midis = 0; juce::String lastErr;
    dupSkipped = 0;
    for (const auto& f : files) {
        if (f.hasFileExtension("mid;midi")) { const auto e = importMidi(f); if (e.isEmpty()) ++midis; else { ++failed; lastErr = e; } continue; }
        std::vector<Sound> got;
        const auto err = soundsFrom(f, root, loop, got);
        if (got.empty()) { ++failed; lastErr = err; continue; }
        if (isDiskImage(f)) ++disks;
        for (auto& s : got) {
            const int slot = kit.firstFree();
            if (slot < 0) { if (first >= 0) loadSound(kit.userSlot(first)); return juce::String(imported) + " IMPORTED, USER VOICES FULL"; }
            kit.setUser(slot, std::move(s));
            if (first < 0) first = slot;
            ++imported;
        }
    }
    if (first >= 0) loadSound(kit.userSlot(first));
    connectDefaults();   // Page R's empty tracks: the CMI voices just loaded
    if (files.isEmpty()) return "NO SOUNDS FOUND";
    if (midis && !imported && !failed) return juce::String(midis - dupSkipped) + (midis - dupSkipped == 1 ? " SONG" : " SONGS") + " IMPORTED (PAGE S)" + (dupSkipped ? ", " + juce::String(dupSkipped) + " DUPLICATES SKIPPED" : juce::String());
    juce::String m = juce::String(imported) + " IMPORTED";
    if (disks) m << " FROM " << disks << (disks == 1 ? " DISK" : " DISKS");
    if (midis) m = (imported ? m + ", " : juce::String()) + juce::String(midis - dupSkipped) + (midis - dupSkipped == 1 ? " SONG" : " SONGS") + " (PAGE S)" + (dupSkipped ? ", " + juce::String(dupSkipped) + " DUPLICATES SKIPPED" : juce::String());
    if (failed) m << ", " << failed << " UNREADABLE" << (failed == 1 && lastErr.isNotEmpty() ? " (" + lastErr.toUpperCase() + ")" : juce::String());
    return m;
}

void CmiProcessor::resetCCMap() {
    for (auto& c : ccMap) c = -1;
    for (const auto& d : defaultCCs()) ccMap[(size_t) d.first] = paramIndex(d.second);
}

// controllers and program changes from the audio thread, applied here (parameters notify the host)
void CmiProcessor::timerCallback() {
    if (recording.load()) pollRecording();
    const int p = pendingProgram.exchange(-1);
    if (p >= 0 && !loadPreset(p)) {   // program n: factory preset n; not in your library: the n-th voice loaded
        // the voice list: the voices loaded (from your library and your samples); bank select (CC 0) pages through it
        int n = 0, pick = -1;
        for (int i = 0; i < kit.bank.count.load() && pick < 0; ++i) {
            if (isBlank(kit, i)) continue;
            if (n++ == p) pick = i;
        }
        if (pick >= 0) loadSound(pick);
    }
    for (int w = 0; w < 2; ++w) {
        uint64_t bits = ccDirty[w].exchange(0);
        for (int b = 0; b < 64 && bits; ++b) {
            if (!(bits & (1ull << b))) continue;
            bits &= ~(1ull << b);
            const int cc = w * 64 + b, pi = ccMap[(size_t) cc].load();
            if (pi < 0 || pi >= (int) paramDefs().size()) continue;
            const PDef& d = paramDefs()[(size_t) pi];
            const float u = ccVal[(size_t) cc].load();
            float v = d.min + u * (d.max - d.min);
            if (d.step == 1 && d.min == 0 && d.max == 1) v = u >= 0.5f ? 1.f : 0.f; else if (d.step == 1) v = std::round(v);
            set(d.id, v);
        }
    }
}

void CmiProcessor::exportAudio(const juce::File& f, bool stems, std::function<void(juce::String)> done) {
    if (exporting.exchange(true)) return;
    const int si = currentSong();
    const double fs = std::max(44100.0, getSampleRate() > 0 ? getSampleRate() : 48000.0);
    const double master = get("master"), hiss = get("hiss"), spread = get("spread"), bpm = get("tempo");
    const SeqData seq = engine.seq;
    const Engine::FxSettings fxs = engine.fxSettings();   // the REV7 as it is set now
    pool.addJob([this, f, stems, si, fs, master, hiss, spread, bpm, seq, fxs, done] {
        juce::String msg;
        const SongEntry* e = songs.at(si);
        auto write = [&](const juce::File& out, const Rendered& r) { const auto b = wavBytes(r, 24); return out.replaceWithData(b.data(), b.size()); };
        if (!e) {
            msg = write(f, renderPageR(kit.bank, seq, bpm, fs, master, hiss, spread, loopBlendNow(), &fxs)) ? "EXPORTED " + f.getFileName() : "CAN'T WRITE " + f.getFileName();
        } else if (!stems) {
            msg = write(f, renderSong(kit.bank, *e, fs, -1, master, hiss, spread, loopBlendNow(), &fxs)) ? "EXPORTED " + f.getFileName() : "CAN'T WRITE " + f.getFileName();
        } else {
            int n = 0;
            for (int t = 0; t < SONG_TRK; ++t) {
                if (!e->song->used[t] || e->mute[t]) continue;
                juce::String nm = juce::String(e->song->trackName[t]).retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-").trim();
                const juce::File out = f.getSiblingFile(f.getFileNameWithoutExtension() + " " + juce::String(t + 1).paddedLeft('0', 2) + (nm.isEmpty() ? "" : " " + nm) + ".wav");
                n += write(out, renderSong(kit.bank, *e, fs, t, master, hiss, spread, loopBlendNow(), &fxs)) ? 1 : 0;
            }
            msg = "EXPORTED " + juce::String(n) + " STEMS";
        }
        exporting = false;
        juce::MessageManager::callAsync([done, msg] { if (done) done(msg); });
    });
}

void CmiProcessor::exportMidi(const juce::File& f, std::function<void(juce::String)> done) {
    const SongEntry* e = songs.at(currentSong());
    if (!e) { if (done) done("NO SONG SELECTED"); return; }
    std::string names[SONG_TRK];
    for (int t = 0; t < SONG_TRK; ++t) { const Sound* s = kit.bank.at(e->voice[t]); names[t] = e->voice[t] == GM_KIT ? "DRUM KIT" : s ? s->name : ""; }
    const auto b = writeSmf(*e->song, names);
    if (done) done(f.replaceWithData(b.data(), b.size()) ? "EXPORTED " + f.getFileName() : "CAN'T WRITE " + f.getFileName());
}

void CmiProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    keyboard.processNextMidiBuffer(midi, 0, n, true);

    engine.setKeyboard(juce::roundToInt(get("sound")), controlsNow());
    if (const int a = auditionReq.exchange(-1); a >= 0) {
        if (auditionOn >= 0) engine.songNote(auditionOn >> 8, auditionOn & 0x7F, 0);
        engine.songNote(a >> 8, a & 0x7F, 100); auditionOn = a; auditionLeft = (int64_t) (getSampleRate() * 0.35);
    } else if (auditionOn >= 0 && (auditionLeft -= n) <= 0) { engine.songNote(auditionOn >> 8, auditionOn & 0x7F, 0); auditionOn = -1; }
    engine.setPoly(juce::roundToInt(get("poly")));
    engine.setBendRange(get("bend"));
    engine.setOutput(get("master"), get("hiss"), get("spread"));
    engine.loopBlend = loopBlendNow();
    engine.setMultiTimbral(get("multi") > 0.5f);
    engine.extSync = get("sync") > 0.5f;
    engine.tempo = get("tempo");
    engine.seq.swing = get("swing");
    engine.songMode = get("songMode") > 0.5f;
    engine.songGain = get("songLevel");

    const int pv = previewReq.exchange(-1);
    if (pv >= 0) engine.trackNote(pv, 0, 0.9f);

    Transport tp;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            tp.valid = true; tp.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) tp.ppq = *q;
            if (auto b = pos->getBpm()) tp.bpm = *b;
        }
    }

    if ((int) bufL.size() < n) { bufL.resize((size_t) n); bufR.resize((size_t) n); }
    int done = 0;
    const double spq = tp.bpm / 60.0 / getSampleRate();
    auto renderTo = [&](int upTo) {
        if (upTo <= done) return;
        Transport t2 = tp; t2.ppq = tp.ppq + done * spq;
        engine.render(bufL.data() + done, bufR.data() + done, upTo - done, &t2);
        done = upTo;
    };
    for (const auto m : midi) {
        renderTo(juce::jlimit(0, n, m.samplePosition));
        const auto msg = m.getMessage();
        if (msg.isProgramChange()) { pendingProgram = bankMsb.load() * 128 + msg.getProgramChangeNumber(); continue; }
        if (msg.isController()) {
            const int cc = msg.getControllerNumber(), v = msg.getControllerValue();
            if (cc == 0) { bankMsb = v; continue; }
            lastCC = cc;
            const int learn = learnParam.load();
            if (learn >= 0) { for (auto& m : ccMap) if (m.load() == learn) m = -1; ccMap[(size_t) cc] = learn; learnParam = -1; }
            if (ccMap[(size_t) cc].load() >= 0) { ccVal[(size_t) cc] = v / 127.f; ccDirty[cc >> 6].fetch_or(1ull << (cc & 63)); continue; }
        }
        // MIDI start / continue / stop: the selected song, or Page R
        if (msg.isMidiStart() || msg.isMidiContinue()) { if (engine.song.load()) engine.songRun = true; else engine.seqRun = true; continue; }
        if (msg.isMidiStop()) { engine.songRun = false; engine.seqRun = false; continue; }
        engine.midi(msg.getRawData(), msg.getRawDataSize());
    }
    renderTo(n);
    midi.clear();

    float pk = 0;
    if (buffer.getNumChannels() >= 2) {
        buffer.copyFrom(0, 0, bufL.data(), n); buffer.copyFrom(1, 0, bufR.data(), n);
        for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
    } else if (buffer.getNumChannels() == 1) {
        auto* o = buffer.getWritePointer(0); for (int i = 0; i < n; ++i) o[i] = 0.5f * (bufL[(size_t) i] + bufR[(size_t) i]);
    }
    for (int i = 0; i < n; ++i) pk = std::max(pk, std::abs(bufL[(size_t) i]));
    meter = std::max(pk, meter.load() * 0.9f);
}

// ---- state: parameters, Page R, user samples ----
void CmiProcessor::writeSeq(juce::XmlElement& x) const {
    auto* s = x.createNewChildElement("PageR");
    s->setAttribute("songLen", engine.seq.songLen);
    juce::String song; for (int i = 0; i < engine.seq.songLen; ++i) song << engine.seq.song[i] << " ";
    s->setAttribute("song", song.trim());
    s->setAttribute("pattern", engine.curPattern.load());
    for (int t = 0; t < TRACKS; ++t) {
        auto* tr = s->createNewChildElement("Track");
        const Sound* snd = kit.bank.at(engine.seq.trk[t].sound);
        tr->setAttribute("sound", snd ? juce::String(snd->name) : juce::String());
        tr->setAttribute("index", engine.seq.trk[t].sound);
        tr->setAttribute("level", engine.seq.trk[t].level);
        tr->setAttribute("audible", engine.seq.trk[t].audible);
        tr->setAttribute("pan", engine.seq.trk[t].pan);
    }
    juce::MemoryBlock mb(&engine.seq.pat[0], sizeof(engine.seq.pat));
    s->setAttribute("patterns", mb.toBase64Encoding());
}

void CmiProcessor::readSeq(const juce::XmlElement& x) {
    const auto* s = x.getChildByName("PageR");
    if (!s) return;
    auto& q = engine.seq;
    q.songLen = juce::jlimit(1, SONG_MAX, s->getIntAttribute("songLen", 1));
    auto toks = juce::StringArray::fromTokens(s->getStringAttribute("song"), " ", "");
    for (int i = 0; i < q.songLen; ++i) q.song[i] = juce::jlimit(0, NPAT - 1, i < toks.size() ? toks[i].getIntValue() : 0);
    engine.curPattern = juce::jlimit(0, NPAT - 1, s->getIntAttribute("pattern", 0));
    int t = 0;
    for (auto* tr : s->getChildWithTagNameIterator("Track")) {
        if (t >= TRACKS) break;
        int idx = findVoice(kit.bank, tr->getStringAttribute("sound").toStdString());
        if (idx < 0) { const int vi = tr->getIntAttribute("index", 0); idx = juce::jlimit(0, kit.bank.count.load() - 1, oldNumbers ? Library::fromOldIndex(vi) : vi); }
        q.trk[t].sound = idx;
        q.trk[t].level = (float) tr->getDoubleAttribute("level", 0.7);
        q.trk[t].audible = tr->getBoolAttribute("audible", true);
        q.trk[t].pan = (float) tr->getDoubleAttribute("pan", 0.5);
        ++t;
    }
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding(s->getStringAttribute("patterns")) && mb.getSize() == sizeof(q.pat)) std::memcpy(&q.pat[0], mb.getData(), sizeof(q.pat));
}

void CmiProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = apvts.copyState();
    std::unique_ptr<juce::XmlElement> x(st.createXml());
    x->setAttribute("program", program);
    x->setAttribute("showCatalog", showCatalog.load());
    x->setAttribute("voiceNumbers", 2);
    if (!pen.silent()) { x->setAttribute("pen", juce::String(pen.save())); x->setAttribute("penSlot", penSlot); }
    x->setAttribute("click", engine.clickOn.load()); x->setAttribute("countIn", recCountIn); x->setAttribute("recQuant", rec.quant); x->setAttribute("recReplace", rec.replace);
    { juce::String m; for (int c = 0; c < 128; ++c) if (ccMap[(size_t) c].load() >= 0) m << c << ":" << paramDefs()[(size_t) ccMap[(size_t) c].load()].id << " "; x->setAttribute("ccMap", m.trim()); }
    const Sound* cur = kit.bank.at(juce::roundToInt(get("sound")));
    if (cur) x->setAttribute("soundName", juce::String(cur->name));
    writeSeq(*x);
    for (int k = 0; k < Library::USER_SLOTS; ++k) {
        if (!kit.userLoaded(k)) continue;
        const Sound* s = kit.user(k);
        auto* u = x->createNewChildElement("User");
        u->setAttribute("slot", k); u->setAttribute("name", juce::String(s->name)); u->setAttribute("title", juce::String(s->title));
        u->setAttribute("rate", s->rate); u->setAttribute("root", s->root); u->setAttribute("mode", (int) s->mode); u->setAttribute("category", juce::String(s->category));
        u->setAttribute("loopStart", s->loopStart); u->setAttribute("loopEnd", s->loopEnd);
        u->setAttribute("ram", juce::MemoryBlock(s->ram.data(), s->ram.size()).toBase64Encoding());
    }
    auto* sg = x->createNewChildElement("Songs");
    sg->setAttribute("selected", currentSong());
    for (int i = 0; i < songs.size(); ++i) {
        const SongEntry* e = songs.at(i); if (!e) continue;
        auto* el = sg->createNewChildElement("Song");
        el->setAttribute("index", i); el->setAttribute("name", juce::String(e->song->name));
        juce::String v, m; for (int t = 0; t < SONG_TRK; ++t) { v << e->voice[t] << " "; m << (e->mute[t] ? 1 : 0) << " "; }
        el->setAttribute("voices", v.trim()); el->setAttribute("mutes", m.trim());
        if (!e->kit.empty()) el->setAttribute("kit", juce::String(e->kit.save()));
        el->setAttribute("midi", juce::MemoryBlock(e->song->raw.data(), e->song->raw.size()).toBase64Encoding());
    }
    {
        auto* mx = x->createNewChildElement("Mixer");
        juce::String fd, pn, so, mu;
        for (int i = 0; i < Engine::STRIPS; ++i) { fd << engine.fader[(size_t) i].load() << " "; pn << engine.panOver[(size_t) i].load() << " "; so << (engine.solo[(size_t) i].load() ? 1 : 0) << " "; mu << (engine.mute[(size_t) i].load() ? 1 : 0) << " "; }
        mx->setAttribute("faders", fd.trim()); mx->setAttribute("pans", pn.trim()); mx->setAttribute("solos", so.trim()); mx->setAttribute("mutes", mu.trim());
        // the effects section: the REV7 and the sends to it
        auto* fx = x->createNewChildElement("Effects");
        juce::String sd; for (int i = 0; i < Engine::STRIPS; ++i) sd << engine.fxSend[(size_t) i].load() << " ";
        fx->setAttribute("on", engine.fxOn.load()); fx->setAttribute("sends", sd.trim()); fx->setAttribute("rev7", juce::String(engine.fx.save()));
    }
    copyXmlToBinary(*x, dest);
}

void CmiProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(data, size));
    if (!x) return;
    for (int k = 0; k < Library::USER_SLOTS; ++k) kit.clearUser(k);   // the project's own voices, in their own places
    oldNumbers = x->getIntAttribute("voiceNumbers", 1) < 2;            // saved when 51 built-in voices came first
    for (auto* u : x->getChildWithTagNameIterator("User")) {
        Sound s; s.name = u->getStringAttribute("name").toStdString(); s.title = u->getStringAttribute("title").toStdString(); s.category = u->getStringAttribute("category", "User").toStdString();
        s.mode = u->getIntAttribute("mode", 0) == 1 ? Mode::Waveform : Mode::Sample;
        s.rate = u->getDoubleAttribute("rate", 20000); s.root = u->getIntAttribute("root", 60);
        s.loopStart = u->getIntAttribute("loopStart", -1); s.loopEnd = u->getIntAttribute("loopEnd", -1);
        juce::MemoryBlock mb; mb.fromBase64Encoding(u->getStringAttribute("ram"));
        const size_t n = std::min<size_t>((size_t) RAM_SIZE, mb.getSize() / PAGE * PAGE);
        if (n == 0) continue;
        s.ram.assign((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + n);
        kit.setUser(u->getIntAttribute("slot"), std::move(s));
    }
    readSeq(*x);
    if (const auto* fx = x->getChildByName("Effects")) {
        engine.fxOn = fx->getBoolAttribute("on", true);
        auto sd = juce::StringArray::fromTokens(fx->getStringAttribute("sends"), " ", "");
        for (int i = 0; i < Engine::STRIPS && i < sd.size(); ++i) engine.fxSend[(size_t) i] = juce::jlimit(0.f, 1.f, sd[i].getFloatValue());
        engine.fx.load(fx->getStringAttribute("rev7").toStdString());
    }
    if (const auto* mx = x->getChildByName("Mixer")) {
        auto fd = juce::StringArray::fromTokens(mx->getStringAttribute("faders"), " ", ""), pn = juce::StringArray::fromTokens(mx->getStringAttribute("pans"), " ", "");
        auto so = juce::StringArray::fromTokens(mx->getStringAttribute("solos"), " ", ""), mu = juce::StringArray::fromTokens(mx->getStringAttribute("mutes"), " ", "");
        for (int i = 0; i < Engine::STRIPS; ++i) {
            if (i < fd.size()) engine.fader[(size_t) i] = juce::jlimit(0.f, 1.5f, fd[i].getFloatValue());
            if (i < pn.size()) engine.panOver[(size_t) i] = pn[i].getFloatValue();
            if (i < so.size()) engine.solo[(size_t) i] = so[i].getIntValue() != 0;
            if (i < mu.size()) engine.mute[(size_t) i] = mu[i].getIntValue() != 0;
        }
    }
    if (const auto* sg = x->getChildByName("Songs")) {
        const int want = sg->getIntAttribute("selected", -1); int pick = -1;
        for (auto* el : sg->getChildWithTagNameIterator("Song")) {
            juce::MemoryBlock mb; mb.fromBase64Encoding(el->getStringAttribute("midi"));
            MidiSong ms; std::string err;
            if (!parseSmf((const uint8_t*) mb.getData(), mb.getSize(), el->getStringAttribute("name").toStdString(), ms, err)) continue;
            const int i = songs.add(std::move(ms), kit.bank);
            auto vs = juce::StringArray::fromTokens(el->getStringAttribute("voices"), " ", ""), mu = juce::StringArray::fromTokens(el->getStringAttribute("mutes"), " ", "");
            if (auto* e = songs.at(i)) e->kit.load(el->getStringAttribute("kit").toStdString());
            if (auto* e = songs.at(i)) for (int t = 0; t < SONG_TRK; ++t) { if (t < vs.size()) { const int vi = vs[t].getIntValue(); e->voice[t] = vi == GM_KIT || !oldNumbers ? vi : Library::fromOldIndex(vi); } if (t < mu.size()) e->mute[t] = mu[t].getIntValue() != 0; }
            if (el->getIntAttribute("index", -2) == want) pick = i;
        }
        chooseSong(pick);
    }
    program = x->getIntAttribute("program", 0);
    showCatalog = x->getBoolAttribute("showCatalog", true);
    if (x->hasAttribute("pen")) { pen.load(x->getStringAttribute("pen").toStdString()); penSlot = x->getIntAttribute("penSlot", -1); }
    engine.clickOn = x->getBoolAttribute("click", false); recCountIn = x->getBoolAttribute("countIn", true);
    rec.quant = x->getDoubleAttribute("recQuant", 0); rec.replace = x->getBoolAttribute("recReplace", false);
    if (x->hasAttribute("ccMap")) {
        for (auto& c : ccMap) c = -1;
        for (auto& tok : juce::StringArray::fromTokens(x->getStringAttribute("ccMap"), " ", "")) {
            const int cc = tok.upToFirstOccurrenceOf(":", false, false).getIntValue(), pi = paramIndex(tok.fromFirstOccurrenceOf(":", false, false).toRawUTF8());
            if (cc >= 0 && cc < 128 && pi >= 0) ccMap[(size_t) cc] = pi;
        }
    }
    const juce::String sn = x->getStringAttribute("soundName");
    if (x->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*x));
    // the library may have grown: find the sound by name
    if (sn.isNotEmpty()) {
        int idx = -1;
        for (int i = 0; i < kit.bank.count.load(); ++i) if (const Sound* s = kit.bank.at(i)) if (juce::String(s->name) == sn) { idx = i; break; }
        if (idx >= 0) set("sound", (float) idx);
    }
    connectDefaults();
}

}  // namespace cmi

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new cmi::CmiProcessor(); }
