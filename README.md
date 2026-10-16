# Marconi

**M**ulti-protocol **A**ccess and **R**emote **CON**trol **I**nterface.

Marconi lets you see and control another computer's screen from your own, as if you were sitting in front of it.
It's free, and runs on Windows, macOS and Linux.

It works with:

- **Macs**, through the Mac's built-in Screen Sharing;
- **Windows and Linux computers** running a VNC server, such as TightVNC, TigerVNC, RealVNC, UltraVNC or x11vnc.

## What it can do

- See the other computer's screen, scaled to fit your window or at full size, in a window or full screen.
- Use its keyboard and mouse. Keys match your keyboard layout, and a Mac's Command key works as expected.
- Copy and paste text between the two computers, in either direction, and pictures with servers that allow it.
- Remember the computers you connect to, with their passwords kept safely in your system's password store (Windows
  Credential Manager, the macOS Keychain, or your Linux desktop's keyring).
- Find computers on your home or office network by itself.
- Copy files to and from the other computer, by dragging them or with Upload and Download buttons.
- Just watch, without sending any keyboard or mouse input ("view only").
- Send Ctrl+Alt+Del to a Windows computer.
- Connect with encryption on macOS and Linux when the other computer asks for it, and tell you, in the bar at the
  bottom of the window, whether the connection is encrypted.

## Before you start: set up the other computer

**A Mac:** on the Mac, open *System Settings > General > Sharing* and turn on **Screen Sharing**. You'll log in with
an account on that Mac, the same name and password you use to log in there.

**Windows or Linux:** install a VNC server on that computer (TightVNC is a free choice for Windows) and set a
password for it.

## Connecting

Open Marconi. In the window that appears, either:

- type the other computer's name or address (for example `studio-mac.local` or `192.168.1.20`) and press Enter;
- pick a computer you've saved before; or
- double-click a computer listed under **On this network**.

You don't need to know what kind of connection the other computer uses. Marconi checks what it offers and picks the
best option it supports. It remembers what it found, so connecting next time is quicker.

**On this network** lists computers nearby that announce themselves, as Macs with Screen Sharing do. The list
updates every 10 seconds.

Some computers offer Remote Desktop (built into Windows) or SSH (a text-only connection) instead. Marconi recognises
these, but can't use them yet, and tells you so.

### Ways to write an address

Most of the time a name or address on its own is all you need. For special cases:

| You type | What it means |
|---|---|
| `studio-mac.local` | connect however the computer allows |
| `studio-mac.local:1` | VNC screen number 1 (used by some Linux servers) |
| `studio-mac.local::5901` | a specific port number, here 5901 |
| `alice@studio-mac.local` | log in to a Mac as the user `alice` |
| `vnc://studio-mac.local` | VNC only |
| `rdp://drips`, `ssh://drips` | Remote Desktop or SSH only (recognised, not supported yet) |
| `[fe80::1]:1` | an IPv6 address, in square brackets |

## While you're connected

Press **F8** to open Marconi's menu, even in full screen. From it you can:

- switch between fitting the window and full size;
- go full screen or back;
- turn view-only and clipboard sharing on or off, or send your clipboard to the other computer and get its
  clipboard by hand;
- send Ctrl+Alt+Del (or F8 itself) to the other computer;
- refresh the picture, or disconnect.

## Copying files

Press **F10**, or click **Files** on the toolbar, to open the file panel: this computer on the left, the other
computer on the right. Copy files by dragging them between the two sides, from your desktop onto the right side, or
from the right side onto your desktop. You can also select files and click **Upload** or **Download**. Files dropped
onto the other computer's screen are copied to its Desktop.

- **A Mac:** turn on *System Settings > General > Sharing > Remote Login* as well as Screen Sharing. Marconi uses the
  same account. The first time, it shows the Mac's security fingerprint and asks whether to trust it.
- **Windows with TightVNC:** nothing more to set up; files travel over the same connection. The first time you drop
  files on its screen, Marconi asks which folder they should go to.

If a file with the same name is already there, Marconi asks whether to replace it, keep both, or skip it.

## Appearance and settings

Marconi follows your system's light or dark appearance, or you can choose **Light** or **Dark** at the bottom of the
connections window. **Settings** has the same choice, and a button that opens Marconi's log folder.

On Windows, a few parts (menus and the title bar) only change colour after you restart Marconi.

## Is the connection private?

Only when the other computer offers encryption. Servers such as TigerVNC and UltraVNC can be set up to encrypt the
connection (a method called VeNCrypt, using TLS, the same encryption as secure websites). On macOS and Linux,
Marconi uses it whenever a server offers it with a certificate. The first time, it shows you the server's certificate
and asks whether to trust it; it remembers your answer with the saved connection, and warns you if the certificate
ever changes. The bar at the bottom of the window then says **encrypted**. The Windows version can't make encrypted
connections yet.

