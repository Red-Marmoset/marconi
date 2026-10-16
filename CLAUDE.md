# Marconi

Cross-platform VNC client. It's an out-of-tree una project; the engine lives at `$UNA_ENGINE_PATH` (default
`C:\code\engine`).

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
- **When to bump the version** (the owner's rule; every agent follows it):
  - A big feature (protocol detection, file transfer and the like) bumps MINOR in `Project.json` (0.2.0 → 0.3.0),
    in the same change that lands it. Small fixes leave it alone.
  - A change that breaks backwards compatibility (saved settings older builds misread, addresses that change meaning,
    protocol behaviour others rely on) would bump MAJOR. Don't: stay on 0.x until the Marconi server component
    exists. Flag the break to the owner instead.
  - **1.0 is the owner's decision alone**, possibly with no compatibility break, once Marconi feels complete. Never
    make or propose that bump.
- **Changelog:** every user-visible change adds a line under "Unreleased" in `CHANGELOG.md`, in the same commit. A
  version bump renames "Unreleased" to that version and starts a new, empty "Unreleased" section.
  - Keep entries terse and minimal (the owner's rule): one short line saying what changed, with no explanation of
    how or why, and no sub-bullets.
- **Installer:** `.\Package.ps1` builds Release and runs NSIS on `Installer/Marconi.nsi`, giving
  `Build/Installer/Marconi-<version>-win64-setup.exe`. It ships the MSVC runtime DLLs app-local. Its branding bitmaps (`Installer/Art/`) come from
  `python Tools/make_installer_art.py`; rerun it after changing the icon, accent colour or fonts.
- **macOS disk image:** `./Package.sh` builds Release, wraps the binary in `Marconi.app` (`Installer/macOS/Info.plist`,
  `Marconi.icns`, fonts in `Contents/Resources`), signs it ad-hoc or with `MARCONI_SIGN_IDENTITY`, and writes
  `Build/Installer/Marconi-<version>-macos-<arch>.dmg`. BuildTool itself makes a bare executable, not a bundle.
- **Manual testing:** `python Tools/fake_vnc_server.py --port 5999`, then connect to `127.0.0.1::5999`. The server
  logs every input event it receives.

## Architecture

- `MarconiCore` has no wx headers. It uses wx's bundled zlib (`#include <zlib.h>`, symbols prefixed `wx_zlib_`),
  stb_image for JPEG, and the engine's `Crypto` module.
- `Rfb::Client` runs the protocol on a worker thread.
  - Its `ClientListener` callbacks arrive on that thread; `ViewerFrame` passes them to the UI thread with
    `CallAfter`.
  - Decoders write into `Framebuffer` under its mutex, through a scratch `PixelBuffer`, and never hold the lock
    while reading the network.
  - Input senders are thread-safe.
- `OnCredentialsRequired` blocks the worker until the UI answers. `ViewerFrame` releases it if the window closes.
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
