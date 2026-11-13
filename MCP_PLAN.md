# Marconi for AI tools: the plan

This document plans a way for AI coding tools (Claude Code, Codex, Claude Desktop) to drive Marconi: connect to a
computer, take screenshots, move the mouse, type and use the clipboard. It works the same way `SERVER_PLAN.md`
does: phases in order, each step about one commit, each phase leaving Marconi releasable.

The rules in `AGENTS.md` apply throughout: no assumptions (verify or ask the owner), the version and changelog rules,
plain words in anything users read. Keep this file current: decisions, findings, finished steps.

## 1. Decisions (owner, agreed in chat)

1. **A separate program, `MarconiMcp`**, that the AI tool starts and talks to over its standard input and output
   (MCP's "stdio" transport). It connects to computers itself, with no window. Nothing has to stay running, there
   is no port and no token. The core is written so an HTTP mode could be added later without a rewrite.
2. **Watching is done with a normal Marconi viewer**, connected to the same computer. MarconiMcp doesn't open
   windows or talk to the viewer.
3. **It ships with Marconi on every platform**: next to `Marconi.exe` on Windows, inside `Marconi.app` on macOS,
   as `/usr/bin/marconi-mcp` in the Linux package.
4. **Windows installer:** a page after installing, "Connect Marconi to your AI tools", with one tick box per tool
   it finds installed. Skipped when it finds none. The uninstaller removes what it registered.
5. **The app gets an "AI tools…" window** on every platform, with the same detection and Add/Remove per tool. It
   covers macOS and Linux (whose packages have no step for tick boxes) and Windows users who didn't tick the box.
6. **Tools supported first:** Claude Code, Codex, and Claude Desktop (as a `.mcpb` desktop extension).
   **opencode and Pi wait**, to be done together later.
7. **Detecting installed apps lives in the engine**, as a general feature any una installer or app can use (not
   just for AI tools: Blender, Ollama and so on): one data file listing the apps and how to find each, an NSIS
   include generated from it for installers, and a small C++ module that reads it for apps.
8. **MCP is the right way in.** Claude's "connectors" are MCP servers; Claude Code, Codex and Claude Desktop all
   take local MCP servers. A command-line tool plus instructions would only suit Claude Code and would reconnect on
   every command.

## 2. What exists and what it gives us

- `QuickConnect()` (`Modules/MarconiCore/QuickConnect.h`): connects from an address with no probe; VNC, the
  Marconi protocol and VNC over SSH. It turns down Remote Desktop addresses, so RDP isn't in the first version.
- `RemoteSession` (`Modules/MarconiCore/RemoteSession.h`): the picture (`GetFramebuffer()`, under its mutex),
  pointer and key events, clipboard, displays, desktop resizing. `SessionListener::OnFramebufferUpdated` tells us
  when the picture changes; `OnCursorShape` gives the cursor, which isn't in the picture when the server sends it
  separately.
- `Keysyms::FromUnicode` (`Modules/MarconiCore/Keysyms.h`) for typing text.
- nlohmann::json, already used by `Update.cpp`.
- wx's bundled zlib (`deflate`), enough for a small PNG writer. MarconiCore has none today (the viewer saves PNGs
  through wxImage).
- VNC connects in shared mode by default (`shared = true`, `Rfb/Client.h`), so a viewer can watch alongside.
- Packaging: the engine's `Packaging/PackageProject.ps1` runs makensis on Marconi's own `Installer/Marconi.nsi`
  with `VERSION`, `STAGE_DIR`, `OUT_FILE` and `SIGN_SCRIPT`. Nothing passes an engine include folder yet. The
  installer already runs things as the signed-in user through the ShellExecAsUser plug-in.
- The finish page's two tick boxes are both used ("Start Marconi now", "Open the source code"), hence a page of
  its own.

## 3. Shape of the work

### Modules and executables

- `Modules/MarconiMcp` (library, no wx): the MCP message loop, the tools, the PNG writer, and registering with each
  AI tool. MarconiUI links it for the "AI tools…" window.
- `Modules/MarconiMcpApp` (console): `main`, the command line. A project file like `Server/Project.json`.
- Command line:
  - no arguments: run as an MCP server on standard input and output;
  - `--register <tool>` / `--unregister <tool>` / `--status`: add, remove or report the registration with
    `claude-code`, `codex` or `claude-desktop`. The installer and the app both use this, so registering always
    runs the same code, as the signed-in user.

### Standard output

In stdio mode standard output carries only MCP messages; one stray line breaks the connection. Logs go to a file
next to the viewer's logs (and errors to standard error). Unchecked: where the engine's `Log` writes by default.

### Tools offered to the AI

| Tool | What it does |
|---|---|
| `connect`, `disconnect`, `status` | Connect to an address; end it; size, displays, encryption |
| `screenshot` | PNG, scaled to fit (scale reported), optional area at full size, optional "wait until the screen settles" |
| `click`, `double_click`, `move`, `drag`, `scroll` | Mouse, in screenshot coordinates, converted back |
| `type_text` | Text as key presses |
| `key` | Combinations such as `ctrl+alt+Delete`, `cmd+space` |
| `get_clipboard`, `set_clipboard` | The remote clipboard |
| `wait_for_change` | Until the picture changes, or a timeout |