Some servers offer encryption without a certificate (TigerVNC calls it "TLSVnc"). Marconi can't make that kind of
connection, so it connects without encryption instead, and says so.

Without encryption, the screen picture, everything you type and the password check itself travel over the network as
they are. Anyone able to watch the network between the two computers could read them. The plain VNC password check
in particular is weak: someone who records it can work out the password later. A Mac's account login protects the
password itself, but nothing after it. Macs with Screen Sharing and TightVNC on Windows don't offer encryption.

So on networks you don't trust, use a VPN, or a server that encrypts. As a reminder, the bar at the bottom of every
connection window says **not encrypted** when the connection isn't.

## If something goes wrong

Marconi keeps a log of what it did, which helps when reporting a problem. Open it from **Settings > Open log
folder**, or find it here:

| System | Log folder |
|---|---|
| Windows | `%LOCALAPPDATA%\marconi\logs` |
| macOS | `~/Library/Application Support/marconi/logs` |
| Linux | `~/.local/share/marconi/logs` |

If the other computer accepts the connection but then never answers, Marconi gives up after 10 seconds and says so.

The log folder can also hold pictures of the other computer's screen. Marconi 0.2.0 and earlier saved a few during
every connection (files named `snapshot_...png`), and you can delete them. The testing mode for developers
(`--headless`, described below) saves pictures there on purpose. Look through the folder before you send it to anyone.

Marconi used to be called rmvnc. The first time it starts, it copies rmvnc's saved computers and passwords, and
leaves the originals as they were.

## Removing what Marconi has saved

Marconi keeps your saved connections and settings, their passwords in your system's password store, and its logs.
To remove all of that at once, open **Settings** and choose **Remove my saved connections, passwords and logs**.
Marconi closes afterwards. On Windows, the uninstaller offers the same choice; it is off unless you tick it, and
upgrading never removes anything.

Only your own data is affected. If several people use Marconi on one computer, each removes their own from their own
account.

If you'd rather do it by hand, this is where everything lives:

| System | Saved connections and settings | Passwords | Logs |
|---|---|---|---|
| Windows | the registry, under `HKEY_CURRENT_USER\Software\Red Marmoset Studios\marconi` | Credential Manager, entries starting `marconi/` | `%LOCALAPPDATA%\marconi\logs` |
| macOS | a file called `marconi Preferences` in `~/Library/Preferences` | Keychain Access, items starting `marconi/` | `~/Library/Application Support/marconi/logs` |
| Linux | `~/.config/marconi.conf` | your desktop's keyring, items starting `marconi/` | `~/.local/share/marconi/logs` |

On macOS and Linux these files can only be read by your own account.

## Not available yet

- **Encrypted connections on Windows**, and encryption without a certificate anywhere. See "Is the connection
  private?" above.
- **Remote Desktop and SSH** connections (recognised, but not supported yet).
- Copying text in some languages and symbols with servers that don't support the newer clipboard messages (Macs,
  TightVNC): with those, the clipboard only carries Western European characters, and Marconi says so when some
  can't be sent.
- VNC passwords containing characters outside Western European languages. The VNC password check only carries
  those, so such a password won't work with Marconi. A Mac account login accepts any characters.
- Changing the other computer's screen resolution from Marconi.
- Apple's newest login methods. The one every recent macOS version offers works.

---

# For developers

## Command line

    marconi [--scale fit|100] [--view-only] [--fullscreen] [--no-clipboard] [--saved NAME] [address]
    marconi --remove-my-data

`--saved NAME` opens a saved connection by its name, address or id, using its stored password. `--remove-my-data`
removes the current user's saved connections, keychain passwords, settings and logs, then exits; the Windows
uninstaller runs it for the "Saved connections, passwords and logs" option.

**Headless mode**, for testing and automation, runs a connection without a window or dialogs. It logs what happens
and saves what the viewer would show as PNG files in the log folder, then exits:

    marconi --saved "Studio Mac" --headless --view-only --snapshot-every 5 --quit-after 60

## Building

You need the una engine (`C:\code\engine`, or set `UNA_ENGINE_PATH`) with BuildTool bootstrapped.

**Windows.** Any PowerShell or cmd window works. The MSVC environment is set up automatically and needs toolset
14.44 or newer.

    .\Build.ps1                       # Release app  -> Build\Marconi\Release\Marconi.exe
    .\Build.ps1 -Config Debug
    .\Build.ps1 -Project Tests\MarconiCoreTest\Project.json -Config Debug

**macOS.** The first build compiles wxWidgets, which takes a while.

    ./Build.sh
    ./Build.sh Tests/MarconiCoreTest/Project.json -config Debug

