# Marconi: what's done, what's planned, and ideas

One list of Marconi's features, finished or not, each pointing at the plan that covers it. The plans hold the
detail; this file says where each thing stands. Update it when a plan moves on or the owner adds an idea.

What each status means:

- **Done:** in Marconi on `main`.
- **Written:** on `main`, but not yet tried where it matters (the line says where).
- **Planned:** a plan covers it, but it isn't built yet.
- **Idea:** suggested, not planned. It waits for the owner's go-ahead before anyone plans or builds it.

Where the plans are:

| Plan | What it covers | Where |
|---|---|---|
| `SERVER_PLAN.md` | Marconi Server and its own protocol | `main` |
| `DRIVER_PLAN.md` | whether to fork ViGEmBus or write our own drivers (research, an idea) | branch `claude/driver-plan`, not merged |
| `MOBILE_PLAN.md` | the Android, iPad and iPhone apps | `main` |
| `Documentation/RDP.md` | Remote Desktop | `main` |
| `MCP_PLAN.md` | Marconi for AI tools (the `MarconiMcp` program) | branch `claude/sweet-bell-nobab4`, not merged |
| `PERFORMANCE_PLAN.md` | speed: video, the datagram channel, sound, games | branch `claude/beautiful-babbage-qh3jhe`, made at 0.6.3, not merged |
| `TERMINAL_PLAN.md` | a terminal over SSH and Marconi Server, and commands and files for AI tools | `main` |

The ideas from the owner's phone chat of 2026-11-13 are folded in below, each next to what already exists.

## Toward 1.0

Drawn up with the owner on 2026-11-14. Calling a version 1.0 stays the owner's decision alone (`AGENTS.md`); this is
the list of what the owner wants done first. Tick items off here as they are finished.

Already settled:

- MarconiMcp works on a Mac (owner, 2026-11-14).
- Installing Marconi Server from the F8 menu works on Windows (owner, on "drips", 2026-11-14).
- The Mac server compiles and links on the owner's Mac, and its disk image is made by the engine's packaging
  (`bdaf4b8`); nothing of it has been run yet.

Needed for 1.0:

1. **Marconi Server on a Mac** (the owner chose all four parts):
   - [ ] it runs: the permission prompts, picture, mouse and keyboard (the checks in `SERVER_PLAN.md`, "Phase 8
     progress: macOS");
   - [ ] the disk image works (`PackageServer.sh`): the app runs from Applications, signing and notarizing pass, the
     permissions survive a rebuild;
   - [ ] start at sign-in, switched on and off from the menu-bar icon;
   - [ ] `Package.sh` makes the server's disk image along with Marconi's;
   - [ ] F8 can install it on a Mac Marconi is connected to (copying it over Remote Login).
2. **Marconi Server on Windows finished:**
   - [ ] the Windows service tried for real: the sign-in screen, surviving a restart, the tray talking to it;
   - [ ] clipboard, files and extra screens tried on Windows;
   - [ ] self-update's Windows side built and run (the signature check, starting the installer, the approval window);
   - [ ] the service follows the tray's settings (who can connect, idle disconnect);
   - [ ] changing the port changes the firewall rule too.
3. **Fix what can't change after 1.0** (changing any of these later would mean a 2.0):
   - [ ] how a newer and an older Marconi and Marconi Server agree on the protocol;
   - [ ] the saved-settings layout;
   - [ ] addresses (`marconi://`, port 1895, the address forms).
4. **Release tidy-up:**
   - [ ] the changelog's "Unreleased" lines released under a version;
   - [ ] the old `claude/*` branches kept or dropped (including `PERFORMANCE_PLAN.md` and `DRIVER_PLAN.md`);
   - [ ] the plans and README brought up to date (`SERVER_PLAN.md`'s "Status at 0.9.7" section, the README's line
     that the Linux server shows a test picture).

The owner's choices, each in 1.0 or after it (undecided):

- [ ] Marconi Server on a Linux Wayland desktop (GNOME, KDE): the real permission window and real input are untried.
- [ ] The client's gaps (encryption without a certificate, `ssh://` addresses, clipboard and resizing over Remote
  Desktop): fixed, or clearly said to be unsupported.
- [ ] The phone and tablet apps: in 1.0, or labelled previews (the iPad app is well behind Android).
- [ ] Video and game controllers: left switched off in 1.0, or finished.

After 1.0 whatever happens: sound, a relay, recording sessions, the built-in terminal, "follow along", our own
drivers, and the ideas for AI tools below.

