"""The engine as a small HTTP server, used by the Android app and the VST3 plugin (and any browser: it serves a
page at /).

  POST /api/jobs            {"link": "...", "quality": "best"|"fast", "tempo": null|120.0}  -> {"id": ...}
  POST /api/upload?name=song.mp3&quality=best&tempo=   (body: the audio file)              -> {"id": ...}
  GET  /api/jobs/<id>       status: state (queued/running/done/error), stage, progress 0..1, message, result
  GET  /api/jobs/<id>/midi  the MIDI file (?split=1: one track per kit piece)
  GET  /api/jobs/<id>/drums the separated drum stem (WAV)
  POST /api/jobs/<id>/tempo {"tempo": 97.5} rewrites the MIDI at another tempo (seconds: the stems are kept)
  GET  /api/health

Jobs run one at a time (separation uses all the CPU / GPU). A link that was done before is answered from the cache.
Set DRUMSCRIBE_TOKEN to require ?token=... (or an X-Token header) from clients.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import queue
import re
import socket
import threading
import time
import traceback
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import __version__

JOBS: dict[str, dict] = {}
LOCK = threading.Lock()
Q: "queue.Queue[str]" = queue.Queue()


def work_root():
    d = os.environ.get("DRUMSCRIBE_WORK") or os.path.join(os.path.expanduser("~"), ".cache", "drumscribe", "jobs")
    os.makedirs(d, exist_ok=True)
    return d


def _job_id(source: str, quality: str):
    return hashlib.sha1((source.strip() + ("|fast" if quality == "fast" else "")).encode()).hexdigest()[:16]


def _public(job, hits=False):
    j = {k: v for k, v in job.items() if not k.startswith("_")}
    if j.get("result") and not hits:
        j["result"] = {k: v for k, v in j["result"].items() if k != "hits"}
    return j


def submit(source: str, quality="best", tempo=None, label=None):
    jid = _job_id(source, quality)
    work = os.path.join(work_root(), jid)
    with LOCK:
        job = JOBS.get(jid)
        if job and job["state"] in ("queued", "running"):
            return jid
        job = {"id": jid, "source": label or source, "state": "queued", "stage": "queued", "progress": 0.0,
               "message": "Waiting", "result": None, "error": None, "created": time.time(),
               "_src": source, "_work": work, "_quality": quality, "_tempo": tempo}
        JOBS[jid] = job
    Q.put(jid)
    return jid


def _worker():
    from . import pipeline

    while True:
        jid = Q.get()
        job = JOBS[jid]
        job.update(state="running", message="Starting")

        def prog(stage, frac, msg):
            job["stage"] = stage
            job["progress"] = round(float(frac), 4)
            if msg:
                job["message"] = msg

        try:
            res = pipeline.run(job["_src"], job["_work"], job["_quality"], job["_tempo"], progress=prog,
                               log=lambda m: job.update(message=m.splitlines()[0][:200]))
            job.update(state="done", progress=1.0, stage="done", result=res,
                       message=f"{len(res['hits'])} hits, {res['tempo']} BPM")
        except Exception as e:  # report anything to the client
            traceback.print_exc()
            job.update(state="error", error=str(e), message=str(e).splitlines()[0][:300] if str(e) else type(e).__name__)
        finally:
            Q.task_done()


def _lan_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


PAGE = os.path.join(os.path.dirname(__file__), "..", "..", "core", "web", "index.html")  # shared with the C++ server


class Handler(BaseHTTPRequestHandler):
    server_version = f"DrumScribe/{__version__}"

    def log_message(self, fmt, *args):
        pass

    # --- helpers
    def _send(self, code, body, ctype="application/json", extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, X-Token")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _err(self, code, msg):
        self._send(code, {"error": msg})

    def _authed(self, qs):
        tok = os.environ.get("DRUMSCRIBE_TOKEN")
        if not tok:
            return True
        return self.headers.get("X-Token") == tok or qs.get("token", [""])[0] == tok

    def _body(self, limit=600 * 1024 * 1024):
        n = int(self.headers.get("Content-Length") or 0)
        if n > limit:
            raise ValueError("file too big")
        return self.rfile.read(n) if n else b""

    def _file(self, path, ctype, name):
        if not os.path.exists(path):
            return self._err(404, "not there")
        with open(path, "rb") as f:
            data = f.read()
        q = urllib.parse.quote(name)
        self._send(200, data, ctype, {"Content-Disposition": f"attachment; filename*=UTF-8''{q}"})

    # --- routes
    def do_OPTIONS(self):
        self._send(204, b"", "text/plain")

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        qs = urllib.parse.parse_qs(u.query)
        if u.path in ("/", "/index.html"):
            with open(PAGE, "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")
        if u.path == "/api/health":
            return self._send(200, {"ok": True, "version": __version__, "queue": Q.qsize(),
                                    "auth": bool(os.environ.get("DRUMSCRIBE_TOKEN"))})
        if not self._authed(qs):
            return self._err(401, "wrong or missing token")
        m = re.fullmatch(r"/api/jobs/([0-9a-f]{16})(/midi|/drums)?", u.path)
        if u.path == "/api/jobs":
            return self._send(200, [_public(j) for j in sorted(JOBS.values(), key=lambda j: -j["created"])])
        if not m:
            return self._err(404, "no such page")
        job = JOBS.get(m.group(1))
        if not job:
            # finished in an earlier run of the server: load it from the cache
            work = os.path.join(work_root(), m.group(1))
            rp = os.path.join(work, "result.json")
            if not os.path.exists(rp):
                return self._err(404, "no such job")
            res = json.load(open(rp))
            job = JOBS[m.group(1)] = {"id": m.group(1), "source": res.get("title"), "state": "done", "stage": "done",
                                      "progress": 1.0, "message": "", "result": res, "error": None,
                                      "created": os.path.getmtime(rp), "_work": work, "_src": "", "_quality": "best",
                                      "_tempo": None}
        if m.group(2) is None:
            return self._send(200, _public(job, hits=qs.get("hits", ["0"])[0] == "1"))
        if job["state"] != "done":
            return self._err(409, "not finished yet")
        if m.group(2) == "/midi":
            name = job["result"]["midi_split" if qs.get("split", ["0"])[0] == "1" else "midi"]
            return self._file(os.path.join(job["_work"], name), "audio/midi", name)
        return self._file(os.path.join(job["_work"], "drums.wav"), "audio/wav", f"{job['result']['title']} - drums.wav")

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        qs = urllib.parse.parse_qs(u.query)
        if not self._authed(qs):
            return self._err(401, "wrong or missing token")
        try:
            if u.path == "/api/jobs":
                d = json.loads(self._body(1 << 16) or b"{}")
                link = (d.get("link") or "").strip()
                if not re.match(r"^(https?://|spotify:)", link):
                    return self._err(400, "Paste a YouTube, Spotify or SoundCloud link.")
                tempo = float(d["tempo"]) if d.get("tempo") else None
                return self._send(200, {"id": submit(link, d.get("quality", "best"), tempo)})
            if u.path == "/api/upload":
                name = os.path.basename(qs.get("name", ["song"])[0]) or "song"
                data = self._body()
                if not data:
                    return self._err(400, "empty file")
                h = hashlib.sha1(data).hexdigest()[:16]
                up = os.path.join(work_root(), "uploads")
                os.makedirs(up, exist_ok=True)
                ext = os.path.splitext(name)[1][:8] or ".audio"
                path = os.path.join(up, h + ext)
                with open(path, "wb") as f:
                    f.write(data)
                tempo = qs.get("tempo", [""])[0]
                jid = submit(path, qs.get("quality", ["best"])[0], float(tempo) if tempo else None,
                             label=os.path.splitext(name)[0])
                return self._send(200, {"id": jid})
            m = re.fullmatch(r"/api/jobs/([0-9a-f]{16})/tempo", u.path)
            if m:
                job = JOBS.get(m.group(1))
                if not job or job["state"] != "done":
                    return self._err(409, "not finished yet")
                d = json.loads(self._body(1 << 16) or b"{}")
                from . import pipeline
                res = pipeline.run(job["_src"], job["_work"], tempo_bpm=float(d["tempo"]) if d.get("tempo") else None,
                                   log=lambda m: None)
                job["result"] = res
                return self._send(200, _public(job))
        except (ValueError, KeyError, json.JSONDecodeError) as e:
            return self._err(400, str(e))
        return self._err(404, "no such page")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="drumscribe serve")
    ap.add_argument("--host", default="0.0.0.0", help="0.0.0.0 lets your phone on the same Wi-Fi connect")
    ap.add_argument("--port", type=int, default=int(os.environ.get("DRUMSCRIBE_PORT", 8765)))
    a = ap.parse_args(argv)
    threading.Thread(target=_worker, daemon=True).start()
    srv = ThreadingHTTPServer((a.host, a.port), Handler)
    print(f"Drum Scribe engine {__version__}")
    print(f"  this computer:      http://127.0.0.1:{a.port}")
    print(f"  phone on your Wi-Fi: http://{_lan_ip()}:{a.port}")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
