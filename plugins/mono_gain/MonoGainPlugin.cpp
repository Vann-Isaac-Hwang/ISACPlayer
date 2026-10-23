#include "MonoGainPlugin.h"
#include "MonoGainNode.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <iterator>

const isac::AudioParameterDescriptor* MonoGainPlugin::parameterAt(
    std::size_t index) const noexcept
{
    static constexpr isac::AudioParameterDescriptor parameters[] = {
        {"factor", "Factor", "x", isac::AudioParameterType::Continuous,
         -4.0, 4.0, 1.0}
    };
    return index < std::size(parameters) ? &parameters[index] : nullptr;
}

std::shared_ptr<isac::IAudioNode> MonoGainPlugin::createNode() const
{
    return std::make_shared<isac::plugins::MonoGainNode>();
}

QWidget* MonoGainPlugin::createControlWidget(
    isac::IAudioNode* node, QWidget* parent) const
{
    auto* monoGain = dynamic_cast<isac::plugins::MonoGainNode*>(node);
    if (!monoGain) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout();
    auto* factor = new QDoubleSpinBox(panel);
    factor->setRange(-4.0, 4.0);
    factor->setDecimals(3);
    factor->setSingleStep(0.01);
    factor->setSuffix(QStringLiteral(" x"));
    factor->setValue(monoGain->factor());
    form->addRow(QStringLiteral("Factor"), factor);
    layout->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("Single-channel gain; negative values invert phase"),
        panel);
    hint->setStyleSheet(QStringLiteral("color: #9499A6; font-size: 11px;"));
    layout->addWidget(hint);

    QObject::connect(
        factor, &QDoubleSpinBox::valueChanged, panel,
        [monoGain](double value) { monoGain->setFactor(value); });
    return panel;
}
