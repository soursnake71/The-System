// random actions, as a person (or a bug) might do them, with the audio rendering in between; ASan / UBSan watch
#include "../../core/SampleEdit.h"
#include "../../core/Record.h"
#include "../../core/GridEdit.h"
#include <cstdio>
#include <chrono>
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
using namespace cmi;
static std::vector<uint8_t> rd(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {}); }
int main(int argc, char** argv) {
    const double secs = argc > 2 ? atof(argv[2]) : 120; const uint32_t seed = argc > 3 ? (uint32_t) atoi(argv[3]) : 1;
    Catalog cat; const std::string L = argv[1];
    for (const char* d : { "DRUMS", "DRUMS1", "CYMBALS", "PERCUSS1", "PERCUSS2", "STRINGS", "BASSES", "CHORAL" }) { auto b = rd(L + "/" + d + ".img"); cat.add(std::string(d) + ".img", b.data(), b.size()); }
    std::mt19937 r(seed); auto R = [&](int n) { return n <= 0 ? 0 : (int) (r() % (unsigned) n); };
    Kit kit; Engine e; e.bank = &kit.bank; e.kitVoice = kitLookup(kit.bank); e.prepare(48000); SongList songs; Recorder rec; PadEditor pe; GridEdit ge;
    std::vector<float> Lb(4096), Rb(4096);
    long ops = 0; int counts[40] {};
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
        const int op = R(36); ++counts[op]; ++ops;
        const int note = R(5) == 0 ? R(500) - 50 : padNote(R(PAD_BANKS), R(16));   // sometimes nonsense
        switch (op) {
            case 0: { DrumKit k = randomKit(cat, r()); loadKitVoices(k, kit, cat); applyPads(e, k); break; }
            case 1: { DrumKit k = kitPresets()[(size_t) R(100)].kit; fitKitToLibrary(k, cat); padsReady(k, kit, cat, false); applyPads(e, k); break; }
            case 2: { DrumKit k; padsReady(k, kit, cat); applyPads(e, k); break; }
            case 3: e.padPush(note, R(128)); break;
            case 4: e.padTap(note, 1 + R(127)); break;
            case 5: { DrumKit k = e.padKit; if (note >= 0 && note < KIT_SLOTS) { KitPiece& p = k.p[(size_t) note]; p.a = R(40) - 5; p.d = R(40) - 5; p.s = (R(13) - 1) / 10.f; p.r = R(40) - 5; p.f = R(300) - 20; p.lr = R(25) - 2; p.ld = R(15) - 2; p.tune = R(60) - 30; p.level = R(30) / 10.f; } for (float& v : k.env) v = R(40) - 5; for (float& v : k.fx) v = R(300) - 20; applyPads(e, k); break; }
            case 6: { DrumKit k = e.padKit; k.rrOn = R(2); if (note >= 0 && note < KIT_SLOTS) k.p[(size_t) note].rr = R(2) ? "KICK1@DRUMS;SNARE1@DRUMS1;NOPE@X;;" : ""; loadKitVoices(k, kit, cat); applyPads(e, k); break; }
            case 7: kit.clearUser(R(Library::USER_SLOTS + 4) - 2); pushKit(e); break;
            case 8: loadCatalogVoice(kit, cat, R((int) cat.voices.size() + 3) - 1); break;
            case 9: { std::vector<char> used = voicesInUse(kit, R(MAX_SOUNDS), e.seq, songs, &e.padKit); unloadUnusedLibrary(kit, used); pushKit(e); break; }
            case 10: kit.keepPlace(R(Library::USER_SLOTS), "STRNGENS", "STRINGS"); fillLibraryPlaces(kit, cat); break;
            case 11: if (!pe.open) pe.begin(kit, e, note); break;
            case 12: if (pe.open) { pe.ed.select(R(pe.ed.len() + 200) - 100, R(pe.ed.len() + 200) - 100); switch (R(12)) { case 0: pe.ed.trim(); break; case 1: pe.ed.cut(); break; case 2: pe.ed.silence(); break; case 3: pe.ed.normalise(); break; case 4: pe.ed.reverse(); break; case 5: pe.ed.fadeIn(); break; case 6: pe.ed.fadeOut(); break; case 7: pe.ed.gain(R(60) - 30); break; case 8: pe.ed.loopSelection(); break; case 9: pe.ed.noLoop(); break; case 10: pe.ed.undoLast(); break; default: pe.ed.selectAll(); } pe.commit(kit, e); } break;
            case 13: if (pe.open) { int made = 0; pe.chop(kit, e, R(3) ? R(20) : 0, &made); } break;
            case 14: if (pe.open) { if (R(2)) pe.revert(kit, e); else pe.close(); } break;
            case 15: { MidiSong ms = emptySong("S" + std::to_string(ops), 40 + R(200), 1 + R(16)); std::vector<SongNote> nn; for (int i = 0, m = R(300); i < m; ++i) nn.push_back({ R(SONG_TRK), R(64) / 4.0, R(16) / 8.0, R(128), 1 + R(127) }); for (int t = 0; t < SONG_TRK; ++t) ms.used[t] = R(2); setNotes(ms, nn); ms.raw = writeSmf(ms); int dup; songs.addUnique(ms, kit.bank, dup); break; }
            case 16: { const int i = R(songs.size() + 2) - 1; SongEntry* se = songs.at(i); if (se) ensureConnected(*se, kit, cat, {}, R(2)); selectSong(e, se); break; }
            case 17: e.songRun = R(2); e.songLoop = R(2); e.clickOn = R(2); if (R(4) == 0) e.songStartReq = R(40) - 8; break;
            case 18: if (SongEntry* se = songs.at(songs.indexOf(e.song.load()))) { if (!rec.on.load()) rec.start(e, se, R(SONG_TRK), R(8)); else rec.stop(e, se); } break;
            case 19: rec.poll(e); break;
            case 20: if (SongEntry* se = songs.at(songs.indexOf(e.song.load()))) { ge.setNotes(notesOf(*se->song)); const int tr = R(SONG_TRK); switch (R(8)) { case 0: ge.selectAll(tr); ge.erase(); break; case 1: ge.humanize(tr, R(10) / 100.0, R(30), r()); break; case 2: ge.duplicate(R(3) / 4.0); break; case 3: ge.copy(); ge.paste(tr, R(16)); break; case 4: ge.quantize(tr, R(3) / 4.0); break; case 5: ge.legato(tr); break; case 6: ge.transpose(tr, R(50) - 25); break; default: ge.stretch(tr, R(30) / 10.0); } editSong(e, se, ge.notes); } break;
            case 21: if (SongEntry* se = songs.at(songs.indexOf(e.song.load()))) undoSong(e, se); break;
            case 22: { const int i = R(songs.size()); if (songs.at(i) && songs.at(i)->song.get() == e.song.load()) selectSong(e, nullptr); songs.remove(i); break; }
            case 23: { std::vector<KitSample> ks; for (int i = 0, m = R(6); i < m; ++i) { KitSample s; s.file = std::string(R(2) ? "Kick " : "") + "x" + std::to_string(R(99)) + ".wav"; s.rate = 8000 + R(90000); s.mono.resize((size_t) R(100000)); for (auto& v : s.mono) v = (R(2000) - 1000) / 900.f; ks.push_back(s); } DrumKit k = e.padKit; importKitSamples(ks, kit, k, R(2)); applyPads(e, k); break; }
            case 24: resetEngineSettings(e); break;
            case 25: { uint8_t m[3] = { (uint8_t) r(), (uint8_t) (r() & 0x7f), (uint8_t) (r() & 0x7f) }; e.midi(m, 1 + R(3)); break; }
            case 26: e.allOff(R(2)); break;
            case 27: { DrumKit k; k.load(e.padKit.save()); applyPads(e, k); break; }
            case 28: e.kickLift = R(40) / 10.f; e.humanize = R(12) / 10.f; break;
            case 29: e.loopBlend = R(12) / 10.f; e.ch10Kit = R(2); break;
            case 30: { Sound s; s.name = "U" + std::to_string(R(50)); s.category = "User"; s.rate = 4000 + R(40000); s.root = R(128); s.ram.assign((size_t) (PAGE * (1 + R(128))), (uint8_t) R(256)); s.loopStart = R(140) - 10; s.loopEnd = R(140) - 10; s.startPage = R(140) - 5; kit.setUser(R(Library::USER_SLOTS), s); break; }
            case 31: { e.seq.trk[R(TRACKS)].sound = R(MAX_SOUNDS + 4) - 2; e.seqRun = R(2); e.curPattern = R(NPAT); break; }
            case 32: { const int n = R(4096) + (R(20) == 0 ? 0 : 1); e.render(Lb.data(), Rb.data(), n > 4096 ? 4096 : n); break; }
            case 33: { Catalog c2; const auto b = cat.save(); c2.load(b.data(), (size_t) R((int) b.size() + 1)); break; }
            case 34: e.setKeyboard(R(MAX_SOUNDS + 4) - 2, Controls()); e.keyOn(R(128), R(100) / 100.f); if (R(2)) e.keyOff(R(128)); break;
            default: e.render(Lb.data(), Rb.data(), 256); break;
        }
        for (int i = 0; i < 4096; i += 4096) for (float v : Lb) if (!std::isfinite(v)) { std::printf("NON-FINITE AUDIO after op %d\n", op); return 2; }
    }
    std::printf("seed %u: %ld random actions in %.0f s: no crash, the audio always finite\n", seed, ops, secs);
}
