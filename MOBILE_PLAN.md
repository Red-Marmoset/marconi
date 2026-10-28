# Marconi on phones and tablets: plan and decisions

Marconi runs on Android (`Apps/Android`) and on iPad and iPhone (`Apps/iPad`). This file records how, what was
decided along the way, and what is still unchecked. Keep it current.

## How it is built

- The viewer core (`MarconiCore`, with the engine's `Crypto`, mbedTLS and libssh2) is portable C++ and builds for
  both platforms unchanged. The desktop front end (`MarconiUI`) is wxWidgets, which has no usable iOS port and no
  Android port, so each mobile app has a front end of its own over `RemoteSession`.
- **Android:** the Una BuildTool compiles the core and the JNI bridge module (`Modules/MarconiAndroid`) with the
  NDK's clang (`--platform Android --abi arm64-v8a`) into `libMarconi.so`; Gradle packages the Kotlin app around
  it. The engine gained the Android target for this (engine `Documentation/AndroidProjects.md`).
- **iPad and iPhone:** the Una BuildTool generates an Xcode project (`--generate-xcode --platform iOS`) from
  `Apps/iPad/Project.json`, the way the Semiprimes iPad helper is built; Xcode compiles it on a Mac. The engine's
  generator gained the ability to compile external libraries (mbedTLS, libssh2) and to pass `-l` libraries.
- `MarconiCore/QuickConnect` is the shared "address to session" step for both apps: VNC for bare and `vnc://`
  addresses, VNC through SSH for `ssh://`, the Marconi protocol for `marconi://`. No probe: the desktop viewer's
  probe and fallbacks are not used on the phones yet.
- zlib comes from the system on both platforms (`-lz`), not from wxWidgets as on the desktop.

## Decisions

- Android is built by BuildTool directly, not by a generated Gradle native build: it keeps Una as the only C++
  build system, as on the desktop, and Gradle only packages. The engine owner's rule against CMake was followed.
- The Android front end is Kotlin with a JNI bridge in a Marconi module; the iOS front end is one Objective-C++
  file in `Apps/iPad`, like the Semiprimes helper. Neither duplicates the desktop UI; both are deliberately small.
- Minimum versions: Android 7 (API 24), iOS and iPadOS 15. The iOS project targets iPad and iPhone.
- Gestures on both: tap = click, double-tap = double-click, touch and hold = right click, (double-)tap and hold
  = drag, one finger moves the pointer, two fingers move and zoom the picture. Scroll wheel has no gesture yet.
- Trusted certificates are remembered per address in the app's preferences, as the desktop remembers them; SSH
  host keys are asked about each time (not yet remembered).
- The version went to 0.8.0: a new platform is a significant feature, even before a device test.

## What is checked and what is not

- Checked here (Linux): the engine's BuildTool tests (Xcode generation, Android cross-compilation), the core and
  bridge cross-compiled with NDK r27 into a 16 KiB aligned `libMarconi.so`, the Kotlin app compiled and packaged
  into a debug APK, the iOS project generated with every source it needs and none of the desktop ones.
- Not checked yet: the apps on a device. Nobody has tapped on either. Expect the first run on a phone or an iPad
  to turn up small things in `RemoteView.kt`, `ViewerActivity.kt` and `Marconi_iOS.mm` (gesture feel, keyboard
  handling, picture scaling). The iOS code has not been compiled with the iOS SDK either.
- Not in the apps: Remote Desktop, finding computers on the network, saved connections, file transfer, the
  clipboard towards the other computer, the local cursor shape, wake-on-LAN, the update check.

## Next steps

1. Install the Android APK on a phone and the iOS app on an iPad; connect to a VNC server on the same Wi-Fi; fix
   what the first session shows.
2. Scroll wheel gesture (two-finger drag when the picture isn't zoomed), and the clipboard towards the server.
3. Saved connections, shared with the desktop format where it makes sense.
4. Remote Desktop through `QuickConnect` once the desktop size question has an answer for a touch screen.
