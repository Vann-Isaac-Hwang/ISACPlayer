#pragma once

#include "engine/graph/IAudioNode.h"
#include <QWidget>
#include <memory>

class QLabel;
class QVBoxLayout;
class QWidget;

namespace isac::ui {

class NodeInspectorWidget final : public QWidget {
public:
    explicit NodeInspectorWidget(QWidget* parent = nullptr);

    void setNode(const std::shared_ptr<IAudioNode>& node,
                 QWidget* externalControl = nullptr);
    void clearNode();

private:
    QLabel* m_nodeTitle = nullptr;
    QLabel* m_emptyState = nullptr;
    QVBoxLayout* m_rootLayout = nullptr;
    QWidget* m_externalControl = nullptr;
};

} // namespace isac::ui
