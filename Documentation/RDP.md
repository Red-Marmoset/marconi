# RDP support: design

Marconi speaks VNC. This document plans Remote Desktop (RDP), the protocol Windows itself uses, so Marconi can
reach a Windows computer without a VNC server installed on it (and a Linux desktop that shares its screen the same
way). The first usable checkpoint (milestones 1 to 5) is in the code: the viewer opens RDP connections. "Already
here" below says exactly what exists; the rest is the plan for what follows.

## Why, and in what order

The owner wants RDP before the gamepad and before the other large pieces, to reach a Windows PC ("drips") with
nothing extra installed on it. RDP is a much larger protocol than VNC, so it is built in milestones (below), each
one testable on its own, with the first usable session as the first real checkpoint.

## The decision: our own implementation

RDP is implemented in Marconi and the engine, the same way VNC and the crypto are, rather than by pulling in
FreeRDP. The owner's steer was that FreeRDP "sounds like a PITA", and it fits the project: FreeRDP is a large C
dependency that wants OpenSSL or mbedTLS on every platform and its own build system, where the engine's whole
approach is to wrap each platform's own facilities behind small interfaces. The cost is that this is months of
work for one person rather than weeks. The decision is reversible: nothing outside the `Rdp` module and the engine
crypto additions below would change if a later owner chose FreeRDP instead, because the viewer talks to a protocol
through the `Session` interface described next, not to RDP directly.

## Shape: a Session interface

Today `ViewerFrame` and `VncCanvas` own an `Rfb::Client` and call it directly. RDP needs the same window, canvas,
toolbar, full-screen bar, reconnection, screenshots and clipboard, driving a different protocol. So the first
structural step is an interface both protocols implement:

```
class Session // MarconiCore
{
    // what the UI already asks of Rfb::Client:
    Connect / Disconnect
    SendPointerEvent / SendKeyEvent
    RequestFullUpdate / RequestDesktopSize / SupportsDesktopResize
    GetFramebuffer / GetHealth / TakeTrafficStats / GetTimeSinceInput
    IsEncrypted / GetEncryptionName
    clipboard: HasClipboard / OfferClipboard / RequestClipboard ...
    a listener with OnConnected / OnFramebufferUpdated / OnDesktopResized / OnCursorShape / OnClipboard /
      OnDisconnected / OnCredentialsRequired / OnUntrustedCertificate
};
```

`Rfb::Client` implements it (a thin rename of what it already exposes). `Rdp::Client` implements it too. The viewer
holds a `std::unique_ptr<Session>` chosen from the address's protocol. File transfer (TightVNC / SFTP) stays
VNC-only for now and is asked for through a separate capability query, so the file panel simply stays closed on RDP.

Splitting the viewer off `Rfb::Client` is a prerequisite and is worth doing on its own; it is the first task under
milestone 1.

## Already here

- The probe recognises an RDP server (sends an X.224 Connection Request, looks for a Connection Confirm) and the
  protocol chooser reports it; `rdp://host` parses; `Protocol::Rdp` and port 3389 exist. Today the viewer then says
  "recognised, not supported yet". These stay and become the entry point.
- The engine Crypto module has AES-128, DES, MD4, MD5, RC4, SHA-1, SHA-256, HMAC-MD5, HMAC-SHA256,
  Diffie-Hellman, secure random, a big-integer modpow, and a TLS client (`Crypto::TlsClient`) on mbedTLS.
  `Net::TlsStream` wraps it over any byte link (a socket or an SSH tunnel), which RDP reuses. That is every hash
  and cipher NTLMv2 and CredSSP need.
- Milestones 1 and 2 connect end to end: `Rdp/Negotiation` and `Rdp/Security` (the X.224 negotiation),
  `Rdp/Ntlm`, `Rdp/NtlmSeal`, `Rdp/CredSsp` and `Rdp/CredSspExchange` (the NLA login), and `Rdp/Connect` which
  ties them together over a real socket: negotiate, TLS, then CredSSP. The `--rdp-check` command runs this against
  a real server. Verified against xrdp through the TLS path (it chose TLS 1.3, no NLA); the NLA login is pinned by
  the NTLMv2 vectors and a mirror-server test, and needs a Windows server to confirm end to end. Not yet wired into
  the viewer, which still says RDP is recognised but not supported.
