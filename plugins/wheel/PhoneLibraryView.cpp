// Omega Wheel of Steel: the phone app's library screen (android/wheel LibraryView.java) drawn with JUCE: a bar with
// BACK, SD CARD / FOLDER..., the status, the search box, LOAD A and LOAD B; under it the crates (All tracks, Serato
// crates as a tree, smart crates, playlists, folders, with track counts) and the tracks of the chosen crate (A / B
// mark for what each deck has, title, artist, length, BPM, Camelot-coloured key). Same sizes in dp as the phone
// (1 dp = the larger scale at which an 800 x 360 dp phone screen fits the view), same colours (Pal.java).
// Backed by the plugin's Library: setting the root, listing a crate, counting crates and searching run on one
// background thread; every result carries a generation number and is dropped when something newer was asked for.
#include "PhoneUI.h"
#include "WheelFonts.h"
#include <cmath>

namespace wheel {

using namespace juce;

namespace {

// Pal.java
namespace lp {
const Colour bg { 0xff0a0b0d }, panel { 0xff141619 }, raised { 0xff24282d }, well { 0xff07080a }, edgeHi { 0xff3a4048 },
             text { 0xffe8eaed }, dim { 0xff9098a2 }, faint { 0xff545b64 }, amber { 0xffffa21f }, black { 0xff000000 },
             selDim { 0xff2a3a4d }, odd { 0xff111316 }, rule { 0xff1d2024 }, selDimText { 0xff333333 };
const Colour deck[2] = { Colour(0xff26a6ff), Colour(0xffff8a1a) };
}

String U(const char* utf8) { return String::fromUTF8(utf8); }

Typeface::Ptr face()
{
    static Typeface::Ptr tf = Typeface::createSystemTypefaceFor(WheelFonts::DejaVuSansBold_ttf, (size_t) WheelFonts::DejaVuSansBold_ttfSize);
    return tf;
}
// Android text sizes are em sizes (Paint.setTextSize): a point height here. DejaVu Sans Bold runs about a sixth wider
// than the phone's Roboto Bold, so it is narrowed to take the same room (labels, columns and ellipses then match).
constexpr float kNarrow = .84f;
Font bold(float px)
{
    px = jmax(1.0f, px);
    auto tf = face();
    return tf != nullptr ? Font(FontOptions(tf).withPointHeight(px).withHorizontalScale(kNarrow)) : Font(FontOptions(px, Font::bold));
}
// the search box's text and hint (an EditText: the regular system face)
Font regular(float px) { return Font(FontOptions().withPointHeight(jmax(1.0f, px))); }
float widthOf(const Font& f, const String& s) { return GlyphArrangement::getStringWidth(f, s); }

// LibraryView.Row.fit: cut with an ellipsis to fit maxW
String fit(const Font& f, const String& s, float maxW)
{
    if (s.isEmpty() || maxW <= 0) return {};
    if (widthOf(f, s) <= maxW) return s;
    const String ell = U("\xe2\x80\xa6");
    const float room = maxW - widthOf(f, ell);
    if (room <= 0) return {};
    int lo = 0, hi = s.length();
    while (lo < hi) {   // the longest prefix that fits
        const int mid = (lo + hi + 1) / 2;
        if (widthOf(f, s.substring(0, mid)) <= room) lo = mid; else hi = mid - 1;
    }
    return lo <= 0 ? String() : s.substring(0, lo).trimEnd() + ell;
}

// text at a baseline (Canvas.drawText)
void textAt(Graphics& g, const Font& f, const String& s, float x, float baseline, bool centred = false)
{
    if (s.isEmpty()) return;
    if (centred) x -= widthOf(f, s) / 2;
    GlyphArrangement ga;
    ga.addLineOfText(f, s, x, baseline);
    g.setFont(f);
    ga.draw(g);
}

// Pal.keyColour(String): Camelot wheel colours, minor deeper, major lighter
Colour keyColour(const String& camelotText)
{
    auto t = camelotText.trim().toUpperCase();
    if (t.length() < 2) return lp::faint;
    const juce_wchar ab = t.getLastCharacter();
    auto num = t.dropLastCharacters(1);
    if (! num.containsOnly("0123456789")) return lp::faint;
    const int n = num.getIntValue();
    if (n < 1 || n > 12 || (ab != 'A' && ab != 'B')) return lp::faint;
    const float hue = (float) std::fmod(0.40 + (n - 1) / 12.0, 1.0);
    return ab == 'A' ? Colour::fromHSV(hue, .78f, .95f, 1.0f) : Colour::fromHSV(hue, .55f, 1.0f, 1.0f);
}

// what the phone shows in the key column: Camelot ("8A"), from whatever the tag said ("Am", "8A", "A minor")
String camelotOf(const Entry& e)
{
    const int k = parseKey(e.keyText.toStdString());
    if (k >= 0) return String(camelot(k));
    return e.keyText.trim();
}

String bpmText(double v)
{
    if (v <= 0) return {};
    return std::abs(v - std::round(v)) < .05 ? String((int) std::round(v)) : String(v, 1);
}

String lengthText(double seconds)
{
    if (seconds <= 0) return {};
    const int s = (int) seconds, h = s / 3600, m = (s / 60) % 60, sec = s % 60;
    return h > 0 ? String::formatted("%d:%02d:%02d", h, m, sec) : String::formatted("%d:%02d", m, sec);
}

String label(const Entry& e) { return e.title.isNotEmpty() ? e.title : e.file.getFileName(); }

// GradientDrawable box(): rounded 6 dp, 1.5 dp edge inside the bounds
void box(Graphics& g, Rectangle<float> r, Colour fill, Colour edge, float dp)
{
    const float sw = jmax(1.0f, std::round(1.5f * dp));
    g.setColour(fill);
    g.fillRoundedRectangle(r, 6 * dp);
    g.setColour(edge);
    g.drawRoundedRectangle(r.reduced(sw / 2), 6 * dp - sw / 2, sw);
}

// the bar's buttons (TextView + box(RAISED, edge), 14 sp bold, centred)
struct PhoneButton : Button {
    PhoneButton(const String& t, Colour c) : Button(t), col(c) { setWantsKeyboardFocus(false); setMouseClickGrabsKeyboardFocus(false); }
    Colour col;
    float dp = 1;
    void paintButton(Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        box(g, r, lp::raised, col == lp::text ? lp::edgeHi : col, dp);
        if (down || over) { g.setColour(Colours::white.withAlpha(down ? .10f : .04f)); g.fillRoundedRectangle(r, 6 * dp); }
        const float ts = 14 * dp;
        const Font f = bold(ts);
        g.setColour(col);
        textAt(g, f, fit(f, getButtonText(), r.getWidth() - 4 * dp), r.getCentreX(), r.getCentreY() + ts * .36f, true);
    }
};

// a ListView: rows of one height, scrolled by wheel or drag, tap = click; a thin fast-scroll thumb at the right
struct RowList : Component {
    std::function<int()> count;
    std::function<void(Graphics&, int row, int w, int h)> paintRow;
    std::function<void(int row, int clicks)> clicked;
    float rowH = 44, off = 0, dp = 1;
    float downY = 0, downOff = 0;
    bool dragging = false;

