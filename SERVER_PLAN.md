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

## Progress so far

Done and merged into `main`, each step building and tested on Linux against the real engine:

- **Phase 1.1** The viewer talks to a `RemoteSession` interface; `Rfb::Client` implements it. No behaviour change.
- **Phase 1.2** The protocol's byte layer: `MarconiCore/Wire` (little-endian stream, varints, the Phase 1
  messages), with round-trip and fuzz tests.
- **Phase 1.3** The client recognises Marconi servers: `marconi://`, port 1895, the `MRCN` greeting, preference
  over VNC, and `_marconi._tcp` discovery.
- **Phase 1.4** `MarconiServerCore`: a `Session` that serves the screen and takes input over the protocol, a
  `Listener`, the `ScreenSource`/`InputSink` interfaces with a `TestPatternScreen`, tested from an in-memory
  viewer and over a loopback socket.
- **Phase 1.8** `Wire::Client` (the client half, a `RemoteSession`), with an end-to-end test of the real client
  against the real server over a socket, and the viewer wired to open Marconi connections.
- **The `MarconiServer` program** (console) serving the test pattern, confirmed by connecting an independent
  client to the running binary.
- **Lean picture:** Still rectangles are zlib-compressed when that is smaller, tested end to end.
- **Windows capture and input** (1.6, 1.7) are written: a GDI screen grab and SendInput, with the server program
  using them on Windows and the test pattern elsewhere. They compile only on Windows, so they are a first cut
  that still needs a build and a run on a Windows machine; GDI stands in until Desktop Duplication (the video
  phase) replaces it.
- **The access password** (the owner's login choice, part of 1.5): the server stores it hashed (PBKDF2-HMAC-SHA256
  with a random salt, on the engine's HMAC-SHA256), checks the viewer's login against it, and locks login out after
  repeated wrong tries; the server program takes `--password`, `--password-hash` and `--make-hash`. Tested, and run
  against the live binary.
- **Encryption (1.5) is done.** The engine's `Crypto` module gained a TLS server (`Crypto::TlsServer`) and
  self-signed identities (`MakeSelfSignedIdentity`, `CertificateFingerprint`), on the same mbedTLS the client's
  `TlsClient` already used. The server sends the four `MRCN` bytes in the clear (so a probe can still spot it), then
  upgrades the connection to TLS before any protocol message; the password check and the whole picture now ride
  inside TLS. The client pins the server's certificate: a fingerprint it already knows connects silently, an unknown
  one goes through the existing "trust this certificate?" dialog. The server makes a self-signed certificate; with
  `--cert-file`/`--key-file` it keeps one identity across restarts (the key is written owner-only), so viewers are
  not asked to trust it again each time, and it prints the fingerprint on start. Tested in `CryptoTest` (server and
  client talk, pinning matches, a broken identity fails) and `MarconiServerTest` (the real client connects
  encrypted; a pinned fingerprint skips the question; a wrong pin that is declined is turned away).
- **The tray app (1.9) is written.** `MarconiServerUI` (wxWidgets) runs the server core on a background thread and
  puts an icon in the system tray with: a status line (what's being shared, the port, whether a password is set, a
  short piece of the fingerprint), set/change password, change port, copy the full fingerprint, and quit. On first
  run it asks for an access password (empty allowed). It keeps its settings, the TLS certificate and key, and its
  logs under a per-user "marconi-server" folder, so the fingerprint stays the same across restarts. The shared
  pieces moved into the core: `IdentityStore` (load-or-create the certificate and key, tested) and
  `LoggingInputSink`. The console `MarconiServer` program stays as a headless way to run and test the same core.
  Built and run on Linux under a virtual display (test-pattern mode): it makes its identity, starts listening, and
  answers a client with the `MRCN` greeting. The Windows screen path still needs a run on a real Windows machine,
  as 1.6/1.7 note.
- **Naming:** the tray project is `MarconiServerTray` (output `Build/MarconiServerTray`) so it doesn't clash with
  the console `MarconiServer`. The installer phase decides the shipped program's name.
- **Version history:** the server arrived in 0.6.0. The project is now at 0.9.7; no commit since 0.6.0 touches the
  server's code or tests (the later versions are client, RDP, mobile and packaging work). It is all on `main`.

Decisions taken as defaults under "get it done" (change any by editing section 1): port **1895**, `marconi://`,
magic bytes `MRCN`, `Wire` namespace, one version for client and server. The server sends the four magic bytes
the instant a viewer connects, before the handshake, so a probe can identify it.

Resequencing from the original plan: 1.8 (client) was done before 1.5 (TLS), since the transport is abstracted
and TLS slots in later; the access password moved from 1.4 into 1.5, because it is checked inside TLS.

## Status at 0.9.7 (checked against the code)

Phase 1 is written but not finished:

- **Linux X11 server works end to end (branch `claude/server-blitz`).** `SystemScreen_Linux.cpp` (XGetImage, 64x64 tile
  differencing, XFixes cursor) and `SystemInput_Linux.cpp` (XTest) compile, `MarconiServerTest` passes (92), and a real
  run under Xvfb showed an xterm window from the server's screen in the desktop client's headless snapshot over the
  encrypted connection. Not yet checked: XTest input arriving, speed, several monitors.
- **Building here:** needs the engine submodules pinned to the engine's recorded commits, plus `libgtk-3-dev`,
  `libsecret-1-dev`, `libxxf86vm-dev`, `libsm-dev`, `libxkbcommon-dev`, `libxtst-dev`, `libxfixes-dev`. Headless client
  runs refuse an untrusted certificate: save a connection with the server's fingerprint and use `--saved`.
- **Not yet run on Windows or macOS.** Everything under `_Windows` and `_Mac` files (GDI capture, SendInput, the
  service, clipboard, display listing and resizing, cursor capture, CGEvent and CGDisplay, the installer scripts) has
  been written but never compiled or run. Phase 1's "done when" (two machines, `marconi://pc` shows the desktop,
  typing and clicking work) has not been checked on Windows.
- **Integration branch `claude/server-integration`** merges the six blitz branches (installer, macOS, files,
  clipboard, displays, service). On Linux it builds and `MarconiServerTest` passes (822 checks, including X11 capture,
  input, displays and clipboard against Xvfb); `MarconiCoreTest` has two connect-timeout failures that look like the
  sandbox's network. See each phase below for what is done and untested.
- **Still to do:** the tray app is not connected to the service's control pipe (Phase 3); video, UDP and gamepads
  (Phases 4, 5 and 7) are not started; Wayland capture; the measurements table in section 8 is empty.
- **Next step:** build and run the server and tray on a Windows machine and fix what it shows.

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
4. **Login. DECIDED (owner): an access password**, settable and changeable, checked inside TLS against a salted
   PBKDF2 hash with a lockout after repeated failures, and the client pins the server's self-signed certificate
   with the existing "trust this certificate?" dialog. Two owner refinements:
   - **It defaults to the password already saved for that connection.** When you install the server from the
     client (Phase 2), the client hands the installer the password it already keeps for that computer, so the
     server works straight away with the password you already use, with nothing new to set. The tray icon can
     change it afterwards.
   - **Injecting it at install must be safe.** The password reaches the new server over the connection that is
     already encrypted (the VNC or Marconi link the installer travelled over), not on a command line (where it
     would show in process lists) and not baked into the installer file on disk. The server reads it once from a
     file only it can read and hashes it immediately; the plaintext is never stored. The risk is low given the
     encrypted channel, but this keeps it from leaking through the install step.
5. **Video codecs from the operating system**, not a bundled ffmpeg: Media Foundation on Windows (hardware H.264
   encoders and decoders from NVIDIA, AMD and Intel are exposed through it, with Microsoft's software ones as
   fallback), VideoToolbox on macOS. H.264 first, HEVC and AV1 later where both ends have them. The open question is
   the **Linux client**: Linux has no system codec API. Options are ffmpeg's libavcodec (LGPL, large) or a small
   software H.264 decoder only. Recommended: ffmpeg via the engine's external dependencies, decided when Phase 4
   starts.
