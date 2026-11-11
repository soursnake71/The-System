// The whole job: link or file -> audio -> separation -> drums + bass -> beats -> groove -> result (+ MIDI on request).
// Header-only so the server and the command line share it. Downloads use yt-dlp and curl (external programs).
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include "ds/kit.h"
#include "ds/modules.h"

#ifdef _WIN32
#define DS_POPEN _popen
#define DS_PCLOSE _pclose
#else
#define DS_POPEN popen
#define DS_PCLOSE pclose
#endif

namespace ds {
namespace fs = std::filesystem;
using json = nlohmann::json;

inline std::string shell_quote(const std::string& s)
{
#ifdef _WIN32
    std::string q = "\"";
    for (char c : s) q += c == '"' ? std::string("\\\"") : std::string(1, c);
    return q + "\"";
#else
    std::string q = "'";
    for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return q + "'";
#endif
}

// run a command, return stdout and the exit code
inline std::string run_cmd(const std::string& cmd, int* rc = nullptr)
{
    std::string out;
    FILE* f = DS_POPEN((cmd + " 2>&1").c_str(), "r");
    if (!f) throw std::runtime_error("can't run: " + cmd);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    const int r = DS_PCLOSE(f);
    if (rc) *rc = r;
    return out;
}

inline std::string safe_name(std::string s)
{
    s = std::regex_replace(s, std::regex(R"([\\/:*?"<>|\x00-\x1f]+)"), " ");
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.size() > 80) s.resize(80);
    return s.empty() ? "drums" : s;
}

inline std::string html_unescape(std::string s)
{
    const std::pair<const char*, const char*> e[] = {{"&amp;", "&"}, {"&quot;", "\""}, {"&#x27;", "'"}, {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}};
    for (auto& [a, b] : e) for (size_t p; (p = s.find(a)) != std::string::npos;) s.replace(p, strlen(a), b);
    return s;
}

inline std::string ytdlp_extra()
{
    std::string x;
    if (const char* b = std::getenv("DRUMSCRIBE_COOKIES_BROWSER")) x += " --cookies-from-browser " + shell_quote(b);
    if (const char* f = std::getenv("DRUMSCRIBE_COOKIES_FILE")) x += " --cookies " + shell_quote(f);
    return x;
}

struct Source { std::string path, title; };

// Spotify's audio is DRM-protected: read the title, artist and length from the track page, find the same recording on
// YouTube (the search result whose length matches, official uploads preferred, no live/cover/remix).
inline std::string spotify_to_youtube(const std::string& link, std::string& title, const std::function<void(std::string)>& log)
{
    std::smatch m;
    if (!std::regex_search(link, m, std::regex(R"((?:track/|spotify:track:)([A-Za-z0-9]{22}))")))
        throw std::runtime_error("Only Spotify track links work (not albums or playlists).");
    const std::string page = run_cmd("curl -sL -A " + shell_quote("Mozilla/5.0 (X11; Linux x86_64) Chrome/130.0") + " " +
                                     shell_quote("https://open.spotify.com/track/" + m[1].str()));
    auto meta = [&](const std::string& key) {
        std::smatch mm;
        std::regex re("<meta (?:property|name)=\"" + key + "\" content=\"([^\"]*)\"");
        return std::regex_search(page, mm, re) ? html_unescape(mm[1].str()) : std::string();
    };
    const std::string name = meta("og:title"), artist = meta("music:musician_description");
    const double dur = std::atof(meta("music:duration").c_str());
    if (name.empty()) throw std::runtime_error("Couldn't read the Spotify track's title.");
    title = artist.empty() ? name : artist + " - " + name;
    log("Spotify: " + title + "; finding it on YouTube");
    std::istringstream lines(run_cmd("yt-dlp -j --flat-playlist" + ytdlp_extra() + " " + shell_quote("ytsearch8:" + title)));
    std::string line, best;
    double best_score = -1e9;
    std::string lname = name;
    std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
    while (std::getline(lines, line)) {
        json e = json::parse(line, nullptr, false);
        if (e.is_discarded() || !e.is_object()) continue;
        std::string t = e.value("title", ""), ch = e.value("channel", e.value("uploader", ""));
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        std::transform(ch.begin(), ch.end(), ch.begin(), ::tolower);
        const double d = e.contains("duration") && e["duration"].is_number() ? e["duration"].get<double>() : 0;
        double s = 0;
        if (dur > 0 && d > 0) s -= std::fabs(d - dur) <= 3 ? std::fabs(d - dur) : 20 + std::fabs(d - dur);
        if (t.find(lname) != std::string::npos) s += 3;
        if ((ch.size() > 8 && ch.substr(ch.size() - 7) == "- topic") || t.find("official audio") != std::string::npos) s += 4;
        for (const char* bad : {"live", "cover", "remix", "karaoke", "instrumental", "8d", "slowed", "sped up", "nightcore"})
            if (t.find(bad) != std::string::npos && lname.find(bad) == std::string::npos) s -= 15;
        if (s > best_score) { best_score = s; best = e.value("url", e.contains("id") ? "https://www.youtube.com/watch?v=" + e.value("id", "") : ""); }
    }
    if (best.empty()) throw std::runtime_error("Couldn't find '" + title + "' on YouTube.");
    log("Using " + best);
    return best;
}

