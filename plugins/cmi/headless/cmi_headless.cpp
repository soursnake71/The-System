// Omega CMI, headless: the same channel card engine as the plugin, for any Linux box with ALSA (an MPC running its
// own OS, a Raspberry Pi, a server). No GUI, no framework: C++17 and libasound.
//
//   cmi-headless [options] [disk.imd | disk.img | voice.vc | sound.wav | folder] ...
//     -d <pcm>      ALSA output (default "default"; e.g. hw:0,0, plughw:1,0)
//     -r <rate>     sample rate (default 48000)
//     -p <frames>   period size (default 128), -n <periods> (default 3)
//     -m <client:port>  also connect this ALSA sequencer port (by default every hardware MIDI input is connected)
//     -c <ch>       MIDI channel for the keyboard voice, 1-16 or 0 = all (default 0)
//     -t            multitimbral: channels 2-9 play Page R tracks 1-8
//     -s <state>    load and save voices / Page R in this file (default ~/.omega-cmi.state)
//     -l            list the voices and quit
//     -w <out.wav> <seconds>  render to a WAV file instead of the sound card (testing)
//   MIDI: notes, velocity, pitch bend, mod wheel, sustain, program change (the voice list: factory voices first,
//   then everything you loaded), CC 7 level, CC 74 filter, CC 73 attack, CC 72 damping, all notes off;
//   start / stop / clock run Page R (or the selected MIDI song) in time with your MPC. Lines on stdin: LOAD <path>,
//   VOICE <n|name>, PLAY, STOP, SPEED <bpm>, PATTERN <n>, SONG ON|OFF, SONGS, SELECT <n>, LOOP ON|OFF,
//   EXPORT <file.wav | file.mid>, LIST, SAVE, QUIT.
#include "../core/Disk.h"
#include "../core/Song.h"
#include <alsa/asoundlib.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <pthread.h>
#include <sys/stat.h>
#include <thread>

using namespace cmi;

static std::atomic<bool> quit { false };
static void onSignal(int) { quit = true; }

struct Box {
    Kit kit;
    Engine eng;
    SongList songs;
    std::mutex lock;            // engine access: the audio thread, MIDI thread and stdin take turns
    int sound = 0; Controls ctl;
    int channel = 0;            // 0 = omni
    // MIDI clock: tempo from the 24 ppq pulses
    double lastClock = -1, clockBpm = 120; int clocks = 0;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double now() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
};

static std::vector<uint8_t> slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {}); }
static std::string lower(std::string s) { for (auto& c : s) c = (char) std::tolower((unsigned char) c); return s; }
static std::string ext(const std::string& p) { const auto d = p.rfind('.'); return d == std::string::npos ? "" : lower(p.substr(d + 1)); }
static std::string baseName(const std::string& p) { auto s = p.substr(p.rfind('/') == std::string::npos ? 0 : p.rfind('/') + 1); const auto d = s.rfind('.'); return d == std::string::npos ? s : s.substr(0, d); }

static void selectSound(Box& b, int i) {
    const Sound* s = b.kit.bank.at(i);
    if (!s) return;
    b.sound = i; b.ctl = s->ctl;
    b.eng.setKeyboard(i, b.ctl);
    std::printf("voice %d: %s  %s\n", i + 1, s->name.c_str(), s->title.c_str()); std::fflush(stdout);
}

