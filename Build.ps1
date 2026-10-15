# Build.ps1 - Build rmvnc (or one of its test projects) using the una engine's BuildTool.
#
# Mirrors the pattern from engine's BuildEditor.ps1: auto-detects vcvarsall.bat
# so the script works from a plain Command Prompt / PowerShell window, not
# only a Visual Studio Developer PowerShell.
#
# Usage:
#   .\Build.ps1 [-Project <Project.json>] [-Config Release|Debug] [-Clean]
#   e.g. .\Build.ps1 -Project Tests\RmVncCoreTest\Project.json -Config Debug
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
Write-Host "rmvnc build complete." -ForegroundColor Green
