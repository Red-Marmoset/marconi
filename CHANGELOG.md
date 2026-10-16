# Changelog

What changed in each version of Marconi, newest first. Versions are `MAJOR.MINOR.PATCH` from `Project.json`; the
build adds the commit count as a fourth number (see [README.md](README.md#versions)).

## Unreleased

- Documented when to bump the version, for people (README) and for coding agents (CLAUDE.md, AGENTS.md).
- Added this changelog.
- Added the 96 px size to the Windows icon file, to match the icon bundle used at runtime.
- The Windows installer is branded: a red welcome panel with the Marconi and Red Marmoset logos, a header logo, and
  welcome and finish pages for the uninstaller too.
- Added `Package.sh`, which makes a macOS disk image with `Marconi.app` and a Mac-style icon.

## 0.2.0

Marconi now works out how to reach a machine, so you don't have to choose a protocol.

- **Automatic protocol choice:**
  - Connecting to a plain `host` checks VNC (5900), Remote Desktop (3389) and SSH (22) at once, and uses the best
    one Marconi supports.
  - A saved connection remembers what it found, so the next connection starts straight away. If that stops working,
    Marconi looks again once.
- **Remote Desktop and SSH are recognised, though not supported yet:** when a machine only offers one of those,
  Marconi says so instead of reporting a failed connection.
- **New address forms:** `vnc://`, `rdp://` and `ssh://` force a protocol. `host::port` now means "whatever answers
  on that port"; `host:1` is still VNC display 1.
- **On this network:** the connections window lists machines that advertise Screen Sharing, Remote Desktop or SSH
  over mDNS (as Macs do). The list refreshes every 10 seconds, and a double-click connects.
- **Faster connecting:** when a host has several addresses, Marconi tries them in parallel (Happy Eyeballs), so an
  address that never answers, such as a firewalled IPv6 address, no longer stalls the connection.
- **Typing isn't interrupted:** automatic reconnects wait until you stop typing.
- **The connections window fits short and scaled screens.**
- **Diagnostics:** a hidden `--discover` switch logs the machines found on the network; hidden headless switches
  control the encodings asked for and the recovery behaviour.

## 0.1.0

The first version.

- **The VNC client:**
  - RFB 3.3, 3.7 and 3.8, with the Raw, CopyRect, RRE, Hextile, ZRLE and Tight (including JPEG) encodings;
  - the server's cursor, drawn locally;
  - following the remote desktop's size (DesktopSize and ExtendedDesktopSize).
- **Logging in:** no password, a VNC password, or a Mac account (Apple Remote Desktop, security type 30).
  `user@host` addresses log in with that account.
- **Viewing:**
  - scale to fit, or show at actual size;
  - full screen and view-only;
  - the F8 menu;
  - Ctrl+Alt+Del.
- **Input:** keyboard and mouse with layout-correct key mapping. Keys are released when the window loses focus.
- **Clipboard:** text is shared in both directions.
- **Saved connections:**
  - an address book, with passwords in the system keychain;
  - each connection remembers its view settings;
  - rmvnc's saved connections are copied on first start.
- **macOS Screen Sharing workarounds:**
  - recovering when a Mac stops sending pixels after changing display modes;
  - repairing mislabelled rectangles;
  - never showing partial or black pictures.
- **Headless mode** for testing and automation, saving snapshots to the log folder.
- **Windows installer, Nebula Sans branding, and light and dark appearance.**
