#include "DspPresetCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>

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

bool isBuiltInId(const QString& id)
{
    return id == QStringLiteral("stereo") ||
           id == QStringLiteral("swap-channels") ||
           id == QStringLiteral("mono-right");
}

bool isSafeId(const QString& id)
{
    if (id.isEmpty()) {
        return false;
    }
    for (const QChar character : id) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('-') &&
            character != QLatin1Char('_')) {
            return false;
        }
    }
    return true;
}

QJsonObject toJson(const DspPreset& preset)
{
    QJsonArray nodes;
    for (const DspPresetNode& node : preset.nodes) {
        nodes.push_back(QJsonObject{
            {QStringLiteral("role"), node.role},
            {QStringLiteral("plugin"), node.pluginId},
            {QStringLiteral("x"), node.position.x()},
            {QStringLiteral("y"), node.position.y()}
        });
    }

    QJsonArray connections;
    for (const DspPresetConnection& connection : preset.connections) {
        connections.push_back(QJsonObject{
            {QStringLiteral("from"), connection.sourceRole},
            {QStringLiteral("output"), connection.sourcePort},
            {QStringLiteral("to"), connection.destinationRole},
            {QStringLiteral("input"), connection.destinationPort}
        });
    }
    return {
        {QStringLiteral("version"), preset.version},
        {QStringLiteral("id"), preset.id},
        {QStringLiteral("name"), preset.name},
        {QStringLiteral("nodes"), nodes},
        {QStringLiteral("connections"), connections}
    };
}

