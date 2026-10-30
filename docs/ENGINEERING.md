# ISACPlayer Engineering Guide

This document describes the current source tree and runtime contracts. It is
intended for contributors working on the application, its audio engine, or
loadable plugins. Keep it aligned with `AGENTS.md` and the implementation.

## 1. Product and Runtime Boundaries

ISACPlayer is a C++20 Qt Widgets application. The repository separates the
Qt-free audio graph engine from the UI, stores library and queue state in
SQLite, and uses two independent dynamic extension systems:

1. DSP plugins create audio nodes that execute inside the graph.
2. Audio-format plugins inspect source files and create playable cache files.

The application currently renders stereo audio. The internal graph bus uses
planar `double` samples; the miniaudio callback writes interleaved `float`
samples to the selected output device. Engine graph buffers are prepared with
the active sample rate, with an engine limit of 768,000 Hz. This is not a
guarantee that the hardware or desktop audio server accepts that rate or that
the complete system path is bit-perfect.

## 2. Repository Map

| Path | Responsibility |
| --- | --- |
| `main.cpp` | Creates `QApplication`, sets application identity, and starts `MainWindow`. |
| `mainwindow.cpp`, `mainwindow.h`, `mainwindow.ui` | Composition root: navigation, settings, playback controls, page wiring, engine coordination, plugin docks, MPRIS setup, and timers. Keep subsystem logic in its owning component. |
| `src/engine/graph/` | Qt-free audio engine, audio buffers, node contract, DAG compiler, execution plan, decoder/output coordination, and real-time tap/ring buffers. |
| `src/engine/nodes/` | Built-in/legacy node implementations used by the engine and tests. Most user-instantiated DSP modules are in `plugins/`. |
| `src/library/` | Track metadata, TagLib reader, SQLite repository, asynchronous scanner, and playback queue model. |
| `src/media/formats/` | Format-plugin discovery, playback/cover cache paths, and background prefetch for upcoming tracks. |
| `src/ui/isac_music/` | Library, playlist, now-playing, and playback-queue widgets/models. |
| `src/ui/patchbay/` | DSP Router canvas, node and cable editing, and JSON routing preset catalog. |
| `src/ui/plugins/` | DSP plugin loading, metadata adaptation, node creation, and inspector controls. |
| `src/ui/inspector/` | Node parameter inspector. |
| `src/ui/player/` | Waveform and FFT waterfall visualization. |
| `src/ui/theme/` | System/light/dark theme handling and shared palette/style application. |
| `src/ui/widgets/` | Reusable playback controls and smooth-scroll behavior. |
| `src/platform/mpris/` | Linux/Unix session D-Bus MPRIS service and desktop media integration. |
| `plugin_sdk/` | Public C++ DSP plugin interfaces. DSP-facing headers do not depend on Qt. |
| `plugins/<module>/` | In-tree DSP plugin targets, each with its own `CMakeLists.txt`. |
| `format_sdk/` | Public audio-format plugin interface. |
| `format_plugins/<format>/` | In-tree format plugin targets; currently includes NCM support. |
| `config/` | Qt resources, desktop metadata, and built-in DSP preset JSON files. |
| `tests/` | Engine, plugin loader, patchbay, scrolling, and library tests. |
| `third_party/miniaudio/` | Vendored miniaudio backend header. |

## 3. Startup and Object Ownership

`main.cpp` sets the organization and application names to `ISACPlayer`, which
also scopes `QSettings` and Qt standard application paths. It then constructs
and shows `MainWindow`.

`MainWindow` owns or coordinates the principal runtime objects:

- `LibraryRepository` for UI-thread database access.
- `LibraryScanner` for background folder/file scanning.
- `AudioEngine` for decoder, graph, and device output.
- `PluginManager` for DSP modules loaded beside the executable.
- `FormatPluginManager` for format modules loaded beside the executable.
- `FormatPlaybackPrefetcher` for preparing upcoming format-plugin tracks.
- Library, playlist, playback-queue, patchbay, visualization, and theme UI.

Qt parent ownership is used for widgets and QObject-based services. The
repository itself is thread-confined; the scanner creates its own repository
connection on its worker thread instead of sharing the UI connection.

