# ISACPlayer

ISACPlayer is a Qt desktop music player for local libraries, playlists, and real-time DSP routing. It combines a compact music-library interface with a visual node-based audio graph and loadable DSP plugins.

## Features

- SQLite-backed music library with background TagLib scanning, paginated queries, search, and sorting
- Track, artist, album, and playback-history views, including album artwork
- User-managed playlists with track addition, removal, drag reordering, and playlist playback
- Persistent playback queue with sequential, shuffle, repeat-one, and repeat-all modes
- Linux desktop media integration through MPRIS, including track metadata, playback controls, and media keys
- Audio output device selection from the DSP Router page
- DSP graph built around a DAG engine and 64-bit planar `double` audio buses
- Independent master gain applied at the final soundcard output stage
- Built-in Gain, Mono Gain, Mono Delay, Mono Feedback Delay, Mono Modulated Delay, Stereo Width, Stereo Balance, Adder, ReplayGain, and Biquad Equalizer (including All-Pass) plugins
- Plugin library and node inspector docks that can be shown, hidden, or rearranged
- Playback settings for fade in/out, history retention, and displayed audio metadata
- English application interface

Audio is decoded and processed at the source sample rate where supported by the selected output path. The engine requests the configured native rate from the audio backend; shared desktop audio services or hardware may resample it. Current device output is stereo. Multi-device aggregation and channel mapping are not implemented.

## Requirements

- CMake 3.16 or newer
- A C++20 compiler
- Qt 5 or Qt 6 with Widgets and SQL modules
- Qt DBus on Linux
- TagLib and FFTW3 development packages discoverable through `pkg-config`

Check that the required packages are visible to `pkg-config`:

```sh
pkg-config --modversion taglib fftw3
```

## Build

Configure and build from the project root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
```

The application is written to `build/ISACPlayer`. Runtime DSP plugins are built under `build/plugins/`.

## Run

```sh
./build/ISACPlayer
```

The application searches for dynamic plugins in a `plugins/` directory beside the executable. Keep the plugin libraries in that directory when deploying the application.

## Usage

### Library

Add music files or folders and scan them from **Settings > Music Library**. The library presents tracks, artists, albums, and playback history. Use the search control to filter the current view; click track-list headers to sort.

### Playlists

Create, rename, or delete playlists from the playlist list's context menu. Add tracks from the library context menu or from the playlist tools. Playlist tracks can be reordered by dragging rows.

### DSP Router

Choose an output device and routing preset from the top control bar. Instantiate DSP modules from the module library, connect node ports on the canvas, and select a node to edit its parameters in the inspector. Custom presets are stored in the application configuration directory under `dsp-presets/`.

### KDE and MPRIS

On Linux, ISACPlayer exports an MPRIS media-player service over the session D-Bus. KDE Plasma media controls and keyboard media keys can control playback and display the current track metadata. When a KDE global menu registrar is available, application menus are exported to the desktop; otherwise, the menus remain in the application window.

## DSP Plugin Development

The public plugin SDK is in `plugin_sdk/include/isac/plugin/`. Plugins implement the C++ audio-plugin interface and may optionally provide Qt controls. See [plugins/README.md](plugins/README.md) for the plugin ABI, metadata interfaces, and a minimal CMake example.

Each plugin factory must create an independent node instance. Audio-node `process()` methods run on the real-time audio thread and must not allocate memory, acquire blocking locks, perform I/O, or access Qt UI objects. Parameter changes should be validated and transferred to the audio thread using lock-free state.

## Tests

Run the test suite after building:

```sh
ctest --test-dir build --output-on-failure
```

The tests cover the audio graph and DSP nodes, dynamic plugin loading, patchbay interactions, and library storage, scanning, search, and playlist behavior.
