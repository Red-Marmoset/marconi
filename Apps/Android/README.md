# Marconi for Android

The Marconi viewer as an Android app: `Modules/MarconiCore` built for Android by the Una BuildTool, a small bridge
module (`Modules/MarconiAndroid`) that an app written in Kotlin (this folder) loads and drives. It connects to the
same computers the desktop app does: VNC servers (`host`, `host:1`, `vnc://host::5901`), VNC through SSH
(`ssh://user@host`) and Marconi servers (`marconi://host`). Remote Desktop and finding computers on the network
aren't in it yet, and neither are saved connections and file transfer.

Needs Android 7 (API 24) or later on the phone or tablet, and to build: the Una engine with its BuildTool
bootstrapped, the Android NDK (r27 or later) and SDK (platform 35, build tools 35), a Java 17 or later JDK, and
Gradle (the wrapper in this folder fetches 8.14).

## Build and install

1. Set `UNA_ENGINE_PATH` to the engine checkout and `ANDROID_HOME` to the SDK (which has the NDK under `ndk/`), or
   `ANDROID_NDK_HOME` to the NDK itself.
2. From this folder:

   ```sh
   ./gradlew assembleDebug          # Linux or macOS
   gradlew.bat assembleDebug        # Windows
   ```

   Gradle first runs `Build.sh` (or `Build.ps1`) with `Apps/Android/Project.json`, `--platform Android` and
   `--abi` for each ABI in `gradle.properties` (`marconiAbis`, `arm64-v8a` by default; add `x86_64` for the
   emulator), which writes `Build/Marconi/Release/Android/<abi>/libMarconi.so`; the app's `jniLibs` folder points
   there. The APK lands in `app/build/outputs/apk/debug/app-debug.apk`.
3. Plug the phone in with USB debugging on, and install:

   ```sh
   adb install -r app/build/outputs/apk/debug/app-debug.apk
   ```

   Or open this folder in Android Studio and press Run.

`assembleRelease` makes an unsigned release APK; sign it with your own key before installing.

## Using it

Type the computer's address, and a user name and password if you want to send them straight away (otherwise
the app asks when the computer does), then Connect. The app remembers the last address and user name, never the
password. A certificate you accept is remembered for that address; a changed one is asked about again.

On the screen: tap to click, double-tap to double-click, touch and hold for a right click, double-tap and hold to
drag with the left button. One finger moves the pointer, two fingers move and zoom the picture. The bar at the
bottom opens the keyboard and has Esc, Tab, the arrows, Del and Win; Ctrl, Alt and Shift stay pressed for the
next key. Text the other computer copies arrives on the phone's clipboard. Rotating the phone keeps the
connection.

## How it fits together

- `Modules/MarconiCore/QuickConnect.cpp` picks the client for an address (VNC, VNC through SSH, or the Marconi
  protocol) the same way for this app and the iPad app.
- `Modules/MarconiAndroid/Bridge_Android.cpp` is the JNI side of `NativeSession.kt`: one native session per
  Kotlin object, the picture copied into an Android bitmap, and the session's questions (password, certificate,
  SSH key) handed to Kotlin, which shows a dialog while the connection thread waits.
- `app/src/main/java/com/redmarmoset/marconi/`: `ConnectActivity` (the first screen), `ViewerActivity` (the
  session and the dialogs), `RemoteView` (drawing, touch and typing), `Keys` (key codes to keysyms).

Engine logging goes to logcat with the channel as the tag: `adb logcat -s Rfb Viewer BuildTool`.