## Marconi app and server

| Feature | Status | Notes |
|---|---|---|
| Video streaming with hardware H.264 or HEVC instead of still tiles, with a bitrate that adapts | Written, tried with a test codec only | `SERVER_PLAN.md` Phase 4 (video) and Phase 5 (datagrams, bitrate adaptation); `PERFORMANCE_PLAN.md` details both. On `main`: the protocol, the server and viewer pipeline (tested on Linux with a test codec), and the Windows and Mac codecs, which are written but never built and switched off. The Linux decoder (FFmpeg, in the engine as `External/ffmpeg`, LGPL, decode only) is on `main` and tested on a small real H.264 clip; there is no Linux H.264 encoder, so it meets real streams only from a Windows or Mac server. The datagram transport (encrypted, fragmented, with fall back to TCP and statistics) is also on `main`, tested on Linux loopback and not yet connected to video or input. HEVC and AV1 are listed under "Not in this plan unless the owner asks". |
| Several monitors: pick one, or see them all | Done for VNC; Written for Marconi Server | With VNC servers that list their displays (TigerVNC, UltraVNC) the viewer shows the whole desktop or one display. Marconi Server: done on Linux, written for Windows (`SERVER_PLAN.md` Phase 6 step 3). Each display in its own window: idea. |
| File transfer, dragging both ways | Done; Written for Marconi Server | Done with Macs (over Remote Login) and TightVNC. Marconi Server: tried on Linux only, and the file panel hasn't been clicked through against it (Phase 6 step 2). Not on the phones. |
| Clipboard both ways, pictures too | Done; Written for Marconi Server | Text both ways, pictures with servers that allow it. Marconi Server: tested on Linux, written for Windows, not yet on macOS, and the tray program doesn't pass it on yet (Phase 6 step 1). Not over Remote Desktop yet. |
| Sound | Planned on a branch | `PERFORMANCE_PLAN.md` step 6 (unmerged). `SERVER_PLAN.md` section 7 still lists it as "not in this plan unless the owner asks". |
| Reaching computers away from home without port forwarding (a relay) | Idea | `SERVER_PLAN.md` decision 10 says none is planned, because it needs a service Red Marmoset would have to run. Today: port forwarding, a VPN, or connecting through SSH. |
| Recording a session (for bug reports and trailers) | Idea | Today Marconi saves single screenshots. |
| Game controllers from a phone or laptop | Written, tried with stand-ins only | `SERVER_PLAN.md` Phase 7 progress. The protocol, the server (Linux uinput; Windows ViGEmBus; no Mac), the viewer menu item and the Windows and Linux readers are written and tested with fake controllers. Never run with a real controller, `/dev/uinput` or ViGEmBus. Not on phones, not on a Mac viewer or server, and not on the datagram channel yet. Relative mouse and scancodes (Phase 7 step 4) not done. |
| Our own Windows drivers (a fork of ViGEmBus, or new ones): virtual gamepad, virtual screen, virtual microphone | Idea | Researched in `DRIVER_PLAN.md`; waits for the owner's go-ahead. Short version: forking is allowed but means owning a signed kernel driver; a USB/IP test could avoid that. Meanwhile controllers use the existing ViGEmBus (`SERVER_PLAN.md` decision 6). |
| A terminal in the same app, opening SSH computers found on the network, and through Marconi Server | Planned | `TERMINAL_PLAN.md` (the owner's go-ahead, 2026-11-14): tabs in the viewer window, SSH first (Phase 4), then Marconi Server (Phases 5 and 6). Nothing built yet. Today Marconi finds SSH computers on the network and recognises `ssh://` addresses, but says it can't open them. It uses SSH only to carry VNC and for Mac file transfer. |
| Watching an iPad or iPhone's screen (view only), through a ReplayKit companion app | Idea | The details are in a separate email, not copied here yet. |
| "Follow along": watching live what an AI tool is doing through Marconi, with a big Stop button | Idea | `MarconiMcp` connects by itself with no window, and "AI control of the viewer's own window" is not planned (`MCP_PLAN.md` section 8). Marconi Server takes one viewer at a time today, so watching alongside would need it to take more (Phase 6 step 7). |

Also planned or missing, from the plans and the README's "Not available yet":

- Marconi Server: the first Windows run is done (the screen, encryption and scaling were seen working), and the owner
  has installed it from the F8 menu on a Windows PC. Still to try on Windows: the Windows service, clipboard, files and
  the extra screens. On a Mac the server installer is still to do (`PackageServer.sh` written, never run; the server
  itself never built there). The tray
  program talking to the Windows service (Phase 3) is written, tried on Linux only. Still planned: the macOS server
  (written, never built) and several viewers at once (Phase 6 step 7). Video (Phase 4), the datagram channel (Phase 5)
  and game controllers (Phase 7) are written and tried only with stand-ins.
- Marconi Server on a Linux Wayland desktop (GNOME, KDE; screen only on Sway) through the desktop's screen-sharing
  permission: written and tried against a stand-in desktop and a real compositor's picture; the real permission window
  and real keyboard and mouse control are still to be tried (Phase 8).
- Marconi Server controls, all written and tried on Linux only: who can connect (by address), disconnecting idle
  viewers, a message when someone connects, a log file for each run, the password asked for in the tray, file
  sharing off until switched on, and a Linux package.
- Updating Marconi Server from a newer Marconi (Windows servers): written; the checks, the protocol and a whole update
  with a stand-in installer are tested on Linux; the Windows signature check, the installer start and the tray's
  question window are written but never built or run (`SERVER_PLAN.md` "Self-update").
- Not in `SERVER_PLAN.md` unless the owner asks: other VNC viewers connecting to Marconi Server, blanking the
  server's screen, a virtual display, touch and pen input.
- The client: SSH sessions, encryption without a certificate (TigerVNC's "TLSVnc"), and over Remote Desktop the
  clipboard and changing the screen size.
- Phones and tablets (`MOBILE_PLAN.md`): a first run on real devices, the iPad app catching up with Android, Remote
  Desktop.

## Marconi for AI tools (MCP)

`MarconiMcp` is on `main` (phases 1 to 9 of `MCP_PLAN.md`), and the owner reports it working on a Mac (2026-11-14). It
takes one connection at a time and offers: `connect`, `disconnect`, `status`,
`screenshot` (an area, a largest size, waiting for the screen to settle), `click`, `move`, `drag`, `scroll`,
`type_text`, `key`, `get_clipboard`, `set_clipboard` and `wait_for_change`.

| Idea from the phone chat | Status | Notes |
|---|---|---|
| The screen's controls as a list (buttons, fields, labels and where they are), from Windows UI Automation, the macOS accessibility interface and Android's accessibility service | Idea | Needs Marconi Server on the other computer: VNC only carries pictures. |
| Listing, focusing, moving, resizing, opening and closing windows and apps | Idea | Needs Marconi Server, as above. |
| Waiting until something happens, with a time limit | Partly done | `wait_for_change` returns when the picture changes (optionally in one area) or the time runs out, and screenshots wait for the screen to settle. Waiting for some text or a control to appear: idea (needs reading text, or the list of controls). |
| Reading text on the screen; typing whole strings | Partly done | `type_text` types whole strings. Reading the screen's text (OCR): idea. |
| Device information: a list of computers, their state, battery (phones), what's running | Partly done | `status` describes the one connection. A list of computers (saved and found on the network), battery and running programs: idea; the last two need Marconi Server. `MCP_PLAN.md` decision 2 keeps room for several connections later. |
| Running commands, reading and writing files, after asking first | Planned | `TERMINAL_PLAN.md` Phase 2 (over SSH: `run_command`, `read_file`, `write_file`), Phase 5 (through Marconi Server) and Phase 7 (a live terminal). Nothing built yet. Marconi already copies files (over SSH, TightVNC and Marconi Server), which the file tools reuse. |
| Cheaper screenshots: one area, smaller, only what changed | Partly done | Areas and a largest size are done. Only what changed since the last one: idea. |

## Safety for AI tools

| Idea from the phone chat | Status | Notes |
|---|---|---|
| Look-only or full control, per computer | Partly done | `--read-only` offers only the tools that look, but for every computer at once. Per computer: idea. |
| Asking before input, commands or file changes | Partly done | The AI tools ask before each tool use unless the user turns that off. Marconi asking by itself: idea. |
| A log of everything the AI did | Idea | `MarconiMcp`'s log file records connecting, not each click or key. |
| An instant stop | Idea | Goes with "follow along" above. |
| Hiding sensitive areas such as password fields from screenshots | Idea | Finding them needs the list of controls above. |
| If it is ever reachable from other computers: OAuth sign-in, Cloudflare Access or Tailscale in front, look-only to start | Idea | Not needed today: `MarconiMcp` talks only to the AI tool on the same computer, over its standard input and output, with no port open. A network mode is listed as not planned (`MCP_PLAN.md` section 8). |
