# Package.ps1 - Build the Windows installer: Build\Installer\Marconi-<version>-win64-setup.exe
#
# Builds the Release app with Build.ps1, stages Marconi.exe with the MSVC runtime DLLs it links against (/MD), and
# compiles Installer\Marconi.nsi with NSIS 3.08 or newer (winget install NSIS.NSIS).
#
# The installer's "Start Marconi now" uses the ShellExecAsUser NSIS plug-in, so Marconi starts as the signed-in user
# rather than as administrator. Install its Unicode build as ShellExecAsUser.dll in the NSIS installation's
# Plugins\x86-unicode folder (C:\Program Files (x86)\NSIS\Plugins\x86-unicode by default). It's on the NSIS wiki:
# https://nsis.sourceforge.io/ShellExecAsUser_plug-in
#
# Signing: unsigned by default, which makes Windows warn before running the installer and Marconi. To publish, sign
# with a code-signing certificate from the current user's certificate store (where a hardware token's certificate
# shows up):
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

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Fail {
    param([string]$Message)
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Invoke-Git {
    # Windows PowerShell turns a native command's stderr into a terminating error under "Stop".
    $ErrorActionPreference = "Continue"
    $output = & git -C $root @args 2>$null
    if ($LASTEXITCODE -ne 0) {
        Fail "git $args failed. Packaging needs git and a clone of the repository."
    }
    return $output
}

# The MSVC runtime DLLs (vcruntime140.dll, msvcp140.dll, ...) to deploy next to Marconi.exe. Prefers the toolset
# that vcvarsall set up for the build, otherwise the newest installed Visual Studio's.
function Find-CrtDirectory {
    $redistRoots = @()
    if ($env:VCToolsRedistDir) {
        $redistRoots += $env:VCToolsRedistDir
    }
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
        if ($crt) {
            return $crt.FullName
        }
    }
    return $null
}

function Find-Makensis {
    $command = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($dir in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if ($dir) {
            $candidate = Join-Path $dir "NSIS\makensis.exe"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }
    return $null
}

# signtool.exe from the Windows SDK: on PATH in a Visual Studio developer shell (Build.ps1 sets one up), otherwise
# the newest installed SDK's x64 one.
function Find-Signtool {
    $command = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($dir in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if ($dir) {
            $candidates = @(Get-ChildItem -Directory -Path (Join-Path $dir "Windows Kits\10\bin") -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
                Sort-Object { [version]$_.Name } -Descending |
                ForEach-Object { Join-Path $_.FullName "x64\signtool.exe" } |
                Where-Object { Test-Path $_ })
            if ($candidates) {
                return $candidates[0]
            }
        }
    }
    return $null
}

# Check the source before spending time on a build.
if ((Invoke-Git rev-parse --is-shallow-repository) -eq "true") {
    Fail "this is a shallow clone, so the revision would be wrong. Fetch the full history (git fetch --unshallow)."
}
$dirty = Invoke-Git status --porcelain --untracked-files=no
if ($dirty -and -not $AllowDirty) {
    Fail "tracked files have uncommitted changes, so the version wouldn't identify the source. Commit them, or pass -AllowDirty for a test package."
}
$makensis = Find-Makensis
if (-not $makensis) {
    Fail "makensis.exe not found. Install NSIS 3.08 or newer (winget install NSIS.NSIS) or put makensis.exe on PATH."
}
$plugin = Join-Path (Split-Path -Parent $makensis) "Plugins\x86-unicode\ShellExecAsUser.dll"
if (-not (Test-Path $plugin)) {
    Fail "the ShellExecAsUser NSIS plug-in wasn't found at $plugin. Download its Unicode build from https://nsis.sourceforge.io/ShellExecAsUser_plug-in and copy ShellExecAsUser.dll there (see the top of Package.ps1)."
}

# Signing is optional; when configured, everything it needs must be there before the build starts.
$thumbprint = $env:MARCONI_SIGN_THUMBPRINT
$signtool = $null
if ($thumbprint) {
    $thumbprint = $thumbprint -replace '\s', ''
    if ($thumbprint -notmatch '^[0-9A-Fa-f]{40}$') {
        Fail "MARCONI_SIGN_THUMBPRINT must be the certificate's SHA-1 thumbprint: 40 hexadecimal digits."
    }
    $signtool = Find-Signtool
    if (-not $signtool) {
        Fail "signtool.exe not found, so the package can't be signed. Install the Windows SDK (Windows 10 SDK signing tools) or put signtool.exe on PATH, or unset MARCONI_SIGN_THUMBPRINT for an unsigned package."
    }
    if (-not (Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $thumbprint.ToUpperInvariant() })) {
        Fail "no certificate with thumbprint $thumbprint in the current user's certificate store (certmgr.msc > Personal > Certificates)."
    }
    $timestampUrl = $env:MARCONI_SIGN_TIMESTAMP_URL
    if (-not $timestampUrl) {
        $timestampUrl = "http://timestamp.digicert.com"
    }
}

