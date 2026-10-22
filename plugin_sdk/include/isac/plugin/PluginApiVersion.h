#pragma once

// The IID is part of the runtime discovery contract. Bump it only when the
// plugin ABI changes incompatibly.
#define ISAC_AUDIO_PLUGIN_IID "org.isacplayer.IAudioPlugin/1.0"
#define ISAC_AUDIO_PLUGIN_API_VERSION 1

#define ISAC_AUDIO_PLUGIN_COMPATIBILITY_IID \
    "org.isacplayer.IAudioPluginCompatibility/1.0"

namespace isac {

class IAudioPluginCompatibility {
public:
    virtual ~IAudioPluginCompatibility() = default;
    virtual int pluginApiVersion() const noexcept = 0;
};

} // namespace isac
