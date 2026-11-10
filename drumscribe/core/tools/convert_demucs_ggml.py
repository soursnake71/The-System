#!/usr/bin/env python3
"""Offline tool: convert an official Demucs v4 (HTDemucs) checkpoint (*.th from dl.fbaipublicfiles.com) to the
ggml-style weight file read by demucs.cpp (github.com/sevagh/demucs.cpp, MIT), which the C++ engine links.

    python tools/convert_demucs_ggml.py 955717e8-8726e21a.th ggml-model-htdemucs-4s-f16.bin

Same format as demucs.cpp's scripts/convert-pth-to-ggml.py (magic 'dmc4', then per tensor: n_dims, name length,
dims, name, float16 data), but it reads the checkpoint directly, so only torch is needed (not the demucs package).
The official checkpoints store float16 weights, so the file is bit-exact to what PyTorch Demucs runs with.
"""
import pickle
import struct
import sys

import numpy as np
import torch


class _Stub:
    def __init__(self, *a, **k):
        pass

    def __setstate__(self, state):
        pass


class _Unpickler(pickle.Unpickler):
    # the checkpoint pickles the model class (demucs.htdemucs.HTDemucs) and a few helpers; we only want 'state'
    def find_class(self, module, name):
        if module.startswith("torch") or module in ("collections", "builtins", "numpy", "_codecs") or \
                module.startswith("numpy"):
            return super().find_class(module, name)
        return _Stub


class _PickleModule:
    Unpickler = _Unpickler
    load = pickle.load
    __name__ = "pickle"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    ck = torch.load(src, map_location="cpu", pickle_module=_PickleModule, weights_only=False)
    state = ck["state"] if isinstance(ck, dict) and "state" in ck else ck
    six = any(k.startswith("decoder.3.conv_tr.weight") and v.shape[1] == 24 for k, v in state.items())
    magic = 0x646d6336 if six else 0x646d6334
    with open(dst, "wb") as f:
        f.write(struct.pack("i", magic))
        for name, t in state.items():
            data = t.detach().cpu().squeeze().numpy()
            if data.dtype != np.float16:
                f32 = data.astype(np.float32)
                data = f32.astype(np.float16)
                if not np.array_equal(data.astype(np.float32), f32):
                    print(f"note: {name} rounded to float16", file=sys.stderr)
            nb = name.encode("utf-8")
            f.write(struct.pack("ii", data.ndim, len(nb)))
            for d in data.shape:
                f.write(struct.pack("i", d))
            f.write(nb)
            data.tofile(f)
    print(f"{dst}: {len(state)} tensors")


if __name__ == "__main__":
    main()
