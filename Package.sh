#!/usr/bin/env bash
# Package.sh - Build the release package for this computer's operating system:
#   macOS:  Build/Installer/Marconi-<version>-macos-<arch>.dmg
#   Linux:  Build/Installer/Marconi-<version>-linux-<arch>.deb
# and, when this computer has the Android tools, the Android app too (see Apps/Android/Package.sh):
#   Build/Installer/Marconi-<version>-android.apk
#
# Both start by building the Release app with Build.sh and checking that the build is the current source's version.
#
# macOS: wraps Build/Marconi/Release/Marconi in Marconi.app (Info.plist from Installer/macOS, the icon, and Nebula
# Sans in Contents/Resources), signs it and puts it on a disk image beside an Applications shortcut, so it installs
# by dragging.
#
# Signing: with the Developer ID Application certificate in this Mac's keychain when there is exactly one, else
# ad hoc, which runs on the Mac that built it (on another Mac, Gatekeeper blocks a downloaded copy until it's
# allowed in System Settings > Privacy & Security, "Open Anyway"). To choose a certificate, or to notarize, which
# publishing needs:
#   MARCONI_SIGN_IDENTITY="Developer ID Application: Red Marmoset Studios Ltd. (TEAMID)"   or "-" for ad hoc
#   MARCONI_NOTARY_PROFILE=<profile>   from: xcrun notarytool store-credentials <profile>
#
# Linux: makes a Debian package (.deb), which installs on Ubuntu, Debian and the distributions based on them with
#   sudo apt install ./Marconi-<version>-linux-<arch>.deb
# It puts the app at /usr/bin/marconi, a menu entry that also opens vnc:// links in /usr/share/applications, the
# icon under /usr/share/icons/hicolor, Nebula Sans in /usr/share/marconi (where the app looks for it when it runs
# from /usr/bin) and the licence and changelog in /usr/share/doc/marconi. The package depends on the system
# libraries the app links against, worked out by dpkg-shlibdeps (from the dpkg-dev package), so it needs a build
# machine no newer than the oldest release it should install on. The package is not signed.
#   MARCONI_DEB_MAINTAINER="Name <email>"   the package's Maintainer field, if not the owner's below
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

# Check the tools and the source before spending time on a build.
case "$(uname -s)" in
    Darwin)
        PLATFORM=macos
        for tool in git hdiutil codesign otool lipo plutil; do
            command -v "$tool" > /dev/null || fail "$tool not found. Install the Xcode command line tools (xcode-select --install)."
        done
        ;;
    Linux)
        PLATFORM=linux
        for tool in git dpkg dpkg-deb dpkg-shlibdeps strip gzip sha256sum; do
            command -v "$tool" > /dev/null || fail "$tool not found. Install the packaging tools (sudo apt install dpkg-dev binutils)."
        done
        ;;
    *)
        fail "Package.sh makes the macOS disk image or the Linux package, so it has to run on a Mac or on Linux."
        ;;
esac
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

INSTALLER_DIR="$ROOT/Build/Installer"
mkdir -p "$INSTALLER_DIR"
OUT_FILE=""

