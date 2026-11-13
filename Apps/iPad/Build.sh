#!/usr/bin/env bash
# Apps/iPad/Build.sh - Generate the Xcode project and build the app from the command line, on a Mac.
#
#   bash Apps/iPad/Build.sh                  build, and install on the iPad or iPhone that is connected; with none
#                                            connected, just build; with several, list them and stop
#   bash Apps/iPad/Build.sh "Sem's iPad"     build for that device (by name or identifier) and install on it
#   bash Apps/iPad/Build.sh --simulator      compile for the simulator, with no signing: a quick check
#
# The destination is always named: with none, xcodebuild may pick this Mac, which the team's provisioning profile
# doesn't include. -allowProvisioningUpdates lets Xcode make or refresh that profile and register a new device,
# as its window does when you press Run.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG="${MARCONI_IOS_CONFIG:-Release}"
PROJECT="$ROOT/Build/Xcode/Marconi/Marconi.xcodeproj"
DERIVED="$ROOT/Build/Xcode/Marconi/DerivedData"

if [[ "${1:-}" == "--simulator" ]]; then
    bash "$ROOT/Apps/iPad/GenerateProject.sh"
    xcodebuild -project "$PROJECT" -scheme Marconi -configuration "$CONFIG" -sdk iphonesimulator \
        -destination 'generic/platform=iOS Simulator' -derivedDataPath "$DERIVED" CODE_SIGNING_ALLOWED=NO build
    exit 0
fi

# The connected iOS devices, one "identifier<TAB>name" per line, from xctrace's list: the lines of its Devices
# section whose identifier has an iPhone's or iPad's form (this Mac's is a UUID, and is left out).
connected_devices() {
    xcrun xctrace list devices 2>/dev/null | parse_devices
}
parse_devices() {
    awk '
        /^== Devices ==/ { on = 1; next }
        /^== / { on = 0 }
        on && match($0, /\(([0-9A-Fa-f]{8}-[0-9A-Fa-f]{16}|[0-9A-Fa-f]{40})\)[[:space:]]*$/) {
            id = substr($0, RSTART + 1, RLENGTH - 2); sub(/\).*/, "", id)
            name = substr($0, 1, RSTART - 1)
            sub(/[[:space:]]*\([0-9][0-9.]*\)[[:space:]]*$/, "", name)   # the OS version
            sub(/[[:space:]]+$/, "", name)
            print id "\t" name
        }'
}
if [[ "${1:-}" == "--parse-devices" ]]; then   # for testing the parsing: reads a device list on standard input
    parse_devices
    exit 0
fi

DEVICE="${1:-}"
DEVICE_NAME="$DEVICE"
if [[ -z "$DEVICE" ]]; then
    FOUND="$(connected_devices || true)"
    COUNT="$(grep -c . <<< "$FOUND" || true)"
    if [[ "$COUNT" -eq 1 ]]; then
        DEVICE="$(cut -f 1 <<< "$FOUND")"
        DEVICE_NAME="$(cut -f 2 <<< "$FOUND")"
        echo "[Build.sh] Installing on $DEVICE_NAME, the one device connected."
    elif [[ "$COUNT" -gt 1 ]]; then
        echo "Several devices are connected. Say which, by name:" >&2
        cut -f 2 <<< "$FOUND" | sed 's/^/    bash Apps\/iPad\/Build.sh "/; s/$/"/' >&2
        exit 1
    else
        echo "[Build.sh] No iPad or iPhone connected: building without installing."
    fi
fi
if [[ -z "$DEVICE" ]]; then
    DESTINATION='generic/platform=iOS'
elif [[ "$DEVICE" =~ ^([0-9A-Fa-f]{8}-[0-9A-Fa-f]{16}|[0-9A-Fa-f]{40})$ ]]; then
    DESTINATION="platform=iOS,id=$DEVICE"
else
    DESTINATION="platform=iOS,name=$DEVICE"
fi
bash "$ROOT/Apps/iPad/GenerateProject.sh"
xcodebuild -project "$PROJECT" -scheme Marconi -configuration "$CONFIG" -destination "$DESTINATION" \
    -derivedDataPath "$DERIVED" -allowProvisioningUpdates -allowProvisioningDeviceRegistration build

APP="$DERIVED/Build/Products/$CONFIG-iphoneos/Marconi.app"
echo "Built $APP"
if [[ -n "$DEVICE" ]]; then
    xcrun devicectl device install app --device "$DEVICE" "$APP"
    echo "Installed on $DEVICE_NAME"
fi
