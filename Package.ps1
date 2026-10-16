# Package.ps1 - Build the Windows installer: Build\Installer\Marconi-<version>-win64-setup.exe
#
# Builds the Release app with Build.ps1, stages Marconi.exe with the MSVC runtime DLLs it links against (/MD), and
# compiles Installer\Marconi.nsi with NSIS 3 (winget install NSIS.NSIS).
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
    Fail "makensis.exe not found. Install NSIS 3 (winget install NSIS.NSIS) or put makensis.exe on PATH."
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
    Remove-Item -Recurse -Force $stageDir
}
New-Item -ItemType Directory -Force -Path $appDir | Out-Null
Copy-Item $exe $appDir
Copy-Item (Join-Path $crtDir "*.dll") $appDir
$uninstallLines = Get-ChildItem -File $appDir | Sort-Object Name | ForEach-Object { "Delete `"`$INSTDIR\$($_.Name)`"" }
[IO.File]::WriteAllLines((Join-Path $stageDir "UninstallFiles.nsh"), [string[]]$uninstallLines)

$outFile = Join-Path $installerDir "Marconi-$version-win64-setup.exe"
if (Test-Path $outFile) {
    Remove-Item -Force $outFile
}

Write-Host "[Package.ps1] Version=$version"
Write-Host "[Package.ps1] Runtime=$crtDir"
Write-Host "[Package.ps1] NSIS=$makensis"

& $makensis /V2 /WX "/DVERSION=$version" "/DSTAGE_DIR=$stageDir" "/DOUT_FILE=$outFile" (Join-Path $root "Installer\Marconi.nsi")
if ($LASTEXITCODE -ne 0) {
    Fail "makensis failed with exit code $LASTEXITCODE."
}

$hash = (Get-FileHash -Algorithm SHA256 $outFile).Hash.ToLowerInvariant()
Write-Host ""
Write-Host "Installer: $outFile" -ForegroundColor Green
Write-Host "SHA256:    $hash"
if ($dirty) {
    Write-Host "WARNING: built with uncommitted changes; don't publish this one." -ForegroundColor Yellow
} else {
    Write-Host "To mark the release: git tag v$version; git push origin v$version"
}
