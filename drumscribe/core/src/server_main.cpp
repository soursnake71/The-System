// drumscribe-server: the engine as a small HTTP server for the Android app, the VST3 plugin and any browser (page at /).
//
//   POST /api/jobs            {"link": "...", "quality": "best"|"fast", "tempo": null|120.0}  -> {"id": ...}
//   POST /api/upload?name=song.mp3&quality=best&tempo=   (body: the audio file)              -> {"id": ...}
//   GET  /api/jobs/<id>       state (queued/running/done/error), stage, progress 0..1, message, result (?hits=1: all
//                             drum hits, bass notes, beat grid; else just the summary + the groove analysis)
//   GET  /api/jobs/<id>/midi  ?part=all|drums|bass  &split=1 (a track per kit piece)  &tempo=map|constant
//   GET  /api/jobs/<id>/groove.txt   the groove analysis as text
//   GET  /api/jobs/<id>/drums | /bass   the separated stems (WAV)
//   POST /api/jobs/<id>/tempo {"tempo": 97.5}  re-grids at a fixed tempo (no re-analysis of the audio)
//   GET  /api/health
// Jobs run one at a time (separation uses every core). Set DRUMSCRIBE_TOKEN to require ?token= or an X-Token header.
#include <httplib.h>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory>
#include <thread>
#include "pipeline.h"
#include "web_page.inc"

using namespace ds;

namespace {
std::mutex jobs_m;
std::map<std::string, std::shared_ptr<Job>> jobs;
std::deque<std::shared_ptr<Job>> queue_;
std::condition_variable queue_cv;
fs::path work_root, model_dir;

std::string sha1ish(const std::string& s)  // short stable id (FNV-1a 64, hex) - not for security
{
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char b[17];
    snprintf(b, sizeof b, "%016llx", (unsigned long long) h);
    return b;
}

json public_json(Job& j, bool hits)
{
    std::lock_guard<std::mutex> l(j.m);
    json o = {{"id", j.id}, {"source", j.source}, {"state", j.state}, {"stage", j.stage}, {"progress", j.progress.load()},
              {"message", j.message}, {"error", j.error.empty() ? json() : json(j.error)}};
    if (j.result.is_object()) {
        json r = j.result;
        if (!hits) { r.erase("hits"); r.erase("bass"); r.erase("grid"); }
        o["result"] = r;
    } else o["result"] = nullptr;
    return o;
}

std::shared_ptr<Job> submit(const std::string& source, const std::string& quality, double tempo, const std::string& label)
{
    const std::string id = sha1ish(source + (quality == "fast" ? "|fast" : ""));
    std::lock_guard<std::mutex> l(jobs_m);
    auto it = jobs.find(id);
    if (it != jobs.end() && (it->second->state == "queued" || it->second->state == "running")) return it->second;
    auto j = std::make_shared<Job>();
    j->id = id;
    j->source = source;
    j->quality = quality;
    j->tempo = tempo;
    j->work = work_root / id;
    if (!label.empty()) j->message = "Waiting: " + label;
    jobs[id] = j;
    queue_.push_back(j);
    queue_cv.notify_one();
    return j;
}

void worker()
{
    for (;;) {
        std::shared_ptr<Job> j;
        {
            std::unique_lock<std::mutex> l(jobs_m);
            queue_cv.wait(l, [] { return !queue_.empty(); });
            j = queue_.front();
            queue_.pop_front();
        }
        { std::lock_guard<std::mutex> l(j->m); j->state = "running"; j->message = "Starting"; }
        try {
            run_job(*j, model_dir.string());
        } catch (const std::exception& e) {
            std::cerr << "job " << j->id << ": " << e.what() << "\n";
            std::lock_guard<std::mutex> l(j->m);
            j->state = "error";
            j->error = e.what();
            j->message = std::string(e.what()).substr(0, 300);
        }
    }
}

// a finished job from an earlier run of the server
std::shared_ptr<Job> find_job(const std::string& id)
{
    std::lock_guard<std::mutex> l(jobs_m);
    auto it = jobs.find(id);
    if (it != jobs.end()) return it->second;
    const fs::path rp = work_root / id / "result.json";
    if (!fs::exists(rp)) return nullptr;
    auto j = std::make_shared<Job>();
    j->id = id;
    j->work = work_root / id;
    j->result = json::parse(std::ifstream(rp));
    j->state = j->stage = "done";
    j->progress = 1;
    j->source = j->result.value("title", "");
    jobs[id] = j;
    return j;
}

std::string lan_ip()
{
    std::string out = run_cmd(
#ifdef _WIN32
        "powershell -NoProfile -Command \"(Get-NetIPAddress -AddressFamily IPv4 | ? {$_.InterfaceAlias -notlike '*Loopback*'} | select -First 1).IPAddress\""
#elif __APPLE__
        "ipconfig getifaddr en0"
#else
        "hostname -I"
#endif
    );
    std::istringstream is(out);
    std::string ip;
    is >> ip;
    return ip.empty() ? "<this computer's IP>" : ip;
}
}  // namespace

