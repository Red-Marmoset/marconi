#!/usr/bin/env bash
# PackageServer.sh - Build the Marconi Server for macOS as "Marconi Server.app" on a disk image:
#   Build/Installer/MarconiServer-<version>-macos-<arch>.dmg
#
# It builds the tray program (ServerUI/Project.json) Release and checks the version, as Package.sh does for the
# client, then hands it to the Una engine's Packaging/PackageProject.sh, which makes the app and the disk image from
# ServerUI/Project.json's package section: the icon and the disk image's background from Assets/ServerIcon-macOS.png,
# Installer/macOS/ServerInfo.plist (a menu-bar program with no Dock icon), and the login item's launch agent
# (Installer/macOS/com.redmarmoset.MarconiServer.plist, carried in Contents/Resources but not installed; see
# SERVER_PLAN.md, Phase 8). Finder lays out the disk image's window, so it needs someone logged in to the Mac's
# screen and permission to control Finder (the engine's Packaging/README.md); that is checked before the build.
# Package.sh doesn't call it yet; run it by hand until the owner has tried the server on a Mac.
#
# Signing and notarizing work as in Package.sh (MARCONI_SIGN_IDENTITY, MARCONI_NOTARY_PROFILE, named by
# ServerUI/Project.json's signing section). Sign with the same Developer ID every time: macOS keeps the Screen
# Recording and Accessibility permissions against the app's signing identity, so an ad-hoc signed build (a new
# identity at every build) has to be allowed again in System Settings after each rebuild.
#
# Usage:
#   ./PackageServer.sh [--clean] [--skip-build] [--allow-dirty]

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

fail() {
    echo "ERROR: $1" >&2
    exit 1
}

CLEAN=0
SKIP_BUILD=0
ALLOW_DIRTY=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        --allow-dirty) ALLOW_DIRTY=1 ;;
        *) fail "unknown option $arg (see the usage at the top of PackageServer.sh)." ;;
    esac
done

[[ "$(uname -s)" == "Darwin" ]] || fail "PackageServer.sh makes the macOS disk image, so it has to run on a Mac."
command -v git > /dev/null || fail "git not found. Install the Xcode command line tools (xcode-select --install)."
ENGINE="${UNA_ENGINE_PATH:-$ROOT/../engine}"
[[ -f "$ENGINE/Packaging/PackageProject.sh" ]] ||
    fail "the Una engine at $ENGINE has no Packaging/PackageProject.sh: update it, or set UNA_ENGINE_PATH."
# The engine's packaging checks Finder, its permission, the tools and ServerUI/Project.json before the build
bash "$ENGINE/Packaging/PackageProject.sh" --check-only --project-dir "$ROOT/ServerUI"
git -C "$ROOT" rev-parse HEAD > /dev/null 2>&1 || fail "packaging needs git and a clone of the repository."
if [[ "$(git -C "$ROOT" rev-parse --is-shallow-repository)" == "true" ]]; then
    fail "this is a shallow clone, so the revision would be wrong. Fetch the full history (git fetch --unshallow)."
fi
DIRTY="$(git -C "$ROOT" status --porcelain --untracked-files=no)"
if [[ -n "$DIRTY" && $ALLOW_DIRTY -eq 0 ]]; then
    fail "tracked files have uncommitted changes, so the version wouldn't identify the source. Commit them, or pass --allow-dirty for a test package."
fi

if [[ $SKIP_BUILD -eq 0 ]]; then
    BUILD_ARGS=(-config Release)
    if [[ $CLEAN -eq 1 ]]; then
        BUILD_ARGS+=(--clean)
    fi
    bash "$ROOT/Build.sh" ServerUI/Project.json "${BUILD_ARGS[@]}"
fi

EXE="$ROOT/Build/MarconiServerTray/Release/MarconiServerTray"
[[ -x "$EXE" ]] || fail "$EXE not found. Run without --skip-build."

BASE_VERSION="$(sed -n 's/^[[:space:]]*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/Project.json" | head -n 1)"
EXPECTED="$BASE_VERSION.$(git -C "$ROOT" rev-list --count HEAD)"
VERSION="$(sed -n 's/^#define MARCONI_VERSION_STRING "\(.*\)"$/\1/p' "$ROOT/Modules/MarconiCore/BuildVersion.h" 2>/dev/null || true)"
if [[ "$VERSION" != "$EXPECTED" ]]; then
    fail "the build is version ${VERSION:-unknown} but the source is $EXPECTED. Rebuild (run without --skip-build)."
fi
if [[ "$EXE" -ot "$ROOT/Modules/MarconiCore/BuildVersion.h" ]]; then
    fail "MarconiServerTray is older than its version header, so it may not be version $VERSION. Rebuild (run without --skip-build)."
fi

bash "$ENGINE/Packaging/PackageProject.sh" --executable "$EXE" --version "$VERSION" --project-dir "$ROOT/ServerUI"
if [[ -n "$DIRTY" ]]; then
    echo "WARNING: built with uncommitted changes; don't publish this one."
fi
echo "If it was signed ad hoc (see above), macOS asks again for Screen Recording and Accessibility after each"
echo "rebuild (see the top of PackageServer.sh)."
