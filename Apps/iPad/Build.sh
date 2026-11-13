#!/usr/bin/env bash
# Apps/iPad/Build.sh - Generate the Xcode project and build the app from the command line, on a Mac.
#
#   bash Apps/iPad/Build.sh                  build for any iOS device (signed with the project's team)
#   bash Apps/iPad/Build.sh "Sem's iPad"     build for that device and install the app on it
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

bash "$ROOT/Apps/iPad/GenerateProject.sh"

if [[ "${1:-}" == "--simulator" ]]; then
    xcodebuild -project "$PROJECT" -scheme Marconi -configuration "$CONFIG" -sdk iphonesimulator \
        -destination 'generic/platform=iOS Simulator' -derivedDataPath "$DERIVED" CODE_SIGNING_ALLOWED=NO build
    exit 0
fi

DEVICE="${1:-}"
if [[ -z "$DEVICE" ]]; then
    DESTINATION='generic/platform=iOS'
else
    DESTINATION="platform=iOS,name=$DEVICE"
fi
xcodebuild -project "$PROJECT" -scheme Marconi -configuration "$CONFIG" -destination "$DESTINATION" \
    -derivedDataPath "$DERIVED" -allowProvisioningUpdates -allowProvisioningDeviceRegistration build

APP="$DERIVED/Build/Products/$CONFIG-iphoneos/Marconi.app"
echo "Built $APP"
if [[ -n "$DEVICE" ]]; then
    xcrun devicectl device install app --device "$DEVICE" "$APP"
fi
