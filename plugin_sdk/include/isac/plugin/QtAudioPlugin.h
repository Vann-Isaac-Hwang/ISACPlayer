#pragma once

#include "IAudioPlugin.h"
#include "PluginApiVersion.h"
#include <QtCore/QObject>
#include <QtWidgets/QWidget>

// This header is only for the Qt dynamic-discovery boundary. DSP nodes and
// their processing implementation should include IAudioPlugin.h instead.
Q_DECLARE_INTERFACE(isac::IAudioPlugin, ISAC_AUDIO_PLUGIN_IID)

#define ISAC_AUDIO_PLUGIN_UI_IID "org.isacplayer.IAudioPluginUi/1.0"

namespace isac {

class IAudioPluginUi {
public:
    virtual ~IAudioPluginUi() = default;
    virtual QWidget* createControlWidget(IAudioNode* node, QWidget* parent) const = 0;
};

} // namespace isac

Q_DECLARE_INTERFACE(isac::IAudioPluginUi, ISAC_AUDIO_PLUGIN_UI_IID)
