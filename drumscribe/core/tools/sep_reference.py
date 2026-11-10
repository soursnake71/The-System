#!/usr/bin/env python3
"""Offline reference for tools/sep_check.cpp: the PyTorch separation the C++ engine reproduces, on the same input.

    python tools/sep_reference.py demucs  <checkpoint.th> <song.wav> <out_prefix> <shift>
    python tools/sep_reference.py drumsep <ckpt_dir> <drums.wav> <out_prefix> <overlap>

demucs:  demucs 4.1 (demucs.api.Separator's normalization + apply_model(shifts=1, split=True, overlap=0.25)) with one
         HTDemucs checkpoint (htdemucs, or one model of the htdemucs_ft bag: in the bag each source comes from its own
         model, so this equals the bag's output for that source). demucs draws the shift at random; it's fixed to
         <shift> here so both sides use the same one.
drumsep: python-audio-separator's MDXCSeparator.demix with MDX23C-DrumSep-aufr33-jarredou.ckpt, overlap <overlap>,
         on the drums scaled to 0.5 peak (as the prototype did), stems scaled back. Writes float WAVs.
"""
import os
import sys
import time

import numpy as np
import soundfile as sf
import torch


def demucs_ref(ckpt, song, prefix, shift):
    import demucs.apply as apply
    from demucs.states import load_model

    model = load_model(ckpt)
    model.eval()
    wav, sr = sf.read(song, dtype="float32", always_2d=True)
    wav = torch.from_numpy(wav.T.copy())
    apply.random.randint = lambda a, b: shift  # demucs: offset = random.randint(0, max_shift)
    t = time.time()
    ref = wav.mean(0)
    mean, std = ref.mean(), ref.std() + 1e-8
    with torch.no_grad():
        out = apply.apply_model(model, ((wav - mean) / std)[None], shifts=1, split=True, overlap=0.25,
                                progress=False, num_workers=0)[0]
    out = out * std + mean
    dt = time.time() - t
    for i, name in enumerate(model.sources):
        if name in ("drums", "bass"):
            sf.write(f"{prefix}_{name}.wav", out[i].numpy().T, sr, subtype="FLOAT")
    print(f"torch demucs {os.path.basename(ckpt)}: {wav.shape[1] / sr:.1f} s audio in {dt:.1f} s "
          f"({torch.get_num_threads()} threads)")


def drumsep_ref(ckpt_dir, drums, prefix, overlap):
    from audio_separator.separator import Separator

    x, sr = sf.read(drums, dtype="float32", always_2d=True)
    gain = 0.5 / (float(np.abs(x).max()) or 1.0)
    sep = Separator(model_file_dir=ckpt_dir, output_dir=os.path.dirname(prefix) or ".", sample_rate=sr,
                    normalization_threshold=1.0, log_level=40,
                    mdxc_params={"segment_size": 256, "override_model_segment_size": False, "batch_size": 1,
                                 "overlap": overlap, "pitch_shift": 0})
    sep.load_model("MDX23C-DrumSep-aufr33-jarredou.ckpt")
    m = sep.model_instance
    t = time.time()
    src = m.demix(mix=(x * gain).T.copy())
    dt = time.time() - t
    for name, y in src.items():
        sf.write(f"{prefix}_{name}.wav", (y / gain).T, sr, subtype="FLOAT")
    print(f"torch drumsep overlap {overlap}: {x.shape[0] / sr:.1f} s audio in {dt:.1f} s "
          f"({torch.get_num_threads()} threads)")


if __name__ == "__main__":
    if len(sys.argv) != 6 or sys.argv[1] not in ("demucs", "drumsep"):
        sys.exit(__doc__)
    if sys.argv[1] == "demucs":
        demucs_ref(sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]))
    else:
        drumsep_ref(sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]))
