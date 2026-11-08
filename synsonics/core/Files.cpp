#include "Files.h"

#include <cstdio>
#include <map>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include "../third_party/minimp3/minimp3_ex.h"

extern "C" {
#include "../third_party/shine/layer3.h"
}

namespace syn {

namespace {

void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
void put32(std::vector<uint8_t>& v, uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }
void putTag(std::vector<uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }
uint32_t rd16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t rd32(const uint8_t* p) { return rd16(p) | rd16(p + 2) << 16; }
uint32_t be16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }
uint32_t be32(const uint8_t* p) { return be16(p) << 16 | be16(p + 2); }

// the steps to play, in order: one pattern `loops` times, or the song chain
std::vector<std::pair<int, int>> stepList(const Engine& e, const ExportOptions& o) {
  std::vector<std::pair<int, int>> out;  // (pattern, step)
  int loops = std::max(1, o.loops);
  for (int k = 0; k < loops; ++k) {
    if (o.song && e.songLength() > 0) {
      for (int i = 0; i < e.songLength(); ++i) {
        int p = e.songEntry(i);
        for (int s = 0; s < e.patternLength(p); ++s) out.push_back({p, s});
      }
    } else {
      for (int s = 0; s < e.patternLength(o.pattern); ++s) out.push_back({o.pattern, s});
    }
  }
  return out;
}

}  // namespace

std::string exportName(const Engine& e, const ExportOptions& o, int op) {
  char b[64];
  if (o.song && e.songLength() > 0) std::snprintf(b, sizeof b, "synsonics-song.%s", opExtension(op));
  else std::snprintf(b, sizeof b, "synsonics-pattern-%02d.%s", o.pattern + 1, opExtension(op));
  return b;
}

void renderOffline(const Engine& src, const ExportOptions& o, std::vector<float>& l, std::vector<float>& r) {
  auto e = std::make_unique<Engine>();
  auto st = src.saveState();
  e->loadState(st.data(), st.size());
  e->setSampleRate(o.sampleRate);
  e->setSample(src.sample());
  size_t steps = stepList(src, o).size();
  double bpm = src.tempo();
  double secs = double(steps) * 60.0 / (bpm * Engine::kStepsPerBeat);
  secs = std::min(secs, 20.0 * 60.0);
  double tail = 1.5 + Rev7::rtSeconds(src.params[pRevTime]) * 0.6;
  size_t body = size_t(secs * o.sampleRate), total = body + size_t(tail * o.sampleRate);
  l.assign(total, 0.f); r.assign(total, 0.f);
  e->startForRender(o.song, o.pattern);
  // throw away nothing: the engine starts at rest, so the first sample is the first step
  size_t pos = 0;
  const int block = 512;
  while (pos < total) {
    if (pos >= body && pos < body + block) e->stopForRender();
    int n = int(std::min<size_t>(block, total - pos));
    float* p[2] = {l.data() + pos, r.data() + pos};
    e->process(p, n);
    pos += size_t(n);
  }
  // 50 ms fade at the very end
  size_t fade = std::min<size_t>(total, size_t(0.05 * o.sampleRate));
  for (size_t i = 0; i < fade; ++i) { float g = float(i) / float(fade); l[total - 1 - i] *= g; r[total - 1 - i] *= g; }
}

