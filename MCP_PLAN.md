# Marconi for AI tools: the plan

This document plans a way for AI tools (Claude Code, Codex, Claude Desktop) to drive Marconi: connect to a
computer, take screenshots, move the mouse, type and use the clipboard, while the user can watch in a normal
Marconi viewer. It works the way `SERVER_PLAN.md` does:

- Work through the phases in order unless the owner reorders them. Each step is about one commit and each phase
  has a "done when" check that can be run by hand. Every phase leaves Marconi releasable.
- The work happens on a branch (currently `claude/sweet-bell-nobab4`), merged into `main` by the owner.
- The rules in `AGENTS.md` apply throughout: no assumptions (verify or ask the owner), the version and changelog
  rules, plain words in anything users read. Engine changes are committed to the engine repo separately.
- Keep this file current: decisions, findings, finished steps. Anything still undecided is in section 7; nothing
  that depends on an undecided point is built until it's decided.

## Progress

- **Phase 0** done 2026-11-13.
- **Phase 1** done 2026-11-13 on `claude/sweet-bell-nobab4`, built and tested on Linux against the real engine:
  `Modules/MarconiMcp` (messages for both MCP revisions, PNG writer, scaler, the connection, `connect`,
  `disconnect`, `status`, `screenshot`), `Modules/MarconiMcpApp` (the program, standard output kept for messages),
  `Mcp/Project.json`, and `Tests/MarconiCoreTest/McpTests.cpp` (all pass). Checked by hand: Claude Code 2.1.323,
  given MarconiMcp with `--mcp-config`, connected to `Tools/fake_vnc_server.py`, took a screenshot and described it
  correctly. Not yet built on Windows. Logging to a file (section 3.3) moved to Phase 2.
- **Phase 2** written and tested 2026-11-13 (same branch): `click`, `move`, `drag`, `scroll`, `type_text`, `key`,
  `get_clipboard`, `set_clipboard`, `wait_for_change`, `--read-only`, the log file, and the README section.
  MarconiCoreTest checks the exact pointer, key and clipboard messages each tool sends (all pass). By hand,
  Claude Code clicked into a terminal on x11vnc (Xvfb, this machine only) and typed commands that ran, while a
  Marconi viewer on a second, view-only port watched. Two things found on the way (findings 10 and 11): screenshots
  now wait for the screen's answer to input and ask for the whole desktop each time, and x11vnc stopped sending
  Marconi pictures because of a loop in `Rfb::Client` (section 7, decision 4).
- **Phase 2 done** 2026-11-13: the loop is fixed in `Rfb::Client` as the owner chose (decision 4 (b), 0.10.5, with a
  test in `DesktopSizeTests`). On x11vnc, three connections in a row then all got a picture (before, the second got
  none), and the session log held two size notices instead of thousands. Claude Code clicked into the terminal,
  typed `echo` with a random word, and its one screenshot straight after showed the command and its output, as did
  the watching viewer.

- **Phase 4 (engine) done before Phase 3** 2026-11-13, so finding the AI tools is written once: on the engine's
  `claude/sweet-bell-nobab4` branch (7038f35), not yet in its `main`. New engine modules `Process` (running a
  program with a time limit and collecting its output; `.cmd` files through `cmd.exe`) and `AppDetect` (reads
  `Packaging/Apps.json`: commands on PATH and on the PATH saved in the registry, files, folders, Windows'
  installed-programs list). `Packaging/Include/DetectApps.nsh` is made from `Apps.json` by `AppDetect` and checked
  by AppDetectTest, rather than by `PackageProject.ps1` as first planned: PowerShell couldn't be run here, and a
  committed file needs no step at packaging time. `PackageProject.ps1` adds `Packaging/Include` to makensis's
  include folders and signs every `.exe` it ships (Phase 5 step 1); that change hasn't been run. Tested on Linux,
  and the Windows code under Wine (cross-compiled ProcessTest and AppDetectTest, and a 64-bit build of
  `Tests/AppDetectTest/DetectAppsTest.nsi`) with stand-in programs and registry entries.
- **Phase 3 done** 2026-11-13: `--register`, `--unregister`, `--status` (one JSON line per tool) and
  `--show-errors` (a message box on Windows, for Setup). Checked with the real Claude Code 2.1.323 and Codex 0.169.0
  (each in a throwaway home folder): found, added, reported as pointing at this program, removed. Claude Code
  refuses a name that's taken, so the old entry is removed first; Codex replaces it. Marconi now needs the engine
  branch above until it's merged.

