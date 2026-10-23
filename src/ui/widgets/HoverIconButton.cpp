#include "HoverIconButton.h"

#include <QEvent>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QtGlobal>

#include <algorithm>

namespace isac::ui {

HoverIconButton::HoverIconButton(QWidget* parent)
    : QPushButton(parent)
    , m_iconAnimation(new QPropertyAnimation(this, "iconSize", this))
{
    m_iconAnimation->setDuration(160);
    m_iconAnimation->setEasingCurve(QEasingCurve::OutCubic);
    setIconSize(m_normalIconSize);
}

void HoverIconButton::setNormalIconSize(const QSize& size)
{
    if (size.isEmpty()) {
        return;
    }
    m_normalIconSize = size;
    if (!underMouse()) {
        setIconSize(m_normalIconSize);
    }
}

void HoverIconButton::setHoverScale(qreal scale)
{
    m_hoverScale = std::max<qreal>(1.0, scale);
    if (underMouse()) {
        animateIconTo(hoverIconSize());
    }
}

bool HoverIconButton::event(QEvent* event)
{
    if (event->type() == QEvent::Enter) {
        animateIconTo(hoverIconSize());
    } else if (event->type() == QEvent::Leave) {
        animateIconTo(m_normalIconSize);
    }
    return QPushButton::event(event);
}

void HoverIconButton::animateIconTo(const QSize& size)
{
    m_iconAnimation->stop();
    m_iconAnimation->setStartValue(iconSize());
    m_iconAnimation->setEndValue(size);
    m_iconAnimation->start();
}

QSize HoverIconButton::hoverIconSize() const
{
    return {
        qRound(m_normalIconSize.width() * m_hoverScale),
        qRound(m_normalIconSize.height() * m_hoverScale)
    };
}

} // namespace isac::ui
