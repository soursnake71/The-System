// Omega Wheel of Steel library: crates (Serato, playlists, folders), decoding, and the glue to SeratoTags (Serato / ID3 /
// Vorbis / MP4 tags) and Analysis (BPM + first downbeat, key, waveform).
#include "Library.h"
#include "SeratoTags.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

namespace wheel {

//==============================================================================================================
// small helpers
namespace {

juce::String fromStd(const std::string& s) { return juce::String::fromUTF8(s.c_str(), (int) s.size()); }

// a file through juce::FileInputStream for SeratoTags
struct JuceFileSource : ByteSource {
    explicit JuceFileSource(const juce::File& f) : in(f) {}
    uint64_t size() override { return in.openedOk() ? (uint64_t) std::max<juce::int64>(0, in.getTotalLength()) : 0; }
    size_t read(uint64_t off, void* dst, size_t n) override
    {
        if (! in.openedOk() || ! in.setPosition((juce::int64) off)) return 0;
        size_t got = 0;
        while (got < n) {
            const int r = in.read((char*) dst + got, (int) std::min<size_t>(n - got, 1u << 30));
            if (r <= 0) break;
            got += (size_t) r;
        }
        return got;
    }
    juce::FileInputStream in;
};

Tags readFileTags(const juce::File& f)
{
    JuceFileSource src(f);
    if (! src.in.openedOk()) return {};
    return readTags(src);
}

// "Artist - Title.mp3", "01 - Artist - Title.mp3", "01. Title.mp3"
void nameFromFile(const juce::File& f, juce::String& title, juce::String& artist)
{
    juce::String s = f.getFileNameWithoutExtension().replaceCharacter('_', ' ').trim();
    int dash = s.indexOf(" - ");
    if (dash > 0 && s.substring(0, dash).trim().containsOnly("0123456789.")) { s = s.substring(dash + 3).trim(); dash = s.indexOf(" - "); }
    else {
        int k = 0;
        while (k < s.length() && juce::CharacterFunctions::isDigit(s[k])) ++k;
        if (k > 0 && k <= 3 && k < s.length() && (s[k] == '.' || s[k] == ' ') && s.length() > k + 2) { s = s.substring(k + 1).trim(); dash = s.indexOf(" - "); }
    }
    if (dash > 0) {
        if (artist.isEmpty()) artist = s.substring(0, dash).trim();
        if (title.isEmpty()) title = s.substring(dash + 3).trim();
    } else if (title.isEmpty()) title = s;
}

//==============================================================================================================
// caches shared by all Library instances (tags per file, the file index per root)

struct CachedEntry { juce::int64 mtime = 0, size = 0; Entry e; };
std::mutex entryMutex;
std::unordered_map<std::string, CachedEntry> entryCache;

// fold a path component for matching across OSes: lower-case ASCII letters and digits only; an accented letter (NFC or
// NFD) drops out entirely so "Beyoncé" matches whichever normalisation the other OS used
std::string fold(const juce::String& s)
{
    std::string o;
    o.reserve((size_t) s.length());
    for (auto p = s.getCharPointer(); ! p.isEmpty();) {
        const juce::juce_wchar c = p.getAndAdvance();
        if (c >= 0x300 && c <= 0x36f) { if (! o.empty()) o.pop_back(); continue; }
        if (c < 128 && juce::CharacterFunctions::isLetterOrDigit(c)) o += (char) juce::CharacterFunctions::toLowerCase(c);
    }
    return o;
}

juce::StringArray components(juce::String path)
{
    path = path.replaceCharacter('\\', '/');
    juce::StringArray a;
    a.addTokens(path, "/", "");
    a.removeEmptyStrings();
    return a;
}

struct RootIndex {
    juce::Array<juce::File> audio;                                  // all audio under the root (capped)
    std::unordered_map<std::string, std::vector<int>> byName;       // folded file name -> indices
    juce::Array<Crate> folders, playlists;
};
std::mutex indexMutex;
std::map<std::string, std::shared_ptr<RootIndex>> indexes;

std::shared_ptr<RootIndex> indexFor(const juce::File& root)
{
    std::lock_guard<std::mutex> g(indexMutex);
    auto it = indexes.find(root.getFullPathName().toStdString());
    return it == indexes.end() ? nullptr : it->second;
}

constexpr int kMaxAudio = 20000, kMaxDirs = 6000, kMaxDepth = 8, kMaxFolderCrates = 2000;

bool skipDir(const juce::File& d)
{
    const juce::String n = d.getFileName();
    if (n.isEmpty() || n.startsWithChar('.') || n.startsWithChar('$') || d.isHidden() || d.isSymbolicLink()) return true;
    static const char* const skip[] = { "_Serato_", "_Serato_Backup", "System Volume Information", "LOST.DIR", "lost+found",
                                        "RECYCLER", "Recycled", "Autosave", "Expansions", "node_modules", "Library", "AppData",
                                        "Applications", "Program Files", "Program Files (x86)", "Windows", "proc", "sys", "dev" };
    for (auto* s : skip) if (n.equalsIgnoreCase(s)) return true;
    return n.containsIgnoreCase("[ProjectData]") || n.endsWithIgnoreCase("_[ProjectData]");
}

struct Walker {
    explicit Walker(RootIndex& i) : idx(i) {}
    RootIndex& idx;
    int dirs = 0;
    juce::uint32 startMs = juce::Time::getMillisecondCounter();
    bool outOfTime() const { return juce::Time::getMillisecondCounter() - startMs > 5000; }

