#include "GainPlugin.h"
#include "GainNode.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <iterator>

const isac::AudioParameterDescriptor* GainPlugin::parameterAt(
    std::size_t index) const noexcept
{
    static constexpr isac::AudioParameterDescriptor parameters[] = {
        {"factor", "Gain", "x", isac::AudioParameterType::Continuous,
         0.0, 4.0, 1.0}
    };
    return index < std::size(parameters) ? &parameters[index] : nullptr;
}

std::shared_ptr<isac::IAudioNode> GainPlugin::createNode() const
{
    return std::make_shared<isac::plugins::GainNode>();
}

QWidget* GainPlugin::createControlWidget(isac::IAudioNode* node,
                                          QWidget* parent) const
{
    auto* gainNode = dynamic_cast<isac::plugins::GainNode*>(node);
    if (!gainNode) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout();
    auto* factor = new QDoubleSpinBox(panel);
    factor->setRange(0.0, 4.0);
    factor->setDecimals(3);
    factor->setSingleStep(0.01);
    factor->setSuffix(" x");
    factor->setValue(gainNode->factor());
    form->addRow("Gain", factor);
    layout->addLayout(form);

    auto* hint = new QLabel("Atomic control parameter", panel);
    hint->setStyleSheet("color: #9499A6; font-size: 11px;");
    layout->addWidget(hint);

    QObject::connect(factor, &QDoubleSpinBox::valueChanged,
                     panel, [gainNode](double value) {
                         gainNode->setFactor(value);
                     });
    return panel;
}