**Linux.** The first build compiles wxWidgets for GTK3. It needs these packages (Debian/Ubuntu names):

    sudo apt install build-essential pkg-config libgtk-3-dev libsecret-1-dev libgl1-mesa-dev \
                     libxxf86vm-dev libxtst-dev libsm-dev
    ./Build.sh

Incremental builds rely on BuildTool's header dependency tracking (engine commit 3d0ce07 or later). With an older
BuildTool, build with `-Clean` after editing a header.

## Versions

Versions are `MAJOR.MINOR.PATCH.REVISION`, for example `0.2.0.48`:

- `MAJOR.MINOR.PATCH` follows semantic versioning and is set in `Project.json`.
- `REVISION` is the commit's position in the history: the first commit is 1, the second is 2, and so on
  (`git rev-list --count HEAD`). Each commit on `main` gets a higher number than the last.

When to change `MAJOR.MINOR.PATCH`:

- PATCH goes up with each bug fix or small improvement (0.2.0 to 0.2.1).
- MINOR goes up when a significant new feature is finished, and PATCH goes back to 0 (0.2.3 to 0.3.0).
- MAJOR goes up only when backwards compatibility breaks, or when the owner decides. Marconi stays on 0.x until its
  own server component exists, and 1.0 is the owner's call, made when Marconi feels complete.

What changed in each version is in [CHANGELOG.md](CHANGELOG.md).

`Build.ps1` and `Build.sh` write the version to `Modules/MarconiCore/BuildVersion.h`, a generated file that isn't
committed, before every build. Marconi logs its version and commit when it starts. On Windows the version is also
in the executable's properties. A build without git history (such as a source zip) gets revision 0, and a shallow
clone gets a revision that is too low.

## Releasing (Windows)

The installer is built with [NSIS 3.08 or newer](https://nsis.sourceforge.io) (`winget install NSIS.NSIS`). It also
needs the [ShellExecAsUser plug-in](https://nsis.sourceforge.io/ShellExecAsUser_plug-in), which lets the installer
start Marconi as you rather than as administrator when you tick "Start Marconi now". Download its Unicode build and
put `ShellExecAsUser.dll` in NSIS's `Plugins\x86-unicode` folder (normally
`C:\Program Files (x86)\NSIS\Plugins\x86-unicode`). `Package.ps1` stops with a message if it isn't there.

    .\Package.ps1                     # -> Build\Installer\Marconi-0.2.0.48-win64-setup.exe

It builds the Release app, then packages `Marconi.exe` with the Visual C++ runtime DLLs from your Visual Studio, so
users don't need the separate redistributable. It also bundles Nebula Sans Black and Semibold and their SIL Open
Font License. The fonts load privately for Marconi, so branding works without installing them, and uninstalling
Marconi doesn't affect other applications. It then prints the installer's SHA-256 and the command to tag the release
(`v0.2.0.48`). It refuses a shallow clone, and refuses uncommitted changes to tracked files unless you pass
`-AllowDirty` for a test package. `-SkipBuild` packages the existing build if it matches the current version.

By default nothing is signed, so Windows warns before running the installer and `Package.ps1` says so at the end.
For a release, set `MARCONI_SIGN_THUMBPRINT` to the thumbprint of a code-signing certificate in your personal
certificate store (where a certificate on a hardware token appears; find the thumbprint in `certmgr.msc` under
Personal > Certificates). `Package.ps1` then uses `signtool` from the Windows SDK to sign `Marconi.exe`, the
uninstaller and the setup executable, each with a timestamp from `http://timestamp.digicert.com` so the signatures
stay valid after the certificate expires. Set `MARCONI_SIGN_TIMESTAMP_URL` to use a different timestamp server.

The installer installs to `Program Files\Marconi`, with a Start menu shortcut and an optional desktop one. It also
adds an entry to Settings > Apps. Installing a newer version replaces the old one, as long as the old one is in
`Program Files`; a copy installed somewhere else is left in place and the new version is installed over it, with a
line in the installer's details saying so. Uninstalling keeps each user's saved connections, passwords and logs
unless the "Saved connections, passwords and logs" option is ticked, which runs `Marconi.exe --remove-my-data` for
the account signed in (and says so if the uninstaller is running as a different account). `Uninstall.exe /S`
uninstalls silently, keeping the data, and the installer accepts `/S` and `/D=<folder>` too.

`Installer/Marconi.nsi` is the installer script. The icons, `Modules/MarconiUI/Resources/Marconi.ico` and
`Marconi.icns`, are rendered by `python Tools/make_app_icon.py` from the same drawing as the window icon in
`AppIcon.cpp`. The installer's welcome panel and header images in `Installer/Art` come from
`python Tools/make_installer_art.py`.

## Releasing (macOS)

    ./Package.sh                      # -> Build/Installer/Marconi-0.2.0.48-macos-arm64.dmg

It builds the Release app, wraps it in `Marconi.app` with the icon, Nebula Sans and `Installer/macOS/Info.plist`,
signs it, and makes a disk image to drag it to Applications from. It takes `--clean`, `--skip-build` and
`--allow-dirty`, which work like the Windows options. The app is built for the Mac's own architecture, and it needs
that Mac's macOS version or newer.

By default the app is signed ad-hoc, which is enough on the Mac that built it. On another Mac, macOS blocks a
downloaded copy the first time. Try to open it, then click *Open Anyway* in *System Settings > Privacy & Security*.
Copies shared by AirDrop or USB stick may be blocked too. For a release, set `MARCONI_SIGN_IDENTITY` to a Developer
ID Application certificate, and set `MARCONI_NOTARY_PROFILE` to a profile from `xcrun notarytool store-credentials`.
`Package.sh` then signs with the hardened runtime, notarizes the disk image and staples the ticket.

## Testing

`Tests/MarconiCoreTest` covers the core:

- address parsing and text conversion;
- streams;
- the handshake, VNC auth and Apple auth against a scripted fake server on loopback;
- VeNCrypt negotiation, switching the connection to an encrypted stream, certificate trust decisions, falling back
  to an unencrypted login when a server only offers encryption without a certificate, and the handshake timeout,
  with a stand-in for TLS;
- the extended clipboard messages (text in any language and images), against the fake server;
- every decoder, using hand-built encoded data;
- the client message loop;
- connecting, probing for protocols, DNS messages and network discovery, against local fake services.

The exit code is the number of failures.

`Tools/fake_vnc_server.py` is a tiny server for manual testing. It serves an animated test pattern with no
authentication and logs every key, pointer and clipboard event it receives:

    python Tools/fake_vnc_server.py --port 5999 --size 1024x768 [--cut-on-connect "text"]
    marconi 127.0.0.1::5999

`Tools/fake_vencrypt_server.py` is the same idea for encryption: it only accepts VeNCrypt over real TLS (a
self-signed certificate, or anonymous TLS), then serves one frame and waits for a key press.

## Encryption: how it works and what's left

- `Modules/MarconiCore/Net/Tls.h` is the interface: `TlsStream` (handshake, read, write, shutdown, protocol name)
  and `CreateTlsStream`, with `TlsOptions` carrying the host name, whether the server is anonymous and a callback
  asked when this computer doesn't trust the certificate. `Net/Tls.cpp` adapts the engine's `Crypto::TlsClient`
  to it.
- The engine's `Crypto` module provides `TlsClient` on mbedTLS (which it already builds for SSH) on macOS and
  Linux, with `Tests/CryptoTest` running it against a loopback TLS server. On Windows it reports itself as
  unavailable: the mbedTLS sources aren't built there (libssh2 uses Windows' own cryptography), so either Schannel
  or building mbedTLS on Windows too is the remaining step.
