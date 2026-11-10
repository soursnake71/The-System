"""Command line: python -m drumscribe <link or audio file> [-o folder] [--fast] [--tempo 120]
Starts the server instead with: python -m drumscribe serve [--port 8765]"""
import argparse
import hashlib
import os
import shutil
import sys


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv and argv[0] == "serve":
        from .server import main as serve
        return serve(argv[1:])
    ap = argparse.ArgumentParser(prog="drumscribe", description="Song link or audio file in, drum MIDI out.")
    ap.add_argument("source", help="YouTube / Spotify / SoundCloud link, or an audio file")
    ap.add_argument("-o", "--out", default=".", help="folder for the MIDI files (default: here)")
    ap.add_argument("--fast", action="store_true", help="faster, slightly less accurate separation")
    ap.add_argument("--tempo", type=float, help="write the MIDI at your project's tempo (default: detected)")
    ap.add_argument("--keep-stems", action="store_true", help="also save the drum stem and the kit stems as WAV")
    a = ap.parse_args(argv)
    from . import pipeline
    from .server import work_root

    key = hashlib.sha1((a.source.strip() + ("|fast" if a.fast else "")).encode()).hexdigest()[:16]
    work = os.path.join(work_root(), key)
    res = pipeline.run(a.source, work, "fast" if a.fast else "best", a.tempo,
                       progress=lambda s, f, m: print(f"\r[{f * 100:5.1f}%] {m:70s}", end="", flush=True),
                       log=lambda m: print("\n" + m))
    print()
    os.makedirs(a.out, exist_ok=True)
    for f in (res["midi"], res["midi_split"]):
        shutil.copy(os.path.join(work, f), os.path.join(a.out, f))
        print("wrote", os.path.join(a.out, f))
    if a.keep_stems:
        pipeline.export_stems(work, a.out)
    print(f"{res['tempo']} BPM (set your project to this tempo before importing)")
    for p in res["pieces"]:
        print(f"  {p['label']:16s} note {p['note']:3d}  {p['count']:5d} hits")


if __name__ == "__main__":
    sys.exit(main())
