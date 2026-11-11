#!/bin/bash
# Game SPIR-V for iOS from the shader cache (build/gtav/shader_cache/*.bin, the player's own
# shaders) with XenosRecomp + dxc on this Mac. As tools/android/make_spirv.sh, with the draw
# constants in dynamic uniform buffers (ubo_constants.py: the Android GPU win), without the
# Adreno-only rewrites (Apple GPUs have 64-bit integers). Also makes the sun-tile variant.
# Output: build/ios/spirv (copy to the app's Documents/spirv). Usage: tools/ios/make_spirv.sh [JOBS]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOLS=${TOOLS:-$ROOT/build/gtav/tools}
CACHE=${CACHE:-$ROOT/build/gtav/shader_cache}
OUT=$ROOT/build/ios/spirv
WORK=$ROOT/build/ios/spirv_work
mkdir -p "$OUT" "$WORK"
# ubo_constants.py maps scalar loads only: split the float2 device-address loads (as Android does).
sed -e 's/vk::RawBufferLoad<float2>(\(g_PushConstants\.SharedConstants + \)\([0-9]*\))/float2(vk::RawBufferLoad<float>(\1\2), vk::RawBufferLoad<float>(\1\2 + 4))/' \
    "$TOOLS/shader_common.h" > "$WORK/shader_common.h"
! grep -q 'RawBufferLoad<float2>' "$WORK/shader_common.h"
one() {
  bin=$1; name=$(basename "$bin" .bin); out=$OUT/$name.spv
  [ -s "$out" ] && [ "$out" -nt "$WORK/shader_common.h" ] && return 0
  hlsl=$WORK/$name.hlsl
  "$TOOLS/XenosRecomp" "$bin" "$hlsl" "$WORK/shader_common.h" >/dev/null 2>&1 || { echo "xr fail $name"; return 0; }
  # Shaders that render wrong with UBO constants on the iPhone keep device-address loads:
  # the sky dome came out white (ps_6A340C638FB8D4C2 + its vs; cause not found yet).
  case " $NO_UBO " in *" $name "*) ;; *) python3 -I "$UBO" "$hlsl" || { echo "ubo fail $name"; return 0; } ;; esac
  case $name in ps_*) prof=ps_6_0; extra=;; *) prof=vs_6_0; extra=-fvk-invert-y;; esac
  "$TOOLS/dxc" -T $prof -HV 2021 -all-resources-bound -spirv -fvk-use-dx-layout -Wno-ignored-attributes \
    -Qstrip_debug $extra -Fo "$out" "$hlsl" >/dev/null 2>&1 || echo "dxc fail $name"
  if [ "$prof" = vs_6_0 ] && grep -q '= vfetchIndirect(' "$hlsl"; then : > "$OUT/$name.indirect"; fi
  if [ "$name" = ps_D258EBE32001470C ]; then
    sed 's/r0.y = select(r0.y > 0.0, c255.x, c255.y);/r0.y = c255.x;/' "$hlsl" > "$WORK/${name}_tiles.hlsl"
    "$TOOLS/dxc" -T ps_6_0 -HV 2021 -all-resources-bound -spirv -fvk-use-dx-layout -Wno-ignored-attributes \
      -Qstrip_debug -Fo "$OUT/${name}_tiles.spv" "$WORK/${name}_tiles.hlsl" >/dev/null 2>&1 || echo "dxc fail tiles"
  fi
  rm -f "$hlsl"
}
UBO=$ROOT/tools/android/ubo_constants.py
NO_UBO="ps_6A340C638FB8D4C2 vs_5338696E6825DEA1"
export -f one; export TOOLS OUT WORK UBO NO_UBO
ls "$CACHE"/*.bin | xargs -P "${1:-4}" -I{} bash -c 'one {}'
echo "spirv: $(ls "$OUT"/*.spv | wc -l) files in $OUT"
