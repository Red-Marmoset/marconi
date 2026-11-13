#!/usr/bin/env bash
# Apps/Android/Package.sh - Build the Android app for release: Build/Installer/Marconi-<version>-android.apk
#
# Package.sh runs this after the macOS or Linux package, and Package.ps1 has a PowerShell twin. It needs a JDK,
# the Android SDK (ANDROID_HOME or ANDROID_SDK_ROOT, else where Android Studio installs it) with an NDK in its
# ndk folder or at ANDROID_NDK_HOME, and the Una engine at UNA_ENGINE_PATH with its BuildTool bootstrapped. When
# any of those is missing, or no release key is set, it says so and exits with 0, so the desktop package still
# gets made.
#
# Signing: with the release key these name. They are the environment variables Project.json's "signing" section
# names for Android; change the names there. Without MARCONI_ANDROID_KEYSTORE the app is not packaged (for a test
# build signed with the debug key, run ./gradlew assembleDebug here instead):
#   MARCONI_ANDROID_KEYSTORE=<path to the keystore>
#   MARCONI_ANDROID_KEYSTORE_PASSWORD=<its password>
#   MARCONI_ANDROID_KEY_ALIAS=<the key's alias>          optional; "marconi" by default
#   MARCONI_ANDROID_KEY_PASSWORD=<the key's password>    optional; the keystore's by default
#
# Usage:
#   bash Apps/Android/Package.sh [--clean]
#     --clean   clean the app and the native library first

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="$ROOT/Apps/Android"
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        *) echo "ERROR: unknown option $arg (see the usage at the top of Apps/Android/Package.sh)." >&2; exit 1 ;;
    esac
done

skip() {
    echo "[Package.sh] Android app not built: $1"
    exit 0
}

# BuildTool builds the native library, and says which environment variables Project.json names for the release key.
source "$ROOT/Tools/SigningSettings.sh"
find_buildtool
ENGINE="${UNA_ENGINE_PATH:-$ROOT/../engine}"
[[ -n "$BUILDTOOL" ]] || skip "no BuildTool under $ENGINE/Build/BuildTool (bootstrap the engine, or set UNA_ENGINE_PATH)."
export UNA_ENGINE_PATH="$ENGINE"

# A release is signed with the release key, so there is nothing to package without one. Only the password's
# variable is named here; Gradle reads what it holds.
KEYSTORE_ENV="$(signing_env android.keystore)"
KEYSTORE_PASSWORD_ENV="$(signing_env android.keystorePassword)"
KEYSTORE="${!KEYSTORE_ENV:-}"
[[ -n "$KEYSTORE" ]] || skip "$KEYSTORE_ENV isn't set (the release key; see the top of Apps/Android/Package.sh)."
[[ -f "$KEYSTORE" ]] || { echo "ERROR: $KEYSTORE_ENV=$KEYSTORE doesn't exist." >&2; exit 1; }
[[ -n "${!KEYSTORE_PASSWORD_ENV:-}" ]] || { echo "ERROR: $KEYSTORE_PASSWORD_ENV isn't set." >&2; exit 1; }

# The other tools: a JDK, the SDK with a platform and build tools, and an NDK.
if [[ -n "${JAVA_HOME:-}" && -x "$JAVA_HOME/bin/java" ]]; then
    JAVA="$JAVA_HOME/bin/java"
else
    JAVA="$(command -v java || true)"
fi
[[ -n "$JAVA" ]] || skip "no Java found (install a JDK, 17 or newer, or set JAVA_HOME)."

SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [[ -z "$SDK" ]]; then
    case "$(uname -s)" in
        Darwin) SDK="$HOME/Library/Android/sdk" ;;
        *) SDK="$HOME/Android/Sdk" ;;
    esac
fi
[[ -d "$SDK/platforms" && -d "$SDK/build-tools" ]] || skip "no Android SDK at ${SDK} (install Android Studio or the command-line tools, or set ANDROID_HOME)."
export ANDROID_HOME="$SDK"

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -z "$NDK" && -d "$SDK/ndk" ]]; then
    NDK="$(ls -d "$SDK"/ndk/*/ 2>/dev/null | sort -V | tail -n 1)"
    NDK="${NDK%/}"
fi
[[ -n "$NDK" && -d "$NDK/toolchains/llvm" ]] || skip "no Android NDK (install one from the SDK manager, or set ANDROID_NDK_HOME)."
export ANDROID_NDK_HOME="$NDK"

# aapt, from the newest build tools, reads the version the app was stamped with.
AAPT="$(ls "$SDK"/build-tools/*/aapt 2>/dev/null | sort -V | tail -n 1)"
[[ -x "$AAPT" ]] || skip "no aapt in $SDK/build-tools."

SIGNING="release key $KEYSTORE"

echo "[Package.sh] Android: SDK=$SDK"
echo "[Package.sh] Android: NDK=$NDK"
echo "[Package.sh] Android: Signing=$SIGNING"

cd "$APP"
GRADLE_ARGS=(--no-daemon -q)
if [[ $CLEAN -eq 1 ]]; then
    rm -rf "$ROOT/Build/Marconi/Android" "$ROOT/Build/Marconi/Android-"* "$ROOT/Build/Intermediate/Marconi/Android" "$ROOT/Build/Intermediate/Marconi/Android-"*
    ./gradlew "${GRADLE_ARGS[@]}" clean
fi
./gradlew "${GRADLE_ARGS[@]}" assembleRelease

APK="$APP/app/build/outputs/apk/release/app-release.apk"
[[ -f "$APK" ]] || { echo "ERROR: Gradle made no $APK." >&2; exit 1; }
VERSION="$("$AAPT" dump badging "$APK" | sed -n "s/.*versionName='\([^']*\)'.*/\1/p" | head -n 1)"
[[ -n "$VERSION" ]] || { echo "ERROR: couldn't read the version from $APK." >&2; exit 1; }

INSTALLER_DIR="$ROOT/Build/Installer"
mkdir -p "$INSTALLER_DIR"
OUT_FILE="$INSTALLER_DIR/Marconi-$VERSION-android.apk"
cp "$APK" "$OUT_FILE"
if command -v sha256sum > /dev/null; then
    HASH="$(sha256sum "$OUT_FILE" | awk '{print $1}')"
else
    HASH="$(shasum -a 256 "$OUT_FILE" | awk '{print $1}')"
fi
echo ""
echo "Android app: $OUT_FILE"
echo "SHA256:      $HASH"
