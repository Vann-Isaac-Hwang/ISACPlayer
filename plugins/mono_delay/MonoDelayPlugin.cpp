#include "MonoDelayPlugin.h"
#include "MonoDelayNode.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <iterator>

const isac::AudioParameterDescriptor* MonoDelayPlugin::parameterAt(
    std::size_t index) const noexcept
{
    static constexpr isac::AudioParameterDescriptor parameters[] = {
        {"delayMs", "Delay", "ms", isac::AudioParameterType::Continuous,
         0.0, isac::plugins::MonoDelayNode::kMaximumDelayMilliseconds, 0.0}
    };
    return index < std::size(parameters) ? &parameters[index] : nullptr;
}

std::shared_ptr<isac::IAudioNode> MonoDelayPlugin::createNode() const
{
    return std::make_shared<isac::plugins::MonoDelayNode>();
}

QWidget* MonoDelayPlugin::createControlWidget(
    isac::IAudioNode* node, QWidget* parent) const
{
    auto* monoDelay = dynamic_cast<isac::plugins::MonoDelayNode*>(node);
    if (!monoDelay) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout();
    auto* delay = new QDoubleSpinBox(panel);
    delay->setRange(0.0,
                    isac::plugins::MonoDelayNode::kMaximumDelayMilliseconds);
    delay->setDecimals(3);
    delay->setSingleStep(0.1);
    delay->setSuffix(QStringLiteral(" ms"));
    delay->setValue(monoDelay->delayMilliseconds());
    form->addRow(QStringLiteral("Delay"), delay);
    layout->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("100 ms maximum; delay changes are smoothed"),
        panel);
    hint->setStyleSheet(QStringLiteral("color: #9499A6; font-size: 11px;"));
    layout->addWidget(hint);

    QObject::connect(
        delay, &QDoubleSpinBox::valueChanged, panel,
        [monoDelay](double value) {
            monoDelay->setDelayMilliseconds(value);
        });
    return panel;
}
