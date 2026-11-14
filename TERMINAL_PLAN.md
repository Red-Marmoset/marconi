# Marconi terminal: the plan

This document plans a terminal in Marconi: a real one (colours, cursor movement, full-screen programs such as `vim`
and `htop`) that reaches another computer over SSH or through Marconi Server, and the same reach for `MarconiMcp`,
so an AI tool can run commands and edit files on that computer as text instead of through screenshots.

The owner's brief, as given (2026-11-14):

- remote terminal access, "very helpful for the mcpserver especially": much cheaper for an AI tool to write code, use
  git and run tests than driving the screen, and generally useful;
- the Mac already allows it (`ssh semessessi@mac.lan`); setting things up there is acceptable but not ideal;
- Marconi Server should offer it too, alongside the screen, "maybe as a tab that can be opened";
- a full terminal: ANSI colour codes "and that kind of stuff";
- use discovery to work out how to reach a computer's SSH;
- how Marconi Server provides a terminal is open, but no shell of our own: not "our own bash, cmd or powershell";
- everything checked in and pushed when the work is done, engine changes included: no dangling changes, no dead or
  forgotten branches.

How to use this document (as `SERVER_PLAN.md` and `MCP_PLAN.md`):

- Work through the phases in order unless the owner reorders them. Each step is about one commit and each phase has a
  "done when" check that can be run by hand. Every phase leaves Marconi releasable.
- The rules in `AGENTS.md` apply throughout: no assumptions (verify or ask the owner), the version and changelog
  rules, plain words in anything users read.
- Keep this file current: decisions, findings, finished steps. Anything undecided is in section 8; nothing that
  depends on an undecided point is built until it's decided.