inline Source fetch(const std::string& link_in, const fs::path& work, const std::function<void(std::string)>& log)
{
    std::string link = link_in;
    while (!link.empty() && isspace((unsigned char) link.back())) link.pop_back();
    if (fs::exists(link)) return {link, fs::path(link).stem().string()};
    if (!std::regex_search(link, std::regex(R"(^(https?://|spotify:))"))) throw std::runtime_error("That isn't a link or a file.");
    std::string title;
    if (link.find("spotify") != std::string::npos) link = spotify_to_youtube(link, title, log);
    fs::create_directories(work);
    const std::string tmpl = (work / "download.%(ext)s").string();
    int rc = 0;
    const std::string out = run_cmd("yt-dlp --no-playlist -f bestaudio/best --print after_move:filepath --print title -o " +
                                    shell_quote(tmpl) + ytdlp_extra() + " " + shell_quote(link), &rc);
    std::vector<std::string> ls;
    std::istringstream is(out);
    for (std::string l; std::getline(is, l);) if (!l.empty()) ls.push_back(l);
    std::string path;
    for (auto& l : ls) if (l.rfind(work.string(), 0) == 0 || fs::exists(l)) path = l;
    if (rc != 0 || path.empty() || !fs::exists(path)) {
        std::string msg = out.size() > 600 ? out.substr(out.size() - 600) : out;
        if (msg.find("Sign in to confirm") != std::string::npos)
            msg += "\nYouTube wants a signed-in browser: set DRUMSCRIBE_COOKIES_BROWSER=chrome (or firefox, edge...) before "
                   "starting the engine, or upload the audio file instead.";
        if (msg.find("not found") != std::string::npos || msg.find("not recognized") != std::string::npos)
            msg = "yt-dlp is not installed (https://github.com/yt-dlp/yt-dlp): install it, or upload the audio file instead.";
        throw std::runtime_error("Download failed: " + msg);
    }
    if (title.empty()) for (auto& l : ls) if (l != path) title = l;
    return {path, title.empty() ? "song" : title};
}

