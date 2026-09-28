#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/Theme.h"
#include "ui/UiHelpers.h"
#include "ui/members/BirthDateEdit.h"
#include "ui/members/MemberEditorDialog.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Core/Strings.h>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStyleFactory>
#include <QTimer>

#include <gtest/gtest.h>

#include <functional>
#include <utility>
#include <vector>

using VLMS::Date;
using VLMS::Locale;
using VLMS::ScopedClock;
using VLMS::Strings;
using namespace VLMS::Test;

namespace {

constexpr auto kArabic = "ar";
constexpr auto kFrench = "fr";
constexpr auto kEnglish = "en";

BirthDateEdit* birthDate(MemberEditorDialog& dialog)
{
    return dialog.findChild<BirthDateEdit*>();
}

MemberRecord validMember()
{
    MemberRecord member;
    member.id = 1;
    member.membershipNumber = "M-0001";
    member.firstName = "Amina";
    member.lastName = "Ben Salah";
    member.sex = "F";
    member.dateOfBirth = "1987-05-12";
    member.email = "amina@example.org";
    member.status = "active";
    return member;
}

}  // namespace

class test_ui_DialogTranslations : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository =
            std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        Locale::setCode(Locale::kDefaultCode);
        m_repository.reset();
        m_db.reset();
    }

    [[nodiscard]] MemberEditorDialog::ValidationFailure failureFor(const MemberRecord& member) const
    {
        MemberEditorDialog dialog(*m_repository, member);
        return dialog.firstValidationFailure();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MemberRepository> m_repository;
};

TEST_F(test_ui_DialogTranslations, ButtonBoxUsesTheApplicationTableInEveryLocale)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        MemberEditorDialog dialog(*m_repository);
        auto* box = dialog.findChild<QDialogButtonBox*>();
        ASSERT_NE(box, nullptr);
        EXPECT_EQ(box->button(QDialogButtonBox::Ok)->text(), qs(Strings::rawValue(locale, "common.ok")));
        EXPECT_EQ(box->button(QDialogButtonBox::Cancel)->text(),
                  qs(Strings::rawValue(locale, "common.cancel")));
    }
}

TEST_F(test_ui_DialogTranslations, MessageBoxButtonsUseTheApplicationTable)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        QMessageBox question(QMessageBox::Question, QStringLiteral("title"), QStringLiteral("text"),
                             QMessageBox::Yes | QMessageBox::No);
        VLMS::localizeMessageBox(&question);
        EXPECT_EQ(question.button(QMessageBox::Yes)->text(), qs(Strings::rawValue(locale, "common.yes")));
        EXPECT_EQ(question.button(QMessageBox::No)->text(), qs(Strings::rawValue(locale, "common.no")));

        QMessageBox warning(QMessageBox::Warning, QStringLiteral("title"), QStringLiteral("text"),
                            QMessageBox::Ok);
        VLMS::localizeMessageBox(&warning);
        EXPECT_EQ(warning.button(QMessageBox::Ok)->text(), qs(Strings::rawValue(locale, "common.ok")));
    }
}

TEST_F(test_ui_DialogTranslations, ArabicNeverShowsAnEnglishStandardButton)
{
    Locale::setCode(kArabic);
    QMessageBox box(QMessageBox::Question, QStringLiteral("title"), QStringLiteral("text"),
                    QMessageBox::Ok | QMessageBox::Cancel | QMessageBox::Yes | QMessageBox::No
                        | QMessageBox::Close);
    VLMS::localizeMessageBox(&box);

    const QStringList english{QStringLiteral("OK"), QStringLiteral("Cancel"), QStringLiteral("Yes"),
                              QStringLiteral("No"), QStringLiteral("Close")};
    const auto buttons = box.buttons();
    EXPECT_EQ(buttons.size(), 5);
    for (const QAbstractButton* button : buttons) {
        EXPECT_FALSE(english.contains(button->text()))
            << "untranslated standard button: " << button->text().toStdString();
    }
}

