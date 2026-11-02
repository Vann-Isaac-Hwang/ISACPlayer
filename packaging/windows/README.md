# Windows Build and Packaging

ISACPlayer builds on Windows with MSVC and Qt 6. TagLib, FFTW3, and the
optional libmysofa come from vcpkg because distributions do not provide
`pkg-config` metadata there.

Windows-specific behaviour that the build and the source already account for:

- The executable is a GUI subsystem application with an embedded icon and
  version block (`resources/windows/`).
- TagLib receives UTF-16 file names on Windows; its narrow `FileName` overload
  would interpret them with the ANSI code page.
- The audio decoder opens files through the wide miniaudio entry point, so
  non-ASCII music paths play correctly.
- MPRIS, D-Bus, and KDE Window System integration are Unix-only and are not
  compiled in. Playback still reports metadata through the player itself.
- DSP and format plugin modules are loaded from `plugins\` and
  `format_plugins\` beside the executable.

## Prerequisites

- Visual Studio 2022 or the Visual Studio Build Tools with the C++ workload
  (`vcvars64.bat` must exist).
- Qt 6 built for MSVC, for example `C:\Qt\6.12.0\msvc2022_64`.
- Ninja (Qt ships one under `Qt\Tools\Ninja`).
- vcpkg with the dependencies installed:

  ```powershell
  vcpkg install taglib fftw3 libmysofa --triplet x64-windows
  ```

libmysofa is optional. Without it the Stereo HRTF Spatializer plugin is not
built and the rest of the player is unaffected. Note that its vcpkg source
archive is large, so the first install can take a while.

## Build

The wrapper loads the MSVC environment, configures with Ninja, and builds:

```powershell
.\packaging\windows\build.ps1 -QtDir C:\Qt\6.12.0\msvc2022_64 -VcpkgRoot C:\vcpkg
```

Add `-RunTests` to run the CTest suite with the Qt and vcpkg runtime on `PATH`,
or `-Configuration Debug` for a debug build. The script defaults to
`build\windows-release` and `build\windows-debug`.

The equivalent manual commands, from a "x64 Native Tools Command Prompt for
VS 2022":

```bat
cmake -S . -B build\windows -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build\windows
```

The build emits `ISACPlayer.exe`, `plugins\*.dll`, and `format_plugins\*.dll`
into the build directory. vcpkg copies the non-Qt runtime DLLs beside the
executable automatically.

## Deploy

`deploy.ps1` runs `cmake --install`, which invokes Qt's deployment tool, and
then copies the vcpkg runtime libraries:

```powershell
.\packaging\windows\deploy.ps1 -BuildDir build\windows-release -VcpkgRoot C:\vcpkg
```

The result is a self-contained folder (default `dist\ISACPlayer`) that can be
started directly or zipped (`-Zip`):

```text
ISACPlayer.exe
Qt6Core.dll, Qt6Gui.dll, Qt6Sql.dll, Qt6Widgets.dll, ...
fftw3.dll, tag.dll, z.dll, mysofa.dll
plugins\               DSP plugin modules and the Qt plugin subdirectories
format_plugins\        Audio-format plugin modules
translations\, licenses\, include\
qt.conf
```

The Qt plugin subdirectories (`platforms\`, `sqldrivers\`, `styles\`, ...) must
be kept next to the executable. The DSP modules sit directly in `plugins\`
beside those subdirectories; Qt ignores loose modules there and ISACPlayer
scans only that directory level.

## Runtime Data

The library database, settings, presets, and the format cache use Qt's standard
locations, which are the user profile on Windows:

- `%APPDATA%\ISACPlayer\ISACPlayer\library.sqlite`
- `%APPDATA%\ISACPlayer\ISACPlayer.ini` (legacy) and the
  `HKEY_CURRENT_USER\Software\ISACPlayer\ISACPlayer` registry key for settings
- `%LOCALAPPDATA%\ISACPlayer\ISACPlayer\format-cache\`

Copying the application folder to another machine does not copy that data.

## Troubleshooting

- **`TagLib was not found` / `FFTW3 was not found` during configure**: pass
  `-DCMAKE_TOOLCHAIN_FILE` for vcpkg or add the dependency prefix to
  `-DCMAKE_PREFIX_PATH`.
- **No plugins listed in the DSP Router**: the `plugins\` directory must be
  beside `ISACPlayer.exe`. A plain copy of the executable is not a deployment.
- **The application starts but stays silent**: check the output device in the
  DSP Router control bar; the default is "System Default", which follows the
  Windows default playback device.
- **Audio fails only for non-ASCII paths**: report it together with the file
  name; the decoder and metadata reader are expected to handle them.
