#include "MonoModulatedDelayPlugin.h"
#include "MonoModulatedDelayNode.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <iterator>

const isac::AudioParameterDescriptor* MonoModulatedDelayPlugin::parameterAt(
    std::size_t index) const noexcept
{
    static constexpr isac::AudioParameterDescriptor parameters[] = {
        {"baseDelayMs", "Base Delay", "ms",
         isac::AudioParameterType::Continuous,
         isac::plugins::MonoModulatedDelayNode::
             kMinimumBaseDelayMilliseconds,
         isac::plugins::MonoModulatedDelayNode::
             kMaximumBaseDelayMilliseconds,
         20.0},
        {"depthMs", "Modulation Depth", "ms",
         isac::AudioParameterType::Continuous,
         0.0,
         isac::plugins::MonoModulatedDelayNode::kMaximumDepthMilliseconds,
         3.0},
        {"rateHz", "LFO Rate", "Hz",
         isac::AudioParameterType::Continuous,
         isac::plugins::MonoModulatedDelayNode::kMinimumRateHz,
         isac::plugins::MonoModulatedDelayNode::kMaximumRateHz,
         0.25},
        {"phaseDeg", "Phase Offset", "deg",
         isac::AudioParameterType::Continuous, 0.0, 360.0, 0.0}
    };
    return index < std::size(parameters) ? &parameters[index] : nullptr;
}

std::shared_ptr<isac::IAudioNode>
MonoModulatedDelayPlugin::createNode() const
{
    return std::make_shared<isac::plugins::MonoModulatedDelayNode>();
}

QWidget* MonoModulatedDelayPlugin::createControlWidget(
    isac::IAudioNode* node, QWidget* parent) const
{
    auto* delayNode =
        dynamic_cast<isac::plugins::MonoModulatedDelayNode*>(node);
    if (!delayNode) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout();
    auto addControl = [panel, form](
                          const QString& label, double minimum,
                          double maximum, int decimals, double step,
                          const QString& suffix, double value,
                          auto setter) {
        auto* control = new QDoubleSpinBox(panel);
        control->setRange(minimum, maximum);
        control->setDecimals(decimals);
        control->setSingleStep(step);
        control->setSuffix(suffix);
        control->setValue(value);
        form->addRow(label, control);
        QObject::connect(
            control, &QDoubleSpinBox::valueChanged, panel,
            [setter](double nextValue) { setter(nextValue); });
    };

    addControl(
        QStringLiteral("Base Delay"),
        isac::plugins::MonoModulatedDelayNode::
            kMinimumBaseDelayMilliseconds,
        isac::plugins::MonoModulatedDelayNode::
            kMaximumBaseDelayMilliseconds,
        2, 0.1, QStringLiteral(" ms"),
        delayNode->baseDelayMilliseconds(),
        [delayNode](double value) {
            delayNode->setBaseDelayMilliseconds(value);
        });
    addControl(
        QStringLiteral("Modulation Depth"), 0.0,
        isac::plugins::MonoModulatedDelayNode::kMaximumDepthMilliseconds,
        2, 0.1, QStringLiteral(" ms"),
        delayNode->depthMilliseconds(),
        [delayNode](double value) {
            delayNode->setDepthMilliseconds(value);
        });
    addControl(
        QStringLiteral("LFO Rate"),
        isac::plugins::MonoModulatedDelayNode::kMinimumRateHz,
        isac::plugins::MonoModulatedDelayNode::kMaximumRateHz,
        2, 0.05, QStringLiteral(" Hz"), delayNode->rateHz(),
        [delayNode](double value) { delayNode->setRateHz(value); });
    addControl(
        QStringLiteral("Phase Offset"), 0.0, 360.0, 1, 1.0,
        QStringLiteral(" deg"), delayNode->phaseDegrees(),
        [delayNode](double value) {
            delayNode->setPhaseDegrees(value);
        });
    layout->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("Wet-only output; phase offset helps spread paired instances"),
        panel);
    hint->setStyleSheet(QStringLiteral("color: #9499A6; font-size: 11px;"));
    layout->addWidget(hint);
    return panel;
}