    RowList() { setOpaque(true); setWantsKeyboardFocus(false); }
    int rows() const { return count ? count() : 0; }
    float maxOff() const { return jmax(0.0f, rows() * rowH - (float) getHeight()); }
    void clampOff() { off = jlimit(0.0f, maxOff(), off); }
    void scrollTo(float o) { off = o; clampOff(); repaint(); }
    void ensureVisible(int row)
    {
        const float top = row * rowH, bottom = top + rowH;
        if (top < off) scrollTo(top);
        else if (bottom > off + (float) getHeight()) scrollTo(bottom - (float) getHeight());
    }
    void paint(Graphics& g) override
    {
        g.fillAll(lp::panel);
        clampOff();
        const int n = rows(), w = getWidth(), h = (int) std::round(rowH);
        const int first = jmax(0, (int) std::floor(off / rowH));
        for (int i = first; i < n; ++i) {
            const float y = i * rowH - off;
            if (y >= (float) getHeight()) break;
            Graphics::ScopedSaveState s(g);
            g.setOrigin(0, (int) std::round(y));
            g.reduceClipRegion(0, 0, w, h);
            if (paintRow) paintRow(g, i, w, h);
        }
        // AbsListView's fast-scroll thumb
        const float total = n * rowH, vis = (float) getHeight();
        if (total > vis * 1.01f) {
            const float tw = 3 * dp, th = jmax(24 * dp, vis * vis / total), ty = (vis - th) * off / (total - vis);
            g.setColour(Colour(0xff3c4249));
            g.fillRect(Rectangle<float>((float) w - tw, ty, tw, th));
        }
    }
    void mouseDown(const MouseEvent& e) override
    {
        if (auto* p = getParentComponent()) p->grabKeyboardFocus();
        downY = (float) e.position.y; downOff = off; dragging = false;
    }
    void mouseDrag(const MouseEvent& e) override
    {
        const float dy = (float) e.position.y - downY;
        if (! dragging && std::abs(dy) > 8 * dp) dragging = true;
        if (dragging) scrollTo(downOff - dy);
    }
    void mouseUp(const MouseEvent& e) override
    {
        if (dragging) return;
        const int r = (int) std::floor((e.position.y + off) / rowH);
        if (r >= 0 && r < rows() && clicked) clicked(r, e.getNumberOfClicks());
    }
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails& w) override
    {
        scrollTo(off - w.deltaY * rowH * 8);
    }
};

}  // namespace

