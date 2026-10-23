#include "StereoWidthPlugin.h"
#include "StereoWidthNode.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <iterator>

const isac::AudioParameterDescriptor* StereoWidthPlugin::parameterAt(
    std::size_t index) const noexcept
{
    static constexpr isac::AudioParameterDescriptor parameters[] = {
        {"width", "Width", "x", isac::AudioParameterType::Continuous,
         isac::plugins::StereoWidthNode::kMinimumWidth,
         isac::plugins::StereoWidthNode::kMaximumWidth, 1.0}
    };
    return index < std::size(parameters) ? &parameters[index] : nullptr;
}

std::shared_ptr<isac::IAudioNode> StereoWidthPlugin::createNode() const
{
    return std::make_shared<isac::plugins::StereoWidthNode>();
}

QWidget* StereoWidthPlugin::createControlWidget(
    isac::IAudioNode* node, QWidget* parent) const
{
    auto* stereoWidth =
        dynamic_cast<isac::plugins::StereoWidthNode*>(node);
    if (!stereoWidth) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout();
    auto* width = new QDoubleSpinBox(panel);
    width->setRange(
        isac::plugins::StereoWidthNode::kMinimumWidth,
        isac::plugins::StereoWidthNode::kMaximumWidth);
    width->setDecimals(3);
    width->setSingleStep(0.01);
    width->setSuffix(QStringLiteral(" x"));
    width->setValue(stereoWidth->width());
    form->addRow(QStringLiteral("Width"), width);
    layout->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("0 x mono, 1 x original stereo, 2 x widened stereo"),
        panel);
    hint->setStyleSheet(QStringLiteral("color: #9499A6; font-size: 11px;"));
    layout->addWidget(hint);

    QObject::connect(
        width, &QDoubleSpinBox::valueChanged, panel,
        [stereoWidth](double value) { stereoWidth->setWidth(value); });
    return panel;
}
