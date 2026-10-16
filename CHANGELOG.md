# Changelog

What changed in each version of Marconi, newest first. Versions are `MAJOR.MINOR.PATCH` from `Project.json`; the
build adds the commit count as a fourth number (see [README.md](README.md#versions)).

## Unreleased

- Added this changelog and documented when to bump the version.
- Added a 96 px Windows icon size.
- Branded the Windows installer.
- Added a macOS disk image (`Package.sh`).
- Rewrote the README in plain language, with the developer notes at the end.

## 0.2.0

- Chooses the protocol automatically: probes VNC, Remote Desktop and SSH, and remembers what it found.
- Says when a machine only offers Remote Desktop or SSH, which aren't supported yet.
- Added `vnc://`, `rdp://` and `ssh://` addresses; `host::port` now means whatever answers on that port.
- Added "On this network", listing machines that announce themselves on the local network.
- Connects faster to machines with several addresses, when one of them doesn't answer.
- Doesn't auto-reconnect while you're typing.
- The connections window fits short and scaled screens.
- Added hidden diagnostic switches, including `--discover`.

## 0.1.0

- First version: VNC client with Raw, CopyRect, RRE, Hextile, ZRLE and Tight encodings.
- VNC password and Mac account login.
- Scaling, full screen, view-only and the F8 menu.
- Keyboard, mouse and two-way text clipboard.
- Saved connections with keychain passwords.
- Workarounds for macOS Screen Sharing's display-mode changes.
- Headless mode for testing.
- Windows installer, branding, and light and dark appearance.
