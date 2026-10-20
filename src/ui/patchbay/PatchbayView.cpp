#include "PatchbayView.h"
#include <QPainter>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QMenu>
#include <algorithm>
#include <cmath>

namespace isac::ui {

// ==================== VisualPortItem ====================
VisualPortItem::VisualPortItem(VisualNodeItem* parentNode, bool isOutput, PortIndex index, const QString& name)
    : QGraphicsEllipseItem(-6, -6, 12, 12, parentNode)
    , m_nodeItem(parentNode)
    , m_isOutput(isOutput)
    , m_portIndex(index)
{
    setAcceptHoverEvents(true);
    setBrush(isOutput ? QColor(240, 140, 50) : QColor(70, 160, 245));
    setPen(QPen(QColor(25, 25, 30), 2.0));

    // 绘制端口名称标签
    auto* label = new QGraphicsSimpleTextItem(name, this);
    label->setBrush(QColor(210, 215, 225));
    QFont f = label->font();
    f.setPointSize(9);
    label->setFont(f);

    QRectF br = label->boundingRect();
    if (isOutput) {
        label->setPos(-br.width() - 10, -br.height() / 2.0);
    } else {
        label->setPos(10, -br.height() / 2.0);
    }
}

NodeId VisualPortItem::nodeId() const {
    return m_nodeItem->audioNode()->id();
}

void VisualPortItem::removeCable(VisualCableItem* cable) {
    m_cables.erase(std::remove(m_cables.begin(), m_cables.end(), cable), m_cables.end());
}

void VisualPortItem::updateCables() {
    for (auto* c : m_cables) {
        c->updatePath();
    }
}

void VisualPortItem::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    setRect(-8, -8, 16, 16); // 鼠标悬停放大针脚
}

void VisualPortItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    setRect(-6, -6, 12, 12);
}

// ==================== VisualCableItem ====================
VisualCableItem::VisualCableItem(VisualPortItem* startPort, VisualPortItem* endPort)
    : m_startPort(startPort), m_endPort(endPort)
{
    setZValue(-1); // 连线置于节点下方
    if (m_startPort) m_startPort->addCable(this);
    if (m_endPort)   m_endPort->addCable(this);
    updatePath();
}

VisualCableItem::~VisualCableItem() {
    if (m_startPort) m_startPort->removeCable(this);
    if (m_endPort)   m_endPort->removeCable(this);
}

void VisualCableItem::setEndPort(VisualPortItem* port) {
    m_endPort = port;
    if (m_endPort) m_endPort->addCable(this);
    updatePath();
}

void VisualCableItem::updatePath(const QPointF& tempEndPos) {
    if (!m_startPort) return;

    QPointF p1 = m_startPort->centerScenePos();
    QPointF p2 = m_endPort ? m_endPort->centerScenePos() : tempEndPos;

    // 若从输入端口反向拖拽，交换起点终点保证弧度自然
    if (!m_startPort->isOutput()) {
        std::swap(p1, p2);
    }

    QPainterPath path(p1);
    qreal dx = std::max(std::abs(p2.x() - p1.x()) * 0.55, 40.0);
    path.cubicTo(QPointF(p1.x() + dx, p1.y()), QPointF(p2.x() - dx, p2.y()), p2);
    setPath(path);

    if (m_endPort) {
        setPen(QPen(QColor(95, 185, 255, 220), 3.0, Qt::SolidLine, Qt::RoundCap));
    } else {
        setPen(QPen(QColor(255, 190, 90, 200), 2.5, Qt::DashLine, Qt::RoundCap));
    }
}

// ==================== VisualNodeItem ====================
VisualNodeItem::VisualNodeItem(std::shared_ptr<IAudioNode> audioNode)
    : m_audioNode(std::move(audioNode))
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);

    int maxPorts = std::max(m_audioNode->numInputs(), m_audioNode->numOutputs());
    m_height = 42.0 + std::max(1, maxPorts) * 28.0;

    for (int i = 0; i < m_audioNode->numInputs(); ++i) {
        auto* port = new VisualPortItem(
            this, false, i, QString::fromStdString(m_audioNode->inputName(i)));
        port->setPos(0, 48.0 + i * 26.0);
        m_inputs.push_back(port);
    }

    for (int i = 0; i < m_audioNode->numOutputs(); ++i) {
        auto* port = new VisualPortItem(
            this, true, i, QString::fromStdString(m_audioNode->outputName(i)));
        port->setPos(m_width, 48.0 + i * 26.0);
        m_outputs.push_back(port);
    }
}

QRectF VisualNodeItem::boundingRect() const {
    return QRectF(0, 0, m_width, m_height);
}

void VisualNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // 1. 节点主体暗黑半透明背景
    QPainterPath bodyPath;
    bodyPath.addRoundedRect(boundingRect(), 8, 8);
    painter->fillPath(bodyPath, QColor(38, 41, 48, 240));

    // 2. 顶部彩色标题栏 (区分源节点、处理算子与声卡输出)
    QPainterPath headerPath;
    headerPath.addRoundedRect(QRectF(0, 0, m_width, 30), 8, 8);
    QColor headerColor(60, 105, 165); // 默认算子蓝色
    if (m_audioNode->numInputs() == 0)  headerColor = QColor(45, 140, 95);  // 源节点绿色
    if (m_audioNode->numOutputs() == 0) headerColor = QColor(165, 65, 80);  // 声卡输出红色
    painter->fillPath(headerPath, headerColor);

    // 3. 边框高亮
    QPen borderPen(isSelected() ? QColor(120, 195, 255) : QColor(70, 75, 88), isSelected() ? 2.0 : 1.2);
    painter->setPen(borderPen);
    painter->drawPath(bodyPath);

    // 4. 绘制节点标题
    painter->setPen(Qt::white);
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setPointSize(9);
    painter->setFont(titleFont);
    painter->drawText(QRectF(10, 0, m_width - 20, 30), Qt::AlignVCenter | Qt::AlignLeft,
                      QString::fromStdString(m_audioNode->name()));
}

QVariant VisualNodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged) {
        for (auto* p : m_inputs)  p->updateCables();
        for (auto* p : m_outputs) p->updateCables();
    }
    return QGraphicsObject::itemChange(change, value);
}

// ==================== PatchbayScene ====================
PatchbayScene::PatchbayScene(AudioEngine& engine, QObject* parent)
    : QGraphicsScene(parent), m_engine(engine)
{
    setSceneRect(-1000, -1000, 2000, 2000);
    syncFromEngine();
}

void PatchbayScene::syncFromEngine() {
    if (m_tempCable) {
        removeItem(m_tempCable);
        delete m_tempCable;
        m_tempCable = nullptr;
    }
    for (auto* cable : m_cableItems) {
        removeItem(cable);
        delete cable;
    }
    m_cableItems.clear();
    m_tempCable = nullptr;
    clear();
    m_nodeItems.clear();

    // 1. 生成所有节点方块并按稳定的 NodeId 排布，避免多个实例重叠。
    std::vector<std::pair<NodeId, std::shared_ptr<IAudioNode>>> orderedNodes;
    orderedNodes.reserve(m_engine.nodes().size());
    for (const auto& entry : m_engine.nodes()) {
        orderedNodes.push_back(entry);
    }
    std::sort(orderedNodes.begin(), orderedNodes.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    int processingNodeIndex = 0;
    for (const auto& [id, node] : orderedNodes) {
        auto* item = new VisualNodeItem(node);
        addItem(item);
        m_nodeItems[id] = item;

        if (node->numInputs() == 0) {
            item->setPos(-320, -60); // 音频源放左边
        } else if (node->numOutputs() == 0) {
            item->setPos(220, -60);  // 声卡输出放右边
        } else {
            item->setPos(-50, -150 + processingNodeIndex * 145);
            ++processingNodeIndex;
        }
    }

    // 2. 根据引擎当前拓扑生成所有贝塞尔连线
    for (const auto& conn : m_engine.connections()) {
        if (m_nodeItems.count(conn.srcNode) && m_nodeItems.count(conn.dstNode)) {
            auto* srcPort = m_nodeItems[conn.srcNode]->outputPort(conn.srcPort);
            auto* dstPort = m_nodeItems[conn.dstNode]->inputPort(conn.dstPort);
            auto* cable = new VisualCableItem(srcPort, dstPort);
            addItem(cable);
            m_cableItems.push_back(cable);
        }
    }
}

void PatchbayScene::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor(24, 26, 31)); // 深灰背景

    const int gridSize = 25;
    qreal left = int(rect.left()) - (int(rect.left()) % gridSize);
    qreal top  = int(rect.top())  - (int(rect.top())  % gridSize);

    painter->setPen(QPen(QColor(46, 50, 60), 1.5));
    for (qreal x = left; x < rect.right(); x += gridSize) {
        for (qreal y = top; y < rect.bottom(); y += gridSize) {
            painter->drawPoint(QPointF(x, y));
        }
    }
}

VisualPortItem* PatchbayScene::portAt(const QPointF& pos) const {
    const auto itemsAtPos = items(QRectF(pos - QPointF(8, 8), QSizeF(16, 16)));
    for (auto* it : itemsAtPos) {
        if (auto* port = dynamic_cast<VisualPortItem*>(it)) {
            return port;
        }
    }
    return nullptr;
}

void PatchbayScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (auto* port = portAt(event->scenePos())) {
            m_tempCable = new VisualCableItem(port, nullptr);
            m_tempCable->updatePath(event->scenePos());
            addItem(m_tempCable);
            event->accept();
            return;
        }
    } else if (event->button() == Qt::RightButton) {
        // 右键点击连线：直接拆除该连线并实时通知引擎热更新
        const auto clickedItems = items(QRectF(event->scenePos() - QPointF(5, 5), QSizeF(10, 10)));
        for (auto* it : clickedItems) {
            if (auto* cable = dynamic_cast<VisualCableItem*>(it)) {
                auto* s = cable->startPort();
                auto* e = cable->endPort();
                if (s && e) {
                    m_engine.disconnect(s->nodeId(), s->portIndex(), e->nodeId(), e->portIndex());
                    m_engine.rebuildGraph();
                    emit topologyChanged("已断开连线");
                }
                m_cableItems.erase(std::remove(m_cableItems.begin(), m_cableItems.end(), cable), m_cableItems.end());
                removeItem(cable);
                delete cable;
                event->accept();
                return;
            }
        }

        for (auto* it : clickedItems) {
            auto* nodeItem = dynamic_cast<VisualNodeItem*>(it);
            if (!nodeItem) {
                continue;
            }

            QMenu menu;
            QAction* deleteAction = menu.addAction("删除模块");
            QAction* selectedAction = menu.exec(QCursor::pos());
            if (selectedAction == deleteAction) {
                const NodeId nodeId = nodeItem->audioNode()->id();
                const QString nodeName =
                    QString::fromStdString(nodeItem->audioNode()->name());
                if (!m_engine.removeNode(nodeId)) {
                    emit topologyChanged("无法删除：Source/Sink 是引擎固定端点");
                } else if (!m_engine.rebuildGraph()) {
                    emit topologyChanged("模块已删除，但新的拓扑编译失败");
                } else {
                    syncFromEngine();
                    emit topologyChanged(QString("已删除模块: %1")
                                             .arg(nodeName));
                }
            }
            event->accept();
            return;
        }
    }
    QGraphicsScene::mousePressEvent(event);
}

void PatchbayScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_tempCable) {
        m_tempCable->updatePath(event->scenePos());
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void PatchbayScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_tempCable && event->button() == Qt::LeftButton) {
        VisualPortItem* startP = m_tempCable->startPort();
        VisualPortItem* endP   = portAt(event->scenePos());

        // 校验：必须是 1 个输出端口 + 1 个输入端口，且不能是同一个节点
        if (endP && startP != endP && startP->isOutput() != endP->isOutput() &&
            startP->nodeId() != endP->nodeId())
        {
            VisualPortItem* outPort = startP->isOutput() ? startP : endP;
            VisualPortItem* inPort  = startP->isOutput() ? endP   : startP;

            m_engine.connect(outPort->nodeId(), outPort->portIndex(),
                             inPort->nodeId(),  inPort->portIndex());

            // 编译拓扑图：如果检测到死循环环路 (Cycle)，自动回滚拒绝连接！
            if (m_engine.rebuildGraph()) {
                delete m_tempCable;
                m_tempCable = nullptr;

                auto* finalCable = new VisualCableItem(outPort, inPort);
                addItem(finalCable);
                m_cableItems.push_back(finalCable);

                emit topologyChanged(QString("已连接: %1 [%2] -> %3 [%4]")
                    .arg(QString::fromStdString(outPort->nodeItem()->audioNode()->name()))
                    .arg(outPort->portIndex())
                    .arg(QString::fromStdString(inPort->nodeItem()->audioNode()->name()))
                    .arg(inPort->portIndex()));
                event->accept();
                return;
            } else {
                // 回滚非法环路连接
                m_engine.disconnect(outPort->nodeId(), outPort->portIndex(),
                                    inPort->nodeId(),  inPort->portIndex());
                emit topologyChanged("拒绝连接：检测到死循环环路 (Cycle)！");
            }
        }

        removeItem(m_tempCable);
        delete m_tempCable;
        m_tempCable = nullptr;
        event->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
}

// ==================== PatchbayView ====================
PatchbayView::PatchbayView(AudioEngine& engine, QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new PatchbayScene(engine, this);
    setScene(m_scene);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag); // 空白处按住鼠标可平移画布
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setMinimumHeight(380);
    centerOn(0, -30);
}

} // namespace isac::ui
