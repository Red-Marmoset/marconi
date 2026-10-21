# Marconi

**M**ulti-protocol **A**ccess and **R**emote **CON**trol **I**nterface.

Marconi lets you see and control another computer's screen from your own, as if you were sitting in front of it.
It's free, and runs on Windows, macOS and Linux.

It works with:

- **Macs**, through the Mac's built-in Screen Sharing;
- **Windows and Linux computers** running a VNC server, such as TightVNC, TigerVNC, RealVNC, UltraVNC or x11vnc.

## What it can do

- See the other computer's screen, scaled to fit your window or zoomed from 50% to 200%, in a window or full
  screen.
- Use its keyboard and mouse. Keys match your keyboard layout, and a Mac's Command key works as expected.
- Copy and paste text between the two computers, in either direction, and pictures with servers that allow it.
- Remember the computers you connect to, with their passwords kept safely in your system's password store (Windows
  Credential Manager, the macOS Keychain, or your Linux desktop's keyring), plus notes, favourites, groups, and
  where each one's window was.
- Open `vnc://` links from other programs (Windows and macOS do this once Marconi is installed; on Linux, see
  Building).
- Connect through SSH, so the whole session is encrypted even with servers that don't encrypt.
- Find computers on your home or office network by itself.
- Copy files to and from the other computer, by dragging them or with Upload and Download buttons.
- Just watch, without sending any keyboard or mouse input ("view only").
- Send Ctrl+Alt+Del to a Windows computer.
- Connect with encryption when the other computer offers it, and tell you, in the bar at the bottom of the window,
  whether the connection is encrypted.

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

Press **F8** to open Marconi's menu, even in full screen. In full screen, resting the pointer at the very top of the
screen also shows a small bar with the computer's name and the main controls; its **Pin** button keeps it there.
From the menu you can:

- zoom: fit the window, or 50% to 200% (Ctrl and the mouse wheel, or Ctrl and + or -, do the same). A zoomed-in
  picture has scrollbars, scrolls with Ctrl+Shift and the wheel, and in full screen scrolls when the pointer rests
  at the edge of the screen; with view only on, you can also drag it around;
- go full screen or back;
- turn view-only and clipboard sharing on or off, or send your clipboard to the other computer and get its
  clipboard by hand;
- send Ctrl+Alt+Del, Alt+Tab, the Windows key and other shortcuts your own computer would otherwise act on (or F8
  itself) to the other computer;
- send system shortcuts such as Alt+Tab, the Windows key and Cmd+Tab to the other computer as you press them, on
  Windows and macOS (macOS asks you to allow Marconi under Privacy & Security > Accessibility first; Linux desktops
  don't allow this);
- show just one of the other computer's displays, if it has several and its server says so (TigerVNC and UltraVNC
  do; Marconi remembers the choice);
- ask the other computer to change its screen size to fit this window, where its server allows it (this changes
  the screen for anyone sitting at it too);
- save a screenshot of the other computer's screen as a PNG file, or copy it, with or without the mouse pointer;
- refresh the picture, or disconnect.

The right-hand end of the status bar shows how long a message takes to reach the other computer and come back,
measured every few seconds. If the other computer stops answering, it says so and for how long, so you can tell a
frozen connection from a slow one.

## Copying files

Press **F10**, or click **Files** on the toolbar, to open the file panel: this computer on the left, the other
computer on the right. Copy files by dragging them between the two sides, from your desktop onto the right side, or
from the right side onto your desktop. You can also select files and click **Upload** or **Download**. Files dropped
onto the other computer's screen are copied to its Desktop.

- **A Mac:** turn on *System Settings > General > Sharing > Remote Login* as well as Screen Sharing. Marconi uses the
  same account. The first time, it shows the Mac's security fingerprint and asks whether to trust it.
- **Windows with TightVNC:** nothing more to set up; files travel over the same connection. The first time you drop
  files on its screen, Marconi asks which folder they should go to.

Copy and paste works too. Copy files in Explorer or Finder, then press Ctrl+V on the other computer's screen (they
go to its Desktop) or in the right side of the file panel (they go to the folder shown). Select files on either side
of the panel and press Ctrl+C to copy them: files from the other computer are brought over first, then you can paste
them in Explorer or Finder like any others.

If a file with the same name is already there, Marconi asks whether to replace it, keep both, or skip it.

