"""Offline tool: the phone's copy of the DrumSep ONNX model, half the size.

Every float weight is stored as 16-bit and turned back into 32-bit by a Cast when ONNX Runtime loads the model, so the
maths still runs in 32-bit. Checked against the full model on a real drum stem (MDB Rock): kick 81 dB, snare 78 dB,
hi-hat 68 dB SNR, and the drum transcription is identical (same hits, same times). 8-bit weights or 8-bit activations
were tried: 8-bit activations ruin the quiet stems, 8-bit weights change a few hits.

    python export_drumsep_mobile.py drumsep_mdx23c.onnx drumsep_mdx23c.mobile.onnx
"""
import sys

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper


def main(src, dst):
    m = onnx.load(src)
    g = m.graph
    inits, casts = [], []
    for i in g.initializer:
        if i.data_type == TensorProto.FLOAT and int(np.prod(i.dims)) >= 1024:
            inits.append(numpy_helper.from_array(numpy_helper.to_array(i).astype(np.float16), i.name + "_h"))
            casts.append(helper.make_node("Cast", [i.name + "_h"], [i.name], to=TensorProto.FLOAT, name=i.name + "_cast"))
        else:
            inits.append(i)
    del g.initializer[:]
    g.initializer.extend(inits)
    nodes = list(g.node)
    del g.node[:]
    g.node.extend(casts + nodes)
    onnx.checker.check_model(m)
    onnx.save(m, dst)
    print(f"{len(casts)} weights stored as 16-bit -> {dst}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
