# Marconi

**M**ulti-protocol **A**ccess and **R**emote **CON**trol **I**nterface.

Marconi lets you see and control another computer's screen from your own, as if you were sitting in front of it.
It's free, and runs on Windows, macOS and Linux, with apps for Android and for iPad and iPhone (see
`Apps/`).

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
- Open `vnc://` links from other programs (Windows and macOS do this once Marconi is installed; on Linux, install
  the package or see Building).
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

Some computers offer Remote Desktop (built into Windows, and a choice on many Linux desktops) instead of VNC. Marconi
connects to those too: you log in with an account on that computer. Over Remote Desktop the picture, keyboard and
mouse work; the clipboard and changing the other computer's screen size come later. SSH (a text-only connection) is
recognised but not supported, and Marconi tells you so.

### Ways to write an address

Most of the time a name or address on its own is all you need. For special cases:

| You type | What it means |
|---|---|
| `studio-mac.local` | connect however the computer allows |
| `studio-mac.local:1` | VNC screen number 1 (used by some Linux servers) |
| `studio-mac.local::5901` | a specific port number, here 5901 |
| `alice@studio-mac.local` | log in to a Mac as the user `alice` |
| `vnc://studio-mac.local` | VNC only |
| `rdp://drips` | Remote Desktop only |
| `ssh://drips` | SSH only (recognised, not supported yet) |
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
- send Ctrl+Alt+Del, Alt+Tab, the Windows key, a Mac's screenshot shortcuts and other shortcuts your own computer
  would otherwise act on (or F8 itself) to the other computer, from a menu for each of Windows, Mac and Linux (just
  the Mac one when the other computer is a Mac using its own screen sharing);
- send system shortcuts such as Alt+Tab, the Windows key and Cmd+Tab to the other computer as you press them, on
  Windows and macOS (macOS asks you to allow Marconi under Privacy & Security > Accessibility first; Linux desktops
  don't allow this);
- show just one of the other computer's displays, if it has several and its server says so (TigerVNC and UltraVNC
  do; Marconi remembers the choice);
- ask the other computer to change its screen size to fit this window, where its server allows it (this changes
  the screen for anyone sitting at it too);
- save a screenshot of the other computer's screen as a PNG file, or copy it, with or without the mouse pointer;
- send game controllers to the other computer (off until you tick it; Marconi Server only, and only while this
  window is in front; see "Game controllers" under Sharing this computer);
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

It's an early version. It has been tried on Linux and on Windows, but not yet on a Mac, and the Windows installer and
the Windows service below have not been fully tried, so expect rough edges.

### Installing it from Marconi

You don't have to walk to the other computer. While you are connected to a Windows computer over VNC:

1. Press F8 and choose **Install Marconi Server on the Other Computer...**. Marconi explains what will happen. If the
   connection can carry files (TightVNC does), it copies the Marconi Server installer to that computer's desktop.
   Otherwise it can copy the download address to the clipboard for you to open in a web browser over there.
2. On the other computer's screen, in Marconi, double-click the installer on its desktop and say yes when Windows asks
   for permission.
3. Setup lets Marconi Server through Windows Firewall (you can untick that) and starts it whenever someone signs in
   (you can untick that too). When Setup finishes, Marconi Server starts and asks you to choose an **access
   password**. Anyone who knows it can control that computer, so pick one you would not mind guarding. It doesn't
   have to match the VNC password.
4. Close the connection and connect again. Marconi finds Marconi Server by itself and uses it;
   your saved connection stays as it is.

To share the computer you are sitting at, open Marconi's **Settings** and choose **Share this computer...**, which
starts the same installer here.

The installer comes inside Marconi's own Windows setup (it is an option you can untick). To remove Marconi Server, use
Windows' Settings > Apps, or run `Uninstall.exe` in `Program Files\Marconi Server`. That removes the program and the
firewall rule, and keeps the settings and password unless you tick the option to remove them too. Running the
installer with `/S` installs without any questions; Marconi Server then asks for a password the first time it starts.

