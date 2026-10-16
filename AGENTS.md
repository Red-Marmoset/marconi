# Marconi: instructions for coding agents

Read this before changing anything. It applies to every agent working in this repository.

Cross-platform VNC client. It's an out-of-tree una project; the engine lives at `$UNA_ENGINE_PATH` (default
`C:\code\engine`).

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
- **Installer:** `.\Package.ps1` builds Release and runs NSIS on `Installer/Marconi.nsi`, giving
  `Build/Installer/Marconi-<version>-win64-setup.exe`. It ships the MSVC runtime DLLs app-local. Its branding bitmaps (`Installer/Art/`) come from
  `python Tools/make_installer_art.py`; rerun it after changing the icon, accent colour or fonts.
- **macOS disk image:** `./Package.sh` builds Release, wraps the binary in `Marconi.app` (`Installer/macOS/Info.plist`,
  `Marconi.icns`, fonts in `Contents/Resources`), signs it ad-hoc or with `MARCONI_SIGN_IDENTITY`, and writes
  `Build/Installer/Marconi-<version>-macos-<arch>.dmg`. BuildTool itself makes a bare executable, not a bundle.
- **Manual testing:** `python Tools/fake_vnc_server.py --port 5999`, then connect to `127.0.0.1::5999`. The server
  logs every input event it receives. `Tools/fake_vencrypt_server.py` is the same for encrypted (VeNCrypt over TLS)
  connections, and `Tools/TlsStream_OpenSSL.cpp` is the reference TLS stream it was checked against (not built).

## Architecture

- `MarconiCore` has no wx headers. It uses wx's bundled zlib (`#include <zlib.h>`, symbols prefixed `wx_zlib_`),
  stb_image for JPEG, and the engine's `Crypto` module.
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
