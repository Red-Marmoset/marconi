# rmvnc

A free, cross-platform VNC client (Windows, macOS, Linux) built on the
[una engine](../engine) with a wxWidgets UI.

Targets macOS Screen Sharing (Apple ARD auth) as well as standard RFB servers
(TightVNC, TigerVNC, RealVNC, x11vnc, UltraVNC).

## Building

Requires the una engine checked out next to this repo (or `UNA_ENGINE_PATH`
set) with BuildTool bootstrapped.

Windows (plain PowerShell or cmd; MSVC environment is set up automatically):

    .\Build.ps1                       # Release app
    .\Build.ps1 -Config Debug
    .\Build.ps1 -Project Tests\RmVncCoreTest\Project.json -Config Debug

macOS / Linux:

    ./Build.sh
    ./Build.sh Tests/RmVncCoreTest/Project.json -config Debug

Output goes to `Build/<Project>/<Config>/`.

## Layout

- `Modules/RmVncCore` - headless RFB client: sockets, protocol, auth, decoders. No wx GUI headers.
- `Modules/RmVncUI` - wxWidgets application.
- `Tests/` - una test projects for the core.
