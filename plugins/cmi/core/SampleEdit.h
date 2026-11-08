// The sample editor: a voice's 16 KB of 8-bit RAM edited as the CMI's waveform pages, a selection on it, and the
// edits: trim to the selection, cut it, silence it, normalise, reverse, fade in / out, gain, loop the selection or
// none; undo. PadEditor opens a pad's sound (a working copy as a voice of your own, on the pad while you edit, so the
// pad plays the edit), keeps it when you close, or puts the pad back as it was (REVERT).
#pragma once
#include "Pads.h"

namespace cmi {

struct SampleEdit {
    Sound s;
    std::vector<Sound> undo;
    int a = 0, b = 0;   // the selection, in samples (b exclusive)

    int len() const { return (int) s.ram.size(); }
    void select(int x, int y) { if (x > y) std::swap(x, y); a = clampi(x, 0, len()); b = clampi(y, 0, len()); if (b - a < 2) { a = 0; b = len(); } }
    void selectAll() { a = 0; b = len(); }
    bool any() const { return b - a >= 2; }
    void push() { undo.push_back(s); if (undo.size() > 40) undo.erase(undo.begin()); }
    bool undoLast() { if (undo.empty()) return false; s = undo.back(); undo.pop_back(); a = std::min(a, len()); b = std::min(b, len()); if (!any()) selectAll(); return true; }

    int at(int i) const { return (int) s.ram[(size_t) i] - 128; }
    void put(int i, double v) { s.ram[(size_t) i] = (uint8_t) clampi((int) std::lround(v) + 128, 1, 255); }
    // whole pages again (the card plays pages), the loop and start kept inside the sound
    void fix() {
        if (s.ram.empty()) s.ram.assign((size_t) PAGE, 0x80);
        s.ram.resize((size_t) ((len() + PAGE - 1) / PAGE * PAGE), 0x80);
        if (len() > RAM_SIZE) s.ram.resize((size_t) RAM_SIZE);
        const int pages = s.pages();
        if (s.loopStart >= pages || s.loopEnd >= pages) { s.loopStart = s.loopEnd = -1; }
        s.startPage = clampi(s.startPage, 0, pages - 1);
        a = std::min(a, len()); b = std::min(b, len()); if (!any()) selectAll();
    }
    // the silence a voice was sampled with ahead of its sound: taken out for good
    void bakeStart() { if (s.startPage <= 0) return; const int n = s.startPage * PAGE; s.ram.erase(s.ram.begin(), s.ram.begin() + std::min(n, len())); if (s.loopStart >= 0) { s.loopStart -= s.startPage; s.loopEnd -= s.startPage; if (s.loopStart < 0) s.loopStart = s.loopEnd = -1; } s.startPage = 0; fix(); }

