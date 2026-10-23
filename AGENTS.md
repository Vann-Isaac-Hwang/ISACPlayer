# ISACPlayer Agent Guide

## Project Overview

ISACPlayer is a C++20 Qt desktop music player for local music libraries. It
combines:

- A SQLite-backed library and playlist manager.
- Background TagLib metadata scanning.
- A real-time DSP graph with a DAG topology.
- 64-bit floating-point planar stereo audio buses.
- A Qt patchbay for visual node and cable editing.
- Dynamically loaded DSP plugins.
- Linux MPRIS and KDE media integration.

The application UI is English. Keep all new visible strings in English.

## Repository Structure

- `mainwindow.cpp`, `mainwindow.h`, `mainwindow.ui`
  - Application composition root.
  - Creates the main navigation, settings dialog, playback header, library
    pages, DSP Router, docks, timers, and top-level signal connections.
  - Keep new domain logic out of this file when it belongs in an existing
    subsystem.
- `src/engine/graph/`
  - Qt-free audio engine, graph compiler, execution plan, audio buffers,
    decoder/output coordination, and real-time tap/ring buffers.
- `src/engine/nodes/`
  - Legacy or built-in node implementations kept for engine-level use and
    tests.
- `src/library/`
  - SQLite repository, library types, TagLib metadata handling, scanner, and
    playback queue behavior.
- `src/ui/isac_music/`
  - Library, playlist, now-playing, playback queue, and related Qt Widgets.
- `src/ui/patchbay/`
  - Graphical DSP Router, node/cable interaction, and routing preset files.
- `src/ui/plugins/`
  - Runtime plugin discovery, plugin metadata, node bindings, and inspector
    control creation.
- `src/ui/player/`
  - Waveform and FFT waterfall visualization widgets.
- `src/ui/widgets/`
  - Reusable interaction widgets such as hover buttons and sliders.
- `src/platform/mpris/`
  - Linux MPRIS and D-Bus integration. This is compiled conditionally on
    Unix-like platforms where Qt DBus is available.
- `plugin_sdk/include/isac/plugin/`
  - Public SDK headers for external DSP plugins. This interface is deliberately
    independent of Qt implementation details on the DSP side.
- `plugins/<name>/`
  - In-tree dynamically loaded DSP plugins. Each plugin has its own CMake
    target and is emitted into the build `plugins/` directory.
- `config/`
  - Qt resources, desktop integration metadata, and built-in DSP presets.
- `tests/`
  - Engine, patchbay, plugin-loader, and library/repository tests.
- `third_party/miniaudio/`
  - Vendored miniaudio header used by the audio output backend.

## Build Dependencies

The root `CMakeLists.txt` uses:

- CMake 3.16 or newer.
- A C++20 compiler.
- Qt 5 or Qt 6 with Widgets and SQL.
- Qt DBus on Linux/Unix builds.
- TagLib development files discoverable through `pkg-config`.
- FFTW3 development files discoverable through `pkg-config`.

The main targets are:

- `isac_engine`: static, Qt-free audio engine library.
- `ISACPlayer`: Qt application and UI.
- `isac_plugin_sdk`: header-only CMake interface target.
- `isac_gain`, `isac_adder`, `isac_replaygain`, `isac_biquad`,
  `isac_mono_gain`, `isac_mono_delay`, `isac_stereo_width`: module plugins.
- `isac_engine_tests`, `isac_patchbay_tests`,
  `isac_plugin_loader_tests`, `isac_library_tests`: CTest targets.