bool writePreset(const QString& path, const DspPreset& preset,
                 QString& error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    const QByteArray contents =
        QJsonDocument(toJson(preset)).toJson(QJsonDocument::Indented);
    if (file.write(contents) != contents.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

bool validateEditablePreset(const DspPreset& preset, QString& error)
{
    if (preset.version != 2 || preset.builtIn || !isSafeId(preset.id) ||
        preset.name.trimmed().isEmpty()) {
        error = QStringLiteral("invalid custom preset identity");
        return false;
    }
    if (preset.nodes.size() > 256 || preset.connections.size() > 256) {
        error = QStringLiteral("preset exceeds the node or connection limit");
        return false;
    }

    QSet<QString> roles;
    for (const DspPresetNode& node : preset.nodes) {
        const bool endpoint =
            (node.role == QStringLiteral("source") ||
             node.role == QStringLiteral("sink")) &&
            node.pluginId.isEmpty();
        const bool processor = !node.pluginId.trimmed().isEmpty() &&
            node.role.startsWith(node.pluginId + QLatin1Char('#'));
        if (node.role.isEmpty() || (!endpoint && !processor) ||
            !std::isfinite(node.position.x()) ||
            !std::isfinite(node.position.y()) || roles.contains(node.role)) {
            error = QStringLiteral("invalid or duplicate node entry");
            return false;
        }
        roles.insert(node.role);
    }
    if (!roles.contains(QStringLiteral("source")) ||
        !roles.contains(QStringLiteral("sink"))) {
        error = QStringLiteral("preset must contain source and sink nodes");
        return false;
    }

    QSet<QString> connections;
    for (const DspPresetConnection& connection : preset.connections) {
        if (!roles.contains(connection.sourceRole) ||
            !roles.contains(connection.destinationRole) ||
            connection.sourcePort < 0 || connection.destinationPort < 0) {
            error = QStringLiteral("connection refers to an invalid node or port");
            return false;
        }
        const QString key = QStringLiteral("%1:%2>%3:%4")
            .arg(connection.sourceRole)
            .arg(connection.sourcePort)
            .arg(connection.destinationRole)
            .arg(connection.destinationPort);
        if (connections.contains(key)) {
            error = QStringLiteral("duplicate connection");
            return false;
        }
        connections.insert(key);
    }
    return true;
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
    const int version = object.value(QStringLiteral("version")).toInt();
    if ((version != 1 && version != 2) ||
        !object.value(QStringLiteral("id")).isString() ||
        !object.value(QStringLiteral("name")).isString() ||
        !object.value(QStringLiteral("connections")).isArray() ||
        (version == 2 && !object.value(QStringLiteral("nodes")).isArray())) {
        error = QStringLiteral("invalid preset schema");
        return false;
    }

    preset.id = object.value(QStringLiteral("id")).toString().trimmed();
    preset.name = object.value(QStringLiteral("name")).toString().trimmed();
    preset.version = version;
    preset.builtIn = isBuiltInId(preset.id);
    if (!isSafeId(preset.id) || preset.name.isEmpty()) {
        error = QStringLiteral("preset id and name are required");
        return false;
    }

    QSet<QString> nodeRoles;
    if (version == 2) {
        const QJsonArray nodes = object.value(QStringLiteral("nodes")).toArray();
        if (nodes.size() > 256) {
            error = QStringLiteral("preset has too many nodes");
            return false;
        }
        for (const QJsonValue& value : nodes) {
            if (!value.isObject()) {
                error = QStringLiteral("each node must be an object");
                return false;
            }
            const QJsonObject node = value.toObject();
            const QJsonValue x = node.value(QStringLiteral("x"));
            const QJsonValue y = node.value(QStringLiteral("y"));
            DspPresetNode parsed{
                node.value(QStringLiteral("role")).toString().trimmed(),
                node.value(QStringLiteral("plugin")).toString().trimmed(),
                QPointF(x.toDouble(), y.toDouble())
            };
            const bool endpoint =
                (parsed.role == QStringLiteral("source") ||
                 parsed.role == QStringLiteral("sink")) &&
                parsed.pluginId.isEmpty();
            const bool processor = !parsed.pluginId.isEmpty() &&
                parsed.role.startsWith(parsed.pluginId + QLatin1Char('#'));
            if ((!endpoint && !processor) || !x.isDouble() || !y.isDouble() ||
                !std::isfinite(parsed.position.x()) ||
                !std::isfinite(parsed.position.y()) ||
                nodeRoles.contains(parsed.role)) {
                error = QStringLiteral("invalid or duplicate node entry");
                return false;
            }
            nodeRoles.insert(parsed.role);
            preset.nodes.push_back(std::move(parsed));
        }
        if (!nodeRoles.contains(QStringLiteral("source")) ||
            !nodeRoles.contains(QStringLiteral("sink"))) {
            error = QStringLiteral("preset must contain source and sink nodes");
            return false;
        }
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
            parsed.sourcePort < 0 || parsed.destinationPort < 0 ||
            (version == 2 &&
             (!nodeRoles.contains(parsed.sourceRole) ||
              !nodeRoles.contains(parsed.destinationRole)))) {
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

    std::sort(presets.begin(), presets.end(),
              [](const DspPreset& left, const DspPreset& right) {
        const int order =
            QString::compare(left.name, right.name, Qt::CaseInsensitive);
        return order == 0 ? left.id < right.id : order < 0;
    });

    if (error) {
        *error = failures.join(QStringLiteral("; "));
    }
    return presets;
}

bool DspPresetCatalog::save(const QString& directory, DspPreset& preset,
                            QString* error)
{
    preset.version = 2;
    preset.builtIn = false;
    preset.name = preset.name.trimmed();
    if (preset.id.isEmpty()) {
        preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QString validationError;
    if (!validateEditablePreset(preset, validationError)) {
        if (error) {
            *error = validationError;
        }
        return false;
    }
    if (QFile::exists(QDir(directory).filePath(preset.id + ".json"))) {
        if (error) {
            *error = QStringLiteral("preset ID already exists");
        }
        return false;
    }

    const auto existing = load(directory);
    for (const DspPreset& item : existing) {
        if (item.name.compare(preset.name, Qt::CaseInsensitive) == 0) {
            if (error) {
                *error = QStringLiteral("a preset with this name already exists");
            }
            return false;
        }
    }
    if (!QDir().mkpath(directory)) {
        if (error) {
            *error = QStringLiteral("cannot create preset directory");
        }
        return false;
    }
    if (!writePreset(QDir(directory).filePath(preset.id + ".json"),
                     preset, validationError)) {
        if (error) {
            *error = validationError;
        }
        return false;
    }
    return true;
}

bool DspPresetCatalog::rename(const QString& directory, const QString& id,
                              const QString& name, QString* error)
{
    const QString normalizedName = name.trimmed();
    if (!isSafeId(id) || isBuiltInId(id) || normalizedName.isEmpty()) {
        if (error) {
            *error = QStringLiteral("built-in presets cannot be renamed");
        }
        return false;
    }

    const QString path = QDir(directory).filePath(id + ".json");
    DspPreset preset;
    QString operationError;
    if (!readPreset(path, preset, operationError) || preset.builtIn) {
        if (error) {
            *error = operationError.isEmpty()
                ? QStringLiteral("built-in presets cannot be renamed")
                : operationError;
        }
        return false;
    }
    for (const DspPreset& item : load(directory)) {
        if (item.id != id &&
            item.name.compare(normalizedName, Qt::CaseInsensitive) == 0) {
            if (error) {
                *error = QStringLiteral("a preset with this name already exists");
            }
            return false;
        }
    }
    preset.name = normalizedName;
    if (!writePreset(path, preset, operationError)) {
        if (error) {
            *error = operationError;
        }
        return false;
    }
    return true;
}

bool DspPresetCatalog::remove(const QString& directory, const QString& id,
                              QString* error)
{
    if (!isSafeId(id) || isBuiltInId(id)) {
        if (error) {
            *error = QStringLiteral("built-in presets cannot be deleted");
        }
        return false;
    }
    const QString path = QDir(directory).filePath(id + ".json");
    if (!QFile::exists(path) || !QFile::remove(path)) {
        if (error) {
            *error = QStringLiteral("cannot delete preset file");
        }
        return false;
    }
    return true;
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
