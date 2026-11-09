// one fuzz target for every reader of outside data: the first byte picks the reader
#include "../../core/SampleEdit.h"
#include "../../core/Pen.h"
using namespace cmi;
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n) {
    if (n < 1) return 0;
    const uint8_t k = d[0] % 9; d++; n--;
    const std::string txt((const char*) d, n);
    switch (k) {
        case 0: { Disk dk = readDiskImage(d, n); for (auto& f : dk.files) if (f.type == "VC") { Sound s; std::string e; vcToSound(f.data, f.name, dk.label, s, e); } break; }
        case 1: { std::vector<uint8_t> out; std::string e; int bad = 0, found = 0; disk::unpackHfe(d, n, out, e, &bad, &found); break; }
        case 2: { Sound s; std::string e; std::vector<uint8_t> v(d, d + n); if (vcToSound(v, "X", "D", s, e)) { SampleEdit ed; ed.s = s; ed.bakeStart(); ed.selectAll(); ed.slices(0); ed.slices(7); ed.normalise(); ed.reverse(); } break; }
        case 3: { WavData w = parseWav(d, n, 48000 * 6); if (w.err.empty() && !w.mono.empty() && w.rate > 0) { Sound s = drumToCmi(w.mono, w.rate, "F", "f.wav"); (void) s; } break; }
        case 4: { MidiSong ms; std::string e; if (parseSmf(d, n, "f", ms, e)) { notesOf(ms); } break; }
        case 5: { DrumKit kt; kt.load(txt); DrumKit k2; k2.load(kt.save()); break; }
        case 6: { Catalog c; c.load(d, n); break; }
        case 7: { PenDrawing p; p.load(txt); (void) p.save(); break; }
        case 8: { (void) partOfName(txt); (void) kitSampleName(Kit().bank, txt); break; }
    }
    return 0;
}
