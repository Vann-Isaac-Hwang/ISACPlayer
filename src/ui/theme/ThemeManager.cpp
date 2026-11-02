#include "ThemeManager.h"

#include <QApplication>
#include <QEvent>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>
#include <QWidget>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <dwmapi.h>
#endif

#include <algorithm>

namespace isac::ui {
namespace {

constexpr auto kThemeSetting = "appearance/theme";
// Authored stylesheet text. It stays the single source of truth for a widget
// and is remapped for every theme change.
constexpr auto kBaseStyleSheetProperty = "isacBaseStyleSheet";
// Stylesheet that already contains theme colors; applied verbatim.
constexpr auto kFinalStyleSheetProperty = "isacFinalStyleSheet";
// Last text this manager wrote, used to tell its own writes apart from
// stylesheet changes made by application code.
constexpr auto kAppliedStyleSheetProperty = "isacAppliedStyleSheet";

#if defined(Q_OS_WIN)
// Documented value on Windows 10 20H1 and later; older builds used 19.
constexpr DWORD kImmersiveDarkModeAttribute = 20;
constexpr DWORD kImmersiveDarkModeAttributeLegacy = 19;

// Paints the native caption of a top-level window for the active theme. Qt
// leaves the frame alone when the application palette changes, so without this
// the title bar keeps its previous color until the window is resized.
void applyNativeWindowFrame(QWidget* widget, bool dark)
{
    if (!widget || !widget->isWindow() || !widget->windowHandle()) {
        return;
    }
    const HWND handle = reinterpret_cast<HWND>(widget->winId());
    if (!handle) {
        return;
    }

    const BOOL enabled = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(handle, kImmersiveDarkModeAttribute,
                                     &enabled, sizeof(enabled)))) {
        DwmSetWindowAttribute(handle, kImmersiveDarkModeAttributeLegacy,
                              &enabled, sizeof(enabled));
    }

    // The non-client area only repaints when the frame is refreshed.
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}
#else
void applyNativeWindowFrame(QWidget*, bool)
{
}
#endif

QString colorName(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

} // namespace

ThemeManager* ThemeManager::s_instance = nullptr;

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
    , m_colors(darkColors())
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    auto* application = qobject_cast<QApplication*>(
        QCoreApplication::instance());
    if (!application) {
        return;
    }

    m_systemPalette = application->palette();
    m_mode = modeFromKey(
        QSettings().value(QString::fromLatin1(kThemeSetting), "system")
            .toString());

    application->installEventFilter(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(application->styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) {
                handleSystemColorSchemeChanged();
            });
#endif
    apply();
}

ThemeManager::~ThemeManager()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ThemeManager* ThemeManager::instance() noexcept
{
    return s_instance;
}

const ThemeColors& ThemeManager::paletteColors() noexcept
{
    static const ThemeColors fallback = darkColors();
    return s_instance ? s_instance->m_colors : fallback;
}

QString ThemeManager::modeKey(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light:
        return QStringLiteral("light");
    case ThemeMode::Dark:
        return QStringLiteral("dark");
    case ThemeMode::System:
    default:
        return QStringLiteral("system");
    }
}

ThemeMode ThemeManager::modeFromKey(const QString& key)
{
    if (key.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::Light;
    }
    if (key.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::Dark;
    }
    return ThemeMode::System;
}

void ThemeManager::setMode(ThemeMode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kThemeSetting), modeKey(mode));
    apply();
    emit themeChanged();
}

void ThemeManager::apply()
{
    const bool dark = m_mode == ThemeMode::Dark ||
                      (m_mode == ThemeMode::System && systemPrefersDark());
    m_isDark = dark;
    m_colors = dark ? darkColors() : lightColors();

    auto* application = qobject_cast<QApplication*>(
        QCoreApplication::instance());
    if (!application) {
        return;
    }

    m_applying = true;
    application->setPalette(buildPalette(m_colors));
    application->setStyleSheet(globalStyleSheet(m_colors));
    m_appliedPalette = application->palette();
    m_applying = false;
    applyToExistingWidgets();
    applyWindowFrames();
}

void ThemeManager::applyWindowFrames()
{
    auto* application = qobject_cast<QApplication*>(
        QCoreApplication::instance());
    if (!application) {
        return;
    }

    for (QWidget* widget : application->topLevelWidgets()) {
        applyNativeWindowFrame(widget, m_isDark);
    }
}

void ThemeManager::applyToExistingWidgets()
{
    auto* application = qobject_cast<QApplication*>(
        QCoreApplication::instance());
    if (!application) {
        return;
    }

    for (QWidget* widget : application->allWidgets()) {
        applyWidgetStyleSheet(widget);
    }
}