- Milestone 4's picture decoding: `Rdp/Bitmap` decodes uncompressed and interleaved-RLE bitmap rectangles at 8
  (with a palette), 15, 16 and 24 bits into top-down 0x00RRGGBB pixels. Tested with streams built from the
  specification.
- **Milestones 1 to 5 wired into the viewer** (`Rdp/RdpClient`, a `RemoteSession` like the VNC and Marconi
  clients): `ViewerFrame` opens an RDP connection when the address says `rdp://`, when the probe finds only RDP (or
  RDP and VNC: RDP ranks higher, as the chooser always planned), or when a remembered probe result says "rdp". The
  client asks for the account (user name and password, `DOMAIN\user` splits the domain off) before connecting, runs
  `Rdp::Connection` (negotiation, TLS with the same certificate questions as VNC and Marconi, CredSSP when the
  server wants NLA, the connection sequence), then reads the session: slow-path Update PDUs and fast-path output
  (bitmap updates into the `Framebuffer`, palette updates for 8-bit pictures, pointer updates through
  `Rdp/Pointer` into `CursorShape`, with the 25-entry pointer cache), a later Demand Active (the server changing
  its desktop: the picture is resized and the activation answered again), Set Error Info (kept as the reason shown
  when the server then closes) and the MCS Disconnect Provider Ultimatum. Input: keysyms map to scancodes
  (`Rdp/Input`), with a Unicode event for characters that have none; the mouse sends a move plus one event per
  button change, and a wheel turn per notch. `RequestFullUpdate` sends a Refresh Rect for the whole desktop.
  Decisions made here:
  - The desktop size asked for is the display the window is on (width rounded down to a multiple of 4; 1024x768
    headless). The server's Demand Active may name another size, which the framebuffer and the Confirm Active then
    take: Windows sizes the session to the client, a shared physical screen keeps its own.
  - Fast-path output is not advertised in the General capability set (slow path is enough and is what every server
    falls back to), but fast-path PDUs are read all the same in case a server sends them.
  - 16 bits per pixel is asked for; 32-bit rectangles (planar) are not decoded, so a server insisting on them shows
    a logged warning and no picture: milestone 8.
  - No keepalive yet: a dead link is noticed when the next send fails or the socket closes.
  - Tested in `RdpClientTests` against a scripted loopback server with a passthrough stand-in for TLS (the
    connection sequence, both update paths, pointers, input, a desktop change and the server's error info), and
    `RdpPointerTests` for the pointer shapes.

## What RDP needs that VNC didn't

- **TLS always.** Modern Windows wraps every RDP connection in TLS, negotiated through an X.224 layer first. The
  engine TLS client already does the TLS; RDP adds the negotiation and the X.224/TPKT framing underneath.
- **Network Level Authentication (NLA / CredSSP), usually on by default.** Before the RDP session proper, the
  server wants the user authenticated over CredSSP, which carries NTLMv2. NTLMv2 needs **MD4**, **HMAC-MD5** and
  **RC4**, none of which the engine has today (it has MD5, AES, DES, DH, random). These are small, well-specified
  additions to the Crypto module with known-answer test vectors, done behind the same backend split as the rest.
  CredSSP also binds to the TLS channel (the "public key" / channel-binding hash), so it sits just above the engine
  TLS client. Turning NLA off on the server is possible for a first milestone against a machine the owner controls,
  but NLA is the real target.
- **Scancodes, not keysyms.** VNC sends X11 keysyms; RDP sends keyboard scancodes (set 1) plus Unicode events.
  `KeyMapper` grows a second output path: from a wx key event to an RDP scancode. Mouse is similar to VNC
  (absolute coordinates, a button mask).
- **Clipboard** rides the `cliprdr` virtual channel and carries UTF-16 text, which also sidesteps VNC's Latin-1
  limit for RDP. Later than the first session.
- **Pictures.** When the client doesn't advertise the newer graphics pipeline, the server falls back to the classic
  bitmap updates: rectangles of either uncompressed or interleaved-RLE-compressed bitmap data. That is a workable,
  if bandwidth-heavy, first picture. The graphics pipeline (RemoteFX, planar, NSCodec, H.264) is a later milestone;
  the classic path is enough to see and use a desktop.
- **Hosting.** Windows Home can't host Remote Desktop; the target PC needs Pro, Enterprise or a Server edition.

## Protocol stack (bottom to top)

1. **TPKT + X.224.** 4-byte TPKT header, then X.224 data (class 0). Connection Request / Confirm carry the
   RDP negotiation (which security protocols both sides support: standard RDP security, TLS, or CredSSP/NLA).
2. **TLS** over the socket once the negotiation chooses it (almost always).
3. **CredSSP/NLA** (when negotiated): a few TLS-protected messages carrying SPNEGO + NTLMv2 and, finally, the
   user's credentials, bound to the TLS channel.
4. **MCS** (T.125): Connect-Initial / Connect-Response, then channel joins (an Erect Domain and Attach User dance),
   over the RDP-specific "GCC conference create" payload that lists the virtual channels and the client's screen.
5. **RDP connection sequence:** security exchange (skipped under TLS/NLA), client info (user, domain, options),
   then the server's demand-active / client's confirm-active exchange of capability sets, then the finalisation
   PDUs (synchronize, control cooperate/request, font list/map).
6. **Operation:** server sends output PDUs (bitmap updates, pointer updates, palette); client sends input PDUs
   (scancode and mouse events) and, as needed, suppress-output, refresh-rect and resize PDUs. Virtual channels
   (clipboard, later) ride alongside.

## Milestones

Each is one finished step, testable against a scripted fake server on loopback (as the VNC code is tested) and
against a real server (the Linux `xrdp` in the dev container, and the owner's Windows PC):

1. **X.224 + TLS, behind the Session interface.** Split the viewer off `Rfb::Client` onto `Session`; add the
   `Rdp` module with TPKT/X.224 framing and the negotiation; upgrade to TLS through the engine client; stop at a
   clean, logged point with the status bar showing "encrypted, TLS 1.x". First checkpoint: Marconi reaches an RDP
   server, agrees on TLS, and reports it, without yet drawing anything.
2. **CredSSP/NLA.** Add MD4, HMAC-MD5 and RC4 to the engine with known-answer tests; build NTLMv2 and the CredSSP
   exchange; log in. (With a server that has NLA off, milestone 3 can be reached without this; NLA is the default
   and the real target.)
3. **MCS + capabilities + finalisation.** Connect-Initial through font-map: the point where the server will start
   sending output.
4. **Classic bitmap output + pointer.** Decode uncompressed and interleaved-RLE bitmap updates into the existing
   `Framebuffer`; show the server's pointer with the cursor code the VNC path already has. First *usable* session.
5. **Scancode + mouse input.** `KeyMapper`'s scancode path and the input PDUs: a session you can drive.
6. **Clipboard** over `cliprdr` (UTF-16 text).
7. **Multiple monitors and dynamic resize** (RDP has both natively).
8. **Graphics pipeline codecs** (RemoteFX / planar / NSCodec / H.264): speed, after correctness.

Milestones 1 to 5 with NLA on (so 1 to 5 including 2) are the first real, usable checkpoint: reached, see "Already
here". Next in order: 6 (clipboard), 7 (resize and multiple monitors), 8 (codecs).

## Testing

- A scripted fake RDP server in `Tools/`, like `fake_vnc_server.py`, plays each milestone's exchange on loopback so
  the core logic is tested without a real server and without secrets. Crypto additions (MD4, HMAC-MD5, RC4, NTLMv2)
  get known-answer vectors in `CryptoTest`, as the existing primitives do.
- `xrdp` on Linux (in the dev container) and a real Windows PC are the end-to-end checks. `xrdp` negotiates TLS and,
  configured for it, NLA, so milestones 1 to 3 can be exercised against it on loopback.

## Scope notes

- No audio, no printing, no drive redirection, no RemoteApp. Not proposed.
- Standard RDP security (the pre-TLS RC4 layer) is not implemented: every current server does TLS, and implementing
  the legacy layer would mean more RC4-based key material for no modern server. If a very old server needs it, that
  is a later decision.
