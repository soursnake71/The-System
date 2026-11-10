"""Source separation: the song -> the drums (Demucs) -> each kit piece (DrumSep).

1. Demucs htdemucs_ft (Meta's fine-tuned hybrid transformer, the most accurate open drum-stem model) takes the drums
   out of the full mix. "fast" quality uses plain htdemucs (about 4x faster).
2. MDX23C DrumSep (aufr33 & jarredou) splits that drum stem into kick, snare, toms, hi-hat, ride and crash.

Models download on first use (about 1.3 GB) into ~/.cache/drumscribe (or $DRUMSCRIBE_MODELS). A GPU (CUDA, or Apple
MPS for Demucs) is used when there is one.
"""
from __future__ import annotations

import glob
import os

import numpy as np
import soundfile as sf

from .kit import STEMS

DRUMSEP = "MDX23C-DrumSep-aufr33-jarredou.ckpt"
SR = 44100


def model_dir():
    d = os.environ.get("DRUMSCRIBE_MODELS") or os.path.join(os.path.expanduser("~"), ".cache", "drumscribe")
    os.makedirs(d, exist_ok=True)
    return d


def _device():
    import torch
    if torch.cuda.is_available():
        return "cuda"
    if getattr(torch.backends, "mps", None) and torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def extract_drums(song_wav: str, out_wav: str, quality="best", progress=None):
    import torch
    from demucs.api import Separator

    os.environ.setdefault("TORCH_HOME", model_dir())

    def cb(d):
        # demucs reports segment offsets per model of the bag (htdemucs_ft is 4 models)
        if progress and d.get("audio_length"):
            n = max(1, d.get("models", 1))
            progress(min(0.99, (d.get("model_idx_in_bag", 0) + d["segment_offset"] / d["audio_length"]) / n))

    sep = Separator(model="htdemucs_ft" if quality == "best" else "htdemucs", device=_device(),
                    shifts=1, overlap=0.25, callback=cb)
    wav, _ = sf.read(song_wav, dtype="float32", always_2d=True)
    with torch.no_grad():
        _, stems = sep.separate_tensor(torch.from_numpy(wav.T.copy()), SR)
    drums = stems["drums"].cpu().numpy().T
    sf.write(out_wav, drums, SR, subtype="FLOAT")
    return out_wav


def split_kit(drums_wav: str, out_dir: str, progress=None) -> dict[str, np.ndarray]:
    """Returns mono float32 arrays for each of STEMS, at the drum stem's level (no per-stem normalizing)."""
    from audio_separator.separator import Separator

    os.makedirs(out_dir, exist_ok=True)
    x, sr = sf.read(drums_wav, dtype="float32", always_2d=True)
    peak = float(np.abs(x).max()) or 1.0
    gain = 0.5 / peak  # headroom so no stem clips in the 16-bit files; undone below
    scaled = os.path.join(out_dir, "drums_in.wav")
    sf.write(scaled, x * gain, sr, subtype="FLOAT")
    sep = Separator(model_file_dir=model_dir(), output_dir=out_dir, output_format="WAV",
                    normalization_threshold=1.0, sample_rate=SR, log_level=40)
    sep.load_model(DRUMSEP)
    files = sep.separate(scaled)
    out = {}
    for name in STEMS:
        f = [p for p in files if f"({name})" in os.path.basename(p)]
        if not f:
            f = glob.glob(os.path.join(out_dir, f"*({name})*.wav"))
        if not f:
            raise RuntimeError(f"DrumSep didn't produce the {name} stem")
        p = f[0] if os.path.isabs(f[0]) else os.path.join(out_dir, f[0])
        y, _ = sf.read(p, dtype="float32", always_2d=True)
        out[name] = y.mean(axis=1) / gain
        os.remove(p)
    os.remove(scaled)
    if progress:
        progress(1.0)
    return out


def load_mono(path):
    y, sr = sf.read(path, dtype="float32", always_2d=True)
    return y.mean(axis=1), sr
