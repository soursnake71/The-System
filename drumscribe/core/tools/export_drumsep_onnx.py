#!/usr/bin/env python3
"""Offline tool: export the MDX23C DrumSep net (aufr33 & jarredou) to ONNX for the C++ engine (ONNX Runtime).

    python tools/export_drumsep_onnx.py MDX23C-DrumSep-aufr33-jarredou.ckpt config_drumsep_mdx23c.yaml drumsep_mdx23c.onnx

Only the network is exported: the STFT and iSTFT stay outside the graph (they're done in C++, src/separate.cpp).
    input  "spec": float32 [batch, 4, 1024, 256]     complex-as-channels STFT of one 130560-sample stereo chunk,
                                                      channels (L.re, L.im, R.re, R.im), bins 0..1023, 256 frames
    output "out" : float32 [batch, 6, 4, 1024, 256]  the same layout per stem (kick snare toms hh ride crash),
                                                      bin 1024 (Nyquist) is zero (padded before the iSTFT)
The net is TFC_TDF_net (tfc_tdf_v3) from ZFTurbo's Music-Source-Separation-Training / python-audio-separator (MIT),
reproduced here without its STFT so only torch + pyyaml (+ onnx/onnxruntime for the check) are needed.
The script checks ONNX Runtime against torch on random and real-looking input and prints the SNR.
"""
import sys
from functools import partial

import numpy as np
import torch
import torch.nn as nn
import yaml


def _norm(c, norm_type):
    if norm_type == "BatchNorm":
        return nn.BatchNorm2d(c)
    if norm_type == "InstanceNorm":
        return nn.InstanceNorm2d(c, affine=True)
    if norm_type and "GroupNorm" in norm_type:
        return nn.GroupNorm(num_groups=int(norm_type.replace("GroupNorm", "")), num_channels=c)
    return nn.Identity()


def _act(a):
    if a == "gelu":
        return nn.GELU()
    if a == "relu":
        return nn.ReLU()
    if a[:3] == "elu":
        return nn.ELU(float(a.replace("elu", "")))
    raise ValueError(a)


class Upscale(nn.Module):
    def __init__(self, in_c, out_c, scale, norm, act):
        super().__init__()
        self.conv = nn.Sequential(norm(in_c), act, nn.ConvTranspose2d(in_c, out_c, kernel_size=scale, stride=scale,
                                                                      bias=False))

    def forward(self, x):
        return self.conv(x)


class Downscale(nn.Module):
    def __init__(self, in_c, out_c, scale, norm, act):
        super().__init__()
        self.conv = nn.Sequential(norm(in_c), act, nn.Conv2d(in_c, out_c, kernel_size=scale, stride=scale, bias=False))

    def forward(self, x):
        return self.conv(x)


