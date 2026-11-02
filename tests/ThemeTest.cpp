// Regression tests for the application theme system.
//
// Authored widget stylesheets use dark-theme hex colors and are remapped on
// every theme change. The tests pin down the invariants that keep a light/dark
// switch complete and repeatable:
//
//  * a stylesheet authored at any time (including after a switch) follows the
//    active theme, never a stale base captured earlier;
//  * switching themes repeatedly is stable;
//  * showing a widget again does not rewrite its stylesheet;
//  * stylesheets built from the current theme colors stay verbatim.

#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QSettings>
#include <QStyleHints>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

using isac::ui::ThemeColors;
using isac::ui::ThemeManager;
using isac::ui::ThemeMode;

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Dark-only authored values of the kind used throughout the widget sources.
// #4A4F5D and #FFFFFF are remapped to different colors in each theme, which
// makes it possible to tell which palette a stylesheet was produced with.
QString authoredStyleSheet()
{
    return QStringLiteral(
        "QLabel { color: #FFFFFF; background: #4A4F5D; "
        "border: 1px solid #343846; }");
}

bool usesColor(const QString& styleSheet, const QColor& color)
{
    return color.isValid() &&
           styleSheet.contains(color.name(), Qt::CaseInsensitive);
}

ThemeColors paletteCopy()
{
    return ThemeManager::paletteColors();
}

void testAuthoredStylesheetFollowsTheme(ThemeManager& theme)
{
    QLabel label;
    label.setStyleSheet(authoredStyleSheet());

    theme.setMode(ThemeMode::Dark);
    const ThemeColors dark = paletteCopy();
    expect(usesColor(label.styleSheet(), dark.textPrimary),
           "authored stylesheet uses the dark text color");
    expect(usesColor(label.styleSheet(), dark.borderStrong),
           "authored stylesheet uses the dark border color");

    theme.setMode(ThemeMode::Light);
    const ThemeColors light = paletteCopy();
    expect(light.borderStrong != dark.borderStrong,
           "the two themes must have different border colors");
    expect(usesColor(label.styleSheet(), light.borderStrong),
           "authored stylesheet uses the light border color");
    expect(!usesColor(label.styleSheet(), dark.borderStrong),
           "the dark border color must be gone after switching to light");
}

void testLateStylesheetReplacesStaleBase(ThemeManager& theme)
{
    // A page created while the light theme is active writes its stylesheet
    // after the initial theme application. Earlier versions kept the first
    // captured stylesheet as the base and lost this write on the next switch.
    theme.setMode(ThemeMode::Light);
    QLabel page;
    page.setStyleSheet(authoredStyleSheet());
    expect(usesColor(page.styleSheet(), paletteCopy().borderStrong),
           "late stylesheet is themed when it is assigned");

    theme.setMode(ThemeMode::Dark);
    const ThemeColors dark = paletteCopy();
    expect(usesColor(page.styleSheet(), dark.borderStrong),
           "late stylesheet follows a later theme change");

    // A second runtime stylesheet must replace the previous source as well.
    // Rewriting an existing stylesheet sends no style change, so the next
    // theme application has to adopt the new text.
    page.setStyleSheet(QStringLiteral("QLabel { background: #565A64; }"));
    theme.setMode(ThemeMode::Light);
    const ThemeColors light = paletteCopy();
    expect(usesColor(page.styleSheet(), light.controlPressed),
           "a replaced stylesheet becomes the new source");
    expect(!usesColor(page.styleSheet(), dark.borderStrong),
           "the superseded source must not leak into the new theme");
}

void testRepeatedSwitchingIsStable(ThemeManager& theme)
{
    QLabel label;
    label.setStyleSheet(authoredStyleSheet());

    theme.setMode(ThemeMode::Light);
    const QString firstLight = label.styleSheet();
    theme.setMode(ThemeMode::Dark);
    const QString firstDark = label.styleSheet();
    theme.setMode(ThemeMode::Light);
    expect(label.styleSheet() == firstLight,
           "returning to a theme reproduces the same stylesheet");
    theme.setMode(ThemeMode::Dark);
    expect(label.styleSheet() == firstDark,
           "repeated switching must not accumulate remapping");

    theme.applyToExistingWidgets();
    expect(label.styleSheet() == firstDark,
           "re-applying to existing widgets is idempotent");
}

void testShowingAWidgetKeepsItsStylesheet(ThemeManager& theme)
{
    QLabel label;
    theme.setMode(ThemeMode::Dark);
    label.setStyleSheet(authoredStyleSheet());
    theme.setMode(ThemeMode::Light);
    const QString beforeShow = label.styleSheet();

    label.show();
    QApplication::processEvents();
    expect(label.styleSheet() == beforeShow,
           "showing a widget must not rewrite its stylesheet");

    theme.setMode(ThemeMode::Dark);
    label.hide();
    label.show();
    QApplication::processEvents();
    expect(usesColor(label.styleSheet(), paletteCopy().borderStrong),
           "re-showing a themed widget keeps the active theme");
}