bool ThemeManager::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ApplicationPaletteChange) {
        if (auto* application = qobject_cast<QApplication*>(
                QCoreApplication::instance())) {
            handleSystemPaletteChanged(application->palette());
        }
    } else if (event->type() == QEvent::StyleChange && !m_applying) {
        // A first stylesheet assignment replaces the widget style and arrives
        // as a style change. Adopt it as the authored source right away so a
        // freshly created widget (menu, dialog, plugin UI) never appears with
        // the unresolved dark colors.
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            applyWidgetStyleSheet(widget);
        }
    } else if (event->type() == QEvent::Show && !m_applying) {
        // Later stylesheet rewrites do not send a style change, so widgets are
        // also re-synchronised when they become visible. Top-level windows
        // additionally get their native caption repainted here, because they
        // are only guaranteed to have a window handle at that point.
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            applyWidgetStyleSheet(widget);
            applyNativeWindowFrame(widget, m_isDark);
        }
    }
    return QObject::eventFilter(watched, event);
}

void ThemeManager::applyWidgetStyleSheet(QWidget* widget)
{
    if (!widget) {
        return;
    }

    const QString current = widget->styleSheet();
    const QVariant applied = widget->property(kAppliedStyleSheetProperty);
    if (applied.isValid() && applied.toString() == current) {
        // The current text is this manager's output. A final stylesheet
        // already contains theme colors and is rewritten by its owner, so it
        // must not be remapped here; an authored one is remapped below.
        if (widget->property(kFinalStyleSheetProperty).isValid()) {
            return;
        }
    } else {
        // Application code replaced the stylesheet since the last write. The
        // text on the widget - not the previously cached base - is the source
        // that has to follow the theme from now on.
        widget->setProperty(kFinalStyleSheetProperty, QVariant());
        if (current.isEmpty()) {
            widget->setProperty(kBaseStyleSheetProperty, QVariant());
            widget->setProperty(kAppliedStyleSheetProperty, QString());
            return;
        }
        widget->setProperty(kBaseStyleSheetProperty, current);
    }

    const QVariant base = widget->property(kBaseStyleSheetProperty);
    if (!base.isValid()) {
        if (current.isEmpty()) {
            return;
        }
        widget->setProperty(kBaseStyleSheetProperty, current);
    }

    writeWidgetStyleSheet(
        widget,
        themedStyleSheet(
            widget->property(kBaseStyleSheetProperty).toString(), m_colors));
}

void ThemeManager::writeWidgetStyleSheet(QWidget* widget,
                                         const QString& styleSheet)
{
    if (!widget) {
        return;
    }

    // Record the text before writing it so the resulting StyleChange event is
    // recognisable as this manager's own output; that comparison is what stops
    // the nested event from being treated as an application write.
    widget->setProperty(kAppliedStyleSheetProperty, styleSheet);
    if (widget->styleSheet() != styleSheet) {
        widget->setStyleSheet(styleSheet);
    }
}

void ThemeManager::applyFinalStyleSheet(QWidget* widget,
                                        const QString& styleSheet)
{
    if (!widget) {
        return;
    }

    widget->setProperty(kBaseStyleSheetProperty, QVariant());
    widget->setProperty(kFinalStyleSheetProperty, styleSheet);
    writeWidgetStyleSheet(widget, styleSheet);
}

void ThemeManager::handleSystemPaletteChanged(const QPalette& palette)
{
    // The application palette is replaced by this manager, so a palette change
    // carrying our own palette is not a desktop theme change. Treating it as
    // one re-applied the theme on unrelated events such as restoring a
    // minimised window.
    if (m_applying || palette == m_appliedPalette ||
        palette == m_systemPalette) {
        return;
    }

    m_systemPalette = palette;
    if (m_mode == ThemeMode::System) {
        apply();
        emit themeChanged();
    }
}

bool ThemeManager::systemPrefersDark() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // "Follow System" has to ask the platform, not inspect the palette:
    // Windows reports the dark app mode only through the style hints and keeps
    // handing out the classic light palette, so palette detection alone picked
    // the light theme on a dark desktop.
    const Qt::ColorScheme scheme =
        QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown) {
        return scheme == Qt::ColorScheme::Dark;
    }
#endif
    return detectDarkPalette(m_systemPalette);
}

void ThemeManager::handleSystemColorSchemeChanged()
{
    if (m_applying || m_mode != ThemeMode::System) {
        return;
    }
    // Some desktops report the colour scheme and the palette in the same
    // update; skip the second pass when the resolved theme did not change.
    if (m_isDark == systemPrefersDark()) {
        return;
    }
    apply();
    emit themeChanged();
}

