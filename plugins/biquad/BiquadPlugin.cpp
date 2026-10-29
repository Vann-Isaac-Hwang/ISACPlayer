#include "BiquadPlugin.h"
#include "BiquadNode.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

void addTypeItems(QComboBox* combo)
{
    combo->addItem("Bypass", static_cast<int>(isac::plugins::BiquadType::Bypass));
    combo->addItem("Low Shelf", static_cast<int>(isac::plugins::BiquadType::LowShelf));
    combo->addItem("High Shelf", static_cast<int>(isac::plugins::BiquadType::HighShelf));
    combo->addItem("Peak", static_cast<int>(isac::plugins::BiquadType::Peak));
    combo->addItem("Notch", static_cast<int>(isac::plugins::BiquadType::Notch));
    combo->addItem("All-Pass", static_cast<int>(isac::plugins::BiquadType::AllPass));
}

int typeIndex(isac::plugins::BiquadType type)
{
    switch (type) {
    case isac::plugins::BiquadType::Bypass: return 0;
    case isac::plugins::BiquadType::LowShelf: return 1;
    case isac::plugins::BiquadType::HighShelf: return 2;
    case isac::plugins::BiquadType::Peak: return 3;
    case isac::plugins::BiquadType::Notch: return 4;
    case isac::plugins::BiquadType::AllPass: return 5;
    }
    return 0;
}

} // namespace

std::shared_ptr<isac::IAudioNode> BiquadPlugin::createNode() const
{
    return std::make_shared<isac::plugins::BiquadNode>();
}

QWidget* BiquadPlugin::createControlWidget(isac::IAudioNode* node,
                                            QWidget* parent) const
{
    auto* equalizer = dynamic_cast<isac::plugins::BiquadNode*>(node);
    if (!equalizer) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* root = new QVBoxLayout(panel);
    root->setContentsMargins(0, 0, 0, 0);

    auto* bypass = new QCheckBox("Bypass", panel);
    bypass->setChecked(equalizer->bypassed());
    root->addWidget(bypass);
    QObject::connect(bypass, &QCheckBox::toggled, panel,
                     [equalizer](bool value) { equalizer->setBypassed(value); });

    auto* bands = new QWidget(panel);
    auto* bandsLayout = new QVBoxLayout(bands);
    bandsLayout->setContentsMargins(0, 0, 0, 0);
    bandsLayout->setSpacing(6);

    struct Controls {
        QComboBox* type;
        QDoubleSpinBox* frequency;
        QDoubleSpinBox* gain;
        QDoubleSpinBox* q;
    };

    for (int band = 0; band < isac::plugins::BiquadNode::kBandCount; ++band) {
        auto* group = new QGroupBox(QString("Band %1").arg(band + 1), bands);
        auto* form = new QFormLayout(group);
        auto* type = new QComboBox(group);
        auto* frequency = new QDoubleSpinBox(group);
        auto* gain = new QDoubleSpinBox(group);
        auto* q = new QDoubleSpinBox(group);

        addTypeItems(type);
        frequency->setRange(1.0, 24000.0);
        frequency->setDecimals(1);
        frequency->setSuffix(" Hz");
        gain->setRange(-60.0, 60.0);
        gain->setDecimals(1);
        gain->setSuffix(" dB");
        q->setRange(0.001, 100.0);
        q->setDecimals(3);
        q->setSingleStep(0.1);

        type->setCurrentIndex(typeIndex(equalizer->bandType(band)));
        frequency->setValue(equalizer->bandFrequency(band));
        gain->setValue(equalizer->bandGainDb(band));
        q->setValue(equalizer->bandQ(band));
        const auto selectedType = equalizer->bandType(band);
        gain->setEnabled(
            selectedType == isac::plugins::BiquadType::LowShelf ||
            selectedType == isac::plugins::BiquadType::HighShelf ||
            selectedType == isac::plugins::BiquadType::Peak);

        form->addRow("Type", type);
        form->addRow("Frequency", frequency);
        form->addRow("Gain", gain);
        form->addRow("Q", q);
        bandsLayout->addWidget(group);

        QObject::connect(type, &QComboBox::currentIndexChanged, panel,
                         [equalizer, type, gain, band](int index) {
                             const auto nextType =
                                 static_cast<isac::plugins::BiquadType>(
                                     type->itemData(index).toInt());
                             equalizer->setBandType(band, nextType);
                             gain->setEnabled(
                                 nextType ==
                                     isac::plugins::BiquadType::LowShelf ||
                                 nextType ==
                                     isac::plugins::BiquadType::HighShelf ||
                                 nextType ==
                                     isac::plugins::BiquadType::Peak);
                         });
        QObject::connect(frequency, &QDoubleSpinBox::valueChanged, panel,
                         [equalizer, band](double value) {
                             equalizer->setBandFrequency(band, value);
                         });
        QObject::connect(gain, &QDoubleSpinBox::valueChanged, panel,
                         [equalizer, band](double value) {
                             equalizer->setBandGainDb(band, value);
                         });
        QObject::connect(q, &QDoubleSpinBox::valueChanged, panel,
                         [equalizer, band](double value) {
                             equalizer->setBandQ(band, value);
                         });
    }
    bandsLayout->addStretch();

    auto* scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(bands);
    root->addWidget(scroll, 1);
    return panel;
}
