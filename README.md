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
- Copy and paste text between the two computers, in either direction.
- Remember the computers you connect to, with their passwords kept safely in your system's password store (Windows
  Credential Manager, the macOS Keychain, or your Linux desktop's keyring).
- Find computers on your home or office network by itself.
- Just watch, without sending any keyboard or mouse input ("view only").
- Send Ctrl+Alt+Del to a Windows computer.
- Tell you, in the bar at the bottom of the window, whether the connection is encrypted.

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
- turn view-only and clipboard sharing on or off;
- send Ctrl+Alt+Del (or F8 itself) to the other computer;
- refresh the picture, or disconnect.

## Appearance and settings

Marconi follows your system's light or dark appearance, or you can choose **Light** or **Dark** at the bottom of the
connections window. **Settings** has the same choice, and a button that opens Marconi's log folder.

On Windows, a few parts (menus and the title bar) only change colour after you restart Marconi.

## Is the connection private?

Not yet. Marconi can't make encrypted connections, so the screen picture, everything you type and the password check
itself travel over the network as they are. Anyone able to watch the network between the two computers could read
them. The plain VNC password check in particular is weak: someone who records it can work out the password later. A
Mac's account login protects the password itself, but nothing after it.

So use Marconi on networks you trust, such as your home network, or over a VPN. As a reminder, the bar at the bottom
of every connection window says **not encrypted**.

Some servers insist on an encrypted connection (they use a method called VeNCrypt). Marconi recognises them and tells
you it can't connect to them yet, rather than failing in a confusing way.

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

- **Encrypted connections.** See "Is the connection private?" above.
- **Remote Desktop and SSH** connections (recognised, but not supported yet).
- **File transfer.**
- Copying text in some languages and symbols (the clipboard only carries Western European characters for now).
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

- A big feature bumps MINOR (0.2.0 to 0.3.0) in the change that adds it.
- Marconi stays on 0.x until its own server component exists, even if something breaks backwards compatibility.
- 1.0 is the owner's call, made when Marconi feels complete, not when a rule says so.

What changed in each version is in [CHANGELOG.md](CHANGELOG.md).

`Build.ps1` and `Build.sh` write the version to `Modules/MarconiCore/BuildVersion.h`, a generated file that isn't
committed, before every build. Marconi logs its version and commit when it starts. On Windows the version is also
in the executable's properties. A build without git history (such as a source zip) gets revision 0, and a shallow
clone gets a revision that is too low.

## Releasing (Windows)

The installer is built with [NSIS 3.08 or newer](https://nsis.sourceforge.io) (`winget install NSIS.NSIS`). It also
needs the [ShellExecAsUser plug-in](https://nsis.sourceforge.io/ShellExecAsUser_plug-in), which lets the installer start
Marconi, or open its source code on GitHub, as you rather than as administrator when you tick "Start Marconi now" or
"Open the source code on GitHub". Download its Unicode build and put `ShellExecAsUser.dll` in NSIS's
`Plugins\x86-unicode` folder (normally `C:\Program Files (x86)\NSIS\Plugins\x86-unicode`). `Package.ps1` stops with a
message if it isn't there.

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

The installer shows the MIT licence, which must be accepted before installing. It installs to `Program Files\Marconi`,
with Start menu shortcuts to Marconi and to its source code on GitHub, and an optional desktop shortcut. It also adds an
entry to Settings > Apps. Installing a newer version replaces the old one, as long as the old one is in `Program Files`;
a copy installed somewhere else is left in place and the new version is installed over it, with a line in the
installer's details saying so. Uninstalling keeps each user's saved connections, passwords and logs unless the "Saved
connections, passwords and logs" option is ticked, which runs `Marconi.exe --remove-my-data` for the account signed in
(and says so if the uninstaller is running as a different account). `Uninstall.exe /S` uninstalls silently, keeping the
data, and the installer accepts `/S` and `/D=<folder>` too.

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
- VeNCrypt negotiation, switching the connection to an encrypted stream, certificate trust decisions and the
  handshake timeout, with a stand-in for TLS;
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

## Encryption: what's done and what's left

Everything Marconi needs for encrypted connections is in place except the TLS stream itself:

- `Modules/MarconiCore/Net/Tls.h` is the interface: `TlsStream` (handshake, read, write, shutdown, protocol name)
  and `CreateTlsStream`, with `TlsOptions` carrying the host name, whether the server is anonymous and a callback
  asked when this computer doesn't trust the certificate.
- `Rfb::Handshake` and `Rfb::Auth` negotiate VeNCrypt (type 19, version 0.2) and prefer it whenever a build can
  encrypt, choosing a subtype with a certificate over anonymous TLS and never the unencrypted Plain subtype.
  `Rfb::Client` switches both directions of the connection to the stream and reports `IsEncrypted()`.
- The viewer shows the certificate's details and fingerprint when it isn't trusted, remembers the fingerprint the
  user accepts with the saved connection, and shows the encryption state in the status bar.
- `Tools/TlsStream_OpenSSL.cpp` is a reference implementation of the interface on OpenSSL, run end to end against
  `fake_vencrypt_server.py` (TLS 1.3 with a certificate, TLS 1.2 anonymous, VNC password and user name logins,
  certificate refusal). It is not part of the build.

What's left is to implement the interface with each platform's own TLS library, as the engine's `Crypto` module
does for its other primitives: Schannel on Windows (certificates only; Schannel has no anonymous suites), the
Security framework on macOS, OpenSSL on Linux. Then `Net/Tls.cpp` returns it, `IsTlsAvailable()` becomes true and
the rest switches on by itself. The reference file documents the two things an implementation must get right: the
handshake completes before the trust decision is asked, and `Read` and `Write` may run on two threads at once
without a writer ever waiting for incoming data.

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
