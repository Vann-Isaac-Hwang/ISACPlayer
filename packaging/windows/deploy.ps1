#Requires -Version 5.1
<#
.SYNOPSIS
Assembles a self-contained ISACPlayer folder for Windows.

.DESCRIPTION
Installs the built application into a destination folder, runs the Qt
deployment tool through the generated CMake install script, and copies the
non-Qt runtime libraries (FFTW3, TagLib, libmysofa, zlib) that vcpkg produced.

The resulting folder is executable as-is: ISACPlayer.exe, the Qt runtime, the
DSP plugin modules in plugins\, and the format plugin modules in
format_plugins\ all sit next to each other.

.EXAMPLE
.\packaging\windows\deploy.ps1 -BuildDir build\windows-release -VcpkgRoot C:\vcpkg

.EXAMPLE
.\packaging\windows\deploy.ps1 -BuildDir build\windows-release -VcpkgRoot C:\vcpkg -Zip
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$Destination,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Triplet = 'x64-windows',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [switch]$Zip
)

$ErrorActionPreference = 'Continue'
$sourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if (-not $BuildDir) {
    $BuildDir = Join-Path $sourceDir ("build\windows-" + $Configuration.ToLowerInvariant())
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
if (-not (Test-Path (Join-Path $BuildDir 'ISACPlayer.exe'))) {
    throw "ISACPlayer.exe was not found in '$BuildDir'. Build the project first."
}

if (-not $Destination) {
    $Destination = Join-Path $sourceDir 'dist\ISACPlayer'
}
$Destination = [System.IO.Path]::GetFullPath($Destination)
if (Test-Path $Destination) {
    Remove-Item -Recurse -Force $Destination
}
New-Item -ItemType Directory -Path $Destination -Force | Out-Null

Write-Host "Installing into $Destination" -ForegroundColor Cyan

# Qt's deployment tool copies the MSVC runtime only when the Visual Studio
# environment is visible, so the install step runs from a developer prompt.
$vcvars = ''
$vsInstall = ''
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vsInstall = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($vsInstall) {
        $candidate = Join-Path $vsInstall 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $candidate) {
            $vcvars = $candidate
        }
    }
}
if (-not $vcvars) {
    Write-Warning ('Visual Studio was not found: the MSVC runtime is not copied. ' +
        'The target machine then needs the Visual C++ Redistributable.')
}

$batchFile = Join-Path $env:TEMP ("isacplayer-deploy-" + [System.Guid]::NewGuid() + '.cmd')
$batchLines = @(
    '@echo off',
    'set CMAKE_SUPPRESS_DEVELOPER_WARNINGS=1'
)
if ($vcvars) {
    $batchLines += "call `"$vcvars`" >nul"
}
$batchLines += "cmake --install `"$BuildDir`" --prefix `"$Destination`" --config $Configuration"
Set-Content -Path $batchFile -Value $batchLines -Encoding ASCII
try {
    & cmd.exe /c "`"$batchFile`""
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --install failed with exit code $LASTEXITCODE."
    }
} finally {
    Remove-Item $batchFile -ErrorAction SilentlyContinue
}

# The Qt deploy tool treats the MSVC runtime as an operating system component
# and skips it. Ship the redistributable DLLs so the folder also runs on a
# machine without the Visual C++ Redistributable installed.
if ($vsInstall) {
    $redistRoot = Join-Path $vsInstall 'VC\Redist\MSVC'
    if (Test-Path $redistRoot) {
        $newestRedist = Get-ChildItem $redistRoot -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName 'x64') } |
            Sort-Object Name -Descending |
            Select-Object -First 1
        if ($newestRedist) {
            $crtDirectory = Get-ChildItem (Join-Path $newestRedist.FullName 'x64') -Directory |
                Where-Object { $_.Name -like 'Microsoft.VC*.CRT' } |
                Select-Object -First 1
            if ($crtDirectory) {
                Copy-Item (Join-Path $crtDirectory.FullName '*.dll') $Destination -Force
                Write-Host "Copied the MSVC runtime from $($crtDirectory.FullName)"
            }
        }
    }
}

# The Qt deploy step only handles Qt libraries. These runtime DLLs come from
# vcpkg and must ship beside the executable as well.
$runtimeLibraries = @('fftw3.dll', 'tag.dll', 'mysofa.dll', 'z.dll')
if ($VcpkgRoot) {
    $vcpkgBin = Join-Path $VcpkgRoot "installed\$Triplet\bin"
    foreach ($library in $runtimeLibraries) {
        $source = Join-Path $vcpkgBin $library
        if (Test-Path $source) {
            Copy-Item $source (Join-Path $Destination $library) -Force
        }
    }
} else {
    Write-Warning 'VcpkgRoot is not set: non-Qt runtime libraries were not copied.'
}

foreach ($library in $runtimeLibraries) {
    if (-not (Test-Path (Join-Path $Destination $library))) {
        Write-Warning "Missing runtime library $library in the deployment folder."
    }
}

$readme = @'
ISACPlayer
==========

Run ISACPlayer.exe. No installation is required.

Layout
------
  ISACPlayer.exe        The player.
  plugins\              DSP plugin modules loaded at startup.
  format_plugins\       Audio-format plugin modules (for example NCM).
  platforms\, styles\,  Qt runtime plugins. Do not rename these folders.
  sqldrivers\, ...

Data
----
The music library database, settings, DSP presets, and the format cache are
stored in the current user profile:

  %APPDATA%\ISACPlayer\ISACPlayer\library.sqlite
  %APPDATA%\ISACPlayer\ISACPlayer\            (QSettings)
  %LOCALAPPDATA%\ISACPlayer\ISACPlayer\       (format cache)

Licenses
--------
See the licenses folder for the Qt, TagLib, FFTW, miniaudio, NCM AES, and
libmysofa notices that apply to this build.
'@
Set-Content -Path (Join-Path $Destination 'README.txt') -Value $readme -Encoding UTF8

$pluginCount = @(Get-ChildItem (Join-Path $Destination 'plugins') -Filter '*.dll' -File -ErrorAction SilentlyContinue).Count
$formatCount = @(Get-ChildItem (Join-Path $Destination 'format_plugins') -Filter '*.dll' -File -ErrorAction SilentlyContinue).Count
Write-Host "Deployed $pluginCount DSP plugin(s) and $formatCount format plugin(s)." -ForegroundColor Green

if ($Zip) {
    $archive = "$Destination.zip"
    if (Test-Path $archive) {
        Remove-Item $archive -Force
    }
    Compress-Archive -Path $Destination -DestinationPath $archive
    Write-Host "Wrote $archive" -ForegroundColor Green
}
