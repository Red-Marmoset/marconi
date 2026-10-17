# Marconi Server: the plan

Marconi is a client today. This document plans the server: the part that runs on the computer being controlled,
shares its screen and takes the viewer's keyboard, mouse and gamepads. It is written so the work can be done one
phase at a time, each phase leaving Marconi releasable, and so any agent can pick up the next step without
re-deriving the design.

The owner's brief, as given:

- our own server, installed from the client with as little fuss as possible ("an easy menu option", "planting an
  installer on the desktop");
- an NSIS installer for the server;
- a lean protocol of our own, built for speed the way video streaming and screen sharing are, with none of the lag
  of most remote-desktop apps;
- gamepad support, on top of everything the client already does (keyboard, mouse, clipboard with images, file
  transfer, several displays, resizing, cursor shapes, encryption, reconnecting, lock on disconnect).

How to use this document:

- Work through the phases in order unless the owner reorders them. Each phase has steps meant to be one commit
  each, or close to it, and a "done when" check that can be run by hand.
- The rules in `AGENTS.md` apply to every step: no assumptions (verify or ask), the version and changelog rules,
  plain words in anything users read. Each phase finishes a significant feature, so each ends with a MINOR bump;
  steps inside a phase that fix or improve something visible take a PATCH bump. Nothing here proposes 1.0.
- The first section lists what the owner needs to decide. Nothing that depends on an undecided point should be
  built before the owner has answered; everything else can start.

## 1. Decisions needed from the owner

Each point gives the recommendation this plan is written against. Answer in this file (edit the line) or in chat.

1. **Which computer "install on the host machine" means.** This plan reads it as *the other computer*: while
   connected to a Windows PC over TightVNC (or any server that carries files), a menu item puts the Marconi Server
   installer on that PC's desktop, and the user double-clicks it on the remote screen. It also adds a
   **Settings > Share this computer** item that runs the same installer on the local machine, since that is cheap
   once the installer ships with the client. Confirm both, or say which one.
2. **The server speaks only Marconi's own protocol.** Recommended: yes. Other VNC viewers won't be able to connect
   to it. Adding an RFB listener later for compatibility is possible (section 7) but is throwaway work if done
   first, and would pull the server's design towards RFB's pull-per-frame model, which is the source of the lag the
   owner wants rid of.
3. **Names and numbers.** The protocol is "the Marconi protocol" to users; in code `Marconi::Wire`
   (`Modules/MarconiCore/Wire/`), because `Marconi::Protocol` is already the enum in `Address.h`. Addresses use
   `marconi://host`. Port **1895**, TCP and UDP (the year of Marconi's first transmissions; unassigned and above
   1024). Magic bytes `MRCN`.
4. **Login.** Recommended: an access password set when the server is installed (and changeable from its tray icon),
   sent inside TLS to a server whose self-signed certificate the client pins with the existing "trust this
   certificate?" dialog, checked against a salted PBKDF2 hash, with a lockout after repeated failures. Alternatives:
   a one-time pairing PIN shown on the server's screen (strong, but you must see that screen once), or Windows
   account passwords (needs LogonUser as a service, and ties access to local accounts).
5. **Video codecs from the operating system**, not a bundled ffmpeg: Media Foundation on Windows (hardware H.264
   encoders and decoders from NVIDIA, AMD and Intel are exposed through it, with Microsoft's software ones as
   fallback), VideoToolbox on macOS. H.264 first, HEVC and AV1 later where both ends have them. The open question is
   the **Linux client**: Linux has no system codec API. Options are ffmpeg's libavcodec (LGPL, large) or a small
   software H.264 decoder only. Recommended: ffmpeg via the engine's external dependencies, decided when Phase 4
   starts.
6. **Gamepads on a Windows host need a virtual controller driver.** There is no built-in way to create one; every
   product in this space (Sunshine, Parsec, DS4Windows) uses **ViGEmBus**, a BSD-licensed, signed kernel driver that
   its author stopped maintaining in 2023 but which still works on Windows 11. Recommended: the server installer
   offers it as an optional component, running its own installer, with the risk stated in the README. Alternative:
   tell users to install it themselves. On Linux hosts the kernel's uinput does it with no driver. On macOS hosts it
   isn't practical (a DriverKit extension needs an Apple entitlement), so gamepads to a Mac are not planned.
7. **One version for client and server**, from the same commit, and one `Package.ps1` run producing both installers,
   with the server's installer embedded in the client's so the client can hand it out. The client installer grows by
   the server installer's size (a few MB).
8. **The server installs for the whole machine** (administrator rights, Program Files, a Windows service from
   Phase 3). There is no "just me" server install: a server that only runs while one account is logged in can't
   show the login screen or survive a restart, which is what people need from it. Phases 1 and 2 run it as a tray
   application for the signed-in user; Phase 3 makes it a service.
9. **Silent installs and the password.** A password on the command line shows in process lists, so `/S` installs
   don't take one; a server with no password refuses every connection and its tray icon says so until one is set.
10. **Reaching a server over the internet** is by port forwarding or a VPN, as with VNC today. No relay or
    hole-punching service is planned; that needs infrastructure Red Marmoset would have to run.
11. **Order.** Installer and menu item (Phase 2) come before video (Phase 4), because the owner asked for them
    explicitly and because they let the server be tried on real machines early. Say so if speed should come first.
12. **Lossless compression for still pictures.** zlib is already a dependency; zstd would be faster and smaller. Start
    with zlib, consider zstd when the measurements in Phase 4 say it matters.

## 2. What exists and what it gives us

Verified by reading the code in this repository (the engine wasn't available here, so engine facts below are marked
"verify"):

- `MarconiCore` already has everything network-side a server needs except listening for real: `Net::Socket` can
  listen and accept (used by tests), `Net::UdpSocket` sends and receives IPv4 datagrams, `Net::Tls.h` defines a
  TLS stream interface (client side only; the engine's `Crypto::TlsClient` on mbedTLS implements it). mbedTLS can
  also be a TLS server, do AES-GCM, PBKDF2 and DTLS; whether the engine's `Crypto` module exposes those is to
  **verify** when each phase needs them.
- The client's viewer (`ViewerFrame`) drives `Rfb::Client` directly. Its listener interface, framebuffer, cursor
  handling, clipboard channel, reconnect logic, probing and discovery are all reusable once there is a seam between
  the viewer and the protocol (Phase 1, step 1).
- `Files::FileSource` is an interface with `LocalFiles`, `SftpFiles` and `TightFiles` behind it, driven by the file
  panel and `TransferQueue`. The server reuses `LocalFiles` as is; the client gets a fourth implementation over the
  Marconi protocol.
- `Net::Probe` identifies servers by their greeting and `Net::ProtocolChoice` ranks them, so a Marconi server is
  found automatically once its greeting is known. `Net::Discovery` queries mDNS; the server needs to answer.
- `Installer/Marconi.nsi` and `Package.ps1` are a complete, signed, per-user-or-per-machine installer pipeline with
  branding and an uninstaller. The server installer copies its structure and shares its signing and art.
- `SystemKeys_Windows.cpp` shows the house style for Windows-only code (`_Windows` suffix, raw Win32, plain
  reasons for failure); `ViewerFrame::AskUser` shows how a worker thread asks the UI a blocking question.
- Tests run against scripted fake servers on loopback (`TestSupport.h`). The same approach, with a fake screen
  source and a recording input sink, tests the server without a display.

## 3. Shape of the work

### Modules and executables

```
Modules/MarconiCore/Wire/          the Marconi protocol, shared by client and server: framing, messages,
                                   handshake, login, UDP packetization and encryption, codec-independent
Modules/MarconiCore/Wire/Client.*  Wire::Client, a sibling of Rfb::Client with the same kind of listener
Modules/MarconiServerCore/         the headless server: Session, Capture, Encoder, Input, Clipboard, Files,
                                   Gamepads, Settings, Auth, service supervision; no wx headers
Modules/MarconiServerUI/           tray icon, status and settings windows (wx), first-run password
Server/Project.json                MarconiServer.exe (modules: MarconiCore, MarconiServerCore, MarconiServerUI)
Tests/MarconiServerTest/           server tests on loopback with a fake screen and recorded input
Installer/MarconiServer.nsi        the NSIS script for the server
```

One `MarconiServer.exe` with modes, chosen on the command line:

| Mode | What it does |
|---|---|
| (none) | tray application: status, settings, Quit; in Phases 1 and 2 it also runs the server itself |
| `--service` | the Windows service entry point (Phase 3): supervises the session helper |
| `--session <id>` | the helper that captures, encodes and serves one console session, as SYSTEM (Phase 3) |
| `--set-password` | asks for and stores the access password (run by the installer and the tray icon, as administrator) |
| `--test-pattern [--port N]` | serves a synthetic moving picture with no display: for tests and manual checks |
| `--bench` | captures and encodes for ten seconds and prints the timings per stage |

Platform-specific files use the existing suffix convention (`Capture_Windows.cpp`, `Input_Windows.cpp`, later
`_Mac.mm` and `_Linux.cpp`). Every platform piece sits behind a small interface with a fake implementation for
tests: `ScreenSource`, `VideoEncoder`, `InputSink`, `ClipboardAccess`, `GamepadSink`.

Client changes, by area:

- `Address.h`: `Protocol::Marconi`, `marconi://`, default port 1895. `Probe`: recognise the greeting.
  `ProtocolChoice`: prefer Marconi over VNC. `Discovery`: `_marconi._tcp`.
- `ViewerFrame`: talk to a `RemoteSession` interface rather than `Rfb::Client`; `RfbSession` wraps the existing
  client unchanged, `WireSession` wraps `Wire::Client`.
- F8 menu: **Install Marconi Server on the Other Computer...**; connections window Settings: **Share this
  computer...**; later **Send Gamepads to the Other Computer** (check item).
- `VncCanvas`: a GPU presentation path for decoded video (Phase 4), keeping today's software path for RFB.
- `Files/MarconiFiles.*`: `FileSource` over the protocol.
- `Gamepads.h` with `_Windows`, `_Mac`, `_Linux` implementations (Phase 7).

### Why not serve RFB

RFB is a request-reply protocol: the client asks for an update and the server answers with one, so every frame costs
a round trip unless the client keeps requests in flight, and a 100 ms link caps the picture at a handful of frames a
second however fast both ends are. Its encodings are per-rectangle still-image codecs with no motion prediction, its
numbers are big-endian, it has no datagram transport, and its handshake takes several round trips before the first
pixel. These are exactly the lag the owner describes. The Marconi server pushes frames as the screen changes, at the
display's rate, and the client steers it with measurements; the protocol is designed for that from the start.

### The Marconi protocol

Principles:

- **Push, don't poll.** The server sends a frame whenever the screen changed and the link has room; the client
  reports what it received and how long it took, and the server adapts bitrate, frame rate and resolution.
- **Two channels.** A reliable control channel (TCP inside TLS) for everything that must arrive in order: the
  handshake, login, keys, clipboard, files, display layout, settings. A datagram channel (UDP, AES-GCM with keys
  agreed over the control channel) for everything where the newest matters more than every one: video, pointer,
  gamepad state, cursor position, receipt reports. Until Phase 5 the control channel carries the video too, and it
  stays the fallback where UDP is blocked.
- **Lean encoding.** Little-endian (nothing on our platforms is big-endian; RFB byte-swaps for nothing), varint
  lengths, UTF-8 strings with lengths, one `type` byte and a length per message so unknown messages are skipped,
  capabilities negotiated once in the handshake and never per message. No text, no padding fields, no
  per-rectangle encoding switches.
- **Few round trips.** TCP connect, TLS 1.3 (one round trip; verify mbedTLS version in the engine), `Hello` and
  `Welcome` inside it, `Login` and `LoginResult`, then frames start flowing. The client's first pixel arrives
  after three round trips plus TLS.
- **Video while it moves, perfect when it stops.** Moving regions are sent as video (H.264 at first); once an area
  has been still for a moment the server sends it again losslessly, so text and fine lines end up pixel-exact. This
  is what makes a video-codec remote desktop usable for work, not only games.
- **Input goes out the moment it happens**, on the datagram channel, with sequence numbers so a late packet never
  undoes a newer one. Keys go on the reliable channel (a lost key-up would stick a key). The cursor is drawn by the
  client from the server's cursor shape, so it never lags the picture.

Messages (first cut; the exact fields are set in Phase 1 and extended per phase; the message table lives in
`Wire/Messages.h` with a test that checks every message round-trips):

| Channel | Message | Direction | Carries |
|---|---|---|---|
| control | Hello / Welcome | both | magic, protocol version, capability bits, server name and version, display list |
| control | Login / LoginResult | both | the access password; ok, wrong, locked out |
| control | Keys | server | the UDP session id and AES-GCM keys for each direction (Phase 5) |
| control | Displays / SelectDisplay / SetDisplayMode | both | layout of displays; which to stream; resize to match the window |
| control | StreamConfig / StreamReady | both | codec, max bitrate, max fps, mode (video, still only, hybrid) |
| control | Frame (when UDP is unavailable) | server | one encoded picture: display, frame id, keyframe flag, capture and encode timestamps, data |
| control | Still | server | a lossless rectangle: display, rect, compression, data |
| control | CursorShape | server | ARGB image and hotspot, or hidden |
| control | Key / Text | client | key with scancode and keysym and down flag; or a string to type |
| control | ClipboardOffer / ClipboardRequest / ClipboardData | both | formats offered; request one; chunked data by MIME type |
| control | File* | both | list, stat, open, read, write, chunk, close, make folder, rename, remove: mirrors `FileSource` |
| control | GamepadAttached / GamepadDetached / Rumble | both | pad index and kind; rumble strengths back to the client |
| control | RequestKeyframe / Lock / Ping / Pong / Bye | both | what they say |
| datagram | FramePacket | server | frame id, packet index and count, FEC group, slice of the encoded frame |
| datagram | Pointer / PointerRelative | client | absolute position and buttons and high-resolution wheel; or deltas when the client has captured the mouse |
| datagram | GamepadState | client | pad index, buttons, triggers, sticks, sequence |
| datagram | CursorPosition | server | where the pointer is, for view-only viewers |
| datagram | Report | client | frames received and lost, decode and present times, estimated RTT |

### Speed: where the time goes

The target is the experience of Moonlight or Parsec on the same hardware: on a LAN, glass to glass under about
40 ms at 60 frames a second; over the internet, the network's own one-way delay plus roughly a frame. The budget
per stage, and the technique that buys it:

| Stage | Budget | How |
|---|---|---|
| Capture | one frame at most, no CPU copy | Desktop Duplication API: a GPU texture per changed frame with dirty and moved rectangles; ScreenCaptureKit on macOS; PipeWire or X11 on Linux |
| Colour conversion | under 1 ms | on the GPU (video processor or compute shader) straight into the encoder's input texture |
| Encode | 2 to 5 ms | hardware H.264 in low-latency mode: constant bitrate, no B-frames, one-frame buffer, intra-refresh or keyframes only on request, slices sized for packets |
| Send | under 1 ms | packetize into about 1200-byte datagrams with forward error correction; no waiting to batch |
| Network | the link's delay | UDP so one lost packet doesn't stall the ones behind it; FEC instead of retransmits; bitrate adapts to loss and delay |
| Decode | 2 to 5 ms | the OS hardware decoder, straight to a GPU texture |
| Present | next vsync at most | draw the texture into a swap chain on the canvas; an option to present immediately without vsync |
| Input | under 1 ms plus the network | sent as it happens; mouse coalesced only above 250 Hz; applied with `SendInput` the moment it arrives |

Measurement is part of the work, not an afterthought: `--bench` on the server, timestamps in every frame, and a
`--stats` log on the client that reports capture-to-present time per frame, so every phase can show its numbers.

### Security

- Always encrypted: there is no unencrypted mode to fall back to, so the status bar always says **encrypted**.
- The server makes itself a self-signed certificate at install time; the client pins its fingerprint with the saved
  connection using the dialog and storage it already has for VeNCrypt servers.
- Passwords are checked inside TLS against a salted PBKDF2-HMAC-SHA256 hash kept in `%ProgramData%\Marconi Server`
  with an administrators-only ACL. Five failures lock login out for a growing period and are logged.
- UDP packets carry a session id and are AES-GCM encrypted and authenticated with keys agreed over the control
  channel; anything that doesn't authenticate is dropped silently.
- The service never runs code the client sends. The later "update the server from the client" step (section 7)
  only runs an installer whose signature the server has checked.
- The protocol parser is fuzzed the way `RandomInputTests.cpp` fuzzes the RFB client.

### Running as a Windows service (Phase 3)

A server that is any use shows the login screen, survives a restart, and keeps working through a UAC prompt or the
lock screen. On Windows that means running as SYSTEM in the console session, where the secure desktop can be
captured and input can be injected into it. The design, the same as TightVNC's and UltraVNC's service modes:

- The service (`--service`, account LocalSystem, start automatic) is a supervisor only. It watches the active
  console session and starts `MarconiServer.exe --session <id>` in it with a SYSTEM token, restarting it when the
  console session changes (fast user switching, remote desktop) or it exits.
- The session helper does everything else: listens, logs viewers in, captures, encodes and injects. It switches to
  whichever desktop is active (`OpenInputDesktop`, `SetThreadDesktop`) so UAC prompts and the lock screen are
  seen and usable. Ctrl+Alt+Del goes through `SendSAS`, which needs the "Software SAS generation" policy the
  installer sets.
- A restart of the helper drops the connection for a second; the client's reconnect bar already copes.
- The tray icon runs in the user's own session and talks to the helper over a local named pipe for status
  ("1 viewer connected: Sem's laptop", Disconnect viewer) and settings; changing the password asks for administrator
  permission and runs `--set-password`.

### Gamepads (Phase 7)

- Client: a `Gamepads` interface polled on its own thread (XInput on Windows, 4 pads, with rumble; the Game
  Controller framework on macOS; evdev on Linux). Forwarded while the viewer window is active and **Send Gamepads to
  the Other Computer** is ticked, as `GamepadState` datagrams on change, plus attach and detach on the control
  channel. Rumble comes back and is applied to the matching local pad.
- Server: a `GamepadSink` interface. Windows: ViGEmClient (BSD, vendored) creating an Xbox 360 pad per attached
  client pad through ViGEmBus; Linux: uinput; macOS: unsupported, with the reason shown in the client menu the way
  `SystemKeyCapture::GetUnsupportedReason` does.
- The same phase adds the two things games need from the keyboard and mouse that RFB never had: **relative mouse**
  (the client captures the pointer and sends deltas, the server injects relative motion, Ctrl+Alt releases) and
  **scancodes alongside keysyms**, so WASD is WASD whatever the layouts.

### Installer and the "install on the other computer" flow (Phase 2)

- `Installer/MarconiServer.nsi`, built by `Package.ps1` before the client installer, which then embeds
  `MarconiServer-<version>-win64-setup.exe` as a component ticked by default ("Server installer, for setting up
  other computers"). Both installers share `Sign.cmd`, the art and the license page.
- The server installer: welcome, license, an **access password** page (password and confirmation; stored by running
  `MarconiServer.exe --set-password`, never by NSIS itself), components (Start with Windows; Allow through Windows
  Firewall, default on, via `netsh advfirewall`; later Gamepad support), progress, finish ("Start Marconi Server
  now"). Upgrades stop the service, replace the files and start it again. The uninstaller removes the service, the
  firewall rule and, if asked, the settings and password.
- In the client, connected to a Windows computer: F8 > **Install Marconi Server on the Other Computer...** explains
  what will happen, copies the installer to the other computer's Desktop with the file transfer already in place
  (TightVNC file transfer; SFTP for Macs once a Mac server exists), then says: "Double-click
  MarconiServer-...-setup.exe on the other computer's desktop to install it." When the connection can't carry files
  (TigerVNC, RealVNC, UltraVNC), the dialog offers to copy the download link to the clipboard, which the shared
  clipboard carries almost everywhere, to paste into a browser on the other computer.
- The installer comes from the client's own install folder, or `Build\Installer\` in a development build; if it
  isn't there, only the link is offered.
- Nothing needs re-saving afterwards: the next connect probes, finds the Marconi server on 1895, and prefers it.

## 4. Phases

Every phase: steps are commits, "done when" is checked by hand, tests are added in the same commits as the code,
`CHANGELOG.md` gets its lines, and the phase ends with the MINOR bump.

### Phase 0: agree the plan, verify the engine

1. Owner answers section 1. Steps that depend on an answer wait for it.
2. Verify in the engine (`$UNA_ENGINE_PATH`): does `Crypto` expose a TLS server, AES-GCM, PBKDF2 and SHA-256, and
   which mbedTLS version and TLS versions it builds? Does `Platform` offer anything for threads, named pipes or
   services worth reusing? Record the answers here. Engine additions (a `TlsServer`) are committed to the engine repo
   as `AGENTS.md` says.
3. Decide the test-pattern picture (`--test-pattern`): a moving pattern with a frame counter and a clock drawn in
   large digits, so a photo of both screens gives glass-to-glass latency.

### Phase 1: a server you can connect to (Windows, TCP, lossless)

Goal: `MarconiServer.exe` runs as a tray app on a Windows PC; Marconi connects to `marconi://that-pc`, sees the
desktop update as it changes and controls its keyboard and mouse. No installer yet (copy the exe by hand).

1. **Viewer seam.** Introduce `RemoteSession` (connect, disconnect, framebuffer, cursor, input, clipboard, screens,
   resize, quality, stats, listener) in `MarconiCore`; `RfbSession` implements it over `Rfb::Client`; `ViewerFrame`
   uses only the interface. No behaviour change; existing tests and headless mode prove it.
2. **Wire messages.** `Wire/Stream.h` (little-endian reader and writer, varints), `Wire/Messages.h` with the
   message table, round-trip tests for every message, and a fuzz test for the parser.
3. **Address, probe, choice, discovery.** `marconi://`, port 1895, the greeting `MRCN`, preference over VNC,
   `_marconi._tcp` querying. Tests in the existing files.
4. **Server core skeleton.** `MarconiServerCore`: `Settings` (port, password hash, in the registry or ProgramData),
   `Auth` (PBKDF2 verify, lockout), `Listener` (accept, TLS, handshake, login), `Session` (one viewer; several later),
   the `ScreenSource`, `InputSink` and `ClipboardAccess` interfaces with fakes. `--test-pattern` works end to end
   with a loopback test client before any Windows capture exists.
5. **TLS server side.** `Net::TlsServerStream` over the engine's mbedTLS, a generated self-signed certificate at first
   run, fingerprint pinning in the client through the existing dialog. Tests with a stand-in like `VeNCryptTests`.
6. **Windows capture.** `Capture_Windows.cpp` on Desktop Duplication: per-display, dirty and move rectangles,
   cursor shape and position, display layout from `EnumDisplayMonitors`. Frames are sent as `Still` rectangles
   (zlib with PNG-style row filtering, measured against raw) and `CopyRect`-like moves. `--bench` prints capture and
   compress times.
7. **Windows input.** `Input_Windows.cpp` with `SendInput`: absolute pointer per display, wheel with high-resolution
   deltas, keys by scancode when given, keysym to virtual key otherwise (the inverse of `KeyMapper`).
8. **Client.** `Wire::Client` (worker thread like `Rfb::Client`, same threading rules), `WireSession`, the viewer
   connects by `marconi://` or by probe. Status bar says **encrypted (TLS 1.3)**.
9. **Tray app.** `MarconiServerUI`: tray icon with status, Set password, Port, Quit; a first-run prompt for the
   password. Logs to `%ProgramData%\Marconi Server\logs`.
10. **Tests.** `Tests/MarconiServerTest`: handshake and login (right, wrong, lockout), frames from the fake screen
    arrive in the client's framebuffer, input reaches the recording sink, disconnect and reconnect, protocol fuzz.
    README gains a "Marconi Server" section in plain words.

Done when: on two machines, `marconi://pc` shows the desktop, typing and clicking work, the status bar says
encrypted, and `MarconiCoreTest` and `MarconiServerTest` pass.

### Phase 2: the server installer, and installing it from the client

1. `Installer/MarconiServer.nsi` and the server parts of `Package.ps1` (stage, sign, build, SHA-256), with the
   password page, the firewall rule, Start with Windows (Run key for now), the uninstaller, `/S` behaviour from
   section 1 point 9. Art variants from `Tools/make_installer_art.py` if a "Marconi Server" header is wanted.
2. The client installer embeds the server installer as a component; the client finds it next to `Marconi.exe`.
3. F8 > **Install Marconi Server on the Other Computer...**: the dialog, the copy to the remote Desktop through
   `TransferQueue`, the finishing instructions, the download-link fallback.
4. Settings > **Share this computer...** runs the bundled installer locally.
5. README: the install flow in plain words, and what the server does and doesn't do yet.

Done when: connected to a Windows PC with TightVNC, the menu item puts the installer on its desktop, running it
there (on the remote screen) installs the server, and reconnecting uses the Marconi server without changing the
saved connection.

### Phase 3: a real service

1. `--service` supervisor, `--session` helper as SYSTEM in the console session, restart on session change; the
   installer registers the service and stops using the Run key; upgrades stop and start it.
2. Desktop switching for UAC prompts and the lock screen; Ctrl+Alt+Del via `SendSAS` and the policy it needs.
3. **Lock on disconnect** server side (`Lock` message); the login screen is shown and usable after a restart.
4. The tray icon in the user's session, over a named pipe: status, connected viewer, Disconnect viewer, Set
   password with administrator permission.
5. Tests: the supervisor's session logic with a fake session list; a manual checklist in the README's developer
   section (log out, UAC prompt, lock, switch user, restart) because these can't be automated.

Done when: the PC restarts and Marconi reconnects by itself to its login screen; a UAC prompt and the lock screen
can be used through Marconi.

### Phase 4: video

1. **Decide the Linux decoder** (section 1 point 5) and add the external dependency if needed.
2. `VideoEncoder` interface; `Encoder_Windows.cpp` on Media Foundation: hardware H.264 when available, software
   fallback, low-latency settings, GPU colour conversion from the captured texture. `--bench` prints encode times
   and bitrates for 1080p and 4K.
3. `Frame` messages over the control channel; `StreamConfig` from the picture-quality menu: Best (video plus a
   fast still refresh), Balanced, Low Bandwidth (lower bitrate, frame rate and, if needed, resolution).
4. **Still refresh**: areas unchanged for about 300 ms are resent losslessly; the client composites stills over
   the video until the next frame touches them. Measured on a text-heavy desktop.
5. `VideoDecoder` interface; `_Windows.cpp` on Media Foundation with D3D11, presented through a swap chain on the
   canvas; `_Mac.mm` on VideoToolbox presented with a sample-buffer display layer; Linux per step 1. Screenshots
   read back the composed picture on demand.
6. Frame pacing and the `--stats` log: capture-to-present per frame, frames a second, bitrate; the status bar shows
   frames a second and bitrate for Marconi servers.
7. Tests: encoder and decoder round trips where the platform has them, the still-refresh scheduler with a fake
   clock, `StreamConfig` mapping.

Done when: full-screen video plays smoothly at the display's rate on a LAN with glass-to-glass under about 40 ms
in the `--stats` log, and text is pixel-exact within half a second of stopping.

### Phase 5: UDP

1. `UdpSocket` for IPv6 and connected use; `Keys` over TLS; the UDP `Hello` that opens the client's path; session
   ids; AES-GCM on every datagram.
2. Packetization and forward error correction (XOR parity first, Reed-Solomon if measurements call for it);
   `Report` from the client; `RequestKeyframe` on unrecoverable loss.
3. Pointer, gamepad-ready input and cursor position over UDP; keys stay on TCP.
4. Bitrate adaptation from loss and delay, with a resolution and frame-rate ladder for Low Bandwidth; the
   status bar shows loss and "over TCP" when UDP is blocked and the Phase 4 path is in use.
5. Tests: packetize and recover with simulated loss, key schedule, out-of-order and replayed datagrams rejected,
   the adaptation controller against scripted loss.

Done when: over a real internet link with a few percent loss, the picture keeps moving without stalls and the
mouse stays responsive; pulling UDP falls back to TCP and says so.

### Phase 6: everything the client already does, on our server

Each item is its own step and its own changelog line; they can be pulled forward into earlier phases when a
tester needs them.

1. Clipboard text (UTF-8) and images (PNG on the wire; DIB on Windows), the Send and Get menu actions.
2. File transfer: `Files::MarconiFiles` on the client, `LocalFiles` on the server, drops onto the screen go to the
   Desktop, the file panel works as with a Mac.
3. Several displays: `Displays`, the Display menu, capture of one display.
4. **Match the other computer's screen to this window**: `SetDisplayMode` changes the chosen display's mode and
   restores it on disconnect.
5. Cursor shape and position from the capture, so the pointer never lags.
6. The server's tray settings: who may connect (allow list of addresses), idle disconnect, a notice to the person at
   the computer when someone connects.
7. Several viewers at once (shared), as VNC allows, or one at a time with "take over": owner's choice, asked then.

### Phase 7: gamepads and game input

1. Client `Gamepads` interface, `Gamepads_Windows.cpp` (XInput, polling thread, rumble), the menu item, forwarding
   rules (active window, view-only off).
2. Protocol: `GamepadAttached`, `GamepadDetached`, `GamepadState`, `Rumble`.
3. Server `GamepadSink`; `VirtualGamepad_Windows.cpp` on ViGEmClient; the installer's optional component for
   ViGEmBus (section 1 point 6); plain-words README section on what it installs and why.
4. Relative mouse mode and scancodes; a menu item to release the mouse; the server honours both.
5. `Gamepads_Mac.mm` and `Gamepads_Linux.cpp` on the client; `VirtualGamepad_Linux.cpp` on uinput when the Linux
   server exists.
6. Tests: state packing and sequence handling, rumble routing, the forwarding rules with a fake pad.

Done when: a game on the host plays from the client with a pad, with rumble, at the Phase 4 and 5 latencies.

### Phase 8: macOS and Linux servers

Outline only; planned in detail when reached.

- macOS: ScreenCaptureKit for capture, VideoToolbox for encoding, CGEvent for input, the Screen Recording and
  Accessibility permissions asked for in plain words, a login item or launch daemon, packaged in the existing DMG
  as "Marconi Server.app" or a mode of Marconi.app (owner's choice).
- Linux: PipeWire and the portal on Wayland, X11 with XDamage and XTest otherwise, a systemd user service, a
  package or tarball.
- The client's install menu then offers the right installer per host, using SFTP for Macs.

## 5. Testing throughout

- `Tests/MarconiServerTest` runs with no display: `FakeScreen` (the test pattern), `RecordingInput`,
  `MemoryClipboard`, `FakeGamepads`. Both the server and `Wire::Client` are real; only the platform edges are faked.
- `MarconiServer --test-pattern` plus `marconi marconi://127.0.0.1 --headless --stats` is the manual and
  scripted smoke test, and the way to try protocol changes without a second machine.
- `--bench` numbers are recorded in this file per phase so regressions are visible.
- `Tools/fake_vnc_server.py` stays for the RFB side; a `Tools/wire_dump.py` that decodes a captured Marconi
  control stream is worth writing in Phase 1 for debugging.

## 6. Risks and unknowns

- **Secure desktop capture** as SYSTEM: known to work for TightVNC and UltraVNC; verify with `--bench` running under
  the service early in Phase 3 before building on it.
- **Hardware encoder availability and quirks**: every vendor's Media Foundation encoder behaves a little
  differently (intra-refresh support, D3D11 input, 4:4:4). `--bench` and a software fallback keep the server
  working everywhere; quality tuning per vendor comes from measurements.
- **ViGEmBus's maintenance state** (section 1 point 6).
- **Linux client decoding** (section 1 point 5).
- **mbedTLS performance** for AES-GCM at tens of megabits: fine with hardware AES, to be measured on the owner's
  machines; the engine's `Crypto` module may need the mbedTLS AES-NI option on.
- **Firewall and NAT**: UDP needs the same port forwarded as TCP; the fallback to TCP covers blocked UDP.

## 7. Not in this plan unless the owner asks

- Sound from the other computer (Opus over the datagram channel; WASAPI loopback on Windows).
- A relay or hole-punching service for computers behind NAT with no port forwarding.
- An RFB listener on the server so other VNC viewers can connect.
- Updating the server from the client ("Update Marconi Server on the Other Computer") by uploading the installer
  and having the server run it silently after checking its signature.
- Blanking the host's screen while a viewer is connected.
- A virtual display driver, so a headless host or a laptop with its lid closed can be resized to any window.
- HEVC, AV1 and 4:4:4 colour where both ends support them; Windows GameInput for more than four pads.
- Touch and pen input.

## 8. Record of measurements

Filled in as phases complete: machine, resolution, capture, encode, decode and present times, bitrate, glass to
glass.

| Date | Phase | Machines | Resolution | Capture | Encode | Decode | Present | Glass to glass | Bitrate |
|---|---|---|---|---|---|---|---|---|---|