6. **Gamepads on a Windows server need a virtual controller driver.** There is no built-in way to create one; every
   product in this space (Sunshine, Parsec, DS4Windows) uses **ViGEmBus**, a BSD-licensed, signed kernel driver that
   its author stopped maintaining in 2023 but which still works on Windows 11. Recommended: the server installer
   offers it as an optional component, running its own installer, with the risk stated in the README. Alternative:
   tell users to install it themselves. On Linux servers the kernel's uinput does it with no driver. On Mac servers
   it needs a small DriverKit extension of our own that creates a virtual controller (the mechanism Karabiner-Elements
   uses for its virtual keyboard and mouse); Apple has to grant the DriverKit entitlement to Red Marmoset's developer
   account first, and the extension is signed with the Developer ID and notarized like the app. Planned in Phase 8,
   gated on that entitlement.
   Forking ViGEmBus or writing our own drivers is researched in `DRIVER_PLAN.md` (an idea, awaiting the owner).
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
    **The owner's first server is Windows** (point chosen), so the capture and input work targets Windows first
    (Desktop Duplication and SendInput), then macOS (the owner also has a Mac) and Linux.
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
  client pad through ViGEmBus; Linux: uinput; macOS: our own DriverKit HID extension creating a virtual gamepad
  (Phase 8, once Apple grants the entitlement). Until a server has a sink, the client menu says so, the way
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

**Phase 2 status (written on Linux, none of it run on Windows).**

- Done and checked here: `Installer/MarconiServer.nsi` and the client's `Installer/Marconi.nsi` (with the new "Marconi
  Server installer" component) both compile with makensis 3.09 under `/WX`, with a stand-in payload and the
  ShellExecAsUser calls swapped for `Exec` (the plug-in is Windows-only). The tray program's `--set-password-file`
  (UTF-16 or UTF-8, hashes, erases the file, exit code 0 or 1) was run under a virtual display and wrote the hash;
  `ReadPasswordFile` and `ServerInstaller::Find` have tests (MarconiServerTest 104 passed; MarconiCoreTest 1915 passed,
  2 failed in `SocketTests.cpp` lines 89-90, a connect-timeout check that needs an unroutable address; this sandbox has
  no such network, so they are not from this change). The client and tray both build on Linux.
- Needs a Windows run: `PackageServer.ps1` and `Package.ps1` (never executed); the whole installer (pages, UAC, the
  password file hand-off, `icacls` on `%ProgramData%\Marconi Server`, the `netsh` rule, the Run key, the uninstaller,
  upgrade over an older version, `/S`); `ServerConfig`'s ProgramData branch (compiled only on Windows); the F8 flow
  against a real TightVNC (the copy uses the existing `UploadToRemoteDesktop`, which asks once for the drop folder and
  defaults to the Public Desktop); "Share this computer..." (`wxLaunchDefaultApplication` on the setup file, so Windows
  shows its permission prompt).
- Differences from the plan, for the owner to confirm:
  1. **The saved password is not handed to the installer.** Section 1 point 4 says the client passes the connection's
     saved password over the encrypted link. TightVNC's link is usually not encrypted, and sending a password file
     beside the installer would leave it on the other computer's desktop, so the installer asks for a password on its
     own page instead and the dialog says it needn't match the VNC password. The hand-off in the installer is a private
     temporary file read by `MarconiServer.exe --set-password-file` (hashed, erased); a client-supplied file could use
     the same entry point.
  2. **`/S` installs set no password, and the server then lets anyone in** (point 9 says it should refuse every
     connection until a password is set; `Auth` currently allows everyone when no hash is set). The tray's first-run
     prompt appears at its first start and accepts an empty password. Not changed here (server core is other
     agents' ground); decide whether `Auth` should refuse.
  3. **The tray program ships as `MarconiServer.exe`** (built as `MarconiServerTray.exe`), as the plan's name for it.
  4. **Settings folder permissions.** The installer gives administrators and SYSTEM full control and the account signed
     in at the screen (`WTSQuerySessionInformation`, user name only) modify rights, since until Phase 3 the tray
     runs as that user. Any other account falls back to its own folder (its own password and certificate) and would
     also try the same port. Phase 3 (the service) replaces this. A domain account's name may not resolve in `icacls`.
  5. **Firewall rule** is for the program on TCP port 1895 on every profile; a changed port needs the rule changed.
  6. **No server version resource and no icon in the exe yet**, so `PackageServer.ps1` can't check the built
     program's version the way the client's packaging does (the installer's version is Project.json plus the commit
     count). The installer's icon and the shortcut's are the client's.
  7. **Uninstaller's data option** removes only `%ProgramData%\Marconi Server`; a per-user fallback folder stays (its
     exact Windows path from wx was not checked).
  8. The download-link fallback points at the repository's latest release page, not at a file: whether releases carry
     the server installer is the owner's call at publishing time (`PackageServer.ps1` prints its SHA-256).
  9. The F8 item offers file sending for TightVNC or SSH-tunnelled connections only; it can't tell what the other
     computer runs, so it says the server is for Windows.

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

#### Phase 3 status (branch `claude/server-service`)

**Written and tested on Linux** (`Tests/MarconiServerTest/ServiceTests.cpp`, 228 server tests pass):

- `ServiceSupervisor` (`ServiceSupervisor.h/.cpp`): the decision logic behind `--service`, behind the interfaces
  `ConsoleSessionSource` (the active console session id, `kNoConsoleSession` while none) and `HelperLauncher`
  (launch in a session, is it running, terminate). `Tick(nowMs)` starts the helper in the console session, moves it
  at once when the session changes, ends it when there is no console session, and restarts a helper that exited
  after 1 s, 2 s, 4 s... up to 30 s (reset after it stayed up 60 s; a new session is tried at once). Tested with a
  fake list and launcher and a driven clock.
