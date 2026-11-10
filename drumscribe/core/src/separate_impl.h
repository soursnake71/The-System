// Drum Scribe separation internals (src/separate.cpp). Private: used by separate() and tools/sep_check.cpp.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "ds/modules.h"

namespace ds::sep {

// model files inside model_dir (tools/get_models.sh writes them)
constexpr const char* kDemucsFast = "ggml-model-htdemucs-4s-f16.bin";         // htdemucs (4 sources)
constexpr const char* kDemucsFtDrums = "ggml-model-htdemucs_ft_drums-4s-f16.bin"; // htdemucs_ft drums model
constexpr const char* kDemucsFtBass = "ggml-model-htdemucs_ft_bass-4s-f16.bin";   // htdemucs_ft bass model
constexpr const char* kDrumSep = "drumsep_mdx23c.onnx";                         // MDX23C DrumSep net (no STFT)
extern const char* const kKitStems[6];                                          // kick snare toms hh ride crash

// "" -> $DRUMSCRIBE_MODELS, else ~/.cache/drumscribe/models (%LOCALAPPDATA%\drumscribe\models on Windows)
std::string resolve_model_dir(const std::string& model_dir);
int num_threads();  // $DS_THREADS or all hardware threads

// Demucs v4 exactly like demucs.api.Separator (whole-track normalization, shifts=1, split, overlap 0.25,
// transition_power 1). Returns the requested sources (0 drums, 1 bass, 2 other, 3 vocals), stereo.
// shift_offset: the random time shift demucs draws (0..22050 samples); -1 = the fixed default (or $DS_DEMUCS_SHIFT).
std::vector<Stereo> demucs_run(const std::string& model_file, const Stereo& song, const std::vector<int>& sources,
                               int shift_offset, const Progress& p);
int demucs_default_shift();

// MDX23C DrumSep exactly like audio-separator's MDXCSeparator.demix (non-RoFormer path): chunk 130560 samples,
// hop chunk/overlap, zero padding, plain overlap-add divided by `overlap`. Input as is (no scaling here).
// Returns stereo stems in kKitStems order.
std::vector<Stereo> drumsep_run(const std::string& onnx_file, const Stereo& drums, int overlap, const Progress& p);
int drumsep_overlap(Quality q);  // fast 2, best 4, or $DS_DRUMSEP_OVERLAP (audio-separator's default is 44)

void release_models();  // drop the cached models / ONNX session (they're kept between separate() calls)

}  // namespace ds::sep