TEST_F(test_ui_DialogTranslations, StandardButtonLabelsSurviveALanguageChange)
{
    // Qt answers QEvent::LanguageChange by relabelling a button box's standard
    // buttons from its own catalogue: English here, Qt's "حسنًا" rather than
    // our "موافق" in the app. Qt sends one to the first message box a process
    // shows, so the first warning after start-up lost its label. Send it
    // explicitly, from inside the running box, so the order tests run in
    // does not decide whether this is covered.
    Locale::setCode(kArabic);
    // Not just "no English": Qt's own Arabic would pass that. Every label must
    // be the application's word, or the action button's own text ("a").
    const QStringList ours{qs(Strings::t("common.ok")), qs(Strings::t("common.cancel")),
                           qs(Strings::t("common.yes")), qs(Strings::t("common.no")),
                           qs(Strings::t("common.close")), qs(Strings::t("file.open")),
                           QStringLiteral("a")};

    const auto labelsAfterLanguageChange = [](const std::function<void()>& open) {
        QStringList labels;
        // Polled rather than one-shot: the box becomes the active modal only
        // once exec() has shown it, and a missed box would block forever.
        QTimer poll;
        poll.setInterval(10);
        QObject::connect(&poll, &QTimer::timeout, [&labels, &poll] {
            QWidget* modal = QApplication::activeModalWidget();
            if (modal == nullptr || !modal->isVisible()) {
                return;
            }
            poll.stop();
            QEvent languageChange(QEvent::LanguageChange);
            QApplication::sendEvent(modal, &languageChange);
            if (auto* buttons = modal->findChild<QDialogButtonBox*>()) {
                for (const QAbstractButton* button : buttons->buttons()) {
                    labels.append(button->text());
                }
            }
            if (auto* dialog = qobject_cast<QDialog*>(modal)) {
                dialog->done(0);
            }
        });
        poll.start();
        open();
        return labels;
    };

    bool checked = false;
    const std::vector<std::pair<const char*, std::function<void()>>> helpers = {
        {"showWarning", [] { VLMS::showWarning(nullptr, QStringLiteral("t"), QStringLiteral("x")); }},
        {"askYesNo", [] { (void)VLMS::askYesNo(nullptr, QStringLiteral("t"), QStringLiteral("x")); }},
        {"askYesNoWithCheckBox",
         [&checked] {
             (void)VLMS::askYesNoWithCheckBox(nullptr, QStringLiteral("t"), QStringLiteral("x"),
                                                   QStringLiteral("c"), &checked);
         }},
        {"showWarningWithAction",
         [] {
             (void)VLMS::showWarningWithAction(nullptr, QStringLiteral("t"), QStringLiteral("x"),
                                                    QStringLiteral("a"));
         }},
        {"member editor",
         [this] {
             MemberEditorDialog dialog(*m_repository);
             dialog.exec();
         }},
        {"input dialog",
         [] {
             QInputDialog dialog;
             dialog.setInputMode(QInputDialog::TextInput);
             VLMS::localizeInputDialog(&dialog);
             dialog.exec();
         }},
        {"image picker",
         [] { (void)VLMS::askForImageFile(nullptr, QStringLiteral("t"), QString()); }},
    };
    for (const auto& [name, open] : helpers) {
        SCOPED_TRACE(name);
        const QStringList labels = labelsAfterLanguageChange(open);
        ASSERT_FALSE(labels.isEmpty());
        for (const QString& label : labels) {
            EXPECT_TRUE(ours.contains(label)) << "button lost its label: " << label.toStdString();
        }
    }
}

TEST_F(test_ui_DialogTranslations, InputDialogButtonsAreReachedThroughItsHiddenButtonBox)
{
    Locale::setCode(kArabic);
    QInputDialog dialog;
    dialog.setInputMode(QInputDialog::TextInput);
    VLMS::localizeInputDialog(&dialog);

    auto* box = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->button(QDialogButtonBox::Ok)->text(), qs(Strings::rawValue(kArabic, "common.ok")));
    EXPECT_EQ(box->button(QDialogButtonBox::Cancel)->text(),
              qs(Strings::rawValue(kArabic, "common.cancel")));
}

TEST_F(test_ui_DialogTranslations, LocalizersTolerateANullBox)
{
    VLMS::localizeButtonBox(nullptr);
    VLMS::localizeMessageBox(nullptr);
}

TEST_F(test_ui_DialogTranslations, AValidMemberProducesNoFailure)
{
    const auto failure = failureFor(validMember());
    EXPECT_TRUE(failure.messageKey.isEmpty()) << failure.messageKey.toStdString();
    EXPECT_EQ(failure.field, nullptr);
}