std::vector<uint8_t> encodeWav(const std::vector<float>& l, const std::vector<float>& r, int rate, int bits) {
  bits = bits == 16 ? 16 : 24;
  size_t n = l.size();
  int bytes = bits / 8, align = 2 * bytes;
  std::vector<uint8_t> v;
  v.reserve(44 + n * size_t(align));
  putTag(v, "RIFF"); put32(v, uint32_t(36 + n * size_t(align))); putTag(v, "WAVE");
  putTag(v, "fmt "); put32(v, 16); put16(v, 1); put16(v, 2); put32(v, uint32_t(rate)); put32(v, uint32_t(rate * align)); put16(v, uint32_t(align)); put16(v, uint32_t(bits));
  putTag(v, "data"); put32(v, uint32_t(n * size_t(align)));
  uint32_t seed = 22222;
  auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.f; };
  for (size_t i = 0; i < n; ++i)
    for (int c = 0; c < 2; ++c) {
      float x = std::clamp(c ? r[i] : l[i], -1.f, 1.f);
      if (bits == 16) {
        float d = (rnd() - rnd()) / 32768.f;  // TPDF dither
        int32_t q = int32_t(std::lround(std::clamp(x + d, -1.f, 1.f) * 32767.f));
        put16(v, uint32_t(q) & 0xFFFF);
      } else {
        int32_t q = int32_t(std::lround(x * 8388607.f));
        v.push_back(uint8_t(q)); v.push_back(uint8_t(q >> 8)); v.push_back(uint8_t(q >> 16));
      }
    }
  return v;
}

std::vector<uint8_t> encodeMp3(const std::vector<float>& l, const std::vector<float>& r, int rate, int kbps) {
  std::vector<uint8_t> out;
  if (shine_check_config(rate, kbps) < 0) return out;
  shine_config_t cfg;
  shine_set_config_mpeg_defaults(&cfg.mpeg);
  cfg.wave.channels = PCM_STEREO;
  cfg.wave.samplerate = rate;
  cfg.mpeg.mode = STEREO;
  cfg.mpeg.bitr = kbps;
  shine_t s = shine_initialise(&cfg);
  if (!s) return out;
  int per = shine_samples_per_pass(s);
  std::vector<int16_t> buf(size_t(per) * 2);
  size_t n = l.size();
  for (size_t pos = 0; pos < n; pos += size_t(per)) {
    for (int i = 0; i < per; ++i) {
      size_t k = pos + size_t(i);
      float a = k < n ? l[k] : 0.f, b = k < n ? r[k] : 0.f;
      buf[size_t(2 * i)] = int16_t(std::lround(std::clamp(a, -1.f, 1.f) * 32767.f));
      buf[size_t(2 * i + 1)] = int16_t(std::lround(std::clamp(b, -1.f, 1.f) * 32767.f));
    }
    int written = 0;
    unsigned char* data = shine_encode_buffer_interleaved(s, buf.data(), &written);
    if (data && written > 0) out.insert(out.end(), data, data + written);
  }
  int written = 0;
  unsigned char* data = shine_flush(s, &written);
  if (data && written > 0) out.insert(out.end(), data, data + written);
  shine_close(s);
  return out;
}

