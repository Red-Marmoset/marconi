#!/usr/bin/env bash
# Package.sh - Build the release package for this computer's operating system:
#   macOS:  Build/Installer/Marconi-<version>-macos-<arch>.dmg
#   Linux:  Build/Installer/Marconi-<version>-linux-<arch>.deb
# and, when this computer has the Android tools, the Android app too (see Apps/Android/Package.sh):
#   Build/Installer/Marconi-<version>-android.apk
#
# Both start by building the Release app with Build.sh and checking that the build is the current source's version.
#
# macOS: hands Build/Marconi/Release/Marconi to the Una engine's Packaging/PackageProject.sh, which makes Marconi.app
# (Installer/macOS/Info.plist, the icon from Assets/AppIcon.png, Nebula Sans in Contents/Resources, MarconiMcp beside
# the app's program with the Claude Desktop extension made from it by Installer/macOS/BeforeSigning.sh), signs it
# and puts it on a disk image to drag it to Applications from, all as Project.json's package section says. Finder
# lays out the disk image's window, so it has to run with someone logged in to the Mac's screen and be allowed to
# control Finder (the engine's Packaging/README.md, "Packaging on a build server"); that is checked before the build.
#
# Signing: with the Developer ID Application certificate in this Mac's keychain when there is exactly one, else
# ad hoc, which runs on the Mac that built it (on another Mac, Gatekeeper blocks a downloaded copy until it's
# allowed in System Settings > Privacy & Security, "Open Anyway"). To choose a certificate, or to notarize, which
# publishing needs, set the environment variables Project.json's "signing" section names (macos.identityEnv and
# macos.notaryProfileEnv):
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
#   MARCONI_DEB_MAINTAINER="Name <email>"   the package's Maintainer field, if not the owner's below (the variable
#                                           Project.json's signing section names as linux.maintainerEnv)
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

ENGINE="${UNA_ENGINE_PATH:-$ROOT/../engine}"

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
        command -v git > /dev/null || fail "git not found. Install the Xcode command line tools (xcode-select --install)."
        [[ -f "$ENGINE/Packaging/PackageProject.sh" ]] ||
            fail "the Una engine at $ENGINE has no Packaging/PackageProject.sh: update it, or set UNA_ENGINE_PATH."
        # The engine's packaging checks Finder, its permission, the tools and Project.json before the build
        bash "$ENGINE/Packaging/PackageProject.sh" --check-only --project-dir "$ROOT"
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

# Which environment variables hold the signing inputs is Project.json's to say; BuildTool reads it.
source "$ROOT/Tools/SigningSettings.sh"
find_buildtool
[[ -n "$BUILDTOOL" ]] || fail "no BuildTool under ${UNA_ENGINE_PATH:-$ROOT/../engine}/Build/BuildTool (bootstrap the engine, or set UNA_ENGINE_PATH)."

EXE="$ROOT/Build/Marconi/Release/Marconi"
# MarconiMcp, the program AI tools start, and the engine's list of apps it looks for: Build.sh puts both beside the
# app.
MCP_EXE="$ROOT/Build/Marconi/Release/MarconiMcp"
APPS_JSON="$ROOT/Build/Marconi/Release/Apps.json"
[[ -x "$EXE" ]] || fail "$EXE not found. Run without --skip-build."
[[ -x "$MCP_EXE" && -f "$APPS_JSON" ]] || fail "$MCP_EXE or $APPS_JSON not found. Run without --skip-build."

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

package_linux() {
    # Only system libraries may be linked dynamically: anything from the build tree would be missing elsewhere.
    local nonSystem
    nonSystem="$(ldd "$EXE" | awk '/=> not found/ { print $1 " (not found)"; next } /=>/ { print $3 }' \
        | grep -vE '^(/lib|/lib64|/usr/lib)' || true)"
    if [[ -n "$nonSystem" ]]; then
        fail "Marconi links libraries that other computers won't have:"$'\n'"$nonSystem"
    fi

    local maintainer
    maintainer="$(signing_input linux.maintainer)"
    [[ -n "$maintainer" ]] || maintainer="Sem Essessi <sem.essessi@redmarmosetstudios.com>"

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
    # MarconiMcp, the program AI tools start; it finds Apps.json in <prefix>/share/marconi.
    install -m 755 "$MCP_EXE" "$pkg/usr/bin/marconi-mcp"
    strip --strip-unneeded "$pkg/usr/bin/marconi-mcp"
    install -m 644 "$ROOT/Installer/Linux/marconi.desktop" "$pkg/usr/share/applications/"
    cp -R "$ROOT/Installer/Linux/icons/hicolor" "$pkg/usr/share/icons/"
    # The app looks for its fonts in <prefix>/share/marconi, with the prefix taken from the executable's location.
    install -m 644 "$ROOT/Assets/Fonts/NebulaSans-Black.ttf" "$ROOT/Assets/Fonts/NebulaSans-Semibold.ttf" \
        "$ROOT/Assets/Fonts/NebulaSans-LICENSE.txt" "$APPS_JSON" "$pkg/usr/share/marconi/"
    install -m 644 "$ROOT/LICENSE" "$pkg/usr/share/doc/marconi/copyright"
    gzip -9n < "$ROOT/CHANGELOG.md" > "$pkg/usr/share/doc/marconi/changelog.gz"

    # The libraries the app needs, as package names with the versions this machine has, found by dpkg-shlibdeps.
    # It insists on a debian/control in the current directory, so give it a stub.
    local shlibsDir depends
    shlibsDir="$stageDir/shlibdeps"
    install -d "$shlibsDir/debian"
    printf 'Source: marconi\n\nPackage: marconi\nArchitecture: any\n' > "$shlibsDir/debian/control"
    if ! depends="$(cd "$shlibsDir" && dpkg-shlibdeps -O "$pkg/usr/bin/marconi" "$pkg/usr/bin/marconi-mcp" \
        2> "$stageDir/shlibdeps.log" \
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
    chmod 755 "$pkg/usr/bin/marconi" "$pkg/usr/bin/marconi-mcp"

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
    bash "$ENGINE/Packaging/PackageProject.sh" --executable "$EXE" --version "$VERSION" --project-dir "$ROOT"
    if [[ -n "$DIRTY" ]]; then
        echo "WARNING: built with uncommitted changes; don't publish this one."
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
# The ${...+...} form keeps macOS's bash 3.2 from calling an empty array unbound under set -u
bash "$ROOT/Apps/Android/Package.sh" ${ANDROID_ARGS[@]+"${ANDROID_ARGS[@]}"}