## Waking a sleeping computer

If you give a saved connection the other computer's hardware address (its "MAC address", which `ipconfig /all`
on Windows shows as the Physical Address), the **Wake up** button sends it a wake-up call over the network, and
Marconi offers to do the same when it can't connect. The computer has to be on the same network and set to wake
from the network (usually a setting in its BIOS or network adapter).

## Appearance and settings

Marconi follows your system's light or dark appearance, or you can choose **Light** or **Dark** at the bottom of the
connections window. **Settings** has the same choice, and a button that opens Marconi's log folder.

On Windows, a few parts (menus and the title bar) only change colour after you restart Marconi.

## Is the connection private?

Only when the other computer offers encryption. Servers such as TigerVNC and UltraVNC can be set up to encrypt the
connection (a method called VeNCrypt, using TLS, the same encryption as secure websites). Marconi uses it whenever
a server offers it with a certificate. The first time, it shows you the server's certificate and asks whether to
trust it; it remembers your answer with the saved connection, and warns you if the certificate ever changes. The
bar at the bottom of the window then says **encrypted**.

**Connecting through SSH.** Tick *Connect through SSH* when editing a saved connection (or use `--ssh`). Marconi then
logs in to the other computer's SSH server with the connection's login name and password, has it connect to its own
VNC server, and runs the whole session inside SSH: the status bar says "encrypted, SSH". This works with any VNC
server, including ones that can't encrypt, and the VNC port needn't be reachable from your computer at all, only
SSH. The first time, Marconi shows the computer's SSH fingerprint and asks whether to trust it, as it does for file
transfer; the two share the answer. On a Mac, turn on *Remote Login* as well as Screen Sharing; on Linux, run an SSH
server; on Windows, install the optional OpenSSH Server feature. No probe is made: the VNC port is the one in the
address, or 5900.

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

If a connection drops while you're using it, the window stays open with the last picture dimmed, and Marconi tries
again by itself: after 2 seconds, then 4, 8, 16 and every 30 seconds after that, for up to 5 minutes. A bar at the
top shows the countdown with **Retry Now** and **Disconnect** buttons. Nothing you type while it's disconnected is
sent later.

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

## Sharing this computer (Marconi Server)

Marconi can now also work the other way round. **Marconi Server** is a separate program that shares *this*
computer, so someone using Marconi on another computer can see its screen and control it. You don't need any other
VNC server.

It's new and still being built, so for now you start it by hand (an installer, and a way to set it up from the
Marconi window, are coming next):

1. Start the server program. It puts a small icon in the system tray (near the clock). The first time, it asks you
   to choose an access password — anyone connecting has to type it. You can leave it empty for no password, though a
   password is a good idea.
2. On the other computer, open Marconi and connect to `marconi://` followed by this computer's name or network
   address (for example `marconi://office-pc`). The first time, Marconi shows you the server's identity and asks if
   you trust it; after that it connects without asking, and warns you if the identity ever changes.
3. The connection is always private: the screen and everything you type are encrypted.

From the tray icon you can set or change the password, change the port the server listens on, and quit. The server
keeps its settings and a log in a "marconi-server" folder in your user account.

Right now the server shows this computer's actual screen on Windows; on macOS and Linux it shows a moving test
picture for the moment, while the screen-sharing part for those systems is finished.

## Not available yet

- **Encryption without a certificate** (TigerVNC's "TLSVnc"). See "Is the connection private?" above.
- **Remote Desktop and SSH** connections (recognised, but not supported yet).
- Copying text in some languages and symbols with servers that don't support the newer clipboard messages (Macs,
  TightVNC): with those, the clipboard only carries Western European characters, and Marconi says so when some
  can't be sent.
- VNC passwords containing characters outside Western European languages. The VNC password check only carries
  those, so such a password won't work with Marconi. A Mac account login accepts any characters.
- Changing the other computer's screen resolution with servers that don't support it (Macs, TightVNC).
- Apple's newest login methods. The one every recent macOS version offers works.

---

# For developers

## Command line

    marconi [--scale fit|50|75|100|125|150|200] [--view-only] [--fullscreen] [--no-clipboard] [--ssh] [--saved NAME] [address]
    marconi --remove-my-data

`--saved NAME` opens a saved connection by its name, address or id, using its stored password. `--remove-my-data`
removes the current user's saved connections, keychain passwords, settings and logs, then exits; the Windows
uninstaller runs it for the "Saved connections, passwords and logs" option.