## 4. Persistent Data and Runtime Directories

Paths are resolved through Qt's standard locations rather than being tied to
the source tree:

- **Library database:** `QStandardPaths::AppDataLocation/library.sqlite`.
- **Application settings:** `QSettings`, scoped by organization/application
  name. Settings are grouped by purpose, including `appearance/`, `display/`,
  `playback/`, `audio/`, and `visualization/`.
- **Custom DSP presets:** `QStandardPaths::AppConfigLocation/dsp-presets/`.
  Built-in presets are copied from Qt resources only when a same-named file
  does not already exist, so user-edited files are not overwritten.
- **Format cache:** under `QStandardPaths::AppLocalDataLocation/format-cache/`,
  separated into playback and cover-art data.
- **DSP modules:** `<application directory>/plugins/`.
- **Format modules:** `<application directory>/format_plugins/`.

The CMake build emits in-tree DSP modules to `build/plugins/` and format
modules to `build/format_plugins/`. Installed module targets are placed in
matching directories beside the installed executable. A deployment must
include both directories and their Qt/runtime dependencies.

Format playback cache keys incorporate the absolute source path, file size,
source modification time, and plugin ID. Updating a source file therefore
produces a distinct cache path rather than silently reusing the previous
materialization.

## 5. Library, Metadata, and Playlists

### Scanning

`LibraryScanner` walks selected directories or explicit files on a worker
thread. It constructs a thread-local `LibraryRepository` and a format-plugin
manager. Ordinary supported audio files are inspected through
`MetadataReader`/TagLib. Files claimed by a format plugin use that plugin's
metadata instead. Successful results are committed in batches of 64, with
queued Qt notifications used to update progress and refresh the UI.

Scanning is incremental: path, file size, and modification time are used to
skip unchanged files where applicable. Do not perform scanning or metadata I/O
on the audio callback.

### Storage Model

The repository creates and initializes its SQLite schema in
`LibraryRepository::initializeSchema()`. The core entities are:

- `tracks`: stable track ID, source path, optional playback/cover cache paths,
  display metadata, duration, track/disc number, year, file size, and
  timestamps.
- `artists` and `track_artists`: normalized artist entities and
  many-to-many track relationships.
- `albums` and `track_albums`: album entities keyed by album name and their
  track relationships.
- `library_directories`: configured scan roots and scan timestamps.
- `playlists` and `playlist_tracks`: user playlists with explicit row
  positions.
- `playback_queue`, `playback_queue_source`, and `playback_state`: persisted
  queue order, source order, cursor, and playback mode.
- `playback_history`: play events associated with tracks and timestamps.

Track rows preserve display strings while relation tables support browsing
individual artists and albums. Album artists are aggregated from their tracks
for presentation. Audio format, sample rate, bit depth, channel count, and
bitrate are exposed through `TrackInfo` by metadata reading; they are not
columns in the current `tracks` schema. Schema compatibility adjustments and
relation normalization are performed during repository initialization;
preserve user data and test migrations when changing this code.

Repository query APIs support bounded/paginated track, artist, album,
playlist, and history results. Keep UI refreshes scoped to the current page
and avoid materializing an entire large library in memory.

### Playback Queue

`PlaybackQueue` owns in-memory source order, effective order, cursor, and mode.
The available modes are sequential, shuffle, repeat-one, and repeat-all.
`LibraryRepository` persists the queue and cursor. When changing queue or mode
behavior, keep the visible queue view and persisted order/cursor synchronized.

## 6. Audio Playback and DSP

### Playback Data Flow

```text
audio file or format-plugin cache
        |
        v
decoder worker -> preallocated SPSC PCM ring
        |
        v
Audio callback -> planar double source bus
        |
        v
compiled DAG execution plan -> DSP nodes -> sink input bus
        |
        v
master gain + transition envelopes + validation
        |
        v
interleaved float device output
```

The decoder worker owns decoder/file work and publishes PCM through a
single-producer/single-consumer ring buffer. The audio callback never waits
for decoded data; short reads are zero-filled and tracked as underruns.
Recovery uses a short output crossfade to reduce discontinuities.

