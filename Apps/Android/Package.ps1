# Apps/Android/Package.ps1 - Build the Android app for release: Build\Installer\Marconi-<version>-android.apk
#
# Package.ps1 runs this after the Windows installer; Apps\Android\Package.sh is the same for macOS and Linux. It
# needs a JDK, the Android SDK (ANDROID_HOME or ANDROID_SDK_ROOT, else %LOCALAPPDATA%\Android\Sdk where Android
# Studio installs it) with an NDK in its ndk folder or at ANDROID_NDK_HOME, and the Una engine at UNA_ENGINE_PATH
# with its BuildTool built. When any of those is missing, or no release key is set, it says so and exits with 0, so
# the installer still gets made.
#
# Signing: with the release key these name. They are the environment variables Project.json's "signing" section
# names for Android; change the names there. Without MARCONI_ANDROID_KEYSTORE the app is not packaged (for a test
# build signed with the debug key, run gradlew.bat assembleDebug here instead):
#   MARCONI_ANDROID_KEYSTORE=<path to the keystore>
#   MARCONI_ANDROID_KEYSTORE_PASSWORD=<its password>
#   MARCONI_ANDROID_KEY_ALIAS=<the key's alias>          optional; "marconi" by default
#   MARCONI_ANDROID_KEY_PASSWORD=<the key's password>    optional; the keystore's by default
#
# Usage:
#   .\Apps\Android\Package.ps1 [-Clean]

param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$app = "$root\Apps\Android"

function Skip([string]$reason) {
    Write-Host "[Package.ps1] Android app not built: $reason"
    exit 0
}

# BuildTool builds the native library, and says which environment variables Project.json names for the release key.
$engine = if ($env:UNA_ENGINE_PATH) { $env:UNA_ENGINE_PATH } else { "C:\code\engine" }
$buildTool = $null
foreach ($cfg in @('Release', 'MinSizeRel', 'Debug', 'Bootstrap')) {
    if (Test-Path "$engine\Build\BuildTool\$cfg\BuildTool.exe") { $buildTool = "$engine\Build\BuildTool\$cfg\BuildTool.exe"; break }
}
if (-not $buildTool) { Skip "no BuildTool under $engine\Build\BuildTool (bootstrap the engine, or set UNA_ENGINE_PATH)." }
$env:UNA_ENGINE_PATH = $engine

# The name of the environment variable a signing setting is read from (never what it holds).
function Signing-Env([string]$setting) {
    $name = & $buildTool "$root\Project.json" --signing-env $setting
    if ($LASTEXITCODE -ne 0 -or -not $name) {
        throw "$buildTool couldn't say which variable holds $setting. Check Project.json's signing section, and that the engine is recent enough to have one (update it and rebuild BuildTool)."
    }
    return "$name".Trim()
}

# A release is signed with the release key, so there is nothing to package without one. Only the password's
# variable is named here; Gradle reads what it holds.
$keystoreEnv = Signing-Env 'android.keystore'
$keystorePasswordEnv = Signing-Env 'android.keystorePassword'
$keystore = [Environment]::GetEnvironmentVariable($keystoreEnv)
if (-not $keystore) { Skip "$keystoreEnv isn't set (the release key; see the top of Apps\Android\Package.ps1)." }
if (-not (Test-Path $keystore)) { throw "$keystoreEnv=$keystore doesn't exist." }
if (-not [Environment]::GetEnvironmentVariable($keystorePasswordEnv)) { throw "$keystorePasswordEnv isn't set." }

# The other tools: a JDK, the SDK with a platform and build tools, and an NDK.
$java = if ($env:JAVA_HOME -and (Test-Path "$env:JAVA_HOME\bin\java.exe")) { "$env:JAVA_HOME\bin\java.exe" } else { (Get-Command java -ErrorAction SilentlyContinue).Source }
if (-not $java) { Skip "no Java found (install a JDK, 17 or newer, or set JAVA_HOME)." }

$sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } elseif ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { "$env:LOCALAPPDATA\Android\Sdk" }
if (-not ((Test-Path "$sdk\platforms") -and (Test-Path "$sdk\build-tools"))) { Skip "no Android SDK at $sdk (install Android Studio or the command-line tools, or set ANDROID_HOME)." }
$env:ANDROID_HOME = $sdk

$ndk = if ($env:ANDROID_NDK_HOME) { $env:ANDROID_NDK_HOME } elseif ($env:ANDROID_NDK_ROOT) { $env:ANDROID_NDK_ROOT } else { $null }
if (-not $ndk -and (Test-Path "$sdk\ndk")) {
    $ndk = Get-ChildItem "$sdk\ndk" -Directory | Sort-Object { [version]($_.Name -replace '[^0-9.].*$', '') } | Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $ndk -or -not (Test-Path "$ndk\toolchains\llvm")) { Skip "no Android NDK (install one from the SDK manager, or set ANDROID_NDK_HOME)." }
$env:ANDROID_NDK_HOME = $ndk

# aapt, from the newest build tools, reads the version the app was stamped with.
$aapt = Get-ChildItem "$sdk\build-tools" -Directory | Sort-Object { [version]$_.Name } | Select-Object -Last 1 | ForEach-Object { Join-Path $_.FullName 'aapt.exe' }
if (-not $aapt -or -not (Test-Path $aapt)) { Skip "no aapt in $sdk\build-tools." }

$signing = "release key $keystore"

Write-Host "[Package.ps1] Android: SDK=$sdk"
Write-Host "[Package.ps1] Android: NDK=$ndk"
Write-Host "[Package.ps1] Android: Signing=$signing"

Push-Location $app
try {
    if ($Clean) {
        foreach ($dir in @("$root\Build\Marconi\Android", "$root\Build\Intermediate\Marconi\Android")) {
            if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
        }
        Get-ChildItem "$root\Build\Marconi", "$root\Build\Intermediate\Marconi" -Directory -Filter 'Android-*' -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
        & .\gradlew.bat --no-daemon -q clean
        if ($LASTEXITCODE -ne 0) { throw "gradlew clean failed ($LASTEXITCODE)" }
    }
    & .\gradlew.bat --no-daemon -q assembleRelease
    if ($LASTEXITCODE -ne 0) { throw "gradlew assembleRelease failed ($LASTEXITCODE)" }
}
finally {
    Pop-Location
}

$apk = "$app\app\build\outputs\apk\release\app-release.apk"
if (-not (Test-Path $apk)) { throw "Gradle made no $apk." }
$badging = & $aapt dump badging $apk
$version = ($badging | Select-String -Pattern "versionName='([^']*)'" | Select-Object -First 1).Matches[0].Groups[1].Value
if (-not $version) { throw "couldn't read the version from $apk." }

$installerDir = "$root\Build\Installer"
New-Item -ItemType Directory -Force $installerDir | Out-Null
$outFile = "$installerDir\Marconi-$version-android.apk"
Copy-Item $apk $outFile -Force
$hash = (Get-FileHash $outFile -Algorithm SHA256).Hash.ToLower()
Write-Host ""
Write-Host "Android app: $outFile"
Write-Host "SHA256:      $hash"
