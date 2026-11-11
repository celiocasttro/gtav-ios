#!/bin/sh
# Build the Linux release package (docs/RELEASE_CLEANROOM.md, "Linux"). Needs the SDK installed
# at rexglue-sdk/out/install/linux-amd64 (v0.10.0 + rexglue-sdk-gtav-linux.patch, Release),
# XenosRecomp built in build/xenosrecomp (+ xenosrecomp-gtav.patch), clang-20 / lld-20 / llvm-nm,
# and the official LLVM 19.1.5 Linux release unpacked (LLVM-19.1.5-Linux-X64.tar.xz).
# usage: build_release_linux.sh OUT_DIR LLVM_DIR LICENSES_DIR
#   LICENSES_DIR  LLVM.txt, DirectXShaderCompiler.txt, third_party/GPL-3.0.txt
set -eu
out=$1 llvm=$2 licenses=$3
repo=$(cd "$(dirname "$0")/../.." && pwd)
sdk=$repo/rexglue-sdk/out/install/linux-amd64
cd "$repo"
# gtav_fast without game code (GTAV_GAME_DLL): links a stub gtav_game.so, exports the hooks.
cmake -S gtav -B build-dll -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_LINKER=ld.lld-20 \
  -DGTAV_GAME_DLL=ON -Drexglue_DIR="$sdk/lib/cmake/rexglue" -DSDL3_DIR="$sdk/lib/cmake/SDL3"
cmake --build build-dll
cmake -S loader -B build-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-20
cmake --build build-launcher
python3 tools/release/make_release_linux.py "$out" --llvm "$llvm" \
  --expected tools/release/expected.txt --licenses "$licenses" --tar
echo "next: tools/release/scan_release.sh $out (must PASS with 0 failing)"
