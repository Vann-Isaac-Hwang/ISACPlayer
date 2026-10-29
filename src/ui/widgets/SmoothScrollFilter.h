#pragma once

#include <QHash>
#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QTimer>

class QScrollBar;

namespace isac::ui {

class SmoothScrollFilter final : public QObject {
public:
    explicit SmoothScrollFilter(QObject* parent = nullptr);

    void setSmoothScrollingEnabled(bool enabled);
    bool smoothScrollingEnabled() const noexcept;
    void setSensitivity(double sensitivity) noexcept;
    double sensitivity() const noexcept;
    void setAnimationDurationMs(int durationMs) noexcept;
    int animationDurationMs() const noexcept;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct ScrollMotion {
        double position = 0.0;
        double velocity = 0.0;
        int lastValue = 0;
    };

    void advanceScrolls();

    QHash<QScrollBar*, ScrollMotion> m_scrollMotions;
    QSet<QScrollBar*> m_observedScrollBars;
    QElapsedTimer m_frameClock;
    QTimer m_scrollTimer;
    bool m_smoothScrollingEnabled = true;
    double m_sensitivity = 1.0;
    int m_animationDurationMs = 210;
};

} // namespace isac::ui