### What it does and doesn't do yet

It does: show this computer's screen and take the other computer's keyboard and mouse, always encrypted, behind an
access password. It also shares the clipboard (text and pictures), shows each of the computer's screens separately,
changes the screen's size to fit your window, shows the other computer's mouse pointer shape, and can lock the computer
when you disconnect. Copying files is available but off until you turn it on in the tray menu. From the tray menu you
can also limit who can connect (by address), disconnect a viewer that has been idle, and choose whether you are told
when someone connects.

It doesn't yet: play sound, or let more than one person connect at once. Game controllers are written but only tried
with stand-ins (see below). It runs as a program
in the signed-in user's session; a Windows service that also works at the sign-in screen and through a restart is
written but has not been tried on Windows. If you change its port, the Windows Firewall rule (made for port 1895) has
to be changed to match. A server with no password refuses every viewer, so set one. Other VNC viewers can't connect to
it; it only talks to Marconi.

### Game controllers

Tick **Send Game Controllers to the Other Computer** in the viewer's menu (it is off until you do) and the controllers
plugged into your computer appear on the other one, while the viewer window is in front: up to four, with the
shaking sent back to your controller. Let go of the window and they are released and removed, so nothing stays
pressed down over there.

What the other computer needs:

- **Windows:** the free ViGEmBus driver, which Marconi Server does not install. Without it the viewer says "Game
  controllers need the ViGEmBus driver, which isn't installed on this computer." ViGEmBus's author stopped
  maintaining it in 2023, though it still works on Windows 11. Marconi Server also needs a `ViGEmClient.dll` next to
  it; that is not supplied yet.
- **Linux:** access to `/dev/uinput`, which most systems only give an administrator. If it is missing, the viewer is
  told how to give Marconi Server access.
- **Mac:** not available yet.

Your computer can send controllers if it is Windows (Xbox-style controllers) or Linux; a Mac can't yet. PlayStation and
other controllers are shown to the other computer as Xbox controllers. All of this is written, but it has only been
tried with stand-ins: no real controller and no real driver has been used yet.

### Starting it by hand

Start the server program (`MarconiServerTray`, built from `ServerUI/Project.json`). It puts a small icon in the system
tray (near the clock) and, the first time, asks you to choose an access password. On the other computer, connect to
`marconi://` followed by this computer's name or network address (for example `marconi://office-pc`). The first time,
Marconi shows you the server's identity and asks if you trust it; after that it connects without asking, and warns you
if the identity ever changes. From the tray icon you can set or change the password, change the port the server
listens on, and quit.

### Who can connect, and when to hang up

All of this is in the tray icon's menu, and it applies at once (you don't need to restart the server).

- **Who can connect...** is a list of the computers allowed to connect, one per line. Write a single address such as
  `192.168.1.20`, or a whole network such as `192.168.1.0/24` (everything from `192.168.1.0` to `192.168.1.255`).
  Addresses in the newer style, such as `fd00::/8`, work too. If the list is empty, anyone who can reach this computer
  can try to connect, as before; they still need the password. A computer that is not on the list is simply cut off,
  and is told nothing about why; the server writes a line in its log with the address. If you list `127.0.0.1`, that
  covers this computer talking to itself over the older address style only; `::1` is the newer-style one and has to be
  listed separately. Someone already connected when you change the list stays connected.
- **Disconnect idle viewers after** hangs up on a viewer that has not used the mouse or keyboard (or copied anything)
  for the time you pick. The viewer is told why. "Never" (the default) turns it off.
- **Tell me when someone connects** shows a small message on this computer when a viewer connects and when it goes
  away. It is on by default and can be turned off.

On the command line the same things are `--allow ADDRESS_OR_NETWORK` (repeat it for more), `--idle-minutes N` and
`--notify` (prints a line instead of showing a message). The tray icon keeps its choices between runs; the Windows
service does not read them yet.

