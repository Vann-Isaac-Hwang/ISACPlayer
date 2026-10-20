#pragma once

#include "engine/graph/AudioEngine.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace isac::ui {

struct DspPresetConnection {
    QString sourceRole;
    int sourcePort = -1;
    QString destinationRole;
    int destinationPort = -1;
};

struct DspPreset {
    QString id;
    QString name;
    QVector<DspPresetConnection> connections;
};

class DspPresetCatalog {
public:
    static bool installBuiltIns(const QString& directory, QString* error = nullptr);
    static QVector<DspPreset> load(const QString& directory, QString* error = nullptr);
    static bool apply(const DspPreset& preset, AudioEngine& engine,
                      const QHash<QString, NodeId>& roleNodes,
                      QString* error = nullptr);
};

} // namespace isac::ui
