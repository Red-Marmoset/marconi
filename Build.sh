#!/usr/bin/env bash
# Build wrapper for macOS/Linux: sets UNA_ENGINE_PATH and invokes the engine's
# BuildTool on a Project.json from the rmvnc root (so output lands in ./Build).
#
# Usage:
#   ./Build.sh [Project.json] [BuildTool args...]
#   ./Build.sh                                   # the app, Release
#   ./Build.sh Tests/RmVncCoreTest/Project.json -config Debug

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${UNA_ENGINE_PATH:=$SCRIPT_DIR/../engine}"
export UNA_ENGINE_PATH

PROJECT="Project.json"
if [[ $# -gt 0 && "$1" == *.json ]]; then
    PROJECT="$1"
    shift
fi

BUILDTOOL=""
for cfg in Release MinSizeRel Debug Bootstrap; do
    if [[ -x "$UNA_ENGINE_PATH/Build/BuildTool/$cfg/BuildTool" ]]; then
        BUILDTOOL="$UNA_ENGINE_PATH/Build/BuildTool/$cfg/BuildTool"
        break
    fi
done

if [[ -z "$BUILDTOOL" ]]; then
    echo "[Build.sh] BuildTool not found under $UNA_ENGINE_PATH/Build/BuildTool/."
    echo "[Build.sh] Bootstrap it first: cd $UNA_ENGINE_PATH && ./BootstrapBuildTool.sh"
    exit 1
fi

JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

# Default to Release unless the caller passed -config.
CONFIG_ARGS=(-config Release)
for arg in "$@"; do
    if [[ "$arg" == "-config" ]]; then
        CONFIG_ARGS=()
    fi
done

echo "[Build.sh] UNA_ENGINE_PATH=$UNA_ENGINE_PATH"
echo "[Build.sh] BuildTool=$BUILDTOOL"
echo "[Build.sh] Project=$PROJECT"

cd "$SCRIPT_DIR"
exec "$BUILDTOOL" "$PROJECT" "${CONFIG_ARGS[@]}" -j "$JOBS" "$@"