int main(int argc, char** argv)
{
    std::string host = "0.0.0.0";
    int port = std::getenv("DRUMSCRIBE_PORT") ? std::atoi(std::getenv("DRUMSCRIBE_PORT")) : 8765;
    work_root = default_dir("DRUMSCRIBE_WORK", "jobs");
    model_dir = default_dir("DRUMSCRIBE_MODELS", "models");
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--host" && i + 1 < argc) host = argv[++i];
        else if (a == "--models" && i + 1 < argc) model_dir = argv[++i];
        else if (a == "--work" && i + 1 < argc) work_root = argv[++i];
        else { std::cout << "drumscribe-server [--port 8765] [--host 0.0.0.0] [--models dir] [--work dir]\n"; return a == "--help" ? 0 : 1; }
    }
    fs::create_directories(work_root);
    std::thread(worker).detach();

    httplib::Server srv;
    const std::string token = std::getenv("DRUMSCRIBE_TOKEN") ? std::getenv("DRUMSCRIBE_TOKEN") : "";
    auto authed = [&](const httplib::Request& q) {
        return token.empty() || q.get_header_value("X-Token") == token || q.get_param_value("token") == token;
    };
    auto send_json = [](httplib::Response& r, const json& j, int code = 200) { r.status = code; r.set_content(j.dump(), "application/json"); };
    auto err = [&](httplib::Response& r, int code, const std::string& m) { send_json(r, {{"error", m}}, code); };
    auto send_file = [&](httplib::Response& r, const fs::path& p, const std::string& type, const std::string& name) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return err(r, 404, "not there");
        std::string data((std::istreambuf_iterator<char>(f)), {});
        r.set_header("Content-Disposition", "attachment; filename*=UTF-8''" + httplib::detail::encode_url(name));
        r.set_content(std::move(data), type);
    };
    srv.set_default_headers({{"Access-Control-Allow-Origin", "*"}, {"Access-Control-Allow-Headers", "Content-Type, X-Token"}});
    srv.Options(".*", [](const httplib::Request&, httplib::Response& r) { r.status = 204; });
    srv.set_payload_max_length(1024ull * 1024 * 1024);

    srv.Get("/", [](const httplib::Request&, httplib::Response& r) { r.set_content(WEB_PAGE, "text/html; charset=utf-8"); });
    srv.Get("/api/health", [&](const httplib::Request&, httplib::Response& r) {
        size_t q;
        { std::lock_guard<std::mutex> l(jobs_m); q = queue_.size(); }
#ifdef DS_HAVE_SEPARATION
        const bool sep = true;
#else
        const bool sep = false;
#endif
        send_json(r, {{"ok", true}, {"version", "2.0.0"}, {"engine", "c++"}, {"separation", sep}, {"queue", q}, {"auth", !token.empty()}});
    });
    srv.Post("/api/jobs", [&](const httplib::Request& q, httplib::Response& r) {
        if (!authed(q)) return err(r, 401, "wrong or missing token");
        json d = json::parse(q.body, nullptr, false);
        if (d.is_discarded()) return err(r, 400, "bad JSON");
        const std::string link = d.value("link", "");
        if (!std::regex_search(link, std::regex(R"(^(https?://|spotify:))"))) return err(r, 400, "Paste a YouTube, Spotify or SoundCloud link.");
        const double tempo = d.contains("tempo") && d["tempo"].is_number() ? d["tempo"].get<double>() : 0;
        send_json(r, {{"id", submit(link, d.value("quality", "best"), tempo, "")->id}});
    });
    srv.Post("/api/upload", [&](const httplib::Request& q, httplib::Response& r) {
        if (!authed(q)) return err(r, 401, "wrong or missing token");
        if (q.body.empty()) return err(r, 400, "empty file");
        std::string name = fs::path(q.has_param("name") ? q.get_param_value("name") : "song").filename().string();
        const fs::path up = work_root / "uploads";
        fs::create_directories(up);
        std::string ext = fs::path(name).extension().string().substr(0, 8);
        const fs::path path = up / (sha1ish(q.body) + (ext.empty() ? ".audio" : ext));
        std::ofstream(path, std::ios::binary).write(q.body.data(), (std::streamsize) q.body.size());
        const std::string t = q.get_param_value("tempo");
        auto j = submit(path.string(), q.has_param("quality") ? q.get_param_value("quality") : "best", t.empty() ? 0 : std::atof(t.c_str()),
                        fs::path(name).stem().string());
        send_json(r, {{"id", j->id}});
    });
    srv.Get("/api/jobs", [&](const httplib::Request& q, httplib::Response& r) {
        if (!authed(q)) return err(r, 401, "wrong or missing token");
        json a = json::array();
        std::vector<std::shared_ptr<Job>> v;
        { std::lock_guard<std::mutex> l(jobs_m); for (auto& [k, j] : jobs) v.push_back(j); }
        for (auto& j : v) a.push_back(public_json(*j, false));
        send_json(r, a);
    });
    srv.Get(R"(/api/jobs/([0-9a-f]{16})(/midi|/drums|/bass|/groove\.txt)?)", [&](const httplib::Request& q, httplib::Response& r) {
        if (!authed(q)) return err(r, 401, "wrong or missing token");
        auto j = find_job(q.matches[1]);
        if (!j) return err(r, 404, "no such job");
        const std::string what = q.matches[2];
        if (what.empty()) return send_json(r, public_json(*j, q.get_param_value("hits") == "1"));
        json res;
        { std::lock_guard<std::mutex> l(j->m); if (j->state != "done") return err(r, 409, "not finished yet"); res = j->result; }
        const std::string title = res.value("title", "song");
        if (what == "/groove.txt") return r.set_content(res.value("groove_text", ""), "text/plain; charset=utf-8");
        if (what == "/drums" || what == "/bass") {
            const fs::path p = j->work / "stems" / (what.substr(1) + ".wav");
            return send_file(r, p, "audio/wav", title + " - " + what.substr(1) + ".wav");
        }
        try {
            const std::string part = q.has_param("part") ? q.get_param_value("part") : "all";
            auto p = make_midi(res, j->work, q.get_param_value("split") == "1", part, q.get_param_value("tempo") != "constant");
            send_file(r, p, "audio/midi", p.filename().string());
        } catch (const std::exception& e) { err(r, 500, e.what()); }
    });
    srv.Post(R"(/api/jobs/([0-9a-f]{16})/tempo)", [&](const httplib::Request& q, httplib::Response& r) {
        if (!authed(q)) return err(r, 401, "wrong or missing token");
        auto j = find_job(q.matches[1]);
        if (!j || j->state != "done") return err(r, 409, "not finished yet");
        json d = json::parse(q.body, nullptr, false);
        const double bpm = !d.is_discarded() && d.contains("tempo") && d["tempo"].is_number() ? d["tempo"].get<double>() : 0;
        json res;
        { std::lock_guard<std::mutex> l(j->m); res = j->result; }
        res = retempo(res, j->work, bpm);
        { std::lock_guard<std::mutex> l(j->m); j->result = res; j->message = "Re-gridded at " + std::to_string(res["tempo"].get<double>()).substr(0, 6) + " BPM"; }
        send_json(r, public_json(*j, false));
    });

    std::cout << "Drum Scribe engine 2.0 (C++)\n  this computer:       http://127.0.0.1:" << port
              << "\n  phone on your Wi-Fi: http://" << lan_ip() << ":" << port << "\n  models: " << model_dir.string()
              << "\n  jobs:   " << work_root.string() << std::endl;
    if (!srv.listen(host, port)) { std::cerr << "can't listen on " << host << ":" << port << "\n"; return 1; }
}
