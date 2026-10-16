# Marconi

**M**ulti-protocol **A**ccess and **R**emote **CON**trol **I**nterface.

A free, cross-platform VNC client for Windows, macOS and Linux, built on the una engine with a wxWidgets UI.

It works with macOS Screen Sharing (Apple account login) and with standard RFB servers: TightVNC, TigerVNC,
RealVNC, x11vnc, UltraVNC and others.

## Features

- **Authentication:** none, VNC password, and Apple Remote Desktop account login (macOS Screen Sharing).
- **Encodings:** Tight (including JPEG), ZRLE, Hextile, RRE, CopyRect and Raw, with configurable compression and JPEG
  quality.
- **Display:** the server's cursor is drawn locally, so the pointer doesn't lag. Remote resolution changes are
  followed (DesktopSize and ExtendedDesktopSize).
- **Viewing:** scale to fit the window or show at actual size, full screen, and view-only mode.
- **Clipboard:** text is shared in both directions.
- **Keyboard:** layout-correct key mapping. On a Mac, Command becomes Super on the remote side. Keys are released
  when the window loses focus, so none get stuck. You can send Ctrl+Alt+Del.
- **Saved connections:** an address book with passwords in the system keychain (Windows Credential Manager, macOS
  Keychain, libsecret on Linux). Each connection remembers its view settings.

## Using it

Run `marconi` to open the connection window. Type an address and press Enter, pick a saved connection, or
double-click a machine under **On this network**.

### Connecting

You don't choose a protocol. Marconi looks at what the machine offers (VNC on 5900, Remote Desktop on 3389 and SSH
on 22, all at once) and uses the best one it can. A saved connection remembers what it found, so the next connection
starts straight away; if that stops working, Marconi looks again once.

Remote Desktop (RDP) and SSH are detected but not supported yet. When a machine only offers one of those, Marconi
says so instead of reporting a failed connection.

**On this network** lists the machines that advertise Screen Sharing (VNC), Remote Desktop or SSH on the local
network (mDNS, as Macs do), and refreshes every 10 seconds.

Addresses can be written as:

| You type | Connects to |
|---|---|
| `host` | whatever the machine offers |
| `host:1` | VNC display 1, which is port 5901 (numbers below 100 are display numbers) |
| `host::5901` | port 5901, whatever answers there |
| `[fe80::1]:1` | an IPv6 address with a display number |
| `vnc://host`, `vnc://host:5901` | VNC only, on port 5900 or the port given |
| `rdp://host`, `ssh://host` | force Remote Desktop or SSH (detected, not supported yet) |
| `name@host` | any of the above, logging in with the account `name` (Mac account login) |

From the command line:

    marconi [--scale fit|100] [--view-only] [--fullscreen] [--no-clipboard] host[:display]

Open a saved connection with `--saved NAME` (its name, address or id), using its keychain password.

**Headless mode** (testing and automation) runs a connection without a window or dialogs. It logs and saves what
the viewer would show as PNGs in the log folder, then exits:

    marconi --saved "Studio Mac" --headless --view-only --snapshot-every 5 --quit-after 60

While connected, **F8** opens the menu, which still works in full screen. From it you can change scaling, switch
full screen, view-only and clipboard sharing, send Ctrl+Alt+Del or F8, refresh, or disconnect.

To connect to a Mac, turn on *System Settings > General > Sharing > Screen Sharing* on the Mac. Then log in with an
account on that Mac.

Logs are written to the user data folder:

| Platform | Log folder |
|---|---|
| Windows | `%LOCALAPPDATA%\marconi\logs` |
| macOS | `~/Library/Application Support/marconi/logs` |
| Linux | `~/.local/share/marconi/logs` |

Marconi used to be called rmvnc. On first start it copies rmvnc's saved connections and passwords; the old ones are
left in place.

## Appearance

The connections window includes search by name or address and a **System / Light / Dark** selector.
Your appearance choice is remembered between launches. The main window updates immediately; on Windows,
restart Marconi to finish updating native menus and title bars. **Settings** also provides appearance selection
and access to the log folder. Connection and credential dialogs use the same palette.

## Building

You need the una engine (`C:\code\engine`, or set `UNA_ENGINE_PATH`) with BuildTool bootstrapped.

**Windows.** Any PowerShell or cmd window works; the MSVC environment is set up automatically and needs
toolset 14.44 or newer.

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