package_macos() {
    # Only system libraries may be linked dynamically: anything else would be missing on other Macs.
    local nonSystem
    nonSystem="$(otool -L "$EXE" | tail -n +2 | awk '{print $1}' | grep -vE '^(/usr/lib/|/System/Library/)' || true)"
    if [[ -n "$nonSystem" ]]; then
        fail "Marconi links libraries that other Macs won't have:"$'\n'"$nonSystem"
    fi

    local archs archLabel minMacos
    archs="$(lipo -archs "$EXE" 2>/dev/null || uname -m)"
    archLabel="$(tr ' ' '-' <<< "$archs")"
    if [[ "$archs" == *" "* ]]; then
        archLabel="universal"
    fi
    # The oldest macOS the binary runs on, from its LC_BUILD_VERSION load command (the build Mac's SDK by default).
    minMacos="$(otool -l "$EXE" | awk '/LC_BUILD_VERSION/ { found = 1 } found && $1 == "minos" { print $2; exit }')"
    [[ -n "$minMacos" ]] || minMacos="11.0"

    # Stage: Marconi.app plus an Applications shortcut, which become the disk image's contents.
    local stageDir app
    stageDir="$INSTALLER_DIR/StageMac"
    app="$stageDir/Marconi.app"
    rm -rf "$stageDir"
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

    cp "$EXE" "$app/Contents/MacOS/Marconi"
    sed -e "s/@VERSION@/$VERSION/g" -e "s/@SHORT_VERSION@/$BASE_VERSION/g" -e "s/@MIN_MACOS@/$minMacos/g" \
        "$ROOT/Installer/macOS/Info.plist" > "$app/Contents/Info.plist"
    plutil -lint "$app/Contents/Info.plist" > /dev/null
    printf 'APPL????' > "$app/Contents/PkgInfo"
    cp "$ROOT/Modules/MarconiUI/Resources/Marconi.icns" "$app/Contents/Resources/"
    cp "$ROOT/Assets/Fonts/NebulaSans-Black.ttf" "$ROOT/Assets/Fonts/NebulaSans-Semibold.ttf" \
        "$ROOT/Assets/Fonts/NebulaSans-LICENSE.txt" "$app/Contents/Resources/"
    cp "$ROOT/LICENSE" "$app/Contents/Resources/LICENSE.txt"

    # The Developer ID certificate in the keychain, when there is exactly one and no choice was made
    IDENTITY="${MARCONI_SIGN_IDENTITY:-}"
    if [[ -z "$IDENTITY" ]]; then
        local found
        found="$(security find-identity -v -p codesigning 2>/dev/null | grep -o '"Developer ID Application: [^"]*"' | tr -d '"' | sort -u || true)"
        if [[ -n "$found" && "$(wc -l <<< "$found")" -eq 1 ]]; then
            IDENTITY="$found"
        else
            IDENTITY="-"
        fi
    fi
    if [[ "$IDENTITY" == "-" ]]; then
        codesign --force --sign - "$app"
    else
        # Notarization requires the hardened runtime and a secure timestamp.
        codesign --force --options runtime --timestamp --sign "$IDENTITY" "$app"
    fi
    codesign --verify --strict "$app"

    ln -s /Applications "$stageDir/Applications"

    OUT_FILE="$INSTALLER_DIR/Marconi-$VERSION-macos-$archLabel.dmg"
    rm -f "$OUT_FILE"

    echo "[Package.sh] Version=$VERSION"
    echo "[Package.sh] Arch=$archs MinMacOS=$minMacos"
    echo "[Package.sh] Signing=$IDENTITY"

    hdiutil create -quiet -volname "Marconi $VERSION" -srcfolder "$stageDir" -fs HFS+ -format UDZO -ov "$OUT_FILE"

    if [[ "$IDENTITY" != "-" ]]; then
        codesign --force --timestamp --sign "$IDENTITY" "$OUT_FILE"
        if [[ -n "${MARCONI_NOTARY_PROFILE:-}" ]]; then
            xcrun notarytool submit "$OUT_FILE" --keychain-profile "$MARCONI_NOTARY_PROFILE" --wait
            xcrun stapler staple "$OUT_FILE"
        fi
    fi
}

package_linux() {
    # Only system libraries may be linked dynamically: anything from the build tree would be missing elsewhere.
    local nonSystem
    nonSystem="$(ldd "$EXE" | awk '/=> not found/ { print $1 " (not found)"; next } /=>/ { print $3 }' \
        | grep -vE '^(/lib|/lib64|/usr/lib)' || true)"
    if [[ -n "$nonSystem" ]]; then
        fail "Marconi links libraries that other computers won't have:"$'\n'"$nonSystem"
    fi

    local maintainer
    maintainer="${MARCONI_DEB_MAINTAINER:-Sem Essessi <sem.essessi@redmarmosetstudios.com>}"

    local arch stageDir pkg
    arch="$(dpkg --print-architecture)"
    stageDir="$INSTALLER_DIR/StageLinux"
    pkg="$stageDir/marconi"
    rm -rf "$stageDir"

    # Stage: the files as they will be installed, under $pkg, plus the package's control file in DEBIAN.
    install -d "$pkg/DEBIAN" "$pkg/usr/bin" "$pkg/usr/share/applications" "$pkg/usr/share/icons" \
        "$pkg/usr/share/marconi" "$pkg/usr/share/doc/marconi"
    install -m 755 "$EXE" "$pkg/usr/bin/marconi"
    strip --strip-unneeded "$pkg/usr/bin/marconi"
    install -m 644 "$ROOT/Installer/Linux/marconi.desktop" "$pkg/usr/share/applications/"
    cp -R "$ROOT/Installer/Linux/icons/hicolor" "$pkg/usr/share/icons/"
    # The app looks for its fonts in <prefix>/share/marconi, with the prefix taken from the executable's location.
    install -m 644 "$ROOT/Assets/Fonts/NebulaSans-Black.ttf" "$ROOT/Assets/Fonts/NebulaSans-Semibold.ttf" \
        "$ROOT/Assets/Fonts/NebulaSans-LICENSE.txt" "$pkg/usr/share/marconi/"
    install -m 644 "$ROOT/LICENSE" "$pkg/usr/share/doc/marconi/copyright"
    gzip -9n < "$ROOT/CHANGELOG.md" > "$pkg/usr/share/doc/marconi/changelog.gz"

    # The libraries the app needs, as package names with the versions this machine has, found by dpkg-shlibdeps.
    # It insists on a debian/control in the current directory, so give it a stub.
    local shlibsDir depends
    shlibsDir="$stageDir/shlibdeps"
    install -d "$shlibsDir/debian"
    printf 'Source: marconi\n\nPackage: marconi\nArchitecture: any\n' > "$shlibsDir/debian/control"
    if ! depends="$(cd "$shlibsDir" && dpkg-shlibdeps -O "$pkg/usr/bin/marconi" 2> "$stageDir/shlibdeps.log" \
        | sed -n 's/^shlibs:Depends=//p')" || [[ -z "$depends" ]]; then
        cat "$stageDir/shlibdeps.log" >&2
        fail "dpkg-shlibdeps couldn't work out which packages Marconi needs (see above)."
    fi

    local installedSize
    installedSize="$(du -sk --exclude=DEBIAN "$pkg" | cut -f 1)"
    cat > "$pkg/DEBIAN/control" <<CONTROL
Package: marconi
Version: $VERSION
Section: net
Priority: optional
Architecture: $arch
Maintainer: $maintainer
Installed-Size: $installedSize
Depends: $depends
Homepage: https://github.com/Red-Marmoset/marconi
Description: See and control another computer's screen
 Marconi lets you see and control another computer's screen from your own,
 as if you were sitting in front of it. It works with Macs through the
 Mac's built-in Screen Sharing, with Windows and Linux computers running a
 VNC server, and with computers that offer Remote Desktop.
CONTROL

    # dpkg wants directories 755, files 644 (the app 755) and everything owned by root.
    find "$pkg" -type d -exec chmod 755 {} +
    find "$pkg" -type f -exec chmod 644 {} +
    chmod 755 "$pkg/usr/bin/marconi"

    OUT_FILE="$INSTALLER_DIR/Marconi-$VERSION-linux-$arch.deb"
    rm -f "$OUT_FILE"

    echo "[Package.sh] Version=$VERSION"
    echo "[Package.sh] Arch=$arch"
    echo "[Package.sh] Maintainer=$maintainer"
    echo "[Package.sh] Depends=$depends"

    # xz rather than this machine's default (zstd on Ubuntu), which older Debian releases can't read.
    dpkg-deb --build --root-owner-group -Zxz "$pkg" "$OUT_FILE"
}

if [[ "$PLATFORM" == macos ]]; then
    package_macos
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
else
    package_linux
    HASH="$(sha256sum "$OUT_FILE" | awk '{print $1}')"
    echo ""
    echo "Package: $OUT_FILE"
    echo "SHA256:  $HASH"
    echo "Install: sudo apt install ./${OUT_FILE#"$ROOT"/}"
    if [[ -n "$DIRTY" ]]; then
        echo "WARNING: built with uncommitted changes; don't publish this one."
    else
        echo "To mark the release: git tag v$VERSION; git push origin v$VERSION"
    fi
fi

# The Android app too, beside the package, when this computer has the Android tools.
ANDROID_ARGS=()
if [[ $CLEAN -eq 1 ]]; then
    ANDROID_ARGS+=(--clean)
fi
bash "$ROOT/Apps/Android/Package.sh" "${ANDROID_ARGS[@]}"
