#include "SmoothScrollFilter.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QGraphicsView>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace isac::ui {
namespace {

constexpr int kWheelPixelsPerNotch = 120;
constexpr double kPixelScrollSensitivity = 1.25;
constexpr int kScrollFrameIntervalMs = 8;
constexpr double kMaximumScrollVelocity = 12000.0;
constexpr double kScrollStopVelocity = 10.0;

bool isWheelValueControl(const QWidget* widget)
{
    return qobject_cast<const QAbstractSlider*>(widget) ||
           qobject_cast<const QAbstractSpinBox*>(widget) ||
           qobject_cast<const QComboBox*>(widget);
}

} // namespace

SmoothScrollFilter::SmoothScrollFilter(QObject* parent)
    : QObject(parent)
{
    m_scrollTimer.setInterval(kScrollFrameIntervalMs);
    m_scrollTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_scrollTimer, &QTimer::timeout,
            this, &SmoothScrollFilter::advanceScrolls);
}

void SmoothScrollFilter::setSmoothScrollingEnabled(bool enabled)
{
    m_smoothScrollingEnabled = enabled;
    if (enabled) {
        return;
    }

    m_scrollTimer.stop();
    m_scrollMotions.clear();
}

bool SmoothScrollFilter::smoothScrollingEnabled() const noexcept
{
    return m_smoothScrollingEnabled;
}

void SmoothScrollFilter::setSensitivity(double sensitivity) noexcept
{
    if (std::isfinite(sensitivity)) {
        m_sensitivity = std::clamp(sensitivity, 0.5, 10.0);
    }
}

double SmoothScrollFilter::sensitivity() const noexcept
{
    return m_sensitivity;
}

void SmoothScrollFilter::setAnimationDurationMs(int durationMs) noexcept
{
    m_animationDurationMs = std::clamp(durationMs, 60, 350);
}

int SmoothScrollFilter::animationDurationMs() const noexcept
{
    return m_animationDurationMs;
}

