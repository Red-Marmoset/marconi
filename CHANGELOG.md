# Changelog

What changed in each version of Marconi, newest first. Versions are `MAJOR.MINOR.PATCH` from `Project.json`; the
build adds the commit count as a fourth number (see [README.md](README.md#versions)).

## Unreleased

## 0.5.2

- Clearer file browsing, smoother buttons and refreshed typography in light and dark mode.

## 0.5.1

- Fixed a black square appearing instead of the mouse pointer on Windows when the other computer hides its pointer.
- The status bar shows the round trip time to the other computer, and says when it has stopped answering.
- Saved connections can be put in groups (a name typed when editing one); the list keeps each group together, and
  searching for a group's name finds its connections.
- Linux: a desktop file for opening vnc:// links in Marconi, with the steps in the README.
- A zoomed-in picture scrolls with Ctrl+Shift and the mouse wheel, and can be dragged around when view only is on.
- Saved connections can connect through SSH (Remote Login on a Mac): the SSH server connects to its own VNC server,
  and the whole session is encrypted inside SSH, whatever the VNC server can do. Also `--ssh` on the command line.

## 0.5.0

- A picture quality setting per connection (best, balanced, low bandwidth), changeable while connected.
- A setting to lock the other computer's screen when you disconnect.
- Saved connections can hold the computer's hardware address, and Marconi can wake it up over the network.
- Favourites are listed first, and each saved connection can carry notes.
- Each saved connection remembers where its window was.
- vnc:// links open in Marconi (Windows and macOS).
- When the other computer has several displays, the menu can show just one of them; the choice is remembered.
- The menu can ask the other computer to change its screen size to fit this window, where its server allows it.
- In full screen, resting the pointer at the top edge shows a small bar with the controls; it can be pinned.
- A lost connection keeps the window open and reconnects by itself, with the old picture dimmed until it's back.
- A dead network link is noticed within about a minute instead of only when something is sent.

## 0.4.2

- Files copied in Explorer or Finder can be pasted onto the other computer's screen or into the file panel.
- Files copied in the file panel (Ctrl+C) can be pasted on this computer, or on the other side.

## 0.4.1

- Encrypted connections on Windows too.

## 0.4.0

- Screenshots of the other computer's screen: save as PNG or copy, with or without the mouse pointer.
- A Send Keys menu: Alt+Tab, Alt+F4, the Windows key, Ctrl+Esc, Ctrl+Shift+Esc, Print Screen and Cmd+Tab.
- On Windows and macOS, a setting sends system shortcuts such as Alt+Tab and Cmd+Tab to the other computer.
- Zoom levels from 50% to 200% besides fitting the window: Ctrl and the mouse wheel, Ctrl and + or -, or the menu.
- A zoomed-in picture scrolls in full screen when the pointer rests at the edge of the screen.
- Each saved connection remembers its zoom level.
- Encrypted connections on macOS and Linux, with servers that offer them with a certificate.
- Connects without encryption, and says so, when a server only offers encryption without a certificate.
- Copied text in any language travels between the computers with servers that support it (TigerVNC, UltraVNC).
- Copied images travel too, with servers that support it.
- The menu can send your clipboard to the other computer, or get its clipboard, with sharing on or off.
- Says when characters can't be sent to a server that only takes Western European text.

## 0.3.7

- The Windows installer asks you to accept the MIT license before installing.
- The Windows installer can open the source code on GitHub and adds a Start menu link to it.

## 0.3.6

- The Windows installer asks whether to install just for you (no administrator permission needed) or for everyone on the PC.

## 0.3.5

- Fixed a warning about old Windows controls when starting an installed copy of Marconi.

## 0.3.4

- The license page in the installer no longer breaks lines in odd places.

## 0.3.3

- The file panel is a proper file browser: folder tree, icons, a clickable path, Back and Forward, sortable columns.
- Opening folders no longer freezes the window.

## 0.3.2

- Cancelling or failing a "Replace" no longer loses the original file.
- Copying from TightVNC servers can no longer mix up two files.
- Copying folders with TightVNC servers is much faster.
- A computer whose security fingerprint changed can be trusted again.
- Closing a window during a copy no longer hangs.

## 0.3.1

- Fixed an error alert when the other computer hides its mouse pointer.
- Fixed copying new files to TightVNC servers.

## 0.3.0

- Copies files to and from Macs with Remote Login on: F10 opens the file panel.
- Files dropped on the remote screen are copied to the remote Desktop.
- Copies files with TightVNC servers too, over the same connection.
- Remote files can be dragged out to the desktop or a folder.
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
