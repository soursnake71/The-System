#!/usr/bin/env bash
# The phone's model pack (Drum Scribe on Android, "Analyse on this phone"): fast Demucs + DrumSep with 16-bit weights,
# and models.json (name, size, sha256) that the app checks downloads against.
#   PYTHON=<python with torch, pyyaml, onnx> tools/make_mobile_models.sh <out_dir> [model_dir]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:?out dir}"
MODELS="${2:-${DRUMSCRIBE_MODELS:-$HOME/.cache/drumscribe/models}}"
PY="${PYTHON:-python3}"
"$HERE/get_models.sh" "$MODELS"
mkdir -p "$OUT"
cp "$MODELS/ggml-model-htdemucs-4s-f16.bin" "$OUT/"
"$PY" -I "$HERE/export_drumsep_mobile.py" "$MODELS/drumsep_mdx23c.onnx" "$OUT/drumsep_mdx23c.onnx"
cd "$OUT"
{
  echo '{"version": 1, "files": ['
  first=1
  for f in ggml-model-htdemucs-4s-f16.bin drumsep_mdx23c.onnx; do
    [ $first = 1 ] || echo ','
    first=0
    printf '  {"name": "%s", "size": %s, "sha256": "%s"}' "$f" "$(wc -c < "$f" | tr -d ' ')" "$(sha256sum "$f" | cut -d' ' -f1)"
  done
  echo ''
  echo ']}'
} > models.json
cat models.json
