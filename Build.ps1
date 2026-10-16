# Build.ps1 - Build Marconi (or one of its test projects) using the una engine's BuildTool.
#
# Mirrors the pattern from engine's BuildEditor.ps1: auto-detects vcvarsall.bat
# so the script works from a plain Command Prompt / PowerShell window, not
# only a Visual Studio Developer PowerShell.
#
# Usage:
#   .\Build.ps1 [-Project <Project.json>] [-Config Release|Debug] [-Clean]
#   e.g. .\Build.ps1 -Project Tests\MarconiCoreTest\Project.json -Config Debug
# Any additional arguments are forwarded to BuildTool verbatim.

param(
    [string]$Project = "Project.json",
    [string]$Config = "Release",
    [switch]$Clean,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Extra
)

$ErrorActionPreference = "Stop"

if (-not $env:UNA_ENGINE_PATH) {
    $env:UNA_ENGINE_PATH = "C:\code\engine"
}
$enginePath = $env:UNA_ENGINE_PATH

function Invoke-EnvironmentSetup {
    param(
        [string]$Path,
        [string]$Arguments
    )
    $tempFile = [IO.Path]::GetTempFileName()
    cmd /c " `"$Path`" $Arguments && set > `"$tempFile`" "
    Get-Content $tempFile | ForEach-Object {
        if ($_ -match "^(.*?)=(.*)$") {
            Set-Content "env:\$($matches[1])" $matches[2]
        }
    }
    Remove-Item $tempFile
}

# Skip MSVC setup entirely if cl.exe is already on PATH (we're already inside
# a Developer PowerShell).
if (-not (Get-Command "cl.exe" -ErrorAction SilentlyContinue)) {
    Write-Host "Setting up MSVC environment for x64..."
    $vcvarsPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat"
    )

    $vcvarsFound = $false
    # The prebuilt wxWidgets under $UNA_ENGINE_PATH\External\wxwidgets was
    # built with MSVC toolset 14.44+, which introduced STL vectorization
    # helpers (__std_find_last_trivial_2 et al). Linking against it from an
    # older toolset (14.38) produces unresolved externals. Try to force a
    # matching toolset; fall back to whatever vcvarsall picks by default.
    $preferredToolset = "14.44"
    foreach ($path in $vcvarsPaths) {
        if (Test-Path $path) {
            $toolsDir = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $path))) "Tools\MSVC"
            $hasPreferred = $false
            if (Test-Path $toolsDir) {
                $hasPreferred = @(Get-ChildItem -Directory -Path $toolsDir -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -like "$preferredToolset.*" }).Count -gt 0
            }
            if ($hasPreferred) {
                Invoke-EnvironmentSetup -Path $path -Arguments "x64 -vcvars_ver=$preferredToolset"
                Write-Host "MSVC environment configured for x64 (toolset $preferredToolset) from:`n  $path"
            } else {
                Invoke-EnvironmentSetup -Path $path -Arguments "x64"
                Write-Host "MSVC environment configured for x64 (default toolset) from:`n  $path"
            }
            $vcvarsFound = $true
            break
        }
    }

    if (-not $vcvarsFound) {
        Write-Host "ERROR: Could not find vcvarsall.bat for any installed Visual Studio." -ForegroundColor Red
        Write-Host "Install VS 2019 or 2022 (Community edition is fine), or run this from a" -ForegroundColor Red
        Write-Host "Visual Studio Developer PowerShell." -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "cl.exe already on PATH; skipping MSVC environment setup."
}

# Locate BuildTool. Prefer Release, fall back to Debug, MinSizeRel, then
# Bootstrap. The engine bootstrap produces Bootstrap\BuildTool.exe at minimum.
$candidates = @(
    (Join-Path $enginePath "Build\BuildTool\Release\BuildTool.exe"),
    (Join-Path $enginePath "Build\BuildTool\MinSizeRel\BuildTool.exe"),
    (Join-Path $enginePath "Build\BuildTool\Debug\BuildTool.exe"),
    (Join-Path $enginePath "Build\BuildTool\Bootstrap\BuildTool.exe")
)
$buildTool = $null
foreach ($candidate in $candidates) {
    if (Test-Path $candidate) {
        $buildTool = $candidate
        break
    }
}

