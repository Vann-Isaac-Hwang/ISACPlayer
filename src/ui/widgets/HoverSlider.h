#pragma once

#include <QSlider>

class QPropertyAnimation;

namespace isac::ui {

class HoverSlider final : public QSlider {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    explicit HoverSlider(Qt::Orientation orientation,
                         QWidget* parent = nullptr);

    qreal hoverProgress() const noexcept { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

protected:
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void animateHoverTo(qreal progress);
    int handleLength() const noexcept;
    QRect visualHandleRect() const noexcept;
    void setSliderPositionFromPoint(const QPoint& point);

    qreal m_hoverProgress = 0.0;
    int m_normalHandleHeight = 12;
    int m_hoverHandleHeight = 20;
    bool m_dragging = false;
    int m_dragOffset = 0;
    QPropertyAnimation* m_hoverAnimation = nullptr;
};

} // namespace isac::ui