bool ThemeManager::detectDarkPalette(const QPalette& palette)
{
    const QColor window = palette.color(QPalette::Window);
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);
    const int backgroundLightness =
        (window.lightness() + base.lightness()) / 2;

    // KDE and Windows both expose their current desktop scheme through the
    // application palette. The text contrast check handles custom palettes
    // whose window color is close to the midpoint.
    return backgroundLightness < 160 ||
           text.lightness() > backgroundLightness + 60;
}

QPalette ThemeManager::buildPalette(const ThemeColors& colors)
{
    QPalette palette;
    palette.setColor(QPalette::Window, colors.window);
    palette.setColor(QPalette::Base, colors.surface);
    palette.setColor(QPalette::AlternateBase, colors.surfaceAlt);
    palette.setColor(QPalette::Button, colors.control);
    palette.setColor(QPalette::Mid, colors.border);
    palette.setColor(QPalette::Dark, colors.borderStrong);
    palette.setColor(QPalette::Light, colors.controlHover);
    palette.setColor(QPalette::Text, colors.text);
    palette.setColor(QPalette::WindowText, colors.text);
    palette.setColor(QPalette::ButtonText, colors.text);
    palette.setColor(QPalette::BrightText, colors.textPrimary);
    palette.setColor(QPalette::Highlight, colors.selection);
    palette.setColor(QPalette::HighlightedText, colors.textPrimary);
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    palette.setColor(QPalette::PlaceholderText, colors.textMuted);
#endif
    return palette;
}

QString ThemeManager::globalStyleSheet(const ThemeColors& colors)
{
    return QStringLiteral(
        "QWidget { color: %1; background: %2; }"
        "QMainWindow, QDialog { background: %2; }"
        "QMenuBar { background: %3; color: %1; }"
        "QMenuBar::item:selected { background: %4; }"
        "QMenu { background: %5; color: %1; border: 1px solid %7; }"
        "QMenu::item:selected { background: %6; color: %8; }"
        "QToolTip { background: %5; color: %8; border: 1px solid %7; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {"
        " background: %5; color: %8; border: 1px solid %7; "
        " selection-background-color: %6; }"
        "QComboBox QAbstractItemView { background: %5; color: %8; "
        " selection-background-color: %6; }"
        "QPushButton, QToolButton { background: %9; color: %8; "
        " border: 1px solid %7; }"
        "QPushButton:hover, QToolButton:hover { background: %4; }"
        "QPushButton:pressed, QToolButton:pressed { background: %10; }"
        "QAbstractItemView { alternate-background-color: %11; }"
        "QScrollBar:vertical, QScrollBar:horizontal { background: %2; }"
        "QSplitter::handle { background: %12; }")
        .arg(colorName(colors.text),
             colorName(colors.window),
             colorName(colors.surface),
             colorName(colors.controlHover),
             colorName(colors.surfaceElevated),
             colorName(colors.selection),
             colorName(colors.borderStrong),
             colorName(colors.textPrimary),
             colorName(colors.control),
             colorName(colors.controlPressed),
             colorName(colors.surfaceAlt),
             colorName(colors.border));
}