class TFC_TDF(nn.Module):
    def __init__(self, in_c, c, l, f, bn, norm, act):
        super().__init__()
        self.blocks = nn.ModuleList()
        for _ in range(l):
            block = nn.Module()
            block.tfc1 = nn.Sequential(norm(in_c), act, nn.Conv2d(in_c, c, 3, 1, 1, bias=False))
            block.tdf = nn.Sequential(norm(c), act, nn.Linear(f, f // bn, bias=False), norm(c), act,
                                      nn.Linear(f // bn, f, bias=False))
            block.tfc2 = nn.Sequential(norm(c), act, nn.Conv2d(c, c, 3, 1, 1, bias=False))
            block.shortcut = nn.Conv2d(in_c, c, 1, 1, 0, bias=False)
            self.blocks.append(block)
            in_c = c

    def forward(self, x):
        for block in self.blocks:
            s = block.shortcut(x)
            x = block.tfc1(x)
            x = x + block.tdf(x)
            x = block.tfc2(x)
            x = x + s
        return x


class TFC_TDF_net_spec(nn.Module):
    """TFC_TDF_net.forward between self.stft(x) and self.stft.inverse(x)."""

    def __init__(self, cfg):
        super().__init__()
        m, a, t = cfg["model"], cfg["audio"], cfg["training"]
        norm = partial(_norm, norm_type=m.get("norm"))
        act = _act(m.get("act", "gelu"))
        self.num_target_instruments = 1 if t.get("target_instrument") else len(t["instruments"])
        self.num_subbands = k = m["num_subbands"]
        dim_c = k * a["num_channels"] * 2
        n, scale, l, c, g, bn = m["num_scales"], m["scale"], m["num_blocks_per_scale"], m["num_channels"], \
            m["growth"], m["bottleneck_factor"]
        f = a["dim_f"] // k
        self.first_conv = nn.Conv2d(dim_c, c, 1, 1, 0, bias=False)
        self.encoder_blocks = nn.ModuleList()
        for _ in range(n):
            block = nn.Module()
            block.tfc_tdf = TFC_TDF(c, c, l, f, bn, norm, act)
            block.downscale = Downscale(c, c + g, scale, norm, act)
            f = f // scale[1]
            c += g
            self.encoder_blocks.append(block)
        self.bottleneck_block = TFC_TDF(c, c, l, f, bn, norm, act)
        self.decoder_blocks = nn.ModuleList()
        for _ in range(n):
            block = nn.Module()
            block.upscale = Upscale(c, c - g, scale, norm, act)
            f = f * scale[1]
            c -= g
            block.tfc_tdf = TFC_TDF(2 * c, c, l, f, bn, norm, act)
            self.decoder_blocks.append(block)
        self.final_conv = nn.Sequential(nn.Conv2d(c + dim_c, c, 1, 1, 0, bias=False), act,
                                        nn.Conv2d(c, self.num_target_instruments * dim_c, 1, 1, 0, bias=False))

    def cac2cws(self, x):
        k = self.num_subbands
        b, c, f, t = x.shape
        return x.reshape(b, c, k, f // k, t).reshape(b, c * k, f // k, t)

    def cws2cac(self, x):
        k = self.num_subbands
        b, c, f, t = x.shape
        return x.reshape(b, c // k, k, f, t).reshape(b, c // k, f * k, t)

    def forward(self, x):
        mix = x = self.cac2cws(x)
        first_conv_out = x = self.first_conv(x)
        x = x.transpose(-1, -2)
        enc = []
        for block in self.encoder_blocks:
            x = block.tfc_tdf(x)
            enc.append(x)
            x = block.downscale(x)
        x = self.bottleneck_block(x)
        for block in self.decoder_blocks:
            x = block.upscale(x)
            x = torch.cat([x, enc.pop()], 1)
            x = block.tfc_tdf(x)
        x = x.transpose(-1, -2)
        x = x * first_conv_out
        x = self.final_conv(torch.cat([mix, x], 1))
        x = self.cws2cac(x)
        b, c, f, t = x.shape
        return x.reshape(b, self.num_target_instruments, -1, f, t)


def snr(ref, x):
    ref = np.asarray(ref, np.float64)
    x = np.asarray(x, np.float64)
    return 10 * np.log10(np.sum(ref ** 2) / max(np.sum((ref - x) ** 2), 1e-30))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    ckpt, cfg_path, out = sys.argv[1:]
    with open(cfg_path) as f:
        cfg = yaml.load(f, Loader=yaml.FullLoader)  # the config has a !!python/tuple
    net = TFC_TDF_net_spec(cfg)
    net.load_state_dict(torch.load(ckpt, map_location="cpu", weights_only=True))
    net.eval()
    a = cfg["audio"]
    dim_t = a["chunk_size"] // a["hop_length"] + 1
    x = torch.randn(1, 2 * a["num_channels"], a["dim_f"], dim_t) * 0.5
    torch.onnx.export(net, (x,), out, input_names=["spec"], output_names=["out"], opset_version=17,
                      dynamic_axes={"spec": {0: "batch"}, "out": {0: "batch"}}, dynamo=False,
                      do_constant_folding=True)
    print("wrote", out, "instruments:", " ".join(cfg["training"]["instruments"]))
    try:
        import onnxruntime as ort
    except ImportError:
        print("onnxruntime not installed: skipped the check")
        return
    sess = ort.InferenceSession(out, providers=["CPUExecutionProvider"])
    with torch.no_grad():
        for b in (1, 2):
            xi = torch.randn(b, *x.shape[1:]) * 0.3
            ref = net(xi).numpy()
            got = sess.run(None, {"spec": xi.numpy()})[0]
            print(f"check batch={b}: ONNX Runtime vs torch SNR {snr(ref, got):.1f} dB")


if __name__ == "__main__":
    main()
