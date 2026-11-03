# ISACPlayer Format Plugins

Format plugins are independent from the real-time DSP plugins in `plugins/`.
They inspect source files, expose library metadata, and materialize a
playable cache file when the audio engine needs one.

Runtime format plugins are loaded from the `format_plugins/` directory beside
the executable. The current in-tree plugin is:

- `isac.ncm`: reads NetEase Cloud Music `.ncm` metadata and cover art, then
  reads track/disc tags from the encrypted MP3/FLAC metadata prefix and
  decrypts the embedded audio into an on-demand cache file.

Scanning does not convert the whole library. The original source path remains
the library path, while generated playback and cover files are stored under
the application cache directory.

The public format SDK is in `format_sdk/include/isac/format/`. A format
plugin must implement `IAudioFormatPlugin` and
`IAudioFormatPluginCompatibility`, export `Q_PLUGIN_METADATA`, and report
`FORMAT_PLUGIN_API_VERSION`. The current interface is version 2; it adds the
source album ID and a metadata revision used to refresh previously scanned
format-plugin tracks.

Format plugins run on control or worker threads. They must never be called
from the real-time audio callback.
