#!/bin/bash
# Signs the AAX build with PACE's wraptool so retail Pro Tools will load it. Run on a Mac or PC with wraptool installed
# and your iLok connected. You need: a PACE (Eden) developer account, and a WCGUID for XL-Desk from PACE.
# Usage: sign-aax.sh <path to XL-Desk.aaxplugin> <PACE account> <WCGUID> [Developer ID Application identity (macOS)]
set -euo pipefail
IN="$1"; ACCOUNT="$2"; WCGUID="$3"; SIGNID="${4:-}"
ARGS=(sign --verbose --account "$ACCOUNT" --wcguid "$WCGUID" --in "$IN" --out "$IN")
[ -n "$SIGNID" ] && ARGS+=(--signid "$SIGNID")
wraptool "${ARGS[@]}"
wraptool verify --verbose --in "$IN"
