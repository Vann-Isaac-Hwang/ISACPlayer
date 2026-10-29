# ISACPlayer Plugins

Runtime plugins are loaded from the `plugins/` directory beside the executable:

```text
ISACPlayer
plugins/
  libReplayGainPlugin.so
```

The in-tree plugins include Stereo Gain, Mono Gain, Mono Delay,
Mono Feedback Delay, Mono Modulated Delay, Stereo Width, Stereo Balance,
Adder, ReplayGain, and Biquad Equalizer. Stereo Width performs a stateless
Mid/Side width transformation: 0 x produces mono, 1 x preserves the original
stereo image, and 2 x doubles the side component. Mono Gain is a single-input,
single-output
linear gain component; it also accepts negative factors for phase inversion. It
does not perform channel mixing, delay, filtering, or spatial processing. Mono
Delay is a single-input, single-output delay with a 100 ms maximum and a
preallocated buffer sized for the engine's 768 kHz maximum sample rate.
Stereo Balance is a linked left/right level control: -1.0 routes to the left
channel, 0.0 preserves both channels, and 1.0 routes to the right channel.
Mono Feedback Delay emits a delayed wet signal and feeds a bounded portion of
that signal back into its preallocated delay line. Mono Modulated Delay uses a
sine-table LFO to vary a fractional delay; its phase parameter can offset
paired instances. The Biquad Equalizer also provides an All-Pass filter mode
for phase rotation without an intended magnitude response change.

The plugin SDK is located at `plugin_sdk/include/isac/plugin/`. DSP plugins
only need to depend on `isac/plugin/IAudioPlugin.h`. The Qt discovery boundary
also includes `isac/plugin/QtAudioPlugin.h`; plugins should export themselves
with `Q_PLUGIN_METADATA` and `Q_INTERFACES(isac::IAudioPlugin)`.

## Minimal CMake Setup

An independent plugin can use the following minimal CMake configuration:

```cmake
add_subdirectory(path/to/ISACPlayer/plugin_sdk)
target_link_libraries(MyPlugin PRIVATE isac_plugin_sdk Qt6::Core)
```

Every call to `createNode()` must return a new
`std::shared_ptr<IAudioNode>`. A node's `process()` method runs on the
real-time audio thread and must not allocate memory, acquire blocking locks,
perform file or network I/O, or call Qt signals and slots. Plugin UI controls
created by `createControlWidget()` must not access Qt objects from the DSP
node.

## ABI and Compatibility

The current plugin ABI is:

```text
org.isacplayer.IAudioPlugin/1.0
```

Plugins must also implement `IAudioPluginCompatibility` and return
`ISAC_AUDIO_PLUGIN_API_VERSION`. The host rejects dynamic libraries that do
not provide this interface or that report an incompatible API version. When
the interface changes incompatibly, the IID and API version must be updated.

The optional `IAudioPluginParameters` interface provides parameter
descriptions, including IDs, display names, units, types, ranges, and default
values. The host can use this metadata to create a basic Inspector. If a
plugin provides a dedicated Qt control widget, that widget takes precedence.

Parameter updates are passed through `IAudioNode::setParameter()`. The node
must validate incoming values and transfer them to the real-time thread using
lock-free state.

The SDK's DSP interfaces do not depend on Qt. Plugin configuration panels
belong to the optional Qt extension layer and must never be accessed from the
real-time `process()` thread.