std::shared_ptr<SampleData> decodeAudio(const std::vector<uint8_t>& d, std::string& err) {
  auto smp = std::make_shared<SampleData>();
  const size_t maxFrames = size_t(48000) * 60 * 5;  // five minutes
  if (d.size() >= 12 && std::memcmp(d.data(), "RIFF", 4) == 0 && std::memcmp(d.data() + 8, "WAVE", 4) == 0) {
    size_t p = 12;
    int fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    size_t dataLen = 0;
    while (p + 8 <= d.size()) {
      const uint8_t* c = d.data() + p;
      uint32_t len = rd32(c + 4);
      size_t body = p + 8, avail = d.size() - body;
      if (std::memcmp(c, "fmt ", 4) == 0 && avail >= 16) {
        fmt = int(rd16(c + 8)); ch = int(rd16(c + 10)); rate = rd32(c + 12); bits = int(rd16(c + 22));
        if (fmt == 0xFFFE && avail >= 26) fmt = int(rd16(c + 32));  // extensible: the sub-format's first two bytes
      } else if (std::memcmp(c, "data", 4) == 0) {
        data = d.data() + body;
        dataLen = std::min<size_t>(len, avail);
        break;
      }
      p = body + len + (len & 1);
    }
    if (!data || ch < 1 || rate < 4000) { err = "That WAV file has no audio this app can read."; return nullptr; }
    int bps = bits / 8;
    bool ok = (fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && (bits == 32 || bits == 64));
    if (!ok) { err = "WAV format not supported (use PCM 8/16/24/32-bit or float)."; return nullptr; }
    size_t frames = std::min(dataLen / size_t(bps * ch), maxFrames);
    smp->rate = rate;
    smp->l.resize(frames);
    if (ch > 1) smp->r.resize(frames);
    for (size_t i = 0; i < frames; ++i)
      for (int c = 0; c < std::min(ch, 2); ++c) {
        const uint8_t* s = data + (i * size_t(ch) + size_t(c)) * size_t(bps);
        float x = 0;
        if (fmt == 3 && bits == 32) { float f; std::memcpy(&f, s, 4); x = f; }
        else if (fmt == 3) { double f; std::memcpy(&f, s, 8); x = float(f); }
        else if (bits == 8) x = (float(s[0]) - 128.f) / 128.f;
        else if (bits == 16) x = float(int16_t(rd16(s))) / 32768.f;
        else if (bits == 24) x = float(int32_t((uint32_t(s[0]) << 8) | (uint32_t(s[1]) << 16) | (uint32_t(s[2]) << 24)) >> 8) / 8388608.f;
        else x = float(int32_t(rd32(s))) / 2147483648.f;
        (c ? smp->r : smp->l)[i] = std::isfinite(x) ? std::clamp(x, -4.f, 4.f) : 0.f;
      }
  } else {
    mp3dec_t dec;
    mp3dec_file_info_t info;
    std::memset(&info, 0, sizeof info);
    if (mp3dec_load_buf(&dec, d.data(), d.size(), &info, nullptr, nullptr) != 0 || !info.buffer || info.samples == 0 || info.channels < 1) {
      std::free(info.buffer);
      err = "Not a WAV or MP3 file this app can read.";
      return nullptr;
    }
    size_t frames = std::min(size_t(info.samples) / size_t(info.channels), maxFrames);
    smp->rate = info.hz;
    smp->l.resize(frames);
    if (info.channels > 1) smp->r.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
      smp->l[i] = info.buffer[i * size_t(info.channels)];
      if (info.channels > 1) smp->r[i] = info.buffer[i * size_t(info.channels) + 1];
    }
    std::free(info.buffer);
  }
  if (smp->l.size() < 2) { err = "The file has no audio in it."; return nullptr; }
  return smp;
}

std::vector<uint8_t> encodeMidi(const Engine& e, const ExportOptions& o) {
  const int ppq = 96, perStep = ppq / Engine::kStepsPerBeat, noteLen = perStep / 2;
  std::vector<uint8_t> trk;
  auto vlq = [&](uint32_t v) {
    uint8_t b[5]; int n = 0;
    b[n++] = uint8_t(v & 0x7F);
    while (v >>= 7) b[n++] = uint8_t((v & 0x7F) | 0x80);
    while (n) trk.push_back(b[--n]);
  };
  // track name and tempo
  const char* name = "Omega Synsonics";
  vlq(0); trk.push_back(0xFF); trk.push_back(0x03); vlq(uint32_t(std::strlen(name))); trk.insert(trk.end(), name, name + std::strlen(name));
  uint32_t uspq = uint32_t(60000000.0 / std::max(1.f, e.tempo()));
  vlq(0); trk.push_back(0xFF); trk.push_back(0x51); trk.push_back(3); trk.push_back(uint8_t(uspq >> 16)); trk.push_back(uint8_t(uspq >> 8)); trk.push_back(uint8_t(uspq));
  vlq(0); trk.push_back(0xFF); trk.push_back(0x58); trk.push_back(4); trk.push_back(4); trk.push_back(2); trk.push_back(24); trk.push_back(8);
  // notes, on channel 10 (General MIDI drums)
  struct Ev { uint32_t t; bool on; int note; };
  std::vector<Ev> ev;
  auto steps = stepList(e, o);
  for (size_t i = 0; i < steps.size(); ++i) {
    uint8_t bits = e.step(steps[i].first, steps[i].second);
    bool acc = (bits >> kAccentBit) & 1;
    for (int v = 0; v < NumVoices; ++v)
      if (bits & bitFor(v)) {
        uint32_t t = uint32_t(i) * perStep;
        int note = noteForVoice(v, acc);
        ev.push_back({t, true, note});
        ev.push_back({t + uint32_t(noteLen), false, note});
      }
  }
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t != b.t ? a.t < b.t : (!a.on && b.on); });
  uint32_t now = 0;
  for (auto& x : ev) {
    vlq(x.t - now); now = x.t;
    trk.push_back(x.on ? 0x99 : 0x89); trk.push_back(uint8_t(x.note)); trk.push_back(x.on ? 100 : 0);
  }
  uint32_t end = uint32_t(steps.size()) * perStep;
  vlq(end > now ? end - now : 0); trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0);
  std::vector<uint8_t> f;
  auto b32 = [&](uint32_t v) { for (int i = 3; i >= 0; --i) f.push_back(uint8_t(v >> (8 * i))); };
  auto b16 = [&](uint32_t v) { f.push_back(uint8_t(v >> 8)); f.push_back(uint8_t(v)); };
  f.insert(f.end(), {'M', 'T', 'h', 'd'}); b32(6); b16(0); b16(1); b16(ppq);
  f.insert(f.end(), {'M', 'T', 'r', 'k'}); b32(uint32_t(trk.size()));
  f.insert(f.end(), trk.begin(), trk.end());
  return f;
}