// ------------------------------------------------------------------ result <-> json
inline json hits_json(const std::vector<DrumHit>& d)
{
    json a = json::array();
    for (auto& h : d) a.push_back({std::round(h.time * 1e5) / 1e5, h.note, h.velocity, std::round((h.end - h.time) * 1e4) / 1e4, h.piece});
    return a;
}
inline std::vector<DrumHit> hits_from(const json& a)
{
    std::vector<DrumHit> d;
    for (auto& e : a) {
        DrumHit h;
        h.time = e[0]; h.note = e[1]; h.velocity = e[2]; h.end = h.time + e[3].get<double>(); h.piece = e[4];
        d.push_back(h);
    }
    return d;
}
inline json bass_json(const std::vector<BassNote>& b)
{
    json a = json::array();
    for (auto& n : b) {
        json bends = json::array();
        for (auto& p : n.bends) bends.push_back({std::round(p.time * 1e4) / 1e4, std::round(p.semitones * 1e3) / 1e3});
        a.push_back({{"t", std::round(n.time * 1e5) / 1e5}, {"end", std::round(n.end * 1e5) / 1e5}, {"note", n.note},
                     {"cents", std::round(n.cents * 10) / 10}, {"vel", n.velocity}, {"accent", n.accent},
                     {"slide", n.slide_to_next}, {"bends", bends}});
    }
    return a;
}
inline std::vector<BassNote> bass_from(const json& a)
{
    std::vector<BassNote> b;
    for (auto& e : a) {
        BassNote n;
        n.time = e["t"]; n.end = e["end"]; n.note = e["note"]; n.cents = e["cents"]; n.velocity = e["vel"];
        n.accent = e["accent"]; n.slide_to_next = e["slide"];
        for (auto& p : e["bends"]) n.bends.push_back({p[0].get<double>(), p[1].get<double>()});
        b.push_back(n);
    }
    return b;
}
inline json grid_json(const BeatGrid& g)
{
    return {{"beats", g.beats}, {"downbeats", g.downbeats}, {"beats_per_bar", g.beats_per_bar}, {"mean_bpm", g.mean_bpm}, {"bpm_at_beat", g.bpm_at_beat}};
}
inline BeatGrid grid_from(const json& j)
{
    BeatGrid g;
    g.beats = j["beats"].get<std::vector<double>>();
    g.downbeats = j["downbeats"].get<std::vector<int>>();
    g.beats_per_bar = j["beats_per_bar"];
    g.mean_bpm = j["mean_bpm"];
    g.bpm_at_beat = j["bpm_at_beat"].get<std::vector<double>>();
    return g;
}

// ------------------------------------------------------------------ the job
struct Job {
    std::string id, source, quality = "best";
    double tempo = 0;  // > 0: constant grid at this tempo
    fs::path work;
    std::atomic<double> progress{0};
    std::atomic<bool> cancel{false};  // set from another thread: the job stops at its next progress report
    std::mutex m;
    std::string state = "queued", stage = "queued", message = "Waiting", error;
    json result;
    void set(const std::string& st, double p, const std::string& msg)
    {
        if (cancel) throw std::runtime_error("Cancelled");
        std::lock_guard<std::mutex> l(m);
        stage = st;
        progress = p;
        if (!msg.empty()) message = msg;
    }
};

// steps and their share of the bar
struct Span { double a, b; };
constexpr Span P_FETCH{0, 0.06}, P_SEP{0.06, 0.88}, P_DRUMS{0.88, 0.95}, P_BASS{0.95, 0.98}, P_GROOVE{0.98, 1.0};

inline Stereo read_stereo(const fs::path& p) { return load_audio(p.string()); }

// analysis from the stems (cached in work/stems): drums, bass, beats, groove
inline json analyze(Job& job, const std::map<std::string, Signal>& kit, const Signal& bass_mono, double duration,
                    const std::string& title)
{
    auto stage = [&](const char* st, Span s, double f, const std::string& msg) { job.set(st, s.a + (s.b - s.a) * f, msg); };
    stage("drums", P_DRUMS, 0, "Finding every drum hit");
    auto drums = transcribe_drums(kit, kSR, [&](double f, const std::string& m) { stage("drums", P_DRUMS, f, m); });
    stage("bass", P_BASS, 0, "Transcribing the bass");
    auto bass = transcribe_bass(bass_mono, kSR, [&](double f, const std::string& m) { stage("bass", P_BASS, f, m); });
    stage("groove", P_GROOVE, 0, "Analysing the groove");
    auto grid = track_beats(drums, bass, duration, job.tempo);
    auto groove = analyze_groove(drums, bass, grid, duration);
    json pieces = json::array();
    for (auto& kp : kit_pieces()) {
        int c = 0;
        for (auto& h : drums) c += h.note == kp.note;
        if (c) pieces.push_back({{"piece", kp.id}, {"label", kp.label}, {"note", kp.note}, {"count", c}});
    }
    const std::string name = safe_name(title);
    json r = {{"title", title}, {"duration", std::round(duration * 1000) / 1000}, {"tempo", std::round(grid.mean_bpm * 100) / 100},
              {"midi", name + " - drums + bass.mid"}, {"midi_split", name + " - drums (track per piece) + bass.mid"},
              {"pieces", pieces}, {"hits", hits_json(drums)}, {"bass", bass_json(bass)}, {"grid", grid_json(grid)},
              {"groove", groove}, {"groove_text", groove_text(groove)}, {"fixed_tempo", job.tempo}};
    std::ofstream(job.work / "result.json") << r.dump();
    return r;
}

