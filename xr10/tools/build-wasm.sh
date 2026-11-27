#!/bin/sh
# Builds xr10/xr10.wasm from the C++ engine (the same source as the VST3) with plain clang + wasm-ld, no Emscripten.
set -e
cd "$(dirname "$0")/.."
${CLANG:-clang} --target=wasm32 -O2 -std=c++17 -nostdlib -fno-exceptions -fno-rtti -fvisibility=hidden -ffreestanding \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--export=__wasm_call_ctors -Wl,--export=__heap_base -Wl,--initial-memory=67108864 -Wl,--max-memory=67108864 \
  -Wl,-z,stack-size=1048576 -Wl,--strip-all -o xr10.wasm core/wasm.cpp
ls -l xr10.wasm
