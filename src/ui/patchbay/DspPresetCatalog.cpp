#include "DspPresetCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace isac::ui {
namespace {

QStringList builtInResourceNames()
{
    return {
        QStringLiteral("stereo.json"),
        QStringLiteral("swap-channels.json"),
        QStringLiteral("mono-right.json")
    };
}

bool readPreset(const QString& path, DspPreset& preset, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        error = parseError.error != QJsonParseError::NoError
            ? parseError.errorString()
            : QStringLiteral("root must be a JSON object");
        return false;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toInt() != 1 ||
        !object.value(QStringLiteral("id")).isString() ||
        !object.value(QStringLiteral("name")).isString() ||
        !object.value(QStringLiteral("connections")).isArray()) {
        error = QStringLiteral("invalid preset schema");
        return false;
    }

    preset.id = object.value(QStringLiteral("id")).toString().trimmed();
    preset.name = object.value(QStringLiteral("name")).toString().trimmed();
    if (preset.id.isEmpty() || preset.name.isEmpty()) {
        error = QStringLiteral("preset id and name are required");
        return false;
    }

    const QJsonArray connections =
        object.value(QStringLiteral("connections")).toArray();
    if (connections.size() > 256) {
        error = QStringLiteral("preset has too many connections");
        return false;
    }

    QSet<QString> uniqueConnections;
    for (const QJsonValue& value : connections) {
        if (!value.isObject()) {
            error = QStringLiteral("each connection must be an object");
            return false;
        }

        const QJsonObject connection = value.toObject();
        const QJsonValue sourcePort =
            connection.value(QStringLiteral("output"));
        const QJsonValue destinationPort =
            connection.value(QStringLiteral("input"));
        DspPresetConnection parsed{
            connection.value(QStringLiteral("from")).toString().trimmed(),
            sourcePort.toInt(-1),
            connection.value(QStringLiteral("to")).toString().trimmed(),
            destinationPort.toInt(-1)
        };
        if (parsed.sourceRole.isEmpty() ||
            parsed.destinationRole.isEmpty() ||
            !sourcePort.isDouble() || !destinationPort.isDouble() ||
            parsed.sourcePort < 0 || parsed.destinationPort < 0) {
            error = QStringLiteral("connection endpoints and ports are required");
            return false;
        }

        const QString key = QStringLiteral("%1:%2>%3:%4")
            .arg(parsed.sourceRole)
            .arg(parsed.sourcePort)
            .arg(parsed.destinationRole)
            .arg(parsed.destinationPort);
        if (uniqueConnections.contains(key)) {
            error = QStringLiteral("duplicate connection");
            return false;
        }
        uniqueConnections.insert(key);
        preset.connections.push_back(std::move(parsed));
    }
    return true;
}

} // namespace

bool DspPresetCatalog::installBuiltIns(const QString& directory, QString* error)
{
    QDir targetDirectory;
    if (!targetDirectory.mkpath(directory)) {
        if (error) {
            *error = QStringLiteral("cannot create preset directory: %1")
                         .arg(directory);
        }
        return false;
    }

    for (const QString& name : builtInResourceNames()) {
        const QString targetPath = QDir(directory).filePath(name);
        if (QFile::exists(targetPath)) {
            continue;
        }

        QFile resource(QStringLiteral(":/dsp-presets/%1").arg(name));
        if (!resource.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = QStringLiteral("built-in preset is missing: %1").arg(name);
            }
            return false;
        }

        QSaveFile target(targetPath);
        if (!target.open(QIODevice::WriteOnly) ||
            target.write(resource.readAll()) < 0 ||
            !target.commit()) {
            if (error) {
                *error = QStringLiteral("cannot install preset: %1").arg(targetPath);
            }
            return false;
        }
    }
    return true;
}

QVector<DspPreset> DspPresetCatalog::load(const QString& directory,
                                         QString* error)
{
    QVector<DspPreset> presets;
    const QFileInfoList files = QDir(directory).entryInfoList(
        {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QStringList failures;
    QSet<QString> ids;

    for (const QFileInfo& info : files) {
        DspPreset preset;
        QString parseError;
        if (!readPreset(info.absoluteFilePath(), preset, parseError)) {
            failures.push_back(QStringLiteral("%1: %2")
                                   .arg(info.fileName(), parseError));
            continue;
        }
        if (ids.contains(preset.id)) {
            failures.push_back(QStringLiteral("%1: duplicate preset id")
                                   .arg(info.fileName()));
            continue;
        }
        ids.insert(preset.id);
        presets.push_back(std::move(preset));
    }

    if (error) {
        *error = failures.join(QStringLiteral("; "));
    }
    return presets;
}

bool DspPresetCatalog::apply(const DspPreset& preset, AudioEngine& engine,
                             const QHash<QString, NodeId>& roleNodes,
                             QString* error)
{
    QVector<Connection> validated;
    validated.reserve(preset.connections.size());
    for (const DspPresetConnection& connection : preset.connections) {
        if (!roleNodes.contains(connection.sourceRole) ||
            !roleNodes.contains(connection.destinationRole)) {
            if (error) {
                *error = QStringLiteral("preset requires an unavailable node");
            }
            return false;
        }

        const NodeId sourceId = roleNodes.value(connection.sourceRole);
        const NodeId destinationId = roleNodes.value(connection.destinationRole);
        if (!engine.hasNode(sourceId) || !engine.hasNode(destinationId)) {
            if (error) {
                *error = QStringLiteral("preset requires a node that is not on the canvas");
            }
            return false;
        }

        const auto& sourceNode = engine.nodes().at(sourceId);
        const auto& destinationNode = engine.nodes().at(destinationId);
        if (connection.sourcePort >= sourceNode->numOutputs() ||
            connection.destinationPort >= destinationNode->numInputs()) {
            if (error) {
                *error = QStringLiteral("preset port does not match the available node");
            }
            return false;
        }
        validated.push_back({
            sourceId, connection.sourcePort,
            destinationId, connection.destinationPort
        });
    }

    const std::vector<Connection> previous = engine.connections();
    engine.clearConnections();
    for (const Connection& connection : validated) {
        engine.connect(connection.srcNode, connection.srcPort,
                       connection.dstNode, connection.dstPort);
    }
    if (engine.rebuildGraph()) {
        return true;
    }

    engine.clearConnections();
    for (const Connection& connection : previous) {
        engine.connect(connection.srcNode, connection.srcPort,
                       connection.dstNode, connection.dstPort);
    }
    engine.rebuildGraph();
    if (error) {
        *error = QStringLiteral("preset creates an invalid audio graph");
    }
    return false;
}

} // namespace isac::ui