Where it keeps its settings and logs: on Windows, in `C:\ProgramData\Marconi Server` when the installer made that folder
(the installer lets the account that was signed in during Setup change things there); otherwise, and on macOS and
Linux, in a "marconi-server" folder in your own user account.

On macOS and Linux the server shows a moving test picture for the moment, while the screen-sharing part for those
systems is finished.

### Log files

Every Marconi Server program writes a log file each time it runs, in the "logs" folder inside its settings folder (on
Linux that is `~/.marconi-server/logs`; on Windows `C:\ProgramData\Marconi Server\logs` for the service and its
helper). The file is named with the date and time, each line is saved as it is written, and the newest 20 files of each
program are kept. If you have to report a problem, send the newest file. Passwords and keys are never written to it.
The console program (`MarconiServer`) writes one only when asked: `--log-file FOLDER`.

### Installing the server on Linux

`./PackageServerLinux.sh` builds `Build/Installer/MarconiServer-<version>-linux-<arch>.deb` for Ubuntu, Debian and
computers like them. Install it with `sudo apt install ./MarconiServer-...deb`. It adds **Marconi Server** to the
applications menu and the programs `marconi-server` (with the tray icon) and `marconi-server-console` (without). The
server has to run inside your signed-in desktop, because that is where the screen is, so it is not a system service: to
start it every time you sign in, run `systemctl --user enable --now marconi-server.service` as yourself. It shares an
X11 desktop; a Wayland-only desktop can't be shared yet.

## Letting AI tools use another computer

AI tools such as Claude Code, Codex and Claude Desktop can use Marconi to connect to another computer, look at its
screen, and use its mouse, keyboard and clipboard. The tool still asks before each step unless you tell it not to,
and you can watch what it does by connecting to the same computer with Marconi as usual.

**Connecting your AI tools.** The Windows installer offers it on a page of its own, for the AI tools it finds on
the computer. Any time later, open **Settings > AI tools** in Marconi: it lists Claude Code, Codex and Claude
Desktop, says which can use Marconi, and adds or removes Marconi for each. For Claude Desktop, Add opens Claude so
you can confirm the extension; to remove it, use Claude's own settings, under Extensions.

**Using it.** Ask the AI tool to connect to one of your saved connections by its name ("connect to studio"), or to
any computer by its address, as you would type it in Marconi, and then to do something there. A saved connection
brings its own settings and its remembered password, which MarconiMcp takes from the system keychain itself, so the
AI tool never sees it. To stop that, untick "Let AI tools use the passwords saved in Marconi" in Settings > AI tools;
to keep one computer away from AI tools altogether, tick "Hide from AI tools" in its connection settings. For a
computer that isn't saved, the AI tool can be given the password, or, to keep it out of the conversation, set it in
the `MARCONI_PASSWORD` environment variable for MarconiMcp (Claude Desktop asks for it in the extension's
settings).

**By hand.** The part the AI tools start is a program called MarconiMcp (`marconi-mcp` on Linux), installed beside
Marconi:

- `MarconiMcp --register claude-code` (or `codex`, `claude-desktop`) adds it to that tool; `--unregister` removes it;
  `--status` shows which tools are installed and which can use it.
- `MarconiMcp --read-only` lets an AI tool look but not touch: no mouse, keyboard or clipboard changes.
- A silent Windows install connects it with `/AITOOLS=all` or a list such as `/AITOOLS=claude-code,codex`.
- To add it to Claude Code yourself:
  `claude mcp add --scope user marconi -e MARCONI_PASSWORD=... -- /path/to/MarconiMcp` (leave out `-e ...` if the
  computers need no password).

It keeps a log beside Marconi's own (see "If something goes wrong").

## Keeping Marconi up to date

When Marconi starts it asks GitHub whether a newer version has been released (it sends nothing about you; GitHub
sees only that a copy of Marconi asked). When there is one, the start window says so, with a button that opens the
download for your computer. Settings has a "Check for updates now" button, and a box to untick if you'd rather
Marconi didn't check by itself. On Windows, the installer closes a running Marconi before updating and starts it
again afterwards.

