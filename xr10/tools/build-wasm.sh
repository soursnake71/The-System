#!/bin/sh
# Builds xr10/xr10.wasm from the C++ engine (the same source as the VST3) and the fold-out's effects rack
# (plugins/dsp), for wasm32-wasi with the C++ library. On Ubuntu: apt install clang lld wasi-libc libc++-18-dev-wasm32
# libc++abi-18-dev-wasm32 libclang-rt-18-dev-wasm32. SYSROOT may point at another wasi sysroot (e.g. wasi-sdk's).
set -e
cd "$(dirname "$0")/.."
${CLANG:-clang++} --target=wasm32-wasi --sysroot=${SYSROOT:-/usr} -O2 -std=c++17 -fno-exceptions -fno-rtti -fvisibility=hidden \
  -mexec-model=reactor -Wl,--export-dynamic -Wl,--initial-memory=83886080 -Wl,--max-memory=268435456 \
  -Wl,-z,stack-size=1048576 -Wl,--strip-all -o xr10.wasm core/wasm.cpp
ls -l xr10.wasm
