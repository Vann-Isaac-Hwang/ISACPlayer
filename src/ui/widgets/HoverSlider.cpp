#include "HoverSlider.h"
#include "ui/theme/ThemeManager.h"

#include <QEvent>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

#include <algorithm>

namespace isac::ui {
namespace {

QPoint mousePoint(const QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

} // namespace

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

void HoverSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QSlider::mousePressEvent(event);
        return;
    }

    const QPoint point = mousePoint(event);
    const QRect handle = visualHandleRect();
    const int coordinate = orientation() == Qt::Vertical
        ? point.y()
        : point.x();
    const int handleStart = orientation() == Qt::Vertical
        ? handle.top()
        : handle.left();

    m_dragging = true;
    m_dragOffset = handle.contains(point)
        ? coordinate - handleStart
        : handleLength() / 2;
    grabMouse();
    setSliderPositionFromPoint(point);
    event->accept();
}

void HoverSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        QSlider::mouseMoveEvent(event);
        return;
    }

    setSliderPositionFromPoint(mousePoint(event));
    event->accept();
}

void HoverSlider::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        setSliderPositionFromPoint(mousePoint(event));
        m_dragging = false;
        releaseMouse();
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

int HoverSlider::handleLength() const noexcept
{
    const int normalLength = m_normalHandleHeight;
    const int hoverLength = m_hoverHandleHeight;
    return qRound(normalLength + (hoverLength - normalLength) *
                                      m_hoverProgress);
}

QRect HoverSlider::visualHandleRect() const noexcept
{
    const int length = handleLength();
    const int range = maximum() - minimum();
    const qreal normalizedValue = range > 0
        ? static_cast<qreal>(sliderPosition() - minimum()) / range
        : 0.0;

    if (orientation() == Qt::Vertical) {
        const int available = std::max(0, height() - length);
        const int y = qRound(
            (invertedAppearance() ? normalizedValue : 1.0 - normalizedValue) *
            available);
        return QRect(0, y, width(), length);
    }

    const int available = std::max(0, width() - length);
    const int x = qRound(
        (invertedAppearance() ? 1.0 - normalizedValue : normalizedValue) *
        available);
    return QRect(x, 0, length, height());
}

void HoverSlider::setSliderPositionFromPoint(const QPoint& point)
{
    const int length = handleLength();
    const int available = orientation() == Qt::Vertical
        ? std::max(0, height() - length)
        : std::max(0, width() - length);
    if (available <= 0 || maximum() <= minimum()) {
        setSliderPosition(minimum());
        return;
    }

    const int coordinate = orientation() == Qt::Vertical
        ? point.y()
        : point.x();
    const int start = std::clamp(coordinate - m_dragOffset, 0, available);
    const qreal normalized = static_cast<qreal>(start) / available;
    const qreal valueProgress = orientation() == Qt::Vertical
        ? (invertedAppearance() ? normalized : 1.0 - normalized)
        : (invertedAppearance() ? 1.0 - normalized : normalized);
    const int value = minimum() + qRound(
        valueProgress * (maximum() - minimum()));
    setSliderPosition(value);
}

void HoverSlider::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setPen(Qt::NoPen);

    const int grooveWidth = std::min(24, width());
    const int grooveX = (width() - grooveWidth) / 2;
    painter.setBrush(ThemeManager::paletteColors().sliderGroove);
    painter.drawRect(grooveX, 0, grooveWidth, height());

    const int handleHeight = handleLength();
    const int availableHeight = std::max(0, height() - handleHeight);
    const int range = maximum() - minimum();
    const qreal normalizedValue = range > 0
        ? static_cast<qreal>(value() - minimum()) / range
        : 0.0;
    const bool inverted = invertedAppearance();
    const int handleY = qRound(
        (inverted ? normalizedValue : 1.0 - normalizedValue) *
        availableHeight);

    painter.setBrush(ThemeManager::paletteColors().sliderHandle);
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