**Headless mode**, for testing and automation, runs a connection without a window or dialogs. It logs what happens
and saves what the viewer would show as PNG files in the log folder, then exits:

    marconi --saved "Studio Mac" --headless --view-only --snapshot-every 5 --quit-after 60

Remote Desktop (RDP) support is being built in stages (see `Documentation/RDP.md`); the viewer can't open an RDP
connection yet. To try the part that is done, `--rdp-check` connects to an RDP server, negotiates, does the TLS
handshake and, if the server asks for Network Level Authentication, logs in, then logs the result and exits:

    marconi rdp://user:password@host --rdp-check

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

To open `vnc://` links in Marconi on Linux, copy `Installer/Linux/marconi.desktop` to
`~/.local/share/applications/`, change its `Exec` line to the path of the `Marconi` you built (keeping the `%u`),
then run `xdg-mime default marconi.desktop x-scheme-handler/vnc`.

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
start Marconi, or open its source code on GitHub, as you rather than as administrator when you tick "Start Marconi
now" or "Open the source code on GitHub". Download its Unicode build and put `ShellExecAsUser.dll` in NSIS's
`Plugins\x86-unicode` folder (normally `C:\Program Files (x86)\NSIS\Plugins\x86-unicode`). `Package.ps1` stops with
a message if it isn't there.

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

The installer shows the MIT license, which must be accepted before installing. It then asks who Marconi is for:

- **Just me** installs to `%LOCALAPPDATA%\Programs\Marconi`, with the shortcuts and the Settings > Apps entry for
  your account only. It needs no administrator permission.
- **Everyone on this PC** installs to `Program Files\Marconi` for every account. Setup asks for administrator
  permission when you choose it, then carries on from the next page. "Start Marconi now" still starts Marconi as
  you, not as the administrator.

It suggests the kind already installed, if any, and says which versions are installed each way. Either way you get
Start menu shortcuts to Marconi and to its source code on GitHub, and an optional desktop shortcut. Installing a newer
version replaces the old one of the same kind.
When Setup has administrator permission it only runs an old uninstaller from inside `Program Files`; a copy
installed somewhere else is left in place and the new version is installed over it, with a line in the installer's
details saying so. The uninstaller asks for administrator permission only for an install for everyone.

Uninstalling keeps each user's saved connections, passwords and logs unless the "Saved connections, passwords and
logs" option is ticked, which runs `Marconi.exe --remove-my-data` for the account signed in (and says so if the
uninstaller is running as a different account). `Uninstall.exe /S` uninstalls silently, keeping the data. The
installer accepts `/S`, `/D=<folder>`, and `/CURRENTUSER` or `/ALLUSERS` to choose without asking; a silent install
with neither picks the kind already installed, or everyone when run as administrator, or otherwise just you.

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
- The engine's `Crypto` module provides `TlsClient` on mbedTLS on every platform (it was already built for SSH on
  macOS and Linux; Windows builds it for TLS alone, libssh2 there keeps Windows' own cryptography), with
  `Tests/CryptoTest` running it against a loopback TLS server.
- `Rfb::Handshake` and `Rfb::Auth` negotiate VeNCrypt (type 19, version 0.2) and prefer it whenever a build can
  encrypt, choosing a subtype with a certificate over anonymous TLS and never the unencrypted Plain subtype. mbedTLS
  has no anonymous suites, so when a server offers only the anonymous subtypes the handshake reports
  `encryptionUnusable` and the viewer connects again without asking for encryption. `Rfb::Client` switches both
  directions of the connection to the stream and reports `IsEncrypted()`.
- The viewer shows the certificate's details and fingerprint when it isn't trusted, remembers the fingerprint the
  user accepts with the saved connection, and shows the encryption state in the status bar.
- `Tools/TlsStream_OpenSSL.cpp` is the reference implementation the mbedTLS one follows (memory buffers and one
  mutex, released while waiting for the network). It is not part of the build.

Certificate trust comes from Windows' root store, or the system's certificate bundle elsewhere
(`/etc/ssl/certs/ca-certificates.crt` and the usual alternatives, `/etc/ssl/cert.pem` on macOS); VNC servers almost
always use self-signed certificates, so the trust question and the remembered fingerprint are the normal path.

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