    void trim() { if (!any()) return; push(); std::vector<uint8_t> r(s.ram.begin() + a, s.ram.begin() + b); s.ram = r; s.loopStart = s.loopEnd = -1; a = 0; b = (int) r.size(); fix(); }
    void cut() { if (!any() || b - a >= len()) return; push(); s.ram.erase(s.ram.begin() + a, s.ram.begin() + b); s.loopStart = s.loopEnd = -1; b = a; fix(); }
    void silence() { push(); for (int i = a; i < b; ++i) s.ram[(size_t) i] = 0x80; }
    void normalise() { push(); int pk = 1; for (int i = a; i < b; ++i) pk = std::max(pk, std::abs(at(i))); const double g = 127.0 / pk; for (int i = a; i < b; ++i) put(i, at(i) * g); }
    void reverse() { push(); std::reverse(s.ram.begin() + a, s.ram.begin() + b); }
    void fadeIn() { push(); const int n = std::max(1, b - a); for (int i = a; i < b; ++i) put(i, at(i) * (double) (i - a) / n); }
    void fadeOut() { push(); const int n = std::max(1, b - a); for (int i = a; i < b; ++i) put(i, at(i) * (double) (b - 1 - i) / n); }
    void gain(double db) { push(); const double g = std::pow(10.0, db / 20); for (int i = a; i < b; ++i) put(i, at(i) * g); }
    void loopSelection() { push(); s.loopStart = a / PAGE; s.loopEnd = std::max(s.loopStart, (b - 1) / PAGE); s.ctl.loop = 1; }
    void noLoop() { push(); s.loopStart = s.loopEnd = -1; }
    // chopping: where the selection splits, into n equal slices, or (n 0) at each hit (a sharp rise, 50 ms apart at
    // least), at most 16; each slice [start, end)
    std::vector<std::pair<int, int>> slices(int n) const {
        std::vector<int> cuts;
        if (n > 0) for (int k = 0; k < n; ++k) cuts.push_back(a + (int) ((int64_t) (b - a) * k / n));
        else {
            const int win = std::max(32, (int) (s.rate * 0.005)), gap = (int) (s.rate * 0.05);
            int pk = 1; for (int i = a; i < b; ++i) pk = std::max(pk, std::abs(at(i)));
            double prev = 0; int last = -gap;
            for (int i = a; i + win <= b; i += win) {
                double e = 0; for (int j = i; j < i + win; ++j) e += std::abs(at(j)); e /= win;
                if (e > pk * 0.12 && e > prev * 2.0 && i - last >= gap) { cuts.push_back(i); last = i; }
                prev = e;
            }
            if (cuts.empty() || cuts[0] > a + win) cuts.insert(cuts.begin(), a);
        }
        if (cuts.size() > 16) cuts.resize(16);
        std::vector<std::pair<int, int>> o;
        for (size_t k = 0; k < cuts.size(); ++k) { const int e = k + 1 < cuts.size() ? cuts[k + 1] : b; if (e - cuts[k] >= PAGE / 2) o.push_back({ cuts[k], e }); }
        return o;
    }
    // a slice as a voice of its own: its samples, whole pages, a short fade at its end so it doesn't click
    Sound slice(int from, int to, const std::string& name) const {
        Sound o = s; o.name = name; o.category = "User"; o.title = "Slice of " + s.name; o.fromLibrary = false; o.waiting = false; o.startPage = 0; o.loopStart = o.loopEnd = -1;
        o.ram.assign(s.ram.begin() + from, s.ram.begin() + to);
        const int fl = std::min((int) o.ram.size() / 4, (int) (s.rate * 0.004)); const int n = (int) o.ram.size();
        for (int i = 0; i < fl; ++i) { const int k = n - 1 - i; o.ram[(size_t) k] = (uint8_t) clampi((int) std::lround(((int) o.ram[(size_t) k] - 128) * (double) i / fl) + 128, 1, 255); }
        o.ram.resize((size_t) ((n + PAGE - 1) / PAGE * PAGE), 0x80);
        return o;
    }
    // the 8-bit samples to draw (-128..127)
    std::vector<int8_t> view() const { std::vector<int8_t> v((size_t) len()); for (int i = 0; i < len(); ++i) v[(size_t) i] = (int8_t) at(i); return v; }
};

struct PadEditor {
    SampleEdit ed;
    int note = -1, slot = -1; bool newSlot = false, open = false;
    KitPiece before; Sound orig;
    // the pad's sound (its own, or the CMI drum map's) for editing; false: the pad has none, or no voice is free
    bool begin(Kit& kit, Engine& eng, int n) {
        if (n < 0 || n > 127 || !eng.bank) return false;
        int idx = eng.kitSound[(size_t) n].load(); if (idx < 0 && eng.kitVoice) idx = eng.kitVoice(n);
        const Sound* src = idx >= 0 ? kit.bank.at(idx) : nullptr;
        if (!src || isBlank(kit, idx)) return false;
        const int k = Library::userOf(idx);
        newSlot = !(k >= 0 && !src->fromLibrary && src->category == "User");
        slot = newSlot ? kit.firstFree() : k; if (slot < 0) return false;
        note = n; before = eng.padKit.p[(size_t) n]; orig = *src;
        ed = SampleEdit(); ed.s = *src; ed.s.fromLibrary = false; ed.s.waiting = false;
        if (newSlot) { ed.s.name = kitSampleName(kit.bank, src->name); ed.s.category = "User"; ed.s.title = "Edited from " + src->name + (src->category.empty() ? "" : " (" + src->category + ")"); }
        ed.bakeStart(); ed.selectAll();
        open = true; commit(kit, eng);
        return true;
    }
    // the edit as it stands: into its voice, on the pad
    void commit(Kit& kit, Engine& eng) {
        if (!open) return;
        kit.setUser(slot, ed.s);
        DrumKit k = eng.padKit; k.p[(size_t) note].voice = ed.s.name + "@User"; applyPads(eng, k);
    }
    void close() { open = false; }
    // CHOP: the selection's slices (n equal, or 0: at its hits) as voices of your own on a free bank's pads (D onwards),
    // in order; the pad being edited keeps its sound. Returns the bank, or -1 (no slices, no free bank or voices)
    int chop(Kit& kit, Engine& eng, int n, int* count = nullptr) {
        if (!open) return -1;
        const auto sl = ed.slices(n); if (sl.empty()) return -1;
        int bank = -1;
        for (int b = 3; b < PAD_BANKS && bank < 0; ++b) { bool free = true; for (int i = 0; i < 16; ++i) free &= eng.padKit.p[(size_t) padNote(b, i)].voice.empty(); if (free) bank = b; }
        if (bank < 0) return -1;
        DrumKit k = eng.padKit; int made = 0;
        const std::string base = ed.s.name.substr(0, 5);
        for (size_t i = 0; i < sl.size() && i < 16; ++i) {
            const int slot = kit.firstFree(); if (slot < 0) break;
            const std::string nm = kitSampleName(kit.bank, base + std::to_string(i + 1));
            kit.setUser(slot, ed.slice(sl[i].first, sl[i].second, nm));
            k.p[(size_t) padNote(bank, (int) i)].voice = nm + "@User"; ++made;
        }
        applyPads(eng, k);
        if (count) *count = made;
        return made ? bank : -1;
    }
    // back as it was before editing
    void revert(Kit& kit, Engine& eng) {
        if (!open) return;
        if (newSlot) kit.clearUser(slot); else kit.setUser(slot, orig);
        DrumKit k = eng.padKit; k.p[(size_t) note] = before; applyPads(eng, k);
        open = false;
    }
};

}  // namespace cmi
