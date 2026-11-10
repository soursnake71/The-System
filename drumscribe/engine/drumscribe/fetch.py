"""Getting the song's audio from a link.

- YouTube / YouTube Music (and SoundCloud, Bandcamp, most other sites): downloaded with yt-dlp.
- Spotify: Spotify's audio is DRM-protected and can't be downloaded. The track's title, artist and length are read
  from its public page, and the same recording is found on YouTube: of the top search results, the one whose length
  matches Spotify's (within 3 s; official "Topic" / "Official Audio" uploads preferred) is used.
- A local file path also works.

Everything is converted to 44.1 kHz stereo 32-bit float WAV with ffmpeg.
"""
from __future__ import annotations

import html
import json
import os
import re
import shutil
import subprocess
import urllib.request
from dataclasses import dataclass

UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36"


@dataclass
class Source:
    path: str
    title: str
    duration: float = 0.0
    origin: str = ""


class FetchError(RuntimeError):
    pass


def _ydl_opts(extra=None):
    opts = {"quiet": True, "no_warnings": True, "noplaylist": True}
    browser = os.environ.get("DRUMSCRIBE_COOKIES_BROWSER")  # e.g. chrome, firefox: helps when YouTube asks to sign in
    if browser:
        opts["cookiesfrombrowser"] = (browser,)
    cookies = os.environ.get("DRUMSCRIBE_COOKIES_FILE")
    if cookies:
        opts["cookiefile"] = cookies
    opts.update(extra or {})
    return opts


def spotify_meta(url: str) -> dict:
    m = re.search(r"(?:open\.spotify\.com/(?:intl-[a-z]+/)?track/|spotify:track:)([A-Za-z0-9]{22})", url)
    if not m:
        raise FetchError("Only Spotify track links work (not albums or playlists).")
    tid = m.group(1)
    req = urllib.request.Request(f"https://open.spotify.com/track/{tid}", headers={"User-Agent": UA})
    page = urllib.request.urlopen(req, timeout=20).read().decode("utf-8", "replace")

    def meta(key):
        mm = re.search(r'<meta (?:property|name)="%s" content="([^"]*)"' % re.escape(key), page)
        return html.unescape(mm.group(1)) if mm else ""

    title = meta("og:title")
    desc = meta("og:description")  # "Artist · Album · Song · 1987"
    artist = meta("music:musician_description") or (desc.split(" · ")[0] if desc else "")
    dur = float(meta("music:duration") or 0)
    if not title:
        raise FetchError("Couldn't read the Spotify track's title.")
    return {"title": title, "artist": artist, "duration": dur}


def _pick_youtube(meta: dict) -> tuple[str, str]:
    import yt_dlp

    q = f"{meta['artist']} - {meta['title']}"
    with yt_dlp.YoutubeDL(_ydl_opts({"extract_flat": True})) as ydl:
        res = ydl.extract_info(f"ytsearch8:{q}", download=False)
    best, best_score = None, -1e9
    want = meta.get("duration") or 0
    words = re.findall(r"\w+", meta["title"].lower())
    for e in res.get("entries") or []:
        title = (e.get("title") or "").lower()
        chan = (e.get("channel") or e.get("uploader") or "").lower()
        d = float(e.get("duration") or 0)
        s = 0.0
        if want and d:
            diff = abs(d - want)
            s -= diff if diff <= 3 else 20 + diff
        s += 3 * sum(w in title for w in words) / max(1, len(words))
        if chan.endswith("- topic") or "official audio" in title:
            s += 4
        if any(w in title for w in ("live", "cover", "remix", "karaoke", "instrumental", "8d", "slowed", "sped up", "nightcore")) \
                and not any(w in meta["title"].lower() for w in ("live", "remix")):
            s -= 15
        if s > best_score:
            best, best_score = e, s
    if not best:
        raise FetchError(f"Couldn't find '{q}' on YouTube.")
    url = best.get("url") or f"https://www.youtube.com/watch?v={best['id']}"
    return url, best.get("title") or q


def to_wav(src: str, dst: str):
    if not shutil.which("ffmpeg"):
        raise FetchError("ffmpeg is not installed.")
    r = subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", src, "-vn", "-ac", "2", "-ar", "44100",
                        "-c:a", "pcm_f32le", dst], capture_output=True, text=True)
    if r.returncode != 0:
        raise FetchError("Couldn't decode the audio: " + r.stderr.strip()[-300:])
    return dst


def _download(url: str, workdir: str) -> tuple[str, str, float]:
    import yt_dlp

    os.makedirs(workdir, exist_ok=True)
    tmpl = os.path.join(workdir, "download.%(ext)s")
    opts = _ydl_opts({"format": "bestaudio/best", "outtmpl": tmpl})
    try:
        with yt_dlp.YoutubeDL(opts) as ydl:
            info = ydl.extract_info(url, download=True)
            path = ydl.prepare_filename(info)
    except Exception as e:  # yt-dlp raises its own DownloadError
        msg = str(e)
        if "Sign in to confirm" in msg or "bot" in msg:
            msg += ("\nYouTube wants a signed-in browser: set DRUMSCRIBE_COOKIES_BROWSER=chrome (or firefox, edge...) "
                    "before starting the engine, or upload the audio file instead.")
        raise FetchError(msg) from e
    return path, info.get("title") or "song", float(info.get("duration") or 0)


def fetch(link: str, workdir: str, log=print) -> Source:
    os.makedirs(workdir, exist_ok=True)
    wav = os.path.join(workdir, "song.wav")
    link = link.strip()
    if os.path.exists(link):
        to_wav(link, wav)
        return Source(wav, os.path.splitext(os.path.basename(link))[0], origin=link)
    if not re.match(r"^(https?://|spotify:)", link):
        raise FetchError("That isn't a link or a file.")
    if "spotify" in link:
        meta = spotify_meta(link)
        log(f"Spotify: {meta['artist']} - {meta['title']} ({meta['duration']:.0f} s); finding it on YouTube")
        url, ytitle = _pick_youtube(meta)
        log(f"Using: {ytitle} ({url})")
        path, _, dur = _download(url, workdir)
        title = f"{meta['artist']} - {meta['title']}"
    else:
        path, title, dur = _download(link, workdir)
    to_wav(path, wav)
    try:
        os.remove(path)
    except OSError:
        pass
    return Source(wav, title, dur, origin=link)


def safe_name(s: str) -> str:
    s = re.sub(r"[^\w\- ]+", "", s).strip()
    return (s or "drums")[:80]


if __name__ == "__main__":
    import sys
    print(json.dumps(spotify_meta(sys.argv[1])))
