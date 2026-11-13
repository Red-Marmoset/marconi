# PackageServer.ps1 - Build the Marconi Server installer: Build\Installer\MarconiServer-<version>-win64-setup.exe
#
# NOT YET RUN ON WINDOWS. Written against the engine's Packaging\PackageProject.ps1 (which it borrows its helpers'
# shape from) and checked only by reading; the NSIS script it runs, Installer\MarconiServer.nsi, was compiled with a
# stand-in payload on Linux. Expect to fix small things on the first real run.
#
# Package.ps1 runs this first, then the client's packaging, because the client's installer embeds the one made here
# (Installer\Marconi.nsi looks for Build\Installer\MarconiServer-<version>-win64-setup.exe).
#
# What it does: builds the tray program (ServerUI\Project.json, output MarconiServerTray.exe) Release with Build.ps1,
# stages it as MarconiServer.exe with the MSVC runtime DLLs, signs it when MARCONI_SIGN_THUMBPRINT is set (see
# Package.ps1), runs makensis on Installer\MarconiServer.nsi and prints the installer's SHA-256.
#
# The version is MAJOR.MINOR.PATCH from the root Project.json plus the number of commits, the same as the client's, so
# the two installers always carry the same version (SERVER_PLAN.md, section 1, point 7). The server program has no
# version resource yet, so unlike the client the script can't check the built program's version: build it fresh
# (without -SkipBuild) when in doubt.
#
# Usage:
#   .\PackageServer.ps1 [-Clean] [-SkipBuild] [-AllowDirty]
#     -Clean       clean Release build first
#     -SkipBuild   package the existing Build\MarconiServerTray\Release\MarconiServerTray.exe
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

function Invoke-Git {
    $ErrorActionPreference = "Continue"
    $output = & git -C $root @args 2>$null
    if ($LASTEXITCODE -ne 0) { Fail "git $args failed. Packaging needs git and a clone of the repository." }
    return $output
}

function Find-Makensis {
    $command = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($dir in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if ($dir -and (Test-Path (Join-Path $dir "NSIS\makensis.exe"))) { return (Join-Path $dir "NSIS\makensis.exe") }
    }
    return $null
}

function Find-Signtool {
    $command = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($dir in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $dir) { continue }
        $candidates = @(Get-ChildItem -Directory -Path (Join-Path $dir "Windows Kits\10\bin") -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName "x64\signtool.exe" } |
            Where-Object { Test-Path $_ })
        if ($candidates) { return $candidates[0] }
    }
    return $null
}

# The MSVC runtime DLLs (vcruntime140.dll, msvcp140.dll, ...) that /MD builds need: the toolset vcvarsall set up,
# otherwise the newest installed Visual Studio's.
function Find-CrtDirectory {
    $redistRoots = @()
    if ($env:VCToolsRedistDir) { $redistRoots += $env:VCToolsRedistDir }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        foreach ($vs in @(& $vswhere -products * -property installationPath)) {
            $redistRoots += @(Get-ChildItem -Directory -Path (Join-Path $vs "VC\Redist\MSVC") -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
                Sort-Object { [version]$_.Name } -Descending |
                ForEach-Object { $_.FullName })
        }
    }
    foreach ($redistRoot in $redistRoots) {
        $crt = Get-ChildItem -Directory -Path (Join-Path $redistRoot "x64") -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($crt) { return $crt.FullName }
    }
    return $null
}

# --- Check the source and the tools before spending time on a build ---
$project = Get-Content -Raw -LiteralPath (Join-Path $root "Project.json") | ConvertFrom-Json
if ((Invoke-Git rev-parse --is-shallow-repository) -eq "true") {
    Fail "this is a shallow clone, so the revision would be wrong. Fetch the full history (git fetch --unshallow)."
}
$dirty = Invoke-Git status --porcelain --untracked-files=no
if ($dirty -and -not $AllowDirty) {
    Fail "tracked files have uncommitted changes, so the version wouldn't identify the source. Commit them, or pass -AllowDirty for a test package."
}
$makensis = Find-Makensis
if (-not $makensis) { Fail "makensis.exe not found. Install NSIS 3.08 or newer (winget install NSIS.NSIS)." }
$pluginPath = Join-Path (Split-Path -Parent $makensis) "Plugins\x86-unicode\ShellExecAsUser.dll"
if (-not (Test-Path $pluginPath)) {
    Fail "the ShellExecAsUser NSIS plug-in wasn't found at $pluginPath. Download its Unicode build from https://nsis.sourceforge.io/ShellExecAsUser_plug-in and copy ShellExecAsUser.dll there."
}
$crtDir = Find-CrtDirectory
if (-not $crtDir) { Fail "the MSVC runtime DLLs (VC\Redist\MSVC\<version>\x64\Microsoft.VC*.CRT) weren't found. Install the 'MSVC v143 - VS 2022 C++ x64/x86 build tools' component." }

$thumbprint = $env:MARCONI_SIGN_THUMBPRINT
$signtool = $null
if ($thumbprint) {
    $thumbprint = $thumbprint -replace '\s', ''
    if ($thumbprint -notmatch '^[0-9A-Fa-f]{40}$') { Fail "MARCONI_SIGN_THUMBPRINT must be the certificate's SHA-1 thumbprint: 40 hexadecimal digits." }
    $signtool = Find-Signtool
    if (-not $signtool) { Fail "signtool.exe not found, so the installer can't be signed. Install the Windows SDK, or unset MARCONI_SIGN_THUMBPRINT for an unsigned package." }
    if (-not (Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $thumbprint.ToUpperInvariant() })) {
        Fail "no certificate with thumbprint $thumbprint in the current user's certificate store (certmgr.msc > Personal > Certificates)."
    }
    $timestampUrl = $env:MARCONI_SIGN_TIMESTAMP_URL
    if (-not $timestampUrl) { $timestampUrl = "http://timestamp.digicert.com" }
}

