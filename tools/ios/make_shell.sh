#!/bin/bash
# The iPhone part of every desktop release (the package's ios/ folder): GTAV.app without any game
# code, unsigned, plus what the iPhone step (loader/src/install.cpp RunIphone, on Windows, Linux or
# macOS) needs to build the player's gtav_game.dylib for it with the bundled clang + lld:
#   OUT_DIR/GTAV.app             the shell (gtav, SDK runtime, MoltenVK, Info.plist)
#   OUT_DIR/interface/           gtav_hooks.h, gtav_game.exports, gtav_game.undefs (names only)
#   OUT_DIR/libSystem_ios.tbd    our list of five C library names (no Apple SDK needed to link)
#   OUT_DIR/include/simde/       simde (MIT): SSE intrinsics on NEON for the generated code
# The player's GTAV.ipa is signed by Sideloadly / AltStore with their own Apple ID.
#   tools/ios/make_shell.sh OUT_DIR
# Inputs: build/ios-shell (cmake ... CMAKE_SYSTEM_NAME=iOS -DGTAV_GAME_DLL=ON, see docs/IOS_PORT.md),
# the iOS SDK install (rexglue-sdk/out/install/ios-arm64) and a MoltenVK ios-arm64 framework.
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=$1
BUNDLE_ID="${BUNDLE_ID:-org.gtavrecomp.gtav}"
SHELL_EXE="${GTAV_SHELL:-$R/build/ios-shell/gtav_fast.app/gtav_fast}"
SDK="${GTAV_IOS_SDK:-$R/rexglue-sdk/out/install/ios-arm64}"
MOLTENVK_FW="${MOLTENVK_FW:-/Volumes/Untitled/MacOffload/skate3/moltenvk-ios/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework}"
APP="$OUT/GTAV.app"
IFACE="${GTAV_IOS_INTERFACE:-$(dirname "$(dirname "$SHELL_EXE")")/game_interface}"

# The renderer's own shaders must be compiled by this build (dxc): prebuilt headers from an older
# tree lack the vertex-buffer overlay shader, and the touch controls and menu drew nothing.
CACHE="$(dirname "$(dirname "$SHELL_EXE")")/CMakeCache.txt"
if [ -f "$CACHE" ] && grep -qE '^GTAV_PREBUILT_NATIVE_SHADERS:PATH=.+' "$CACHE"; then
  echo "the shell was built with GTAV_PREBUILT_NATIVE_SHADERS: reconfigure with -DGTAV_PREBUILT_NATIVE_SHADERS="; exit 1
fi
rm -rf "$APP" "$OUT/interface" "$OUT/include" "$OUT/LICENSES"; mkdir -p "$APP" "$OUT/interface" "$OUT/include" "$OUT/LICENSES"
cp "$SHELL_EXE" "$APP/gtav"
cp "$SDK/lib/librexruntime.dylib" "$SDK/lib/librexgpu-xenos.dylib" "$APP/"
cp "$MOLTENVK_FW/MoltenVK" "$APP/libMoltenVK.dylib"
chmod u+w "$APP"/*.dylib
install_name_tool -id @rpath/libMoltenVK.dylib "$APP/libMoltenVK.dylib" 2>/dev/null
install_name_tool -add_rpath @executable_path "$APP/gtav" 2>/dev/null || true
strip -S -x "$APP/gtav" "$APP/librexruntime.dylib" "$APP/librexgpu-xenos.dylib"
# No code signature (a sideloading tool signs everything) and no build paths.
for f in "$APP/gtav" "$APP"/*.dylib; do codesign --remove-signature "$f" 2>/dev/null || true; done
if strings "$APP/gtav" "$APP"/lib*.dylib | grep -q "$R"; then echo "build path in a binary: $R"; exit 1; fi
otool -L "$APP/gtav" | grep -q "gtav_game.dylib" || { echo "gtav is not the shell build (no gtav_game.dylib)"; exit 1; }
sed -e "s/@BUNDLE_ID@/$BUNDLE_ID/" -e "s/@BUILD@/$(date +%Y%m%d%H%M)/" "$R/tools/ios/Info.plist" > "$APP/Info.plist"
plutil -convert binary1 "$APP/Info.plist"
printf 'APPL????' > "$APP/PkgInfo"
cp "$IFACE/gtav_hooks.h" "$IFACE/gtav_game.exports" "$IFACE/gtav_game.undefs" "$OUT/interface/"
cp "$R/tools/game_dll/libSystem_ios.tbd" "$OUT/"
rsync -a --exclude='*.md' --exclude='*.txt' --exclude='.*' "$R/rexglue-sdk/thirdparty/simde/simde/" "$OUT/include/simde/"
# Licenses of what ships in here beyond the desktop package's own: MoltenVK (Apache-2.0), simde (MIT).
cp "$R/rexglue-sdk/thirdparty/moltenvk/LICENSE" "$OUT/LICENSES/MoltenVK.txt"
cp "$R/rexglue-sdk/thirdparty/simde/COPYING" "$OUT/LICENSES/simde.txt"
echo "ios: $OUT ($(du -sh "$OUT" | cut -f1); shell $(du -sh "$APP" | cut -f1))"
