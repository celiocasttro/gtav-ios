#!/bin/bash
# Install the built app and push args.txt (+ spirv with --spirv) to the connected phone.
#   tools/ios/deploy.sh [--spirv]
set -e
R="$(cd "$(dirname "$0")/../.." && pwd)"
D=${DEVICE:-00008110-00145D001E38801E}
cp() { xcrun devicectl device copy to --device $D --domain-type appDataContainer --domain-identifier com.nakas.gtav \
         --source "$1" --destination "$2" >/dev/null; }
"$R/tools/ios/make_app.sh" --install | grep -E "app:|installed"
cp "$R/tools/ios/args.txt" Documents/args.txt && echo "args pushed"
if [ "${1:-}" = "--spirv" ]; then cp "$R/build/ios/spirv" Documents/spirv && echo "spirv pushed"; fi