- **Phase 5 done in code** 2026-11-13 (0.11.0): the app's build (`Build.ps1`, `Build.sh`) also builds MarconiMcp and
  puts it and the engine's `Apps.json` beside Marconi; `Project.json` ships both (`buildFiles`); the page is
  `Installer/AiTools.nsh`, included by `Marconi.nsi` (silent installs: `/AITOOLS=`); the uninstaller removes what it
  added, recorded as `AiTools` in the uninstall entry. Tested under Wine with a 64-bit build of the real
  `Marconi.nsi`, a stand-in ShellExecAsUser plug-in and a stand-in MarconiMcp.exe that record what they're asked:
  a silent install for one user registered with the one tool installed (waiting for it), uninstalling unregistered
  it first, an install for everyone went through ShellExecAsUser with `--show-errors`, and the page showed and
  worked. Not yet run: `Package.ps1`, `Build.ps1` and `PackageProject.ps1` (PowerShell isn't available here), the
  real plug-in, and a real Windows PC.

- **Phase 6 done** 2026-11-13 (0.11.1): Settings > "AI tools..." (`Modules/MarconiUI/AiToolsDialog.cpp`) lists
  Claude Code, Codex and Claude Desktop with their state and Add/Remove, running the MarconiMcp beside Marconi
  (`--status`, `--register`, `--unregister`, with the new `--quiet` so only the answer comes back) on a worker
  thread. Placed in Settings rather than the menus (the plan asked to show the owner first: easy to move). Checked
  on Linux under Xvfb with the real Claude Code and Codex: both found, added, shown as "Can use Marconi", removed.

- **Phase 7 done** 2026-11-13 (0.11.2): the Linux package has `/usr/bin/marconi-mcp` and
  `/usr/share/marconi/Apps.json` (dpkg-shlibdeps covers both programs). Built with `Package.sh` here, installed with
  apt: `marconi-mcp --status` found Claude Code and Codex, `--register claude-code` added `/usr/bin/marconi-mcp`, and
  `claude mcp get` reported it connected. `Package.sh` also puts MarconiMcp in `Marconi.app/Contents/MacOS` (signed
  before the app, without `--deep`) and `Apps.json` in `Contents/Resources` for Phase 9; that part needs a Mac.

- **Phase 8 done in code** 2026-11-13 (0.11.3): `Installer/Mcpb/manifest.json` (manifest 0.3, a binary server,
  the password as a hidden setting passed in `MARCONI_PASSWORD`); `Build.ps1` makes `marconi.mcpb` beside Marconi
  on Windows (written entry by entry with .NET), `Tools/make_mcpb.sh` does it on macOS from the signed MarconiMcp
  (`Package.sh`), and the Windows package ships it. Anthropic's `mcpb` tool 2.1.2 validated the manifest and
  unpacked a bundle `make_mcpb.sh` made here (a zip without folder entries, which `mcpb unpack` can't handle), and
  the program inside answered as an MCP server. Not checked: installing it in Claude Desktop (not available here);
  whether Claude Desktop finds `server/MarconiMcp.exe` as written (the spec's own Windows example names the `.exe`);
  and on Windows the bundled copy is made before the package signs MarconiMcp.exe, so it's unsigned.

- **Phase 9 written, not run** 2026-11-13: `Package.sh` puts MarconiMcp, `Apps.json` and `marconi.mcpb` in
  `Marconi.app` and signs MarconiMcp before the bundle; nothing of it has been run on a Mac.

**Testing on Windows:** `TestAiTools.bat`, run from any Marconi checkout, puts both branches in their own folders
(git worktrees under `%LOCALAPPDATA%\MarconiAiToolsTest`, the engine one linked to your engine's modules and
BuildTool), builds and runs the tests, then walks through Claude Code, the AI tools window and the installer;
`TestAiTools.bat clean` removes the copies. Its flow was run under Wine with a stand-in git.

**To ship** (as of 0.11.3): merge the engine's `claude/sweet-bell-nobab4` into its `main` (Marconi needs its
`Process`, `AppDetect`, `Apps.json`, `Include/DetectApps.nsh` and the `PackageProject.ps1` change), then on a
Windows PC run `Build.ps1`, MarconiCoreTest and `Package.ps1` (none of the PowerShell or MSVC builds has been run;
the Windows code has only been compiled with MinGW and run under Wine), install the result, and try the AI tools
page, the window, Claude Code, Codex and Claude Desktop for real. On a Mac, `Package.sh` and the same checks.

## 1. Decisions (owner, agreed in chat 2026-11-13)

1. **MCP is the way in.** Claude's "connectors" are MCP servers, and Claude Code, Codex and Claude Desktop all take
   local MCP servers. A command-line tool plus instructions would only suit Claude Code and would reconnect on
   every command.
2. **A separate program, `MarconiMcp`,** that the AI tool starts and talks to over standard input and output (MCP's
   "stdio" transport). It connects to computers itself, with no window. Nothing keeps running between sessions;
   there is no port and no token. The core is written so an HTTP mode could be added later without a rewrite.
3. **Watching is done with a normal Marconi viewer** connected to the same computer. MarconiMcp doesn't open
   windows or talk to the viewer.
4. **It ships with Marconi on every platform:** beside `Marconi.exe` on Windows, inside `Marconi.app` on macOS, as
   `/usr/bin/marconi-mcp` in the Linux package.
5. **Windows installer:** a page after installing, "Connect Marconi to your AI tools", with a tick box for each
   supported tool it finds installed, skipped when it finds none. The uninstaller removes what it registered.
6. **The app gets an "AI tools" window** on every platform, with the same detection and an Add/Remove button per
   tool. It covers macOS and Linux (whose packages have no step for tick boxes) and Windows users who didn't tick
   the box.
7. **Supported first:** Claude Code, Codex, and Claude Desktop (as a `.mcpb` desktop extension). **opencode and Pi
   come later, together.**
8. **Detecting installed apps lives in the engine** as a general feature any una installer or app can use (Blender,
   Ollama and so on, not just AI tools): one data file listing the apps and how to find each, an NSIS include
   generated from it for installers, and a small C++ module that reads it for apps. Both, not just the include.
9. **macOS waits** until it can be tested on a Mac. The code stays cross-platform; the macOS packaging and signing
   steps are done last.

## 2. What exists and what it gives us

Checked against `main` at 0.9.7 (757f76f) and the engine at b5a405e.

- `QuickConnect()` (`Modules/MarconiCore/QuickConnect.h`) connects straight from an address with no probe: VNC, the
  Marconi protocol and VNC over SSH, with options for user name, password, a trusted certificate, picture quality
  and an SSH tunnel. It turns down Remote Desktop addresses, so RDP isn't in the first version.
- `RemoteSession` (`Modules/MarconiCore/RemoteSession.h`): the picture (`GetFramebuffer()`, under its mutex),
  pointer and key events, clipboard, displays, desktop resizing, traffic counts. Every method is safe from any
  thread. `SessionListener` callbacks arrive on the connection thread: `OnFramebufferUpdated` (when the picture
  changes), `OnCursorShape` (the cursor, which isn't in the picture when the server sends it separately),
  `OnDisconnected` (exactly once), and three that block the connection until answered: credentials, an untrusted
  certificate, an untrusted SSH host key.
- `Keysyms.h`: `FromUnicode` for typing text, and named keysyms for the modifiers (`Shift_L`, `Control_L`,
  `Alt_L`, `Super_L`, `Meta_L`, AltGr).
- nlohmann::json, already used by `Update.cpp` (MarconiCoreTest lists `json` as an external dependency).
- wx's bundled zlib (`deflate`), enough for a small PNG writer. MarconiCore has none; the viewer saves PNGs through
  wxImage, which MarconiMcp can't use.
- VNC connects in shared mode by default (`shared = true`, `Rfb/Client.h`), so a viewer can watch alongside.
- `Server/Project.json` + `Modules/MarconiServerApp` show how a second console program sits in this repo;
  `Build.ps1 -Project <file>` builds any project.
- Packaging (engine `Packaging/PackageProject.ps1`): builds the main program only, stages it with
  `package.files` (paths relative to the project) and `package.buildFiles`, signs **only `<name>.exe`**, then runs
  makensis on Marconi's own `Installer/Marconi.nsi`. Nothing passes an engine include folder to makensis yet.
  `Project.json`'s `signing` section names the signing variables; `Package.sh` reads them through
  `Tools/SigningSettings.sh`.
- The installer installs for one user (not elevated) or for everyone (it starts itself again elevated). It already
  runs things as the signed-in user with the ShellExecAsUser plug-in, and finds the console user's name
  (`GetConsoleUser`). The finish page's two tick boxes are both used, hence a page of its own.

## 3. Design

### 3.1 Programs and modules

- `Modules/MarconiMcp` (library, no wx): the MCP message loop, the tools, the screenshot code and PNG writer, the
  key-name table, and registering with each AI tool. MarconiUI links it for the "AI tools" window, so the window
  and the program share one copy of the registration code.
- `Modules/MarconiMcpApp` (console): `main` and the command line. Project file `Mcp/Project.json` (as
  `Server/Project.json`), output `Build/MarconiMcp/<Config>/MarconiMcp[.exe]`.
- Tests go in `Tests/MarconiCoreTest` (new `Mcp*Tests.cpp` files) unless they need their own project.
- Command line:
  - no arguments: run as an MCP server on standard input and output;
  - `--read-only`: offer only the tools that look (section 3.4);
  - `--register <tool>`, `--unregister <tool>`: `claude-code`, `codex` or `claude-desktop`; exit code 0 on success;
  - `--status`: one JSON line per tool (found or not, where, registered or not), for the app and the installer;
  - `--version`.
- The name it registers under is `marconi` in every tool.

### 3.2 Talking MCP

- Messages are JSON-RPC 2.0, one per line, on standard input and output (MCP's stdio transport, the same in both
  revisions below).
- Two protocol revisions at once (finding 8), chosen per request:
  - **2026-07-28** (current): no handshake. Each request carries `io.modelcontextprotocol/protocolVersion` and
    `io.modelcontextprotocol/clientCapabilities` in `params._meta`. An unsupported version gets error -32022 with
    `data.supported` and `data.requested`; missing capabilities get -32602. Results carry `resultType: "complete"`
    and our `io.modelcontextprotocol/serverInfo` in `_meta`. `server/discover` answers with the supported
    versions, capabilities and instructions. `ping` and `initialize` are gone in this revision.
  - **2025-11-25, 2025-06-18, 2025-03-26** (older clients): `initialize` first; the answer is the client's version
    if it's one of these, otherwise 2025-11-25. `ping` answers `{}`.
- Handled in both: `tools/list`, `tools/call`. Notifications (`initialized`, `cancelled`) need no answer; calls are
  handled one at a time and finish on their own. Anything else gets -32601.
- Tool failures (no connection, bad arguments, connection lost) come back as a normal result with `isError: true`
  and a plain sentence, as the spec asks, not as protocol errors. An unknown tool is -32602.
- Images come back as `{"type": "image", "data": <base64 PNG>, "mimeType": "image/png"}`, followed by a text item
  with the facts the AI needs (size, scale, which area).
- Claude Code 2.1.323 uses 2026-07-28 (finding 8). Still to check: Codex and Claude Desktop.

### 3.3 Keeping standard output clean

Standard output must carry only MCP messages; one stray line breaks the connection. The engine's `Log` writes info
and verbose messages to standard output (finding 1), and MarconiCore logs from the connection code. So the first
thing `main` does in server mode is keep a private copy of the real standard output for MCP messages and point the
process's standard output at standard error (`dup`/`dup2`; `_dup`/`_dup2` on Windows, with binary mode so `\n`
isn't turned into `\r\n`). Anything else that prints then goes to standard error, which the AI tools keep as logs.
It also logs to a file beside the viewer's logs. No engine change needed.

### 3.4 Tools offered to the AI

One connection at a time (section 7). Coordinates are in the last screenshot's pixels unless the call says otherwise.

| Tool | Arguments | What it does |
|---|---|---|
| `connect` | `address`, optional `username`, `password`, `trust_fingerprint`, `quality` | Connects; returns size, displays, encryption |
| `disconnect` | | Ends the connection |
| `status` | | Connected or not, address, size, displays, encryption, last screenshot's scale |
| `screenshot` | optional `area` (x, y, width, height in desktop pixels), `max_size`, `settle_ms` | PNG of the desktop or an area |
| `click` | `x`, `y`, optional `button` (left/middle/right), `count` (1-3), `modifiers` | Clicks |
| `move` | `x`, `y` | Moves the pointer |
| `drag` | `from_x`, `from_y`, `to_x`, `to_y`, optional `button`, `modifiers` | Press, move in steps, release |
| `scroll` | `x`, `y`, `direction` (up/down/left/right), optional `amount` | Wheel clicks |
| `type_text` | `text` | Types the text as key presses |
| `key` | `keys` (e.g. `ctrl+alt+Delete`, `cmd+space`, `Return`), optional `repeat` | Presses a combination |
| `get_clipboard` | | The remote clipboard's text |
| `set_clipboard` | `text` | Sets the remote clipboard |
| `wait_for_change` | optional `timeout_ms`, `area` | Returns when the picture changes, or on timeout |

`--read-only` offers `connect`, `disconnect`, `status`, `screenshot`, `get_clipboard` and `wait_for_change`.

### 3.5 Connections, screenshots and input

- **Connecting:** `QuickConnect` with a listener that records updates, the cursor and disconnects. The blocking
  questions are answered without a person:
  - credentials: the `password` argument, else the `MARCONI_PASSWORD` environment variable, else turned down with
    "this computer needs a password";
  - untrusted certificate or SSH host key: turned down, and the result gives the fingerprint; calling `connect`
    again with `trust_fingerprint` set to it accepts it. Nothing is remembered between runs.
- **Scaling:** by default a screenshot fits within 1568 pixels on each side and 1.15 megapixels, which every current
  Claude model takes as it is; `max_size` allows up to 2576 for models that take more (finding 4). Scaling is an
  area average (sharper text than nearest-pixel). The scale is kept so the input tools convert back.
- **A current picture:** until the first picture after connecting or a resize, ask for the whole desktop again
  every 700 ms (as the viewer does, up to 3 s). Then every screenshot asks for the whole desktop and waits up to
  1.5 s for it, so it's current even on a server that has stopped reporting changes (finding 10).
- **Settling:** before taking the picture, wait until nothing has happened for `settle_ms` (default 300), counted
  from the last picture update or the last input sent, whichever is later (straight after a click the screen hasn't
  answered yet); up to 2 seconds (or twice `settle_ms`). The text item says if it gave up waiting.
- **Cursor:** drawn into the screenshot from the last `OnCursorShape` and the last pointer position we sent.
- **PNG:** 8-bit RGB, zlib `deflate` at a fast level, row filter "sub"; tested by decoding with stb_image.
- **Mouse:** the VNC button mask: 1 left, 2 middle, 4 right, 8/16 wheel up/down, 32/64 wheel left/right. A click
  is press then release; a double click is two clicks with a short pause; a drag moves in about ten steps.
- **Keys:** a table of names (`ctrl`, `shift`, `alt`, `cmd`/`super`/`win`, `Return`, `Tab`, `Escape`, `F1`-`F24`,
  arrows, `Delete`, `BackSpace`, `Home`, `End`, `Page_Up`, `Page_Down`, and any single character). Modifiers go
  down in order, the key is pressed and released, modifiers come up in reverse. `type_text` sends each character's
  keysym from `FromUnicode`, with `Return` for new lines.
- **Clipboard:** `OfferClipboard`/`RequestServerClipboard` when the server has the extended clipboard, else
  `SendClientCutText` and `OnServerCutText` (Western European characters only; the result says so if text was
  changed).

### 3.6 Registering with each tool

| Tool | Add | Remove | Where it's found |
|---|---|---|---|
| Claude Code | `claude mcp add --scope user marconi -- <path>` | `claude mcp remove --scope user marconi` | PATH; Windows `%USERPROFILE%\.local\bin\claude.exe`, `%LOCALAPPDATA%\Microsoft\WinGet\Links\claude.exe`, npm's global folder; macOS/Linux `~/.local/bin/claude` |
| Codex | `codex mcp add marconi -- <path>` | `codex mcp remove marconi` | PATH; Windows `%LOCALAPPDATA%\Programs\OpenAI\Codex\bin` (or `CODEX_INSTALL_DIR`), npm's global folder |
| Claude Desktop | open `marconi.mcpb` | in Claude Desktop | the app's install folder (to be checked when built) |

- Each tool's own command is used rather than editing its settings, so we never write their file formats.
  Codex keeps its settings in `CODEX_HOME`, `%USERPROFILE%\.codex` by default (finding 3).
- The command found is run by its full path. On Windows, PATH is read from the registry (the user's and the
  machine's), not just inherited, because a tool installed since the user signed in isn't in the inherited PATH yet
  (both Codex's and Claude Code's installers add their folder to the user's PATH).
- `--status` asks each tool (`claude mcp get marconi`, `codex mcp get marconi`) rather than reading their files.
- Claude Desktop: opening the `.mcpb` shows Claude Desktop's own install screen and the user confirms there, so
  Marconi can't tell whether it was installed. The window says that.

### 3.7 Detecting installed apps (engine)

- `Packaging/Apps.json` (name to be confirmed in Phase 4): each app with an id, a display name, and per platform
  the ways to find it, tried in order:
  - `command`: a program name looked up on PATH (on Windows also the registry's user and machine PATH);
  - `file`: a path with environment variables (`%LOCALAPPDATA%\Programs\OpenAI\Codex\bin\codex.exe`);
  - `uninstallKey`: a display-name match in Windows' installed-programs list (HKCU, HKLM and HKLM's 32-bit view);
  - `bundleId` (macOS, later).
- `Packaging/Include/DetectApps.nsh`, generated from `Apps.json` by `PackageProject.ps1` (so the two can't drift),
  with macros such as `!insertmacro DetectApp "codex" $0` (path or empty). `PackageProject.ps1` passes
  `/X!addincludedir <engine>\Packaging\Include` so any installer script can `!include "DetectApps.nsh"`.
- A small engine module (`AppDetect`, name to be confirmed) reading the same file: `FindApp(id)` returns the path
  or nothing. Marconi copies `Apps.json` beside the program at packaging time, or embeds it at build time (to be
  decided in Phase 4, whichever BuildTool supports).
- Tests: the engine module against a temporary folder and PATH; the `.nsh` with a small test installer.

### 3.8 Windows installer

- Marconi's `Project.json` lists `MarconiMcp.exe` in `package.files` (from `Build/MarconiMcp/Release`), and
  `Package.ps1` builds `Mcp/Project.json` before calling the engine's script.
- **Signing:** the engine's script signs only `Marconi.exe` (finding 9). It needs to sign every program it stages
  (or a list the project gives). Engine change, Phase 5.
- A custom page after installing, "Connect Marconi to your AI tools", built with nsDialogs: a tick box per detected
  tool, ticked by default, with a line saying what it does. Skipped when none is found or in a silent install
  (a command-line switch can ask for it in a silent install).
- Registering runs `MarconiMcp.exe --register <tool>` as the signed-in user:
  - installing for one user: `ExecWait`, with the exit code; a failure shows a message and carries on;
  - installing for everyone: ShellExecAsUser, which can't wait or see the result, so MarconiMcp shows its own
    message if registering fails (section 7).
- The uninstaller runs `--unregister` for each tool registered (recorded in the registry at install). For an
  install for everyone it can only reach whoever is signed in when it runs.

### 3.9 The "AI tools" window

- Reached from the start screen's menu and the viewer's menu (exact place shown to the owner first).
- One row per supported tool: found or not, registered or not, Add or Remove. Claude Desktop's row opens the
  `.mcpb` instead.
- Runs the shipped MarconiMcp with `--register`/`--unregister`/`--status`, in the background so the window doesn't
  freeze, and shows what failed in plain words.

### 3.10 Packages on other platforms

- **Linux:** `Package.sh` builds MarconiMcp and installs it as `/usr/bin/marconi-mcp`.
- **macOS (last, needs a Mac):** `Marconi.app/Contents/MacOS/MarconiMcp`, signed before the app, without `--deep`,
  with the same identity and options `Package.sh` now chooses (finding 6).

### 3.11 Claude Desktop extension

- `marconi.mcpb` per platform: a zip with `manifest.json` (`manifest_version` "0.3", `name`, `version` from the
  build, `description`, `author.name`, `server.type` "binary", `server.entry_point` and `mcp_config.command` the
  program, `compatibility.platforms` the one platform) and the program itself (finding 5).
- Shipped beside the program; the installer page and the window offer it.

### 3.12 Security and privacy

- MarconiMcp lets an AI tool control real computers. The AI tools ask before each tool use unless the user allows
  it; `--read-only` exists for looking without touching.
- Passwords are never logged or echoed back. `MARCONI_PASSWORD` keeps them out of the conversation.
- Nothing is saved between runs (no remembered certificates or passwords) in the first version.

## 4. Phases

### Phase 0: agree the plan, check the unknowns

- Checks done 2026-11-13 (section 6); the owner answered section 7 the same day. **Done.**

### Phase 1: the program, connect and screenshot

1. `Modules/MarconiMcp`, `Modules/MarconiMcpApp`, `Mcp/Project.json`; `main` with the standard-output redirect and
   the log file; builds on Windows and Linux.
2. The message loop: `initialize`, `ping`, `tools/list`, `tools/call`, errors. Tests feed it messages and check
   the replies.
3. PNG writer and the scaler, with tests (decode with stb_image and compare).
4. `connect`, `disconnect`, `status`, `screenshot` (settling, area, cursor). Tests against the scripted fake server
   MarconiCoreTest already uses.
- Done when: Claude Code, registered by hand (`claude mcp add`), connects to `Tools/fake_vnc_server.py` and
  describes the screenshot.
- No version bump: nothing ships yet.

### Phase 2: input and clipboard

1. Mouse tools with coordinate conversion; tests on the button masks sent.
2. Key-name table, `key`, `type_text`; tests on the keysyms sent.
3. Clipboard tools, `wait_for_change`, `--read-only` checked against the full tool list.
4. Logging to a file beside the viewer's logs (section 3.3); until then the log goes to standard error only.
- Done when: the fake server logs the clicks and keys Claude sends, and Claude completes a small task on a real VNC
  server while a viewer watches.
- README section on using it by hand. No version bump: not shipped yet.

### Phase 3: registering

1. Finding each tool (section 3.6), with PATH from the registry on Windows; tests with a fake folder and PATH.
2. `--register`, `--unregister`, `--status` for Claude Code and Codex; tests with stand-in `claude`/`codex`
   scripts that record their arguments.
- Done when: registering and removing works for both on Windows and Linux with the real tools.

### Phase 4: detecting apps (engine repo)

1. `Apps.json` with Claude Code, Codex, Claude Desktop, Blender and Ollama; the C++ module and its tests.
2. The generated `DetectApps.nsh`, `addincludedir` in `PackageProject.ps1`, a test installer.
3. Marconi's registration code (Phase 3) moves onto the engine module where they overlap.
- Done when: a test installer and a test program both list which of the apps are installed on a Windows PC.

### Phase 5: Windows installer

1. Engine: sign every staged program (finding 9).
2. `Package.ps1` builds MarconiMcp; `Project.json` ships it.
3. The "Connect Marconi to your AI tools" page and registering (section 3.8); the uninstaller removes them.
- Done when: installing for one user and for everyone both register correctly, uninstalling removes them, and both
  programs are signed when a certificate is set.
- MINOR bump (the feature reaches users), changelog line, README section.

### Phase 6: the "AI tools" window

- Section 3.9. Done when: Add and Remove work for both tools on Windows and Linux. PATCH bump, changelog line.

### Phase 7: Linux package

- Section 3.10. Done when: the installed package's window registers `/usr/bin/marconi-mcp` with Claude Code and it
  works. PATCH bump.

### Phase 8: Claude Desktop

- Section 3.11, for Windows (and Linux if Claude Desktop runs there). Done when: Claude Desktop installs the file
  and takes a screenshot through it. PATCH bump.

### Phase 9: macOS (needs a Mac)

- Section 3.10 and the `.mcpb` for macOS. Check whether macOS blocks the program Claude Desktop unpacks from the
  `.mcpb`. PATCH bump.

## 5. Testing throughout

- MarconiCoreTest for everything that doesn't need a real AI tool: messages, scaling, PNG, key names, button masks,
  the commands built for each tool, finding tools.
- The scripted fake server in MarconiCoreTest for connecting and input; `Tools/fake_vnc_server.py` by hand, since it
  logs every input event.
- By hand with the real tools at the end of each phase, recorded here.

## 6. Findings (checked 2026-11-13)

1. **Engine logging writes to standard output.** `Log::Print` (engine-log `Log.cpp`) sends info and verbose
   messages to `stdout`, warnings and errors to `stderr`, plus the debugger and the log file when set. Handled in
   section 3.3.
2. **ShellExecAsUser can't wait or report an exit code.** Its documentation describes it as `ExecShell` run as
   the signed-in user, with no wait mode. Fine when installing for one user (Setup isn't elevated, so `ExecWait`
   works); a problem when installing for everyone (section 7).
3. **Where the tools live on Windows.**
   - Claude Code: Anthropic's installer (`install.ps1`, which runs `claude install`) puts it in
     `%USERPROFILE%\.local\bin\claude.exe`, which isn't always added to PATH; WinGet in
     `%LOCALAPPDATA%\Microsoft\WinGet\Links\claude.exe`; npm in npm's global folder.
   - Codex: its install script (`chatgpt.com/codex/install.ps1`, read 2026-11-13) puts it in
     `%LOCALAPPDATA%\Programs\OpenAI\Codex\bin` unless `CODEX_INSTALL_DIR` says otherwise, and adds that folder to
     the user's PATH. Settings live in `CODEX_HOME`, `%USERPROFILE%\.codex` by default; OpenAI's Windows app page
     says the desktop app uses the same folder. npm (`@openai/codex`) also works. The desktop app comes from the
     Microsoft Store; whether it brings a `codex` command isn't documented.
4. **Image size.** Anthropic's vision guide recommends at most 1.15 megapixels and 1568 pixels per side; newer
   models take up to 2576 pixels and 3.75 megapixels. The real limit is a token budget, so a 1920x1080 picture
   becomes 1456x819, not 1568x882. Anthropic's computer-use tool rejects oversized screenshots rather than
   shrinking them.
5. **`.mcpb` format** (modelcontextprotocol/mcpb `MANIFEST.md`): current `manifest_version` "0.3"; required
   `manifest_version`, `name`, `version`, `description`, `author.name`, `server`. `server.type` "binary" is
   supported, with `mcp_config` (`command`, `args`, `env`) and `${__dirname}`; `.exe` is added on Windows
   automatically. `compatibility.platforms` takes `darwin`, `win32`, `linux`. The spec doesn't say whether one
   file can carry programs for several platforms, so we build one per platform. Installed by double-clicking,
   dragging into Claude Desktop, or its Extensions settings. Team and Enterprise admins can turn extensions off.
   Plans: Anthropic's pages say extensions need the desktop app signed in to claude.ai and list no plan limit for
   them (the documented Free-plan limit, one custom connector, is for connectors added by web address). No page
   says outright that Free and Pro can install them; not pursued further, as the owner doesn't mind.
6. **Signing a second program in `Marconi.app`.** Apple's guidance: helper programs go in `Contents/MacOS`, nested
   code is signed before what contains it, `--deep` is to be avoided. `Package.sh` (0.9.6) now picks the Developer
   ID certificate itself when there's one; MarconiMcp must be signed with the same identity before the app.
7. **Marconi's own server takes one viewer at a time** (`Listener::ServeOne`). A second connection waits unanswered
   until the first ends, so watching alongside works for VNC only until `SERVER_PLAN.md` phase 6 step 7.
8. **MCP version.** 2026-07-28 is final and current (MCP blog and versioning page, checked 2026-11-13). It drops
   the `initialize` handshake: every request names its revision and the client's capabilities in `_meta`, servers
   must answer `server/discover`, `ping` is removed, results carry `resultType`, and a "dual-era" server picks the
   older rules when a client opens with `initialize`. stdio framing is unchanged (one JSON message per line;
   standard error is for logs). MarconiMcp speaks both. Claude Code 2.1.323 uses 2026-07-28: it opens with
   `server/discover` and sends the revision, client info and capabilities in every request's `_meta` (seen
   2026-11-13 by logging what it sent); our `server/discover` answer without `ttlMs`/`cacheScope` was accepted.
9. **The engine's packaging signs only the main program** (`PackageProject.ps1` signs `<name>.exe`, then the
   uninstaller and Setup). A second program would ship unsigned, so Phase 5 starts with an engine change.

10. **x11vnc and Marconi loop on desktop-size notices** (seen 2026-11-13 with x11vnc 0.9.16 on Xvfb, by the viewer
    as well as MarconiMcp). `Rfb::Client` treats every ExtendedDesktopSize rectangle as a resize, even when the size
    is unchanged: it sends SetPixelFormat again and asks for the whole desktop (`Client.cpp`, the `resized` flag).
    x11vnc answers that request with another ExtendedDesktopSize, so the two loop (about 48 updates a second with
    no pixels in the viewer's own log). After the first picture x11vnc then sends no more pixels to that
    connection, and a second connection after the first ends gets none at all, while input still works. A minimal
    client of our own that doesn't resend its pixel format always got pictures, with or without ExtendedDesktopSize
    and the extended clipboard. The same behaviour explains the occasional `DesktopSizeTests` failure (the
    SetPixelFormat and update request can arrive before the SetDesktopSize the test reads). The pixel-format resend
    is there for macOS switching display modes (its comment); whether macOS needs it when the size doesn't change
    can't be checked here. Fixed as decision 4 in section 7 chose; the resend stays, so macOS should behave as
    before, but that wants a run on a Mac.
11. **The viewer's log folder on Linux is `~/.marconi/logs`**, not `~/.local/share/marconi/logs` as the comment in
    `MarconiApp.cpp` says: wxWidgets' XDG file layout only moves the settings file (`GetUserLocalDataDir` is
    `~/.<app name>` in `src/unix/stdpaths.cpp`), and a real run wrote its logs and snapshots there. MarconiMcp logs
    beside them; the comment is left as it is for now.

Still open (none blocks Phase 1): what Claude Code does with an oversized image from an MCP server; whether the
Codex desktop app brings a `codex` command; the exact file npm installs for each tool on Windows; which MCP
revision Codex and Claude Desktop use; where Claude Desktop installs on Windows.

## 7. Decisions needed from the owner

None open. Answered 2026-11-13:

1. **Registering when installing for everyone:** Setup starts `MarconiMcp --register ...` as the signed-in user
   (ShellExecAsUser, as "Start Marconi now" does), and MarconiMcp shows its own message only if registering fails.
2. **One connection at a time** for now, built so several can be added later: the connection lives in one object
   the tools look up, so a later `connection` argument can pick between several without changing the tools.
3. **`Apps.json` starts with** Claude Code, Codex, Claude Desktop, Blender and Ollama.
4. **The desktop-size loop (finding 10): (b).** A desktop-size notice with an unchanged size still resends the pixel
   format (kept for macOS display-mode switches, which work well and mustn't break) but no longer asks for the whole
   desktop; a real change of size does both, as before.

## 8. Not in this plan unless the owner asks

- opencode and Pi (later, together).
- Remote Desktop (needs `QuickConnect` support first).
- An HTTP mode, and AI control of the viewer's own window.
- Remembering certificates or passwords between runs.
