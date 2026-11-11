#!/bin/bash
# Install a release GTAV.ipa (the player's, from "Build for iPhone") on the dev phone, signed the way a
# sideloading tool would: the dev bundle id and profile (com.nakas.gtav, docs/IOS_PORT.md), every
# Mach-O re-signed. Reinstalling keeps the app's Documents (game data, logs).
#   tools/ios/install_ipa_dev.sh GTAV.ipa [--free]
#   --free  without increased-memory-limit and extended-virtual-addressing, which a free Apple ID's
#           profile cannot carry: the memory limit and address space a free-account player gets.
set -euo pipefail
R="$(cd "$(dirname "$0")/../.." && pwd)"
IPA=$1
FREE=${2:-}
BUNDLE_ID="${BUNDLE_ID:-com.nakas.gtav}"
TEAM_ID="${TEAM_ID:-FBCD7748P3}"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
(cd "$W" && unzip -q "$IPA")
APP="$W/Payload/GTAV.app"
plutil -replace CFBundleIdentifier -string "$BUNDLE_ID" "$APP/Info.plist"
PROFILE=""
for p in "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles/"*.mobileprovision; do
  appid=$(security cms -D -i "$p" 2>/dev/null | plutil -extract Entitlements.application-identifier raw - 2>/dev/null) || continue
  [ "$appid" = "$TEAM_ID.$BUNDLE_ID" ] && PROFILE="$p"
done
[ -n "$PROFILE" ] || { echo "no provisioning profile for $TEAM_ID.$BUNDLE_ID"; exit 1; }
cp "$PROFILE" "$APP/embedded.mobileprovision"
security cms -D -i "$PROFILE" > "$W/profile.plist" 2>/dev/null
plutil -extract Entitlements xml1 -o "$W/ent.plist" "$W/profile.plist"
if [ "$FREE" = "--free" ]; then
  # (PlistBuddy: plutil would read the dots in these keys as a path)
  /usr/libexec/PlistBuddy -c "Delete :com.apple.developer.kernel.increased-memory-limit" "$W/ent.plist" 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Delete :com.apple.developer.kernel.extended-virtual-addressing" "$W/ent.plist" 2>/dev/null || true
  echo "signing without the memory entitlements (free Apple ID)"
fi
IDENTITY=$(security find-identity -v -p codesigning | awk '/Apple Development/ {print $2; exit}')
for d in "$APP"/*.dylib; do codesign --force --sign "$IDENTITY" --timestamp=none "$d"; done
codesign --force --sign "$IDENTITY" --entitlements "$W/ent.plist" --timestamp=none --generate-entitlement-der "$APP"
codesign -d --entitlements - "$APP" 2>/dev/null | grep -E "kernel\.(increased|extended)" || echo "(no kernel memory entitlements)"
DEVICE=${DEVICE:-00008110-00145D001E38801E}
xcrun devicectl device install app --device "$DEVICE" "$APP" | grep -E "installed|bundleID" || true
