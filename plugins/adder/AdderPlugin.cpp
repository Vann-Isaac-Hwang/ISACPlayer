#include "AdderPlugin.h"
#include "AdderNode.h"

#include <QLabel>
#include <QVBoxLayout>

std::shared_ptr<isac::IAudioNode> AdderPlugin::createNode() const
{
    return std::make_shared<isac::plugins::AdderNode>();
}

QWidget* AdderPlugin::createControlWidget(isac::IAudioNode* node,
                                           QWidget* parent) const
{
    if (!dynamic_cast<isac::plugins::AdderNode*>(node)) {
        return nullptr;
    }

    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* hint = new QLabel("Linear A + B; peaks above 0 dBFS are preserved", panel);
    hint->setStyleSheet("color: #9499A6; font-size: 11px;");
    layout->addWidget(hint);
    return panel;
}
