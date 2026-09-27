#include "ModalTest.h"
#include "QtBridge.h"

#include "ui/BulkAction.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Result.h>
#include <VLMS/Core/Strings.h>

#include <QWidget>

#include <gtest/gtest.h>

using VLMS::ErrorKind;
using VLMS::Locale;
using VLMS::Status;
using VLMS::Strings;
using VLMS::qs;

namespace {

BulkActionTexts archiveBooks()
{
    BulkActionTexts texts;
    texts.title = qs(Strings::t("catalog.deleteBook"));
    texts.verb = qs(Strings::t("bulk.verb.archive"));
    texts.passive = qs(Strings::t("bulk.passive.archive"));
    texts.noun = qs(Strings::t("bulk.noun.books"));
    return texts;
}

BulkRow row(qint64 id, const QString& label)
{
    BulkRow bulk;
    bulk.id = id;
    bulk.row = static_cast<int>(id);
    bulk.label = label;
    return bulk;
}

}  // namespace

class test_ui_BulkAction : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }
    static void TearDownTestSuite() { Locale::setCode(Locale::kDefaultCode); }
};

TEST_F(test_ui_BulkAction, NoLeavesEveryRowAlone)
{
    int acted = 0;
    const ModalOutcome outcome = runAndAnswerModal(
        [&]() {
            const int done = runBulkAction(
                nullptr, archiveBooks(), {row(1, QStringLiteral("A")), row(2, QStringLiteral("B"))},
                [](const BulkRow&) { return Status::ok(); },
                [&](const BulkRow&) {
                    ++acted;
                    return Status::ok();
                });
            EXPECT_EQ(done, 0);
        },
        qs(Strings::t("common.no")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QStringLiteral("Archive 2 books?"));
    EXPECT_EQ(acted, 0);
}

TEST_F(test_ui_BulkAction, YesActsOnEveryPassingRow)
{
    QList<qint64> acted;
    const QList<ModalOutcome> outcomes = runAndAnswerModals(
        [&]() {
            const int done = runBulkAction(
                nullptr, archiveBooks(), {row(1, QStringLiteral("A")), row(2, QStringLiteral("B"))},
                [](const BulkRow&) { return Status::ok(); },
                [&](const BulkRow& bulk) {
                    acted.append(bulk.id);
                    return Status::ok();
                });
            EXPECT_EQ(done, 2);
        },
        {ModalAnswer{qs(Strings::t("common.yes")), std::nullopt, {}},
         ModalAnswer{qs(Strings::t("common.ok")), std::nullopt, {}}});

    ASSERT_TRUE(outcomes.at(0).appeared);
    EXPECT_FALSE(outcomes.at(1).appeared);
    EXPECT_EQ(acted, (QList<qint64>{1, 2}));
}

TEST_F(test_ui_BulkAction, ABlockedRowIsListedAndTheOthersStillRun)
{
    QList<qint64> acted;
    const ModalOutcome outcome = runAndAnswerModal(
        [&]() {
            const int done = runBulkAction(
                nullptr, archiveBooks(),
                {row(1, QStringLiteral("Kept")), row(2, QStringLiteral("Out")),
                 row(3, QStringLiteral("Also"))},
                [](const BulkRow& bulk) {
                    if (bulk.id == 2) {
                        return Status::fail(ErrorKind::Validation, "error.book.hasActiveLoans");
                    }
                    return Status::ok();
                },
                [&](const BulkRow& bulk) {
                    acted.append(bulk.id);
                    return Status::ok();
                });
            EXPECT_EQ(done, 2);
        },
        qs(Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_TRUE(outcome.text.contains(QStringLiteral("1 of 3 books can't be archived:")));
    EXPECT_TRUE(outcome.text.contains(QStringLiteral("Out — ")
                                      + qs(Strings::t("error.book.hasActiveLoans"))));
    EXPECT_TRUE(outcome.text.contains(QStringLiteral("Archive the other 2?")));
    EXPECT_EQ(acted, (QList<qint64>{1, 3}));
}

TEST_F(test_ui_BulkAction, AllBlockedStopsBeforeAnyAction)
{
    int acted = 0;
    const ModalOutcome outcome = runAndAnswerModal(
        [&]() {
            const int done = runBulkAction(
                nullptr, archiveBooks(),
                {row(1, QStringLiteral("A")), row(2, QStringLiteral("B"))},
                [](const BulkRow&) {
                    return Status::fail(ErrorKind::Validation, "error.book.hasActiveLoans");
                },
                [&](const BulkRow&) {
                    ++acted;
                    return Status::ok();
                });
            EXPECT_EQ(done, 0);
        },
        qs(Strings::t("common.ok")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_TRUE(outcome.text.contains(QStringLiteral("2 of 2 books can't be archived:")));
    EXPECT_FALSE(outcome.text.contains(QStringLiteral("the other")));
    EXPECT_EQ(acted, 0);
}

TEST_F(test_ui_BulkAction, ALongBlockedListStopsAfterTenLines)
{
    QList<BulkRow> rows;
    for (int i = 0; i < 12; ++i) {
        rows.append(row(i + 1, QStringLiteral("Blocked %1").arg(i)));
    }
    rows.append(row(100, QStringLiteral("Passes")));

    const ModalOutcome outcome = runAndAnswerModal(
        [&]() {
            runBulkAction(
                nullptr, archiveBooks(), rows,
                [](const BulkRow& bulk) {
                    if (bulk.id == 100) {
                        return Status::ok();
                    }
                    return Status::fail(ErrorKind::Validation, "error.book.hasCopies");
                },
                [](const BulkRow&) { return Status::ok(); });
        },
        qs(Strings::t("common.no")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_TRUE(outcome.text.contains(QStringLiteral("…and 2 more")));
    EXPECT_TRUE(outcome.text.contains(QStringLiteral("Blocked 0 —")));
    EXPECT_TRUE(outcome.text.contains(QStringLiteral("Blocked 9 —")));
    EXPECT_FALSE(outcome.text.contains(QStringLiteral("Blocked 10 —")));
}

TEST_F(test_ui_BulkAction, AFailureDuringTheRunIsSummarised)
{
    const QList<ModalOutcome> outcomes = runAndAnswerModals(
        [&]() {
            const int done = runBulkAction(
                nullptr, archiveBooks(), {row(1, QStringLiteral("A")), row(2, QStringLiteral("B"))},
                [](const BulkRow&) { return Status::ok(); },
                [](const BulkRow& bulk) {
                    if (bulk.id == 2) {
                        return Status::fail(ErrorKind::NotFound, "error.book.notFound");
                    }
                    return Status::ok();
                });
            EXPECT_EQ(done, 1);
        },
        {ModalAnswer{qs(Strings::t("common.yes")), std::nullopt, {}},
         ModalAnswer{qs(Strings::t("common.ok")), std::nullopt, {}}});

    ASSERT_TRUE(outcomes.at(0).appeared);
    ASSERT_TRUE(outcomes.at(1).appeared);
    EXPECT_EQ(outcomes.at(1).text,
              QStringLiteral("1 archived, 1 failed: ") + qs(Strings::t("error.book.notFound")));
}

TEST_F(test_ui_BulkAction, OneTickedRowIsNamedNotCounted)
{
    const ModalOutcome outcome = runAndAnswerModal(
        [&]() {
            runBulkAction(
                nullptr, archiveBooks(), {row(1, QStringLiteral("Dune"))},
                [](const BulkRow&) { return Status::ok(); },
                [](const BulkRow&) { return Status::ok(); });
        },
        qs(Strings::t("common.no")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QStringLiteral("Archive “Dune”?"));
}

TEST_F(test_ui_BulkAction, OneBlockedRowGivesItsReasonWithoutACount)
{
    const ModalOutcome outcome = runAndAnswerModal(
        [&]() {
            runBulkAction(
                nullptr, archiveBooks(), {row(1, QStringLiteral("Dune"))},
                [](const BulkRow&) {
                    return Status::fail(ErrorKind::Validation, "error.book.hasActiveLoans");
                },
                [](const BulkRow&) { return Status::ok(); });
        },
        qs(Strings::t("common.ok")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text,
              QStringLiteral("Dune — ") + qs(Strings::t("error.book.hasActiveLoans")));
}

TEST_F(test_ui_BulkAction, ArabicPurgeSaysItIsPermanent)
{
    // The Delete buttons archive and are labelled حذف; the Archive's purge has
    // to say it is final, or the two read the same.
    Locale::setCode("ar");
    BulkActionTexts texts;
    texts.title = qs(Strings::t("archive.purge"));
    texts.verb = qs(Strings::t("bulk.verb.purge"));
    texts.passive = qs(Strings::t("bulk.passive.purge"));
    texts.noun = qs(Strings::t("bulk.noun.books"));
    const ModalOutcome outcome = runAndAnswerModal(
        [&]() {
            runBulkAction(
                nullptr, texts, {row(1, QStringLiteral("A")), row(2, QStringLiteral("B"))},
                [](const BulkRow&) { return Status::ok(); },
                [](const BulkRow&) { return Status::ok(); });
        },
        qs(Strings::t("common.no")));
    Locale::setCode("en");

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QStringLiteral("الكتب (2): هل تريد الحذف النهائي؟"));
}
