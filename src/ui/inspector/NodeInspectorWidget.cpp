#include "NodeInspectorWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace isac::ui {

NodeInspectorWidget::NodeInspectorWidget(QWidget* parent)
    : QWidget(parent)
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(10, 10, 10, 10);
    m_rootLayout->setSpacing(8);

    m_nodeTitle = new QLabel("未选择模块", this);
    m_nodeTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: white;");
    m_emptyState = new QLabel("选择画布中的 DSP 模块查看参数", this);
    m_emptyState->setWordWrap(true);
    m_emptyState->setStyleSheet("color: #9499A6;");

    auto* header = new QHBoxLayout();
    header->addWidget(m_nodeTitle);
    header->addStretch();
    m_rootLayout->addLayout(header);
    m_rootLayout->addWidget(m_emptyState);
}

void NodeInspectorWidget::setNode(const std::shared_ptr<IAudioNode>& node,
                                  QWidget* externalControl)
{
    if (m_externalControl) {
        m_rootLayout->removeWidget(m_externalControl);
        delete m_externalControl;
        m_externalControl = nullptr;
    }

    m_nodeTitle->setText(node ? QString::fromStdString(node->name()) : "未选择模块");
    m_externalControl = externalControl;
    if (m_externalControl) {
        m_rootLayout->insertWidget(1, m_externalControl, 1);
    }

    m_emptyState->setVisible(!node || !m_externalControl);
    m_emptyState->setText(!node ? "选择画布中的 DSP 模块查看参数"
                                : "当前模块暂无可用的 Inspector");
}

void NodeInspectorWidget::clearNode()
{
    setNode(nullptr);
}

} // namespace isac::ui
