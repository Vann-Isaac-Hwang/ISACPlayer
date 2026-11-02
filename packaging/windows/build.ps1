#Requires -Version 5.1
<#
.SYNOPSIS
Configures, builds, and optionally tests ISACPlayer on Windows with MSVC.

.DESCRIPTION
Wraps CMake in the Visual Studio developer environment so the MSVC linker and
compiler are available, points CMake at the Qt installation, and uses the
vcpkg toolchain file to resolve TagLib, FFTW3, and libmysofa.

Prerequisites:
  * Visual Studio 2022 Build Tools with the C++ workload.
  * Qt 6 for MSVC (for example C:\Qt\6.12.0\msvc2022_64).
  * vcpkg with the dependencies installed:
        vcpkg install taglib fftw3 libmysofa --triplet x64-windows

.EXAMPLE
.\packaging\windows\build.ps1 -QtDir C:\Qt\6.12.0\msvc2022_64 -VcpkgRoot C:\vcpkg

.EXAMPLE
.\packaging\windows\build.ps1 -QtDir C:\Qt\6.12.0\msvc2022_64 -VcpkgRoot C:\vcpkg `
    -Configuration Debug -RunTests
#>
[CmdletBinding()]
param(
    [string]$QtDir = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$BuildDir,
    [string]$Generator = 'Ninja',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [switch]$RunTests
)

$ErrorActionPreference = 'Continue'
$sourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if (-not $QtDir) {
    throw "Pass -QtDir (for example C:\Qt\6.12.0\msvc2022_64) or set QTDIR."
}
if (-not (Test-Path (Join-Path $QtDir 'bin'))) {
    throw "Qt directory '$QtDir' does not contain a bin folder."
}
$QtDir = (Resolve-Path $QtDir).Path

if (-not $VcpkgRoot) {
    throw "Pass -VcpkgRoot or set VCPKG_ROOT so the vcpkg toolchain file can be used."
}
$vcpkgToolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path $vcpkgToolchain)) {
    throw "vcpkg toolchain file not found at '$vcpkgToolchain'."
}
$vcpkgBin = Join-Path $VcpkgRoot 'installed\x64-windows\bin'

if (-not $BuildDir) {
    $BuildDir = Join-Path $sourceDir ("build\windows-" + $Configuration.ToLowerInvariant())
} else {
    $BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'vswhere.exe was not found. Install the Visual Studio C++ build tools.'
}
$vsInstall = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsInstall) {
    throw 'No Visual Studio installation with the C++ toolset was found.'
}
$vcvars = Join-Path $vsInstall 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat was not found at '$vcvars'."
}

$makeProgramArgs = ''
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $qtNinja = Join-Path (Split-Path (Split-Path $QtDir -Parent) -Parent) 'Tools\Ninja\ninja.exe'
    if (Test-Path $qtNinja) {
        $makeProgramArgs = "-DCMAKE_MAKE_PROGRAM=`"$qtNinja`" "
    } else {
        throw "Ninja was not found. Install Ninja or pass a generator with -Generator."
    }
}

$configure = "cmake -S `"$sourceDir`" -B `"$BuildDir`" -G `"$Generator`"" +
    " -DCMAKE_BUILD_TYPE=$Configuration" +
    " -DCMAKE_PREFIX_PATH=`"$QtDir`"" +
    " -DCMAKE_TOOLCHAIN_FILE=`"$vcpkgToolchain`"" +
    " -DVCPKG_TARGET_TRIPLET=x64-windows" +
    " $makeProgramArgs" +
    " -DBUILD_TESTING=" + $(if ($RunTests) { 'ON' } else { 'OFF' })

$batchFile = Join-Path $env:TEMP ("isacplayer-build-" + [System.Guid]::NewGuid() + '.cmd')
$batchLines = @(
    '@echo off',
    "call `"$vcvars`" >nul || exit /b 1",
    $configure,
    'if errorlevel 1 exit /b 1',
    "cmake --build `"$BuildDir`" --config $Configuration"
)
Set-Content -Path $batchFile -Value $batchLines -Encoding ASCII
try {
    & cmd.exe /c "`"$batchFile`""
    if ($LASTEXITCODE -ne 0) {
        throw "The build failed with exit code $LASTEXITCODE."
    }
} finally {
    Remove-Item $batchFile -ErrorAction SilentlyContinue
}

Write-Host "Built ISACPlayer into $BuildDir" -ForegroundColor Green

if ($RunTests) {
    # Test executables load Qt and the vcpkg runtime from PATH.
    $env:PATH = "$(Join-Path $QtDir 'bin');$vcpkgBin;$env:PATH"
    $env:QT_QPA_PLATFORM = 'offscreen'
    & ctest --test-dir $BuildDir --output-on-failure -C $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Tests failed with exit code $LASTEXITCODE."
    }
}
