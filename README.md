# rmvnc

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

Run `rmvnc` to open the connection window. Type an address and press Enter, or pick a saved connection.

Addresses can be written as:

| You type | Connects to |
|---|---|
| `host` | port 5900 |
| `host:1` | display 1, which is port 5901 (numbers below 100 are display numbers) |
| `host::5901` | port 5901 |
| `[fe80::1]:1` | an IPv6 address with a display number |
| `vnc://host:5901` | URL form, where the number is a port |

From the command line:

    rmvnc [--scale fit|100] [--view-only] [--fullscreen] [--no-clipboard] host[:display]

While connected, **F8** opens the menu, which still works in full screen. From it you can change scaling, switch
full screen, view-only and clipboard sharing, send Ctrl+Alt+Del or F8, refresh, or disconnect.

To connect to a Mac, turn on *System Settings > General > Sharing > Screen Sharing* on the Mac. Then log in with an
account on that Mac.

Logs are written to the user data folder:

| Platform | Log folder |
|---|---|
| Windows | `%LOCALAPPDATA%\rmvnc\logs` |
| macOS | `~/Library/Application Support/rmvnc/logs` |
| Linux | `~/.local/share/rmvnc/logs` |

## Building

You need the una engine (`C:\code\engine`, or set `UNA_ENGINE_PATH`) with BuildTool bootstrapped.

**Windows.** Any PowerShell or cmd window works; the MSVC environment is set up automatically and needs
toolset 14.44 or newer.

    .\Build.ps1                       # Release app  -> Build\RmVnc\Release\RmVnc.exe
    .\Build.ps1 -Config Debug
    .\Build.ps1 -Project Tests\RmVncCoreTest\Project.json -Config Debug

**macOS.** The first build compiles wxWidgets, which takes a while.

    ./Build.sh
    ./Build.sh Tests/RmVncCoreTest/Project.json -config Debug

**Linux.** The first build compiles wxWidgets for GTK3. It needs these packages (Debian/Ubuntu names):

    sudo apt install build-essential pkg-config libgtk-3-dev libsecret-1-dev libgl1-mesa-dev \
                     libxxf86vm-dev libxtst-dev libsm-dev
    ./Build.sh

> Incremental builds rely on BuildTool's header dependency tracking (engine commit 26d7d0b or later). With an older BuildTool, build with `-Clean` after editing a header.

## Testing

`Tests/RmVncCoreTest` covers the protocol core:

- address parsing and text conversion;
- streams;
- the handshake, VNC auth and Apple auth against a scripted fake server on loopback;
- every decoder, using hand-built encoded data;
- the client message loop.

The exit code is the number of failures.

`Tools/fake_vnc_server.py` is a tiny server for manual testing. It serves an animated test pattern with no
authentication and logs every key, pointer and clipboard event it receives:

    python Tools/fake_vnc_server.py --port 5999 --size 1024x768 [--cut-on-connect "text"]
    rmvnc 127.0.0.1::5999

## Layout

- `Modules/RmVncCore`: the headless RFB client. It covers sockets, the stream, the handshake and authentication,
  decoders, the framebuffer and the client thread, and includes no wx headers.
- `Modules/RmVncUI`: the wxWidgets app. It contains the connections window, the viewer window and canvas, key
  mapping, clipboard sync and the saved connections store.
- `Tests/RmVncCoreTest`: core tests.
- `Tools/`: development helpers.

rmvnc also uses the una engine's `Crypto` module, which provides DES, AES, MD5, Diffie-Hellman and random numbers
through each platform's own crypto library.

## Not yet supported

- Encrypted connections (VeNCrypt/TLS). Tunnel over SSH if you need encryption: VNC password and Apple
  authentication protect the login, not the session.
- Apple's newer authentication types (33, 35 and 36). Type 30, which all recent macOS versions offer, is
  supported.
- File transfer, the extended (UTF-8) clipboard, and resizing the remote desktop from the client.
