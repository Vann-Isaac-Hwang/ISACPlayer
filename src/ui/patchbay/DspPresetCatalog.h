#pragma once

#include "engine/graph/AudioEngine.h"

#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>

namespace isac::ui {

struct DspPresetConnection {
    QString sourceRole;
    int sourcePort = -1;
    QString destinationRole;
    int destinationPort = -1;

    bool operator==(const DspPresetConnection&) const = default;
};

struct DspPresetNode {
    QString role;
    QString pluginId;
    QPointF position;

    bool operator==(const DspPresetNode&) const = default;
};

struct DspPreset {
    QString id;
    QString name;
    int version = 1;
    bool builtIn = false;
    QVector<DspPresetNode> nodes;
    QVector<DspPresetConnection> connections;
};

class DspPresetCatalog {
public:
    static bool installBuiltIns(const QString& directory, QString* error = nullptr);
    static QVector<DspPreset> load(const QString& directory, QString* error = nullptr);
    static bool save(const QString& directory, DspPreset& preset,
                     QString* error = nullptr);
    static bool rename(const QString& directory, const QString& id,
                       const QString& name, QString* error = nullptr);
    static bool remove(const QString& directory, const QString& id,
                       QString* error = nullptr);
    static bool apply(const DspPreset& preset, AudioEngine& engine,
                      const QHash<QString, NodeId>& roleNodes,
                      QString* error = nullptr);
};

} // namespace isac::ui
