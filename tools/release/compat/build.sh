#!/bin/sh
# Portable Linux release build: everything shipped (SDK runtime + codegen tool, XenosRecomp,
# gtav_fast, the launcher) built in the Ubuntu 22.04 container (Dockerfile here), into separate
# trees (*-compat) next to the native ones. Then package with
#   tools/release/make_release_linux.py OUT --compat ...
# usage: tools/release/compat/build.sh            (builds the image first if missing)
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
image=gtav-compat-build:22.04
docker image inspect $image >/dev/null 2>&1 || docker build -t $image "$repo/tools/release/compat"
jobs=${JOBS:-8}
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e JOBS=$jobs -v "$repo:$repo" -w "$repo" $image sh -euc '
  sdk=rexglue-sdk
  # __FILE__ strings (logs, asserts) become repo-relative: no build machine paths in the release.
  map="-ffile-prefix-map=$PWD/="
  cmake -S $sdk -B $sdk/out/build/compat -G "Ninja Multi-Config" \
    -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
    "-DCMAKE_C_FLAGS=-march=x86-64-v2 $map" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2 $map" -DCMAKE_CXX_STANDARD=23 \
    "-DCMAKE_CONFIGURATION_TYPES=Debug;Release;RelWithDebInfo" \
    -DCMAKE_INSTALL_PREFIX=$PWD/$sdk/out/install/compat -DSDL_X11_XSCRNSAVER=OFF \
    -DREXGLUE_BUILD_TESTS=OFF
  cmake --build $sdk/out/build/compat --config Release -j $JOBS
  cmake --install $sdk/out/build/compat --config Release
  cmake -S thirdparty/XenosRecomp -B build/xenosrecomp-compat -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 "-DCMAKE_C_FLAGS=$map" "-DCMAKE_CXX_FLAGS=$map" \
    -DBUILD_SHARED_LIBS=OFF  # fmt and zstd static: the package ships only libdxcompiler.so with it
  cmake --build build/xenosrecomp-compat --target XenosRecomp -j $JOBS
  sdkp=$PWD/$sdk/out/install/compat
  cmake -S gtav -B build-dll-compat -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_LINKER=ld.lld-20 \
    "-DCMAKE_C_FLAGS=$map" "-DCMAKE_CXX_FLAGS=$map" \
    -DGTAV_GAME_DLL=ON -Drexglue_DIR=$sdkp/lib/cmake/rexglue -DSDL3_DIR=$sdkp/lib/cmake/SDL3 \
    -DGTAV_XR_BIN=$PWD/build/xenosrecomp-compat/XenosRecomp/XenosRecomp
  cmake --build build-dll-compat -j $JOBS
  cmake -S loader -B build-launcher-compat -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-20 \
    "-DCMAKE_CXX_FLAGS=$map"
  cmake --build build-launcher-compat -j $JOBS
'
echo "built: rexglue-sdk/out/install/compat build/xenosrecomp-compat build-dll-compat build-launcher-compat"
