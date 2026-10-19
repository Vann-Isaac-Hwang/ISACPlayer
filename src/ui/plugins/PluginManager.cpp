#include "PluginManager.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <algorithm>

namespace {
Q_LOGGING_CATEGORY(pluginLog, "isac.plugins")
}

namespace isac::ui {

PluginManager::PluginManager(const QString& pluginDirectory)
{
    if (!pluginDirectory.isEmpty()) {
        scanDirectory(pluginDirectory);
    }
}

void PluginManager::registerPlugin(PluginInfo info, NodeFactory factory,
                                   IAudioPlugin* plugin,
                                   IAudioPluginUi* pluginUi)
{
    for (const auto& existing : m_plugins) {
        if (existing.id == info.id) {
            qCWarning(pluginLog) << "Duplicate plugin id ignored:" << info.id;
            return;
        }
    }

    m_records.push_back({
        std::move(info),
        std::move(factory),
        plugin,
        pluginUi
    });
    m_plugins.push_back(m_records.back().info);
}

void PluginManager::scanDirectory(const QString& directoryPath)
{
    QDir directory(directoryPath);
    if (!directory.exists()) {
        return;
    }

    const QStringList patterns{"*.so", "*.dll", "*.dylib"};
    const QFileInfoList files = directory.entryInfoList(
        patterns, QDir::Files | QDir::Readable, QDir::Name);

    for (const QFileInfo& file : files) {
        auto loader = std::make_unique<QPluginLoader>(file.absoluteFilePath());
        QObject* instance = loader->instance();
        auto* plugin = qobject_cast<IAudioPlugin*>(instance);
        if (!plugin) {
            qCWarning(pluginLog) << "Ignoring invalid audio plugin:" << file.filePath()
                                  << loader->errorString();
            continue;
        }

        const PluginInfo info{
            QString::fromUtf8(plugin->pluginId()),
            QString::fromUtf8(plugin->displayName()),
            QString::fromUtf8(plugin->description())
        };
        auto* pluginPtr = plugin;
        auto* pluginUi = qobject_cast<IAudioPluginUi*>(instance);
        registerPlugin(
            info,
            [pluginPtr] { return pluginPtr->createNode(); },
            pluginPtr,
            pluginUi);
        m_loaders.push_back(std::move(loader));
    }
}

std::shared_ptr<IAudioNode> PluginManager::createNode(const QString& pluginId) const
{
    for (const auto& record : m_records) {
        if (record.info.id == pluginId) {
            auto node = record.factory ? record.factory() : nullptr;
            if (node && record.pluginUi) {
                m_nodeBindings.push_back({node, record.pluginUi});
            }
            return node;
        }
    }
    return nullptr;
}

QWidget* PluginManager::createControlWidget(
    const std::shared_ptr<IAudioNode>& node, QWidget* parent) const
{
    if (!node) {
        return nullptr;
    }

    m_nodeBindings.erase(
        std::remove_if(
            m_nodeBindings.begin(),
            m_nodeBindings.end(),
            [](const NodeBinding& binding) { return binding.node.expired(); }),
        m_nodeBindings.end());

    for (const auto& binding : m_nodeBindings) {
        const auto boundNode = binding.node.lock();
        if (boundNode && boundNode.get() == node.get() && binding.pluginUi) {
            return binding.pluginUi->createControlWidget(node.get(), parent);
        }
    }
    return nullptr;
}

} // namespace isac::ui