    // returns whether the folder (or anything below it) holds audio
    bool walk(const juce::File& dir, int depth)
    {
        if (++dirs > kMaxDirs || outOfTime()) return false;
        juce::Array<juce::File> files, subs;
        for (const auto& e : juce::RangedDirectoryIterator(dir, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles)) {
            const auto& f = e.getFile();
            if (e.isDirectory()) { if (! skipDir(f)) subs.add(f); }
            else if (Library::isAudio(f)) files.add(f);
            else if (depth <= 2 && (f.hasFileExtension("m3u") || f.hasFileExtension("m3u8")))
                idx.playlists.add({ f.getFileNameWithoutExtension(), 0, Crate::playlist, f });
        }
        files.sort();
        subs.sort();
        for (const auto& f : files) {
            if (idx.audio.size() >= kMaxAudio) break;
            idx.byName[fold(f.getFileName())].push_back(idx.audio.size());
            idx.audio.add(f);
        }
        bool any = ! files.isEmpty();
        if (depth >= kMaxDepth) return any;
        for (const auto& s : subs) {
            const int at = idx.folders.size();
            if (walk(s, depth + 1)) {
                any = true;
                if (idx.folders.size() < kMaxFolderCrates) idx.folders.insert(at, { s.getFileName(), depth, Crate::folder, s });
            }
        }
        return any;
    }
};

void loadM3u(const juce::File& f, juce::StringArray& out)
{
    juce::StringArray lines;
    lines.addLines(f.loadFileAsString());
    for (auto l : lines) {
        l = l.trim();
        if (l.isEmpty() || l.startsWithChar('#')) continue;
        if (l.startsWithIgnoreCase("file://")) l = juce::URL::removeEscapeChars(l.substring(7));
        out.add(l);
    }
}

}  // namespace

//==============================================================================================================
// crates

juce::StringArray parseCrate(const juce::MemoryBlock& data)
{
    juce::StringArray paths;
    for (const auto& p : parseCrate((const uint8_t*) data.getData(), data.getSize())) paths.add(fromStd(p));
    return paths;
}

Library::Library() { formats.registerBasicFormats(); }

bool Library::isAudio(const juce::File& f)
{
    if (f.getFileName().startsWithChar('.')) return false;   // "._x.mp3" AppleDouble files
    return f.hasFileExtension("mp3;wav;wave;aif;aiff;aifc;flac;ogg;m4a;mp4;aac;alac;caf;wma");
}

void Library::setRoot(const juce::File& root)
{
    const juce::ScopedLock scoped(scanLock);
    rootDir = root;
    seratoDir = juce::File();
    crateList.clearQuick();
    if (! root.isDirectory()) return;

    // the _Serato_ folder: in the root, in <root>/Music, or in a parent (up to the volume root)
    for (juce::File d = root;; d = d.getParentDirectory()) {
        for (auto c : { d.getChildFile("_Serato_"), d.getChildFile("Music").getChildFile("_Serato_") })
            if (c.isDirectory()) { seratoDir = c; break; }
        if (seratoDir.exists() || d.getParentDirectory() == d) break;
    }

    crateList.add({ "All tracks", 0, Crate::all, root });

    if (seratoDir.isDirectory()) {
        struct C { juce::StringArray parts; juce::File f; };
        std::vector<C> cs;
        for (const auto& e : juce::RangedDirectoryIterator(seratoDir.getChildFile("Subcrates"), false, "*.crate")) {
            juce::StringArray parts;
            juce::String nm = e.getFile().getFileNameWithoutExtension();
            for (int at; (at = nm.indexOf("%%")) >= 0; nm = nm.substring(at + 2)) parts.add(nm.substring(0, at));
            parts.add(nm);
            parts.removeEmptyStrings();
            if (! parts.isEmpty()) cs.push_back({ parts, e.getFile() });
        }
        std::sort(cs.begin(), cs.end(), [](const C& a, const C& b) {
            for (int i = 0; i < std::min(a.parts.size(), b.parts.size()); ++i) {
                const int r = a.parts[i].compareNatural(b.parts[i]);
                if (r != 0) return r < 0;
            }
            return a.parts.size() < b.parts.size();
        });
        for (const auto& c : cs) crateList.add({ c.parts.joinIntoString(" / "), c.parts.size() - 1, Crate::serato, c.f });
    }

    // one walk: every audio file (for "All tracks" and matching crate paths), playlists, folders with audio
    auto idx = std::make_shared<RootIndex>();
    Walker w(*idx);
    w.walk(root, 0);
    for (const auto& p : idx->playlists) crateList.add(p);
    for (const auto& f : idx->folders) crateList.add(f);
    std::lock_guard<std::mutex> g(indexMutex);
    indexes[root.getFullPathName().toStdString()] = idx;
}

namespace {
// a path from a crate or playlist, made on whatever OS, to a file here
juce::File resolvePath(const juce::String& rawPath, const juce::File& rootDir, const juce::File& seratoDir,
                       const juce::File& relativeTo, const RootIndex* idx, juce::File& lastBase)
{
    juce::String p = rawPath.trim();
    if (p.isEmpty()) return {};
    if (juce::File::isAbsolutePath(p)) {
        const juce::File f(p);
        if (f.existsAsFile()) return f;
    }
    juce::String rel = p.replaceCharacter('\\', '/');
    if (rel.length() >= 2 && rel[1] == ':') rel = rel.substring(2);   // "D:/Music/..."
    while (rel.startsWithChar('/')) rel = rel.substring(1);
    juce::StringArray tries { rel };
    if (rel.startsWithIgnoreCase("Volumes/")) {   // "Volumes/<card>/Music/x.mp3" -> "Music/x.mp3"
        const int s = rel.indexOfChar(8, '/');
        if (s > 0) tries.add(rel.substring(s + 1));
    }
    if (rel.startsWithIgnoreCase("media/") || rel.startsWithIgnoreCase("mnt/")) {   // Linux mounts
        const juce::StringArray c = components(rel);
        for (int k = 2; k < c.size() - 1 && k <= 3; ++k) {
            juce::StringArray tail;
            for (int i = k; i < c.size(); ++i) tail.add(c[i]);
            tries.add(tail.joinIntoString("/"));
        }
    }

    if (lastBase.isDirectory())
        for (const auto& t : tries) { const juce::File f = lastBase.getChildFile(t); if (f.existsAsFile()) return f; }

    juce::Array<juce::File> bases;
    if (relativeTo.isDirectory()) bases.add(relativeTo);
    for (juce::File d : { seratoDir.exists() ? seratoDir.getParentDirectory() : juce::File(), rootDir })
        for (int guard = 0; d.exists() && guard < 32; ++guard) {
            bases.addIfNotAlreadyThere(d);
            if (d.getParentDirectory() == d) break;
            d = d.getParentDirectory();
        }
    for (const auto& b : bases)
        for (const auto& t : tries) {
            const juce::File f = b.getChildFile(t);
            if (f.existsAsFile()) { lastBase = b; return f; }
        }

    // match by path suffix against the files under the root
    if (idx == nullptr) return {};
    const juce::StringArray want = components(rel);
    if (want.isEmpty()) return {};
    auto it = idx->byName.find(fold(want[want.size() - 1]));
    if (it == idx->byName.end()) return {};
    int best = -1, bestScore = 0;
    for (int i : it->second) {
        const juce::File& f = idx->audio.getReference(i);
        const juce::StringArray have = components(f.getRelativePathFrom(rootDir));
        int score = 0;
        while (score < want.size() && score < have.size()
               && fold(want[want.size() - 1 - score]) == fold(have[have.size() - 1 - score])) ++score;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    return best >= 0 ? idx->audio[best] : juce::File();
}
}  // namespace

juce::Array<Entry> Library::list(const Crate& c)
{
    const juce::ScopedLock scoped(scanLock);
    juce::Array<Entry> out;
    const auto idx = indexFor(rootDir);
    if (c.kind == Crate::all) {
        if (idx) for (const auto& f : idx->audio) out.add(readEntry(f));
        return out;
    }
    if (c.kind == Crate::folder) {
        int n = 0;
        if (idx) {   // from the index (already sorted, recursive)
            const juce::String base = c.source.getFullPathName() + juce::File::getSeparatorString();
            for (const auto& f : idx->audio)
                if (f.getFullPathName().startsWith(base) && ++n <= kMaxAudio) out.add(readEntry(f));
            if (n > 0) return out;
        }
        for (const auto& e : juce::RangedDirectoryIterator(c.source, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles)) {
            if (isAudio(e.getFile())) out.add(readEntry(e.getFile()));
            if (out.size() >= kMaxAudio) break;
        }
        return out;
    }
    juce::StringArray paths;
    juce::File relTo;
    if (c.kind == Crate::serato) {
        juce::MemoryBlock mb;
        if (c.source.loadFileAsData(mb)) paths = parseCrate(mb);
    } else {
        loadM3u(c.source, paths);
        relTo = c.source.getParentDirectory();
    }
    juce::File lastBase;
    juce::Array<juce::File> seen;
    for (const auto& p : paths) {
        const juce::File f = resolvePath(p, rootDir, seratoDir, relTo, idx.get(), lastBase);
        if (f.existsAsFile() && ! seen.contains(f)) { seen.add(f); out.add(readEntry(f)); }
    }
    return out;
}

juce::Array<Entry> Library::search(const juce::String& text, const Crate& c)
{
    const juce::ScopedLock scoped(scanLock);
    juce::StringArray words;
    words.addTokens(text, " \t", "\"");
    words.trim();
    words.removeEmptyStrings();
    for (auto& w : words) w = w.unquoted();
    juce::Array<Entry> all = list(c), out;
    for (const auto& e : all) {
        const juce::String hay = e.title + " " + e.artist + " " + e.album + " " + e.file.getFileName();
        bool ok = true;
        for (const auto& w : words) if (! hay.containsIgnoreCase(w)) { ok = false; break; }
        if (ok) out.add(e);
    }
    return out;
}

namespace {
void applyFileTags(const juce::File& f, Tags g, Track& t, Entry* e)
{
    juce::String title = fromStd(g.title), artist = fromStd(g.artist);
    nameFromFile(f, title, artist);
    g.title = title.toStdString();
    g.artist = artist.toStdString();
    applyTags(g, t);
    if (e) {
        e->title = title;
        e->artist = artist;
        e->album = fromStd(g.album);
        e->keyText = fromStd(g.keyText);
        e->bpm = t.bpm;
        e->serato = g.serato;
        if (g.length > 0 && e->length <= 0) e->length = g.length;
    }
}
}  // namespace

void readSeratoTags(const juce::File& f, Track& t, Entry* e) { applyFileTags(f, readFileTags(f), t, e); }

Entry Library::readEntry(const juce::File& f)
{
    const std::string key = f.getFullPathName().toStdString();
    const juce::int64 mtime = f.getLastModificationTime().toMilliseconds(), size = f.getSize();
    {
        std::lock_guard<std::mutex> g(entryMutex);
        auto it = entryCache.find(key);
        if (it != entryCache.end() && it->second.mtime == mtime && it->second.size == size) return it->second.e;
    }
    Entry e;
    e.file = f;
    Track tmp;
    readSeratoTags(f, tmp, &e);
    if (e.length <= 0) {
        if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor(f) })
            if (r->sampleRate > 0) e.length = (double) r->lengthInSamples / r->sampleRate;
    }
    std::lock_guard<std::mutex> g(entryMutex);
    if (entryCache.size() > 200000) entryCache.clear();
    entryCache[key] = { mtime, size, e };
    return e;
}