QString ThemeManager::themedStyleSheet(const QString& base,
                                        const ThemeColors& colors)
{
    const std::pair<QString, QColor> replacements[] = {
        {QStringLiteral("#14161C"), colors.window},
        {QStringLiteral("#181A20"), colors.surface},
        {QStringLiteral("#191B22"), colors.surface},
        {QStringLiteral("#1A1C22"), colors.surfaceAlt},
        {QStringLiteral("#1E2028"), colors.surfaceAlt},
        {QStringLiteral("#1E2129"), colors.divider},
        {QStringLiteral("#20232B"), colors.surfaceElevated},
        {QStringLiteral("#252834"), colors.control},
        {QStringLiteral("#252934"), colors.control},
        {QStringLiteral("#292C34"), colors.control},
        {QStringLiteral("#2A2E39"), colors.divider},
        {QStringLiteral("#2B2F39"), colors.controlHover},
        {QStringLiteral("#2B2F3C"), colors.border},
        {QStringLiteral("#262934"), colors.border},
        {QStringLiteral("#30333D"), colors.divider},
        {QStringLiteral("#343740"), colors.controlHover},
        {QStringLiteral("#343846"), colors.border},
        {QStringLiteral("#343A4B"), colors.controlHover},
        {QStringLiteral("#393C45"), colors.controlPressed},
        {QStringLiteral("#42454E"), colors.sliderGroove},
        {QStringLiteral("#454952"), colors.controlPressed},
        {QStringLiteral("#454A56"), colors.borderStrong},
        {QStringLiteral("#4A4F5D"), colors.borderStrong},
        {QStringLiteral("#565A64"), colors.controlPressed},
        {QStringLiteral("#626875"), colors.borderStrong},
        {QStringLiteral("#666B78"), colors.textMuted},
        {QStringLiteral("#858992"), colors.textMuted},
        {QStringLiteral("#858B98"), colors.textMuted},
        {QStringLiteral("#8A8F9E"), colors.textMuted},
        {QStringLiteral("#9499A6"), colors.textSecondary},
        {QStringLiteral("#A5A8B0"), colors.textSecondary},
        {QStringLiteral("#A5AAB8"), colors.textSecondary},
        {QStringLiteral("#C9CDD8"), colors.text},
        {QStringLiteral("#D0D4E0"), colors.text},
        {QStringLiteral("#E0E4F0"), colors.text},
        {QStringLiteral("#E5E7EF"), colors.text},
        {QStringLiteral("#E6E8ED"), colors.text},
        {QStringLiteral("#EAEAEA"), colors.textPrimary},
        {QStringLiteral("#EAECEF"), colors.textPrimary},
        {QStringLiteral("#F0F1F4"), colors.textPrimary},
        {QStringLiteral("#F2F3F5"), colors.textPrimary},
        {QStringLiteral("#F5F6F8"), colors.textPrimary},
        {QStringLiteral("#FA2D48"), colors.accent},
        {QStringLiteral("#FF5268"), colors.accentHover},
        {QStringLiteral("#FFFFFF"), colors.textPrimary},
        {QStringLiteral("white"), colors.textPrimary}
    };

    static const QRegularExpression colorTokenPattern(
        QStringLiteral("#[0-9A-Fa-f]{6}|\\bwhite\\b"),
        QRegularExpression::CaseInsensitiveOption);
    QString styleSheet;
    styleSheet.reserve(base.size());
    qsizetype copiedThrough = 0;
    auto matches = colorTokenPattern.globalMatch(base);
    while (matches.hasNext()) {
        const auto match = matches.next();
        styleSheet.append(base.mid(copiedThrough,
                                   match.capturedStart() - copiedThrough));

        const QString token = match.captured();
        const auto replacement = std::find_if(
            std::begin(replacements), std::end(replacements),
            [&token](const auto& entry) {
                return entry.first.compare(token, Qt::CaseInsensitive) == 0;
            });
        styleSheet.append(replacement == std::end(replacements)
                              ? token
                              : colorName(replacement->second));
        copiedThrough = match.capturedEnd();
    }
    styleSheet.append(base.mid(copiedThrough));
    return styleSheet;
}

ThemeColors ThemeManager::darkColors()
{
    return {
        QColor("#14161C"), QColor("#191B22"), QColor("#1E2028"),
        QColor("#20232B"), QColor("#252834"), QColor("#343A4B"),
        QColor("#393C45"), QColor("#30333D"), QColor("#454A56"),
        QColor("#2A2E39"), QColor("#D0D4E0"), QColor("#F5F6F8"),
        QColor("#A5AAB8"), QColor("#858B98"), QColor("#FA2D48"),
        QColor("#FF5268"), QColor("#FA2D48"), QColor("#42454E"),
        QColor("#E6E8ED"), QColor("#181A20"), QColor("#2E323C"),
        QColor("#262930"), QColor("#464B59"), QColor("#78C3FF"),
        QColor("#2D8C5F"), QColor("#A54150"), QColor("#46A0F5"),
        QColor("#F08C32"), QColor("#5FB9FF"), QColor("#FFBE5A"),
        QColor("#16181D"), QColor("#323640"), QColor("#E5E7EF"),
        QColor("#494C54")
    };
}

ThemeColors ThemeManager::lightColors()
{
    return {
        QColor("#F4F5F7"), QColor("#FFFFFF"), QColor("#ECEEF2"),
        QColor("#E4E7EC"), QColor("#F0F1F4"), QColor("#DDE1E8"),
        QColor("#CBD1DB"), QColor("#D3D7E0"), QColor("#B5BDC9"),
        QColor("#D9DDE5"), QColor("#4A5260"), QColor("#20242C"),
        QColor("#687180"), QColor("#858D9A"), QColor("#C52F4B"),
        QColor("#A91F3B"), QColor("#C52F4B"), QColor("#B8BFCC"),
        QColor("#FFFFFF"), QColor("#EEF0F3"), QColor("#D8DDE5"),
        QColor("#FFFFFF"), QColor("#AAB3C0"), QColor("#2D6FA8"),
        QColor("#3C8A5F"), QColor("#B64B59"), QColor("#367DB5"),
        QColor("#C6782A"), QColor("#347EAF"), QColor("#BD762D"),
        QColor("#F0F2F5"), QColor("#D9DEE6"), QColor("#3C4654"),
        QColor("#AAB2BE")
    };
}

} // namespace isac::ui
