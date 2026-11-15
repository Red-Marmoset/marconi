# PackageServer.ps1 - Build the Marconi Server installer: Build\Installer\MarconiServer-<version>-win64-setup.exe
#
# Package.ps1 runs this first, then the client's packaging, because the client's installer embeds the one made here
# (Installer\Marconi.nsh looks for Build\Installer\MarconiServer-<version>-win64-setup.exe).
#
# It builds the tray program (ServerUI\Project.json, which makes MarconiServerTray.exe) Release with Build.ps1, then hands
# it to the una engine's Packaging\PackageProject.ps1 with its version, the same packaging the client and every una
# project uses. ServerUI\Project.json's "package" section says the rest: it ships as MarconiServer.exe
# (package.windows.executableName) with the MSVC runtime DLLs, is signed when MARCONI_SIGN_THUMBPRINT is set (its
# "signing" section, as in Package.ps1), and Installer\MarconiServer.nsi (package.installerScript) makes the installer,
# which installs for the whole computer. The engine prints the installer's SHA-256.
#
# The version is MAJOR.MINOR.PATCH from the root Project.json plus the number of commits, the same as the client's, so
# the two installers always carry the same version (SERVER_PLAN.md, section 1, point 7). The engine checks the version
# stamped into the built program against it.
#
# Usage:
#   .\PackageServer.ps1 [-Clean] [-SkipBuild] [-AllowDirty]
#     -Clean       clean Release build first
#     -SkipBuild   package the existing Build\MarconiServerTray\Release\MarconiServerTray.exe (it must be this version)
#     -AllowDirty  package even though tracked files have uncommitted changes (for testing the installer)

param(
    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$AllowDirty
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

function Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

$engine = if ($env:UNA_ENGINE_PATH) { $env:UNA_ENGINE_PATH } else { "C:\code\engine" }
$script = Join-Path $engine "Packaging\PackageProject.ps1"
# -Executable and package.windows.executableName came with the engine's Windows options; an older engine can't do this.
if (-not (Test-Path $script) -or -not (Select-String -LiteralPath $script -Pattern 'executableName' -SimpleMatch -Quiet)) {
    Fail "the una engine at $engine is too old to package Marconi Server (its Packaging\PackageProject.ps1 has no package.windows.executableName). Update it (git pull), or set UNA_ENGINE_PATH."
}

# --- Build ---
$serverProject = Join-Path $root "ServerUI\Project.json"
$name = (Get-Content -Raw -LiteralPath $serverProject | ConvertFrom-Json).name   # MarconiServerTray
if (-not $SkipBuild) {
    # In a PowerShell of its own, as from a prompt (as the engine's PackageProject.ps1 runs a project's build).
    $buildArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $root "Build.ps1"),
                   "-Project", "ServerUI\Project.json", "-Config", "Release")
    if ($Clean) { $buildArgs += "-Clean" }
    $ErrorActionPreference = "Continue"
    & powershell.exe @buildArgs
    $buildExit = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    if ($buildExit -ne 0) { Fail "the server build failed." }
}
$exe = Join-Path $root "Build\$name\Release\$name.exe"
if (-not (Test-Path $exe)) { Fail "$exe not found. Run without -SkipBuild." }

# --- Package, with the root Project.json's version (ServerUI\Project.json's own is the server's internal one) ---
$ErrorActionPreference = "Continue"
$count = & git -C $root rev-list --count HEAD 2>$null
$gitExit = $LASTEXITCODE
$ErrorActionPreference = "Stop"
if ($gitExit -ne 0) { Fail "git rev-list failed. Packaging needs git and a clone of the repository." }
$version = "$((Get-Content -Raw -LiteralPath (Join-Path $root "Project.json") | ConvertFrom-Json).version).$count"
& $script -ProjectDir (Join-Path $root "ServerUI") -Executable $exe -Version $version -NoZip -AllowDirty:$AllowDirty
exit $LASTEXITCODE
