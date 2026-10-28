# Marconi for iPad and iPhone

The Marconi viewer as an iOS app: `Modules/MarconiCore` built by Xcode from a project the Una BuildTool generates,
with a UIKit screen (`Marconi_iOS.mm`, the whole app). It connects to the same computers the desktop app does: VNC
servers (`host`, `host:1`, `vnc://host::5901`), VNC through SSH (`ssh://user@host`) and Marconi servers
(`marconi://host`). Remote Desktop and finding computers on the network aren't in it yet, and neither are saved
connections and file transfer.

Needs iOS or iPadOS 15 or later, and to build: a Mac with Xcode and its iOS SDK, and the Una engine with its
BuildTool bootstrapped (the engine and BuildTool revisions this app was made with are in `UnaEngine.json`).

## Build and install from your Mac

1. Pull `main` in the Una engine and run `git submodule update --init --recursive`, then bootstrap BuildTool
   (`bash BootstrapBuildTool.sh` in the engine checkout).
2. In the Marconi checkout, generate the project through Una:

   ```sh
   export UNA_ENGINE_PATH="/absolute/path/to/engine"
   bash Apps/iPad/GenerateProject.sh
   open Build/Xcode/Marconi/Marconi.xcodeproj
   ```

   The script goes through `Build.sh`, which writes the version header the core needs, then runs
   `BuildTool --generate-xcode --platform iOS Apps/iPad/Project.json`. Rerun it after a source, manifest or version
   change; it resets signing settings, so choose your team again afterwards.
3. In Xcode, select the **Marconi** target, then **Signing & Capabilities**, and choose your development team (a
   personal Apple ID installs on your own device). Change the bundle identifier if yours must be unique.
4. Connect the iPad or iPhone, trust the Mac on it, turn on Developer Mode if asked, select it as the run
   destination and press Run. Use the Release configuration for a smooth picture.

To check that it compiles without a device or signing:

```sh
xcodebuild -project Build/Xcode/Marconi/Marconi.xcodeproj -scheme Marconi -configuration Release \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build
```

## Using it

Type the computer's address, and a user name and password if you want to send them straight away (otherwise
the app asks when the computer does), then Connect. The app remembers the last address and user name, never the
password. A certificate you accept is remembered for that address.

On the screen: tap to click, double-tap to double-click, touch and hold for a right click, tap then hold and move
to drag with the left button. One finger moves the pointer, two fingers move and pinch to zoom the picture. The
bar at the bottom shows the keyboard and has Esc, Tab, the arrows and Del; Ctrl, Alt, Shift and Cmd stay pressed
for the next key. A hardware keyboard works too. Text the other computer copies goes to the clipboard. Links of
the form `vnc://host` and `marconi://host` open in the app.

## What was and wasn't checked

The project generation is checked on Linux and the core builds for Android with the same sources, but compiling
with the iOS SDK, signing, and the app on a device need Xcode and a Mac: the UIKit code in `Marconi_iOS.mm` has
not been run yet. Start with a VNC server on the same Wi-Fi.
