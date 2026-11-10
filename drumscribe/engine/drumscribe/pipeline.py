"""Link (or file) in, drum MIDI out."""
from __future__ import annotations

import json
import os
import shutil
import time

import numpy as np
import soundfile as sf

from . import articulate, detect, fetch, midi, separate, tempo
from .kit import LABEL_OF, NOTE_OF, PIECES, STEMS

STAGES = [("fetch", 0.0, 0.08), ("drums", 0.08, 0.50), ("kit", 0.50, 0.92), ("hits", 0.92, 1.0)]


def transcribe_stems(stems: dict, sr: int, duration: float, tempo_bpm=None, progress=None):
    hits = detect.detect_all(stems, sr, progress)
    articulate.articulate(hits, stems, sr)
    flat = sorted((h for hl in hits.values() for h in hl), key=lambda h: h.time)
    bpm = float(tempo_bpm) if tempo_bpm else tempo.estimate_tempo(flat, duration)
    return flat, bpm


def write_outputs(flat, bpm, duration, outdir, title):
    os.makedirs(outdir, exist_ok=True)
    name = fetch.safe_name(title)
    m1 = midi.write_midi(os.path.join(outdir, f"{name} - drums.mid"), flat, bpm, duration, title=f"{title} drums")
    m2 = midi.write_midi(os.path.join(outdir, f"{name} - drums (one track per piece).mid"), flat, bpm, duration,
                         split=True, title=f"{title} drums")
    counts = {p: sum(h.piece == p for h in flat) for p in PIECES}
    result = {
        "title": title,
        "duration": round(duration, 3),
        "tempo": bpm,
        "midi": os.path.basename(m1),
        "midi_split": os.path.basename(m2),
        "pieces": [{"piece": p, "label": LABEL_OF[p], "note": NOTE_OF[p], "count": c} for p, c in counts.items() if c],
        "hits": [[round(h.time, 5), NOTE_OF[h.piece], h.velocity] for h in flat],
    }
    with open(os.path.join(outdir, "result.json"), "w") as f:
        json.dump(result, f)
    return result


def export_stems(workdir, outdir):
    """The kit stems (kick, snare, toms, hh, ride, crash) and the whole drum stem as 32-bit WAVs, for checking the
    MIDI against what was heard."""
    z = np.load(os.path.join(workdir, "stems.npz"))
    meta = json.load(open(os.path.join(workdir, "source.json")))
    name = fetch.safe_name(meta["title"])
    paths = []
    for k in STEMS:
        p = os.path.join(outdir, f"{name} - {k}.wav")
        sf.write(p, z[k].astype(np.float32), int(meta["sr"]), subtype="FLOAT")
        paths.append(p)
    d = os.path.join(workdir, "drums.wav")
    if os.path.exists(d):
        p = os.path.join(outdir, f"{name} - drums.wav")
        shutil.copy(d, p)
        paths.append(p)
    return paths


def run(link: str, workdir: str, quality="best", tempo_bpm=None, progress=None, log=print):
    """progress(stage, fraction_of_whole_job, message)"""
    t0 = time.time()

    def stage(i, frac, msg=""):
        name, a, b = STAGES[i]
        if progress:
            progress(name, a + (b - a) * frac, msg)

    os.makedirs(workdir, exist_ok=True)
    cache = os.path.join(workdir, "stems.npz")
    meta_path = os.path.join(workdir, "source.json")
    if os.path.exists(cache) and os.path.exists(meta_path):
        meta = json.load(open(meta_path))
        z = np.load(cache)
        stems = {k: z[k].astype(np.float32) for k in STEMS}
        sr, title, duration = int(meta["sr"]), meta["title"], meta["duration"]
        stage(2, 1.0, "Using the stems from last time")
    else:
        stage(0, 0.0, "Getting the audio")
        src = fetch.fetch(link, workdir, log=lambda m: (log(m), stage(0, 0.5, m)))
        info = sf.info(src.path)
        duration, title = info.duration, src.title
        stage(1, 0.0, "Taking the drums out of the mix")
        drums_wav = os.path.join(workdir, "drums.wav")
        separate.extract_drums(src.path, drums_wav, quality, progress=lambda f: stage(1, f, "Taking the drums out of the mix"))
        stage(2, 0.0, "Splitting the kit: kick, snare, toms, hi-hat, ride, crash")
        stems = separate.split_kit(drums_wav, os.path.join(workdir, "kit"), progress=lambda f: stage(2, f))
        sr = separate.SR
        np.savez(cache, **{k: v.astype(np.float16) for k, v in stems.items()})
        json.dump({"sr": sr, "title": title, "duration": duration, "origin": link}, open(meta_path, "w"))
        try:
            os.remove(src.path)  # the stems are kept; the full song isn't needed any more
        except OSError:
            pass
    stage(3, 0.0, "Finding every hit")
    flat, bpm = transcribe_stems(stems, sr, duration, tempo_bpm, progress=lambda f: stage(3, 0.9 * f))
    result = write_outputs(flat, bpm, duration, workdir, title)
    stage(3, 1.0, f"{len(flat)} hits")
    log(f"Done in {time.time() - t0:.0f} s: {len(flat)} hits, {bpm} BPM")
    return result
