<#
.SYNOPSIS
    Builds foo_rubato for every requested architecture and packages the
    result as an installable .fb2k-component in dist\.

.DESCRIPTION
    Configures and builds with CMake, runs the test suite, then assembles one
    archive holding both architectures:

        foo_rubato-<version>.fb2k-component
          foo_rubato.dll        <- 32 bit, foobar2000 1.x and 2.x (x86)
          x64/foo_rubato.dll    <- 64 bit, foobar2000 2.x (x64)

    foobar2000 ignores subfolders it does not understand, so one file installs
    everywhere. Debug symbols go into a separate archive that is NOT part of
    the component - keep it so foobar2000 crash reports can be resolved.

    The DLL is named foo_rubato.dll on both architectures because
    foo_rubato.cpp asserts that name with VALIDATE_COMPONENT_FILENAME; only
    the archive name carries the version.

    The SDK and WTL are fetched on the first configure; see scripts\get_sdk.ps1.

.PARAMETER Arch
    Which architectures to build. Default: x86 and x64.

.PARAMETER Configuration
    CMake configuration. Default: Release.

.PARAMETER Pffft
    Build the spectral stage on PFFFT at single precision instead of KISS FFT
    at double: about 1.7x on a whole analysis, and NOT what the component
    ships. Gets its own build directory and its own archive name, so the two
    can never be mistaken for one another. See cmake\fft_backend.cmake.

.PARAMETER Dynamic
    Link the C runtime as a DLL rather than statically: 209KB off each DLL and
    111KB off each packed, and NOT what the component ships. The build
    then needs the Visual C++ redistributable present, and foobar2000 refuses
    to load a component whose runtime is missing without saying much about
    why. Own build directory and own archive name, as -Pffft.

.PARAMETER SkipTests
    Do not run the verification harness. Not recommended.

.PARAMETER Clean
    Wipe the build directories first.

.EXAMPLE
    .\scripts\build_release.ps1

.EXAMPLE
    .\scripts\build_release.ps1 -Arch x64 -Clean

.EXAMPLE
    .\scripts\build_release.ps1 -Pffft

.EXAMPLE
    .\scripts\build_release.ps1 -Dynamic -Arch x64
#>

[CmdletBinding()]
param(
    [ValidateSet('x86', 'x64')]
    [string[]] $Arch = @('x86', 'x64'),
    [string]   $Configuration = 'Release',
    [switch]   $Pffft,
    [switch]   $Dynamic,
    [switch]   $SkipTests,
    [switch]   $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root    = Split-Path -Parent $PSScriptRoot
$distDir = Join-Path $root 'dist'
$stage   = Join-Path $root 'build\_package'
$symbols = Join-Path $root 'build\_symbols'

function Invoke-Checked([string] $what, [scriptblock] $action) {
    & $action
    if ($LASTEXITCODE -ne 0) { throw "$what failed with exit code $LASTEXITCODE" }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'cmake was not found on PATH. Install CMake 3.21 or newer, or run this from a Developer PowerShell.'
}

# --- version, straight out of the project so the archive name cannot drift ---
$cmakeLists = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch '(?m)^\s*VERSION\s+([0-9]+(?:\.[0-9]+)*)') {
    throw 'Could not read VERSION out of CMakeLists.txt'
}
$version = $Matches[1]
Write-Host "foo_rubato $version" -ForegroundColor Cyan

# --- what is being built, and so which build tree and which archive ---------
# Anything that changes the binary gets its own directory and its own archive
# name, and the suffixes compose. Sharing either would be a trap rather than a
# convenience: every one of these is a CMake cache variable, so a plain build
# run after a switched one into the same directory would keep what was cached
# and package it as the shipping component without saying so.
#
# One switch each, rather than the options underneath them. There is no
# configuration worth releasing that needs finer control than this: KISS at
# float is a precision probe rather than something to ship, PFFFT has no double
# to offer, and cmake\fft_backend.cmake and the FOO_RUBATO_* options are still
# there for a build that wants to say something else.
$cmakeArgs = @()
$suffix    = ''

if ($Pffft) {
    $cmakeArgs += '-DBPMCORE_FFT_BACKEND=pffft', '-DBPMCORE_FFT_SCALAR=float'
    $suffix    += '-pffft'
    Write-Host '  spectral stage: PFFFT, float - not the shipping configuration' -ForegroundColor Yellow
} else {
    Write-Host '  spectral stage: KISS FFT, double' -ForegroundColor DarkGray
}