Configure and build from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
```

Run the tests:

```sh
ctest --test-dir build --output-on-failure
```

For a headless environment, patchbay tests already configure
`QT_QPA_PLATFORM=offscreen` through CTest. The application itself requires a
working Qt desktop session for normal UI/audio testing.

## Audio Engine Rules

The audio callback is a hard real-time path. Code reached from
`AudioEngine::renderAudio()` or `IAudioNode::process()` must:

- Avoid heap allocation, deallocation, blocking locks, condition variables,
  file/network I/O, database access, logging with uncertain blocking behavior,
  Qt calls, and signal/slot operations.
- Avoid unbounded loops and operations whose cost depends on external state.
- Use preallocated buffers sized for the engine limits.
- Use atomics for control-thread parameter handoff. Prefer lock-free atomics
  and validate/sanitize values before publishing them.
- Preserve finite sample values and handle invalid or out-of-range parameters
  defensively.

The DSP bus contract is:

- Planar `double` samples.
- Two channels for the current playback path.
- The graph keeps the requested native sample rate in its buffers.
- Conversion to the device's interleaved `float` output occurs at the final
  audio boundary.
- The UI volume slider controls the engine-level master gain at that final
  boundary. It must not depend on a particular Gain plugin instance.

Graph editing and recompilation happen on the control/UI side. Do not replace
the active execution plan in a way that can destroy objects while the callback
is using them. Follow the existing `AudioEngine::rebuildGraph()` ownership and
device-stop/start pattern.

Decoder file I/O belongs on the decoder thread. UI visualization may consume
audio tap data on its timer, but visualization configuration must not add work
or synchronization to the audio callback.

## Plugin Rules

Every plugin must:

- Be added under `plugins/<name>/` with a dedicated `CMakeLists.txt`.
- Link against `isac_plugin_sdk` and only add Qt dependencies for optional UI
  controls or Qt plugin discovery.
- Export a stable plugin identity and compatible API version.
- Return a new independent `std::shared_ptr<IAudioNode>` from every
  `createNode()` call.
- Keep DSP processing free from Qt, allocation, locks, I/O, and UI state.
- Validate all parameters and transfer them to the real-time path through
  lock-free state.
- Keep plugin UI controls on the control/UI thread and never access Qt objects
  from a DSP node.

When adding or changing a public plugin interface, update the API version/IID
when compatibility is broken and update the plugin documentation. Keep the SDK
headers minimal and framework-independent.

Runtime plugin libraries are loaded from a `plugins/` directory beside the
executable. Do not assume a development build's source-tree plugin directory
is the runtime directory.

## Library and Database Rules

`LibraryRepository` owns a SQLite connection and is confined to the thread
where it was created. Do not move it across threads or use its connection from
the audio callback. `LibraryScanner` performs scanning work on its worker
thread and communicates with the UI through Qt signals.

For schema or query changes:

- Preserve existing data where possible.
- Keep upsert behavior idempotent.
- Maintain stable track IDs and playlist ordering.
- Treat artist separators and album aggregation consistently with the
  repository's existing normalization rules.
- Add or update repository tests for persistence, search, sorting, playlist
  behavior, and migration-sensitive behavior.

## Qt and UI Style

Follow the existing Qt Widgets approach:

- Use parent ownership for widgets and Qt signals/slots for UI events.
- Prefer existing local widgets, repository APIs, plugin metadata, and
  resource files over new parallel abstractions.
- Use `QStringLiteral` for fixed strings in C++.
- Keep controls and layout behavior explicit and responsive; avoid hidden
  geometry assumptions.
- Keep page-specific behavior in its page class. Keep `MainWindow` focused on
  composition and cross-page coordination.
- Use existing resource paths and Qt resource files for icons and application
  assets.
- Keep UI text English and use tooltips for unfamiliar icon-only controls.
- Preserve the current flat, rectangular visual language unless the task
  explicitly changes it.

Settings are persisted through `QSettings`. Use stable, namespaced keys such
as `playback/...`, `display/...`, `visualization/...`, or `audio/...`, provide
bounded defaults, and apply accepted settings immediately when the existing
UI supports it.

## Code Style

The codebase uses:

- C++20 language features.
- Four-space indentation.
- Braces on the same line as declarations/control statements.
- `PascalCase` for types and `camelCase` for functions and local variables.
- `m_` prefixes for member fields.
- `std::unique_ptr`, `std::shared_ptr`, `std::optional`, STL containers, and
  Qt parent ownership according to lifetime needs.
- `noexcept` on small real-time-safe accessors and parameter setters where
  appropriate.
- Small comments for architectural constraints or non-obvious algorithms.

Do not perform unrelated formatting or broad refactors while changing a
feature. Use ASCII for new source and documentation unless a file clearly
requires another character set.

## Testing and Verification

Before finishing a code change:

1. Build the affected configuration.
2. Run `ctest --test-dir <build-dir> --output-on-failure`.
3. Run `git diff --check`.
4. For audio changes, inspect real-time safety and test multiple sample rates,
   including high-rate paths where relevant.
5. For plugin changes, run the plugin loader test and verify the generated
   module is copied or available beside the executable.
6. For UI changes, test both the relevant page and navigation/lifecycle
   transitions, including empty states and resize behavior.

When adding source files, update the appropriate source list in
`CMakeLists.txt` or the relevant plugin CMake file. Do not rely on an IDE
project file noticing new files automatically.

Do not commit, push, delete generated files, or rewrite unrelated user changes
unless the user explicitly requests it.
