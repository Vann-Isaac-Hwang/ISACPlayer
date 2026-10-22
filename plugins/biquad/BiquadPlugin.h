#pragma once

#include <isac/plugin/QtAudioPlugin.h>
#include <QObject>

class BiquadPlugin final : public QObject,
                           public isac::IAudioPlugin,
                           public isac::IAudioPluginUi,
                           public isac::IAudioPluginCompatibility {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ISAC_AUDIO_PLUGIN_IID)
    Q_INTERFACES(isac::IAudioPlugin isac::IAudioPluginUi
                 isac::IAudioPluginCompatibility)

public:
    const char* pluginId() const noexcept override { return "isac.biquad"; }
    const char* displayName() const noexcept override {
        return "Biquad Equalizer (10 Band)";
    }
    const char* description() const noexcept override {
        return "Ten-band RBJ parametric equalizer on a 64-bit stereo bus";
    }
    int pluginApiVersion() const noexcept override {
        return ISAC_AUDIO_PLUGIN_API_VERSION;
    }

    std::shared_ptr<isac::IAudioNode> createNode() const override;
    QWidget* createControlWidget(isac::IAudioNode* node,
                                  QWidget* parent) const override;
};
