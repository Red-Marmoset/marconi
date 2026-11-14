#!/usr/bin/env bash
# Copyright (c) 2026 Red Marmoset Studios Ltd.
# Installer/macOS/BeforeSigning.sh - Adds the Claude Desktop extension (marconi.mcpb) to Marconi.app, made from the
# app's signed MarconiMcp, so the app's signature seals it. The Una engine's Packaging/PackageProject.sh runs it
# (Project.json's package.macos.beforeSigning) once MarconiMcp is signed and before the app is:
#
#   Installer/macOS/BeforeSigning.sh <Marconi.app> <version MAJOR.MINOR.PATCH.REVISION>

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="$1"
VERSION="$2"

bash "$ROOT/Tools/make_mcpb.sh" "$APP/Contents/MacOS/MarconiMcp" darwin "${VERSION%.*}" \
    "$APP/Contents/Resources/marconi.mcpb"
