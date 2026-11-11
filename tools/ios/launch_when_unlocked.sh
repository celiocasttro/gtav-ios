#!/bin/bash
# Waits for the phone to unlock, launches the app with its console, logs to $1 (default build/ios/run.log).
D=${DEVICE:-00008110-00145D001E38801E}; L=${1:-build/ios/run.log}
until xcrun devicectl device info lockState --device $D 2>/dev/null | grep -q "passcodeRequired: false"; do sleep 3; done
exec xcrun devicectl device process launch --device $D --console --terminate-existing com.nakas.gtav > "$L" 2>&1
