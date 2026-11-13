# Marconi: instructions for coding agents

Read this before changing anything. It applies to every agent working in this repository.

Cross-platform VNC client. It's an out-of-tree una project; the engine lives at `$UNA_ENGINE_PATH` (default
`C:\code\engine`).

The server component (Marconi Server) is planned in `SERVER_PLAN.md`: read it before working on the server, follow
its phases in order, and keep it current (decisions, measurements, finished steps).

Letting AI tools (Claude Code, Codex, Claude Desktop) drive Marconi is planned in `MCP_PLAN.md`, the same way: read
it before working on that, follow its phases in order, and keep it current.

## Working with the owner

- **Don't fill gaps with assumptions** (the owner's rule). When you notice you're assuming something about what the
  owner wants, how the code behaves or how a system works, treat it as a risk and remove it:
  - check it against something verifiable (the code, a test, a log, documentation, a real run) if you can;
  - otherwise ask the owner, before acting on it.
- Take requests as written. Don't widen or narrow their scope; if a request could mean more than one thing, ask
  which.

## Build and test

- **App:** `.\Build.ps1 [-Config Debug] [-Clean]` (Windows) or `./Build.sh` (macOS/Linux). Output goes to
  `Build/Marconi/<Config>/`.
- **Android:** `./gradlew assembleDebug` in `Apps/Android` (runs `Build.sh`/`Build.ps1` with `--platform Android`
  first; needs `ANDROID_HOME` or `ANDROID_NDK_HOME`). **iPad:** `bash Apps/iPad/GenerateProject.sh` (with
  `UNA_IOS_TEAM_ID` set for signing), then Xcode, or `bash Apps/iPad/Build.sh ["device name" | --simulator]` on a
  Mac, which names the destination (a bare `xcodebuild` may pick the Mac and fail to sign).
  `Tools/make_app_icon.py` also draws the iOS icon.
- **Tests:** `.\Build.ps1 -Project Tests\MarconiCoreTest\Project.json -Config Debug -Clean`, then run
  `Build\MarconiCoreTest\Debug\MarconiCoreTest.exe`. The exit code is the number of failures.
- **Building an engine test from here:** `-Project C:\code\engine\Tests\CryptoTest\Project.json` works too; output
  lands in this repo's `Build/`.
- Header edits rebuild their includers (needs the engine's BuildTool from 3d0ce07 or later; rebuild BuildTool if it's older, otherwise use `-Clean` after touching a header).
- BuildTool compiles every .cpp under a module folder. Files with `_Windows`, `_Mac`, `_Apple`, `_Linux` or
  `_Posix` suffixes are only compiled on that platform.
- **Version:** `MAJOR.MINOR.PATCH` in `Project.json` plus the commit count (`0.1.0.27`). The build scripts generate
  `Modules/MarconiCore/BuildVersion.h` (gitignored), included by `Version.cpp` and `MarconiUI/Marconi.rc`. Building
  without the scripts fails because it's missing.
