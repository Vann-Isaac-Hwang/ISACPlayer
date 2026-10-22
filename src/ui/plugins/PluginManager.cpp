#include "PluginManager.h"

#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QCheckBox>
#include <algorithm>
#include <cmath>

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
                                   IAudioPluginUi* pluginUi,
                                   IAudioPluginParameters* parameters)
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
        pluginUi,
        parameters
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

        auto* compatibility =
            qobject_cast<IAudioPluginCompatibility*>(instance);
        if (!compatibility ||
            compatibility->pluginApiVersion() != ISAC_AUDIO_PLUGIN_API_VERSION) {
            qCWarning(pluginLog)
                << "Ignoring incompatible audio plugin:" << file.filePath();
            continue;
        }

        PluginInfo info{
            QString::fromUtf8(plugin->pluginId()),
            QString::fromUtf8(plugin->displayName()),
            QString::fromUtf8(plugin->description()),
            {},
            compatibility->pluginApiVersion()
        };
        auto* pluginPtr = plugin;
        auto* pluginUi = qobject_cast<IAudioPluginUi*>(instance);
        auto* parameters = qobject_cast<IAudioPluginParameters*>(instance);
        if (parameters) {
            for (std::size_t index = 0;
                 index < parameters->parameterCount(); ++index) {
                const auto* descriptor = parameters->parameterAt(index);
                if (!descriptor || !descriptor->id ||
                    !descriptor->displayName ||
                    !std::isfinite(descriptor->minimum) ||
                    !std::isfinite(descriptor->maximum) ||
                    !std::isfinite(descriptor->defaultValue) ||
                    descriptor->minimum > descriptor->maximum ||
                    descriptor->defaultValue < descriptor->minimum ||
                    descriptor->defaultValue > descriptor->maximum) {
                    continue;
                }
                info.parameters.push_back({
                    QString::fromUtf8(descriptor->id),
                    QString::fromUtf8(descriptor->displayName),
                    QString::fromUtf8(descriptor->unit ? descriptor->unit : ""),
                    descriptor->type,
                    descriptor->minimum,
                    descriptor->maximum,
                    descriptor->defaultValue
                });
            }
        }
        registerPlugin(
            info,
            [pluginPtr] { return pluginPtr->createNode(); },
            pluginPtr,
            pluginUi,
            parameters);
        m_loaders.push_back(std::move(loader));
    }
}

std::shared_ptr<IAudioNode> PluginManager::createNode(const QString& pluginId) const
{
    for (const auto& record : m_records) {
        if (record.info.id == pluginId) {
            auto node = record.factory ? record.factory() : nullptr;
            if (node && (record.pluginUi || record.parameters)) {
                m_nodeBindings.push_back(
                    {node, record.pluginUi, record.parameters});
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
        if (!boundNode || boundNode.get() != node.get()) {
            continue;
        }
        if (binding.pluginUi) {
            return binding.pluginUi->createControlWidget(node.get(), parent);
        }
        if (binding.parameters) {
            return createSchemaControlWidget(binding.parameters, node, parent);
        }
    }
    return nullptr;
}

QWidget* PluginManager::createSchemaControlWidget(
    IAudioPluginParameters* parameters,
    const std::shared_ptr<IAudioNode>& node, QWidget* parent)
{
    if (!parameters || !node || parameters->parameterCount() == 0) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* form = new QFormLayout(panel);
    for (std::size_t index = 0;
         index < parameters->parameterCount(); ++index) {
        const auto* descriptor = parameters->parameterAt(index);
        if (!descriptor || !descriptor->id || !descriptor->displayName ||
            !std::isfinite(descriptor->minimum) ||
            !std::isfinite(descriptor->maximum) ||
            !std::isfinite(descriptor->defaultValue) ||
            descriptor->minimum > descriptor->maximum ||
            descriptor->defaultValue < descriptor->minimum ||
            descriptor->defaultValue > descriptor->maximum) {
            continue;
        }

        if (descriptor->type == AudioParameterType::Boolean) {
            auto* control = new QCheckBox(panel);
            control->setChecked(descriptor->defaultValue >= 0.5);
            QObject::connect(control, &QCheckBox::toggled, panel,
                             [node, id = descriptor->id](bool value) {
                                 node->setParameter(
                                     id, value ? 1.0 : 0.0);
                             });
            form->addRow(QString::fromUtf8(descriptor->displayName), control);
        } else {
            auto* control = new QDoubleSpinBox(panel);
            control->setRange(descriptor->minimum, descriptor->maximum);
            control->setValue(descriptor->defaultValue);
            if (descriptor->unit && *descriptor->unit) {
                control->setSuffix(
                    QStringLiteral(" ") + QString::fromUtf8(descriptor->unit));
            }
            QObject::connect(control,
                             qOverload<double>(&QDoubleSpinBox::valueChanged),
                             panel,
                             [node, id = descriptor->id](double value) {
                                 node->setParameter(id, value);
                             });
            form->addRow(QString::fromUtf8(descriptor->displayName), control);
        }
    }
    return panel;
}

} // namespace isac::ui
