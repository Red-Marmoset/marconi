# Marconi: the performance plan

`SERVER_PLAN.md` plans the Marconi server: what it is, how it installs, how it logs viewers in. This document plans
how fast it is. It takes over the speed work that `SERVER_PLAN.md` sketched in "Speed: where the time goes" and
Phases 4 (video) and 5 (UDP), and adds what the sketch left out: measuring, the picture that is video while it
moves and exact when it stops, input for games, sound, and the rules that keep latency low when something is slow.

The owner's brief, as given:

- outperform tools like Parsec;
- the same kind of low-latency experience as video streaming;
- better quality than video, since most of a desktop's picture is usually still;
- good enough to play a game remotely ("since we make games").

How to use this document:

- Work through the steps in order. Each step is a set of commits with a "done when" check that can be run by hand,
  and ends with a MINOR bump; commits inside a step that fix or improve something visible take a PATCH bump.
  The rules in `AGENTS.md` apply throughout: verify or ask instead of assuming, version and changelog rules, plain
  words in anything users read.
- Steps 1 and 2 can start now: step 2 finishes `SERVER_PLAN.md` step 1.6 (Desktop Duplication). Steps 3 onwards
  follow `SERVER_PLAN.md`'s order (installer and service before video) unless the owner reorders them.
- Numbers come from measurements, not from this document. Every target below is checked with the tools step 1
  builds, and the results go in section 6. Claims about other products and platform APIs are linked to a source
  or marked **verify**; a "verify" is checked on the bench before anything is built on it.

## Progress

**Step 1 is under way** (branch `claude/beautiful-babbage-qh3jhe`, versions 0.6.1 to 0.6.3), built and tested on
Linux against engine e0cc258:

- Done: timestamps in the protocol and the clock comparison (item 2), `--stats` (3), F8 > Show Performance (4),
  the measuring test pattern (5), `MarconiServer --bench` (1), and a test relay that delays each direction by a set
  time (the delay part of 6). The status bar's round trip now works for Marconi servers too.
- Written but not run: the encoder trial (7, `Tools/EncoderTrial`). It compiles and links against the Windows
  headers and libraries; it needs a run on each of the owner's Windows machines.
- Still to do: run the encoder trial and fill in its table; measure today's lossless path on the owner's machines
  with `--bench`, `--stats` and a 240 frames a second film, and add the rows to section 6. Jitter, loss and
  reordering in the simulator wait for the datagram channel (step 4), where they matter. Step 1's MINOR bump comes
  when these are done.
- Differences from the text below, as built: the server's times ride on `FrameEnd` rather than `FrameBegin` (the
  update is complete by then, so its send time is real); the readout isn't remembered per connection; it shows
  pictures received against pictures shown rather than "dropped" and has no loss line yet (there is no loss on
  TCP; it comes with step 4).

## 1. Decisions

### Taken by the owner

- **This document exists** alongside `SERVER_PLAN.md`, which points here for the speed work.
- **Sound is in.** Playing a game without it isn't a fair test of "playing remotely".
- **The local network first.** The pipeline (capture, encode, decode, present, input) is made fast on a LAN
  before the internet transport work; everything the internet work adds sits on top of it.
- **Video codecs live in a new engine module**, the way cryptography lives in `Crypto`: one interface for
  encoders and decoders, a platform implementation behind it on each system. Committed to the engine repo as
  `AGENTS.md` says.
- **The Linux decoder is decided when step 3 reaches Linux** (see open point 1). The owner leans towards VA-API
  with small decoders because of the LGPL, but would prefer ffmpeg if it can be included in line with its licence.
- **The datagram channel is encrypted with DTLS**, built with the channel itself in step 4, not before (open point
  5, decided).

### Open, each with a recommendation