if ($Dynamic) {
    $cmakeArgs += '-DFOO_RUBATO_STATIC_CRT=OFF'
    $suffix    += '-dynamic'
    Write-Host '  C runtime: DLL - needs the VC redist on the target machine' -ForegroundColor Yellow
} else {
    Write-Host '  C runtime: static' -ForegroundColor DarkGray
}

foreach ($dir in @($stage, $symbols)) {
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
}
New-Item -ItemType Directory -Force $stage, $symbols, $distDir | Out-Null

foreach ($a in $Arch) {
    $platform = if ($a -eq 'x64') { 'x64' } else { 'Win32' }
    $buildDir = Join-Path $root "build\$a$suffix"

    if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

    Write-Host "`n=== Configuring $a ===" -ForegroundColor Cyan
    Invoke-Checked "cmake configure ($a)" {
        & cmake -S $root -B $buildDir -A $platform @cmakeArgs
    }

    Write-Host "`n=== Building $a ===" -ForegroundColor Cyan
    Invoke-Checked "cmake build ($a)" {
        & cmake --build $buildDir --config $Configuration --parallel
    }

    if (-not $SkipTests) {
        Write-Host "`n=== Testing $a ===" -ForegroundColor Cyan
        Invoke-Checked "ctest ($a)" {
            & ctest --test-dir $buildDir -C $Configuration --output-on-failure
        }
    }

    # 32 bit goes at the archive root, 64 bit in x64\ - that is the layout
    # foobar2000 2.x expects, and 1.x simply ignores the subfolder.
    $subdir = if ($a -eq 'x64') { Join-Path $stage 'x64' } else { $stage }
    New-Item -ItemType Directory -Force $subdir | Out-Null

    $built = Join-Path $buildDir "foo_rubato\$Configuration\foo_rubato.dll"
    if (-not (Test-Path $built)) { throw "Expected output missing: $built" }
    Copy-Item $built $subdir -Force

    $pdb = [System.IO.Path]::ChangeExtension($built, '.pdb')
    if (Test-Path $pdb) {
        $symDir = Join-Path $symbols $a
        New-Item -ItemType Directory -Force $symDir | Out-Null
        Copy-Item $pdb $symDir -Force
    }

    Write-Host ("  foo_rubato.dll  {0,-4} {1,9:N0} bytes" -f $a, (Get-Item $built).Length) -ForegroundColor Green
}

# --- package ---------------------------------------------------------------
# cmake -E tar produces the same zip on every PowerShell version, and CMake is
# already a hard dependency here.
Write-Host "`n=== Package ===" -ForegroundColor Cyan
$componentPath = Join-Path $distDir "foo_rubato-$version$suffix.fb2k-component"
$symbolsPath   = Join-Path $distDir "foo_rubato-$version$suffix-symbols.zip"
foreach ($p in @($componentPath, $symbolsPath)) {
    if (Test-Path $p) { Remove-Item -Force $p }
}

Invoke-Checked "packaging the component" {
    & cmake -E chdir $stage cmake -E tar cf $componentPath --format=zip .
}
Invoke-Checked "packaging the symbols" {
    & cmake -E chdir $symbols cmake -E tar cf $symbolsPath --format=zip .
}

Write-Host ("  {0}  ({1:N0} bytes)" -f $componentPath, (Get-Item $componentPath).Length) -ForegroundColor Green
& cmake -E tar tf $componentPath | ForEach-Object { Write-Host "      $_" }
Write-Host ("  {0}  ({1:N0} bytes)" -f $symbolsPath, (Get-Item $symbolsPath).Length) -ForegroundColor DarkGray

Write-Host @"

To install: drag the .fb2k-component file onto foobar2000, or use
File > Preferences > Components > Install...
"@ -ForegroundColor Yellow

$notes = @()
if ($Pffft) {
    $notes += 'It analyses with PFFFT at single precision rather than KISS at double.'
}
if ($Dynamic) {
    $notes += 'It links the C runtime as a DLL, so foobar2000 will refuse to load it where the Visual C++ redistributable is missing, and will say little about why.'
}
if ($notes.Count -gt 0) {
    Write-Host "`nThis is not the release build. $($notes -join ' ')" -ForegroundColor Yellow
    Write-Host 'Install it to measure or to test, and do not publish it under that name.' -ForegroundColor Yellow
}
