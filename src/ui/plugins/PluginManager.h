#pragma once

#include <isac/plugin/QtAudioPlugin.h>
#include <QPluginLoader>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

namespace isac::ui {

struct PluginInfo {
    QString id;
    QString displayName;
    QString description;
    struct Parameter {
        QString id;
        QString displayName;
        QString unit;
        isac::AudioParameterType type = isac::AudioParameterType::Continuous;
        double minimum = 0.0;
        double maximum = 1.0;
        double defaultValue = 0.0;
    };
    std::vector<Parameter> parameters;
    int apiVersion = 0;
};

class PluginManager {
public:
    explicit PluginManager(const QString& pluginDirectory = {});

    void scanDirectory(const QString& directoryPath);
    const std::vector<PluginInfo>& plugins() const { return m_plugins; }
    std::shared_ptr<IAudioNode> createNode(const QString& pluginId) const;
    QString pluginIdForNode(
        const std::shared_ptr<IAudioNode>& node) const;
    QWidget* createControlWidget(const std::shared_ptr<IAudioNode>& node,
                                 QWidget* parent) const;

private:
    using NodeFactory = std::function<std::shared_ptr<IAudioNode>()>;

    struct Record {
        PluginInfo info;
        NodeFactory factory;
        IAudioPlugin* plugin = nullptr;
        IAudioPluginUi* pluginUi = nullptr;
        IAudioPluginParameters* parameters = nullptr;
    };

    struct NodeBinding {
        std::weak_ptr<IAudioNode> node;
        QString pluginId;
        IAudioPluginUi* pluginUi = nullptr;
        IAudioPluginParameters* parameters = nullptr;
    };

    void registerPlugin(PluginInfo info, NodeFactory factory,
                        IAudioPlugin* plugin = nullptr,
                        IAudioPluginUi* pluginUi = nullptr,
                        IAudioPluginParameters* parameters = nullptr);
    static QWidget* createSchemaControlWidget(
        IAudioPluginParameters* parameters,
        const std::shared_ptr<IAudioNode>& node, QWidget* parent);

    std::vector<PluginInfo> m_plugins;
    std::vector<Record> m_records;
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders;
    mutable std::vector<NodeBinding> m_nodeBindings;
};

} // namespace isac::ui
