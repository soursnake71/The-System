// Files: export the pattern or the song as WAV, MP3 or a MIDI file; import a MIDI file into the patterns, or a WAV
// or MP3 onto the sample pad. Everything works on byte buffers, so the plugin (desktop files) and the Android app
// (the system file picker) share it; each only moves the bytes.
//
// MP3: decoding with minimp3 (public domain), encoding with shine (LGPL), both in third_party/. Their C code is
// compiled once, in Files.cpp.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Engine.h"

namespace syn {

enum FileOp { OpExportWav, OpExportMp3, OpExportMidi, OpImportMidi, OpImportAudio, NumFileOps };
inline bool isExport(int op) { return op <= OpExportMidi; }
inline const char* opExtension(int op) {
  switch (op) { case OpExportWav: return "wav"; case OpExportMp3: return "mp3"; case OpExportMidi: case OpImportMidi: return "mid"; default: return ""; }
}
inline const char* opMime(int op) {
  switch (op) { case OpExportWav: return "audio/wav"; case OpExportMp3: return "audio/mpeg"; case OpExportMidi: case OpImportMidi: return "audio/midi"; default: return "audio/*"; }
}

struct ExportOptions {
  bool song = false;  // the song chain, else the pattern being edited
  int pattern = 0;
  int loops = 1;      // times through it
  int sampleRate = 44100;
  int wavBits = 24;   // 16 or 24
  int mp3Kbps = 256;
};

// suggested file name
std::string exportName(const Engine& e, const ExportOptions& o, int op);

// render (pattern or song, loops, plus the reverb/delay tail) with a copy of the engine's state
void renderOffline(const Engine& e, const ExportOptions& o, std::vector<float>& l, std::vector<float>& r);

// Each returns an empty string on success, else a message for the user.
std::string exportFile(const Engine& e, const ExportOptions& o, int op, std::vector<uint8_t>& out);
// MIDI goes into `pattern` (and the patterns after it if it is longer than 340 steps, which also builds a song);
// audio goes onto the sample pad. `info` says what was loaded.
std::string importFile(Engine& e, int op, const std::vector<uint8_t>& data, const std::string& name, int pattern, std::string& info);

// pieces, also used by the tests and the hosts
std::vector<uint8_t> encodeWav(const std::vector<float>& l, const std::vector<float>& r, int rate, int bits);
std::vector<uint8_t> encodeMp3(const std::vector<float>& l, const std::vector<float>& r, int rate, int kbps);
std::shared_ptr<SampleData> decodeAudio(const std::vector<uint8_t>& data, std::string& err);  // WAV or MP3
std::vector<uint8_t> encodeMidi(const Engine& e, const ExportOptions& o);

}  // namespace syn
