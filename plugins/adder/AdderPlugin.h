#pragma once

#include <isac/plugin/QtAudioPlugin.h>
#include <QObject>

class AdderPlugin final : public QObject,
                          public isac::IAudioPlugin,
                          public isac::IAudioPluginUi,
                          public isac::IAudioPluginCompatibility {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ISAC_AUDIO_PLUGIN_IID)
    Q_INTERFACES(isac::IAudioPlugin isac::IAudioPluginUi
                 isac::IAudioPluginCompatibility)

public:
    const char* pluginId() const noexcept override { return "isac.add"; }
    const char* displayName() const noexcept override { return "Adder"; }
    const char* description() const noexcept override {
        return "Two-input linear mixer with floating-point headroom";
    }
    int pluginApiVersion() const noexcept override {
        return ISAC_AUDIO_PLUGIN_API_VERSION;
    }

    std::shared_ptr<isac::IAudioNode> createNode() const override;
    QWidget* createControlWidget(isac::IAudioNode* node,
                                  QWidget* parent) const override;
};
