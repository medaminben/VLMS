#include "ui/Theme.h"

#include <QPalette>
#include <QRegularExpression>
#include <QString>

#include <gtest/gtest.h>

using VLMS::applicationPalette;
using VLMS::applicationStylesheet;
using VLMS::Theme;
using VLMS::ThemeMode;

TEST(test_ui_ThemeModes, NoPlaceholderSurvivesSubstitution)
{
    for (const ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark}) {
        SCOPED_TRACE(static_cast<int>(mode));
        const QString sheet = applicationStylesheet(mode);
        EXPECT_FALSE(sheet.isEmpty());
        const qsizetype leftover = sheet.indexOf(QLatin1Char('@'));
        if (leftover >= 0) {
            FAIL() << "unsubstituted placeholder near: "
                   << sheet.mid(leftover, 40).toStdString();
        }
    }
}

TEST(test_ui_ThemeModes, ModesProduceDifferentSheets)
{
    const QString light = applicationStylesheet(ThemeMode::Light);
    const QString dark = applicationStylesheet(ThemeMode::Dark);

    EXPECT_NE(light, dark);
    static const QRegularExpression colour(QStringLiteral("#[0-9a-fA-F]{6}"));
    EXPECT_EQ(dark.count(colour), light.count(colour));
    EXPECT_TRUE(light.contains(QLatin1String("#f1f5f9")));
    EXPECT_FALSE(dark.contains(QLatin1String("#f1f5f9")));
}

TEST(test_ui_ThemeModes, DarkPaletteIsActuallyDark)
{
    const QPalette light = applicationPalette(ThemeMode::Light);
    const QPalette dark = applicationPalette(ThemeMode::Dark);

    for (const QPalette::ColorRole role :
         {QPalette::Base, QPalette::Window, QPalette::AlternateBase, QPalette::Button}) {
        EXPECT_LT(dark.color(role).lightness(), light.color(role).lightness())
            << "role " << int(role) << " is not darker in dark mode";
        EXPECT_LT(dark.color(role).lightness(), 96);
    }

    for (const QPalette::ColorRole role :
         {QPalette::Text, QPalette::WindowText, QPalette::ButtonText}) {
        EXPECT_GT(dark.color(role).lightness(), light.color(role).lightness());
    }
}

TEST(test_ui_ThemeModes, DisabledColoursAreSetInBothModes)
{
    for (const ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark}) {
        const QPalette palette = applicationPalette(mode);
        EXPECT_NE(palette.color(QPalette::Disabled, QPalette::Text),
                  palette.color(QPalette::Active, QPalette::Text));
        EXPECT_EQ(palette.color(QPalette::Disabled, QPalette::Base),
                  palette.color(QPalette::Disabled, QPalette::Button));
    }
}

TEST(test_ui_ThemeModes, ModeRoundTrips)
{
    const ThemeMode original = Theme::mode();

    Theme::setMode(ThemeMode::Dark);
    EXPECT_TRUE(Theme::isDark());
    EXPECT_EQ(Theme::mode(), ThemeMode::Dark);

    Theme::setMode(ThemeMode::Light);
    EXPECT_FALSE(Theme::isDark());
    EXPECT_EQ(Theme::mode(), ThemeMode::Light);

    Theme::setMode(original);
}

TEST(test_ui_ThemeModes, TheLanguageButtonsAreRoundAndMarkTheActiveOne)
{
    for (const ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark}) {
        SCOPED_TRACE(static_cast<int>(mode));
        const QString sheet = applicationStylesheet(mode);
        EXPECT_TRUE(sheet.contains(QLatin1String("QPushButton#languageButton")));
        EXPECT_TRUE(sheet.contains(QLatin1String("QPushButton#languageButton[active=\"true\"]")));
        // Round, and pinned to the diameter the radius assumes -- the same
        // treatment QPushButton#themeToggle gets right above it.
        EXPECT_TRUE(sheet.contains(QLatin1String("border-radius: 17px")));
    }
}