- **When to bump the version** (the owner's rule; every agent follows it). Versions are
  `MAJOR.MINOR.PATCH.REVISION`; `MAJOR.MINOR.PATCH` lives in `Project.json`:
  - **PATCH** goes up with each bug fix or small improvement, in the same commit (0.2.0 → 0.2.1).
  - **MINOR** goes up when a significant new feature is finished, in the commit that finishes it, and PATCH goes back
    to 0 (0.2.3 → 0.3.0).
  - **MAJOR** goes up only when backwards compatibility breaks (saved settings older builds misread, addresses that
    change meaning, protocol behaviour others rely on) or when the owner decides. Don't bump it yourself: flag the
    break to the owner, who would rather stay on 0.x until the Marconi server component exists.
  - **1.0 is the owner's decision alone**, possibly with no compatibility break, once Marconi feels complete. Never
    make or propose that bump.
  - **REVISION** is the commit count, worked out from git by the build scripts. Never set it by hand.
- **Changelog:** every user-visible change adds a line under "Unreleased" in `CHANGELOG.md`, in the same commit. A
  version bump renames "Unreleased" to that version and starts a new, empty "Unreleased" section.
  - Keep entries terse and minimal (the owner's rule): one short line saying what changed, with no explanation of
    how or why, and no sub-bullets.
- **Public-facing text** (the changelog, README, the app's UI text and the installer) uses plain words anyone can
  follow (the owner's rule). No jargon-metaphors such as load-bearing, seam, cadence, substrate, spine, blast radius,
  anchored, byte-identical, "that's not nothing", surface (as a verb), land (for merge), plumbing, footgun or happy
  path, and no technical names a reader may not know (say what it does instead).
- **AI tools (MarconiMcp):** `MCP_PLAN.md`. The app's build also builds `Mcp/Project.json` and puts `MarconiMcp` and
  the engine's `Packaging/Apps.json` beside Marconi; the installer's AI tools page is `Installer/AiTools.nsh`, which
  includes the engine's `Packaging/Include/DetectApps.nsh`.
- **Installer:** `.\Package.ps1` builds Release and runs NSIS on `Installer/Marconi.nsi`, giving
  `Build/Installer/Marconi-<version>-win64-setup.exe`. It ships the MSVC runtime DLLs app-local. Its branding bitmaps (`Installer/Art/`) come from
  `python Tools/make_installer_art.py`; rerun it after changing the icon, accent colour or fonts.
- **macOS disk image:** `./Package.sh` builds Release, wraps the binary in `Marconi.app` (`Installer/macOS/Info.plist`,
  `Marconi.icns`, fonts in `Contents/Resources`), signs it ad-hoc or with `MARCONI_SIGN_IDENTITY`, and writes
  `Build/Installer/Marconi-<version>-macos-<arch>.dmg`. BuildTool itself makes a bare executable, not a bundle.
- **Linux package:** `./Package.sh` on Linux builds Release and makes a Debian package,
  `Build/Installer/Marconi-<version>-linux-<arch>.deb`: the app as `/usr/bin/marconi`,
  `/usr/bin/marconi-mcp` (MarconiMcp) with `Apps.json` in `/usr/share/marconi`, `Installer/Linux/marconi.desktop`, the
  icons from `Installer/Linux/icons` (also made by `make_app_icon.py`), the
  fonts in `/usr/share/marconi`. Its dependencies come from `dpkg-shlibdeps` (package `dpkg-dev`); its maintainer
  is the owner's address in `Package.sh` unless `MARCONI_DEB_MAINTAINER` overrides it.
- **Android app package:** `Package.ps1` and `Package.sh` end by running `Apps/Android/Package.ps1` or `.sh`, which
  builds the release app with Gradle, signed with the key in `MARCONI_ANDROID_KEYSTORE`, as
  `Build/Installer/Marconi-<version>-android.apk` when that key is set and a JDK, the Android SDK, an NDK and
  BuildTool are found, and says what is missing otherwise without failing the desktop package.
- **Signing variables:** the `signing` section of `Project.json` names the environment variable each signing input
  is read from (the Windows certificate, the macOS identity and notary profile, the Android keystore, its passwords
  and alias, the Linux maintainer). The packaging scripts and Gradle ask the engine's BuildTool for the names
  (`--signing-env`; `Tools/SigningSettings.sh` for bash), so rename a variable there, not in the scripts. Passwords
  are only ever named, never written to a file or printed. The engine's `Packaging/README.md` describes the section.
- **Manual testing:** `python Tools/fake_vnc_server.py --port 5999`, then connect to `127.0.0.1::5999`. The server
  logs every input event it receives. `Tools/fake_vencrypt_server.py` is the same for encrypted (VeNCrypt over TLS)
  connections, and `Tools/TlsStream_OpenSSL.cpp` is the reference TLS stream it was checked against (not built).

## Architecture

- `MarconiCore` has no wx headers. It uses wx's bundled zlib (`#include <zlib.h>`, symbols prefixed `wx_zlib_`),
  stb_image for JPEG, and the engine's `Crypto` module.
- `RemoteSession` (`RemoteSession.h`) is what the viewer drives; `Rfb::Client` (VNC), `Wire::Client` (the Marconi
  protocol) and `Rdp::Client` (Remote Desktop) implement it. The viewer picks one from the address's protocol or
  the probe. Each runs its protocol on a worker thread.
- The phone and tablet apps (`Apps/Android`, `Apps/iPad`) drive `RemoteSession` through `QuickConnect`
  (`MarconiCore/QuickConnect.h`), which picks the client from the address without the probe. The Android app is
  Kotlin over the JNI bridge in `Modules/MarconiAndroid`; the iOS app is one UIKit file, `Apps/iPad/Marconi_iOS.mm`.
  Neither has `MarconiUI` (wxWidgets has no usable port there). `MOBILE_PLAN.md` records the decisions and what
  is still unchecked; read it before working on either app.
- `Rfb::Client` runs the protocol on a worker thread.
  - Its `ClientListener` callbacks arrive on that thread; `ViewerFrame` passes them to the UI thread with
    `CallAfter`.
  - Decoders write into `Framebuffer` under its mutex, through a scratch `PixelBuffer`, and never hold the lock
    while reading the network.
  - Input senders are thread-safe.
- `OnCredentialsRequired` and `OnUntrustedCertificate` block the worker until the UI answers (`ViewerFrame::AskUser`).
  `ViewerFrame` releases it if the window closes.
- Encryption: `Net/Tls.h` is the TLS stream interface; `Net/Tls.cpp` returns nothing until a platform implementation
  exists (see README, "Encryption: what's done and what's left"). The handshake only chooses VeNCrypt when
  `IsTlsAvailable()` is true, so nothing changes for users until then except the "not encrypted" status.
- The client always requests 32bpp little-endian 0x00RRGGBB pixels. ZRLE's CPIXEL is B,G,R; Tight's TPIXEL is
  R,G,B.

## Conventions (follow the engine's CODING_STANDARDS.md)

- PascalCase for types, functions and files; camelCase for locals; `m_` for members. Allman braces, always braces,
  4 spaces, 120 columns.
- File header: `// Copyright (c) 2026 Red Marmoset Studios Ltd.` plus a one-line purpose. Use `#pragma once`.
- Don't use `LOG_ERROR` for expected network failures: it breaks into the debugger in Debug builds.
- Engine changes are committed and pushed to the engine repo separately (for example `Modules/Crypto` and
  `External/wxwidgets.manifest`). The user often has local work in progress there, so only stage your own paths
  and preserve each file's line endings (the manifests use CRLF).
