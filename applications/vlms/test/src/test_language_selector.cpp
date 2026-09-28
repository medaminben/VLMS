#include "ui/LanguageSelector.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QPushButton>
#include <QSignalSpy>

#include <gtest/gtest.h>

using namespace VLMS;

using VLMS::LanguageSelector;
using VLMS::Locale;

namespace {

QPushButton* buttonFor(LanguageSelector& selector, const QString& code)
{
    for (QPushButton* button : selector.findChildren<QPushButton*>()) {
        if (button->property("langCode").toString() == code) {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

TEST(test_ui_LanguageSelector, ItOffersTheThreeUiLanguages)
{
    // Iterates the widget's own buttons rather than a hardcoded list of
    // codes, so a language added to languageCodes() with no painted flag
    // (a blank button) fails this test instead of sailing past it.
    LanguageSelector selector;
    const QList<QPushButton*> buttons = selector.findChildren<QPushButton*>();
    ASSERT_EQ(buttons.size(), 3);
    for (QPushButton* button : buttons) {
        SCOPED_TRACE(button->property("langCode").toString().toStdString());
        EXPECT_TRUE(button->isCheckable());
        EXPECT_FALSE(button->property("langCode").toString().isEmpty());
        EXPECT_FALSE(button->icon().isNull());
    }
}

TEST(test_ui_LanguageSelector, OnlyOneLanguageIsEverPicked)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("ar"));

    buttonFor(selector, QStringLiteral("fr"))->click();

    EXPECT_EQ(selector.currentLanguage(), QStringLiteral("fr"));
    EXPECT_TRUE(buttonFor(selector, QStringLiteral("fr"))->isChecked());
    EXPECT_FALSE(buttonFor(selector, QStringLiteral("ar"))->isChecked());
    EXPECT_FALSE(buttonFor(selector, QStringLiteral("en"))->isChecked());
}

TEST(test_ui_LanguageSelector, PickingALanguageAnnouncesItsCode)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("ar"));
    QSignalSpy spy(&selector, &LanguageSelector::languageSelected);

    buttonFor(selector, QStringLiteral("en"))->click();

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("en"));
}

TEST(test_ui_LanguageSelector, ClickingTheLanguageAlreadyInUseChangesNothing)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("fr"));
    QSignalSpy spy(&selector, &LanguageSelector::languageSelected);

    buttonFor(selector, QStringLiteral("fr"))->click();

    EXPECT_EQ(spy.count(), 0);
    // And it cannot be toggled off: the row always has a language.
    EXPECT_TRUE(buttonFor(selector, QStringLiteral("fr"))->isChecked());
    EXPECT_EQ(selector.currentLanguage(), QStringLiteral("fr"));
}

TEST(test_ui_LanguageSelector, SettingTheLanguageDoesNotAnnounceIt)
{
    // The window sets the selector from the locale on every retranslate; that
    // must not loop back into a locale change.
    LanguageSelector selector;
    QSignalSpy spy(&selector, &LanguageSelector::languageSelected);

    selector.setCurrentLanguage(QStringLiteral("en"));

    EXPECT_EQ(spy.count(), 0);
    EXPECT_EQ(selector.currentLanguage(), QStringLiteral("en"));
    EXPECT_TRUE(buttonFor(selector, QStringLiteral("en"))->isChecked());
    // Proves the previously selected button was actually released, not just
    // that the new one is checked -- the group would silently ignore an
    // errant setChecked(false) here too.
    EXPECT_FALSE(buttonFor(selector, QStringLiteral("ar"))->isChecked());
}

TEST(test_ui_LanguageSelector, AnUnknownCodeLeavesTheSelectionAlone)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("fr"));

    selector.setCurrentLanguage(QStringLiteral("de"));

    EXPECT_EQ(selector.currentLanguage(), QStringLiteral("fr"));
}

TEST(test_ui_LanguageSelector, ThePickedFlagIsMarkedForTheStylesheet)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("ar"));

    EXPECT_TRUE(buttonFor(selector, QStringLiteral("ar"))->property("active").toBool());
    EXPECT_FALSE(buttonFor(selector, QStringLiteral("fr"))->property("active").toBool());
    // The picked flag is drawn larger than the two it sits beside.
    EXPECT_GT(buttonFor(selector, QStringLiteral("ar"))->iconSize().width(),
              buttonFor(selector, QStringLiteral("fr"))->iconSize().width());
}

TEST(test_ui_LanguageSelector, TooltipsFollowTheUiLanguage)
{
    const std::string restore = Locale::code();
    LanguageSelector selector;

    Locale::setCode("en");
    selector.retranslateUi();
    EXPECT_EQ(buttonFor(selector, QStringLiteral("fr"))->toolTip(), QStringLiteral("Français"));

    Locale::setCode("ar");
    selector.retranslateUi();
    EXPECT_EQ(buttonFor(selector, QStringLiteral("ar"))->toolTip(),
              QString::fromUtf8("العربية"));
    // A screen reader gets nothing but this.
    EXPECT_FALSE(buttonFor(selector, QStringLiteral("en"))->accessibleName().isEmpty());

    Locale::setCode(restore);
}