- `Rfb::Handshake` and `Rfb::Auth` negotiate VeNCrypt (type 19, version 0.2) and prefer it whenever a build can
  encrypt, choosing a subtype with a certificate over anonymous TLS and never the unencrypted Plain subtype. mbedTLS
  has no anonymous suites, so when a server offers only the anonymous subtypes the handshake reports
  `encryptionUnusable` and the viewer connects again without asking for encryption. `Rfb::Client` switches both
  directions of the connection to the stream and reports `IsEncrypted()`.
- The viewer shows the certificate's details and fingerprint when it isn't trusted, remembers the fingerprint the
  user accepts with the saved connection, and shows the encryption state in the status bar.
- `Tools/TlsStream_OpenSSL.cpp` is the reference implementation the mbedTLS one follows (memory buffers and one
  mutex, released while waiting for the network). It is not part of the build.

Certificate trust comes from the system's certificate bundle (`/etc/ssl/certs/ca-certificates.crt` and the usual
alternatives, `/etc/ssl/cert.pem` on macOS); VNC servers almost always use self-signed certificates, so the trust
question and the remembered fingerprint are the normal path.

## Layout

- `Modules/MarconiCore`: the VNC (RFB) protocol client, with no UI. It covers sockets, the stream, the handshake and
  authentication (including VeNCrypt and the TLS stream interface), decoders, the framebuffer, the client thread,
  protocol probing and network discovery (mDNS), and includes no wx headers.
- `Modules/MarconiUI`: the wxWidgets app. It contains the connections window, the viewer window and canvas, key
  mapping, clipboard sync and the saved connections store.
- `Tests/MarconiCoreTest`: core tests.
- `Installer/`: the Windows installer script (NSIS) and the macOS app bundle files.
- `Tools/`: development helpers.

Marconi also uses the una engine's `Crypto` module, which provides DES, AES, MD5, Diffie-Hellman and random numbers
through each platform's own crypto library.
