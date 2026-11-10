"""MIDI file writing. Notes go at their real times (no quantizing): the file's tempo only sets where the DAW's grid
falls, and at 1920 ticks per beat one tick is 0.26 ms at 120 BPM.

DAWs differ in how they import tempo: some (Ableton Live, FL Studio) ignore the file's tempo and place notes by beats.
So the file is written at the tempo your project will use (detected, or the one you give), and the times are
converted with that same tempo: set the project to that tempo and the notes land exactly on the original hits.
"""
from __future__ import annotations

import mido

from .kit import LABEL_OF, NOTE_OF, PIECES

PPQ = 1920
CHANNEL = 9  # MIDI channel 10, the drum channel


def _events(hits, tempo_bpm, note_len_max=0.12):
    ev = []
    # a MIDI tempo is whole microseconds per beat: convert with exactly the tempo that goes in the file
    sec_per_tick = mido.bpm2tempo(tempo_bpm) / 1e6 / PPQ
    for h in hits:
        on = int(round(h.time / sec_per_tick))
        dur = max(0.010, min(note_len_max, h.end - h.time - 0.002))
        off = max(on + 1, int(round((h.time + dur) / sec_per_tick)))
        n = NOTE_OF[h.piece]
        ev.append((on, 1, mido.Message("note_on", channel=CHANNEL, note=n, velocity=h.velocity)))
        ev.append((off, 0, mido.Message("note_off", channel=CHANNEL, note=n, velocity=0)))
    ev.sort(key=lambda e: (e[0], e[1]))  # note-offs before note-ons on the same tick
    return ev


def _track(name, events, tempo_bpm=None, length_ticks=None):
    tr = mido.MidiTrack()
    tr.append(mido.MetaMessage("track_name", name=name, time=0))
    if tempo_bpm:
        tr.append(mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(tempo_bpm), time=0))
        tr.append(mido.MetaMessage("time_signature", numerator=4, denominator=4, time=0))
    now = 0
    for t, _, msg in events:
        tr.append(msg.copy(time=t - now))
        now = t
    end = max(now, length_ticks or 0)
    tr.append(mido.MetaMessage("end_of_track", time=end - now))
    return tr


def write_midi(path, hits, tempo_bpm: float, duration: float, split=False, title="Drums"):
    """hits: flat list of Hit with piece/velocity set. split=True writes one track per kit piece."""
    mf = mido.MidiFile(type=1, ticks_per_beat=PPQ)
    length = int(round(duration * 1e6 / mido.bpm2tempo(tempo_bpm) * PPQ))
    if not split:
        mf.tracks.append(_track(title, _events(hits, tempo_bpm), tempo_bpm, length))
    else:
        mf.tracks.append(_track(title, [], tempo_bpm, length))
        for piece in PIECES:
            ph = [h for h in hits if h.piece == piece]
            if ph:
                mf.tracks.append(_track(LABEL_OF[piece], _events(ph, tempo_bpm), None, length))
    mf.save(path)
    return path