bool SmoothScrollFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Wheel) {
        return QObject::eventFilter(watched, event);
    }
    if (!m_smoothScrollingEnabled) {
        return false;
    }

    auto* wheelEvent = static_cast<QWheelEvent*>(event);
    if (wheelEvent->modifiers().testFlag(Qt::ControlModifier)) {
        return false;
    }

    auto* receiver = qobject_cast<QWidget*>(watched);
    if (!receiver) {
        return false;
    }

    QAbstractScrollArea* scrollArea = nullptr;
    for (QWidget* ancestor = receiver; ancestor;
         ancestor = ancestor->parentWidget()) {
        if (qobject_cast<QGraphicsView*>(ancestor)) {
            return false;
        }
        if (isWheelValueControl(ancestor) &&
            !qobject_cast<QScrollBar*>(ancestor)) {
            return false;
        }
        if (auto* candidate = qobject_cast<QAbstractScrollArea*>(ancestor)) {
            scrollArea = candidate;
            break;
        }
    }
    if (!scrollArea) {
        return false;
    }

    if (auto* itemView = qobject_cast<QAbstractItemView*>(scrollArea)) {
        itemView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        itemView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

    const QPoint pixelDelta = wheelEvent->pixelDelta();
    const QPoint angleDelta = wheelEvent->angleDelta();
    const bool usePixelDelta = !pixelDelta.isNull();
    const bool shiftScroll =
        wheelEvent->modifiers().testFlag(Qt::ShiftModifier);
    const QPoint delta = usePixelDelta ? pixelDelta : angleDelta;
    const bool horizontal = shiftScroll || delta.x() != 0;
    const int axisDelta = horizontal && delta.x() != 0
        ? delta.x()
        : delta.y();
    if (axisDelta == 0) {
        return false;
    }

    QScrollBar* scrollBar = horizontal
        ? scrollArea->horizontalScrollBar()
        : scrollArea->verticalScrollBar();
    if (!scrollBar || scrollBar->maximum() <= scrollBar->minimum()) {
        return false;
    }

    const double scrollPixels = usePixelDelta
        ? static_cast<double>(axisDelta) *
              kPixelScrollSensitivity * m_sensitivity
        : static_cast<double>(axisDelta) *
              kWheelPixelsPerNotch * m_sensitivity / 120.0;
    if (std::abs(scrollPixels) < 0.01) {
        return false;
    }

    const double displacement = -scrollPixels;
    auto motionIt = m_scrollMotions.find(scrollBar);
    if (motionIt == m_scrollMotions.end()) {
        if (qFuzzyIsNull(displacement) ||
            (displacement < 0.0 &&
             scrollBar->value() <= scrollBar->minimum()) ||
            (displacement > 0.0 &&
             scrollBar->value() >= scrollBar->maximum())) {
            return false;
        }
        motionIt = m_scrollMotions.insert(
            scrollBar, ScrollMotion{
                static_cast<double>(scrollBar->value()), 0.0,
                scrollBar->value()});
    } else if (motionIt->lastValue != scrollBar->value()) {
        motionIt->position = scrollBar->value();
        motionIt->velocity = 0.0;
        motionIt->lastValue = scrollBar->value();
    }

    const double timeConstantSeconds = std::max(
        0.012, static_cast<double>(m_animationDurationMs) / 3500.0);
    motionIt->velocity = std::clamp(
        motionIt->velocity + displacement / timeConstantSeconds,
        -kMaximumScrollVelocity,
        kMaximumScrollVelocity);
    if (!m_observedScrollBars.contains(scrollBar)) {
        m_observedScrollBars.insert(scrollBar);
        connect(scrollBar, &QObject::destroyed, this,
                [this, scrollBar]() {
            m_scrollMotions.remove(scrollBar);
            m_observedScrollBars.remove(scrollBar);
        });
    }
    if (!m_scrollTimer.isActive()) {
        m_frameClock.start();
        m_scrollTimer.start();
    }
    wheelEvent->accept();
    return true;
}

void SmoothScrollFilter::advanceScrolls()
{
    if (m_scrollMotions.isEmpty()) {
        m_scrollTimer.stop();
        return;
    }

    const double elapsedSeconds = std::clamp(
        static_cast<double>(m_frameClock.restart()) / 1000.0,
        0.001, 0.05);
    const double timeConstantSeconds = std::max(
        0.012, static_cast<double>(m_animationDurationMs) / 3500.0);
    const double velocityRetention = std::exp(
        -elapsedSeconds / timeConstantSeconds);
    const double frameFraction = 1.0 - velocityRetention;

    for (auto it = m_scrollMotions.begin(); it != m_scrollMotions.end();) {
        QScrollBar* scrollBar = it.key();
        if (!scrollBar) {
            it = m_scrollMotions.erase(it);
            continue;
        }

        ScrollMotion& motion = it.value();
        if (scrollBar->value() != motion.lastValue) {
            motion.position = scrollBar->value();
            motion.velocity = 0.0;
        }
        motion.position += motion.velocity *
                           timeConstantSeconds * frameFraction;
        motion.velocity *= velocityRetention;
        if ((motion.position < scrollBar->minimum() &&
             motion.velocity < 0.0) ||
            (motion.position > scrollBar->maximum() &&
             motion.velocity > 0.0)) {
            motion.velocity = 0.0;
        }
        motion.position = std::clamp(
            motion.position,
            static_cast<double>(scrollBar->minimum()),
            static_cast<double>(scrollBar->maximum()));
        scrollBar->setValue(static_cast<int>(std::lround(motion.position)));
        motion.lastValue = scrollBar->value();

        if (std::abs(motion.velocity) < kScrollStopVelocity) {
            it = m_scrollMotions.erase(it);
        } else {
            ++it;
        }
    }

    if (m_scrollMotions.isEmpty()) {
        m_scrollTimer.stop();
    }
}

} // namespace isac::ui
