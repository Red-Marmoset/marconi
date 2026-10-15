# rmvnc

Cross-platform VNC client. It's an out-of-tree una project; the engine lives at `$UNA_ENGINE_PATH` (default
`C:\code\engine`).

## Build and test

- **App:** `.\Build.ps1 [-Config Debug] [-Clean]` (Windows) or `./Build.sh` (macOS/Linux). Output goes to
  `Build/RmVnc/<Config>/`.
- **Tests:** `.\Build.ps1 -Project Tests\RmVncCoreTest\Project.json -Config Debug -Clean`, then run
  `Build\RmVncCoreTest\Debug\RmVncCoreTest.exe`. The exit code is the number of failures.
- **Building an engine test from here:** `-Project C:\code\engine\Tests\CryptoTest\Project.json` works too; output
  lands in this repo's `Build/`.
- **Gotcha:** BuildTool records no header dependencies for MSVC builds (`.una/*.cache.json` shows
  `dependencies: []`). Editing any header does NOT recompile the .cpp files that include it, in the same module or
  in others. The stale objects cause silent class-layout mismatches (heap corruption, garbage reads). Always build
  with `-Clean` after touching a header.
- BuildTool compiles every .cpp under a module folder. Files with `_Windows`, `_Mac`, `_Apple`, `_Linux` or
  `_Posix` suffixes are only compiled on that platform.
- **Manual testing:** `python Tools/fake_vnc_server.py --port 5999`, then connect to `127.0.0.1::5999`. The server
  logs every input event it receives.

## Architecture

- `RmVncCore` has no wx headers. It uses wx's bundled zlib (`#include <zlib.h>`, symbols prefixed `wx_zlib_`),
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
