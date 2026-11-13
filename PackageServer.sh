#!/usr/bin/env bash
# PackageServer.sh - Build the Marconi Server for macOS as "Marconi Server.app" on a disk image:
#   Build/Installer/MarconiServer-<version>-macos-<arch>.dmg
#
# NOT YET RUN ON A MAC: written without one (see SERVER_PLAN.md, Phase 8). It mirrors the client's package_macos in
# Package.sh, with the server's Info.plist (Installer/macOS/ServerInfo.plist, a menu-bar program with no Dock icon).
# Package.sh doesn't call it yet; run it by hand until the owner has tried the result.
#
# Signing and notarizing work as in Package.sh (MARCONI_SIGN_IDENTITY, MARCONI_NOTARY_PROFILE). Sign with the same
# Developer ID every time: macOS keeps the Screen Recording and Accessibility permissions against the app's signing
# identity, so an ad-hoc signed build (a new identity at every build) has to be allowed again in System Settings
# after each rebuild.
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
for tool in git hdiutil codesign otool lipo plutil; do
    command -v "$tool" > /dev/null || fail "$tool not found. Install the Xcode command line tools (xcode-select --install)."
done
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

# Only system libraries may be linked dynamically: anything else would be missing on other Macs.
NON_SYSTEM="$(otool -L "$EXE" | tail -n +2 | awk '{print $1}' | grep -vE '^(/usr/lib/|/System/Library/)' || true)"
if [[ -n "$NON_SYSTEM" ]]; then
    fail "Marconi Server links libraries that other Macs won't have:"$'\n'"$NON_SYSTEM"
fi

ARCHS="$(lipo -archs "$EXE" 2>/dev/null || uname -m)"
ARCH_LABEL="$(tr ' ' '-' <<< "$ARCHS")"
if [[ "$ARCHS" == *" "* ]]; then
    ARCH_LABEL="universal"
fi
MIN_MACOS="$(otool -l "$EXE" | awk '/LC_BUILD_VERSION/ { found = 1 } found && $1 == "minos" { print $2; exit }')"
[[ -n "$MIN_MACOS" ]] || MIN_MACOS="11.0"

INSTALLER_DIR="$ROOT/Build/Installer"
STAGE_DIR="$INSTALLER_DIR/StageMacServer"
APP="$STAGE_DIR/Marconi Server.app"
rm -rf "$STAGE_DIR"
mkdir -p "$INSTALLER_DIR" "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$EXE" "$APP/Contents/MacOS/MarconiServer"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@SHORT_VERSION@/$BASE_VERSION/g" -e "s/@MIN_MACOS@/$MIN_MACOS/g" \
    "$ROOT/Installer/macOS/ServerInfo.plist" > "$APP/Contents/Info.plist"
plutil -lint "$APP/Contents/Info.plist" > /dev/null
printf 'APPL????' > "$APP/Contents/PkgInfo"
cp "$ROOT/Modules/MarconiUI/Resources/Marconi.icns" "$APP/Contents/Resources/" # the viewer's icon, until the server has its own
cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE.txt"

# The login-item plan's launch agent travels with the app but is not installed by it (see SERVER_PLAN.md).
cp "$ROOT/Installer/macOS/com.redmarmoset.MarconiServer.plist" "$APP/Contents/Resources/"

IDENTITY="${MARCONI_SIGN_IDENTITY:-}"
if [[ -z "$IDENTITY" ]]; then
    FOUND="$(security find-identity -v -p codesigning 2>/dev/null | grep -o '"Developer ID Application: [^"]*"' | tr -d '"' | sort -u || true)"
    if [[ -n "$FOUND" && "$(wc -l <<< "$FOUND")" -eq 1 ]]; then
        IDENTITY="$FOUND"
    else
        IDENTITY="-"
    fi
fi
if [[ "$IDENTITY" == "-" ]]; then
    codesign --force --sign - "$APP"
else
    codesign --force --options runtime --timestamp --sign "$IDENTITY" "$APP"
fi
codesign --verify --strict "$APP"

ln -s /Applications "$STAGE_DIR/Applications"

OUT_FILE="$INSTALLER_DIR/MarconiServer-$VERSION-macos-$ARCH_LABEL.dmg"
rm -f "$OUT_FILE"
echo "[PackageServer.sh] Version=$VERSION Arch=$ARCHS MinMacOS=$MIN_MACOS Signing=$IDENTITY"
hdiutil create -quiet -volname "Marconi Server $VERSION" -srcfolder "$STAGE_DIR" -fs HFS+ -format UDZO -ov "$OUT_FILE"

if [[ "$IDENTITY" != "-" ]]; then
    codesign --force --timestamp --sign "$IDENTITY" "$OUT_FILE"
    if [[ -n "${MARCONI_NOTARY_PROFILE:-}" ]]; then
        xcrun notarytool submit "$OUT_FILE" --keychain-profile "$MARCONI_NOTARY_PROFILE" --wait
        xcrun stapler staple "$OUT_FILE"
    fi
fi

echo ""
echo "Disk image: $OUT_FILE"
echo "SHA256:     $(shasum -a 256 "$OUT_FILE" | awk '{print $1}')"
if [[ -n "$DIRTY" ]]; then
    echo "WARNING: built with uncommitted changes; don't publish this one."
elif [[ "$IDENTITY" == "-" ]]; then
    echo "Ad-hoc signed: fine for testing on this Mac. After each rebuild, allow Marconi Server again under Screen"
    echo "Recording and Accessibility in System Settings > Privacy & Security."
fi