//==============================================================================================================
// load

std::shared_ptr<Track> Library::load(const juce::File& f, juce::String& err, std::function<bool()> cancelled)
{
    auto stop = [&] { return cancelled && cancelled(); };
    if (! f.existsAsFile()) { err = "File not found: " + f.getFileName(); return nullptr; }
    std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor(f) };
    if (! r || r->sampleRate <= 0 || r->numChannels < 1) { err = "Can't decode " + f.getFileName(); return nullptr; }
    auto t = std::make_shared<Track>();
    t->sr = r->sampleRate;
    t->path = f.getFullPathName().toStdString();
    const juce::int64 cap = (juce::int64) (20 * 60 * r->sampleRate);
    const juce::int64 total = std::min(r->lengthInSamples, cap);
    if (total <= 0) { err = "Empty file: " + f.getFileName(); return nullptr; }
    t->L.resize((size_t) total);
    t->R.resize((size_t) total);
    const int chunk = 1 << 16;
    juce::AudioBuffer<float> buf((int) std::min<juce::uint32>(2, r->numChannels), chunk);
    for (juce::int64 pos = 0; pos < total; pos += chunk) {
        if (stop()) { err = "Cancelled"; return nullptr; }
        const int num = (int) std::min<juce::int64>(chunk, total - pos);
        if (! r->read(&buf, 0, num, pos, true, true)) { if (pos == 0) { err = "Read error: " + f.getFileName(); return nullptr; } t->L.resize((size_t) pos); t->R.resize((size_t) pos); break; }
        std::memcpy(t->L.data() + pos, buf.getReadPointer(0), sizeof(float) * (size_t) num);
        std::memcpy(t->R.data() + pos, buf.getReadPointer(buf.getNumChannels() > 1 ? 1 : 0), sizeof(float) * (size_t) num);
    }
    r.reset();

    const Tags tg = readFileTags(f);
    applyFileTags(f, tg, *t, nullptr);
    if (stop()) { err = "Cancelled"; return nullptr; }

    analyseTrack(*t, tg.hasGrid, hintBpm(tg), stop);
    if (stop()) { err = "Cancelled"; return nullptr; }
    return t;
}

}  // namespace wheel
