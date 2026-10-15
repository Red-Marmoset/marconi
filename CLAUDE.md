# rmvnc

Cross-platform VNC client. Out-of-tree una project; engine lives at `$UNA_ENGINE_PATH` (default `C:\code\engine`).

## Build
- App: `.\Build.ps1 [-Config Debug]` (Windows) / `./Build.sh` (macOS/Linux). Output `Build/RmVnc/<Config>/`.
- Tests: `.\Build.ps1 -Project Tests\<Name>\Project.json -Config Debug`, then run `Build\<Name>\Debug\<Name>.exe`; exit code = failure count.
- **Gotcha:** BuildTool records no header dependencies for MSVC builds (`.una/*.cache.json` shows `dependencies: []`), so editing any header does NOT recompile the .cpp files that include it, in the same module or others. Stale objects give silent class-layout mismatches (heap corruption, garbage reads). Always build with `-Clean` after touching a header.
- BuildTool compiles every .cpp under a module folder; `_Windows`/`_Mac`/`_Apple`/`_Linux`/`_Posix` suffixes are platform-filtered.

## Conventions (follow engine CODING_STANDARDS.md)
- PascalCase types/functions/files, camelCase locals, `m_` members, Allman braces, always braces, 4 spaces, 120 cols.
- Header: `// Copyright (c) 2026 Red Marmoset Studios Ltd.` + one-line purpose. `#pragma once`.
- `RmVncCore` must not include wx GUI headers; all platform code in suffixed files.
- Don't use `LOG_ERROR` for expected network failures (it breaks into the debugger in Debug).
- Engine changes (e.g. `Modules/Crypto`) are committed and pushed to the engine repo separately; only stage our own paths there.