// a disk, a voice file, an audio file or a folder of them; returns how many voices went in
static int load(Box& b, const std::string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) { std::fprintf(stderr, "can't open %s\n", path.c_str()); return 0; }
    if (S_ISDIR(st.st_mode)) {
        std::vector<std::string> names;
        if (DIR* d = opendir(path.c_str())) { while (auto* e = readdir(d)) if (e->d_name[0] != '.') names.push_back(path + "/" + e->d_name); closedir(d); }
        std::sort(names.begin(), names.end());
        int n = 0; for (auto& p : names) n += load(b, p); return n;
    }
    const std::string e = ext(path);
    std::vector<Sound> got;
    const auto bytes = slurp(path);
    if (e == "imd" || e == "img" || e == "bin" || e == "raw" || e == "dsk" || e == "hfe") {
        Disk dk = readDiskImage(bytes.data(), bytes.size());
        if (!dk.err.empty()) { std::fprintf(stderr, "%s: %s\n", path.c_str(), dk.err.c_str()); return 0; }
        for (auto& f : dk.files) if (f.type == "VC") { Sound s; std::string err; if (vcToSound(f.data, f.name, dk.label.empty() ? baseName(path) : dk.label, s, err)) got.push_back(std::move(s)); }
    } else if (e == "vc") {
        Sound s; std::string err;
        if (vcToSound(bytes, baseName(path), "", s, err)) got.push_back(std::move(s)); else std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
    } else if (e == "mid" || e == "midi") {
        MidiSong ms; std::string err;
        std::string nm = baseName(path); for (auto& c : nm) c = (char) (c == '_' || c == '-' ? ' ' : std::toupper((unsigned char) c));
        if (!parseSmf(bytes.data(), bytes.size(), nm, ms, err)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str()); return 0; }
        std::lock_guard<std::mutex> g(b.lock);
        const int i = b.songs.add(std::move(ms), b.kit.bank);
        selectSong(b.eng, b.songs.at(i));
        const MidiSong& s = *b.songs.at(i)->song;
        std::printf("song %d: %s, %.0f BPM %d/%d, %.0f s\n", b.songs.count(), s.name.c_str(), s.bpmAt(0), s.num, s.den, s.length * 60 / s.bpmAt(0));
        for (int t = 0; t < SONG_TRK; ++t) if (s.used[t]) { const int v = b.songs.at(i)->voice[t]; std::printf("  track %d %-20s %s\n", t + 1, s.trackName[t].c_str(), v == GM_KIT ? "DRUM KIT" : b.kit.bank.at(v)->name.c_str()); }
        std::fflush(stdout);
        return 0;
    } else if (e == "wav") {
        WavData w = parseWav(bytes.data(), bytes.size());
        if (!w.err.empty()) { std::fprintf(stderr, "%s: %s\n", path.c_str(), w.err.c_str()); return 0; }
        got.push_back(sampleToCmi(w.mono, w.rate, baseName(path), w.root >= 0 ? w.root : 60, false, w.loopStart, w.loopEnd));
    } else return 0;
    int n = 0;
    std::lock_guard<std::mutex> g(b.lock);
    for (auto& s : got) {
        const int k = b.kit.firstFree();
        if (k < 0) { std::fprintf(stderr, "user voices full\n"); break; }
        b.kit.setUser(k, std::move(s)); ++n;
    }
    std::printf("%s: %d voice%s\n", path.c_str(), n, n == 1 ? "" : "s"); std::fflush(stdout);
    return n;
}

// ---- state: the user voices and Page R ----
template <class T> static void put(std::ofstream& o, const T& x) { o.write((const char*) &x, sizeof(T)); }
template <class T> static bool get(std::ifstream& i, T& x) { return (bool) i.read((char*) &x, sizeof(T)); }
static void saveState(Box& b, const std::string& path) {
    if (path.empty()) return;
    std::ofstream o(path, std::ios::binary); if (!o) return;
    std::lock_guard<std::mutex> g(b.lock);
    put(o, (uint32_t) 0x434D4948); put(o, b.eng.seq); put(o, (int32_t) b.sound);
    for (int k = 0; k < Library::USER_SLOTS; ++k) {
        if (!b.kit.userLoaded(k)) continue;
        const Sound* s = b.kit.user(k);
        put(o, (int32_t) k); char nm[16] {}, cat[16] {}; std::strncpy(nm, s->name.c_str(), 15); std::strncpy(cat, s->category.c_str(), 15);
        put(o, nm); put(o, cat); put(o, (int32_t) s->mode); put(o, s->rate); put(o, (int32_t) s->root); put(o, (int32_t) s->loopStart); put(o, (int32_t) s->loopEnd);
        put(o, (int32_t) s->ram.size()); o.write((const char*) s->ram.data(), (std::streamsize) s->ram.size());
    }
}
static void loadState(Box& b, const std::string& path) {
    std::ifstream i(path, std::ios::binary); if (!i) return;
    uint32_t magic = 0; if (!get(i, magic) || magic != 0x434D4948) return;
    SeqData sd; int32_t snd = 0; if (!get(i, sd) || !get(i, snd)) return;
    b.eng.seq = sd;
    int32_t k;
    while (get(i, k)) {
        Sound s; char nm[16], cat[16]; int32_t mode, root, ls, le, sz;
        if (!get(i, nm) || !get(i, cat) || !get(i, mode) || !get(i, s.rate) || !get(i, root) || !get(i, ls) || !get(i, le) || !get(i, sz) || sz <= 0 || sz > RAM_SIZE) break;
        nm[15] = cat[15] = 0; s.name = nm; s.category = cat; s.title = s.name + " from " + s.category; s.mode = mode == 1 ? Mode::Waveform : Mode::Sample;
        s.root = root; s.loopStart = ls; s.loopEnd = le; s.ram.resize((size_t) sz);
        if (!i.read((char*) s.ram.data(), sz)) break;
        b.kit.setUser(k, std::move(s));
    }
    if (b.kit.bank.at(snd)) selectSound(b, snd);
}