if (-not $buildTool) {
    Write-Host "ERROR: BuildTool not found under $enginePath\Build\BuildTool\." -ForegroundColor Red
    Write-Host "Bootstrap it first: cd $enginePath && .\BootstrapBuildTool.bat" -ForegroundColor Red
    exit 1
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectFile = if ([IO.Path]::IsPathRooted($Project)) { $Project } else { Join-Path $scriptDir $Project }
if (-not (Test-Path $projectFile)) {
    Write-Host "ERROR: Project file not found at $projectFile" -ForegroundColor Red
    exit 1
}

# Version: MAJOR.MINOR.PATCH from Project.json plus a revision, the number of commits in HEAD's history (the first
# commit is 1). Written to a generated header that Version.cpp and Marconi.rc include. Build.sh does the same.
function Update-BuildVersion {
    param([string]$Root)

    # Windows PowerShell turns a native command's redirected stderr into a terminating error under "Stop".
    $ErrorActionPreference = "Continue"

    $baseVersion = (Get-Content (Join-Path $Root "Project.json") -Raw | ConvertFrom-Json).version
    if ($baseVersion -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
        Write-Host "ERROR: Project.json version '$baseVersion' must be MAJOR.MINOR.PATCH." -ForegroundColor Red
        exit 1
    }
    $major = $matches[1]; $minor = $matches[2]; $patch = $matches[3]

    $revision = "0"
    $commit = "unknown"
    if (Get-Command "git" -ErrorAction SilentlyContinue) {
        $count = git -C $Root rev-list --count HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $count) {
            $revision = "$count".Trim()
            $commit = "$(git -C $Root rev-parse --short=7 HEAD)".Trim()
            if ("$(git -C $Root rev-parse --is-shallow-repository)".Trim() -eq "true") {
                Write-Host "WARNING: shallow clone; revision $revision undercounts the history." -ForegroundColor Yellow
            }
        }
    }
    if ($revision -eq "0") {
        Write-Host "WARNING: no git history found; building with revision 0." -ForegroundColor Yellow
    }

    $version = "$major.$minor.$patch.$revision"
    $header = @(
        "// Generated by Build.ps1 / Build.sh from Project.json and the git history. Do not edit or commit.",
        "// Included by Version.cpp and by MarconiUI/Marconi.rc, so it must stay plain enough for rc.exe.",
        "",
        "#ifndef MARCONI_BUILD_VERSION_H",
        "#define MARCONI_BUILD_VERSION_H",
        "",
        "#define MARCONI_VERSION_MAJOR $major",
        "#define MARCONI_VERSION_MINOR $minor",
        "#define MARCONI_VERSION_PATCH $patch",
        "#define MARCONI_VERSION_REVISION $revision",
        "#define MARCONI_VERSION_STRING `"$version`"",
        "#define MARCONI_VERSION_COMMIT `"$commit`"",
        "",
        "#endif",
        ""
    ) -join "`n"

    # Only rewrite on change, so an unchanged version doesn't trigger rebuilds.
    $headerPath = Join-Path $Root "Modules\MarconiCore\BuildVersion.h"
    $existing = if (Test-Path $headerPath) { [IO.File]::ReadAllText($headerPath) } else { "" }
    if ($existing -ne $header) {
        [IO.File]::WriteAllText($headerPath, $header, (New-Object System.Text.UTF8Encoding $false))
        # BuildTool doesn't track the headers an .rc file includes, so touch it to refresh the exe's version info.
        $rcPath = Join-Path $Root "Modules\MarconiUI\Marconi.rc"
        if (Test-Path $rcPath) {
            (Get-Item $rcPath).LastWriteTime = Get-Date
        }
    }
    Write-Host "[Build.ps1] Version=$version ($commit)"
}

Update-BuildVersion -Root $scriptDir

$cores = [System.Environment]::ProcessorCount
if ($cores -lt 1) { $cores = 4 }

Write-Host "[Build.ps1] UNA_ENGINE_PATH=$enginePath"
Write-Host "[Build.ps1] BuildTool=$buildTool"
Write-Host "[Build.ps1] Project=$projectFile"
Write-Host "[Build.ps1] Config=$Config"

$buildArgs = @($projectFile, "-config", $Config, "-j", "$cores")
if ($Clean) { $buildArgs += "--clean" }
if ($Extra) { $buildArgs += $Extra }

Push-Location $scriptDir
try {
    & $buildTool @buildArgs
    $rc = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($rc -ne 0) {
    Write-Host "Build failed with exit code $rc." -ForegroundColor Red
    exit $rc
}
Write-Host "Marconi build complete." -ForegroundColor Green
