// drumscribe-cli: link or audio file in, MIDI files + the groove report out, no server.
//   drumscribe-cli <link or file> [-o folder] [--fast] [--tempo 120] [--stems]
// Also: drumscribe-cli --from-stems <folder with kick/snare/toms/hh/ride/crash/bass.wav> (skips separation).
#include <iostream>
#include <thread>
#include "pipeline.h"

using namespace ds;

int main(int argc, char** argv)
{
    std::string src, out = ".", from_stems;
    bool fast = false, stems = false;
    double tempo = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-o" || a == "--out") && i + 1 < argc) out = argv[++i];
        else if (a == "--fast") fast = true;
        else if (a == "--stems") stems = true;
        else if (a == "--tempo" && i + 1 < argc) tempo = std::atof(argv[++i]);
        else if (a == "--from-stems" && i + 1 < argc) from_stems = argv[++i];
        else if (!a.empty() && a[0] != '-') src = a;
        else { src.clear(); from_stems.clear(); break; }
    }
    if (src.empty() && from_stems.empty()) {
        std::cout << "drumscribe-cli <YouTube/Spotify/SoundCloud link or audio file> [-o folder] [--fast] [--tempo BPM] [--stems]\n"
                     "drumscribe-cli --from-stems <folder> [-o folder] [--tempo BPM]\n";
        return 1;
    }
    try {
        Job job;
        job.quality = fast ? "fast" : "best";
        job.tempo = tempo;
        job.source = src;
        int last = -1;
        auto show = [&] {
            const int p = (int) (job.progress * 1000);
            if (p == last) return;
            last = p;
            std::lock_guard<std::mutex> l(job.m);
            std::cout << "\r[" << p / 10 << "." << p % 10 << "%] " << job.message.substr(0, 70) << std::string(10, ' ') << std::flush;
        };
        if (!from_stems.empty()) {
            job.work = fs::path(out) / ".drumscribe";
            fs::create_directories(job.work);
            auto kit = load_kit(from_stems);
            auto bass = to_mono(load_audio((fs::path(from_stems) / "bass.wav").string()));
            job.result = analyze(job, kit, bass, (double) bass.size() / kSR, fs::path(from_stems).filename().string());
        } else {
            job.work = default_dir("DRUMSCRIBE_WORK", "jobs") / ("cli-" + std::to_string(std::hash<std::string>()(src + job.quality)));
            std::atomic<bool> finished{false};
            std::string failure;
            std::thread t([&] {
                try { run_job(job, default_dir("DRUMSCRIBE_MODELS", "models").string()); }
                catch (const std::exception& e) { failure = e.what(); }
                finished = true;
            });
            while (!finished) {
                show();
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            t.join();
            if (!failure.empty()) throw std::runtime_error(failure);
        }
        std::cout << "\n";
        fs::create_directories(out);
        const json& r = job.result;
        for (auto [split, map] : {std::pair{false, true}, std::pair{true, true}, std::pair{false, false}}) {
            auto p = make_midi(r, job.work, split, "all", map);
            fs::copy_file(p, fs::path(out) / p.filename(), fs::copy_options::overwrite_existing);
            std::cout << "wrote " << (fs::path(out) / p.filename()).string() << "\n";
        }
        const fs::path g = fs::path(out) / (safe_name(r["title"]) + " - groove.txt");
        std::ofstream(g) << r["groove_text"].get<std::string>();
        if (stems && fs::exists(job.work / "stems"))
            for (auto& e : fs::directory_iterator(job.work / "stems"))
                fs::copy_file(e.path(), fs::path(out) / (safe_name(r["title"]) + " - " + e.path().filename().string()), fs::copy_options::overwrite_existing);
        std::cout << "\n" << r["groove_text"].get<std::string>() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "\n" << e.what() << "\n";
        return 1;
    }
    return 0;
}