static void list(Box& b) {
    for (int i = 0; i < b.kit.bank.count.load(); ++i) {
        const bool user = i >= Library::get().builtIn;
        if (user && !b.kit.userLoaded(i - Library::get().builtIn)) continue;
        const Sound* s = b.kit.bank.at(i);
        std::printf("%3d  %-8s  %-10s  %s\n", i + 1, s->name.c_str(), s->category.c_str(), s->title.c_str());
    }
    std::fflush(stdout);
}

// ---- MIDI ----
static void midiBytes(Box& b, const uint8_t* m, int len) {
    if (len < 1) return;
    const uint8_t st = m[0];
    std::lock_guard<std::mutex> g(b.lock);
    // real time: Page R follows the MPC's transport
    if (st == 0xFA || st == 0xFB) { b.eng.extSync = false; if (b.eng.song.load()) b.eng.songRun = true; else b.eng.seqRun = true; b.clocks = 0; return; }
    if (st == 0xFC) { b.eng.seqRun = false; b.eng.songRun = false; return; }
    if (st == 0xF8) {
        const double t = b.now();
        if (b.lastClock >= 0) { const double dt = t - b.lastClock; if (dt > 0.001 && dt < 0.2) { b.clockBpm += (60.0 / (dt * 24) - b.clockBpm) * 0.08; b.eng.tempo = b.clockBpm; } }
        b.lastClock = t; return;
    }
    if (st >= 0xF0) return;
    const int ch = (st & 0x0F) + 1, hi = st & 0xF0;
    if (b.channel && ch != b.channel && !(ch >= 2 && ch <= 9)) return;
    if (hi == 0xC0 && len >= 2) {
        // program change: the n-th voice in the list (factory, then yours)
        int n = 0;
        for (int i = 0; i < b.kit.bank.count.load(); ++i) {
            const bool user = i >= Library::get().builtIn;
            if (user && !b.kit.userLoaded(i - Library::get().builtIn)) continue;
            if (n++ == m[1]) { selectSound(b, i); break; }
        }
        return;
    }
    if (hi == 0xB0 && len >= 3) {
        const float v = m[2] / 127.f;
        if (m[1] == 7) { b.ctl.level = v; b.eng.setKeyboard(b.sound, b.ctl); return; }
        if (m[1] == 74) { b.ctl.filter = v * 255; b.eng.setKeyboard(b.sound, b.ctl); return; }
        if (m[1] == 73) { b.ctl.attack = v * 31; b.eng.setKeyboard(b.sound, b.ctl); return; }
        if (m[1] == 72) { b.ctl.damp = v * 31; b.eng.setKeyboard(b.sound, b.ctl); return; }
    }
    b.eng.midi(m, len);
}