//==============================================================================================================

struct PhoneLibraryView::Impl : private Timer, private KeyListener, private ComponentListener {
    Impl(PhoneLibraryView& o, WheelProcessor& p, PhoneHost& ph)
        : owner(o), proc(p), host(ph), pool(ThreadPoolOptions{}.withNumberOfThreads(1).withThreadName("wheel phone library")),
          back(U("\xe2\x97\x80 BACK"), lp::text), choose(U("SD CARD / FOLDER\xe2\x80\xa6"), lp::text),
          loadA("LOAD A", lp::deck[0]), loadB("LOAD B", lp::deck[1])
    {
        back.onClick = [this] { if (host.closeLibrary) host.closeLibrary(); };
        choose.onClick = [this] { chooseFolder(); };
        loadA.onClick = [this] { loadSel(0); };
        loadB.onClick = [this] { loadSel(1); };
        loadA.setTooltip("Load the selected track on deck A (Shift+Left)");
        loadB.setTooltip("Load the selected track on deck B (Shift+Right)");

        for (auto id : { TextEditor::backgroundColourId, TextEditor::outlineColourId, TextEditor::focusedOutlineColourId, TextEditor::shadowColourId })
            search.setColour(id, Colours::transparentBlack);
        search.setColour(TextEditor::textColourId, lp::text);
        search.setColour(CaretComponent::caretColourId, lp::deck[0]);
        search.setColour(TextEditor::highlightColourId, lp::deck[0].withAlpha(.35f));
        search.setColour(TextEditor::highlightedTextColourId, lp::text);
        search.setJustification(Justification::centredLeft);
        search.setBorder({});
        search.setIndents(0, 0);
        search.setScrollbarsShown(false);
        search.setPopupMenuEnabled(true);
        search.onTextChange = [this] { refilter(); owner.repaint(searchBox.getSmallestIntegerContainer()); };
        search.onReturnKey = [this] { intoTracks(); owner.grabKeyboardFocus(); };
        search.onEscapeKey = [this] { if (host.closeLibrary) host.closeLibrary(); };
        search.addKeyListener(this);

        crateList.count = [this] { return crates.size(); };
        crateList.paintRow = [this](Graphics& g, int r, int w, int h) { paintCrate(g, r, w, h); };
        crateList.clicked = [this](int r, int) { focusList = 0; pickCrate(r); };
        trackList.count = [this] { return shown.size(); };
        trackList.paintRow = [this](Graphics& g, int r, int w, int h) { paintTrack(g, r, w, h); };
        trackList.clicked = [this](int r, int clicks) {
            focusList = 1;
            if (r >= 0 && r < shown.size()) selected = shown.getReference(r).file;
            repaintLists();
            if (clicks >= 2) loadSel(freeDeck());
        };

        for (Component* c : std::initializer_list<Component*> { &back, &choose, &search, &loadA, &loadB, &crateList, &trackList })
            owner.addAndMakeVisible(c);
        owner.setWantsKeyboardFocus(true);
        owner.addKeyListener(this);
        owner.addComponentListener(this);

        // what the processor already has (state restore / the hardware browser), else the last folder
        readCrates(true);
        startTimerHz(15);
    }

    ~Impl() override
    {
        stopTimer();
        quit->store(true);
        pool.removeAllJobs(true, 4000);
        owner.removeKeyListener(this);
        owner.removeComponentListener(this);
    }

    PhoneLibraryView& owner;
    WheelProcessor& proc;
    PhoneHost& host;
    ThreadPool pool;
    std::shared_ptr<std::atomic<bool>> quit = std::make_shared<std::atomic<bool>>(false);
    std::unique_ptr<FileChooser> chooser;

    PhoneButton back, choose, loadA, loadB;
    TextEditor search;
    RowList crateList, trackList;

    Array<Crate> crates;
    Array<int> counts;                       // tracks per crate, -1 = not counted yet
    std::shared_ptr<const Array<Entry>> crateTracks = std::make_shared<Array<Entry>>();
    Array<Entry> shown;
    int crateSel = 0;
    File selected;
    int focusList = 0;                       // 0 crates, 1 tracks (keyboard / encoder target)
    String libStatus = "Choose your SD card or music folder";
    String note; uint32 noteAt = 0;          // a passing message ("Pick a track first")
    String deckPath[2];
    bool loadingWas[2] { false, false };
    bool askedForFolder = false;
    float dp = 1;
    Rectangle<float> statusArea, searchBox;

