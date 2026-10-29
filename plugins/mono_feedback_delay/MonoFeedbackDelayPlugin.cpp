#include "MonoFeedbackDelayPlugin.h"
#include "MonoFeedbackDelayNode.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <iterator>

const isac::AudioParameterDescriptor* MonoFeedbackDelayPlugin::parameterAt(
    std::size_t index) const noexcept
{
    static constexpr isac::AudioParameterDescriptor parameters[] = {
        {"delayMs", "Delay", "ms", isac::AudioParameterType::Continuous,
         isac::plugins::MonoFeedbackDelayNode::kMinimumDelayMilliseconds,
         isac::plugins::MonoFeedbackDelayNode::kMaximumDelayMilliseconds,
         20.0},
        {"feedback", "Feedback", "", isac::AudioParameterType::Continuous,
         -isac::plugins::MonoFeedbackDelayNode::kMaximumFeedback,
         isac::plugins::MonoFeedbackDelayNode::kMaximumFeedback, 0.35}
    };
    return index < std::size(parameters) ? &parameters[index] : nullptr;
}

std::shared_ptr<isac::IAudioNode>
MonoFeedbackDelayPlugin::createNode() const
{
    return std::make_shared<isac::plugins::MonoFeedbackDelayNode>();
}

QWidget* MonoFeedbackDelayPlugin::createControlWidget(
    isac::IAudioNode* node, QWidget* parent) const
{
    auto* delayNode =
        dynamic_cast<isac::plugins::MonoFeedbackDelayNode*>(node);
    if (!delayNode) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout();
    auto* delay = new QDoubleSpinBox(panel);
    delay->setRange(
        isac::plugins::MonoFeedbackDelayNode::kMinimumDelayMilliseconds,
        isac::plugins::MonoFeedbackDelayNode::kMaximumDelayMilliseconds);
    delay->setDecimals(2);
    delay->setSingleStep(0.1);
    delay->setSuffix(QStringLiteral(" ms"));
    delay->setValue(delayNode->delayMilliseconds());

    auto* feedback = new QDoubleSpinBox(panel);
    feedback->setRange(
        -isac::plugins::MonoFeedbackDelayNode::kMaximumFeedback,
        isac::plugins::MonoFeedbackDelayNode::kMaximumFeedback);
    feedback->setDecimals(2);
    feedback->setSingleStep(0.01);
    feedback->setValue(delayNode->feedback());

    form->addRow(QStringLiteral("Delay"), delay);
    form->addRow(QStringLiteral("Feedback"), feedback);
    layout->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("Wet-only output; feedback is limited to ±0.95"),
        panel);
    hint->setStyleSheet(QStringLiteral("color: #9499A6; font-size: 11px;"));
    layout->addWidget(hint);

    QObject::connect(
        delay, &QDoubleSpinBox::valueChanged, panel,
        [delayNode](double value) {
            delayNode->setDelayMilliseconds(value);
        });
    QObject::connect(
        feedback, &QDoubleSpinBox::valueChanged, panel,
        [delayNode](double value) { delayNode->setFeedback(value); });
    return panel;
}