static void midiThread(Box& b, const std::string& extra) {
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) { std::fprintf(stderr, "no ALSA sequencer: MIDI off\n"); return; }
    snd_seq_set_client_name(seq, "Omega CMI");
    const int port = snd_seq_create_simple_port(seq, "Omega CMI in", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    // connect every hardware MIDI source, plus the one asked for
    snd_seq_client_info_t* ci; snd_seq_port_info_t* pi;
    snd_seq_client_info_alloca(&ci); snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        const int c = snd_seq_client_info_get_client(ci);
        if (c == SND_SEQ_CLIENT_SYSTEM || c == snd_seq_client_id(seq)) continue;
        snd_seq_port_info_set_client(pi, c); snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            const unsigned cap = snd_seq_port_info_get_capability(pi), type = snd_seq_port_info_get_type(pi);
            if ((cap & SND_SEQ_PORT_CAP_READ) && (cap & SND_SEQ_PORT_CAP_SUBS_READ) && (type & SND_SEQ_PORT_TYPE_HARDWARE)) {
                snd_seq_connect_from(seq, port, c, snd_seq_port_info_get_port(pi));
                std::printf("MIDI in: %s\n", snd_seq_port_info_get_name(pi));
            }
        }
    }
    if (!extra.empty()) { snd_seq_addr_t a; if (snd_seq_parse_address(seq, &a, extra.c_str()) >= 0) snd_seq_connect_from(seq, port, a.client, a.port); }
    std::fflush(stdout);
    snd_midi_event_t* dec = nullptr; snd_midi_event_new(256, &dec); snd_midi_event_no_status(dec, 1);
    const int npfd = snd_seq_poll_descriptors_count(seq, POLLIN);
    std::vector<pollfd> pfd((size_t) npfd); snd_seq_poll_descriptors(seq, pfd.data(), (unsigned) npfd, POLLIN);
    while (!quit) {
        if (poll(pfd.data(), (nfds_t) npfd, 200) <= 0) continue;
        snd_seq_event_t* ev = nullptr;
        while (snd_seq_event_input(seq, &ev) >= 0 && ev) {
            uint8_t buf[64];
            const long n = snd_midi_event_decode(dec, buf, sizeof buf, ev);
            if (n > 0) midiBytes(b, buf, (int) n);
            if (snd_seq_event_input_pending(seq, 0) <= 0) break;
        }
    }
    snd_midi_event_free(dec); snd_seq_close(seq);
}

// ---- stdin commands ----
static void commandThread(Box& b, const std::string& statePath) {
    std::string line;
    while (!quit && std::getline(std::cin, line)) {
        std::string w = line.substr(0, line.find(' ')), arg = line.find(' ') == std::string::npos ? "" : line.substr(line.find(' ') + 1);
        for (auto& c : w) c = (char) std::toupper((unsigned char) c);
        if (w == "QUIT" || w == "EXIT") { quit = true; break; }
        if (w == "LOAD") { load(b, arg); continue; }
        if (w == "LIST") { list(b); continue; }
        if (w == "SAVE") { saveState(b, statePath); std::puts("saved"); continue; }
        if (w == "EXPORT") {   // EXPORT song.wav | song.mid: the selected song (or Page R) to a file
            const SongEntry* e = b.songs.at(b.songs.indexOf(b.eng.song.load()));
            if (ext(arg) == "mid") { if (!e) { std::puts("no song selected"); continue; } const auto bytes = writeSmf(*e->song); std::ofstream(arg, std::ios::binary).write((const char*) bytes.data(), (std::streamsize) bytes.size()); std::printf("exported %s\n", arg.c_str()); continue; }
            Rendered r = e ? renderSong(b.kit.bank, *e, 48000) : renderPageR(b.kit.bank, b.eng.seq, b.eng.tempo.load(), 48000);
            const auto bytes = wavBytes(r, 24); std::ofstream(arg, std::ios::binary).write((const char*) bytes.data(), (std::streamsize) bytes.size());
            std::printf("exported %s (%.1f s)\n", arg.c_str(), r.L.size() / 48000.0); std::fflush(stdout); continue;
        }
        std::lock_guard<std::mutex> g(b.lock);
        if (w == "VOICE") {
            int idx = -1;
            if (!arg.empty() && std::isdigit((unsigned char) arg[0])) idx = std::atoi(arg.c_str()) - 1;
            else { std::string up = arg; for (auto& c : up) c = (char) std::toupper((unsigned char) c); for (int i = b.kit.bank.count.load() - 1; i >= 0; --i) if (const Sound* s = b.kit.bank.at(i)) if (s->name == up) { idx = i; break; } }
            if (b.kit.bank.at(idx)) selectSound(b, idx); else std::puts("no such voice");
        } else if (w == "PLAY") { b.eng.extSync = false; if (b.eng.song.load()) b.eng.songRun = true; else b.eng.seqRun = true; }
        else if (w == "STOP") { b.eng.seqRun = false; b.eng.songRun = false; }
        else if (w == "SONGS") { std::puts("0  Page R patterns"); for (int k = 0; k < b.songs.count(); ++k) std::printf("%d  %s\n", k + 1, b.songs.at(b.songs.nth(k))->song->name.c_str()); }
        else if (w == "SELECT") { const int k = std::atoi(arg.c_str()); selectSong(b.eng, k <= 0 ? nullptr : b.songs.at(b.songs.nth(k - 1))); }
        else if (w == "LOOP") b.eng.songLoop = lower(arg) != "off";
        else if (w == "SPEED" || w == "TEMPO") b.eng.tempo = std::atof(arg.c_str());
        else if (w == "PATTERN") b.eng.curPattern = clampi(std::atoi(arg.c_str()) - 1, 0, NPAT - 1);
        else if (w == "SONG") b.eng.songMode = lower(arg) != "off";
        else if (!w.empty()) std::puts("?SYNTAX ERROR");
        std::fflush(stdout);
    }
}

