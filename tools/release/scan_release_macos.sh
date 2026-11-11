#!/bin/sh
# Taint scan of a macOS release folder against this Mac's retail-derived files (dirty side).
# usage: scan_release_macos.sh RELEASE_DIR [GENERATED_DIR [SOURCE ...]]
#   GENERATED_DIR  codegen output made with the release's manifest and partition seed
#                  (default ~/gtav-ssd/rel/cg/generated/default)
#   SOURCE         more retail-derived files or folders, e.g. a test install's data/spirv and
#                  data/shader_cache (the shaders the release made from the game)
# The decrypted executable image comes from scripts/extract_basefile.py (made on first use, kept
# next to the game data, never in the repo). Controls: the SDK sources and codegen templates,
# XenosRecomp's sources, simde and clang's resource headers (both ship and are public); macOS
# keeps its system libraries in the dyld shared cache, so there are no system-library controls.
set -e
REL=${1:?usage: scan_release_macos.sh RELEASE_DIR [GENERATED_DIR]}
GEN=${2:-$HOME/gtav-ssd/rel/cg/generated/default}
shift; [ $# -gt 0 ] && shift
REPO=$(cd "$(dirname "$0")/../.." && pwd)
GAME=${GTAV_GAME:-$HOME/gtav-ssd/game}
BASE=${GTAV_BASEFILE:-$HOME/gtav-ssd/rel/basefile.bin}
PY=${PYTHON:-python3}   # needs numpy (and cryptography for the basefile)
[ -f "$BASE" ] || "$PY" "$REPO/scripts/extract_basefile.py" "$GAME/disc2_files/default.xex" "$BASE"
LLVM_INC=$(find "$REL" -path '*tools/llvm/lib/clang/*/include' -type d | head -1)
SPV=""
[ -d "$REPO/build/gtav/spirv" ] && SPV="$REPO/build/gtav/spirv"
exec "$PY" -I "$REPO/tools/release/taint_scan.py" "$REL" \
  "$BASE" "$GAME/disc2_files/default.xexp" "$GAME/update/update.rpf" \
  $SPV "$GEN" "$@" \
  --control "$REPO/rexglue-sdk/src" "$REPO/rexglue-sdk/include" "$REPO/rexglue-sdk/resources/templates" \
  "$REPO/thirdparty/XenosRecomp/XenosRecomp" "$REPO/rexglue-sdk/thirdparty/simde/simde" "$LLVM_INC"
