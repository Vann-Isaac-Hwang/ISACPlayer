#include "HrtfSpatializerPlugin.h"
#include "HrtfSpatializerNode.h"

#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <iterator>

const isac::AudioParameterDescriptor* HrtfSpatializerPlugin::parameterAt(
    std::size_t index) const noexcept
{
    static constexpr isac::AudioParameterDescriptor parameters[] = {
        {"azimuthDeg", "Scene Azimuth", "deg",
         isac::AudioParameterType::Continuous,
         isac::plugins::HrtfSpatializerNode::kMinimumAzimuthDegrees,
         isac::plugins::HrtfSpatializerNode::kMaximumAzimuthDegrees, 0.0},
        {"elevationDeg", "Scene Elevation", "deg",
         isac::AudioParameterType::Continuous,
         isac::plugins::HrtfSpatializerNode::kMinimumElevationDegrees,
         isac::plugins::HrtfSpatializerNode::kMaximumElevationDegrees, 0.0},
        {"speakerAngleDeg", "Speaker Angle", "deg",
         isac::AudioParameterType::Continuous,
         isac::plugins::HrtfSpatializerNode::kMinimumSpeakerAngleDegrees,
         isac::plugins::HrtfSpatializerNode::kMaximumSpeakerAngleDegrees,
         60.0},
        {"wet", "Wet", "%",
         isac::AudioParameterType::Continuous,
         isac::plugins::HrtfSpatializerNode::kMinimumWet,
         isac::plugins::HrtfSpatializerNode::kMaximumWet, 1.0}
    };
    return index < std::size(parameters) ? &parameters[index] : nullptr;
}

std::shared_ptr<isac::IAudioNode> HrtfSpatializerPlugin::createNode() const
{
    return std::make_shared<isac::plugins::HrtfSpatializerNode>();
}

QWidget* HrtfSpatializerPlugin::createControlWidget(
    isac::IAudioNode* node, QWidget* parent) const
{
    auto* spatializer =
        dynamic_cast<isac::plugins::HrtfSpatializerNode*>(node);
    if (!spatializer) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* sofaRow = new QHBoxLayout();
    auto* pathEdit = new QLineEdit(panel);
    pathEdit->setReadOnly(true);
    pathEdit->setPlaceholderText(QStringLiteral("Select a SOFA file"));
    auto* browse = new QPushButton(QStringLiteral("Browse..."), panel);
    sofaRow->addWidget(pathEdit, 1);
    sofaRow->addWidget(browse);
    layout->addLayout(sofaRow);

    auto* form = new QFormLayout();
    auto addControl = [panel, form, spatializer](
                          const QString& label, double minimum,
                          double maximum, int decimals, double step,
                          const QString& suffix, double value,
                          bool refresh, auto setter) {
        auto* control = new QDoubleSpinBox(panel);
        control->setRange(minimum, maximum);
        control->setDecimals(decimals);
        control->setSingleStep(step);
        control->setSuffix(suffix);
        control->setValue(value);
        form->addRow(label, control);
        QObject::connect(
            control, &QDoubleSpinBox::valueChanged, panel,
            [spatializer, refresh, setter](double nextValue) {
                setter(nextValue);
                if (refresh) {
                    spatializer->refreshFilters();
                }
            });
    };

    addControl(
        QStringLiteral("Scene Azimuth"),
        isac::plugins::HrtfSpatializerNode::kMinimumAzimuthDegrees,
        isac::plugins::HrtfSpatializerNode::kMaximumAzimuthDegrees,
        1, 1.0, QStringLiteral(" deg"), spatializer->azimuthDegrees(),
        true,
        [spatializer](double value) {
            spatializer->setAzimuthDegrees(value);
        });
    addControl(
        QStringLiteral("Scene Elevation"),
        isac::plugins::HrtfSpatializerNode::kMinimumElevationDegrees,
        isac::plugins::HrtfSpatializerNode::kMaximumElevationDegrees,
        1, 1.0, QStringLiteral(" deg"), spatializer->elevationDegrees(),
        true,
        [spatializer](double value) {
            spatializer->setElevationDegrees(value);
        });
    addControl(
        QStringLiteral("Speaker Angle"),
        isac::plugins::HrtfSpatializerNode::kMinimumSpeakerAngleDegrees,
        isac::plugins::HrtfSpatializerNode::kMaximumSpeakerAngleDegrees,
        1, 1.0, QStringLiteral(" deg"),
        spatializer->speakerAngleDegrees(),
        true,
        [spatializer](double value) {
            spatializer->setSpeakerAngleDegrees(value);
        });
    addControl(
        QStringLiteral("Wet"), isac::plugins::HrtfSpatializerNode::kMinimumWet,
        isac::plugins::HrtfSpatializerNode::kMaximumWet, 2, 0.01,
        QStringLiteral(""), spatializer->wet(),
        false,
        [spatializer](double value) { spatializer->setWet(value); });
    layout->addLayout(form);

    auto* status = new QLabel(
        QStringLiteral("Select a measured SOFA HRTF file. Without one, audio "
                       "passes through unchanged."),
        panel);
    status->setWordWrap(true);
    status->setStyleSheet(QStringLiteral("color: #9499A6; font-size: 11px;"));
    layout->addWidget(status);

    QObject::connect(
        browse, &QPushButton::clicked, panel,
        [panel, spatializer, pathEdit, status] {
            const QString path = QFileDialog::getOpenFileName(
                panel, QStringLiteral("Select SOFA HRTF"),
                QString(), QStringLiteral("SOFA files (*.sofa);;All files (*)"));
            if (path.isEmpty()) {
                return;
            }
            pathEdit->setText(path);
            if (spatializer->setSofaPath(path.toStdString())) {
                status->setText(QStringLiteral("SOFA HRTF loaded."));
            } else {
                status->setText(QStringLiteral(
                    "Unable to load this SOFA file at the current sample rate."));
            }
        });

    return panel;
}