// ---- audio ----
static int runAlsa(Box& b, const std::string& dev, unsigned rate, snd_pcm_uframes_t period, unsigned periods) {
    snd_pcm_t* pcm = nullptr;
    int err = snd_pcm_open(&pcm, dev.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) { std::fprintf(stderr, "can't open %s: %s\n", dev.c_str(), snd_strerror(err)); return 1; }
    snd_pcm_hw_params_t* hw; snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    bool f32 = snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_FLOAT_LE) == 0;
    if (!f32 && snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S32_LE) != 0 && snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE) != 0) { std::fprintf(stderr, "no usable sample format\n"); return 1; }
    snd_pcm_format_t fmt; snd_pcm_hw_params_get_format(hw, &fmt);
    unsigned ch = 2; snd_pcm_hw_params_set_channels_near(pcm, hw, &ch);
    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);
    snd_pcm_hw_params_set_periods_near(pcm, hw, &periods, nullptr);
    if ((err = snd_pcm_hw_params(pcm, hw)) < 0) { std::fprintf(stderr, "can't set up %s: %s\n", dev.c_str(), snd_strerror(err)); return 1; }
    snd_pcm_hw_params_get_period_size(hw, &period, nullptr);
    std::printf("audio: %s, %u Hz, %u channels, %s, %lu-frame periods x %u (%.1f ms)\n", dev.c_str(), rate, ch, snd_pcm_format_name(fmt), (unsigned long) period, periods,
                1000.0 * (double) period * periods / rate);
    std::fflush(stdout);
    { std::lock_guard<std::mutex> g(b.lock); b.eng.prepare(rate); b.eng.setKeyboard(b.sound, b.ctl); }
    // real-time priority when allowed
    sched_param sp {}; sp.sched_priority = 70; pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    std::vector<float> L(period), R(period), fl(period * ch);
    std::vector<int32_t> i32(period * ch); std::vector<int16_t> i16(period * ch);
    while (!quit) {
        { std::lock_guard<std::mutex> g(b.lock); b.eng.render(L.data(), R.data(), (int) period); }
        for (snd_pcm_uframes_t i = 0; i < period; ++i) for (unsigned c = 0; c < ch; ++c) fl[i * ch + c] = c == 0 ? L[i] : c == 1 ? R[i] : 0.f;
        const void* buf = fl.data();
        if (fmt == SND_PCM_FORMAT_S32_LE) { for (size_t i = 0; i < fl.size(); ++i) i32[i] = (int32_t) std::lround(clampd(fl[i], -1, 1) * 2147483520.0); buf = i32.data(); }
        else if (fmt == SND_PCM_FORMAT_S16_LE) { for (size_t i = 0; i < fl.size(); ++i) i16[i] = (int16_t) std::lround(clampd(fl[i], -1, 1) * 32767.0); buf = i16.data(); }
        snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf, period);
        if (w < 0) { w = snd_pcm_recover(pcm, (int) w, 1); if (w < 0) { std::fprintf(stderr, "audio: %s\n", snd_strerror((int) w)); break; } }
    }
    snd_pcm_drain(pcm); snd_pcm_close(pcm);
    return 0;
}