// write a MIDI file from a stored result. part: all | drums | bass; tempo_map: grid follows the band
inline fs::path make_midi(const json& r, const fs::path& work, bool split, const std::string& part, bool tempo_map)
{
    MidiOptions o;
    o.tempo_map = tempo_map;
    o.fixed_bpm = r.value("fixed_tempo", 0.0);
    o.split_drums = split;
    o.include_drums = part != "bass";
    o.include_bass = part != "drums";
    o.title = r.value("title", "Drum Scribe");
    std::string name = safe_name(o.title) + (part == "drums" ? " - drums" : part == "bass" ? " - bass" : " - drums + bass");
    if (split && part != "bass") name += " (track per piece)";
    if (!tempo_map) name += " (constant tempo)";
    const fs::path p = work / (name + ".mid");
    write_midi(p.string(), hits_from(r["hits"]), bass_from(r["bass"]), grid_from(r["grid"]), r["groove"], o);
    return p;
}

// a different tempo: only the grid and the groove maths change, the hits stay
inline json retempo(json r, const fs::path& work, double bpm)
{
    auto drums = hits_from(r["hits"]);
    auto bass = bass_from(r["bass"]);
    auto grid = track_beats(drums, bass, r["duration"].get<double>(), bpm);
    auto groove = analyze_groove(drums, bass, grid, r["duration"].get<double>());
    r["grid"] = grid_json(grid);
    r["tempo"] = std::round(grid.mean_bpm * 100) / 100;
    r["groove"] = groove;
    r["groove_text"] = groove_text(groove);
    r["fixed_tempo"] = bpm;
    std::ofstream(work / "result.json") << r.dump();
    return r;
}

inline std::map<std::string, Signal> load_kit(const fs::path& dir)
{
    std::map<std::string, Signal> kit;
    for (auto& s : stem_names()) kit[s] = to_mono(load_audio((dir / (s + ".wav")).string()));
    return kit;
}

inline void finish_job(Job& job, const json& r)
{
    std::lock_guard<std::mutex> l(job.m);
    job.result = r;
    job.state = "done";
    job.stage = "done";
    job.progress = 1;
    job.message = std::to_string(r["hits"].size()) + " drum hits, " + std::to_string(r["bass"].size()) + " bass notes, " +
                  std::to_string(r["tempo"].get<double>()).substr(0, 6) + " BPM";
}

