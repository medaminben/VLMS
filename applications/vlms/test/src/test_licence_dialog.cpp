#include "UiTest.h"

#include "ui/LicenceDialog.h"
#include "ui/Theme.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QAbstractButton>
#include <QColor>
#include <QDialogButtonBox>
#include <QImage>
#include <QLayout>
#include <QPushButton>
#include <QRect>
#include <QTextBrowser>
#include <Qt>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace VLMS;

using VLMS::applicationStylesheet;
using VLMS::Locale;
using VLMS::Strings;
using VLMS::Theme;
using VLMS::ThemeMode;

namespace {

constexpr auto kArabic = "ar";
constexpr auto kFrench = "fr";
constexpr auto kEnglish = "en";

const std::vector<std::string>& clauseBodyKeys()
{
    static const std::vector<std::string> keys = {
        "licence.intro",
        "licence.clause.ownership.body",
        "licence.clause.data.body",
        "licence.clause.warranty.body",
        "licence.clause.liability.body",
        "licence.clause.support.body",
    };
    return keys;
}

}  // namespace

class test_ui_LicenceDialog : public ::testing::Test {
protected:
    void TearDown() override { Locale::setCode(Locale::kDefaultCode); }
};

TEST_F(test_ui_LicenceDialog, EveryClauseIsShownInTheChosenLanguage)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        const QString text = dialog.documentText();

        for (const std::string& key : clauseBodyKeys()) {
            SCOPED_TRACE(key);
            EXPECT_TRUE(text.contains(qs(Strings::rawValue(locale, key))));
        }
        EXPECT_TRUE(text.contains(qs(Strings::rawValue(locale, "licence.contact.author"))));
    }
}

TEST_F(test_ui_LicenceDialog, TheContactDetailsAreTheSameInEveryLanguage)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        const QString text = dialog.documentText();

        EXPECT_TRUE(text.contains(QStringLiteral("https://www.linkedin.com/in/mlbh/")));
        EXPECT_TRUE(text.contains(QStringLiteral("mohamed.ben-hassine@mailfence.com")));
    }
}

TEST_F(test_ui_LicenceDialog, TheCloseButtonComesFromTheApplicationTable)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        auto* box = dialog.findChild<QDialogButtonBox*>();
        ASSERT_NE(box, nullptr);
        ASSERT_NE(box->button(QDialogButtonBox::Close), nullptr);
        EXPECT_EQ(box->button(QDialogButtonBox::Close)->text(),
                  qs(Strings::rawValue(locale, "common.close")));
    }
}

TEST_F(test_ui_LicenceDialog, OnlyTheArabicLicenceReadsRightToLeft)
{
    Locale::setCode(kArabic);
    LicenceDialog arabic;
    auto* arabicBody = arabic.findChild<QTextBrowser*>();
    ASSERT_NE(arabicBody, nullptr);
    EXPECT_EQ(arabicBody->layoutDirection(), Qt::RightToLeft);

    for (const char* locale : {kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        auto* body = dialog.findChild<QTextBrowser*>();
        ASSERT_NE(body, nullptr);
        EXPECT_EQ(body->layoutDirection(), Qt::LeftToRight);
    }
}

// A licence is read, not edited, and a click on a line in it should not open
// a web browser on a library workstation.
TEST_F(test_ui_LicenceDialog, TheLicenceCanBeReadAndCopiedButNotChanged)
{
    LicenceDialog dialog;
    auto* body = dialog.findChild<QTextBrowser*>();
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->isReadOnly());
    EXPECT_FALSE(body->openExternalLinks());
    EXPECT_TRUE(body->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
}

TEST_F(test_ui_LicenceDialog, TheWindowIsTitledInTheChosenLanguage)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        EXPECT_EQ(dialog.windowTitle(), qs(Strings::rawValue(locale, "licence.title")));
    }
}

// The trap this guards is the one Theme.cpp already records twice: a stylesheet
// type selector matches subclasses, so the QLineEdit/QTextEdit rule paints a
// border, a radius and padding onto the QTextBrowser, and setFrameShape
// (NoFrame) does not override it. That cannot be seen from any property -- the
// widget reports NoFrame either way -- so this reads the pixels back, the way
// TheDropDownEntriesAreActuallyPaintedInTheirState does for the combo pop-up.
TEST_F(test_ui_LicenceDialog, TheLicenceIsNotPaintedAsAnInputField)
{
    struct Case {
        ThemeMode mode;
        const char* borderColour;
    };
    for (const Case& c : {Case{ThemeMode::Light, "#cbd5e1"}, Case{ThemeMode::Dark, "#334155"}}) {
        SCOPED_TRACE(c.borderColour);
        Theme::setMode(c.mode);

        LicenceDialog dialog;
        dialog.setStyleSheet(applicationStylesheet(c.mode));
        dialog.resize(560, 620);
        dialog.layout()->activate();

        auto* body = dialog.findChild<QTextBrowser*>();
        ASSERT_NE(body, nullptr);

        const QImage shot = dialog.grab().toImage();
        const QRect box = body->geometry();
        ASSERT_FALSE(box.isEmpty());

        // Every edge of where the box would be drawn.
        const QColor left = shot.pixelColor(box.left(), box.center().y());
        const QColor right = shot.pixelColor(box.right(), box.center().y());
        const QColor top = shot.pixelColor(box.center().x(), box.top());
        const QColor bottom = shot.pixelColor(box.center().x(), box.bottom());

        const QColor border(QLatin1String(c.borderColour));
        EXPECT_NE(left, border);
        EXPECT_NE(right, border);
        EXPECT_NE(top, border);
        EXPECT_NE(bottom, border);
    }
    Theme::setMode(Theme::kDefaultMode);
}