static int writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, unsigned rate);
static int renderFile(Box& b, const std::string& path, double secs, unsigned rate) {
    if (secs <= 0 && !b.eng.song.load()) secs = 4;
    b.eng.prepare(rate); b.eng.setKeyboard(b.sound, b.ctl);
    const size_t n = (size_t) (secs * rate);
    std::vector<float> L(n), R(n);
    if (const SongEntry* e = b.songs.at(b.songs.indexOf(b.eng.song.load()))) {   // a song: all of it (or secs, if given)
        Rendered r = renderSong(b.kit.bank, *e, rate);
        if (secs > 0 && r.L.size() > n) { r.L.resize(n); r.R.resize(n); }
        return writeWav(path, r.L, r.R, rate);
    }
    if (secs <= 0) return renderFile(b, path, 4, rate);
    // play the keyboard voice: a chord, then Page R if nothing else is asked
    const int notes[3] = { 60, 64, 67 };
    for (int k : notes) b.eng.keyOn(k, 0.9f);
    b.eng.render(L.data(), R.data(), (int) std::min(n, (size_t) rate));
    for (int k : notes) b.eng.keyOff(k);
    if (n > rate) { b.eng.extSync = false; b.eng.seqRun = true; b.eng.render(L.data() + rate, R.data() + rate, (int) (n - rate)); }
    return writeWav(path, L, R, rate);
}

static int writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, unsigned rate) {
    const size_t n = L.size(); const double secs = (double) n / rate;
    std::ofstream o(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { o.write((const char*) &v, 4); }; auto u16 = [&](uint16_t v) { o.write((const char*) &v, 2); };
    o.write("RIFF", 4); u32(36 + (uint32_t) n * 4); o.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16); o.write("data", 4); u32((uint32_t) n * 4);
    double pk = 0;
    for (size_t i = 0; i < n; ++i) { pk = std::max(pk, (double) std::fabs(L[i])); int16_t a = (int16_t) std::lround(clampd(L[i], -1, 1) * 32767), c = (int16_t) std::lround(clampd(R[i], -1, 1) * 32767); u16((uint16_t) a); u16((uint16_t) c); }
    std::printf("wrote %s (%.1f s, peak %.3f)\n", path.c_str(), secs, pk);
    return pk > 0.001 ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string dev = "default", extraMidi, wavOut, statePath;
    unsigned rate = 48000, periods = 3; snd_pcm_uframes_t period = 128; double wavSecs = 0; bool listOnly = false, multi = false; int chan = 0;
    if (const char* h = std::getenv("HOME")) statePath = std::string(h) + "/.omega-cmi.state";
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "-d") dev = next(); else if (a == "-r") rate = (unsigned) std::atoi(next().c_str());
        else if (a == "-p") period = (snd_pcm_uframes_t) std::atoi(next().c_str()); else if (a == "-n") periods = (unsigned) std::atoi(next().c_str());
        else if (a == "-m") extraMidi = next(); else if (a == "-c") chan = std::atoi(next().c_str()); else if (a == "-t") multi = true;
        else if (a == "-s") statePath = next(); else if (a == "-l") listOnly = true;
        else if (a == "-w") { wavOut = next(); wavSecs = std::atof(next().c_str()); }
        else if (a == "-h" || a == "--help") { std::puts("cmi-headless [-d pcm] [-r rate] [-p period] [-n periods] [-m client:port] [-c ch] [-t] [-s state] [-l] [-w out.wav secs] [disks / voices / wavs / folders]"); return 0; }
        else files.push_back(a);
    }
    std::signal(SIGINT, onSignal); std::signal(SIGTERM, onSignal);
    auto* box = new Box();   // the engine lives for the whole run
    Box& b = *box;
    b.eng.bank = &b.kit.bank;
    b.eng.kitVoice = kitLookup(b.kit.bank);
    b.channel = chan; b.eng.setMultiTimbral(multi);
    Library::factorySeq(b.eng.seq, Library::get());
    b.eng.extSync = false;
    selectSound(b, Library::get().find("ORCHHIT"));
    if (wavOut.empty()) loadState(b, statePath);
    int first = -1;
    for (auto& f : files) { const int before = b.kit.loadedCount(); if (load(b, f) > 0 && first < 0) { for (int k = 0; k < Library::USER_SLOTS; ++k) if (b.kit.userLoaded(k) && k >= before) { first = k; break; } } }
    if (first >= 0) selectSound(b, b.kit.userSlot(first));
    if (listOnly) { list(b); return 0; }
    if (!wavOut.empty()) return renderFile(b, wavOut, wavSecs, rate);
    std::thread midi(midiThread, std::ref(b), extraMidi);
    std::thread cmd(commandThread, std::ref(b), statePath);
    cmd.detach();
    const int rc = runAlsa(b, dev, rate, period, periods);
    quit = true;
    midi.join();
    saveState(b, statePath);
    return rc;
}
