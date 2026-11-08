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
    // back as it was before editing
    void revert(Kit& kit, Engine& eng) {
        if (!open) return;
        if (newSlot) kit.clearUser(slot); else kit.setUser(slot, orig);
        DrumKit k = eng.padKit; k.p[(size_t) note] = before; applyPads(eng, k);
        open = false;
    }
};

}  // namespace cmi
