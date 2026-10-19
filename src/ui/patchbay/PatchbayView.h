#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <unordered_map>
#include "engine/graph/AudioEngine.h"

namespace isac::ui {

class VisualNodeItem;
class VisualCableItem;
class PatchbayScene;

// 1. 针脚图元 (Port/Pin)：挂载在节点左右两侧的小圆点
class VisualPortItem : public QGraphicsEllipseItem {
public:
    VisualPortItem(VisualNodeItem* parentNode, bool isOutput, PortIndex index, const QString& name);

    VisualNodeItem* nodeItem() const { return m_nodeItem; }
    bool isOutput() const { return m_isOutput; }
    PortIndex portIndex() const { return m_portIndex; }
    NodeId nodeId() const;
    QPointF centerScenePos() const { return mapToScene(rect().center()); }

    void addCable(VisualCableItem* cable) { m_cables.push_back(cable); }
    void removeCable(VisualCableItem* cable);
    void updateCables();

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    VisualNodeItem* m_nodeItem;
    bool m_isOutput;
    PortIndex m_portIndex;
    std::vector<VisualCableItem*> m_cables;
};

// 2. 贝塞尔音频连线图元 (Cable)
class VisualCableItem : public QGraphicsPathItem {
public:
    VisualCableItem(VisualPortItem* startPort, VisualPortItem* endPort = nullptr);
    ~VisualCableItem() override;

    void updatePath(const QPointF& tempEndPos = QPointF());
    VisualPortItem* startPort() const { return m_startPort; }
    VisualPortItem* endPort() const { return m_endPort; }
    void setEndPort(VisualPortItem* port);

private:
    VisualPortItem* m_startPort = nullptr;
    VisualPortItem* m_endPort = nullptr;
};

// 3. 音频处理器节点方块 (Node Box)
class VisualNodeItem : public QGraphicsObject {
    Q_OBJECT
public:
    VisualNodeItem(std::shared_ptr<IAudioNode> audioNode);

    std::shared_ptr<IAudioNode> audioNode() const { return m_audioNode; }
    VisualPortItem* inputPort(PortIndex idx) const { return m_inputs.at(idx); }
    VisualPortItem* outputPort(PortIndex idx) const { return m_outputs.at(idx); }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    std::shared_ptr<IAudioNode> m_audioNode;
    qreal m_width = 175.0;
    qreal m_height = 80.0;
    std::vector<VisualPortItem*> m_inputs;
    std::vector<VisualPortItem*> m_outputs;
};

// 4. 拓扑交互场景 (处理鼠标拉线、吸附、右键拆线并同步给 AudioEngine)
class PatchbayScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit PatchbayScene(AudioEngine& engine, QObject* parent = nullptr);
    void syncFromEngine();

signals:
    void topologyChanged(const QString& message);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    VisualPortItem* portAt(const QPointF& pos) const;

    AudioEngine& m_engine;
    std::unordered_map<NodeId, VisualNodeItem*> m_nodeItems;
    std::vector<VisualCableItem*> m_cableItems;

    // 正在拖拽中的临时连线
    VisualCableItem* m_tempCable = nullptr;
};

// 5. 外部可直接嵌入 UI 的视图控件
class PatchbayView : public QGraphicsView {
    Q_OBJECT
public:
    explicit PatchbayView(AudioEngine& engine, QWidget* parent = nullptr);
    PatchbayScene* patchbayScene() const { return m_scene; }

private:
    PatchbayScene* m_scene;
};

} // namespace isac::ui
