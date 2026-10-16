# Changelog

What changed in each version of Marconi, newest first. Versions are `MAJOR.MINOR.PATCH` from `Project.json`; the
build adds the commit count as a fourth number (see [README.md](README.md#versions)).

## Unreleased

- Copies files to and from Macs with Remote Login on: F10 opens the file panel.
- Files dropped on the remote screen are copied to the remote Desktop.
- Copies files with TightVNC servers too, over the same connection.
- Shows whether the connection is encrypted.
- Says so when a server requires an encrypted connection.
- Settings can delete all saved data; so can the Windows uninstaller.
- Gives up, and says so, when a server never answers.
- Settings and logs are private to the user on macOS and Linux.
- The README explains what isn't private and how to remove saved data.
- Only shares copied text while the Marconi window is in front.
- No longer saves pictures of the other computer's screen during normal use.
- An incomplete picture is no longer reported as a closed connection.
- The toolbar can be moved to any edge or floated; F9 shows or hides it.
- Added buttons that send F8, F9 and F10.
- Saved connections remember where the toolbar was.
- The installer starts Marconi without administrator rights and can be signed.
- Branded the Windows installer.
- Added a macOS disk image.
- Added a 96 px Windows icon.
- Rewrote the README in plain language.
- Added this changelog.

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
