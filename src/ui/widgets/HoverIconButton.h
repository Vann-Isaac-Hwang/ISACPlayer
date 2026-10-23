#pragma once

#include <QPushButton>
#include <QSize>

class QPropertyAnimation;

namespace isac::ui {

class HoverIconButton final : public QPushButton {
public:
    explicit HoverIconButton(QWidget* parent = nullptr);

    void setNormalIconSize(const QSize& size);
    QSize normalIconSize() const noexcept { return m_normalIconSize; }

    void setHoverScale(qreal scale);
    qreal hoverScale() const noexcept { return m_hoverScale; }

protected:
    bool event(QEvent* event) override;

private:
    void animateIconTo(const QSize& size);
    QSize hoverIconSize() const;

    QSize m_normalIconSize{31, 31};
    qreal m_hoverScale = 1.2;
    QPropertyAnimation* m_iconAnimation = nullptr;
};

} // namespace isac::ui