1. **Linux decoding.** Linux has no system codec API, so one of:
   - **ffmpeg's libavcodec, under the LGPL.** How a program like Marconi includes it within the licence (this is
     the usual practice, not legal advice; have it checked before the first release that ships it):
     - Configure ffmpeg without `--enable-gpl` and `--enable-nonfree`. That leaves out libx264, libx265 and
       the other GPL parts; the built-in decoders and the hardware decoding paths (VA-API, Vulkan, and on other
       systems D3D11 and VideoToolbox) stay in and stay LGPL.
     - **Link it dynamically**: ship `libavcodec.so` (and `libavutil.so`) as separate files beside Marconi, so
       anyone can swap in their own build. Dynamic linking is what lets a program under another licence (Marconi
       is MIT) use an LGPL library without the LGPL covering the program.
     - Ship the LGPL licence text and ffmpeg's notices with Marconi, and say in the README and the about box
       that Marconi uses FFmpeg under the LGPL 2.1 or later.
     - Publish the exact ffmpeg source used (a link to the unmodified release tag is enough if nothing was
       changed) and the configure line. Any change to ffmpeg itself is published under the LGPL.
     - A build with only the H.264 and HEVC decoders and the hardware paths is a few MB.
   - **Our own VA-API path plus small decoders.** VA-API is not a decoder: the program reads the H.264 stream's
     headers itself, keeps track of reference pictures, and hands each slice to the GPU. That is what libavcodec
     does for VA-API, written again in the engine module: a bounded piece of work, but a real one. The software
     fallback would be openh264 (BSD licence; decodes Constrained Baseline, Main and High: **verify** it handles
     everything our encoders emit).
   - **Patents, either way.** Hardware decoders on Windows and macOS come with the operating system's licence.
     Software H.264 decoding inside Marconi (libavcodec, or openh264 compiled in) is not covered by any licence
     Red Marmoset holds; Cisco's patent cover for openh264 applies only to the binary Cisco distributes, not to a
     copy built into a program. The H.264 pool has historically had a royalty-free tier for small volumes
     (**verify** the current terms). On Linux the hardware decoder is the normal path and software is the
     fallback.
   - **Recommendation:** libavcodec, LGPL, dynamically linked, for the first Linux client, if the owner accepts
     the steps above; our own VA-API path if not.