    // generations: the root (setRoot / counts), the crate listing, the search filter
    int rootGen = 0, listGen = 0, filterGen = 0;
    std::shared_ptr<std::atomic<int>> rootGenA = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> pending = std::make_shared<std::atomic<int>>(0);   // listing / filter jobs queued

    // ---- the root and the crates
    void chooseFolder()
    {
        auto start = proc.lastRoot.isDirectory() ? proc.lastRoot : File::getSpecialLocation(File::userMusicDirectory);
        chooser = std::make_unique<FileChooser>("Choose the SD card, drive or music folder", start);
        Component::SafePointer<PhoneLibraryView> safe(&owner);
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [safe](const FileChooser& fc) {
            auto dir = fc.getResult();
            if (safe != nullptr && dir.isDirectory()) safe->d->setRoot(dir);
        });
    }

    void setRoot(const File& dir)
    {
        proc.lastRoot = dir;
        const int g = ++rootGen;
        rootGenA->store(g);
        ++listGen; ++filterGen;
        libStatus = U("Scanning\xe2\x80\xa6");
        crates.clear(); counts.clear(); shown.clear(); crateTracks = std::make_shared<Array<Entry>>();
        repaintLists(); owner.repaint();
        Component::SafePointer<PhoneLibraryView> safe(&owner);
        auto* lib = &proc.library;
        auto q = quit;
        pool.addJob([safe, lib, dir, g, q] {
            if (q->load()) return;
            lib->setRoot(dir);
            auto list = lib->cratesCopy();
            const File r = lib->root();
            MessageManager::callAsync([safe, g, list, r] {
                if (safe == nullptr || g != safe->d->rootGen) return;
                safe->d->applyCrates(g, list, r, false);
            });
        });
    }

    // the crates the library has now (after the root changed elsewhere): read on the library thread, since the
    // library is locked while it lists or scans
    void readCrates(bool orLastRoot)
    {
        const int g = ++rootGen;
        rootGenA->store(g);
        Component::SafePointer<PhoneLibraryView> safe(&owner);
        auto* lib = &proc.library;
        auto q = quit;
        pool.addJob([safe, lib, g, q, orLastRoot] {
            if (q->load()) return;
            auto list = lib->cratesCopy();
            const File r = lib->root();
            MessageManager::callAsync([safe, g, list, r, orLastRoot] {
                if (safe == nullptr || g != safe->d->rootGen) return;
                safe->d->applyCrates(g, list, r, orLastRoot);
            });
        });
    }

    void applyCrates(int g, const Array<Crate>& list, const File& r, bool orLastRoot)
    {
        if (orLastRoot && list.isEmpty() && proc.lastRoot.isDirectory()) { setRoot(proc.lastRoot); return; }
        const String want = crateSel >= 0 && crateSel < crates.size() ? crates.getReference(crateSel).name
                                                                         : proc.apvts.state.getProperty("phoneLibCrate").toString();
        crates = list;
        root = r;
        counts.clearQuick();
        counts.insertMultiple(0, -1, crates.size());
        int seratoCrates = 0;
        for (auto& c : crates) if (c.kind == Crate::serato) ++seratoCrates;
        seratoCount = seratoCrates;
        if (! root.isDirectory()) libStatus = "Choose your SD card or music folder";
        else if (crates.isEmpty()) libStatus = "No crates or audio found there";
        else libStatus = String(seratoCrates) + " crates";
        crateSel = 0;
        for (int i = 0; i < crates.size(); ++i) if (crates.getReference(i).name == want) crateSel = i;
        crateList.scrollTo(0);
        crateList.ensureVisible(crateSel);
        listCrate();
        // then count every crate in the background (behind anything the user asks for), All tracks last
        for (int i = 0; i < crates.size(); ++i) if (crates.getReference(i).kind != Crate::all) queueCount(g, i);
        for (int i = 0; i < crates.size(); ++i) if (crates.getReference(i).kind == Crate::all) queueCount(g, i);
        owner.repaint();
    }
    int seratoCount = 0;
    File root;                               // the library's root as last read
    bool awaitingRoot() const { return pool.getNumJobs() > 0; }

    void queueCount(int g, int i)
    {
        countJob(&pool, Component::SafePointer<PhoneLibraryView>(&owner), &proc.library, quit, rootGenA, pending, crates[i], g, i);
    }
    // runs on the library thread too (re-queues itself behind the user's listing), so it captures everything by value
    static void countJob(ThreadPool* pool, Component::SafePointer<PhoneLibraryView> safe, Library* lib, std::shared_ptr<std::atomic<bool>> q,
                         std::shared_ptr<std::atomic<int>> rg, std::shared_ptr<std::atomic<int>> pend, Crate c, int g, int i)
    {
        pool->addJob([=] {
            if (q->load() || rg->load() != g) return;
            if (pend->load() > 0) { countJob(pool, safe, lib, q, rg, pend, c, g, i); return; }   // the user's listing first
            const int n = lib->list(c).size();
            MessageManager::callAsync([safe, g, i, n] {
                if (safe == nullptr || g != safe->d->rootGen) return;
                safe->d->setCount(i, n);
            });
        });
    }

    void setCount(int i, int n)
    {
        if (i < 0 || i >= counts.size()) return;
        counts.set(i, n);
        if (crates.getReference(i).kind == Crate::all) libStatus = String(n) + " tracks " + U("\xc2\xb7 ") + String(seratoCount) + " crates";
        crateList.repaint();
        owner.repaint(statusArea.getSmallestIntegerContainer());
    }

    void pickCrate(int pos)
    {
        if (pos < 0 || pos >= crates.size()) return;
        crateSel = pos;
        proc.apvts.state.setProperty("phoneLibCrate", crates.getReference(pos).name, nullptr);
        crateList.ensureVisible(pos);
        listCrate();
        trackList.scrollTo(0);
    }

    void listCrate()
    {
        const int g = ++listGen;
        ++filterGen;
        repaintLists();
        if (crateSel < 0 || crateSel >= crates.size()) { crateTracks = std::make_shared<Array<Entry>>(); shown.clear(); return; }
        const Crate c = crates[crateSel];
        const int idx = crateSel, rg = rootGen;
        Component::SafePointer<PhoneLibraryView> safe(&owner);
        auto* lib = &proc.library;
        auto q = quit; auto pend = pending;
        pend->fetch_add(1);
        pool.addJob([safe, lib, q, pend, c, g, idx, rg] {
            if (q->load()) { pend->fetch_sub(1); return; }
            auto list = std::make_shared<Array<Entry>>(lib->list(c));
            pend->fetch_sub(1);
            MessageManager::callAsync([safe, g, idx, rg, list] {
                if (safe == nullptr) return;
                auto& d = *safe->d;
                if (rg == d.rootGen) d.setCount(idx, list->size());
                if (g != d.listGen) return;
                d.crateTracks = list;
                d.refilter();
            });
        });
    }

    // LibraryView.refilter: every word in title / artist / album / file name / key / BPM (on the library thread)
    void refilter()
    {
        const String q = search.getText().trim().toLowerCase();
        const int g = ++filterGen;
        if (q.isEmpty()) { shown = *crateTracks; afterFilter(); return; }
        Component::SafePointer<PhoneLibraryView> safe(&owner);
        auto src = crateTracks;
        auto qq = quit; auto pend = pending;
        pend->fetch_add(1);
        pool.addJob([safe, src, q, qq, pend, g] {
            if (qq->load()) { pend->fetch_sub(1); return; }
            StringArray terms;
            terms.addTokens(q, " \t", "");
            terms.removeEmptyStrings();
            Array<Entry> out;
            for (const auto& e : *src) {
                const String hay = (e.title + " " + e.artist + " " + e.album + " " + e.file.getFileName() + " " + camelotOf(e) + " "
                                    + e.keyText + " " + bpmText(e.bpm)).toLowerCase();
                bool all = true;
                for (const auto& t : terms) if (! hay.contains(t)) { all = false; break; }
                if (all) out.add(e);
            }
            pend->fetch_sub(1);
            MessageManager::callAsync([safe, g, out] {
                if (safe == nullptr || g != safe->d->filterGen) return;
                safe->d->shown = out;
                safe->d->afterFilter();
            });
        });
    }
    void afterFilter() { trackList.clampOff(); repaintLists(); }

    int selIndex() const
    {
        if (selected == File()) return -1;
        for (int i = 0; i < shown.size(); ++i) if (shown.getReference(i).file == selected) return i;
        return -1;
    }
    const Entry* selEntry() const { const int i = selIndex(); return i >= 0 ? &shown.getReference(i) : nullptr; }

    // ---- loading
    int freeDeck() const
    {
        // the deck that is not playing (A if both are idle)
        const bool aPlaying = proc.status(0).playing.load(), bPlaying = proc.status(1).playing.load();
        return aPlaying && ! bPlaying ? 1 : 0;
    }
    void loadSel(int deck)
    {
        if (selected == File() || ! selected.existsAsFile()) { say("Pick a track first"); return; }
        proc.loadTrack(deck, selected);
        loadingWas[deck] = true;
        if (host.closeLibrary) host.closeLibrary();   // as the phone does
    }
    void say(const String& s) { note = s; noteAt = Time::getMillisecondCounter(); owner.repaint(statusArea.getSmallestIntegerContainer()); }

    // the encoder-style moves (LibraryView.midiMove / midiSelect / midiBack)
    void move(int delta)
    {
        if (focusList == 0) {
            if (crates.isEmpty()) return;
            const int p = jlimit(0, crates.size() - 1, crateSel + delta);
            if (p != crateSel) pickCrate(p);
            crateList.ensureVisible(p);
        } else {
            if (shown.isEmpty()) return;
            const int p = jlimit(0, shown.size() - 1, selIndex() + delta);
            selected = shown.getReference(p).file;
            trackList.ensureVisible(p);
            repaintLists();
        }
    }
    void intoTracks()
    {
        focusList = 1;
        if (selIndex() < 0 && ! shown.isEmpty()) { selected = shown.getReference(0).file; trackList.ensureVisible(0); }
        repaintLists();
    }

    using KeyListener::keyPressed;
    bool keyPressed(const KeyPress& k, Component* from) override
    {
        const int code = k.getKeyCode();
        const bool shift = k.getModifiers().isShiftDown();
        const bool inSearch = from == &search;
        if (shift && code == KeyPress::leftKey) { loadSel(0); return true; }
        if (shift && code == KeyPress::rightKey) { loadSel(1); return true; }
        if (code == KeyPress::escapeKey) { if (host.closeLibrary) host.closeLibrary(); return true; }
        if (code == KeyPress::upKey || code == KeyPress::downKey) {
            if (inSearch) focusList = 1;
            move(code == KeyPress::upKey ? -1 : 1);
            return true;
        }
        if (code == KeyPress::pageUpKey || code == KeyPress::pageDownKey) {
            auto& l = focusList == 0 ? crateList : trackList;
            const int page = jmax(1, (int) ((float) l.getHeight() / l.rowH) - 1);
            move(code == KeyPress::pageUpKey ? -page : page);
            return true;
        }
        if (inSearch) return false;   // the rest is typing
        if (code == KeyPress::returnKey) {
            if (focusList == 0) intoTracks();
            else if (selEntry() != nullptr) loadSel(freeDeck());
            return true;
        }
        if (code == KeyPress::rightKey || code == KeyPress::tabKey) { intoTracks(); return true; }
        if (code == KeyPress::leftKey || code == KeyPress::backspaceKey) { focusList = 0; repaintLists(); return true; }
        // typing anywhere goes to the search box
        const juce_wchar ch = k.getTextCharacter();
        if (ch >= ' ' && ! k.getModifiers().isCommandDown() && ! k.getModifiers().isCtrlDown()) {
            search.grabKeyboardFocus();
            search.insertTextAtCaret(String::charToString(ch));
            return true;
        }
        return false;
    }

    // the phone asks for a folder when the library opens without one
    void componentVisibilityChanged(Component& c) override
    {
        if (&c != &owner || ! owner.isShowing()) return;
        if (! askedForFolder && ! root.isDirectory() && ! proc.lastRoot.isDirectory() && rootGen > 0 && ! awaitingRoot()) { askedForFolder = true; chooseFolder(); }
        owner.grabKeyboardFocus();
    }

    void timerCallback() override
    {
        // the A / B marks follow the decks; the status shows a load in progress or its error
        bool changed = false;
        for (int d = 0; d < 2; ++d) {
            auto t = proc.deckTrack(d);
            const String p = t ? String::fromUTF8(t->path.c_str()) : String();
            if (p != deckPath[d]) { deckPath[d] = p; changed = true; }
            const bool l = proc.loading[d].load();
            if (l != loadingWas[d]) { loadingWas[d] = l; owner.repaint(statusArea.getSmallestIntegerContainer()); }
        }
        if (changed) trackList.repaint();
        if (note.isNotEmpty() && Time::getMillisecondCounter() - noteAt > 3000) { note.clear(); owner.repaint(statusArea.getSmallestIntegerContainer()); }
    }

    void repaintLists() { crateList.repaint(); trackList.repaint(); }

    // ---- painting (LibraryView.Row.onDraw)
    void paintCrate(Graphics& g, int i, int w, int h)
    {
        if (i < 0 || i >= crates.size()) return;
        const auto& c = crates.getReference(i);
        const bool sel = i == crateSel, focus = focusList == 0;
        g.fillAll(sel ? (focus ? lp::text : lp::selDim) : lp::panel);
        const Colour fg = sel && focus ? lp::black : lp::text, dim = sel && focus ? lp::selDimText : lp::dim;
        const float ts = jmin(15 * dp, (float) h * .42f), base = (float) h / 2 + ts * .36f;
        const float x = std::round(10 * dp) + (float) c.depth * std::round(16 * dp);
        const bool isCrate = c.kind == Crate::serato || c.kind == Crate::smart;
        const String icon = c.kind == Crate::all ? U("\xe2\x99\xab ") : isCrate ? (c.depth > 0 ? U("\xe2\x94\x94 ") : U("\xe2\x96\xb8 "))
                          : c.kind == Crate::playlist ? U("\xe2\x89\xa1 ") : U("\xe2\x96\xab ");
        String name = c.name;
        if (c.kind == Crate::serato && c.depth > 0) name = c.name.fromLastOccurrenceOf(" / ", false, false);
        if (name.isEmpty()) name = c.name;
        const Font f = bold(ts), fs = bold(ts * .8f);
        const String cnt = i < counts.size() && counts[i] >= 0 ? String(counts[i]) : String();
        const float cw = widthOf(fs, cnt) + 12 * dp;
        g.setColour(isCrate ? (sel && focus ? lp::black : lp::amber) : fg);
        textAt(g, f, fit(f, icon + name, (float) w - x - cw - 6 * dp), x, base);
        g.setColour(dim);
        textAt(g, fs, cnt, (float) w - cw + 4 * dp, base);
        if (sel) { g.setColour(lp::deck[0]); g.fillRect(0.0f, 0.0f, std::round(4 * dp), (float) h); }
    }

    void paintTrack(Graphics& g, int i, int w, int h)
    {
        if (i < 0 || i >= shown.size()) return;
        const auto& t = shown.getReference(i);
        const bool sel = selected != File() && t.file == selected, focus = focusList == 1;
        g.fillAll(sel ? (focus ? lp::text : lp::selDim) : ((i & 1) ? lp::odd : lp::panel));
        const Colour fg = sel && focus ? lp::black : lp::text, dim = sel && focus ? lp::selDimText : lp::dim;
        const float ts = jmin(15 * dp, (float) h * .42f), base = (float) h / 2 + ts * .36f;
        // columns: mark | title | artist | length | BPM | key
        const float mark = std::round(22 * dp), keyW = std::round(48 * dp), bpmW = std::round(56 * dp), lenW = std::round(52 * dp);
        const float rest = (float) w - mark - keyW - bpmW - lenW - std::round(10 * dp);
        const float titleW = rest * .56f, artistW = rest - titleW;
        const String path = t.file.getFullPathName();
        const int onDeck = path == deckPath[0] ? 0 : path == deckPath[1] ? 1 : -1;
        const Font f = bold(ts), f9 = bold(ts * .9f);
        if (onDeck >= 0) {
            g.setColour(lp::deck[onDeck]);
            g.fillRect(0.0f, 0.0f, std::round(4 * dp), (float) h);
            textAt(g, f9, onDeck == 0 ? "A" : "B", std::round(8 * dp), base);
        }
        float x = mark;
        g.setColour(fg);
        textAt(g, f, fit(f, label(t), titleW - 8 * dp), x, base);
        x += titleW;
        g.setColour(dim);
        textAt(g, f9, fit(f9, t.artist, artistW - 8 * dp), x, base);
        x += artistW;
        textAt(g, f9, fit(f9, lengthText(t.length), lenW - 6 * dp), x, base);
        x += lenW;
        g.setColour(fg);
        textAt(g, f9, fit(f9, bpmText(t.bpm), bpmW - 6 * dp), x, base);
        x += bpmW;
        const String key = camelotOf(t);
        if (key.isNotEmpty()) {
            const float kw = jmin(keyW - 6 * dp, widthOf(f9, key) + 10 * dp);
            g.setColour(keyColour(key));
            g.fillRoundedRectangle(Rectangle<float>(x, (float) h * .22f, kw, (float) h * .56f), std::round(3 * dp));
            g.setColour(lp::black);
            textAt(g, f9, fit(f9, key, kw), x + kw / 2, base, true);
        }
        g.setColour(lp::rule);
        g.fillRect(0, h - 1, w, 1);
    }

    String statusLine1() const
    {
        if (note.isNotEmpty()) return note;
        for (int d = 0; d < 2; ++d)
            if (proc.loading[d].load()) return String("Loading on ") + (d == 0 ? "A" : "B") + U("\xe2\x80\xa6");
        for (int d = 0; d < 2; ++d)
            if (proc.loadError[d].isNotEmpty()) return String(d == 0 ? "A: " : "B: ") + proc.loadError[d];
        return libStatus;
    }
    String treeName() const
    {
        if (! root.isDirectory()) return {};
        return root.getFileName().isNotEmpty() ? root.getFileName() : root.getFullPathName();
    }

    void paint(Graphics& g)
    {
        g.fillAll(lp::bg);
        box(g, searchBox, lp::well, lp::edgeHi, dp);
        if (search.isEmpty()) {   // the hint, clipped at the box like the phone's (not ellipsised)
            Graphics::ScopedSaveState s(g);
            auto r = search.getBounds().toFloat();
            g.reduceClipRegion(r.getSmallestIntegerContainer());
            g.setColour(lp::faint);
            textAt(g, regular(16 * dp), "Search title / artist / key / BPM", r.getX(), r.getCentreY() + 16 * dp * .36f);
        }
        // status: 12 sp bold, two lines from the top of the bar
        const float ts = 12 * dp;
        const Font f = bold(ts);
        g.setColour(lp::dim);
        const String l1 = statusLine1(), l2 = treeName();
        const float w = statusArea.getWidth();
        float y = statusArea.getY() + ts * 1.1f;
        // TextView maxLines 2: a long first line wraps into the second
        if (l2.isEmpty() && widthOf(f, l1) > w) {
            String a = fit(f, l1, w);
            const int cut = jmax(1, a.length() - 1);
            int sp = l1.substring(0, cut).lastIndexOfChar(' ');
            if (sp <= 0) sp = cut;
            textAt(g, f, l1.substring(0, sp), statusArea.getX(), y);
            textAt(g, f, fit(f, l1.substring(sp).trimStart(), w), statusArea.getX(), y + ts * 1.17f);
        } else {
            textAt(g, f, fit(f, l1, w), statusArea.getX(), y);
            if (l2.isNotEmpty()) textAt(g, f, fit(f, l2, w), statusArea.getX(), y + ts * 1.17f);
        }
    }

    void layout()
    {
        const int W = owner.getWidth(), H = owner.getHeight();
        dp = jmax(.5f, jmin((float) W / 800.0f, (float) H / 360.0f));
        auto px = [this](float v) { return (int) std::round(v * dp); };
        for (auto* b : { &back, &choose, &loadA, &loadB }) b->dp = dp;
        crateList.dp = trackList.dp = dp;
        crateList.rowH = (float) px(42);
        trackList.rowH = (float) px(44);
        search.setFont(regular(16 * dp));
        search.applyFontToAllText(regular(16 * dp));

        auto b = owner.getLocalBounds().reduced(px(6));
        auto bar = b.removeFromTop(px(48));
        const int gap = px(6);
        back.setBounds(bar.removeFromLeft(px(96))); bar.removeFromLeft(gap);
        choose.setBounds(bar.removeFromLeft(px(170))); bar.removeFromLeft(gap);
        loadB.setBounds(bar.removeFromRight(px(96))); bar.removeFromRight(gap);
        loadA.setBounds(bar.removeFromRight(px(96))); bar.removeFromRight(gap);
        // status (weight 1) | gap | search (weight 1.4)
        const int flex = jmax(0, bar.getWidth() - gap);
        const int sw = (int) std::round(flex * 1.4 / 2.4);
        auto s = bar.removeFromRight(sw);
        bar.removeFromRight(gap);
        statusArea = bar.toFloat();
        searchBox = s.toFloat();
        search.setBounds(s.reduced(px(10), 0));

        b.removeFromTop(px(6));
        const int cw = (int) std::round((b.getWidth() - gap) / 3.6);
        crateList.setBounds(b.removeFromLeft(cw));
        b.removeFromLeft(gap);
        trackList.setBounds(b);
        crateList.ensureVisible(crateSel);
    }
};

PhoneLibraryView::PhoneLibraryView(WheelProcessor& p, PhoneHost& h) { setOpaque(true); d = std::make_unique<Impl>(*this, p, h); }
PhoneLibraryView::~PhoneLibraryView() { d.reset(); }
void PhoneLibraryView::paint(Graphics& g) { d->paint(g); }
void PhoneLibraryView::resized() { d->layout(); }
File PhoneLibraryView::selectedFile() const { return d->selEntry() != nullptr ? d->selected : File(); }
String PhoneLibraryView::selectedTitle() const
{
    const auto* e = d->selEntry();
    if (e == nullptr) return {};
    return label(*e) + (e->artist.isEmpty() ? String() : U("  \xc2\xb7  ") + e->artist);
}
void PhoneLibraryView::refresh() { d->readCrates(false); }

}  // namespace wheel