When overlap is enabled, the engine can use two decoder lanes. The next track
is prepared off the callback thread, then both lane signals are mixed through
the same active DSP graph during the overlap. The control/UI side observes the
transition and updates the queue/current-track state. Format-plugin
materialization is performed by the prefetch worker, not by the callback.

### Bus and Graph Contract

`plugin_sdk/include/isac/plugin/AudioBuffer.h` defines `AudioSample` as
`double`. `AudioBuffer` stores channels in planar contiguous regions, and
each graph buffer carries a frame count and sample rate. The current
playback path uses two channels.

Graph edits occur on the control/UI side. `GraphCompiler`:

1. Validates graph acyclicity using a topological sort.
2. Builds an ordered `ExecutionPlan` with node references and port routes.
3. Allocates each step's input/output buffers before the plan is used by the
   callback.
4. Rejects cyclic graphs by returning no plan.

For each block, routed upstream outputs are summed into the destination input
port, then each node's `process()` method is called in topological order.
Multiple incoming cables to a destination port therefore add together.
Buffers are cleared only for the active frame range.

The active plan is read as a raw pointer by the callback. Its owning
`shared_ptr` stays on the engine/control side. Graph rebuild follows the
engine's device-stop/replacement pattern so an old plan and its nodes cannot
be destroyed while the callback is using them. Do not replace this with
callback-side shared-pointer refcounting or reclamation.

### Real-Time Thread Rules

Code reachable from `AudioEngine::renderAudio()` or
`IAudioNode::process()` must:

- Not allocate/free memory, resize vectors, or construct UI objects.
- Not acquire blocking mutexes, wait on condition variables, or perform file,
  network, database, or device-enumeration work.
- Not call Qt, emit signals, or perform potentially blocking logging.
- Use bounded loops over the current block and validated graph state.
- Sanitize non-finite or invalid parameter values before publishing them.
- Transfer mutable controls through lock-free atomics or another explicitly
  real-time-safe handoff.
- Keep stateful DSP buffers allocated for the maximum supported block/rate
  before processing begins.

The callback checks output samples for non-finite values and tracks
over-range samples. These diagnostics are atomic counters; they do not make
clipping impossible or imply normalization.

### Output and Master Controls

The graph's `Soundcard Output` is a sink. Its input bus contains the sum of
connected graph outputs. Engine-level master Gain is applied after the sink,
along with playback transition envelopes, at the final device-output
boundary. The main volume control must remain independent of any user-created
Gain node.

The device boundary converts processed `double` samples to interleaved
`float`. Sample rate support is constrained by miniaudio, the selected device,
and the desktop sound server; a high internal graph rate alone does not ensure
native DAC playback at that rate.

## 7. DSP Plugin System

### SDK and Discovery

The public DSP API lives under `plugin_sdk/include/isac/plugin/`:

- `IAudioNode`: node identity, ports, parameter setter, and block processor.
- `IAudioPlugin`: plugin identity/description and independent node factory.
- `PluginParameters.h`: optional parameter descriptors for generated inspector
  controls.
- `QtAudioPlugin.h`: optional Qt discovery and UI extension interfaces.
- `PluginApiVersion.h`: stable plugin IID and compatibility version.

DSP-facing interfaces are plain C++ and do not include Qt. Dynamic modules
are discovered from the executable-adjacent `plugins/` directory with
`QPluginLoader`. The host validates the plugin interface and API version.
Plugin IDs must be unique, and every `createNode()` call must return an
independent node instance.

Each in-tree module has its own CMake target under `plugins/<name>/`. Modules
link to `isac_plugin_sdk`; Qt Widgets is needed only for plugin discovery or
optional inspector widgets. Plugin controls run on the UI thread and publish
validated parameters to their node.

### Adding a DSP Plugin

1. Add a focused node implementation and plugin factory in
   `plugins/<name>/`.
2. Give it a dedicated `CMakeLists.txt`, link `isac_plugin_sdk`, and emit the
   module into `${CMAKE_BINARY_DIR}/plugins`.
3. Add the plugin subdirectory and loader-test target path in the root
   `CMakeLists.txt`.
4. Expose parameter metadata or a custom control only when needed. Keep UI
   objects out of the node's `process()` implementation.
