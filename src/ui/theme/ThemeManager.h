#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

class QWidget;

namespace isac::ui {

enum class ThemeMode {
    System = 0,
    Light = 1,
    Dark = 2
};

struct ThemeColors {
    QColor window;
    QColor surface;
    QColor surfaceAlt;
    QColor surfaceElevated;
    QColor control;
    QColor controlHover;
    QColor controlPressed;
    QColor border;
    QColor borderStrong;
    QColor divider;
    QColor text;
    QColor textPrimary;
    QColor textSecondary;
    QColor textMuted;
    QColor accent;
    QColor accentHover;
    QColor selection;
    QColor sliderGroove;
    QColor sliderHandle;
    QColor canvas;
    QColor canvasGrid;
    QColor nodeBody;
    QColor nodeBorder;
    QColor nodeSelected;
    QColor sourceNode;
    QColor sinkNode;
    QColor portInput;
    QColor portOutput;
    QColor cable;
    QColor cablePending;
    QColor visualizationBackground;
    QColor visualizationLow;
    QColor visualizationHigh;
    QColor visualizationBorder;
};

class ThemeManager final : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override;

    static ThemeManager* instance() noexcept;
    static const ThemeColors& paletteColors() noexcept;

    ThemeMode mode() const noexcept { return m_mode; }
    bool isDark() const noexcept { return m_isDark; }
    const ThemeColors& colors() const noexcept { return m_colors; }

    void setMode(ThemeMode mode);
    void applyToExistingWidgets();

    // Applies a stylesheet that already contains the current theme colors
    // (for example one built from colors()) and remembers it verbatim, so the
    // authored-hex remapping never rewrites it. The owner is expected to
    // re-apply it from its themeChanged handler.
    void applyFinalStyleSheet(QWidget* widget, const QString& styleSheet);

    static QString modeKey(ThemeMode mode);
    static ThemeMode modeFromKey(const QString& key);

signals:
    void themeChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static ThemeColors darkColors();
    static ThemeColors lightColors();
    static bool detectDarkPalette(const QPalette& palette);
    static QPalette buildPalette(const ThemeColors& colors);
    static QString globalStyleSheet(const ThemeColors& colors);
    static QString themedStyleSheet(const QString& base,
                                    const ThemeColors& colors);

    bool systemPrefersDark() const;
    void apply();
    void applyWindowFrames();
    void applyWidgetStyleSheet(QWidget* widget);
    void writeWidgetStyleSheet(QWidget* widget, const QString& styleSheet);
    void handleSystemPaletteChanged(const QPalette& palette);
    void handleSystemColorSchemeChanged();

    static ThemeManager* s_instance;

    QPalette m_systemPalette;
    QPalette m_appliedPalette;
    ThemeColors m_colors;
    ThemeMode m_mode = ThemeMode::System;
    bool m_isDark = true;
    bool m_applying = false;
};

} // namespace isac::ui
