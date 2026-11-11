#!/bin/sh
# Complete corresponding source for a binary release (GPL-3.0 for this project and binutils'
# disassembler in the SDK; LGPL-2.1+ for the FFmpeg that is linked statically into the runtime).
# Packs, at the exact commits the binaries were built from: this repo, ReXGlue SDK + every
# submodule (FFmpeg included), XenosRecomp + submodules, plume. Our changes to the three
# upstreams are the *.patch files in this repo. Sources come from `git archive`, so only committed
# files go in: no build output, no game data.
#
# usage: make_source_archive.sh OUT.tar.xz TAG SDK_DIR XENOSRECOMP_DIR PLUME_DIR
#   TAG  the release tag of this repo (e.g. v0.1.0-windows)
set -eu
out=$1 tag=$2 sdk=$3 xr=$4 plume=$5
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
top=gtav-recomp-src
mkdir -p "$work/$top"

# Each part is unpacked into one staging tree and packed once at the end (GNU tar and macOS's
# bsdtar both work; no GNU-only options).
add() {  # add <git dir> <commit> <prefix>
  git -C "$1" archive --format=tar --prefix="$top/$3/" "$2" | tar -C "$work" -xf -
  echo "  $3 @ $(git -C "$1" rev-parse --short "$2^{commit}")"
}
add_tree() {  # add a repo and, recursively, its submodules at their checked-out commits
  add "$1" HEAD "$2"
  git -C "$1" submodule status --recursive | while read -r sha path rest; do
    sha=${sha#[-+ U]}
    add "$1/$path" "$sha" "$2/$path"
  done
}

echo "packing:"
add "$repo" "$tag" gtav-recomp
add_tree "$sdk" rexglue-sdk
add_tree "$xr" XenosRecomp
add_tree "$plume" plume

cat > "$work/$top/SOURCE.txt" <<EOF
Corresponding source for the gtav-recomp $tag binaries.

gtav-recomp/   this project at $tag (GPL-3.0-or-later)
rexglue-sdk/   ReXGlue SDK $(git -C "$sdk" describe --tags --always) ($(git -C "$sdk" rev-parse --short HEAD)) with every submodule
               at the commit the release was built from. Apply gtav-recomp/rexglue-sdk-gtav-linux.patch
               (all platforms) before building: git apply ../gtav-recomp/rexglue-sdk-gtav-linux.patch
XenosRecomp/   $(git -C "$xr" rev-parse --short HEAD) + submodules; apply gtav-recomp/xenosrecomp-gtav.patch
plume/         $(git -C "$plume" rev-parse --short HEAD); apply gtav-recomp/plume-gtav.patch

FFmpeg (rexglue-sdk/thirdparty/FFmpeg, LGPL-2.1-or-later; FFmpeg's GPL parts are not enabled
by the SDK's build) is linked statically into rexruntimerd.dll (Linux: librexruntime.so, macOS:
librexruntime.dylib). To use a
modified FFmpeg: change it in rexglue-sdk/thirdparty/FFmpeg, rebuild the SDK
(gtav-recomp/build_sdk.cmd on Windows; on Linux the SDK's linux-amd64 CMake preset, Release, then
cmake --install; on macOS the mac-arm64 preset with CMAKE_OSX_DEPLOYMENT_TARGET=15.0), and put the
new rexruntimerd.dll / librexruntime.so (and the other SDK libraries it was built with) in the
release folder (macOS: GTAV.app/Contents/Resources/gtav, then sign the app again ad hoc:
codesign --force --deep --sign - GTAV.app). The rest of the program needs no change: gtav_fast links
to the runtime library dynamically.

Building everything: gtav-recomp/docs/RELEASE_CLEANROOM.md (release layout and tools) and
gtav-recomp/build_sdk.cmd, build_xr.cmd, build_release_exe.cmd, build_launcher.cmd (Windows),
gtav-recomp/tools/release/build_release_linux.sh (Linux), gtav-recomp/tools/release/
build_release_macos.sh (macOS).
LLVM (clang, lld) and the DirectX Shader Compiler are bundled unmodified from their official
releases (LLVM 19.1.5 on Windows and Linux, 19.1.7 on macOS: github.com/llvm/llvm-project; dxc:
XenosRecomp/thirdparty/dxc-bin).

No game data is in this archive or in the release.
EOF
tar -C "$work" -cf - "$top" | xz -T0 -6 -c > "$out"
echo "$(du -h "$out" | cut -f1) -> $out"