# --- Build ---
$serverProject = Join-Path $root "ServerUI\Project.json"
$name = (Get-Content -Raw -LiteralPath $serverProject | ConvertFrom-Json).name   # MarconiServerTray
if (-not $SkipBuild) {
    # In a PowerShell of its own, as from a prompt (see the engine's PackageProject.ps1).
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

$version = "$($project.version).$(Invoke-Git rev-list --count HEAD)"
$packageDir = Join-Path $root "Build\Installer"
$stageDir = Join-Path $packageDir "ServerStage"
$appDir = Join-Path $stageDir "App"

# --- Stage: App\ is exactly what's installed ---
if (Test-Path $stageDir) {
    $resolved = (Resolve-Path -LiteralPath $stageDir).Path
    if ($resolved -ne [IO.Path]::GetFullPath($stageDir) -or (Get-Item -LiteralPath $stageDir).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
        Fail "refusing to remove an unexpected staging folder: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $appDir | Out-Null
# The tray program ships as MarconiServer.exe (the plan's name for it); the console program in Server\ is not shipped.
Copy-Item -LiteralPath $exe -Destination (Join-Path $appDir "MarconiServer.exe")
Copy-Item (Join-Path $crtDir "*.dll") $appDir

$signScript = $null
if ($thumbprint) {
    $signScript = Join-Path $stageDir "Sign.cmd"
    $signCommand = "`"$signtool`" sign /fd SHA256 /sha1 $thumbprint /tr $timestampUrl /td SHA256 %1"
    $oem = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
    [IO.File]::WriteAllLines($signScript, [string[]]@("@echo off", $signCommand, "exit /b %errorlevel%"), $oem)
    & $signScript (Join-Path $appDir "MarconiServer.exe")
    if ($LASTEXITCODE -ne 0) { Fail "signing MarconiServer.exe failed (signtool exit code $LASTEXITCODE)." }
}

$files = @(Get-ChildItem -LiteralPath $appDir -Recurse -File)
$bytes = ($files | Measure-Object Length -Sum).Sum
Write-Host ("[PackageServer] Marconi Server {0}: {1} files, {2:N0} MB staged" -f $version, $files.Count, ($bytes / 1MB))

# The uninstaller removes exactly what was installed: every file, then the folders deepest first.
$lines = @()
$files | Sort-Object FullName | ForEach-Object { $lines += "Delete `"`$INSTDIR\$($_.FullName.Substring($appDir.Length + 1))`"" }
Get-ChildItem -LiteralPath $appDir -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending |
    ForEach-Object { $lines += "RMDir `"`$INSTDIR\$($_.FullName.Substring($appDir.Length + 1))`"" }
[IO.File]::WriteAllLines((Join-Path $stageDir "UninstallFiles.nsh"), [string[]]$lines)

# The licence page wraps text itself, so each paragraph goes on one line (the same as the engine's script does).
$text = [IO.File]::ReadAllText((Join-Path $root "LICENSE")) -replace "`r`n", "`n"
$paragraphs = foreach ($block in ($text.Trim() -split "`n\s*`n")) {
    $items = @()
    foreach ($line in ($block -split "`n")) {
        $line = ($line.Trim() -replace '^#+\s*', '') -replace '\*\*', ''
        if ($items.Count -eq 0 -or $line.StartsWith("- ")) { $items += $line } else { $items[-1] += " " + $line }
    }
    $items -join "`r`n"
}
[IO.File]::WriteAllText((Join-Path $stageDir "LicensePage.txt"), ($paragraphs -join "`r`n`r`n") + "`r`n")

# --- Setup ---
$setup = Join-Path $packageDir "MarconiServer-$version-win64-setup.exe"
if (Test-Path $setup) { Remove-Item -Force $setup }
$defines = @("/DVERSION=$version", "/DSTAGE_DIR=$stageDir", "/DOUT_FILE=$setup")
if ($signScript) { $defines += "/DSIGN_SCRIPT=$signScript" }
Write-Host "[PackageServer] Compressing Setup with NSIS ($makensis)"
& $makensis /V2 /WX @defines (Join-Path $root "Installer\MarconiServer.nsi")
if ($LASTEXITCODE -ne 0) { Fail "makensis failed with exit code $LASTEXITCODE." }

Write-Host ""
$hash = (Get-FileHash -Algorithm SHA256 $setup).Hash.ToLowerInvariant()
Write-Host ("{0}  ({1:N1} MB)" -f $setup, ((Get-Item $setup).Length / 1MB)) -ForegroundColor Green
Write-Host "  SHA256 $hash"
if ($dirty) {
    Write-Host "WARNING: built with uncommitted changes; don't publish this one." -ForegroundColor Yellow
} elseif (-not $thumbprint) {
    Write-Host "Unsigned: fine for testing, but Windows SmartScreen will warn before running it. Set MARCONI_SIGN_THUMBPRINT to sign a release." -ForegroundColor Yellow
}
exit 0