void testThemeBuiltStylesheetIsNotRemapped(ThemeManager& theme)
{
    // Stylesheets built from colors() already contain the active theme colors
    // and must be applied verbatim; remapping them would corrupt the colors.
    QLabel derived;
    const QString derivedStyle =
        QStringLiteral("QLabel { background-color: #f0f1f4; }");

    theme.setMode(ThemeMode::Light);
    theme.applyFinalStyleSheet(&derived, derivedStyle);
    expect(derived.styleSheet() == derivedStyle,
           "final stylesheet is applied verbatim");

    theme.setMode(ThemeMode::Dark);
    expect(derived.styleSheet() == derivedStyle,
           "a final stylesheet survives a theme change unchanged");

    // The owner rewrites it for the new theme, which must be honoured.
    const QString darkDerived =
        QStringLiteral("QLabel { background-color: #252834; }");
    theme.applyFinalStyleSheet(&derived, darkDerived);
    expect(derived.styleSheet() == darkDerived,
           "the owner can replace a final stylesheet");

    // An authored stylesheet takes over from a final one. The rewrite is
    // adopted by the next theme application.
    derived.setStyleSheet(authoredStyleSheet());
    theme.applyToExistingWidgets();
    expect(usesColor(derived.styleSheet(), paletteCopy().borderStrong),
           "an authored write after a final stylesheet is themed again");
    theme.setMode(ThemeMode::Light);
    expect(usesColor(derived.styleSheet(), paletteCopy().borderStrong),
           "that authored stylesheet keeps following the theme");
}

void testSystemModeFollowsPlatformColorScheme(ThemeManager& theme)
{
    // "Follow System" must resolve like the desktop does. Windows reports the
    // dark app mode through the style hints while keeping the classic light
    // palette, so a palette-only check picked the wrong theme there.
    theme.setMode(ThemeMode::System);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme =
        QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Unknown) {
        return;
    }
    const bool expectDark = scheme == Qt::ColorScheme::Dark;
    expect(theme.isDark() == expectDark,
           "Follow System matches the platform color scheme");
    expect((theme.colors().window.lightness() < 128) == expectDark,
           "Follow System selects the matching palette");
#endif
}

void testWidgetTreeFollowsTheme(ThemeManager& theme)
{
    // Mirrors the application layering: a surface whose background is built
    // from theme colors, authored children, and a themeChanged handler that
    // rewrites the surface. This is the combination that a light/dark switch
    // has to keep consistent.
    QWidget page;
    QLabel title(&page);
    title.setStyleSheet(authoredStyleSheet());

    const auto applySurface = [&theme, &page]() {
        theme.applyFinalStyleSheet(
            &page,
            QStringLiteral("QWidget#page { background-color: %1; }")
                .arg(theme.colors().window.name()));
    };
    applySurface();
    QObject::connect(&theme, &ThemeManager::themeChanged, &page,
                     applySurface);

    theme.setMode(ThemeMode::Dark);
    const ThemeColors dark = paletteCopy();
    expect(usesColor(page.styleSheet(), dark.window),
           "theme-built surface follows the dark theme");
    expect(usesColor(title.styleSheet(), dark.borderStrong),
           "authored child follows the dark theme");

    theme.setMode(ThemeMode::Light);
    const ThemeColors light = paletteCopy();
    expect(usesColor(page.styleSheet(), light.window),
           "theme-built surface follows the light theme");
    expect(usesColor(title.styleSheet(), light.borderStrong),
           "authored child follows the light theme");

    // A widget created while a theme is active starts in that theme, as a
    // menu or dialog opened later must.
    QLabel late(&page);
    late.setStyleSheet(authoredStyleSheet());
    page.show();
    QApplication::processEvents();
    expect(usesColor(late.styleSheet(), light.borderStrong),
           "a widget created after a switch uses the active theme");
    page.hide();
}

} // namespace

int main(int argc, char** argv)
{
    // Keep the test out of the real application settings.
    QTemporaryDir settingsDirectory;
    if (!settingsDirectory.isValid()) {
        std::cerr << "FAIL: temporary settings directory is invalid\n";
        return EXIT_FAILURE;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(
        QStringLiteral("ISACPlayerThemeTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("ISACPlayerThemeTest"));

    ThemeManager theme;
    testAuthoredStylesheetFollowsTheme(theme);
    testLateStylesheetReplacesStaleBase(theme);
    testRepeatedSwitchingIsStable(theme);
    testShowingAWidgetKeepsItsStylesheet(theme);
    testThemeBuiltStylesheetIsNotRemapped(theme);
    testSystemModeFollowsPlatformColorScheme(theme);
    testWidgetTreeFollowsTheme(theme);

    if (failures != 0) {
        std::cerr << failures << " theme test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All theme tests passed.\n";
    return EXIT_SUCCESS;
}