> Incremental builds rely on BuildTool's header dependency tracking (engine commit 3d0ce07 or later). With an older BuildTool, build with `-Clean` after editing a header.

## Versions

Versions are `MAJOR.MINOR.PATCH.REVISION`, for example `0.1.0.27`:

- `MAJOR.MINOR.PATCH` follows semantic versioning and is set in `Project.json`. To start a new release line, change
  it there.
- `REVISION` is the commit's position in the history: the first commit is 1, the second is 2, and so on
  (`git rev-list --count HEAD`). Each commit on `main` gets a higher number than the last.

When to change `MAJOR.MINOR.PATCH`:

- A big feature bumps MINOR (0.2.0 to 0.3.0) in the change that adds it.
- Marconi stays on 0.x until its own server component exists, even if something breaks backwards compatibility.
- 1.0 is the owner's call, made when Marconi feels complete, not when a rule says so.

`Build.ps1` and `Build.sh` write the version to `Modules/MarconiCore/BuildVersion.h`, a generated file that isn't
committed, before every build. Marconi logs its version and commit when it starts. On Windows the version is also
in the executable's properties. A build without git history (such as a source zip) gets revision 0, and a shallow
clone gets a revision that is too low.

## Releasing (Windows)

The installer is built with [NSIS 3](https://nsis.sourceforge.io) (`winget install NSIS.NSIS`):

    .\Package.ps1                     # -> Build\Installer\Marconi-0.1.0.27-win64-setup.exe

It builds the Release app, then packages `Marconi.exe` with the Visual C++ runtime DLLs from your Visual Studio, so
users don't need the separate redistributable. It also bundles Nebula Sans Black and Semibold and their SIL Open Font License.
The font loads privately for Marconi, so branding works without a system font installation and uninstalling
Marconi does not affect fonts used by other applications. Then it prints the installer's SHA-256 and the command to tag the
release (`v0.1.0.27`). It refuses a shallow clone, and refuses uncommitted changes to tracked files unless you pass
`-AllowDirty` for a test package. `-SkipBuild` packages the existing build if it matches the current version.

The installer installs to `Program Files\Marconi`, with a Start menu shortcut and an optional desktop one. It also
adds an entry to Settings > Apps. Installing a newer version replaces the old one. Uninstalling keeps each user's
saved connections, passwords and logs. `Uninstall.exe /S` uninstalls silently, and the installer accepts `/S` and
`/D=<folder>` too.

`Installer/Marconi.nsi` is the installer script. The icon, `Modules/MarconiUI/Resources/Marconi.ico`, is rendered by
`python Tools/make_app_icon.py` from the same drawing as the window icon in `AppIcon.cpp`.

## Testing

`Tests/MarconiCoreTest` covers the protocol core:

- address parsing and text conversion;
- streams;
- the handshake, VNC auth and Apple auth against a scripted fake server on loopback;
- every decoder, using hand-built encoded data;
- the client message loop.

The exit code is the number of failures.

`Tools/fake_vnc_server.py` is a tiny server for manual testing. It serves an animated test pattern with no
authentication and logs every key, pointer and clipboard event it receives:

    python Tools/fake_vnc_server.py --port 5999 --size 1024x768 [--cut-on-connect "text"]
    marconi 127.0.0.1::5999

## Layout

- `Modules/MarconiCore`: the headless RFB client. It covers sockets, the stream, the handshake and authentication,
  decoders, the framebuffer and the client thread, and includes no wx headers.
- `Modules/MarconiUI`: the wxWidgets app. It contains the connections window, the viewer window and canvas, key
  mapping, clipboard sync and the saved connections store.
- `Tests/MarconiCoreTest`: core tests.
- `Installer/`: the Windows installer script (NSIS), built by `Package.ps1`.
- `Tools/`: development helpers.

Marconi also uses the una engine's `Crypto` module, which provides DES, AES, MD5, Diffie-Hellman and random numbers
through each platform's own crypto library.

## Not yet supported

- Encrypted connections (VeNCrypt/TLS). Tunnel over SSH if you need encryption: VNC password and Apple
  authentication protect the login, not the session.
- Apple's newer authentication types (33, 35 and 36). Type 30, which all recent macOS versions offer, is
  supported.
- File transfer, the extended (UTF-8) clipboard, and resizing the remote desktop from the client.