### Checking the Android app is ours

The Android app on the releases page is signed by Red Marmoset Studios, and every version is signed the same way.
To check a download is genuine, compare the SHA-256 fingerprint of its signing certificate with this one:

    58:95:65:E6:06:FA:1D:E5:E0:C4:F2:03:D3:E8:26:6E:09:44:F2:03:D7:3C:39:50:F0:BC:CA:7F:E8:89:E2:99

With the Android SDK, `apksigner verify --print-certs Marconi-<version>-android.apk` prints it. Because it doesn't
come from the Play Store, Android asks the first time whether the app you downloaded it with may install apps.

## Not available yet

- **Encryption without a certificate** (TigerVNC's "TLSVnc"). See "Is the connection private?" above.
- **SSH** connections (recognised, but not supported yet).
- Over **Remote Desktop**: the clipboard, changing the other computer's screen size, the server's pointer when it
  uses the newest large pointer shapes, and the faster picture formats (the plain ones are used, which need more
  bandwidth).
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

Remote Desktop (RDP) support is built in stages (see `Documentation/RDP.md`). `--rdp-check` connects to an RDP
server without a window, negotiates, does the TLS handshake and, if the server asks for Network Level
Authentication, logs in, runs the connection sequence, then logs the result and exits:

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

To open `vnc://` links in Marconi on Linux without installing the package (see Releasing (Linux)), copy
`Installer/Linux/marconi.desktop` to `~/.local/share/applications/`, change its `Exec` line to the path of the
`Marconi` you built (keeping the `%u`), then run `xdg-mime default marconi.desktop x-scheme-handler/vnc`.

**Android, iPad and iPhone.** The viewer core builds for both with the same engine; the apps are in `Apps/`.
`Apps/Android/README.md` explains the Android build (the Una BuildTool with the Android NDK, then Gradle for the
app) and `Apps/iPad/README.md` the iOS one (an Xcode project generated through Una, built on a Mac). Both apps do
VNC, VNC through SSH and Marconi connections, not Remote Desktop yet.

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

`Package.ps1` runs the una engine's `Packaging\PackageProject.ps1`, the packaging script every una project shares,
so the engine (`UNA_ENGINE_PATH`, `C:\code\engine` by default) needs to be up to date. What goes in the installer is
the `package` section of `Project.json`.

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

The installer closes a running Marconi (politely first, by force after ten seconds) before replacing its files, and
starts it again at the end: the finish page's "Start Marconi now" box, or straight away for a silent install. The
uninstaller closes it the same way. It shows the MIT license, which must be accepted before installing. It then asks
who Marconi is for:

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

`Package.ps1` also builds the Marconi Server installer first (`PackageServer.ps1`, from `Installer/MarconiServer.nsi`),
as `Build\Installer\MarconiServer-<version>-win64-setup.exe` with the same version and the same signing settings,
prints its SHA-256, and puts it inside the Marconi installer as the "Marconi Server installer" option. The server
installer needs the same NSIS and ShellExecAsUser plug-in as the client's. It installs for the whole computer (it asks
for administrator permission straight away), asks for an access password, adds a Windows Firewall rule for port 1895,
starts the tray program when someone signs in, and has an uninstaller. Neither the script nor the installer has been
run on Windows yet (see `SERVER_PLAN.md`, Phase 2).

`Installer/Marconi.nsi` is the installer script. The icons, `Modules/MarconiUI/Resources/Marconi.ico` and
`Marconi.icns` and the Linux package's PNGs in `Installer/Linux/icons`, are rendered by
`python Tools/make_app_icon.py` from the same drawing as the window icon in `AppIcon.cpp`. The installer's welcome panel and header images in `Installer/Art` come from
`python Tools/make_installer_art.py`.

## Releasing (Android)

The three package scripts end by building the Android app too, when a release key is set (below) and the computer
has the tools: a JDK, the Android SDK (`ANDROID_HOME`, or where Android Studio installs it), an NDK in the SDK's
`ndk` folder or at `ANDROID_NDK_HOME`, and the engine's BuildTool. The result goes next to the installer, disk
image or package:

    Build/Installer/Marconi-0.9.4.107-android.apk

