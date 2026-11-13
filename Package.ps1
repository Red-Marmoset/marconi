# Package.ps1 - Build the Windows installers: Build\Installer\MarconiServer-<version>-win64-setup.exe (the server) and
# Build\Installer\Marconi-<version>-win64-setup.exe (the client, which carries the server installer inside)
# and, when this computer has the Android tools, the Android app: Build\Installer\Marconi-<version>-android.apk
#
# The server installer comes first, from PackageServer.ps1 (see there), because the client's installer embeds it: Marconi
# hands it out through F8 > Install Marconi Server on the Other Computer and Settings > Share this computer.
#
# The client is packaged by the una engine's Packaging\PackageProject.ps1, which every una project packages with. Project.json's "package"
# section says what goes in: Marconi.exe (built Release by Build.ps1), Nebula Sans and its licence, and the MSVC
# runtime DLLs it links against (/MD). The installer is Installer\Marconi.nsi, compiled with NSIS 3.08 or newer
# (winget install NSIS.NSIS).
#
# The installer's "Start Marconi now" uses the ShellExecAsUser NSIS plug-in, so Marconi starts as the signed-in user
# rather than as administrator. Install its Unicode build as ShellExecAsUser.dll in the NSIS installation's
# Plugins\x86-unicode folder (C:\Program Files (x86)\NSIS\Plugins\x86-unicode by default). It's on the NSIS wiki:
# https://nsis.sourceforge.io/ShellExecAsUser_plug-in
#
# Signing: unsigned by default, which makes Windows warn before running the installer and Marconi. To publish, sign
# with a code-signing certificate from the current user's certificate store (where a hardware token's certificate
# shows up). Project.json's "signing" section names these environment variables (windows.thumbprintEnv; the
# timestamp variable's name follows from it):
#   MARCONI_SIGN_THUMBPRINT=<the certificate's SHA-1 thumbprint>
#   MARCONI_SIGN_TIMESTAMP_URL=<RFC 3161 timestamp server>   optional; http://timestamp.digicert.com by default
# signtool (Windows SDK) then signs Marconi.exe, the uninstaller and the setup executable, with a timestamp so the
# signatures outlive the certificate.
#
# The version is MAJOR.MINOR.PATCH from Project.json plus the revision, the number of commits in the history (see
# README.md). Release builds should come from a clean, full clone so the version identifies the source exactly.
#
# Usage:
#   .\Package.ps1 [-Clean] [-SkipBuild] [-AllowDirty]
#     -Clean       clean Release build first
#     -SkipBuild   package the existing Build\Marconi\Release\Marconi.exe (it must match the current version)
#     -AllowDirty  package even though tracked files have uncommitted changes (for testing the installer)

param(
    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$AllowDirty
)

$engine = if ($env:UNA_ENGINE_PATH) { $env:UNA_ENGINE_PATH } else { "C:\code\engine" }
$script = Join-Path $engine "Packaging\PackageProject.ps1"
if (-not (Test-Path $script)) {
    Write-Host "ERROR: the una engine at $engine has no Packaging\PackageProject.ps1. Update it (git pull), or set UNA_ENGINE_PATH." -ForegroundColor Red
    exit 1
}
# Project.json names the signing variables in its "signing" section, which an older engine doesn't read: it would
# make an unsigned installer without saying why.
if (-not (Select-String -LiteralPath $script -Pattern 'thumbprintEnv' -SimpleMatch -Quiet)) {
    Write-Host "ERROR: the una engine at $engine is too old to read Project.json's signing section. Update it (git pull)." -ForegroundColor Red
    exit 1
}
& "$PSScriptRoot\PackageServer.ps1" -Clean:$Clean -SkipBuild:$SkipBuild -AllowDirty:$AllowDirty
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $script -ProjectDir $PSScriptRoot -NoZip -Clean:$Clean -SkipBuild:$SkipBuild -AllowDirty:$AllowDirty
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The Android app too, beside the installer, when this computer has the Android tools (see Apps\Android\Package.ps1).
& "$PSScriptRoot\Apps\Android\Package.ps1" -Clean:$Clean
exit $LASTEXITCODE