inline void run_job(Job& job, const std::string& model_dir)
{
    auto stage = [&](const char* st, Span s, double f, const std::string& msg) { job.set(st, s.a + (s.b - s.a) * f, msg); };
    fs::create_directories(job.work);
    const fs::path stems = job.work / "stems", meta = job.work / "source.json";
    std::string title;
    double duration;
    if (fs::exists(stems / "bass.wav") && fs::exists(meta)) {
        json m = json::parse(std::ifstream(meta));
        title = m["title"];
        duration = m["duration"];
        stage("kit", P_SEP, 1, "Using the stems from last time");
    } else {
        stage("fetch", P_FETCH, 0, "Getting the audio");
        auto src = fetch(job.source, job.work, [&](const std::string& m) { stage("fetch", P_FETCH, 0.5, m); });
        title = src.title;
        auto song = load_audio(src.path);
        duration = (double) song.size() / kSR;
#ifdef DS_HAVE_SEPARATION
        auto st = separate(song, job.quality == "fast" ? Quality::fast : Quality::best, model_dir,
                           [&](double f, const std::string& m) { stage(f < 0.5 ? "drums" : "kit", P_SEP, f, m); });
        fs::create_directories(stems);
        save_wav((stems / "drums.wav").string(), st.drums);
        save_wav((stems / "bass.wav").string(), st.bass);
        for (auto& [k, x] : st.kit) save_wav((stems / (k + ".wav")).string(), x);
#else
        (void) model_dir;
        throw std::runtime_error("this engine was built without separation (DS_WITH_SEPARATION=OFF)");
#endif
        std::ofstream(meta) << json{{"title", title}, {"duration", duration}, {"origin", job.source}}.dump();
        if (src.path.rfind(job.work.string(), 0) == 0) fs::remove(src.path);  // the download isn't needed any more
    }
    auto r = analyze(job, load_kit(stems), to_mono(load_audio((stems / "bass.wav").string())), duration, title);
    finish_job(job, r);
}

// The same job from audio that is already decoded to 44.1 kHz stereo (no download, no ffmpeg): the Android app, which
// decodes with MediaCodec. An empty `song` is fine when job.work already has the stems from an earlier run. The stems
// are cached in job.work/stems as 32-bit float WAV and read back with load_wav.
inline void run_job_audio(Job& job, Stereo song, const std::string& title, const std::string& model_dir)
{
    auto stage = [&](const char* st, Span s, double f, const std::string& msg) { job.set(st, s.a + (s.b - s.a) * f, msg); };
    fs::create_directories(job.work);
    const fs::path stems = job.work / "stems", meta = job.work / "source.json";
    std::string name = title;
    double duration;
    std::map<std::string, Signal> kit;
    Signal bass;
    if (fs::exists(stems / "bass.wav") && fs::exists(meta)) {
        json m = json::parse(std::ifstream(meta));
        name = m["title"];
        duration = m["duration"];
        stage("kit", P_SEP, 1, "Using the stems from last time");
        for (auto& s : stem_names()) kit[s] = to_mono(load_wav((stems / (s + ".wav")).string()));
        bass = to_mono(load_wav((stems / "bass.wav").string()));
    } else {
        if (song.size() == 0) throw std::runtime_error("No audio.");
        duration = (double) song.size() / kSR;
#ifdef DS_HAVE_SEPARATION
        stage("drums", P_SEP, 0, "Separating the drums and the bass");
        auto st = separate(song, job.quality == "fast" ? Quality::fast : Quality::best, model_dir,
                           [&](double f, const std::string& m) { stage(f < 0.5 ? "drums" : "kit", P_SEP, f, m); });
        song = Stereo{};  // free the mix before the analysis
        fs::create_directories(stems);
        save_wav((stems / "drums.wav").string(), st.drums);
        save_wav((stems / "bass.wav").string(), st.bass);
        for (auto& [k, x] : st.kit) save_wav((stems / (k + ".wav")).string(), x);
        bass = to_mono(st.bass);
        kit = std::move(st.kit);
#else
        (void) model_dir;
        throw std::runtime_error("this engine was built without separation (DS_WITH_SEPARATION=OFF)");
#endif
        std::ofstream(meta) << json{{"title", name}, {"duration", duration}, {"origin", title}}.dump();
    }
    auto r = analyze(job, kit, bass, duration, name);
    finish_job(job, r);
}

inline fs::path default_dir(const char* env, const char* sub)
{
    if (const char* e = std::getenv(env)) return e;
#ifdef _WIN32
    const char* h = std::getenv("LOCALAPPDATA");
    return fs::path(h ? h : ".") / "DrumScribe" / sub;
#else
    const char* h = std::getenv("HOME");
    return fs::path(h ? h : ".") / ".cache" / "drumscribe" / sub;
#endif
}

}  // namespace ds