5. Sanitize parameters on the control path; use lock-free atomics or
   preallocated state for callback access.
6. Test multiple instances, boundaries, representative sample rates,
   finite output, and loader compatibility. Update `plugins/README.md` if
   behavior or SDK guidance changes.

Breaking changes to the public plugin contract require a compatibility
decision and an API/IID version update.

## 8. Audio-Format Plugin System

The format SDK in `format_sdk/include/isac/format/` is separate from the DSP
SDK. `IAudioFormatPlugin` provides:

- Stable identity, name, description, and extension list.
- `canHandle()` for file selection.
- `inspect()` for library metadata and embedded cover data.
- `materialize()` for creating a decoder-readable playback file.

The host discovers modules from the executable-adjacent
`format_plugins/` directory and checks the format-plugin API version. Format
plugins execute on scanning, control, or prefetch worker threads. They must
never be called from `AudioEngine::renderAudio()`.

The in-tree `isac.ncm` plugin reads NCM metadata/cover data and decrypts the
embedded MP3/FLAC payload to an application-cache playback file when needed.
The original NCM path remains the library identity; conversion is not a
permanent rewrite of the music library. Upcoming format-plugin tracks can be
materialized by `FormatPlaybackPrefetcher`.

### Adding a Format Plugin

1. Implement the format SDK interfaces in `format_plugins/<format>/`.
2. Add a dedicated CMake module target and link `isac_format_sdk`.
3. Add the target to the root build and place its module in
   `${CMAKE_BINARY_DIR}/format_plugins`.
4. Keep scanning/materialization off the audio callback and report failures
   through the existing result/error paths.
5. Test metadata, invalid input, cache generation, and playback with the
   normal decoder. Update `format_plugins/README.md`.

## 9. UI, Themes, and Desktop Integration

`MainWindow` assembles Library, Playlists, DSP Router, playback controls,
visualization, settings, and desktop integration. Page-specific behavior
belongs in its page/widget class; use `MainWindow` for cross-page
coordination.

`ThemeManager` supports system, light, and dark modes. Persist appearance
choices through `QSettings` and update the existing palette/style machinery
instead of introducing competing global styles. Display, playback,
visualization, audio-device, and appearance settings are persisted through
stable namespaced keys.

MPRIS is compiled on Unix-like non-Apple builds when Qt DBus is available;
the current CMake configuration requires Qt DBus on that platform. KDE
Window System support is detected optionally for compositor effects. Such
effects also depend on the active desktop/compositor session. Do not treat
them as portable Qt behavior.

## 10. Build and Test

The root CMake project requires CMake 3.16+, C++20, Qt Widgets/SQL, TagLib,
FFTW3, and `pkg-config`. Unix-like non-Apple builds also require Qt DBus.
KDE Window System support is optional.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

Current CTest targets are:

- `isac_engine_tests`: graph buffers, node/DSP behavior, playback envelopes,
  ring/tap buffers, rates, and engine diagnostics.
- `isac_patchbay_tests`: patchbay interactions and preset handling.
- `isac_smooth_scroll_tests`: smooth-scroll behavior.
- `isac_plugin_loader_tests`: runtime loading and API compatibility for the
  in-tree DSP modules.
- `isac_library_tests`: repository, scanner, metadata properties, category
  normalization, sorting, playlists, queue persistence, and history.

For a UI test without a display, patchbay and smooth-scroll tests configure
Qt's offscreen platform in CTest. Running the application and verifying real
audio output still requires an appropriate desktop/audio session.

## 11. Contributor Checklist

Before submitting a change:

- Preserve existing user data and stable track/playlist identity.
- Keep source lists and plugin target registration explicit in CMake.
- Keep all new UI strings in English and follow the flat rectangular UI style.
- Keep new DSP code allocation-free and non-blocking on the callback.
- Test plugin discovery and executable-adjacent deployment paths when
  changing a plugin.
- Build the affected target, run the relevant CTest suite, and run
  `git diff --check`.
- For audio work, test more than one sample rate, including a high-rate path
  when relevant; inspect underrun/non-finite/over-range behavior.
- For UI work, check navigation, empty states, resize behavior, and
  theme changes.
