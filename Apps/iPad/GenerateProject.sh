#!/usr/bin/env bash
# Generates Build/Xcode/Marconi/Marconi.xcodeproj through Una's BuildTool. Goes through Build.sh so the version
# header the core needs (Modules/MarconiCore/BuildVersion.h) is written first; rerun after a version change.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
./Build.sh Apps/iPad/Project.json --generate-xcode --platform iOS
echo "Open $ROOT/Build/Xcode/Marconi/Marconi.xcodeproj in Xcode."
