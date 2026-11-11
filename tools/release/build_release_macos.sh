#!/bin/sh
# macOS release build: everything shipped in GTAV.app, built for macOS 15 on Apple silicon with
# no build paths in the binaries (-ffile-prefix-map), then packaged by make_release_macos.py.
# usage: build_release_macos.sh OUT_DIR LLVM_DIR LICENSES_DIR [--zip]
#   IOS_SHELL=1   also the iPhone shell (GTAV.app/Contents/Resources/gtav/ios: the SDK runtime and
#                 gtav for iOS 17 / A14+ without game code, MoltenVK; MOLTENVK_FW as in make_shell.sh).
#                 build/release-mac/ios-shell-app is then also the --ios folder for the Windows and
#                 Linux release scripts.
#   LLVM_DIR      unpacked LLVM-19.1.7-macOS-ARM64 (official release)
#   LICENSES_DIR  LLVM.txt and DirectXShaderCompiler.txt
# Needs Xcode (clang, the macOS SDK), CMake, Ninja, Python 3. The SDK tree must be v0.10.0 +
# rexglue-sdk-gtav-linux.patch, XenosRecomp + xenosrecomp-gtav.patch, plume + plume-gtav.patch.
set -eu
OUT=$1 LLVM=$2 LIC=$3; shift 3
R=$(cd "$(dirname "$0")/../.." && pwd)
J=${JOBS:-3}
F="-mcpu=apple-m1 -ffile-prefix-map=$R=."
MAC="-DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0"
SDK=$R/rexglue-sdk/out/install/mac-rel
# Always fresh trees: a tree first configured against another SDK keeps that SDK's libraries in its
# cache (find_library) and once shipped a gtav_fast that rendered the lit world flat grey.
B=$R/build/release-mac
rm -rf "$B"
mkdir -p "$B"

(cd "$R/rexglue-sdk" &&
  cmake --preset mac-arm64 -B out/build/mac-rel -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_INSTALL_PREFIX="$SDK" "-DCMAKE_C_FLAGS=$F" "-DCMAKE_CXX_FLAGS=$F" \
    "-DCMAKE_OBJC_FLAGS=$F" "-DCMAKE_OBJCXX_FLAGS=$F" -DREXGLUE_BUILD_TESTS=OFF \
    -DSYSCONFDIR=/etc &&  # the Vulkan loader would embed the install prefix; VK_DRIVER_FILES is set at run time
  cmake --build out/build/mac-rel --config Release -j "$J" &&
  cmake --install out/build/mac-rel --config Release)
# XenosRecomp: static fmt/zstd; only libdxcompiler.dylib ships next to it.
cmake -S "$R/thirdparty/XenosRecomp" -B "$B/xenosrecomp" -G Ninja $MAC \
  "-DCMAKE_C_FLAGS=$F" "-DCMAKE_CXX_FLAGS=$F" -DBUILD_SHARED_LIBS=OFF
cmake --build "$B/xenosrecomp" --target XenosRecomp -j "$J"
cmake -S "$R/gtav" -B "$B/gtav-fast" -G Ninja $MAC "-DCMAKE_CXX_FLAGS=$F" \
  -DCMAKE_PREFIX_PATH="$SDK" -Drexglue_DIR="$SDK/lib/cmake/rexglue" -DSDL3_DIR="$SDK/lib/cmake/SDL3" \
  -DGTAV_GAME_DLL=ON -DGTAV_XR_BIN="$B/xenosrecomp/XenosRecomp/XenosRecomp"
cmake --build "$B/gtav-fast" -j "$J"
cmake -S "$R/loader" -B "$B/launcher" -G Ninja $MAC "-DCMAKE_CXX_FLAGS=$F" "-DCMAKE_OBJCXX_FLAGS=$F"
cmake --build "$B/launcher" -j "$J"
IOS_ARGS=
if [ "${IOS_SHELL:-0}" = 1 ]; then
  IOS="-DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64
       -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
  FI="-mcpu=apple-a14 -ffile-prefix-map=$R=."
  ISDK=$R/rexglue-sdk/out/install/ios-rel
  (cd "$R/rexglue-sdk" &&
    cmake -S . -B out/build/ios-rel -G Ninja $IOS "-DCMAKE_C_FLAGS=$FI" "-DCMAKE_CXX_FLAGS=$FI" \
      -DCMAKE_CXX_STANDARD=23 -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$ISDK" &&
    cmake --build out/build/ios-rel -j "$J" &&
    cmake --install out/build/ios-rel)
  # The install registers itself as a CMake package: a Mac configure could pick up the iOS SDK.
  grep -l "ios-rel" "$HOME"/.cmake/packages/rexglue/* 2>/dev/null | xargs rm -f
  cmake -S "$R/gtav" -B "$B/ios-shell" -G Ninja $IOS "-DCMAKE_CXX_FLAGS=$FI" -DCMAKE_PREFIX_PATH="$ISDK" \
    -DCMAKE_FIND_ROOT_PATH="$ISDK" -DGTAV_GAME_DLL=ON
  cmake --build "$B/ios-shell" -j "$J"
  GTAV_SHELL="$B/ios-shell/gtav_fast.app/gtav_fast" GTAV_IOS_SDK="$ISDK" "$R/tools/ios/make_shell.sh" "$B/ios-shell-app"
  IOS_ARGS="--ios-shell $B/ios-shell-app"
fi
python3 "$R/tools/release/make_release_macos.py" "$OUT" --llvm "$LLVM" --licenses "$LIC" \
  --expected "$R/tools/release/expected.txt" --sdk "$SDK" $IOS_ARGS "$@"
