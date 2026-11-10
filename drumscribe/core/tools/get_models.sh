#!/usr/bin/env bash
# Download and convert the separation models for the C++ engine (one-off, offline; nothing Python runs later).
#
#   drumscribe/core/tools/get_models.sh [model_dir]        default: $DRUMSCRIBE_MODELS or ~/.cache/drumscribe/models
#
# Writes into model_dir:
#   ggml-model-htdemucs-4s-f16.bin            Demucs v4 htdemucs        (quality "fast": drums + bass)      84 MB
#   ggml-model-htdemucs_ft_drums-4s-f16.bin   htdemucs_ft drums model   (quality "best": drums)             84 MB
#   ggml-model-htdemucs_ft_bass-4s-f16.bin    htdemucs_ft bass model    (quality "best": bass)              84 MB
#   drumsep_mdx23c.onnx                       MDX23C DrumSep net (kick snare toms hh ride crash)          438 MB
# and keeps the downloaded originals in model_dir/src (delete it to save ~690 MB; re-running then re-downloads).
#
# Needs curl and a Python 3 with torch, pyyaml and onnx (optional: onnxruntime for the export check), e.g.
#   python3 -m venv /tmp/dsvenv && /tmp/dsvenv/bin/pip install torch pyyaml onnx onnxruntime
#   PYTHON=/tmp/dsvenv/bin/python drumscribe/core/tools/get_models.sh
# Set PYTHON to choose the interpreter (default: python3).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIR="${1:-${DRUMSCRIBE_MODELS:-$HOME/.cache/drumscribe/models}}"
PY="${PYTHON:-python3}"
SRC="$DIR/src"
mkdir -p "$SRC"

"$PY" -c "import torch, yaml, onnx" 2>/dev/null || {
  echo "error: $PY needs torch, pyyaml and onnx (pip install torch pyyaml onnx onnxruntime); set PYTHON=..." >&2
  exit 1
}

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

# fetch <url> <file> <sha256 or sha256-prefix>
fetch() {
  local url="$1" out="$2" want="$3"
  if [ -f "$out" ] && [[ "$(sha256 "$out")" == "$want"* ]]; then return; fi
  echo "downloading $(basename "$out")"
  curl -fL --retry 3 -o "$out.part" "$url"
  local got; got="$(sha256 "$out.part")"
  if [[ "$got" != "$want"* ]]; then
    echo "error: checksum mismatch for $url ($got)" >&2
    rm -f "$out.part"
    exit 1
  fi
  mv "$out.part" "$out"
}

# --- Demucs v4 (Meta, MIT): the official checkpoints (their file name ends with the start of their sha256)
DEMUCS=https://dl.fbaipublicfiles.com/demucs/hybrid_transformer
for spec in "955717e8-8726e21a:htdemucs" "f7e0c4bc-ba3fe64a:htdemucs_ft_drums" "d12395a8-e57c48e6:htdemucs_ft_bass"; do
  sig="${spec%%:*}"; name="${spec#*:}"
  fetch "$DEMUCS/$sig.th" "$SRC/$sig.th" "${sig#*-}"
  out="$DIR/ggml-model-$name-4s-f16.bin"
  if [ ! -s "$out" ] || [ "$SRC/$sig.th" -nt "$out" ]; then
    "$PY" -I "$HERE/convert_demucs_ggml.py" "$SRC/$sig.th" "$out.part" && mv "$out.part" "$out"
  fi
done

# --- MDX23C DrumSep (aufr33 & jarredou), as distributed by python-audio-separator
AS=https://github.com/nomadkaraoke/python-audio-separator/releases/download/model-configs
fetch "$AS/MDX23C-DrumSep-aufr33-jarredou.ckpt" "$SRC/MDX23C-DrumSep-aufr33-jarredou.ckpt" \
  d2a4aa53eb584d21eead358a4e66d1882ad182911be018f052b5da73be9096d0
fetch "$AS/config_drumsep_mdx23c.yaml" "$SRC/config_drumsep_mdx23c.yaml" \
  17d1649a227f841165bdb4c11a42082898192a1ea3ceab7e7e0b9293d6589dd6
out="$DIR/drumsep_mdx23c.onnx"
if [ ! -s "$out" ] || [ "$SRC/MDX23C-DrumSep-aufr33-jarredou.ckpt" -nt "$out" ]; then
  "$PY" -I "$HERE/export_drumsep_onnx.py" "$SRC/MDX23C-DrumSep-aufr33-jarredou.ckpt" \
    "$SRC/config_drumsep_mdx23c.yaml" "$out.part.onnx"
  mv "$out.part.onnx" "$out"
fi

echo "models ready in $DIR"
ls -la "$DIR"