namespace {

std::string importMidi(Engine& e, const std::vector<uint8_t>& d, int pattern, std::string& info) {
  if (d.size() < 14 || std::memcmp(d.data(), "MThd", 4) != 0) return "Not a MIDI file.";
  uint32_t hlen = be32(d.data() + 4);
  int ntrk = int(be16(d.data() + 10));
  int div = int(int16_t(be16(d.data() + 12)));
  if (div <= 0) return "MIDI files timed in SMPTE frames aren't supported.";
  struct Note { uint32_t tick; int ch, note; };
  std::vector<Note> notes;
  double fileBpm = 0;
  size_t p = 8 + hlen;
  for (int t = 0; t < ntrk && p + 8 <= d.size(); ++t) {
    if (std::memcmp(d.data() + p, "MTrk", 4) != 0) break;
    uint32_t len = be32(d.data() + p + 4);
    size_t q = p + 8, end = std::min(d.size(), q + len);
    uint32_t tick = 0;
    uint8_t status = 0;
    auto vlq = [&](uint32_t& v) { v = 0; for (int i = 0; i < 4 && q < end; ++i) { uint8_t b = d[q++]; v = (v << 7) | (b & 0x7F); if (!(b & 0x80)) return true; } return false; };
    while (q < end) {
      uint32_t delta;
      if (!vlq(delta)) break;
      tick += delta;
      if (q >= end) break;
      uint8_t b = d[q];
      if (b & 0x80) { status = b; ++q; }
      if (status == 0xFF) {
        if (q >= end) break;
        uint8_t type = d[q++];
        uint32_t ml;
        if (!vlq(ml) || q + ml > end) break;
        if (type == 0x51 && ml == 3 && fileBpm == 0) fileBpm = 60000000.0 / double((uint32_t(d[q]) << 16) | (uint32_t(d[q + 1]) << 8) | d[q + 2]);
        q += ml;
        status = 0;
      } else if (status == 0xF0 || status == 0xF7) {
        uint32_t sl;
        if (!vlq(sl)) break;
        q += sl;
        status = 0;
      } else if (status >= 0x80) {
        int kind = status & 0xF0, ch = status & 0x0F;
        int nbytes = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
        if (q + size_t(nbytes) > end) break;
        if (kind == 0x90 && d[q + 1] > 0) notes.push_back({tick, ch, d[q]});
        q += size_t(nbytes);
      } else {
        ++q;  // stray data byte
      }
    }
    p = q > p + 8 + len ? q : p + 8 + len;
  }
  bool hasDrumChannel = false;
  for (auto& n : notes) hasDrumChannel |= n.ch == 9;
  // steps: (step, bits)
  std::map<int, uint8_t> steps;
  int skipped = 0;
  for (auto& n : notes) {
    if (hasDrumChannel && n.ch != 9) continue;
    bool closed = false;
    int v = drumForNote(n.note, closed);
    if (v < 0) { ++skipped; continue; }
    int s = int(std::lround(double(n.tick) * Engine::kStepsPerBeat / div));
    uint8_t bits = bitFor(v);
    if (closed) bits |= uint8_t(1 << kAccentBit);
    steps[s] |= bits;
  }
  if (steps.empty()) return "No drum notes in that MIDI file.";
  int last = steps.rbegin()->first;
  int len = (last / 16 + 1) * 16;  // whole bars
  char b[160];
  if (len <= Engine::kMemSteps) {
    e.clearPattern(pattern);
    for (auto& s : steps) e.setStepBits(pattern, s.first, s.second);
    e.setPatternLength(pattern, len);
    std::snprintf(b, sizeof b, "%d bars into pattern %d", len / 16, pattern + 1);
  } else {
    // long files: 4-bar patterns from here on, chained into a song
    const int chunk = 64;
    int count = std::min((len + chunk - 1) / chunk, Engine::kNumPat - pattern);
    for (int k = 0; k < count; ++k) e.clearPattern(pattern + k);
    for (auto& s : steps) {
      int k = s.first / chunk;
      if (k < count) e.setStepBits(pattern + k, s.first % chunk, s.second);
    }
    for (int k = 0; k < count; ++k) {
      e.setPatternLength(pattern + k, std::min(chunk, len - k * chunk));
      e.setSongEntry(k, pattern + k);
    }
    e.setSongLength(count);
    e.setSongMode(true);
    std::snprintf(b, sizeof b, "%d bars into patterns %d-%d, chained as the song%s", std::min(len, count * chunk) / 16, pattern + 1, pattern + count,
                  count * chunk < len ? " (the rest didn't fit)" : "");
  }
  info = b;
  if (fileBpm > 0) e.setTempo(float(fileBpm));
  if (skipped) info += " (" + std::to_string(skipped) + " notes with no Synsonics voice skipped)";
  return "";
}

}  // namespace