- **Finishing rule (the owner's, for this work in particular).** All of it happens on `main` in both repositories.
  - A step isn't done until it is committed and pushed: Marconi to `origin/main`, engine changes to the engine's
    `origin/main`, and the engine first, so Marconi's `main` never needs an engine commit that isn't pushed.
  - No feature branches. If one can't be avoided, it is merged and deleted, here and on the remote, before the
    session that made it ends.
  - Every session ends with `git status` clean and nothing ahead of `origin/main` in both repositories (apart from
    the owner's own work in progress in the engine, which is left alone and never staged). If that can't be done,
    the session says exactly what is left and where.

## Progress

- **Phase 0** started 2026-11-14: the owner's decisions (section 1) and the first checks (section 7) are in. Still
  to do: the checks listed under Phase 0 in section 5, and the owner's answers to section 8.
- **When it starts is the owner's call.** `ROADMAP.md`'s "Toward 1.0" list, drawn up the same day, puts the built-in
  terminal after 1.0. This plan doesn't change that list (section 8, question 6). One part touches 1.0 either way:
  what `ssh://` addresses mean (question 2), since that list fixes addresses before 1.0.

## 1. Decisions

Owner, 2026-11-14:

1. **Two ways to reach a terminal:** SSH (any computer with an SSH server; the owner's Mac today) and Marconi Server.
2. **Marconi Server starts the system's own shell.** It doesn't interpret commands; it runs the computer's shell in a
   pseudo-terminal and carries the bytes.
3. **In the app the terminal is a tab in the viewer window:** a tab strip with the screen first, then one tab per
   terminal, and a `+` that opens another. A computer that only has SSH gets the same window with no screen tab.
4. **AI tools get commands first, a live terminal later.** First: a tool that runs one command and returns its output
   and exit code as text, plus reading and writing files. A later phase adds a terminal the AI types into and reads,
   for programs that wait for typed answers.
5. **Windows Marconi Server, which account the shell runs as** (the owner's words: "always prompt, name filled in, but
   we should open a default terminal as the logged in user too, something which can then always be used at least"):
   - a **default terminal** that runs as the person signed in at that PC and needs no Windows password, so there is
     always one that works;
   - a **login** that asks for a Windows name and password, with the name box already holding the signed-in person's
     name, for a terminal as a named account.
   - The planner's reading, to be confirmed (section 8, question 1): the first terminal tab is the default terminal,
     and the login is what `+` offers beside "another default terminal", and what is shown when nobody is signed in.

The planner's calls (the owner can overrule any of them):

6. **SSH first, Marconi Server second.** SSH works with the owner's Mac now and with any SSH server; the server part
   needs new protocol messages and three platforms' pseudo-terminals.
7. **AI commands before the terminal window.** Running a command over SSH needs only the engine's SSH additions, not
   the terminal emulator, and it is what the owner asked for "especially". So it comes straight after Phase 1.
8. **The emulator (what turns the shell's bytes into a screen of coloured cells) is the libvterm library** (MIT
   licence; the one Neovim's terminal was built on), added to the engine as an external library, with our own drawing
   and keyboard code on top. Writing an emulator ourselves is the fallback if Phase 0 finds libvterm can't be built
   through the engine on every platform; it sits behind one interface (`TerminalScreen`) either way.
9. **Off until switched on, at the server.** A terminal through Marconi Server is refused unless the person at that
   computer has turned it on (tray menu, or `--terminal`), exactly as file sharing is. See "Security" in section 3.
10. **Phones and tablets are not in the first version** (section 9).
11. **The pseudo-terminal code lives in Marconi** (`MarconiServerCore`), not the engine, to keep engine changes to
    the two that are needed: the SSH additions and the libvterm external.

## 2. What exists and what it gives us

Checked 2026-11-14 against Marconi `main` at 0.11.10 (cf39933) and the engine's `main` at ddae5c9. Both moved the
same day (Marconi to 0.11.11 at 9e33c39, the engine to c10f568) with packaging and roadmap changes only; nothing
below changed.

- **Discovery** (`MarconiCore/Net/Discovery.*`) already asks the network for `_ssh._tcp` beside `_marconi._tcp`,
  `_rfb._tcp` and `_rdp._tcp`, and gives each service's name, host name, IPv4 address and port.
- **SSH in the engine** (`Crypto/Ssh.h`, libssh2 1.11.1; WinCNG on Windows, mbedTLS elsewhere): handshake, host key
  and fingerprint, login by password ("password", then "keyboard-interactive", which macOS uses), SFTP, and a
  forwarded TCP connection (`OpenTunnel`, a "direct-tcpip" channel). `SshChannel` already has a thread of its own that
  moves bytes, so one thread can read while another writes. **Missing:** a shell with a pseudo-terminal, running one
  command, changing the terminal's size, the command's exit code, its error output, and login with a key.
- **Marconi's uses of it:** `Net/SshTunnel` (VNC through SSH), `Files/SftpFiles` (the file panel against a Mac). Both
  log in with the connection's login name and password and share one trusted host key per saved connection
  (`sshHostKey`), asking the person the first time.
- **`ssh://` addresses** parse to `Protocol::Ssh`, port 22 (`Address.cpp`). What they do today differs:
  - the desktop viewer probes, and when SSH is all it finds says "only offers SSH (a text terminal), which Marconi
    doesn't support yet" (`Net/ProtocolChoice.cpp`);
  - `QuickConnect` (MarconiMcp and the phone apps) treats `ssh://host` as "VNC through that computer's SSH server"
    (`QuickConnect.cpp`). Section 8, question 2.
- **The Marconi protocol** (`Wire/WireMessages.h`): one type byte per message, unknown types skipped by length,
  capabilities agreed once as bits. Bits 0 to 7 and types up to 109 are taken; bit 8 and types from 110 are free.
- **Marconi Server** (`MarconiServerCore`): `Session` hands file messages to `FileService`, which works on its own
  thread and is refused unless `Settings::filesEnabled` is on. The terminal service follows that shape.
  - On Windows the session helper runs as SYSTEM in the console session (`SessionHost.h`, `SERVER_PLAN.md` "Running
    as a Windows service"). On Linux it is a systemd user service and on macOS a LaunchAgent, so there it already
    runs as the signed-in person.
  - It takes one viewer at a time (`Listener::ServeOne`; `SERVER_PLAN.md` Phase 6 step 7 plans more).
  - `SERVER_PLAN.md` "Security" says "The service never runs code the client sends". A terminal changes that.
- **The viewer window** (`MarconiUI/ViewerFrame.*`) docks its toolbar and the file panel with `wxAuiManager`; it has
  no tabs today.
- **MarconiMcp** (`Modules/MarconiMcp`): one `Connection` at a time, which is a `RemoteSession` (a screen). Tools are
  listed in `Tools.cpp`, `ToolsInput.cpp` and `ToolsNetwork.cpp`, each marked `readOnly` or not for `--read-only`.
  Nobody is there to answer questions, so an untrusted SSH host key is turned down and `connect` is called again
  with `trust_fingerprint`.
- **Tests:** `Tests/MarconiCoreTest` doesn't include `MarconiServerCore`; the server's services are tested in
  `Tests/MarconiServerTest` (`FileTests.cpp`). Each family of protocol messages has its own test file with a
  round-trip check and a random-input pass (`WireFileTests.cpp`, `WireGamepadTests.cpp`); there is no shared one.
  `CryptoTest`'s `SshTests` talk to a real SSH server only when `CRYPTOTEST_SSH_HOST` or `CRYPTOTEST_SSH_LOGIN`
  (`user:password@IPv4:port`) is set.
- **The server's idle limit** counts only messages from the viewer as activity (`Session.cpp`), and its lock-out
  (`Auth`) only knows the server's own access password.
- **Sending** from the server is one blocking `Session::Send` under a lock; there is no queue of things waiting to
  go out.
- **The engine's `Process` module** runs a local program with a time limit and collects its output. It has no
  pseudo-terminal.

## 3. Design

### 3.1 The pieces

```
engine  Modules/Crypto            SSH: shell, one command, resize, exit code, error output (Phase 1); keys (Phase 8)
engine  External/libvterm         the emulator library (Phase 3)
Modules/MarconiCore/Terminal/     TerminalLink (bytes to and from a remote shell) and CommandRunner (one command):
                                  SshTerminal, SshCommands over the engine; WireTerminal, WireCommands over the
                                  Marconi protocol. No wx.
Modules/MarconiTerminal/          TerminalScreen (libvterm inside): cells, colours, cursor, scrollback, the text on
                                  screen, and turning keys and mouse into the bytes a shell expects. No wx.
Modules/MarconiUI/                TerminalView (draws a TerminalScreen, takes the keyboard), the tab strip
Modules/MarconiCore/Wire/         WireTerminalMessages.h: the new messages
Modules/MarconiServerCore/        Pty (interface), Pty_Windows.cpp (ConPTY), Pty_Posix.cpp (forkpty), a fake for
                                  tests; TerminalService and CommandService (as FileService)
Modules/MarconiMcp/               run_command, read_file, write_file; later the terminal_* tools
```

`TerminalLink` is the one interface the window and MarconiMcp use, whatever carries it:

- `Open(columns, rows)`, `Write(bytes)`, `Resize(columns, rows)`, `Close()`;
- a listener, called on the link's own thread: `OnOutput(bytes)`, `OnClosed(exit code or reason)`.

`CommandRunner` runs one command with no terminal: the command, an optional folder to run it in, a time limit and
a cap on collected output go in; the normal output, the error output, the exit code, and whether it was cut short
or timed out come back.

### 3.2 Finding and logging in to SSH

- **The address comes from discovery or the saved connection.** Marconi makes the SSH connection itself, so nobody
  types an `ssh` command. The network's announcement gives the name, address and port but not the account (checked,
  section 7 finding 1), so the login name is the saved connection's, or asked once and saved with it.
- **Login** is the connection's login name and remembered password, as the SSH tunnel and Mac file transfer do now,
  with the same trusted host key and the same first-time question.
- **Computers found on the network that offer SSH** become openable in the connections window (today they are shown
  but can't be opened), and `ssh://host` in the address box opens a terminal window.
- **A computer that has a screen and SSH** (the Mac): the `+` in its viewer window opens a terminal over SSH to the
  same host, port 22, the way the file panel already reaches the Mac's files.
- The tab's menu has **Copy SSH Command** (`ssh name@host`, with `-p` when the port isn't 22), for use elsewhere.
- **Keys** come later (Phase 8). The owner's key on this PC is `id_ed25519`, which this build of libssh2 can't use
  (finding 3).
- **Terminals and commands use an SSH connection of their own**, apart from the file panel's and the tunnel's: the
  engine's SFTP calls can't be used while a channel is open on the same login, and `SftpFiles` keeps its session to
  itself.
- **MarconiMcp** can't ask a person, so it trusts an SSH host key the way it does now (`trust_fingerprint`, or the
  saved connection's trusted key) and takes the password from the saved connection, the `password` argument or
  `MARCONI_PASSWORD`.
- A saved connection holds one login name and one password. Where a computer's SSH login differs from its screen
  login, see section 8, question 7.

### 3.3 The engine's SSH additions (Phase 1)

In `Crypto/Ssh.h`, on `SshSession`:

- `OpenShell(terminalName, columns, rows, error)`: a channel with a pseudo-terminal and the account's login shell;
- `OpenCommand(command, error)`: a channel that runs one command with no terminal ("exec").

On `SshChannel`, for these two kinds:

- `Resize(columns, rows)`;
- the error output, kept apart from the normal output for commands (a terminal mixes them, as a real one does);
- `GetExitCode()` once the far side has closed.

Both go through the channel's existing thread, so reading on one thread while writing on another keeps working.
More than one channel on a session at a time (two terminal tabs on one login) needs that thread to serve several
channels; until it does, each tab makes its own SSH connection. Phase 1 decides which after trying it.

Tests: `CryptoTest`'s `SshTests`, which reach a real SSH server only when `CRYPTOTEST_SSH_LOGIN` is set
(`user:password@IPv4:port`, so the Mac by its address, not `mac.lan`). The owner sets that variable; the password is
never written into a file or a command the plan records.

### 3.4 The emulator: `TerminalScreen`

- Bytes from the shell go in (`Feed`); out come a grid of cells (character, colours, bold, italic, underline,
  inverse), the cursor, the title a program set, the bell, and which rows changed.
- Colours: the 16 standard ones, the 256-colour table and 24-bit colour. The shell is told `TERM=xterm-256color`.
- The alternate screen (what `vim` and `htop` switch to), scroll regions, wide characters (Chinese, emoji), and
  resizing.
- Scrollback: lines that scroll off the top are kept (10,000 by default).
- Keys and mouse go the other way: `EncodeKey`, `EncodeMouse`, and pasted text wrapped the way shells expect when
  they ask for it ("bracketed paste").
- `GetText()` returns the screen as plain text (and optionally the scrollback), for selection, for tests, and later
  for the AI's live terminal.
- Tests feed escape sequences and compare the resulting cells and text. A few sessions recorded as raw bytes through
  Phase 1's shell (`vim`, `top`, a coloured `ls`) are replayed too; their expected screens are read over once by a
  person and then guard against later changes.

### 3.5 The window: tabs and `TerminalView`

```
+----------------------------------------------+
| [Screen] [Terminal 1] [Terminal 2] [+]       |
+----------------------------------------------+
| sem@Ford ~ % git status                      |
| On branch main                               |
| nothing to commit, working tree clean        |
| sem@Ford ~ % _                               |
+----------------------------------------------+
| Ford  encrypted, SSH                 80x24   |
+----------------------------------------------+
```

- The tab strip appears once a terminal is open or the computer has no screen; a window showing only a screen looks
  as it does today, with **Open Terminal** on the toolbar and in the F8 menu. Shown to the owner before it is
  settled (Phase 4 step 3).
- A tab's name is the title the shell sets, else "Terminal 1", "Terminal 2". Closing the tab ends that shell; when
  the shell exits the tab says so and stays until closed, so its last output can be read.
- `TerminalView` draws the grid in the system's fixed-width font (Cascadia Mono or Consolas, Menlo, DejaVu Sans
  Mono), with a text size setting; it follows Marconi's light and dark appearance, and programs' own colours are
  shown as they ask.
- Resizing the window tells the shell the new size in columns and rows; the status bar shows it.
- Selecting with the mouse copies on Ctrl+Shift+C (Cmd+C on a Mac); pasting is Ctrl+Shift+V (Cmd+V) or the right
  button. When the program on the other side asks for the mouse (`htop`, `vim`), clicks go to it and Shift+drag
  selects.
- Every other key goes to the shell while a terminal tab is in front, F8 included (programs use the function
  keys); the viewer's menu is then reached from the toolbar.
- Scrolling: the wheel and a scroll bar move through the scrollback; typing jumps back to the bottom.
- The screen tab keeps running while a terminal is in front (its picture just isn't drawn).
- Saved with the connection: nothing new except the SSH login name when it had to be asked.

### 3.6 The Marconi protocol: terminal and commands

One new capability bit, `Terminal` (bit 8), offered by the viewer and granted by the server only when terminals are
switched on there. Messages, in `Wire/WireTerminalMessages.h` (numbers 110 to 125 are theirs):

| Message | Direction | Carries |
|---|---|---|
| `TerminalOpen` | viewer to server | an id the viewer picks, columns, rows, and the account: the default one, or a name and password |
| `TerminalOpened` | server to viewer | the id, ok or why not, the shell's name, the account it runs as |
| `TerminalData` | both | the id and bytes: keys one way, the shell's output the other |
| `TerminalResize` | viewer to server | the id, columns, rows |
| `TerminalClose` | viewer to server | the id: end that shell |
| `TerminalExit` | server to viewer | the id and the shell's exit code |
| `TerminalAccounts` | server to viewer | who is signed in (for filling in the name box), and whether a default terminal is possible now |
| `CommandRun` | viewer to server | an id, the command, the folder, the time limit, the output cap, the account as above |
| `CommandOutput` | server to viewer | the id, which output (normal or error), bytes |
| `CommandExit` | server to viewer | the id, the exit code, whether it timed out or was cut short |
| `CommandCancel` | viewer to server | the id: stop it |

- Output is sent in pieces of at most 16 KB, so a flood of text (`cat` of a huge file) takes turns with the picture
  rather than holding it up. The service's thread reads from the shell only as fast as it can send (each send
  blocks until it has gone), which slows the program down instead of filling memory.
- Terminal output and a command that is still running count as activity for the server's idle limit, so a long
  quiet build doesn't get its viewer disconnected.
- `Welcome` gains a trailing field saying what kind of system the server is (Windows, macOS, Linux), for the AI
  tools' `status`.
- A password in `TerminalOpen` or `CommandRun` travels inside the connection's encryption, is handed to the system
  to check, and is never logged or kept.
- The messages get their own test file in `MarconiCoreTest` (`WireTerminalTests.cpp`) with a round-trip check and a
  random-input pass, as the file and controller messages have.

### 3.7 Marconi Server: starting the shell

`TerminalService` and `CommandService` sit beside `FileService` in `Session`, each on its own thread. They use `Pty`:

- **macOS and Linux (`Pty_Posix.cpp`):** `forkpty`, then the account's login shell (from the password database) as a
  login shell, in its home folder, with `TERM=xterm-256color`. The server already runs as the signed-in person
  there, so that is who the shell runs as. A login as another account isn't offered on these two (it would need the
  server to run as root).
- **Windows (`Pty_Windows.cpp`):** ConPTY (`CreatePseudoConsole`, Windows 10 version 1809 and later; on older Windows
  the terminal is refused with a sentence saying why). The shell is PowerShell 7 when installed, else Windows
  PowerShell; a server setting can name another (`cmd.exe`, Git Bash). Typing `cmd` or `bash` inside it works as
  usual.
  - **The default terminal** runs as the person signed in at the console: the helper (SYSTEM) takes that session's
    user token (`WTSQueryUserToken`), builds their environment and starts the shell with it in their home folder.
    Not elevated: it has the rights that person's ordinary programs have. Nobody signed in: no default terminal.
  - **A login** checks the Windows name and password (`LogonUser`), loads that account's profile and starts the
    shell as it, also not elevated. Wrong passwords are logged and slow down further tries on that connection (a
    growing wait, then the connection is closed). They are counted apart from the server's own lock-out, which
    would otherwise shut every viewer out; Windows' own account lock-out policy still applies.
- Commands (`CommandService`) run through the same account choice with no pseudo-terminal: `sh -c` in the account's
  shell on macOS and Linux, the configured shell on Windows, with the two outputs on separate pipes, and are ended
  when their time is up or the viewer goes.
- When the viewer disconnects, its shells and commands are ended.

### 3.8 Security

- A terminal is the whole computer for whoever reaches it, more than the screen is (it needs no unlocked desktop
  and leaves nothing on screen). So on Marconi Server:
  - off by default; turned on at that computer (tray menu "Allow a terminal", or `--terminal`), not from a viewer;
  - the "someone connected" message says when the viewer has opened a terminal;
  - the log records each terminal and command opened and closed, with the account and the viewer's address, never
    what was typed or shown.
- `SERVER_PLAN.md` "Security" is changed in the same commit that adds the service, to say the server runs commands
  only through this one switched-on path, as an account chosen above, and still never runs code otherwise.
- SSH terminals rely on the SSH server's own login, as the tunnel and file transfer do.
- AI tools: section 3.9.

### 3.9 AI tools (MarconiMcp)

New tools, on the one connection MarconiMcp has:

| Tool | Arguments | What it does |
|---|---|---|
| `run_command` | `command`, optional `folder`, `timeout_ms` (default 120000), `max_output` | Runs it; returns the output, the error output, the exit code, and whether it timed out or was cut short |
| `read_file` | `path`, optional `offset`, `length` | The file's text (or a part of it); says so if it isn't text |
| `write_file` | `path`, `content`, optional `append` | Writes or adds to the file |

- **Where they go.** A Marconi Server connection carries them itself (section 3.6). A VNC connection uses SSH to the
  same computer with the saved connection's login, as the file panel does. Files use what Marconi already has
  (`SftpFiles`, `MarconiFiles`).
- **No screen needed.** `connect` to a computer that only has SSH gives a connection with commands and files but no
  screen; `screenshot` and the mouse and key tools then answer "this computer has no screen here". `status` and
  `connect`'s answer say what the connection can do (screen, commands, files) and what kind of system it is (asked
  with `uname` over SSH, falling back to `ver` for Windows; from `Welcome` on Marconi Server), so the AI knows
  whether to write `ls` or `dir`.
- `read_file` returns at most about 100,000 characters a call and refuses a file that isn't text (it has a zero
  byte or isn't valid UTF-8) with a sentence saying so.
- **Each command starts fresh:** no folder or variable carries from one `run_command` to the next (over SSH each is
  its own "exec" channel), which is why `folder` is an argument. The tool's description says so.
- **Long output** is cut to `max_output` (default about 30,000 characters), keeping the start and the end and saying
  how much was dropped, so one command can't cost a fortune.
- `--read-only` offers `read_file` but not `run_command` or `write_file`.
- Settings > AI tools gets "Let AI tools run commands and change files" (section 8, question 3, for its default),
  and a saved connection's "Hide from AI tools" covers these tools too.
- MarconiMcp's log records each command's first line, exit code and time taken (the roadmap's "a log of everything
  the AI did" starts here).

Later (Phase 7), the live terminal: `terminal_open`, `terminal_send` (text, or named keys as the `key` tool takes),
`terminal_read` (the screen as text, optionally after waiting for it to go quiet), `terminal_close`. It uses
`TerminalScreen::GetText`, so the AI reads text, not a picture.

## 4. What users see change, phase by phase

| Phase | Reaches users as |
|---|---|
| 2 | AI tools can run commands and read and write files on a computer with SSH |
| 4 | Open a terminal to any computer with SSH, in a tab beside its screen or on its own |
| 5, 6 | The same through Marconi Server (Linux and macOS, then Windows) |
| 7 | AI tools can use a live terminal |

## 5. Phases

### Phase 0: agree the plan, check the unknowns

1. The owner answers section 8.
2. Checks, each recorded in section 7:
   - libvterm: where its source is kept, whether the engine can take it as an external library, and that it builds
     with MSVC, clang and gcc (it generates two small tables with a script when built its own way);
   - against the Mac from this PC: a shell and a single command through libssh2 with password login (a few lines in
     `CryptoTest`), and what size, colours and exit code come back;
   - what Windows' own OpenSSH server does with a single command (which shell runs it, how quoting behaves);
   - ConPTY from a SYSTEM process with a user's token (a small test program run from the installed service).
- Done when: section 8 is empty and section 7 has an answer for each check, or a changed plan where a check failed.
- No version bump.

### Phase 1: SSH shells and commands (engine)

1. `OpenShell`, `OpenCommand`, `Resize`, the error output and `GetExitCode` (section 3.3), with `SshTests`.
2. Committed and pushed to the engine's `main` (only these paths staged).
- Done when: `CryptoTest` passes on Windows, and with `CRYPTOTEST_SSH_LOGIN` pointing at the Mac a shell returns
  the colour codes `printf '\033[31mred\033[0m\n'` asks for, a resize is seen by `stty size`, and `echo hello; exit 3`
  as a command returns "hello" and 3.
- No Marconi version bump (nothing reaches users yet).

### Phase 2: AI tools run commands over SSH

1. `MarconiCore/Terminal`: `CommandRunner`, `SshCommands` (login, host key trust and cancelling as `SftpFiles`).
   Tests with a stand-in runner; by hand against the Mac.
2. MarconiMcp: a connection that may have no screen; `connect` to SSH-only computers and `ssh://` addresses as
   section 8 question 2 decides; `status` says what the connection can do.
3. `run_command`, `read_file`, `write_file`, the output cap, `--read-only`, the setting, the log lines. Tests on the
   exact requests each tool makes and on cutting long output.
- Done when: Claude Code, through MarconiMcp alone and with no screenshot, clones a small repository on the Mac,
  changes a file, runs its tests and reports the result; and the same tools are refused with `--read-only`.
- MINOR bump, changelog line, README section.

### Phase 3: the emulator

1. Engine: `External/libvterm` and its manifest (or, if Phase 0 ruled it out, the start of our own). Pushed to the
   engine's `main`.
2. `Modules/MarconiTerminal`: `TerminalScreen` (section 3.4) with its tests.
3. Keys, mouse and paste encoding, with tests on the exact bytes.
- Done when: `MarconiCoreTest` passes the screen tests on Windows, including the replayed `vim` and `top`
  sessions.
- No version bump (nothing reaches users yet).

### Phase 4: the terminal in the app, over SSH

1. `TerminalLink`, `SshTerminal`.
2. `TerminalView`: drawing, the keyboard, resizing, scrollback, selection, copy and paste.
3. The tab strip in the viewer window, **Open Terminal** on the toolbar and F8 menu; shown to the owner (a
   screenshot) before the details are settled.
4. Windows with no screen: SSH-only computers from the connections list, from the network list and from `ssh://`;
   the probe's "only offers SSH" message goes. `ViewerFrame` assumes a screen connection throughout, so this is a
   lighter window of its own that shares the tab strip and `TerminalView`.
5. The text size setting, Copy SSH Command, the status bar.
- Done when: the owner opens a terminal to the Mac from its viewer window and from the network list, runs `vim`
  and `top` (`htop` if it's installed there), sees `ls -G` and `git diff` in colour, resizes the window, copies and
  pastes, and opens a second tab. Before handing over, the builder checks the same through the headless viewer's
  snapshots where it can and says what it couldn't check.
- MINOR bump, changelog line, README section; `ROADMAP.md` updated.

### Phase 5: Marconi Server terminal on Linux and macOS

1. The messages and the capability (section 3.6), with round-trip tests.
2. `Pty` with the fake, and `Pty_Posix.cpp`; `TerminalService`, `CommandService`, the setting, the tray item (not
   shown on Windows until Phase 6), the log lines; `SERVER_PLAN.md` "Security" changed. The services' tests go in
   `Tests/MarconiServerTest`, against the fake `Pty`.
3. `WireTerminal` and `WireCommands` in the viewer (beside the file messages in `WireClient.cpp`) and MarconiMcp, so
   the same tab and the same tools work through a Marconi Server connection.
- Done when: on a Marconi Server with the terminal switched on, the viewer's `+` opens a shell and `run_command`
  works through MarconiMcp; with it switched off both are refused with a plain sentence. Which computer that is
  depends on section 8, question 8: the owner's Mac (its server compiles there, bdaf4b8, but hasn't been run yet,
  and running it is on the 1.0 list) or a Linux machine.
- MINOR bump, changelog line.

### Phase 6: Marconi Server terminal on Windows

1. `Pty_Windows.cpp` (ConPTY), run as the server's own account first (the tray program in Phases 1 and 2 style).
2. The default terminal as the signed-in person, from the service.
3. The login (name and password), `TerminalAccounts`, the prompt in the viewer with the name filled in.
- Done when: against the service on a Windows PC, the default terminal opens as the signed-in person
  (`whoami` says so) with no password asked; a login as another account opens as that account; a wrong password is
  refused and counted; with nobody signed in only the login is offered (checked from a viewer on another computer,
  since the PC under test is at its sign-in screen). Needs what section 8, question 8 lists.
- PATCH bump, changelog line.

### Phase 7: a live terminal for AI tools

- `terminal_open`, `terminal_send`, `terminal_read`, `terminal_close` (section 3.9), over SSH and Marconi Server.
- Done when: Claude Code answers a program's typed questions (for example `git add -p`) through them.
- PATCH bump, changelog line.

### Phase 8: logging in with a key

- Needs a decision first (finding 3): which kinds of key, and where the key and its passphrase are kept. Its steps
  and "done when" are written once that's decided.
- PATCH bump, changelog line.

## 6. Testing throughout

- `MarconiCoreTest` for everything that needs no other computer and no server code: the messages, `TerminalScreen`,
  key and mouse encoding, the tools' requests, cutting long output.
- `MarconiServerTest` for `TerminalService` and `CommandService` against the fake `Pty`.
- `CryptoTest` for the SSH additions (against the Mac when `CRYPTOTEST_SSH_LOGIN` is set).
- By hand at the end of each phase, against the Mac (SSH) and a Marconi Server, recorded under Progress with what
  was and wasn't run, as the other plans do.
- Each phase's last step: both repositories clean and pushed (the finishing rule above).

## 7. Findings (checked 2026-11-14)

1. **Discovery finds the owner's Mac's SSH.** MarconiMcp's `list_connections`, run on this PC, listed "Ford" at
   192.168.1.165 twice: VNC, and SSH marked not usable. `mac.lan` resolves to the same address and port 22 answers.
   The announcement carries the name, host and port only; the login name isn't in what Marconi reads from it.
2. **The saved connection for the Mac** ("Sem Essessi", `Mac.lan`) has a password in the keychain and the login name
   "Sem Essessi"; the Mac's own hint uses the short name `semessessi`. File transfer over SSH already works with
   what is saved. Phase 0's test against the Mac confirms which names its SSH server takes.
3. **This build of libssh2 can't use ed25519 keys**, on any platform: `LIBSSH2_ED25519` is 0 in both `wincng.h` and
   `mbedtls.h` (1.11.1). The owner's key on this PC is `~/.ssh/id_ed25519`. RSA and ECDSA keys are supported by
   both. So key login (Phase 8) means either an RSA or ECDSA key, or a change to how the engine builds libssh2.
4. **Login is by password only today** (`SshSession::Login`); libssh2's key functions are compiled in but unused.
5. **`ssh://` already means something to MarconiMcp and the phone apps:** VNC through that computer's SSH server
   (`QuickConnect.cpp`). Section 8, question 2.
6. **The engine's `main`** has the MCP work merged (ddae5c9) and one path of the owner's in progress
   (`Modules/BuildTool`, untracked), which this work never stages.

Not yet checked (Phase 0): everything about libvterm beyond its licence and purpose; Windows OpenSSH's handling of
single commands; ConPTY with another account's token from the service; whether two channels can share one libssh2
session through `SshChannel`'s thread.

## 8. Decisions needed from the owner

1. **The Windows login, exactly** (decision 5). Is this right: the first terminal tab opens as the signed-in person
   with nothing asked; `+` offers "as the signed-in person" or "log in", and "log in" shows the name (filled in)
   and password boxes; when nobody is signed in, only "log in" is offered?
2. **`ssh://host` for AI tools and the phone apps.** Today it means "the screen, through SSH". Proposed, so nothing
   that works now stops working: in MarconiMcp, `ssh://host` logs in over SSH for commands and files and also tries
   the screen through it as now; if there's no screen it is a commands-only connection. The phone apps keep today's
   meaning until they get a terminal. The desktop viewer, where `ssh://` never worked, opens a terminal window.
   (Changing what an address means is the kind of break `AGENTS.md` says to flag; this proposal avoids one.)
3. **"Let AI tools run commands and change files": on or off to start with?** Proposed: on, as saved passwords are,
   with the switch in Settings > AI tools. `ROADMAP.md` words this idea as "after asking first": the AI tool itself
   asks before each use unless the user has told it not to; Marconi asking by itself is a separate idea there.
4. **The Windows server's shell:** PowerShell (7 if installed, else the built-in one) as proposed, or `cmd.exe`?
5. **Version bumps:** MINOR at Phases 2, 4 and 5 and PATCH at 6, 7 and 8, as proposed?
6. **When to start:** now, or after 1.0 as `ROADMAP.md`'s "Toward 1.0" list has it? Phases 1 and 2 (AI tools running
   commands on the Mac) are small and stand alone, if only part should go first.
7. **A different SSH login.** A saved connection has one login name and one password. For the Mac the screen login
   and the SSH login are the same account, so nothing more is needed. Where they differ (a VNC server with its own
   password), proposed: ask once and remember a second login name and password for SSH with that connection.
8. **What the checks need from the owner:**
   - `CRYPTOTEST_SSH_LOGIN` set on this PC for the Mac (Phases 0 and 1), so its password never passes through a chat;
   - Phase 0: turning on Windows' own OpenSSH server on a PC for one check, or leaving that check out and treating
     Windows SSH servers as untested;
   - Phase 5: which computer runs the server: the Mac, or a Linux machine if there is one to use;
   - Phase 6: Marconi Server installed as a service on a Windows PC (this one or "drips"), a second Windows account
     to log in as, and a viewer on another computer for the sign-in screen check.

## 9. Not in this plan unless the owner asks

- A terminal on the phones and tablets (the emulator and the links have no wx in them, so they could be reused).
- An elevated (administrator) shell on Windows.
- A login as another account on macOS and Linux servers.
- A terminal through Remote Desktop connections other than by SSH to the same computer.
- Keeping a shell alive after the viewer disconnects, and picking it up again later.
- Port forwarding, X11 forwarding and an SSH agent.
- Colour themes and fonts beyond the text size; clickable links.
- A person watching an AI's terminal live (goes with the roadmap's "follow along").