2. **Vendor encoder kits.** Media Foundation (the owner's choice in `SERVER_PLAN.md`) reaches the NVIDIA, AMD and
   Intel hardware encoders, but how much of each it exposes varies (section 3, step 1's encoder trial). If the
   trial shows Media Foundation lacks what the targets need on the owner's GPUs (gradual refresh instead of
   keyframes, long-term reference frames, slice sizing, 4:4:4), add the vendors' own kits behind the same
   interface: NVENC and AMF are headers plus a DLL the driver installs, loaded at run time; Intel's oneVPL needs
   its dispatcher. **Recommendation:** decide from the trial; Media Foundation stays as the fallback.
3. **New libraries.** Opus (BSD) for sound. zstd (BSD) only if step 2's measurements show it pays over zlib.
   **Recommendation:** accept both, zstd conditionally.
4. **The frame rate to design for.** **Recommendation:** 144 a second (60 and 120 come with it); 240 later.
5. **Encrypting the datagram channel. DECIDED (owner): DTLS, later.** `SERVER_PLAN.md` planned AES-GCM with keys
   sent over the control channel; **DTLS** (TLS for datagrams), which mbedTLS implements and Parsec uses
   ([Parsec](https://parsec.app/blog/a-networking-protocol-built-for-the-lowest-latency-interactive-game-streaming-1fd5a03a6007)),
   saves designing nonces, replay protection, rekeying and address changes ourselves. It is built in step 4;
   checking that the engine's mbedTLS build has DTLS moves there too.
6. **HDR** (high dynamic range) **stays out until asked.** When a server display is in HDR mode, the capture
   either converts the picture to standard range or reports that it can't, chosen in step 2.

## 2. Targets

### What the others do

- **Parsec** runs its own protocol over UDP, encrypted with DTLS 1.2, with its own congestion control, and
  adjusts bitrate to the network
  ([Parsec](https://parsec.app/blog/a-networking-protocol-built-for-the-lowest-latency-interactive-game-streaming-1fd5a03a6007)).
  It encodes H.264 and H.265 with the picture kept on the GPU from capture to encoder
  ([Parsec](https://support.parsec.app/hc/en-us/articles/32361354307348-Overview)). Its often-quoted 4 to 8 ms
  is the time inside its pipeline at 240 frames a second on a LAN
  ([Parsec](https://parsec.app/blog/parsec-game-streaming-total-latency-at-240-frames-per-second-c0818cc0daa5)),
  not the time from one screen to the other. Full colour (4:4:4) is reported to need its paid tier (**verify**).
- **Sunshine and Moonlight** (open source) talk to NVENC, AMF and Quick Sync directly, offer H.264, HEVC and
  AV1, HDR and 4:4:4, gradual refresh instead of keyframes, and forward error correction
  ([Sunshine encoder settings](https://docs.lizardbyte.dev/projects/sunshine/latest/nvenc__config_8h_source.html)).

### Where Marconi beats them

- The still parts of the picture end up **exact**, for everyone, not as a paid option, because still areas are
  resent losslessly (section 3, step 3). Text and fine lines look as they do on the other computer.
- An idle desktop costs nothing: no bitrate, no decoding, no battery.
- No account, no relay, an open protocol, one installer with the client.
- Games work with a gamepad, a captured mouse and the right keys from the day video arrives.

### Numbers to hit

Two measurements, both from step 1's tools:

- **Pipeline time**: from the moment the server has the captured picture to the moment the client hands the
  decoded picture to the display. Read from the `--stats` log, per frame.
- **Glass to glass**: from a change on the server's screen to it showing on the client's screen. Measured by
  filming both screens at 240 frames a second with the test pattern's clock and frame counter (accurate to about
  4 ms). A still photo isn't accurate enough for these targets.

| Situation | Target |
|---|---|
| LAN, 1080p at the display's rate (60, 120 or 144) | pipeline 12 ms or less (median); glass to glass 2 frames or less at that rate |
| LAN, 4K desktop with text | exact within 250 ms of the picture stopping; an idle link under 50 kbit/s with no decoding |
| Internet, 20 ms round trip, 2% random loss | 99% of frames shown on time; bitrate settles within 2 s of a change in the link |
| Input | mouse sent at the device's own rate, up to 1000 a second; applied on the server within 1 ms of arriving |
| Smoothness | frame-to-frame time varies by under 1.5 ms (standard deviation) when the stream's rate matches the display |
| Sound | under 60 ms end to end on a LAN; 40 ms if Windows' low-latency audio mode allows it (**verify**) |

Why glass to glass is "2 frames" and not a fixed number: at 60 frames a second a frame lasts 16.7 ms, and
capture waits on average half a frame for the next refresh, presentation waits on average half a frame for the
client's, and the display takes time to scan out. Those waits don't shrink with faster code, only with a faster
display rate; the pipeline time is the part the code controls.

## 3. The steps

### Step 1: measure, and try the encoders

Nothing else starts until these numbers exist.

1. **`MarconiServer --bench`**: captures (and, from step 3, encodes) for a number of seconds and prints each
   stage's time and size: waiting for the screen, capture, conversion, compression or encoding, packing,
   sending. The results are added to section 6.
2. **Timestamps in the protocol.** A `Timestamps` capability bit. With it, `FrameEnd` (and later the video
   frame) carries when the picture was captured and when it was sent, and `Ping`/`Pong` carry the four times
   needed to work out the difference between the two computers' clocks (sent, received, answered, answer
   received). Old builds ignore the extra bytes: `Ping::Decode` and `Pong::Decode` already skip trailing data
   (`WireMessages.cpp`). The client keeps the readings with the shortest round trip, because the server's
   control channel holds its write lock for a whole frame (`Session::Send`), which delays a `Pong` behind a big
   picture. Clock sync moves to the datagram channel in step 4.
3. **Client `--stats`**: a log line per frame (pipeline time and each stage the server reported, decode and
   present time, size), and a summary every 10 seconds.
4. **F8 > Show Performance**: an overlay in the viewer with frames a second, bitrate, loss, round trip, the time
   each stage takes, and frames dropped.
5. **The test pattern for measuring**: `--test-pattern` draws a frame counter and a clock in milliseconds in
   large digits, and the last pointer position it received, so a film of both screens gives glass to glass for
   the picture and for the mouse.
6. **A bad-network simulator** for tests: a stand-in for the transport (and later the datagram channel) that adds
   delay, jitter, loss and reordering from a fixed random seed, so `MarconiServerTest` can check behaviour under
   loss repeatably. Notes for `tc netem` on Linux for manual runs.
7. **The encoder trial** (`Tools/EncoderTrial`, a separate tool kept out of the release): on each of the owner's
   Windows GPUs, through Media Foundation, find out and record per vendor:
   - whether the hardware encoder takes a D3D11 texture directly, and its latency in low-latency mode
     ([CODECAPI_AVLowLatencyMode](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avlowlatencymode));
   - constant bitrate, no B-frames, one picture out per picture in;
   - gradual refresh (`CODECAPI_AVEncVideoGradualIntraRefresh`; the name checked against the Windows headers),
     so there are no keyframe bursts;
   - long-term reference frames (`AVEncVideoLTRBufferControl`, `AVEncVideoMarkLTRFrame`,
     `AVEncVideoUseLTRFrame`), which let the client recover from loss without a keyframe;
   - slices limited by size (`AVEncSliceControlMode`), so a slice fits a network packet;
   - what happens when the bitrate is changed mid-stream (some encoders restart: **verify**);
   - whether 4:4:4 is possible (believed not, through Media Foundation: **verify**).
   This decides open point 2 and the loss-repair design in step 4.
8. (Checking the engine for DTLS moved to step 4 with the owner's decision on open point 5.)

Done when: section 6 has a row per machine for today's lossless path, and the encoder trial's table is filled
for every GPU the owner has.

### Step 2: the exact picture, made fast

This finishes `SERVER_PLAN.md` step 1.6, and builds the exact layer the video picture sits on in step 3.

Today the server copies the whole screen with GDI every 16 ms (`SystemScreen_Windows.cpp`, `Session.cpp`), which
with Windows' default timer resolution actually runs about every 31 ms; it compares the whole screen to the last
one, repacks every pixel and compresses with zlib. The client converts pixels one at a time into a `wxImage`, then
a `wxBitmap`, draws that into a backing bitmap and stretches it with GDI on paint (`VncCanvas`).

Server:

1. **Desktop Duplication.** Capture waits on the display (`AcquireNextFrame` with a timeout): no sleeps, the
   display is the clock. The changed and moved rectangles come from Windows, so nothing compares whole screens.
   - The pointer's shape, visibility and position come from the same call and go to the client separately, so
     the pointer never lags the picture. Masked and monochrome pointer shapes are converted to the
     `CursorShape` format; inverting pointers are approximated.
   - Each display is duplicated on the graphics card that drives it. On laptops with two GPUs that is usually
     the built-in one, while the encoder may be on the other, so step 3 copies between them.
   - When Windows reports `DXGI_ERROR_ACCESS_LOST` (a mode change, a game going full screen, a desktop switch
     such as UAC), the duplication is made again. A wake with no changed rectangles means only the pointer moved.
   - A display in HDR mode delivers 16-bit floating-point pixels: converted or refused (open point 6).
   - While a viewer is connected the server keeps the display awake (`SetThreadExecutionState` with
     `ES_DISPLAY_REQUIRED`); Desktop Duplication stops when the display sleeps.
2. **A new `ScreenSource` shape.** `WaitForChange(timeout)`, then a grab into either CPU pixels (today's
   `Capture`, kept for `TestPatternScreen` and the tests) or a GPU texture (the video path). The screen is
   divided into 64 by 64 tiles; each grab produces the set of changed tiles and keeps, per tile, how long it has
   been still. The exact-picture sender and step 3's scheduler both use it.
3. **The newest picture wins.** One slot between capture and compression: if a new picture arrives before the
   last was sent, the old one is dropped, never queued. Queues are where lag comes from.
4. **Lossless compression, measured.** Compare zlib (today), zstd, and a scheme made for screens (predict each
   pixel from the ones above and to the left, then zstd; or a palette and run lengths for tiles with 16 colours
   or fewer, as Tight does). Choose by bytes saved per millisecond. New `Still::Compression` values are only
   sent to clients that set a `StillFilters` capability bit. Compress rows straight from the capture instead of
   repacking each pixel (`PackRect`).
5. **Threads and timing.** The capture and compression thread runs above normal priority (Windows' MMCSS
   "Games" class), the input thread at high priority. Waiting uses Desktop Duplication's own wait and
   high-resolution waitable timers, not `timeBeginPeriod` (Windows 11 ignores it for programs without a visible
   window: **verify**).
6. **Never block on a full network.** When the socket can't take a picture right away, the picture is dropped
   and the next one carries the changes. The TCP fallback in step 4 depends on this.

Client:

7. **Drawing on the GPU.** A `Presenter` replaces the chain in `VncCanvas`: only the changed rectangles are
   uploaded to a texture, the GPU scales, and the pointer is drawn over it from the latest shape and position.
   - Windows: a Direct3D 11 swap chain in flip mode, with a waitable object and at most one frame queued, so a
     picture is never shown later than the next refresh. For Game mode (step 3), presenting immediately without
     waiting for the refresh (`DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` with a sync interval of 0, after checking
     the system supports it).
   - macOS: a Metal layer. Linux: OpenGL.
   - Updates that arrive between two paints are merged into one (today each server update becomes its own call
     to the UI thread).
8. **Sharp on high-resolution screens.** Marconi declares per-monitor DPI awareness on Windows (its manifest
   doesn't today, so Windows scales the window as a bitmap), and "fit the other computer's screen to this
   window" asks for physical pixels.
9. **Input off the UI thread.** Mouse and key events go into a queue that a sender thread writes, instead of a
   blocking write under a lock on the UI thread.

Done when: a 4K desktop with text updates at the display's rate with a pipeline time under 10 ms on a LAN, an idle
desktop sends nothing, and section 6 says so.

### Step 3: video while it moves, exact when it stops, and games over the LAN

`SERVER_PLAN.md` Phase 4, in detail, with the game input from its Phase 7 brought forward so a game can be played
on a LAN the day video works.

The engine module:

1. **`Video`** (name checked against the engine's conventions first): `VideoEncoder` and `VideoDecoder`
   interfaces, a query for what the machine can do (codecs, 4:4:4, gradual refresh, long-term references), and
   platform implementations: Media Foundation on Windows (with the vendor kits if open point 2 says so),
   VideoToolbox on macOS, Linux per open point 1. Committed to the engine repo separately, as `AGENTS.md` says.

Encoding (server):

2. **The low-latency encoder.** Settings from the trial: low-latency mode, constant bitrate, no B-frames, one
   picture out per picture in, no periodic keyframes; gradual refresh where the trial showed it works, otherwise
   keyframes sent only on request and spread over one frame interval so they don't burst; slices sized for
   network packets where supported; a quality floor so still areas don't smear; bitrate changed at most every
   100 ms.
   - Media Foundation's hardware encoders work asynchronously (`MF_TRANSFORM_ASYNC_UNLOCK`, with "need input"
     and "have output" events) and take textures through an `IMFDXGIDeviceManager` on a Direct3D device created
     for video with multithread protection on. Microsoft's software encoder takes no textures, so the fallback
     reads the picture back to memory.
   - Colour conversion from the captured BGRA to the encoder's NV12 happens on the GPU (the Direct3D video
     processor), with no copy through memory.
   - 4:2:0 colour first. The exact layer is what keeps text sharp despite it; 4:4:4 where the hardware has it,
     later (step 7).
3. **Choosing a codec.** `StreamConfig` gains a `codec` field (H.264 now; HEVC and AV1 later), behind the
   `Video` capability bit already reserved.

The picture that is video while it moves and exact when it stops:

4. **Two layers on the client.** The decoded video frame, and the exact layer (`Rfb::Framebuffer`, as now) with a
   per-tile mark saying whether the exact copy is current. Each video frame carries the set of tiles that changed
   in it; those tiles lose their mark. When the presenter draws, a marked tile comes from the exact layer, the rest
   from the video.
5. **The server's refresh scheduler.** A tile that has been still for a short while (150 to 300 ms, chosen from
   measurements) is sent again losslessly as a `Still`, within a share of the bandwidth so a big change doesn't
   swamp the link. The tiles that stopped most recently and are most visible (largest area, nearest the pointer)
   go first.
6. **Nothing moves, nothing is sent.** After a number of still frames the encoder stops: no bitrate, no decoding,
   no battery. The next change restarts it with gradual refresh or a keyframe.
7. **Picture quality menu** (the existing Best, Balanced, Low Bandwidth, plus Game):
   - **Best**: video plus a fast exact refresh.
   - **Balanced**: video at a lower bitrate plus the exact refresh.
   - **Low bandwidth**: video only, dropping resolution and frame rate when needed.
   - **Game**: video only, the highest frame rate, shown immediately without waiting for the display's refresh.
8. **A game changes the screen mode.** The sequence is written down and tested: the server makes the duplication
   again, sends `Displays`, restarts the encoder, sends a keyframe; the client resizes its picture.

Decoding and presenting (client):

9. **Hardware decoding straight to the presenter.** Windows: Media Foundation with D3D11, decoding into a texture
   the presenter draws. macOS: VideoToolbox, decoding synchronously with frame reordering off, into Metal through
   a `CVMetalTextureCache`. Linux: per open point 1.
10. **Shown as soon as decoded.** By default each frame is presented the moment it is decoded. An option paces
    frames to the display when the stream's rate matches, trading a little latency for smoothness. Between
    receiving and decoding, the newest frame wins, as on the server.
11. **Screenshots** and the headless mode's pictures read back the combined picture.
12. **The status bar** shows frames a second and bitrate for Marconi servers.
13. **Tests:** encoder and decoder round trips where the platform has them; the refresh scheduler with a fake
    clock; combining the two layers from scripted frames and tile sets; `StreamConfig` from the quality menu;
    the screen-mode change sequence.

Input for games, over the control channel for now (datagrams in step 4):

14. **Keys by position.** `Key.scancode` is filled in. It stays a USB HID usage on the wire, as `WireMessages.h`
    defines it, because that means the same on every system: each client converts its own key codes (the Windows
    scan code in the key message's flags, macOS virtual key codes, Linux evdev codes), and the server converts to
    what its system wants (on Windows, the scan code `SendInput` takes with `KEYEVENTF_SCANCODE`, plus
    `KEYEVENTF_EXTENDEDKEY` for the keys that need it). So W, A, S and D are the same keys whatever either
    computer's layout. The keysym stays alongside for typing text.
15. **Key repeat** comes from the client (the user's own repeat settings), forwarded as it happens; the server
    never makes its own.
16. **The mouse captured for games.** A menu item and a key combination capture the pointer: the local pointer is
    hidden and held in the window, raw movement is read (Windows `WM_INPUT`, the macOS event tap, Linux XI2 raw
    events) and sent as `PointerRelative` deltas at the device's rate. The server applies each one as it arrives
    with `SendInput` and `MOUSEEVENTF_MOVE_NOCOALESCE`. Shortcuts such as Alt+Tab and the Windows key go to the
    other computer through the existing `SystemKeys` capture.
17. **Sound starts here**, alongside (step 6); it depends on nothing in steps 4 or 5.

Done when: 1080p and 1440p games at the display's rate play smoothly on a LAN within section 2's targets with
keyboard and mouse; text is exact within 250 ms of stopping; an idle desktop costs no processor time or bandwidth.

### Step 4: the datagram channel

`SERVER_PLAN.md` Phase 5, in detail.

1. **UDP sockets** get IPv6, connected use, non-blocking receive and sending several packets per call.
2. **The datagram channel** in `Wire/`, with its own small header and version, opened by a UDP hello that proves
   it belongs to a logged-in session. Encrypted with DTLS (open point 5); first check that the engine's mbedTLS
   build has DTLS, and add it there if not. The server never answers a datagram it can't
   authenticate (so it can't be used to flood someone else), and a change of address is authenticated again.
3. **Packets.** Each frame's slices are cut into packets of about 1200 bytes (frame number, packet number and
   count, keyframe and end-of-frame flags). The path's packet size limit is checked once at the start. Large
   frames are spread out over up to one frame interval rather than sent in one burst, which matters most on wifi.
4. **Repairing loss,** chosen from the measured round trip:
   - when a round trip is shorter than a frame (a LAN), the client asks for the missing packets again;
   - otherwise, extra repair packets are sent with each frame (forward error correction: simple parity first, a
     stronger code if measurements call for it), more of them as loss rises;
   - both can be on at once;
   - when a frame can't be repaired, the client asks for a fresh picture: a frame that refers only to a long-term
     reference the client has, where the encoder supports it (step 1's trial), otherwise a keyframe.
5. **Bitrate control.** The server watches for delay building up (the one-way delay trend, as video calling does),
   with loss as the backstop, and sets the encoder's bitrate at most every 100 ms; below a floor it drops
   resolution or frame rate. The client sends a `Report` about every 50 ms: frames received and lost, decode and
   present times, jitter.
6. **What travels as datagrams:** video, pointer position and buttons (each packet carries the whole state, so a
   lost one is corrected by the next), relative mouse, gamepad state, pointer position for view-only viewers,
   reports, sound. **Keys and the mouse wheel stay on the control channel**, because a lost key-up or wheel turn
   isn't corrected by the next packet.
7. **Priority on the network.** Packets are marked for low delay: on Windows through the quality-of-service API
   (`QOSAddSocketToFlow`; Windows ignores the plain socket option), elsewhere with the socket option.
8. **When UDP is blocked**, everything stays on the control channel, dropping frames instead of queuing them
   (step 2, point 6) and asking for a keyframe after a drop; the status bar says "over TCP". The status bar also
   shows loss when there is any.
9. **Tests:** packing and repair under the bad-network simulator; replayed and out-of-order packets rejected;
   bitrate control against scripted traces of delay and loss; an hour-long soak.

Done when: section 2's internet row holds on the simulator and on a real internet link; blocking UDP falls back to
TCP and says so.

### Step 5: gamepads

`SERVER_PLAN.md` Phase 7 as planned there (XInput on the client, ViGEmBus on Windows servers, uinput on Linux,
rumble back to the client), with these additions:

- The client reads pads 1000 times a second on its own thread and sends `GamepadState` on change, as datagrams.
- Clock sync moves to the datagram channel, so input latency can be measured on its own.
- The test pattern shows the last pad state it received, for filming.

Done when: a game on the server plays from the client with a pad, with rumble, at section 2's latencies.

### Step 6: sound (alongside steps 3 to 5)

- **Server:** WASAPI loopback capture, event-driven, on Windows (older versions need a silent stream playing for
  loopback to deliver while nothing else plays: **verify**); ScreenCaptureKit's audio on macOS; PipeWire on Linux.
  Opus at 48 kHz, 10 ms frames, about 96 to 128 kbit/s, nothing sent during silence. Over the control channel
  until step 4, then datagrams.
- **Client:** low-latency output (WASAPI's shared low-latency mode through `IAudioClient3` on Windows, Core Audio,
  PipeWire), a jitter buffer of 10 to 30 ms that adapts to the link, and gentle resampling so the two computers'
  sound clocks don't drift apart.
- Sound and picture each run as fast as they can; there is no lip-sync delay added to either.
- A menu item to turn sound off.
- Opus is a new library (open point 3).

Done when: sound plays without gaps on a LAN and over the simulator at 2% loss, within section 2's target.

### Step 7: later

- HEVC and AV1 where both ends have them; 4:4:4 where the hardware has it; 240 frames a second.
- The vendor kits, if open point 2 said so, tuned per vendor from measurements.
- Left out until the owner asks: HDR, a virtual display (so a server with no monitor can match any window), a
  relay for computers that can't forward a port.

## 4. Known limits, to state plainly in the README when they apply

- Some games' anti-cheat software ignores or rejects input that a program injects, as every remote-play tool's
  does.
- Relative mouse movement injected on Windows passes through Windows' pointer acceleration unless the game reads
  raw input (**verify** whether games reading raw input see injected movement).
- Games in exclusive full screen: Desktop Duplication captures them on Windows 10 and later (**verify** on the
  owner's games).
- No variable refresh rate support is assumed; a 60 frames a second stream on a 144 Hz display isn't perfectly
  even.

## 5. What changes in the protocol

No protocol version bump is needed: every addition sits behind a capability bit, unknown messages are skipped by
their length, and the messages that gain fields ignore extra bytes in older builds. Capability bits from 32 up
need 64-bit constants (`1ull << n`; today's use `1u`).

| Addition | Where | Behind |
|---|---|---|
| capture, compress and send times on `FrameEnd` and video frames; the server's two times on `Pong` (done, 0.6.1) | control | `Timestamps` |
| new `Still::Compression` values | control | `StillFilters` |
| `StreamConfig.codec`; video `Frame` with its changed-tile set; `RequestKeyframe` | control, then datagram | `Video` (reserved) |
| `PointerRelative` | control, then datagram | `RelativeMouse` |
| UDP hello, `FramePacket`, repair packets, `Report`, datagram clock sync | datagram | `Datagram` (reserved) |
| `GamepadState`, `GamepadAttached`, `GamepadDetached`, `Rumble` | datagram and control | `Gamepads` (reserved) |
| `AudioConfig`, `AudioPacket` | control, then datagram | `Audio` |

## 6. Record of measurements

Filled in as steps finish. Glass to glass is from a 240 frames a second film.

| Date | Step | Machines (CPU, GPU) | Resolution and rate | Capture | Encode | Decode | Present | Pipeline | Glass to glass | Bitrate |
|---|---|---|---|---|---|---|---|---|---|---|
| 2026-10-21 | 1 | Linux cloud container, 4 cores, no GPU; server and client on one machine (loopback); test pattern | 1280x720, 45 to 52 a second | 1.8 ms (pattern drawn, not captured) | 0.5 ms zlib | 0.3 ms | 1.5 ms to paint | 7.9 ms median, 18.8 ms slowest 1% | not filmed | 530 to 900 kbit/s |

The first row is a baseline of the tools, not of real capture: the test pattern redraws itself in memory, and the
rate is held under 60 a second by the session's 16 ms pause after each picture's work (step 2 replaces it with
waiting on the display).

Encoder trial (step 1), one row per GPU:

| GPU and driver | Texture input | Latency (ms) | Gradual refresh | Long-term references | Slice size control | Bitrate change | 4:4:4 |
|---|---|---|---|---|---|---|---|