TEST_F(test_ui_DialogTranslations, ANewMemberDateOfBirthIsUnsetAndRequired)
{
    MemberEditorDialog dialog(*m_repository);
    const QList<QLineEdit*> edits = dialog.findChildren<QLineEdit*>();
    ASSERT_GE(edits.size(), 2);
    edits.at(0)->setText(QStringLiteral("Amina"));
    edits.at(1)->setText(QStringLiteral("Ben Salah"));
    auto* birth = birthDate(dialog);
    ASSERT_NE(birth, nullptr);
    EXPECT_EQ(birth->dayCombo()->currentIndex(), -1);
    EXPECT_EQ(birth->monthCombo()->currentIndex(), -1);
    EXPECT_EQ(birth->yearCombo()->currentIndex(), -1);
    const auto failure = dialog.firstValidationFailure();
    EXPECT_EQ(failure.messageKey, QStringLiteral("member.dateOfBirthRequired"));
    EXPECT_EQ(failure.field, birth->dayCombo());
    EXPECT_TRUE(Strings::rawValue(kArabic, "member.field.dateOfBirthHint").empty());
    EXPECT_TRUE(Strings::rawValue(kFrench, "member.field.dateOfBirthHint").empty());
    EXPECT_TRUE(Strings::rawValue(kEnglish, "member.field.dateOfBirthHint").empty());
}

TEST_F(test_ui_DialogTranslations, ThirtyFirstOfFebruaryFocusesTheDay)
{
    MemberEditorDialog dialog(*m_repository);
    const QList<QLineEdit*> edits = dialog.findChildren<QLineEdit*>();
    ASSERT_GE(edits.size(), 2);
    edits.at(0)->setText(QStringLiteral("Amina"));
    edits.at(1)->setText(QStringLiteral("Ben Salah"));
    auto* birth = birthDate(dialog);
    ASSERT_NE(birth, nullptr);
    birth->dayCombo()->setCurrentText(QStringLiteral("31"));
    birth->monthCombo()->setCurrentText(QStringLiteral("02"));
    birth->yearCombo()->setCurrentText(QStringLiteral("1990"));
    const auto failure = dialog.firstValidationFailure();
    EXPECT_EQ(failure.messageKey, QStringLiteral("member.dateOfBirthInvalid"));
    EXPECT_EQ(failure.field, birth->dayCombo());
}

TEST_F(test_ui_DialogTranslations, AChosenBirthDateIsReturnedAsIso)
{
    MemberRecord member = validMember();
    member.dateOfBirth = "1990-05-12";
    MemberEditorDialog dialog(*m_repository, member);
    EXPECT_TRUE(dialog.firstValidationFailure().messageKey.isEmpty());
    EXPECT_EQ(dialog.memberInput().dateOfBirth, "1990-05-12");
}

TEST_F(test_ui_DialogTranslations, AnOutOfRangeStoredYearStaysSelected)
{
    MemberRecord member = validMember();
    member.dateOfBirth = "1890-05-12";
    MemberEditorDialog dialog(*m_repository, member);
    auto* birth = birthDate(dialog);
    ASSERT_NE(birth, nullptr);
    EXPECT_EQ(birth->yearCombo()->currentText(), QStringLiteral("1890"));
    EXPECT_EQ(birth->monthCombo()->currentText(), QStringLiteral("05"));
    EXPECT_EQ(birth->dayCombo()->currentText(), QStringLiteral("12"));
}

TEST_F(test_ui_DialogTranslations, ABareYearOfBirthIsRefusedBeforeTheDialogCloses)
{
    MemberRecord member = validMember();
    member.dateOfBirth = "1987";
    EXPECT_EQ(failureFor(member).messageKey, QStringLiteral("member.dateOfBirthRequired"));
}

TEST_F(test_ui_DialogTranslations, AnImpossibleDateOfBirthIsRefused)
{
    MemberRecord member = validMember();
    member.dateOfBirth = "1987-02-31";
    EXPECT_EQ(failureFor(member).messageKey, QStringLiteral("member.dateOfBirthInvalid"));
}

TEST_F(test_ui_DialogTranslations, ABirthDateAfterTodayIsRefused)
{
    const ScopedClock pinned(Date(2026, 9, 19));
    MemberRecord member = validMember();
    member.dateOfBirth = "2026-09-20";
    EXPECT_EQ(failureFor(member).messageKey, QStringLiteral("member.dateOfBirthInFuture"));

    member.dateOfBirth = "2026-09-19";
    EXPECT_TRUE(failureFor(member).messageKey.isEmpty());
}

TEST_F(test_ui_DialogTranslations, AnEmptyDateOfBirthIsRefused)
{
    MemberRecord member = validMember();
    member.dateOfBirth.clear();
    MemberEditorDialog dialog(*m_repository, member);
    auto* birth = birthDate(dialog);
    ASSERT_NE(birth, nullptr);
    const auto failure = dialog.firstValidationFailure();
    EXPECT_EQ(failure.messageKey, QStringLiteral("member.dateOfBirthRequired"));
    EXPECT_EQ(failure.field, birth->dayCombo());
}

