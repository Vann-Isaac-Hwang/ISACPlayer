#include "HoverSlider.h"

#include <QEvent>
#include <QEasingCurve>
#include <QPainter>
#include <QPropertyAnimation>

#include <algorithm>

namespace isac::ui {

HoverSlider::HoverSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
    , m_hoverAnimation(new QPropertyAnimation(this, "hoverProgress", this))
{
    setMouseTracking(true);
    m_hoverAnimation->setDuration(160);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void HoverSlider::setHoverProgress(qreal progress)
{
    m_hoverProgress = std::clamp<qreal>(progress, 0.0, 1.0);
    update();
}

bool HoverSlider::event(QEvent* event)
{
    if (event->type() == QEvent::Enter) {
        animateHoverTo(1.0);
    } else if (event->type() == QEvent::Leave) {
        animateHoverTo(0.0);
    }
    return QSlider::event(event);
}

void HoverSlider::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setPen(Qt::NoPen);

    const int grooveWidth = std::min(24, width());
    const int grooveX = (width() - grooveWidth) / 2;
    painter.setBrush(QColor(66, 69, 78));
    painter.drawRect(grooveX, 0, grooveWidth, height());

    const int handleHeight = qRound(
        m_normalHandleHeight +
        (m_hoverHandleHeight - m_normalHandleHeight) * m_hoverProgress);
    const int availableHeight = std::max(0, height() - handleHeight);
    const int range = maximum() - minimum();
    const qreal normalizedValue = range > 0
        ? static_cast<qreal>(value() - minimum()) / range
        : 0.0;
    const bool inverted = invertedAppearance();
    const int handleY = qRound(
        (inverted ? normalizedValue : 1.0 - normalizedValue) *
        availableHeight);

    painter.setBrush(QColor(230, 232, 237));
    painter.drawRect(0, handleY, width(), handleHeight);
}

void HoverSlider::animateHoverTo(qreal progress)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(progress);
    m_hoverAnimation->start();
}

} // namespace isac::ui
