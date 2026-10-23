#include "StereoBalancePlugin.h"
#include "StereoBalanceNode.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <iterator>

const isac::AudioParameterDescriptor* StereoBalancePlugin::parameterAt(
    std::size_t index) const noexcept
{
    static constexpr isac::AudioParameterDescriptor parameters[] = {
        {"balance", "Balance", "", isac::AudioParameterType::Continuous,
         isac::plugins::StereoBalanceNode::kMinimumBalance,
         isac::plugins::StereoBalanceNode::kMaximumBalance, 0.0}
    };
    return index < std::size(parameters) ? &parameters[index] : nullptr;
}

std::shared_ptr<isac::IAudioNode> StereoBalancePlugin::createNode() const
{
    return std::make_shared<isac::plugins::StereoBalanceNode>();
}

QWidget* StereoBalancePlugin::createControlWidget(
    isac::IAudioNode* node, QWidget* parent) const
{
    auto* balanceNode =
        dynamic_cast<isac::plugins::StereoBalanceNode*>(node);
    if (!balanceNode) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout();
    auto* balance = new QDoubleSpinBox(panel);
    balance->setRange(
        isac::plugins::StereoBalanceNode::kMinimumBalance,
        isac::plugins::StereoBalanceNode::kMaximumBalance);
    balance->setDecimals(3);
    balance->setSingleStep(0.01);
    balance->setValue(balanceNode->balance());
    form->addRow(QStringLiteral("Balance"), balance);
    layout->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("-1.0 = left, 0.0 = center, 1.0 = right"),
        panel);
    hint->setStyleSheet(QStringLiteral("color: #9499A6; font-size: 11px;"));
    layout->addWidget(hint);

    QObject::connect(
        balance, &QDoubleSpinBox::valueChanged, panel,
        [balanceNode](double value) { balanceNode->setBalance(value); });
    return panel;
}
