#pragma once

#include <isac/plugin/QtAudioPlugin.h>
#include <QObject>

class GainPlugin final : public QObject,
                         public isac::IAudioPlugin,
                         public isac::IAudioPluginUi {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ISAC_AUDIO_PLUGIN_IID)
    Q_INTERFACES(isac::IAudioPlugin isac::IAudioPluginUi)

public:
    const char* pluginId() const noexcept override { return "isac.gain"; }
    const char* displayName() const noexcept override { return "Stereo Gain"; }
    const char* description() const noexcept override {
        return "64-bit stereo gain with an atomic control parameter";
    }

    std::shared_ptr<isac::IAudioNode> createNode() const override;
    QWidget* createControlWidget(isac::IAudioNode* node,
                                  QWidget* parent) const override;
};