- The tray icon's control pipe messages (`ControlProtocol.h/.cpp`, own type numbers on the Marconi framing):
  Hello/Welcome, Status (version, port, password set, lock on disconnect, console session, active desktop, viewers),
  GetStatus, DisconnectViewer, SetPassword (sends the *hash*, so the password never crosses the pipe), Result. The
  state machine (`ControlSession` for the helper, `ControlClient` for the tray icon) runs over any `Wire::Transport`
  and is tested over an in-memory pipe: Hello first, unknown messages skipped, malformed ones end the connection,
  pushed status, and **Set password is refused unless the pipe says the peer is an administrator** (`PeerInfo`, filled
  by the pipe from the client's token, never by anything the client sends).
- `Lock`: new wire message `Wire::Lock` (type 8, no fields). `Session` calls `InputSink::LockWorkstation()` (default
  does nothing; Windows and Linux sinks call `LockThisComputer()` in `SystemLock_*.cpp`: `LockWorkStation`, or
  `loginctl lock-session` then `xdg-screensaver lock`; none on macOS yet). New setting `Settings::lockOnDisconnect`
  (file key `lock-on-disconnect`): a viewer that got in and then leaves (window closed, connection lost, Bye) locks
  the screen. A server-side end (tray icon's Disconnect viewer, server stopping) does not lock. A Lock message from a
  logged-in viewer always locks, once per viewer. Client: `RemoteSession::SendLock()` (false by default),
  `Wire::Client` sends `Lock`, and the viewer's existing "lock the other computer on disconnect" option uses it
  before falling back to Win+L keys. No new menu item: the plan did not call for one.
- `Listener` now reports the connected viewer (`GetViewer`) and can end it (`DisconnectViewer`, which also shuts the
  socket so the session really ends), and `SetPasswordHash` changes the live login check. `ListenerControlHost`
  connects it to the control pipe.
- `ServiceConfig`: the settings file `server.conf` (port, name, password-hash, lock-on-disconnect) plus `server.crt`
  and `server.key` in the all-users data folder (`%ProgramData%\Marconi Server`; `--data-dir` overrides).
- `SessionHost` (`--session <id>`): loads the settings and identity, captures and injects, listens, serves the
  control pipe, exits cleanly on SIGINT/SIGTERM. Run on Linux with `--test-pattern --data-dir D --port N` it starts,
  serves and stops (the control pipe is Windows only, so it logs that and goes on).
- Server program modes: `--service`, `--session ID`, `--data-dir`, `--set-password` (password from standard input or
  the `MARCONI_SET_PASSWORD` environment variable, never the command line; refuses an empty one), `--install-service
  [--no-start]`, `--remove-service`, `--lock-on-disconnect` (console mode), and `--control-status`,
  `--control-disconnect ID`, `--control-set-password`, which make the tray icon's requests from a command line.

**Written for Windows, never built or run** (headers say so): `ServiceHost_Windows.cpp` (Service Control Manager
entry point, `WTSGetActiveConsoleSessionId`, helper start with `DuplicateTokenEx` + `TokenSessionId` +
`CreateProcessAsUser` in `winsta0\default`, kill-on-close job object, install with failure actions and the SAS
policy, remove), `DesktopSwitch_Windows.cpp` (`OpenInputDesktop`/`SetThreadDesktop` per thread, at most every 50 ms;
`SendSecureAttention`), `ControlPipe_Windows.cpp` (overlapped named pipe `\\.\pipe\MarconiServerControl` with
SYSTEM/administrators full and interactive users read-write, remote clients refused, administrator check from the
client process's token), `SystemLock_Windows.cpp`, plus edits in `SystemScreen_Windows.cpp` (follow the desktop, take
the device contexts again after a switch) and `SystemInput_Windows.cpp` (follow the desktop, `LockWorkstation`,
Ctrl+Alt+Del detection: Delete down with Ctrl and Alt held asks for the secure attention sequence instead of being
injected). The manual checklist is in the README under "Testing the Windows service by hand".

**Tray icon and the control pipe (branch `claude/server-tray-pipe`; Linux-tested, Windows pipe path never run).**

- `TrayMode.h/.cpp` (portable): the polling state machine behind two small interfaces, `Connector` (opens a link to a
  helper) and `Link` (GetStatus, DisconnectViewer, SetPasswordHash), with an injected clock. Modes: `OwnServer` (nothing
  answers: run the server in-process with the per-user settings, exactly as before), `StatusOnly` (a helper answers:
  no listener is started, so it can't fight the service for port 1895) and `ServiceUnreadable` (something answers but
  speaks another protocol version or breaks off during the hello: no listener either, the menu says why). The first
  poll is immediate; then every 3 s. Leaving `StatusOnly` for `OwnServer` needs 3 failed polls in a row (a poll that
  fails and reconnects cleanly counts as nothing); going to `StatusOnly` is immediate. The mode-changed handler is
  where the app starts or stops its own server.
- `StatusOnlyMenu()` is the menu in the two helper modes (status lines, "Lost contact..." while polls fail, Disconnect
  viewer, Set password / Change password, Copy full fingerprint, Quit that leaves the server running). The wx menu in
  `TrayIcon.cpp` is built from it. Set password in status-only mode hashes what was typed and sends it with
  `SetPassword`; the helper's answer is turned into plain words (`DescribePasswordResult`): a refusal says only an
  administrator can change it and to start the tray icon with Run as administrator. Nothing is saved in the user's
  own settings in that mode.
- `TrayTransportLink.h/.cpp`: `TransportConnector` wraps any `Wire::Transport` factory with the existing `ControlClient`
  (the real Windows pipe is `NamedPipeFactory()` = `ConnectToControlPipe`, so `ControlPipe_Windows.cpp` is untouched).
  A pipe that can't be opened counts as "nothing answers" (the Windows code can't tell "not there" from "access
  denied"; both leave the tray icon running its own server).
- `ControlProtocol`: `ServerStatus::fingerprint` added at the end of the Status message (old readers ignore it; a
  Status without it reads as empty; `kVersion` not bumped). `ListenerControlHost` fills it. `ControlClient` has
  `WasVersionMismatch()`.
- `ControlSocket.h`, `ControlSocket_Posix.cpp`: the same control messages over a Unix domain socket (macOS, Linux),
  with a developer choice of who counts as an administrator (`SocketAdmins`: nobody, everybody, root). Server option
  `MarconiServer --control-socket PATH [--control-admin yes|no]` serves it from the console mode; the `--control-*`
  commands and the tray program (`--control-socket PATH`) connect to it instead of the pipe. `ControlSocket_Windows.cpp`
  is a stub that only says it is for macOS and Linux (**not built on Windows**).
- Tray program options: `--control-socket PATH` and `--dump-menu` (prints `MODE ...` and `MENU | label | ...` lines,
  the real wx menu's labels, whenever the mode or status changes).
- Tests (`Tests/MarconiServerTest/TrayTests.cpp`, MarconiServerTest 951 passed): pipe absent gives own-server,
  present gives status-only, both ways with a fake clock, no flapping on a brief failing poll, version mismatch (a
  different Welcome and a refused Hello) falls back to `ServiceUnreadable` and never to a competing listener, the
  menu labels, the Set password refusal text, and an end-to-end test on Linux: a real `Listener` with
  `ListenerControlHost` on a Unix socket, the tray logic seeing the viewer, Disconnect viewer really ending it, the
  refusal for a non-administrator, then the socket stopping (own-server) and returning (status-only, Set password
  applied).
- **Not verified:** that the Windows pipe client behaves the same as the Unix socket under this logic (timeouts: the
  poll runs on the UI thread, so a helper that accepts the pipe and then never answers would freeze the menu; the
  pipe's own read has no timeout that this code adds); the tray icon on Windows at all; Run as administrator giving
  the pipe an administrator token.

**Decisions made here (please check):**

- Ctrl+Alt+Del: the helper sets the named event `Global\MarconiServerSAS` and the *service* calls `SendSAS(FALSE)`,
  because `SendSAS` is documented for services; the helper falls back to calling it itself. The installer (or
  `--install-service`) sets `SoftwareSASGeneration` = 1 (services). The meaning of the values and whether a SYSTEM
  helper that is not a service may call it are from memory of the documentation: verify on Windows (checklist 8).
- The administrator check reads the connecting process's token (`GetNamedPipeClientProcessId`), not
  `ImpersonateNamedPipeClient`, which needs a message read first. A user in the Administrators group who has not
  elevated counts as not an administrator, so Set password asks for elevation. The process id could in theory be
  reused between connect and check: an impersonation check after the first message would close that.
- Stopping the helper is `TerminateProcess` (viewers drop and reconnect). A gentler stop is possible later.
- The helper is started from the service's own SYSTEM token moved to the console session; the plan's TightVNC-style
  alternative (the winlogon token) is not used.
- Session 0 is not special-cased: if the console session id ever is 0 the helper starts there and will probably see
  nothing. Unverified either way.
- The helper's settings come from the file only (`--lock-on-disconnect` applies to the console mode).

**Not done:**

- **The tray icon does not use the pipe yet.** `ServerUI` still runs the server itself with its per-user settings. It
  needs: on start, `Control::ConnectToControlPipe`; if that works, run in status-only mode (menu shows the status and
  the viewer, Disconnect viewer, Set password via `--control-set-password` elevated or by reconnecting elevated) and
  never start a listener of its own (the port would clash with the helper's); if no helper is running and the service
  is not installed, keep today's behaviour. Its autostart (Run key) is then replaced by the service for the server
  and kept only for the status-only tray icon. The pipe can be tried today with the `--control-*` modes.
- ~~A log file for the service and helper~~ done, see "Log files and the Linux package" below.
- **Tray icon and the pipe (branch `claude/server-tray-pipe`).** Done, see "Tray icon and the control pipe" below. Still
  open: the tray icon's autostart (Run key) is not changed by this: the installer should keep it only for the
  status-only tray icon once the service runs the server.
- A log file for the service and helper (they log through `LOG_*`; nothing writes it to the data folder yet).
- Everything under "Written for Windows".
- The service's data folder permissions: the installer must restrict `%ProgramData%\Marconi Server` to SYSTEM and
  administrators (it holds the password hash and the private key).

**Installer notes** (for `Installer/MarconiServer.nsi`, which another branch creates; none of this is in the repo
yet):

- Create `$COMMONAPPDATA\Marconi Server`, lock it down: `icacls "$COMMONAPPDATA\Marconi Server" /inheritance:r /grant:r
  "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F"`.
- Password page: NSIS cannot feed standard input, so set the environment variable `MARCONI_SET_PASSWORD` in the
  installer process (`System::Call 'kernel32::SetEnvironmentVariable(t "MARCONI_SET_PASSWORD", t "$Password")'`),
  run `nsExec::Exec '"$INSTDIR\MarconiServer.exe" --set-password'`, then clear the variable. Nothing is written to disk
  or a command line. (The Phase 2 plan's `--set-password` is this mode.)
- Service: `"$INSTDIR\MarconiServer.exe" --install-service` (installs or updates, sets the policy, starts it; use
  `--no-start` to start later). "Start with Windows" off: `sc config MarconiServer start= demand`. Do not use a Run
  key for the server any more (the Phase 1 tray app that runs the server itself would also try to take port 1895).
- Upgrade: close the tray icon, `net stop MarconiServer` (the helper dies with it, releasing the exe), replace the
  files, `--install-service` again.
- Uninstall: close the tray icon, `"$INSTDIR\MarconiServer.exe" --remove-service` before deleting files; remove the
  data folder only if the person asks.
- Firewall: a program rule, `netsh advfirewall firewall add rule name="Marconi Server" dir=in action=allow
  program="$INSTDIR\MarconiServer.exe" enable=yes profile=any` (the helper is this same exe).

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

#### Phase 4 status (branch `claude/server-video`)

**Built and tested on Linux with a test-only codec: the whole pipeline except a real codec.** The Windows Media Foundation
encoder and decoder and the macOS VideoToolbox decoder are written but have never been compiled; the Linux decoder is the
ffmpeg slot below, for the `claude/ffmpeg` branch. Nothing here has produced a real H.264 picture yet. Hardware
encoding, GPU colour conversion, GPU presentation, scaling a too-big picture down, `--bench` and the glass-to-glass
numbers are not done (steps 2, 5 and 6 of the list above are done only as far as written here).

**Decision recorded: how video and the Still rectangles share the screen.** A viewer that asked for video gets a hybrid:
- *Moving parts are video.* A change that covers more than 1/32 of the picture is encoded as video. A smaller change (typing,
  a menu, a blinking cursor) goes straight out as lossless Stills, because video would blur small text for a moment for
  nothing (`VideoSessionOptions::smallChangeDivisor`).
- *Still parts are sharp.* Every area sent as video is sent again as a lossless Still once nothing on the screen has changed
  for 300 ms (`videoStillDelayMs`). While something keeps changing, the refresh waits.
- *The viewer copies only what the frame says.* Each `VideoFrame` lists the rectangles of its picture to copy into the
  framebuffer (`applyRects`), so a Still that already made some text sharp is not painted over by a blurry video picture of
  the same text. The decoder always sees whole pictures; only the copy is selective. A rectangle with no area means "copy
  nothing" (a keyframe sent only so a viewer's decoder can recover).
- *Modes* (`StreamConfig.mode`): `Hybrid` is all of the above; `Video` is video only (no small-change Stills, no refresh);
  `StillOnly` is the old path and always works. A viewer can change mode at any time; going back to `StillOnly` ends the
  stream with `VideoStop(Switched)` and the server sends the whole picture as Stills.
- *Why not a clean switch.* A clean switch (video while the screen moves, then stills of everything) re-sends the whole
  screen on every pause and flashes blurry-to-sharp across all of it, even when only a video in one corner moved. Hybrid
  costs a little bookkeeping (the dirty rectangles the encoder is told about) and keeps a static window sharp while another
  one plays video.
- Known limits of this choice: the dirty rectangles come from the capture (Linux X11 reports tiles; Windows GDI reports the
  whole screen or nothing, so on Windows a hybrid stream today is "everything is video, then everything is sharp after
  300 ms"); the sharp refresh is one rectangle per merged region, so a big region is a big message.

**Protocol** (`Wire/WireVideoMessages.h`, numbers 80 to 85, all on the control channel; Phase 5 moves `VideoFrame` to datagrams).
Negotiation: the viewer sets `Capability::Video` in Hello only if it has a decoder and was asked to (`--video`); the server
grants it only if it has an encoder (`Welcome`). After the login the viewer sends `VideoOffer` (its codecs in order of
preference, biggest picture, fastest rate) and `StreamConfig` (mode, maximum bitrate, maximum frame rate). The server picks the
viewer's first codec it can encode and answers `VideoStart` (new stream id, codec, picture size, frame rate, starting bitrate,
the desktop area the picture shows, codec set-up bytes), or `VideoStop` with the reason (no codec in common, no encoder, a
picture bigger than the viewer's decoder takes) and goes on with Stills. `VideoFrame`: stream id, frame number (1, 2, ... in
the stream, so a gap shows a loss), keyframe flag, capture time (server clock, microseconds), encode time, `applyRects`, payload.
A change of picture size or place (a resolution change, another display) is a new `VideoStart` with a new stream id and a
keyframe. `VideoRequestKeyframe` (stream id, reason, last good frame) from the viewer; the server serves it at most once per
100 ms. `VideoFeedback` every half second (frames received and dropped, average decode time, queue depth, and the viewer's
wishes: target bitrate, frame rate, quality 1 to 100); `VideoStop` either way ends the stream (from the viewer: "I can't
decode"). Unknown codec numbers and message types are skipped, so newer peers can offer more.

**Server** (`MarconiServerCore`): `VideoEncoder` interface (`VideoEncoder.h`: configure, extradata, encode a capture area, force
keyframe, set bitrate, flush), `VideoSession` (negotiation, hybrid decision, pacing, still refresh, keyframe throttling,
encoder failure fallback after 3 failures in a row, `--stats` log), `VideoRateControl` (the bitrate rule is written in its header:
the lowest of the wishes is the ceiling, congestion cuts to three quarters, four clean reports raise by an eighth). The
`Session` owns one `VideoSession` per viewer that was granted video; encoders come from `CreatePlatformVideoEncoder` (only the
Windows one exists, and only when switched on, see below) or from `Session::SetVideoEncoderFactory` (tests).

**Viewer** (`MarconiCore`): `VideoDecoder` interface (`Video/VideoDecoder.h`), `VideoReceiver` (follows the stream, spots lost
frames from the frame numbers and asks for a keyframe once, retries after a second, gives up and says `VideoStop` after five
failed decodes in a row, sends feedback), and `Wire::Client` copies decoded frames into the existing `Framebuffer` and reports
them through `OnFramebufferUpdated`, exactly like Stills. `ClientOptions::offerVideo` is off by default; `--video` turns it on in
the desktop app and `--stats` logs the viewer's frames a second, bitrate and decode time.

**Platform backends are switched off until someone with the machine turns them on**
(`Modules/MarconiCore/Video/VideoBackendSwitch.h`: uncomment `MARCONI_VIDEO_WINDOWS` or `MARCONI_VIDEO_VIDEOTOOLBOX`), so that an
error in code written blind cannot stop the Windows or Mac build. With them off, a Windows or Mac server does not offer video and
a viewer does not ask for it: everything is Stills as before. Files: `VideoEncoder_Windows.cpp` (Media Foundation H.264,
hardware transform first with an event loop, Microsoft's software one second, no B-frames, CBR, GOP from `keyframeInterval`,
keyframe on request, bitrate change at run time, CPU BGRX-to-NV12), `VideoDecoder_Windows.cpp` (Media Foundation decoder
transform, software, one frame in and one out, NV12 to 0x00RRGGBB on the CPU), `VideoDecoder_Mac.cpp` (VideoToolbox, Annex B to
AVCC, needs the frameworks added to the manifest). The first things to check on a machine are listed at the top of each file.

**The ffmpeg decoder slot (for branch `claude/ffmpeg`).** Add `Modules/MarconiCore/Video/VideoDecoder_Ffmpeg.cpp` implementing
`Marconi::Video::VideoDecoder` for `Wire::VideoCodec::H264` (HEVC and AV1 can follow; add them to the list it returns), plus the
two functions declared in `VideoDecoder.h` under `MARCONI_VIDEO_FFMPEG`: `FfmpegDecodableCodecs()` and
`CreateFfmpegVideoDecoder(codec)`, and make the module build define `MARCONI_VIDEO_FFMPEG` (so `VideoDecoderFactory.cpp` picks it
up; it does nothing else). What is expected of it:
1. Input is H.264 in Annex B (start codes), one `VideoFrame` payload per `Decode` call, no B-frames, parameter sets (SPS and PPS)
   repeated in every keyframe. `Configure` also gets the stream's `extradata` (Annex B SPS and PPS; may be empty): pass it to the
   decoder (or feed it first) so a keyframe without in-band sets still decodes.
2. Low latency: open the codec with `AV_CODEC_FLAG_LOW_DELAY`, `AV_CODEC_FLAG2_FAST`, no frame threading (`thread_count = 1`, or
   slice threading only), and no output reordering. Each call is `avcodec_send_packet` then `avcodec_receive_frame`; return
   `DecodeStatus::Picture` when a frame came out and `NeedMoreInput` on `EAGAIN`. A frame a call late is a delay, not an error, but
   the viewer shows it that much later, so the decoder must not hold frames back on purpose.
3. Output is `DecodedPicture`: 0x00RRGGBB, one `uint32_t` per pixel, row by row, no padding, cropped to the coded picture's real
   size (`width`/`height` of the frame, not a padded size). Convert with `sws_scale` (YUV to BGRA, BT.709, matching the server's
   NV12 conversion) or an equivalent; keep the `SwsContext` between frames and rebuild it when the size changes.
4. `Reset()` flushes (`avcodec_flush_buffers`) and the next frame is a keyframe. A frame that does not decode returns `Failed`
   with a sentence in `error`; the viewer then asks the server for a keyframe by itself, so the decoder needs no recovery logic of
   its own, and must never crash or leak on corrupt input (the receiver does feed it whatever the server sent).
5. Used from one thread (the client's worker thread) and created once per stream (`VideoStart`); `Configure` may be called again
   on the same object. No global state that two viewers in one process would share. Frame-copy cost counts: at 1080p the whole
   conversion should stay under about 4 ms.
6. It is built only where ffmpeg is built; other platforms keep an empty list, so the viewer there does not offer video.
7. Test it with the loopback tests' shape: `Tests/MarconiServerTest/VideoTests.cpp` shows how a decoder is driven; an ffmpeg test
   needs an H.264 stream, which can be made by the ffmpeg encoder in the same branch or checked in as a few hundred bytes (an IDR
   frame and a P frame of a small picture), decoded and compared with a tolerance.

**What was run on Linux (`MarconiServerTest`, `MarconiCoreTest`).** Every message round-trips, bad values are refused, every
truncation of a valid message is refused, and 3000 random bodies plus every single-bit flip of a valid frame decode or are
refused without crashing. With the test codec (lossy, deterministic, sequence-checked so a receiver that forgets to ask for a
keyframe fails the test) and a fake clock: negotiation picks the viewer's first common codec; still-only and back; a lost
frame and a lost first keyframe recover through one keyframe request each (throttled to 100 ms); a decoder-refused frame asks for
a keyframe; a request for an old stream is ignored; a resolution change and a display change start a new stream; no common
codec, a picture too big for the viewer, an encoder that won't start and one that fails mid-stream, and a viewer whose decoder
refuses, all fall back to Stills; hybrid sharpening exactly 300 ms after the last change, not before, once only; small changes
as Stills; video-only mode; pacing at 30 and 60 frames a second and a change that arrives early not being lost; bitrate
hints (maximum, congestion, clean reports, a frame-rate wish, a stale stream's report ignored); the `--stats` line and its
numbers; the real `Session` over a pipe (no encoder means no grant; the encoder is released when the viewer goes away); and the
real client against the real listener over TLS (video starts and fills the framebuffer, then switching to still-only gives exact
pixels). **Not run:** any real H.264, any Windows or Mac code, a Wayland or multi-viewer case, UDP, a long soak, real timing.
`--bench` and the Phase 4 "done when" numbers do not exist yet.

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
   **Built (Linux tested, Windows not).** The messages were already in the protocol (`ClipboardOffer`, `ClipboardRequest`,
   `ClipboardData`; text as UTF-8, image as PNG; limits 4 MiB text and 32 MiB PNG, `Wire::kMax...`). Flow as with VNC's
   extended clipboard: the side whose clipboard changed sends an Offer, the other asks for the formats it offers, the
   data comes back; a viewer's Get asks for both formats and the server answers with empty data for what it lacks.
   `ClipboardAccess` (read, write, change handler) is in `MarconiServerCore`, wired into `Session` through
   `SetClipboard` (the Welcome grants the Clipboard capability only when one is set; `Listener::SetClipboard` passes it
   on). `Session` tells its own writes from real changes by hashing the content, so nothing is offered back in a loop.
   `Wire::Client` implements `OfferClipboard`, `RequestServerClipboard` and `OnServerClipboard` (the viewer needed no
   change); DIB and PNG convert in `Wire/WireImage.*` (24 and 32 bits per pixel).
   X11 (`ClipboardAccess_Linux.cpp`): the CLIPBOARD selection, XFixes for changes, an event thread to own it and serve
   pastes, reads on a short connection of their own (INCR handled). Tested against a real Xvfb with xclip: text and
   image both ways, a 3 MB text, no echo (see `Tests/MarconiServerTest/ClipboardTests.cpp`: run it with
   `MARCONI_LIVE_PORT`, `MARCONI_LIVE_TMP` and `DISPLAY` set against a running `MarconiServer`).
   **Not tested:** `ClipboardAccess_Windows.cpp` (never built or run; text CRLF conversion, CF_DIB, the format
   listener) and the macOS stub (`ClipboardAccess_Mac.cpp` returns none). **Known limits:** serving a paste larger
   than the X server's maximum request size is refused (no INCR when serving); images are written as 24-bit
   PNG without transparency; only the CLIPBOARD selection, not PRIMARY; `ServerUI`'s controller doesn't pass the
   clipboard on yet (the console server does); a clipboard read can block the input thread for up to 2 seconds
   when the owning program is slow.
2. File transfer: `Files::MarconiFiles` on the client, `LocalFiles` on the server, drops onto the screen go to the
   Desktop, the file panel works as with a Mac.
   **Step 2 is written (branch `claude/server-files`), run only on Linux over loopback.**
   - Messages: `FileRequest` 64, `FileResult` 65, `FileOpen` 66, `FileOpened` 67, `FileRead` 68, `FileData` 69,
     `FileWrite` 70, `FileClose` 71, `FileWriteFailed` 72 (`Wire/WireFileMessages.*`). Metadata calls are one
     request and one answer carrying an id (so the panel and the queue can work at once); contents move in ranges of
     at most 1 MiB. Reads keep two 256 KiB ranges in flight; writes are not answered, a failure comes back as
     `FileWriteFailed` and again at close. Resume: `FileOpen` mode Resume keeps the first N bytes and carries on
     (`MarconiFiles::OpenWriteResume`, `OpenReadAt`); `TransferQueue` does not use them yet, so nothing resumes by
     itself. Extra operation `Desktop` tells the viewer where dropped files go (the Desktop folder of the user the
     server runs as, else their home), so the viewer never guesses.
   - Server: `FileService` (one thread per viewer, so disk work never holds up the picture or keys),
     `GuardedFiles` over the existing `Files::LocalFiles`, and `FilePaths` (clean absolute paths only: no `..`
     above the top, no backslashes, control characters, bad UTF-8, over-long names, Windows device names and
     `C:` oddities). `Settings::filesEnabled` (default OFF, owner's choice: the tray's "Let viewers copy files" item or `--files` turns it on) and `Settings::fileRoot` (default none:
     the whole computer; `--file-root PATH` confines viewers to one folder, with links that point out refused).
     Only plain files open (a named pipe or device is refused), 32 open files at most, writes must arrive in
     order (no holes), a listing is capped at 200,000 items.
   - Viewer: `Wire::Client::GetFiles()` and `SupportsFiles()`, and the F10 panel's `SetMarconiFiles`, so the panel
     and drops onto the screen use it without a second login. The desktop app compiles with this; the panel has
     **not been clicked through**.
   - **Untested:** anything on Windows or macOS (drive listing in `GuardedFiles.cpp`, Windows path rules beyond the
     parser's unit tests, the Desktop folder under a service account); the F10 panel against a real server; a disk
     that fills; very slow links (the 120 second answer timeout); the symbolic-link checks have a race between the
     check and the open; folder drops copying many files (that is `TransferQueue`'s, as with other servers).
     Measured on this Linux box: 24 MiB up in about 1.0 s and down in about 1.1 s over loopback with TLS in a Debug
     build.
3. Several displays: `Displays`, the Display menu, capture of one display.
3. Several displays: `Displays`, the Display menu, capture of one display. **Done (branch `claude/server-displays`).**
   `ScreenSource::GetDisplays` reports the layout (Linux: RandR 1.5 monitors, primary first, id = the monitor's name
   atom; Windows: `EnumDisplayMonitors`, id = the number in `\\.\DISPLAYn`). The viewer's choice goes through
   `RemoteSession::SelectDisplay` to `SelectDisplay`: the server then sends only that display's pictures (clipped,
   still in desktop coordinates, so the pointer and the viewport need no change) and the whole display again at once;
   0 is the whole desktop. The server sends a new `Displays` when the layout changes. Tested: loopback tests with a
   scripted fake screen, and on Linux under Xvfb with two virtual monitors made by `XRRSetMonitor`.
4. **Match the other computer's screen to this window**: `SetDisplayMode` changes the chosen display's mode and
   restores it on disconnect. **Done on Linux; Windows written, not built.** `Capability::ResizeDisplay` is granted
   only when the source can do it; the answer to a `SetDisplayMode` is the new message `DisplayModeResult` (type 26)
   and, on success, `Displays`. Linux changes the monitor's first output to the *listed* mode nearest the request
   (never invents one), resizes the X screen to hold every active output, and puts the CRTCs and the screen size back
   when the viewer leaves (a crashed server leaves the changed mode in place). Display id 0 means the display being
   watched, else the first. Windows uses `ChangeDisplaySettingsEx` without saving to the registry and restores with a
   null mode. Not done: rotated outputs are refused; a virtual monitor without an output can't change.
5. Cursor shape and position from the capture, so the pointer never lags. **Done on Linux; Windows written, not
   built.** `ScreenSource::GetCursor` now returns true only when the image is new (XFixes cursor-change events on
   Linux; the cursor handle on Windows, drawn on black and white to get the alpha); the new `CursorPosition` message
   (type 25, control channel until Phase 5) carries the position when it moves. The client forwards the shape through
   the existing `OnCursorShape` and the position through the new `SessionListener::OnCursorPosition`; the viewer does
   not draw a remote pointer from the position yet (its own pointer, with the server's shape, never lags).
6. **Done on Linux (branch `claude/server-rules`); tray not clicked through.** The server's tray settings: who may connect (allow list of addresses), idle disconnect, a notice to the person at
   the computer when someone connects.
   - Allow list (`AllowList.*`): IPv4 and IPv6 addresses and CIDR ranges, held as 16 bytes so an IPv4-mapped IPv6 peer
     matches an IPv4 entry; `127.0.0.1` and `::1` are separate entries. Empty list = anyone (unchanged). Checked in
     `Listener::ServeOne` straight after accept (the peer address comes from the new `Net::Socket::GetPeerAddress`),
     before the magic bytes, TLS or any message; the refused peer just sees the connection close, the log says so.
     A list with an unreadable entry stops `Start` (never quietly allows everyone). Changes apply to the next
     connection; a connected viewer is not dropped. The listener is IPv4-only today, so IPv6 entries only matter once
     it is dual-stack.
   - Idle disconnect: `Settings::idleMinutes` (0 = never), `IdleWatch` with an injectable clock. Any message except
     Ping and Bye counts as the viewer being there. At the limit `Session` sends `Bye{reason}` ("Disconnected because
     nothing was done for N minutes.", which the viewer already shows as its disconnect reason) and ends; the
     listener's end handler shuts the socket. With lock-on-disconnect on, an idle viewer's end locks the screen too.
   - `ConnectionNotifier` (core, no wx): `Connected` (after login), `Disconnected`, `IdleDisconnected`, with viewer
     name and address; `DescribeNotice` is the shared plain-words text; `RecordingNotifier` for tests. The tray's
     `TrayNotifier` shows a `wxNotificationMessage` via `CallAfter`; the console prints the line with `--notify`.
   - Settings: `allowedAddresses`, `idleMinutes`; flags `--allow` (repeatable), `--idle-minutes`, `--notify`; tray
     keys `AllowedAddresses`, `IdleMinutes`, `NotifyOnConnect` (notices default ON in the tray); menu "Who can
     connect...", "Disconnect idle viewers after" and "Tell me when someone connects".
   - Tests: `Tests/MarconiServerTest/RuleTests.cpp` (parser, matcher, idle clock, session, and over a real loopback
     socket: excluded 127.0.0.1 is refused with no bytes sent, included connects, live list change).
   - **Untested:** the tray dialogs and the notification (never shown, needs a desktop; `UseTaskBarIcon` is compiled
     on Windows only); Windows; the Windows service/session helper does not read these settings yet
     (`SessionHost`, `ServiceConfig`); an IPv6 peer over a real socket.
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

Done when: a game on the server plays from the client with a pad, with rumble, at the Phase 4 and 5 latencies.

#### Phase 7 progress: steps 1, 2, 3, 5 and 6 written; tried with stand-ins only (branch `claude/server-gamepads`)

What exists, all behind interfaces so a platform piece can be replaced without touching the rest:

- **Protocol** (`Wire/WireGamepadMessages.*`, message numbers 80 to 84; the capability bit `Capability::Gamepads` was
  already reserved at bit 4 and is used as it is): `GamepadConnect` (slot 0 to 3, kind Xbox/PlayStation/Generic, name),
  `GamepadDisconnect`, `GamepadState`, `GamepadRumble` (server to viewer) and `GamepadResult` (the server's answer to a
  connect: Ok, NoDriver, TooMany, NotSupported, Failed, with a plain-words sentence). A state is 16 bytes and carries the
  controller's whole state plus a 16-bit sequence number that wraps, so it can move to the datagram channel later: a
  lost one is harmless and a late one is ignored (`IsNewerSequence`). Buttons use XInput's bit values (the Guide button is
  0x0400), sticks are -32768 to 32767 with up positive, triggers 0 to 255, battery 0 to 100 or 0xFF for unknown. Decode
  refuses a slot of 4 or more, cuts bits that aren't buttons, and limits the name length. Round-trip and fuzz tests
  are in `Tests/MarconiCoreTest/WireGamepadTests.cpp`.
- **Server** (`MarconiServerCore`): `VirtualGamepad.h` is the interface (`VirtualGamepadBackend::Create` makes a pad and takes the
  rumble callback; `VirtualGamepad::Update` sets its whole state; destroying it removes it). `GamepadHost` is made per
  viewer: it maps each viewer pad to a virtual one, limits the count (4), ignores states for an unconnected slot or with a
  sequence that isn't newer, and on disconnect, replacement or the viewer leaving, sets the neutral state before removing
  the pad (no stuck buttons). `Session` grants the capability when the listener has a backend, and a heartbeat state
  with no change does not count as the viewer being active (so the idle limit still works). The backend is made by
  `CreateSystemGamepadBackend()` and given to the listener only when the real screen is served (not the test pattern).
  When a backend can't make a pad, the viewer is told why in the `GamepadResult`.
  - Linux `VirtualGamepad_Linux.cpp` (+ `UinputMapping.h`, `UinputMapping_Linux.cpp`): an Xbox 360 style device (USB IDs
    045e:028e) with xpad's evdev codes and ranges, the d-pad as the hat axes, Y inverted as xpad does, and force feedback
    (upload, play, erase, gain, lengths) turned into rumble messages. **Never run against the kernel**: this machine has no
    `/dev/uinput` (the kernel has `CONFIG_INPUT_UINPUT` off). The mapping and the rumble bookkeeping are unit tested; the
    device creation and the force feedback handshake are not.
  - Windows `VirtualGamepad_Windows.cpp`: **not built on Windows.** A small dynamic binding of ViGEmClient
    (`vigem_alloc`, `vigem_connect`, `vigem_target_x360_alloc`/`_add`/`_update`, the notification callback for rumble),
    loaded with `LoadLibrary("ViGEmClient.dll")` so the server runs without the driver and says "Game controllers need the
    ViGEmBus driver, which isn't installed on this computer" when asked. All ViGEm-specific code is in this one file. To
    check: ViGEmClient is normally a static library, so a `ViGEmClient.dll` has to be built from its source (MIT) and shipped
    next to the server, or this file changed to link it; the function signatures and the 12-byte report layout come from
    memory of its header, not from a compile. The installer does not offer ViGEmBus yet (Phase 7 step 3's installer part).
    Every kind of controller is made as an Xbox 360 pad; a DualShock 4 target (`vigem_target_ds4_alloc`) is not used.
  - macOS `VirtualGamepad_Mac.cpp`: says "Game controllers aren't available on a Mac yet." Not built on a Mac.
- **Viewer** (`MarconiCore/Gamepad`): `GamepadSource` (slots, `Read` the state of a slot, `SetRumble`), `GamepadLink`
  (what the forwarder needs from a connection; `Wire::Client` implements it), and `GamepadForwarder`, which does the
  rules: announce a pad when it appears, send a state when it changes (at most one every 8 ms per pad) and every 250 ms as
  a heartbeat, and when forwarding stops (setting off, window not in front, view only, no server support) or a pad is
  unplugged, send a neutral state and a disconnect. A refusal is shown once in the status bar and not retried until the
  pad is replugged. Rumble is applied to the matching pad, ignoring one with an older sequence. The window
  (`ViewerFrameGamepads.cpp`, menu item **Send Game Controllers to the Other Computer**, off by default and not saved)
  runs it from an 8 ms timer.
  - Windows `GamepadSource_Windows.cpp`: **not built on Windows.** XInput loaded dynamically (4 pads, rumble). The Guide
    button and the battery are not read (they need `XInputGetStateEx` and `XInputGetBatteryInformation`). **GameInput**
    (Windows 11) would remove the four-pad limit and see PlayStation pads directly; it would be a second source behind
    the same interface, not started.
  - Linux `GamepadSource_Linux.cpp`: evdev (`/dev/input/event*`, found again each second), the xpad layout, rumble with
    `EVIOCSFF`; a PlayStation pad's square and triangle are swapped to match, from reading the kernel's documentation. It
    skips the server's own virtual controller (so a viewer on the same computer doesn't send it back to itself). **Never run
    with a real device**; the code has no test.
  - macOS, Android and iOS: stubs that say sending controllers isn't available yet (the Mac one should become the Game
    Controller framework).
- **Tests** (`Tests/MarconiServerTest/GamepadTests.cpp`, `Tests/MarconiCoreTest/WireGamepadTests.cpp`): host logic with a fake
  backend; the real Wire client, forwarder and server over a loopback socket with a fake pad on each side (connect,
  state follows, rumble comes back, unplug removes it, losing focus releases, a viewer that vanishes with buttons held
  leaves nothing pressed, a refusal reaches the viewer, no backend means no capability, bad slots ignored); the uinput
  mapping and rumble tracker; and the real Linux backend, which here reports "no uinput" in plain words.

Not done: relative mouse and scancodes (step 4), the installer's ViGEmBus offer, the datagram channel (states are on the
control connection for now), several viewers' pads at once (one viewer at a time anyway), a Mac server driver, a Mac
viewer source, and any real-hardware test on any platform.

### Phase 8: macOS and Linux servers

Outline only; planned in detail when reached.

- macOS: ScreenCaptureKit for capture, VideoToolbox for encoding, CGEvent for input, the Screen Recording and
  Accessibility permissions asked for in plain words, a login item or launch daemon, packaged in the existing DMG
  as "Marconi Server.app" or a mode of Marconi.app (owner's choice).
- Linux: PipeWire and the portal on Wayland, X11 with XDamage and XTest otherwise, a systemd user service, a
  package or tarball.
- Mac gamepads, tried on the owner's Mac in this order: first a virtual HID controller made from the server process
  itself with IOKit's user-device call (`IOHIDUserDeviceCreate`, present in the framework but not public), which
  needs nothing installed and no Apple approval if the owner's macOS allows it; only if macOS refuses, a
  `HIDDriverKit` extension presenting the controller, which needs Apple's DriverKit entitlement, shipped inside the
  server app bundle, signed and notarized. Either way the server feeds it `GamepadState`.
- The client's install menu then offers the right installer per server, using SFTP for Macs.

#### Phase 8 progress: macOS (written, never built) and Linux X11 (built and tested)

**Linux X11** (`SystemScreen_Linux.cpp`, `SystemInput_Linux.cpp`, `X11Support.h`): built and tested under Xvfb by
`Tests/MarconiServerTest/X11Tests.cpp` (run with `DISPLAY` set to an Xvfb `-screen 0 1024x768x24`; without a display
the tests print SKIP). The tests draw a known pattern with Xlib and check the grabbed pixels, the dirty rectangles
(64x64 tiles, joined along a row, partial tiles at the screen edge, nothing when nothing changed), and that XTest
pointer, buttons, wheel, keys, Shift and typed text (including a character with no key) reach an X client. Fixed along
the way: an X error from a shrinking screen no longer ends the program; keys with no keycode are typed by borrowing a
spare key; cursor colours are no longer premultiplied. The dirty-tile code is shared (`DirtyTracker.h`).
Wayland: `XDG_SESSION_TYPE=wayland`, or no `DISPLAY`, gives a plain-words message (console prints it, the tray shows it
in a dialog and a menu item, `ServerController::GetProblem`) and the server serves the test pattern. The tray program
with no `DISPLAY` and no `WAYLAND_DISPLAY` prints the same message and exits. Not done: Wayland capture (PipeWire and the
portal), XDamage, several monitors (one display covering the whole root window), a systemd user service, a Linux server
package, releasing held keys and buttons when a viewer vanishes, and X's own I/O error handler (an X server that dies
ends the program).

**macOS** (`SystemScreen_Mac.cpp`, `SystemInput_Mac.cpp`, `MacDisplays.h`; frameworks in `Module.manifest`): written
without a Mac. Only syntax-checked against hand-written stub headers, so no claim is made that it compiles against the
real SDK, still less that it works. Decisions:

- **Capture:** `CGDisplayCreateImage` per display (looked up with `dlsym`, since Apple is retiring it). ScreenCaptureKit
  only once this has been run and measured (also what Phase 4's GPU frames want). Needs the Screen Recording permission,
  checked with `CGPreflightScreenCaptureAccess` (a refusal is explained in plain words and the server falls back to the
  test pattern; macOS only applies the permission to a program started afterwards).
- **Retina:** the viewer gets the desktop **in points** (what the Mac user sees as the screen size), each display's
  pixels scaled down by Core Graphics. Pictures, pointer coordinates and bandwidth stay in step; 2x detail is lost. A
  "full pixels" setting is not built.
- **Displays:** all active displays form one desktop (their bounding rectangle, black gaps), as on Windows; each is
  also listed with its `CGDirectDisplayID`, position and size. Re-read at every grab, so plugging a display in or out
  changes the size (the viewer side of resizing is Phase 6). No cursor image yet (needs AppKit).
- **Input:** `CGEventPost` at the HID tap, needing the Accessibility permission (`AXIsProcessTrustedWithOptions`, which
  also shows macOS's prompt). Mouse with click counts and drags, wheel (a notch is three lines), keys by a fixed
  keysym-to-virtual-key table, characters typed with `CGEventKeyboardSetUnicodeString` (also for `TypeText`) except when
  Control, Option or Command is held, when the US key position is used so shortcuts work. Modifiers are mapped
  literally (Ctrl to Control, Alt to Option, Windows to Command): whether a Windows viewer's Ctrl should become Command is
  the owner's call. Hardware scancodes are ignored.
- **Tray (`MarconiServerUI`):** wx's default tray type on macOS is a menu-bar status item, so the tray icon already
  lives in the menu bar; `ServerInfo.plist` sets `LSUIElement` so there is no Dock icon. A permission problem shows in
  a dialog at start, as a "Problem" line in the menu, with "Open Screen Recording settings", "Open Accessibility
  settings" and "Check again" items (the screen permission still needs a restart of the app).
- **Package:** `PackageServer.sh` makes `Marconi Server.app` on a `.dmg` (Developer ID signing and notarizing as in
  `Package.sh`; not called from it yet). Sign every build with the same identity, or the permissions have to be given
  again after each rebuild.
- **Start at login (plan, nothing installs it):** a per-user launch agent
  (`Installer/macOS/com.redmarmoset.MarconiServer.plist`, into `~/Library/LaunchAgents`), not a daemon, because only a
  program in the login session may read the screen or post events. Next step: a tray menu item "Start at login" that
  writes or removes that file (or uses `SMAppService.mainApp` on macOS 13 and later, which needs a small `.mm`). Showing
  the login window itself (before anyone has logged in) is not possible this way and is not planned.

What has to be checked on a Mac (nothing below has been done):

1. It compiles and links: `./Build.sh Server/Project.json`, `./Build.sh ServerUI/Project.json`, and the tests.
2. Screen Recording: first run shows the prompt; after allowing it and restarting, the picture is the real desktop, not
   just the wallpaper. Is `CGDisplayCreateImage` still present and working on the Mac's macOS version (it is marked as
   going away; if not, ScreenCaptureKit is needed)?
3. Colours: the picture is not tinted or dark (sRGB conversion, the byte order of the pixels), the top byte is clean.
4. Retina: on a built-in display the viewer shows the full desktop at the "looks like" size, sharp enough, and the mouse
   lands where it should at 1x and 2x, with a second display left of and above the main one (negative coordinates), and
   with displays of different scales. Plug and unplug a display while connected.
5. CPU and latency of a full-screen grab at 60 Hz (the numbers go in section 8).
6. Accessibility: the prompt appears; with it off, the plain-words text shows; "Check again" after allowing works
   without a restart.
7. Mouse: move, left, right, middle, double and triple click, drag, scroll direction (vertical and horizontal; the
   horizontal sign is a guess), scroll amount.
8. Keyboard: letters, shifted symbols, digits, Return, Backspace, Delete, arrows, Home/End/Page keys (Mac mapping),
   F1 to F12, keypad, Shift/Control/Option/Command (do modifier key events work as plain key events, or do they need
   `kCGEventFlagsChanged`?), Caps Lock, held keys repeating, Command-C and Command-V, typing an accented letter and an
   emoji through `TypeText`, a non-US layout.
9. The menu-bar icon (size and look, light and dark menu bars), the dialogs from a program with no Dock icon, Quit.
10. `PackageServer.sh`: the `.app` runs from `/Applications`, the signature verifies, notarization passes, and
    permissions survive a rebuild signed with the same identity.
11. The viewer's behaviour against it (probe, TLS, password) is unchanged by the platform, but run it once.

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
- Blanking the server's screen while a viewer is connected.
- A virtual display driver, so a server with no monitor or a laptop with its lid closed can be resized to any window.
- HEVC, AV1 and 4:4:4 colour where both ends support them; Windows GameInput for more than four pads.
- Touch and pen input.

## 8. Record of measurements

Filled in as phases complete: machine, resolution, capture, encode, decode and present times, bitrate, glass to
glass.

| Date | Phase | Machines | Resolution | Capture | Encode | Decode | Present | Glass to glass | Bitrate |
|---|---|---|---|---|---|---|---|---|---|

## Known issues (integration branch)

- The clipboard flake (rapid copies sometimes ended on an older copy, and a copy made during the login could be lost)
  is fixed: the server and the client now count outstanding requests instead of using one flag, and the server
  listens for clipboard changes from the moment it grants the clipboard. 15 of 15 runs pass.
- A server with no password now refuses every login (`Settings::allowNoPassword`, `--allow-no-password` for testing).
  The tray's first-run prompt and its Set password item now refuse an empty password, its menu says "No password set:
  viewers can't connect" until one is set, and the server installer no longer has a password page (the owner prefers
  setting the password in the tray). `--set-password-file` stays in the tray program but nothing in the installer
  uses it now.

### Log files and the Linux package (branch `claude/server-logs-linux`)

**Log files (built and tested on Linux; the Windows service and helper paths are not run on Windows).**
`ServerLog.h/.cpp` in `MarconiServerCore` opens one `<program>_<date>_<time>.log` per run in `<data folder>/logs`
through the engine's `Log::InitializeFileLogging`, flushes after every line (a `Log` callback), keeps the newest 20
per program and says nothing if the folder can't be written. Programs and file names: tray `MarconiServerTray` (and
`MarconiServerSetPassword` for the installer's `--set-password-file` runs, so they don't push real logs out), console
`MarconiServerConsole` (only with `--log-file PATH`, which takes a folder), `--service` `MarconiServerService`
(`<default data folder>/logs`, opened in `ServerModes.cpp`), `--session` `MarconiServerSession` (in `SessionHost.cpp`,
`<data dir>/logs`). The helper also logs why it gives up (settings, certificate, capture, listen failures).
Audit: `LoggingInputSink` logged every key, typed text and pointer position (a password typed into a test-pattern
server would have been logged): it now only says once that input is being ignored. A viewer's name is cut and stripped
of control characters before it is logged (it arrives before the login). No password or hash is logged anywhere else;
no `LOG_ERROR` is used. Limits: the engine's file format shows a level only for warnings and errors (information
lines have no label); `ServiceHost_Windows.cpp` was not touched (its two `LOG_INFO` lines are harmless).

**Linux package (`PackageServerLinux.sh`).** Builds Release of `ServerUI` and `Server` and makes
`Build/Installer/MarconiServer-<version>-linux-<arch>.deb` (tray as `/usr/bin/marconi-server`, console as
`/usr/bin/marconi-server-console`, desktop file, icons, README, and a systemd *user* unit
`/usr/lib/systemd/user/marconi-server.service` for `graphical-session.target`). Built here, unpacked with `dpkg-deb -x`,
`lintian` run. Not done: the unit is not enabled by the package (each user runs `systemctl --user enable --now`); it
was not started under a real user session, and the package was not installed with `apt`/`dpkg -i` or tried on another
distribution.
