#pragma once

#include <isac/plugin/QtAudioPlugin.h>
#include <QObject>

class MonoDelayPlugin final : public QObject,
                              public isac::IAudioPlugin,
                              public isac::IAudioPluginUi,
                              public isac::IAudioPluginParameters,
                              public isac::IAudioPluginCompatibility {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ISAC_AUDIO_PLUGIN_IID)
    Q_INTERFACES(isac::IAudioPlugin isac::IAudioPluginUi
                 isac::IAudioPluginParameters isac::IAudioPluginCompatibility)

public:
    const char* pluginId() const noexcept override
    {
        return "isac.mono_delay";
    }

    const char* displayName() const noexcept override
    {
        return "Mono Delay";
    }

    const char* description() const noexcept override
    {
        return "Single-channel delay with a preallocated real-time buffer";
    }

    int pluginApiVersion() const noexcept override
    {
        return ISAC_AUDIO_PLUGIN_API_VERSION;
    }

    std::size_t parameterCount() const noexcept override { return 1; }

    const isac::AudioParameterDescriptor* parameterAt(
        std::size_t index) const noexcept override;

    std::shared_ptr<isac::IAudioNode> createNode() const override;

    QWidget* createControlWidget(isac::IAudioNode* node,
                                 QWidget* parent) const override;
};
