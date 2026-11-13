#!/usr/bin/env bash
# PackageServerLinux.sh - Build the Marconi Server package for Linux:
#   Build/Installer/MarconiServer-<version>-linux-<arch>.deb
#
# A Debian package (Ubuntu, Debian and the distributions based on them): sudo apt install ./MarconiServer-...deb
# It builds Release of ServerUI (the tray program) and Server (the console program) and puts:
#   /usr/bin/marconi-server                              the tray program
#   /usr/bin/marconi-server-console                      the same server without a tray icon, for a terminal
#   /usr/share/applications/marconi-server.desktop       the menu entry
#   /usr/share/icons/hicolor/*/apps/marconi-server.png   the icons
#   /usr/lib/systemd/user/marconi-server.service         a systemd USER unit (see below)
#   /usr/share/doc/marconi-server/                       README, licence, changelog
#
# The server has to run inside a signed-in graphical session, because that is where the screen it shares is. So the
# unit is a user unit, started with the session (WantedBy graphical-session.target), not a system service. The
# package does not turn it on for anyone: each user runs `systemctl --user enable --now marconi-server.service`.
#
# The package depends on the system libraries the programs link against, worked out by dpkg-shlibdeps (from the
# dpkg-dev package), so build on a machine no newer than the oldest release it should install on. It is not signed.
#   MARCONI_DEB_MAINTAINER="Name <email>"   the Maintainer field, if not the owner's below
#
# The version is MAJOR.MINOR.PATCH from Project.json plus the revision (commit count), as for the client package.
#
# Usage:
#   ./PackageServerLinux.sh [--clean] [--skip-build] [--allow-dirty]
#     --clean        clean Release builds first
#     --skip-build   package the existing Build/MarconiServerTray/Release and Build/MarconiServer/Release builds
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
        *) fail "unknown option $arg (see the usage at the top of PackageServerLinux.sh)." ;;
    esac
done

[[ "$(uname -s)" == "Linux" ]] || fail "PackageServerLinux.sh makes the Linux package, so it has to run on Linux."
for tool in git dpkg dpkg-deb dpkg-shlibdeps strip gzip sha256sum; do
    command -v "$tool" > /dev/null || fail "$tool not found. Install the packaging tools (sudo apt install dpkg-dev binutils)."
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
    bash "$ROOT/Build.sh" Server/Project.json "${BUILD_ARGS[@]}"
fi

TRAY="$ROOT/Build/MarconiServerTray/Release/MarconiServerTray"
CONSOLE="$ROOT/Build/MarconiServer/Release/MarconiServer"
[[ -x "$TRAY" ]] || fail "$TRAY not found. Run without --skip-build."
[[ -x "$CONSOLE" ]] || fail "$CONSOLE not found. Run without --skip-build."

# The package is labelled with the version the build stamped into the programs, which must be the current source's.
BASE_VERSION="$(sed -n 's/^[[:space:]]*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/Project.json" | head -n 1)"
EXPECTED="$BASE_VERSION.$(git -C "$ROOT" rev-list --count HEAD)"
VERSION="$(sed -n 's/^#define MARCONI_VERSION_STRING "\(.*\)"$/\1/p' "$ROOT/Modules/MarconiCore/BuildVersion.h" 2>/dev/null || true)"
if [[ "$VERSION" != "$EXPECTED" ]]; then
    fail "the build is version ${VERSION:-unknown} but the source is $EXPECTED. Rebuild (run without --skip-build)."
fi
for exe in "$TRAY" "$CONSOLE"; do
    if [[ "$exe" -ot "$ROOT/Modules/MarconiCore/BuildVersion.h" ]]; then
        fail "$(basename "$exe") is older than its version header, so it may not be version $VERSION. Rebuild (run without --skip-build)."
    fi
done

# Only system libraries may be linked dynamically: anything from the build tree would be missing elsewhere.
for exe in "$TRAY" "$CONSOLE"; do
    nonSystem="$(ldd "$exe" | awk '/=> not found/ { print $1 " (not found)"; next } /=>/ { print $3 }' \
        | grep -vE '^(/lib|/lib64|/usr/lib)' || true)"
    if [[ -n "$nonSystem" ]]; then
        fail "$(basename "$exe") links libraries that other computers won't have:"$'\n'"$nonSystem"
    fi
done

MAINTAINER="${MARCONI_DEB_MAINTAINER:-Sem Essessi <sem.essessi@redmarmosetstudios.com>}"
ARCH="$(dpkg --print-architecture)"
INSTALLER_DIR="$ROOT/Build/Installer"
STAGE_DIR="$INSTALLER_DIR/StageServerLinux"
PKG="$STAGE_DIR/marconi-server"
rm -rf "$STAGE_DIR"
mkdir -p "$INSTALLER_DIR"

