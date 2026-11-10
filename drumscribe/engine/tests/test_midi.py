"""The MIDI file keeps the exact hit times (no quantizing) at any tempo."""
import mido
import pytest

from drumscribe import midi
from drumscribe.detect import Hit
from drumscribe.kit import NOTE_OF


def _hits():
    import random
    r = random.Random(4)
    out, t = [], 0.3
    pieces = list(NOTE_OF)
    while t < 30:
        h = Hit(stem="x", time=t, level_db=0, rise_db=10, end=t + 0.2)
        h.piece, h.velocity = r.choice(pieces), r.randint(1, 127)
        out.append(h)
        t += r.uniform(0.02, 0.4)
    return out


@pytest.mark.parametrize("bpm", [60.0, 97.37, 120.0, 174.0])
@pytest.mark.parametrize("split", [False, True])
def test_times_survive(tmp_path, bpm, split):
    hits = _hits()
    p = midi.write_midi(str(tmp_path / "d.mid"), hits, bpm, 31.0, split=split)
    mf = mido.MidiFile(p)
    assert mf.ticks_per_beat == midi.PPQ
    got = []
    for tr in mf.tracks:
        now = 0
        for m in tr:
            now += m.time
            if m.type == "note_on" and m.velocity > 0:
                assert m.channel == 9
                got.append((mido.tick2second(now, midi.PPQ, mido.bpm2tempo(bpm)), m.note, m.velocity))
    got.sort()
    want = sorted((h.time, NOTE_OF[h.piece], h.velocity) for h in hits)
    assert len(got) == len(want)
    half_tick = 60.0 / (bpm * midi.PPQ) / 2
    for (tg, ng, vg), (tw, nw, vw) in zip(got, want):
        assert abs(tg - tw) <= half_tick + 1e-9 and ng == nw and vg == vw