Without the key or the tools the scripts say what is missing and carry on. `bash Apps/Android/Package.sh` (or
`.\Apps\Android\Package.ps1`) builds just the app. The release key is a keystore file kept out of the repository:
set `MARCONI_ANDROID_KEYSTORE` to its path and `MARCONI_ANDROID_KEYSTORE_PASSWORD` to its password, and, if they
differ from the defaults, `MARCONI_ANDROID_KEY_ALIAS` (`marconi`) and `MARCONI_ANDROID_KEY_PASSWORD` (the
keystore's). For a test build signed with the debug key, run `./gradlew assembleDebug` in `Apps/Android`. An app
signed with a different key than the one installed has to be uninstalled first.

Before publishing, check the app is signed with the release key, not the debug one:
`apksigner verify --print-certs Build/Installer/Marconi-<version>-android.apk` (in the SDK's `build-tools`) must
show the SHA-256 fingerprint in [Checking the Android app is ours](#checking-the-android-app-is-ours). Put that
fingerprint in the release's notes on GitHub too. Keep the keystore and its passwords backed up: updates install
over an earlier version only when they're signed with the same key.

## Releasing (macOS)

    ./Package.sh                      # -> Build/Installer/Marconi-0.2.0.48-macos-arm64.dmg

It builds the Release app, wraps it in `Marconi.app` with the icon, Nebula Sans and `Installer/macOS/Info.plist`,
signs it, and makes a disk image to drag it to Applications from. It takes `--clean`, `--skip-build` and
`--allow-dirty`, which work like the Windows options. The app is built for the Mac's own architecture, and it needs
that Mac's macOS version or newer.

The app is signed with the Developer ID Application certificate in the Mac's keychain when there is exactly one
(set `MARCONI_SIGN_IDENTITY` to choose between several, or to `-` for none). Without one it is signed ad-hoc, which
is enough on the Mac that built it; on another Mac, macOS blocks a downloaded copy the first time. Try to open it,
then click *Open Anyway* in *System Settings > Privacy & Security*. Copies shared by AirDrop or USB stick may be
blocked too. For a release, also set `MARCONI_NOTARY_PROFILE` to a profile from `xcrun notarytool
store-credentials`: `Package.sh` then signs with the hardened runtime, notarizes the disk image and staples the
ticket.

### Packaging on a build server

Finder lays out the disk image's window (the background with the arrow, and where the icons sit), because macOS
26 only shows a background picture that Finder set itself. So the Mac that runs `Package.sh` needs:

- **Someone logged in to its screen.** Finder doesn't run for a program started over SSH alone or as a background
  service. Run the build server's agent as a user who is logged in, for example with automatic login and the agent
  started when that user logs in, not as a service for the whole Mac.
- **Permission for the agent to control Finder.** The first time, macOS asks on the screen, and the answer is kept
  in *System Settings > Privacy & Security > Automation*. Allow it once while setting the Mac up: run `Package.sh`,
  or `osascript -e 'tell application "Finder" to get name of startup disk'`, from the agent and click *Allow*. A
  Mac managed by a device management service can be given the same permission by a profile instead (a privacy
  preferences profile allowing the agent to send Apple events to Finder).

`Package.sh` checks both before it builds, and says which one is missing. It hasn't been tried on hosted build
services such as GitHub's Macs.

## Releasing (Linux)

    ./Package.sh                      # -> Build/Installer/Marconi-0.2.0.48-linux-amd64.deb

It builds the Release app and makes a Debian package, which installs on Ubuntu, Debian and the distributions based
on them:

    sudo apt install ./Build/Installer/Marconi-0.2.0.48-linux-amd64.deb

The package puts Marconi in the applications menu, registers it for `vnc://` links, and installs the icon, Nebula
Sans and the licence. `sudo apt remove marconi` takes it out again. `Package.sh` takes `--clean`, `--skip-build`
and `--allow-dirty`, which work like the Windows options, and needs the `dpkg-dev` package. The package depends on
the system libraries the app links against, at the versions on the build machine, so build it on the oldest release
it should install on. Its `Maintainer` field is Sem Essessi's address unless `MARCONI_DEB_MAINTAINER` (`Name <email>`) says
otherwise. The package is not signed.

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

## Testing the Windows service by hand

`Tests/MarconiServerTest` covers the service's logic on any platform: which console session the helper should be in
and when to restart it (fake session list and launcher), the messages between the tray icon and the helper and the
administrator check for Set password (in-memory pipe), and the Lock message with lock on disconnect. What it cannot
cover is Windows itself, so the Windows-only files (`ServiceHost_Windows.cpp`, `DesktopSwitch_Windows.cpp`,
`ControlPipe_Windows.cpp`, `SystemLock_Windows.cpp` and the edits in `SystemScreen_Windows.cpp` and
`SystemInput_Windows.cpp`) were written without a Windows machine and have never been built or run. On a Windows
machine, build `Server\Project.json`, fix what the compiler says, then work through this list (an administrator
prompt for each `MarconiServer.exe` command; the viewer is Marconi on another computer):

1. **Helper by hand.** `psexec -s -i 1 MarconiServer.exe --session 1 --data-dir C:\Temp\ms` (session 1 is the console
   session; use the number Task Manager's Users tab shows). Connect: the desktop shows, typing and clicking work.
2. **Install.** `MarconiServer.exe --set-password` (type the password, then Enter) and `MarconiServer.exe
   --install-service`. "Marconi Server" is running in Services; Task Manager shows `MarconiServer.exe` as SYSTEM in
   the console session. Connect.
3. **Control pipe.** `MarconiServer.exe --control-status` lists the connected viewer; `--control-disconnect 0` ends
   it; `--control-set-password` from a normal prompt says it needs administrator permission, from an elevated one it
   works and the new password is needed at the next connect.
4. **Sign out.** With a viewer connected, sign out: the viewer reconnects (the helper restarts for the sign-in
   screen) and the sign-in screen is visible and usable (type the password and sign in through Marconi).
5. **Permission prompt (UAC).** Start something that asks for administrator permission: the dimmed prompt shows in
   the viewer and Yes/No can be clicked. Also try it with the keyboard.
6. **Lock screen.** Press Win+L at the computer, and from the viewer: the lock screen shows and the password can be
   typed through Marconi.
7. **Lock on disconnect.** With the Marconi viewer's "lock the other computer when I disconnect" on, disconnect: the
   other computer locks. Then put `lock-on-disconnect=yes` in `C:\ProgramData\Marconi Server\server.conf`, restart
   the service, connect from a viewer with the option off, close the window: it locks too. A viewer being ended from
   the tray icon or `--control-disconnect` must not lock it.
8. **Ctrl+Alt+Del.** From the viewer send Ctrl+Alt+Del (once at the sign-in screen, once signed in): the Windows
   security screen appears. If it does not, check the registry value `SoftwareSASGeneration` under
   `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System` (the installer sets 1) and report what the
   Event Viewer says.
9. **Switch user.** Use Start, then the user's picture, then another account: within a second or two the helper moves
   to the new console session and the viewer reconnects to it. Switch back.
10. **Remote Desktop.** Connect to the machine with Remote Desktop, then disconnect: whatever takes the console
    session, Marconi follows it.
11. **Restart.** Restart (not Shut down, which keeps part of the system with Fast Startup) with nobody signing in: from
    the viewer, Marconi reconnects by itself to the sign-in screen. Note how long after the restart.
12. **Crash and stop.** End the `MarconiServer.exe` helper in Task Manager: it is back in about a second; end it a
    few times quickly: the waits grow to 30 seconds. Stop the service: the helper goes and the port closes.
13. **Remove.** `MarconiServer.exe --remove-service` stops and removes it and the policy value.

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
