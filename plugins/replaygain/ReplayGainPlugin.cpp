#include "ReplayGainPlugin.h"
#include "ReplayGainNode.h"
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

std::shared_ptr<isac::IAudioNode> ReplayGainPlugin::createNode() const {
    return std::make_shared<isac::plugins::ReplayGainNode>();
}

QWidget* ReplayGainPlugin::createControlWidget(
    isac::IAudioNode* node, QWidget* parent) const
{
    auto* replayGain = dynamic_cast<isac::plugins::ReplayGainNode*>(node);
    if (!replayGain) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* form = new QFormLayout();
    auto* gain = new QDoubleSpinBox(panel);
    gain->setRange(-24.0, 12.0);
    gain->setDecimals(1);
    gain->setSingleStep(0.5);
    gain->setSuffix(" dB");
    gain->setValue(replayGain->gainDb());
    form->addRow("Target gain", gain);
    layout->addLayout(form);

    auto* hint = new QLabel("External plugin / atomic smoothing", panel);
    hint->setStyleSheet("color: #9499A6; font-size: 11px;");
    layout->addWidget(hint);

    QObject::connect(gain, &QDoubleSpinBox::valueChanged,
                     panel, [replayGain](double value) {
                         replayGain->setGainDb(value);
                     });
    return panel;
}