std::string exportFile(const Engine& e, const ExportOptions& o, int op, std::vector<uint8_t>& out) {
  out.clear();
  if (stepList(e, o).empty()) return o.song ? "The song is empty: add patterns to it in the grid." : "That pattern is empty.";
  if (op == OpExportMidi) { out = encodeMidi(e, o); return ""; }
  ExportOptions ro = o;
  if (op == OpExportMp3) ro.sampleRate = 44100;
  std::vector<float> l, r;
  renderOffline(e, ro, l, r);
  if (op == OpExportWav) out = encodeWav(l, r, ro.sampleRate, ro.wavBits);
  else out = encodeMp3(l, r, ro.sampleRate, ro.mp3Kbps);
  return out.empty() ? "Encoding failed." : "";
}

std::string importFile(Engine& e, int op, const std::vector<uint8_t>& data, const std::string& name, int pattern, std::string& info) {
  if (data.empty()) return "The file is empty.";
  if (op == OpImportMidi) return importMidi(e, data, std::clamp(pattern, 0, Engine::kNumPat - 1), info);
  std::string err;
  auto smp = decodeAudio(data, err);
  if (!smp) return err;
  std::string nm = name;
  size_t slash = nm.find_last_of("/\\");
  if (slash != std::string::npos) nm = nm.substr(slash + 1);
  smp->name = nm.empty() ? "SAMPLE" : nm;
  char b[200];
  std::snprintf(b, sizeof b, "%s: %.2f s, %.1f kHz %s", smp->name.c_str(), double(smp->frames()) / smp->rate, smp->rate / 1000.0, smp->r.empty() ? "mono" : "stereo");
  info = b;
  e.setSample(std::move(smp));
  return "";
}

}  // namespace syn