- **Scaling:** Claude scales big images down, so screenshots are scaled by us and the mouse tools take screenshot
  coordinates. Unchecked: the exact size limit (recalled as about 1568 pixels on the long side).
- **Settling:** wait until no update has arrived for about 300 ms, with a time limit.
- **Cursor:** drawn into the screenshot from `OnCursorShape` and the last pointer position.
- **Blocking questions:** passwords come from `connect` arguments or an environment variable; an untrusted
  certificate or SSH key is turned down and its fingerprint reported, and a second `connect` naming the fingerprint
  accepts it (`QuickConnectOptions::trustedCertificateSha256`).
- **`--read-only`** offers only `connect`, `status` and `screenshot`.

### Registering with each tool

- **Claude Code:** `claude mcp add --scope user marconi -- <path>`; remove with `claude mcp remove`.
- **Codex:** `codex mcp add marconi -- <path>`; remove with `codex mcp remove`. Writes `~/.codex/config.toml`.
- **Claude Desktop:** a `marconi.mcpb` built at packaging time (the program plus a `manifest.json`), opened with
  Claude Desktop to install.
- Each tool's own command is used rather than editing its settings, so we never write their file formats.

### Detecting installed apps (engine)

- `Packaging/Apps.json` (name to be agreed): each app with its ways to be found: a command on PATH, a Windows
  installed-programs entry, a known folder, per platform.
- `Packaging/Include/DetectApps.nsh`, generated from it by `PackageProject.ps1`, which also passes
  `/X!addincludedir` for that folder so any installer script can `!include "DetectApps.nsh"`.
- A small engine module (name to be agreed) reading the same file, for apps.
- Engine changes are committed to the engine repo separately (`AGENTS.md`).

### Security

MarconiMcp lets an AI tool control real computers. The AI tools ask before each tool use unless the user allows it;
`--read-only` exists for looking without touching. Passwords aren't written to its log.

## 4. Phases

### Phase 0: agree the plan, check the unknowns

- Owner reviews this file.
- Check each item in section 6, recording findings here.
- Done when: the owner agrees and section 6 has answers or a plan for each.

### Phase 1: connect and screenshot

- Module and project skeleton, the stdio message loop (`initialize`, `tools/list`, `tools/call`), logs to a file.
- PNG writer on zlib, with tests.
- `connect`, `disconnect`, `status`, `screenshot` (scaling, area, settling, cursor).
- Tests in MarconiCoreTest for message handling and scaling; by hand against `Tools/fake_vnc_server.py` with
  Claude Code registered manually.
- Done when: Claude Code connects to the fake server and describes the screenshot.

### Phase 2: input and clipboard

- Mouse tools with coordinate conversion, `type_text`, `key`, clipboard, `wait_for_change`, `--read-only`.
- Done when: the fake server logs the clicks and keys Claude sends, and a real VNC server can be used end to end.
- MINOR bump; README section on using it by hand.

### Phase 3: registering

- `--register`, `--unregister`, `--status` for Claude Code and Codex, with tests where the tool commands can be
  faked.
- Done when: registering and removing works for both on Windows, macOS and Linux.

### Phase 4: detecting apps (engine)

- The data file, the generated `.nsh`, `addincludedir`, the C++ module, tests. Engine repo.
- Done when: a test installer and a test program both report which of the listed apps are installed.

### Phase 5: Windows installer

- MarconiMcp staged with the app; the "Connect Marconi to your AI tools" page; registering as the signed-in user;
  the uninstaller removes registrations.
- Done when: installing for one user and for everyone both register correctly, and uninstalling removes them.

### Phase 6: the "AI tools…" window

- Lists the detected tools with Add/Remove, using MarconiMcp's registration code.
- MINOR bump.

### Phase 7: macOS and Linux packages

- MarconiMcp inside `Marconi.app` (signed with it) and in the Linux package.
- Done when: the window registers the bundled program on both, and Claude Code can use it.

### Phase 8: Claude Desktop

- Build `marconi.mcpb` when packaging; the window and installer offer it.
- Done when: Claude Desktop installs it and takes a screenshot through it.

## 5. Testing throughout

- MarconiCoreTest for everything that doesn't need a real AI tool: messages, scaling, PNG, key names,
  registration commands built for each tool.
- `Tools/fake_vnc_server.py` logs every input event, so input can be checked without a real desktop.

## 6. Unknowns to check before relying on them

- Where the engine's `Log` writes by default (must not be standard output).
- Whether ShellExecAsUser can wait for a command and report its exit code; if not, another way to run registration
  as the signed-in user.
- How `claude` is installed on Windows (a `.cmd` from npm, or the native installer), so the installer and app can
  find and run it.
- Claude's image size limit for screenshots.
- The `.mcpb` manifest format, how a user installs one, and which Claude plans allow desktop extensions.
- Signing a second program inside `Marconi.app`.
- Whether Marconi's own server accepts a second connection, so a viewer can watch a Marconi-protocol session.

## 7. Not in this plan unless the owner asks

- opencode and Pi (later, together).
- Remote Desktop (needs `QuickConnect` support first).
- An HTTP mode, and AI control of the viewer's own window.