# Stage: the files as they will be installed, under $PKG, plus the package's control file in DEBIAN.
install -d "$PKG/DEBIAN" "$PKG/usr/bin" "$PKG/usr/share/applications" "$PKG/usr/lib/systemd/user" \
    "$PKG/usr/share/doc/marconi-server"
install -m 755 "$TRAY" "$PKG/usr/bin/marconi-server"
install -m 755 "$CONSOLE" "$PKG/usr/bin/marconi-server-console"
strip --strip-unneeded "$PKG/usr/bin/marconi-server" "$PKG/usr/bin/marconi-server-console"
install -m 644 "$ROOT/Installer/Linux/server/marconi-server.desktop" "$PKG/usr/share/applications/"
install -m 644 "$ROOT/Installer/Linux/server/marconi-server.service" "$PKG/usr/lib/systemd/user/"
install -m 644 "$ROOT/Installer/Linux/server/README" "$PKG/usr/share/doc/marconi-server/README"
install -m 644 "$ROOT/LICENSE" "$PKG/usr/share/doc/marconi-server/copyright"
gzip -9n < "$ROOT/CHANGELOG.md" > "$PKG/usr/share/doc/marconi-server/changelog.gz"
# The icons are the client's, under the server's own name so the two packages don't share a file.
while IFS= read -r icon; do
    size="$(basename "$(dirname "$(dirname "$icon")")")"
    install -d "$PKG/usr/share/icons/hicolor/$size/apps"
    install -m 644 "$icon" "$PKG/usr/share/icons/hicolor/$size/apps/marconi-server.png"
done < <(find "$ROOT/Installer/Linux/icons/hicolor" -path '*/apps/marconi.png' | sort)

# The libraries the programs need, as package names with the versions this machine has, found by dpkg-shlibdeps.
# It insists on a debian/control in the current directory, so give it a stub.
SHLIBS_DIR="$STAGE_DIR/shlibdeps"
install -d "$SHLIBS_DIR/debian"
printf 'Source: marconi-server\n\nPackage: marconi-server\nArchitecture: any\n' > "$SHLIBS_DIR/debian/control"
if ! DEPENDS="$(cd "$SHLIBS_DIR" && dpkg-shlibdeps -O "$PKG/usr/bin/marconi-server" "$PKG/usr/bin/marconi-server-console" \
    2> "$STAGE_DIR/shlibdeps.log" | sed -n 's/^shlibs:Depends=//p')" || [[ -z "$DEPENDS" ]]; then
    cat "$STAGE_DIR/shlibdeps.log" >&2
    fail "dpkg-shlibdeps couldn't work out which packages Marconi Server needs (see above)."
fi

INSTALLED_SIZE="$(du -sk --exclude=DEBIAN "$PKG" | cut -f 1)"
cat > "$PKG/DEBIAN/control" <<CONTROL
Package: marconi-server
Version: $VERSION
Section: net
Priority: optional
Architecture: $ARCH
Maintainer: $MAINTAINER
Installed-Size: $INSTALLED_SIZE
Depends: $DEPENDS
Homepage: https://github.com/Red-Marmoset/marconi
Description: Share this computer's screen with Marconi
 Marconi Server lets the Marconi viewer on another computer see and control
 this computer's screen. It runs in your signed-in desktop X11 session, with
 an icon in the system tray, and asks you to choose an access password the
 first time. Nobody can connect until a password is set.
CONTROL

# dpkg wants directories 755, files 644 (the programs 755) and everything owned by root.
find "$PKG" -type d -exec chmod 755 {} +
find "$PKG" -type f -exec chmod 644 {} +
chmod 755 "$PKG/usr/bin/marconi-server" "$PKG/usr/bin/marconi-server-console"

OUT_FILE="$INSTALLER_DIR/MarconiServer-$VERSION-linux-$ARCH.deb"
rm -f "$OUT_FILE"

echo "[PackageServerLinux.sh] Version=$VERSION"
echo "[PackageServerLinux.sh] Arch=$ARCH"
echo "[PackageServerLinux.sh] Maintainer=$MAINTAINER"
echo "[PackageServerLinux.sh] Depends=$DEPENDS"

# xz rather than this machine's default (zstd on Ubuntu), which older Debian releases can't read.
dpkg-deb --build --root-owner-group -Zxz "$PKG" "$OUT_FILE"

echo ""
echo "Package: $OUT_FILE"
echo "SHA256:  $(sha256sum "$OUT_FILE" | awk '{print $1}')"
echo "Install: sudo apt install ./${OUT_FILE#"$ROOT"/}"
if [[ -n "$DIRTY" ]]; then
    echo "WARNING: built with uncommitted changes; don't publish this one."
fi
