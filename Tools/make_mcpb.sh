#!/bin/bash
# Copyright (c) 2026 Red Marmoset Studios Ltd.
# Tools/make_mcpb.sh - Make the Claude Desktop extension (marconi.mcpb): Installer/Mcpb/manifest.json filled in, and
# the MarconiMcp program as server/<its name>, in a zip without folder entries (Anthropic's mcpb tool can't unpack
# those). Package.sh runs it on macOS after signing MarconiMcp; Build.ps1 does the same on Windows itself.
#
#   Tools/make_mcpb.sh <MarconiMcp program> <platform: darwin|win32|linux> <version MAJOR.MINOR.PATCH> <output.mcpb>

set -euo pipefail

[[ $# -eq 4 ]] || { echo "Usage: $0 <program> <darwin|win32|linux> <version> <output.mcpb>" >&2; exit 2; }
PROGRAM="$1"
PLATFORM="$2"
VERSION="$3"
OUTPUT="$4"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ -x "$PROGRAM" ]] || { echo "ERROR: $PROGRAM isn't a program" >&2; exit 1; }
command -v zip > /dev/null || { echo "ERROR: zip not found" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
NAME="$(basename "$PROGRAM")"
mkdir -p "$STAGE/server"
cp -p "$PROGRAM" "$STAGE/server/$NAME"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@PROGRAM@/$NAME/g" -e "s/@PLATFORM@/$PLATFORM/g" \
    "$ROOT/Installer/Mcpb/manifest.json" > "$STAGE/manifest.json"

OUTPUT_DIR="$(cd "$(dirname "$OUTPUT")" && pwd)"
rm -f "$OUTPUT"
(cd "$STAGE" && zip -q -X -D -r "$OUTPUT_DIR/$(basename "$OUTPUT")" manifest.json server)
echo "[make_mcpb.sh] $OUTPUT ($PLATFORM, $VERSION)"
