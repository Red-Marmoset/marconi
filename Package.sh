#!/usr/bin/env bash
# Package.sh - Build the macOS disk image: Build/Installer/Marconi-<version>-macos-<arch>.dmg
#
# Builds the Release app with Build.sh, wraps Build/Marconi/Release/Marconi in Marconi.app (Info.plist from
# Installer/macOS, the icon, and Nebula Sans in Contents/Resources), signs it and puts it on a disk image beside an
# Applications shortcut, so it installs by dragging.
#
# Signing: ad-hoc by default, which runs on the Mac that built it. On another Mac, Gatekeeper blocks a downloaded
# copy until it's allowed in System Settings > Privacy & Security ("Open Anyway"). To publish, sign with a
# Developer ID and notarize:
#   MARCONI_SIGN_IDENTITY="Developer ID Application: Red Marmoset Studios Ltd. (TEAMID)"
#   MARCONI_NOTARY_PROFILE=<profile>   from: xcrun notarytool store-credentials <profile>
#
# The version is MAJOR.MINOR.PATCH from Project.json plus the revision, the number of commits in the history (see
# README.md). Release builds should come from a clean, full clone so the version identifies the source exactly.
#
# Usage:
#   ./Package.sh [--clean] [--skip-build] [--allow-dirty]
#     --clean        clean Release build first
#     --skip-build   package the existing Build/Marconi/Release/Marconi (it must match the current version)
#     --allow-dirty  package even though tracked files have uncommitted changes (for testing the package)

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
        *) fail "unknown option $arg (see the usage at the top of Package.sh)." ;;
    esac
done

# Check the source before spending time on a build.
[[ "$(uname -s)" == "Darwin" ]] || fail "Package.sh makes the macOS disk image and has to run on a Mac."
for tool in git hdiutil codesign otool plutil; do
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
    bash "$ROOT/Build.sh" "${BUILD_ARGS[@]}"
fi

EXE="$ROOT/Build/Marconi/Release/Marconi"
[[ -x "$EXE" ]] || fail "$EXE not found. Run without --skip-build."

# The package is labelled with the version the build stamped into the app, which must be the current source's.
BASE_VERSION="$(sed -n 's/^[[:space:]]*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/Project.json" | head -n 1)"
EXPECTED="$BASE_VERSION.$(git -C "$ROOT" rev-list --count HEAD)"
VERSION="$(sed -n 's/^#define MARCONI_VERSION_STRING "\(.*\)"$/\1/p' "$ROOT/Modules/MarconiCore/BuildVersion.h" 2>/dev/null || true)"
if [[ "$VERSION" != "$EXPECTED" ]]; then
    fail "the build is version ${VERSION:-unknown} but the source is $EXPECTED. Rebuild (run without --skip-build)."
fi
if [[ "$EXE" -ot "$ROOT/Modules/MarconiCore/BuildVersion.h" ]]; then
    fail "Marconi is older than its version header, so it may not be version $VERSION. Rebuild (run without --skip-build)."
fi

# Only system libraries may be linked dynamically: anything else would be missing on other Macs.
NON_SYSTEM="$(otool -L "$EXE" | tail -n +2 | awk '{print $1}' | grep -vE '^(/usr/lib/|/System/Library/)' || true)"
if [[ -n "$NON_SYSTEM" ]]; then
    fail "Marconi links libraries that other Macs won't have:"$'\n'"$NON_SYSTEM"
fi

ARCHS="$(lipo -archs "$EXE" 2>/dev/null || uname -m)"
ARCH_LABEL="$(tr ' ' '-' <<< "$ARCHS")"
if [[ "$ARCHS" == *" "* ]]; then
    ARCH_LABEL="universal"
fi
# The oldest macOS the binary runs on, from its LC_BUILD_VERSION load command (the build Mac's SDK by default).
MIN_MACOS="$(otool -l "$EXE" | awk '/LC_BUILD_VERSION/ { found = 1 } found && $1 == "minos" { print $2; exit }')"
[[ -n "$MIN_MACOS" ]] || MIN_MACOS="11.0"

# Stage: Marconi.app plus an Applications shortcut, which become the disk image's contents.
INSTALLER_DIR="$ROOT/Build/Installer"
STAGE_DIR="$INSTALLER_DIR/StageMac"
APP="$STAGE_DIR/Marconi.app"
rm -rf "$STAGE_DIR"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$EXE" "$APP/Contents/MacOS/Marconi"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@SHORT_VERSION@/$BASE_VERSION/g" -e "s/@MIN_MACOS@/$MIN_MACOS/g" \
    "$ROOT/Installer/macOS/Info.plist" > "$APP/Contents/Info.plist"
plutil -lint "$APP/Contents/Info.plist" > /dev/null
printf 'APPL????' > "$APP/Contents/PkgInfo"
cp "$ROOT/Modules/MarconiUI/Resources/Marconi.icns" "$APP/Contents/Resources/"
cp "$ROOT/Assets/Fonts/NebulaSans-Black.ttf" "$ROOT/Assets/Fonts/NebulaSans-Semibold.ttf" \
    "$ROOT/Assets/Fonts/NebulaSans-LICENSE.txt" "$APP/Contents/Resources/"
cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE.txt"

IDENTITY="${MARCONI_SIGN_IDENTITY:--}"
if [[ "$IDENTITY" == "-" ]]; then
    codesign --force --sign - "$APP"
else
    # Notarization requires the hardened runtime and a secure timestamp.
    codesign --force --options runtime --timestamp --sign "$IDENTITY" "$APP"
fi
codesign --verify --strict "$APP"

ln -s /Applications "$STAGE_DIR/Applications"

mkdir -p "$INSTALLER_DIR"
OUT_FILE="$INSTALLER_DIR/Marconi-$VERSION-macos-$ARCH_LABEL.dmg"
rm -f "$OUT_FILE"

echo "[Package.sh] Version=$VERSION"
echo "[Package.sh] Arch=$ARCHS MinMacOS=$MIN_MACOS"
echo "[Package.sh] Signing=$IDENTITY"

hdiutil create -quiet -volname "Marconi $VERSION" -srcfolder "$STAGE_DIR" -fs HFS+ -format UDZO -ov "$OUT_FILE"

if [[ "$IDENTITY" != "-" ]]; then
    codesign --force --timestamp --sign "$IDENTITY" "$OUT_FILE"
    if [[ -n "${MARCONI_NOTARY_PROFILE:-}" ]]; then
        xcrun notarytool submit "$OUT_FILE" --keychain-profile "$MARCONI_NOTARY_PROFILE" --wait
        xcrun stapler staple "$OUT_FILE"
    fi
fi

HASH="$(shasum -a 256 "$OUT_FILE" | awk '{print $1}')"
echo ""
echo "Disk image: $OUT_FILE"
echo "SHA256:     $HASH"
if [[ -n "$DIRTY" ]]; then
    echo "WARNING: built with uncommitted changes; don't publish this one."
elif [[ "$IDENTITY" == "-" ]]; then
    echo "Ad-hoc signed: fine for testing, but other Macs need 'Open Anyway' in System Settings > Privacy & Security."
else
    echo "To mark the release: git tag v$VERSION; git push origin v$VERSION"
fi
