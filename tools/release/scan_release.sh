#!/bin/sh
# Taint scan of a Linux release folder against this machine's retail-derived files (dirty side).
# usage: scan_release.sh RELEASE_DIR [GENERATED_DIR [SOURCE ...]]
#   GENERATED_DIR  codegen output made with the release's manifest and partition seed
#                  (default ~/gtav-e2e-linux-cg/generated/default)
#   SOURCE         more retail-derived files or folders, e.g. a test install's data/spirv and
#                  data/shader_cache (the shaders the release made from the game)
# Controls: system libraries, the SDK sources and codegen templates, and XenosRecomp's sources
# (shader_common.h opens every HLSL file it writes) - no game in them.
set -e
REL=${1:?usage: scan_release.sh RELEASE_DIR [GENERATED_DIR]}
GEN=${2:-$HOME/gtav-e2e-linux-cg/generated/default}
shift; [ $# -gt 0 ] && shift
REPO=$(cd "$(dirname "$0")/../.." && pwd)
GAME="/run/media/nakas/TOSHIBA EXT/gtav-rex/game"
PY=${PYTHON:-$HOME/.venvs/reskate/bin/python}   # needs numpy
L=/usr/lib/x86_64-linux-gnu
exec "$PY" -I "$REPO/tools/release/taint_scan.py" "$REL" \
  "$GAME/basefile_tu27.bin" "$GAME/disc2_files/default.xexp" "$GAME/update/update.rpf" \
  "$REPO/build/gtav/spirv" "$REPO/derive/native/census2" "$GEN" "$REPO/gtav/generated/default" "$@" \
  --control $L/libc.so.6 $L/libm.so.6 $L/libstdc++.so.6 $L/libvulkan.so.1 $L/libz.so.1 \
  $L/libzstd.so.1 $L/libX11.so.6 $L/libcrypto.so.3 \
  "$REPO/rexglue-sdk/src" "$REPO/rexglue-sdk/include" "$REPO/rexglue-sdk/resources/templates" \
  "$REPO/thirdparty/XenosRecomp/XenosRecomp"