TEST_F(test_ui_DialogTranslations, EditorHasNoAgeGroupCombo)
{
    MemberEditorDialog dialog(*m_repository);
    for (const QComboBox* combo : dialog.findChildren<QComboBox*>()) {
        EXPECT_EQ(combo->findData(QStringLiteral("youth")), -1)
            << combo->objectName().toStdString();
        EXPECT_EQ(combo->findData(QStringLiteral("adult")), -1)
            << combo->objectName().toStdString();
        EXPECT_FALSE(combo->objectName().contains(QStringLiteral("ageGroup"), Qt::CaseInsensitive));
    }
}

TEST_F(test_ui_DialogTranslations, ARefusedDateOfBirthPointsAtItsOwnField)
{
    MemberRecord member = validMember();
    member.dateOfBirth = "not a date";
    MemberEditorDialog dialog(*m_repository, member);
    auto* birth = birthDate(dialog);
    ASSERT_NE(birth, nullptr);
    EXPECT_EQ(birth->dayCombo()->currentIndex(), -1);
    EXPECT_EQ(birth->monthCombo()->currentIndex(), -1);
    EXPECT_EQ(birth->yearCombo()->currentIndex(), -1);
    const auto failure = dialog.firstValidationFailure();
    EXPECT_EQ(failure.messageKey, QStringLiteral("member.dateOfBirthRequired"));
    EXPECT_EQ(failure.field, birth->dayCombo());
}

TEST_F(test_ui_DialogTranslations, EveryFailureMessageIsTranslatedInEveryLocale)
{
    MemberRecord blankName = validMember();
    blankName.firstName.clear();
    MemberRecord emptyDate = validMember();
    emptyDate.dateOfBirth.clear();
    MemberRecord badDate = validMember();
    badDate.dateOfBirth = "1987";
    MemberRecord futureDate = validMember();
    futureDate.dateOfBirth = "2999-01-01";
    MemberRecord badEmail = validMember();
    badEmail.email = "not-an-address";
    for (const MemberRecord& member : {blankName, emptyDate, badDate, futureDate, badEmail}) {
        const QString key = failureFor(member).messageKey;
        ASSERT_FALSE(key.isEmpty());
        for (const char* locale : {kArabic, kFrench, kEnglish}) {
            const QString text = qs(Strings::rawValue(locale, key.toStdString()));
            EXPECT_FALSE(text.isEmpty() || text == key)
                << "no " << locale << " text for '" << key.toStdString() << "'";
        }
    }
}

TEST_F(test_ui_DialogTranslations, BirthDateBoxesFitBesideTheirLabelsInEveryLocale)
{
    // Under the application sheet each box used to be ~87px wide for "00" and
    // the dialog opened below its layout's minimum, so the row ran over the Sex
    // label (French, English), over its own label (Arabic), and lost its
    // bottom edge.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
    }
    VLMS::setupFonts(*qApp);
    qApp->setStyleSheet(VLMS::applicationStylesheet());
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        qApp->setLayoutDirection(QString::fromLatin1(locale) == QLatin1String(kArabic) ? Qt::RightToLeft
                                                                                        : Qt::LeftToRight);
        MemberEditorDialog dialog(*m_repository, validMember());
        dialog.show();
        QApplication::processEvents();

        EXPECT_GE(dialog.width(), dialog.layout()->minimumSize().width());
        EXPECT_GE(dialog.height(), dialog.layout()->minimumSize().height());
        auto* birth = birthDate(dialog);
        ASSERT_NE(birth, nullptr);
        const QRect row(birth->mapTo(&dialog, QPoint(0, 0)), birth->size());
        for (const QLabel* label : dialog.findChildren<QLabel*>()) {
            if (!label->isVisible() || label->text().isEmpty()) {
                continue;
            }
            const QRect box(label->mapTo(&dialog, QPoint(0, 0)), label->size());
            EXPECT_FALSE(row.intersects(box)) << "over label " << label->text().toStdString();
        }
        for (const QComboBox* box : {birth->dayCombo(), birth->monthCombo(), birth->yearCombo()}) {
            EXPECT_GE(birth->height(), box->y() + box->height());
            EXPECT_GE(box->height(), box->sizeHint().height());
            EXPECT_LT(box->width(), box->fontMetrics().horizontalAdvance(box->currentText()) + 60);
        }
    }
    qApp->setLayoutDirection(Qt::LeftToRight);
    qApp->setStyleSheet({});
}