if (-not $SkipBuild) {
    & (Join-Path $root "Build.ps1") -Config Release -Clean:$Clean
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$exe = Join-Path $root "Build\Marconi\Release\Marconi.exe"
if (-not (Test-Path $exe)) {
    Fail "$exe not found. Run without -SkipBuild."
}

# The installer is labelled with the version stamped into the exe, which must be the current source's.
$baseVersion = (Get-Content (Join-Path $root "Project.json") -Raw | ConvertFrom-Json).version
$expected = "$baseVersion.$(Invoke-Git rev-list --count HEAD)"
$info = (Get-Item $exe).VersionInfo
$version = "$($info.FileMajorPart).$($info.FileMinorPart).$($info.FileBuildPart).$($info.FilePrivatePart)"
if ($version -ne $expected) {
    Fail "Marconi.exe is version $version but the source is $expected. Rebuild (run without -SkipBuild)."
}

$crtDir = Find-CrtDirectory
if (-not $crtDir) {
    Fail "the MSVC runtime DLLs (VC\Redist\MSVC\<version>\x64\Microsoft.VC*.CRT) weren't found. Install the 'MSVC v143 - VS 2022 C++ x64/x86 build tools' component."
}

# Stage: App\ is everything installed next to Marconi.exe; UninstallFiles.nsh lists the same files for the uninstaller.
$installerDir = Join-Path $root "Build\Installer"
$stageDir = Join-Path $installerDir "Stage"
$appDir = Join-Path $stageDir "App"
if (Test-Path $stageDir) {
    $resolvedStage = (Resolve-Path -LiteralPath $stageDir).Path
    $expectedStage = [IO.Path]::GetFullPath((Join-Path $root "Build\Installer\Stage"))
    if ($resolvedStage -ne $expectedStage -or (Get-Item -LiteralPath $stageDir).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
        Fail "Refusing to remove unexpected staging directory: $resolvedStage"
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $appDir | Out-Null
Copy-Item $exe $appDir

# Sign.cmd signs the file given as its argument. Package.ps1 runs it on the staged Marconi.exe, and makensis runs it
# on the uninstaller and the setup executable (the SIGN_SCRIPT define in Marconi.nsi).
$signScript = $null
if ($thumbprint) {
    $signScript = Join-Path $stageDir "Sign.cmd"
    $signCommand = "`"$signtool`" sign /fd SHA256 /sha1 $thumbprint /tr $timestampUrl /td SHA256 %1"
    # In cmd.exe's own code page, with no byte order mark, which it would read as part of the first command.
    $oem = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
    [IO.File]::WriteAllLines($signScript, [string[]]@("@echo off", $signCommand, "exit /b %errorlevel%"), $oem)
    & $signScript (Join-Path $appDir "Marconi.exe")
    if ($LASTEXITCODE -ne 0) {
        Fail "signing Marconi.exe failed (signtool exit code $LASTEXITCODE)."
    }
}
# Explicit sources also make -SkipBuild packages independent of development asset staging.
Copy-Item -LiteralPath (Join-Path $root "Assets\Fonts\NebulaSans-Black.ttf") -Destination $appDir
Copy-Item -LiteralPath (Join-Path $root "Assets\Fonts\NebulaSans-Semibold.ttf") -Destination $appDir
Copy-Item -LiteralPath (Join-Path $root "Assets\Fonts\NebulaSans-LICENSE.txt") -Destination $appDir
Copy-Item (Join-Path $crtDir "*.dll") $appDir
# The licence page wraps lines to fit, so join each paragraph of LICENSE into one line; its 80-column breaks would
# otherwise leave ragged lines. The installed LICENSE.txt is the original.
$licence = (Get-Content -Raw (Join-Path $root "LICENSE")) -replace "`r`n", "`n"
$paragraphs = $licence.Trim() -split "`n{2,}" | ForEach-Object { ($_ -split "`n" | ForEach-Object { $_.Trim() }) -join " " }
[IO.File]::WriteAllText((Join-Path $stageDir "License.txt"), ($paragraphs -join "`r`n`r`n") + "`r`n")
$uninstallLines = Get-ChildItem -File $appDir | Sort-Object Name | ForEach-Object { "Delete `"`$INSTDIR\$($_.Name)`"" }
[IO.File]::WriteAllLines((Join-Path $stageDir "UninstallFiles.nsh"), [string[]]$uninstallLines)

$outFile = Join-Path $installerDir "Marconi-$version-win64-setup.exe"
if (Test-Path $outFile) {
    Remove-Item -Force $outFile
}

Write-Host "[Package.ps1] Version=$version"
Write-Host "[Package.ps1] Runtime=$crtDir"
Write-Host "[Package.ps1] NSIS=$makensis"
if ($thumbprint) {
    Write-Host "[Package.ps1] Signing=$thumbprint via $signtool, timestamps from $timestampUrl"
} else {
    Write-Host "[Package.ps1] Signing=none"
}

$defines = @("/DVERSION=$version", "/DSTAGE_DIR=$stageDir", "/DOUT_FILE=$outFile")
if ($signScript) {
    $defines += "/DSIGN_SCRIPT=$signScript"
}
& $makensis /V2 /WX @defines (Join-Path $root "Installer\Marconi.nsi")
if ($LASTEXITCODE -ne 0) {
    Fail "makensis failed with exit code $LASTEXITCODE."
}

$hash = (Get-FileHash -Algorithm SHA256 $outFile).Hash.ToLowerInvariant()
Write-Host ""
Write-Host "Installer: $outFile" -ForegroundColor Green
Write-Host "SHA256:    $hash"
if ($dirty) {
    Write-Host "WARNING: built with uncommitted changes; don't publish this one." -ForegroundColor Yellow
} elseif (-not $thumbprint) {
    Write-Host "Unsigned: fine for testing, but Windows will warn before running it. Set MARCONI_SIGN_THUMBPRINT to sign a release (see the top of Package.ps1)." -ForegroundColor Yellow
} else {
    Write-Host "To mark the release: git tag v$version; git push origin v$version"
}
