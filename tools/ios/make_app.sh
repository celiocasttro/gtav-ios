#!/bin/bash
# Assemble, sign and (optionally) install the iOS dev build of the recompiled GTA V.
#   tools/ios/make_app.sh [--install]
# Inputs: build/ios/gtav (cmake -B build/ios ... CMAKE_SYSTEM_NAME=iOS), the iOS SDK install
# (rexglue-sdk/out/install/ios-arm64), a MoltenVK ios-arm64 dynamic framework (MOLTENVK_FW) and
# a development provisioning profile for $BUNDLE_ID (minted by Xcode, see docs/IOS_PORT.md).
# DEV BUILD ONLY: the executable contains the recompiled game code; never publish the .app.
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
BUNDLE_ID="${BUNDLE_ID:-com.nakas.gtav}"
TEAM_ID="${TEAM_ID:-FBCD7748P3}"
BUILD="${GTAV_IOS_BUILD:-$R/build/ios}"
SDK="${GTAV_IOS_SDK:-$R/rexglue-sdk/out/install/ios-arm64}"
MOLTENVK_FW="${MOLTENVK_FW:-/Volumes/Untitled/MacOffload/skate3/moltenvk-ios/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework}"
APP="$BUILD/package/GTAV.app"

rm -rf "$APP"; mkdir -p "$APP"  # not build/ios/gtav.app: same folder on a case-insensitive disk
# GTAV_GAME_DYLIB=<path>: the release layout, the shell exe (build/ios-shell, no game code) plus the
# game library built from the player's discs (tools/game_dll/build_game_dll.py --ios).
if [ -n "${GTAV_GAME_DYLIB:-}" ]; then
  cp "${GTAV_SHELL:-$R/build/ios-shell/gtav_fast.app/gtav_fast}" "$APP/gtav"
  cp "$GTAV_GAME_DYLIB" "$APP/gtav_game.dylib"
else
  cp "$BUILD/gtav.app/gtav" "$APP/gtav"
fi
cp "$SDK/lib/librexruntime.dylib" "$SDK/lib/librexgpu-xenos.dylib" "$APP/"
cp "$MOLTENVK_FW/MoltenVK" "$APP/libMoltenVK.dylib"
chmod u+w "$APP"/*.dylib
install_name_tool -id @rpath/libMoltenVK.dylib "$APP/libMoltenVK.dylib" 2>/dev/null
# Everything sits in the bundle root next to the executable.
install_name_tool -add_rpath @executable_path "$APP/gtav" 2>/dev/null || true
sed -e "s/@BUNDLE_ID@/$BUNDLE_ID/" -e "s/@BUILD@/$(date +%Y%m%d%H%M)/" "$R/tools/ios/Info.plist" > "$APP/Info.plist"
plutil -convert binary1 "$APP/Info.plist"
printf 'APPL????' > "$APP/PkgInfo"

PROFILE=""
for p in "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles/"*.mobileprovision; do
  appid=$(security cms -D -i "$p" 2>/dev/null | plutil -extract Entitlements.application-identifier raw - 2>/dev/null) || continue
  [ "$appid" = "$TEAM_ID.$BUNDLE_ID" ] && PROFILE="$p"
done
[ -n "$PROFILE" ] || { echo "no provisioning profile for $TEAM_ID.$BUNDLE_ID"; exit 1; }
cp "$PROFILE" "$APP/embedded.mobileprovision"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
security cms -D -i "$PROFILE" > "$W/profile.plist" 2>/dev/null
plutil -extract Entitlements xml1 -o "$W/ent.plist" "$W/profile.plist"
IDENTITY=$(security find-identity -v -p codesigning | awk '/Apple Development/ {print $2; exit}')
for d in "$APP"/*.dylib; do codesign --force --sign "$IDENTITY" --timestamp=none "$d"; done
codesign --force --sign "$IDENTITY" --entitlements "$W/ent.plist" --timestamp=none --generate-entitlement-der "$APP"
codesign --verify --verbose "$APP"
echo "app: $APP ($(du -sh "$APP" | cut -f1))"

if [ "${1:-}" = "--install" ]; then
  DEVICE=$(xcrun devicectl list devices 2>/dev/null | grep -v unavailable | grep -E 'available|connected' \
    | grep -oE '[0-9A-F]{8}-[0-9A-F]{16}|[0-9A-Fa-f]{8}-([0-9A-Fa-f]{4}-){3}[0-9A-Fa-f]{12}' | head -1)
  [ -n "$DEVICE" ] || { echo "no connected device"; exit 1; }
  xcrun devicectl device install app --device "$DEVICE" "$APP"
fi
